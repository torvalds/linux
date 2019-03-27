//===-- OptionGroupPlatform.cpp ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/OptionGroupPlatform.h"

#include "lldb/Host/OptionParser.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Target/Platform.h"

using namespace lldb;
using namespace lldb_private;

PlatformSP OptionGroupPlatform::CreatePlatformWithOptions(
    CommandInterpreter &interpreter, const ArchSpec &arch, bool make_selected,
    Status &error, ArchSpec &platform_arch) const {
  PlatformSP platform_sp;

  if (!m_platform_name.empty()) {
    platform_sp = Platform::Create(ConstString(m_platform_name.c_str()), error);
    if (platform_sp) {
      if (platform_arch.IsValid() &&
          !platform_sp->IsCompatibleArchitecture(arch, false, &platform_arch)) {
        error.SetErrorStringWithFormat("platform '%s' doesn't support '%s'",
                                       platform_sp->GetName().GetCString(),
                                       arch.GetTriple().getTriple().c_str());
        platform_sp.reset();
        return platform_sp;
      }
    }
  } else if (arch.IsValid()) {
    platform_sp = Platform::Create(arch, &platform_arch, error);
  }

  if (platform_sp) {
    interpreter.GetDebugger().GetPlatformList().Append(platform_sp,
                                                       make_selected);
    if (!m_os_version.empty())
      platform_sp->SetOSVersion(m_os_version);

    if (m_sdk_sysroot)
      platform_sp->SetSDKRootDirectory(m_sdk_sysroot);

    if (m_sdk_build)
      platform_sp->SetSDKBuild(m_sdk_build);
  }

  return platform_sp;
}

void OptionGroupPlatform::OptionParsingStarting(
    ExecutionContext *execution_context) {
  m_platform_name.clear();
  m_sdk_sysroot.Clear();
  m_sdk_build.Clear();
  m_os_version = llvm::VersionTuple();
}

static constexpr OptionDefinition g_option_table[] = {
    {LLDB_OPT_SET_ALL, false, "platform", 'p', OptionParser::eRequiredArgument,
     nullptr, {}, 0, eArgTypePlatform, "Specify name of the platform to "
                                       "use for this target, creating the "
                                       "platform if necessary."},
    {LLDB_OPT_SET_ALL, false, "version", 'v', OptionParser::eRequiredArgument,
     nullptr, {}, 0, eArgTypeNone,
     "Specify the initial SDK version to use prior to connecting."},
    {LLDB_OPT_SET_ALL, false, "build", 'b', OptionParser::eRequiredArgument,
     nullptr, {}, 0, eArgTypeNone,
     "Specify the initial SDK build number."},
    {LLDB_OPT_SET_ALL, false, "sysroot", 'S', OptionParser::eRequiredArgument,
     nullptr, {}, 0, eArgTypeFilename, "Specify the SDK root directory "
                                       "that contains a root of all "
                                       "remote system files."}};

llvm::ArrayRef<OptionDefinition> OptionGroupPlatform::GetDefinitions() {
  llvm::ArrayRef<OptionDefinition> result(g_option_table);
  if (m_include_platform_option)
    return result;
  return result.drop_front();
}

Status
OptionGroupPlatform::SetOptionValue(uint32_t option_idx,
                                    llvm::StringRef option_arg,
                                    ExecutionContext *execution_context) {
  Status error;
  if (!m_include_platform_option)
    ++option_idx;

  const int short_option = g_option_table[option_idx].short_option;

  switch (short_option) {
  case 'p':
    m_platform_name.assign(option_arg);
    break;

  case 'v':
    if (m_os_version.tryParse(option_arg))
      error.SetErrorStringWithFormatv("invalid version string '{0}'",
                                      option_arg);
    break;

  case 'b':
    m_sdk_build.SetString(option_arg);
    break;

  case 'S':
    m_sdk_sysroot.SetString(option_arg);
    break;

  default:
    error.SetErrorStringWithFormat("unrecognized option '%c'", short_option);
    break;
  }
  return error;
}

bool OptionGroupPlatform::PlatformMatches(
    const lldb::PlatformSP &platform_sp) const {
  if (platform_sp) {
    if (!m_platform_name.empty()) {
      if (platform_sp->GetName() != ConstString(m_platform_name.c_str()))
        return false;
    }

    if (m_sdk_build && m_sdk_build != platform_sp->GetSDKBuild())
      return false;

    if (m_sdk_sysroot && m_sdk_sysroot != platform_sp->GetSDKRootDirectory())
      return false;

    if (!m_os_version.empty() && m_os_version != platform_sp->GetOSVersion())
      return false;
    return true;
  }
  return false;
}
