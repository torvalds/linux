
#include "msan.h"
#include "msan_thread.h"
#include "msan_interface_internal.h"

#include "sanitizer_common/sanitizer_tls_get_addr.h"

namespace __msan {

MsanThread *MsanThread::Create(thread_callback_t start_routine,
                               void *arg) {
  uptr PageSize = GetPageSizeCached();
  uptr size = RoundUpTo(sizeof(MsanThread), PageSize);
  MsanThread *thread = (MsanThread*)MmapOrDie(size, __func__);
  thread->start_routine_ = start_routine;
  thread->arg_ = arg;
  thread->destructor_iterations_ = GetPthreadDestructorIterations();

  return thread;
}

void MsanThread::SetThreadStackAndTls() {
  uptr tls_size = 0;
  uptr stack_size = 0;
  GetThreadStackAndTls(IsMainThread(), &stack_bottom_, &stack_size,
                       &tls_begin_, &tls_size);
  stack_top_ = stack_bottom_ + stack_size;
  tls_end_ = tls_begin_ + tls_size;

  int local;
  CHECK(AddrIsInStack((uptr)&local));
}

void MsanThread::ClearShadowForThreadStackAndTLS() {
  __msan_unpoison((void *)stack_bottom_, stack_top_ - stack_bottom_);
  if (tls_begin_ != tls_end_)
    __msan_unpoison((void *)tls_begin_, tls_end_ - tls_begin_);
  DTLS *dtls = DTLS_Get();
  CHECK_NE(dtls, 0);
  for (uptr i = 0; i < dtls->dtv_size; ++i)
    __msan_unpoison((void *)(dtls->dtv[i].beg), dtls->dtv[i].size);
}

void MsanThread::Init() {
  SetThreadStackAndTls();
  CHECK(MEM_IS_APP(stack_bottom_));
  CHECK(MEM_IS_APP(stack_top_ - 1));
  ClearShadowForThreadStackAndTLS();
}

void MsanThread::TSDDtor(void *tsd) {
  MsanThread *t = (MsanThread*)tsd;
  t->Destroy();
}

void MsanThread::Destroy() {
  malloc_storage().CommitBack();
  // We also clear the shadow on thread destruction because
  // some code may still be executing in later TSD destructors
  // and we don't want it to have any poisoned stack.
  ClearShadowForThreadStackAndTLS();
  uptr size = RoundUpTo(sizeof(MsanThread), GetPageSizeCached());
  UnmapOrDie(this, size);
  DTLS_Destroy();
}

thread_return_t MsanThread::ThreadStart() {
  Init();

  if (!start_routine_) {
    // start_routine_ == 0 if we're on the main thread or on one of the
    // OS X libdispatch worker threads. But nobody is supposed to call
    // ThreadStart() for the worker threads.
    return 0;
  }

  thread_return_t res = start_routine_(arg_);

  return res;
}

} // namespace __msan
