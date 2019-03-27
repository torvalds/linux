//===- WholeProgramDevirt.h - Whole-program devirt pass ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines parts of the whole-program devirtualization pass
// implementation that may be usefully unit tested.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_WHOLEPROGRAMDEVIRT_H
#define LLVM_TRANSFORMS_IPO_WHOLEPROGRAMDEVIRT_H

#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>

namespace llvm {

template <typename T> class ArrayRef;
template <typename T> class MutableArrayRef;
class Function;
class GlobalVariable;
class ModuleSummaryIndex;

namespace wholeprogramdevirt {

// A bit vector that keeps track of which bits are used. We use this to
// pack constant values compactly before and after each virtual table.
struct AccumBitVector {
  std::vector<uint8_t> Bytes;

  // Bits in BytesUsed[I] are 1 if matching bit in Bytes[I] is used, 0 if not.
  std::vector<uint8_t> BytesUsed;

  std::pair<uint8_t *, uint8_t *> getPtrToData(uint64_t Pos, uint8_t Size) {
    if (Bytes.size() < Pos + Size) {
      Bytes.resize(Pos + Size);
      BytesUsed.resize(Pos + Size);
    }
    return std::make_pair(Bytes.data() + Pos, BytesUsed.data() + Pos);
  }

  // Set little-endian value Val with size Size at bit position Pos,
  // and mark bytes as used.
  void setLE(uint64_t Pos, uint64_t Val, uint8_t Size) {
    assert(Pos % 8 == 0);
    auto DataUsed = getPtrToData(Pos / 8, Size);
    for (unsigned I = 0; I != Size; ++I) {
      DataUsed.first[I] = Val >> (I * 8);
      assert(!DataUsed.second[I]);
      DataUsed.second[I] = 0xff;
    }
  }

  // Set big-endian value Val with size Size at bit position Pos,
  // and mark bytes as used.
  void setBE(uint64_t Pos, uint64_t Val, uint8_t Size) {
    assert(Pos % 8 == 0);
    auto DataUsed = getPtrToData(Pos / 8, Size);
    for (unsigned I = 0; I != Size; ++I) {
      DataUsed.first[Size - I - 1] = Val >> (I * 8);
      assert(!DataUsed.second[Size - I - 1]);
      DataUsed.second[Size - I - 1] = 0xff;
    }
  }

  // Set bit at bit position Pos to b and mark bit as used.
  void setBit(uint64_t Pos, bool b) {
    auto DataUsed = getPtrToData(Pos / 8, 1);
    if (b)
      *DataUsed.first |= 1 << (Pos % 8);
    assert(!(*DataUsed.second & (1 << Pos % 8)));
    *DataUsed.second |= 1 << (Pos % 8);
  }
};

// The bits that will be stored before and after a particular vtable.
struct VTableBits {
  // The vtable global.
  GlobalVariable *GV;

  // Cache of the vtable's size in bytes.
  uint64_t ObjectSize = 0;

  // The bit vector that will be laid out before the vtable. Note that these
  // bytes are stored in reverse order until the globals are rebuilt. This means
  // that any values in the array must be stored using the opposite endianness
  // from the target.
  AccumBitVector Before;

  // The bit vector that will be laid out after the vtable.
  AccumBitVector After;
};

// Information about a member of a particular type identifier.
struct TypeMemberInfo {
  // The VTableBits for the vtable.
  VTableBits *Bits;

  // The offset in bytes from the start of the vtable (i.e. the address point).
  uint64_t Offset;

  bool operator<(const TypeMemberInfo &other) const {
    return Bits < other.Bits || (Bits == other.Bits && Offset < other.Offset);
  }
};

// A virtual call target, i.e. an entry in a particular vtable.
struct VirtualCallTarget {
  VirtualCallTarget(Function *Fn, const TypeMemberInfo *TM);

  // For testing only.
  VirtualCallTarget(const TypeMemberInfo *TM, bool IsBigEndian)
      : Fn(nullptr), TM(TM), IsBigEndian(IsBigEndian), WasDevirt(false) {}

  // The function stored in the vtable.
  Function *Fn;

  // A pointer to the type identifier member through which the pointer to Fn is
  // accessed.
  const TypeMemberInfo *TM;

  // When doing virtual constant propagation, this stores the return value for
  // the function when passed the currently considered argument list.
  uint64_t RetVal;

  // Whether the target is big endian.
  bool IsBigEndian;

