//===-- CompileUnitIndex.cpp ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "CompileUnitIndex.h"

#include "PdbIndex.h"
#include "PdbUtil.h"

#include "llvm/DebugInfo/CodeView/LazyRandomTypeCollection.h"
#include "llvm/DebugInfo/CodeView/SymbolDeserializer.h"
#include "llvm/DebugInfo/CodeView/TypeDeserializer.h"
#include "llvm/DebugInfo/MSF/MappedBlockStream.h"
#include "llvm/DebugInfo/PDB/Native/DbiModuleDescriptor.h"
#include "llvm/DebugInfo/PDB/Native/DbiStream.h"
#include "llvm/DebugInfo/PDB/Native/InfoStream.h"
#include "llvm/DebugInfo/PDB/Native/ModuleDebugStream.h"
#include "llvm/DebugInfo/PDB/Native/NamedStreamMap.h"
#include "llvm/DebugInfo/PDB/Native/TpiStream.h"
#include "llvm/Support/Path.h"

#include "lldb/Utility/LLDBAssert.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::npdb;
using namespace llvm::codeview;
using namespace llvm::pdb;

static bool IsMainFile(llvm::StringRef main, llvm::StringRef other) {
  if (main == other)
    return true;

  // If the files refer to the local file system, we can just ask the file
  // system if they're equivalent.  But if the source isn't present on disk
  // then we still want to try.
  if (llvm::sys::fs::equivalent(main, other))
    return true;

  llvm::SmallString<64> normalized(other);
  llvm::sys::path::native(normalized);
  return main.equals_lower(normalized);
}

static void ParseCompile3(const CVSymbol &sym, CompilandIndexItem &cci) {
  cci.m_compile_opts.emplace();
  llvm::cantFail(
      SymbolDeserializer::deserializeAs<Compile3Sym>(sym, *cci.m_compile_opts));
}

static void ParseObjname(const CVSymbol &sym, CompilandIndexItem &cci) {
  cci.m_obj_name.emplace();
  llvm::cantFail(
      SymbolDeserializer::deserializeAs<ObjNameSym>(sym, *cci.m_obj_name));
}

static void ParseBuildInfo(PdbIndex &index, const CVSymbol &sym,
                           CompilandIndexItem &cci) {
  BuildInfoSym bis(SymbolRecordKind::BuildInfoSym);
  llvm::cantFail(SymbolDeserializer::deserializeAs<BuildInfoSym>(sym, bis));

  // S_BUILDINFO just points to an LF_BUILDINFO in the IPI stream.  Let's do
  // a little extra work to pull out the LF_BUILDINFO.
  LazyRandomTypeCollection &types = index.ipi().typeCollection();
  llvm::Optional<CVType> cvt = types.tryGetType(bis.BuildId);

  if (!cvt || cvt->kind() != LF_BUILDINFO)
    return;

  BuildInfoRecord bir;
  llvm::cantFail(TypeDeserializer::deserializeAs<BuildInfoRecord>(*cvt, bir));
  cci.m_build_info.assign(bir.ArgIndices.begin(), bir.ArgIndices.end());
}

static void ParseExtendedInfo(PdbIndex &index, CompilandIndexItem &item) {
  const CVSymbolArray &syms = item.m_debug_stream.getSymbolArray();

  // This is a private function, it shouldn't be called if the information
  // has already been parsed.
  lldbassert(!item.m_obj_name);
  lldbassert(!item.m_compile_opts);
  lldbassert(item.m_build_info.empty());

  // We're looking for 3 things.  S_COMPILE3, S_OBJNAME, and S_BUILDINFO.
  int found = 0;
  for (const CVSymbol &sym : syms) {
    switch (sym.kind()) {
    case S_COMPILE3:
      ParseCompile3(sym, item);
      break;
    case S_OBJNAME:
      ParseObjname(sym, item);
      break;
    case S_BUILDINFO:
      ParseBuildInfo(index, sym, item);
      break;
    default:
      continue;
    }
    if (++found >= 3)
      break;
  }
}

CompilandIndexItem::CompilandIndexItem(
    PdbCompilandId id, llvm::pdb::ModuleDebugStreamRef debug_stream,
    llvm::pdb::DbiModuleDescriptor descriptor)
    : m_id(id), m_debug_stream(std::move(debug_stream)),
      m_module_descriptor(std::move(descriptor)) {}

