//===--- SwiftCallingConv.cpp - Lowering for the Swift calling convention -===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Implementation of the abstract lowering for the Swift calling convention.
//
//===----------------------------------------------------------------------===//

#include "clang/CodeGen/SwiftCallingConv.h"
#include "clang/Basic/TargetInfo.h"
#include "CodeGenModule.h"
#include "TargetInfo.h"

using namespace clang;
using namespace CodeGen;
using namespace swiftcall;

static const SwiftABIInfo &getSwiftABIInfo(CodeGenModule &CGM) {
  return cast<SwiftABIInfo>(CGM.getTargetCodeGenInfo().getABIInfo());
}

static bool isPowerOf2(unsigned n) {
  return n == (n & -n);
}

/// Given two types with the same size, try to find a common type.
static llvm::Type *getCommonType(llvm::Type *first, llvm::Type *second) {
  assert(first != second);

  // Allow pointers to merge with integers, but prefer the integer type.
  if (first->isIntegerTy()) {
    if (second->isPointerTy()) return first;
  } else if (first->isPointerTy()) {
    if (second->isIntegerTy()) return second;
    if (second->isPointerTy()) return first;

  // Allow two vectors to be merged (given that they have the same size).
  // This assumes that we never have two different vector register sets.
  } else if (auto firstVecTy = dyn_cast<llvm::VectorType>(first)) {
    if (auto secondVecTy = dyn_cast<llvm::VectorType>(second)) {
      if (auto commonTy = getCommonType(firstVecTy->getElementType(),
                                        secondVecTy->getElementType())) {
        return (commonTy == firstVecTy->getElementType() ? first : second);
      }
    }
  }

  return nullptr;
}

static CharUnits getTypeStoreSize(CodeGenModule &CGM, llvm::Type *type) {
  return CharUnits::fromQuantity(CGM.getDataLayout().getTypeStoreSize(type));
}

static CharUnits getTypeAllocSize(CodeGenModule &CGM, llvm::Type *type) {
  return CharUnits::fromQuantity(CGM.getDataLayout().getTypeAllocSize(type));
}

void SwiftAggLowering::addTypedData(QualType type, CharUnits begin) {
  // Deal with various aggregate types as special cases:

  // Record types.
  if (auto recType = type->getAs<RecordType>()) {
    addTypedData(recType->getDecl(), begin);

  // Array types.
  } else if (type->isArrayType()) {
    // Incomplete array types (flexible array members?) don't provide
    // data to lay out, and the other cases shouldn't be possible.
    auto arrayType = CGM.getContext().getAsConstantArrayType(type);
    if (!arrayType) return;

    QualType eltType = arrayType->getElementType();
    auto eltSize = CGM.getContext().getTypeSizeInChars(eltType);
    for (uint64_t i = 0, e = arrayType->getSize().getZExtValue(); i != e; ++i) {
      addTypedData(eltType, begin + i * eltSize);
    }

  // Complex types.
  } else if (auto complexType = type->getAs<ComplexType>()) {
    auto eltType = complexType->getElementType();
    auto eltSize = CGM.getContext().getTypeSizeInChars(eltType);
    auto eltLLVMType = CGM.getTypes().ConvertType(eltType);
    addTypedData(eltLLVMType, begin, begin + eltSize);
    addTypedData(eltLLVMType, begin + eltSize, begin + 2 * eltSize);

  // Member pointer types.
  } else if (type->getAs<MemberPointerType>()) {
    // Just add it all as opaque.
    addOpaqueData(begin, begin + CGM.getContext().getTypeSizeInChars(type));

  // Everything else is scalar and should not convert as an LLVM aggregate.
  } else {
    // We intentionally convert as !ForMem because we want to preserve
    // that a type was an i1.
    auto llvmType = CGM.getTypes().ConvertType(type);
    addTypedData(llvmType, begin);
  }
}

void SwiftAggLowering::addTypedData(const RecordDecl *record, CharUnits begin) {
  addTypedData(record, begin, CGM.getContext().getASTRecordLayout(record));
}

