//===-- SBTypeSummary.cpp -----------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBTypeSummary.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBValue.h"
#include "lldb/DataFormatters/DataVisualization.h"

#include "llvm/Support/Casting.h"

using namespace lldb;
using namespace lldb_private;

SBTypeSummaryOptions::SBTypeSummaryOptions() {
  m_opaque_ap.reset(new TypeSummaryOptions());
}

SBTypeSummaryOptions::SBTypeSummaryOptions(
    const lldb::SBTypeSummaryOptions &rhs) {
  if (rhs.m_opaque_ap)
    m_opaque_ap.reset(new TypeSummaryOptions(*rhs.m_opaque_ap));
  else
    m_opaque_ap.reset(new TypeSummaryOptions());
}

SBTypeSummaryOptions::~SBTypeSummaryOptions() {}

bool SBTypeSummaryOptions::IsValid() { return m_opaque_ap.get(); }

lldb::LanguageType SBTypeSummaryOptions::GetLanguage() {
  if (IsValid())
    return m_opaque_ap->GetLanguage();
  return lldb::eLanguageTypeUnknown;
}

lldb::TypeSummaryCapping SBTypeSummaryOptions::GetCapping() {
  if (IsValid())
    return m_opaque_ap->GetCapping();
  return eTypeSummaryCapped;
}

void SBTypeSummaryOptions::SetLanguage(lldb::LanguageType l) {
  if (IsValid())
    m_opaque_ap->SetLanguage(l);
}

void SBTypeSummaryOptions::SetCapping(lldb::TypeSummaryCapping c) {
  if (IsValid())
    m_opaque_ap->SetCapping(c);
}

lldb_private::TypeSummaryOptions *SBTypeSummaryOptions::operator->() {
  return m_opaque_ap.get();
}

const lldb_private::TypeSummaryOptions *SBTypeSummaryOptions::
operator->() const {
  return m_opaque_ap.get();
}

lldb_private::TypeSummaryOptions *SBTypeSummaryOptions::get() {
  return m_opaque_ap.get();
}

lldb_private::TypeSummaryOptions &SBTypeSummaryOptions::ref() {
  return *m_opaque_ap;
}

const lldb_private::TypeSummaryOptions &SBTypeSummaryOptions::ref() const {
  return *m_opaque_ap;
}

SBTypeSummaryOptions::SBTypeSummaryOptions(
    const lldb_private::TypeSummaryOptions *lldb_object_ptr) {
  SetOptions(lldb_object_ptr);
}

void SBTypeSummaryOptions::SetOptions(
    const lldb_private::TypeSummaryOptions *lldb_object_ptr) {
  if (lldb_object_ptr)
    m_opaque_ap.reset(new TypeSummaryOptions(*lldb_object_ptr));
  else
    m_opaque_ap.reset(new TypeSummaryOptions());
}

SBTypeSummary::SBTypeSummary() : m_opaque_sp() {}

SBTypeSummary SBTypeSummary::CreateWithSummaryString(const char *data,
                                                     uint32_t options) {
  if (!data || data[0] == 0)
    return SBTypeSummary();

  return SBTypeSummary(
      TypeSummaryImplSP(new StringSummaryFormat(options, data)));
}

SBTypeSummary SBTypeSummary::CreateWithFunctionName(const char *data,
                                                    uint32_t options) {
  if (!data || data[0] == 0)
    return SBTypeSummary();

  return SBTypeSummary(
      TypeSummaryImplSP(new ScriptSummaryFormat(options, data)));
}

SBTypeSummary SBTypeSummary::CreateWithScriptCode(const char *data,
                                                  uint32_t options) {
  if (!data || data[0] == 0)
    return SBTypeSummary();

  return SBTypeSummary(
      TypeSummaryImplSP(new ScriptSummaryFormat(options, "", data)));
}

SBTypeSummary SBTypeSummary::CreateWithCallback(FormatCallback cb,
                                                uint32_t options,
                                                const char *description) {
  SBTypeSummary retval;
  if (cb) {
    retval.SetSP(TypeSummaryImplSP(new CXXFunctionSummaryFormat(
        options,
        [cb](ValueObject &valobj, Stream &stm,
             const TypeSummaryOptions &opt) -> bool {
          SBStream stream;
          SBValue sb_value(valobj.GetSP());
          SBTypeSummaryOptions options(&opt);
          if (!cb(sb_value, options, stream))
            return false;
          stm.Write(stream.GetData(), stream.GetSize());
          return true;
        },
        description ? description : "callback summary formatter")));
  }

  return retval;
}

