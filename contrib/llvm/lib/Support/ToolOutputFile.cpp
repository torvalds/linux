//===--- ToolOutputFile.cpp - Implement the ToolOutputFile class --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This implements the ToolOutputFile class.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Signals.h"
using namespace llvm;

ToolOutputFile::CleanupInstaller::CleanupInstaller(StringRef Filename)
    : Filename(Filename), Keep(false) {
  // Arrange for the file to be deleted if the process is killed.
  if (Filename != "-")
    sys::RemoveFileOnSignal(Filename);
}

ToolOutputFile::CleanupInstaller::~CleanupInstaller() {
  // Delete the file if the client hasn't told us not to.
  if (!Keep && Filename != "-")
    sys::fs::remove(Filename);

  // Ok, the file is successfully written and closed, or deleted. There's no
  // further need to clean it up on signals.
  if (Filename != "-")
    sys::DontRemoveFileOnSignal(Filename);
}

ToolOutputFile::ToolOutputFile(StringRef Filename, std::error_code &EC,
                               sys::fs::OpenFlags Flags)
    : Installer(Filename), OS(Filename, EC, Flags) {
  // If open fails, no cleanup is needed.
  if (EC)
    Installer.Keep = true;
}

ToolOutputFile::ToolOutputFile(StringRef Filename, int FD)
    : Installer(Filename), OS(FD, true) {}
