//===- FunctionComparator.h - Function Comparator ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the FunctionComparator and GlobalNumberState classes which
// are used by the MergeFunctions pass for comparing functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_FUNCTIONCOMPARATOR_H
#define LLVM_TRANSFORMS_UTILS_FUNCTIONCOMPARATOR_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Casting.h"
#include <cstdint>
#include <tuple>

namespace llvm {

class APFloat;
class APInt;
class BasicBlock;
class Constant;
class Function;
class GlobalValue;
class InlineAsm;
class Instruction;
class MDNode;
class Type;
class Value;

/// GlobalNumberState assigns an integer to each global value in the program,
/// which is used by the comparison routine to order references to globals. This
/// state must be preserved throughout the pass, because Functions and other
/// globals need to maintain their relative order. Globals are assigned a number
/// when they are first visited. This order is deterministic, and so the
/// assigned numbers are as well. When two functions are merged, neither number
/// is updated. If the symbols are weak, this would be incorrect. If they are
/// strong, then one will be replaced at all references to the other, and so
/// direct callsites will now see one or the other symbol, and no update is
/// necessary. Note that if we were guaranteed unique names, we could just
/// compare those, but this would not work for stripped bitcodes or for those
/// few symbols without a name.
class GlobalNumberState {
  struct Config : ValueMapConfig<GlobalValue *> {
    enum { FollowRAUW = false };
  };

  // Each GlobalValue is mapped to an identifier. The Config ensures when RAUW
  // occurs, the mapping does not change. Tracking changes is unnecessary, and
  // also problematic for weak symbols (which may be overwritten).
  using ValueNumberMap = ValueMap<GlobalValue *, uint64_t, Config>;
  ValueNumberMap GlobalNumbers;

  // The next unused serial number to assign to a global.
  uint64_t NextNumber = 0;

public:
  GlobalNumberState() = default;

  uint64_t getNumber(GlobalValue* Global) {
    ValueNumberMap::iterator MapIter;
    bool Inserted;
    std::tie(MapIter, Inserted) = GlobalNumbers.insert({Global, NextNumber});
    if (Inserted)
      NextNumber++;
    return MapIter->second;
  }

  void erase(GlobalValue *Global) {
    GlobalNumbers.erase(Global);
  }

  void clear() {
    GlobalNumbers.clear();
  }
};

/// FunctionComparator - Compares two functions to determine whether or not
/// they will generate machine code with the same behaviour. DataLayout is
/// used if available. The comparator always fails conservatively (erring on the
/// side of claiming that two functions are different).
class FunctionComparator {
public:
  FunctionComparator(const Function *F1, const Function *F2,
                     GlobalNumberState* GN)
      : FnL(F1), FnR(F2), GlobalNumbers(GN) {}

  /// Test whether the two functions have equivalent behaviour.
  int compare();

  /// Hash a function. Equivalent functions will have the same hash, and unequal
  /// functions will have different hashes with high probability.
  using FunctionHash = uint64_t;
  static FunctionHash functionHash(Function &);

protected:
  /// Start the comparison.
  void beginCompare() {
    sn_mapL.clear();
    sn_mapR.clear();
  }

  /// Compares the signature and other general attributes of the two functions.
  int compareSignature() const;

  /// Test whether two basic blocks have equivalent behaviour.
  int cmpBasicBlocks(const BasicBlock *BBL, const BasicBlock *BBR) const;

