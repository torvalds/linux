//===-- CompileUnit.cpp -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Core/Module.h"
#include "lldb/Symbol/LineTable.h"
#include "lldb/Symbol/SymbolVendor.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Target/Language.h"

using namespace lldb;
using namespace lldb_private;

CompileUnit::CompileUnit(const lldb::ModuleSP &module_sp, void *user_data,
                         const char *pathname, const lldb::user_id_t cu_sym_id,
                         lldb::LanguageType language,
                         lldb_private::LazyBool is_optimized)
    : ModuleChild(module_sp), FileSpec(pathname), UserID(cu_sym_id),
      m_user_data(user_data), m_language(language), m_flags(0),
      m_support_files(), m_line_table_ap(), m_variables(),
      m_is_optimized(is_optimized) {
  if (language != eLanguageTypeUnknown)
    m_flags.Set(flagsParsedLanguage);
  assert(module_sp);
}

CompileUnit::CompileUnit(const lldb::ModuleSP &module_sp, void *user_data,
                         const FileSpec &fspec, const lldb::user_id_t cu_sym_id,
                         lldb::LanguageType language,
                         lldb_private::LazyBool is_optimized)
    : ModuleChild(module_sp), FileSpec(fspec), UserID(cu_sym_id),
      m_user_data(user_data), m_language(language), m_flags(0),
      m_support_files(), m_line_table_ap(), m_variables(),
      m_is_optimized(is_optimized) {
  if (language != eLanguageTypeUnknown)
    m_flags.Set(flagsParsedLanguage);
  assert(module_sp);
}

CompileUnit::~CompileUnit() {}

void CompileUnit::CalculateSymbolContext(SymbolContext *sc) {
  sc->comp_unit = this;
  GetModule()->CalculateSymbolContext(sc);
}

ModuleSP CompileUnit::CalculateSymbolContextModule() { return GetModule(); }

CompileUnit *CompileUnit::CalculateSymbolContextCompileUnit() { return this; }

void CompileUnit::DumpSymbolContext(Stream *s) {
  GetModule()->DumpSymbolContext(s);
  s->Printf(", CompileUnit{0x%8.8" PRIx64 "}", GetID());
}

void CompileUnit::GetDescription(Stream *s,
                                 lldb::DescriptionLevel level) const {
  const char *language = Language::GetNameForLanguageType(m_language);
  *s << "id = " << (const UserID &)*this << ", file = \""
     << (const FileSpec &)*this << "\", language = \"" << language << '"';
}

void CompileUnit::ForeachFunction(
    llvm::function_ref<bool(const FunctionSP &)> lambda) const {
  std::vector<lldb::FunctionSP> sorted_functions;
  sorted_functions.reserve(m_functions_by_uid.size());
  for (auto &p : m_functions_by_uid)
    sorted_functions.push_back(p.second);
  llvm::sort(sorted_functions.begin(), sorted_functions.end(),
             [](const lldb::FunctionSP &a, const lldb::FunctionSP &b) {
               return a->GetID() < b->GetID();
             });

  for (auto &f : sorted_functions)
    if (lambda(f))
      return;
}

//----------------------------------------------------------------------
// Dump the current contents of this object. No functions that cause on demand
// parsing of functions, globals, statics are called, so this is a good
// function to call to get an idea of the current contents of the CompileUnit
// object.
//----------------------------------------------------------------------
void CompileUnit::Dump(Stream *s, bool show_context) const {
  const char *language = Language::GetNameForLanguageType(m_language);

  s->Printf("%p: ", static_cast<const void *>(this));
  s->Indent();
  *s << "CompileUnit" << static_cast<const UserID &>(*this) << ", language = \""
     << language << "\", file = '" << static_cast<const FileSpec &>(*this)
     << "'\n";

  //  m_types.Dump(s);

  if (m_variables.get()) {
    s->IndentMore();
    m_variables->Dump(s, show_context);
    s->IndentLess();
  }

  if (!m_functions_by_uid.empty()) {
    s->IndentMore();
    ForeachFunction([&s, show_context](const FunctionSP &f) {
      f->Dump(s, show_context);
      return false;
    });

    s->IndentLess();
    s->EOL();
  }
}

