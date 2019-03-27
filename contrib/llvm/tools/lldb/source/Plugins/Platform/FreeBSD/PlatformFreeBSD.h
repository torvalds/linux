//===-- PlatformFreeBSD.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_PlatformFreeBSD_h_
#define liblldb_PlatformFreeBSD_h_

#include "Plugins/Platform/POSIX/PlatformPOSIX.h"

namespace lldb_private {
namespace platform_freebsd {

class PlatformFreeBSD : public PlatformPOSIX {
public:
  PlatformFreeBSD(bool is_host);

  ~PlatformFreeBSD() override;

  static void Initialize();

  static void Terminate();

  //------------------------------------------------------------
  // lldb_private::PluginInterface functions
  //------------------------------------------------------------
  static lldb::PlatformSP CreateInstance(bool force, const ArchSpec *arch);

  static ConstString GetPluginNameStatic(bool is_host);

  static const char *GetPluginDescriptionStatic(bool is_host);

  ConstString GetPluginName() override;

  uint32_t GetPluginVersion() override { return 1; }

  //------------------------------------------------------------
  // lldb_private::Platform functions
  //------------------------------------------------------------
  const char *GetDescription() override {
    return GetPluginDescriptionStatic(IsHost());
  }

  void GetStatus(Stream &strm) override;

  bool GetSupportedArchitectureAtIndex(uint32_t idx, ArchSpec &arch) override;

  bool CanDebugProcess() override;

  size_t GetSoftwareBreakpointTrapOpcode(Target &target,
                                         BreakpointSite *bp_site) override;

  Status LaunchProcess(ProcessLaunchInfo &launch_info) override;

  lldb::ProcessSP Attach(ProcessAttachInfo &attach_info, Debugger &debugger,
                         Target *target, Status &error) override;

  void CalculateTrapHandlerSymbolNames() override;

  MmapArgList GetMmapArgumentList(const ArchSpec &arch, lldb::addr_t addr,
                                  lldb::addr_t length, unsigned prot,
                                  unsigned flags, lldb::addr_t fd,
                                  lldb::addr_t offset) override;

private:
  DISALLOW_COPY_AND_ASSIGN(PlatformFreeBSD);
};

} // namespace platform_freebsd
} // namespace lldb_private

#endif // liblldb_PlatformFreeBSD_h_
