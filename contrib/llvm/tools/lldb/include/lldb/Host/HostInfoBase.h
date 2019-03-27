//===-- HostInfoBase.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_Host_HostInfoBase_h_
#define lldb_Host_HostInfoBase_h_

#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/lldb-enumerations.h"

#include "llvm/ADT/StringRef.h"

#include <stdint.h>

#include <string>

namespace lldb_private {

class FileSpec;

class HostInfoBase {
private:
  // Static class, unconstructable.
  HostInfoBase() {}
  ~HostInfoBase() {}

public:
  static void Initialize();
  static void Terminate();

  //------------------------------------------------------------------
  /// Gets the host target triple as a const string.
  ///
  /// @return
  ///     A const string object containing the host target triple.
  //------------------------------------------------------------------
  static llvm::StringRef GetTargetTriple();

  //------------------------------------------------------------------
  /// Gets the host architecture.
  ///
  /// @return
  ///     A const architecture object that represents the host
  ///     architecture.
  //------------------------------------------------------------------
  enum ArchitectureKind {
    eArchKindDefault, // The overall default architecture that applications will
                      // run on this host
    eArchKind32, // If this host supports 32 bit programs, return the default 32
                 // bit arch
    eArchKind64  // If this host supports 64 bit programs, return the default 64
                 // bit arch
  };

  static const ArchSpec &
  GetArchitecture(ArchitectureKind arch_kind = eArchKindDefault);

  static llvm::Optional<ArchitectureKind> ParseArchitectureKind(llvm::StringRef kind);

  /// Returns the directory containing the lldb shared library. Only the
  /// directory member of the FileSpec is filled in.
  static FileSpec GetShlibDir();

  /// Returns the directory containing the support executables (debugserver,
  /// ...). Only the directory member of the FileSpec is filled in.
  static FileSpec GetSupportExeDir();

  /// Returns the directory containing the lldb headers. Only the directory
  /// member of the FileSpec is filled in.
  static FileSpec GetHeaderDir();

  /// Returns the directory containing the system plugins. Only the directory
  /// member of the FileSpec is filled in.
  static FileSpec GetSystemPluginDir();

  /// Returns the directory containing the user plugins. Only the directory
  /// member of the FileSpec is filled in.
  static FileSpec GetUserPluginDir();

  /// Returns the proces temporary directory. This directory will be cleaned up
  /// when this process exits. Only the directory member of the FileSpec is
  /// filled in.
  static FileSpec GetProcessTempDir();

  /// Returns the global temporary directory. This directory will **not** be
  /// cleaned up when this process exits. Only the directory member of the
  /// FileSpec is filled in.
  static FileSpec GetGlobalTempDir();

  //---------------------------------------------------------------------------
  /// If the triple does not specify the vendor, os, and environment parts, we
  /// "augment" these using information from the host and return the resulting
  /// ArchSpec object.
  //---------------------------------------------------------------------------
  static ArchSpec GetAugmentedArchSpec(llvm::StringRef triple);

protected:
  static bool ComputeSharedLibraryDirectory(FileSpec &file_spec);
  static bool ComputeSupportExeDirectory(FileSpec &file_spec);
  static bool ComputeProcessTempFileDirectory(FileSpec &file_spec);
  static bool ComputeGlobalTempFileDirectory(FileSpec &file_spec);
  static bool ComputeTempFileBaseDirectory(FileSpec &file_spec);
  static bool ComputeHeaderDirectory(FileSpec &file_spec);
  static bool ComputeSystemPluginsDirectory(FileSpec &file_spec);
  static bool ComputeUserPluginsDirectory(FileSpec &file_spec);

  static void ComputeHostArchitectureSupport(ArchSpec &arch_32,
                                             ArchSpec &arch_64);
};
}

#endif
