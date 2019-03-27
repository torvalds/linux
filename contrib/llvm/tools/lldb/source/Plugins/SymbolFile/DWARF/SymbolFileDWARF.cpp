//===-- SymbolFileDWARF.cpp ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "SymbolFileDWARF.h"

#include "llvm/Support/Casting.h"
#include "llvm/Support/Threading.h"

#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleList.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Section.h"
#include "lldb/Core/StreamFile.h"
#include "lldb/Core/Value.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/RegularExpression.h"
#include "lldb/Utility/Scalar.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/Timer.h"

#include "Plugins/ExpressionParser/Clang/ClangModulesDeclVendor.h"
#include "Plugins/Language/CPlusPlus/CPlusPlusLanguage.h"

#include "lldb/Host/FileSystem.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/Symbols.h"

#include "lldb/Interpreter/OptionValueFileSpecList.h"
#include "lldb/Interpreter/OptionValueProperties.h"

#include "lldb/Symbol/Block.h"
#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Symbol/ClangUtil.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/CompilerDecl.h"
#include "lldb/Symbol/CompilerDeclContext.h"
#include "lldb/Symbol/DebugMacros.h"
#include "lldb/Symbol/LineTable.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/SymbolVendor.h"
#include "lldb/Symbol/TypeMap.h"
#include "lldb/Symbol/TypeSystem.h"
#include "lldb/Symbol/VariableList.h"

#include "lldb/Target/Language.h"

#include "AppleDWARFIndex.h"
#include "DWARFASTParser.h"
#include "DWARFASTParserClang.h"
#include "DWARFDIECollection.h"
#include "DWARFDebugAbbrev.h"
#include "DWARFDebugAranges.h"
#include "DWARFDebugInfo.h"
#include "DWARFDebugLine.h"
#include "DWARFDebugMacro.h"
#include "DWARFDebugRanges.h"
#include "DWARFDeclContext.h"
#include "DWARFFormValue.h"
#include "DWARFUnit.h"
#include "DebugNamesDWARFIndex.h"
#include "LogChannelDWARF.h"
#include "ManualDWARFIndex.h"
#include "SymbolFileDWARFDebugMap.h"
#include "SymbolFileDWARFDwo.h"
#include "SymbolFileDWARFDwp.h"

#include "llvm/Support/FileSystem.h"

#include <map>

#include <ctype.h>
#include <string.h>

//#define ENABLE_DEBUG_PRINTF // COMMENT OUT THIS LINE PRIOR TO CHECKIN

#ifdef ENABLE_DEBUG_PRINTF
#include <stdio.h>
#define DEBUG_PRINTF(fmt, ...) printf(fmt, __VA_ARGS__)
#else
#define DEBUG_PRINTF(fmt, ...)
#endif

using namespace lldb;
using namespace lldb_private;

// static inline bool
// child_requires_parent_class_union_or_struct_to_be_completed (dw_tag_t tag)
//{
//    switch (tag)
//    {
//    default:
//        break;
//    case DW_TAG_subprogram:
//    case DW_TAG_inlined_subroutine:
//    case DW_TAG_class_type:
//    case DW_TAG_structure_type:
//    case DW_TAG_union_type:
//        return true;
//    }
//    return false;
//}
//

namespace {

static constexpr PropertyDefinition g_properties[] = {
    {"comp-dir-symlink-paths", OptionValue::eTypeFileSpecList, true, 0, nullptr,
     {},
     "If the DW_AT_comp_dir matches any of these paths the symbolic "
     "links will be resolved at DWARF parse time."},
    {"ignore-file-indexes", OptionValue::eTypeBoolean, true, 0, nullptr, {},
     "Ignore indexes present in the object files and always index DWARF "
     "manually."}};

enum {
  ePropertySymLinkPaths,
  ePropertyIgnoreIndexes,
};

class PluginProperties : public Properties {
public:
  static ConstString GetSettingName() {
    return SymbolFileDWARF::GetPluginNameStatic();
  }

  PluginProperties() {
    m_collection_sp.reset(new OptionValueProperties(GetSettingName()));
    m_collection_sp->Initialize(g_properties);
  }

  FileSpecList &GetSymLinkPaths() {
    OptionValueFileSpecList *option_value =
        m_collection_sp->GetPropertyAtIndexAsOptionValueFileSpecList(
            nullptr, true, ePropertySymLinkPaths);
    assert(option_value);
    return option_value->GetCurrentValue();
  }

  bool IgnoreFileIndexes() const {
    return m_collection_sp->GetPropertyAtIndexAsBoolean(
        nullptr, ePropertyIgnoreIndexes, false);
  }
};

typedef std::shared_ptr<PluginProperties> SymbolFileDWARFPropertiesSP;

static const SymbolFileDWARFPropertiesSP &GetGlobalPluginProperties() {
  static const auto g_settings_sp(std::make_shared<PluginProperties>());
  return g_settings_sp;
}

} // anonymous namespace end

static const char *removeHostnameFromPathname(const char *path_from_dwarf) {
  if (!path_from_dwarf || !path_from_dwarf[0]) {
    return path_from_dwarf;
  }

  const char *colon_pos = strchr(path_from_dwarf, ':');
  if (nullptr == colon_pos) {
    return path_from_dwarf;
  }

  const char *slash_pos = strchr(path_from_dwarf, '/');
  if (slash_pos && (slash_pos < colon_pos)) {
    return path_from_dwarf;
  }

  // check whether we have a windows path, and so the first character is a
  // drive-letter not a hostname.
  if (colon_pos == path_from_dwarf + 1 && isalpha(*path_from_dwarf) &&
      strlen(path_from_dwarf) > 2 && '\\' == path_from_dwarf[2]) {
    return path_from_dwarf;
  }

  return colon_pos + 1;
}

static FileSpec resolveCompDir(const char *path_from_dwarf) {
  if (!path_from_dwarf)
    return FileSpec();

  // DWARF2/3 suggests the form hostname:pathname for compilation directory.
  // Remove the host part if present.
  const char *local_path = removeHostnameFromPathname(path_from_dwarf);
  if (!local_path)
    return FileSpec();

  bool is_symlink = false;
  // Always normalize our compile unit directory to get rid of redundant
  // slashes and other path anomalies before we use it for path prepending
  FileSpec local_spec(local_path);
  const auto &file_specs = GetGlobalPluginProperties()->GetSymLinkPaths();
  for (size_t i = 0; i < file_specs.GetSize() && !is_symlink; ++i)
    is_symlink = FileSpec::Equal(file_specs.GetFileSpecAtIndex(i),
                                 local_spec, true);

  if (!is_symlink)
    return local_spec;

  namespace fs = llvm::sys::fs;
  if (fs::get_file_type(local_spec.GetPath(), false) !=
      fs::file_type::symlink_file)
    return local_spec;

  FileSpec resolved_symlink;
  const auto error = FileSystem::Instance().Readlink(local_spec, resolved_symlink);
  if (error.Success())
    return resolved_symlink;

  return local_spec;
}

DWARFUnit *SymbolFileDWARF::GetBaseCompileUnit() {
  return nullptr;
}

void SymbolFileDWARF::Initialize() {
  LogChannelDWARF::Initialize();
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance,
                                DebuggerInitialize);
}

void SymbolFileDWARF::DebuggerInitialize(Debugger &debugger) {
  if (!PluginManager::GetSettingForSymbolFilePlugin(
          debugger, PluginProperties::GetSettingName())) {
    const bool is_global_setting = true;
    PluginManager::CreateSettingForSymbolFilePlugin(
        debugger, GetGlobalPluginProperties()->GetValueProperties(),
        ConstString("Properties for the dwarf symbol-file plug-in."),
        is_global_setting);
  }
}

void SymbolFileDWARF::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
  LogChannelDWARF::Terminate();
}

lldb_private::ConstString SymbolFileDWARF::GetPluginNameStatic() {
  static ConstString g_name("dwarf");
  return g_name;
}

const char *SymbolFileDWARF::GetPluginDescriptionStatic() {
  return "DWARF and DWARF3 debug symbol file reader.";
}

SymbolFile *SymbolFileDWARF::CreateInstance(ObjectFile *obj_file) {
  return new SymbolFileDWARF(obj_file);
}

TypeList *SymbolFileDWARF::GetTypeList() {
  // This method can be called without going through the symbol vendor so we
  // need to lock the module.
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  SymbolFileDWARFDebugMap *debug_map_symfile = GetDebugMapSymfile();
  if (debug_map_symfile)
    return debug_map_symfile->GetTypeList();
  else
    return m_obj_file->GetModule()->GetTypeList();
}
void SymbolFileDWARF::GetTypes(const DWARFDIE &die, dw_offset_t min_die_offset,
                               dw_offset_t max_die_offset, uint32_t type_mask,
                               TypeSet &type_set) {
  if (die) {
    const dw_offset_t die_offset = die.GetOffset();

    if (die_offset >= max_die_offset)
      return;

    if (die_offset >= min_die_offset) {
      const dw_tag_t tag = die.Tag();

      bool add_type = false;

      switch (tag) {
      case DW_TAG_array_type:
        add_type = (type_mask & eTypeClassArray) != 0;
        break;
      case DW_TAG_unspecified_type:
      case DW_TAG_base_type:
        add_type = (type_mask & eTypeClassBuiltin) != 0;
        break;
      case DW_TAG_class_type:
        add_type = (type_mask & eTypeClassClass) != 0;
        break;
      case DW_TAG_structure_type:
        add_type = (type_mask & eTypeClassStruct) != 0;
        break;
      case DW_TAG_union_type:
        add_type = (type_mask & eTypeClassUnion) != 0;
        break;
      case DW_TAG_enumeration_type:
        add_type = (type_mask & eTypeClassEnumeration) != 0;
        break;
      case DW_TAG_subroutine_type:
      case DW_TAG_subprogram:
      case DW_TAG_inlined_subroutine:
        add_type = (type_mask & eTypeClassFunction) != 0;
        break;
      case DW_TAG_pointer_type:
        add_type = (type_mask & eTypeClassPointer) != 0;
        break;
      case DW_TAG_rvalue_reference_type:
      case DW_TAG_reference_type:
        add_type = (type_mask & eTypeClassReference) != 0;
        break;
      case DW_TAG_typedef:
        add_type = (type_mask & eTypeClassTypedef) != 0;
        break;
      case DW_TAG_ptr_to_member_type:
        add_type = (type_mask & eTypeClassMemberPointer) != 0;
        break;
      }

      if (add_type) {
        const bool assert_not_being_parsed = true;
        Type *type = ResolveTypeUID(die, assert_not_being_parsed);
        if (type) {
          if (type_set.find(type) == type_set.end())
            type_set.insert(type);
        }
      }
    }

    for (DWARFDIE child_die = die.GetFirstChild(); child_die.IsValid();
         child_die = child_die.GetSibling()) {
      GetTypes(child_die, min_die_offset, max_die_offset, type_mask, type_set);
    }
  }
}

size_t SymbolFileDWARF::GetTypes(SymbolContextScope *sc_scope,
                                 TypeClass type_mask, TypeList &type_list)

{
  ASSERT_MODULE_LOCK(this);
  TypeSet type_set;

  CompileUnit *comp_unit = NULL;
  DWARFUnit *dwarf_cu = NULL;
  if (sc_scope)
    comp_unit = sc_scope->CalculateSymbolContextCompileUnit();

  if (comp_unit) {
    dwarf_cu = GetDWARFCompileUnit(comp_unit);
    if (dwarf_cu == 0)
      return 0;
    GetTypes(dwarf_cu->DIE(), dwarf_cu->GetOffset(),
             dwarf_cu->GetNextCompileUnitOffset(), type_mask, type_set);
  } else {
    DWARFDebugInfo *info = DebugInfo();
    if (info) {
      const size_t num_cus = info->GetNumCompileUnits();
      for (size_t cu_idx = 0; cu_idx < num_cus; ++cu_idx) {
        dwarf_cu = info->GetCompileUnitAtIndex(cu_idx);
        if (dwarf_cu) {
          GetTypes(dwarf_cu->DIE(), 0, UINT32_MAX, type_mask, type_set);
        }
      }
    }
  }

  std::set<CompilerType> compiler_type_set;
  size_t num_types_added = 0;
  for (Type *type : type_set) {
    CompilerType compiler_type = type->GetForwardCompilerType();
    if (compiler_type_set.find(compiler_type) == compiler_type_set.end()) {
      compiler_type_set.insert(compiler_type);
      type_list.Insert(type->shared_from_this());
      ++num_types_added;
    }
  }
  return num_types_added;
}

//----------------------------------------------------------------------
// Gets the first parent that is a lexical block, function or inlined
// subroutine, or compile unit.
//----------------------------------------------------------------------
DWARFDIE
SymbolFileDWARF::GetParentSymbolContextDIE(const DWARFDIE &child_die) {
  DWARFDIE die;
  for (die = child_die.GetParent(); die; die = die.GetParent()) {
    dw_tag_t tag = die.Tag();

    switch (tag) {
    case DW_TAG_compile_unit:
    case DW_TAG_partial_unit:
    case DW_TAG_subprogram:
    case DW_TAG_inlined_subroutine:
    case DW_TAG_lexical_block:
      return die;
    }
  }
  return DWARFDIE();
}

SymbolFileDWARF::SymbolFileDWARF(ObjectFile *objfile)
    : SymbolFile(objfile), UserID(uint64_t(DW_INVALID_OFFSET)
                                  << 32), // Used by SymbolFileDWARFDebugMap to
                                          // when this class parses .o files to
                                          // contain the .o file index/ID
      m_debug_map_module_wp(), m_debug_map_symfile(NULL), m_data_debug_abbrev(),
      m_data_debug_aranges(), m_data_debug_frame(), m_data_debug_info(),
      m_data_debug_line(), m_data_debug_macro(), m_data_debug_loc(),
      m_data_debug_ranges(), m_data_debug_rnglists(), m_data_debug_str(),
      m_data_apple_names(), m_data_apple_types(), m_data_apple_namespaces(),
      m_abbr(), m_info(), m_line(), m_fetched_external_modules(false),
      m_supports_DW_AT_APPLE_objc_complete_type(eLazyBoolCalculate), m_ranges(),
      m_unique_ast_type_map() {}

SymbolFileDWARF::~SymbolFileDWARF() {}

static const ConstString &GetDWARFMachOSegmentName() {
  static ConstString g_dwarf_section_name("__DWARF");
  return g_dwarf_section_name;
}

UniqueDWARFASTTypeMap &SymbolFileDWARF::GetUniqueDWARFASTTypeMap() {
  SymbolFileDWARFDebugMap *debug_map_symfile = GetDebugMapSymfile();
  if (debug_map_symfile)
    return debug_map_symfile->GetUniqueDWARFASTTypeMap();
  else
    return m_unique_ast_type_map;
}

TypeSystem *SymbolFileDWARF::GetTypeSystemForLanguage(LanguageType language) {
  SymbolFileDWARFDebugMap *debug_map_symfile = GetDebugMapSymfile();
  TypeSystem *type_system;
  if (debug_map_symfile) {
    type_system = debug_map_symfile->GetTypeSystemForLanguage(language);
  } else {
    type_system = m_obj_file->GetModule()->GetTypeSystemForLanguage(language);
    if (type_system)
      type_system->SetSymbolFile(this);
  }
  return type_system;
}

void SymbolFileDWARF::InitializeObject() {
  Log *log = LogChannelDWARF::GetLogIfAll(DWARF_LOG_DEBUG_INFO);
  ModuleSP module_sp(m_obj_file->GetModule());
  if (module_sp) {
    const SectionList *section_list = module_sp->GetSectionList();
    Section *section =
        section_list->FindSectionByName(GetDWARFMachOSegmentName()).get();

    if (section)
      m_obj_file->ReadSectionData(section, m_dwarf_data);
  }

  if (!GetGlobalPluginProperties()->IgnoreFileIndexes()) {
    DWARFDataExtractor apple_names, apple_namespaces, apple_types, apple_objc;
    LoadSectionData(eSectionTypeDWARFAppleNames, apple_names);
    LoadSectionData(eSectionTypeDWARFAppleNamespaces, apple_namespaces);
    LoadSectionData(eSectionTypeDWARFAppleTypes, apple_types);
    LoadSectionData(eSectionTypeDWARFAppleObjC, apple_objc);

    m_index = AppleDWARFIndex::Create(
        *GetObjectFile()->GetModule(), apple_names, apple_namespaces,
        apple_types, apple_objc, get_debug_str_data());

    if (m_index)
      return;

    DWARFDataExtractor debug_names;
    LoadSectionData(eSectionTypeDWARFDebugNames, debug_names);
    if (debug_names.GetByteSize() > 0) {
      llvm::Expected<std::unique_ptr<DebugNamesDWARFIndex>> index_or =
          DebugNamesDWARFIndex::Create(*GetObjectFile()->GetModule(),
                                       debug_names, get_debug_str_data(),
                                       DebugInfo());
      if (index_or) {
        m_index = std::move(*index_or);
        return;
      }
      LLDB_LOG_ERROR(log, index_or.takeError(),
                     "Unable to read .debug_names data: {0}");
    }
  }

  m_index = llvm::make_unique<ManualDWARFIndex>(*GetObjectFile()->GetModule(),
                                                DebugInfo());
}

bool SymbolFileDWARF::SupportedVersion(uint16_t version) {
  return version >= 2 && version <= 5;
}

