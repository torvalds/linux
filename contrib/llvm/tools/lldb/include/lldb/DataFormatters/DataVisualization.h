//===-- DataVisualization.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_DataVisualization_h_
#define lldb_DataVisualization_h_


#include "lldb/DataFormatters/FormatClasses.h"
#include "lldb/DataFormatters/FormatManager.h"
#include "lldb/Utility/ConstString.h"

namespace lldb_private {

// this class is the high-level front-end of LLDB Data Visualization code in
// FormatManager.h/cpp is the low-level implementation of this feature clients
// should refer to this class as the entry-point into the data formatters
// unless they have a good reason to bypass this and go to the backend
class DataVisualization {
public:
  // use this call to force the FM to consider itself updated even when there
  // is no apparent reason for that
  static void ForceUpdate();

  static uint32_t GetCurrentRevision();

  static bool ShouldPrintAsOneLiner(ValueObject &valobj);

  static lldb::TypeFormatImplSP GetFormat(ValueObject &valobj,
                                          lldb::DynamicValueType use_dynamic);

  static lldb::TypeFormatImplSP
  GetFormatForType(lldb::TypeNameSpecifierImplSP type_sp);

  static lldb::TypeSummaryImplSP
  GetSummaryFormat(ValueObject &valobj, lldb::DynamicValueType use_dynamic);

  static lldb::TypeSummaryImplSP
  GetSummaryForType(lldb::TypeNameSpecifierImplSP type_sp);

#ifndef LLDB_DISABLE_PYTHON
  static lldb::SyntheticChildrenSP
  GetSyntheticChildrenForType(lldb::TypeNameSpecifierImplSP type_sp);
#endif

  static lldb::TypeFilterImplSP
  GetFilterForType(lldb::TypeNameSpecifierImplSP type_sp);

#ifndef LLDB_DISABLE_PYTHON
  static lldb::ScriptedSyntheticChildrenSP
  GetSyntheticForType(lldb::TypeNameSpecifierImplSP type_sp);
#endif

#ifndef LLDB_DISABLE_PYTHON
  static lldb::SyntheticChildrenSP
  GetSyntheticChildren(ValueObject &valobj, lldb::DynamicValueType use_dynamic);
#endif

  static lldb::TypeValidatorImplSP
  GetValidator(ValueObject &valobj, lldb::DynamicValueType use_dynamic);

  static lldb::TypeValidatorImplSP
  GetValidatorForType(lldb::TypeNameSpecifierImplSP type_sp);

  static bool
  AnyMatches(ConstString type_name,
             TypeCategoryImpl::FormatCategoryItems items =
                 TypeCategoryImpl::ALL_ITEM_TYPES,
             bool only_enabled = true, const char **matching_category = nullptr,
             TypeCategoryImpl::FormatCategoryItems *matching_type = nullptr);

  class NamedSummaryFormats {
  public:
    static bool GetSummaryFormat(const ConstString &type,
                                 lldb::TypeSummaryImplSP &entry);

    static void Add(const ConstString &type,
                    const lldb::TypeSummaryImplSP &entry);

    static bool Delete(const ConstString &type);

    static void Clear();

    static void
    ForEach(std::function<bool(ConstString, const lldb::TypeSummaryImplSP &)>
                callback);

    static uint32_t GetCount();
  };

  class Categories {
  public:
    static bool GetCategory(const ConstString &category,
                            lldb::TypeCategoryImplSP &entry,
                            bool allow_create = true);

    static bool GetCategory(lldb::LanguageType language,
                            lldb::TypeCategoryImplSP &entry);

    static void Add(const ConstString &category);

    static bool Delete(const ConstString &category);

    static void Clear();

    static void Clear(const ConstString &category);

    static void Enable(const ConstString &category,
                       TypeCategoryMap::Position = TypeCategoryMap::Default);

    static void Enable(lldb::LanguageType lang_type);

    static void Disable(const ConstString &category);

    static void Disable(lldb::LanguageType lang_type);

    static void Enable(const lldb::TypeCategoryImplSP &category,
                       TypeCategoryMap::Position = TypeCategoryMap::Default);

    static void Disable(const lldb::TypeCategoryImplSP &category);

    static void EnableStar();

    static void DisableStar();

    static void ForEach(TypeCategoryMap::ForEachCallback callback);

    static uint32_t GetCount();

    static lldb::TypeCategoryImplSP GetCategoryAtIndex(size_t);
  };
};

} // namespace lldb_private

#endif // lldb_DataVisualization_h_
