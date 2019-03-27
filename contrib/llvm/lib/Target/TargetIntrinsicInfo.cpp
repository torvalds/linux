//===-- TargetIntrinsicInfo.cpp - Target Instruction Information ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the TargetIntrinsicInfo class.
//
//===----------------------------------------------------------------------===//

#include "llvm/Target/TargetIntrinsicInfo.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/IR/Function.h"
using namespace llvm;

TargetIntrinsicInfo::TargetIntrinsicInfo() {
}

TargetIntrinsicInfo::~TargetIntrinsicInfo() {
}

unsigned TargetIntrinsicInfo::getIntrinsicID(const Function *F) const {
  const ValueName *ValName = F->getValueName();
  if (!ValName)
    return 0;
  return lookupName(ValName->getKeyData(), ValName->getKeyLength());
}
