//===-- SBSymbolContext.cpp -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBSymbolContext.h"
#include "lldb/API/SBStream.h"
#include "lldb/Core/Module.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Utility/Log.h"

using namespace lldb;
using namespace lldb_private;

SBSymbolContext::SBSymbolContext() : m_opaque_ap() {}

SBSymbolContext::SBSymbolContext(const SymbolContext *sc_ptr) : m_opaque_ap() {
  if (sc_ptr)
    m_opaque_ap.reset(new SymbolContext(*sc_ptr));
}

SBSymbolContext::SBSymbolContext(const SBSymbolContext &rhs) : m_opaque_ap() {
  if (rhs.IsValid()) {
    if (m_opaque_ap)
      *m_opaque_ap = *rhs.m_opaque_ap;
    else
      ref() = *rhs.m_opaque_ap;
  }
}

SBSymbolContext::~SBSymbolContext() {}

const SBSymbolContext &SBSymbolContext::operator=(const SBSymbolContext &rhs) {
  if (this != &rhs) {
    if (rhs.IsValid())
      m_opaque_ap.reset(new lldb_private::SymbolContext(*rhs.m_opaque_ap));
  }
  return *this;
}

void SBSymbolContext::SetSymbolContext(const SymbolContext *sc_ptr) {
  if (sc_ptr) {
    if (m_opaque_ap)
      *m_opaque_ap = *sc_ptr;
    else
      m_opaque_ap.reset(new SymbolContext(*sc_ptr));
  } else {
    if (m_opaque_ap)
      m_opaque_ap->Clear(true);
  }
}

bool SBSymbolContext::IsValid() const { return m_opaque_ap != NULL; }

SBModule SBSymbolContext::GetModule() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBModule sb_module;
  ModuleSP module_sp;
  if (m_opaque_ap) {
    module_sp = m_opaque_ap->module_sp;
    sb_module.SetSP(module_sp);
  }

  if (log) {
    SBStream sstr;
    sb_module.GetDescription(sstr);
    log->Printf("SBSymbolContext(%p)::GetModule () => SBModule(%p): %s",
                static_cast<void *>(m_opaque_ap.get()),
                static_cast<void *>(module_sp.get()), sstr.GetData());
  }

  return sb_module;
}

SBCompileUnit SBSymbolContext::GetCompileUnit() {
  return SBCompileUnit(m_opaque_ap ? m_opaque_ap->comp_unit : NULL);
}

SBFunction SBSymbolContext::GetFunction() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  Function *function = NULL;

  if (m_opaque_ap)
    function = m_opaque_ap->function;

  SBFunction sb_function(function);

  if (log)
    log->Printf("SBSymbolContext(%p)::GetFunction () => SBFunction(%p)",
                static_cast<void *>(m_opaque_ap.get()),
                static_cast<void *>(function));

  return sb_function;
}

SBBlock SBSymbolContext::GetBlock() {
  return SBBlock(m_opaque_ap ? m_opaque_ap->block : NULL);
}

SBLineEntry SBSymbolContext::GetLineEntry() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBLineEntry sb_line_entry;
  if (m_opaque_ap)
    sb_line_entry.SetLineEntry(m_opaque_ap->line_entry);

  if (log) {
    log->Printf("SBSymbolContext(%p)::GetLineEntry () => SBLineEntry(%p)",
                static_cast<void *>(m_opaque_ap.get()),
                static_cast<void *>(sb_line_entry.get()));
  }

  return sb_line_entry;
}

SBSymbol SBSymbolContext::GetSymbol() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  Symbol *symbol = NULL;

  if (m_opaque_ap)
    symbol = m_opaque_ap->symbol;

  SBSymbol sb_symbol(symbol);

  if (log)
    log->Printf("SBSymbolContext(%p)::GetSymbol () => SBSymbol(%p)",
                static_cast<void *>(m_opaque_ap.get()),
                static_cast<void *>(symbol));

  return sb_symbol;
}

void SBSymbolContext::SetModule(lldb::SBModule module) {
  ref().module_sp = module.GetSP();
}

void SBSymbolContext::SetCompileUnit(lldb::SBCompileUnit compile_unit) {
  ref().comp_unit = compile_unit.get();
}

void SBSymbolContext::SetFunction(lldb::SBFunction function) {
  ref().function = function.get();
}

void SBSymbolContext::SetBlock(lldb::SBBlock block) {
  ref().block = block.GetPtr();
}

void SBSymbolContext::SetLineEntry(lldb::SBLineEntry line_entry) {
  if (line_entry.IsValid())
    ref().line_entry = line_entry.ref();
  else
    ref().line_entry.Clear();
}

void SBSymbolContext::SetSymbol(lldb::SBSymbol symbol) {
  ref().symbol = symbol.get();
}

lldb_private::SymbolContext *SBSymbolContext::operator->() const {
  return m_opaque_ap.get();
}

const lldb_private::SymbolContext &SBSymbolContext::operator*() const {
  assert(m_opaque_ap.get());
  return *m_opaque_ap;
}

lldb_private::SymbolContext &SBSymbolContext::operator*() {
  if (m_opaque_ap == NULL)
    m_opaque_ap.reset(new SymbolContext);
  return *m_opaque_ap;
}

lldb_private::SymbolContext &SBSymbolContext::ref() {
  if (m_opaque_ap == NULL)
    m_opaque_ap.reset(new SymbolContext);
  return *m_opaque_ap;
}

lldb_private::SymbolContext *SBSymbolContext::get() const {
  return m_opaque_ap.get();
}

bool SBSymbolContext::GetDescription(SBStream &description) {
  Stream &strm = description.ref();

  if (m_opaque_ap) {
    m_opaque_ap->GetDescription(&strm, lldb::eDescriptionLevelFull, NULL);
  } else
    strm.PutCString("No value");

  return true;
}

SBSymbolContext
SBSymbolContext::GetParentOfInlinedScope(const SBAddress &curr_frame_pc,
                                         SBAddress &parent_frame_addr) const {
  SBSymbolContext sb_sc;
  if (m_opaque_ap.get() && curr_frame_pc.IsValid()) {
    if (m_opaque_ap->GetParentOfInlinedScope(curr_frame_pc.ref(), sb_sc.ref(),
                                             parent_frame_addr.ref()))
      return sb_sc;
  }
  return SBSymbolContext();
}
