//===-- RegisterUtilities.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_REGISTERUTILITIES_H
#define LLDB_REGISTERUTILITIES_H

#include "Plugins/ObjectFile/ELF/ObjectFileELF.h"
#include "lldb/Utility/DataExtractor.h"

namespace lldb_private {
/// Core files PT_NOTE segment descriptor types

namespace FREEBSD {
enum {
  NT_PRSTATUS = 1,
  NT_FPREGSET,
  NT_PRPSINFO,
  NT_THRMISC = 7,
  NT_PROCSTAT_AUXV = 16,
  NT_PPC_VMX = 0x100
};
}

namespace NETBSD {
enum { NT_PROCINFO = 1, NT_AUXV, NT_AMD64_REGS = 33, NT_AMD64_FPREGS = 35 };
}

namespace OPENBSD {
enum {
  NT_PROCINFO = 10,
  NT_AUXV = 11,
  NT_REGS = 20,
  NT_FPREGS = 21,
};
}

namespace LINUX {
enum {
  NT_PRSTATUS = 1,
  NT_FPREGSET,
  NT_PRPSINFO,
  NT_TASKSTRUCT,
  NT_PLATFORM,
  NT_AUXV,
  NT_FILE = 0x46494c45,
  NT_SIGINFO = 0x53494749,
  NT_PPC_VMX = 0x100,
  NT_PPC_VSX = 0x102,
  NT_PRXFPREG = 0x46e62b7f,
};
}

struct CoreNote {
  ELFNote info;
  DataExtractor data;
};

// A structure describing how to find a register set in a core file from a given
// OS.
struct RegsetDesc {
  // OS to which this entry applies to. Must not be UnknownOS.
  llvm::Triple::OSType OS;

  // Architecture to which this entry applies to. Can be UnknownArch, in which
  // case it applies to all architectures of a given OS.
  llvm::Triple::ArchType Arch;

  // The note type under which the register set can be found.
  uint32_t Note;
};

// Returns the register set in Notes which corresponds to the specified Triple
// according to the list of register set descriptions in RegsetDescs. The list
// is scanned linearly, so you can use a more specific entry (e.g. linux-i386)
// to override a more general entry (e.g. general linux), as long as you place
// it earlier in the list. If a register set is not found, it returns an empty
// DataExtractor.
DataExtractor getRegset(llvm::ArrayRef<CoreNote> Notes,
                        const llvm::Triple &Triple,
                        llvm::ArrayRef<RegsetDesc> RegsetDescs);

constexpr RegsetDesc FPR_Desc[] = {
    {llvm::Triple::FreeBSD, llvm::Triple::UnknownArch, FREEBSD::NT_FPREGSET},
    // In a i386 core file NT_FPREGSET is present, but it's not the result
    // of the FXSAVE instruction like in 64 bit files.
    // The result from FXSAVE is in NT_PRXFPREG for i386 core files
    {llvm::Triple::Linux, llvm::Triple::x86, LINUX::NT_PRXFPREG},
    {llvm::Triple::Linux, llvm::Triple::UnknownArch, LINUX::NT_FPREGSET},
    {llvm::Triple::NetBSD, llvm::Triple::x86_64, NETBSD::NT_AMD64_FPREGS},
    {llvm::Triple::OpenBSD, llvm::Triple::UnknownArch, OPENBSD::NT_FPREGS},
};

constexpr RegsetDesc PPC_VMX_Desc[] = {
    {llvm::Triple::FreeBSD, llvm::Triple::UnknownArch, FREEBSD::NT_PPC_VMX},
    {llvm::Triple::Linux, llvm::Triple::UnknownArch, LINUX::NT_PPC_VMX},
};

constexpr RegsetDesc PPC_VSX_Desc[] = {
    {llvm::Triple::Linux, llvm::Triple::UnknownArch, LINUX::NT_PPC_VSX},
};

} // namespace lldb_private

#endif // #ifndef LLDB_REGISTERUTILITIES_H
