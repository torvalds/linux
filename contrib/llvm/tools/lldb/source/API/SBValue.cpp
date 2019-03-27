//===-- SBValue.cpp ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBValue.h"

#include "lldb/API/SBDeclaration.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBTypeFilter.h"
#include "lldb/API/SBTypeFormat.h"
#include "lldb/API/SBTypeSummary.h"
#include "lldb/API/SBTypeSynthetic.h"

#include "lldb/Breakpoint/Watchpoint.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/Section.h"
#include "lldb/Core/StreamFile.h"
#include "lldb/Core/Value.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/DataFormatters/DataVisualization.h"
#include "lldb/Symbol/Block.h"
#include "lldb/Symbol/Declaration.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Symbol/Variable.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Scalar.h"
#include "lldb/Utility/Stream.h"

#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBExpressionOptions.h"
#include "lldb/API/SBFrame.h"
#include "lldb/API/SBProcess.h"
#include "lldb/API/SBTarget.h"
#include "lldb/API/SBThread.h"

using namespace lldb;
using namespace lldb_private;

class ValueImpl {
public:
  ValueImpl() {}

  ValueImpl(lldb::ValueObjectSP in_valobj_sp,
            lldb::DynamicValueType use_dynamic, bool use_synthetic,
            const char *name = NULL)
      : m_valobj_sp(), m_use_dynamic(use_dynamic),
        m_use_synthetic(use_synthetic), m_name(name) {
    if (in_valobj_sp) {
      if ((m_valobj_sp = in_valobj_sp->GetQualifiedRepresentationIfAvailable(
               lldb::eNoDynamicValues, false))) {
        if (!m_name.IsEmpty())
          m_valobj_sp->SetName(m_name);
      }
    }
  }

  ValueImpl(const ValueImpl &rhs)
      : m_valobj_sp(rhs.m_valobj_sp), m_use_dynamic(rhs.m_use_dynamic),
        m_use_synthetic(rhs.m_use_synthetic), m_name(rhs.m_name) {}

  ValueImpl &operator=(const ValueImpl &rhs) {
    if (this != &rhs) {
      m_valobj_sp = rhs.m_valobj_sp;
      m_use_dynamic = rhs.m_use_dynamic;
      m_use_synthetic = rhs.m_use_synthetic;
      m_name = rhs.m_name;
    }
    return *this;
  }

  bool IsValid() {
    if (m_valobj_sp.get() == NULL)
      return false;
    else {
      // FIXME: This check is necessary but not sufficient.  We for sure don't
      // want to touch SBValues whose owning
      // targets have gone away.  This check is a little weak in that it
      // enforces that restriction when you call IsValid, but since IsValid
      // doesn't lock the target, you have no guarantee that the SBValue won't
      // go invalid after you call this... Also, an SBValue could depend on
      // data from one of the modules in the target, and those could go away
      // independently of the target, for instance if a module is unloaded.
      // But right now, neither SBValues nor ValueObjects know which modules
      // they depend on.  So I have no good way to make that check without
      // tracking that in all the ValueObject subclasses.
      TargetSP target_sp = m_valobj_sp->GetTargetSP();
      return target_sp && target_sp->IsValid();
    }
  }

  lldb::ValueObjectSP GetRootSP() { return m_valobj_sp; }

  lldb::ValueObjectSP GetSP(Process::StopLocker &stop_locker,
                            std::unique_lock<std::recursive_mutex> &lock,
                            Status &error) {
    Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
    if (!m_valobj_sp) {
      error.SetErrorString("invalid value object");
      return m_valobj_sp;
    }

    lldb::ValueObjectSP value_sp = m_valobj_sp;

    Target *target = value_sp->GetTargetSP().get();
    if (!target)
      return ValueObjectSP();

    lock = std::unique_lock<std::recursive_mutex>(target->GetAPIMutex());

    ProcessSP process_sp(value_sp->GetProcessSP());
    if (process_sp && !stop_locker.TryLock(&process_sp->GetRunLock())) {
      // We don't allow people to play around with ValueObject if the process
      // is running. If you want to look at values, pause the process, then
      // look.
      if (log)
        log->Printf("SBValue(%p)::GetSP() => error: process is running",
                    static_cast<void *>(value_sp.get()));
      error.SetErrorString("process must be stopped.");
      return ValueObjectSP();
    }

    if (m_use_dynamic != eNoDynamicValues) {
      ValueObjectSP dynamic_sp = value_sp->GetDynamicValue(m_use_dynamic);
      if (dynamic_sp)
        value_sp = dynamic_sp;
    }

    if (m_use_synthetic) {
      ValueObjectSP synthetic_sp = value_sp->GetSyntheticValue(m_use_synthetic);
      if (synthetic_sp)
        value_sp = synthetic_sp;
    }

    if (!value_sp)
      error.SetErrorString("invalid value object");
    if (!m_name.IsEmpty())
      value_sp->SetName(m_name);

    return value_sp;
  }

  void SetUseDynamic(lldb::DynamicValueType use_dynamic) {
    m_use_dynamic = use_dynamic;
  }

  void SetUseSynthetic(bool use_synthetic) { m_use_synthetic = use_synthetic; }

  lldb::DynamicValueType GetUseDynamic() { return m_use_dynamic; }

  bool GetUseSynthetic() { return m_use_synthetic; }

  // All the derived values that we would make from the m_valobj_sp will share
  // the ExecutionContext with m_valobj_sp, so we don't need to do the
  // calculations in GetSP to return the Target, Process, Thread or Frame.  It
  // is convenient to provide simple accessors for these, which I do here.
  TargetSP GetTargetSP() {
    if (m_valobj_sp)
      return m_valobj_sp->GetTargetSP();
    else
      return TargetSP();
  }

  ProcessSP GetProcessSP() {
    if (m_valobj_sp)
      return m_valobj_sp->GetProcessSP();
    else
      return ProcessSP();
  }