uint32_t SymbolFileDWARF::CalculateAbilities() {
  uint32_t abilities = 0;
  if (m_obj_file != NULL) {
    const Section *section = NULL;
    const SectionList *section_list = m_obj_file->GetSectionList();
    if (section_list == NULL)
      return 0;

    // On non Apple platforms we might have .debug_types debug info that is
    // created by using "-fdebug-types-section". LLDB currently will try to
    // load this debug info, but it causes crashes during debugging when types
    // are missing since it doesn't know how to parse the info in the
    // .debug_types type units. This causes all complex debug info types to be
    // unresolved. Because this causes LLDB to crash and since it really
    // doesn't provide a solid debuggiung experience, we should disable trying
    // to debug this kind of DWARF until support gets added or deprecated.
    if (section_list->FindSectionByName(ConstString(".debug_types"))) {
      m_obj_file->GetModule()->ReportWarning(
        "lldb doesn’t support .debug_types debug info");
      return 0;
    }

    uint64_t debug_abbrev_file_size = 0;
    uint64_t debug_info_file_size = 0;
    uint64_t debug_line_file_size = 0;

    section = section_list->FindSectionByName(GetDWARFMachOSegmentName()).get();

    if (section)
      section_list = &section->GetChildren();

    section =
        section_list->FindSectionByType(eSectionTypeDWARFDebugInfo, true).get();
    if (section != NULL) {
      debug_info_file_size = section->GetFileSize();

      section =
          section_list->FindSectionByType(eSectionTypeDWARFDebugAbbrev, true)
              .get();
      if (section)
        debug_abbrev_file_size = section->GetFileSize();

      DWARFDebugAbbrev *abbrev = DebugAbbrev();
      if (abbrev) {
        std::set<dw_form_t> invalid_forms;
        abbrev->GetUnsupportedForms(invalid_forms);
        if (!invalid_forms.empty()) {
          StreamString error;
          error.Printf("unsupported DW_FORM value%s:", invalid_forms.size() > 1 ? "s" : "");
          for (auto form : invalid_forms)
            error.Printf(" %#x", form);
          m_obj_file->GetModule()->ReportWarning("%s", error.GetString().str().c_str());
          return 0;
        }
      }

      section =
          section_list->FindSectionByType(eSectionTypeDWARFDebugLine, true)
              .get();
      if (section)
        debug_line_file_size = section->GetFileSize();
    } else {
      const char *symfile_dir_cstr =
          m_obj_file->GetFileSpec().GetDirectory().GetCString();
      if (symfile_dir_cstr) {
        if (strcasestr(symfile_dir_cstr, ".dsym")) {
          if (m_obj_file->GetType() == ObjectFile::eTypeDebugInfo) {
            // We have a dSYM file that didn't have a any debug info. If the
            // string table has a size of 1, then it was made from an
            // executable with no debug info, or from an executable that was
            // stripped.
            section =
                section_list->FindSectionByType(eSectionTypeDWARFDebugStr, true)
                    .get();
            if (section && section->GetFileSize() == 1) {
              m_obj_file->GetModule()->ReportWarning(
                  "empty dSYM file detected, dSYM was created with an "
                  "executable with no debug info.");
            }
          }
        }
      }
    }

    if (debug_abbrev_file_size > 0 && debug_info_file_size > 0)
      abilities |= CompileUnits | Functions | Blocks | GlobalVariables |
                   LocalVariables | VariableTypes;

    if (debug_line_file_size > 0)
      abilities |= LineTables;
  }
  return abilities;
}

const DWARFDataExtractor &
SymbolFileDWARF::GetCachedSectionData(lldb::SectionType sect_type,
                                      DWARFDataSegment &data_segment) {
  llvm::call_once(data_segment.m_flag, [this, sect_type, &data_segment] {
    this->LoadSectionData(sect_type, std::ref(data_segment.m_data));
  });
  return data_segment.m_data;
}

void SymbolFileDWARF::LoadSectionData(lldb::SectionType sect_type,
                                      DWARFDataExtractor &data) {
  ModuleSP module_sp(m_obj_file->GetModule());
  const SectionList *section_list = module_sp->GetSectionList();
  if (section_list) {
    SectionSP section_sp(section_list->FindSectionByType(sect_type, true));
    if (section_sp) {
      // See if we memory mapped the DWARF segment?
      if (m_dwarf_data.GetByteSize()) {
        data.SetData(m_dwarf_data, section_sp->GetOffset(),
                     section_sp->GetFileSize());
      } else {
        if (m_obj_file->ReadSectionData(section_sp.get(), data) == 0)
          data.Clear();
      }
    }
  }
}

const DWARFDataExtractor &SymbolFileDWARF::get_debug_abbrev_data() {
  return GetCachedSectionData(eSectionTypeDWARFDebugAbbrev,
                              m_data_debug_abbrev);
}

const DWARFDataExtractor &SymbolFileDWARF::get_debug_addr_data() {
  return GetCachedSectionData(eSectionTypeDWARFDebugAddr, m_data_debug_addr);
}

const DWARFDataExtractor &SymbolFileDWARF::get_debug_aranges_data() {
  return GetCachedSectionData(eSectionTypeDWARFDebugAranges,
                              m_data_debug_aranges);
}

const DWARFDataExtractor &SymbolFileDWARF::get_debug_frame_data() {
  return GetCachedSectionData(eSectionTypeDWARFDebugFrame, m_data_debug_frame);
}

const DWARFDataExtractor &SymbolFileDWARF::get_debug_info_data() {
  return GetCachedSectionData(eSectionTypeDWARFDebugInfo, m_data_debug_info);
}

const DWARFDataExtractor &SymbolFileDWARF::get_debug_line_data() {
  return GetCachedSectionData(eSectionTypeDWARFDebugLine, m_data_debug_line);
}

const DWARFDataExtractor &SymbolFileDWARF::get_debug_line_str_data() {
 return GetCachedSectionData(eSectionTypeDWARFDebugLineStr, m_data_debug_line_str);
}

const DWARFDataExtractor &SymbolFileDWARF::get_debug_macro_data() {
  return GetCachedSectionData(eSectionTypeDWARFDebugMacro, m_data_debug_macro);
}

const DWARFDataExtractor &SymbolFileDWARF::DebugLocData() {
  const DWARFDataExtractor &debugLocData = get_debug_loc_data();
  if (debugLocData.GetByteSize() > 0)
    return debugLocData;
  return get_debug_loclists_data();
}

const DWARFDataExtractor &SymbolFileDWARF::get_debug_loc_data() {
  return GetCachedSectionData(eSectionTypeDWARFDebugLoc, m_data_debug_loc);
}

const DWARFDataExtractor &SymbolFileDWARF::get_debug_loclists_data() {
  return GetCachedSectionData(eSectionTypeDWARFDebugLocLists,
                              m_data_debug_loclists);
}

const DWARFDataExtractor &SymbolFileDWARF::get_debug_ranges_data() {
  return GetCachedSectionData(eSectionTypeDWARFDebugRanges,
                              m_data_debug_ranges);
}

const DWARFDataExtractor &SymbolFileDWARF::get_debug_rnglists_data() {
  return GetCachedSectionData(eSectionTypeDWARFDebugRngLists,
                              m_data_debug_rnglists);
}

const DWARFDataExtractor &SymbolFileDWARF::get_debug_str_data() {
  return GetCachedSectionData(eSectionTypeDWARFDebugStr, m_data_debug_str);
}

const DWARFDataExtractor &SymbolFileDWARF::get_debug_str_offsets_data() {
  return GetCachedSectionData(eSectionTypeDWARFDebugStrOffsets,
                              m_data_debug_str_offsets);
}

const DWARFDataExtractor &SymbolFileDWARF::get_debug_types_data() {
  return GetCachedSectionData(eSectionTypeDWARFDebugTypes, m_data_debug_types);
}

const DWARFDataExtractor &SymbolFileDWARF::get_apple_names_data() {
  return GetCachedSectionData(eSectionTypeDWARFAppleNames, m_data_apple_names);
}

const DWARFDataExtractor &SymbolFileDWARF::get_apple_types_data() {
  return GetCachedSectionData(eSectionTypeDWARFAppleTypes, m_data_apple_types);
}

const DWARFDataExtractor &SymbolFileDWARF::get_apple_namespaces_data() {
  return GetCachedSectionData(eSectionTypeDWARFAppleNamespaces,
                              m_data_apple_namespaces);
}

const DWARFDataExtractor &SymbolFileDWARF::get_apple_objc_data() {
  return GetCachedSectionData(eSectionTypeDWARFAppleObjC, m_data_apple_objc);
}

const DWARFDataExtractor &SymbolFileDWARF::get_gnu_debugaltlink() {
  return GetCachedSectionData(eSectionTypeDWARFGNUDebugAltLink,
                              m_data_gnu_debugaltlink);
}

DWARFDebugAbbrev *SymbolFileDWARF::DebugAbbrev() {
  if (m_abbr.get() == NULL) {
    const DWARFDataExtractor &debug_abbrev_data = get_debug_abbrev_data();
    if (debug_abbrev_data.GetByteSize() > 0) {
      m_abbr.reset(new DWARFDebugAbbrev());
      if (m_abbr.get())
        m_abbr->Parse(debug_abbrev_data);
    }
  }
  return m_abbr.get();
}

const DWARFDebugAbbrev *SymbolFileDWARF::DebugAbbrev() const {
  return m_abbr.get();
}

DWARFDebugInfo *SymbolFileDWARF::DebugInfo() {
  if (m_info.get() == NULL) {
    static Timer::Category func_cat(LLVM_PRETTY_FUNCTION);
    Timer scoped_timer(func_cat, "%s this = %p", LLVM_PRETTY_FUNCTION,
                       static_cast<void *>(this));
    if (get_debug_info_data().GetByteSize() > 0) {
      m_info.reset(new DWARFDebugInfo());
      if (m_info.get()) {
        m_info->SetDwarfData(this);
      }
    }
  }
  return m_info.get();
}

const DWARFDebugInfo *SymbolFileDWARF::DebugInfo() const {
  return m_info.get();
}

DWARFUnit *
SymbolFileDWARF::GetDWARFCompileUnit(lldb_private::CompileUnit *comp_unit) {
  if (!comp_unit)
    return nullptr;

  DWARFDebugInfo *info = DebugInfo();
  if (info) {
    // Just a normal DWARF file whose user ID for the compile unit is the DWARF
    // offset itself

    DWARFUnit *dwarf_cu =
        info->GetCompileUnit((dw_offset_t)comp_unit->GetID());
    if (dwarf_cu && dwarf_cu->GetUserData() == NULL)
      dwarf_cu->SetUserData(comp_unit);
    return dwarf_cu;
  }
  return NULL;
}

DWARFDebugRangesBase *SymbolFileDWARF::DebugRanges() {
  if (m_ranges.get() == NULL) {
    static Timer::Category func_cat(LLVM_PRETTY_FUNCTION);
    Timer scoped_timer(func_cat, "%s this = %p", LLVM_PRETTY_FUNCTION,
                       static_cast<void *>(this));

    if (get_debug_ranges_data().GetByteSize() > 0)
      m_ranges.reset(new DWARFDebugRanges());
    else if (get_debug_rnglists_data().GetByteSize() > 0)
      m_ranges.reset(new DWARFDebugRngLists());

    if (m_ranges.get())
      m_ranges->Extract(this);
  }
  return m_ranges.get();
}

const DWARFDebugRangesBase *SymbolFileDWARF::DebugRanges() const {
  return m_ranges.get();
}

lldb::CompUnitSP SymbolFileDWARF::ParseCompileUnit(DWARFUnit *dwarf_cu,
                                                   uint32_t cu_idx) {
  CompUnitSP cu_sp;
  if (dwarf_cu) {
    CompileUnit *comp_unit = (CompileUnit *)dwarf_cu->GetUserData();
    if (comp_unit) {
      // We already parsed this compile unit, had out a shared pointer to it
      cu_sp = comp_unit->shared_from_this();
    } else {
      if (dwarf_cu->GetSymbolFileDWARF() != this) {
        return dwarf_cu->GetSymbolFileDWARF()->ParseCompileUnit(dwarf_cu,
                                                                cu_idx);
      } else if (dwarf_cu->GetOffset() == 0 && GetDebugMapSymfile()) {
        // Let the debug map create the compile unit
        cu_sp = m_debug_map_symfile->GetCompileUnit(this);
        dwarf_cu->SetUserData(cu_sp.get());
      } else {
        ModuleSP module_sp(m_obj_file->GetModule());
        if (module_sp) {
          const DWARFDIE cu_die = dwarf_cu->DIE();
          if (cu_die) {
            FileSpec cu_file_spec(cu_die.GetName());
            if (cu_file_spec) {
              // If we have a full path to the compile unit, we don't need to
              // resolve the file.  This can be expensive e.g. when the source
              // files are
              // NFS mounted.
              if (cu_file_spec.IsRelative()) {
                const char *cu_comp_dir{
                    cu_die.GetAttributeValueAsString(DW_AT_comp_dir, nullptr)};
                cu_file_spec.PrependPathComponent(resolveCompDir(cu_comp_dir));
              }

              std::string remapped_file;
              if (module_sp->RemapSourceFile(cu_file_spec.GetPath(),
                                             remapped_file))
                cu_file_spec.SetFile(remapped_file, FileSpec::Style::native);
            }

            LanguageType cu_language = DWARFUnit::LanguageTypeFromDWARF(
                cu_die.GetAttributeValueAsUnsigned(DW_AT_language, 0));

            bool is_optimized = dwarf_cu->GetIsOptimized();
            cu_sp.reset(new CompileUnit(
                module_sp, dwarf_cu, cu_file_spec, dwarf_cu->GetID(),
                cu_language, is_optimized ? eLazyBoolYes : eLazyBoolNo));
            if (cu_sp) {
              // If we just created a compile unit with an invalid file spec,
              // try and get the first entry in the supports files from the
              // line table as that should be the compile unit.
              if (!cu_file_spec) {
                cu_file_spec = cu_sp->GetSupportFiles().GetFileSpecAtIndex(1);
                if (cu_file_spec) {
                  (FileSpec &)(*cu_sp) = cu_file_spec;
                  // Also fix the invalid file spec which was copied from the
                  // compile unit.
                  cu_sp->GetSupportFiles().Replace(0, cu_file_spec);
                }
              }

              dwarf_cu->SetUserData(cu_sp.get());

              // Figure out the compile unit index if we weren't given one
              if (cu_idx == UINT32_MAX)
                DebugInfo()->GetCompileUnit(dwarf_cu->GetOffset(), &cu_idx);

              m_obj_file->GetModule()->GetSymbolVendor()->SetCompileUnitAtIndex(
                  cu_idx, cu_sp);
            }
          }
        }
      }
    }
  }
  return cu_sp;
}

uint32_t SymbolFileDWARF::GetNumCompileUnits() {
  DWARFDebugInfo *info = DebugInfo();
  if (info)
    return info->GetNumCompileUnits();
  return 0;
}

CompUnitSP SymbolFileDWARF::ParseCompileUnitAtIndex(uint32_t cu_idx) {
  ASSERT_MODULE_LOCK(this);
  CompUnitSP cu_sp;
  DWARFDebugInfo *info = DebugInfo();
  if (info) {
    DWARFUnit *dwarf_cu = info->GetCompileUnitAtIndex(cu_idx);
    if (dwarf_cu)
      cu_sp = ParseCompileUnit(dwarf_cu, cu_idx);
  }
  return cu_sp;
}

Function *SymbolFileDWARF::ParseFunction(CompileUnit &comp_unit,
                                         const DWARFDIE &die) {
  ASSERT_MODULE_LOCK(this);
  if (die.IsValid()) {
    TypeSystem *type_system =
        GetTypeSystemForLanguage(die.GetCU()->GetLanguageType());

    if (type_system) {
      DWARFASTParser *dwarf_ast = type_system->GetDWARFParser();
      if (dwarf_ast)
        return dwarf_ast->ParseFunctionFromDWARF(comp_unit, die);
    }
  }
  return nullptr;
}

bool SymbolFileDWARF::FixupAddress(Address &addr) {
  SymbolFileDWARFDebugMap *debug_map_symfile = GetDebugMapSymfile();
  if (debug_map_symfile) {
    return debug_map_symfile->LinkOSOAddress(addr);
  }
  // This is a normal DWARF file, no address fixups need to happen
  return true;
}
lldb::LanguageType SymbolFileDWARF::ParseLanguage(CompileUnit &comp_unit) {
  ASSERT_MODULE_LOCK(this);
  DWARFUnit *dwarf_cu = GetDWARFCompileUnit(&comp_unit);
  if (dwarf_cu)
    return dwarf_cu->GetLanguageType();
  else
    return eLanguageTypeUnknown;
}

size_t SymbolFileDWARF::ParseFunctions(CompileUnit &comp_unit) {
  ASSERT_MODULE_LOCK(this);
  size_t functions_added = 0;
  DWARFUnit *dwarf_cu = GetDWARFCompileUnit(&comp_unit);
  if (dwarf_cu) {
    DWARFDIECollection function_dies;
    const size_t num_functions =
        dwarf_cu->AppendDIEsWithTag(DW_TAG_subprogram, function_dies);
    size_t func_idx;
    for (func_idx = 0; func_idx < num_functions; ++func_idx) {
      DWARFDIE die = function_dies.GetDIEAtIndex(func_idx);
      if (comp_unit.FindFunctionByUID(die.GetID()).get() == NULL) {
        if (ParseFunction(comp_unit, die))
          ++functions_added;
      }
    }
    // FixupTypes();
  }
  return functions_added;
}

bool SymbolFileDWARF::ParseSupportFiles(CompileUnit &comp_unit,
                                        FileSpecList &support_files) {
  ASSERT_MODULE_LOCK(this);
  DWARFUnit *dwarf_cu = GetDWARFCompileUnit(&comp_unit);
  if (dwarf_cu) {
    const DWARFBaseDIE cu_die = dwarf_cu->GetUnitDIEOnly();

    if (cu_die) {
      FileSpec cu_comp_dir = resolveCompDir(
          cu_die.GetAttributeValueAsString(DW_AT_comp_dir, nullptr));
      const dw_offset_t stmt_list = cu_die.GetAttributeValueAsUnsigned(
          DW_AT_stmt_list, DW_INVALID_OFFSET);
      if (stmt_list != DW_INVALID_OFFSET) {
        // All file indexes in DWARF are one based and a file of index zero is
        // supposed to be the compile unit itself.
        support_files.Append(comp_unit);
        return DWARFDebugLine::ParseSupportFiles(
            comp_unit.GetModule(), get_debug_line_data(), cu_comp_dir,
            stmt_list, support_files, dwarf_cu);
      }
    }
  }
  return false;
}

bool SymbolFileDWARF::ParseIsOptimized(CompileUnit &comp_unit) {
  ASSERT_MODULE_LOCK(this);
  DWARFUnit *dwarf_cu = GetDWARFCompileUnit(&comp_unit);
  if (dwarf_cu)
    return dwarf_cu->GetIsOptimized();
  return false;
}

