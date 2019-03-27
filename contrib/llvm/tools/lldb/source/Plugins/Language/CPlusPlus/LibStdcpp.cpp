//===-- LibStdcpp.cpp -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "LibStdcpp.h"

#include "lldb/Core/ValueObject.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/DataFormatters/StringPrinter.h"
#include "lldb/DataFormatters/VectorIterator.h"
#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;

namespace {

class LibstdcppMapIteratorSyntheticFrontEnd : public SyntheticChildrenFrontEnd {
  /*
   (std::_Rb_tree_iterator<std::pair<const int, std::basic_string<char,
   std::char_traits<char>, std::allocator<char> > > >) ibeg = {
   (_Base_ptr) _M_node = 0x0000000100103910 {
   (std::_Rb_tree_color) _M_color = _S_black
   (std::_Rb_tree_node_base::_Base_ptr) _M_parent = 0x00000001001038c0
   (std::_Rb_tree_node_base::_Base_ptr) _M_left = 0x0000000000000000
   (std::_Rb_tree_node_base::_Base_ptr) _M_right = 0x0000000000000000
   }
   }
   */

public:
  explicit LibstdcppMapIteratorSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp);

  size_t CalculateNumChildren() override;

  lldb::ValueObjectSP GetChildAtIndex(size_t idx) override;

  bool Update() override;

  bool MightHaveChildren() override;

  size_t GetIndexOfChildWithName(const ConstString &name) override;

private:
  ExecutionContextRef m_exe_ctx_ref;
  lldb::addr_t m_pair_address;
  CompilerType m_pair_type;
  lldb::ValueObjectSP m_pair_sp;
};

class LibStdcppSharedPtrSyntheticFrontEnd : public SyntheticChildrenFrontEnd {
public:
  explicit LibStdcppSharedPtrSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp);

  size_t CalculateNumChildren() override;

  lldb::ValueObjectSP GetChildAtIndex(size_t idx) override;

  bool Update() override;

  bool MightHaveChildren() override;

  size_t GetIndexOfChildWithName(const ConstString &name) override;
};

} // end of anonymous namespace

LibstdcppMapIteratorSyntheticFrontEnd::LibstdcppMapIteratorSyntheticFrontEnd(
    lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp), m_exe_ctx_ref(), m_pair_address(0),
      m_pair_type(), m_pair_sp() {
  if (valobj_sp)
    Update();
}

bool LibstdcppMapIteratorSyntheticFrontEnd::Update() {
  ValueObjectSP valobj_sp = m_backend.GetSP();
  if (!valobj_sp)
    return false;

  TargetSP target_sp(valobj_sp->GetTargetSP());

  if (!target_sp)
    return false;

  bool is_64bit = (target_sp->GetArchitecture().GetAddressByteSize() == 8);

  if (!valobj_sp)
    return false;
  m_exe_ctx_ref = valobj_sp->GetExecutionContextRef();

  ValueObjectSP _M_node_sp(
      valobj_sp->GetChildMemberWithName(ConstString("_M_node"), true));
  if (!_M_node_sp)
    return false;

  m_pair_address = _M_node_sp->GetValueAsUnsigned(0);
  if (m_pair_address == 0)
    return false;

  m_pair_address += (is_64bit ? 32 : 16);

  CompilerType my_type(valobj_sp->GetCompilerType());
  if (my_type.GetNumTemplateArguments() >= 1) {
    CompilerType pair_type = my_type.GetTypeTemplateArgument(0);
    if (!pair_type)
      return false;
    m_pair_type = pair_type;
  } else
    return false;

  return true;
}

size_t LibstdcppMapIteratorSyntheticFrontEnd::CalculateNumChildren() {
  return 2;
}

lldb::ValueObjectSP
LibstdcppMapIteratorSyntheticFrontEnd::GetChildAtIndex(size_t idx) {
  if (m_pair_address != 0 && m_pair_type) {
    if (!m_pair_sp)
      m_pair_sp = CreateValueObjectFromAddress("pair", m_pair_address,
                                               m_exe_ctx_ref, m_pair_type);
    if (m_pair_sp)
      return m_pair_sp->GetChildAtIndex(idx, true);
  }
  return lldb::ValueObjectSP();
}

bool LibstdcppMapIteratorSyntheticFrontEnd::MightHaveChildren() { return true; }

size_t LibstdcppMapIteratorSyntheticFrontEnd::GetIndexOfChildWithName(
    const ConstString &name) {
  if (name == ConstString("first"))
    return 0;
  if (name == ConstString("second"))
    return 1;
  return UINT32_MAX;
}

SyntheticChildrenFrontEnd *
lldb_private::formatters::LibstdcppMapIteratorSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  return (valobj_sp ? new LibstdcppMapIteratorSyntheticFrontEnd(valobj_sp)
                    : nullptr);
}