void SwiftAggLowering::addTypedData(const RecordDecl *record, CharUnits begin,
                                    const ASTRecordLayout &layout) {
  // Unions are a special case.
  if (record->isUnion()) {
    for (auto field : record->fields()) {
      if (field->isBitField()) {
        addBitFieldData(field, begin, 0);
      } else {
        addTypedData(field->getType(), begin);
      }
    }
    return;
  }

  // Note that correctness does not rely on us adding things in
  // their actual order of layout; it's just somewhat more efficient
  // for the builder.

  // With that in mind, add "early" C++ data.
  auto cxxRecord = dyn_cast<CXXRecordDecl>(record);
  if (cxxRecord) {
    //   - a v-table pointer, if the class adds its own
    if (layout.hasOwnVFPtr()) {
      addTypedData(CGM.Int8PtrTy, begin);
    }

    //   - non-virtual bases
    for (auto &baseSpecifier : cxxRecord->bases()) {
      if (baseSpecifier.isVirtual()) continue;

      auto baseRecord = baseSpecifier.getType()->getAsCXXRecordDecl();
      addTypedData(baseRecord, begin + layout.getBaseClassOffset(baseRecord));
    }

    //   - a vbptr if the class adds its own
    if (layout.hasOwnVBPtr()) {
      addTypedData(CGM.Int8PtrTy, begin + layout.getVBPtrOffset());
    }
  }

  // Add fields.
  for (auto field : record->fields()) {
    auto fieldOffsetInBits = layout.getFieldOffset(field->getFieldIndex());
    if (field->isBitField()) {
      addBitFieldData(field, begin, fieldOffsetInBits);
    } else {
      addTypedData(field->getType(),
              begin + CGM.getContext().toCharUnitsFromBits(fieldOffsetInBits));
    }
  }

  // Add "late" C++ data:
  if (cxxRecord) {
    //   - virtual bases
    for (auto &vbaseSpecifier : cxxRecord->vbases()) {
      auto baseRecord = vbaseSpecifier.getType()->getAsCXXRecordDecl();
      addTypedData(baseRecord, begin + layout.getVBaseClassOffset(baseRecord));
    }
  }
}

void SwiftAggLowering::addBitFieldData(const FieldDecl *bitfield,
                                       CharUnits recordBegin,
                                       uint64_t bitfieldBitBegin) {
  assert(bitfield->isBitField());
  auto &ctx = CGM.getContext();
  auto width = bitfield->getBitWidthValue(ctx);

  // We can ignore zero-width bit-fields.
  if (width == 0) return;

  // toCharUnitsFromBits rounds down.
  CharUnits bitfieldByteBegin = ctx.toCharUnitsFromBits(bitfieldBitBegin);

  // Find the offset of the last byte that is partially occupied by the
  // bit-field; since we otherwise expect exclusive ends, the end is the
  // next byte.
  uint64_t bitfieldBitLast = bitfieldBitBegin + width - 1;
  CharUnits bitfieldByteEnd =
    ctx.toCharUnitsFromBits(bitfieldBitLast) + CharUnits::One();
  addOpaqueData(recordBegin + bitfieldByteBegin,
                recordBegin + bitfieldByteEnd);
}

void SwiftAggLowering::addTypedData(llvm::Type *type, CharUnits begin) {
  assert(type && "didn't provide type for typed data");
  addTypedData(type, begin, begin + getTypeStoreSize(CGM, type));
}

