//===-- ExpressionTypeSystemHelper.h ---------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef ExpressionTypeSystemHelper_h
#define ExpressionTypeSystemHelper_h

#include "llvm/Support/Casting.h"

namespace lldb_private {

//----------------------------------------------------------------------
/// @class ExpressionTypeSystemHelper ExpressionTypeSystemHelper.h
/// "lldb/Expression/ExpressionTypeSystemHelper.h"
/// A helper object that the Expression can pass to its ExpressionParser
/// to provide generic information that
/// any type of expression will need to supply.  It's only job is to support
/// dyn_cast so that the expression parser can cast it back to the requisite
/// specific type.
///
//----------------------------------------------------------------------

class ExpressionTypeSystemHelper {
public:
  enum LLVMCastKind {
    eKindClangHelper,
    eKindSwiftHelper,
    eKindGoHelper,
    kNumKinds
  };

  LLVMCastKind getKind() const { return m_kind; }

  ExpressionTypeSystemHelper(LLVMCastKind kind) : m_kind(kind) {}

  ~ExpressionTypeSystemHelper() {}

protected:
  LLVMCastKind m_kind;
};

} // namespace lldb_private

#endif /* ExpressionTypeSystemHelper_h */
