//===-- xray_basic_logging.cc -----------------------------------*- C++ -*-===//
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
// Implementation of a simple in-memory log of XRay events. This defines a
// logging function that's compatible with the XRay handler interface, and
// routines for exporting data to files.
//
//===----------------------------------------------------------------------===//

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#if SANITIZER_FREEBSD || SANITIZER_NETBSD || SANITIZER_OPENBSD || SANITIZER_MAC
#include <sys/syscall.h>
#endif
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "sanitizer_common/sanitizer_allocator_internal.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "xray/xray_records.h"
#include "xray_recursion_guard.h"
#include "xray_basic_flags.h"
#include "xray_basic_logging.h"
#include "xray_defs.h"
#include "xray_flags.h"
#include "xray_interface_internal.h"
#include "xray_tsc.h"
#include "xray_utils.h"

namespace __xray {

static SpinMutex LogMutex;

namespace {
// We use elements of this type to record the entry TSC of every function ID we
// see as we're tracing a particular thread's execution.
struct alignas(16) StackEntry {
  int32_t FuncId;
  uint16_t Type;
  uint8_t CPU;
  uint8_t Padding;
  uint64_t TSC;
};

static_assert(sizeof(StackEntry) == 16, "Wrong size for StackEntry");

struct XRAY_TLS_ALIGNAS(64) ThreadLocalData {
  void *InMemoryBuffer = nullptr;
  size_t BufferSize = 0;
  size_t BufferOffset = 0;
  void *ShadowStack = nullptr;
  size_t StackSize = 0;
  size_t StackEntries = 0;
  __xray::LogWriter *LogWriter = nullptr;
};

struct BasicLoggingOptions {
  int DurationFilterMicros = 0;
  size_t MaxStackDepth = 0;
  size_t ThreadBufferSize = 0;
};
} // namespace

static pthread_key_t PThreadKey;

static atomic_uint8_t BasicInitialized{0};

struct BasicLoggingOptions GlobalOptions;

thread_local atomic_uint8_t Guard{0};

static atomic_uint8_t UseRealTSC{0};
static atomic_uint64_t ThresholdTicks{0};
static atomic_uint64_t TicksPerSec{0};
static atomic_uint64_t CycleFrequency{NanosecondsPerSecond};

static LogWriter *getLog() XRAY_NEVER_INSTRUMENT {
  LogWriter* LW = LogWriter::Open();
  if (LW == nullptr)
    return LW;

  static pthread_once_t DetectOnce = PTHREAD_ONCE_INIT;
  pthread_once(&DetectOnce, +[] {
    if (atomic_load(&UseRealTSC, memory_order_acquire))
      atomic_store(&CycleFrequency, getTSCFrequency(), memory_order_release);
  });

  // Since we're here, we get to write the header. We set it up so that the
  // header will only be written once, at the start, and let the threads
  // logging do writes which just append.
  XRayFileHeader Header;
  // Version 2 includes tail exit records.
  // Version 3 includes pid inside records.
  Header.Version = 3;
  Header.Type = FileTypes::NAIVE_LOG;
  Header.CycleFrequency = atomic_load(&CycleFrequency, memory_order_acquire);

  // FIXME: Actually check whether we have 'constant_tsc' and 'nonstop_tsc'
  // before setting the values in the header.
  Header.ConstantTSC = 1;
  Header.NonstopTSC = 1;
  LW->WriteAll(reinterpret_cast<char *>(&Header),
               reinterpret_cast<char *>(&Header) + sizeof(Header));
  return LW;
}

static LogWriter *getGlobalLog() XRAY_NEVER_INSTRUMENT {
  static pthread_once_t OnceInit = PTHREAD_ONCE_INIT;
  static LogWriter *LW = nullptr;
  pthread_once(&OnceInit, +[] { LW = getLog(); });
  return LW;
}

static ThreadLocalData &getThreadLocalData() XRAY_NEVER_INSTRUMENT {
  thread_local ThreadLocalData TLD;
  thread_local bool UNUSED TOnce = [] {
    if (GlobalOptions.ThreadBufferSize == 0) {
      if (Verbosity())
        Report("Not initializing TLD since ThreadBufferSize == 0.\n");
      return false;
    }
    pthread_setspecific(PThreadKey, &TLD);
    TLD.LogWriter = getGlobalLog();
    TLD.InMemoryBuffer = reinterpret_cast<XRayRecord *>(
        InternalAlloc(sizeof(XRayRecord) * GlobalOptions.ThreadBufferSize,
                      nullptr, alignof(XRayRecord)));
    TLD.BufferSize = GlobalOptions.ThreadBufferSize;
    TLD.BufferOffset = 0;
    if (GlobalOptions.MaxStackDepth == 0) {
      if (Verbosity())
        Report("Not initializing the ShadowStack since MaxStackDepth == 0.\n");
      TLD.StackSize = 0;
      TLD.StackEntries = 0;
      TLD.ShadowStack = nullptr;
      return false;
    }
    TLD.ShadowStack = reinterpret_cast<StackEntry *>(
        InternalAlloc(sizeof(StackEntry) * GlobalOptions.MaxStackDepth, nullptr,
                      alignof(StackEntry)));
    TLD.StackSize = GlobalOptions.MaxStackDepth;
    TLD.StackEntries = 0;
    return false;
  }();
  return TLD;
}

template <class RDTSC>
void InMemoryRawLog(int32_t FuncId, XRayEntryType Type,
                    RDTSC ReadTSC) XRAY_NEVER_INSTRUMENT {
  auto &TLD = getThreadLocalData();
  LogWriter *LW = getGlobalLog();
  if (LW == nullptr)
    return;

  // Use a simple recursion guard, to handle cases where we're already logging
  // and for one reason or another, this function gets called again in the same
  // thread.
  RecursionGuard G(Guard);
  if (!G)
    return;

  uint8_t CPU = 0;
  uint64_t TSC = ReadTSC(CPU);

  switch (Type) {
  case XRayEntryType::ENTRY:
  case XRayEntryType::LOG_ARGS_ENTRY: {
    // Short circuit if we've reached the maximum depth of the stack.
    if (TLD.StackEntries++ >= TLD.StackSize)
      return;

    // When we encounter an entry event, we keep track of the TSC and the CPU,
    // and put it in the stack.
    StackEntry E;
    E.FuncId = FuncId;
    E.CPU = CPU;
    E.Type = Type;
    E.TSC = TSC;
    auto StackEntryPtr = static_cast<char *>(TLD.ShadowStack) +
                         (sizeof(StackEntry) * (TLD.StackEntries - 1));
    internal_memcpy(StackEntryPtr, &E, sizeof(StackEntry));
    break;
  }
  case XRayEntryType::EXIT:
  case XRayEntryType::TAIL: {
    if (TLD.StackEntries == 0)
      break;

    if (--TLD.StackEntries >= TLD.StackSize)
      return;

    // When we encounter an exit event, we check whether all the following are
    // true:
    //
    // - The Function ID is the same as the most recent entry in the stack.
    // - The CPU is the same as the most recent entry in the stack.
    // - The Delta of the TSCs is less than the threshold amount of time we're
    //   looking to record.
    //
    // If all of these conditions are true, we pop the stack and don't write a
    // record and move the record offset back.
    StackEntry StackTop;
    auto StackEntryPtr = static_cast<char *>(TLD.ShadowStack) +
                         (sizeof(StackEntry) * TLD.StackEntries);
    internal_memcpy(&StackTop, StackEntryPtr, sizeof(StackEntry));
    if (StackTop.FuncId == FuncId && StackTop.CPU == CPU &&
        StackTop.TSC < TSC) {
      auto Delta = TSC - StackTop.TSC;
      if (Delta < atomic_load(&ThresholdTicks, memory_order_relaxed)) {
        DCHECK(TLD.BufferOffset > 0);
        TLD.BufferOffset -= StackTop.Type == XRayEntryType::ENTRY ? 1 : 2;
        return;
      }
    }
    break;
  }
  default:
    // Should be unreachable.
    DCHECK(false && "Unsupported XRayEntryType encountered.");
    break;
  }

  // First determine whether the delta between the function's enter record and
  // the exit record is higher than the threshold.
  XRayRecord R;
  R.RecordType = RecordTypes::NORMAL;
  R.CPU = CPU;
  R.TSC = TSC;
  R.TId = GetTid(); 
  R.PId = internal_getpid(); 
  R.Type = Type;
  R.FuncId = FuncId;
  auto FirstEntry = reinterpret_cast<XRayRecord *>(TLD.InMemoryBuffer);
  internal_memcpy(FirstEntry + TLD.BufferOffset, &R, sizeof(R));
  if (++TLD.BufferOffset == TLD.BufferSize) {
    SpinMutexLock Lock(&LogMutex);
    LW->WriteAll(reinterpret_cast<char *>(FirstEntry),
                 reinterpret_cast<char *>(FirstEntry + TLD.BufferOffset));
    TLD.BufferOffset = 0;
    TLD.StackEntries = 0;
  }
}

template <class RDTSC>
void InMemoryRawLogWithArg(int32_t FuncId, XRayEntryType Type, uint64_t Arg1,
                           RDTSC ReadTSC) XRAY_NEVER_INSTRUMENT {
  auto &TLD = getThreadLocalData();
  auto FirstEntry =
      reinterpret_cast<XRayArgPayload *>(TLD.InMemoryBuffer);
  const auto &BuffLen = TLD.BufferSize;
  LogWriter *LW = getGlobalLog();
  if (LW == nullptr)
    return;

  // First we check whether there's enough space to write the data consecutively
  // in the thread-local buffer. If not, we first flush the buffer before
  // attempting to write the two records that must be consecutive.
  if (TLD.BufferOffset + 2 > BuffLen) {
    SpinMutexLock Lock(&LogMutex);
    LW->WriteAll(reinterpret_cast<char *>(FirstEntry),
                 reinterpret_cast<char *>(FirstEntry + TLD.BufferOffset));
    TLD.BufferOffset = 0;
    TLD.StackEntries = 0;
  }

  // Then we write the "we have an argument" record.
  InMemoryRawLog(FuncId, Type, ReadTSC);

  RecursionGuard G(Guard);
  if (!G)
    return;

  // And, from here on write the arg payload.
  XRayArgPayload R;
  R.RecordType = RecordTypes::ARG_PAYLOAD;
  R.FuncId = FuncId;
  R.TId = GetTid(); 
  R.PId = internal_getpid(); 
  R.Arg = Arg1;
  internal_memcpy(FirstEntry + TLD.BufferOffset, &R, sizeof(R));
  if (++TLD.BufferOffset == BuffLen) {
    SpinMutexLock Lock(&LogMutex);
    LW->WriteAll(reinterpret_cast<char *>(FirstEntry),
                 reinterpret_cast<char *>(FirstEntry + TLD.BufferOffset));
    TLD.BufferOffset = 0;
    TLD.StackEntries = 0;
  }
}

void basicLoggingHandleArg0RealTSC(int32_t FuncId,
                                   XRayEntryType Type) XRAY_NEVER_INSTRUMENT {
  InMemoryRawLog(FuncId, Type, readTSC);
}

void basicLoggingHandleArg0EmulateTSC(int32_t FuncId, XRayEntryType Type)
    XRAY_NEVER_INSTRUMENT {
  InMemoryRawLog(FuncId, Type, [](uint8_t &CPU) XRAY_NEVER_INSTRUMENT {
    timespec TS;
    int result = clock_gettime(CLOCK_REALTIME, &TS);
    if (result != 0) {
      Report("clock_gettimg(2) return %d, errno=%d.", result, int(errno));
      TS = {0, 0};
    }
    CPU = 0;
    return TS.tv_sec * NanosecondsPerSecond + TS.tv_nsec;
  });
}

void basicLoggingHandleArg1RealTSC(int32_t FuncId, XRayEntryType Type,
                                   uint64_t Arg1) XRAY_NEVER_INSTRUMENT {
  InMemoryRawLogWithArg(FuncId, Type, Arg1, readTSC);
}

void basicLoggingHandleArg1EmulateTSC(int32_t FuncId, XRayEntryType Type,
                                      uint64_t Arg1) XRAY_NEVER_INSTRUMENT {
  InMemoryRawLogWithArg(
      FuncId, Type, Arg1, [](uint8_t &CPU) XRAY_NEVER_INSTRUMENT {
        timespec TS;
        int result = clock_gettime(CLOCK_REALTIME, &TS);
        if (result != 0) {
          Report("clock_gettimg(2) return %d, errno=%d.", result, int(errno));
          TS = {0, 0};
        }
        CPU = 0;
        return TS.tv_sec * NanosecondsPerSecond + TS.tv_nsec;
      });
}

static void TLDDestructor(void *P) XRAY_NEVER_INSTRUMENT {
  ThreadLocalData &TLD = *reinterpret_cast<ThreadLocalData *>(P);
  auto ExitGuard = at_scope_exit([&TLD] {
    // Clean up dynamic resources.
    if (TLD.InMemoryBuffer)
      InternalFree(TLD.InMemoryBuffer);
    if (TLD.ShadowStack)
      InternalFree(TLD.ShadowStack);
    if (Verbosity())
      Report("Cleaned up log for TID: %d\n", GetTid());
  });

  if (TLD.LogWriter == nullptr || TLD.BufferOffset == 0) {
    if (Verbosity())
      Report("Skipping buffer for TID: %d; Offset = %llu\n", GetTid(),
             TLD.BufferOffset);
    return;
  }

  {
    SpinMutexLock L(&LogMutex);
    TLD.LogWriter->WriteAll(reinterpret_cast<char *>(TLD.InMemoryBuffer),
                            reinterpret_cast<char *>(TLD.InMemoryBuffer) +
                            (sizeof(XRayRecord) * TLD.BufferOffset));
  }

  // Because this thread's exit could be the last one trying to write to
  // the file and that we're not able to close out the file properly, we
  // sync instead and hope that the pending writes are flushed as the
  // thread exits.
  TLD.LogWriter->Flush();
}

XRayLogInitStatus basicLoggingInit(UNUSED size_t BufferSize,
                                   UNUSED size_t BufferMax, void *Options,
                                   size_t OptionsSize) XRAY_NEVER_INSTRUMENT {
  uint8_t Expected = 0;
  if (!atomic_compare_exchange_strong(&BasicInitialized, &Expected, 1,
                                      memory_order_acq_rel)) {
    if (Verbosity())
      Report("Basic logging already initialized.\n");
    return XRayLogInitStatus::XRAY_LOG_INITIALIZED;
  }

  static pthread_once_t OnceInit = PTHREAD_ONCE_INIT;
  pthread_once(&OnceInit, +[] {
    pthread_key_create(&PThreadKey, TLDDestructor);
    atomic_store(&UseRealTSC, probeRequiredCPUFeatures(), memory_order_release);
    // Initialize the global TicksPerSec value.
    atomic_store(&TicksPerSec,
                 probeRequiredCPUFeatures() ? getTSCFrequency()
                                            : NanosecondsPerSecond,
                 memory_order_release);
    if (!atomic_load(&UseRealTSC, memory_order_relaxed) && Verbosity())
      Report("WARNING: Required CPU features missing for XRay instrumentation, "
             "using emulation instead.\n");
  });

  FlagParser P;
  BasicFlags F;
  F.setDefaults();
  registerXRayBasicFlags(&P, &F);
  P.ParseString(useCompilerDefinedBasicFlags());
  auto *EnvOpts = GetEnv("XRAY_BASIC_OPTIONS");
  if (EnvOpts == nullptr)
    EnvOpts = "";

  P.ParseString(EnvOpts);

  // If XRAY_BASIC_OPTIONS was not defined, then we use the deprecated options
  // set through XRAY_OPTIONS instead.
  if (internal_strlen(EnvOpts) == 0) {
    F.func_duration_threshold_us =
        flags()->xray_naive_log_func_duration_threshold_us;
    F.max_stack_depth = flags()->xray_naive_log_max_stack_depth;
    F.thread_buffer_size = flags()->xray_naive_log_thread_buffer_size;
  }

  P.ParseString(static_cast<const char *>(Options));
  GlobalOptions.ThreadBufferSize = F.thread_buffer_size;
  GlobalOptions.DurationFilterMicros = F.func_duration_threshold_us;
  GlobalOptions.MaxStackDepth = F.max_stack_depth;
  *basicFlags() = F;

  atomic_store(&ThresholdTicks,
               atomic_load(&TicksPerSec, memory_order_acquire) *
                   GlobalOptions.DurationFilterMicros / 1000000,
               memory_order_release);
  __xray_set_handler_arg1(atomic_load(&UseRealTSC, memory_order_acquire)
                              ? basicLoggingHandleArg1RealTSC
                              : basicLoggingHandleArg1EmulateTSC);
  __xray_set_handler(atomic_load(&UseRealTSC, memory_order_acquire)
                         ? basicLoggingHandleArg0RealTSC
                         : basicLoggingHandleArg0EmulateTSC);

  // TODO: Implement custom event and typed event handling support in Basic
  // Mode.
  __xray_remove_customevent_handler();
  __xray_remove_typedevent_handler();

  return XRayLogInitStatus::XRAY_LOG_INITIALIZED;
}

XRayLogInitStatus basicLoggingFinalize() XRAY_NEVER_INSTRUMENT {
  uint8_t Expected = 0;
  if (!atomic_compare_exchange_strong(&BasicInitialized, &Expected, 0,
                                      memory_order_acq_rel) &&
      Verbosity())
    Report("Basic logging already finalized.\n");

  // Nothing really to do aside from marking state of the global to be
  // uninitialized.

  return XRayLogInitStatus::XRAY_LOG_FINALIZED;
}

XRayLogFlushStatus basicLoggingFlush() XRAY_NEVER_INSTRUMENT {
  // This really does nothing, since flushing the logs happen at the end of a
  // thread's lifetime, or when the buffers are full.
  return XRayLogFlushStatus::XRAY_LOG_FLUSHED;
}

// This is a handler that, effectively, does nothing.
void basicLoggingHandleArg0Empty(int32_t, XRayEntryType) XRAY_NEVER_INSTRUMENT {
}

bool basicLogDynamicInitializer() XRAY_NEVER_INSTRUMENT {
  XRayLogImpl Impl{
      basicLoggingInit,
      basicLoggingFinalize,
      basicLoggingHandleArg0Empty,
      basicLoggingFlush,
  };
  auto RegistrationResult = __xray_log_register_mode("xray-basic", Impl);
  if (RegistrationResult != XRayLogRegisterStatus::XRAY_REGISTRATION_OK &&
      Verbosity())
    Report("Cannot register XRay Basic Mode to 'xray-basic'; error = %d\n",
           RegistrationResult);
  if (flags()->xray_naive_log ||
      !internal_strcmp(flags()->xray_mode, "xray-basic")) {
    auto SelectResult = __xray_log_select_mode("xray-basic");
    if (SelectResult != XRayLogRegisterStatus::XRAY_REGISTRATION_OK) {
      if (Verbosity())
        Report("Failed selecting XRay Basic Mode; error = %d\n", SelectResult);
      return false;
    }

    // We initialize the implementation using the data we get from the
    // XRAY_BASIC_OPTIONS environment variable, at this point of the
    // implementation.
    auto *Env = GetEnv("XRAY_BASIC_OPTIONS");
    auto InitResult =
        __xray_log_init_mode("xray-basic", Env == nullptr ? "" : Env);
    if (InitResult != XRayLogInitStatus::XRAY_LOG_INITIALIZED) {
      if (Verbosity())
        Report("Failed initializing XRay Basic Mode; error = %d\n", InitResult);
      return false;
    }

    // At this point we know that we've successfully initialized Basic mode
    // tracing, and the only chance we're going to get for the current thread to
    // clean-up may be at thread/program exit. To ensure that we're going to get
    // the cleanup even without calling the finalization routines, we're
    // registering a program exit function that will do the cleanup.
    static pthread_once_t DynamicOnce = PTHREAD_ONCE_INIT;
    pthread_once(&DynamicOnce, +[] {
      static void *FakeTLD = nullptr;
      FakeTLD = &getThreadLocalData();
      Atexit(+[] { TLDDestructor(FakeTLD); });
    });
  }
  return true;
}

} // namespace __xray

static auto UNUSED Unused = __xray::basicLogDynamicInitializer();
