/*===- InstrProfilingValue.c - Support library for PGO instrumentation ----===*\
|*
|*                     The LLVM Compiler Infrastructure
|*
|* This file is distributed under the University of Illinois Open Source
|* License. See LICENSE.TXT for details.
|*
\*===----------------------------------------------------------------------===*/

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "InstrProfiling.h"
#include "InstrProfilingInternal.h"
#include "InstrProfilingUtil.h"

#define INSTR_PROF_VALUE_PROF_DATA
#define INSTR_PROF_COMMON_API_IMPL
#include "InstrProfData.inc"

static int hasStaticCounters = 1;
static int OutOfNodesWarnings = 0;
static int hasNonDefaultValsPerSite = 0;
#define INSTR_PROF_MAX_VP_WARNS 10
#define INSTR_PROF_DEFAULT_NUM_VAL_PER_SITE 16
#define INSTR_PROF_VNODE_POOL_SIZE 1024

#ifndef _MSC_VER
/* A shared static pool in addition to the vnodes statically
 * allocated by the compiler.  */
COMPILER_RT_VISIBILITY ValueProfNode
    lprofValueProfNodes[INSTR_PROF_VNODE_POOL_SIZE] COMPILER_RT_SECTION(
       COMPILER_RT_SEG INSTR_PROF_VNODES_SECT_NAME_STR);
#endif

COMPILER_RT_VISIBILITY uint32_t VPMaxNumValsPerSite =
    INSTR_PROF_DEFAULT_NUM_VAL_PER_SITE;

COMPILER_RT_VISIBILITY void lprofSetupValueProfiler() {
  const char *Str = 0;
  Str = getenv("LLVM_VP_MAX_NUM_VALS_PER_SITE");
  if (Str && Str[0]) {
    VPMaxNumValsPerSite = atoi(Str);
    hasNonDefaultValsPerSite = 1;
  }
  if (VPMaxNumValsPerSite > INSTR_PROF_MAX_NUM_VAL_PER_SITE)
    VPMaxNumValsPerSite = INSTR_PROF_MAX_NUM_VAL_PER_SITE;
}

COMPILER_RT_VISIBILITY void lprofSetMaxValsPerSite(uint32_t MaxVals) {
  VPMaxNumValsPerSite = MaxVals;
  hasNonDefaultValsPerSite = 1;
}

/* This method is only used in value profiler mock testing.  */
COMPILER_RT_VISIBILITY void
__llvm_profile_set_num_value_sites(__llvm_profile_data *Data,
                                   uint32_t ValueKind, uint16_t NumValueSites) {
  *((uint16_t *)&Data->NumValueSites[ValueKind]) = NumValueSites;
}

/* This method is only used in value profiler mock testing.  */
COMPILER_RT_VISIBILITY const __llvm_profile_data *
__llvm_profile_iterate_data(const __llvm_profile_data *Data) {
  return Data + 1;
}

/* This method is only used in value profiler mock testing.  */
COMPILER_RT_VISIBILITY void *
__llvm_get_function_addr(const __llvm_profile_data *Data) {
  return Data->FunctionPointer;
}

/* Allocate an array that holds the pointers to the linked lists of
 * value profile counter nodes. The number of element of the array
 * is the total number of value profile sites instrumented. Returns
 * 0 if allocation fails.
 */

static int allocateValueProfileCounters(__llvm_profile_data *Data) {
  uint64_t NumVSites = 0;
  uint32_t VKI;

  /* This function will never be called when value site array is allocated
     statically at compile time.  */
  hasStaticCounters = 0;
  /* When dynamic allocation is enabled, allow tracking the max number of
   * values allowd.  */
  if (!hasNonDefaultValsPerSite)
    VPMaxNumValsPerSite = INSTR_PROF_MAX_NUM_VAL_PER_SITE;

  for (VKI = IPVK_First; VKI <= IPVK_Last; ++VKI)
    NumVSites += Data->NumValueSites[VKI];

  ValueProfNode **Mem =
      (ValueProfNode **)calloc(NumVSites, sizeof(ValueProfNode *));
  if (!Mem)
    return 0;
  if (!COMPILER_RT_BOOL_CMPXCHG(&Data->Values, 0, Mem)) {
    free(Mem);
    return 0;
  }
  return 1;
}

static ValueProfNode *allocateOneNode(void) {
  ValueProfNode *Node;

  if (!hasStaticCounters)
    return (ValueProfNode *)calloc(1, sizeof(ValueProfNode));

  /* Early check to avoid value wrapping around.  */
  if (CurrentVNode + 1 > EndVNode) {
    if (OutOfNodesWarnings++ < INSTR_PROF_MAX_VP_WARNS) {
      PROF_WARN("Unable to track new values: %s. "
                " Consider using option -mllvm -vp-counters-per-site=<n> to "
                "allocate more"
                " value profile counters at compile time. \n",
                "Running out of static counters");
    }
    return 0;
  }
  Node = COMPILER_RT_PTR_FETCH_ADD(ValueProfNode, CurrentVNode, 1);
  /* Due to section padding, EndVNode point to a byte which is one pass
   * an incomplete VNode, so we need to skip the last incomplete node. */
  if (Node + 1 > EndVNode)
    return 0;

  return Node;
}

