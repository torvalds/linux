//===-- cache_frag.cpp ----------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of EfficiencySanitizer, a family of performance tuners.
//
// This file contains cache fragmentation-specific code.
//===----------------------------------------------------------------------===//

#include "esan.h"
#include "esan_flags.h"
#include "sanitizer_common/sanitizer_addrhashmap.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_placement_new.h"
#include <string.h>

namespace __esan {

//===-- Struct field access counter runtime -------------------------------===//

// This should be kept consistent with LLVM's EfficiencySanitizer StructInfo.
struct StructInfo {
  const char *StructName;
  u32 Size;
  u32 NumFields;
  u32 *FieldOffset;           // auxiliary struct field info.
  u32 *FieldSize;             // auxiliary struct field info.
  const char **FieldTypeName; // auxiliary struct field info.
  u64 *FieldCounters;
  u64 *ArrayCounter;
  bool hasAuxFieldInfo() { return FieldOffset != nullptr; }
};

// This should be kept consistent with LLVM's EfficiencySanitizer CacheFragInfo.
// The tool-specific information per compilation unit (module).
struct CacheFragInfo {
  const char *UnitName;
  u32 NumStructs;
  StructInfo *Structs;
};

struct StructCounter {
  StructInfo *Struct;
  u64 Count; // The total access count of the struct.
  u64 Ratio; // Difference ratio for the struct layout access.
};

// We use StructHashMap to keep track of an unique copy of StructCounter.
typedef AddrHashMap<StructCounter, 31051> StructHashMap;
struct Context {
  StructHashMap StructMap;
  u32 NumStructs;
  u64 TotalCount; // The total access count of all structs.
};
static Context *Ctx;

static void reportStructSummary() {
  // FIXME: provide a better struct field access summary report.
  Report("%s: total struct field access count = %llu\n", SanitizerToolName,
         Ctx->TotalCount);
}

// FIXME: we are still exploring proper ways to evaluate the difference between
// struct field counts.  Currently, we use a simple formula to calculate the
// difference ratio: V1/V2.
static inline u64 computeDifferenceRatio(u64 Val1, u64 Val2) {
  if (Val2 > Val1) {
    Swap(Val1, Val2);
  }
  if (Val2 == 0)
    Val2 = 1;
  return (Val1 / Val2);
}

static void reportStructCounter(StructHashMap::Handle &Handle) {
  const u32 TypePrintLimit = 512;
  const char *type, *start, *end;
  StructInfo *Struct = Handle->Struct;
  // Union field address calculation is done via bitcast instead of GEP,
  // so the count for union is always 0.
  // We skip the union report to avoid confusion.
  if (strncmp(Struct->StructName, "union.", 6) == 0)
    return;
  // Remove the '.' after class/struct during print.
  if (strncmp(Struct->StructName, "class.", 6) == 0) {
    type = "class";
    start = &Struct->StructName[6];
  } else {
    type = "struct";
    start = &Struct->StructName[7];
  }
  // Remove the suffixes with '$' during print.
  end = strchr(start, '$');
  CHECK(end != nullptr);
  Report("  %s %.*s\n", type, end - start, start);
  Report("   size = %u, count = %llu, ratio = %llu, array access = %llu\n",
         Struct->Size, Handle->Count, Handle->Ratio, *Struct->ArrayCounter);
  if (Struct->hasAuxFieldInfo()) {
    for (u32 i = 0; i < Struct->NumFields; ++i) {
      Report("   #%2u: offset = %u,\t size = %u,"
             "\t count = %llu,\t type = %.*s\n",
             i, Struct->FieldOffset[i], Struct->FieldSize[i],
             Struct->FieldCounters[i], TypePrintLimit, Struct->FieldTypeName[i]);
    }
  } else {
    for (u32 i = 0; i < Struct->NumFields; ++i) {
      Report("   #%2u: count = %llu\n", i, Struct->FieldCounters[i]);
    }
  }
}

static void computeStructRatio(StructHashMap::Handle &Handle) {
  Handle->Ratio = 0;
  Handle->Count = Handle->Struct->FieldCounters[0];
  for (u32 i = 1; i < Handle->Struct->NumFields; ++i) {
    Handle->Count += Handle->Struct->FieldCounters[i];
    Handle->Ratio += computeDifferenceRatio(
        Handle->Struct->FieldCounters[i - 1], Handle->Struct->FieldCounters[i]);
  }
  Ctx->TotalCount += Handle->Count;
  if (Handle->Ratio >= (u64)getFlags()->report_threshold ||
      (Verbosity() >= 1 && Handle->Count > 0))
    reportStructCounter(Handle);
}

static void registerStructInfo(CacheFragInfo *CacheFrag) {
  for (u32 i = 0; i < CacheFrag->NumStructs; ++i) {
    StructInfo *Struct = &CacheFrag->Structs[i];
    StructHashMap::Handle H(&Ctx->StructMap, (uptr)Struct->FieldCounters);
    if (H.created()) {
      VPrintf(2, " Register %s: %u fields\n", Struct->StructName,
              Struct->NumFields);
      H->Struct = Struct;
      ++Ctx->NumStructs;
    } else {
      VPrintf(2, " Duplicated %s: %u fields\n", Struct->StructName,
              Struct->NumFields);
    }
  }
}

static void unregisterStructInfo(CacheFragInfo *CacheFrag) {
  // FIXME: if the library is unloaded before finalizeCacheFrag, we should
  // collect the result for later report.
  for (u32 i = 0; i < CacheFrag->NumStructs; ++i) {
    StructInfo *Struct = &CacheFrag->Structs[i];
    StructHashMap::Handle H(&Ctx->StructMap, (uptr)Struct->FieldCounters, true);
    if (H.exists()) {
      VPrintf(2, " Unregister %s: %u fields\n", Struct->StructName,
              Struct->NumFields);
      // FIXME: we should move this call to finalizeCacheFrag once we can
      // iterate over the hash map there.
      computeStructRatio(H);
      --Ctx->NumStructs;
    } else {
      VPrintf(2, " Duplicated %s: %u fields\n", Struct->StructName,
              Struct->NumFields);
    }
  }
  static bool Reported = false;
  if (Ctx->NumStructs == 0 && !Reported) {
    Reported = true;
    reportStructSummary();
  }
}

//===-- Init/exit functions -----------------------------------------------===//

void processCacheFragCompilationUnitInit(void *Ptr) {
  CacheFragInfo *CacheFrag = (CacheFragInfo *)Ptr;
  VPrintf(2, "in esan::%s: %s with %u class(es)/struct(s)\n", __FUNCTION__,
          CacheFrag->UnitName, CacheFrag->NumStructs);
  registerStructInfo(CacheFrag);
}

void processCacheFragCompilationUnitExit(void *Ptr) {
  CacheFragInfo *CacheFrag = (CacheFragInfo *)Ptr;
  VPrintf(2, "in esan::%s: %s with %u class(es)/struct(s)\n", __FUNCTION__,
          CacheFrag->UnitName, CacheFrag->NumStructs);
  unregisterStructInfo(CacheFrag);
}

void initializeCacheFrag() {
  VPrintf(2, "in esan::%s\n", __FUNCTION__);
  // We use placement new to initialize Ctx before C++ static initializaion.
  // We make CtxMem 8-byte aligned for atomic operations in AddrHashMap.
  static u64 CtxMem[sizeof(Context) / sizeof(u64) + 1];
  Ctx = new (CtxMem) Context();
  Ctx->NumStructs = 0;
}

int finalizeCacheFrag() {
  VPrintf(2, "in esan::%s\n", __FUNCTION__);
  return 0;
}

void reportCacheFrag() {
  VPrintf(2, "in esan::%s\n", __FUNCTION__);
  // FIXME: Not yet implemented.  We need to iterate over all of the
  // compilation unit data.
}

} // namespace __esan
