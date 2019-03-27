//===- DIAUtils.h - Utility functions for working with DIA ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_DIA_DIAUTILS_H
#define LLVM_DEBUGINFO_PDB_DIA_DIAUTILS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/ConvertUTF.h"

template <typename Obj>
std::string invokeBstrMethod(Obj &Object,
                             HRESULT (__stdcall Obj::*Func)(BSTR *)) {
  CComBSTR Str16;
  HRESULT Result = (Object.*Func)(&Str16);
  if (S_OK != Result)
    return std::string();

  std::string Str8;
  llvm::ArrayRef<char> StrBytes(reinterpret_cast<char *>(Str16.m_str),
                                Str16.ByteLength());
  llvm::convertUTF16ToUTF8String(StrBytes, Str8);
  return Str8;
}

#endif // LLVM_DEBUGINFO_PDB_DIA_DIAUTILS_H
