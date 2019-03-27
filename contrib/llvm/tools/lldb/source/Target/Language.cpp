//===-- Language.cpp -------------------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <functional>
#include <map>
#include <mutex>

#include "lldb/Target/Language.h"

#include "lldb/Core/PluginManager.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/TypeList.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/Stream.h"

#include "llvm/Support/Threading.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;

typedef std::unique_ptr<Language> LanguageUP;
typedef std::map<lldb::LanguageType, LanguageUP> LanguagesMap;

static LanguagesMap &GetLanguagesMap() {
  static LanguagesMap *g_map = nullptr;
  static llvm::once_flag g_initialize;

  llvm::call_once(g_initialize, [] {
    g_map = new LanguagesMap(); // NOTE: INTENTIONAL LEAK due to global
                                // destructor chain
  });

  return *g_map;
}
static std::mutex &GetLanguagesMutex() {
  static std::mutex *g_mutex = nullptr;
  static llvm::once_flag g_initialize;

  llvm::call_once(g_initialize, [] {
    g_mutex = new std::mutex(); // NOTE: INTENTIONAL LEAK due to global
                                // destructor chain
  });

  return *g_mutex;
}

Language *Language::FindPlugin(lldb::LanguageType language) {
  std::lock_guard<std::mutex> guard(GetLanguagesMutex());
  LanguagesMap &map(GetLanguagesMap());
  auto iter = map.find(language), end = map.end();
  if (iter != end)
    return iter->second.get();

  Language *language_ptr = nullptr;
  LanguageCreateInstance create_callback;

  for (uint32_t idx = 0;
       (create_callback =
            PluginManager::GetLanguageCreateCallbackAtIndex(idx)) != nullptr;
       ++idx) {
    language_ptr = create_callback(language);

    if (language_ptr) {
      map[language] = std::unique_ptr<Language>(language_ptr);
      return language_ptr;
    }
  }

  return nullptr;
}

Language *Language::FindPlugin(llvm::StringRef file_path) {
  Language *result = nullptr;
  ForEach([&result, file_path](Language *language) {
    if (language->IsSourceFile(file_path)) {
      result = language;
      return false;
    }
    return true;
  });
  return result;
}

Language *Language::FindPlugin(LanguageType language,
                               llvm::StringRef file_path) {
  Language *result = FindPlugin(language);
  // Finding a language by file path is slower, we so we use this as the
  // fallback.
  if (!result)
    result = FindPlugin(file_path);
  return result;
}

void Language::ForEach(std::function<bool(Language *)> callback) {
  // If we want to iterate over all languages, we first have to complete the
  // LanguagesMap.
  static llvm::once_flag g_initialize;
  llvm::call_once(g_initialize, [] {
    for (unsigned lang = eLanguageTypeUnknown; lang < eNumLanguageTypes;
         ++lang) {
      FindPlugin(static_cast<lldb::LanguageType>(lang));
    }
  });

  std::lock_guard<std::mutex> guard(GetLanguagesMutex());
  LanguagesMap &map(GetLanguagesMap());
  for (const auto &entry : map) {
    if (!callback(entry.second.get()))
      break;
  }
}

bool Language::IsTopLevelFunction(Function &function) { return false; }

lldb::TypeCategoryImplSP Language::GetFormatters() { return nullptr; }

HardcodedFormatters::HardcodedFormatFinder Language::GetHardcodedFormats() {
  return {};
}

HardcodedFormatters::HardcodedSummaryFinder Language::GetHardcodedSummaries() {
  return {};
}

HardcodedFormatters::HardcodedSyntheticFinder
Language::GetHardcodedSynthetics() {
  return {};
}

HardcodedFormatters::HardcodedValidatorFinder
Language::GetHardcodedValidators() {
  return {};
}

std::vector<ConstString>
Language::GetPossibleFormattersMatches(ValueObject &valobj,
                                       lldb::DynamicValueType use_dynamic) {
  return {};
}

lldb_private::formatters::StringPrinter::EscapingHelper
Language::GetStringPrinterEscapingHelper(
    lldb_private::formatters::StringPrinter::GetPrintableElementType
        elem_type) {
  return StringPrinter::GetDefaultEscapingHelper(elem_type);
}

struct language_name_pair {
  const char *name;
  LanguageType type;
};

