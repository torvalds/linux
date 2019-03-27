/*
 * tsan_annotations.cpp -- ThreadSanitizer annotations to support data
 * race detection in OpenMP programs.
 */

//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//

#include "tsan_annotations.h"

#include <stdio.h>

typedef unsigned long uptr;
typedef signed long sptr;

extern "C" __attribute__((weak)) void AnnotateHappensBefore(const char *f,
                                                            int l, uptr addr) {}
extern "C" __attribute__((weak)) void AnnotateHappensAfter(const char *f, int l,
                                                           uptr addr) {}
extern "C" __attribute__((weak)) void AnnotateCondVarSignal(const char *f,
                                                            int l, uptr cv) {}
extern "C" __attribute__((weak)) void AnnotateCondVarSignalAll(const char *f,
                                                               int l, uptr cv) {
}
extern "C" __attribute__((weak)) void AnnotateMutexIsNotPHB(const char *f,
                                                            int l, uptr mu) {}
extern "C" __attribute__((weak)) void AnnotateCondVarWait(const char *f, int l,
                                                          uptr cv, uptr lock) {}
extern "C" __attribute__((weak)) void AnnotateRWLockCreate(const char *f, int l,
                                                           uptr m) {}
extern "C" __attribute__((weak)) void
AnnotateRWLockCreateStatic(const char *f, int l, uptr m) {}
extern "C" __attribute__((weak)) void AnnotateRWLockDestroy(const char *f,
                                                            int l, uptr m) {}
extern "C" __attribute__((weak)) void
AnnotateRWLockAcquired(const char *f, int l, uptr m, uptr is_w) {}
extern "C" __attribute__((weak)) void
AnnotateRWLockReleased(const char *f, int l, uptr m, uptr is_w) {}
extern "C" __attribute__((weak)) void AnnotateTraceMemory(const char *f, int l,
                                                          uptr mem) {}
extern "C" __attribute__((weak)) void AnnotateFlushState(const char *f, int l) {
}
extern "C" __attribute__((weak)) void AnnotateNewMemory(const char *f, int l,
                                                        uptr mem, uptr size) {}
extern "C" __attribute__((weak)) void AnnotateNoOp(const char *f, int l,
                                                   uptr mem) {}
extern "C" __attribute__((weak)) void AnnotateFlushExpectedRaces(const char *f,
                                                                 int l) {}
extern "C" __attribute__((weak)) void
AnnotateEnableRaceDetection(const char *f, int l, int enable) {}
extern "C" __attribute__((weak)) void
AnnotateMutexIsUsedAsCondVar(const char *f, int l, uptr mu) {}
extern "C" __attribute__((weak)) void AnnotatePCQGet(const char *f, int l,
                                                     uptr pcq) {}
extern "C" __attribute__((weak)) void AnnotatePCQPut(const char *f, int l,
                                                     uptr pcq) {}
extern "C" __attribute__((weak)) void AnnotatePCQDestroy(const char *f, int l,
                                                         uptr pcq) {}
extern "C" __attribute__((weak)) void AnnotatePCQCreate(const char *f, int l,
                                                        uptr pcq) {}
extern "C" __attribute__((weak)) void AnnotateExpectRace(const char *f, int l,
                                                         uptr mem, char *desc) {
}
extern "C" __attribute__((weak)) void
AnnotateBenignRaceSized(const char *f, int l, uptr mem, uptr size, char *desc) {
}
extern "C" __attribute__((weak)) void AnnotateBenignRace(const char *f, int l,
                                                         uptr mem, char *desc) {
}
extern "C" __attribute__((weak)) void AnnotateIgnoreReadsBegin(const char *f,
                                                               int l) {}
extern "C" __attribute__((weak)) void AnnotateIgnoreReadsEnd(const char *f,
                                                             int l) {}
extern "C" __attribute__((weak)) void AnnotateIgnoreWritesBegin(const char *f,
                                                                int l) {}
extern "C" __attribute__((weak)) void AnnotateIgnoreWritesEnd(const char *f,
                                                              int l) {}
extern "C" __attribute__((weak)) void AnnotateIgnoreSyncBegin(const char *f,
                                                              int l) {}
extern "C" __attribute__((weak)) void AnnotateIgnoreSyncEnd(const char *f,
                                                            int l) {}
extern "C" __attribute__((weak)) void
AnnotatePublishMemoryRange(const char *f, int l, uptr addr, uptr size) {}
extern "C" __attribute__((weak)) void
AnnotateUnpublishMemoryRange(const char *f, int l, uptr addr, uptr size) {}
extern "C" __attribute__((weak)) void AnnotateThreadName(const char *f, int l,
                                                         char *name) {}
extern "C" __attribute__((weak)) void
WTFAnnotateHappensBefore(const char *f, int l, uptr addr) {}
extern "C" __attribute__((weak)) void
WTFAnnotateHappensAfter(const char *f, int l, uptr addr) {}
extern "C" __attribute__((weak)) void
WTFAnnotateBenignRaceSized(const char *f, int l, uptr mem, uptr sz,
                           char *desc) {}
extern "C" __attribute__((weak)) int RunningOnValgrind() { return 0; }
extern "C" __attribute__((weak)) double ValgrindSlowdown(void) { return 0; }
extern "C" __attribute__((weak)) const char __attribute__((weak)) *
    ThreadSanitizerQuery(const char *query) {
  return 0;
}
extern "C" __attribute__((weak)) void
AnnotateMemoryIsInitialized(const char *f, int l, uptr mem, uptr sz) {}
