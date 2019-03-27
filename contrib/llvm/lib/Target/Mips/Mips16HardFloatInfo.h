//===---- Mips16HardFloatInfo.h for Mips16 Hard Float              --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines some data structures relevant to the implementation of
// Mips16 hard float.
//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MIPS_MIPS16HARDFLOATINFO_H
#define LLVM_LIB_TARGET_MIPS_MIPS16HARDFLOATINFO_H

namespace llvm {

namespace Mips16HardFloatInfo {

// Return types that matter for hard float are:
// float, double, complex float, and complex double
//
enum FPReturnVariant { FRet, DRet, CFRet, CDRet, NoFPRet };

//
// Parameter type that matter are float, (float, float), (float, double),
// double, (double, double), (double, float)
//
enum FPParamVariant { FSig, FFSig, FDSig, DSig, DDSig, DFSig, NoSig };

struct FuncSignature {
  FPParamVariant ParamSig;
  FPReturnVariant RetSig;
};

struct FuncNameSignature {
  const char *Name;
  FuncSignature Signature;
};

extern const FuncNameSignature PredefinedFuncs[];

extern FuncSignature const *findFuncSignature(const char *name);
}
}

#endif
