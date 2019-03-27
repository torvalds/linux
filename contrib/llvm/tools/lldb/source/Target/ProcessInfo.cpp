//===-- ProcessInfo.cpp -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/ProcessInfo.h"

#include <climits>

#include "lldb/Host/PosixApi.h"
#include "lldb/Utility/Stream.h"

#include "llvm/ADT/SmallString.h"

using namespace lldb;
using namespace lldb_private;

ProcessInfo::ProcessInfo()
    : m_executable(), m_arguments(), m_environment(), m_uid(UINT32_MAX),
      m_gid(UINT32_MAX), m_arch(), m_pid(LLDB_INVALID_PROCESS_ID) {}

ProcessInfo::ProcessInfo(const char *name, const ArchSpec &arch,
                         lldb::pid_t pid)
    : m_executable(name), m_arguments(), m_environment(), m_uid(UINT32_MAX),
      m_gid(UINT32_MAX), m_arch(arch), m_pid(pid) {}

void ProcessInfo::Clear() {
  m_executable.Clear();
  m_arguments.Clear();
  m_environment.clear();
  m_uid = UINT32_MAX;
  m_gid = UINT32_MAX;
  m_arch.Clear();
  m_pid = LLDB_INVALID_PROCESS_ID;
}

const char *ProcessInfo::GetName() const {
  return m_executable.GetFilename().GetCString();
}

size_t ProcessInfo::GetNameLength() const {
  return m_executable.GetFilename().GetLength();
}

void ProcessInfo::Dump(Stream &s, Platform *platform) const {
  s << "Executable: " << GetName() << "\n";
  s << "Triple: ";
  m_arch.DumpTriple(s);
  s << "\n";

  s << "Arguments:\n";
  m_arguments.Dump(s);

  s.Format("Environment:\n{0}", m_environment);
}

void ProcessInfo::SetExecutableFile(const FileSpec &exe_file,
                                    bool add_exe_file_as_first_arg) {
  if (exe_file) {
    m_executable = exe_file;
    if (add_exe_file_as_first_arg) {
      llvm::SmallString<128> filename;
      exe_file.GetPath(filename);
      if (!filename.empty())
        m_arguments.InsertArgumentAtIndex(0, filename);
    }
  } else {
    m_executable.Clear();
  }
}

llvm::StringRef ProcessInfo::GetArg0() const {
  return m_arg0;
}

void ProcessInfo::SetArg0(llvm::StringRef arg) {
  m_arg0 = arg;
}

void ProcessInfo::SetArguments(char const **argv,
                               bool first_arg_is_executable) {
  m_arguments.SetArguments(argv);

  // Is the first argument the executable?
  if (first_arg_is_executable) {
    const char *first_arg = m_arguments.GetArgumentAtIndex(0);
    if (first_arg) {
      // Yes the first argument is an executable, set it as the executable in
      // the launch options. Don't resolve the file path as the path could be a
      // remote platform path
      m_executable.SetFile(first_arg, FileSpec::Style::native);
    }
  }
}

void ProcessInfo::SetArguments(const Args &args, bool first_arg_is_executable) {
  // Copy all arguments
  m_arguments = args;

  // Is the first argument the executable?
  if (first_arg_is_executable) {
    const char *first_arg = m_arguments.GetArgumentAtIndex(0);
    if (first_arg) {
      // Yes the first argument is an executable, set it as the executable in
      // the launch options. Don't resolve the file path as the path could be a
      // remote platform path
      m_executable.SetFile(first_arg, FileSpec::Style::native);
    }
  }
}
