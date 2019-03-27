//===-- tsan_stat.cc ------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
//===----------------------------------------------------------------------===//
#include "tsan_stat.h"
#include "tsan_rtl.h"

namespace __tsan {

#if TSAN_COLLECT_STATS

void StatAggregate(u64 *dst, u64 *src) {
  for (int i = 0; i < StatCnt; i++)
    dst[i] += src[i];
}

void StatOutput(u64 *stat) {
  stat[StatShadowNonZero] = stat[StatShadowProcessed] - stat[StatShadowZero];

  static const char *name[StatCnt] = {};
  name[StatMop]                          = "Memory accesses                   ";
  name[StatMopRead]                      = "  Including reads                 ";
  name[StatMopWrite]                     = "            writes                ";
  name[StatMop1]                         = "  Including size 1                ";
  name[StatMop2]                         = "            size 2                ";
  name[StatMop4]                         = "            size 4                ";
  name[StatMop8]                         = "            size 8                ";
  name[StatMopSame]                      = "  Including same                  ";
  name[StatMopIgnored]                   = "  Including ignored               ";
  name[StatMopRange]                     = "  Including range                 ";
  name[StatMopRodata]                    = "  Including .rodata               ";
  name[StatMopRangeRodata]               = "  Including .rodata range         ";
  name[StatShadowProcessed]              = "Shadow processed                  ";
  name[StatShadowZero]                   = "  Including empty                 ";
  name[StatShadowNonZero]                = "  Including non empty             ";
  name[StatShadowSameSize]               = "  Including same size             ";
  name[StatShadowIntersect]              = "            intersect             ";
  name[StatShadowNotIntersect]           = "            not intersect         ";
  name[StatShadowSameThread]             = "  Including same thread           ";
  name[StatShadowAnotherThread]          = "            another thread        ";
  name[StatShadowReplace]                = "  Including evicted               ";

  name[StatFuncEnter]                    = "Function entries                  ";
  name[StatFuncExit]                     = "Function exits                    ";
  name[StatEvents]                       = "Events collected                  ";

  name[StatThreadCreate]                 = "Total threads created             ";
  name[StatThreadFinish]                 = "  threads finished                ";
  name[StatThreadReuse]                  = "  threads reused                  ";
  name[StatThreadMaxTid]                 = "  max tid                         ";
  name[StatThreadMaxAlive]               = "  max alive threads               ";

  name[StatMutexCreate]                  = "Mutexes created                   ";
  name[StatMutexDestroy]                 = "  destroyed                       ";
  name[StatMutexLock]                    = "  lock                            ";
  name[StatMutexUnlock]                  = "  unlock                          ";
  name[StatMutexRecLock]                 = "  recursive lock                  ";
  name[StatMutexRecUnlock]               = "  recursive unlock                ";
  name[StatMutexReadLock]                = "  read lock                       ";
  name[StatMutexReadUnlock]              = "  read unlock                     ";

  name[StatSyncCreated]                  = "Sync objects created              ";
  name[StatSyncDestroyed]                = "             destroyed            ";
  name[StatSyncAcquire]                  = "             acquired             ";
  name[StatSyncRelease]                  = "             released             ";

  name[StatClockAcquire]                 = "Clock acquire                     ";
  name[StatClockAcquireEmpty]            = "  empty clock                     ";
  name[StatClockAcquireFastRelease]      = "  fast from release-store         ";
  name[StatClockAcquireFull]             = "  full (slow)                     ";
  name[StatClockAcquiredSomething]       = "  acquired something              ";
  name[StatClockRelease]                 = "Clock release                     ";
  name[StatClockReleaseResize]           = "  resize                          ";
  name[StatClockReleaseFast]             = "  fast                            ";
  name[StatClockReleaseSlow]             = "  dirty overflow (slow)           ";
  name[StatClockReleaseFull]             = "  full (slow)                     ";
  name[StatClockReleaseAcquired]         = "  was acquired                    ";
  name[StatClockReleaseClearTail]        = "  clear tail                      ";
  name[StatClockStore]                   = "Clock release store               ";
  name[StatClockStoreResize]             = "  resize                          ";
  name[StatClockStoreFast]               = "  fast                            ";
  name[StatClockStoreFull]               = "  slow                            ";
  name[StatClockStoreTail]               = "  clear tail                      ";
  name[StatClockAcquireRelease]          = "Clock acquire-release             ";

  name[StatAtomic]                       = "Atomic operations                 ";
  name[StatAtomicLoad]                   = "  Including load                  ";
  name[StatAtomicStore]                  = "            store                 ";
  name[StatAtomicExchange]               = "            exchange              ";
  name[StatAtomicFetchAdd]               = "            fetch_add             ";
  name[StatAtomicFetchSub]               = "            fetch_sub             ";
  name[StatAtomicFetchAnd]               = "            fetch_and             ";
  name[StatAtomicFetchOr]                = "            fetch_or              ";
  name[StatAtomicFetchXor]               = "            fetch_xor             ";
  name[StatAtomicFetchNand]              = "            fetch_nand            ";
  name[StatAtomicCAS]                    = "            compare_exchange      ";
  name[StatAtomicFence]                  = "            fence                 ";
  name[StatAtomicRelaxed]                = "  Including relaxed               ";
  name[StatAtomicConsume]                = "            consume               ";
  name[StatAtomicAcquire]                = "            acquire               ";
  name[StatAtomicRelease]                = "            release               ";
  name[StatAtomicAcq_Rel]                = "            acq_rel               ";
  name[StatAtomicSeq_Cst]                = "            seq_cst               ";
  name[StatAtomic1]                      = "  Including size 1                ";
  name[StatAtomic2]                      = "            size 2                ";
  name[StatAtomic4]                      = "            size 4                ";
  name[StatAtomic8]                      = "            size 8                ";
  name[StatAtomic16]                     = "            size 16               ";

  name[StatAnnotation]                   = "Dynamic annotations               ";
  name[StatAnnotateHappensBefore]        = "  HappensBefore                   ";
  name[StatAnnotateHappensAfter]         = "  HappensAfter                    ";
  name[StatAnnotateCondVarSignal]        = "  CondVarSignal                   ";
  name[StatAnnotateCondVarSignalAll]     = "  CondVarSignalAll                ";
  name[StatAnnotateMutexIsNotPHB]        = "  MutexIsNotPHB                   ";
  name[StatAnnotateCondVarWait]          = "  CondVarWait                     ";
  name[StatAnnotateRWLockCreate]         = "  RWLockCreate                    ";
  name[StatAnnotateRWLockCreateStatic]   = "  StatAnnotateRWLockCreateStatic  ";
  name[StatAnnotateRWLockDestroy]        = "  RWLockDestroy                   ";
  name[StatAnnotateRWLockAcquired]       = "  RWLockAcquired                  ";
  name[StatAnnotateRWLockReleased]       = "  RWLockReleased                  ";
  name[StatAnnotateTraceMemory]          = "  TraceMemory                     ";
  name[StatAnnotateFlushState]           = "  FlushState                      ";
  name[StatAnnotateNewMemory]            = "  NewMemory                       ";
  name[StatAnnotateNoOp]                 = "  NoOp                            ";
  name[StatAnnotateFlushExpectedRaces]   = "  FlushExpectedRaces              ";
  name[StatAnnotateEnableRaceDetection]  = "  EnableRaceDetection             ";
  name[StatAnnotateMutexIsUsedAsCondVar] = "  MutexIsUsedAsCondVar            ";
  name[StatAnnotatePCQGet]               = "  PCQGet                          ";
  name[StatAnnotatePCQPut]               = "  PCQPut                          ";
  name[StatAnnotatePCQDestroy]           = "  PCQDestroy                      ";
  name[StatAnnotatePCQCreate]            = "  PCQCreate                       ";
  name[StatAnnotateExpectRace]           = "  ExpectRace                      ";
  name[StatAnnotateBenignRaceSized]      = "  BenignRaceSized                 ";
  name[StatAnnotateBenignRace]           = "  BenignRace                      ";
  name[StatAnnotateIgnoreReadsBegin]     = "  IgnoreReadsBegin                ";
  name[StatAnnotateIgnoreReadsEnd]       = "  IgnoreReadsEnd                  ";
  name[StatAnnotateIgnoreWritesBegin]    = "  IgnoreWritesBegin               ";
  name[StatAnnotateIgnoreWritesEnd]      = "  IgnoreWritesEnd                 ";
  name[StatAnnotateIgnoreSyncBegin]      = "  IgnoreSyncBegin                 ";
  name[StatAnnotateIgnoreSyncEnd]        = "  IgnoreSyncEnd                   ";
  name[StatAnnotatePublishMemoryRange]   = "  PublishMemoryRange              ";
  name[StatAnnotateUnpublishMemoryRange] = "  UnpublishMemoryRange            ";
  name[StatAnnotateThreadName]           = "  ThreadName                      ";
  name[Stat__tsan_mutex_create]          = "  __tsan_mutex_create             ";
  name[Stat__tsan_mutex_destroy]         = "  __tsan_mutex_destroy            ";
  name[Stat__tsan_mutex_pre_lock]        = "  __tsan_mutex_pre_lock           ";
  name[Stat__tsan_mutex_post_lock]       = "  __tsan_mutex_post_lock          ";
  name[Stat__tsan_mutex_pre_unlock]      = "  __tsan_mutex_pre_unlock         ";
  name[Stat__tsan_mutex_post_unlock]     = "  __tsan_mutex_post_unlock        ";
  name[Stat__tsan_mutex_pre_signal]      = "  __tsan_mutex_pre_signal         ";
  name[Stat__tsan_mutex_post_signal]     = "  __tsan_mutex_post_signal        ";
  name[Stat__tsan_mutex_pre_divert]      = "  __tsan_mutex_pre_divert         ";
  name[Stat__tsan_mutex_post_divert]     = "  __tsan_mutex_post_divert        ";

  name[StatMtxTotal]                     = "Contentionz                       ";
  name[StatMtxTrace]                     = "  Trace                           ";
  name[StatMtxThreads]                   = "  Threads                         ";
  name[StatMtxReport]                    = "  Report                          ";
  name[StatMtxSyncVar]                   = "  SyncVar                         ";
  name[StatMtxSyncTab]                   = "  SyncTab                         ";
  name[StatMtxSlab]                      = "  Slab                            ";
  name[StatMtxAtExit]                    = "  Atexit                          ";
  name[StatMtxAnnotations]               = "  Annotations                     ";
  name[StatMtxMBlock]                    = "  MBlock                          ";
  name[StatMtxDeadlockDetector]          = "  DeadlockDetector                ";
  name[StatMtxFired]                     = "  FiredSuppressions               ";
  name[StatMtxRacy]                      = "  RacyStacks                      ";
  name[StatMtxFD]                        = "  FD                              ";
  name[StatMtxGlobalProc]                = "  GlobalProc                      ";

  Printf("Statistics:\n");
  for (int i = 0; i < StatCnt; i++)
    Printf("%s: %16zu\n", name[i], (uptr)stat[i]);
}

#endif

}  // namespace __tsan