  ThreadSP GetThreadSP() {
    if (m_valobj_sp)
      return m_valobj_sp->GetThreadSP();
    else
      return ThreadSP();
  }

  StackFrameSP GetFrameSP() {
    if (m_valobj_sp)
      return m_valobj_sp->GetFrameSP();
    else
      return StackFrameSP();
  }

private:
  lldb::ValueObjectSP m_valobj_sp;
  lldb::DynamicValueType m_use_dynamic;
  bool m_use_synthetic;
  ConstString m_name;
};

class ValueLocker {
public:
  ValueLocker() {}

  ValueObjectSP GetLockedSP(ValueImpl &in_value) {
    return in_value.GetSP(m_stop_locker, m_lock, m_lock_error);
  }

  Status &GetError() { return m_lock_error; }

private:
  Process::StopLocker m_stop_locker;
  std::unique_lock<std::recursive_mutex> m_lock;
  Status m_lock_error;
};

SBValue::SBValue() : m_opaque_sp() {}

SBValue::SBValue(const lldb::ValueObjectSP &value_sp) { SetSP(value_sp); }

SBValue::SBValue(const SBValue &rhs) { SetSP(rhs.m_opaque_sp); }

SBValue &SBValue::operator=(const SBValue &rhs) {
  if (this != &rhs) {
    SetSP(rhs.m_opaque_sp);
  }
  return *this;
}

SBValue::~SBValue() {}

bool SBValue::IsValid() {
  // If this function ever changes to anything that does more than just check
  // if the opaque shared pointer is non NULL, then we need to update all "if
  // (m_opaque_sp)" code in this file.
  return m_opaque_sp.get() != NULL && m_opaque_sp->IsValid() &&
         m_opaque_sp->GetRootSP().get() != NULL;
}

void SBValue::Clear() { m_opaque_sp.reset(); }

SBError SBValue::GetError() {
  SBError sb_error;

  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp)
    sb_error.SetError(value_sp->GetError());
  else
    sb_error.SetErrorStringWithFormat("error: %s",
                                      locker.GetError().AsCString());

  return sb_error;
}

user_id_t SBValue::GetID() {
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp)
    return value_sp->GetID();
  return LLDB_INVALID_UID;
}

const char *SBValue::GetName() {
  const char *name = NULL;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp)
    name = value_sp->GetName().GetCString();

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log) {
    if (name)
      log->Printf("SBValue(%p)::GetName () => \"%s\"",
                  static_cast<void *>(value_sp.get()), name);
    else
      log->Printf("SBValue(%p)::GetName () => NULL",
                  static_cast<void *>(value_sp.get()));
  }

  return name;
}

const char *SBValue::GetTypeName() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  const char *name = NULL;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    name = value_sp->GetQualifiedTypeName().GetCString();
  }

  if (log) {
    if (name)
      log->Printf("SBValue(%p)::GetTypeName () => \"%s\"",
                  static_cast<void *>(value_sp.get()), name);
    else
      log->Printf("SBValue(%p)::GetTypeName () => NULL",
                  static_cast<void *>(value_sp.get()));
  }

  return name;
}

const char *SBValue::GetDisplayTypeName() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  const char *name = NULL;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    name = value_sp->GetDisplayTypeName().GetCString();
  }

  if (log) {
    if (name)
      log->Printf("SBValue(%p)::GetTypeName () => \"%s\"",
                  static_cast<void *>(value_sp.get()), name);
    else
      log->Printf("SBValue(%p)::GetTypeName () => NULL",
                  static_cast<void *>(value_sp.get()));
  }

  return name;
}

size_t SBValue::GetByteSize() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  size_t result = 0;

  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    result = value_sp->GetByteSize();
  }

  if (log)
    log->Printf("SBValue(%p)::GetByteSize () => %" PRIu64,
                static_cast<void *>(value_sp.get()),
                static_cast<uint64_t>(result));

  return result;
}

bool SBValue::IsInScope() {
  bool result = false;

  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    result = value_sp->IsInScope();
  }

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf("SBValue(%p)::IsInScope () => %i",
                static_cast<void *>(value_sp.get()), result);

  return result;
}

const char *SBValue::GetValue() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  const char *cstr = NULL;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    cstr = value_sp->GetValueAsCString();
  }
  if (log) {
    if (cstr)
      log->Printf("SBValue(%p)::GetValue() => \"%s\"",
                  static_cast<void *>(value_sp.get()), cstr);
    else
      log->Printf("SBValue(%p)::GetValue() => NULL",
                  static_cast<void *>(value_sp.get()));
  }

  return cstr;
}

ValueType SBValue::GetValueType() {
  ValueType result = eValueTypeInvalid;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp)
    result = value_sp->GetValueType();

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log) {
    switch (result) {
    case eValueTypeInvalid:
      log->Printf("SBValue(%p)::GetValueType () => eValueTypeInvalid",
                  static_cast<void *>(value_sp.get()));
      break;
    case eValueTypeVariableGlobal:
      log->Printf("SBValue(%p)::GetValueType () => eValueTypeVariableGlobal",
                  static_cast<void *>(value_sp.get()));
      break;
    case eValueTypeVariableStatic:
      log->Printf("SBValue(%p)::GetValueType () => eValueTypeVariableStatic",
                  static_cast<void *>(value_sp.get()));
      break;
    case eValueTypeVariableArgument:
      log->Printf("SBValue(%p)::GetValueType () => eValueTypeVariableArgument",
                  static_cast<void *>(value_sp.get()));
      break;
    case eValueTypeVariableLocal:
      log->Printf("SBValue(%p)::GetValueType () => eValueTypeVariableLocal",
                  static_cast<void *>(value_sp.get()));
      break;
    case eValueTypeRegister:
      log->Printf("SBValue(%p)::GetValueType () => eValueTypeRegister",
                  static_cast<void *>(value_sp.get()));
      break;
    case eValueTypeRegisterSet:
      log->Printf("SBValue(%p)::GetValueType () => eValueTypeRegisterSet",
                  static_cast<void *>(value_sp.get()));
      break;
    case eValueTypeConstResult:
      log->Printf("SBValue(%p)::GetValueType () => eValueTypeConstResult",
                  static_cast<void *>(value_sp.get()));
      break;
    case eValueTypeVariableThreadLocal:
      log->Printf(
          "SBValue(%p)::GetValueType () => eValueTypeVariableThreadLocal",
          static_cast<void *>(value_sp.get()));
      break;
    }
  }
  return result;
}

