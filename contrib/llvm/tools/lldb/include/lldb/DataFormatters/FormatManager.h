//===-- FormatManager.h -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_FormatManager_h_
#define lldb_FormatManager_h_

#include <atomic>
#include <initializer_list>
#include <map>
#include <mutex>
#include <vector>

#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-public.h"

#include "lldb/DataFormatters/FormatCache.h"
#include "lldb/DataFormatters/FormatClasses.h"
#include "lldb/DataFormatters/FormattersContainer.h"
#include "lldb/DataFormatters/LanguageCategory.h"
#include "lldb/DataFormatters/TypeCategory.h"
#include "lldb/DataFormatters/TypeCategoryMap.h"

namespace lldb_private {

// this file (and its. cpp) contain the low-level implementation of LLDB Data
// Visualization class DataVisualization is the high-level front-end of this
// feature clients should refer to that class as the entry-point into the data
// formatters unless they have a good reason to bypass it and prefer to use
// this file's objects directly

class FormatManager : public IFormatChangeListener {
  typedef FormatMap<ConstString, TypeSummaryImpl> NamedSummariesMap;
  typedef TypeCategoryMap::MapType::iterator CategoryMapIterator;

public:
  typedef std::map<lldb::LanguageType, LanguageCategory::UniquePointer>
      LanguageCategories;

  FormatManager();

  ~FormatManager() override = default;

  NamedSummariesMap &GetNamedSummaryContainer() {
    return m_named_summaries_map;
  }

  void
  EnableCategory(const ConstString &category_name,
                 TypeCategoryMap::Position pos = TypeCategoryMap::Default) {
    EnableCategory(category_name, pos,
                   std::initializer_list<lldb::LanguageType>());
  }

  void EnableCategory(const ConstString &category_name,
                      TypeCategoryMap::Position pos, lldb::LanguageType lang) {
    std::initializer_list<lldb::LanguageType> langs = {lang};
    EnableCategory(category_name, pos, langs);
  }

  void EnableCategory(const ConstString &category_name,
                      TypeCategoryMap::Position pos = TypeCategoryMap::Default,
                      std::initializer_list<lldb::LanguageType> langs = {}) {
    TypeCategoryMap::ValueSP category_sp;
    if (m_categories_map.Get(category_name, category_sp) && category_sp) {
      m_categories_map.Enable(category_sp, pos);
      for (const lldb::LanguageType lang : langs)
        category_sp->AddLanguage(lang);
    }
  }

  void DisableCategory(const ConstString &category_name) {
    m_categories_map.Disable(category_name);
  }

  void
  EnableCategory(const lldb::TypeCategoryImplSP &category,
                 TypeCategoryMap::Position pos = TypeCategoryMap::Default) {
    m_categories_map.Enable(category, pos);
  }

  void DisableCategory(const lldb::TypeCategoryImplSP &category) {
    m_categories_map.Disable(category);
  }

  void EnableAllCategories();

  void DisableAllCategories();

  bool DeleteCategory(const ConstString &category_name) {
    return m_categories_map.Delete(category_name);
  }

  void ClearCategories() { return m_categories_map.Clear(); }

  uint32_t GetCategoriesCount() { return m_categories_map.GetCount(); }

  lldb::TypeCategoryImplSP GetCategoryAtIndex(size_t index) {
    return m_categories_map.GetAtIndex(index);
  }

  void ForEachCategory(TypeCategoryMap::ForEachCallback callback);

  lldb::TypeCategoryImplSP GetCategory(const char *category_name = nullptr,
                                       bool can_create = true) {
    if (!category_name)
      return GetCategory(m_default_category_name);
    return GetCategory(ConstString(category_name));
  }

  lldb::TypeCategoryImplSP GetCategory(const ConstString &category_name,
                                       bool can_create = true);

  lldb::TypeFormatImplSP
  GetFormatForType(lldb::TypeNameSpecifierImplSP type_sp);

  lldb::TypeSummaryImplSP
  GetSummaryForType(lldb::TypeNameSpecifierImplSP type_sp);

  lldb::TypeFilterImplSP
  GetFilterForType(lldb::TypeNameSpecifierImplSP type_sp);

#ifndef LLDB_DISABLE_PYTHON
  lldb::ScriptedSyntheticChildrenSP
  GetSyntheticForType(lldb::TypeNameSpecifierImplSP type_sp);
#endif

#ifndef LLDB_DISABLE_PYTHON
  lldb::SyntheticChildrenSP
  GetSyntheticChildrenForType(lldb::TypeNameSpecifierImplSP type_sp);
#endif

  lldb::TypeValidatorImplSP
  GetValidatorForType(lldb::TypeNameSpecifierImplSP type_sp);