  /// Constants comparison.
  /// Its analog to lexicographical comparison between hypothetical numbers
  /// of next format:
  /// <bitcastability-trait><raw-bit-contents>
  ///
  /// 1. Bitcastability.
  /// Check whether L's type could be losslessly bitcasted to R's type.
  /// On this stage method, in case when lossless bitcast is not possible
  /// method returns -1 or 1, thus also defining which type is greater in
  /// context of bitcastability.
  /// Stage 0: If types are equal in terms of cmpTypes, then we can go straight
  ///          to the contents comparison.
  ///          If types differ, remember types comparison result and check
  ///          whether we still can bitcast types.
  /// Stage 1: Types that satisfies isFirstClassType conditions are always
  ///          greater then others.
  /// Stage 2: Vector is greater then non-vector.
  ///          If both types are vectors, then vector with greater bitwidth is
  ///          greater.
  ///          If both types are vectors with the same bitwidth, then types
  ///          are bitcastable, and we can skip other stages, and go to contents
  ///          comparison.
  /// Stage 3: Pointer types are greater than non-pointers. If both types are
  ///          pointers of the same address space - go to contents comparison.
  ///          Different address spaces: pointer with greater address space is
  ///          greater.
  /// Stage 4: Types are neither vectors, nor pointers. And they differ.
  ///          We don't know how to bitcast them. So, we better don't do it,
  ///          and return types comparison result (so it determines the
  ///          relationship among constants we don't know how to bitcast).
  ///
  /// Just for clearance, let's see how the set of constants could look
  /// on single dimension axis:
  ///
  /// [NFCT], [FCT, "others"], [FCT, pointers], [FCT, vectors]
  /// Where: NFCT - Not a FirstClassType
  ///        FCT - FirstClassTyp:
  ///
  /// 2. Compare raw contents.
  /// It ignores types on this stage and only compares bits from L and R.
  /// Returns 0, if L and R has equivalent contents.
  /// -1 or 1 if values are different.
  /// Pretty trivial:
  /// 2.1. If contents are numbers, compare numbers.
  ///    Ints with greater bitwidth are greater. Ints with same bitwidths
  ///    compared by their contents.
  /// 2.2. "And so on". Just to avoid discrepancies with comments
  /// perhaps it would be better to read the implementation itself.
  /// 3. And again about overall picture. Let's look back at how the ordered set
  /// of constants will look like:
  /// [NFCT], [FCT, "others"], [FCT, pointers], [FCT, vectors]
  ///
  /// Now look, what could be inside [FCT, "others"], for example:
  /// [FCT, "others"] =
  /// [
  ///   [double 0.1], [double 1.23],
  ///   [i32 1], [i32 2],
  ///   { double 1.0 },       ; StructTyID, NumElements = 1
  ///   { i32 1 },            ; StructTyID, NumElements = 1
  ///   { double 1, i32 1 },  ; StructTyID, NumElements = 2
  ///   { i32 1, double 1 }   ; StructTyID, NumElements = 2
  /// ]
  ///
  /// Let's explain the order. Float numbers will be less than integers, just
  /// because of cmpType terms: FloatTyID < IntegerTyID.
  /// Floats (with same fltSemantics) are sorted according to their value.
  /// Then you can see integers, and they are, like a floats,
  /// could be easy sorted among each others.
  /// The structures. Structures are grouped at the tail, again because of their
  /// TypeID: StructTyID > IntegerTyID > FloatTyID.
  /// Structures with greater number of elements are greater. Structures with
  /// greater elements going first are greater.
  /// The same logic with vectors, arrays and other possible complex types.
  ///
  /// Bitcastable constants.
  /// Let's assume, that some constant, belongs to some group of
  /// "so-called-equal" values with different types, and at the same time
  /// belongs to another group of constants with equal types
  /// and "really" equal values.
  ///
  /// Now, prove that this is impossible:
  ///
  /// If constant A with type TyA is bitcastable to B with type TyB, then:
  /// 1. All constants with equal types to TyA, are bitcastable to B. Since
  ///    those should be vectors (if TyA is vector), pointers
  ///    (if TyA is pointer), or else (if TyA equal to TyB), those types should
  ///    be equal to TyB.
  /// 2. All constants with non-equal, but bitcastable types to TyA, are
  ///    bitcastable to B.
  ///    Once again, just because we allow it to vectors and pointers only.
  ///    This statement could be expanded as below:
  /// 2.1. All vectors with equal bitwidth to vector A, has equal bitwidth to
  ///      vector B, and thus bitcastable to B as well.
  /// 2.2. All pointers of the same address space, no matter what they point to,
  ///      bitcastable. So if C is pointer, it could be bitcasted to A and to B.
  /// So any constant equal or bitcastable to A is equal or bitcastable to B.
  /// QED.
  ///
  /// In another words, for pointers and vectors, we ignore top-level type and
  /// look at their particular properties (bit-width for vectors, and
  /// address space for pointers).
  /// If these properties are equal - compare their contents.
  int cmpConstants(const Constant *L, const Constant *R) const;

