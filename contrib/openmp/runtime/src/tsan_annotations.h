/*! \file */
/*
 * tsan_annotations.h -- ThreadSanitizer annotations to support data
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

#ifndef TSAN_ANNOTATIONS_H
#define TSAN_ANNOTATIONS_H

#include "kmp_config.h"

/* types as used in tsan/rtl/tsan_interface_ann.cc */
typedef unsigned long uptr;
typedef signed long sptr;

#ifdef __cplusplus
extern "C" {
#endif

/* Declaration of all annotation functions in tsan/rtl/tsan_interface_ann.cc */
void AnnotateHappensBefore(const char *f, int l, uptr addr);
void AnnotateHappensAfter(const char *f, int l, uptr addr);
void AnnotateCondVarSignal(const char *f, int l, uptr cv);
void AnnotateCondVarSignalAll(const char *f, int l, uptr cv);
void AnnotateMutexIsNotPHB(const char *f, int l, uptr mu);
void AnnotateCondVarWait(const char *f, int l, uptr cv, uptr lock);
void AnnotateRWLockCreate(const char *f, int l, uptr m);
void AnnotateRWLockCreateStatic(const char *f, int l, uptr m);
void AnnotateRWLockDestroy(const char *f, int l, uptr m);
void AnnotateRWLockAcquired(const char *f, int l, uptr m, uptr is_w);
void AnnotateRWLockReleased(const char *f, int l, uptr m, uptr is_w);
void AnnotateTraceMemory(const char *f, int l, uptr mem);
void AnnotateFlushState(const char *f, int l);
void AnnotateNewMemory(const char *f, int l, uptr mem, uptr size);
void AnnotateNoOp(const char *f, int l, uptr mem);
void AnnotateFlushExpectedRaces(const char *f, int l);
void AnnotateEnableRaceDetection(const char *f, int l, int enable);
void AnnotateMutexIsUsedAsCondVar(const char *f, int l, uptr mu);
void AnnotatePCQGet(const char *f, int l, uptr pcq);
void AnnotatePCQPut(const char *f, int l, uptr pcq);
void AnnotatePCQDestroy(const char *f, int l, uptr pcq);
void AnnotatePCQCreate(const char *f, int l, uptr pcq);
void AnnotateExpectRace(const char *f, int l, uptr mem, char *desc);
void AnnotateBenignRaceSized(const char *f, int l, uptr mem, uptr size,
                             char *desc);
void AnnotateBenignRace(const char *f, int l, uptr mem, char *desc);
void AnnotateIgnoreReadsBegin(const char *f, int l);
void AnnotateIgnoreReadsEnd(const char *f, int l);
void AnnotateIgnoreWritesBegin(const char *f, int l);
void AnnotateIgnoreWritesEnd(const char *f, int l);
void AnnotateIgnoreSyncBegin(const char *f, int l);
void AnnotateIgnoreSyncEnd(const char *f, int l);
void AnnotatePublishMemoryRange(const char *f, int l, uptr addr, uptr size);
void AnnotateUnpublishMemoryRange(const char *f, int l, uptr addr, uptr size);
void AnnotateThreadName(const char *f, int l, char *name);
void WTFAnnotateHappensBefore(const char *f, int l, uptr addr);
void WTFAnnotateHappensAfter(const char *f, int l, uptr addr);
void WTFAnnotateBenignRaceSized(const char *f, int l, uptr mem, uptr sz,
                                char *desc);
int RunningOnValgrind();
double ValgrindSlowdown(void);
const char *ThreadSanitizerQuery(const char *query);
void AnnotateMemoryIsInitialized(const char *f, int l, uptr mem, uptr sz);

#ifdef __cplusplus
}
#endif

#ifdef TSAN_SUPPORT
#define ANNOTATE_HAPPENS_AFTER(addr)                                           \
  AnnotateHappensAfter(__FILE__, __LINE__, (uptr)addr)
#define ANNOTATE_HAPPENS_BEFORE(addr)                                          \
  AnnotateHappensBefore(__FILE__, __LINE__, (uptr)addr)
#define ANNOTATE_IGNORE_WRITES_BEGIN()                                         \
  AnnotateIgnoreWritesBegin(__FILE__, __LINE__)
#define ANNOTATE_IGNORE_WRITES_END() AnnotateIgnoreWritesEnd(__FILE__, __LINE__)
#define ANNOTATE_RWLOCK_CREATE(lck)                                            \
  AnnotateRWLockCreate(__FILE__, __LINE__, (uptr)lck)
#define ANNOTATE_RWLOCK_RELEASED(lck)                                          \
  AnnotateRWLockAcquired(__FILE__, __LINE__, (uptr)lck, 1)
