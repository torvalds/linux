//===-- BreakpointResolverAddress.cpp ---------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Breakpoint/BreakpointResolverAddress.h"


#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/Section.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"

using namespace lldb;
using namespace lldb_private;

//----------------------------------------------------------------------
// BreakpointResolverAddress:
//----------------------------------------------------------------------
BreakpointResolverAddress::BreakpointResolverAddress(
    Breakpoint *bkpt, const Address &addr, const FileSpec &module_spec)
    : BreakpointResolver(bkpt, BreakpointResolver::AddressResolver),
      m_addr(addr), m_resolved_addr(LLDB_INVALID_ADDRESS),
      m_module_filespec(module_spec) {}

BreakpointResolverAddress::BreakpointResolverAddress(Breakpoint *bkpt,
                                                     const Address &addr)
    : BreakpointResolver(bkpt, BreakpointResolver::AddressResolver),
      m_addr(addr), m_resolved_addr(LLDB_INVALID_ADDRESS), m_module_filespec() {
}

BreakpointResolverAddress::~BreakpointResolverAddress() {}

BreakpointResolver *BreakpointResolverAddress::CreateFromStructuredData(
    Breakpoint *bkpt, const StructuredData::Dictionary &options_dict,
    Status &error) {
  llvm::StringRef module_name;
  lldb::addr_t addr_offset;
  FileSpec module_filespec;
  bool success;

  success = options_dict.GetValueForKeyAsInteger(
      GetKey(OptionNames::AddressOffset), addr_offset);
  if (!success) {
    error.SetErrorString("BRFL::CFSD: Couldn't find address offset entry.");
    return nullptr;
  }
  Address address(addr_offset);

  success = options_dict.HasKey(GetKey(OptionNames::ModuleName));
  if (success) {
    success = options_dict.GetValueForKeyAsString(
        GetKey(OptionNames::ModuleName), module_name);
    if (!success) {
      error.SetErrorString("BRA::CFSD: Couldn't read module name entry.");
      return nullptr;
    }
    module_filespec.SetFile(module_name, FileSpec::Style::native);
  }
  return new BreakpointResolverAddress(bkpt, address, module_filespec);
}

StructuredData::ObjectSP
BreakpointResolverAddress::SerializeToStructuredData() {
  StructuredData::DictionarySP options_dict_sp(
      new StructuredData::Dictionary());
  SectionSP section_sp = m_addr.GetSection();
  if (section_sp) {
    ModuleSP module_sp = section_sp->GetModule();
    ConstString module_name;
    if (module_sp)
      module_name.SetCString(module_name.GetCString());

    options_dict_sp->AddStringItem(GetKey(OptionNames::ModuleName),
                                   module_name.GetCString());
    options_dict_sp->AddIntegerItem(GetKey(OptionNames::AddressOffset),
                                    m_addr.GetOffset());
  } else {
    options_dict_sp->AddIntegerItem(GetKey(OptionNames::AddressOffset),
                                    m_addr.GetOffset());
    if (m_module_filespec) {
      options_dict_sp->AddStringItem(GetKey(OptionNames::ModuleName),
                                     m_module_filespec.GetPath());
    }
  }

  return WrapOptionsDict(options_dict_sp);
  return StructuredData::ObjectSP();
}

void BreakpointResolverAddress::ResolveBreakpoint(SearchFilter &filter) {
  // If the address is not section relative, then we should not try to re-
  // resolve it, it is just some random address and we wouldn't know what to do
  // on reload.  But if it is section relative, we need to re-resolve it since
  // the section it's in may have shifted on re-run.
  bool re_resolve = false;
  if (m_addr.GetSection() || m_module_filespec)
    re_resolve = true;
  else if (m_breakpoint->GetNumLocations() == 0)
    re_resolve = true;

  if (re_resolve)
    BreakpointResolver::ResolveBreakpoint(filter);
}

void BreakpointResolverAddress::ResolveBreakpointInModules(
    SearchFilter &filter, ModuleList &modules) {
  // See comment in ResolveBreakpoint.
  bool re_resolve = false;
  if (m_addr.GetSection())
    re_resolve = true;
  else if (m_breakpoint->GetNumLocations() == 0)
    re_resolve = true;

  if (re_resolve)
    BreakpointResolver::ResolveBreakpointInModules(filter, modules);
}

Searcher::CallbackReturn
BreakpointResolverAddress::SearchCallback(SearchFilter &filter,
                                          SymbolContext &context, Address *addr,
                                          bool containing) {
  assert(m_breakpoint != NULL);

  if (filter.AddressPasses(m_addr)) {
    if (m_breakpoint->GetNumLocations() == 0) {
      // If the address is just an offset, and we're given a module, see if we
      // can find the appropriate module loaded in the binary, and fix up
      // m_addr to use that.
      if (!m_addr.IsSectionOffset() && m_module_filespec) {
        Target &target = m_breakpoint->GetTarget();
        ModuleSpec module_spec(m_module_filespec);
        ModuleSP module_sp = target.GetImages().FindFirstModule(module_spec);
        if (module_sp) {
          Address tmp_address;
          if (module_sp->ResolveFileAddress(m_addr.GetOffset(), tmp_address))
            m_addr = tmp_address;
        }
      }

      m_resolved_addr = m_addr.GetLoadAddress(&m_breakpoint->GetTarget());
      BreakpointLocationSP bp_loc_sp(AddLocation(m_addr));
      if (bp_loc_sp && !m_breakpoint->IsInternal()) {
        StreamString s;
        bp_loc_sp->GetDescription(&s, lldb::eDescriptionLevelVerbose);
        Log *log(
            lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_BREAKPOINTS));
        if (log)
          log->Printf("Added location: %s\n", s.GetData());
      }
    } else {
      BreakpointLocationSP loc_sp = m_breakpoint->GetLocationAtIndex(0);
      lldb::addr_t cur_load_location =
          m_addr.GetLoadAddress(&m_breakpoint->GetTarget());
      if (cur_load_location != m_resolved_addr) {
        m_resolved_addr = cur_load_location;
        loc_sp->ClearBreakpointSite();
        loc_sp->ResolveBreakpointSite();
      }
    }
  }
  return Searcher::eCallbackReturnStop;
}

lldb::SearchDepth BreakpointResolverAddress::GetDepth() {
  return lldb::eSearchDepthTarget;
}

void BreakpointResolverAddress::GetDescription(Stream *s) {
  s->PutCString("address = ");
  m_addr.Dump(s, m_breakpoint->GetTarget().GetProcessSP().get(),
              Address::DumpStyleModuleWithFileAddress,
              Address::DumpStyleLoadAddress);
}

void BreakpointResolverAddress::Dump(Stream *s) const {}

lldb::BreakpointResolverSP
BreakpointResolverAddress::CopyForBreakpoint(Breakpoint &breakpoint) {
  lldb::BreakpointResolverSP ret_sp(
      new BreakpointResolverAddress(&breakpoint, m_addr));
  return ret_sp;
}