void SwiftAggLowering::addTypedData(llvm::Type *type,
                                    CharUnits begin, CharUnits end) {
  assert(type && "didn't provide type for typed data");
  assert(getTypeStoreSize(CGM, type) == end - begin);

  // Legalize vector types.
  if (auto vecTy = dyn_cast<llvm::VectorType>(type)) {
    SmallVector<llvm::Type*, 4> componentTys;
    legalizeVectorType(CGM, end - begin, vecTy, componentTys);
    assert(componentTys.size() >= 1);

    // Walk the initial components.
    for (size_t i = 0, e = componentTys.size(); i != e - 1; ++i) {
      llvm::Type *componentTy = componentTys[i];
      auto componentSize = getTypeStoreSize(CGM, componentTy);
      assert(componentSize < end - begin);
      addLegalTypedData(componentTy, begin, begin + componentSize);
      begin += componentSize;
    }

    return addLegalTypedData(componentTys.back(), begin, end);
  }

  // Legalize integer types.
  if (auto intTy = dyn_cast<llvm::IntegerType>(type)) {
    if (!isLegalIntegerType(CGM, intTy))
      return addOpaqueData(begin, end);
  }

  // All other types should be legal.
  return addLegalTypedData(type, begin, end);
}

void SwiftAggLowering::addLegalTypedData(llvm::Type *type,
                                         CharUnits begin, CharUnits end) {
  // Require the type to be naturally aligned.
  if (!begin.isZero() && !begin.isMultipleOf(getNaturalAlignment(CGM, type))) {

    // Try splitting vector types.
    if (auto vecTy = dyn_cast<llvm::VectorType>(type)) {
      auto split = splitLegalVectorType(CGM, end - begin, vecTy);
      auto eltTy = split.first;
      auto numElts = split.second;

      auto eltSize = (end - begin) / numElts;
      assert(eltSize == getTypeStoreSize(CGM, eltTy));
      for (size_t i = 0, e = numElts; i != e; ++i) {
        addLegalTypedData(eltTy, begin, begin + eltSize);
        begin += eltSize;
      }
      assert(begin == end);
      return;
    }

    return addOpaqueData(begin, end);
  }

  addEntry(type, begin, end);
}

void SwiftAggLowering::addEntry(llvm::Type *type,
                                CharUnits begin, CharUnits end) {
  assert((!type ||
          (!isa<llvm::StructType>(type) && !isa<llvm::ArrayType>(type))) &&
         "cannot add aggregate-typed data");
  assert(!type || begin.isMultipleOf(getNaturalAlignment(CGM, type)));

  // Fast path: we can just add entries to the end.
  if (Entries.empty() || Entries.back().End <= begin) {
    Entries.push_back({begin, end, type});
    return;
  }

  // Find the first existing entry that ends after the start of the new data.
  // TODO: do a binary search if Entries is big enough for it to matter.
  size_t index = Entries.size() - 1;
  while (index != 0) {
    if (Entries[index - 1].End <= begin) break;
    --index;
  }

  // The entry ends after the start of the new data.
  // If the entry starts after the end of the new data, there's no conflict.
  if (Entries[index].Begin >= end) {
    // This insertion is potentially O(n), but the way we generally build
    // these layouts makes that unlikely to matter: we'd need a union of
    // several very large types.
    Entries.insert(Entries.begin() + index, {begin, end, type});
    return;
  }

  // Otherwise, the ranges overlap.  The new range might also overlap
  // with later ranges.
restartAfterSplit:

  // Simplest case: an exact overlap.
  if (Entries[index].Begin == begin && Entries[index].End == end) {
    // If the types match exactly, great.
    if (Entries[index].Type == type) return;

    // If either type is opaque, make the entry opaque and return.
    if (Entries[index].Type == nullptr) {
      return;
    } else if (type == nullptr) {
      Entries[index].Type = nullptr;
      return;
    }

    // If they disagree in an ABI-agnostic way, just resolve the conflict
    // arbitrarily.
    if (auto entryType = getCommonType(Entries[index].Type, type)) {
      Entries[index].Type = entryType;
      return;
    }

    // Otherwise, make the entry opaque.
    Entries[index].Type = nullptr;
    return;
  }

  // Okay, we have an overlapping conflict of some sort.

  // If we have a vector type, split it.
  if (auto vecTy = dyn_cast_or_null<llvm::VectorType>(type)) {
    auto eltTy = vecTy->getElementType();
    CharUnits eltSize = (end - begin) / vecTy->getNumElements();
    assert(eltSize == getTypeStoreSize(CGM, eltTy));
    for (unsigned i = 0, e = vecTy->getNumElements(); i != e; ++i) {
      addEntry(eltTy, begin, begin + eltSize);
      begin += eltSize;
    }
    assert(begin == end);
    return;
  }

  // If the entry is a vector type, split it and try again.
  if (Entries[index].Type && Entries[index].Type->isVectorTy()) {
    splitVectorEntry(index);
    goto restartAfterSplit;
  }

  // Okay, we have no choice but to make the existing entry opaque.

  Entries[index].Type = nullptr;

  // Stretch the start of the entry to the beginning of the range.
  if (begin < Entries[index].Begin) {
    Entries[index].Begin = begin;
    assert(index == 0 || begin >= Entries[index - 1].End);
  }

  // Stretch the end of the entry to the end of the range; but if we run
  // into the start of the next entry, just leave the range there and repeat.
  while (end > Entries[index].End) {
    assert(Entries[index].Type == nullptr);

    // If the range doesn't overlap the next entry, we're done.
    if (index == Entries.size() - 1 || end <= Entries[index + 1].Begin) {
      Entries[index].End = end;
      break;
    }

    // Otherwise, stretch to the start of the next entry.
    Entries[index].End = Entries[index + 1].Begin;

    // Continue with the next entry.
    index++;

    // This entry needs to be made opaque if it is not already.
    if (Entries[index].Type == nullptr)
      continue;

    // Split vector entries unless we completely subsume them.
    if (Entries[index].Type->isVectorTy() &&
        end < Entries[index].End) {
      splitVectorEntry(index);
    }

    // Make the entry opaque.
    Entries[index].Type = nullptr;
  }
}

