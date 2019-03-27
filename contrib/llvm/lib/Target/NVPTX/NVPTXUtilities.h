//===-- NVPTXUtilities - Utilities -----------------------------*- C++ -*-====//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the NVVM specific utility functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_NVPTX_NVPTXUTILITIES_H
#define LLVM_LIB_TARGET_NVPTX_NVPTXUTILITIES_H

#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Value.h"
#include <cstdarg>
#include <set>
#include <string>
#include <vector>

namespace llvm {

void clearAnnotationCache(const Module *);

bool findOneNVVMAnnotation(const GlobalValue *, const std::string &,
                           unsigned &);
bool findAllNVVMAnnotation(const GlobalValue *, const std::string &,
                           std::vector<unsigned> &);

bool isTexture(const Value &);
bool isSurface(const Value &);
bool isSampler(const Value &);
bool isImage(const Value &);
bool isImageReadOnly(const Value &);
bool isImageWriteOnly(const Value &);
bool isImageReadWrite(const Value &);
bool isManaged(const Value &);

std::string getTextureName(const Value &);
std::string getSurfaceName(const Value &);
std::string getSamplerName(const Value &);

bool getMaxNTIDx(const Function &, unsigned &);
bool getMaxNTIDy(const Function &, unsigned &);
bool getMaxNTIDz(const Function &, unsigned &);

bool getReqNTIDx(const Function &, unsigned &);
bool getReqNTIDy(const Function &, unsigned &);
bool getReqNTIDz(const Function &, unsigned &);

bool getMinCTASm(const Function &, unsigned &);
bool getMaxNReg(const Function &, unsigned &);
bool isKernelFunction(const Function &);

bool getAlign(const Function &, unsigned index, unsigned &);
bool getAlign(const CallInst &, unsigned index, unsigned &);

}

#endif