  // Whether at least one call site to the target was devirtualized.
  bool WasDevirt;

  // The minimum byte offset before the address point. This covers the bytes in
  // the vtable object before the address point (e.g. RTTI, access-to-top,
  // vtables for other base classes) and is equal to the offset from the start
  // of the vtable object to the address point.
  uint64_t minBeforeBytes() const { return TM->Offset; }

  // The minimum byte offset after the address point. This covers the bytes in
  // the vtable object after the address point (e.g. the vtable for the current
  // class and any later base classes) and is equal to the size of the vtable
  // object minus the offset from the start of the vtable object to the address
  // point.
  uint64_t minAfterBytes() const { return TM->Bits->ObjectSize - TM->Offset; }

  // The number of bytes allocated (for the vtable plus the byte array) before
  // the address point.
  uint64_t allocatedBeforeBytes() const {
    return minBeforeBytes() + TM->Bits->Before.Bytes.size();
  }

  // The number of bytes allocated (for the vtable plus the byte array) after
  // the address point.
  uint64_t allocatedAfterBytes() const {
    return minAfterBytes() + TM->Bits->After.Bytes.size();
  }

  // Set the bit at position Pos before the address point to RetVal.
  void setBeforeBit(uint64_t Pos) {
    assert(Pos >= 8 * minBeforeBytes());
    TM->Bits->Before.setBit(Pos - 8 * minBeforeBytes(), RetVal);
  }

  // Set the bit at position Pos after the address point to RetVal.
  void setAfterBit(uint64_t Pos) {
    assert(Pos >= 8 * minAfterBytes());
    TM->Bits->After.setBit(Pos - 8 * minAfterBytes(), RetVal);
  }

  // Set the bytes at position Pos before the address point to RetVal.
  // Because the bytes in Before are stored in reverse order, we use the
  // opposite endianness to the target.
  void setBeforeBytes(uint64_t Pos, uint8_t Size) {
    assert(Pos >= 8 * minBeforeBytes());
    if (IsBigEndian)
      TM->Bits->Before.setLE(Pos - 8 * minBeforeBytes(), RetVal, Size);
    else
      TM->Bits->Before.setBE(Pos - 8 * minBeforeBytes(), RetVal, Size);
  }

  // Set the bytes at position Pos after the address point to RetVal.
  void setAfterBytes(uint64_t Pos, uint8_t Size) {
    assert(Pos >= 8 * minAfterBytes());
    if (IsBigEndian)
      TM->Bits->After.setBE(Pos - 8 * minAfterBytes(), RetVal, Size);
    else
      TM->Bits->After.setLE(Pos - 8 * minAfterBytes(), RetVal, Size);
  }
};

// Find the minimum offset that we may store a value of size Size bits at. If
// IsAfter is set, look for an offset before the object, otherwise look for an
// offset after the object.
uint64_t findLowestOffset(ArrayRef<VirtualCallTarget> Targets, bool IsAfter,
                          uint64_t Size);

// Set the stored value in each of Targets to VirtualCallTarget::RetVal at the
// given allocation offset before the vtable address. Stores the computed
// byte/bit offset to OffsetByte/OffsetBit.
void setBeforeReturnValues(MutableArrayRef<VirtualCallTarget> Targets,
                           uint64_t AllocBefore, unsigned BitWidth,
                           int64_t &OffsetByte, uint64_t &OffsetBit);

// Set the stored value in each of Targets to VirtualCallTarget::RetVal at the
// given allocation offset after the vtable address. Stores the computed
// byte/bit offset to OffsetByte/OffsetBit.
void setAfterReturnValues(MutableArrayRef<VirtualCallTarget> Targets,
                          uint64_t AllocAfter, unsigned BitWidth,
                          int64_t &OffsetByte, uint64_t &OffsetBit);

} // end namespace wholeprogramdevirt

struct WholeProgramDevirtPass : public PassInfoMixin<WholeProgramDevirtPass> {
  ModuleSummaryIndex *ExportSummary;
  const ModuleSummaryIndex *ImportSummary;
  WholeProgramDevirtPass(ModuleSummaryIndex *ExportSummary,
                         const ModuleSummaryIndex *ImportSummary)
      : ExportSummary(ExportSummary), ImportSummary(ImportSummary) {
    assert(!(ExportSummary && ImportSummary));
  }
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_WHOLEPROGRAMDEVIRT_H