/// Replace the entry of vector type at offset 'index' with a sequence
/// of its component vectors.
void SwiftAggLowering::splitVectorEntry(unsigned index) {
  auto vecTy = cast<llvm::VectorType>(Entries[index].Type);
  auto split = splitLegalVectorType(CGM, Entries[index].getWidth(), vecTy);

  auto eltTy = split.first;
  CharUnits eltSize = getTypeStoreSize(CGM, eltTy);
  auto numElts = split.second;
  Entries.insert(Entries.begin() + index + 1, numElts - 1, StorageEntry());

  CharUnits begin = Entries[index].Begin;
  for (unsigned i = 0; i != numElts; ++i) {
    Entries[index].Type = eltTy;
    Entries[index].Begin = begin;
    Entries[index].End = begin + eltSize;
    begin += eltSize;
  }
}

/// Given a power-of-two unit size, return the offset of the aligned unit
/// of that size which contains the given offset.
///
/// In other words, round down to the nearest multiple of the unit size.
static CharUnits getOffsetAtStartOfUnit(CharUnits offset, CharUnits unitSize) {
  assert(isPowerOf2(unitSize.getQuantity()));
  auto unitMask = ~(unitSize.getQuantity() - 1);
  return CharUnits::fromQuantity(offset.getQuantity() & unitMask);
}

static bool areBytesInSameUnit(CharUnits first, CharUnits second,
                               CharUnits chunkSize) {
  return getOffsetAtStartOfUnit(first, chunkSize)
      == getOffsetAtStartOfUnit(second, chunkSize);
}

static bool isMergeableEntryType(llvm::Type *type) {
  // Opaquely-typed memory is always mergeable.
  if (type == nullptr) return true;

  // Pointers and integers are always mergeable.  In theory we should not
  // merge pointers, but (1) it doesn't currently matter in practice because
  // the chunk size is never greater than the size of a pointer and (2)
  // Swift IRGen uses integer types for a lot of things that are "really"
  // just storing pointers (like Optional<SomePointer>).  If we ever have a
  // target that would otherwise combine pointers, we should put some effort
  // into fixing those cases in Swift IRGen and then call out pointer types
  // here.

  // Floating-point and vector types should never be merged.
  // Most such types are too large and highly-aligned to ever trigger merging
  // in practice, but it's important for the rule to cover at least 'half'
  // and 'float', as well as things like small vectors of 'i1' or 'i8'.
  return (!type->isFloatingPointTy() && !type->isVectorTy());
}