const char *SBValue::GetObjectDescription() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  const char *cstr = NULL;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    cstr = value_sp->GetObjectDescription();
  }
  if (log) {
    if (cstr)
      log->Printf("SBValue(%p)::GetObjectDescription() => \"%s\"",
                  static_cast<void *>(value_sp.get()), cstr);
    else
      log->Printf("SBValue(%p)::GetObjectDescription() => NULL",
                  static_cast<void *>(value_sp.get()));
  }
  return cstr;
}

const char *SBValue::GetTypeValidatorResult() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  const char *cstr = NULL;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    const auto &validation(value_sp->GetValidationStatus());
    if (TypeValidatorResult::Failure == validation.first) {
      if (validation.second.empty())
        cstr = "unknown error";
      else
        cstr = validation.second.c_str();
    }
  }
  if (log) {
    if (cstr)
      log->Printf("SBValue(%p)::GetTypeValidatorResult() => \"%s\"",
                  static_cast<void *>(value_sp.get()), cstr);
    else
      log->Printf("SBValue(%p)::GetTypeValidatorResult() => NULL",
                  static_cast<void *>(value_sp.get()));
  }
  return cstr;
}

SBType SBValue::GetType() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  SBType sb_type;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  TypeImplSP type_sp;
  if (value_sp) {
    type_sp.reset(new TypeImpl(value_sp->GetTypeImpl()));
    sb_type.SetSP(type_sp);
  }
  if (log) {
    if (type_sp)
      log->Printf("SBValue(%p)::GetType => SBType(%p)",
                  static_cast<void *>(value_sp.get()),
                  static_cast<void *>(type_sp.get()));
    else
      log->Printf("SBValue(%p)::GetType => NULL",
                  static_cast<void *>(value_sp.get()));
  }
  return sb_type;
}

bool SBValue::GetValueDidChange() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  bool result = false;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    if (value_sp->UpdateValueIfNeeded(false))
      result = value_sp->GetValueDidChange();
  }
  if (log)
    log->Printf("SBValue(%p)::GetValueDidChange() => %i",
                static_cast<void *>(value_sp.get()), result);

  return result;
}

const char *SBValue::GetSummary() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  const char *cstr = NULL;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    cstr = value_sp->GetSummaryAsCString();
  }
  if (log) {
    if (cstr)
      log->Printf("SBValue(%p)::GetSummary() => \"%s\"",
                  static_cast<void *>(value_sp.get()), cstr);
    else
      log->Printf("SBValue(%p)::GetSummary() => NULL",
                  static_cast<void *>(value_sp.get()));
  }
  return cstr;
}

const char *SBValue::GetSummary(lldb::SBStream &stream,
                                lldb::SBTypeSummaryOptions &options) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    std::string buffer;
    if (value_sp->GetSummaryAsCString(buffer, options.ref()) && !buffer.empty())
      stream.Printf("%s", buffer.c_str());
  }
  const char *cstr = stream.GetData();
  if (log) {
    if (cstr)
      log->Printf("SBValue(%p)::GetSummary() => \"%s\"",
                  static_cast<void *>(value_sp.get()), cstr);
    else
      log->Printf("SBValue(%p)::GetSummary() => NULL",
                  static_cast<void *>(value_sp.get()));
  }
  return cstr;
}

const char *SBValue::GetLocation() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  const char *cstr = NULL;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    cstr = value_sp->GetLocationAsCString();
  }
  if (log) {
    if (cstr)
      log->Printf("SBValue(%p)::GetLocation() => \"%s\"",
                  static_cast<void *>(value_sp.get()), cstr);
    else
      log->Printf("SBValue(%p)::GetLocation() => NULL",
                  static_cast<void *>(value_sp.get()));
  }
  return cstr;
}

// Deprecated - use the one that takes an lldb::SBError
bool SBValue::SetValueFromCString(const char *value_str) {
  lldb::SBError dummy;
  return SetValueFromCString(value_str, dummy);
}

bool SBValue::SetValueFromCString(const char *value_str, lldb::SBError &error) {
  bool success = false;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (value_sp) {
    success = value_sp->SetValueFromCString(value_str, error.ref());
  } else
    error.SetErrorStringWithFormat("Could not get value: %s",
                                   locker.GetError().AsCString());

  if (log)
    log->Printf("SBValue(%p)::SetValueFromCString(\"%s\") => %i",
                static_cast<void *>(value_sp.get()), value_str, success);

  return success;
}

lldb::SBTypeFormat SBValue::GetTypeFormat() {
  lldb::SBTypeFormat format;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    if (value_sp->UpdateValueIfNeeded(true)) {
      lldb::TypeFormatImplSP format_sp = value_sp->GetValueFormat();
      if (format_sp)
        format.SetSP(format_sp);
    }
  }
  return format;
}

lldb::SBTypeSummary SBValue::GetTypeSummary() {
  lldb::SBTypeSummary summary;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    if (value_sp->UpdateValueIfNeeded(true)) {
      lldb::TypeSummaryImplSP summary_sp = value_sp->GetSummaryFormat();
      if (summary_sp)
        summary.SetSP(summary_sp);
    }
  }
  return summary;
}

