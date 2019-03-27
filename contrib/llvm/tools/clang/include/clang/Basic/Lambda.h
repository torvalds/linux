//===--- Lambda.h - Types for C++ Lambdas -----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines several types used to describe C++ lambda expressions
/// that are shared between the parser and AST.
///
//===----------------------------------------------------------------------===//


#ifndef LLVM_CLANG_BASIC_LAMBDA_H
#define LLVM_CLANG_BASIC_LAMBDA_H

namespace clang {

/// The default, if any, capture method for a lambda expression.
enum LambdaCaptureDefault {
  LCD_None,
  LCD_ByCopy,
  LCD_ByRef
};

/// The different capture forms in a lambda introducer
///
/// C++11 allows capture of \c this, or of local variables by copy or
/// by reference.  C++1y also allows "init-capture", where the initializer
/// is an expression.
enum LambdaCaptureKind {
  LCK_This,   ///< Capturing the \c *this object by reference
  LCK_StarThis, /// < Capturing the \c *this object by copy
  LCK_ByCopy, ///< Capturing by copy (a.k.a., by value)
  LCK_ByRef,  ///< Capturing by reference
  LCK_VLAType ///< Capturing variable-length array type
};

} // end namespace clang

#endif // LLVM_CLANG_BASIC_LAMBDA_H
