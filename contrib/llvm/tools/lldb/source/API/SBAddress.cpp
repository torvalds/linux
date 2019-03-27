//===-- SBAddress.cpp -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBAddress.h"
#include "lldb/API/SBProcess.h"
#include "lldb/API/SBSection.h"
#include "lldb/API/SBStream.h"
#include "lldb/Core/Address.h"
#include "lldb/Core/Module.h"
#include "lldb/Symbol/LineEntry.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"

using namespace lldb;
using namespace lldb_private;

SBAddress::SBAddress() : m_opaque_ap(new Address()) {}

SBAddress::SBAddress(const Address *lldb_object_ptr)
    : m_opaque_ap(new Address()) {
  if (lldb_object_ptr)
    ref() = *lldb_object_ptr;
}

SBAddress::SBAddress(const SBAddress &rhs) : m_opaque_ap(new Address()) {
  if (rhs.IsValid())
    ref() = rhs.ref();
}

SBAddress::SBAddress(lldb::SBSection section, lldb::addr_t offset)
    : m_opaque_ap(new Address(section.GetSP(), offset)) {}

// Create an address by resolving a load address using the supplied target
SBAddress::SBAddress(lldb::addr_t load_addr, lldb::SBTarget &target)
    : m_opaque_ap(new Address()) {
  SetLoadAddress(load_addr, target);
}

SBAddress::~SBAddress() {}

const SBAddress &SBAddress::operator=(const SBAddress &rhs) {
  if (this != &rhs) {
    if (rhs.IsValid())
      ref() = rhs.ref();
    else
      m_opaque_ap.reset(new Address());
  }
  return *this;
}

bool lldb::operator==(const SBAddress &lhs, const SBAddress &rhs) {
  if (lhs.IsValid() && rhs.IsValid())
    return lhs.ref() == rhs.ref();
  return false;
}

bool SBAddress::IsValid() const {
  return m_opaque_ap != NULL && m_opaque_ap->IsValid();
}

void SBAddress::Clear() { m_opaque_ap.reset(new Address()); }

void SBAddress::SetAddress(lldb::SBSection section, lldb::addr_t offset) {
  Address &addr = ref();
  addr.SetSection(section.GetSP());
  addr.SetOffset(offset);
}

void SBAddress::SetAddress(const Address *lldb_object_ptr) {
  if (lldb_object_ptr)
    ref() = *lldb_object_ptr;
  else
    m_opaque_ap.reset(new Address());
}

lldb::addr_t SBAddress::GetFileAddress() const {
  if (m_opaque_ap->IsValid())
    return m_opaque_ap->GetFileAddress();
  else
    return LLDB_INVALID_ADDRESS;
}

lldb::addr_t SBAddress::GetLoadAddress(const SBTarget &target) const {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  lldb::addr_t addr = LLDB_INVALID_ADDRESS;
  TargetSP target_sp(target.GetSP());
  if (target_sp) {
    if (m_opaque_ap->IsValid()) {
      std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
      addr = m_opaque_ap->GetLoadAddress(target_sp.get());
    }
  }

  if (log) {
    if (addr == LLDB_INVALID_ADDRESS)
      log->Printf(
          "SBAddress::GetLoadAddress (SBTarget(%p)) => LLDB_INVALID_ADDRESS",
          static_cast<void *>(target_sp.get()));
    else
      log->Printf("SBAddress::GetLoadAddress (SBTarget(%p)) => 0x%" PRIx64,
                  static_cast<void *>(target_sp.get()), addr);
  }

  return addr;
}

void SBAddress::SetLoadAddress(lldb::addr_t load_addr, lldb::SBTarget &target) {
  // Create the address object if we don't already have one
  ref();
  if (target.IsValid())
    *this = target.ResolveLoadAddress(load_addr);
  else
    m_opaque_ap->Clear();

  // Check if we weren't were able to resolve a section offset address. If we
  // weren't it is ok, the load address might be a location on the stack or
  // heap, so we should just have an address with no section and a valid offset
  if (!m_opaque_ap->IsValid())
    m_opaque_ap->SetOffset(load_addr);
}