lldb::SBTypeFilter SBValue::GetTypeFilter() {
  lldb::SBTypeFilter filter;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    if (value_sp->UpdateValueIfNeeded(true)) {
      lldb::SyntheticChildrenSP synthetic_sp = value_sp->GetSyntheticChildren();

      if (synthetic_sp && !synthetic_sp->IsScripted()) {
        TypeFilterImplSP filter_sp =
            std::static_pointer_cast<TypeFilterImpl>(synthetic_sp);
        filter.SetSP(filter_sp);
      }
    }
  }
  return filter;
}

#ifndef LLDB_DISABLE_PYTHON
lldb::SBTypeSynthetic SBValue::GetTypeSynthetic() {
  lldb::SBTypeSynthetic synthetic;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    if (value_sp->UpdateValueIfNeeded(true)) {
      lldb::SyntheticChildrenSP children_sp = value_sp->GetSyntheticChildren();

      if (children_sp && children_sp->IsScripted()) {
        ScriptedSyntheticChildrenSP synth_sp =
            std::static_pointer_cast<ScriptedSyntheticChildren>(children_sp);
        synthetic.SetSP(synth_sp);
      }
    }
  }
  return synthetic;
}
#endif

lldb::SBValue SBValue::CreateChildAtOffset(const char *name, uint32_t offset,
                                           SBType type) {
  lldb::SBValue sb_value;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  lldb::ValueObjectSP new_value_sp;
  if (value_sp) {
    TypeImplSP type_sp(type.GetSP());
    if (type.IsValid()) {
      sb_value.SetSP(value_sp->GetSyntheticChildAtOffset(
                         offset, type_sp->GetCompilerType(false), true),
                     GetPreferDynamicValue(), GetPreferSyntheticValue(), name);
    }
  }
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log) {
    if (new_value_sp)
      log->Printf("SBValue(%p)::CreateChildAtOffset => \"%s\"",
                  static_cast<void *>(value_sp.get()),
                  new_value_sp->GetName().AsCString());
    else
      log->Printf("SBValue(%p)::CreateChildAtOffset => NULL",
                  static_cast<void *>(value_sp.get()));
  }
  return sb_value;
}

lldb::SBValue SBValue::Cast(SBType type) {
  lldb::SBValue sb_value;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  TypeImplSP type_sp(type.GetSP());
  if (value_sp && type_sp)
    sb_value.SetSP(value_sp->Cast(type_sp->GetCompilerType(false)),
                   GetPreferDynamicValue(), GetPreferSyntheticValue());
  return sb_value;
}

lldb::SBValue SBValue::CreateValueFromExpression(const char *name,
                                                 const char *expression) {
  SBExpressionOptions options;
  options.ref().SetKeepInMemory(true);
  return CreateValueFromExpression(name, expression, options);
}

lldb::SBValue SBValue::CreateValueFromExpression(const char *name,
                                                 const char *expression,
                                                 SBExpressionOptions &options) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  lldb::SBValue sb_value;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  lldb::ValueObjectSP new_value_sp;
  if (value_sp) {
    ExecutionContext exe_ctx(value_sp->GetExecutionContextRef());
    new_value_sp = ValueObject::CreateValueObjectFromExpression(
        name, expression, exe_ctx, options.ref());
    if (new_value_sp)
      new_value_sp->SetName(ConstString(name));
  }
  sb_value.SetSP(new_value_sp);
  if (log) {
    if (new_value_sp)
      log->Printf("SBValue(%p)::CreateValueFromExpression(name=\"%s\", "
                  "expression=\"%s\") => SBValue (%p)",
                  static_cast<void *>(value_sp.get()), name, expression,
                  static_cast<void *>(new_value_sp.get()));
    else
      log->Printf("SBValue(%p)::CreateValueFromExpression(name=\"%s\", "
                  "expression=\"%s\") => NULL",
                  static_cast<void *>(value_sp.get()), name, expression);
  }
  return sb_value;
}

lldb::SBValue SBValue::CreateValueFromAddress(const char *name,
                                              lldb::addr_t address,
                                              SBType sb_type) {
  lldb::SBValue sb_value;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  lldb::ValueObjectSP new_value_sp;
  lldb::TypeImplSP type_impl_sp(sb_type.GetSP());
  if (value_sp && type_impl_sp) {
    CompilerType ast_type(type_impl_sp->GetCompilerType(true));
    ExecutionContext exe_ctx(value_sp->GetExecutionContextRef());
    new_value_sp = ValueObject::CreateValueObjectFromAddress(name, address,
                                                             exe_ctx, ast_type);
  }
  sb_value.SetSP(new_value_sp);
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log) {
    if (new_value_sp)
      log->Printf("SBValue(%p)::CreateValueFromAddress => \"%s\"",
                  static_cast<void *>(value_sp.get()),
                  new_value_sp->GetName().AsCString());
    else
      log->Printf("SBValue(%p)::CreateValueFromAddress => NULL",
                  static_cast<void *>(value_sp.get()));
  }
  return sb_value;
}

lldb::SBValue SBValue::CreateValueFromData(const char *name, SBData data,
                                           SBType sb_type) {
  lldb::SBValue sb_value;
  lldb::ValueObjectSP new_value_sp;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  lldb::TypeImplSP type_impl_sp(sb_type.GetSP());
  if (value_sp && type_impl_sp) {
    ExecutionContext exe_ctx(value_sp->GetExecutionContextRef());
    new_value_sp = ValueObject::CreateValueObjectFromData(
        name, **data, exe_ctx, type_impl_sp->GetCompilerType(true));
    new_value_sp->SetAddressTypeOfChildren(eAddressTypeLoad);
  }
  sb_value.SetSP(new_value_sp);
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log) {
    if (new_value_sp)
      log->Printf("SBValue(%p)::CreateValueFromData => \"%s\"",
                  static_cast<void *>(value_sp.get()),
                  new_value_sp->GetName().AsCString());
    else
      log->Printf("SBValue(%p)::CreateValueFromData => NULL",
                  static_cast<void *>(value_sp.get()));
  }
  return sb_value;
}

