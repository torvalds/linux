//===-- NSError.cpp ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/DeclCXX.h"

#include "Cocoa.h"

#include "lldb/Core/ValueObject.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/DataFormatters/FormattersHelpers.h"
#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Target/ObjCLanguageRuntime.h"
#include "lldb/Target/ProcessStructReader.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"

#include "Plugins/Language/ObjC/NSString.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;

static lldb::addr_t DerefToNSErrorPointer(ValueObject &valobj) {
  CompilerType valobj_type(valobj.GetCompilerType());
  Flags type_flags(valobj_type.GetTypeInfo());
  if (type_flags.AllClear(eTypeHasValue)) {
    if (valobj.IsBaseClass() && valobj.GetParent())
      return valobj.GetParent()->GetValueAsUnsigned(LLDB_INVALID_ADDRESS);
  } else {
    lldb::addr_t ptr_value = valobj.GetValueAsUnsigned(LLDB_INVALID_ADDRESS);
    if (type_flags.AllSet(eTypeIsPointer)) {
      CompilerType pointee_type(valobj_type.GetPointeeType());
      Flags pointee_flags(pointee_type.GetTypeInfo());
      if (pointee_flags.AllSet(eTypeIsPointer)) {
        if (ProcessSP process_sp = valobj.GetProcessSP()) {
          Status error;
          ptr_value = process_sp->ReadPointerFromMemory(ptr_value, error);
        }
      }
    }
    return ptr_value;
  }

  return LLDB_INVALID_ADDRESS;
}

bool lldb_private::formatters::NSError_SummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  ProcessSP process_sp(valobj.GetProcessSP());
  if (!process_sp)
    return false;

  lldb::addr_t ptr_value = DerefToNSErrorPointer(valobj);
  if (ptr_value == LLDB_INVALID_ADDRESS)
    return false;

  size_t ptr_size = process_sp->GetAddressByteSize();
  lldb::addr_t code_location = ptr_value + 2 * ptr_size;
  lldb::addr_t domain_location = ptr_value + 3 * ptr_size;

  Status error;
  uint64_t code = process_sp->ReadUnsignedIntegerFromMemory(code_location,
                                                            ptr_size, 0, error);
  if (error.Fail())
    return false;

  lldb::addr_t domain_str_value =
      process_sp->ReadPointerFromMemory(domain_location, error);
  if (error.Fail() || domain_str_value == LLDB_INVALID_ADDRESS)
    return false;

  if (!domain_str_value) {
    stream.Printf("domain: nil - code: %" PRIu64, code);
    return true;
  }

  InferiorSizedWord isw(domain_str_value, *process_sp);

  ValueObjectSP domain_str_sp = ValueObject::CreateValueObjectFromData(
      "domain_str", isw.GetAsData(process_sp->GetByteOrder()),
      valobj.GetExecutionContextRef(), process_sp->GetTarget()
                                           .GetScratchClangASTContext()
                                           ->GetBasicType(lldb::eBasicTypeVoid)
                                           .GetPointerType());

  if (!domain_str_sp)
    return false;

  StreamString domain_str_summary;
  if (NSStringSummaryProvider(*domain_str_sp, domain_str_summary, options) &&
      !domain_str_summary.Empty()) {
    stream.Printf("domain: %s - code: %" PRIu64, domain_str_summary.GetData(),
                  code);
    return true;
  } else {
    stream.Printf("domain: nil - code: %" PRIu64, code);
    return true;
  }
}

class NSErrorSyntheticFrontEnd : public SyntheticChildrenFrontEnd {
public:
  NSErrorSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
      : SyntheticChildrenFrontEnd(*valobj_sp) {}

  ~NSErrorSyntheticFrontEnd() override = default;
  // no need to delete m_child_ptr - it's kept alive by the cluster manager on
  // our behalf

  size_t CalculateNumChildren() override {
    if (m_child_ptr)
      return 1;
    if (m_child_sp)
      return 1;
    return 0;
  }

  lldb::ValueObjectSP GetChildAtIndex(size_t idx) override {
    if (idx != 0)
      return lldb::ValueObjectSP();

    if (m_child_ptr)
      return m_child_ptr->GetSP();
    return m_child_sp;
  }

  bool Update() override {
    m_child_ptr = nullptr;
    m_child_sp.reset();

    ProcessSP process_sp(m_backend.GetProcessSP());
    if (!process_sp)
      return false;

    lldb::addr_t userinfo_location = DerefToNSErrorPointer(m_backend);
    if (userinfo_location == LLDB_INVALID_ADDRESS)
      return false;

    size_t ptr_size = process_sp->GetAddressByteSize();

    userinfo_location += 4 * ptr_size;
    Status error;
    lldb::addr_t userinfo =
        process_sp->ReadPointerFromMemory(userinfo_location, error);
    if (userinfo == LLDB_INVALID_ADDRESS || error.Fail())
      return false;
    InferiorSizedWord isw(userinfo, *process_sp);
    m_child_sp = CreateValueObjectFromData(
        "_userInfo", isw.GetAsData(process_sp->GetByteOrder()),
        m_backend.GetExecutionContextRef(),
        process_sp->GetTarget().GetScratchClangASTContext()->GetBasicType(
            lldb::eBasicTypeObjCID));
    return false;
  }

  bool MightHaveChildren() override { return true; }

  size_t GetIndexOfChildWithName(const ConstString &name) override {
    static ConstString g___userInfo("_userInfo");
    if (name == g___userInfo)
      return 0;
    return UINT32_MAX;
  }

private:
  // the child here can be "real" (i.e. an actual child of the root) or
  // synthetized from raw memory if the former, I need to store a plain pointer
  // to it - or else a loop of references will cause this entire hierarchy of
  // values to leak if the latter, then I need to store a SharedPointer to it -
  // so that it only goes away when everyone else in the cluster goes away oh
  // joy!
  ValueObject *m_child_ptr;
  ValueObjectSP m_child_sp;
};

SyntheticChildrenFrontEnd *
lldb_private::formatters::NSErrorSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  lldb::ProcessSP process_sp(valobj_sp->GetProcessSP());
  if (!process_sp)
    return nullptr;
  ObjCLanguageRuntime *runtime =
      (ObjCLanguageRuntime *)process_sp->GetLanguageRuntime(
          lldb::eLanguageTypeObjC);
  if (!runtime)
    return nullptr;

  ObjCLanguageRuntime::ClassDescriptorSP descriptor(
      runtime->GetClassDescriptor(*valobj_sp.get()));

  if (!descriptor.get() || !descriptor->IsValid())
    return nullptr;

  const char *class_name = descriptor->GetClassName().GetCString();

  if (!class_name || !*class_name)
    return nullptr;

  if (!strcmp(class_name, "NSError"))
    return (new NSErrorSyntheticFrontEnd(valobj_sp));
  else if (!strcmp(class_name, "__NSCFError"))
    return (new NSErrorSyntheticFrontEnd(valobj_sp));

  return nullptr;
}