bool SymbolFileDWARF::ParseImportedModules(
    const lldb_private::SymbolContext &sc,
    std::vector<lldb_private::ConstString> &imported_modules) {
  ASSERT_MODULE_LOCK(this);
  assert(sc.comp_unit);
  DWARFUnit *dwarf_cu = GetDWARFCompileUnit(sc.comp_unit);
  if (dwarf_cu) {
    if (ClangModulesDeclVendor::LanguageSupportsClangModules(
            sc.comp_unit->GetLanguage())) {
      UpdateExternalModuleListIfNeeded();

      if (sc.comp_unit) {
        const DWARFDIE die = dwarf_cu->DIE();

        if (die) {
          for (DWARFDIE child_die = die.GetFirstChild(); child_die;
               child_die = child_die.GetSibling()) {
            if (child_die.Tag() == DW_TAG_imported_declaration) {
              if (DWARFDIE module_die =
                      child_die.GetReferencedDIE(DW_AT_import)) {
                if (module_die.Tag() == DW_TAG_module) {
                  if (const char *name = module_die.GetAttributeValueAsString(
                          DW_AT_name, nullptr)) {
                    ConstString const_name(name);
                    imported_modules.push_back(const_name);
                  }
                }
              }
            }
          }
        }
      } else {
        for (const auto &pair : m_external_type_modules) {
          imported_modules.push_back(pair.first);
        }
      }
    }
  }
  return false;
}

struct ParseDWARFLineTableCallbackInfo {
  LineTable *line_table;
  std::unique_ptr<LineSequence> sequence_ap;
  lldb::addr_t addr_mask;
};

//----------------------------------------------------------------------
// ParseStatementTableCallback
//----------------------------------------------------------------------
static void ParseDWARFLineTableCallback(dw_offset_t offset,
                                        const DWARFDebugLine::State &state,
                                        void *userData) {
  if (state.row == DWARFDebugLine::State::StartParsingLineTable) {
    // Just started parsing the line table
  } else if (state.row == DWARFDebugLine::State::DoneParsingLineTable) {
    // Done parsing line table, nothing to do for the cleanup
  } else {
    ParseDWARFLineTableCallbackInfo *info =
        (ParseDWARFLineTableCallbackInfo *)userData;
    LineTable *line_table = info->line_table;

    // If this is our first time here, we need to create a sequence container.
    if (!info->sequence_ap.get()) {
      info->sequence_ap.reset(line_table->CreateLineSequenceContainer());
      assert(info->sequence_ap.get());
    }
    line_table->AppendLineEntryToSequence(
        info->sequence_ap.get(), state.address & info->addr_mask, state.line,
        state.column, state.file, state.is_stmt, state.basic_block,
        state.prologue_end, state.epilogue_begin, state.end_sequence);
    if (state.end_sequence) {
      // First, put the current sequence into the line table.
      line_table->InsertSequence(info->sequence_ap.get());
      // Then, empty it to prepare for the next sequence.
      info->sequence_ap->Clear();
    }
  }
}

bool SymbolFileDWARF::ParseLineTable(CompileUnit &comp_unit) {
  ASSERT_MODULE_LOCK(this);
  if (comp_unit.GetLineTable() != NULL)
    return true;

  DWARFUnit *dwarf_cu = GetDWARFCompileUnit(&comp_unit);
  if (dwarf_cu) {
    const DWARFBaseDIE dwarf_cu_die = dwarf_cu->GetUnitDIEOnly();
    if (dwarf_cu_die) {
      const dw_offset_t cu_line_offset =
          dwarf_cu_die.GetAttributeValueAsUnsigned(DW_AT_stmt_list,
                                                   DW_INVALID_OFFSET);
      if (cu_line_offset != DW_INVALID_OFFSET) {
        std::unique_ptr<LineTable> line_table_ap(new LineTable(&comp_unit));
        if (line_table_ap.get()) {
          ParseDWARFLineTableCallbackInfo info;
          info.line_table = line_table_ap.get();

          /*
           * MIPS:
           * The SymbolContext may not have a valid target, thus we may not be
           * able
           * to call Address::GetOpcodeLoadAddress() which would clear the bit
           * #0
           * for MIPS. Use ArchSpec to clear the bit #0.
          */
          switch (GetObjectFile()->GetArchitecture().GetMachine()) {
          case llvm::Triple::mips:
          case llvm::Triple::mipsel:
          case llvm::Triple::mips64:
          case llvm::Triple::mips64el:
            info.addr_mask = ~((lldb::addr_t)1);
            break;
          default:
            info.addr_mask = ~((lldb::addr_t)0);
            break;
          }

          lldb::offset_t offset = cu_line_offset;
          DWARFDebugLine::ParseStatementTable(get_debug_line_data(), &offset,
                                              ParseDWARFLineTableCallback,
                                              &info, dwarf_cu);
          SymbolFileDWARFDebugMap *debug_map_symfile = GetDebugMapSymfile();
          if (debug_map_symfile) {
            // We have an object file that has a line table with addresses that
            // are not linked. We need to link the line table and convert the
            // addresses that are relative to the .o file into addresses for
            // the main executable.
            comp_unit.SetLineTable(
                debug_map_symfile->LinkOSOLineTable(this, line_table_ap.get()));
          } else {
            comp_unit.SetLineTable(line_table_ap.release());
            return true;
          }
        }
      }
    }
  }
  return false;
}

lldb_private::DebugMacrosSP
SymbolFileDWARF::ParseDebugMacros(lldb::offset_t *offset) {
  auto iter = m_debug_macros_map.find(*offset);
  if (iter != m_debug_macros_map.end())
    return iter->second;

  const DWARFDataExtractor &debug_macro_data = get_debug_macro_data();
  if (debug_macro_data.GetByteSize() == 0)
    return DebugMacrosSP();

  lldb_private::DebugMacrosSP debug_macros_sp(new lldb_private::DebugMacros());
  m_debug_macros_map[*offset] = debug_macros_sp;

  const DWARFDebugMacroHeader &header =
      DWARFDebugMacroHeader::ParseHeader(debug_macro_data, offset);
  DWARFDebugMacroEntry::ReadMacroEntries(debug_macro_data, get_debug_str_data(),
                                         header.OffsetIs64Bit(), offset, this,
                                         debug_macros_sp);

  return debug_macros_sp;
}

bool SymbolFileDWARF::ParseDebugMacros(CompileUnit &comp_unit) {
  ASSERT_MODULE_LOCK(this);

  DWARFUnit *dwarf_cu = GetDWARFCompileUnit(&comp_unit);
  if (dwarf_cu == nullptr)
    return false;

  const DWARFBaseDIE dwarf_cu_die = dwarf_cu->GetUnitDIEOnly();
  if (!dwarf_cu_die)
    return false;

  lldb::offset_t sect_offset =
      dwarf_cu_die.GetAttributeValueAsUnsigned(DW_AT_macros, DW_INVALID_OFFSET);
  if (sect_offset == DW_INVALID_OFFSET)
    sect_offset = dwarf_cu_die.GetAttributeValueAsUnsigned(DW_AT_GNU_macros,
                                                           DW_INVALID_OFFSET);
  if (sect_offset == DW_INVALID_OFFSET)
    return false;

  comp_unit.SetDebugMacros(ParseDebugMacros(&sect_offset));

  return true;
}

size_t SymbolFileDWARF::ParseBlocksRecursive(
    lldb_private::CompileUnit &comp_unit, Block *parent_block,
    const DWARFDIE &orig_die, addr_t subprogram_low_pc, uint32_t depth) {
  size_t blocks_added = 0;
  DWARFDIE die = orig_die;
  while (die) {
    dw_tag_t tag = die.Tag();

    switch (tag) {
    case DW_TAG_inlined_subroutine:
    case DW_TAG_subprogram:
    case DW_TAG_lexical_block: {
      Block *block = NULL;
      if (tag == DW_TAG_subprogram) {
        // Skip any DW_TAG_subprogram DIEs that are inside of a normal or
        // inlined functions. These will be parsed on their own as separate
        // entities.

        if (depth > 0)
          break;

        block = parent_block;
      } else {
        BlockSP block_sp(new Block(die.GetID()));
        parent_block->AddChild(block_sp);
        block = block_sp.get();
      }
      DWARFRangeList ranges;
      const char *name = NULL;
      const char *mangled_name = NULL;

      int decl_file = 0;
      int decl_line = 0;
      int decl_column = 0;
      int call_file = 0;
      int call_line = 0;
      int call_column = 0;
      if (die.GetDIENamesAndRanges(name, mangled_name, ranges, decl_file,
                                   decl_line, decl_column, call_file, call_line,
                                   call_column, nullptr)) {
        if (tag == DW_TAG_subprogram) {
          assert(subprogram_low_pc == LLDB_INVALID_ADDRESS);
          subprogram_low_pc = ranges.GetMinRangeBase(0);
        } else if (tag == DW_TAG_inlined_subroutine) {
          // We get called here for inlined subroutines in two ways. The first
          // time is when we are making the Function object for this inlined
          // concrete instance.  Since we're creating a top level block at
          // here, the subprogram_low_pc will be LLDB_INVALID_ADDRESS.  So we
          // need to adjust the containing address. The second time is when we
          // are parsing the blocks inside the function that contains the
          // inlined concrete instance.  Since these will be blocks inside the
          // containing "real" function the offset will be for that function.
          if (subprogram_low_pc == LLDB_INVALID_ADDRESS) {
            subprogram_low_pc = ranges.GetMinRangeBase(0);
          }
        }

        const size_t num_ranges = ranges.GetSize();
        for (size_t i = 0; i < num_ranges; ++i) {
          const DWARFRangeList::Entry &range = ranges.GetEntryRef(i);
          const addr_t range_base = range.GetRangeBase();
          if (range_base >= subprogram_low_pc)
            block->AddRange(Block::Range(range_base - subprogram_low_pc,
                                         range.GetByteSize()));
          else {
            GetObjectFile()->GetModule()->ReportError(
                "0x%8.8" PRIx64 ": adding range [0x%" PRIx64 "-0x%" PRIx64
                ") which has a base that is less than the function's low PC "
                "0x%" PRIx64 ". Please file a bug and attach the file at the "
                             "start of this error message",
                block->GetID(), range_base, range.GetRangeEnd(),
                subprogram_low_pc);
          }
        }
        block->FinalizeRanges();

        if (tag != DW_TAG_subprogram &&
            (name != NULL || mangled_name != NULL)) {
          std::unique_ptr<Declaration> decl_ap;
          if (decl_file != 0 || decl_line != 0 || decl_column != 0)
            decl_ap.reset(new Declaration(
                comp_unit.GetSupportFiles().GetFileSpecAtIndex(decl_file),
                decl_line, decl_column));

          std::unique_ptr<Declaration> call_ap;
          if (call_file != 0 || call_line != 0 || call_column != 0)
            call_ap.reset(new Declaration(
                comp_unit.GetSupportFiles().GetFileSpecAtIndex(call_file),
                call_line, call_column));

          block->SetInlinedFunctionInfo(name, mangled_name, decl_ap.get(),
                                        call_ap.get());
        }

        ++blocks_added;

        if (die.HasChildren()) {
          blocks_added +=
              ParseBlocksRecursive(comp_unit, block, die.GetFirstChild(),
                                   subprogram_low_pc, depth + 1);
        }
      }
    } break;
    default:
      break;
    }

    // Only parse siblings of the block if we are not at depth zero. A depth of
    // zero indicates we are currently parsing the top level DW_TAG_subprogram
    // DIE

    if (depth == 0)
      die.Clear();
    else
      die = die.GetSibling();
  }
  return blocks_added;
}

bool SymbolFileDWARF::ClassOrStructIsVirtual(const DWARFDIE &parent_die) {
  if (parent_die) {
    for (DWARFDIE die = parent_die.GetFirstChild(); die;
         die = die.GetSibling()) {
      dw_tag_t tag = die.Tag();
      bool check_virtuality = false;
      switch (tag) {
      case DW_TAG_inheritance:
      case DW_TAG_subprogram:
        check_virtuality = true;
        break;
      default:
        break;
      }
      if (check_virtuality) {
        if (die.GetAttributeValueAsUnsigned(DW_AT_virtuality, 0) != 0)
          return true;
      }
    }
  }
  return false;
}

void SymbolFileDWARF::ParseDeclsForContext(CompilerDeclContext decl_ctx) {
  TypeSystem *type_system = decl_ctx.GetTypeSystem();
  DWARFASTParser *ast_parser = type_system->GetDWARFParser();
  std::vector<DWARFDIE> decl_ctx_die_list =
      ast_parser->GetDIEForDeclContext(decl_ctx);

  for (DWARFDIE decl_ctx_die : decl_ctx_die_list)
    for (DWARFDIE decl = decl_ctx_die.GetFirstChild(); decl;
         decl = decl.GetSibling())
      ast_parser->GetDeclForUIDFromDWARF(decl);
}

SymbolFileDWARF *SymbolFileDWARF::GetDWARFForUID(lldb::user_id_t uid) {
  // This method can be called without going through the symbol vendor so we
  // need to lock the module.
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  // Anytime we get a "lldb::user_id_t" from an lldb_private::SymbolFile API we
  // must make sure we use the correct DWARF file when resolving things. On
  // MacOSX, when using SymbolFileDWARFDebugMap, we will use multiple
  // SymbolFileDWARF classes, one for each .o file. We can often end up with
  // references to other DWARF objects and we must be ready to receive a
  // "lldb::user_id_t" that specifies a DIE from another SymbolFileDWARF
  // instance.
  SymbolFileDWARFDebugMap *debug_map = GetDebugMapSymfile();
  if (debug_map)
    return debug_map->GetSymbolFileByOSOIndex(
        debug_map->GetOSOIndexFromUserID(uid));
  return this;
}

DWARFDIE
SymbolFileDWARF::GetDIEFromUID(lldb::user_id_t uid) {
  // This method can be called without going through the symbol vendor so we
  // need to lock the module.
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  // Anytime we get a "lldb::user_id_t" from an lldb_private::SymbolFile API we
  // must make sure we use the correct DWARF file when resolving things. On
  // MacOSX, when using SymbolFileDWARFDebugMap, we will use multiple
  // SymbolFileDWARF classes, one for each .o file. We can often end up with
  // references to other DWARF objects and we must be ready to receive a
  // "lldb::user_id_t" that specifies a DIE from another SymbolFileDWARF
  // instance.
  SymbolFileDWARF *dwarf = GetDWARFForUID(uid);
  if (dwarf)
    return dwarf->GetDIE(DIERef(uid, dwarf));
  return DWARFDIE();
}

CompilerDecl SymbolFileDWARF::GetDeclForUID(lldb::user_id_t type_uid) {
  // This method can be called without going through the symbol vendor so we
  // need to lock the module.
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  // Anytime we have a lldb::user_id_t, we must get the DIE by calling
  // SymbolFileDWARF::GetDIEFromUID(). See comments inside the
  // SymbolFileDWARF::GetDIEFromUID() for details.
  DWARFDIE die = GetDIEFromUID(type_uid);
  if (die)
    return die.GetDecl();
  return CompilerDecl();
}

CompilerDeclContext
SymbolFileDWARF::GetDeclContextForUID(lldb::user_id_t type_uid) {
  // This method can be called without going through the symbol vendor so we
  // need to lock the module.
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  // Anytime we have a lldb::user_id_t, we must get the DIE by calling
  // SymbolFileDWARF::GetDIEFromUID(). See comments inside the
  // SymbolFileDWARF::GetDIEFromUID() for details.
  DWARFDIE die = GetDIEFromUID(type_uid);
  if (die)
    return die.GetDeclContext();
  return CompilerDeclContext();
}

CompilerDeclContext
SymbolFileDWARF::GetDeclContextContainingUID(lldb::user_id_t type_uid) {
  // This method can be called without going through the symbol vendor so we
  // need to lock the module.
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  // Anytime we have a lldb::user_id_t, we must get the DIE by calling
  // SymbolFileDWARF::GetDIEFromUID(). See comments inside the
  // SymbolFileDWARF::GetDIEFromUID() for details.
  DWARFDIE die = GetDIEFromUID(type_uid);
  if (die)
    return die.GetContainingDeclContext();
  return CompilerDeclContext();
}

Type *SymbolFileDWARF::ResolveTypeUID(lldb::user_id_t type_uid) {
  // This method can be called without going through the symbol vendor so we
  // need to lock the module.
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  // Anytime we have a lldb::user_id_t, we must get the DIE by calling
  // SymbolFileDWARF::GetDIEFromUID(). See comments inside the
  // SymbolFileDWARF::GetDIEFromUID() for details.
  DWARFDIE type_die = GetDIEFromUID(type_uid);
  if (type_die)
    return type_die.ResolveType();
  else
    return nullptr;
}

llvm::Optional<SymbolFile::ArrayInfo>
SymbolFileDWARF::GetDynamicArrayInfoForUID(
    lldb::user_id_t type_uid, const lldb_private::ExecutionContext *exe_ctx) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  DWARFDIE type_die = GetDIEFromUID(type_uid);
  if (type_die)
    return DWARFASTParser::ParseChildArrayInfo(type_die, exe_ctx);
  else
    return llvm::None;
}

Type *SymbolFileDWARF::ResolveTypeUID(const DIERef &die_ref) {
  return ResolveType(GetDIE(die_ref), true);
}

Type *SymbolFileDWARF::ResolveTypeUID(const DWARFDIE &die,
                                      bool assert_not_being_parsed) {
  if (die) {
    Log *log(LogChannelDWARF::GetLogIfAll(DWARF_LOG_DEBUG_INFO));
    if (log)
      GetObjectFile()->GetModule()->LogMessage(
          log, "SymbolFileDWARF::ResolveTypeUID (die = 0x%8.8x) %s '%s'",
          die.GetOffset(), die.GetTagAsCString(), die.GetName());

    // We might be coming in in the middle of a type tree (a class within a
    // class, an enum within a class), so parse any needed parent DIEs before
    // we get to this one...
    DWARFDIE decl_ctx_die = GetDeclContextDIEContainingDIE(die);
    if (decl_ctx_die) {
      if (log) {
        switch (decl_ctx_die.Tag()) {
        case DW_TAG_structure_type:
        case DW_TAG_union_type:
        case DW_TAG_class_type: {
          // Get the type, which could be a forward declaration
          if (log)
            GetObjectFile()->GetModule()->LogMessage(
                log, "SymbolFileDWARF::ResolveTypeUID (die = 0x%8.8x) %s '%s' "
                     "resolve parent forward type for 0x%8.8x",
                die.GetOffset(), die.GetTagAsCString(), die.GetName(),
                decl_ctx_die.GetOffset());
        } break;

        default:
          break;
        }
      }
    }
    return ResolveType(die);
  }
  return NULL;
}

