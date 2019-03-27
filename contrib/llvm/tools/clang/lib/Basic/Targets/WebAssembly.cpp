//===--- WebAssembly.cpp - Implement WebAssembly target feature support ---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements WebAssembly TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#include "WebAssembly.h"
#include "Targets.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/TargetBuiltins.h"
#include "llvm/ADT/StringSwitch.h"

using namespace clang;
using namespace clang::targets;

const Builtin::Info WebAssemblyTargetInfo::BuiltinInfo[] = {
#define BUILTIN(ID, TYPE, ATTRS)                                               \
  {#ID, TYPE, ATTRS, nullptr, ALL_LANGUAGES, nullptr},
#define TARGET_BUILTIN(ID, TYPE, ATTRS, FEATURE)                               \
  {#ID, TYPE, ATTRS, nullptr, ALL_LANGUAGES, FEATURE},
#define LIBBUILTIN(ID, TYPE, ATTRS, HEADER)                                    \
  {#ID, TYPE, ATTRS, HEADER, ALL_LANGUAGES, nullptr},
#include "clang/Basic/BuiltinsWebAssembly.def"
};

static constexpr llvm::StringLiteral ValidCPUNames[] = {
    {"mvp"}, {"bleeding-edge"}, {"generic"}};

bool WebAssemblyTargetInfo::hasFeature(StringRef Feature) const {
  return llvm::StringSwitch<bool>(Feature)
      .Case("simd128", SIMDLevel >= SIMD128)
      .Case("unimplemented-simd128", SIMDLevel >= UnimplementedSIMD128)
      .Case("nontrapping-fptoint", HasNontrappingFPToInt)
      .Case("sign-ext", HasSignExt)
      .Case("exception-handling", HasExceptionHandling)
      .Default(false);
}

bool WebAssemblyTargetInfo::isValidCPUName(StringRef Name) const {
  return llvm::find(ValidCPUNames, Name) != std::end(ValidCPUNames);
}

void WebAssemblyTargetInfo::fillValidCPUList(
    SmallVectorImpl<StringRef> &Values) const {
  Values.append(std::begin(ValidCPUNames), std::end(ValidCPUNames));
}

void WebAssemblyTargetInfo::getTargetDefines(const LangOptions &Opts,
                                             MacroBuilder &Builder) const {
  defineCPUMacros(Builder, "wasm", /*Tuning=*/false);
  if (SIMDLevel >= SIMD128)
    Builder.defineMacro("__wasm_simd128__");
  if (SIMDLevel >= UnimplementedSIMD128)
    Builder.defineMacro("__wasm_unimplemented_simd128__");
}

void WebAssemblyTargetInfo::setSIMDLevel(llvm::StringMap<bool> &Features,
                                         SIMDEnum Level) {
  switch (Level) {
  case UnimplementedSIMD128:
    Features["unimplemented-simd128"] = true;
    LLVM_FALLTHROUGH;
  case SIMD128:
    Features["simd128"] = true;
    LLVM_FALLTHROUGH;
  case NoSIMD:
    break;
  }
}

bool WebAssemblyTargetInfo::initFeatureMap(
    llvm::StringMap<bool> &Features, DiagnosticsEngine &Diags, StringRef CPU,
    const std::vector<std::string> &FeaturesVec) const {
  if (CPU == "bleeding-edge") {
    Features["nontrapping-fptoint"] = true;
    Features["sign-ext"] = true;
    setSIMDLevel(Features, SIMD128);
  }
  // Other targets do not consider user-configured features here, but while we
  // are actively developing new features it is useful to let user-configured
  // features control availability of builtins
  setSIMDLevel(Features, SIMDLevel);
  if (HasNontrappingFPToInt)
    Features["nontrapping-fptoint"] = true;
  if (HasSignExt)
    Features["sign-ext"] = true;
  if (HasExceptionHandling)
    Features["exception-handling"] = true;

  return TargetInfo::initFeatureMap(Features, Diags, CPU, FeaturesVec);
}

bool WebAssemblyTargetInfo::handleTargetFeatures(
    std::vector<std::string> &Features, DiagnosticsEngine &Diags) {
  for (const auto &Feature : Features) {
    if (Feature == "+simd128") {
      SIMDLevel = std::max(SIMDLevel, SIMD128);
      continue;
    }
    if (Feature == "-simd128") {
      SIMDLevel = std::min(SIMDLevel, SIMDEnum(SIMD128 - 1));
      continue;
    }
    if (Feature == "+unimplemented-simd128") {
      SIMDLevel = std::max(SIMDLevel, SIMDEnum(UnimplementedSIMD128));
      continue;
    }
    if (Feature == "-unimplemented-simd128") {
      SIMDLevel = std::min(SIMDLevel, SIMDEnum(UnimplementedSIMD128 - 1));
      continue;
    }
    if (Feature == "+nontrapping-fptoint") {
      HasNontrappingFPToInt = true;
      continue;
    }
    if (Feature == "-nontrapping-fptoint") {
      HasNontrappingFPToInt = false;
      continue;
    }
    if (Feature == "+sign-ext") {
      HasSignExt = true;
      continue;
    }
    if (Feature == "-sign-ext") {
      HasSignExt = false;
      continue;
    }
    if (Feature == "+exception-handling") {
      HasExceptionHandling = true;
      continue;
    }
    if (Feature == "-exception-handling") {
      HasExceptionHandling = false;
      continue;
    }

    Diags.Report(diag::err_opt_not_valid_with_opt)
        << Feature << "-target-feature";
    return false;
  }
  return true;
}

ArrayRef<Builtin::Info> WebAssemblyTargetInfo::getTargetBuiltins() const {
  return llvm::makeArrayRef(BuiltinInfo, clang::WebAssembly::LastTSBuiltin -
                                             Builtin::FirstTSBuiltin);
}

void WebAssembly32TargetInfo::getTargetDefines(const LangOptions &Opts,
                                               MacroBuilder &Builder) const {
  WebAssemblyTargetInfo::getTargetDefines(Opts, Builder);
  defineCPUMacros(Builder, "wasm32", /*Tuning=*/false);
}

void WebAssembly64TargetInfo::getTargetDefines(const LangOptions &Opts,
                                               MacroBuilder &Builder) const {
  WebAssemblyTargetInfo::getTargetDefines(Opts, Builder);
  defineCPUMacros(Builder, "wasm64", /*Tuning=*/false);
}