bool SwiftAggLowering::shouldMergeEntries(const StorageEntry &first,
                                          const StorageEntry &second,
                                          CharUnits chunkSize) {
  // Only merge entries that overlap the same chunk.  We test this first
  // despite being a bit more expensive because this is the condition that
  // tends to prevent merging.
  if (!areBytesInSameUnit(first.End - CharUnits::One(), second.Begin,
                          chunkSize))
    return false;

  return (isMergeableEntryType(first.Type) &&
          isMergeableEntryType(second.Type));
}

void SwiftAggLowering::finish() {
  if (Entries.empty()) {
    Finished = true;
    return;
  }

  // We logically split the layout down into a series of chunks of this size,
  // which is generally the size of a pointer.
  const CharUnits chunkSize = getMaximumVoluntaryIntegerSize(CGM);

  // First pass: if two entries should be merged, make them both opaque
  // and stretch one to meet the next.
  // Also, remember if there are any opaque entries.
  bool hasOpaqueEntries = (Entries[0].Type == nullptr);
  for (size_t i = 1, e = Entries.size(); i != e; ++i) {
    if (shouldMergeEntries(Entries[i - 1], Entries[i], chunkSize)) {
      Entries[i - 1].Type = nullptr;
      Entries[i].Type = nullptr;
      Entries[i - 1].End = Entries[i].Begin;
      hasOpaqueEntries = true;

    } else if (Entries[i].Type == nullptr) {
      hasOpaqueEntries = true;
    }
  }

  // The rest of the algorithm leaves non-opaque entries alone, so if we
  // have no opaque entries, we're done.
  if (!hasOpaqueEntries) {
    Finished = true;
    return;
  }

  // Okay, move the entries to a temporary and rebuild Entries.
  auto orig = std::move(Entries);
  assert(Entries.empty());

  for (size_t i = 0, e = orig.size(); i != e; ++i) {
    // Just copy over non-opaque entries.
    if (orig[i].Type != nullptr) {
      Entries.push_back(orig[i]);
      continue;
    }

    // Scan forward to determine the full extent of the next opaque range.
    // We know from the first pass that only contiguous ranges will overlap
    // the same aligned chunk.
    auto begin = orig[i].Begin;
    auto end = orig[i].End;
    while (i + 1 != e &&
           orig[i + 1].Type == nullptr &&
           end == orig[i + 1].Begin) {
      end = orig[i + 1].End;
      i++;
    }

    // Add an entry per intersected chunk.
    do {
      // Find the smallest aligned storage unit in the maximal aligned
      // storage unit containing 'begin' that contains all the bytes in
      // the intersection between the range and this chunk.
      CharUnits localBegin = begin;
      CharUnits chunkBegin = getOffsetAtStartOfUnit(localBegin, chunkSize);
      CharUnits chunkEnd = chunkBegin + chunkSize;
      CharUnits localEnd = std::min(end, chunkEnd);

      // Just do a simple loop over ever-increasing unit sizes.
      CharUnits unitSize = CharUnits::One();
      CharUnits unitBegin, unitEnd;
      for (; ; unitSize *= 2) {
        assert(unitSize <= chunkSize);
        unitBegin = getOffsetAtStartOfUnit(localBegin, unitSize);
        unitEnd = unitBegin + unitSize;
        if (unitEnd >= localEnd) break;
      }

      // Add an entry for this unit.
      auto entryTy =
        llvm::IntegerType::get(CGM.getLLVMContext(),
                               CGM.getContext().toBits(unitSize));
      Entries.push_back({unitBegin, unitEnd, entryTy});

      // The next chunk starts where this chunk left off.
      begin = localEnd;
    } while (begin != end);
  }

  // Okay, finally finished.
  Finished = true;
}

void SwiftAggLowering::enumerateComponents(EnumerationCallback callback) const {
  assert(Finished && "haven't yet finished lowering");

  for (auto &entry : Entries) {
    callback(entry.Begin, entry.End, entry.Type);
  }
}

