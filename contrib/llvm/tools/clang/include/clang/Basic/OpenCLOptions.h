//===--- OpenCLOptions.h ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines the clang::OpenCLOptions class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_OPENCLOPTIONS_H
#define LLVM_CLANG_BASIC_OPENCLOPTIONS_H

#include "clang/Basic/LangOptions.h"
#include "llvm/ADT/StringMap.h"

namespace clang {

/// OpenCL supported extensions and optional core features
class OpenCLOptions {
  struct Info {
    bool Supported; // Is this option supported
    bool Enabled;   // Is this option enabled
    unsigned Avail; // Option starts to be available in this OpenCL version
    unsigned Core;  // Option becomes (optional) core feature in this OpenCL
                    // version
    Info(bool S = false, bool E = false, unsigned A = 100, unsigned C = ~0U)
      :Supported(S), Enabled(E), Avail(A), Core(C){}
  };
  llvm::StringMap<Info> OptMap;
public:
  bool isKnown(llvm::StringRef Ext) const {
    return OptMap.find(Ext) != OptMap.end();
  }

  bool isEnabled(llvm::StringRef Ext) const {
    return OptMap.find(Ext)->second.Enabled;
  }

  // Is supported as either an extension or an (optional) core feature for
  // OpenCL version \p CLVer.
  bool isSupported(llvm::StringRef Ext, LangOptions LO) const {
    // In C++ mode all extensions should work at least as in v2.0.
    auto CLVer = LO.OpenCLCPlusPlus ? 200 : LO.OpenCLVersion;
    auto I = OptMap.find(Ext)->getValue();
    return I.Supported && I.Avail <= CLVer;
  }

  // Is supported (optional) OpenCL core features for OpenCL version \p CLVer.
  // For supported extension, return false.
  bool isSupportedCore(llvm::StringRef Ext, LangOptions LO) const {
    // In C++ mode all extensions should work at least as in v2.0.
    auto CLVer = LO.OpenCLCPlusPlus ? 200 : LO.OpenCLVersion;
    auto I = OptMap.find(Ext)->getValue();
    return I.Supported && I.Avail <= CLVer && I.Core != ~0U && CLVer >= I.Core;
  }

  // Is supported OpenCL extension for OpenCL version \p CLVer.
  // For supported (optional) core feature, return false.
  bool isSupportedExtension(llvm::StringRef Ext, LangOptions LO) const {
    // In C++ mode all extensions should work at least as in v2.0.
    auto CLVer = LO.OpenCLCPlusPlus ? 200 : LO.OpenCLVersion;
    auto I = OptMap.find(Ext)->getValue();
    return I.Supported && I.Avail <= CLVer && (I.Core == ~0U || CLVer < I.Core);
  }

  void enable(llvm::StringRef Ext, bool V = true) {
    OptMap[Ext].Enabled = V;
  }

  /// Enable or disable support for OpenCL extensions
  /// \param Ext name of the extension optionally prefixed with
  ///        '+' or '-'
  /// \param V used when \p Ext is not prefixed by '+' or '-'
  void support(llvm::StringRef Ext, bool V = true) {
    assert(!Ext.empty() && "Extension is empty.");

    switch (Ext[0]) {
    case '+':
      V = true;
      Ext = Ext.drop_front();
      break;
    case '-':
      V = false;
      Ext = Ext.drop_front();
      break;
    }

    if (Ext.equals("all")) {
      supportAll(V);
      return;
    }
    OptMap[Ext].Supported = V;
  }

  OpenCLOptions(){
#define OPENCLEXT_INTERNAL(Ext, AvailVer, CoreVer) \
    OptMap[#Ext].Avail = AvailVer; \
    OptMap[#Ext].Core = CoreVer;
#include "clang/Basic/OpenCLExtensions.def"
  }

  void addSupport(const OpenCLOptions &Opts) {
    for (auto &I:Opts.OptMap)
      if (I.second.Supported)
        OptMap[I.getKey()].Supported = true;
  }

  void copy(const OpenCLOptions &Opts) {
    OptMap = Opts.OptMap;
  }

  // Turn on or off support of all options.
  void supportAll(bool On = true) {
    for (llvm::StringMap<Info>::iterator I = OptMap.begin(),
         E = OptMap.end(); I != E; ++I)
      I->second.Supported = On;
  }

  void disableAll() {
    for (llvm::StringMap<Info>::iterator I = OptMap.begin(),
         E = OptMap.end(); I != E; ++I)
      I->second.Enabled = false;
  }

  void enableSupportedCore(LangOptions LO) {
    for (llvm::StringMap<Info>::iterator I = OptMap.begin(), E = OptMap.end();
         I != E; ++I)
      if (isSupportedCore(I->getKey(), LO))
        I->second.Enabled = true;
  }

  friend class ASTWriter;
  friend class ASTReader;
};

} // end namespace clang

#endif
