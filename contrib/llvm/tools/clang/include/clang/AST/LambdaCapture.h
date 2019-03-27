//===--- LambdaCapture.h - Types for C++ Lambda Captures --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines the LambdaCapture class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_LAMBDACAPTURE_H
#define LLVM_CLANG_AST_LAMBDACAPTURE_H

#include "clang/AST/Decl.h"
#include "clang/Basic/Lambda.h"
#include "llvm/ADT/PointerIntPair.h"

namespace clang {

/// Describes the capture of a variable or of \c this, or of a
/// C++1y init-capture.
class LambdaCapture {
  enum {
    /// Flag used by the Capture class to indicate that the given
    /// capture was implicit.
    Capture_Implicit = 0x01,

    /// Flag used by the Capture class to indicate that the
    /// given capture was by-copy.
    ///
    /// This includes the case of a non-reference init-capture.
    Capture_ByCopy = 0x02,

    /// Flag used by the Capture class to distinguish between a capture
    /// of '*this' and a capture of a VLA type.
    Capture_This = 0x04
  };

  // Decl could represent:
  // - a VarDecl* that represents the variable that was captured or the
  //   init-capture.
  // - or, is a nullptr and Capture_This is set in Bits if this represents a
  //   capture of '*this' by value or reference.
  // - or, is a nullptr and Capture_This is not set in Bits if this represents
  //   a capture of a VLA type.
  llvm::PointerIntPair<Decl*, 3> DeclAndBits;

  SourceLocation Loc;
  SourceLocation EllipsisLoc;

  friend class ASTStmtReader;
  friend class ASTStmtWriter;

public:
  /// Create a new capture of a variable or of \c this.
  ///
  /// \param Loc The source location associated with this capture.
  ///
  /// \param Kind The kind of capture (this, byref, bycopy), which must
  /// not be init-capture.
  ///
  /// \param Implicit Whether the capture was implicit or explicit.
  ///
  /// \param Var The local variable being captured, or null if capturing
  /// \c this.
  ///
  /// \param EllipsisLoc The location of the ellipsis (...) for a
  /// capture that is a pack expansion, or an invalid source
  /// location to indicate that this is not a pack expansion.
  LambdaCapture(SourceLocation Loc, bool Implicit, LambdaCaptureKind Kind,
                VarDecl *Var = nullptr,
                SourceLocation EllipsisLoc = SourceLocation());

  /// Determine the kind of capture.
  LambdaCaptureKind getCaptureKind() const;

  /// Determine whether this capture handles the C++ \c this
  /// pointer.
  bool capturesThis() const {
    return DeclAndBits.getPointer() == nullptr &&
          (DeclAndBits.getInt() & Capture_This);
  }

  /// Determine whether this capture handles a variable.
  bool capturesVariable() const {
    return dyn_cast_or_null<VarDecl>(DeclAndBits.getPointer());
  }

  /// Determine whether this captures a variable length array bound
  /// expression.
  bool capturesVLAType() const {
    return DeclAndBits.getPointer() == nullptr &&
           !(DeclAndBits.getInt() & Capture_This);
  }

  /// Retrieve the declaration of the local variable being
  /// captured.
  ///
  /// This operation is only valid if this capture is a variable capture
  /// (other than a capture of \c this).
  VarDecl *getCapturedVar() const {
    assert(capturesVariable() && "No variable available for capture");
    return static_cast<VarDecl *>(DeclAndBits.getPointer());
  }

  /// Determine whether this was an implicit capture (not
  /// written between the square brackets introducing the lambda).
  bool isImplicit() const {
    return DeclAndBits.getInt() & Capture_Implicit;
  }

  /// Determine whether this was an explicit capture (written
  /// between the square brackets introducing the lambda).
  bool isExplicit() const { return !isImplicit(); }

  /// Retrieve the source location of the capture.
  ///
  /// For an explicit capture, this returns the location of the
  /// explicit capture in the source. For an implicit capture, this
  /// returns the location at which the variable or \c this was first
  /// used.
  SourceLocation getLocation() const { return Loc; }

  /// Determine whether this capture is a pack expansion,
  /// which captures a function parameter pack.
  bool isPackExpansion() const { return EllipsisLoc.isValid(); }

  /// Retrieve the location of the ellipsis for a capture
  /// that is a pack expansion.
  SourceLocation getEllipsisLoc() const {
    assert(isPackExpansion() && "No ellipsis location for a non-expansion");
    return EllipsisLoc;
  }
};

} // end namespace clang

#endif // LLVM_CLANG_AST_LAMBDACAPTURE_H