//----------------------------------------------------------------------
// Add a function to this compile unit
//----------------------------------------------------------------------
void CompileUnit::AddFunction(FunctionSP &funcSP) {
  m_functions_by_uid[funcSP->GetID()] = funcSP;
}

//----------------------------------------------------------------------
// Find functions using the Mangled::Tokens token list. This function currently
// implements an interactive approach designed to find all instances of certain
// functions. It isn't designed to the quickest way to lookup functions as it
// will need to iterate through all functions and see if they match, though it
// does provide a powerful and context sensitive way to search for all
// functions with a certain name, all functions in a namespace, or all
// functions of a template type. See Mangled::Tokens::Parse() comments for more
// information.
//
// The function prototype will need to change to return a list of results. It
// was originally used to help debug the Mangled class and the
// Mangled::Tokens::MatchesQuery() function and it currently will print out a
// list of matching results for the functions that are currently in this
// compile unit.
//
// A FindFunctions method should be called prior to this that takes
// a regular function name (const char * or ConstString as a parameter) before
// resorting to this slower but more complete function. The other FindFunctions
// method should be able to take advantage of any accelerator tables available
// in the debug information (which is parsed by the SymbolFile parser plug-ins
// and registered with each Module).
//----------------------------------------------------------------------
// void
// CompileUnit::FindFunctions(const Mangled::Tokens& tokens)
//{
//  if (!m_functions.empty())
//  {
//      Stream s(stdout);
//      std::vector<FunctionSP>::const_iterator pos;
//      std::vector<FunctionSP>::const_iterator end = m_functions.end();
//      for (pos = m_functions.begin(); pos != end; ++pos)
//      {
//          const ConstString& demangled = (*pos)->Mangled().Demangled();
//          if (demangled)
//          {
//              const Mangled::Tokens& func_tokens =
//              (*pos)->Mangled().GetTokens();
//              if (func_tokens.MatchesQuery (tokens))
//                  s << "demangled MATCH found: " << demangled << "\n";
//          }
//      }
//  }
//}

FunctionSP CompileUnit::FindFunctionByUID(lldb::user_id_t func_uid) {
  auto it = m_functions_by_uid.find(func_uid);
  if (it == m_functions_by_uid.end())
    return FunctionSP();
  return it->second;
}

lldb::LanguageType CompileUnit::GetLanguage() {
  if (m_language == eLanguageTypeUnknown) {
    if (m_flags.IsClear(flagsParsedLanguage)) {
      m_flags.Set(flagsParsedLanguage);
      SymbolVendor *symbol_vendor = GetModule()->GetSymbolVendor();
      if (symbol_vendor) {
        m_language = symbol_vendor->ParseLanguage(*this);
      }
    }
  }
  return m_language;
}

LineTable *CompileUnit::GetLineTable() {
  if (m_line_table_ap.get() == nullptr) {
    if (m_flags.IsClear(flagsParsedLineTable)) {
      m_flags.Set(flagsParsedLineTable);
      SymbolVendor *symbol_vendor = GetModule()->GetSymbolVendor();
      if (symbol_vendor)
        symbol_vendor->ParseLineTable(*this);
    }
  }
  return m_line_table_ap.get();
}

void CompileUnit::SetLineTable(LineTable *line_table) {
  if (line_table == nullptr)
    m_flags.Clear(flagsParsedLineTable);
  else
    m_flags.Set(flagsParsedLineTable);
  m_line_table_ap.reset(line_table);
}