CompilandIndexItem &CompileUnitIndex::GetOrCreateCompiland(uint16_t modi) {
  auto result = m_comp_units.try_emplace(modi, nullptr);
  if (!result.second)
    return *result.first->second;

  // Find the module list and load its debug information stream and cache it
  // since we need to use it for almost all interesting operations.
  const DbiModuleList &modules = m_index.dbi().modules();
  llvm::pdb::DbiModuleDescriptor descriptor = modules.getModuleDescriptor(modi);
  uint16_t stream = descriptor.getModuleStreamIndex();
  std::unique_ptr<llvm::msf::MappedBlockStream> stream_data =
      m_index.pdb().createIndexedStream(stream);
  llvm::pdb::ModuleDebugStreamRef debug_stream(descriptor,
                                               std::move(stream_data));
  cantFail(debug_stream.reload());

  std::unique_ptr<CompilandIndexItem> &cci = result.first->second;

  cci = llvm::make_unique<CompilandIndexItem>(
      PdbCompilandId{modi}, std::move(debug_stream), std::move(descriptor));
  ParseExtendedInfo(m_index, *cci);

  cci->m_strings.initialize(debug_stream.getSubsectionsArray());
  PDBStringTable &strings = cantFail(m_index.pdb().getStringTable());
  cci->m_strings.setStrings(strings.getStringTable());

  // We want the main source file to always comes first.  Note that we can't
  // just push_back the main file onto the front because `GetMainSourceFile`
  // computes it in such a way that it doesn't own the resulting memory.  So we
  // have to iterate the module file list comparing each one to the main file
  // name until we find it, and we can cache that one since the memory is backed
  // by a contiguous chunk inside the mapped PDB.
  llvm::SmallString<64> main_file = GetMainSourceFile(*cci);
  std::string s = main_file.str();
  llvm::sys::path::native(main_file);

  uint32_t file_count = modules.getSourceFileCount(modi);
  cci->m_file_list.reserve(file_count);
  bool found_main_file = false;
  for (llvm::StringRef file : modules.source_files(modi)) {
    if (!found_main_file && IsMainFile(main_file, file)) {
      cci->m_file_list.insert(cci->m_file_list.begin(), file);
      found_main_file = true;
      continue;
    }
    cci->m_file_list.push_back(file);
  }

  return *cci;
}

const CompilandIndexItem *CompileUnitIndex::GetCompiland(uint16_t modi) const {
  auto iter = m_comp_units.find(modi);
  if (iter == m_comp_units.end())
    return nullptr;
  return iter->second.get();
}

CompilandIndexItem *CompileUnitIndex::GetCompiland(uint16_t modi) {
  auto iter = m_comp_units.find(modi);
  if (iter == m_comp_units.end())
    return nullptr;
  return iter->second.get();
}

llvm::SmallString<64>
CompileUnitIndex::GetMainSourceFile(const CompilandIndexItem &item) const {
  // LF_BUILDINFO contains a list of arg indices which point to LF_STRING_ID
  // records in the IPI stream.  The order of the arg indices is as follows:
  // [0] - working directory where compiler was invoked.
  // [1] - absolute path to compiler binary
  // [2] - source file name
  // [3] - path to compiler generated PDB (the /Zi PDB, although this entry gets
  //       added even when using /Z7)
  // [4] - full command line invocation.
  //
  // We need to form the path [0]\[2] to generate the full path to the main
  // file.source
  if (item.m_build_info.size() < 3)
    return {""};

  LazyRandomTypeCollection &types = m_index.ipi().typeCollection();

  StringIdRecord working_dir;
  StringIdRecord file_name;
  CVType dir_cvt = types.getType(item.m_build_info[0]);
  CVType file_cvt = types.getType(item.m_build_info[2]);
  llvm::cantFail(
      TypeDeserializer::deserializeAs<StringIdRecord>(dir_cvt, working_dir));
  llvm::cantFail(
      TypeDeserializer::deserializeAs<StringIdRecord>(file_cvt, file_name));

  llvm::sys::path::Style style = working_dir.String.startswith("/")
                                     ? llvm::sys::path::Style::posix
                                     : llvm::sys::path::Style::windows;
  if (llvm::sys::path::is_absolute(file_name.String, style))
    return file_name.String;

  llvm::SmallString<64> absolute_path = working_dir.String;
  llvm::sys::path::append(absolute_path, file_name.String);
  return absolute_path;
}