// This function is used when SymbolFileDWARFDebugMap owns a bunch of
// SymbolFileDWARF objects to detect if this DWARF file is the one that can
// resolve a compiler_type.
bool SymbolFileDWARF::HasForwardDeclForClangType(
    const CompilerType &compiler_type) {
  CompilerType compiler_type_no_qualifiers =
      ClangUtil::RemoveFastQualifiers(compiler_type);
  if (GetForwardDeclClangTypeToDie().count(
          compiler_type_no_qualifiers.GetOpaqueQualType())) {
    return true;
  }
  TypeSystem *type_system = compiler_type.GetTypeSystem();

  ClangASTContext *clang_type_system =
      llvm::dyn_cast_or_null<ClangASTContext>(type_system);
  if (!clang_type_system)
    return false;
  DWARFASTParserClang *ast_parser =
      static_cast<DWARFASTParserClang *>(clang_type_system->GetDWARFParser());
  return ast_parser->GetClangASTImporter().CanImport(compiler_type);
}

bool SymbolFileDWARF::CompleteType(CompilerType &compiler_type) {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());

  ClangASTContext *clang_type_system =
      llvm::dyn_cast_or_null<ClangASTContext>(compiler_type.GetTypeSystem());
  if (clang_type_system) {
    DWARFASTParserClang *ast_parser =
        static_cast<DWARFASTParserClang *>(clang_type_system->GetDWARFParser());
    if (ast_parser &&
        ast_parser->GetClangASTImporter().CanImport(compiler_type))
      return ast_parser->GetClangASTImporter().CompleteType(compiler_type);
  }

  // We have a struct/union/class/enum that needs to be fully resolved.
  CompilerType compiler_type_no_qualifiers =
      ClangUtil::RemoveFastQualifiers(compiler_type);
  auto die_it = GetForwardDeclClangTypeToDie().find(
      compiler_type_no_qualifiers.GetOpaqueQualType());
  if (die_it == GetForwardDeclClangTypeToDie().end()) {
    // We have already resolved this type...
    return true;
  }

  DWARFDIE dwarf_die = GetDIE(die_it->getSecond());
  if (dwarf_die) {
    // Once we start resolving this type, remove it from the forward
    // declaration map in case anyone child members or other types require this
    // type to get resolved. The type will get resolved when all of the calls
    // to SymbolFileDWARF::ResolveClangOpaqueTypeDefinition are done.
    GetForwardDeclClangTypeToDie().erase(die_it);

    Type *type = GetDIEToType().lookup(dwarf_die.GetDIE());

    Log *log(LogChannelDWARF::GetLogIfAny(DWARF_LOG_DEBUG_INFO |
                                          DWARF_LOG_TYPE_COMPLETION));
    if (log)
      GetObjectFile()->GetModule()->LogMessageVerboseBacktrace(
          log, "0x%8.8" PRIx64 ": %s '%s' resolving forward declaration...",
          dwarf_die.GetID(), dwarf_die.GetTagAsCString(),
          type->GetName().AsCString());
    assert(compiler_type);
    DWARFASTParser *dwarf_ast = dwarf_die.GetDWARFParser();
    if (dwarf_ast)
      return dwarf_ast->CompleteTypeFromDWARF(dwarf_die, type, compiler_type);
  }
  return false;
}

Type *SymbolFileDWARF::ResolveType(const DWARFDIE &die,
                                   bool assert_not_being_parsed,
                                   bool resolve_function_context) {
  if (die) {
    Type *type = GetTypeForDIE(die, resolve_function_context).get();

    if (assert_not_being_parsed) {
      if (type != DIE_IS_BEING_PARSED)
        return type;

      GetObjectFile()->GetModule()->ReportError(
          "Parsing a die that is being parsed die: 0x%8.8x: %s %s",
          die.GetOffset(), die.GetTagAsCString(), die.GetName());

    } else
      return type;
  }
  return nullptr;
}

CompileUnit *
SymbolFileDWARF::GetCompUnitForDWARFCompUnit(DWARFUnit *dwarf_cu,
                                             uint32_t cu_idx) {
  // Check if the symbol vendor already knows about this compile unit?
  if (dwarf_cu->GetUserData() == NULL) {
    // The symbol vendor doesn't know about this compile unit, we need to parse
    // and add it to the symbol vendor object.
    return ParseCompileUnit(dwarf_cu, cu_idx).get();
  }
  return (CompileUnit *)dwarf_cu->GetUserData();
}

size_t SymbolFileDWARF::GetObjCMethodDIEOffsets(ConstString class_name,
                                                DIEArray &method_die_offsets) {
  method_die_offsets.clear();
  m_index->GetObjCMethods(class_name, method_die_offsets);
  return method_die_offsets.size();
}

bool SymbolFileDWARF::GetFunction(const DWARFDIE &die, SymbolContext &sc) {
  sc.Clear(false);

  if (die) {
    // Check if the symbol vendor already knows about this compile unit?
    sc.comp_unit = GetCompUnitForDWARFCompUnit(die.GetCU(), UINT32_MAX);

    sc.function = sc.comp_unit->FindFunctionByUID(die.GetID()).get();
    if (sc.function == NULL)
      sc.function = ParseFunction(*sc.comp_unit, die);

    if (sc.function) {
      sc.module_sp = sc.function->CalculateSymbolContextModule();
      return true;
    }
  }

  return false;
}

lldb::ModuleSP SymbolFileDWARF::GetDWOModule(ConstString name) {
  UpdateExternalModuleListIfNeeded();
  const auto &pos = m_external_type_modules.find(name);
  if (pos != m_external_type_modules.end())
    return pos->second;
  else
    return lldb::ModuleSP();
}

DWARFDIE
SymbolFileDWARF::GetDIE(const DIERef &die_ref) {
  DWARFDebugInfo *debug_info = DebugInfo();
  if (debug_info)
    return debug_info->GetDIE(die_ref);
  else
    return DWARFDIE();
}

std::unique_ptr<SymbolFileDWARFDwo>
SymbolFileDWARF::GetDwoSymbolFileForCompileUnit(
    DWARFUnit &dwarf_cu, const DWARFDebugInfoEntry &cu_die) {
  // If we are using a dSYM file, we never want the standard DWO files since
  // the -gmodules support uses the same DWO machanism to specify full debug
  // info files for modules.
  if (GetDebugMapSymfile())
    return nullptr;

  const char *dwo_name = cu_die.GetAttributeValueAsString(
      this, &dwarf_cu, DW_AT_GNU_dwo_name, nullptr);
  if (!dwo_name)
    return nullptr;

  SymbolFileDWARFDwp *dwp_symfile = GetDwpSymbolFile();
  if (dwp_symfile) {
    uint64_t dwo_id = cu_die.GetAttributeValueAsUnsigned(this, &dwarf_cu,
                                                         DW_AT_GNU_dwo_id, 0);
    std::unique_ptr<SymbolFileDWARFDwo> dwo_symfile =
        dwp_symfile->GetSymbolFileForDwoId(&dwarf_cu, dwo_id);
    if (dwo_symfile)
      return dwo_symfile;
  }

  FileSpec dwo_file(dwo_name);
  FileSystem::Instance().Resolve(dwo_file);
  if (dwo_file.IsRelative()) {
    const char *comp_dir = cu_die.GetAttributeValueAsString(
        this, &dwarf_cu, DW_AT_comp_dir, nullptr);
    if (!comp_dir)
      return nullptr;

    dwo_file.SetFile(comp_dir, FileSpec::Style::native);
    FileSystem::Instance().Resolve(dwo_file);
    dwo_file.AppendPathComponent(dwo_name);
  }

  if (!FileSystem::Instance().Exists(dwo_file))
    return nullptr;

  const lldb::offset_t file_offset = 0;
  DataBufferSP dwo_file_data_sp;
  lldb::offset_t dwo_file_data_offset = 0;
  ObjectFileSP dwo_obj_file = ObjectFile::FindPlugin(
      GetObjectFile()->GetModule(), &dwo_file, file_offset,
      FileSystem::Instance().GetByteSize(dwo_file), dwo_file_data_sp,
      dwo_file_data_offset);
  if (dwo_obj_file == nullptr)
    return nullptr;

  return llvm::make_unique<SymbolFileDWARFDwo>(dwo_obj_file, &dwarf_cu);
}

void SymbolFileDWARF::UpdateExternalModuleListIfNeeded() {
  if (m_fetched_external_modules)
    return;
  m_fetched_external_modules = true;

  DWARFDebugInfo *debug_info = DebugInfo();

  const uint32_t num_compile_units = GetNumCompileUnits();
  for (uint32_t cu_idx = 0; cu_idx < num_compile_units; ++cu_idx) {
    DWARFUnit *dwarf_cu = debug_info->GetCompileUnitAtIndex(cu_idx);

    const DWARFBaseDIE die = dwarf_cu->GetUnitDIEOnly();
    if (die && !die.HasChildren()) {
      const char *name = die.GetAttributeValueAsString(DW_AT_name, nullptr);

      if (name) {
        ConstString const_name(name);
        if (m_external_type_modules.find(const_name) ==
            m_external_type_modules.end()) {
          ModuleSP module_sp;
          const char *dwo_path =
              die.GetAttributeValueAsString(DW_AT_GNU_dwo_name, nullptr);
          if (dwo_path) {
            ModuleSpec dwo_module_spec;
            dwo_module_spec.GetFileSpec().SetFile(dwo_path,
                                                  FileSpec::Style::native);
            if (dwo_module_spec.GetFileSpec().IsRelative()) {
              const char *comp_dir =
                  die.GetAttributeValueAsString(DW_AT_comp_dir, nullptr);
              if (comp_dir) {
                dwo_module_spec.GetFileSpec().SetFile(comp_dir,
                                                      FileSpec::Style::native);
                FileSystem::Instance().Resolve(dwo_module_spec.GetFileSpec());
                dwo_module_spec.GetFileSpec().AppendPathComponent(dwo_path);
              }
            }
            dwo_module_spec.GetArchitecture() =
                m_obj_file->GetModule()->GetArchitecture();

            // When LLDB loads "external" modules it looks at the presence of
            // DW_AT_GNU_dwo_name. However, when the already created module
            // (corresponding to .dwo itself) is being processed, it will see
            // the presence of DW_AT_GNU_dwo_name (which contains the name of
            // dwo file) and will try to call ModuleList::GetSharedModule
            // again. In some cases (i.e. for empty files) Clang 4.0 generates
            // a *.dwo file which has DW_AT_GNU_dwo_name, but no
            // DW_AT_comp_dir. In this case the method
            // ModuleList::GetSharedModule will fail and the warning will be
            // printed. However, as one can notice in this case we don't
            // actually need to try to load the already loaded module
            // (corresponding to .dwo) so we simply skip it.
            if (m_obj_file->GetFileSpec()
                        .GetFileNameExtension()
                        .GetStringRef() == ".dwo" &&
                llvm::StringRef(m_obj_file->GetFileSpec().GetPath())
                    .endswith(dwo_module_spec.GetFileSpec().GetPath())) {
              continue;
            }

            Status error = ModuleList::GetSharedModule(
                dwo_module_spec, module_sp, NULL, NULL, NULL);
            if (!module_sp) {
              GetObjectFile()->GetModule()->ReportWarning(
                  "0x%8.8x: unable to locate module needed for external types: "
                  "%s\nerror: %s\nDebugging will be degraded due to missing "
                  "types. Rebuilding your project will regenerate the needed "
                  "module files.",
                  die.GetOffset(),
                  dwo_module_spec.GetFileSpec().GetPath().c_str(),
                  error.AsCString("unknown error"));
            }
          }
          m_external_type_modules[const_name] = module_sp;
        }
      }
    }
  }
}

SymbolFileDWARF::GlobalVariableMap &SymbolFileDWARF::GetGlobalAranges() {
  if (!m_global_aranges_ap) {
    m_global_aranges_ap.reset(new GlobalVariableMap());

    ModuleSP module_sp = GetObjectFile()->GetModule();
    if (module_sp) {
      const size_t num_cus = module_sp->GetNumCompileUnits();
      for (size_t i = 0; i < num_cus; ++i) {
        CompUnitSP cu_sp = module_sp->GetCompileUnitAtIndex(i);
        if (cu_sp) {
          VariableListSP globals_sp = cu_sp->GetVariableList(true);
          if (globals_sp) {
            const size_t num_globals = globals_sp->GetSize();
            for (size_t g = 0; g < num_globals; ++g) {
              VariableSP var_sp = globals_sp->GetVariableAtIndex(g);
              if (var_sp && !var_sp->GetLocationIsConstantValueData()) {
                const DWARFExpression &location = var_sp->LocationExpression();
                Value location_result;
                Status error;
                if (location.Evaluate(nullptr, LLDB_INVALID_ADDRESS, nullptr,
                                      nullptr, location_result, &error)) {
                  if (location_result.GetValueType() ==
                      Value::eValueTypeFileAddress) {
                    lldb::addr_t file_addr =
                        location_result.GetScalar().ULongLong();
                    lldb::addr_t byte_size = 1;
                    if (var_sp->GetType())
                      byte_size = var_sp->GetType()->GetByteSize();
                    m_global_aranges_ap->Append(GlobalVariableMap::Entry(
                        file_addr, byte_size, var_sp.get()));
                  }
                }
              }
            }
          }
        }
      }
    }
    m_global_aranges_ap->Sort();
  }
  return *m_global_aranges_ap;
}

uint32_t SymbolFileDWARF::ResolveSymbolContext(const Address &so_addr,
                                               SymbolContextItem resolve_scope,
                                               SymbolContext &sc) {
  static Timer::Category func_cat(LLVM_PRETTY_FUNCTION);
  Timer scoped_timer(func_cat,
                     "SymbolFileDWARF::"
                     "ResolveSymbolContext (so_addr = { "
                     "section = %p, offset = 0x%" PRIx64
                     " }, resolve_scope = 0x%8.8x)",
                     static_cast<void *>(so_addr.GetSection().get()),
                     so_addr.GetOffset(), resolve_scope);
  uint32_t resolved = 0;
  if (resolve_scope &
      (eSymbolContextCompUnit | eSymbolContextFunction | eSymbolContextBlock |
       eSymbolContextLineEntry | eSymbolContextVariable)) {
    lldb::addr_t file_vm_addr = so_addr.GetFileAddress();

    DWARFDebugInfo *debug_info = DebugInfo();
    if (debug_info) {
      const dw_offset_t cu_offset =
          debug_info->GetCompileUnitAranges().FindAddress(file_vm_addr);
      if (cu_offset == DW_INVALID_OFFSET) {
        // Global variables are not in the compile unit address ranges. The
        // only way to currently find global variables is to iterate over the
        // .debug_pubnames or the __apple_names table and find all items in
        // there that point to DW_TAG_variable DIEs and then find the address
        // that matches.
        if (resolve_scope & eSymbolContextVariable) {
          GlobalVariableMap &map = GetGlobalAranges();
          const GlobalVariableMap::Entry *entry =
              map.FindEntryThatContains(file_vm_addr);
          if (entry && entry->data) {
            Variable *variable = entry->data;
            SymbolContextScope *scc = variable->GetSymbolContextScope();
            if (scc) {
              scc->CalculateSymbolContext(&sc);
              sc.variable = variable;
            }
            return sc.GetResolvedMask();
          }
        }
      } else {
        uint32_t cu_idx = DW_INVALID_INDEX;
        DWARFUnit *dwarf_cu =
            debug_info->GetCompileUnit(cu_offset, &cu_idx);
        if (dwarf_cu) {
          sc.comp_unit = GetCompUnitForDWARFCompUnit(dwarf_cu, cu_idx);
          if (sc.comp_unit) {
            resolved |= eSymbolContextCompUnit;

            bool force_check_line_table = false;
            if (resolve_scope &
                (eSymbolContextFunction | eSymbolContextBlock)) {
              DWARFDIE function_die = dwarf_cu->LookupAddress(file_vm_addr);
              DWARFDIE block_die;
              if (function_die) {
                sc.function =
                    sc.comp_unit->FindFunctionByUID(function_die.GetID()).get();
                if (sc.function == NULL)
                  sc.function = ParseFunction(*sc.comp_unit, function_die);

                if (sc.function && (resolve_scope & eSymbolContextBlock))
                  block_die = function_die.LookupDeepestBlock(file_vm_addr);
              } else {
                // We might have had a compile unit that had discontiguous
                // address ranges where the gaps are symbols that don't have
                // any debug info. Discontiguous compile unit address ranges
                // should only happen when there aren't other functions from
                // other compile units in these gaps. This helps keep the size
                // of the aranges down.
                force_check_line_table = true;
              }

              if (sc.function != NULL) {
                resolved |= eSymbolContextFunction;

                if (resolve_scope & eSymbolContextBlock) {
                  Block &block = sc.function->GetBlock(true);

                  if (block_die)
                    sc.block = block.FindBlockByID(block_die.GetID());
                  else
                    sc.block = block.FindBlockByID(function_die.GetID());
                  if (sc.block)
                    resolved |= eSymbolContextBlock;
                }
              }
            }

            if ((resolve_scope & eSymbolContextLineEntry) ||
                force_check_line_table) {
              LineTable *line_table = sc.comp_unit->GetLineTable();
              if (line_table != NULL) {
                // And address that makes it into this function should be in
                // terms of this debug file if there is no debug map, or it
                // will be an address in the .o file which needs to be fixed up
                // to be in terms of the debug map executable. Either way,
                // calling FixupAddress() will work for us.
                Address exe_so_addr(so_addr);
                if (FixupAddress(exe_so_addr)) {
                  if (line_table->FindLineEntryByAddress(exe_so_addr,
                                                         sc.line_entry)) {
                    resolved |= eSymbolContextLineEntry;
                  }
                }
              }
            }

            if (force_check_line_table &&
                !(resolved & eSymbolContextLineEntry)) {
              // We might have had a compile unit that had discontiguous
              // address ranges where the gaps are symbols that don't have any
              // debug info. Discontiguous compile unit address ranges should
              // only happen when there aren't other functions from other
              // compile units in these gaps. This helps keep the size of the
              // aranges down.
              sc.comp_unit = NULL;
              resolved &= ~eSymbolContextCompUnit;
            }
          } else {
            GetObjectFile()->GetModule()->ReportWarning(
                "0x%8.8x: compile unit %u failed to create a valid "
                "lldb_private::CompileUnit class.",
                cu_offset, cu_idx);
          }
        }
      }
    }
  }
  return resolved;
}

