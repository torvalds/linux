//===-- WindowsManifestMerger.h ---------------------------------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===---------------------------------------------------------------------===//
//
// This file provides a utility for merging Microsoft .manifest files.  These
// files are xml documents which contain meta-information about applications,
// such as whether or not admin access is required, system compatibility,
// versions, etc.  Part of the linking process of an executable may require
// merging several of these .manifest files using a tree-merge following
// specific rules.  Unfortunately, these rules are not documented well
// anywhere.  However, a careful investigation of the behavior of the original
// Microsoft Manifest Tool (mt.exe) revealed the rules of this merge.  As the
// saying goes, code is the best documentation, so please look below if you are
// interested in the exact merging requirements.
//
// Ref:
// https://msdn.microsoft.com/en-us/library/windows/desktop/aa374191(v=vs.85).aspx
//
//===---------------------------------------------------------------------===//

#ifndef LLVM_INCLUDE_LLVM_SUPPORT_WINDOWS_MANIFEST_MERGER_H
#define LLVM_INCLUDE_LLVM_SUPPORT_WINDOWS_MANIFEST_MERGER_H

#include "llvm/Support/Error.h"

namespace llvm {

class MemoryBuffer;

namespace windows_manifest {

bool isAvailable();

class WindowsManifestError : public ErrorInfo<WindowsManifestError, ECError> {
public:
  static char ID;
  WindowsManifestError(const Twine &Msg);
  void log(raw_ostream &OS) const override;

private:
  std::string Msg;
};

class WindowsManifestMerger {
public:
  WindowsManifestMerger();
  ~WindowsManifestMerger();
  Error merge(const MemoryBuffer &Manifest);

  // Returns vector containing merged xml manifest, or uninitialized vector for
  // empty manifest.
  std::unique_ptr<MemoryBuffer> getMergedManifest();

private:
  class WindowsManifestMergerImpl;
  std::unique_ptr<WindowsManifestMergerImpl> Impl;
};

} // namespace windows_manifest
} // namespace llvm
#endif
