//===- ToolOutputFile.h - Output files for compiler-like tools -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ToolOutputFile class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_TOOLOUTPUTFILE_H
#define LLVM_SUPPORT_TOOLOUTPUTFILE_H

#include "llvm/Support/raw_ostream.h"

namespace llvm {

/// This class contains a raw_fd_ostream and adds a few extra features commonly
/// needed for compiler-like tool output files:
///   - The file is automatically deleted if the process is killed.
///   - The file is automatically deleted when the ToolOutputFile
///     object is destroyed unless the client calls keep().
class ToolOutputFile {
  /// This class is declared before the raw_fd_ostream so that it is constructed
  /// before the raw_fd_ostream is constructed and destructed after the
  /// raw_fd_ostream is destructed. It installs cleanups in its constructor and
  /// uninstalls them in its destructor.
  class CleanupInstaller {
    /// The name of the file.
    std::string Filename;
  public:
    /// The flag which indicates whether we should not delete the file.
    bool Keep;

    explicit CleanupInstaller(StringRef Filename);
    ~CleanupInstaller();
  } Installer;

  /// The contained stream. This is intentionally declared after Installer.
  raw_fd_ostream OS;

public:
  /// This constructor's arguments are passed to raw_fd_ostream's
  /// constructor.
  ToolOutputFile(StringRef Filename, std::error_code &EC,
                 sys::fs::OpenFlags Flags);

  ToolOutputFile(StringRef Filename, int FD);

  /// Return the contained raw_fd_ostream.
  raw_fd_ostream &os() { return OS; }

  /// Indicate that the tool's job wrt this output file has been successful and
  /// the file should not be deleted.
  void keep() { Installer.Keep = true; }
};

} // end llvm namespace

#endif