DebugMacros *CompileUnit::GetDebugMacros() {
  if (m_debug_macros_sp.get() == nullptr) {
    if (m_flags.IsClear(flagsParsedDebugMacros)) {
      m_flags.Set(flagsParsedDebugMacros);
      SymbolVendor *symbol_vendor = GetModule()->GetSymbolVendor();
      if (symbol_vendor) {
        symbol_vendor->ParseDebugMacros(*this);
      }
    }
  }

  return m_debug_macros_sp.get();
}

void CompileUnit::SetDebugMacros(const DebugMacrosSP &debug_macros_sp) {
  if (debug_macros_sp.get() == nullptr)
    m_flags.Clear(flagsParsedDebugMacros);
  else
    m_flags.Set(flagsParsedDebugMacros);
  m_debug_macros_sp = debug_macros_sp;
}

VariableListSP CompileUnit::GetVariableList(bool can_create) {
  if (m_variables.get() == nullptr && can_create) {
    SymbolContext sc;
    CalculateSymbolContext(&sc);
    assert(sc.module_sp);
    sc.module_sp->GetSymbolVendor()->ParseVariablesForContext(sc);
  }

  return m_variables;
}

uint32_t CompileUnit::FindLineEntry(uint32_t start_idx, uint32_t line,
                                    const FileSpec *file_spec_ptr, bool exact,
                                    LineEntry *line_entry_ptr) {
  uint32_t file_idx = 0;

  if (file_spec_ptr) {
    file_idx = GetSupportFiles().FindFileIndex(1, *file_spec_ptr, true);
    if (file_idx == UINT32_MAX)
      return UINT32_MAX;
  } else {
    // All the line table entries actually point to the version of the Compile
    // Unit that is in the support files (the one at 0 was artificially added.)
    // So prefer the one further on in the support files if it exists...
    FileSpecList &support_files = GetSupportFiles();
    const bool full = true;
    file_idx = support_files.FindFileIndex(
        1, support_files.GetFileSpecAtIndex(0), full);
    if (file_idx == UINT32_MAX)
      file_idx = 0;
  }
  LineTable *line_table = GetLineTable();
  if (line_table)
    return line_table->FindLineEntryIndexByFileIndex(start_idx, file_idx, line,
                                                     exact, line_entry_ptr);
  return UINT32_MAX;
}

