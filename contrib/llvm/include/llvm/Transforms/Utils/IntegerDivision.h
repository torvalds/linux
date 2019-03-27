//===- llvm/Transforms/Utils/IntegerDivision.h ------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains an implementation of 32bit and 64bit scalar integer
// division for targets that don't have native support. It's largely derived
// from compiler-rt's implementations of __udivsi3 and __udivmoddi4,
// but hand-tuned for targets that prefer less control flow.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_INTEGERDIVISION_H
#define LLVM_TRANSFORMS_UTILS_INTEGERDIVISION_H

namespace llvm {
  class BinaryOperator;
}

namespace llvm {

  /// Generate code to calculate the remainder of two integers, replacing Rem
  /// with the generated code. This currently generates code using the udiv
  /// expansion, but future work includes generating more specialized code,
  /// e.g. when more information about the operands are known. Implements both
  /// 32bit and 64bit scalar division.
  ///
  /// Replace Rem with generated code.
  bool expandRemainder(BinaryOperator *Rem);

  /// Generate code to divide two integers, replacing Div with the generated
  /// code. This currently generates code similarly to compiler-rt's
  /// implementations, but future work includes generating more specialized code
  /// when more information about the operands are known. Implements both
  /// 32bit and 64bit scalar division.
  ///
  /// Replace Div with generated code.
  bool expandDivision(BinaryOperator* Div);

  /// Generate code to calculate the remainder of two integers, replacing Rem
  /// with the generated code. Uses ExpandReminder with a 32bit Rem which
  /// makes it useful for targets with little or no support for less than
  /// 32 bit arithmetic.
  ///
  /// Replace Rem with generated code.
  bool expandRemainderUpTo32Bits(BinaryOperator *Rem);

  /// Generate code to calculate the remainder of two integers, replacing Rem
  /// with the generated code. Uses ExpandReminder with a 64bit Rem.
  ///
  /// Replace Rem with generated code.
  bool expandRemainderUpTo64Bits(BinaryOperator *Rem);

  /// Generate code to divide two integers, replacing Div with the generated
  /// code. Uses ExpandDivision with a 32bit Div which makes it useful for
  /// targets with little or no support for less than 32 bit arithmetic.
  ///
  /// Replace Rem with generated code.
  bool expandDivisionUpTo32Bits(BinaryOperator *Div);

  /// Generate code to divide two integers, replacing Div with the generated
  /// code. Uses ExpandDivision with a 64bit Div.
  ///
  /// Replace Rem with generated code.
  bool expandDivisionUpTo64Bits(BinaryOperator *Div);

} // End llvm namespace

#endif
