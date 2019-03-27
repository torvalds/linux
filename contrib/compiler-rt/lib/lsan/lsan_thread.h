//=-- lsan_thread.h -------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of LeakSanitizer.
// Thread registry for standalone LSan.
//
//===----------------------------------------------------------------------===//

#ifndef LSAN_THREAD_H
#define LSAN_THREAD_H

#include "sanitizer_common/sanitizer_thread_registry.h"

namespace __sanitizer {
struct DTLS;
}

namespace __lsan {

class ThreadContext : public ThreadContextBase {
 public:
  explicit ThreadContext(int tid);
  void OnStarted(void *arg) override;
  void OnFinished() override;
  uptr stack_begin() { return stack_begin_; }
  uptr stack_end() { return stack_end_; }
  uptr tls_begin() { return tls_begin_; }
  uptr tls_end() { return tls_end_; }
  uptr cache_begin() { return cache_begin_; }
  uptr cache_end() { return cache_end_; }
  DTLS *dtls() { return dtls_; }

 private:
  uptr stack_begin_, stack_end_,
       cache_begin_, cache_end_,
       tls_begin_, tls_end_;
  DTLS *dtls_;
};

void InitializeThreadRegistry();

void ThreadStart(u32 tid, tid_t os_id, bool workerthread = false);
void ThreadFinish();
u32 ThreadCreate(u32 tid, uptr uid, bool detached);
void ThreadJoin(u32 tid);
u32 ThreadTid(uptr uid);

u32 GetCurrentThread();
void SetCurrentThread(u32 tid);
ThreadContext *CurrentThreadContext();
void EnsureMainThreadIDIsCorrect();
}  // namespace __lsan

#endif  // LSAN_THREAD_H