static COMPILER_RT_ALWAYS_INLINE void
instrumentTargetValueImpl(uint64_t TargetValue, void *Data,
                          uint32_t CounterIndex, uint64_t CountValue) {
  __llvm_profile_data *PData = (__llvm_profile_data *)Data;
  if (!PData)
    return;
  if (!CountValue)
    return;
  if (!PData->Values) {
    if (!allocateValueProfileCounters(PData))
      return;
  }

  ValueProfNode **ValueCounters = (ValueProfNode **)PData->Values;
  ValueProfNode *PrevVNode = NULL;
  ValueProfNode *MinCountVNode = NULL;
  ValueProfNode *CurVNode = ValueCounters[CounterIndex];
  uint64_t MinCount = UINT64_MAX;

  uint8_t VDataCount = 0;
  while (CurVNode) {
    if (TargetValue == CurVNode->Value) {
      CurVNode->Count += CountValue;
      return;
    }
    if (CurVNode->Count < MinCount) {
      MinCount = CurVNode->Count;
      MinCountVNode = CurVNode;
    }
    PrevVNode = CurVNode;
    CurVNode = CurVNode->Next;
    ++VDataCount;
  }

  if (VDataCount >= VPMaxNumValsPerSite) {
    /* Bump down the min count node's count. If it reaches 0,
     * evict it. This eviction/replacement policy makes hot
     * targets more sticky while cold targets less so. In other
     * words, it makes it less likely for the hot targets to be
     * prematurally evicted during warmup/establishment period,
     * when their counts are still low. In a special case when
     * the number of values tracked is reduced to only one, this
     * policy will guarantee that the dominating target with >50%
     * total count will survive in the end. Note that this scheme
     * allows the runtime to track the min count node in an adaptive
     * manner. It can correct previous mistakes and eventually
     * lock on a cold target that is alread in stable state.
     *
     * In very rare cases,  this replacement scheme may still lead
     * to target loss. For instance, out of \c N value slots, \c N-1
     * slots are occupied by luke warm targets during the warmup
     * period and the remaining one slot is competed by two or more
     * very hot targets. If those hot targets occur in an interleaved
     * way, none of them will survive (gain enough weight to throw out
     * other established entries) due to the ping-pong effect.
     * To handle this situation, user can choose to increase the max
     * number of tracked values per value site. Alternatively, a more
     * expensive eviction mechanism can be implemented. It requires
     * the runtime to track the total number of evictions per-site.
     * When the total number of evictions reaches certain threshold,
     * the runtime can wipe out more than one lowest count entries
     * to give space for hot targets.
     */
    if (MinCountVNode->Count <= CountValue) {
      CurVNode = MinCountVNode;
      CurVNode->Value = TargetValue;
      CurVNode->Count = CountValue;
    } else
      MinCountVNode->Count -= CountValue;

    return;
  }

  CurVNode = allocateOneNode();
  if (!CurVNode)
    return;
  CurVNode->Value = TargetValue;
  CurVNode->Count += CountValue;

  uint32_t Success = 0;
  if (!ValueCounters[CounterIndex])
    Success =
        COMPILER_RT_BOOL_CMPXCHG(&ValueCounters[CounterIndex], 0, CurVNode);
  else if (PrevVNode && !PrevVNode->Next)
    Success = COMPILER_RT_BOOL_CMPXCHG(&(PrevVNode->Next), 0, CurVNode);

  if (!Success && !hasStaticCounters) {
    free(CurVNode);
    return;
  }
}

COMPILER_RT_VISIBILITY void
__llvm_profile_instrument_target(uint64_t TargetValue, void *Data,
                                 uint32_t CounterIndex) {
  instrumentTargetValueImpl(TargetValue, Data, CounterIndex, 1);
}
COMPILER_RT_VISIBILITY void
__llvm_profile_instrument_target_value(uint64_t TargetValue, void *Data,
                                       uint32_t CounterIndex,
                                       uint64_t CountValue) {
  instrumentTargetValueImpl(TargetValue, Data, CounterIndex, CountValue);
}

/*
 * The target values are partitioned into multiple regions/ranges. There is one
 * contiguous region which is precise -- every value in the range is tracked
 * individually. A value outside the precise region will be collapsed into one
 * value depending on the region it falls in.
 *
 * There are three regions:
 * 1. (-inf, PreciseRangeStart) and (PreciseRangeLast, LargeRangeValue) belong
 * to one region -- all values here should be mapped to one value of
 * "PreciseRangeLast + 1".
 * 2. [PreciseRangeStart, PreciseRangeLast]
 * 3. Large values: [LargeValue, +inf) maps to one value of LargeValue.
 *
 * The range for large values is optional. The default value of INT64_MIN
 * indicates it is not specified.
 */