  lldb::TypeFormatImplSP GetFormat(ValueObject &valobj,
                                   lldb::DynamicValueType use_dynamic);

  lldb::TypeSummaryImplSP GetSummaryFormat(ValueObject &valobj,
                                           lldb::DynamicValueType use_dynamic);

#ifndef LLDB_DISABLE_PYTHON
  lldb::SyntheticChildrenSP
  GetSyntheticChildren(ValueObject &valobj, lldb::DynamicValueType use_dynamic);
#endif

  lldb::TypeValidatorImplSP GetValidator(ValueObject &valobj,
                                         lldb::DynamicValueType use_dynamic);

  bool
  AnyMatches(ConstString type_name,
             TypeCategoryImpl::FormatCategoryItems items =
                 TypeCategoryImpl::ALL_ITEM_TYPES,
             bool only_enabled = true, const char **matching_category = nullptr,
             TypeCategoryImpl::FormatCategoryItems *matching_type = nullptr) {
    return m_categories_map.AnyMatches(type_name, items, only_enabled,
                                       matching_category, matching_type);
  }

  static bool GetFormatFromCString(const char *format_cstr,
                                   bool partial_match_ok, lldb::Format &format);

  static char GetFormatAsFormatChar(lldb::Format format);

  static const char *GetFormatAsCString(lldb::Format format);

  // if the user tries to add formatters for, say, "struct Foo" those will not
  // match any type because of the way we strip qualifiers from typenames this
  // method looks for the case where the user is adding a
  // "class","struct","enum" or "union" Foo and strips the unnecessary
  // qualifier
  static ConstString GetValidTypeName(const ConstString &type);

  // when DataExtractor dumps a vectorOfT, it uses a predefined format for each
  // item this method returns it, or eFormatInvalid if vector_format is not a
  // vectorOf
  static lldb::Format GetSingleItemFormat(lldb::Format vector_format);

  // this returns true if the ValueObjectPrinter is *highly encouraged* to
  // actually represent this ValueObject in one-liner format If this object has
  // a summary formatter, however, we should not try and do one-lining, just
  // let the summary do the right thing
  bool ShouldPrintAsOneLiner(ValueObject &valobj);

  void Changed() override;

  uint32_t GetCurrentRevision() override { return m_last_revision; }

  static FormattersMatchVector
  GetPossibleMatches(ValueObject &valobj, lldb::DynamicValueType use_dynamic) {
    FormattersMatchVector matches;
    GetPossibleMatches(valobj, valobj.GetCompilerType(),
                       lldb_private::eFormatterChoiceCriterionDirectChoice,
                       use_dynamic, matches, false, false, false, true);
    return matches;
  }

  static ConstString GetTypeForCache(ValueObject &, lldb::DynamicValueType);

  LanguageCategory *GetCategoryForLanguage(lldb::LanguageType lang_type);

  static std::vector<lldb::LanguageType>
  GetCandidateLanguages(lldb::LanguageType lang_type);

private:
  static std::vector<lldb::LanguageType>
  GetCandidateLanguages(ValueObject &valobj);

  static void GetPossibleMatches(ValueObject &valobj,
                                 CompilerType compiler_type, uint32_t reason,
                                 lldb::DynamicValueType use_dynamic,
                                 FormattersMatchVector &entries,
                                 bool did_strip_ptr, bool did_strip_ref,
                                 bool did_strip_typedef,
                                 bool root_level = false);

  std::atomic<uint32_t> m_last_revision;
  FormatCache m_format_cache;
  std::recursive_mutex m_language_categories_mutex;
  LanguageCategories m_language_categories_map;
  NamedSummariesMap m_named_summaries_map;
  TypeCategoryMap m_categories_map;

  ConstString m_default_category_name;
  ConstString m_system_category_name;
  ConstString m_vectortypes_category_name;

  lldb::TypeFormatImplSP GetHardcodedFormat(FormattersMatchData &);

  lldb::TypeSummaryImplSP GetHardcodedSummaryFormat(FormattersMatchData &);

  lldb::SyntheticChildrenSP
  GetHardcodedSyntheticChildren(FormattersMatchData &);

  lldb::TypeValidatorImplSP GetHardcodedValidator(FormattersMatchData &);

  TypeCategoryMap &GetCategories() { return m_categories_map; }

  // These functions are meant to initialize formatters that are very low-
  // level/global in nature and do not naturally belong in any language. The
  // intent is that most formatters go in language-specific categories.
  // Eventually, the runtimes should also be allowed to vend their own
  // formatters, and then one could put formatters that depend on specific
  // library load events in the language runtimes, on an as-needed basis
  void LoadSystemFormatters();

  void LoadVectorFormatters();

  friend class FormattersMatchData;
};

} // namespace lldb_private

#endif // lldb_FormatManager_h_