SBValue SBValue::GetChildAtIndex(uint32_t idx) {
  const bool can_create_synthetic = false;
  lldb::DynamicValueType use_dynamic = eNoDynamicValues;
  TargetSP target_sp;
  if (m_opaque_sp)
    target_sp = m_opaque_sp->GetTargetSP();

  if (target_sp)
    use_dynamic = target_sp->GetPreferDynamicValue();

  return GetChildAtIndex(idx, use_dynamic, can_create_synthetic);
}

SBValue SBValue::GetChildAtIndex(uint32_t idx,
                                 lldb::DynamicValueType use_dynamic,
                                 bool can_create_synthetic) {
  lldb::ValueObjectSP child_sp;
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    const bool can_create = true;
    child_sp = value_sp->GetChildAtIndex(idx, can_create);
    if (can_create_synthetic && !child_sp) {
      child_sp = value_sp->GetSyntheticArrayMember(idx, can_create);
    }
  }

  SBValue sb_value;
  sb_value.SetSP(child_sp, use_dynamic, GetPreferSyntheticValue());
  if (log)
    log->Printf("SBValue(%p)::GetChildAtIndex (%u) => SBValue(%p)",
                static_cast<void *>(value_sp.get()), idx,
                static_cast<void *>(value_sp.get()));

  return sb_value;
}

uint32_t SBValue::GetIndexOfChildWithName(const char *name) {
  uint32_t idx = UINT32_MAX;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    idx = value_sp->GetIndexOfChildWithName(ConstString(name));
  }
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log) {
    if (idx == UINT32_MAX)
      log->Printf(
          "SBValue(%p)::GetIndexOfChildWithName (name=\"%s\") => NOT FOUND",
          static_cast<void *>(value_sp.get()), name);
    else
      log->Printf("SBValue(%p)::GetIndexOfChildWithName (name=\"%s\") => %u",
                  static_cast<void *>(value_sp.get()), name, idx);
  }
  return idx;
}

SBValue SBValue::GetChildMemberWithName(const char *name) {
  lldb::DynamicValueType use_dynamic_value = eNoDynamicValues;
  TargetSP target_sp;
  if (m_opaque_sp)
    target_sp = m_opaque_sp->GetTargetSP();

  if (target_sp)
    use_dynamic_value = target_sp->GetPreferDynamicValue();
  return GetChildMemberWithName(name, use_dynamic_value);
}

SBValue
SBValue::GetChildMemberWithName(const char *name,
                                lldb::DynamicValueType use_dynamic_value) {
  lldb::ValueObjectSP child_sp;
  const ConstString str_name(name);

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    child_sp = value_sp->GetChildMemberWithName(str_name, true);
  }

  SBValue sb_value;
  sb_value.SetSP(child_sp, use_dynamic_value, GetPreferSyntheticValue());

  if (log)
    log->Printf(
        "SBValue(%p)::GetChildMemberWithName (name=\"%s\") => SBValue(%p)",
        static_cast<void *>(value_sp.get()), name,
        static_cast<void *>(value_sp.get()));

  return sb_value;
}

lldb::SBValue SBValue::GetDynamicValue(lldb::DynamicValueType use_dynamic) {
  SBValue value_sb;
  if (IsValid()) {
    ValueImplSP proxy_sp(new ValueImpl(m_opaque_sp->GetRootSP(), use_dynamic,
                                       m_opaque_sp->GetUseSynthetic()));
    value_sb.SetSP(proxy_sp);
  }
  return value_sb;
}

lldb::SBValue SBValue::GetStaticValue() {
  SBValue value_sb;
  if (IsValid()) {
    ValueImplSP proxy_sp(new ValueImpl(m_opaque_sp->GetRootSP(),
                                       eNoDynamicValues,
                                       m_opaque_sp->GetUseSynthetic()));
    value_sb.SetSP(proxy_sp);
  }
  return value_sb;
}

lldb::SBValue SBValue::GetNonSyntheticValue() {
  SBValue value_sb;
  if (IsValid()) {
    ValueImplSP proxy_sp(new ValueImpl(m_opaque_sp->GetRootSP(),
                                       m_opaque_sp->GetUseDynamic(), false));
    value_sb.SetSP(proxy_sp);
  }
  return value_sb;
}

lldb::DynamicValueType SBValue::GetPreferDynamicValue() {
  if (!IsValid())
    return eNoDynamicValues;
  return m_opaque_sp->GetUseDynamic();
}

void SBValue::SetPreferDynamicValue(lldb::DynamicValueType use_dynamic) {
  if (IsValid())
    return m_opaque_sp->SetUseDynamic(use_dynamic);
}

bool SBValue::GetPreferSyntheticValue() {
  if (!IsValid())
    return false;
  return m_opaque_sp->GetUseSynthetic();
}

void SBValue::SetPreferSyntheticValue(bool use_synthetic) {
  if (IsValid())
    return m_opaque_sp->SetUseSynthetic(use_synthetic);
}

bool SBValue::IsDynamic() {
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp)
    return value_sp->IsDynamic();
  return false;
}

bool SBValue::IsSynthetic() {
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp)
    return value_sp->IsSynthetic();
  return false;
}

bool SBValue::IsSyntheticChildrenGenerated() {
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp)
    return value_sp->IsSyntheticChildrenGenerated();
  return false;
}

void SBValue::SetSyntheticChildrenGenerated(bool is) {
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp)
    return value_sp->SetSyntheticChildrenGenerated(is);
}

lldb::SBValue SBValue::GetValueForExpressionPath(const char *expr_path) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  lldb::ValueObjectSP child_sp;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    // using default values for all the fancy options, just do it if you can
    child_sp = value_sp->GetValueForExpressionPath(expr_path);
  }

  SBValue sb_value;
  sb_value.SetSP(child_sp, GetPreferDynamicValue(), GetPreferSyntheticValue());

  if (log)
    log->Printf("SBValue(%p)::GetValueForExpressionPath (expr_path=\"%s\") => "
                "SBValue(%p)",
                static_cast<void *>(value_sp.get()), expr_path,
                static_cast<void *>(value_sp.get()));

  return sb_value;
}

