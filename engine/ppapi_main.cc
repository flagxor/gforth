#include <ppapi/cpp/module.h>
#include <ppapi/cpp/instance.h>
#include <stdio.h>
#include "nacl-mounts/base/UrlLoaderJob.h"
#include "nacl-mounts/console/JSPipeMount.h"
#include "nacl-mounts/console/JSPostMessageBridge.h"
#include "nacl-mounts/pepper/PepperMount.h"


//#define USE_PSEUDO_THREADS


extern "C" int main(int argc, char *argv[]);
extern "C" int umount(const char *path);
extern "C" int mount(const char *source, const char *target,
    const char *filesystemtype, unsigned long mountflags, const void *data);
extern "C" int simple_tar_extract(const char *path);


class GforthInstance : public pp::Instance {
 public:
  explicit GforthInstance(PP_Instance instance) : pp::Instance(instance) {
    jspipe_ = NULL;
    jsbridge_ = NULL;
  }

  virtual ~GforthInstance() {
    if (jspipe_) delete jspipe_;
    if (jsbridge_) delete jsbridge_;
    if (runner_) delete runner_;
  }

  virtual bool Init(uint32_t argc, const char* argn[], const char* argv[]) {
    fs_ = new pp::FileSystem(this, PP_FILESYSTEMTYPE_LOCALPERSISTENT);
    runner_ = new MainThreadRunner(this);
    jsbridge_ = new JSPostMessageBridge(runner_);
    jspipe_ = new JSPipeMount();
#ifdef USE_PSEUDO_THREADS
    jspipe_->set_using_pseudo_thread(true);
#endif
    jspipe_->set_outbound_bridge(jsbridge_);
    // Replace stdin, stdout, stderr with js console.
    mount(0, "/jspipe", 0, 0, jspipe_);
    close(0);
    close(1);
    close(2);
    int fd;
    fd = open("/jspipe/0", O_RDONLY);
    assert(fd == 0);
    fd = open("/jspipe/1", O_WRONLY);
    assert(fd == 1);
    fd = open("/jspipe/2", O_WRONLY);
    assert(fd == 2);

#ifdef USE_PSEUDO_THREADS
    runner_->PseudoThreadFork(RunThread, this);
#else
    pthread_create(&gforth_thread_, NULL, RunThread, this);
#endif

    return true;
  }

  virtual void HandleMessage(const pp::Var& message) {
    std::string msg = message.AsString();
    jspipe_->Receive(msg.c_str(), msg.size());
  }

 private:
  pthread_t gforth_thread_;
  JSPipeMount* jspipe_;
  JSPostMessageBridge* jsbridge_;
  MainThreadRunner* runner_;
  pp::FileSystem *fs_;

  static void *RunThread(void *arg) {
    GforthInstance *inst = static_cast<GforthInstance*>(arg);
    inst->Run();
    return 0;
  }

  void Download(const char *url, const char *filename) {
    UrlLoaderJob* job = new UrlLoaderJob;
    job->set_url(url);
    std::vector<char> data;
    job->set_dst(&data);
    runner_->RunJob(job);
    int fh = open(filename, O_CREAT | O_WRONLY);
    write(fh, &data[0], data.size());
    close(fh);
  }

  void Run() {
    // Mount local storage.
    {
      PepperMount* pm = new PepperMount(runner_, fs_, 20 * 1024 * 1024);
      mount(0, "/save", 0, 0, pm);
    }

    chdir("/");
#if NACL_BITS == 32
    Download("gforth32.tar", "/gforth.tar");
#elif NACL_BITS == 64
    Download("gforth64.tar", "/gforth.tar");
#else
# error "Bad NACL_BITS value"
#endif
    simple_tar_extract("/gforth.tar");

    setenv("OUTSIDE_BROWSER", "1", 1);
    const char *argv[] = {"gforth"};
    main(1, const_cast<char**>(argv));
  }
};


class GforthModule : public pp::Module {
 public:
  virtual pp::Instance* CreateInstance(PP_Instance instance) {
    return new GforthInstance(instance);
  }
  virtual ~GforthModule() { }
};


namespace pp {
pp::Module *CreateModule(void) {
  return new GforthModule();
}
};
