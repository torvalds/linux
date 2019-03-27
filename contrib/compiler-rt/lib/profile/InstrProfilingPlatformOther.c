/*===- InstrProfilingPlatformOther.c - Profile data default platform ------===*\
|*
|*                     The LLVM Compiler Infrastructure
|*
|* This file is distributed under the University of Illinois Open Source
|* License. See LICENSE.TXT for details.
|*
\*===----------------------------------------------------------------------===*/

#if !defined(__APPLE__) && !defined(__linux__) && !defined(__FreeBSD__) && \
    !(defined(__sun__) && defined(__svr4__)) && !defined(__NetBSD__)

#include <stdlib.h>

#include "InstrProfiling.h"

static const __llvm_profile_data *DataFirst = NULL;
static const __llvm_profile_data *DataLast = NULL;
static const char *NamesFirst = NULL;
static const char *NamesLast = NULL;
static uint64_t *CountersFirst = NULL;
static uint64_t *CountersLast = NULL;

static const void *getMinAddr(const void *A1, const void *A2) {
  return A1 < A2 ? A1 : A2;
}

static const void *getMaxAddr(const void *A1, const void *A2) {
  return A1 > A2 ? A1 : A2;
}

/*!
 * \brief Register an instrumented function.
 *
 * Calls to this are emitted by clang with -fprofile-instr-generate.  Such
 * calls are only required (and only emitted) on targets where we haven't
 * implemented linker magic to find the bounds of the sections.
 */
COMPILER_RT_VISIBILITY
void __llvm_profile_register_function(void *Data_) {
  /* TODO: Only emit this function if we can't use linker magic. */
  const __llvm_profile_data *Data = (__llvm_profile_data *)Data_;
  if (!DataFirst) {
    DataFirst = Data;
    DataLast = Data + 1;
    CountersFirst = Data->CounterPtr;
    CountersLast = (uint64_t *)Data->CounterPtr + Data->NumCounters;
    return;
  }

  DataFirst = (const __llvm_profile_data *)getMinAddr(DataFirst, Data);
  CountersFirst = (uint64_t *)getMinAddr(CountersFirst, Data->CounterPtr);

  DataLast = (const __llvm_profile_data *)getMaxAddr(DataLast, Data + 1);
  CountersLast = (uint64_t *)getMaxAddr(
      CountersLast, (uint64_t *)Data->CounterPtr + Data->NumCounters);
}

COMPILER_RT_VISIBILITY
void __llvm_profile_register_names_function(void *NamesStart,
                                            uint64_t NamesSize) {
  if (!NamesFirst) {
    NamesFirst = (const char *)NamesStart;
    NamesLast = (const char *)NamesStart + NamesSize;
    return;
  }
  NamesFirst = (const char *)getMinAddr(NamesFirst, NamesStart);
  NamesLast =
      (const char *)getMaxAddr(NamesLast, (const char *)NamesStart + NamesSize);
}

COMPILER_RT_VISIBILITY
const __llvm_profile_data *__llvm_profile_begin_data(void) { return DataFirst; }
COMPILER_RT_VISIBILITY
const __llvm_profile_data *__llvm_profile_end_data(void) { return DataLast; }
COMPILER_RT_VISIBILITY
const char *__llvm_profile_begin_names(void) { return NamesFirst; }
COMPILER_RT_VISIBILITY
const char *__llvm_profile_end_names(void) { return NamesLast; }
COMPILER_RT_VISIBILITY
uint64_t *__llvm_profile_begin_counters(void) { return CountersFirst; }
COMPILER_RT_VISIBILITY
uint64_t *__llvm_profile_end_counters(void) { return CountersLast; }

COMPILER_RT_VISIBILITY
ValueProfNode *__llvm_profile_begin_vnodes(void) {
  return 0;
}
COMPILER_RT_VISIBILITY
ValueProfNode *__llvm_profile_end_vnodes(void) { return 0; }

COMPILER_RT_VISIBILITY ValueProfNode *CurrentVNode = 0;
COMPILER_RT_VISIBILITY ValueProfNode *EndVNode = 0;

#endif