/*
 (lldb) fr var ibeg --ptr-depth 1
 (__gnu_cxx::__normal_iterator<int *, std::vector<int, std::allocator<int> > >)
 ibeg = {
 _M_current = 0x00000001001037a0 {
 *_M_current = 1
 }
 }
 */

SyntheticChildrenFrontEnd *
lldb_private::formatters::LibStdcppVectorIteratorSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  static ConstString g_item_name;
  if (!g_item_name)
    g_item_name.SetCString("_M_current");
  return (valobj_sp
              ? new VectorIteratorSyntheticFrontEnd(valobj_sp, g_item_name)
              : nullptr);
}

lldb_private::formatters::VectorIteratorSyntheticFrontEnd::
    VectorIteratorSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp,
                                    ConstString item_name)
    : SyntheticChildrenFrontEnd(*valobj_sp), m_exe_ctx_ref(),
      m_item_name(item_name), m_item_sp() {
  if (valobj_sp)
    Update();
}

bool VectorIteratorSyntheticFrontEnd::Update() {
  m_item_sp.reset();

  ValueObjectSP valobj_sp = m_backend.GetSP();
  if (!valobj_sp)
    return false;

  if (!valobj_sp)
    return false;

  ValueObjectSP item_ptr(valobj_sp->GetChildMemberWithName(m_item_name, true));
  if (!item_ptr)
    return false;
  if (item_ptr->GetValueAsUnsigned(0) == 0)
    return false;
  Status err;
  m_exe_ctx_ref = valobj_sp->GetExecutionContextRef();
  m_item_sp = CreateValueObjectFromAddress(
      "item", item_ptr->GetValueAsUnsigned(0), m_exe_ctx_ref,
      item_ptr->GetCompilerType().GetPointeeType());
  if (err.Fail())
    m_item_sp.reset();
  return false;
}

size_t VectorIteratorSyntheticFrontEnd::CalculateNumChildren() { return 1; }

lldb::ValueObjectSP
VectorIteratorSyntheticFrontEnd::GetChildAtIndex(size_t idx) {
  if (idx == 0)
    return m_item_sp;
  return lldb::ValueObjectSP();
}

bool VectorIteratorSyntheticFrontEnd::MightHaveChildren() { return true; }

size_t VectorIteratorSyntheticFrontEnd::GetIndexOfChildWithName(
    const ConstString &name) {
  if (name == ConstString("item"))
    return 0;
  return UINT32_MAX;
}

bool lldb_private::formatters::LibStdcppStringSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  const bool scalar_is_load_addr = true;
  AddressType addr_type;
  lldb::addr_t addr_of_string =
      valobj.GetAddressOf(scalar_is_load_addr, &addr_type);
  if (addr_of_string != LLDB_INVALID_ADDRESS) {
    switch (addr_type) {
    case eAddressTypeLoad: {
      ProcessSP process_sp(valobj.GetProcessSP());
      if (!process_sp)
        return false;

      StringPrinter::ReadStringAndDumpToStreamOptions options(valobj);
      Status error;
      lldb::addr_t addr_of_data =
          process_sp->ReadPointerFromMemory(addr_of_string, error);
      if (error.Fail() || addr_of_data == 0 ||
          addr_of_data == LLDB_INVALID_ADDRESS)
        return false;
      options.SetLocation(addr_of_data);
      options.SetProcessSP(process_sp);
      options.SetStream(&stream);
      options.SetNeedsZeroTermination(false);
      options.SetBinaryZeroIsTerminator(true);
      lldb::addr_t size_of_data = process_sp->ReadPointerFromMemory(
          addr_of_string + process_sp->GetAddressByteSize(), error);
      if (error.Fail())
        return false;
      options.SetSourceSize(size_of_data);

      if (!StringPrinter::ReadStringAndDumpToStream<
              StringPrinter::StringElementType::UTF8>(options)) {
        stream.Printf("Summary Unavailable");
        return true;
      } else
        return true;
    } break;
    case eAddressTypeHost:
      break;
    case eAddressTypeInvalid:
    case eAddressTypeFile:
      break;
    }
  }
  return false;
}