SBTypeSummary::SBTypeSummary(const lldb::SBTypeSummary &rhs)
    : m_opaque_sp(rhs.m_opaque_sp) {}

SBTypeSummary::~SBTypeSummary() {}

bool SBTypeSummary::IsValid() const { return m_opaque_sp.get() != NULL; }

bool SBTypeSummary::IsFunctionCode() {
  if (!IsValid())
    return false;
  if (ScriptSummaryFormat *script_summary_ptr =
          llvm::dyn_cast<ScriptSummaryFormat>(m_opaque_sp.get())) {
    const char *ftext = script_summary_ptr->GetPythonScript();
    return (ftext && *ftext != 0);
  }
  return false;
}

bool SBTypeSummary::IsFunctionName() {
  if (!IsValid())
    return false;
  if (ScriptSummaryFormat *script_summary_ptr =
          llvm::dyn_cast<ScriptSummaryFormat>(m_opaque_sp.get())) {
    const char *ftext = script_summary_ptr->GetPythonScript();
    return (!ftext || *ftext == 0);
  }
  return false;
}

bool SBTypeSummary::IsSummaryString() {
  if (!IsValid())
    return false;

  return m_opaque_sp->GetKind() == TypeSummaryImpl::Kind::eSummaryString;
}

const char *SBTypeSummary::GetData() {
  if (!IsValid())
    return NULL;
  if (ScriptSummaryFormat *script_summary_ptr =
          llvm::dyn_cast<ScriptSummaryFormat>(m_opaque_sp.get())) {
    const char *fname = script_summary_ptr->GetFunctionName();
    const char *ftext = script_summary_ptr->GetPythonScript();
    if (ftext && *ftext)
      return ftext;
    return fname;
  } else if (StringSummaryFormat *string_summary_ptr =
                 llvm::dyn_cast<StringSummaryFormat>(m_opaque_sp.get()))
    return string_summary_ptr->GetSummaryString();
  return nullptr;
}

uint32_t SBTypeSummary::GetOptions() {
  if (!IsValid())
    return lldb::eTypeOptionNone;
  return m_opaque_sp->GetOptions();
}

void SBTypeSummary::SetOptions(uint32_t value) {
  if (!CopyOnWrite_Impl())
    return;
  m_opaque_sp->SetOptions(value);
}

void SBTypeSummary::SetSummaryString(const char *data) {
  if (!IsValid())
    return;
  if (!llvm::isa<StringSummaryFormat>(m_opaque_sp.get()))
    ChangeSummaryType(false);
  if (StringSummaryFormat *string_summary_ptr =
          llvm::dyn_cast<StringSummaryFormat>(m_opaque_sp.get()))
    string_summary_ptr->SetSummaryString(data);
}

void SBTypeSummary::SetFunctionName(const char *data) {
  if (!IsValid())
    return;
  if (!llvm::isa<ScriptSummaryFormat>(m_opaque_sp.get()))
    ChangeSummaryType(true);
  if (ScriptSummaryFormat *script_summary_ptr =
          llvm::dyn_cast<ScriptSummaryFormat>(m_opaque_sp.get()))
    script_summary_ptr->SetFunctionName(data);
}

void SBTypeSummary::SetFunctionCode(const char *data) {
  if (!IsValid())
    return;
  if (!llvm::isa<ScriptSummaryFormat>(m_opaque_sp.get()))
    ChangeSummaryType(true);
  if (ScriptSummaryFormat *script_summary_ptr =
          llvm::dyn_cast<ScriptSummaryFormat>(m_opaque_sp.get()))
    script_summary_ptr->SetPythonScript(data);
}

bool SBTypeSummary::GetDescription(lldb::SBStream &description,
                                   lldb::DescriptionLevel description_level) {
  if (!CopyOnWrite_Impl())
    return false;
  else {
    description.Printf("%s\n", m_opaque_sp->GetDescription().c_str());
    return true;
  }
}

bool SBTypeSummary::DoesPrintValue(lldb::SBValue value) {
  if (!IsValid())
    return false;
  lldb::ValueObjectSP value_sp = value.GetSP();
  return m_opaque_sp->DoesPrintValue(value_sp.get());
}

lldb::SBTypeSummary &SBTypeSummary::operator=(const lldb::SBTypeSummary &rhs) {
  if (this != &rhs) {
    m_opaque_sp = rhs.m_opaque_sp;
  }
  return *this;
}