  /// Compares two global values by number. Uses the GlobalNumbersState to
  /// identify the same gobals across function calls.
  int cmpGlobalValues(GlobalValue *L, GlobalValue *R) const;

  /// Assign or look up previously assigned numbers for the two values, and
  /// return whether the numbers are equal. Numbers are assigned in the order
  /// visited.
  /// Comparison order:
  /// Stage 0: Value that is function itself is always greater then others.
  ///          If left and right values are references to their functions, then
  ///          they are equal.
  /// Stage 1: Constants are greater than non-constants.
  ///          If both left and right are constants, then the result of
  ///          cmpConstants is used as cmpValues result.
  /// Stage 2: InlineAsm instances are greater than others. If both left and
  ///          right are InlineAsm instances, InlineAsm* pointers casted to
  ///          integers and compared as numbers.
  /// Stage 3: For all other cases we compare order we meet these values in
  ///          their functions. If right value was met first during scanning,
  ///          then left value is greater.
  ///          In another words, we compare serial numbers, for more details
  ///          see comments for sn_mapL and sn_mapR.
  int cmpValues(const Value *L, const Value *R) const;

  /// Compare two Instructions for equivalence, similar to
  /// Instruction::isSameOperationAs.
  ///
  /// Stages are listed in "most significant stage first" order:
  /// On each stage below, we do comparison between some left and right
  /// operation parts. If parts are non-equal, we assign parts comparison
  /// result to the operation comparison result and exit from method.
  /// Otherwise we proceed to the next stage.
  /// Stages:
  /// 1. Operations opcodes. Compared as numbers.
  /// 2. Number of operands.
  /// 3. Operation types. Compared with cmpType method.
  /// 4. Compare operation subclass optional data as stream of bytes:
  /// just convert it to integers and call cmpNumbers.
  /// 5. Compare in operation operand types with cmpType in
  /// most significant operand first order.
  /// 6. Last stage. Check operations for some specific attributes.
  /// For example, for Load it would be:
  /// 6.1.Load: volatile (as boolean flag)
  /// 6.2.Load: alignment (as integer numbers)
  /// 6.3.Load: ordering (as underlying enum class value)
  /// 6.4.Load: synch-scope (as integer numbers)
  /// 6.5.Load: range metadata (as integer ranges)
  /// On this stage its better to see the code, since its not more than 10-15
  /// strings for particular instruction, and could change sometimes.
  ///
  /// Sets \p needToCmpOperands to true if the operands of the instructions
  /// still must be compared afterwards. In this case it's already guaranteed
  /// that both instructions have the same number of operands.
  int cmpOperations(const Instruction *L, const Instruction *R,
                    bool &needToCmpOperands) const;

  /// cmpType - compares two types,
  /// defines total ordering among the types set.
  ///
  /// Return values:
  /// 0 if types are equal,
  /// -1 if Left is less than Right,
  /// +1 if Left is greater than Right.
  ///
  /// Description:
  /// Comparison is broken onto stages. Like in lexicographical comparison
  /// stage coming first has higher priority.
  /// On each explanation stage keep in mind total ordering properties.
  ///
  /// 0. Before comparison we coerce pointer types of 0 address space to
  /// integer.
  /// We also don't bother with same type at left and right, so
  /// just return 0 in this case.
  ///
  /// 1. If types are of different kind (different type IDs).
  ///    Return result of type IDs comparison, treating them as numbers.
  /// 2. If types are integers, check that they have the same width. If they
  /// are vectors, check that they have the same count and subtype.
  /// 3. Types have the same ID, so check whether they are one of:
  /// * Void
  /// * Float
  /// * Double
  /// * X86_FP80
  /// * FP128
  /// * PPC_FP128
  /// * Label
  /// * Metadata
  /// We can treat these types as equal whenever their IDs are same.
  /// 4. If Left and Right are pointers, return result of address space
  /// comparison (numbers comparison). We can treat pointer types of same
  /// address space as equal.
  /// 5. If types are complex.
  /// Then both Left and Right are to be expanded and their element types will
  /// be checked with the same way. If we get Res != 0 on some stage, return it.
  /// Otherwise return 0.
  /// 6. For all other cases put llvm_unreachable.
  int cmpTypes(Type *TyL, Type *TyR) const;