int64_t SBValue::GetValueAsSigned(SBError &error, int64_t fail_value) {
  error.Clear();
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    bool success = true;
    uint64_t ret_val = fail_value;
    ret_val = value_sp->GetValueAsSigned(fail_value, &success);
    if (!success)
      error.SetErrorString("could not resolve value");
    return ret_val;
  } else
    error.SetErrorStringWithFormat("could not get SBValue: %s",
                                   locker.GetError().AsCString());

  return fail_value;
}

uint64_t SBValue::GetValueAsUnsigned(SBError &error, uint64_t fail_value) {
  error.Clear();
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    bool success = true;
    uint64_t ret_val = fail_value;
    ret_val = value_sp->GetValueAsUnsigned(fail_value, &success);
    if (!success)
      error.SetErrorString("could not resolve value");
    return ret_val;
  } else
    error.SetErrorStringWithFormat("could not get SBValue: %s",
                                   locker.GetError().AsCString());

  return fail_value;
}

int64_t SBValue::GetValueAsSigned(int64_t fail_value) {
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    return value_sp->GetValueAsSigned(fail_value);
  }
  return fail_value;
}

uint64_t SBValue::GetValueAsUnsigned(uint64_t fail_value) {
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    return value_sp->GetValueAsUnsigned(fail_value);
  }
  return fail_value;
}

bool SBValue::MightHaveChildren() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  bool has_children = false;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp)
    has_children = value_sp->MightHaveChildren();

  if (log)
    log->Printf("SBValue(%p)::MightHaveChildren() => %i",
                static_cast<void *>(value_sp.get()), has_children);
  return has_children;
}

bool SBValue::IsRuntimeSupportValue() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  bool is_support = false;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp)
    is_support = value_sp->IsRuntimeSupportValue();

  if (log)
    log->Printf("SBValue(%p)::IsRuntimeSupportValue() => %i",
                static_cast<void *>(value_sp.get()), is_support);
  return is_support;
}

uint32_t SBValue::GetNumChildren() { return GetNumChildren(UINT32_MAX); }

uint32_t SBValue::GetNumChildren(uint32_t max) {
  uint32_t num_children = 0;

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp)
    num_children = value_sp->GetNumChildren(max);

  if (log)
    log->Printf("SBValue(%p)::GetNumChildren (%u) => %u",
                static_cast<void *>(value_sp.get()), max, num_children);

  return num_children;
}

SBValue SBValue::Dereference() {
  SBValue sb_value;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    Status error;
    sb_value = value_sp->Dereference(error);
  }
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf("SBValue(%p)::Dereference () => SBValue(%p)",
                static_cast<void *>(value_sp.get()),
                static_cast<void *>(value_sp.get()));

  return sb_value;
}

// Deprecated - please use GetType().IsPointerType() instead.
bool SBValue::TypeIsPointerType() { return GetType().IsPointerType(); }

void *SBValue::GetOpaqueType() {
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp)
    return value_sp->GetCompilerType().GetOpaqueQualType();
  return NULL;
}

lldb::SBTarget SBValue::GetTarget() {
  SBTarget sb_target;
  TargetSP target_sp;
  if (m_opaque_sp) {
    target_sp = m_opaque_sp->GetTargetSP();
    sb_target.SetSP(target_sp);
  }
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log) {
    if (target_sp.get() == NULL)
      log->Printf("SBValue(%p)::GetTarget () => NULL",
                  static_cast<void *>(m_opaque_sp.get()));
    else
      log->Printf("SBValue(%p)::GetTarget () => %p",
                  static_cast<void *>(m_opaque_sp.get()),
                  static_cast<void *>(target_sp.get()));
  }
  return sb_target;
}

lldb::SBProcess SBValue::GetProcess() {
  SBProcess sb_process;
  ProcessSP process_sp;
  if (m_opaque_sp) {
    process_sp = m_opaque_sp->GetProcessSP();
    sb_process.SetSP(process_sp);
  }
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log) {
    if (process_sp.get() == NULL)
      log->Printf("SBValue(%p)::GetProcess () => NULL",
                  static_cast<void *>(m_opaque_sp.get()));
    else
      log->Printf("SBValue(%p)::GetProcess () => %p",
                  static_cast<void *>(m_opaque_sp.get()),
                  static_cast<void *>(process_sp.get()));
  }
  return sb_process;
}

lldb::SBThread SBValue::GetThread() {
  SBThread sb_thread;
  ThreadSP thread_sp;
  if (m_opaque_sp) {
    thread_sp = m_opaque_sp->GetThreadSP();
    sb_thread.SetThread(thread_sp);
  }
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log) {
    if (thread_sp.get() == NULL)
      log->Printf("SBValue(%p)::GetThread () => NULL",
                  static_cast<void *>(m_opaque_sp.get()));
    else
      log->Printf("SBValue(%p)::GetThread () => %p",
                  static_cast<void *>(m_opaque_sp.get()),
                  static_cast<void *>(thread_sp.get()));
  }
  return sb_thread;
}

lldb::SBFrame SBValue::GetFrame() {
  SBFrame sb_frame;
  StackFrameSP frame_sp;
  if (m_opaque_sp) {
    frame_sp = m_opaque_sp->GetFrameSP();
    sb_frame.SetFrameSP(frame_sp);
  }
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log) {
    if (frame_sp.get() == NULL)
      log->Printf("SBValue(%p)::GetFrame () => NULL",
                  static_cast<void *>(m_opaque_sp.get()));
    else
      log->Printf("SBValue(%p)::GetFrame () => %p",
                  static_cast<void *>(m_opaque_sp.get()),
                  static_cast<void *>(frame_sp.get()));
  }
  return sb_frame;
}

lldb::ValueObjectSP SBValue::GetSP(ValueLocker &locker) const {
  if (!m_opaque_sp || !m_opaque_sp->IsValid()) {
    locker.GetError().SetErrorString("No value");
    return ValueObjectSP();
  }
  return locker.GetLockedSP(*m_opaque_sp.get());
}