uint32_t SymbolFileDWARF::ResolveSymbolContext(const FileSpec &file_spec,
                                               uint32_t line,
                                               bool check_inlines,
                                               SymbolContextItem resolve_scope,
                                               SymbolContextList &sc_list) {
  const uint32_t prev_size = sc_list.GetSize();
  if (resolve_scope & eSymbolContextCompUnit) {
    DWARFDebugInfo *debug_info = DebugInfo();
    if (debug_info) {
      uint32_t cu_idx;
      DWARFUnit *dwarf_cu = NULL;

      for (cu_idx = 0;
           (dwarf_cu = debug_info->GetCompileUnitAtIndex(cu_idx)) != NULL;
           ++cu_idx) {
        CompileUnit *dc_cu = GetCompUnitForDWARFCompUnit(dwarf_cu, cu_idx);
        const bool full_match = (bool)file_spec.GetDirectory();
        bool file_spec_matches_cu_file_spec =
            dc_cu != NULL && FileSpec::Equal(file_spec, *dc_cu, full_match);
        if (check_inlines || file_spec_matches_cu_file_spec) {
          SymbolContext sc(m_obj_file->GetModule());
          sc.comp_unit = GetCompUnitForDWARFCompUnit(dwarf_cu, cu_idx);
          if (sc.comp_unit) {
            uint32_t file_idx = UINT32_MAX;

            // If we are looking for inline functions only and we don't find it
            // in the support files, we are done.
            if (check_inlines) {
              file_idx = sc.comp_unit->GetSupportFiles().FindFileIndex(
                  1, file_spec, true);
              if (file_idx == UINT32_MAX)
                continue;
            }

            if (line != 0) {
              LineTable *line_table = sc.comp_unit->GetLineTable();

              if (line_table != NULL && line != 0) {
                // We will have already looked up the file index if we are
                // searching for inline entries.
                if (!check_inlines)
                  file_idx = sc.comp_unit->GetSupportFiles().FindFileIndex(
                      1, file_spec, true);

                if (file_idx != UINT32_MAX) {
                  uint32_t found_line;
                  uint32_t line_idx = line_table->FindLineEntryIndexByFileIndex(
                      0, file_idx, line, false, &sc.line_entry);
                  found_line = sc.line_entry.line;

                  while (line_idx != UINT32_MAX) {
                    sc.function = NULL;
                    sc.block = NULL;
                    if (resolve_scope &
                        (eSymbolContextFunction | eSymbolContextBlock)) {
                      const lldb::addr_t file_vm_addr =
                          sc.line_entry.range.GetBaseAddress().GetFileAddress();
                      if (file_vm_addr != LLDB_INVALID_ADDRESS) {
                        DWARFDIE function_die =
                            dwarf_cu->LookupAddress(file_vm_addr);
                        DWARFDIE block_die;
                        if (function_die) {
                          sc.function =
                              sc.comp_unit
                                  ->FindFunctionByUID(function_die.GetID())
                                  .get();
                          if (sc.function == NULL)
                            sc.function =
                                ParseFunction(*sc.comp_unit, function_die);

                          if (sc.function &&
                              (resolve_scope & eSymbolContextBlock))
                            block_die =
                                function_die.LookupDeepestBlock(file_vm_addr);
                        }

                        if (sc.function != NULL) {
                          Block &block = sc.function->GetBlock(true);

                          if (block_die)
                            sc.block = block.FindBlockByID(block_die.GetID());
                          else if (function_die)
                            sc.block =
                                block.FindBlockByID(function_die.GetID());
                        }
                      }
                    }

                    sc_list.Append(sc);
                    line_idx = line_table->FindLineEntryIndexByFileIndex(
                        line_idx + 1, file_idx, found_line, true,
                        &sc.line_entry);
                  }
                }
              } else if (file_spec_matches_cu_file_spec && !check_inlines) {
                // only append the context if we aren't looking for inline call
                // sites by file and line and if the file spec matches that of
                // the compile unit
                sc_list.Append(sc);
              }
            } else if (file_spec_matches_cu_file_spec && !check_inlines) {
              // only append the context if we aren't looking for inline call
              // sites by file and line and if the file spec matches that of
              // the compile unit
              sc_list.Append(sc);
            }

            if (!check_inlines)
              break;
          }
        }
      }
    }
  }
  return sc_list.GetSize() - prev_size;
}

void SymbolFileDWARF::PreloadSymbols() {
  std::lock_guard<std::recursive_mutex> guard(GetModuleMutex());
  m_index->Preload();
}

std::recursive_mutex &SymbolFileDWARF::GetModuleMutex() const {
  lldb::ModuleSP module_sp(m_debug_map_module_wp.lock());
  if (module_sp)
    return module_sp->GetMutex();
  return GetObjectFile()->GetModule()->GetMutex();
}

bool SymbolFileDWARF::DeclContextMatchesThisSymbolFile(
    const lldb_private::CompilerDeclContext *decl_ctx) {
  if (decl_ctx == nullptr || !decl_ctx->IsValid()) {
    // Invalid namespace decl which means we aren't matching only things in
    // this symbol file, so return true to indicate it matches this symbol
    // file.
    return true;
  }

  TypeSystem *decl_ctx_type_system = decl_ctx->GetTypeSystem();
  TypeSystem *type_system = GetTypeSystemForLanguage(
      decl_ctx_type_system->GetMinimumLanguage(nullptr));
  if (decl_ctx_type_system == type_system)
    return true; // The type systems match, return true

  // The namespace AST was valid, and it does not match...
  Log *log(LogChannelDWARF::GetLogIfAll(DWARF_LOG_LOOKUPS));

  if (log)
    GetObjectFile()->GetModule()->LogMessage(
        log, "Valid namespace does not match symbol file");

  return false;
}

uint32_t SymbolFileDWARF::FindGlobalVariables(
    const ConstString &name, const CompilerDeclContext *parent_decl_ctx,
    uint32_t max_matches, VariableList &variables) {
  Log *log(LogChannelDWARF::GetLogIfAll(DWARF_LOG_LOOKUPS));

  if (log)
    GetObjectFile()->GetModule()->LogMessage(
        log,
        "SymbolFileDWARF::FindGlobalVariables (name=\"%s\", "
        "parent_decl_ctx=%p, max_matches=%u, variables)",
        name.GetCString(), static_cast<const void *>(parent_decl_ctx),
        max_matches);

  if (!DeclContextMatchesThisSymbolFile(parent_decl_ctx))
    return 0;

  DWARFDebugInfo *info = DebugInfo();
  if (info == NULL)
    return 0;

  // Remember how many variables are in the list before we search.
  const uint32_t original_size = variables.GetSize();

  llvm::StringRef basename;
  llvm::StringRef context;

  if (!CPlusPlusLanguage::ExtractContextAndIdentifier(name.GetCString(),
                                                      context, basename))
    basename = name.GetStringRef();

  DIEArray die_offsets;
  m_index->GetGlobalVariables(ConstString(basename), die_offsets);
  const size_t num_die_matches = die_offsets.size();
  if (num_die_matches) {
    SymbolContext sc;
    sc.module_sp = m_obj_file->GetModule();
    assert(sc.module_sp);

    // Loop invariant: Variables up to this index have been checked for context
    // matches.
    uint32_t pruned_idx = original_size;

    bool done = false;
    for (size_t i = 0; i < num_die_matches && !done; ++i) {
      const DIERef &die_ref = die_offsets[i];
      DWARFDIE die = GetDIE(die_ref);

      if (die) {
        switch (die.Tag()) {
        default:
        case DW_TAG_subprogram:
        case DW_TAG_inlined_subroutine:
        case DW_TAG_try_block:
        case DW_TAG_catch_block:
          break;

        case DW_TAG_variable: {
          sc.comp_unit = GetCompUnitForDWARFCompUnit(die.GetCU(), UINT32_MAX);

          if (parent_decl_ctx) {
            DWARFASTParser *dwarf_ast = die.GetDWARFParser();
            if (dwarf_ast) {
              CompilerDeclContext actual_parent_decl_ctx =
                  dwarf_ast->GetDeclContextContainingUIDFromDWARF(die);
              if (!actual_parent_decl_ctx ||
                  actual_parent_decl_ctx != *parent_decl_ctx)
                continue;
            }
          }

          ParseVariables(sc, die, LLDB_INVALID_ADDRESS, false, false,
                         &variables);
          while (pruned_idx < variables.GetSize()) {
            VariableSP var_sp = variables.GetVariableAtIndex(pruned_idx);
            if (var_sp->GetName().GetStringRef().contains(name.GetStringRef()))
              ++pruned_idx;
            else
              variables.RemoveVariableAtIndex(pruned_idx);
          }

          if (variables.GetSize() - original_size >= max_matches)
            done = true;
        } break;
        }
      } else {
        m_index->ReportInvalidDIEOffset(die_ref.die_offset,
                                        name.GetStringRef());
      }
    }
  }

  // Return the number of variable that were appended to the list
  const uint32_t num_matches = variables.GetSize() - original_size;
  if (log && num_matches > 0) {
    GetObjectFile()->GetModule()->LogMessage(
        log,
        "SymbolFileDWARF::FindGlobalVariables (name=\"%s\", "
        "parent_decl_ctx=%p, max_matches=%u, variables) => %u",
        name.GetCString(), static_cast<const void *>(parent_decl_ctx),
        max_matches, num_matches);
  }
  return num_matches;
}

uint32_t SymbolFileDWARF::FindGlobalVariables(const RegularExpression &regex,
                                              uint32_t max_matches,
                                              VariableList &variables) {
  Log *log(LogChannelDWARF::GetLogIfAll(DWARF_LOG_LOOKUPS));

  if (log) {
    GetObjectFile()->GetModule()->LogMessage(
        log,
        "SymbolFileDWARF::FindGlobalVariables (regex=\"%s\", "
        "max_matches=%u, variables)",
        regex.GetText().str().c_str(), max_matches);
  }

  DWARFDebugInfo *info = DebugInfo();
  if (info == NULL)
    return 0;

  // Remember how many variables are in the list before we search.
  const uint32_t original_size = variables.GetSize();

  DIEArray die_offsets;
  m_index->GetGlobalVariables(regex, die_offsets);

  SymbolContext sc;
  sc.module_sp = m_obj_file->GetModule();
  assert(sc.module_sp);

  const size_t num_matches = die_offsets.size();
  if (num_matches) {
    for (size_t i = 0; i < num_matches; ++i) {
      const DIERef &die_ref = die_offsets[i];
      DWARFDIE die = GetDIE(die_ref);

      if (die) {
        sc.comp_unit = GetCompUnitForDWARFCompUnit(die.GetCU(), UINT32_MAX);

        ParseVariables(sc, die, LLDB_INVALID_ADDRESS, false, false, &variables);

        if (variables.GetSize() - original_size >= max_matches)
          break;
      } else
        m_index->ReportInvalidDIEOffset(die_ref.die_offset, regex.GetText());
    }
  }

  // Return the number of variable that were appended to the list
  return variables.GetSize() - original_size;
}

bool SymbolFileDWARF::ResolveFunction(const DWARFDIE &orig_die,
                                      bool include_inlines,
                                      SymbolContextList &sc_list) {
  SymbolContext sc;

  if (!orig_die)
    return false;

  // If we were passed a die that is not a function, just return false...
  if (!(orig_die.Tag() == DW_TAG_subprogram ||
        (include_inlines && orig_die.Tag() == DW_TAG_inlined_subroutine)))
    return false;

  DWARFDIE die = orig_die;
  DWARFDIE inlined_die;
  if (die.Tag() == DW_TAG_inlined_subroutine) {
    inlined_die = die;

    while (1) {
      die = die.GetParent();

      if (die) {
        if (die.Tag() == DW_TAG_subprogram)
          break;
      } else
        break;
    }
  }
  assert(die && die.Tag() == DW_TAG_subprogram);
  if (GetFunction(die, sc)) {
    Address addr;
    // Parse all blocks if needed
    if (inlined_die) {
      Block &function_block = sc.function->GetBlock(true);
      sc.block = function_block.FindBlockByID(inlined_die.GetID());
      if (sc.block == NULL)
        sc.block = function_block.FindBlockByID(inlined_die.GetOffset());
      if (sc.block == NULL || !sc.block->GetStartAddress(addr))
        addr.Clear();
    } else {
      sc.block = NULL;
      addr = sc.function->GetAddressRange().GetBaseAddress();
    }

    if (addr.IsValid()) {
      sc_list.Append(sc);
      return true;
    }
  }

  return false;
}

bool SymbolFileDWARF::DIEInDeclContext(const CompilerDeclContext *decl_ctx,
                                       const DWARFDIE &die) {
  // If we have no parent decl context to match this DIE matches, and if the
  // parent decl context isn't valid, we aren't trying to look for any
  // particular decl context so any die matches.
  if (decl_ctx == nullptr || !decl_ctx->IsValid())
    return true;

  if (die) {
    DWARFASTParser *dwarf_ast = die.GetDWARFParser();
    if (dwarf_ast) {
      CompilerDeclContext actual_decl_ctx =
          dwarf_ast->GetDeclContextContainingUIDFromDWARF(die);
      if (actual_decl_ctx)
        return actual_decl_ctx == *decl_ctx;
    }
  }
  return false;
}

uint32_t SymbolFileDWARF::FindFunctions(
    const ConstString &name, const CompilerDeclContext *parent_decl_ctx,
    FunctionNameType name_type_mask, bool include_inlines, bool append,
    SymbolContextList &sc_list) {
  static Timer::Category func_cat(LLVM_PRETTY_FUNCTION);
  Timer scoped_timer(func_cat, "SymbolFileDWARF::FindFunctions (name = '%s')",
                     name.AsCString());

  // eFunctionNameTypeAuto should be pre-resolved by a call to
  // Module::LookupInfo::LookupInfo()
  assert((name_type_mask & eFunctionNameTypeAuto) == 0);

  Log *log(LogChannelDWARF::GetLogIfAll(DWARF_LOG_LOOKUPS));

  if (log) {
    GetObjectFile()->GetModule()->LogMessage(
        log, "SymbolFileDWARF::FindFunctions (name=\"%s\", "
             "name_type_mask=0x%x, append=%u, sc_list)",
        name.GetCString(), name_type_mask, append);
  }

  // If we aren't appending the results to this list, then clear the list
  if (!append)
    sc_list.Clear();

  if (!DeclContextMatchesThisSymbolFile(parent_decl_ctx))
    return 0;

  // If name is empty then we won't find anything.
  if (name.IsEmpty())
    return 0;

  // Remember how many sc_list are in the list before we search in case we are
  // appending the results to a variable list.

  const uint32_t original_size = sc_list.GetSize();

  DWARFDebugInfo *info = DebugInfo();
  if (info == NULL)
    return 0;

  llvm::DenseSet<const DWARFDebugInfoEntry *> resolved_dies;
  DIEArray offsets;
  CompilerDeclContext empty_decl_ctx;
  if (!parent_decl_ctx)
    parent_decl_ctx = &empty_decl_ctx;

  std::vector<DWARFDIE> dies;
  m_index->GetFunctions(name, *info, *parent_decl_ctx, name_type_mask, dies);
  for (const DWARFDIE &die: dies) {
    if (resolved_dies.insert(die.GetDIE()).second)
      ResolveFunction(die, include_inlines, sc_list);
  }

  // Return the number of variable that were appended to the list
  const uint32_t num_matches = sc_list.GetSize() - original_size;

  if (log && num_matches > 0) {
    GetObjectFile()->GetModule()->LogMessage(
        log, "SymbolFileDWARF::FindFunctions (name=\"%s\", "
             "name_type_mask=0x%x, include_inlines=%d, append=%u, sc_list) => "
             "%u",
        name.GetCString(), name_type_mask, include_inlines, append,
        num_matches);
  }
  return num_matches;
}

uint32_t SymbolFileDWARF::FindFunctions(const RegularExpression &regex,
                                        bool include_inlines, bool append,
                                        SymbolContextList &sc_list) {
  static Timer::Category func_cat(LLVM_PRETTY_FUNCTION);
  Timer scoped_timer(func_cat, "SymbolFileDWARF::FindFunctions (regex = '%s')",
                     regex.GetText().str().c_str());

  Log *log(LogChannelDWARF::GetLogIfAll(DWARF_LOG_LOOKUPS));

  if (log) {
    GetObjectFile()->GetModule()->LogMessage(
        log,
        "SymbolFileDWARF::FindFunctions (regex=\"%s\", append=%u, sc_list)",
        regex.GetText().str().c_str(), append);
  }

  // If we aren't appending the results to this list, then clear the list
  if (!append)
    sc_list.Clear();

  DWARFDebugInfo *info = DebugInfo();
  if (!info)
    return 0;

  // Remember how many sc_list are in the list before we search in case we are
  // appending the results to a variable list.
  uint32_t original_size = sc_list.GetSize();

  DIEArray offsets;
  m_index->GetFunctions(regex, offsets);

  llvm::DenseSet<const DWARFDebugInfoEntry *> resolved_dies;
  for (DIERef ref : offsets) {
    DWARFDIE die = info->GetDIE(ref);
    if (!die) {
      m_index->ReportInvalidDIEOffset(ref.die_offset, regex.GetText());
      continue;
    }
    if (resolved_dies.insert(die.GetDIE()).second)
      ResolveFunction(die, include_inlines, sc_list);
  }

  // Return the number of variable that were appended to the list
  return sc_list.GetSize() - original_size;
}

void SymbolFileDWARF::GetMangledNamesForFunction(
    const std::string &scope_qualified_name,
    std::vector<ConstString> &mangled_names) {
  DWARFDebugInfo *info = DebugInfo();
  uint32_t num_comp_units = 0;
  if (info)
    num_comp_units = info->GetNumCompileUnits();

  for (uint32_t i = 0; i < num_comp_units; i++) {
    DWARFUnit *cu = info->GetCompileUnitAtIndex(i);
    if (cu == nullptr)
      continue;

    SymbolFileDWARFDwo *dwo = cu->GetDwoSymbolFile();
    if (dwo)
      dwo->GetMangledNamesForFunction(scope_qualified_name, mangled_names);
  }

  NameToOffsetMap::iterator iter =
      m_function_scope_qualified_name_map.find(scope_qualified_name);
  if (iter == m_function_scope_qualified_name_map.end())
    return;

  DIERefSetSP set_sp = (*iter).second;
  std::set<DIERef>::iterator set_iter;
  for (set_iter = set_sp->begin(); set_iter != set_sp->end(); set_iter++) {
    DWARFDIE die = DebugInfo()->GetDIE(*set_iter);
    mangled_names.push_back(ConstString(die.GetMangledName()));
  }
}

