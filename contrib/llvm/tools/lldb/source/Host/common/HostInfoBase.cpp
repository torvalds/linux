//===-- HostInfoBase.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/Config.h"

#include "lldb/Host/FileSystem.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Host/HostInfoBase.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/raw_ostream.h"

#include <mutex>
#include <thread>

using namespace lldb;
using namespace lldb_private;

namespace {
//----------------------------------------------------------------------
// The HostInfoBaseFields is a work around for windows not supporting static
// variables correctly in a thread safe way. Really each of the variables in
// HostInfoBaseFields should live in the functions in which they are used and
// each one should be static, but the work around is in place to avoid this
// restriction. Ick.
//----------------------------------------------------------------------

struct HostInfoBaseFields {
  ~HostInfoBaseFields() {
    if (FileSystem::Instance().Exists(m_lldb_process_tmp_dir)) {
      // Remove the LLDB temporary directory if we have one. Set "recurse" to
      // true to all files that were created for the LLDB process can be
      // cleaned up.
      llvm::sys::fs::remove_directories(m_lldb_process_tmp_dir.GetPath());
    }
  }

  std::string m_host_triple;

  ArchSpec m_host_arch_32;
  ArchSpec m_host_arch_64;

  FileSpec m_lldb_so_dir;
  FileSpec m_lldb_support_exe_dir;
  FileSpec m_lldb_headers_dir;
  FileSpec m_lldb_clang_resource_dir;
  FileSpec m_lldb_system_plugin_dir;
  FileSpec m_lldb_user_plugin_dir;
  FileSpec m_lldb_process_tmp_dir;
  FileSpec m_lldb_global_tmp_dir;
};

HostInfoBaseFields *g_fields = nullptr;
}

void HostInfoBase::Initialize() { g_fields = new HostInfoBaseFields(); }

void HostInfoBase::Terminate() {
  delete g_fields;
  g_fields = nullptr;
}

llvm::StringRef HostInfoBase::GetTargetTriple() {
  static llvm::once_flag g_once_flag;
  llvm::call_once(g_once_flag, []() {
    g_fields->m_host_triple =
        HostInfo::GetArchitecture().GetTriple().getTriple();
  });
  return g_fields->m_host_triple;
}

const ArchSpec &HostInfoBase::GetArchitecture(ArchitectureKind arch_kind) {
  static llvm::once_flag g_once_flag;
  llvm::call_once(g_once_flag, []() {
    HostInfo::ComputeHostArchitectureSupport(g_fields->m_host_arch_32,
                                             g_fields->m_host_arch_64);
  });

  // If an explicit 32 or 64-bit architecture was requested, return that.
  if (arch_kind == eArchKind32)
    return g_fields->m_host_arch_32;
  if (arch_kind == eArchKind64)
    return g_fields->m_host_arch_64;

  // Otherwise prefer the 64-bit architecture if it is valid.
  return (g_fields->m_host_arch_64.IsValid()) ? g_fields->m_host_arch_64
                                              : g_fields->m_host_arch_32;
}

llvm::Optional<HostInfoBase::ArchitectureKind> HostInfoBase::ParseArchitectureKind(llvm::StringRef kind) {
  return llvm::StringSwitch<llvm::Optional<ArchitectureKind>>(kind)
      .Case(LLDB_ARCH_DEFAULT, eArchKindDefault)
      .Case(LLDB_ARCH_DEFAULT_32BIT, eArchKind32)
      .Case(LLDB_ARCH_DEFAULT_64BIT, eArchKind64)
      .Default(llvm::None);
}

FileSpec HostInfoBase::GetShlibDir() {
  static llvm::once_flag g_once_flag;
  static bool success = false;
  llvm::call_once(g_once_flag, []() {
    success = HostInfo::ComputeSharedLibraryDirectory(g_fields->m_lldb_so_dir);
    Log *log = lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_HOST);
    LLDB_LOG(log, "shlib dir -> `{0}`", g_fields->m_lldb_so_dir);
  });
  return success ? g_fields->m_lldb_so_dir : FileSpec();
}

