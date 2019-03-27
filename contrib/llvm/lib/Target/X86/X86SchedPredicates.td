//===-- X86SchedPredicates.td - X86 Scheduling Predicates --*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines scheduling predicate definitions that are common to
// all X86 subtargets.
//
//===----------------------------------------------------------------------===//

// A predicate used to identify dependency-breaking instructions that clear the
// content of the destination register. Note that this predicate only checks if
// input registers are the same. This predicate doesn't make any assumptions on
// the expected instruction opcodes, because different processors may implement
// different zero-idioms.
def ZeroIdiomPredicate : CheckSameRegOperand<1, 2>;

// A predicate used to identify VPERM that have bits 3 and 7 of their mask set.
// On some processors, these VPERM instructions are zero-idioms.
def ZeroIdiomVPERMPredicate : CheckAll<[
  ZeroIdiomPredicate,
  CheckImmOperand<3, 0x88>
]>;

// A predicate used to check if a LEA instruction uses all three source
// operands: base, index, and offset.
def IsThreeOperandsLEAPredicate: CheckAll<[
  // isRegOperand(Base)
  CheckIsRegOperand<1>,
  CheckNot<CheckInvalidRegOperand<1>>,

  // isRegOperand(Index)
  CheckIsRegOperand<3>,
  CheckNot<CheckInvalidRegOperand<3>>,

  // hasLEAOffset(Offset)
  CheckAny<[
    CheckAll<[
      CheckIsImmOperand<4>,
      CheckNot<CheckZeroOperand<4>>
    ]>,
    CheckNonPortable<"MI.getOperand(4).isGlobal()">
  ]>
]>;

def LEACases : MCOpcodeSwitchCase<
    [LEA32r, LEA64r, LEA64_32r, LEA16r],
    MCReturnStatement<IsThreeOperandsLEAPredicate>
>;

// Used to generate the body of a TII member function.
def IsThreeOperandsLEABody :
    MCOpcodeSwitchStatement<[LEACases], MCReturnStatement<FalsePred>>;

// This predicate evaluates to true only if the input machine instruction is a
// 3-operands LEA.  Tablegen automatically generates a new method for it in
// X86GenInstrInfo.
def IsThreeOperandsLEAFn :
    TIIPredicate<"isThreeOperandsLEA", IsThreeOperandsLEABody>;