  int cmpNumbers(uint64_t L, uint64_t R) const;
  int cmpAPInts(const APInt &L, const APInt &R) const;
  int cmpAPFloats(const APFloat &L, const APFloat &R) const;
  int cmpMem(StringRef L, StringRef R) const;

  // The two functions undergoing comparison.
  const Function *FnL, *FnR;

private:
  int cmpOrderings(AtomicOrdering L, AtomicOrdering R) const;
  int cmpInlineAsm(const InlineAsm *L, const InlineAsm *R) const;
  int cmpAttrs(const AttributeList L, const AttributeList R) const;
  int cmpRangeMetadata(const MDNode *L, const MDNode *R) const;
  int cmpOperandBundlesSchema(const Instruction *L, const Instruction *R) const;

  /// Compare two GEPs for equivalent pointer arithmetic.
  /// Parts to be compared for each comparison stage,
  /// most significant stage first:
  /// 1. Address space. As numbers.
  /// 2. Constant offset, (using GEPOperator::accumulateConstantOffset method).
  /// 3. Pointer operand type (using cmpType method).
  /// 4. Number of operands.
  /// 5. Compare operands, using cmpValues method.
  int cmpGEPs(const GEPOperator *GEPL, const GEPOperator *GEPR) const;
  int cmpGEPs(const GetElementPtrInst *GEPL,
              const GetElementPtrInst *GEPR) const {
    return cmpGEPs(cast<GEPOperator>(GEPL), cast<GEPOperator>(GEPR));
  }

  /// Assign serial numbers to values from left function, and values from
  /// right function.
  /// Explanation:
  /// Being comparing functions we need to compare values we meet at left and
  /// right sides.
  /// Its easy to sort things out for external values. It just should be
  /// the same value at left and right.
  /// But for local values (those were introduced inside function body)
  /// we have to ensure they were introduced at exactly the same place,
  /// and plays the same role.
  /// Let's assign serial number to each value when we meet it first time.
  /// Values that were met at same place will be with same serial numbers.
  /// In this case it would be good to explain few points about values assigned
  /// to BBs and other ways of implementation (see below).
  ///
  /// 1. Safety of BB reordering.
  /// It's safe to change the order of BasicBlocks in function.
  /// Relationship with other functions and serial numbering will not be
  /// changed in this case.
  /// As follows from FunctionComparator::compare(), we do CFG walk: we start
  /// from the entry, and then take each terminator. So it doesn't matter how in
  /// fact BBs are ordered in function. And since cmpValues are called during
  /// this walk, the numbering depends only on how BBs located inside the CFG.
  /// So the answer is - yes. We will get the same numbering.
  ///
  /// 2. Impossibility to use dominance properties of values.
  /// If we compare two instruction operands: first is usage of local
  /// variable AL from function FL, and second is usage of local variable AR
  /// from FR, we could compare their origins and check whether they are
  /// defined at the same place.
  /// But, we are still not able to compare operands of PHI nodes, since those
  /// could be operands from further BBs we didn't scan yet.
  /// So it's impossible to use dominance properties in general.
  mutable DenseMap<const Value*, int> sn_mapL, sn_mapR;

  // The global state we will use
  GlobalNumberState* GlobalNumbers;
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_FUNCTIONCOMPARATOR_H