FileSpec HostInfoBase::GetSupportExeDir() {
  static llvm::once_flag g_once_flag;
  static bool success = false;
  llvm::call_once(g_once_flag, []() {
    success =
        HostInfo::ComputeSupportExeDirectory(g_fields->m_lldb_support_exe_dir);
    Log *log = lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_HOST);
    LLDB_LOG(log, "support exe dir -> `{0}`", g_fields->m_lldb_support_exe_dir);
  });
  return success ? g_fields->m_lldb_support_exe_dir : FileSpec();
}

FileSpec HostInfoBase::GetHeaderDir() {
  static llvm::once_flag g_once_flag;
  static bool success = false;
  llvm::call_once(g_once_flag, []() {
    success = HostInfo::ComputeHeaderDirectory(g_fields->m_lldb_headers_dir);
    Log *log = lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_HOST);
    LLDB_LOG(log, "header dir -> `{0}`", g_fields->m_lldb_headers_dir);
  });
  return success ? g_fields->m_lldb_headers_dir : FileSpec();
}

FileSpec HostInfoBase::GetSystemPluginDir() {
  static llvm::once_flag g_once_flag;
  static bool success = false;
  llvm::call_once(g_once_flag, []() {
    success = HostInfo::ComputeSystemPluginsDirectory(
        g_fields->m_lldb_system_plugin_dir);
    Log *log = lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_HOST);
    LLDB_LOG(log, "system plugin dir -> `{0}`",
             g_fields->m_lldb_system_plugin_dir);
  });
  return success ? g_fields->m_lldb_system_plugin_dir : FileSpec();
}

FileSpec HostInfoBase::GetUserPluginDir() {
  static llvm::once_flag g_once_flag;
  static bool success = false;
  llvm::call_once(g_once_flag, []() {
    success =
        HostInfo::ComputeUserPluginsDirectory(g_fields->m_lldb_user_plugin_dir);
    Log *log = lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_HOST);
    LLDB_LOG(log, "user plugin dir -> `{0}`", g_fields->m_lldb_user_plugin_dir);
  });
  return success ? g_fields->m_lldb_user_plugin_dir : FileSpec();
}

FileSpec HostInfoBase::GetProcessTempDir() {
  static llvm::once_flag g_once_flag;
  static bool success = false;
  llvm::call_once(g_once_flag, []() {
    success = HostInfo::ComputeProcessTempFileDirectory(
        g_fields->m_lldb_process_tmp_dir);
    Log *log = lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_HOST);
    LLDB_LOG(log, "process temp dir -> `{0}`",
             g_fields->m_lldb_process_tmp_dir);
  });
  return success ? g_fields->m_lldb_process_tmp_dir : FileSpec();
}

FileSpec HostInfoBase::GetGlobalTempDir() {
  static llvm::once_flag g_once_flag;
  static bool success = false;
  llvm::call_once(g_once_flag, []() {
    success = HostInfo::ComputeGlobalTempFileDirectory(
        g_fields->m_lldb_global_tmp_dir);
    Log *log = lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_HOST);
    LLDB_LOG(log, "global temp dir -> `{0}`", g_fields->m_lldb_global_tmp_dir);
  });
  return success ? g_fields->m_lldb_global_tmp_dir : FileSpec();
}

ArchSpec HostInfoBase::GetAugmentedArchSpec(llvm::StringRef triple) {
  if (triple.empty())
    return ArchSpec();
  llvm::Triple normalized_triple(llvm::Triple::normalize(triple));
  if (!ArchSpec::ContainsOnlyArch(normalized_triple))
    return ArchSpec(triple);

  if (auto kind = HostInfo::ParseArchitectureKind(triple))
    return HostInfo::GetArchitecture(*kind);

  llvm::Triple host_triple(llvm::sys::getDefaultTargetTriple());

  if (normalized_triple.getVendorName().empty())
    normalized_triple.setVendor(host_triple.getVendor());
  if (normalized_triple.getOSName().empty())
    normalized_triple.setOS(host_triple.getOS());
  if (normalized_triple.getEnvironmentName().empty())
    normalized_triple.setEnvironment(host_triple.getEnvironment());
  return ArchSpec(normalized_triple);
}

