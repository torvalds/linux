//===-- VectorType.cpp ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/DataFormatters/VectorType.h"

#include "lldb/Core/ValueObject.h"
#include "lldb/DataFormatters/FormattersHelpers.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/TypeSystem.h"
#include "lldb/Target/Target.h"

#include "lldb/Utility/LLDBAssert.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;

static CompilerType GetCompilerTypeForFormat(lldb::Format format,
                                             CompilerType element_type,
                                             TypeSystem *type_system) {
  lldbassert(type_system && "type_system needs to be not NULL");

  switch (format) {
  case lldb::eFormatAddressInfo:
  case lldb::eFormatPointer:
    return type_system->GetBuiltinTypeForEncodingAndBitSize(
        eEncodingUint, 8 * type_system->GetPointerByteSize());

  case lldb::eFormatBoolean:
    return type_system->GetBasicTypeFromAST(lldb::eBasicTypeBool);

  case lldb::eFormatBytes:
  case lldb::eFormatBytesWithASCII:
  case lldb::eFormatChar:
  case lldb::eFormatCharArray:
  case lldb::eFormatCharPrintable:
    return type_system->GetBasicTypeFromAST(lldb::eBasicTypeChar);

  case lldb::eFormatComplex /* lldb::eFormatComplexFloat */:
    return type_system->GetBasicTypeFromAST(lldb::eBasicTypeFloatComplex);

  case lldb::eFormatCString:
    return type_system->GetBasicTypeFromAST(lldb::eBasicTypeChar)
        .GetPointerType();

  case lldb::eFormatFloat:
    return type_system->GetBasicTypeFromAST(lldb::eBasicTypeFloat);

  case lldb::eFormatHex:
  case lldb::eFormatHexUppercase:
  case lldb::eFormatOctal:
    return type_system->GetBasicTypeFromAST(lldb::eBasicTypeInt);

  case lldb::eFormatHexFloat:
    return type_system->GetBasicTypeFromAST(lldb::eBasicTypeFloat);

  case lldb::eFormatUnicode16:
  case lldb::eFormatUnicode32:

  case lldb::eFormatUnsigned:
    return type_system->GetBasicTypeFromAST(lldb::eBasicTypeUnsignedInt);

  case lldb::eFormatVectorOfChar:
    return type_system->GetBasicTypeFromAST(lldb::eBasicTypeChar);

  case lldb::eFormatVectorOfFloat32:
    return type_system->GetBuiltinTypeForEncodingAndBitSize(eEncodingIEEE754,
                                                            32);

  case lldb::eFormatVectorOfFloat64:
    return type_system->GetBuiltinTypeForEncodingAndBitSize(eEncodingIEEE754,
                                                            64);

  case lldb::eFormatVectorOfSInt16:
    return type_system->GetBuiltinTypeForEncodingAndBitSize(eEncodingSint, 16);

  case lldb::eFormatVectorOfSInt32:
    return type_system->GetBuiltinTypeForEncodingAndBitSize(eEncodingSint, 32);

  case lldb::eFormatVectorOfSInt64:
    return type_system->GetBuiltinTypeForEncodingAndBitSize(eEncodingSint, 64);

  case lldb::eFormatVectorOfSInt8:
    return type_system->GetBuiltinTypeForEncodingAndBitSize(eEncodingSint, 8);

  case lldb::eFormatVectorOfUInt128:
    return type_system->GetBuiltinTypeForEncodingAndBitSize(eEncodingUint, 128);

  case lldb::eFormatVectorOfUInt16:
    return type_system->GetBuiltinTypeForEncodingAndBitSize(eEncodingUint, 16);

  case lldb::eFormatVectorOfUInt32:
    return type_system->GetBuiltinTypeForEncodingAndBitSize(eEncodingUint, 32);

  case lldb::eFormatVectorOfUInt64:
    return type_system->GetBuiltinTypeForEncodingAndBitSize(eEncodingUint, 64);

  case lldb::eFormatVectorOfUInt8:
    return type_system->GetBuiltinTypeForEncodingAndBitSize(eEncodingUint, 8);

  case lldb::eFormatDefault:
    return element_type;

  case lldb::eFormatBinary:
  case lldb::eFormatComplexInteger:
  case lldb::eFormatDecimal:
  case lldb::eFormatEnum:
  case lldb::eFormatInstruction:
  case lldb::eFormatOSType:
  case lldb::eFormatVoid:
  default:
    return type_system->GetBuiltinTypeForEncodingAndBitSize(eEncodingUint, 8);
  }
}

