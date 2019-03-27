//===-- xray_fdr_logging.cc ------------------------------------*- C++ -*-===//
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
// Here we implement the Flight Data Recorder mode for XRay, where we use
// compact structures to store records in memory as well as when writing out the
// data to files.
//
//===----------------------------------------------------------------------===//
#include "xray_fdr_logging.h"
#include <cassert>
#include <errno.h>
#include <limits>
#include <memory>
#include <pthread.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "sanitizer_common/sanitizer_allocator_internal.h"
#include "sanitizer_common/sanitizer_atomic.h"
#include "sanitizer_common/sanitizer_common.h"
#include "xray/xray_interface.h"
#include "xray/xray_records.h"
#include "xray_allocator.h"
#include "xray_buffer_queue.h"
#include "xray_defs.h"
#include "xray_fdr_controller.h"
#include "xray_fdr_flags.h"
#include "xray_fdr_log_writer.h"
#include "xray_flags.h"
#include "xray_recursion_guard.h"
#include "xray_tsc.h"
#include "xray_utils.h"

namespace __xray {

static atomic_sint32_t LoggingStatus = {
    XRayLogInitStatus::XRAY_LOG_UNINITIALIZED};

namespace {

// Group together thread-local-data in a struct, then hide it behind a function
// call so that it can be initialized on first use instead of as a global. We
// force the alignment to 64-bytes for x86 cache line alignment, as this
// structure is used in the hot path of implementation.
struct XRAY_TLS_ALIGNAS(64) ThreadLocalData {
  BufferQueue::Buffer Buffer{};
  BufferQueue *BQ = nullptr;

  using LogWriterStorage =
      typename std::aligned_storage<sizeof(FDRLogWriter),
                                    alignof(FDRLogWriter)>::type;

  LogWriterStorage LWStorage;
  FDRLogWriter *Writer = nullptr;

