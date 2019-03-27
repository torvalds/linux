//===-- SBTypeSummary.h -------------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBTypeSummary_h_
#define LLDB_SBTypeSummary_h_

#include "lldb/API/SBDefines.h"

namespace lldb {
class LLDB_API SBTypeSummaryOptions {
public:
  SBTypeSummaryOptions();

  SBTypeSummaryOptions(const lldb::SBTypeSummaryOptions &rhs);

  SBTypeSummaryOptions(const lldb_private::TypeSummaryOptions *lldb_object_ptr);

  ~SBTypeSummaryOptions();

  bool IsValid();

  lldb::LanguageType GetLanguage();

  lldb::TypeSummaryCapping GetCapping();

  void SetLanguage(lldb::LanguageType);

  void SetCapping(lldb::TypeSummaryCapping);

protected:
  friend class SBValue;

  lldb_private::TypeSummaryOptions *operator->();

  const lldb_private::TypeSummaryOptions *operator->() const;

  lldb_private::TypeSummaryOptions *get();

  lldb_private::TypeSummaryOptions &ref();

  const lldb_private::TypeSummaryOptions &ref() const;

  void SetOptions(const lldb_private::TypeSummaryOptions *lldb_object_ptr);

private:
  std::unique_ptr<lldb_private::TypeSummaryOptions> m_opaque_ap;
};

class SBTypeSummary {
public:
  SBTypeSummary();

  // Native function summary formatter callback
  typedef bool (*FormatCallback)(SBValue, SBTypeSummaryOptions, SBStream &);

  static SBTypeSummary
  CreateWithSummaryString(const char *data,
                          uint32_t options = 0); // see lldb::eTypeOption values

  static SBTypeSummary
  CreateWithFunctionName(const char *data,
                         uint32_t options = 0); // see lldb::eTypeOption values

  static SBTypeSummary
  CreateWithScriptCode(const char *data,
                       uint32_t options = 0); // see lldb::eTypeOption values

  static SBTypeSummary CreateWithCallback(FormatCallback cb,
                                          uint32_t options = 0,
                                          const char *description = nullptr);

  SBTypeSummary(const lldb::SBTypeSummary &rhs);

  ~SBTypeSummary();

  bool IsValid() const;

  bool IsFunctionCode();

  bool IsFunctionName();

  bool IsSummaryString();

  const char *GetData();

  void SetSummaryString(const char *data);

  void SetFunctionName(const char *data);

  void SetFunctionCode(const char *data);

  uint32_t GetOptions();

  void SetOptions(uint32_t);

  bool GetDescription(lldb::SBStream &description,
                      lldb::DescriptionLevel description_level);

  lldb::SBTypeSummary &operator=(const lldb::SBTypeSummary &rhs);

  bool DoesPrintValue(lldb::SBValue value);

  bool IsEqualTo(lldb::SBTypeSummary &rhs);

  bool operator==(lldb::SBTypeSummary &rhs);

  bool operator!=(lldb::SBTypeSummary &rhs);

protected:
  friend class SBDebugger;
  friend class SBTypeCategory;
  friend class SBValue;

  lldb::TypeSummaryImplSP GetSP();

  void SetSP(const lldb::TypeSummaryImplSP &typefilter_impl_sp);

  lldb::TypeSummaryImplSP m_opaque_sp;

  SBTypeSummary(const lldb::TypeSummaryImplSP &);

  bool CopyOnWrite_Impl();

  bool ChangeSummaryType(bool want_script);
};

} // namespace lldb

#endif // LLDB_SBTypeSummary_h_