uint32_t CompileUnit::ResolveSymbolContext(const FileSpec &file_spec,
                                           uint32_t line, bool check_inlines,
                                           bool exact,
                                           SymbolContextItem resolve_scope,
                                           SymbolContextList &sc_list) {
  // First find all of the file indexes that match our "file_spec". If
  // "file_spec" has an empty directory, then only compare the basenames when
  // finding file indexes
  std::vector<uint32_t> file_indexes;
  const bool full_match = (bool)file_spec.GetDirectory();
  bool file_spec_matches_cu_file_spec =
      FileSpec::Equal(file_spec, *this, full_match);

  // If we are not looking for inlined functions and our file spec doesn't
  // match then we are done...
  if (!file_spec_matches_cu_file_spec && !check_inlines)
    return 0;

  uint32_t file_idx =
      GetSupportFiles().FindFileIndex(1, file_spec, true);
  while (file_idx != UINT32_MAX) {
    file_indexes.push_back(file_idx);
    file_idx = GetSupportFiles().FindFileIndex(file_idx + 1, file_spec, true);
  }

  const size_t num_file_indexes = file_indexes.size();
  if (num_file_indexes == 0)
    return 0;

  const uint32_t prev_size = sc_list.GetSize();

  SymbolContext sc(GetModule());
  sc.comp_unit = this;

  if (line != 0) {
    LineTable *line_table = sc.comp_unit->GetLineTable();

    if (line_table != nullptr) {
      uint32_t found_line;
      uint32_t line_idx;

      if (num_file_indexes == 1) {
        // We only have a single support file that matches, so use the line
        // table function that searches for a line entries that match a single
        // support file index
        LineEntry line_entry;
        line_idx = line_table->FindLineEntryIndexByFileIndex(
            0, file_indexes.front(), line, exact, &line_entry);

        // If "exact == true", then "found_line" will be the same as "line". If
        // "exact == false", the "found_line" will be the closest line entry
        // with a line number greater than "line" and we will use this for our
        // subsequent line exact matches below.
        found_line = line_entry.line;

        while (line_idx != UINT32_MAX) {
          // If they only asked for the line entry, then we're done, we can
          // just copy that over. But if they wanted more than just the line
          // number, fill it in.
          if (resolve_scope == eSymbolContextLineEntry) {
            sc.line_entry = line_entry;
          } else {
            line_entry.range.GetBaseAddress().CalculateSymbolContext(
                &sc, resolve_scope);
          }

          sc_list.Append(sc);
          line_idx = line_table->FindLineEntryIndexByFileIndex(
              line_idx + 1, file_indexes.front(), found_line, true,
              &line_entry);
        }
      } else {
        // We found multiple support files that match "file_spec" so use the
        // line table function that searches for a line entries that match a
        // multiple support file indexes.
        LineEntry line_entry;
        line_idx = line_table->FindLineEntryIndexByFileIndex(
            0, file_indexes, line, exact, &line_entry);

        // If "exact == true", then "found_line" will be the same as "line". If
        // "exact == false", the "found_line" will be the closest line entry
        // with a line number greater than "line" and we will use this for our
        // subsequent line exact matches below.
        found_line = line_entry.line;

        while (line_idx != UINT32_MAX) {
          if (resolve_scope == eSymbolContextLineEntry) {
            sc.line_entry = line_entry;
          } else {
            line_entry.range.GetBaseAddress().CalculateSymbolContext(
                &sc, resolve_scope);
          }

          sc_list.Append(sc);
          line_idx = line_table->FindLineEntryIndexByFileIndex(
              line_idx + 1, file_indexes, found_line, true, &line_entry);
        }
      }
    }
  } else if (file_spec_matches_cu_file_spec && !check_inlines) {
    // only append the context if we aren't looking for inline call sites by
    // file and line and if the file spec matches that of the compile unit
    sc_list.Append(sc);
  }
  return sc_list.GetSize() - prev_size;
}

bool CompileUnit::GetIsOptimized() {
  if (m_is_optimized == eLazyBoolCalculate) {
    m_is_optimized = eLazyBoolNo;
    if (SymbolVendor *symbol_vendor = GetModule()->GetSymbolVendor()) {
      if (symbol_vendor->ParseIsOptimized(*this))
        m_is_optimized = eLazyBoolYes;
    }
  }
  return m_is_optimized;
}

void CompileUnit::SetVariableList(VariableListSP &variables) {
  m_variables = variables;
}

const std::vector<ConstString> &CompileUnit::GetImportedModules() {
  if (m_imported_modules.empty() &&
      m_flags.IsClear(flagsParsedImportedModules)) {
    m_flags.Set(flagsParsedImportedModules);
    if (SymbolVendor *symbol_vendor = GetModule()->GetSymbolVendor()) {
      SymbolContext sc;
      CalculateSymbolContext(&sc);
      symbol_vendor->ParseImportedModules(sc, m_imported_modules);
    }
  }
  return m_imported_modules;
}

FileSpecList &CompileUnit::GetSupportFiles() {
  if (m_support_files.GetSize() == 0) {
    if (m_flags.IsClear(flagsParsedSupportFiles)) {
      m_flags.Set(flagsParsedSupportFiles);
      SymbolVendor *symbol_vendor = GetModule()->GetSymbolVendor();
      if (symbol_vendor) {
        symbol_vendor->ParseSupportFiles(*this, m_support_files);
      }
    }
  }
  return m_support_files;
}

void *CompileUnit::GetUserData() const { return m_user_data; }