  using ControllerStorage =
      typename std::aligned_storage<sizeof(FDRController<>),
                                    alignof(FDRController<>)>::type;
  ControllerStorage CStorage;
  FDRController<> *Controller = nullptr;
};

} // namespace

static_assert(std::is_trivially_destructible<ThreadLocalData>::value,
              "ThreadLocalData must be trivially destructible");

// Use a global pthread key to identify thread-local data for logging.
static pthread_key_t Key;

// Global BufferQueue.
static std::aligned_storage<sizeof(BufferQueue)>::type BufferQueueStorage;
static BufferQueue *BQ = nullptr;

// Global thresholds for function durations.
static atomic_uint64_t ThresholdTicks{0};

// Global for ticks per second.
static atomic_uint64_t TicksPerSec{0};

static atomic_sint32_t LogFlushStatus = {
    XRayLogFlushStatus::XRAY_LOG_NOT_FLUSHING};

// This function will initialize the thread-local data structure used by the FDR
// logging implementation and return a reference to it. The implementation
// details require a bit of care to maintain.
//
// First, some requirements on the implementation in general:
//
//   - XRay handlers should not call any memory allocation routines that may
//     delegate to an instrumented implementation. This means functions like
//     malloc() and free() should not be called while instrumenting.
//
//   - We would like to use some thread-local data initialized on first-use of
//     the XRay instrumentation. These allow us to implement unsynchronized
//     routines that access resources associated with the thread.
//
// The implementation here uses a few mechanisms that allow us to provide both
// the requirements listed above. We do this by:
//
//   1. Using a thread-local aligned storage buffer for representing the
//      ThreadLocalData struct. This data will be uninitialized memory by
//      design.
//
//   2. Not requiring a thread exit handler/implementation, keeping the
//      thread-local as purely a collection of references/data that do not
//      require cleanup.
//
// We're doing this to avoid using a `thread_local` object that has a
// non-trivial destructor, because the C++ runtime might call std::malloc(...)
// to register calls to destructors. Deadlocks may arise when, for example, an
// externally provided malloc implementation is XRay instrumented, and
// initializing the thread-locals involves calling into malloc. A malloc
// implementation that does global synchronization might be holding a lock for a
// critical section, calling a function that might be XRay instrumented (and
// thus in turn calling into malloc by virtue of registration of the
// thread_local's destructor).
#if XRAY_HAS_TLS_ALIGNAS
static_assert(alignof(ThreadLocalData) >= 64,
              "ThreadLocalData must be cache line aligned.");
#endif
static ThreadLocalData &getThreadLocalData() {
  thread_local typename std::aligned_storage<
      sizeof(ThreadLocalData), alignof(ThreadLocalData)>::type TLDStorage{};

  if (pthread_getspecific(Key) == NULL) {
    new (reinterpret_cast<ThreadLocalData *>(&TLDStorage)) ThreadLocalData{};
    pthread_setspecific(Key, &TLDStorage);
  }

  return *reinterpret_cast<ThreadLocalData *>(&TLDStorage);
}

static XRayFileHeader &fdrCommonHeaderInfo() {
  static std::aligned_storage<sizeof(XRayFileHeader)>::type HStorage;
  static pthread_once_t OnceInit = PTHREAD_ONCE_INIT;
  static bool TSCSupported = true;
  static uint64_t CycleFrequency = NanosecondsPerSecond;
  pthread_once(
      &OnceInit, +[] {
        XRayFileHeader &H = reinterpret_cast<XRayFileHeader &>(HStorage);
        // Version 2 of the log writes the extents of the buffer, instead of
        // relying on an end-of-buffer record.
        // Version 3 includes PID metadata record.
        // Version 4 includes CPU data in the custom event records.
        // Version 5 uses relative deltas for custom and typed event records,
        // and removes the CPU data in custom event records (similar to how
        // function records use deltas instead of full TSCs and rely on other
        // metadata records for TSC wraparound and CPU migration).
        H.Version = 5;
        H.Type = FileTypes::FDR_LOG;

        // Test for required CPU features and cache the cycle frequency
        TSCSupported = probeRequiredCPUFeatures();
        if (TSCSupported)
          CycleFrequency = getTSCFrequency();
        H.CycleFrequency = CycleFrequency;

        // FIXME: Actually check whether we have 'constant_tsc' and
        // 'nonstop_tsc' before setting the values in the header.
        H.ConstantTSC = 1;
        H.NonstopTSC = 1;
      });
  return reinterpret_cast<XRayFileHeader &>(HStorage);
}

// This is the iterator implementation, which knows how to handle FDR-mode
// specific buffers. This is used as an implementation of the iterator function
// needed by __xray_set_buffer_iterator(...). It maintains a global state of the
// buffer iteration for the currently installed FDR mode buffers. In particular:
//
//   - If the argument represents the initial state of XRayBuffer ({nullptr, 0})
//     then the iterator returns the header information.
//   - If the argument represents the header information ({address of header
//     info, size of the header info}) then it returns the first FDR buffer's
//     address and extents.
//   - It will keep returning the next buffer and extents as there are more
//     buffers to process. When the input represents the last buffer, it will
//     return the initial state to signal completion ({nullptr, 0}).
//
// See xray/xray_log_interface.h for more details on the requirements for the
// implementations of __xray_set_buffer_iterator(...) and
// __xray_log_process_buffers(...).
XRayBuffer fdrIterator(const XRayBuffer B) {
  DCHECK(internal_strcmp(__xray_log_get_current_mode(), "xray-fdr") == 0);
  DCHECK(BQ->finalizing());

  if (BQ == nullptr || !BQ->finalizing()) {
    if (Verbosity())
      Report(
          "XRay FDR: Failed global buffer queue is null or not finalizing!\n");
    return {nullptr, 0};
  }

  // We use a global scratch-pad for the header information, which only gets
  // initialized the first time this function is called. We'll update one part
  // of this information with some relevant data (in particular the number of
  // buffers to expect).
  static std::aligned_storage<sizeof(XRayFileHeader)>::type HeaderStorage;
  static pthread_once_t HeaderOnce = PTHREAD_ONCE_INIT;
  pthread_once(
      &HeaderOnce, +[] {
        reinterpret_cast<XRayFileHeader &>(HeaderStorage) =
            fdrCommonHeaderInfo();
      });

  // We use a convenience alias for code referring to Header from here on out.
  auto &Header = reinterpret_cast<XRayFileHeader &>(HeaderStorage);
  if (B.Data == nullptr && B.Size == 0) {
    Header.FdrData = FdrAdditionalHeaderData{BQ->ConfiguredBufferSize()};
    return XRayBuffer{static_cast<void *>(&Header), sizeof(Header)};
  }

  static BufferQueue::const_iterator It{};
  static BufferQueue::const_iterator End{};
  static uint8_t *CurrentBuffer{nullptr};
  static size_t SerializedBufferSize = 0;
  if (B.Data == static_cast<void *>(&Header) && B.Size == sizeof(Header)) {
    // From this point on, we provide raw access to the raw buffer we're getting
    // from the BufferQueue. We're relying on the iterators from the current
    // Buffer queue.
    It = BQ->cbegin();
    End = BQ->cend();
  }

  if (CurrentBuffer != nullptr) {
    deallocateBuffer(CurrentBuffer, SerializedBufferSize);
    CurrentBuffer = nullptr;
  }

  if (It == End)
    return {nullptr, 0};

  // Set up the current buffer to contain the extents like we would when writing
  // out to disk. The difference here would be that we still write "empty"
  // buffers, or at least go through the iterators faithfully to let the
  // handlers see the empty buffers in the queue.
  //
  // We need this atomic fence here to ensure that writes happening to the
  // buffer have been committed before we load the extents atomically. Because
  // the buffer is not explicitly synchronised across threads, we rely on the
  // fence ordering to ensure that writes we expect to have been completed
  // before the fence are fully committed before we read the extents.
  atomic_thread_fence(memory_order_acquire);
  auto BufferSize = atomic_load(It->Extents, memory_order_acquire);
  SerializedBufferSize = BufferSize + sizeof(MetadataRecord);
  CurrentBuffer = allocateBuffer(SerializedBufferSize);
  if (CurrentBuffer == nullptr)
    return {nullptr, 0};

  // Write out the extents as a Metadata Record into the CurrentBuffer.
  MetadataRecord ExtentsRecord;
  ExtentsRecord.Type = uint8_t(RecordType::Metadata);
  ExtentsRecord.RecordKind =
      uint8_t(MetadataRecord::RecordKinds::BufferExtents);
  internal_memcpy(ExtentsRecord.Data, &BufferSize, sizeof(BufferSize));
  auto AfterExtents =
      static_cast<char *>(internal_memcpy(CurrentBuffer, &ExtentsRecord,
                                          sizeof(MetadataRecord))) +
      sizeof(MetadataRecord);
  internal_memcpy(AfterExtents, It->Data, BufferSize);

  XRayBuffer Result;
  Result.Data = CurrentBuffer;
  Result.Size = SerializedBufferSize;
  ++It;
  return Result;
}

// Must finalize before flushing.
XRayLogFlushStatus fdrLoggingFlush() XRAY_NEVER_INSTRUMENT {
  if (atomic_load(&LoggingStatus, memory_order_acquire) !=
      XRayLogInitStatus::XRAY_LOG_FINALIZED) {
    if (Verbosity())
      Report("Not flushing log, implementation is not finalized.\n");
    return XRayLogFlushStatus::XRAY_LOG_NOT_FLUSHING;
  }

  s32 Result = XRayLogFlushStatus::XRAY_LOG_NOT_FLUSHING;
  if (!atomic_compare_exchange_strong(&LogFlushStatus, &Result,
                                      XRayLogFlushStatus::XRAY_LOG_FLUSHING,
                                      memory_order_release)) {
    if (Verbosity())
      Report("Not flushing log, implementation is still finalizing.\n");
    return static_cast<XRayLogFlushStatus>(Result);
  }

  if (BQ == nullptr) {
    if (Verbosity())
      Report("Cannot flush when global buffer queue is null.\n");
    return XRayLogFlushStatus::XRAY_LOG_NOT_FLUSHING;
  }

  // We wait a number of milliseconds to allow threads to see that we've
  // finalised before attempting to flush the log.
  SleepForMillis(fdrFlags()->grace_period_ms);

  // At this point, we're going to uninstall the iterator implementation, before
  // we decide to do anything further with the global buffer queue.
  __xray_log_remove_buffer_iterator();

  // Once flushed, we should set the global status of the logging implementation
  // to "uninitialized" to allow for FDR-logging multiple runs.
  auto ResetToUnitialized = at_scope_exit([] {
    atomic_store(&LoggingStatus, XRayLogInitStatus::XRAY_LOG_UNINITIALIZED,
                 memory_order_release);
  });

  auto CleanupBuffers = at_scope_exit([] {
    auto &TLD = getThreadLocalData();
    if (TLD.Controller != nullptr)
      TLD.Controller->flush();
  });

  if (fdrFlags()->no_file_flush) {
    if (Verbosity())
      Report("XRay FDR: Not flushing to file, 'no_file_flush=true'.\n");

    atomic_store(&LogFlushStatus, XRayLogFlushStatus::XRAY_LOG_FLUSHED,
                 memory_order_release);
    return XRayLogFlushStatus::XRAY_LOG_FLUSHED;
  }

  // We write out the file in the following format:
  //
  //   1) We write down the XRay file header with version 1, type FDR_LOG.
  //   2) Then we use the 'apply' member of the BufferQueue that's live, to
  //      ensure that at this point in time we write down the buffers that have
  //      been released (and marked "used") -- we dump the full buffer for now
  //      (fixed-sized) and let the tools reading the buffers deal with the data
  //      afterwards.
  //
  LogWriter *LW = LogWriter::Open();
  if (LW == nullptr) {
    auto Result = XRayLogFlushStatus::XRAY_LOG_NOT_FLUSHING;
    atomic_store(&LogFlushStatus, Result, memory_order_release);
    return Result;
  }

  XRayFileHeader Header = fdrCommonHeaderInfo();
  Header.FdrData = FdrAdditionalHeaderData{BQ->ConfiguredBufferSize()};
  LW->WriteAll(reinterpret_cast<char *>(&Header),
               reinterpret_cast<char *>(&Header) + sizeof(Header));

  // Release the current thread's buffer before we attempt to write out all the
  // buffers. This ensures that in case we had only a single thread going, that
  // we are able to capture the data nonetheless.
  auto &TLD = getThreadLocalData();
  if (TLD.Controller != nullptr)
    TLD.Controller->flush();

  BQ->apply([&](const BufferQueue::Buffer &B) {
    // Starting at version 2 of the FDR logging implementation, we only write
    // the records identified by the extents of the buffer. We use the Extents
    // from the Buffer and write that out as the first record in the buffer.  We
    // still use a Metadata record, but fill in the extents instead for the
    // data.
    MetadataRecord ExtentsRecord;
    auto BufferExtents = atomic_load(B.Extents, memory_order_acquire);
    DCHECK(BufferExtents <= B.Size);
    ExtentsRecord.Type = uint8_t(RecordType::Metadata);
    ExtentsRecord.RecordKind =
        uint8_t(MetadataRecord::RecordKinds::BufferExtents);
    internal_memcpy(ExtentsRecord.Data, &BufferExtents, sizeof(BufferExtents));
    if (BufferExtents > 0) {
      LW->WriteAll(reinterpret_cast<char *>(&ExtentsRecord),
                   reinterpret_cast<char *>(&ExtentsRecord) +
                       sizeof(MetadataRecord));
      LW->WriteAll(reinterpret_cast<char *>(B.Data),
                   reinterpret_cast<char *>(B.Data) + BufferExtents);
    }
  });

  atomic_store(&LogFlushStatus, XRayLogFlushStatus::XRAY_LOG_FLUSHED,
               memory_order_release);
  return XRayLogFlushStatus::XRAY_LOG_FLUSHED;
}

XRayLogInitStatus fdrLoggingFinalize() XRAY_NEVER_INSTRUMENT {
  s32 CurrentStatus = XRayLogInitStatus::XRAY_LOG_INITIALIZED;
  if (!atomic_compare_exchange_strong(&LoggingStatus, &CurrentStatus,
                                      XRayLogInitStatus::XRAY_LOG_FINALIZING,
                                      memory_order_release)) {
    if (Verbosity())
      Report("Cannot finalize log, implementation not initialized.\n");
    return static_cast<XRayLogInitStatus>(CurrentStatus);
  }

  // Do special things to make the log finalize itself, and not allow any more
  // operations to be performed until re-initialized.
  if (BQ == nullptr) {
    if (Verbosity())
      Report("Attempting to finalize an uninitialized global buffer!\n");
  } else {
    BQ->finalize();
  }

  atomic_store(&LoggingStatus, XRayLogInitStatus::XRAY_LOG_FINALIZED,
               memory_order_release);
  return XRayLogInitStatus::XRAY_LOG_FINALIZED;
}

struct TSCAndCPU {
  uint64_t TSC = 0;
  unsigned char CPU = 0;
};

static TSCAndCPU getTimestamp() XRAY_NEVER_INSTRUMENT {
  // We want to get the TSC as early as possible, so that we can check whether
  // we've seen this CPU before. We also do it before we load anything else,
  // to allow for forward progress with the scheduling.
  TSCAndCPU Result;

  // Test once for required CPU features
  static pthread_once_t OnceProbe = PTHREAD_ONCE_INIT;
  static bool TSCSupported = true;
  pthread_once(
      &OnceProbe, +[] { TSCSupported = probeRequiredCPUFeatures(); });

  if (TSCSupported) {
    Result.TSC = __xray::readTSC(Result.CPU);
  } else {
    // FIXME: This code needs refactoring as it appears in multiple locations
    timespec TS;
    int result = clock_gettime(CLOCK_REALTIME, &TS);
    if (result != 0) {
      Report("clock_gettime(2) return %d, errno=%d", result, int(errno));
      TS = {0, 0};
    }
    Result.CPU = 0;
    Result.TSC = TS.tv_sec * __xray::NanosecondsPerSecond + TS.tv_nsec;
  }
  return Result;
}

thread_local atomic_uint8_t Running{0};

static bool setupTLD(ThreadLocalData &TLD) XRAY_NEVER_INSTRUMENT {
  // Check if we're finalizing, before proceeding.
  {
    auto Status = atomic_load(&LoggingStatus, memory_order_acquire);
    if (Status == XRayLogInitStatus::XRAY_LOG_FINALIZING ||
        Status == XRayLogInitStatus::XRAY_LOG_FINALIZED) {
      if (TLD.Controller != nullptr) {
        TLD.Controller->flush();
        TLD.Controller = nullptr;
      }
      return false;
    }
  }

  if (UNLIKELY(TLD.Controller == nullptr)) {
    // Set up the TLD buffer queue.
    if (UNLIKELY(BQ == nullptr))
      return false;
    TLD.BQ = BQ;

    // Check that we have a valid buffer.
    if (TLD.Buffer.Generation != BQ->generation() &&
        TLD.BQ->releaseBuffer(TLD.Buffer) != BufferQueue::ErrorCode::Ok)
      return false;

    // Set up a buffer, before setting up the log writer. Bail out on failure.
    if (TLD.BQ->getBuffer(TLD.Buffer) != BufferQueue::ErrorCode::Ok)
      return false;

    // Set up the Log Writer for this thread.
    if (UNLIKELY(TLD.Writer == nullptr)) {
      auto *LWStorage = reinterpret_cast<FDRLogWriter *>(&TLD.LWStorage);
      new (LWStorage) FDRLogWriter(TLD.Buffer);
      TLD.Writer = LWStorage;
    } else {
      TLD.Writer->resetRecord();
    }

    auto *CStorage = reinterpret_cast<FDRController<> *>(&TLD.CStorage);
    new (CStorage)
        FDRController<>(TLD.BQ, TLD.Buffer, *TLD.Writer, clock_gettime,
                        atomic_load_relaxed(&ThresholdTicks));
    TLD.Controller = CStorage;
  }

  DCHECK_NE(TLD.Controller, nullptr);
  return true;
}

void fdrLoggingHandleArg0(int32_t FuncId,
                          XRayEntryType Entry) XRAY_NEVER_INSTRUMENT {
  auto TC = getTimestamp();
  auto &TSC = TC.TSC;
  auto &CPU = TC.CPU;
  RecursionGuard Guard{Running};
  if (!Guard)
    return;

  auto &TLD = getThreadLocalData();
  if (!setupTLD(TLD))
    return;

  switch (Entry) {
  case XRayEntryType::ENTRY:
  case XRayEntryType::LOG_ARGS_ENTRY:
    TLD.Controller->functionEnter(FuncId, TSC, CPU);
    return;
  case XRayEntryType::EXIT:
    TLD.Controller->functionExit(FuncId, TSC, CPU);
    return;
  case XRayEntryType::TAIL:
    TLD.Controller->functionTailExit(FuncId, TSC, CPU);
    return;
  case XRayEntryType::CUSTOM_EVENT:
  case XRayEntryType::TYPED_EVENT:
    break;
  }
}

void fdrLoggingHandleArg1(int32_t FuncId, XRayEntryType Entry,
                          uint64_t Arg) XRAY_NEVER_INSTRUMENT {
  auto TC = getTimestamp();
  auto &TSC = TC.TSC;
  auto &CPU = TC.CPU;
  RecursionGuard Guard{Running};
  if (!Guard)
    return;

  auto &TLD = getThreadLocalData();
  if (!setupTLD(TLD))
    return;

  switch (Entry) {
  case XRayEntryType::ENTRY:
  case XRayEntryType::LOG_ARGS_ENTRY:
    TLD.Controller->functionEnterArg(FuncId, TSC, CPU, Arg);
    return;
  case XRayEntryType::EXIT:
    TLD.Controller->functionExit(FuncId, TSC, CPU);
    return;
  case XRayEntryType::TAIL:
    TLD.Controller->functionTailExit(FuncId, TSC, CPU);
    return;
  case XRayEntryType::CUSTOM_EVENT:
  case XRayEntryType::TYPED_EVENT:
    break;
  }
}

void fdrLoggingHandleCustomEvent(void *Event,
                                 std::size_t EventSize) XRAY_NEVER_INSTRUMENT {
  auto TC = getTimestamp();
  auto &TSC = TC.TSC;
  auto &CPU = TC.CPU;
  RecursionGuard Guard{Running};
  if (!Guard)
    return;

  // Complain when we ever get at least one custom event that's larger than what
  // we can possibly support.
  if (EventSize >
      static_cast<std::size_t>(std::numeric_limits<int32_t>::max())) {
    static pthread_once_t Once = PTHREAD_ONCE_INIT;
    pthread_once(
        &Once, +[] {
          Report("Custom event size too large; truncating to %d.\n",
                 std::numeric_limits<int32_t>::max());
        });
  }

  auto &TLD = getThreadLocalData();
  if (!setupTLD(TLD))
    return;

  int32_t ReducedEventSize = static_cast<int32_t>(EventSize);
  TLD.Controller->customEvent(TSC, CPU, Event, ReducedEventSize);
}

void fdrLoggingHandleTypedEvent(
    uint16_t EventType, const void *Event,
    std::size_t EventSize) noexcept XRAY_NEVER_INSTRUMENT {
  auto TC = getTimestamp();
  auto &TSC = TC.TSC;
  auto &CPU = TC.CPU;
  RecursionGuard Guard{Running};
  if (!Guard)
    return;

  // Complain when we ever get at least one typed event that's larger than what
  // we can possibly support.
  if (EventSize >
      static_cast<std::size_t>(std::numeric_limits<int32_t>::max())) {
    static pthread_once_t Once = PTHREAD_ONCE_INIT;
    pthread_once(
        &Once, +[] {
          Report("Typed event size too large; truncating to %d.\n",
                 std::numeric_limits<int32_t>::max());
        });
  }

  auto &TLD = getThreadLocalData();
  if (!setupTLD(TLD))
    return;

  int32_t ReducedEventSize = static_cast<int32_t>(EventSize);
  TLD.Controller->typedEvent(TSC, CPU, EventType, Event, ReducedEventSize);
}

XRayLogInitStatus fdrLoggingInit(size_t, size_t, void *Options,
                                 size_t OptionsSize) XRAY_NEVER_INSTRUMENT {
  if (Options == nullptr)
    return XRayLogInitStatus::XRAY_LOG_UNINITIALIZED;

  s32 CurrentStatus = XRayLogInitStatus::XRAY_LOG_UNINITIALIZED;
  if (!atomic_compare_exchange_strong(&LoggingStatus, &CurrentStatus,
                                      XRayLogInitStatus::XRAY_LOG_INITIALIZING,
                                      memory_order_release)) {
    if (Verbosity())
      Report("Cannot initialize already initialized implementation.\n");
    return static_cast<XRayLogInitStatus>(CurrentStatus);
  }

  if (Verbosity())
    Report("Initializing FDR mode with options: %s\n",
           static_cast<const char *>(Options));

  // TODO: Factor out the flags specific to the FDR mode implementation. For
  // now, use the global/single definition of the flags, since the FDR mode
  // flags are already defined there.
  FlagParser FDRParser;
  FDRFlags FDRFlags;
  registerXRayFDRFlags(&FDRParser, &FDRFlags);
  FDRFlags.setDefaults();

  // Override first from the general XRAY_DEFAULT_OPTIONS compiler-provided
  // options until we migrate everyone to use the XRAY_FDR_OPTIONS
  // compiler-provided options.
  FDRParser.ParseString(useCompilerDefinedFlags());
  FDRParser.ParseString(useCompilerDefinedFDRFlags());
  auto *EnvOpts = GetEnv("XRAY_FDR_OPTIONS");
  if (EnvOpts == nullptr)
    EnvOpts = "";
  FDRParser.ParseString(EnvOpts);

  // FIXME: Remove this when we fully remove the deprecated flags.
  if (internal_strlen(EnvOpts) == 0) {
    FDRFlags.func_duration_threshold_us =
        flags()->xray_fdr_log_func_duration_threshold_us;
    FDRFlags.grace_period_ms = flags()->xray_fdr_log_grace_period_ms;
  }

  // The provided options should always override the compiler-provided and
  // environment-variable defined options.
  FDRParser.ParseString(static_cast<const char *>(Options));
  *fdrFlags() = FDRFlags;
  auto BufferSize = FDRFlags.buffer_size;
  auto BufferMax = FDRFlags.buffer_max;

  if (BQ == nullptr) {
    bool Success = false;
    BQ = reinterpret_cast<BufferQueue *>(&BufferQueueStorage);
    new (BQ) BufferQueue(BufferSize, BufferMax, Success);
    if (!Success) {
      Report("BufferQueue init failed.\n");
      return XRayLogInitStatus::XRAY_LOG_UNINITIALIZED;
    }
  } else {
    if (BQ->init(BufferSize, BufferMax) != BufferQueue::ErrorCode::Ok) {
      if (Verbosity())
        Report("Failed to re-initialize global buffer queue. Init failed.\n");
      return XRayLogInitStatus::XRAY_LOG_UNINITIALIZED;
    }
  }

  static pthread_once_t OnceInit = PTHREAD_ONCE_INIT;
  pthread_once(
      &OnceInit, +[] {
        atomic_store(&TicksPerSec,
                     probeRequiredCPUFeatures() ? getTSCFrequency()
                                                : __xray::NanosecondsPerSecond,
                     memory_order_release);
        pthread_key_create(
            &Key, +[](void *TLDPtr) {
              if (TLDPtr == nullptr)
                return;
              auto &TLD = *reinterpret_cast<ThreadLocalData *>(TLDPtr);
              if (TLD.BQ == nullptr)
                return;
              if (TLD.Buffer.Data == nullptr)
                return;
              auto EC = TLD.BQ->releaseBuffer(TLD.Buffer);
              if (EC != BufferQueue::ErrorCode::Ok)
                Report("At thread exit, failed to release buffer at %p; "
                       "error=%s\n",
                       TLD.Buffer.Data, BufferQueue::getErrorString(EC));
            });
      });

  atomic_store(&ThresholdTicks,
               atomic_load_relaxed(&TicksPerSec) *
                   fdrFlags()->func_duration_threshold_us / 1000000,
               memory_order_release);
  // Arg1 handler should go in first to avoid concurrent code accidentally
  // falling back to arg0 when it should have ran arg1.
  __xray_set_handler_arg1(fdrLoggingHandleArg1);
  // Install the actual handleArg0 handler after initialising the buffers.
  __xray_set_handler(fdrLoggingHandleArg0);
  __xray_set_customevent_handler(fdrLoggingHandleCustomEvent);
  __xray_set_typedevent_handler(fdrLoggingHandleTypedEvent);

  // Install the buffer iterator implementation.
  __xray_log_set_buffer_iterator(fdrIterator);

  atomic_store(&LoggingStatus, XRayLogInitStatus::XRAY_LOG_INITIALIZED,
               memory_order_release);

  if (Verbosity())
    Report("XRay FDR init successful.\n");
  return XRayLogInitStatus::XRAY_LOG_INITIALIZED;
}

bool fdrLogDynamicInitializer() XRAY_NEVER_INSTRUMENT {
  XRayLogImpl Impl{
      fdrLoggingInit,
      fdrLoggingFinalize,
      fdrLoggingHandleArg0,
      fdrLoggingFlush,
  };
  auto RegistrationResult = __xray_log_register_mode("xray-fdr", Impl);
  if (RegistrationResult != XRayLogRegisterStatus::XRAY_REGISTRATION_OK &&
      Verbosity()) {
    Report("Cannot register XRay FDR mode to 'xray-fdr'; error = %d\n",
           RegistrationResult);
    return false;
  }

  if (flags()->xray_fdr_log ||
      !internal_strcmp(flags()->xray_mode, "xray-fdr")) {
    auto SelectResult = __xray_log_select_mode("xray-fdr");
    if (SelectResult != XRayLogRegisterStatus::XRAY_REGISTRATION_OK &&
        Verbosity()) {
      Report("Cannot select XRay FDR mode as 'xray-fdr'; error = %d\n",
             SelectResult);
      return false;
    }
  }
  return true;
}

} // namespace __xray

static auto UNUSED Unused = __xray::fdrLogDynamicInitializer();
