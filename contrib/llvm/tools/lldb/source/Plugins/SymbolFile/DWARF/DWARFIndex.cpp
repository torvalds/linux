//===-- DWARFIndex.cpp -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Plugins/SymbolFile/DWARF/DWARFIndex.h"
#include "Plugins/SymbolFile/DWARF/DWARFDIE.h"
#include "Plugins/SymbolFile/DWARF/DWARFDebugInfo.h"

#include "Plugins/Language/ObjC/ObjCLanguage.h"

using namespace lldb_private;
using namespace lldb;

DWARFIndex::~DWARFIndex() = default;

void DWARFIndex::ProcessFunctionDIE(llvm::StringRef name, DIERef ref,
                                    DWARFDebugInfo &info,
                                    const CompilerDeclContext &parent_decl_ctx,
                                    uint32_t name_type_mask,
                                    std::vector<DWARFDIE> &dies) {
  DWARFDIE die = info.GetDIE(ref);
  if (!die) {
    ReportInvalidDIEOffset(ref.die_offset, name);
    return;
  }

  // Exit early if we're searching exclusively for methods or selectors and
  // we have a context specified (no methods in namespaces).
  uint32_t looking_for_nonmethods =
      name_type_mask & ~(eFunctionNameTypeMethod | eFunctionNameTypeSelector);
  if (!looking_for_nonmethods && parent_decl_ctx.IsValid())
    return;

  // Otherwise, we need to also check that the context matches. If it does not
  // match, we do nothing.
  if (!SymbolFileDWARF::DIEInDeclContext(&parent_decl_ctx, die))
    return;

  // In case of a full match, we just insert everything we find.
  if (name_type_mask & eFunctionNameTypeFull) {
    dies.push_back(die);
    return;
  }

  // If looking for ObjC selectors, we need to also check if the name is a
  // possible selector.
  if (name_type_mask & eFunctionNameTypeSelector &&
      ObjCLanguage::IsPossibleObjCMethodName(die.GetName())) {
    dies.push_back(die);
    return;
  }

  bool looking_for_methods = name_type_mask & lldb::eFunctionNameTypeMethod;
  bool looking_for_functions = name_type_mask & lldb::eFunctionNameTypeBase;
  if (looking_for_methods || looking_for_functions) {
    // If we're looking for either methods or functions, we definitely want this
    // die. Otherwise, only keep it if the die type matches what we are
    // searching for.
    if ((looking_for_methods && looking_for_functions) ||
        looking_for_methods == die.IsMethod())
      dies.push_back(die);
  }
}
