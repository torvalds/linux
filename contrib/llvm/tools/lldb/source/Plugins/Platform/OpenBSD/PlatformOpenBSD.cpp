//===-- PlatformOpenBSD.cpp -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "PlatformOpenBSD.h"
#include "lldb/Host/Config.h"

#include <stdio.h>
#ifndef LLDB_DISABLE_POSIX
#include <sys/utsname.h>
#endif

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

// Define these constants from OpenBSD mman.h for use when targeting remote
// openbsd systems even when host has different values.
#define MAP_PRIVATE 0x0002
#define MAP_ANON 0x1000

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::platform_openbsd;

static uint32_t g_initialize_count = 0;

//------------------------------------------------------------------

PlatformSP PlatformOpenBSD::CreateInstance(bool force, const ArchSpec *arch) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PLATFORM));
  LLDB_LOG(log, "force = {0}, arch=({1}, {2})", force,
           arch ? arch->GetArchitectureName() : "<null>",
           arch ? arch->GetTriple().getTriple() : "<null>");

  bool create = force;
  if (!create && arch && arch->IsValid()) {
    const llvm::Triple &triple = arch->GetTriple();
    switch (triple.getOS()) {
    case llvm::Triple::OpenBSD:
      create = true;
      break;

#if defined(__OpenBSD__)
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
    return PlatformSP(new PlatformOpenBSD(false));
  }
  return PlatformSP();
}

ConstString PlatformOpenBSD::GetPluginNameStatic(bool is_host) {
  if (is_host) {
    static ConstString g_host_name(Platform::GetHostPlatformName());
    return g_host_name;
  } else {
    static ConstString g_remote_name("remote-openbsd");
    return g_remote_name;
  }
}

const char *PlatformOpenBSD::GetPluginDescriptionStatic(bool is_host) {
  if (is_host)
    return "Local OpenBSD user platform plug-in.";
  else
    return "Remote OpenBSD user platform plug-in.";
}

ConstString PlatformOpenBSD::GetPluginName() {
  return GetPluginNameStatic(IsHost());
}

void PlatformOpenBSD::Initialize() {
  Platform::Initialize();

  if (g_initialize_count++ == 0) {
#if defined(__OpenBSD__)
    PlatformSP default_platform_sp(new PlatformOpenBSD(true));
    default_platform_sp->SetSystemArchitecture(HostInfo::GetArchitecture());
    Platform::SetHostPlatform(default_platform_sp);
#endif
    PluginManager::RegisterPlugin(
        PlatformOpenBSD::GetPluginNameStatic(false),
        PlatformOpenBSD::GetPluginDescriptionStatic(false),
        PlatformOpenBSD::CreateInstance, nullptr);
  }
}

void PlatformOpenBSD::Terminate() {
  if (g_initialize_count > 0) {
    if (--g_initialize_count == 0) {
      PluginManager::UnregisterPlugin(PlatformOpenBSD::CreateInstance);
    }
  }

  PlatformPOSIX::Terminate();
}

//------------------------------------------------------------------
/// Default Constructor
//------------------------------------------------------------------
PlatformOpenBSD::PlatformOpenBSD(bool is_host)
    : PlatformPOSIX(is_host) // This is the local host platform
{}

PlatformOpenBSD::~PlatformOpenBSD() = default;

bool PlatformOpenBSD::GetSupportedArchitectureAtIndex(uint32_t idx,
                                                      ArchSpec &arch) {
  if (IsHost()) {
    ArchSpec hostArch = HostInfo::GetArchitecture(HostInfo::eArchKindDefault);
    if (hostArch.GetTriple().isOSOpenBSD()) {
      if (idx == 0) {
        arch = hostArch;
        return arch.IsValid();
      }
    }
  } else {
    if (m_remote_platform_sp)
      return m_remote_platform_sp->GetSupportedArchitectureAtIndex(idx, arch);

    llvm::Triple triple;
    // Set the OS to OpenBSD
    triple.setOS(llvm::Triple::OpenBSD);
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

void PlatformOpenBSD::GetStatus(Stream &strm) {
  Platform::GetStatus(strm);

#ifndef LLDB_DISABLE_POSIX
  // Display local kernel information only when we are running in host mode.
  // Otherwise, we would end up printing non-OpenBSD information (when running
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

// OpenBSD processes cannot yet be launched by spawning and attaching.
bool PlatformOpenBSD::CanDebugProcess() {
  return false;
}

void PlatformOpenBSD::CalculateTrapHandlerSymbolNames() {
  m_trap_handlers.push_back(ConstString("_sigtramp"));
}

MmapArgList PlatformOpenBSD::GetMmapArgumentList(const ArchSpec &arch,
                                                 addr_t addr, addr_t length,
                                                 unsigned prot, unsigned flags,
                                                 addr_t fd, addr_t offset) {
  uint64_t flags_platform = 0;

  if (flags & eMmapFlagsPrivate)
    flags_platform |= MAP_PRIVATE;
  if (flags & eMmapFlagsAnon)
    flags_platform |= MAP_ANON;

  MmapArgList args({addr, length, prot, flags_platform, fd, offset});
  return args;
}