std::pair<llvm::StructType*, llvm::Type*>
SwiftAggLowering::getCoerceAndExpandTypes() const {
  assert(Finished && "haven't yet finished lowering");

  auto &ctx = CGM.getLLVMContext();

  if (Entries.empty()) {
    auto type = llvm::StructType::get(ctx);
    return { type, type };
  }

  SmallVector<llvm::Type*, 8> elts;
  CharUnits lastEnd = CharUnits::Zero();
  bool hasPadding = false;
  bool packed = false;
  for (auto &entry : Entries) {
    if (entry.Begin != lastEnd) {
      auto paddingSize = entry.Begin - lastEnd;
      assert(!paddingSize.isNegative());

      auto padding = llvm::ArrayType::get(llvm::Type::getInt8Ty(ctx),
                                          paddingSize.getQuantity());
      elts.push_back(padding);
      hasPadding = true;
    }

    if (!packed && !entry.Begin.isMultipleOf(
          CharUnits::fromQuantity(
            CGM.getDataLayout().getABITypeAlignment(entry.Type))))
      packed = true;

    elts.push_back(entry.Type);

    lastEnd = entry.Begin + getTypeAllocSize(CGM, entry.Type);
    assert(entry.End <= lastEnd);
  }

  // We don't need to adjust 'packed' to deal with possible tail padding
  // because we never do that kind of access through the coercion type.
  auto coercionType = llvm::StructType::get(ctx, elts, packed);

  llvm::Type *unpaddedType = coercionType;
  if (hasPadding) {
    elts.clear();
    for (auto &entry : Entries) {
      elts.push_back(entry.Type);
    }
    if (elts.size() == 1) {
      unpaddedType = elts[0];
    } else {
      unpaddedType = llvm::StructType::get(ctx, elts, /*packed*/ false);
    }
  } else if (Entries.size() == 1) {
    unpaddedType = Entries[0].Type;
  }

  return { coercionType, unpaddedType };
}

bool SwiftAggLowering::shouldPassIndirectly(bool asReturnValue) const {
  assert(Finished && "haven't yet finished lowering");

  // Empty types don't need to be passed indirectly.
  if (Entries.empty()) return false;

  // Avoid copying the array of types when there's just a single element.
  if (Entries.size() == 1) {
    return getSwiftABIInfo(CGM).shouldPassIndirectlyForSwift(
                                                           Entries.back().Type,
                                                             asReturnValue);
  }

  SmallVector<llvm::Type*, 8> componentTys;
  componentTys.reserve(Entries.size());
  for (auto &entry : Entries) {
    componentTys.push_back(entry.Type);
  }
  return getSwiftABIInfo(CGM).shouldPassIndirectlyForSwift(componentTys,
                                                           asReturnValue);
}

bool swiftcall::shouldPassIndirectly(CodeGenModule &CGM,
                                     ArrayRef<llvm::Type*> componentTys,
                                     bool asReturnValue) {
  return getSwiftABIInfo(CGM).shouldPassIndirectlyForSwift(componentTys,
                                                           asReturnValue);
}

CharUnits swiftcall::getMaximumVoluntaryIntegerSize(CodeGenModule &CGM) {
  // Currently always the size of an ordinary pointer.
  return CGM.getContext().toCharUnitsFromBits(
           CGM.getContext().getTargetInfo().getPointerWidth(0));
}

CharUnits swiftcall::getNaturalAlignment(CodeGenModule &CGM, llvm::Type *type) {
  // For Swift's purposes, this is always just the store size of the type
  // rounded up to a power of 2.
  auto size = (unsigned long long) getTypeStoreSize(CGM, type).getQuantity();
  if (!isPowerOf2(size)) {
    size = 1ULL << (llvm::findLastSet(size, llvm::ZB_Undefined) + 1);
  }
  assert(size >= CGM.getDataLayout().getABITypeAlignment(type));
  return CharUnits::fromQuantity(size);
}

