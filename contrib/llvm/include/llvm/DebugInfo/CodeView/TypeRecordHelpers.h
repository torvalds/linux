//===- TypeRecordHelpers.h --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_TYPERECORDHELPERS_H
#define LLVM_DEBUGINFO_CODEVIEW_TYPERECORDHELPERS_H

#include "llvm/DebugInfo/CodeView/TypeRecord.h"

namespace llvm {
  namespace codeview {
    /// Given an arbitrary codeview type, determine if it is an LF_STRUCTURE,
    /// LF_CLASS, LF_INTERFACE, LF_UNION, or LF_ENUM with the forward ref class
    /// option.
    bool isUdtForwardRef(CVType CVT);

    /// Given a CVType which is assumed to be an LF_MODIFIER, return the
    /// TypeIndex of the type that the LF_MODIFIER modifies.
    TypeIndex getModifiedType(const CVType &CVT);
  }
}

#endif
