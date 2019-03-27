//===-- AuxVector.h ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_AuxVector_H_
#define liblldb_AuxVector_H_

#include <vector>

#include "lldb/lldb-forward.h"

namespace lldb_private {
class DataExtractor;
}

/// @class AuxVector
/// Represents a processes auxiliary vector.
///
/// When a process is loaded on Linux a vector of values is placed onto the
/// stack communicating operating system specific information.  On
/// construction this class locates and parses this information and provides a
/// simple read-only interface to the entries found.
class AuxVector {

public:
  AuxVector(lldb_private::Process *process);

  struct Entry {
    uint64_t type;
    uint64_t value;

    Entry() : type(0), value(0) {}
  };

  /// Constants describing the type of entry.
  /// On Linux, running "LD_SHOW_AUXV=1 ./executable" will spew AUX
  /// information. Added AUXV prefix to avoid potential conflicts with system-
  /// defined macros
  enum EntryType {
    AUXV_AT_NULL = 0,            ///< End of auxv.
    AUXV_AT_IGNORE = 1,          ///< Ignore entry.
    AUXV_AT_EXECFD = 2,          ///< File descriptor of program.
    AUXV_AT_PHDR = 3,            ///< Program headers.
    AUXV_AT_PHENT = 4,           ///< Size of program header.
    AUXV_AT_PHNUM = 5,           ///< Number of program headers.
    AUXV_AT_PAGESZ = 6,          ///< Page size.
    AUXV_AT_BASE = 7,            ///< Interpreter base address.
    AUXV_AT_FLAGS = 8,           ///< Flags.
    AUXV_AT_ENTRY = 9,           ///< Program entry point.
    AUXV_AT_NOTELF = 10,         ///< Set if program is not an ELF.
    AUXV_AT_UID = 11,            ///< UID.
    AUXV_AT_EUID = 12,           ///< Effective UID.
    AUXV_AT_GID = 13,            ///< GID.
    AUXV_AT_EGID = 14,           ///< Effective GID.
    AUXV_AT_CLKTCK = 17,         ///< Clock frequency (e.g. times(2)).
    AUXV_AT_PLATFORM = 15,       ///< String identifying platform.
    AUXV_AT_HWCAP = 16,          ///< Machine dependent hints about processor capabilities.
    AUXV_AT_FPUCW = 18,          ///< Used FPU control word.
    AUXV_AT_DCACHEBSIZE = 19,    ///< Data cache block size.
    AUXV_AT_ICACHEBSIZE = 20,    ///< Instruction cache block size.
    AUXV_AT_UCACHEBSIZE = 21,    ///< Unified cache block size.
    AUXV_AT_IGNOREPPC = 22,      ///< Entry should be ignored.
    AUXV_AT_SECURE = 23,         ///< Boolean, was exec setuid-like?
    AUXV_AT_BASE_PLATFORM = 24,  ///< String identifying real platforms.
    AUXV_AT_RANDOM = 25,         ///< Address of 16 random bytes.
    AUXV_AT_EXECFN = 31,         ///< Filename of executable.
    AUXV_AT_SYSINFO = 32,        ///< Pointer to the global system page used for system
                                 ///calls and other nice things.
    AUXV_AT_SYSINFO_EHDR = 33,
    AUXV_AT_L1I_CACHESHAPE = 34, ///< Shapes of the caches.
    AUXV_AT_L1D_CACHESHAPE = 35,
    AUXV_AT_L2_CACHESHAPE = 36,
    AUXV_AT_L3_CACHESHAPE = 37,
  };

private:
  typedef std::vector<Entry> EntryVector;

public:
  typedef EntryVector::const_iterator iterator;

  iterator begin() const { return m_auxv.begin(); }
  iterator end() const { return m_auxv.end(); }

  iterator FindEntry(EntryType type) const;

  static const char *GetEntryName(const Entry &entry) {
    return GetEntryName(static_cast<EntryType>(entry.type));
  }

  static const char *GetEntryName(EntryType type);

  void DumpToLog(lldb_private::Log *log) const;

private:
  lldb_private::Process *m_process;
  EntryVector m_auxv;

  lldb::DataBufferSP GetAuxvData();

  void ParseAuxv(lldb_private::DataExtractor &data);
};

#endif