bool swiftcall::isLegalIntegerType(CodeGenModule &CGM,
                                   llvm::IntegerType *intTy) {
  auto size = intTy->getBitWidth();
  switch (size) {
  case 1:
  case 8:
  case 16:
  case 32:
  case 64:
    // Just assume that the above are always legal.
    return true;

  case 128:
    return CGM.getContext().getTargetInfo().hasInt128Type();

  default:
    return false;
  }
}

bool swiftcall::isLegalVectorType(CodeGenModule &CGM, CharUnits vectorSize,
                                  llvm::VectorType *vectorTy) {
  return isLegalVectorType(CGM, vectorSize, vectorTy->getElementType(),
                           vectorTy->getNumElements());
}

bool swiftcall::isLegalVectorType(CodeGenModule &CGM, CharUnits vectorSize,
                                  llvm::Type *eltTy, unsigned numElts) {
  assert(numElts > 1 && "illegal vector length");
  return getSwiftABIInfo(CGM)
           .isLegalVectorTypeForSwift(vectorSize, eltTy, numElts);
}

std::pair<llvm::Type*, unsigned>
swiftcall::splitLegalVectorType(CodeGenModule &CGM, CharUnits vectorSize,
                                llvm::VectorType *vectorTy) {
  auto numElts = vectorTy->getNumElements();
  auto eltTy = vectorTy->getElementType();

  // Try to split the vector type in half.
  if (numElts >= 4 && isPowerOf2(numElts)) {
    if (isLegalVectorType(CGM, vectorSize / 2, eltTy, numElts / 2))
      return {llvm::VectorType::get(eltTy, numElts / 2), 2};
  }

  return {eltTy, numElts};
}

void swiftcall::legalizeVectorType(CodeGenModule &CGM, CharUnits origVectorSize,
                                   llvm::VectorType *origVectorTy,
                             llvm::SmallVectorImpl<llvm::Type*> &components) {
  // If it's already a legal vector type, use it.
  if (isLegalVectorType(CGM, origVectorSize, origVectorTy)) {
    components.push_back(origVectorTy);
    return;
  }

  // Try to split the vector into legal subvectors.
  auto numElts = origVectorTy->getNumElements();
  auto eltTy = origVectorTy->getElementType();
  assert(numElts != 1);

  // The largest size that we're still considering making subvectors of.
  // Always a power of 2.
  unsigned logCandidateNumElts = llvm::findLastSet(numElts, llvm::ZB_Undefined);
  unsigned candidateNumElts = 1U << logCandidateNumElts;
  assert(candidateNumElts <= numElts && candidateNumElts * 2 > numElts);

  // Minor optimization: don't check the legality of this exact size twice.
  if (candidateNumElts == numElts) {
    logCandidateNumElts--;
    candidateNumElts >>= 1;
  }

  CharUnits eltSize = (origVectorSize / numElts);
  CharUnits candidateSize = eltSize * candidateNumElts;

  // The sensibility of this algorithm relies on the fact that we never
  // have a legal non-power-of-2 vector size without having the power of 2
  // also be legal.
  while (logCandidateNumElts > 0) {
    assert(candidateNumElts == 1U << logCandidateNumElts);
    assert(candidateNumElts <= numElts);
    assert(candidateSize == eltSize * candidateNumElts);

    // Skip illegal vector sizes.
    if (!isLegalVectorType(CGM, candidateSize, eltTy, candidateNumElts)) {
      logCandidateNumElts--;
      candidateNumElts /= 2;
      candidateSize /= 2;
      continue;
    }

    // Add the right number of vectors of this size.
    auto numVecs = numElts >> logCandidateNumElts;
    components.append(numVecs, llvm::VectorType::get(eltTy, candidateNumElts));
    numElts -= (numVecs << logCandidateNumElts);

    if (numElts == 0) return;

    // It's possible that the number of elements remaining will be legal.
    // This can happen with e.g. <7 x float> when <3 x float> is legal.
    // This only needs to be separately checked if it's not a power of 2.
    if (numElts > 2 && !isPowerOf2(numElts) &&
        isLegalVectorType(CGM, eltSize * numElts, eltTy, numElts)) {
      components.push_back(llvm::VectorType::get(eltTy, numElts));
      return;
    }

    // Bring vecSize down to something no larger than numElts.
    do {
      logCandidateNumElts--;
      candidateNumElts /= 2;
      candidateSize /= 2;
    } while (candidateNumElts > numElts);
  }

  // Otherwise, just append a bunch of individual elements.
  components.append(numElts, eltTy);
}