bool HostInfoBase::ComputeSharedLibraryDirectory(FileSpec &file_spec) {
  // To get paths related to LLDB we get the path to the executable that
  // contains this function. On MacOSX this will be "LLDB.framework/.../LLDB".
  // On other posix systems, we will get .../lib(64|32)?/liblldb.so.

  FileSpec lldb_file_spec(Host::GetModuleFileSpecForHostAddress(
      reinterpret_cast<void *>(reinterpret_cast<intptr_t>(
          HostInfoBase::ComputeSharedLibraryDirectory))));

  // This is necessary because when running the testsuite the shlib might be a
  // symbolic link inside the Python resource dir.
  FileSystem::Instance().ResolveSymbolicLink(lldb_file_spec, lldb_file_spec);

  // Remove the filename so that this FileSpec only represents the directory.
  file_spec.GetDirectory() = lldb_file_spec.GetDirectory();

  return (bool)file_spec.GetDirectory();
}

bool HostInfoBase::ComputeSupportExeDirectory(FileSpec &file_spec) {
  file_spec = GetShlibDir();
  return bool(file_spec);
}

bool HostInfoBase::ComputeProcessTempFileDirectory(FileSpec &file_spec) {
  FileSpec temp_file_spec;
  if (!HostInfo::ComputeGlobalTempFileDirectory(temp_file_spec))
    return false;

  std::string pid_str{llvm::to_string(Host::GetCurrentProcessID())};
  temp_file_spec.AppendPathComponent(pid_str);
  if (llvm::sys::fs::create_directory(temp_file_spec.GetPath()))
    return false;

  file_spec.GetDirectory().SetCString(temp_file_spec.GetCString());
  return true;
}

bool HostInfoBase::ComputeTempFileBaseDirectory(FileSpec &file_spec) {
  llvm::SmallVector<char, 16> tmpdir;
  llvm::sys::path::system_temp_directory(/*ErasedOnReboot*/ true, tmpdir);
  file_spec = FileSpec(std::string(tmpdir.data(), tmpdir.size()));
  FileSystem::Instance().Resolve(file_spec);
  return true;
}

bool HostInfoBase::ComputeGlobalTempFileDirectory(FileSpec &file_spec) {
  file_spec.Clear();

  FileSpec temp_file_spec;
  if (!HostInfo::ComputeTempFileBaseDirectory(temp_file_spec))
    return false;

  temp_file_spec.AppendPathComponent("lldb");
  if (llvm::sys::fs::create_directory(temp_file_spec.GetPath()))
    return false;

  file_spec.GetDirectory().SetCString(temp_file_spec.GetCString());
  return true;
}

bool HostInfoBase::ComputeHeaderDirectory(FileSpec &file_spec) {
  // TODO(zturner): Figure out how to compute the header directory for all
  // platforms.
  return false;
}

bool HostInfoBase::ComputeSystemPluginsDirectory(FileSpec &file_spec) {
  // TODO(zturner): Figure out how to compute the system plugins directory for
  // all platforms.
  return false;
}

bool HostInfoBase::ComputeUserPluginsDirectory(FileSpec &file_spec) {
  // TODO(zturner): Figure out how to compute the user plugins directory for
  // all platforms.
  return false;
}

void HostInfoBase::ComputeHostArchitectureSupport(ArchSpec &arch_32,
                                                  ArchSpec &arch_64) {
  llvm::Triple triple(llvm::sys::getProcessTriple());

  arch_32.Clear();
  arch_64.Clear();

  switch (triple.getArch()) {
  default:
    arch_32.SetTriple(triple);
    break;

  case llvm::Triple::aarch64:
  case llvm::Triple::ppc64:
  case llvm::Triple::ppc64le:
  case llvm::Triple::x86_64:
    arch_64.SetTriple(triple);
    arch_32.SetTriple(triple.get32BitArchVariant());
    break;

  case llvm::Triple::mips64:
  case llvm::Triple::mips64el:
  case llvm::Triple::sparcv9:
  case llvm::Triple::systemz:
    arch_64.SetTriple(triple);
    break;
  }
}
