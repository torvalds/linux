//===-- BreakpointResolverName.cpp ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Breakpoint/BreakpointResolverName.h"

#include "Plugins/Language/CPlusPlus/CPlusPlusLanguage.h"
#include "Plugins/Language/ObjC/ObjCLanguage.h"
#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Core/Architecture.h"
#include "lldb/Core/Module.h"
#include "lldb/Symbol/Block.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"

using namespace lldb;
using namespace lldb_private;

BreakpointResolverName::BreakpointResolverName(
    Breakpoint *bkpt, const char *name_cstr, FunctionNameType name_type_mask,
    LanguageType language, Breakpoint::MatchType type, lldb::addr_t offset,
    bool skip_prologue)
    : BreakpointResolver(bkpt, BreakpointResolver::NameResolver, offset),
      m_class_name(), m_regex(), m_match_type(type), m_language(language),
      m_skip_prologue(skip_prologue) {
  if (m_match_type == Breakpoint::Regexp) {
    if (!m_regex.Compile(llvm::StringRef::withNullAsEmpty(name_cstr))) {
      Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_BREAKPOINTS));

      if (log)
        log->Warning("function name regexp: \"%s\" did not compile.",
                     name_cstr);
    }
  } else {
    AddNameLookup(ConstString(name_cstr), name_type_mask);
  }
}

BreakpointResolverName::BreakpointResolverName(
    Breakpoint *bkpt, const char *names[], size_t num_names,
    FunctionNameType name_type_mask, LanguageType language, lldb::addr_t offset,
    bool skip_prologue)
    : BreakpointResolver(bkpt, BreakpointResolver::NameResolver, offset),
      m_match_type(Breakpoint::Exact), m_language(language),
      m_skip_prologue(skip_prologue) {
  for (size_t i = 0; i < num_names; i++) {
    AddNameLookup(ConstString(names[i]), name_type_mask);
  }
}

BreakpointResolverName::BreakpointResolverName(Breakpoint *bkpt,
                                               std::vector<std::string> names,
                                               FunctionNameType name_type_mask,
                                               LanguageType language,
                                               lldb::addr_t offset,
                                               bool skip_prologue)
    : BreakpointResolver(bkpt, BreakpointResolver::NameResolver, offset),
      m_match_type(Breakpoint::Exact), m_language(language),
      m_skip_prologue(skip_prologue) {
  for (const std::string &name : names) {
    AddNameLookup(ConstString(name.c_str(), name.size()), name_type_mask);
  }
}

BreakpointResolverName::BreakpointResolverName(Breakpoint *bkpt,
                                               RegularExpression &func_regex,
                                               lldb::LanguageType language,
                                               lldb::addr_t offset,
                                               bool skip_prologue)
    : BreakpointResolver(bkpt, BreakpointResolver::NameResolver, offset),
      m_class_name(nullptr), m_regex(func_regex),
      m_match_type(Breakpoint::Regexp), m_language(language),
      m_skip_prologue(skip_prologue) {}

BreakpointResolverName::~BreakpointResolverName() = default;

BreakpointResolverName::BreakpointResolverName(
    const BreakpointResolverName &rhs)
    : BreakpointResolver(rhs.m_breakpoint, BreakpointResolver::NameResolver,
                         rhs.m_offset),
      m_lookups(rhs.m_lookups), m_class_name(rhs.m_class_name),
      m_regex(rhs.m_regex), m_match_type(rhs.m_match_type),
      m_language(rhs.m_language), m_skip_prologue(rhs.m_skip_prologue) {}

