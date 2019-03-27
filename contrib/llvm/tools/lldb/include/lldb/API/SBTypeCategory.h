//===-- SBTypeCategory.h --------------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBTypeCategory_h_
#define LLDB_SBTypeCategory_h_

#include "lldb/API/SBDefines.h"

namespace lldb {

class LLDB_API SBTypeCategory {
public:
  SBTypeCategory();

  SBTypeCategory(const lldb::SBTypeCategory &rhs);

  ~SBTypeCategory();

  bool IsValid() const;

  bool GetEnabled();

  void SetEnabled(bool);

  const char *GetName();

  lldb::LanguageType GetLanguageAtIndex(uint32_t idx);

  uint32_t GetNumLanguages();

  void AddLanguage(lldb::LanguageType language);

  bool GetDescription(lldb::SBStream &description,
                      lldb::DescriptionLevel description_level);

  uint32_t GetNumFormats();

  uint32_t GetNumSummaries();

  uint32_t GetNumFilters();

#ifndef LLDB_DISABLE_PYTHON
  uint32_t GetNumSynthetics();
#endif

  SBTypeNameSpecifier GetTypeNameSpecifierForFilterAtIndex(uint32_t);

  SBTypeNameSpecifier GetTypeNameSpecifierForFormatAtIndex(uint32_t);

  SBTypeNameSpecifier GetTypeNameSpecifierForSummaryAtIndex(uint32_t);

#ifndef LLDB_DISABLE_PYTHON
  SBTypeNameSpecifier GetTypeNameSpecifierForSyntheticAtIndex(uint32_t);
#endif

  SBTypeFilter GetFilterForType(SBTypeNameSpecifier);

  SBTypeFormat GetFormatForType(SBTypeNameSpecifier);

#ifndef LLDB_DISABLE_PYTHON
  SBTypeSummary GetSummaryForType(SBTypeNameSpecifier);
#endif

#ifndef LLDB_DISABLE_PYTHON
  SBTypeSynthetic GetSyntheticForType(SBTypeNameSpecifier);
#endif

#ifndef LLDB_DISABLE_PYTHON
  SBTypeFilter GetFilterAtIndex(uint32_t);
#endif

  SBTypeFormat GetFormatAtIndex(uint32_t);

#ifndef LLDB_DISABLE_PYTHON
  SBTypeSummary GetSummaryAtIndex(uint32_t);
#endif

#ifndef LLDB_DISABLE_PYTHON
  SBTypeSynthetic GetSyntheticAtIndex(uint32_t);
#endif

  bool AddTypeFormat(SBTypeNameSpecifier, SBTypeFormat);

  bool DeleteTypeFormat(SBTypeNameSpecifier);

#ifndef LLDB_DISABLE_PYTHON
  bool AddTypeSummary(SBTypeNameSpecifier, SBTypeSummary);
#endif

  bool DeleteTypeSummary(SBTypeNameSpecifier);

  bool AddTypeFilter(SBTypeNameSpecifier, SBTypeFilter);

  bool DeleteTypeFilter(SBTypeNameSpecifier);

#ifndef LLDB_DISABLE_PYTHON
  bool AddTypeSynthetic(SBTypeNameSpecifier, SBTypeSynthetic);

  bool DeleteTypeSynthetic(SBTypeNameSpecifier);
#endif

  lldb::SBTypeCategory &operator=(const lldb::SBTypeCategory &rhs);

  bool operator==(lldb::SBTypeCategory &rhs);

  bool operator!=(lldb::SBTypeCategory &rhs);

protected:
  friend class SBDebugger;

  lldb::TypeCategoryImplSP GetSP();

  void SetSP(const lldb::TypeCategoryImplSP &typecategory_impl_sp);

  TypeCategoryImplSP m_opaque_sp;

  SBTypeCategory(const lldb::TypeCategoryImplSP &);

  SBTypeCategory(const char *);

  bool IsDefaultCategory();
};

} // namespace lldb

#endif // LLDB_SBTypeCategory_h_
