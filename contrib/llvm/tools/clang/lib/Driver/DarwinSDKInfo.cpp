//===--- DarwinSDKInfo.cpp - SDK Information parser for darwin - ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Driver/DarwinSDKInfo.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"

using namespace clang::driver;
using namespace clang;

Expected<Optional<DarwinSDKInfo>>
driver::parseDarwinSDKInfo(llvm::vfs::FileSystem &VFS, StringRef SDKRootPath) {
  llvm::SmallString<256> Filepath = SDKRootPath;
  llvm::sys::path::append(Filepath, "SDKSettings.json");
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> File =
      VFS.getBufferForFile(Filepath);
  if (!File) {
    // If the file couldn't be read, assume it just doesn't exist.
    return None;
  }
  Expected<llvm::json::Value> Result =
      llvm::json::parse(File.get()->getBuffer());
  if (!Result)
    return Result.takeError();

  if (const auto *Obj = Result->getAsObject()) {
    auto VersionString = Obj->getString("Version");
    if (VersionString) {
      VersionTuple Version;
      if (!Version.tryParse(*VersionString))
        return DarwinSDKInfo(Version);
    }
  }
  return llvm::make_error<llvm::StringError>("invalid SDKSettings.json",
                                             llvm::inconvertibleErrorCode());
}
