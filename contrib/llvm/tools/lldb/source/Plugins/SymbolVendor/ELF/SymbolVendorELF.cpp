//===-- SymbolVendorELF.cpp ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "SymbolVendorELF.h"

#include <string.h>

#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Section.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/Symbols.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/Timer.h"

using namespace lldb;
using namespace lldb_private;

//----------------------------------------------------------------------
// SymbolVendorELF constructor
//----------------------------------------------------------------------
SymbolVendorELF::SymbolVendorELF(const lldb::ModuleSP &module_sp)
    : SymbolVendor(module_sp) {}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
SymbolVendorELF::~SymbolVendorELF() {}

void SymbolVendorELF::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance);
}

void SymbolVendorELF::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

lldb_private::ConstString SymbolVendorELF::GetPluginNameStatic() {
  static ConstString g_name("ELF");
  return g_name;
}

const char *SymbolVendorELF::GetPluginDescriptionStatic() {
  return "Symbol vendor for ELF that looks for dSYM files that match "
         "executables.";
}

//----------------------------------------------------------------------
// CreateInstance
//
// Platforms can register a callback to use when creating symbol vendors to
// allow for complex debug information file setups, and to also allow for
// finding separate debug information files.
//----------------------------------------------------------------------
SymbolVendor *
SymbolVendorELF::CreateInstance(const lldb::ModuleSP &module_sp,
                                lldb_private::Stream *feedback_strm) {
  if (!module_sp)
    return NULL;

  ObjectFile *obj_file = module_sp->GetObjectFile();
  if (!obj_file)
    return NULL;

  static ConstString obj_file_elf("elf");
  ConstString obj_name = obj_file->GetPluginName();
  if (obj_name != obj_file_elf)
    return NULL;

  lldb_private::UUID uuid;
  if (!obj_file->GetUUID(&uuid))
    return NULL;

  // Get the .gnu_debuglink file (if specified).
  FileSpecList file_spec_list = obj_file->GetDebugSymbolFilePaths();

  // If the module specified a filespec, use it first.
  FileSpec debug_symbol_fspec(module_sp->GetSymbolFileFileSpec());
  if (debug_symbol_fspec)
    file_spec_list.Insert(0, debug_symbol_fspec);

  // If we have no debug symbol files, then nothing to do.
  if (file_spec_list.IsEmpty())
    return NULL;

  static Timer::Category func_cat(LLVM_PRETTY_FUNCTION);
  Timer scoped_timer(func_cat, "SymbolVendorELF::CreateInstance (module = %s)",
                     module_sp->GetFileSpec().GetPath().c_str());

  for (size_t idx = 0; idx < file_spec_list.GetSize(); ++idx) {
    ModuleSpec module_spec;
    const FileSpec fspec = file_spec_list.GetFileSpecAtIndex(idx);

    module_spec.GetFileSpec() = obj_file->GetFileSpec();
    FileSystem::Instance().Resolve(module_spec.GetFileSpec());
    module_spec.GetSymbolFileSpec() = fspec;
    module_spec.GetUUID() = uuid;
    FileSpec dsym_fspec = Symbols::LocateExecutableSymbolFile(module_spec);
    if (dsym_fspec) {
      DataBufferSP dsym_file_data_sp;
      lldb::offset_t dsym_file_data_offset = 0;
      ObjectFileSP dsym_objfile_sp =
          ObjectFile::FindPlugin(module_sp, &dsym_fspec, 0,
                                 FileSystem::Instance().GetByteSize(dsym_fspec),
                                 dsym_file_data_sp, dsym_file_data_offset);
      if (dsym_objfile_sp) {
        // This objfile is for debugging purposes. Sadly, ObjectFileELF won't
        // be able to figure this out consistently as the symbol file may not
        // have stripped the code sections, etc.
        dsym_objfile_sp->SetType(ObjectFile::eTypeDebugInfo);

        SymbolVendorELF *symbol_vendor = new SymbolVendorELF(module_sp);
        if (symbol_vendor) {
          // Get the module unified section list and add our debug sections to
          // that.
          SectionList *module_section_list = module_sp->GetSectionList();
          SectionList *objfile_section_list = dsym_objfile_sp->GetSectionList();

          static const SectionType g_sections[] = {
              eSectionTypeDWARFDebugAbbrev,   eSectionTypeDWARFDebugAddr,
              eSectionTypeDWARFDebugAranges,  eSectionTypeDWARFDebugCuIndex,
              eSectionTypeDWARFDebugFrame,    eSectionTypeDWARFDebugInfo,
              eSectionTypeDWARFDebugLine,     eSectionTypeDWARFDebugLoc,
              eSectionTypeDWARFDebugMacInfo,  eSectionTypeDWARFDebugPubNames,
              eSectionTypeDWARFDebugPubTypes, eSectionTypeDWARFDebugRanges,
              eSectionTypeDWARFDebugStr,      eSectionTypeDWARFDebugStrOffsets,
              eSectionTypeELFSymbolTable,     eSectionTypeDWARFGNUDebugAltLink,
          };
          for (size_t idx = 0; idx < sizeof(g_sections) / sizeof(g_sections[0]);
               ++idx) {
            SectionType section_type = g_sections[idx];
            SectionSP section_sp(
                objfile_section_list->FindSectionByType(section_type, true));
            if (section_sp) {
              SectionSP module_section_sp(
                  module_section_list->FindSectionByType(section_type, true));
              if (module_section_sp)
                module_section_list->ReplaceSection(module_section_sp->GetID(),
                                                    section_sp);
              else
                module_section_list->AddSection(section_sp);
            }
          }

          symbol_vendor->AddSymbolFileRepresentation(dsym_objfile_sp);
          return symbol_vendor;
        }
      }
    }
  }
  return NULL;
}

//------------------------------------------------------------------
// PluginInterface protocol
//------------------------------------------------------------------
ConstString SymbolVendorELF::GetPluginName() { return GetPluginNameStatic(); }

uint32_t SymbolVendorELF::GetPluginVersion() { return 1; }
