//===-- ObjCARC.h - ObjCARC Scalar Transformations --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This header file defines prototypes for accessor functions that expose passes
// in the ObjCARC Scalar Transformations library.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_OBJCARC_H
#define LLVM_TRANSFORMS_OBJCARC_H

namespace llvm {

class Pass;

//===----------------------------------------------------------------------===//
//
// ObjCARCAPElim - ObjC ARC autorelease pool elimination.
//
Pass *createObjCARCAPElimPass();

//===----------------------------------------------------------------------===//
//
// ObjCARCExpand - ObjC ARC preliminary simplifications.
//
Pass *createObjCARCExpandPass();

//===----------------------------------------------------------------------===//
//
// ObjCARCContract - Late ObjC ARC cleanups.
//
Pass *createObjCARCContractPass();

//===----------------------------------------------------------------------===//
//
// ObjCARCOpt - ObjC ARC optimization.
//
Pass *createObjCARCOptPass();

} // End llvm namespace

#endif
