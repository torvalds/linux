//===-- sanitizer_stoptheworld_mac.cc -------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// See sanitizer_stoptheworld.h for details.
//
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"

#if SANITIZER_MAC && (defined(__x86_64__) || defined(__aarch64__) || \
                      defined(__i386))

#include <mach/mach.h>
#include <mach/thread_info.h>
#include <pthread.h>

#include "sanitizer_stoptheworld.h"

namespace __sanitizer {
typedef struct {
  tid_t tid;
  thread_t thread;
} SuspendedThreadInfo;

class SuspendedThreadsListMac : public SuspendedThreadsList {
 public:
  SuspendedThreadsListMac() : threads_(1024) {}

  tid_t GetThreadID(uptr index) const;
  thread_t GetThread(uptr index) const;
  uptr ThreadCount() const;
  bool ContainsThread(thread_t thread) const;
  void Append(thread_t thread);

  PtraceRegistersStatus GetRegistersAndSP(uptr index, uptr *buffer,
                                          uptr *sp) const;
  uptr RegisterCount() const;

 private:
  InternalMmapVector<SuspendedThreadInfo> threads_;
};

struct RunThreadArgs {
  StopTheWorldCallback callback;
  void *argument;
};

void RunThread(void *arg) {
  struct RunThreadArgs *run_args = (struct RunThreadArgs *)arg;
  SuspendedThreadsListMac suspended_threads_list;

  thread_array_t threads;
  mach_msg_type_number_t num_threads;
  kern_return_t err = task_threads(mach_task_self(), &threads, &num_threads);
  if (err != KERN_SUCCESS) {
    VReport(1, "Failed to get threads for task (errno %d).\n", err);
    return;
  }

  thread_t thread_self = mach_thread_self();
  for (unsigned int i = 0; i < num_threads; ++i) {
    if (threads[i] == thread_self) continue;

    thread_suspend(threads[i]);
    suspended_threads_list.Append(threads[i]);
  }

  run_args->callback(suspended_threads_list, run_args->argument);

  uptr num_suspended = suspended_threads_list.ThreadCount();
  for (unsigned int i = 0; i < num_suspended; ++i) {
    thread_resume(suspended_threads_list.GetThread(i));
  }
}

void StopTheWorld(StopTheWorldCallback callback, void *argument) {
  struct RunThreadArgs arg = {callback, argument};
  pthread_t run_thread = (pthread_t)internal_start_thread(RunThread, &arg);
  internal_join_thread(run_thread);
}

#if defined(__x86_64__)
typedef x86_thread_state64_t regs_struct;

#define SP_REG __rsp

#elif defined(__aarch64__)
typedef arm_thread_state64_t regs_struct;

# if __DARWIN_UNIX03
#  define SP_REG __sp
# else
#  define SP_REG sp
# endif

#elif defined(__i386)
typedef x86_thread_state32_t regs_struct;

#define SP_REG __esp

#else
#error "Unsupported architecture"
#endif

tid_t SuspendedThreadsListMac::GetThreadID(uptr index) const {
  CHECK_LT(index, threads_.size());
  return threads_[index].tid;
}

thread_t SuspendedThreadsListMac::GetThread(uptr index) const {
  CHECK_LT(index, threads_.size());
  return threads_[index].thread;
}

uptr SuspendedThreadsListMac::ThreadCount() const {
  return threads_.size();
}

bool SuspendedThreadsListMac::ContainsThread(thread_t thread) const {
  for (uptr i = 0; i < threads_.size(); i++) {
    if (threads_[i].thread == thread) return true;
  }
  return false;
}

void SuspendedThreadsListMac::Append(thread_t thread) {
  thread_identifier_info_data_t info;
  mach_msg_type_number_t info_count = THREAD_IDENTIFIER_INFO_COUNT;
  kern_return_t err = thread_info(thread, THREAD_IDENTIFIER_INFO,
                                  (thread_info_t)&info, &info_count);
  if (err != KERN_SUCCESS) {
    VReport(1, "Error - unable to get thread ident for a thread\n");
    return;
  }
  threads_.push_back({info.thread_id, thread});
}

PtraceRegistersStatus SuspendedThreadsListMac::GetRegistersAndSP(
    uptr index, uptr *buffer, uptr *sp) const {
  thread_t thread = GetThread(index);
  regs_struct regs;
  int err;
  mach_msg_type_number_t reg_count = MACHINE_THREAD_STATE_COUNT;
  err = thread_get_state(thread, MACHINE_THREAD_STATE, (thread_state_t)&regs,
                         &reg_count);
  if (err != KERN_SUCCESS) {
    VReport(1, "Error - unable to get registers for a thread\n");
    // KERN_INVALID_ARGUMENT indicates that either the flavor is invalid,
    // or the thread does not exist. The other possible error case,
    // MIG_ARRAY_TOO_LARGE, means that the state is too large, but it's
    // still safe to proceed.
    return err == KERN_INVALID_ARGUMENT ? REGISTERS_UNAVAILABLE_FATAL
                                        : REGISTERS_UNAVAILABLE;
  }

  internal_memcpy(buffer, &regs, sizeof(regs));
  *sp = regs.SP_REG;

  // On x86_64 and aarch64, we must account for the stack redzone, which is 128
  // bytes.
  if (SANITIZER_WORDSIZE == 64) *sp -= 128;

  return REGISTERS_AVAILABLE;
}

uptr SuspendedThreadsListMac::RegisterCount() const {
  return MACHINE_THREAD_STATE_COUNT;
}
} // namespace __sanitizer

#endif  // SANITIZER_MAC && (defined(__x86_64__) || defined(__aarch64__)) ||
        //                   defined(__i386))
