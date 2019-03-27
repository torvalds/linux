//===-- PlatformFreeBSD.cpp -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "PlatformFreeBSD.h"
#include "lldb/Host/Config.h"

#include <stdio.h>
#ifndef LLDB_DISABLE_POSIX
#include <sys/utsname.h>
#endif

#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Breakpoint/BreakpointSite.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/State.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/StreamString.h"

// Define these constants from FreeBSD mman.h for use when targeting remote
// FreeBSD systems even when host has different values.
#define MAP_PRIVATE 0x0002
#define MAP_ANON 0x1000

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::platform_freebsd;

static uint32_t g_initialize_count = 0;

//------------------------------------------------------------------

PlatformSP PlatformFreeBSD::CreateInstance(bool force, const ArchSpec *arch) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PLATFORM));
  LLDB_LOG(log, "force = {0}, arch=({1}, {2})", force,
           arch ? arch->GetArchitectureName() : "<null>",
           arch ? arch->GetTriple().getTriple() : "<null>");

  bool create = force;
  if (!create && arch && arch->IsValid()) {
    const llvm::Triple &triple = arch->GetTriple();
    switch (triple.getOS()) {
    case llvm::Triple::FreeBSD:
      create = true;
      break;

#if defined(__FreeBSD__)
    // Only accept "unknown" for the OS if the host is BSD and it "unknown"
    // wasn't specified (it was just returned because it was NOT specified)
    case llvm::Triple::OSType::UnknownOS:
      create = !arch->TripleOSWasSpecified();
      break;
#endif
    default:
      break;
    }
  }
  LLDB_LOG(log, "create = {0}", create);
  if (create) {
    return PlatformSP(new PlatformFreeBSD(false));
  }
  return PlatformSP();
}

ConstString PlatformFreeBSD::GetPluginNameStatic(bool is_host) {
  if (is_host) {
    static ConstString g_host_name(Platform::GetHostPlatformName());
    return g_host_name;
  } else {
    static ConstString g_remote_name("remote-freebsd");
    return g_remote_name;
  }
}

const char *PlatformFreeBSD::GetPluginDescriptionStatic(bool is_host) {
  if (is_host)
    return "Local FreeBSD user platform plug-in.";
  else
    return "Remote FreeBSD user platform plug-in.";
}

ConstString PlatformFreeBSD::GetPluginName() {
  return GetPluginNameStatic(IsHost());
}

void PlatformFreeBSD::Initialize() {
  Platform::Initialize();

  if (g_initialize_count++ == 0) {
#if defined(__FreeBSD__)
    PlatformSP default_platform_sp(new PlatformFreeBSD(true));
    default_platform_sp->SetSystemArchitecture(HostInfo::GetArchitecture());
    Platform::SetHostPlatform(default_platform_sp);
#endif
    PluginManager::RegisterPlugin(
        PlatformFreeBSD::GetPluginNameStatic(false),
        PlatformFreeBSD::GetPluginDescriptionStatic(false),
        PlatformFreeBSD::CreateInstance, nullptr);
  }
}

void PlatformFreeBSD::Terminate() {
  if (g_initialize_count > 0) {
    if (--g_initialize_count == 0) {
      PluginManager::UnregisterPlugin(PlatformFreeBSD::CreateInstance);
    }
  }

  PlatformPOSIX::Terminate();
}

//------------------------------------------------------------------
/// Default Constructor
//------------------------------------------------------------------
PlatformFreeBSD::PlatformFreeBSD(bool is_host)
    : PlatformPOSIX(is_host) // This is the local host platform
{}

PlatformFreeBSD::~PlatformFreeBSD() = default;

bool PlatformFreeBSD::GetSupportedArchitectureAtIndex(uint32_t idx,
                                                      ArchSpec &arch) {
  if (IsHost()) {
    ArchSpec hostArch = HostInfo::GetArchitecture(HostInfo::eArchKindDefault);
    if (hostArch.GetTriple().isOSFreeBSD()) {
      if (idx == 0) {
        arch = hostArch;
        return arch.IsValid();
      } else if (idx == 1) {
        // If the default host architecture is 64-bit, look for a 32-bit
        // variant
        if (hostArch.IsValid() && hostArch.GetTriple().isArch64Bit()) {
          arch = HostInfo::GetArchitecture(HostInfo::eArchKind32);
          return arch.IsValid();
        }
      }
    }
  } else {
    if (m_remote_platform_sp)
      return m_remote_platform_sp->GetSupportedArchitectureAtIndex(idx, arch);

    llvm::Triple triple;
    // Set the OS to FreeBSD
    triple.setOS(llvm::Triple::FreeBSD);
    // Set the architecture
    switch (idx) {
    case 0:
      triple.setArchName("x86_64");
      break;
    case 1:
      triple.setArchName("i386");
      break;
    case 2:
      triple.setArchName("aarch64");
      break;
    case 3:
      triple.setArchName("arm");
      break;
    case 4:
      triple.setArchName("mips64");
      break;
    case 5:
      triple.setArchName("mips");
      break;
    case 6:
      triple.setArchName("ppc64");
      break;
    case 7:
      triple.setArchName("ppc");
      break;
    default:
      return false;
    }
    // Leave the vendor as "llvm::Triple:UnknownVendor" and don't specify the
    // vendor by calling triple.SetVendorName("unknown") so that it is a
    // "unspecified unknown". This means when someone calls
    // triple.GetVendorName() it will return an empty string which indicates
    // that the vendor can be set when two architectures are merged

    // Now set the triple into "arch" and return true
    arch.SetTriple(triple);
    return true;
  }
  return false;
}