#define ANNOTATE_RWLOCK_ACQUIRED(lck)                                          \
  AnnotateRWLockReleased(__FILE__, __LINE__, (uptr)lck, 1)
#define ANNOTATE_BARRIER_BEGIN(addr)                                           \
  AnnotateHappensBefore(__FILE__, __LINE__, (uptr)addr)
#define ANNOTATE_BARRIER_END(addr)                                             \
  AnnotateHappensAfter(__FILE__, __LINE__, (uptr)addr)
#define ANNOTATE_REDUCE_AFTER(addr)                                            \
  AnnotateHappensAfter(__FILE__, __LINE__, (uptr)addr)
#define ANNOTATE_REDUCE_BEFORE(addr)                                           \
  AnnotateHappensBefore(__FILE__, __LINE__, (uptr)addr)
#else
#define ANNOTATE_HAPPENS_AFTER(addr)
#define ANNOTATE_HAPPENS_BEFORE(addr)
#define ANNOTATE_IGNORE_WRITES_BEGIN()
#define ANNOTATE_IGNORE_WRITES_END()
#define ANNOTATE_RWLOCK_CREATE(lck)
#define ANNOTATE_RWLOCK_RELEASED(lck)
#define ANNOTATE_RWLOCK_ACQUIRED(lck)
#define ANNOTATE_BARRIER_BEGIN(addr)
#define ANNOTATE_BARRIER_END(addr)
#define ANNOTATE_REDUCE_AFTER(addr)
#define ANNOTATE_REDUCE_BEFORE(addr)
#endif

#define ANNOTATE_QUEUING
#define ANNOTATE_TICKET
#define ANNOTATE_FUTEX
#define ANNOTATE_TAS
#define ANNOTATE_DRDPA

#ifdef ANNOTATE_QUEUING
#define ANNOTATE_QUEUING_CREATE(lck)
#define ANNOTATE_QUEUING_RELEASED(lck) ANNOTATE_HAPPENS_BEFORE(lck)
#define ANNOTATE_QUEUING_ACQUIRED(lck) ANNOTATE_HAPPENS_AFTER(lck)
#else
#define ANNOTATE_QUEUING_CREATE(lck)
#define ANNOTATE_QUEUING_RELEASED(lck)
#define ANNOTATE_QUEUING_ACQUIRED(lck)
#endif

#ifdef ANNOTATE_TICKET
#define ANNOTATE_TICKET_CREATE(lck)
#define ANNOTATE_TICKET_RELEASED(lck) ANNOTATE_HAPPENS_BEFORE(lck)
#define ANNOTATE_TICKET_ACQUIRED(lck) ANNOTATE_HAPPENS_AFTER(lck)
#else
#define ANNOTATE_TICKET_CREATE(lck)
#define ANNOTATE_TICKET_RELEASED(lck)
#define ANNOTATE_TICKET_ACQUIRED(lck)
#endif

#ifdef ANNOTATE_FUTEX
#define ANNOTATE_FUTEX_CREATE(lck)
#define ANNOTATE_FUTEX_RELEASED(lck) ANNOTATE_HAPPENS_BEFORE(lck)
#define ANNOTATE_FUTEX_ACQUIRED(lck) ANNOTATE_HAPPENS_AFTER(lck)
#else
#define ANNOTATE_FUTEX_CREATE(lck)
#define ANNOTATE_FUTEX_RELEASED(lck)
#define ANNOTATE_FUTEX_ACQUIRED(lck)
#endif

#ifdef ANNOTATE_TAS
#define ANNOTATE_TAS_CREATE(lck)
#define ANNOTATE_TAS_RELEASED(lck) ANNOTATE_HAPPENS_BEFORE(lck)
#define ANNOTATE_TAS_ACQUIRED(lck) ANNOTATE_HAPPENS_AFTER(lck)
#else
#define ANNOTATE_TAS_CREATE(lck)
#define ANNOTATE_TAS_RELEASED(lck)
#define ANNOTATE_TAS_ACQUIRED(lck)
#endif

#ifdef ANNOTATE_DRDPA
#define ANNOTATE_DRDPA_CREATE(lck)
#define ANNOTATE_DRDPA_RELEASED(lck) ANNOTATE_HAPPENS_BEFORE(lck)
#define ANNOTATE_DRDPA_ACQUIRED(lck) ANNOTATE_HAPPENS_AFTER(lck)
#else
#define ANNOTATE_DRDPA_CREATE(lck)
#define ANNOTATE_DRDPA_RELEASED(lck)
#define ANNOTATE_DRDPA_ACQUIRED(lck)
#endif

#endif