struct language_name_pair language_names[] = {
    // To allow GetNameForLanguageType to be a simple array lookup, the first
    // part of this array must follow enum LanguageType exactly.
    {"unknown", eLanguageTypeUnknown},
    {"c89", eLanguageTypeC89},
    {"c", eLanguageTypeC},
    {"ada83", eLanguageTypeAda83},
    {"c++", eLanguageTypeC_plus_plus},
    {"cobol74", eLanguageTypeCobol74},
    {"cobol85", eLanguageTypeCobol85},
    {"fortran77", eLanguageTypeFortran77},
    {"fortran90", eLanguageTypeFortran90},
    {"pascal83", eLanguageTypePascal83},
    {"modula2", eLanguageTypeModula2},
    {"java", eLanguageTypeJava},
    {"c99", eLanguageTypeC99},
    {"ada95", eLanguageTypeAda95},
    {"fortran95", eLanguageTypeFortran95},
    {"pli", eLanguageTypePLI},
    {"objective-c", eLanguageTypeObjC},
    {"objective-c++", eLanguageTypeObjC_plus_plus},
    {"upc", eLanguageTypeUPC},
    {"d", eLanguageTypeD},
    {"python", eLanguageTypePython},
    {"opencl", eLanguageTypeOpenCL},
    {"go", eLanguageTypeGo},
    {"modula3", eLanguageTypeModula3},
    {"haskell", eLanguageTypeHaskell},
    {"c++03", eLanguageTypeC_plus_plus_03},
    {"c++11", eLanguageTypeC_plus_plus_11},
    {"ocaml", eLanguageTypeOCaml},
    {"rust", eLanguageTypeRust},
    {"c11", eLanguageTypeC11},
    {"swift", eLanguageTypeSwift},
    {"julia", eLanguageTypeJulia},
    {"dylan", eLanguageTypeDylan},
    {"c++14", eLanguageTypeC_plus_plus_14},
    {"fortran03", eLanguageTypeFortran03},
    {"fortran08", eLanguageTypeFortran08},
    // Vendor Extensions
    {"mipsassem", eLanguageTypeMipsAssembler},
    {"renderscript", eLanguageTypeExtRenderScript},
    // Now synonyms, in arbitrary order
    {"objc", eLanguageTypeObjC},
    {"objc++", eLanguageTypeObjC_plus_plus},
    {"pascal", eLanguageTypePascal83}};

static uint32_t num_languages =
    sizeof(language_names) / sizeof(struct language_name_pair);

LanguageType Language::GetLanguageTypeFromString(llvm::StringRef string) {
  for (const auto &L : language_names) {
    if (string.equals_lower(L.name))
      return static_cast<LanguageType>(L.type);
  }

  return eLanguageTypeUnknown;
}

const char *Language::GetNameForLanguageType(LanguageType language) {
  if (language < num_languages)
    return language_names[language].name;
  else
    return language_names[eLanguageTypeUnknown].name;
}

void Language::PrintAllLanguages(Stream &s, const char *prefix,
                                 const char *suffix) {
  for (uint32_t i = 1; i < num_languages; i++) {
    s.Printf("%s%s%s", prefix, language_names[i].name, suffix);
  }
}

void Language::ForAllLanguages(
    std::function<bool(lldb::LanguageType)> callback) {
  for (uint32_t i = 1; i < num_languages; i++) {
    if (!callback(language_names[i].type))
      break;
  }
}

bool Language::LanguageIsCPlusPlus(LanguageType language) {
  switch (language) {
  case eLanguageTypeC_plus_plus:
  case eLanguageTypeC_plus_plus_03:
  case eLanguageTypeC_plus_plus_11:
  case eLanguageTypeC_plus_plus_14:
  case eLanguageTypeObjC_plus_plus:
    return true;
  default:
    return false;
  }
}

bool Language::LanguageIsObjC(LanguageType language) {
  switch (language) {
  case eLanguageTypeObjC:
  case eLanguageTypeObjC_plus_plus:
    return true;
  default:
    return false;
  }
}

bool Language::LanguageIsC(LanguageType language) {
  switch (language) {
  case eLanguageTypeC:
  case eLanguageTypeC89:
  case eLanguageTypeC99:
  case eLanguageTypeC11:
    return true;
  default:
    return false;
  }
}

bool Language::LanguageIsPascal(LanguageType language) {
  switch (language) {
  case eLanguageTypePascal83:
    return true;
  default:
    return false;
  }
}