uint32_t SymbolFileDWARF::FindTypes(
    const ConstString &name, const CompilerDeclContext *parent_decl_ctx,
    bool append, uint32_t max_matches,
    llvm::DenseSet<lldb_private::SymbolFile *> &searched_symbol_files,
    TypeMap &types) {
  // If we aren't appending the results to this list, then clear the list
  if (!append)
    types.Clear();

  // Make sure we haven't already searched this SymbolFile before...
  if (searched_symbol_files.count(this))
    return 0;
  else
    searched_symbol_files.insert(this);

  DWARFDebugInfo *info = DebugInfo();
  if (info == NULL)
    return 0;

  Log *log(LogChannelDWARF::GetLogIfAll(DWARF_LOG_LOOKUPS));

  if (log) {
    if (parent_decl_ctx)
      GetObjectFile()->GetModule()->LogMessage(
          log, "SymbolFileDWARF::FindTypes (sc, name=\"%s\", parent_decl_ctx = "
               "%p (\"%s\"), append=%u, max_matches=%u, type_list)",
          name.GetCString(), static_cast<const void *>(parent_decl_ctx),
          parent_decl_ctx->GetName().AsCString("<NULL>"), append, max_matches);
    else
      GetObjectFile()->GetModule()->LogMessage(
          log, "SymbolFileDWARF::FindTypes (sc, name=\"%s\", parent_decl_ctx = "
               "NULL, append=%u, max_matches=%u, type_list)",
          name.GetCString(), append, max_matches);
  }

  if (!DeclContextMatchesThisSymbolFile(parent_decl_ctx))
    return 0;

  DIEArray die_offsets;
  m_index->GetTypes(name, die_offsets);
  const size_t num_die_matches = die_offsets.size();

  if (num_die_matches) {
    const uint32_t initial_types_size = types.GetSize();
    for (size_t i = 0; i < num_die_matches; ++i) {
      const DIERef &die_ref = die_offsets[i];
      DWARFDIE die = GetDIE(die_ref);

      if (die) {
        if (!DIEInDeclContext(parent_decl_ctx, die))
          continue; // The containing decl contexts don't match

        Type *matching_type = ResolveType(die, true, true);
        if (matching_type) {
          // We found a type pointer, now find the shared pointer form our type
          // list
          types.InsertUnique(matching_type->shared_from_this());
          if (types.GetSize() >= max_matches)
            break;
        }
      } else {
        m_index->ReportInvalidDIEOffset(die_ref.die_offset,
                                        name.GetStringRef());
      }
    }
    const uint32_t num_matches = types.GetSize() - initial_types_size;
    if (log && num_matches) {
      if (parent_decl_ctx) {
        GetObjectFile()->GetModule()->LogMessage(
            log, "SymbolFileDWARF::FindTypes (sc, name=\"%s\", parent_decl_ctx "
                 "= %p (\"%s\"), append=%u, max_matches=%u, type_list) => %u",
            name.GetCString(), static_cast<const void *>(parent_decl_ctx),
            parent_decl_ctx->GetName().AsCString("<NULL>"), append, max_matches,
            num_matches);
      } else {
        GetObjectFile()->GetModule()->LogMessage(
            log, "SymbolFileDWARF::FindTypes (sc, name=\"%s\", parent_decl_ctx "
                 "= NULL, append=%u, max_matches=%u, type_list) => %u",
            name.GetCString(), append, max_matches, num_matches);
      }
    }
    return num_matches;
  } else {
    UpdateExternalModuleListIfNeeded();

    for (const auto &pair : m_external_type_modules) {
      ModuleSP external_module_sp = pair.second;
      if (external_module_sp) {
        SymbolVendor *sym_vendor = external_module_sp->GetSymbolVendor();
        if (sym_vendor) {
          const uint32_t num_external_matches =
              sym_vendor->FindTypes(name, parent_decl_ctx, append, max_matches,
                                    searched_symbol_files, types);
          if (num_external_matches)
            return num_external_matches;
        }
      }
    }
  }

  return 0;
}

size_t SymbolFileDWARF::FindTypes(const std::vector<CompilerContext> &context,
                                  bool append, TypeMap &types) {
  if (!append)
    types.Clear();

  if (context.empty())
    return 0;

  ConstString name = context.back().name;

  if (!name)
    return 0;

  DIEArray die_offsets;
  m_index->GetTypes(name, die_offsets);
  const size_t num_die_matches = die_offsets.size();

  if (num_die_matches) {
    size_t num_matches = 0;
    for (size_t i = 0; i < num_die_matches; ++i) {
      const DIERef &die_ref = die_offsets[i];
      DWARFDIE die = GetDIE(die_ref);

      if (die) {
        std::vector<CompilerContext> die_context;
        die.GetDeclContext(die_context);
        if (die_context != context)
          continue;

        Type *matching_type = ResolveType(die, true, true);
        if (matching_type) {
          // We found a type pointer, now find the shared pointer form our type
          // list
          types.InsertUnique(matching_type->shared_from_this());
          ++num_matches;
        }
      } else {
        m_index->ReportInvalidDIEOffset(die_ref.die_offset,
                                        name.GetStringRef());
      }
    }
    return num_matches;
  }
  return 0;
}

CompilerDeclContext
SymbolFileDWARF::FindNamespace(const ConstString &name,
                               const CompilerDeclContext *parent_decl_ctx) {
  Log *log(LogChannelDWARF::GetLogIfAll(DWARF_LOG_LOOKUPS));

  if (log) {
    GetObjectFile()->GetModule()->LogMessage(
        log, "SymbolFileDWARF::FindNamespace (sc, name=\"%s\")",
        name.GetCString());
  }

  CompilerDeclContext namespace_decl_ctx;

  if (!DeclContextMatchesThisSymbolFile(parent_decl_ctx))
    return namespace_decl_ctx;

  DWARFDebugInfo *info = DebugInfo();
  if (info) {
    DIEArray die_offsets;
    m_index->GetNamespaces(name, die_offsets);
    const size_t num_matches = die_offsets.size();
    if (num_matches) {
      for (size_t i = 0; i < num_matches; ++i) {
        const DIERef &die_ref = die_offsets[i];
        DWARFDIE die = GetDIE(die_ref);

        if (die) {
          if (!DIEInDeclContext(parent_decl_ctx, die))
            continue; // The containing decl contexts don't match

          DWARFASTParser *dwarf_ast = die.GetDWARFParser();
          if (dwarf_ast) {
            namespace_decl_ctx = dwarf_ast->GetDeclContextForUIDFromDWARF(die);
            if (namespace_decl_ctx)
              break;
          }
        } else {
          m_index->ReportInvalidDIEOffset(die_ref.die_offset,
                                          name.GetStringRef());
        }
      }
    }
  }
  if (log && namespace_decl_ctx) {
    GetObjectFile()->GetModule()->LogMessage(
        log, "SymbolFileDWARF::FindNamespace (sc, name=\"%s\") => "
             "CompilerDeclContext(%p/%p) \"%s\"",
        name.GetCString(),
        static_cast<const void *>(namespace_decl_ctx.GetTypeSystem()),
        static_cast<const void *>(namespace_decl_ctx.GetOpaqueDeclContext()),
        namespace_decl_ctx.GetName().AsCString("<NULL>"));
  }

  return namespace_decl_ctx;
}

TypeSP SymbolFileDWARF::GetTypeForDIE(const DWARFDIE &die,
                                      bool resolve_function_context) {
  TypeSP type_sp;
  if (die) {
    Type *type_ptr = GetDIEToType().lookup(die.GetDIE());
    if (type_ptr == NULL) {
      CompileUnit *lldb_cu = GetCompUnitForDWARFCompUnit(die.GetCU());
      assert(lldb_cu);
      SymbolContext sc(lldb_cu);
      const DWARFDebugInfoEntry *parent_die = die.GetParent().GetDIE();
      while (parent_die != nullptr) {
        if (parent_die->Tag() == DW_TAG_subprogram)
          break;
        parent_die = parent_die->GetParent();
      }
      SymbolContext sc_backup = sc;
      if (resolve_function_context && parent_die != nullptr &&
          !GetFunction(DWARFDIE(die.GetCU(), parent_die), sc))
        sc = sc_backup;

      type_sp = ParseType(sc, die, NULL);
    } else if (type_ptr != DIE_IS_BEING_PARSED) {
      // Grab the existing type from the master types lists
      type_sp = type_ptr->shared_from_this();
    }
  }
  return type_sp;
}

DWARFDIE
SymbolFileDWARF::GetDeclContextDIEContainingDIE(const DWARFDIE &orig_die) {
  if (orig_die) {
    DWARFDIE die = orig_die;

    while (die) {
      // If this is the original DIE that we are searching for a declaration
      // for, then don't look in the cache as we don't want our own decl
      // context to be our decl context...
      if (orig_die != die) {
        switch (die.Tag()) {
        case DW_TAG_compile_unit:
        case DW_TAG_partial_unit:
        case DW_TAG_namespace:
        case DW_TAG_structure_type:
        case DW_TAG_union_type:
        case DW_TAG_class_type:
        case DW_TAG_lexical_block:
        case DW_TAG_subprogram:
          return die;
        case DW_TAG_inlined_subroutine: {
          DWARFDIE abs_die = die.GetReferencedDIE(DW_AT_abstract_origin);
          if (abs_die) {
            return abs_die;
          }
          break;
        }
        default:
          break;
        }
      }

      DWARFDIE spec_die = die.GetReferencedDIE(DW_AT_specification);
      if (spec_die) {
        DWARFDIE decl_ctx_die = GetDeclContextDIEContainingDIE(spec_die);
        if (decl_ctx_die)
          return decl_ctx_die;
      }

      DWARFDIE abs_die = die.GetReferencedDIE(DW_AT_abstract_origin);
      if (abs_die) {
        DWARFDIE decl_ctx_die = GetDeclContextDIEContainingDIE(abs_die);
        if (decl_ctx_die)
          return decl_ctx_die;
      }

      die = die.GetParent();
    }
  }
  return DWARFDIE();
}

Symbol *
SymbolFileDWARF::GetObjCClassSymbol(const ConstString &objc_class_name) {
  Symbol *objc_class_symbol = NULL;
  if (m_obj_file) {
    Symtab *symtab = m_obj_file->GetSymtab();
    if (symtab) {
      objc_class_symbol = symtab->FindFirstSymbolWithNameAndType(
          objc_class_name, eSymbolTypeObjCClass, Symtab::eDebugNo,
          Symtab::eVisibilityAny);
    }
  }
  return objc_class_symbol;
}

// Some compilers don't emit the DW_AT_APPLE_objc_complete_type attribute. If
// they don't then we can end up looking through all class types for a complete
// type and never find the full definition. We need to know if this attribute
// is supported, so we determine this here and cache th result. We also need to
// worry about the debug map
// DWARF file
// if we are doing darwin DWARF in .o file debugging.
bool SymbolFileDWARF::Supports_DW_AT_APPLE_objc_complete_type(
    DWARFUnit *cu) {
  if (m_supports_DW_AT_APPLE_objc_complete_type == eLazyBoolCalculate) {
    m_supports_DW_AT_APPLE_objc_complete_type = eLazyBoolNo;
    if (cu && cu->Supports_DW_AT_APPLE_objc_complete_type())
      m_supports_DW_AT_APPLE_objc_complete_type = eLazyBoolYes;
    else {
      DWARFDebugInfo *debug_info = DebugInfo();
      const uint32_t num_compile_units = GetNumCompileUnits();
      for (uint32_t cu_idx = 0; cu_idx < num_compile_units; ++cu_idx) {
        DWARFUnit *dwarf_cu = debug_info->GetCompileUnitAtIndex(cu_idx);
        if (dwarf_cu != cu &&
            dwarf_cu->Supports_DW_AT_APPLE_objc_complete_type()) {
          m_supports_DW_AT_APPLE_objc_complete_type = eLazyBoolYes;
          break;
        }
      }
    }
    if (m_supports_DW_AT_APPLE_objc_complete_type == eLazyBoolNo &&
        GetDebugMapSymfile())
      return m_debug_map_symfile->Supports_DW_AT_APPLE_objc_complete_type(this);
  }
  return m_supports_DW_AT_APPLE_objc_complete_type == eLazyBoolYes;
}

// This function can be used when a DIE is found that is a forward declaration
// DIE and we want to try and find a type that has the complete definition.
TypeSP SymbolFileDWARF::FindCompleteObjCDefinitionTypeForDIE(
    const DWARFDIE &die, const ConstString &type_name,
    bool must_be_implementation) {

  TypeSP type_sp;

  if (!type_name || (must_be_implementation && !GetObjCClassSymbol(type_name)))
    return type_sp;

  DIEArray die_offsets;
  m_index->GetCompleteObjCClass(type_name, must_be_implementation, die_offsets);

  const size_t num_matches = die_offsets.size();

  if (num_matches) {
    for (size_t i = 0; i < num_matches; ++i) {
      const DIERef &die_ref = die_offsets[i];
      DWARFDIE type_die = GetDIE(die_ref);

      if (type_die) {
        bool try_resolving_type = false;

        // Don't try and resolve the DIE we are looking for with the DIE
        // itself!
        if (type_die != die) {
          switch (type_die.Tag()) {
          case DW_TAG_class_type:
          case DW_TAG_structure_type:
            try_resolving_type = true;
            break;
          default:
            break;
          }
        }

        if (try_resolving_type) {
          if (must_be_implementation &&
              type_die.Supports_DW_AT_APPLE_objc_complete_type())
            try_resolving_type = type_die.GetAttributeValueAsUnsigned(
                DW_AT_APPLE_objc_complete_type, 0);

          if (try_resolving_type) {
            Type *resolved_type = ResolveType(type_die, false, true);
            if (resolved_type && resolved_type != DIE_IS_BEING_PARSED) {
              DEBUG_PRINTF("resolved 0x%8.8" PRIx64 " from %s to 0x%8.8" PRIx64
                           " (cu 0x%8.8" PRIx64 ")\n",
                           die.GetID(),
                           m_obj_file->GetFileSpec().GetFilename().AsCString(
                               "<Unknown>"),
                           type_die.GetID(), type_cu->GetID());

              if (die)
                GetDIEToType()[die.GetDIE()] = resolved_type;
              type_sp = resolved_type->shared_from_this();
              break;
            }
          }
        }
      } else {
        m_index->ReportInvalidDIEOffset(die_ref.die_offset,
                                        type_name.GetStringRef());
      }
    }
  }
  return type_sp;
}

//----------------------------------------------------------------------
// This function helps to ensure that the declaration contexts match for two
// different DIEs. Often times debug information will refer to a forward
// declaration of a type (the equivalent of "struct my_struct;". There will
// often be a declaration of that type elsewhere that has the full definition.
// When we go looking for the full type "my_struct", we will find one or more
// matches in the accelerator tables and we will then need to make sure the
// type was in the same declaration context as the original DIE. This function
// can efficiently compare two DIEs and will return true when the declaration
// context matches, and false when they don't.
//----------------------------------------------------------------------
bool SymbolFileDWARF::DIEDeclContextsMatch(const DWARFDIE &die1,
                                           const DWARFDIE &die2) {
  if (die1 == die2)
    return true;

  DWARFDIECollection decl_ctx_1;
  DWARFDIECollection decl_ctx_2;
  // The declaration DIE stack is a stack of the declaration context DIEs all
  // the way back to the compile unit. If a type "T" is declared inside a class
  // "B", and class "B" is declared inside a class "A" and class "A" is in a
  // namespace "lldb", and the namespace is in a compile unit, there will be a
  // stack of DIEs:
  //
  //   [0] DW_TAG_class_type for "B"
  //   [1] DW_TAG_class_type for "A"
  //   [2] DW_TAG_namespace  for "lldb"
  //   [3] DW_TAG_compile_unit or DW_TAG_partial_unit for the source file.
  //
  // We grab both contexts and make sure that everything matches all the way
  // back to the compiler unit.

  // First lets grab the decl contexts for both DIEs
  die1.GetDeclContextDIEs(decl_ctx_1);
  die2.GetDeclContextDIEs(decl_ctx_2);
  // Make sure the context arrays have the same size, otherwise we are done
  const size_t count1 = decl_ctx_1.Size();
  const size_t count2 = decl_ctx_2.Size();
  if (count1 != count2)
    return false;

  // Make sure the DW_TAG values match all the way back up the compile unit. If
  // they don't, then we are done.
  DWARFDIE decl_ctx_die1;
  DWARFDIE decl_ctx_die2;
  size_t i;
  for (i = 0; i < count1; i++) {
    decl_ctx_die1 = decl_ctx_1.GetDIEAtIndex(i);
    decl_ctx_die2 = decl_ctx_2.GetDIEAtIndex(i);
    if (decl_ctx_die1.Tag() != decl_ctx_die2.Tag())
      return false;
  }
#if defined LLDB_CONFIGURATION_DEBUG

  // Make sure the top item in the decl context die array is always
  // DW_TAG_compile_unit or DW_TAG_partial_unit. If it isn't then
  // something went wrong in the DWARFDIE::GetDeclContextDIEs()
  // function.
  dw_tag_t cu_tag = decl_ctx_1.GetDIEAtIndex(count1 - 1).Tag();
  UNUSED_IF_ASSERT_DISABLED(cu_tag);
  assert(cu_tag == DW_TAG_compile_unit || cu_tag == DW_TAG_partial_unit);

#endif
  // Always skip the compile unit when comparing by only iterating up to "count
  // - 1". Here we compare the names as we go.
  for (i = 0; i < count1 - 1; i++) {
    decl_ctx_die1 = decl_ctx_1.GetDIEAtIndex(i);
    decl_ctx_die2 = decl_ctx_2.GetDIEAtIndex(i);
    const char *name1 = decl_ctx_die1.GetName();
    const char *name2 = decl_ctx_die2.GetName();
    // If the string was from a DW_FORM_strp, then the pointer will often be
    // the same!
    if (name1 == name2)
      continue;

    // Name pointers are not equal, so only compare the strings if both are not
    // NULL.
    if (name1 && name2) {
      // If the strings don't compare, we are done...
      if (strcmp(name1, name2) != 0)
        return false;
    } else {
      // One name was NULL while the other wasn't
      return false;
    }
  }
  // We made it through all of the checks and the declaration contexts are
  // equal.
  return true;
}

