//===-- xray_profiling.cc ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of XRay, a dynamic runtime instrumentation system.
//
// This is the implementation of a profiling handler.
//
//===----------------------------------------------------------------------===//
#include <memory>
#include <time.h>

#include "sanitizer_common/sanitizer_atomic.h"
#include "sanitizer_common/sanitizer_flags.h"
#include "xray/xray_interface.h"
#include "xray/xray_log_interface.h"
#include "xray_buffer_queue.h"
#include "xray_flags.h"
#include "xray_profile_collector.h"
#include "xray_profiling_flags.h"
#include "xray_recursion_guard.h"
#include "xray_tsc.h"
#include "xray_utils.h"
#include <pthread.h>

namespace __xray {

namespace {

static atomic_sint32_t ProfilerLogFlushStatus = {
    XRayLogFlushStatus::XRAY_LOG_NOT_FLUSHING};

static atomic_sint32_t ProfilerLogStatus = {
    XRayLogInitStatus::XRAY_LOG_UNINITIALIZED};

static SpinMutex ProfilerOptionsMutex;

struct ProfilingData {
  atomic_uintptr_t Allocators;
  atomic_uintptr_t FCT;
};

static pthread_key_t ProfilingKey;

// We use a global buffer queue, which gets initialized once at initialisation
// time, and gets reset when profiling is "done".
static std::aligned_storage<sizeof(BufferQueue), alignof(BufferQueue)>::type
    BufferQueueStorage;
static BufferQueue *BQ = nullptr;

thread_local FunctionCallTrie::Allocators::Buffers ThreadBuffers;
thread_local std::aligned_storage<sizeof(FunctionCallTrie::Allocators),
                                  alignof(FunctionCallTrie::Allocators)>::type
    AllocatorsStorage;
thread_local std::aligned_storage<sizeof(FunctionCallTrie),
                                  alignof(FunctionCallTrie)>::type
    FunctionCallTrieStorage;
thread_local ProfilingData TLD{{0}, {0}};
thread_local atomic_uint8_t ReentranceGuard{0};

// We use a separate guard for ensuring that for this thread, if we're already
// cleaning up, that any signal handlers don't attempt to cleanup nor
// initialise.
thread_local atomic_uint8_t TLDInitGuard{0};

// We also use a separate latch to signal that the thread is exiting, and
// non-essential work should be ignored (things like recording events, etc.).
thread_local atomic_uint8_t ThreadExitingLatch{0};

static ProfilingData *getThreadLocalData() XRAY_NEVER_INSTRUMENT {
  thread_local auto ThreadOnce = []() XRAY_NEVER_INSTRUMENT {
    pthread_setspecific(ProfilingKey, &TLD);
    return false;
  }();
  (void)ThreadOnce;

  RecursionGuard TLDInit(TLDInitGuard);
  if (!TLDInit)
    return nullptr;

  if (atomic_load_relaxed(&ThreadExitingLatch))
    return nullptr;

  uptr Allocators = 0;
  if (atomic_compare_exchange_strong(&TLD.Allocators, &Allocators, 1,
                                     memory_order_acq_rel)) {
    bool Success = false;
    auto AllocatorsUndo = at_scope_exit([&]() XRAY_NEVER_INSTRUMENT {
      if (!Success)
        atomic_store(&TLD.Allocators, 0, memory_order_release);
    });

    // Acquire a set of buffers for this thread.
    if (BQ == nullptr)
      return nullptr;

    if (BQ->getBuffer(ThreadBuffers.NodeBuffer) != BufferQueue::ErrorCode::Ok)
      return nullptr;
    auto NodeBufferUndo = at_scope_exit([&]() XRAY_NEVER_INSTRUMENT {
      if (!Success)
        BQ->releaseBuffer(ThreadBuffers.NodeBuffer);
    });

    if (BQ->getBuffer(ThreadBuffers.RootsBuffer) != BufferQueue::ErrorCode::Ok)
      return nullptr;
    auto RootsBufferUndo = at_scope_exit([&]() XRAY_NEVER_INSTRUMENT {
      if (!Success)
        BQ->releaseBuffer(ThreadBuffers.RootsBuffer);
    });

    if (BQ->getBuffer(ThreadBuffers.ShadowStackBuffer) !=
        BufferQueue::ErrorCode::Ok)
      return nullptr;
    auto ShadowStackBufferUndo = at_scope_exit([&]() XRAY_NEVER_INSTRUMENT {
      if (!Success)
        BQ->releaseBuffer(ThreadBuffers.ShadowStackBuffer);
    });

    if (BQ->getBuffer(ThreadBuffers.NodeIdPairBuffer) !=
        BufferQueue::ErrorCode::Ok)
      return nullptr;

    Success = true;
    new (&AllocatorsStorage) FunctionCallTrie::Allocators(
        FunctionCallTrie::InitAllocatorsFromBuffers(ThreadBuffers));
    Allocators = reinterpret_cast<uptr>(
        reinterpret_cast<FunctionCallTrie::Allocators *>(&AllocatorsStorage));
    atomic_store(&TLD.Allocators, Allocators, memory_order_release);
  }

  if (Allocators == 1)
    return nullptr;

  uptr FCT = 0;
  if (atomic_compare_exchange_strong(&TLD.FCT, &FCT, 1, memory_order_acq_rel)) {
    new (&FunctionCallTrieStorage)
        FunctionCallTrie(*reinterpret_cast<FunctionCallTrie::Allocators *>(
            atomic_load_relaxed(&TLD.Allocators)));
    FCT = reinterpret_cast<uptr>(
        reinterpret_cast<FunctionCallTrie *>(&FunctionCallTrieStorage));
    atomic_store(&TLD.FCT, FCT, memory_order_release);
  }

  if (FCT == 1)
    return nullptr;

  return &TLD;
}

static void cleanupTLD() XRAY_NEVER_INSTRUMENT {
  auto FCT = atomic_exchange(&TLD.FCT, 0, memory_order_acq_rel);
  if (FCT == reinterpret_cast<uptr>(reinterpret_cast<FunctionCallTrie *>(
                 &FunctionCallTrieStorage)))
    reinterpret_cast<FunctionCallTrie *>(FCT)->~FunctionCallTrie();

  auto Allocators = atomic_exchange(&TLD.Allocators, 0, memory_order_acq_rel);
  if (Allocators ==
      reinterpret_cast<uptr>(
          reinterpret_cast<FunctionCallTrie::Allocators *>(&AllocatorsStorage)))
    reinterpret_cast<FunctionCallTrie::Allocators *>(Allocators)->~Allocators();
}

static void postCurrentThreadFCT(ProfilingData &T) XRAY_NEVER_INSTRUMENT {
  RecursionGuard TLDInit(TLDInitGuard);
  if (!TLDInit)
    return;

  uptr P = atomic_exchange(&T.FCT, 0, memory_order_acq_rel);
  if (P != reinterpret_cast<uptr>(
               reinterpret_cast<FunctionCallTrie *>(&FunctionCallTrieStorage)))
    return;

  auto FCT = reinterpret_cast<FunctionCallTrie *>(P);
  DCHECK_NE(FCT, nullptr);

  uptr A = atomic_exchange(&T.Allocators, 0, memory_order_acq_rel);
  if (A !=
      reinterpret_cast<uptr>(
          reinterpret_cast<FunctionCallTrie::Allocators *>(&AllocatorsStorage)))
    return;

  auto Allocators = reinterpret_cast<FunctionCallTrie::Allocators *>(A);
  DCHECK_NE(Allocators, nullptr);

  // Always move the data into the profile collector.
  profileCollectorService::post(BQ, std::move(*FCT), std::move(*Allocators),
                                std::move(ThreadBuffers), GetTid());

  // Re-initialize the ThreadBuffers object to a known "default" state.
  ThreadBuffers = FunctionCallTrie::Allocators::Buffers{};
}

} // namespace

const char *profilingCompilerDefinedFlags() XRAY_NEVER_INSTRUMENT {
#ifdef XRAY_PROFILER_DEFAULT_OPTIONS
  return SANITIZER_STRINGIFY(XRAY_PROFILER_DEFAULT_OPTIONS);
#else
  return "";
#endif
}

XRayLogFlushStatus profilingFlush() XRAY_NEVER_INSTRUMENT {
  if (atomic_load(&ProfilerLogStatus, memory_order_acquire) !=
      XRayLogInitStatus::XRAY_LOG_FINALIZED) {
    if (Verbosity())
      Report("Not flushing profiles, profiling not been finalized.\n");
    return XRayLogFlushStatus::XRAY_LOG_NOT_FLUSHING;
  }

  RecursionGuard SignalGuard(ReentranceGuard);
  if (!SignalGuard) {
    if (Verbosity())
      Report("Cannot finalize properly inside a signal handler!\n");
    atomic_store(&ProfilerLogFlushStatus,
                 XRayLogFlushStatus::XRAY_LOG_NOT_FLUSHING,
                 memory_order_release);
    return XRayLogFlushStatus::XRAY_LOG_NOT_FLUSHING;
  }

  s32 Previous = atomic_exchange(&ProfilerLogFlushStatus,
                                 XRayLogFlushStatus::XRAY_LOG_FLUSHING,
                                 memory_order_acq_rel);
  if (Previous == XRayLogFlushStatus::XRAY_LOG_FLUSHING) {
    if (Verbosity())
      Report("Not flushing profiles, implementation still flushing.\n");
    return XRayLogFlushStatus::XRAY_LOG_FLUSHING;
  }

  // At this point, we'll create the file that will contain the profile, but
  // only if the options say so.
  if (!profilingFlags()->no_flush) {
    // First check whether we have data in the profile collector service
    // before we try and write anything down.
    XRayBuffer B = profileCollectorService::nextBuffer({nullptr, 0});
    if (B.Data == nullptr) {
      if (Verbosity())
        Report("profiling: No data to flush.\n");
    } else {
      LogWriter *LW = LogWriter::Open();
      if (LW == nullptr) {
        if (Verbosity())
          Report("profiling: Failed to flush to file, dropping data.\n");
      } else {
        // Now for each of the buffers, write out the profile data as we would
        // see it in memory, verbatim.
        while (B.Data != nullptr && B.Size != 0) {
          LW->WriteAll(reinterpret_cast<const char *>(B.Data),
                       reinterpret_cast<const char *>(B.Data) + B.Size);
          B = profileCollectorService::nextBuffer(B);
        }
      }
      LogWriter::Close(LW);
    }
  }

  profileCollectorService::reset();

  atomic_store(&ProfilerLogFlushStatus, XRayLogFlushStatus::XRAY_LOG_FLUSHED,
               memory_order_release);
  atomic_store(&ProfilerLogStatus, XRayLogInitStatus::XRAY_LOG_UNINITIALIZED,
               memory_order_release);

  return XRayLogFlushStatus::XRAY_LOG_FLUSHED;
}

void profilingHandleArg0(int32_t FuncId,
                         XRayEntryType Entry) XRAY_NEVER_INSTRUMENT {
  unsigned char CPU;
  auto TSC = readTSC(CPU);
  RecursionGuard G(ReentranceGuard);
  if (!G)
    return;

  auto Status = atomic_load(&ProfilerLogStatus, memory_order_acquire);
  if (UNLIKELY(Status == XRayLogInitStatus::XRAY_LOG_UNINITIALIZED ||
               Status == XRayLogInitStatus::XRAY_LOG_INITIALIZING))
    return;

  if (UNLIKELY(Status == XRayLogInitStatus::XRAY_LOG_FINALIZED ||
               Status == XRayLogInitStatus::XRAY_LOG_FINALIZING)) {
    postCurrentThreadFCT(TLD);
    return;
  }

  auto T = getThreadLocalData();
  if (T == nullptr)
    return;

  auto FCT = reinterpret_cast<FunctionCallTrie *>(atomic_load_relaxed(&T->FCT));
  switch (Entry) {
  case XRayEntryType::ENTRY:
  case XRayEntryType::LOG_ARGS_ENTRY:
    FCT->enterFunction(FuncId, TSC, CPU);
    break;
  case XRayEntryType::EXIT:
  case XRayEntryType::TAIL:
    FCT->exitFunction(FuncId, TSC, CPU);
    break;
  default:
    // FIXME: Handle bugs.
    break;
  }
}

void profilingHandleArg1(int32_t FuncId, XRayEntryType Entry,
                         uint64_t) XRAY_NEVER_INSTRUMENT {
  return profilingHandleArg0(FuncId, Entry);
}

XRayLogInitStatus profilingFinalize() XRAY_NEVER_INSTRUMENT {
  s32 CurrentStatus = XRayLogInitStatus::XRAY_LOG_INITIALIZED;
  if (!atomic_compare_exchange_strong(&ProfilerLogStatus, &CurrentStatus,
                                      XRayLogInitStatus::XRAY_LOG_FINALIZING,
                                      memory_order_release)) {
    if (Verbosity())
      Report("Cannot finalize profile, the profiling is not initialized.\n");
    return static_cast<XRayLogInitStatus>(CurrentStatus);
  }

  // Mark then finalize the current generation of buffers. This allows us to let
  // the threads currently holding onto new buffers still use them, but let the
  // last reference do the memory cleanup.
  DCHECK_NE(BQ, nullptr);
  BQ->finalize();

  // Wait a grace period to allow threads to see that we're finalizing.
  SleepForMillis(profilingFlags()->grace_period_ms);

  // If we for some reason are entering this function from an instrumented
  // handler, we bail out.
  RecursionGuard G(ReentranceGuard);
  if (!G)
    return static_cast<XRayLogInitStatus>(CurrentStatus);

  // Post the current thread's data if we have any.
  postCurrentThreadFCT(TLD);

  // Then we force serialize the log data.
  profileCollectorService::serialize();

  atomic_store(&ProfilerLogStatus, XRayLogInitStatus::XRAY_LOG_FINALIZED,
               memory_order_release);
  return XRayLogInitStatus::XRAY_LOG_FINALIZED;
}

XRayLogInitStatus
profilingLoggingInit(size_t, size_t, void *Options,
                     size_t OptionsSize) XRAY_NEVER_INSTRUMENT {
  RecursionGuard G(ReentranceGuard);
  if (!G)
    return XRayLogInitStatus::XRAY_LOG_UNINITIALIZED;

  s32 CurrentStatus = XRayLogInitStatus::XRAY_LOG_UNINITIALIZED;
  if (!atomic_compare_exchange_strong(&ProfilerLogStatus, &CurrentStatus,
                                      XRayLogInitStatus::XRAY_LOG_INITIALIZING,
                                      memory_order_acq_rel)) {
    if (Verbosity())
      Report("Cannot initialize already initialised profiling "
             "implementation.\n");
    return static_cast<XRayLogInitStatus>(CurrentStatus);
  }

  {
    SpinMutexLock Lock(&ProfilerOptionsMutex);
    FlagParser ConfigParser;
    ProfilerFlags Flags;
    Flags.setDefaults();
    registerProfilerFlags(&ConfigParser, &Flags);
    ConfigParser.ParseString(profilingCompilerDefinedFlags());
    const char *Env = GetEnv("XRAY_PROFILING_OPTIONS");
    if (Env == nullptr)
      Env = "";
    ConfigParser.ParseString(Env);

    // Then parse the configuration string provided.
    ConfigParser.ParseString(static_cast<const char *>(Options));
    if (Verbosity())
      ReportUnrecognizedFlags();
    *profilingFlags() = Flags;
  }

  // We need to reset the profile data collection implementation now.
  profileCollectorService::reset();

  // Then also reset the buffer queue implementation.
  if (BQ == nullptr) {
    bool Success = false;
    new (&BufferQueueStorage)
        BufferQueue(profilingFlags()->per_thread_allocator_max,
                    profilingFlags()->buffers_max, Success);
    if (!Success) {
      if (Verbosity())
        Report("Failed to initialize preallocated memory buffers!");
      atomic_store(&ProfilerLogStatus,
                   XRayLogInitStatus::XRAY_LOG_UNINITIALIZED,
                   memory_order_release);
      return XRayLogInitStatus::XRAY_LOG_UNINITIALIZED;
    }

    // If we've succeded, set the global pointer to the initialised storage.
    BQ = reinterpret_cast<BufferQueue *>(&BufferQueueStorage);
  } else {
    BQ->finalize();
    auto InitStatus = BQ->init(profilingFlags()->per_thread_allocator_max,
                               profilingFlags()->buffers_max);

    if (InitStatus != BufferQueue::ErrorCode::Ok) {
      if (Verbosity())
        Report("Failed to initialize preallocated memory buffers; error: %s",
               BufferQueue::getErrorString(InitStatus));
      atomic_store(&ProfilerLogStatus,
                   XRayLogInitStatus::XRAY_LOG_UNINITIALIZED,
                   memory_order_release);
      return XRayLogInitStatus::XRAY_LOG_UNINITIALIZED;
    }

    DCHECK(!BQ->finalizing());
  }

  // We need to set up the exit handlers.
  static pthread_once_t Once = PTHREAD_ONCE_INIT;
  pthread_once(
      &Once, +[] {
        pthread_key_create(
            &ProfilingKey, +[](void *P) XRAY_NEVER_INSTRUMENT {
              if (atomic_exchange(&ThreadExitingLatch, 1, memory_order_acq_rel))
                return;

              if (P == nullptr)
                return;

              auto T = reinterpret_cast<ProfilingData *>(P);
              if (atomic_load_relaxed(&T->Allocators) == 0)
                return;

              {
                // If we're somehow executing this while inside a
                // non-reentrant-friendly context, we skip attempting to post
                // the current thread's data.
                RecursionGuard G(ReentranceGuard);
                if (!G)
                  return;

                postCurrentThreadFCT(*T);
              }
            });

        // We also need to set up an exit handler, so that we can get the
        // profile information at exit time. We use the C API to do this, to not
        // rely on C++ ABI functions for registering exit handlers.
        Atexit(+[]() XRAY_NEVER_INSTRUMENT {
          if (atomic_exchange(&ThreadExitingLatch, 1, memory_order_acq_rel))
            return;

          auto Cleanup =
              at_scope_exit([]() XRAY_NEVER_INSTRUMENT { cleanupTLD(); });

          // Finalize and flush.
          if (profilingFinalize() != XRAY_LOG_FINALIZED ||
              profilingFlush() != XRAY_LOG_FLUSHED)
            return;

          if (Verbosity())
            Report("XRay Profile flushed at exit.");
        });
      });

  __xray_log_set_buffer_iterator(profileCollectorService::nextBuffer);
  __xray_set_handler(profilingHandleArg0);
  __xray_set_handler_arg1(profilingHandleArg1);

  atomic_store(&ProfilerLogStatus, XRayLogInitStatus::XRAY_LOG_INITIALIZED,
               memory_order_release);
  if (Verbosity())
    Report("XRay Profiling init successful.\n");

  return XRayLogInitStatus::XRAY_LOG_INITIALIZED;
}

bool profilingDynamicInitializer() XRAY_NEVER_INSTRUMENT {
  // Set up the flag defaults from the static defaults and the
  // compiler-provided defaults.
  {
    SpinMutexLock Lock(&ProfilerOptionsMutex);
    auto *F = profilingFlags();
    F->setDefaults();
    FlagParser ProfilingParser;
    registerProfilerFlags(&ProfilingParser, F);
    ProfilingParser.ParseString(profilingCompilerDefinedFlags());
  }

  XRayLogImpl Impl{
      profilingLoggingInit,
      profilingFinalize,
      profilingHandleArg0,
      profilingFlush,
  };
  auto RegistrationResult = __xray_log_register_mode("xray-profiling", Impl);
  if (RegistrationResult != XRayLogRegisterStatus::XRAY_REGISTRATION_OK) {
    if (Verbosity())
      Report("Cannot register XRay Profiling mode to 'xray-profiling'; error = "
             "%d\n",
             RegistrationResult);
    return false;
  }

  if (!internal_strcmp(flags()->xray_mode, "xray-profiling"))
    __xray_log_select_mode("xray_profiling");
  return true;
}

} // namespace __xray

static auto UNUSED Unused = __xray::profilingDynamicInitializer();