LanguageType Language::GetPrimaryLanguage(LanguageType language) {
  switch (language) {
  case eLanguageTypeC_plus_plus:
  case eLanguageTypeC_plus_plus_03:
  case eLanguageTypeC_plus_plus_11:
  case eLanguageTypeC_plus_plus_14:
    return eLanguageTypeC_plus_plus;
  case eLanguageTypeC:
  case eLanguageTypeC89:
  case eLanguageTypeC99:
  case eLanguageTypeC11:
    return eLanguageTypeC;
  case eLanguageTypeObjC:
  case eLanguageTypeObjC_plus_plus:
    return eLanguageTypeObjC;
  case eLanguageTypePascal83:
  case eLanguageTypeCobol74:
  case eLanguageTypeCobol85:
  case eLanguageTypeFortran77:
  case eLanguageTypeFortran90:
  case eLanguageTypeFortran95:
  case eLanguageTypeFortran03:
  case eLanguageTypeFortran08:
  case eLanguageTypeAda83:
  case eLanguageTypeAda95:
  case eLanguageTypeModula2:
  case eLanguageTypeJava:
  case eLanguageTypePLI:
  case eLanguageTypeUPC:
  case eLanguageTypeD:
  case eLanguageTypePython:
  case eLanguageTypeOpenCL:
  case eLanguageTypeGo:
  case eLanguageTypeModula3:
  case eLanguageTypeHaskell:
  case eLanguageTypeOCaml:
  case eLanguageTypeRust:
  case eLanguageTypeSwift:
  case eLanguageTypeJulia:
  case eLanguageTypeDylan:
  case eLanguageTypeMipsAssembler:
  case eLanguageTypeExtRenderScript:
  case eLanguageTypeUnknown:
  default:
    return language;
  }
}

void Language::GetLanguagesSupportingTypeSystems(
    std::set<lldb::LanguageType> &languages,
    std::set<lldb::LanguageType> &languages_for_expressions) {
  uint32_t idx = 0;

  while (TypeSystemEnumerateSupportedLanguages enumerate = PluginManager::
             GetTypeSystemEnumerateSupportedLanguagesCallbackAtIndex(idx++)) {
    (*enumerate)(languages, languages_for_expressions);
  }
}

void Language::GetLanguagesSupportingREPLs(
    std::set<lldb::LanguageType> &languages) {
  uint32_t idx = 0;

  while (REPLEnumerateSupportedLanguages enumerate =
             PluginManager::GetREPLEnumerateSupportedLanguagesCallbackAtIndex(
                 idx++)) {
    (*enumerate)(languages);
  }
}

std::unique_ptr<Language::TypeScavenger> Language::GetTypeScavenger() {
  return nullptr;
}

const char *Language::GetLanguageSpecificTypeLookupHelp() { return nullptr; }

size_t Language::TypeScavenger::Find(ExecutionContextScope *exe_scope,
                                     const char *key, ResultSet &results,
                                     bool append) {
  if (!exe_scope || !exe_scope->CalculateTarget().get())
    return false;

  if (!key || !key[0])
    return false;

  if (!append)
    results.clear();

  size_t old_size = results.size();

  if (this->Find_Impl(exe_scope, key, results))
    return results.size() - old_size;
  return 0;
}

bool Language::ImageListTypeScavenger::Find_Impl(
    ExecutionContextScope *exe_scope, const char *key, ResultSet &results) {
  bool result = false;

  Target *target = exe_scope->CalculateTarget().get();
  if (target) {
    const auto &images(target->GetImages());
    ConstString cs_key(key);
    llvm::DenseSet<SymbolFile *> searched_sym_files;
    TypeList matches;
    images.FindTypes(nullptr, cs_key, false, UINT32_MAX, searched_sym_files,
                     matches);
    for (const auto &match : matches.Types()) {
      if (match.get()) {
        CompilerType compiler_type(match->GetFullCompilerType());
        compiler_type = AdjustForInclusion(compiler_type);
        if (!compiler_type)
          continue;
        std::unique_ptr<Language::TypeScavenger::Result> scavengeresult(
            new Result(compiler_type));
        results.insert(std::move(scavengeresult));
        result = true;
      }
    }
  }

  return result;
}

bool Language::GetFormatterPrefixSuffix(ValueObject &valobj,
                                        ConstString type_hint,
                                        std::string &prefix,
                                        std::string &suffix) {
  return false;
}

DumpValueObjectOptions::DeclPrintingHelper Language::GetDeclPrintingHelper() {
  return nullptr;
}

LazyBool Language::IsLogicalTrue(ValueObject &valobj, Status &error) {
  return eLazyBoolCalculate;
}

bool Language::IsNilReference(ValueObject &valobj) { return false; }

bool Language::IsUninitializedReference(ValueObject &valobj) { return false; }

bool Language::GetFunctionDisplayName(const SymbolContext *sc,
                                      const ExecutionContext *exe_ctx,
                                      FunctionNameRepresentation representation,
                                      Stream &s) {
  return false;
}

void Language::GetExceptionResolverDescription(bool catch_on, bool throw_on,
                                               Stream &s) {
  GetDefaultExceptionResolverDescription(catch_on, throw_on, s);
}

void Language::GetDefaultExceptionResolverDescription(bool catch_on,
                                                      bool throw_on,
                                                      Stream &s) {
  s.Printf("Exception breakpoint (catch: %s throw: %s)",
           catch_on ? "on" : "off", throw_on ? "on" : "off");
}
//----------------------------------------------------------------------
// Constructor
//----------------------------------------------------------------------
Language::Language() {}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
Language::~Language() {}