TypeSP SymbolFileDWARF::FindDefinitionTypeForDWARFDeclContext(
    const DWARFDeclContext &dwarf_decl_ctx) {
  TypeSP type_sp;

  const uint32_t dwarf_decl_ctx_count = dwarf_decl_ctx.GetSize();
  if (dwarf_decl_ctx_count > 0) {
    const ConstString type_name(dwarf_decl_ctx[0].name);
    const dw_tag_t tag = dwarf_decl_ctx[0].tag;

    if (type_name) {
      Log *log(LogChannelDWARF::GetLogIfAny(DWARF_LOG_TYPE_COMPLETION |
                                            DWARF_LOG_LOOKUPS));
      if (log) {
        GetObjectFile()->GetModule()->LogMessage(
            log, "SymbolFileDWARF::FindDefinitionTypeForDWARFDeclContext(tag=%"
                 "s, qualified-name='%s')",
            DW_TAG_value_to_name(dwarf_decl_ctx[0].tag),
            dwarf_decl_ctx.GetQualifiedName());
      }

      DIEArray die_offsets;
      m_index->GetTypes(dwarf_decl_ctx, die_offsets);
      const size_t num_matches = die_offsets.size();

      // Get the type system that we are looking to find a type for. We will
      // use this to ensure any matches we find are in a language that this
      // type system supports
      const LanguageType language = dwarf_decl_ctx.GetLanguage();
      TypeSystem *type_system = (language == eLanguageTypeUnknown)
                                    ? nullptr
                                    : GetTypeSystemForLanguage(language);

      if (num_matches) {
        for (size_t i = 0; i < num_matches; ++i) {
          const DIERef &die_ref = die_offsets[i];
          DWARFDIE type_die = GetDIE(die_ref);

          if (type_die) {
            // Make sure type_die's langauge matches the type system we are
            // looking for. We don't want to find a "Foo" type from Java if we
            // are looking for a "Foo" type for C, C++, ObjC, or ObjC++.
            if (type_system &&
                !type_system->SupportsLanguage(type_die.GetLanguage()))
              continue;
            bool try_resolving_type = false;

            // Don't try and resolve the DIE we are looking for with the DIE
            // itself!
            const dw_tag_t type_tag = type_die.Tag();
            // Make sure the tags match
            if (type_tag == tag) {
              // The tags match, lets try resolving this type
              try_resolving_type = true;
            } else {
              // The tags don't match, but we need to watch our for a forward
              // declaration for a struct and ("struct foo") ends up being a
              // class ("class foo { ... };") or vice versa.
              switch (type_tag) {
              case DW_TAG_class_type:
                // We had a "class foo", see if we ended up with a "struct foo
                // { ... };"
                try_resolving_type = (tag == DW_TAG_structure_type);
                break;
              case DW_TAG_structure_type:
                // We had a "struct foo", see if we ended up with a "class foo
                // { ... };"
                try_resolving_type = (tag == DW_TAG_class_type);
                break;
              default:
                // Tags don't match, don't event try to resolve using this type
                // whose name matches....
                break;
              }
            }

            if (try_resolving_type) {
              DWARFDeclContext type_dwarf_decl_ctx;
              type_die.GetDWARFDeclContext(type_dwarf_decl_ctx);

              if (log) {
                GetObjectFile()->GetModule()->LogMessage(
                    log, "SymbolFileDWARF::"
                         "FindDefinitionTypeForDWARFDeclContext(tag=%s, "
                         "qualified-name='%s') trying die=0x%8.8x (%s)",
                    DW_TAG_value_to_name(dwarf_decl_ctx[0].tag),
                    dwarf_decl_ctx.GetQualifiedName(), type_die.GetOffset(),
                    type_dwarf_decl_ctx.GetQualifiedName());
              }

              // Make sure the decl contexts match all the way up
              if (dwarf_decl_ctx == type_dwarf_decl_ctx) {
                Type *resolved_type = ResolveType(type_die, false);
                if (resolved_type && resolved_type != DIE_IS_BEING_PARSED) {
                  type_sp = resolved_type->shared_from_this();
                  break;
                }
              }
            } else {
              if (log) {
                std::string qualified_name;
                type_die.GetQualifiedName(qualified_name);
                GetObjectFile()->GetModule()->LogMessage(
                    log, "SymbolFileDWARF::"
                         "FindDefinitionTypeForDWARFDeclContext(tag=%s, "
                         "qualified-name='%s') ignoring die=0x%8.8x (%s)",
                    DW_TAG_value_to_name(dwarf_decl_ctx[0].tag),
                    dwarf_decl_ctx.GetQualifiedName(), type_die.GetOffset(),
                    qualified_name.c_str());
              }
            }
          } else {
            m_index->ReportInvalidDIEOffset(die_ref.die_offset,
                                            type_name.GetStringRef());
          }
        }
      }
    }
  }
  return type_sp;
}

TypeSP SymbolFileDWARF::ParseType(const SymbolContext &sc, const DWARFDIE &die,
                                  bool *type_is_new_ptr) {
  TypeSP type_sp;

  if (die) {
    TypeSystem *type_system =
        GetTypeSystemForLanguage(die.GetCU()->GetLanguageType());

    if (type_system) {
      DWARFASTParser *dwarf_ast = type_system->GetDWARFParser();
      if (dwarf_ast) {
        Log *log = LogChannelDWARF::GetLogIfAll(DWARF_LOG_DEBUG_INFO);
        type_sp = dwarf_ast->ParseTypeFromDWARF(sc, die, log, type_is_new_ptr);
        if (type_sp) {
          TypeList *type_list = GetTypeList();
          if (type_list)
            type_list->Insert(type_sp);

          if (die.Tag() == DW_TAG_subprogram) {
            DIERef die_ref = die.GetDIERef();
            std::string scope_qualified_name(GetDeclContextForUID(die.GetID())
                                                 .GetScopeQualifiedName()
                                                 .AsCString(""));
            if (scope_qualified_name.size()) {
              NameToOffsetMap::iterator iter =
                  m_function_scope_qualified_name_map.find(
                      scope_qualified_name);
              if (iter != m_function_scope_qualified_name_map.end())
                (*iter).second->insert(die_ref);
              else {
                DIERefSetSP new_set(new std::set<DIERef>);
                new_set->insert(die_ref);
                m_function_scope_qualified_name_map.emplace(
                    std::make_pair(scope_qualified_name, new_set));
              }
            }
          }
        }
      }
    }
  }

  return type_sp;
}

size_t SymbolFileDWARF::ParseTypes(const SymbolContext &sc,
                                   const DWARFDIE &orig_die,
                                   bool parse_siblings, bool parse_children) {
  size_t types_added = 0;
  DWARFDIE die = orig_die;
  while (die) {
    bool type_is_new = false;
    if (ParseType(sc, die, &type_is_new).get()) {
      if (type_is_new)
        ++types_added;
    }

    if (parse_children && die.HasChildren()) {
      if (die.Tag() == DW_TAG_subprogram) {
        SymbolContext child_sc(sc);
        child_sc.function = sc.comp_unit->FindFunctionByUID(die.GetID()).get();
        types_added += ParseTypes(child_sc, die.GetFirstChild(), true, true);
      } else
        types_added += ParseTypes(sc, die.GetFirstChild(), true, true);
    }

    if (parse_siblings)
      die = die.GetSibling();
    else
      die.Clear();
  }
  return types_added;
}

size_t SymbolFileDWARF::ParseBlocksRecursive(Function &func) {
  ASSERT_MODULE_LOCK(this);
  CompileUnit *comp_unit = func.GetCompileUnit();
  lldbassert(comp_unit);

  DWARFUnit *dwarf_cu = GetDWARFCompileUnit(comp_unit);
  if (!dwarf_cu)
    return 0;

  size_t functions_added = 0;
  const dw_offset_t function_die_offset = func.GetID();
  DWARFDIE function_die = dwarf_cu->GetDIE(function_die_offset);
  if (function_die) {
    ParseBlocksRecursive(*comp_unit, &func.GetBlock(false), function_die,
                         LLDB_INVALID_ADDRESS, 0);
  }

  return functions_added;
}

size_t SymbolFileDWARF::ParseTypes(CompileUnit &comp_unit) {
  ASSERT_MODULE_LOCK(this);
  size_t types_added = 0;
  DWARFUnit *dwarf_cu = GetDWARFCompileUnit(&comp_unit);
  if (dwarf_cu) {
    DWARFDIE dwarf_cu_die = dwarf_cu->DIE();
    if (dwarf_cu_die && dwarf_cu_die.HasChildren()) {
      SymbolContext sc;
      sc.comp_unit = &comp_unit;
      types_added = ParseTypes(sc, dwarf_cu_die.GetFirstChild(), true, true);
    }
  }

  return types_added;
}

size_t SymbolFileDWARF::ParseVariablesForContext(const SymbolContext &sc) {
  ASSERT_MODULE_LOCK(this);
  if (sc.comp_unit != NULL) {
    DWARFDebugInfo *info = DebugInfo();
    if (info == NULL)
      return 0;

    if (sc.function) {
      DWARFDIE function_die = info->GetDIE(DIERef(sc.function->GetID(), this));

      const dw_addr_t func_lo_pc = function_die.GetAttributeValueAsAddress(
          DW_AT_low_pc, LLDB_INVALID_ADDRESS);
      if (func_lo_pc != LLDB_INVALID_ADDRESS) {
        const size_t num_variables = ParseVariables(
            sc, function_die.GetFirstChild(), func_lo_pc, true, true);

        // Let all blocks know they have parse all their variables
        sc.function->GetBlock(false).SetDidParseVariables(true, true);
        return num_variables;
      }
    } else if (sc.comp_unit) {
      DWARFUnit *dwarf_cu = info->GetCompileUnit(sc.comp_unit->GetID());

      if (dwarf_cu == NULL)
        return 0;

      uint32_t vars_added = 0;
      VariableListSP variables(sc.comp_unit->GetVariableList(false));

      if (variables.get() == NULL) {
        variables.reset(new VariableList());
        sc.comp_unit->SetVariableList(variables);

        DIEArray die_offsets;
        m_index->GetGlobalVariables(*dwarf_cu, die_offsets);
        const size_t num_matches = die_offsets.size();
        if (num_matches) {
          for (size_t i = 0; i < num_matches; ++i) {
            const DIERef &die_ref = die_offsets[i];
            DWARFDIE die = GetDIE(die_ref);
            if (die) {
              VariableSP var_sp(
                  ParseVariableDIE(sc, die, LLDB_INVALID_ADDRESS));
              if (var_sp) {
                variables->AddVariableIfUnique(var_sp);
                ++vars_added;
              }
            } else
              m_index->ReportInvalidDIEOffset(die_ref.die_offset, "");
          }
        }
      }
      return vars_added;
    }
  }
  return 0;
}

