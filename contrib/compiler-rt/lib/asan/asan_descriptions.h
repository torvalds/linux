//===-- asan_descriptions.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// ASan-private header for asan_descriptions.cc.
// TODO(filcab): Most struct definitions should move to the interface headers.
//===----------------------------------------------------------------------===//
#ifndef ASAN_DESCRIPTIONS_H
#define ASAN_DESCRIPTIONS_H

#include "asan_allocator.h"
#include "asan_thread.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_report_decorator.h"

namespace __asan {

void DescribeThread(AsanThreadContext *context);
static inline void DescribeThread(AsanThread *t) {
  if (t) DescribeThread(t->context());
}

class AsanThreadIdAndName {
 public:
  explicit AsanThreadIdAndName(AsanThreadContext *t);
  explicit AsanThreadIdAndName(u32 tid);

  // Contains "T%tid (%name)" or "T%tid" if the name is empty.
  const char *c_str() const { return &name[0]; }

 private:
  void Init(u32 tid, const char *tname);

  char name[128];
};

class Decorator : public __sanitizer::SanitizerCommonDecorator {
 public:
  Decorator() : SanitizerCommonDecorator() {}
  const char *Access() { return Blue(); }
  const char *Location() { return Green(); }
  const char *Allocation() { return Magenta(); }

  const char *ShadowByte(u8 byte) {
    switch (byte) {
      case kAsanHeapLeftRedzoneMagic:
      case kAsanArrayCookieMagic:
        return Red();
      case kAsanHeapFreeMagic:
        return Magenta();
      case kAsanStackLeftRedzoneMagic:
      case kAsanStackMidRedzoneMagic:
      case kAsanStackRightRedzoneMagic:
        return Red();
      case kAsanStackAfterReturnMagic:
        return Magenta();
      case kAsanInitializationOrderMagic:
        return Cyan();
      case kAsanUserPoisonedMemoryMagic:
      case kAsanContiguousContainerOOBMagic:
      case kAsanAllocaLeftMagic:
      case kAsanAllocaRightMagic:
        return Blue();
      case kAsanStackUseAfterScopeMagic:
        return Magenta();
      case kAsanGlobalRedzoneMagic:
        return Red();
      case kAsanInternalHeapMagic:
        return Yellow();
      case kAsanIntraObjectRedzone:
        return Yellow();
      default:
        return Default();
    }
  }
};

enum ShadowKind : u8 {
  kShadowKindLow,
  kShadowKindGap,
  kShadowKindHigh,
};
static const char *const ShadowNames[] = {"low shadow", "shadow gap",
                                          "high shadow"};

struct ShadowAddressDescription {
  uptr addr;
  ShadowKind kind;
  u8 shadow_byte;

  void Print() const;
};

bool GetShadowAddressInformation(uptr addr, ShadowAddressDescription *descr);
bool DescribeAddressIfShadow(uptr addr);

enum AccessType {
  kAccessTypeLeft,
  kAccessTypeRight,
  kAccessTypeInside,
  kAccessTypeUnknown,  // This means we have an AddressSanitizer bug!
};

struct ChunkAccess {
  uptr bad_addr;
  sptr offset;
  uptr chunk_begin;
  uptr chunk_size;
  u32 user_requested_alignment : 12;
  u32 access_type : 2;
  u32 alloc_type : 2;
};

struct HeapAddressDescription {
  uptr addr;
  uptr alloc_tid;
  uptr free_tid;
  u32 alloc_stack_id;
  u32 free_stack_id;
  ChunkAccess chunk_access;

  void Print() const;
};

bool GetHeapAddressInformation(uptr addr, uptr access_size,
                               HeapAddressDescription *descr);
bool DescribeAddressIfHeap(uptr addr, uptr access_size = 1);

struct StackAddressDescription {
  uptr addr;
  uptr tid;
  uptr offset;
  uptr frame_pc;
  uptr access_size;
  const char *frame_descr;

