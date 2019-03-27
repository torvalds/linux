//===-- ClangHost.cpp -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ClangHost.h"

#include "clang/Basic/Version.h"
#include "clang/Config/config.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Threading.h"

#include "lldb/Host/FileSystem.h"
#include "lldb/Host/HostInfo.h"
#if !defined(_WIN32)
#include "lldb/Host/posix/HostInfoPosix.h"
#endif
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Log.h"

#include <string>

using namespace lldb_private;

#if defined(_WIN32)
static bool ComputeClangDirectory(FileSpec &file_spec) { return false; }
#else
static bool DefaultComputeClangDirectory(FileSpec &file_spec) {
  return HostInfoPosix::ComputePathRelativeToLibrary(
      file_spec, (llvm::Twine("/lib") + CLANG_LIBDIR_SUFFIX + "/clang/" +
                  CLANG_VERSION_STRING)
                     .str());
}

#if defined(__APPLE__)

static bool VerifyClangPath(const llvm::Twine &clang_path) {
  if (FileSystem::Instance().IsDirectory(clang_path))
    return true;
  Log *log = lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_HOST);
  if (log)
    log->Printf("VerifyClangPath(): "
                "failed to stat clang resource directory at \"%s\"",
                clang_path.str().c_str());
  return false;
}

bool lldb_private::ComputeClangDirectory(FileSpec &lldb_shlib_spec,
                                         FileSpec &file_spec, bool verify) {
  std::string raw_path = lldb_shlib_spec.GetPath();

  auto rev_it = llvm::sys::path::rbegin(raw_path);
  auto r_end = llvm::sys::path::rend(raw_path);

  // Check for a Posix-style build of LLDB.
  while (rev_it != r_end) {
    if (*rev_it == "LLDB.framework")
      break;
    ++rev_it;
  }

  if (rev_it == r_end)
    return DefaultComputeClangDirectory(file_spec);

  // Inside Xcode and in Xcode toolchains LLDB is always in lockstep
  // with the Swift compiler, so it can reuse its Clang resource
  // directory. This allows LLDB and the Swift compiler to share the
  // same Clang module cache.
  llvm::SmallString<256> clang_path;
  const char *swift_clang_resource_dir = "usr/lib/swift/clang";
  auto parent = std::next(rev_it);
  if (parent != r_end && *parent == "SharedFrameworks") {
    // This is the top-level LLDB in the Xcode.app bundle.
    // E.g., "Xcode.app/Contents/SharedFrameworks/LLDB.framework/Versions/A"
    raw_path.resize(parent - r_end);
    llvm::sys::path::append(clang_path, raw_path,
                            "Developer/Toolchains/XcodeDefault.xctoolchain",
                            swift_clang_resource_dir);
    if (!verify || VerifyClangPath(clang_path)) {
      file_spec.SetFile(clang_path.c_str(), FileSpec::Style::native);
      FileSystem::Instance().Resolve(file_spec);
      return true;
    }
  } else if (parent != r_end && *parent == "PrivateFrameworks" &&
             std::distance(parent, r_end) > 2) {
    ++parent;
    ++parent;
    if (*parent == "System") {
      // This is LLDB inside an Xcode toolchain.
      // E.g., "Xcode.app/Contents/Developer/Toolchains/"               \
      //       "My.xctoolchain/System/Library/PrivateFrameworks/LLDB.framework"
      raw_path.resize(parent - r_end);
      llvm::sys::path::append(clang_path, raw_path, swift_clang_resource_dir);
      if (!verify || VerifyClangPath(clang_path)) {
        file_spec.SetFile(clang_path.c_str(), FileSpec::Style::native);
        FileSystem::Instance().Resolve(file_spec);
        return true;
      }
      raw_path = lldb_shlib_spec.GetPath();
    }
    raw_path.resize(rev_it - r_end);
  } else {
    raw_path.resize(rev_it - r_end);
  }

  // Fall back to the Clang resource directory inside the framework.
  raw_path.append("LLDB.framework/Resources/Clang");
  file_spec.SetFile(raw_path.c_str(), FileSpec::Style::native);
  FileSystem::Instance().Resolve(file_spec);
  return true;
}

static bool ComputeClangDirectory(FileSpec &file_spec) {
  if (FileSpec lldb_file_spec = HostInfo::GetShlibDir())
    return ComputeClangDirectory(lldb_file_spec, file_spec, true);
  return false;
}
#else  // __APPLE__

// All non-Apple posix systems.
static bool ComputeClangDirectory(FileSpec &file_spec) {
  return DefaultComputeClangDirectory(file_spec);
}
#endif // __APPLE__
#endif // _WIN32

FileSpec lldb_private::GetClangResourceDir() {
  static FileSpec g_cached_resource_dir;
  static llvm::once_flag g_once_flag;
  llvm::call_once(g_once_flag, []() {
    ::ComputeClangDirectory(g_cached_resource_dir);
    Log *log = lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_HOST);
    if (log)
      log->Printf("GetClangResourceDir() => '%s'",
                  g_cached_resource_dir.GetPath().c_str());
  });
  return g_cached_resource_dir;
}