BreakpointResolver *BreakpointResolverName::CreateFromStructuredData(
    Breakpoint *bkpt, const StructuredData::Dictionary &options_dict,
    Status &error) {
  LanguageType language = eLanguageTypeUnknown;
  llvm::StringRef language_name;
  bool success = options_dict.GetValueForKeyAsString(
      GetKey(OptionNames::LanguageName), language_name);
  if (success) {
    language = Language::GetLanguageTypeFromString(language_name);
    if (language == eLanguageTypeUnknown) {
      error.SetErrorStringWithFormatv("BRN::CFSD: Unknown language: {0}.",
                                      language_name);
      return nullptr;
    }
  }

  lldb::addr_t offset = 0;
  success =
      options_dict.GetValueForKeyAsInteger(GetKey(OptionNames::Offset), offset);
  if (!success) {
    error.SetErrorStringWithFormat("BRN::CFSD: Missing offset entry.");
    return nullptr;
  }

  bool skip_prologue;
  success = options_dict.GetValueForKeyAsBoolean(
      GetKey(OptionNames::SkipPrologue), skip_prologue);
  if (!success) {
    error.SetErrorStringWithFormat("BRN::CFSD: Missing Skip prologue entry.");
    return nullptr;
  }

  llvm::StringRef regex_text;
  success = options_dict.GetValueForKeyAsString(
      GetKey(OptionNames::RegexString), regex_text);
  if (success) {
    RegularExpression regex(regex_text);
    return new BreakpointResolverName(bkpt, regex, language, offset,
                                      skip_prologue);
  } else {
    StructuredData::Array *names_array;
    success = options_dict.GetValueForKeyAsArray(
        GetKey(OptionNames::SymbolNameArray), names_array);
    if (!success) {
      error.SetErrorStringWithFormat("BRN::CFSD: Missing symbol names entry.");
      return nullptr;
    }
    StructuredData::Array *names_mask_array;
    success = options_dict.GetValueForKeyAsArray(
        GetKey(OptionNames::NameMaskArray), names_mask_array);
    if (!success) {
      error.SetErrorStringWithFormat(
          "BRN::CFSD: Missing symbol names mask entry.");
      return nullptr;
    }

    size_t num_elem = names_array->GetSize();
    if (num_elem != names_mask_array->GetSize()) {
      error.SetErrorString(
          "BRN::CFSD: names and names mask arrays have different sizes.");
      return nullptr;
    }

    if (num_elem == 0) {
      error.SetErrorString(
          "BRN::CFSD: no name entry in a breakpoint by name breakpoint.");
      return nullptr;
    }
    std::vector<std::string> names;
    std::vector<FunctionNameType> name_masks;
    for (size_t i = 0; i < num_elem; i++) {
      llvm::StringRef name;

      success = names_array->GetItemAtIndexAsString(i, name);
      if (!success) {
        error.SetErrorString("BRN::CFSD: name entry is not a string.");
        return nullptr;
      }
      std::underlying_type<FunctionNameType>::type fnt;
      success = names_mask_array->GetItemAtIndexAsInteger(i, fnt);
      if (!success) {
        error.SetErrorString("BRN::CFSD: name mask entry is not an integer.");
        return nullptr;
      }
      names.push_back(name);
      name_masks.push_back(static_cast<FunctionNameType>(fnt));
    }

    BreakpointResolverName *resolver = new BreakpointResolverName(
        bkpt, names[0].c_str(), name_masks[0], language,
        Breakpoint::MatchType::Exact, offset, skip_prologue);
    for (size_t i = 1; i < num_elem; i++) {
      resolver->AddNameLookup(ConstString(names[i]), name_masks[i]);
    }
    return resolver;
  }
}

StructuredData::ObjectSP BreakpointResolverName::SerializeToStructuredData() {
  StructuredData::DictionarySP options_dict_sp(
      new StructuredData::Dictionary());

  if (m_regex.IsValid()) {
    options_dict_sp->AddStringItem(GetKey(OptionNames::RegexString),
                                   m_regex.GetText());
  } else {
    StructuredData::ArraySP names_sp(new StructuredData::Array());
    StructuredData::ArraySP name_masks_sp(new StructuredData::Array());
    for (auto lookup : m_lookups) {
      names_sp->AddItem(StructuredData::StringSP(
          new StructuredData::String(lookup.GetName().AsCString())));
      name_masks_sp->AddItem(StructuredData::IntegerSP(
          new StructuredData::Integer(lookup.GetNameTypeMask())));
    }
    options_dict_sp->AddItem(GetKey(OptionNames::SymbolNameArray), names_sp);
    options_dict_sp->AddItem(GetKey(OptionNames::NameMaskArray), name_masks_sp);
  }
  if (m_language != eLanguageTypeUnknown)
    options_dict_sp->AddStringItem(
        GetKey(OptionNames::LanguageName),
        Language::GetNameForLanguageType(m_language));
  options_dict_sp->AddBooleanItem(GetKey(OptionNames::SkipPrologue),
                                  m_skip_prologue);

  return WrapOptionsDict(options_dict_sp);
}

