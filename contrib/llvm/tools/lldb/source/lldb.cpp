//===-- lldb.cpp ------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/lldb-private.h"

using namespace lldb;
using namespace lldb_private;

#include "clang/Basic/Version.h"

#ifdef HAVE_SVN_VERSION_INC
#include "SVNVersion.inc"
#endif

#ifdef HAVE_APPLE_VERSION_INC
#include "AppleVersion.inc"
#endif

static const char *GetLLDBRevision() {
#ifdef LLDB_REVISION
  return LLDB_REVISION;
#else
  return NULL;
#endif
}

static const char *GetLLDBRepository() {
#ifdef LLDB_REPOSITORY
  return LLDB_REPOSITORY;
#else
  return NULL;
#endif
}

#define QUOTE(str) #str
#define EXPAND_AND_QUOTE(str) QUOTE(str)

const char *lldb_private::GetVersion() {
  // On platforms other than Darwin, report a version number in the same style
  // as the clang tool.
  static std::string g_version_str;
  if (g_version_str.empty()) {
    g_version_str += "lldb version ";
    g_version_str += CLANG_VERSION_STRING;

    const char *lldb_repo = GetLLDBRepository();
    const char *lldb_rev = GetLLDBRevision();
    if (lldb_repo || lldb_rev) {
      g_version_str += " (";
      if (lldb_repo)
        g_version_str += lldb_repo;
      if (lldb_rev) {
        g_version_str += " revision ";
        g_version_str += lldb_rev;
      }
      g_version_str += ")";
    }

    std::string clang_rev(clang::getClangRevision());
    if (clang_rev.length() > 0) {
      g_version_str += "\n  clang revision ";
      g_version_str += clang_rev;
    }
    std::string llvm_rev(clang::getLLVMRevision());
    if (llvm_rev.length() > 0) {
      g_version_str += "\n  llvm revision ";
      g_version_str += llvm_rev;
    }
  }
  return g_version_str.c_str();
}