bool lldb_private::formatters::LibStdcppWStringSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  const bool scalar_is_load_addr = true;
  AddressType addr_type;
  lldb::addr_t addr_of_string =
      valobj.GetAddressOf(scalar_is_load_addr, &addr_type);
  if (addr_of_string != LLDB_INVALID_ADDRESS) {
    switch (addr_type) {
    case eAddressTypeLoad: {
      ProcessSP process_sp(valobj.GetProcessSP());
      if (!process_sp)
        return false;

      CompilerType wchar_compiler_type =
          valobj.GetCompilerType().GetBasicTypeFromAST(lldb::eBasicTypeWChar);

      if (!wchar_compiler_type)
        return false;

      // Safe to pass nullptr for exe_scope here.
      llvm::Optional<uint64_t> size = wchar_compiler_type.GetBitSize(nullptr);
      if (!size)
        return false;
      const uint32_t wchar_size = *size;

      StringPrinter::ReadStringAndDumpToStreamOptions options(valobj);
      Status error;
      lldb::addr_t addr_of_data =
          process_sp->ReadPointerFromMemory(addr_of_string, error);
      if (error.Fail() || addr_of_data == 0 ||
          addr_of_data == LLDB_INVALID_ADDRESS)
        return false;
      options.SetLocation(addr_of_data);
      options.SetProcessSP(process_sp);
      options.SetStream(&stream);
      options.SetNeedsZeroTermination(false);
      options.SetBinaryZeroIsTerminator(false);
      lldb::addr_t size_of_data = process_sp->ReadPointerFromMemory(
          addr_of_string + process_sp->GetAddressByteSize(), error);
      if (error.Fail())
        return false;
      options.SetSourceSize(size_of_data);
      options.SetPrefixToken("L");

      switch (wchar_size) {
      case 8:
        return StringPrinter::ReadStringAndDumpToStream<
            StringPrinter::StringElementType::UTF8>(options);
      case 16:
        return StringPrinter::ReadStringAndDumpToStream<
            StringPrinter::StringElementType::UTF16>(options);
      case 32:
        return StringPrinter::ReadStringAndDumpToStream<
            StringPrinter::StringElementType::UTF32>(options);
      default:
        stream.Printf("size for wchar_t is not valid");
        return true;
      }
      return true;
    } break;
    case eAddressTypeHost:
      break;
    case eAddressTypeInvalid:
    case eAddressTypeFile:
      break;
    }
  }
  return false;
}

LibStdcppSharedPtrSyntheticFrontEnd::LibStdcppSharedPtrSyntheticFrontEnd(
    lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp) {
  if (valobj_sp)
    Update();
}

size_t LibStdcppSharedPtrSyntheticFrontEnd::CalculateNumChildren() { return 1; }

lldb::ValueObjectSP
LibStdcppSharedPtrSyntheticFrontEnd::GetChildAtIndex(size_t idx) {
  ValueObjectSP valobj_sp = m_backend.GetSP();
  if (!valobj_sp)
    return lldb::ValueObjectSP();

  if (idx == 0)
    return valobj_sp->GetChildMemberWithName(ConstString("_M_ptr"), true);
  else
    return lldb::ValueObjectSP();
}

bool LibStdcppSharedPtrSyntheticFrontEnd::Update() { return false; }

bool LibStdcppSharedPtrSyntheticFrontEnd::MightHaveChildren() { return true; }

size_t LibStdcppSharedPtrSyntheticFrontEnd::GetIndexOfChildWithName(
    const ConstString &name) {
  if (name == ConstString("_M_ptr"))
    return 0;
  return UINT32_MAX;
}

SyntheticChildrenFrontEnd *
lldb_private::formatters::LibStdcppSharedPtrSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  return (valobj_sp ? new LibStdcppSharedPtrSyntheticFrontEnd(valobj_sp)
                    : nullptr);
}

bool lldb_private::formatters::LibStdcppSmartPointerSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  ValueObjectSP valobj_sp(valobj.GetNonSyntheticValue());
  if (!valobj_sp)
    return false;

  ValueObjectSP ptr_sp(
      valobj_sp->GetChildMemberWithName(ConstString("_M_ptr"), true));
  if (!ptr_sp)
    return false;

  ValueObjectSP usecount_sp(valobj_sp->GetChildAtNamePath(
      {ConstString("_M_refcount"), ConstString("_M_pi"),
       ConstString("_M_use_count")}));
  if (!usecount_sp)
    return false;

  if (ptr_sp->GetValueAsUnsigned(0) == 0 ||
      usecount_sp->GetValueAsUnsigned(0) == 0) {
    stream.Printf("nullptr");
    return true;
  }

  Status error;
  ValueObjectSP pointee_sp = ptr_sp->Dereference(error);
  if (pointee_sp && error.Success()) {
    if (pointee_sp->DumpPrintableRepresentation(
            stream, ValueObject::eValueObjectRepresentationStyleSummary,
            lldb::eFormatInvalid,
            ValueObject::PrintableRepresentationSpecialCases::eDisable,
            false)) {
      return true;
    }
  }

  stream.Printf("ptr = 0x%" PRIx64, ptr_sp->GetValueAsUnsigned(0));
  return true;
}