lldb::ValueObjectSP SBValue::GetSP() const {
  ValueLocker locker;
  return GetSP(locker);
}

void SBValue::SetSP(ValueImplSP impl_sp) { m_opaque_sp = impl_sp; }

void SBValue::SetSP(const lldb::ValueObjectSP &sp) {
  if (sp) {
    lldb::TargetSP target_sp(sp->GetTargetSP());
    if (target_sp) {
      lldb::DynamicValueType use_dynamic = target_sp->GetPreferDynamicValue();
      bool use_synthetic =
          target_sp->TargetProperties::GetEnableSyntheticValue();
      m_opaque_sp = ValueImplSP(new ValueImpl(sp, use_dynamic, use_synthetic));
    } else
      m_opaque_sp = ValueImplSP(new ValueImpl(sp, eNoDynamicValues, true));
  } else
    m_opaque_sp = ValueImplSP(new ValueImpl(sp, eNoDynamicValues, false));
}

void SBValue::SetSP(const lldb::ValueObjectSP &sp,
                    lldb::DynamicValueType use_dynamic) {
  if (sp) {
    lldb::TargetSP target_sp(sp->GetTargetSP());
    if (target_sp) {
      bool use_synthetic =
          target_sp->TargetProperties::GetEnableSyntheticValue();
      SetSP(sp, use_dynamic, use_synthetic);
    } else
      SetSP(sp, use_dynamic, true);
  } else
    SetSP(sp, use_dynamic, false);
}

void SBValue::SetSP(const lldb::ValueObjectSP &sp, bool use_synthetic) {
  if (sp) {
    lldb::TargetSP target_sp(sp->GetTargetSP());
    if (target_sp) {
      lldb::DynamicValueType use_dynamic = target_sp->GetPreferDynamicValue();
      SetSP(sp, use_dynamic, use_synthetic);
    } else
      SetSP(sp, eNoDynamicValues, use_synthetic);
  } else
    SetSP(sp, eNoDynamicValues, use_synthetic);
}

void SBValue::SetSP(const lldb::ValueObjectSP &sp,
                    lldb::DynamicValueType use_dynamic, bool use_synthetic) {
  m_opaque_sp = ValueImplSP(new ValueImpl(sp, use_dynamic, use_synthetic));
}

void SBValue::SetSP(const lldb::ValueObjectSP &sp,
                    lldb::DynamicValueType use_dynamic, bool use_synthetic,
                    const char *name) {
  m_opaque_sp =
      ValueImplSP(new ValueImpl(sp, use_dynamic, use_synthetic, name));
}

bool SBValue::GetExpressionPath(SBStream &description) {
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    value_sp->GetExpressionPath(description.ref(), false);
    return true;
  }
  return false;
}

bool SBValue::GetExpressionPath(SBStream &description,
                                bool qualify_cxx_base_classes) {
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    value_sp->GetExpressionPath(description.ref(), qualify_cxx_base_classes);
    return true;
  }
  return false;
}

bool SBValue::GetDescription(SBStream &description) {
  Stream &strm = description.ref();

  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp)
    value_sp->Dump(strm);
  else
    strm.PutCString("No value");

  return true;
}

lldb::Format SBValue::GetFormat() {
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp)
    return value_sp->GetFormat();
  return eFormatDefault;
}

void SBValue::SetFormat(lldb::Format format) {
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp)
    value_sp->SetFormat(format);
}

lldb::SBValue SBValue::AddressOf() {
  SBValue sb_value;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    Status error;
    sb_value.SetSP(value_sp->AddressOf(error), GetPreferDynamicValue(),
                   GetPreferSyntheticValue());
  }
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf("SBValue(%p)::AddressOf () => SBValue(%p)",
                static_cast<void *>(value_sp.get()),
                static_cast<void *>(value_sp.get()));

  return sb_value;
}

lldb::addr_t SBValue::GetLoadAddress() {
  lldb::addr_t value = LLDB_INVALID_ADDRESS;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    TargetSP target_sp(value_sp->GetTargetSP());
    if (target_sp) {
      const bool scalar_is_load_address = true;
      AddressType addr_type;
      value = value_sp->GetAddressOf(scalar_is_load_address, &addr_type);
      if (addr_type == eAddressTypeFile) {
        ModuleSP module_sp(value_sp->GetModule());
        if (!module_sp)
          value = LLDB_INVALID_ADDRESS;
        else {
          Address addr;
          module_sp->ResolveFileAddress(value, addr);
          value = addr.GetLoadAddress(target_sp.get());
        }
      } else if (addr_type == eAddressTypeHost ||
                 addr_type == eAddressTypeInvalid)
        value = LLDB_INVALID_ADDRESS;
    }
  }
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf("SBValue(%p)::GetLoadAddress () => (%" PRIu64 ")",
                static_cast<void *>(value_sp.get()), value);

  return value;
}

lldb::SBAddress SBValue::GetAddress() {
  Address addr;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    TargetSP target_sp(value_sp->GetTargetSP());
    if (target_sp) {
      lldb::addr_t value = LLDB_INVALID_ADDRESS;
      const bool scalar_is_load_address = true;
      AddressType addr_type;
      value = value_sp->GetAddressOf(scalar_is_load_address, &addr_type);
      if (addr_type == eAddressTypeFile) {
        ModuleSP module_sp(value_sp->GetModule());
        if (module_sp)
          module_sp->ResolveFileAddress(value, addr);
      } else if (addr_type == eAddressTypeLoad) {
        // no need to check the return value on this.. if it can actually do
        // the resolve addr will be in the form (section,offset), otherwise it
        // will simply be returned as (NULL, value)
        addr.SetLoadAddress(value, target_sp.get());
      }
    }
  }
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf("SBValue(%p)::GetAddress () => (%s,%" PRIu64 ")",
                static_cast<void *>(value_sp.get()),
                (addr.GetSection() ? addr.GetSection()->GetName().GetCString()
                                   : "NULL"),
                addr.GetOffset());
  return SBAddress(new Address(addr));
}

