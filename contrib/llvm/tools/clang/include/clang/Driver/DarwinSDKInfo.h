//===--- DarwinSDKInfo.h - SDK Information parser for darwin ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_DRIVER_DARWIN_SDK_INFO_H
#define LLVM_CLANG_DRIVER_DARWIN_SDK_INFO_H

#include "clang/Basic/LLVM.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/Support/VirtualFileSystem.h"

namespace clang {
namespace driver {

/// The information about the darwin SDK that was used during this compilation.
class DarwinSDKInfo {
public:
  DarwinSDKInfo(llvm::VersionTuple Version) : Version(Version) {}

  const llvm::VersionTuple &getVersion() const { return Version; }

private:
  llvm::VersionTuple Version;
};

/// Parse the SDK information from the SDKSettings.json file.
///
/// \returns an error if the SDKSettings.json file is invalid, None if the
/// SDK has no SDKSettings.json, or a valid \c DarwinSDKInfo otherwise.
Expected<Optional<DarwinSDKInfo>> parseDarwinSDKInfo(llvm::vfs::FileSystem &VFS,
                                                     StringRef SDKRootPath);

} // end namespace driver
} // end namespace clang

#endif // LLVM_CLANG_DRIVER_DARWIN_SDK_INFO_H