void BreakpointResolverName::AddNameLookup(const ConstString &name,
                                           FunctionNameType name_type_mask) {
  ObjCLanguage::MethodName objc_method(name.GetCString(), false);
  if (objc_method.IsValid(false)) {
    std::vector<ConstString> objc_names;
    objc_method.GetFullNames(objc_names, true);
    for (ConstString objc_name : objc_names) {
      Module::LookupInfo lookup;
      lookup.SetName(name);
      lookup.SetLookupName(objc_name);
      lookup.SetNameTypeMask(eFunctionNameTypeFull);
      m_lookups.push_back(lookup);
    }
  } else {
    Module::LookupInfo lookup(name, name_type_mask, m_language);
    m_lookups.push_back(lookup);
  }
}

// FIXME: Right now we look at the module level, and call the module's
// "FindFunctions".
// Greg says he will add function tables, maybe at the CompileUnit level to
// accelerate function lookup.  At that point, we should switch the depth to
// CompileUnit, and look in these tables.

Searcher::CallbackReturn
BreakpointResolverName::SearchCallback(SearchFilter &filter,
                                       SymbolContext &context, Address *addr,
                                       bool containing) {
  SymbolContextList func_list;
  // SymbolContextList sym_list;

  uint32_t i;
  bool new_location;
  Address break_addr;
  assert(m_breakpoint != nullptr);

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_BREAKPOINTS));

  if (m_class_name) {
    if (log)
      log->Warning("Class/method function specification not supported yet.\n");
    return Searcher::eCallbackReturnStop;
  }
  bool filter_by_cu =
      (filter.GetFilterRequiredItems() & eSymbolContextCompUnit) != 0;
  bool filter_by_language = (m_language != eLanguageTypeUnknown);
  const bool include_symbols = !filter_by_cu;
  const bool include_inlines = true;
  const bool append = true;

  switch (m_match_type) {
  case Breakpoint::Exact:
    if (context.module_sp) {
      for (const auto &lookup : m_lookups) {
        const size_t start_func_idx = func_list.GetSize();
        context.module_sp->FindFunctions(
            lookup.GetLookupName(), nullptr, lookup.GetNameTypeMask(),
            include_symbols, include_inlines, append, func_list);

        const size_t end_func_idx = func_list.GetSize();

        if (start_func_idx < end_func_idx)
          lookup.Prune(func_list, start_func_idx);
      }
    }
    break;
  case Breakpoint::Regexp:
    if (context.module_sp) {
      context.module_sp->FindFunctions(
          m_regex,
          !filter_by_cu, // include symbols only if we aren't filtering by CU
          include_inlines, append, func_list);
    }
    break;
  case Breakpoint::Glob:
    if (log)
      log->Warning("glob is not supported yet.");
    break;
  }

  // If the filter specifies a Compilation Unit, remove the ones that don't
  // pass at this point.
  if (filter_by_cu || filter_by_language) {
    uint32_t num_functions = func_list.GetSize();

    for (size_t idx = 0; idx < num_functions; idx++) {
      bool remove_it = false;
      SymbolContext sc;
      func_list.GetContextAtIndex(idx, sc);
      if (filter_by_cu) {
        if (!sc.comp_unit || !filter.CompUnitPasses(*sc.comp_unit))
          remove_it = true;
      }

      if (filter_by_language) {
        LanguageType sym_language = sc.GetLanguage();
        if ((Language::GetPrimaryLanguage(sym_language) !=
             Language::GetPrimaryLanguage(m_language)) &&
            (sym_language != eLanguageTypeUnknown)) {
          remove_it = true;
        }
      }

      if (remove_it) {
        func_list.RemoveContextAtIndex(idx);
        num_functions--;
        idx--;
      }
    }
  }

  // Remove any duplicates between the function list and the symbol list
  SymbolContext sc;
  if (func_list.GetSize()) {
    for (i = 0; i < func_list.GetSize(); i++) {
      if (func_list.GetContextAtIndex(i, sc)) {
        bool is_reexported = false;

        if (sc.block && sc.block->GetInlinedFunctionInfo()) {
          if (!sc.block->GetStartAddress(break_addr))
            break_addr.Clear();
        } else if (sc.function) {
          break_addr = sc.function->GetAddressRange().GetBaseAddress();
          if (m_skip_prologue && break_addr.IsValid()) {
            const uint32_t prologue_byte_size =
                sc.function->GetPrologueByteSize();
            if (prologue_byte_size)
              break_addr.SetOffset(break_addr.GetOffset() + prologue_byte_size);
          }
        } else if (sc.symbol) {
          if (sc.symbol->GetType() == eSymbolTypeReExported) {
            const Symbol *actual_symbol =
                sc.symbol->ResolveReExportedSymbol(m_breakpoint->GetTarget());
            if (actual_symbol) {
              is_reexported = true;
              break_addr = actual_symbol->GetAddress();
            }
          } else {
            break_addr = sc.symbol->GetAddress();
          }

          if (m_skip_prologue && break_addr.IsValid()) {
            const uint32_t prologue_byte_size =
                sc.symbol->GetPrologueByteSize();
            if (prologue_byte_size)
              break_addr.SetOffset(break_addr.GetOffset() + prologue_byte_size);
            else {
              const Architecture *arch =
                  m_breakpoint->GetTarget().GetArchitecturePlugin();
              if (arch)
                arch->AdjustBreakpointAddress(*sc.symbol, break_addr);
            }
          }
        }

        if (break_addr.IsValid()) {
          if (filter.AddressPasses(break_addr)) {
            BreakpointLocationSP bp_loc_sp(
                AddLocation(break_addr, &new_location));
            bp_loc_sp->SetIsReExported(is_reexported);
            if (bp_loc_sp && new_location && !m_breakpoint->IsInternal()) {
              if (log) {
                StreamString s;
                bp_loc_sp->GetDescription(&s, lldb::eDescriptionLevelVerbose);
                log->Printf("Added location: %s\n", s.GetData());
              }
            }
          }
        }
      }
    }
  }

  return Searcher::eCallbackReturnContinue;
}

lldb::SearchDepth BreakpointResolverName::GetDepth() {
  return lldb::eSearchDepthModule;
}

void BreakpointResolverName::GetDescription(Stream *s) {
  if (m_match_type == Breakpoint::Regexp)
    s->Printf("regex = '%s'", m_regex.GetText().str().c_str());
  else {
    size_t num_names = m_lookups.size();
    if (num_names == 1)
      s->Printf("name = '%s'", m_lookups[0].GetName().GetCString());
    else {
      s->Printf("names = {");
      for (size_t i = 0; i < num_names; i++) {
        s->Printf("%s'%s'", (i == 0 ? "" : ", "),
                  m_lookups[i].GetName().GetCString());
      }
      s->Printf("}");
    }
  }
  if (m_language != eLanguageTypeUnknown) {
    s->Printf(", language = %s", Language::GetNameForLanguageType(m_language));
  }
}

void BreakpointResolverName::Dump(Stream *s) const {}

lldb::BreakpointResolverSP
BreakpointResolverName::CopyForBreakpoint(Breakpoint &breakpoint) {
  lldb::BreakpointResolverSP ret_sp(new BreakpointResolverName(*this));
  ret_sp->SetBreakpoint(&breakpoint);
  return ret_sp;
}