bool swiftcall::mustPassRecordIndirectly(CodeGenModule &CGM,
                                         const RecordDecl *record) {
  // FIXME: should we not rely on the standard computation in Sema, just in
  // case we want to diverge from the platform ABI (e.g. on targets where
  // that uses the MSVC rule)?
  return !record->canPassInRegisters();
}

static ABIArgInfo classifyExpandedType(SwiftAggLowering &lowering,
                                       bool forReturn,
                                       CharUnits alignmentForIndirect) {
  if (lowering.empty()) {
    return ABIArgInfo::getIgnore();
  } else if (lowering.shouldPassIndirectly(forReturn)) {
    return ABIArgInfo::getIndirect(alignmentForIndirect, /*byval*/ false);
  } else {
    auto types = lowering.getCoerceAndExpandTypes();
    return ABIArgInfo::getCoerceAndExpand(types.first, types.second);
  }
}

static ABIArgInfo classifyType(CodeGenModule &CGM, CanQualType type,
                               bool forReturn) {
  if (auto recordType = dyn_cast<RecordType>(type)) {
    auto record = recordType->getDecl();
    auto &layout = CGM.getContext().getASTRecordLayout(record);

    if (mustPassRecordIndirectly(CGM, record))
      return ABIArgInfo::getIndirect(layout.getAlignment(), /*byval*/ false);

    SwiftAggLowering lowering(CGM);
    lowering.addTypedData(recordType->getDecl(), CharUnits::Zero(), layout);
    lowering.finish();

    return classifyExpandedType(lowering, forReturn, layout.getAlignment());
  }

  // Just assume that all of our target ABIs can support returning at least
  // two integer or floating-point values.
  if (isa<ComplexType>(type)) {
    return (forReturn ? ABIArgInfo::getDirect() : ABIArgInfo::getExpand());
  }

  // Vector types may need to be legalized.
  if (isa<VectorType>(type)) {
    SwiftAggLowering lowering(CGM);
    lowering.addTypedData(type, CharUnits::Zero());
    lowering.finish();

    CharUnits alignment = CGM.getContext().getTypeAlignInChars(type);
    return classifyExpandedType(lowering, forReturn, alignment);
  }

  // Member pointer types need to be expanded, but it's a simple form of
  // expansion that 'Direct' can handle.  Note that CanBeFlattened should be
  // true for this to work.

  // 'void' needs to be ignored.
  if (type->isVoidType()) {
    return ABIArgInfo::getIgnore();
  }

  // Everything else can be passed directly.
  return ABIArgInfo::getDirect();
}

ABIArgInfo swiftcall::classifyReturnType(CodeGenModule &CGM, CanQualType type) {
  return classifyType(CGM, type, /*forReturn*/ true);
}

ABIArgInfo swiftcall::classifyArgumentType(CodeGenModule &CGM,
                                           CanQualType type) {
  return classifyType(CGM, type, /*forReturn*/ false);
}

void swiftcall::computeABIInfo(CodeGenModule &CGM, CGFunctionInfo &FI) {
  auto &retInfo = FI.getReturnInfo();
  retInfo = classifyReturnType(CGM, FI.getReturnType());

  for (unsigned i = 0, e = FI.arg_size(); i != e; ++i) {
    auto &argInfo = FI.arg_begin()[i];
    argInfo.info = classifyArgumentType(CGM, argInfo.type);
  }
}

// Is swifterror lowered to a register by the target ABI.
bool swiftcall::isSwiftErrorLoweredInRegister(CodeGenModule &CGM) {
  return getSwiftABIInfo(CGM).isSwiftErrorInRegister();
}
