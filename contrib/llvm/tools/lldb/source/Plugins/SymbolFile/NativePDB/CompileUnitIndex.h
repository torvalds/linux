//===-- CompileUnitIndex.h --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_PLUGINS_SYMBOLFILENATIVEPDB_COMPILEUNITINDEX_H
#define LLDB_PLUGINS_SYMBOLFILENATIVEPDB_COMPILEUNITINDEX_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/IntervalMap.h"
#include "llvm/ADT/Optional.h"
#include "llvm/DebugInfo/CodeView/StringsAndChecksums.h"
#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/DebugInfo/PDB/Native/DbiModuleDescriptor.h"
#include "llvm/DebugInfo/PDB/Native/ModuleDebugStream.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"

#include "PdbSymUid.h"

#include <map>
#include <memory>

namespace lldb_private {

namespace npdb {
class PdbIndex;

/// Represents a single compile unit.  This class is useful for collecting the
/// important accessors and information about a compile unit from disparate
/// parts of the PDB into a single place, simplifying acess to compile unit
/// information for the callers.
struct CompilandIndexItem {
  CompilandIndexItem(PdbCompilandId m_id,
                     llvm::pdb::ModuleDebugStreamRef debug_stream,
                     llvm::pdb::DbiModuleDescriptor descriptor);

  // index of this compile unit.
  PdbCompilandId m_id;

  // debug stream.
  llvm::pdb::ModuleDebugStreamRef m_debug_stream;

  // dbi module descriptor.
  llvm::pdb::DbiModuleDescriptor m_module_descriptor;

  llvm::codeview::StringsAndChecksumsRef m_strings;

  // List of files which contribute to this compiland.
  std::vector<llvm::StringRef> m_file_list;

  // Maps virtual address to global symbol id, which can then be used to
  // locate the exact compile unit and offset of the symbol.  Note that this
  // is intentionally an ordered map so that we can find all symbols up to a
  // given starting address.
  std::map<lldb::addr_t, PdbSymUid> m_symbols_by_va;

  // S_COMPILE3 sym describing compilation settings for the module.
  llvm::Optional<llvm::codeview::Compile3Sym> m_compile_opts;

  // S_OBJNAME sym describing object name.
  llvm::Optional<llvm::codeview::ObjNameSym> m_obj_name;

  // LF_BUILDINFO sym describing source file name, working directory,
  // command line, etc.  This usually contains exactly 5 items which
  // are references to other strings.
  llvm::SmallVector<llvm::codeview::TypeIndex, 5> m_build_info;
};

/// Indexes information about all compile units.  This is really just a map of
/// global compile unit index to |CompilandIndexItem| structures.
class CompileUnitIndex {
  PdbIndex &m_index;
  llvm::DenseMap<uint16_t, std::unique_ptr<CompilandIndexItem>> m_comp_units;

public:
  explicit CompileUnitIndex(PdbIndex &index) : m_index(index) {}

  CompilandIndexItem &GetOrCreateCompiland(uint16_t modi);

  const CompilandIndexItem *GetCompiland(uint16_t modi) const;

  CompilandIndexItem *GetCompiland(uint16_t modi);

  llvm::SmallString<64> GetMainSourceFile(const CompilandIndexItem &item) const;
};
} // namespace npdb
} // namespace lldb_private

#endif