lldb::SBData SBValue::GetPointeeData(uint32_t item_idx, uint32_t item_count) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  lldb::SBData sb_data;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    TargetSP target_sp(value_sp->GetTargetSP());
    if (target_sp) {
      DataExtractorSP data_sp(new DataExtractor());
      value_sp->GetPointeeData(*data_sp, item_idx, item_count);
      if (data_sp->GetByteSize() > 0)
        *sb_data = data_sp;
    }
  }
  if (log)
    log->Printf("SBValue(%p)::GetPointeeData (%d, %d) => SBData(%p)",
                static_cast<void *>(value_sp.get()), item_idx, item_count,
                static_cast<void *>(sb_data.get()));

  return sb_data;
}

lldb::SBData SBValue::GetData() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  lldb::SBData sb_data;
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  if (value_sp) {
    DataExtractorSP data_sp(new DataExtractor());
    Status error;
    value_sp->GetData(*data_sp, error);
    if (error.Success())
      *sb_data = data_sp;
  }
  if (log)
    log->Printf("SBValue(%p)::GetData () => SBData(%p)",
                static_cast<void *>(value_sp.get()),
                static_cast<void *>(sb_data.get()));

  return sb_data;
}

bool SBValue::SetData(lldb::SBData &data, SBError &error) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  bool ret = true;

  if (value_sp) {
    DataExtractor *data_extractor = data.get();

    if (!data_extractor) {
      if (log)
        log->Printf("SBValue(%p)::SetData() => error: no data to set",
                    static_cast<void *>(value_sp.get()));

      error.SetErrorString("No data to set");
      ret = false;
    } else {
      Status set_error;

      value_sp->SetData(*data_extractor, set_error);

      if (!set_error.Success()) {
        error.SetErrorStringWithFormat("Couldn't set data: %s",
                                       set_error.AsCString());
        ret = false;
      }
    }
  } else {
    error.SetErrorStringWithFormat(
        "Couldn't set data: could not get SBValue: %s",
        locker.GetError().AsCString());
    ret = false;
  }

  if (log)
    log->Printf("SBValue(%p)::SetData (%p) => %s",
                static_cast<void *>(value_sp.get()),
                static_cast<void *>(data.get()), ret ? "true" : "false");
  return ret;
}

lldb::SBDeclaration SBValue::GetDeclaration() {
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  SBDeclaration decl_sb;
  if (value_sp) {
    Declaration decl;
    if (value_sp->GetDeclaration(decl))
      decl_sb.SetDeclaration(decl);
  }
  return decl_sb;
}

lldb::SBWatchpoint SBValue::Watch(bool resolve_location, bool read, bool write,
                                  SBError &error) {
  SBWatchpoint sb_watchpoint;

  // If the SBValue is not valid, there's no point in even trying to watch it.
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  TargetSP target_sp(GetTarget().GetSP());
  if (value_sp && target_sp) {
    // Read and Write cannot both be false.
    if (!read && !write)
      return sb_watchpoint;

    // If the value is not in scope, don't try and watch and invalid value
    if (!IsInScope())
      return sb_watchpoint;

    addr_t addr = GetLoadAddress();
    if (addr == LLDB_INVALID_ADDRESS)
      return sb_watchpoint;
    size_t byte_size = GetByteSize();
    if (byte_size == 0)
      return sb_watchpoint;

    uint32_t watch_type = 0;
    if (read)
      watch_type |= LLDB_WATCH_TYPE_READ;
    if (write)
      watch_type |= LLDB_WATCH_TYPE_WRITE;

    Status rc;
    CompilerType type(value_sp->GetCompilerType());
    WatchpointSP watchpoint_sp =
        target_sp->CreateWatchpoint(addr, byte_size, &type, watch_type, rc);
    error.SetError(rc);

    if (watchpoint_sp) {
      sb_watchpoint.SetSP(watchpoint_sp);
      Declaration decl;
      if (value_sp->GetDeclaration(decl)) {
        if (decl.GetFile()) {
          StreamString ss;
          // True to show fullpath for declaration file.
          decl.DumpStopContext(&ss, true);
          watchpoint_sp->SetDeclInfo(ss.GetString());
        }
      }
    }
  } else if (target_sp) {
    Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
    if (log)
      log->Printf("SBValue(%p)::Watch() => error getting SBValue: %s",
                  static_cast<void *>(value_sp.get()),
                  locker.GetError().AsCString());

    error.SetErrorStringWithFormat("could not get SBValue: %s",
                                   locker.GetError().AsCString());
  } else {
    Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
    if (log)
      log->Printf("SBValue(%p)::Watch() => error getting SBValue: no target",
                  static_cast<void *>(value_sp.get()));
    error.SetErrorString("could not set watchpoint, a target is required");
  }

  return sb_watchpoint;
}

// FIXME: Remove this method impl (as well as the decl in .h) once it is no
// longer needed.
// Backward compatibility fix in the interim.
lldb::SBWatchpoint SBValue::Watch(bool resolve_location, bool read,
                                  bool write) {
  SBError error;
  return Watch(resolve_location, read, write, error);
}

lldb::SBWatchpoint SBValue::WatchPointee(bool resolve_location, bool read,
                                         bool write, SBError &error) {
  SBWatchpoint sb_watchpoint;
  if (IsInScope() && GetType().IsPointerType())
    sb_watchpoint = Dereference().Watch(resolve_location, read, write, error);
  return sb_watchpoint;
}

lldb::SBValue SBValue::Persist() {
  ValueLocker locker;
  lldb::ValueObjectSP value_sp(GetSP(locker));
  SBValue persisted_sb;
  if (value_sp) {
    persisted_sb.SetSP(value_sp->Persist());
  }
  return persisted_sb;
}
