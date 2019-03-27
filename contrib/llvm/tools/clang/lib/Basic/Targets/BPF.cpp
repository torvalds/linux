//===--- BPF.cpp - Implement BPF target feature support -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements BPF TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#include "BPF.h"
#include "Targets.h"
#include "clang/Basic/MacroBuilder.h"
#include "llvm/ADT/StringRef.h"

using namespace clang;
using namespace clang::targets;

void BPFTargetInfo::getTargetDefines(const LangOptions &Opts,
                                     MacroBuilder &Builder) const {
  DefineStd(Builder, "bpf", Opts);
  Builder.defineMacro("__BPF__");
}

static constexpr llvm::StringLiteral ValidCPUNames[] = {"generic", "v1", "v2",
                                                        "probe"};

bool BPFTargetInfo::isValidCPUName(StringRef Name) const {
  return llvm::find(ValidCPUNames, Name) != std::end(ValidCPUNames);
}

void BPFTargetInfo::fillValidCPUList(SmallVectorImpl<StringRef> &Values) const {
  Values.append(std::begin(ValidCPUNames), std::end(ValidCPUNames));
}