bool SBTypeSummary::operator==(lldb::SBTypeSummary &rhs) {
  if (!IsValid())
    return !rhs.IsValid();
  return m_opaque_sp == rhs.m_opaque_sp;
}

bool SBTypeSummary::IsEqualTo(lldb::SBTypeSummary &rhs) {
  if (IsValid()) {
    // valid and invalid are different
    if (!rhs.IsValid())
      return false;
  } else {
    // invalid and valid are different
    if (rhs.IsValid())
      return false;
    else
      // both invalid are the same
      return true;
  }

  if (m_opaque_sp->GetKind() != rhs.m_opaque_sp->GetKind())
    return false;

  switch (m_opaque_sp->GetKind()) {
  case TypeSummaryImpl::Kind::eCallback:
    return llvm::dyn_cast<CXXFunctionSummaryFormat>(m_opaque_sp.get()) ==
           llvm::dyn_cast<CXXFunctionSummaryFormat>(rhs.m_opaque_sp.get());
  case TypeSummaryImpl::Kind::eScript:
    if (IsFunctionCode() != rhs.IsFunctionCode())
      return false;
    if (IsFunctionName() != rhs.IsFunctionName())
      return false;
    return GetOptions() == rhs.GetOptions();
  case TypeSummaryImpl::Kind::eSummaryString:
    if (IsSummaryString() != rhs.IsSummaryString())
      return false;
    return GetOptions() == rhs.GetOptions();
  case TypeSummaryImpl::Kind::eInternal:
    return (m_opaque_sp.get() == rhs.m_opaque_sp.get());
  }

  return false;
}

bool SBTypeSummary::operator!=(lldb::SBTypeSummary &rhs) {
  if (!IsValid())
    return !rhs.IsValid();
  return m_opaque_sp != rhs.m_opaque_sp;
}

lldb::TypeSummaryImplSP SBTypeSummary::GetSP() { return m_opaque_sp; }

void SBTypeSummary::SetSP(const lldb::TypeSummaryImplSP &typesummary_impl_sp) {
  m_opaque_sp = typesummary_impl_sp;
}

SBTypeSummary::SBTypeSummary(const lldb::TypeSummaryImplSP &typesummary_impl_sp)
    : m_opaque_sp(typesummary_impl_sp) {}

bool SBTypeSummary::CopyOnWrite_Impl() {
  if (!IsValid())
    return false;

  if (m_opaque_sp.unique())
    return true;

  TypeSummaryImplSP new_sp;

  if (CXXFunctionSummaryFormat *current_summary_ptr =
          llvm::dyn_cast<CXXFunctionSummaryFormat>(m_opaque_sp.get())) {
    new_sp = TypeSummaryImplSP(new CXXFunctionSummaryFormat(
        GetOptions(), current_summary_ptr->m_impl,
        current_summary_ptr->m_description.c_str()));
  } else if (ScriptSummaryFormat *current_summary_ptr =
                 llvm::dyn_cast<ScriptSummaryFormat>(m_opaque_sp.get())) {
    new_sp = TypeSummaryImplSP(new ScriptSummaryFormat(
        GetOptions(), current_summary_ptr->GetFunctionName(),
        current_summary_ptr->GetPythonScript()));
  } else if (StringSummaryFormat *current_summary_ptr =
                 llvm::dyn_cast<StringSummaryFormat>(m_opaque_sp.get())) {
    new_sp = TypeSummaryImplSP(new StringSummaryFormat(
        GetOptions(), current_summary_ptr->GetSummaryString()));
  }

  SetSP(new_sp);

  return nullptr != new_sp.get();
}

bool SBTypeSummary::ChangeSummaryType(bool want_script) {
  if (!IsValid())
    return false;

  TypeSummaryImplSP new_sp;

  if (want_script ==
      (m_opaque_sp->GetKind() == TypeSummaryImpl::Kind::eScript)) {
    if (m_opaque_sp->GetKind() ==
            lldb_private::TypeSummaryImpl::Kind::eCallback &&
        !want_script)
      new_sp = TypeSummaryImplSP(new StringSummaryFormat(GetOptions(), ""));
    else
      return CopyOnWrite_Impl();
  }

  if (!new_sp) {
    if (want_script)
      new_sp = TypeSummaryImplSP(new ScriptSummaryFormat(GetOptions(), "", ""));
    else
      new_sp = TypeSummaryImplSP(new StringSummaryFormat(GetOptions(), ""));
  }

  SetSP(new_sp);

  return true;
}