COMPILER_RT_VISIBILITY void __llvm_profile_instrument_range(
    uint64_t TargetValue, void *Data, uint32_t CounterIndex,
    int64_t PreciseRangeStart, int64_t PreciseRangeLast, int64_t LargeValue) {

  if (LargeValue != INT64_MIN && (int64_t)TargetValue >= LargeValue)
    TargetValue = LargeValue;
  else if ((int64_t)TargetValue < PreciseRangeStart ||
           (int64_t)TargetValue > PreciseRangeLast)
    TargetValue = PreciseRangeLast + 1;

  __llvm_profile_instrument_target(TargetValue, Data, CounterIndex);
}

/*
 * A wrapper struct that represents value profile runtime data.
 * Like InstrProfRecord class which is used by profiling host tools,
 * ValueProfRuntimeRecord also implements the abstract intefaces defined in
 * ValueProfRecordClosure so that the runtime data can be serialized using
 * shared C implementation.
 */
typedef struct ValueProfRuntimeRecord {
  const __llvm_profile_data *Data;
  ValueProfNode **NodesKind[IPVK_Last + 1];
  uint8_t **SiteCountArray;
} ValueProfRuntimeRecord;

/* ValueProfRecordClosure Interface implementation. */

static uint32_t getNumValueSitesRT(const void *R, uint32_t VK) {
  return ((const ValueProfRuntimeRecord *)R)->Data->NumValueSites[VK];
}

static uint32_t getNumValueDataRT(const void *R, uint32_t VK) {
  uint32_t S = 0, I;
  const ValueProfRuntimeRecord *Record = (const ValueProfRuntimeRecord *)R;
  if (Record->SiteCountArray[VK] == INSTR_PROF_NULLPTR)
    return 0;
  for (I = 0; I < Record->Data->NumValueSites[VK]; I++)
    S += Record->SiteCountArray[VK][I];
  return S;
}

static uint32_t getNumValueDataForSiteRT(const void *R, uint32_t VK,
                                         uint32_t S) {
  const ValueProfRuntimeRecord *Record = (const ValueProfRuntimeRecord *)R;
  return Record->SiteCountArray[VK][S];
}

static ValueProfRuntimeRecord RTRecord;
static ValueProfRecordClosure RTRecordClosure = {
    &RTRecord,          INSTR_PROF_NULLPTR, /* GetNumValueKinds */
    getNumValueSitesRT, getNumValueDataRT,  getNumValueDataForSiteRT,
    INSTR_PROF_NULLPTR, /* RemapValueData */
    INSTR_PROF_NULLPTR, /* GetValueForSite, */
    INSTR_PROF_NULLPTR  /* AllocValueProfData */
};

static uint32_t
initializeValueProfRuntimeRecord(const __llvm_profile_data *Data,
                                 uint8_t *SiteCountArray[]) {
  unsigned I, J, S = 0, NumValueKinds = 0;
  ValueProfNode **Nodes = (ValueProfNode **)Data->Values;
  RTRecord.Data = Data;
  RTRecord.SiteCountArray = SiteCountArray;
  for (I = 0; I <= IPVK_Last; I++) {
    uint16_t N = Data->NumValueSites[I];
    if (!N)
      continue;

    NumValueKinds++;

    RTRecord.NodesKind[I] = Nodes ? &Nodes[S] : INSTR_PROF_NULLPTR;
    for (J = 0; J < N; J++) {
      /* Compute value count for each site. */
      uint32_t C = 0;
      ValueProfNode *Site =
          Nodes ? RTRecord.NodesKind[I][J] : INSTR_PROF_NULLPTR;
      while (Site) {
        C++;
        Site = Site->Next;
      }
      if (C > UCHAR_MAX)
        C = UCHAR_MAX;
      RTRecord.SiteCountArray[I][J] = C;
    }
    S += N;
  }
  return NumValueKinds;
}

static ValueProfNode *getNextNValueData(uint32_t VK, uint32_t Site,
                                        InstrProfValueData *Dst,
                                        ValueProfNode *StartNode, uint32_t N) {
  unsigned I;
  ValueProfNode *VNode = StartNode ? StartNode : RTRecord.NodesKind[VK][Site];
  for (I = 0; I < N; I++) {
    Dst[I].Value = VNode->Value;
    Dst[I].Count = VNode->Count;
    VNode = VNode->Next;
  }
  return VNode;
}

static uint32_t getValueProfDataSizeWrapper(void) {
  return getValueProfDataSize(&RTRecordClosure);
}

static uint32_t getNumValueDataForSiteWrapper(uint32_t VK, uint32_t S) {
  return getNumValueDataForSiteRT(&RTRecord, VK, S);
}

static VPDataReaderType TheVPDataReader = {
    initializeValueProfRuntimeRecord, getValueProfRecordHeaderSize,
    getFirstValueProfRecord,          getNumValueDataForSiteWrapper,
    getValueProfDataSizeWrapper,      getNextNValueData};

COMPILER_RT_VISIBILITY VPDataReaderType *lprofGetVPDataReader() {
  return &TheVPDataReader;
}