void PlatformFreeBSD::GetStatus(Stream &strm) {
  Platform::GetStatus(strm);

#ifndef LLDB_DISABLE_POSIX
  // Display local kernel information only when we are running in host mode.
  // Otherwise, we would end up printing non-FreeBSD information (when running
  // on Mac OS for example).
  if (IsHost()) {
    struct utsname un;

    if (uname(&un))
      return;

    strm.Printf("    Kernel: %s\n", un.sysname);
    strm.Printf("   Release: %s\n", un.release);
    strm.Printf("   Version: %s\n", un.version);
  }
#endif
}

size_t
PlatformFreeBSD::GetSoftwareBreakpointTrapOpcode(Target &target,
                                                 BreakpointSite *bp_site) {
  switch (target.GetArchitecture().GetMachine()) {
  case llvm::Triple::arm: {
    lldb::BreakpointLocationSP bp_loc_sp(bp_site->GetOwnerAtIndex(0));
    AddressClass addr_class = AddressClass::eUnknown;

    if (bp_loc_sp) {
      addr_class = bp_loc_sp->GetAddress().GetAddressClass();
      if (addr_class == AddressClass::eUnknown &&
          (bp_loc_sp->GetAddress().GetFileAddress() & 1))
        addr_class = AddressClass::eCodeAlternateISA;
    }

    if (addr_class == AddressClass::eCodeAlternateISA) {
      // TODO: Enable when FreeBSD supports thumb breakpoints.
      // FreeBSD kernel as of 10.x, does not support thumb breakpoints
      return 0;
    }

    static const uint8_t g_arm_breakpoint_opcode[] = {0xFE, 0xDE, 0xFF, 0xE7};
    size_t trap_opcode_size = sizeof(g_arm_breakpoint_opcode);
    assert(bp_site);
    if (bp_site->SetTrapOpcode(g_arm_breakpoint_opcode, trap_opcode_size))
      return trap_opcode_size;
  }
    LLVM_FALLTHROUGH;
  default:
    return Platform::GetSoftwareBreakpointTrapOpcode(target, bp_site);
  }
}

Status PlatformFreeBSD::LaunchProcess(ProcessLaunchInfo &launch_info) {
  Status error;
  if (IsHost()) {
    error = Platform::LaunchProcess(launch_info);
  } else {
    if (m_remote_platform_sp)
      error = m_remote_platform_sp->LaunchProcess(launch_info);
    else
      error.SetErrorString("the platform is not currently connected");
  }
  return error;
}

lldb::ProcessSP PlatformFreeBSD::Attach(ProcessAttachInfo &attach_info,
                                        Debugger &debugger, Target *target,
                                        Status &error) {
  lldb::ProcessSP process_sp;
  if (IsHost()) {
    if (target == NULL) {
      TargetSP new_target_sp;
      ArchSpec emptyArchSpec;

      error = debugger.GetTargetList().CreateTarget(
          debugger, "", emptyArchSpec, eLoadDependentsNo, m_remote_platform_sp,
          new_target_sp);
      target = new_target_sp.get();
    } else
      error.Clear();

    if (target && error.Success()) {
      debugger.GetTargetList().SetSelectedTarget(target);
      // The freebsd always currently uses the GDB remote debugger plug-in so
      // even when debugging locally we are debugging remotely! Just like the
      // darwin plugin.
      process_sp = target->CreateProcess(
          attach_info.GetListenerForProcess(debugger), "gdb-remote", NULL);

      if (process_sp)
        error = process_sp->Attach(attach_info);
    }
  } else {
    if (m_remote_platform_sp)
      process_sp =
          m_remote_platform_sp->Attach(attach_info, debugger, target, error);
    else
      error.SetErrorString("the platform is not currently connected");
  }
  return process_sp;
}

// FreeBSD processes cannot yet be launched by spawning and attaching.
bool PlatformFreeBSD::CanDebugProcess() {
  return false;
}

void PlatformFreeBSD::CalculateTrapHandlerSymbolNames() {
  m_trap_handlers.push_back(ConstString("_sigtramp"));
}

MmapArgList PlatformFreeBSD::GetMmapArgumentList(const ArchSpec &arch,
                                                 addr_t addr, addr_t length,
                                                 unsigned prot, unsigned flags,
                                                 addr_t fd, addr_t offset) {
  uint64_t flags_platform = 0;

  if (flags & eMmapFlagsPrivate)
    flags_platform |= MAP_PRIVATE;
  if (flags & eMmapFlagsAnon)
    flags_platform |= MAP_ANON;

  MmapArgList args({addr, length, prot, flags_platform, fd, offset});
  if (arch.GetTriple().getArch() == llvm::Triple::x86)
    args.push_back(0);
  return args;
}