VariableSP SymbolFileDWARF::ParseVariableDIE(const SymbolContext &sc,
                                             const DWARFDIE &die,
                                             const lldb::addr_t func_low_pc) {
  if (die.GetDWARF() != this)
    return die.GetDWARF()->ParseVariableDIE(sc, die, func_low_pc);

  VariableSP var_sp;
  if (!die)
    return var_sp;

  var_sp = GetDIEToVariable()[die.GetDIE()];
  if (var_sp)
    return var_sp; // Already been parsed!

  const dw_tag_t tag = die.Tag();
  ModuleSP module = GetObjectFile()->GetModule();

  if ((tag == DW_TAG_variable) || (tag == DW_TAG_constant) ||
      (tag == DW_TAG_formal_parameter && sc.function)) {
    DWARFAttributes attributes;
    const size_t num_attributes = die.GetAttributes(attributes);
    DWARFDIE spec_die;
    if (num_attributes > 0) {
      const char *name = NULL;
      const char *mangled = NULL;
      Declaration decl;
      uint32_t i;
      DWARFFormValue type_die_form;
      DWARFExpression location(die.GetCU());
      bool is_external = false;
      bool is_artificial = false;
      bool location_is_const_value_data = false;
      bool has_explicit_location = false;
      DWARFFormValue const_value;
      Variable::RangeList scope_ranges;
      // AccessType accessibility = eAccessNone;

      for (i = 0; i < num_attributes; ++i) {
        dw_attr_t attr = attributes.AttributeAtIndex(i);
        DWARFFormValue form_value;

        if (attributes.ExtractFormValueAtIndex(i, form_value)) {
          switch (attr) {
          case DW_AT_decl_file:
            decl.SetFile(sc.comp_unit->GetSupportFiles().GetFileSpecAtIndex(
                form_value.Unsigned()));
            break;
          case DW_AT_decl_line:
            decl.SetLine(form_value.Unsigned());
            break;
          case DW_AT_decl_column:
            decl.SetColumn(form_value.Unsigned());
            break;
          case DW_AT_name:
            name = form_value.AsCString();
            break;
          case DW_AT_linkage_name:
          case DW_AT_MIPS_linkage_name:
            mangled = form_value.AsCString();
            break;
          case DW_AT_type:
            type_die_form = form_value;
            break;
          case DW_AT_external:
            is_external = form_value.Boolean();
            break;
          case DW_AT_const_value:
            // If we have already found a DW_AT_location attribute, ignore this
            // attribute.
            if (!has_explicit_location) {
              location_is_const_value_data = true;
              // The constant value will be either a block, a data value or a
              // string.
              auto debug_info_data = die.GetData();
              if (DWARFFormValue::IsBlockForm(form_value.Form())) {
                // Retrieve the value as a block expression.
                uint32_t block_offset =
                    form_value.BlockData() - debug_info_data.GetDataStart();
                uint32_t block_length = form_value.Unsigned();
                location.CopyOpcodeData(module, debug_info_data, block_offset,
                                        block_length);
              } else if (DWARFFormValue::IsDataForm(form_value.Form())) {
                // Retrieve the value as a data expression.
                DWARFFormValue::FixedFormSizes fixed_form_sizes =
                    DWARFFormValue::GetFixedFormSizesForAddressSize(
                        attributes.CompileUnitAtIndex(i)->GetAddressByteSize(),
                        attributes.CompileUnitAtIndex(i)->IsDWARF64());
                uint32_t data_offset = attributes.DIEOffsetAtIndex(i);
                uint32_t data_length =
                    fixed_form_sizes.GetSize(form_value.Form());
                if (data_length == 0) {
                  const uint8_t *data_pointer = form_value.BlockData();
                  if (data_pointer) {
                    form_value.Unsigned();
                  } else if (DWARFFormValue::IsDataForm(form_value.Form())) {
                    // we need to get the byte size of the type later after we
                    // create the variable
                    const_value = form_value;
                  }
                } else
                  location.CopyOpcodeData(module, debug_info_data, data_offset,
                                          data_length);
              } else {
                // Retrieve the value as a string expression.
                if (form_value.Form() == DW_FORM_strp) {
                  DWARFFormValue::FixedFormSizes fixed_form_sizes =
                      DWARFFormValue::GetFixedFormSizesForAddressSize(
                          attributes.CompileUnitAtIndex(i)
                              ->GetAddressByteSize(),
                          attributes.CompileUnitAtIndex(i)->IsDWARF64());
                  uint32_t data_offset = attributes.DIEOffsetAtIndex(i);
                  uint32_t data_length =
                      fixed_form_sizes.GetSize(form_value.Form());
                  location.CopyOpcodeData(module, debug_info_data, data_offset,
                                          data_length);
                } else {
                  const char *str = form_value.AsCString();
                  uint32_t string_offset =
                      str - (const char *)debug_info_data.GetDataStart();
                  uint32_t string_length = strlen(str) + 1;
                  location.CopyOpcodeData(module, debug_info_data,
                                          string_offset, string_length);
                }
              }
            }
            break;
          case DW_AT_location: {
            location_is_const_value_data = false;
            has_explicit_location = true;
            if (DWARFFormValue::IsBlockForm(form_value.Form())) {
              auto data = die.GetData();

              uint32_t block_offset =
                  form_value.BlockData() - data.GetDataStart();
              uint32_t block_length = form_value.Unsigned();
              location.CopyOpcodeData(module, data, block_offset, block_length);
            } else {
              const DWARFDataExtractor &debug_loc_data = DebugLocData();
              const dw_offset_t debug_loc_offset = form_value.Unsigned();

              size_t loc_list_length = DWARFExpression::LocationListSize(
                  die.GetCU(), debug_loc_data, debug_loc_offset);
              if (loc_list_length > 0) {
                location.CopyOpcodeData(module, debug_loc_data,
                                        debug_loc_offset, loc_list_length);
                assert(func_low_pc != LLDB_INVALID_ADDRESS);
                location.SetLocationListSlide(
                    func_low_pc -
                    attributes.CompileUnitAtIndex(i)->GetBaseAddress());
              }
            }
          } break;
          case DW_AT_specification:
            spec_die = GetDIE(DIERef(form_value));
            break;
          case DW_AT_start_scope: {
            if (form_value.Form() == DW_FORM_sec_offset) {
              DWARFRangeList dwarf_scope_ranges;
              const DWARFDebugRangesBase *debug_ranges = DebugRanges();
              debug_ranges->FindRanges(die.GetCU(),
                                       form_value.Unsigned(),
                                       dwarf_scope_ranges);
            } else {
              // TODO: Handle the case when DW_AT_start_scope have form
              // constant. The
              // dwarf spec is a bit ambiguous about what is the expected
              // behavior in case the enclosing block have a non coninious
              // address range and the DW_AT_start_scope entry have a form
              // constant.
              GetObjectFile()->GetModule()->ReportWarning(
                  "0x%8.8" PRIx64
                  ": DW_AT_start_scope has unsupported form type (0x%x)\n",
                  die.GetID(), form_value.Form());
            }

            scope_ranges.Sort();
            scope_ranges.CombineConsecutiveRanges();
          } break;
          case DW_AT_artificial:
            is_artificial = form_value.Boolean();
            break;
          case DW_AT_accessibility:
            break; // accessibility =
                   // DW_ACCESS_to_AccessType(form_value.Unsigned()); break;
          case DW_AT_declaration:
          case DW_AT_description:
          case DW_AT_endianity:
          case DW_AT_segment:
          case DW_AT_visibility:
          default:
          case DW_AT_abstract_origin:
          case DW_AT_sibling:
            break;
          }
        }
      }

      const DWARFDIE parent_context_die = GetDeclContextDIEContainingDIE(die);
      const dw_tag_t parent_tag = die.GetParent().Tag();
      bool is_static_member =
          (parent_tag == DW_TAG_compile_unit ||
           parent_tag == DW_TAG_partial_unit) &&
          (parent_context_die.Tag() == DW_TAG_class_type ||
           parent_context_die.Tag() == DW_TAG_structure_type);

      ValueType scope = eValueTypeInvalid;

      const DWARFDIE sc_parent_die = GetParentSymbolContextDIE(die);
      SymbolContextScope *symbol_context_scope = NULL;

      bool has_explicit_mangled = mangled != nullptr;
      if (!mangled) {
        // LLDB relies on the mangled name (DW_TAG_linkage_name or
        // DW_AT_MIPS_linkage_name) to generate fully qualified names
        // of global variables with commands like "frame var j". For
        // example, if j were an int variable holding a value 4 and
        // declared in a namespace B which in turn is contained in a
        // namespace A, the command "frame var j" returns
        //   "(int) A::B::j = 4".
        // If the compiler does not emit a linkage name, we should be
        // able to generate a fully qualified name from the
        // declaration context.
        if ((parent_tag == DW_TAG_compile_unit ||
             parent_tag == DW_TAG_partial_unit) &&
            Language::LanguageIsCPlusPlus(die.GetLanguage())) {
          DWARFDeclContext decl_ctx;

          die.GetDWARFDeclContext(decl_ctx);
          mangled = decl_ctx.GetQualifiedNameAsConstString().GetCString();
        }
      }

      if (tag == DW_TAG_formal_parameter)
        scope = eValueTypeVariableArgument;
      else {
        // DWARF doesn't specify if a DW_TAG_variable is a local, global
        // or static variable, so we have to do a little digging:
        // 1) DW_AT_linkage_name implies static lifetime (but may be missing)
        // 2) An empty DW_AT_location is an (optimized-out) static lifetime var.
        // 3) DW_AT_location containing a DW_OP_addr implies static lifetime.
        // Clang likes to combine small global variables into the same symbol
        // with locations like: DW_OP_addr(0x1000), DW_OP_constu(2), DW_OP_plus
        // so we need to look through the whole expression.
        bool is_static_lifetime =
            has_explicit_mangled ||
            (has_explicit_location && !location.IsValid());
        // Check if the location has a DW_OP_addr with any address value...
        lldb::addr_t location_DW_OP_addr = LLDB_INVALID_ADDRESS;
        if (!location_is_const_value_data) {
          bool op_error = false;
          location_DW_OP_addr = location.GetLocation_DW_OP_addr(0, op_error);
          if (op_error) {
            StreamString strm;
            location.DumpLocationForAddress(&strm, eDescriptionLevelFull, 0, 0,
                                            NULL);
            GetObjectFile()->GetModule()->ReportError(
                "0x%8.8x: %s has an invalid location: %s", die.GetOffset(),
                die.GetTagAsCString(), strm.GetData());
          }
          if (location_DW_OP_addr != LLDB_INVALID_ADDRESS)
            is_static_lifetime = true;
        }
        SymbolFileDWARFDebugMap *debug_map_symfile = GetDebugMapSymfile();
        if (debug_map_symfile)
          // Set the module of the expression to the linked module
          // instead of the oject file so the relocated address can be
          // found there.
          location.SetModule(debug_map_symfile->GetObjectFile()->GetModule());

        if (is_static_lifetime) {
          if (is_external)
            scope = eValueTypeVariableGlobal;
          else
            scope = eValueTypeVariableStatic;

          if (debug_map_symfile) {
            // When leaving the DWARF in the .o files on darwin, when we have a
            // global variable that wasn't initialized, the .o file might not
            // have allocated a virtual address for the global variable. In
            // this case it will have created a symbol for the global variable
            // that is undefined/data and external and the value will be the
            // byte size of the variable. When we do the address map in
            // SymbolFileDWARFDebugMap we rely on having an address, we need to
            // do some magic here so we can get the correct address for our
            // global variable. The address for all of these entries will be
            // zero, and there will be an undefined symbol in this object file,
            // and the executable will have a matching symbol with a good
            // address. So here we dig up the correct address and replace it in
            // the location for the variable, and set the variable's symbol
            // context scope to be that of the main executable so the file
            // address will resolve correctly.
            bool linked_oso_file_addr = false;
            if (is_external && location_DW_OP_addr == 0) {
              // we have a possible uninitialized extern global
              ConstString const_name(mangled ? mangled : name);
              ObjectFile *debug_map_objfile =
                  debug_map_symfile->GetObjectFile();
              if (debug_map_objfile) {
                Symtab *debug_map_symtab = debug_map_objfile->GetSymtab();
                if (debug_map_symtab) {
                  Symbol *exe_symbol =
                      debug_map_symtab->FindFirstSymbolWithNameAndType(
                          const_name, eSymbolTypeData, Symtab::eDebugYes,
                          Symtab::eVisibilityExtern);
                  if (exe_symbol) {
                    if (exe_symbol->ValueIsAddress()) {
                      const addr_t exe_file_addr =
                          exe_symbol->GetAddressRef().GetFileAddress();
                      if (exe_file_addr != LLDB_INVALID_ADDRESS) {
                        if (location.Update_DW_OP_addr(exe_file_addr)) {
                          linked_oso_file_addr = true;
                          symbol_context_scope = exe_symbol;
                        }
                      }
                    }
                  }
                }
              }
            }

            if (!linked_oso_file_addr) {
              // The DW_OP_addr is not zero, but it contains a .o file address
              // which needs to be linked up correctly.
              const lldb::addr_t exe_file_addr =
                  debug_map_symfile->LinkOSOFileAddress(this,
                                                        location_DW_OP_addr);
              if (exe_file_addr != LLDB_INVALID_ADDRESS) {
                // Update the file address for this variable
                location.Update_DW_OP_addr(exe_file_addr);
              } else {
                // Variable didn't make it into the final executable
                return var_sp;
              }
            }
          }
        } else {
          if (location_is_const_value_data)
            scope = eValueTypeVariableStatic;
          else {
            scope = eValueTypeVariableLocal;
            if (debug_map_symfile) {
              // We need to check for TLS addresses that we need to fixup
              if (location.ContainsThreadLocalStorage()) {
                location.LinkThreadLocalStorage(
                    debug_map_symfile->GetObjectFile()->GetModule(),
                    [this, debug_map_symfile](
                        lldb::addr_t unlinked_file_addr) -> lldb::addr_t {
                      return debug_map_symfile->LinkOSOFileAddress(
                          this, unlinked_file_addr);
                    });
                scope = eValueTypeVariableThreadLocal;
              }
            }
          }
        }
      }

      if (symbol_context_scope == NULL) {
        switch (parent_tag) {
        case DW_TAG_subprogram:
        case DW_TAG_inlined_subroutine:
        case DW_TAG_lexical_block:
          if (sc.function) {
            symbol_context_scope = sc.function->GetBlock(true).FindBlockByID(
                sc_parent_die.GetID());
            if (symbol_context_scope == NULL)
              symbol_context_scope = sc.function;
          }
          break;

        default:
          symbol_context_scope = sc.comp_unit;
          break;
        }
      }

      if (symbol_context_scope) {
        SymbolFileTypeSP type_sp(
            new SymbolFileType(*this, DIERef(type_die_form).GetUID(this)));

        if (const_value.Form() && type_sp && type_sp->GetType())
          location.CopyOpcodeData(const_value.Unsigned(),
                                  type_sp->GetType()->GetByteSize(),
                                  die.GetCU()->GetAddressByteSize());

        var_sp.reset(new Variable(die.GetID(), name, mangled, type_sp, scope,
                                  symbol_context_scope, scope_ranges, &decl,
                                  location, is_external, is_artificial,
                                  is_static_member));

        var_sp->SetLocationIsConstantValueData(location_is_const_value_data);
      } else {
        // Not ready to parse this variable yet. It might be a global or static
        // variable that is in a function scope and the function in the symbol
        // context wasn't filled in yet
        return var_sp;
      }
    }
    // Cache var_sp even if NULL (the variable was just a specification or was
    // missing vital information to be able to be displayed in the debugger
    // (missing location due to optimization, etc)) so we don't re-parse this
    // DIE over and over later...
    GetDIEToVariable()[die.GetDIE()] = var_sp;
    if (spec_die)
      GetDIEToVariable()[spec_die.GetDIE()] = var_sp;
  }
  return var_sp;
}

DWARFDIE
SymbolFileDWARF::FindBlockContainingSpecification(
    const DIERef &func_die_ref, dw_offset_t spec_block_die_offset) {
  // Give the concrete function die specified by "func_die_offset", find the
  // concrete block whose DW_AT_specification or DW_AT_abstract_origin points
  // to "spec_block_die_offset"
  return FindBlockContainingSpecification(DebugInfo()->GetDIE(func_die_ref),
                                          spec_block_die_offset);
}

DWARFDIE
SymbolFileDWARF::FindBlockContainingSpecification(
    const DWARFDIE &die, dw_offset_t spec_block_die_offset) {
  if (die) {
    switch (die.Tag()) {
    case DW_TAG_subprogram:
    case DW_TAG_inlined_subroutine:
    case DW_TAG_lexical_block: {
      if (die.GetAttributeValueAsReference(
              DW_AT_specification, DW_INVALID_OFFSET) == spec_block_die_offset)
        return die;

      if (die.GetAttributeValueAsReference(DW_AT_abstract_origin,
                                           DW_INVALID_OFFSET) ==
          spec_block_die_offset)
        return die;
    } break;
    }

    // Give the concrete function die specified by "func_die_offset", find the
    // concrete block whose DW_AT_specification or DW_AT_abstract_origin points
    // to "spec_block_die_offset"
    for (DWARFDIE child_die = die.GetFirstChild(); child_die;
         child_die = child_die.GetSibling()) {
      DWARFDIE result_die =
          FindBlockContainingSpecification(child_die, spec_block_die_offset);
      if (result_die)
        return result_die;
    }
  }

  return DWARFDIE();
}

size_t SymbolFileDWARF::ParseVariables(const SymbolContext &sc,
                                       const DWARFDIE &orig_die,
                                       const lldb::addr_t func_low_pc,
                                       bool parse_siblings, bool parse_children,
                                       VariableList *cc_variable_list) {
  if (!orig_die)
    return 0;

  VariableListSP variable_list_sp;

  size_t vars_added = 0;
  DWARFDIE die = orig_die;
  while (die) {
    dw_tag_t tag = die.Tag();

    // Check to see if we have already parsed this variable or constant?
    VariableSP var_sp = GetDIEToVariable()[die.GetDIE()];
    if (var_sp) {
      if (cc_variable_list)
        cc_variable_list->AddVariableIfUnique(var_sp);
    } else {
      // We haven't already parsed it, lets do that now.
      if ((tag == DW_TAG_variable) || (tag == DW_TAG_constant) ||
          (tag == DW_TAG_formal_parameter && sc.function)) {
        if (variable_list_sp.get() == NULL) {
          DWARFDIE sc_parent_die = GetParentSymbolContextDIE(orig_die);
          dw_tag_t parent_tag = sc_parent_die.Tag();
          switch (parent_tag) {
          case DW_TAG_compile_unit:
          case DW_TAG_partial_unit:
            if (sc.comp_unit != NULL) {
              variable_list_sp = sc.comp_unit->GetVariableList(false);
              if (variable_list_sp.get() == NULL) {
                variable_list_sp.reset(new VariableList());
              }
            } else {
              GetObjectFile()->GetModule()->ReportError(
                  "parent 0x%8.8" PRIx64 " %s with no valid compile unit in "
                                         "symbol context for 0x%8.8" PRIx64
                  " %s.\n",
                  sc_parent_die.GetID(), sc_parent_die.GetTagAsCString(),
                  orig_die.GetID(), orig_die.GetTagAsCString());
            }
            break;

          case DW_TAG_subprogram:
          case DW_TAG_inlined_subroutine:
          case DW_TAG_lexical_block:
            if (sc.function != NULL) {
              // Check to see if we already have parsed the variables for the
              // given scope

              Block *block = sc.function->GetBlock(true).FindBlockByID(
                  sc_parent_die.GetID());
              if (block == NULL) {
                // This must be a specification or abstract origin with a
                // concrete block counterpart in the current function. We need
                // to find the concrete block so we can correctly add the
                // variable to it
                const DWARFDIE concrete_block_die =
                    FindBlockContainingSpecification(
                        DIERef(sc.function->GetID(), this),
                        sc_parent_die.GetOffset());
                if (concrete_block_die)
                  block = sc.function->GetBlock(true).FindBlockByID(
                      concrete_block_die.GetID());
              }

              if (block != NULL) {
                const bool can_create = false;
                variable_list_sp = block->GetBlockVariableList(can_create);
                if (variable_list_sp.get() == NULL) {
                  variable_list_sp.reset(new VariableList());
                  block->SetVariableList(variable_list_sp);
                }
              }
            }
            break;

          default:
            GetObjectFile()->GetModule()->ReportError(
                "didn't find appropriate parent DIE for variable list for "
                "0x%8.8" PRIx64 " %s.\n",
                orig_die.GetID(), orig_die.GetTagAsCString());
            break;
          }
        }

        if (variable_list_sp) {
          VariableSP var_sp(ParseVariableDIE(sc, die, func_low_pc));
          if (var_sp) {
            variable_list_sp->AddVariableIfUnique(var_sp);
            if (cc_variable_list)
              cc_variable_list->AddVariableIfUnique(var_sp);
            ++vars_added;
          }
        }
      }
    }

    bool skip_children = (sc.function == NULL && tag == DW_TAG_subprogram);

    if (!skip_children && parse_children && die.HasChildren()) {
      vars_added += ParseVariables(sc, die.GetFirstChild(), func_low_pc, true,
                                   true, cc_variable_list);
    }

    if (parse_siblings)
      die = die.GetSibling();
    else
      die.Clear();
  }
  return vars_added;
}

/// Collect call graph edges present in a function DIE.
static std::vector<lldb_private::CallEdge>
CollectCallEdges(DWARFDIE function_die) {
  // Check if the function has a supported call site-related attribute.
  // TODO: In the future it may be worthwhile to support call_all_source_calls.
  uint64_t has_call_edges =
      function_die.GetAttributeValueAsUnsigned(DW_AT_call_all_calls, 0);
  if (!has_call_edges)
    return {};

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_STEP));
  LLDB_LOG(log, "CollectCallEdges: Found call site info in {0}",
           function_die.GetPubname());

  // Scan the DIE for TAG_call_site entries.
  // TODO: A recursive scan of all blocks in the subprogram is needed in order
  // to be DWARF5-compliant. This may need to be done lazily to be performant.
  // For now, assume that all entries are nested directly under the subprogram
  // (this is the kind of DWARF LLVM produces) and parse them eagerly.
  std::vector<CallEdge> call_edges;
  for (DWARFDIE child = function_die.GetFirstChild(); child.IsValid();
       child = child.GetSibling()) {
    if (child.Tag() != DW_TAG_call_site)
      continue;

    // Extract DW_AT_call_origin (the call target's DIE).
    DWARFDIE call_origin = child.GetReferencedDIE(DW_AT_call_origin);
    if (!call_origin.IsValid()) {
      LLDB_LOG(log, "CollectCallEdges: Invalid call origin in {0}",
               function_die.GetPubname());
      continue;
    }

    // Extract DW_AT_call_return_pc (the PC the call returns to) if it's
    // available. It should only ever be unavailable for tail call edges, in
    // which case use LLDB_INVALID_ADDRESS.
    addr_t return_pc = child.GetAttributeValueAsAddress(DW_AT_call_return_pc,
                                                        LLDB_INVALID_ADDRESS);

    LLDB_LOG(log, "CollectCallEdges: Found call origin: {0} (retn-PC: {1:x})",
             call_origin.GetPubname(), return_pc);
    call_edges.emplace_back(call_origin.GetMangledName(), return_pc);
  }
  return call_edges;
}

std::vector<lldb_private::CallEdge>
SymbolFileDWARF::ParseCallEdgesInFunction(UserID func_id) {
  DWARFDIE func_die = GetDIEFromUID(func_id.GetID());
  if (func_die.IsValid())
    return CollectCallEdges(func_die);
  return {};
}

//------------------------------------------------------------------
// PluginInterface protocol
//------------------------------------------------------------------
ConstString SymbolFileDWARF::GetPluginName() { return GetPluginNameStatic(); }

uint32_t SymbolFileDWARF::GetPluginVersion() { return 1; }

void SymbolFileDWARF::Dump(lldb_private::Stream &s) { m_index->Dump(s); }

void SymbolFileDWARF::DumpClangAST(Stream &s) {
  TypeSystem *ts = GetTypeSystemForLanguage(eLanguageTypeC_plus_plus);
  ClangASTContext *clang = llvm::dyn_cast_or_null<ClangASTContext>(ts);
  if (!clang)
    return;
  clang->Dump(s);
}

SymbolFileDWARFDebugMap *SymbolFileDWARF::GetDebugMapSymfile() {
  if (m_debug_map_symfile == NULL && !m_debug_map_module_wp.expired()) {
    lldb::ModuleSP module_sp(m_debug_map_module_wp.lock());
    if (module_sp) {
      SymbolVendor *sym_vendor = module_sp->GetSymbolVendor();
      if (sym_vendor)
        m_debug_map_symfile =
            (SymbolFileDWARFDebugMap *)sym_vendor->GetSymbolFile();
    }
  }
  return m_debug_map_symfile;
}

DWARFExpression::LocationListFormat
SymbolFileDWARF::GetLocationListFormat() const {
  if (m_data_debug_loclists.m_data.GetByteSize() > 0)
    return DWARFExpression::LocLists;
  return DWARFExpression::RegularLocationList;
}

SymbolFileDWARFDwp *SymbolFileDWARF::GetDwpSymbolFile() {
  llvm::call_once(m_dwp_symfile_once_flag, [this]() {
    ModuleSpec module_spec;
    module_spec.GetFileSpec() = m_obj_file->GetFileSpec();
    module_spec.GetSymbolFileSpec() =
        FileSpec(m_obj_file->GetFileSpec().GetPath() + ".dwp");
    FileSpec dwp_filespec = Symbols::LocateExecutableSymbolFile(module_spec);
    if (FileSystem::Instance().Exists(dwp_filespec)) {
      m_dwp_symfile = SymbolFileDWARFDwp::Create(GetObjectFile()->GetModule(),
                                                 dwp_filespec);
    }
  });
  return m_dwp_symfile.get();
}