bool SBAddress::OffsetAddress(addr_t offset) {
  if (m_opaque_ap->IsValid()) {
    addr_t addr_offset = m_opaque_ap->GetOffset();
    if (addr_offset != LLDB_INVALID_ADDRESS) {
      m_opaque_ap->SetOffset(addr_offset + offset);
      return true;
    }
  }
  return false;
}

lldb::SBSection SBAddress::GetSection() {
  lldb::SBSection sb_section;
  if (m_opaque_ap->IsValid())
    sb_section.SetSP(m_opaque_ap->GetSection());
  return sb_section;
}

lldb::addr_t SBAddress::GetOffset() {
  if (m_opaque_ap->IsValid())
    return m_opaque_ap->GetOffset();
  return 0;
}

Address *SBAddress::operator->() { return m_opaque_ap.get(); }

const Address *SBAddress::operator->() const { return m_opaque_ap.get(); }

Address &SBAddress::ref() {
  if (m_opaque_ap == NULL)
    m_opaque_ap.reset(new Address());
  return *m_opaque_ap;
}

const Address &SBAddress::ref() const {
  // This object should already have checked with "IsValid()" prior to calling
  // this function. In case you didn't we will assert and die to let you know.
  assert(m_opaque_ap.get());
  return *m_opaque_ap;
}

Address *SBAddress::get() { return m_opaque_ap.get(); }

bool SBAddress::GetDescription(SBStream &description) {
  // Call "ref()" on the stream to make sure it creates a backing stream in
  // case there isn't one already...
  Stream &strm = description.ref();
  if (m_opaque_ap->IsValid()) {
    m_opaque_ap->Dump(&strm, NULL, Address::DumpStyleResolvedDescription,
                      Address::DumpStyleModuleWithFileAddress, 4);
    StreamString sstrm;
    //        m_opaque_ap->Dump (&sstrm, NULL,
    //        Address::DumpStyleResolvedDescription, Address::DumpStyleInvalid,
    //        4);
    //        if (sstrm.GetData())
    //            strm.Printf (" (%s)", sstrm.GetData());
  } else
    strm.PutCString("No value");

  return true;
}

SBModule SBAddress::GetModule() {
  SBModule sb_module;
  if (m_opaque_ap->IsValid())
    sb_module.SetSP(m_opaque_ap->GetModule());
  return sb_module;
}

SBSymbolContext SBAddress::GetSymbolContext(uint32_t resolve_scope) {
  SBSymbolContext sb_sc;
  SymbolContextItem scope = static_cast<SymbolContextItem>(resolve_scope);
  if (m_opaque_ap->IsValid())
    m_opaque_ap->CalculateSymbolContext(&sb_sc.ref(), scope);
  return sb_sc;
}

SBCompileUnit SBAddress::GetCompileUnit() {
  SBCompileUnit sb_comp_unit;
  if (m_opaque_ap->IsValid())
    sb_comp_unit.reset(m_opaque_ap->CalculateSymbolContextCompileUnit());
  return sb_comp_unit;
}

SBFunction SBAddress::GetFunction() {
  SBFunction sb_function;
  if (m_opaque_ap->IsValid())
    sb_function.reset(m_opaque_ap->CalculateSymbolContextFunction());
  return sb_function;
}

SBBlock SBAddress::GetBlock() {
  SBBlock sb_block;
  if (m_opaque_ap->IsValid())
    sb_block.SetPtr(m_opaque_ap->CalculateSymbolContextBlock());
  return sb_block;
}

SBSymbol SBAddress::GetSymbol() {
  SBSymbol sb_symbol;
  if (m_opaque_ap->IsValid())
    sb_symbol.reset(m_opaque_ap->CalculateSymbolContextSymbol());
  return sb_symbol;
}

SBLineEntry SBAddress::GetLineEntry() {
  SBLineEntry sb_line_entry;
  if (m_opaque_ap->IsValid()) {
    LineEntry line_entry;
    if (m_opaque_ap->CalculateSymbolContextLineEntry(line_entry))
      sb_line_entry.SetLineEntry(line_entry);
  }
  return sb_line_entry;
}