  void Print() const;
};

bool GetStackAddressInformation(uptr addr, uptr access_size,
                                StackAddressDescription *descr);

struct GlobalAddressDescription {
  uptr addr;
  // Assume address is close to at most four globals.
  static const int kMaxGlobals = 4;
  __asan_global globals[kMaxGlobals];
  u32 reg_sites[kMaxGlobals];
  uptr access_size;
  u8 size;

  void Print(const char *bug_type = "") const;

  // Returns true when this descriptions points inside the same global variable
  // as other. Descriptions can have different address within the variable
  bool PointsInsideTheSameVariable(const GlobalAddressDescription &other) const;
};

bool GetGlobalAddressInformation(uptr addr, uptr access_size,
                                 GlobalAddressDescription *descr);
bool DescribeAddressIfGlobal(uptr addr, uptr access_size, const char *bug_type);

// General function to describe an address. Will try to describe the address as
// a shadow, global (variable), stack, or heap address.
// bug_type is optional and is used for checking if we're reporting an
// initialization-order-fiasco
// The proper access_size should be passed for stack, global, and heap
// addresses. Defaults to 1.
// Each of the *AddressDescription functions has its own Print() member, which
// may take access_size and bug_type parameters if needed.
void PrintAddressDescription(uptr addr, uptr access_size = 1,
                             const char *bug_type = "");

enum AddressKind {
  kAddressKindWild,
  kAddressKindShadow,
  kAddressKindHeap,
  kAddressKindStack,
  kAddressKindGlobal,
};

class AddressDescription {
  struct AddressDescriptionData {
    AddressKind kind;
    union {
      ShadowAddressDescription shadow;
      HeapAddressDescription heap;
      StackAddressDescription stack;
      GlobalAddressDescription global;
      uptr addr;
    };
  };

  AddressDescriptionData data;

 public:
  AddressDescription() = default;
  // shouldLockThreadRegistry allows us to skip locking if we're sure we already
  // have done it.
  AddressDescription(uptr addr, bool shouldLockThreadRegistry = true)
      : AddressDescription(addr, 1, shouldLockThreadRegistry) {}
  AddressDescription(uptr addr, uptr access_size,
                     bool shouldLockThreadRegistry = true);

  uptr Address() const {
    switch (data.kind) {
      case kAddressKindWild:
        return data.addr;
      case kAddressKindShadow:
        return data.shadow.addr;
      case kAddressKindHeap:
        return data.heap.addr;
      case kAddressKindStack:
        return data.stack.addr;
      case kAddressKindGlobal:
        return data.global.addr;
    }
    UNREACHABLE("AddressInformation kind is invalid");
  }
  void Print(const char *bug_descr = nullptr) const {
    switch (data.kind) {
      case kAddressKindWild:
        Printf("Address %p is a wild pointer.\n", data.addr);
        return;
      case kAddressKindShadow:
        return data.shadow.Print();
      case kAddressKindHeap:
        return data.heap.Print();
      case kAddressKindStack:
        return data.stack.Print();
      case kAddressKindGlobal:
        // initialization-order-fiasco has a special Print()
        return data.global.Print(bug_descr);
    }
    UNREACHABLE("AddressInformation kind is invalid");
  }

  void StoreTo(AddressDescriptionData *dst) const { *dst = data; }

  const ShadowAddressDescription *AsShadow() const {
    return data.kind == kAddressKindShadow ? &data.shadow : nullptr;
  }
  const HeapAddressDescription *AsHeap() const {
    return data.kind == kAddressKindHeap ? &data.heap : nullptr;
  }
  const StackAddressDescription *AsStack() const {
    return data.kind == kAddressKindStack ? &data.stack : nullptr;
  }
  const GlobalAddressDescription *AsGlobal() const {
    return data.kind == kAddressKindGlobal ? &data.global : nullptr;
  }
};

}  // namespace __asan

#endif  // ASAN_DESCRIPTIONS_H