static lldb::Format GetItemFormatForFormat(lldb::Format format,
                                           CompilerType element_type) {
  switch (format) {
  case lldb::eFormatVectorOfChar:
    return lldb::eFormatChar;

  case lldb::eFormatVectorOfFloat32:
  case lldb::eFormatVectorOfFloat64:
    return lldb::eFormatFloat;

  case lldb::eFormatVectorOfSInt16:
  case lldb::eFormatVectorOfSInt32:
  case lldb::eFormatVectorOfSInt64:
  case lldb::eFormatVectorOfSInt8:
    return lldb::eFormatDecimal;

  case lldb::eFormatVectorOfUInt128:
  case lldb::eFormatVectorOfUInt16:
  case lldb::eFormatVectorOfUInt32:
  case lldb::eFormatVectorOfUInt64:
  case lldb::eFormatVectorOfUInt8:
    return lldb::eFormatUnsigned;

  case lldb::eFormatBinary:
  case lldb::eFormatComplexInteger:
  case lldb::eFormatDecimal:
  case lldb::eFormatEnum:
  case lldb::eFormatInstruction:
  case lldb::eFormatOSType:
  case lldb::eFormatVoid:
    return eFormatHex;

  case lldb::eFormatDefault: {
    // special case the (default, char) combination to actually display as an
    // integer value most often, you won't want to see the ASCII characters...
    // (and if you do, eFormatChar is a keystroke away)
    bool is_char = element_type.IsCharType();
    bool is_signed = false;
    element_type.IsIntegerType(is_signed);
    return is_char ? (is_signed ? lldb::eFormatDecimal : eFormatHex) : format;
  } break;

  default:
    return format;
  }
}

static size_t CalculateNumChildren(
    CompilerType container_type, CompilerType element_type,
    lldb_private::ExecutionContextScope *exe_scope =
        nullptr // does not matter here because all we trade in are basic types
    ) {
  llvm::Optional<uint64_t> container_size =
      container_type.GetByteSize(exe_scope);
  llvm::Optional<uint64_t> element_size = element_type.GetByteSize(exe_scope);

  if (container_size && element_size && *element_size) {
    if (*container_size % *element_size)
      return 0;
    return *container_size / *element_size;
  }
  return 0;
}

namespace lldb_private {
namespace formatters {

class VectorTypeSyntheticFrontEnd : public SyntheticChildrenFrontEnd {
public:
  VectorTypeSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
      : SyntheticChildrenFrontEnd(*valobj_sp), m_parent_format(eFormatInvalid),
        m_item_format(eFormatInvalid), m_child_type(), m_num_children(0) {}

  ~VectorTypeSyntheticFrontEnd() override = default;

  size_t CalculateNumChildren() override { return m_num_children; }

  lldb::ValueObjectSP GetChildAtIndex(size_t idx) override {
    if (idx >= CalculateNumChildren())
      return {};
    llvm::Optional<uint64_t> size = m_child_type.GetByteSize(nullptr);
    if (!size)
      return {};
    auto offset = idx * *size;
    StreamString idx_name;
    idx_name.Printf("[%" PRIu64 "]", (uint64_t)idx);
    ValueObjectSP child_sp(m_backend.GetSyntheticChildAtOffset(
        offset, m_child_type, true, ConstString(idx_name.GetString())));
    if (!child_sp)
      return child_sp;

    child_sp->SetFormat(m_item_format);

    return child_sp;
  }

  bool Update() override {
    m_parent_format = m_backend.GetFormat();
    CompilerType parent_type(m_backend.GetCompilerType());
    CompilerType element_type;
    parent_type.IsVectorType(&element_type, nullptr);
    TargetSP target_sp(m_backend.GetTargetSP());
    m_child_type = ::GetCompilerTypeForFormat(
        m_parent_format, element_type,
        target_sp
            ? target_sp->GetScratchTypeSystemForLanguage(nullptr,
                                                         lldb::eLanguageTypeC)
            : nullptr);
    m_num_children = ::CalculateNumChildren(parent_type, m_child_type);
    m_item_format = GetItemFormatForFormat(m_parent_format, m_child_type);
    return false;
  }

  bool MightHaveChildren() override { return true; }

  size_t GetIndexOfChildWithName(const ConstString &name) override {
    const char *item_name = name.GetCString();
    uint32_t idx = ExtractIndexFromString(item_name);
    if (idx < UINT32_MAX && idx >= CalculateNumChildren())
      return UINT32_MAX;
    return idx;
  }

private:
  lldb::Format m_parent_format;
  lldb::Format m_item_format;
  CompilerType m_child_type;
  size_t m_num_children;
};

} // namespace formatters
} // namespace lldb_private

bool lldb_private::formatters::VectorTypeSummaryProvider(
    ValueObject &valobj, Stream &s, const TypeSummaryOptions &) {
  auto synthetic_children =
      VectorTypeSyntheticFrontEndCreator(nullptr, valobj.GetSP());
  if (!synthetic_children)
    return false;

  synthetic_children->Update();

  s.PutChar('(');
  bool first = true;

  size_t idx = 0, len = synthetic_children->CalculateNumChildren();

  for (; idx < len; idx++) {
    auto child_sp = synthetic_children->GetChildAtIndex(idx);
    if (!child_sp)
      continue;
    child_sp = child_sp->GetQualifiedRepresentationIfAvailable(
        lldb::eDynamicDontRunTarget, true);

    const char *child_value = child_sp->GetValueAsCString();
    if (child_value && *child_value) {
      if (first) {
        s.Printf("%s", child_value);
        first = false;
      } else {
        s.Printf(", %s", child_value);
      }
    }
  }

  s.PutChar(')');

  return true;
}

lldb_private::SyntheticChildrenFrontEnd *
lldb_private::formatters::VectorTypeSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  if (!valobj_sp)
    return nullptr;
  return new VectorTypeSyntheticFrontEnd(valobj_sp);
}
