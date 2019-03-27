//===-- CompilerType.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/CompilerType.h"

#include "lldb/Core/Debugger.h"
#include "lldb/Core/StreamFile.h"
#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Symbol/ClangExternalASTSourceCommon.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Scalar.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StreamString.h"

#include <iterator>
#include <mutex>

using namespace lldb;
using namespace lldb_private;

CompilerType::CompilerType(TypeSystem *type_system,
                           lldb::opaque_compiler_type_t type)
    : m_type(type), m_type_system(type_system) {}

CompilerType::CompilerType(clang::ASTContext *ast, clang::QualType qual_type)
    : m_type(qual_type.getAsOpaquePtr()),
      m_type_system(ClangASTContext::GetASTContext(ast)) {
#ifdef LLDB_CONFIGURATION_DEBUG
  if (m_type)
    assert(m_type_system != nullptr);
#endif
}

CompilerType::~CompilerType() {}

//----------------------------------------------------------------------
// Tests
//----------------------------------------------------------------------

bool CompilerType::IsAggregateType() const {
  if (IsValid())
    return m_type_system->IsAggregateType(m_type);
  return false;
}

bool CompilerType::IsAnonymousType() const {
  if (IsValid())
    return m_type_system->IsAnonymousType(m_type);
  return false;
}

bool CompilerType::IsArrayType(CompilerType *element_type_ptr, uint64_t *size,
                               bool *is_incomplete) const {
  if (IsValid())
    return m_type_system->IsArrayType(m_type, element_type_ptr, size,
                                      is_incomplete);

  if (element_type_ptr)
    element_type_ptr->Clear();
  if (size)
    *size = 0;
  if (is_incomplete)
    *is_incomplete = false;
  return false;
}

bool CompilerType::IsVectorType(CompilerType *element_type,
                                uint64_t *size) const {
  if (IsValid())
    return m_type_system->IsVectorType(m_type, element_type, size);
  return false;
}

bool CompilerType::IsRuntimeGeneratedType() const {
  if (IsValid())
    return m_type_system->IsRuntimeGeneratedType(m_type);
  return false;
}

bool CompilerType::IsCharType() const {
  if (IsValid())
    return m_type_system->IsCharType(m_type);
  return false;
}

bool CompilerType::IsCompleteType() const {
  if (IsValid())
    return m_type_system->IsCompleteType(m_type);
  return false;
}

bool CompilerType::IsConst() const {
  if (IsValid())
    return m_type_system->IsConst(m_type);
  return false;
}

bool CompilerType::IsCStringType(uint32_t &length) const {
  if (IsValid())
    return m_type_system->IsCStringType(m_type, length);
  return false;
}

bool CompilerType::IsFunctionType(bool *is_variadic_ptr) const {
  if (IsValid())
    return m_type_system->IsFunctionType(m_type, is_variadic_ptr);
  return false;
}

// Used to detect "Homogeneous Floating-point Aggregates"
uint32_t
CompilerType::IsHomogeneousAggregate(CompilerType *base_type_ptr) const {
  if (IsValid())
    return m_type_system->IsHomogeneousAggregate(m_type, base_type_ptr);
  return 0;
}

size_t CompilerType::GetNumberOfFunctionArguments() const {
  if (IsValid())
    return m_type_system->GetNumberOfFunctionArguments(m_type);
  return 0;
}

CompilerType
CompilerType::GetFunctionArgumentAtIndex(const size_t index) const {
  if (IsValid())
    return m_type_system->GetFunctionArgumentAtIndex(m_type, index);
  return CompilerType();
}

bool CompilerType::IsFunctionPointerType() const {
  if (IsValid())
    return m_type_system->IsFunctionPointerType(m_type);
  return false;
}

bool CompilerType::IsBlockPointerType(
    CompilerType *function_pointer_type_ptr) const {
  if (IsValid())
    return m_type_system->IsBlockPointerType(m_type, function_pointer_type_ptr);
  return 0;
}

bool CompilerType::IsIntegerType(bool &is_signed) const {
  if (IsValid())
    return m_type_system->IsIntegerType(m_type, is_signed);
  return false;
}

bool CompilerType::IsEnumerationType(bool &is_signed) const {
  if (IsValid())
    return m_type_system->IsEnumerationType(m_type, is_signed);
  return false;
}

bool CompilerType::IsIntegerOrEnumerationType(bool &is_signed) const {
  return IsIntegerType(is_signed) || IsEnumerationType(is_signed);
}

bool CompilerType::IsPointerType(CompilerType *pointee_type) const {
  if (IsValid()) {
    return m_type_system->IsPointerType(m_type, pointee_type);
  }
  if (pointee_type)
    pointee_type->Clear();
  return false;
}

bool CompilerType::IsPointerOrReferenceType(CompilerType *pointee_type) const {
  if (IsValid()) {
    return m_type_system->IsPointerOrReferenceType(m_type, pointee_type);
  }
  if (pointee_type)
    pointee_type->Clear();
  return false;
}

bool CompilerType::IsReferenceType(CompilerType *pointee_type,
                                   bool *is_rvalue) const {
  if (IsValid()) {
    return m_type_system->IsReferenceType(m_type, pointee_type, is_rvalue);
  }
  if (pointee_type)
    pointee_type->Clear();
  return false;
}

bool CompilerType::ShouldTreatScalarValueAsAddress() const {
  if (IsValid())
    return m_type_system->ShouldTreatScalarValueAsAddress(m_type);
  return false;
}

bool CompilerType::IsFloatingPointType(uint32_t &count,
                                       bool &is_complex) const {
  if (IsValid()) {
    return m_type_system->IsFloatingPointType(m_type, count, is_complex);
  }
  count = 0;
  is_complex = false;
  return false;
}

bool CompilerType::IsDefined() const {
  if (IsValid())
    return m_type_system->IsDefined(m_type);
  return true;
}

bool CompilerType::IsPolymorphicClass() const {
  if (IsValid()) {
    return m_type_system->IsPolymorphicClass(m_type);
  }
  return false;
}

bool CompilerType::IsPossibleDynamicType(CompilerType *dynamic_pointee_type,
                                         bool check_cplusplus,
                                         bool check_objc) const {
  if (IsValid())
    return m_type_system->IsPossibleDynamicType(m_type, dynamic_pointee_type,
                                                check_cplusplus, check_objc);
  return false;
}

bool CompilerType::IsScalarType() const {
  if (!IsValid())
    return false;

  return m_type_system->IsScalarType(m_type);
}

bool CompilerType::IsTypedefType() const {
  if (!IsValid())
    return false;
  return m_type_system->IsTypedefType(m_type);
}

bool CompilerType::IsVoidType() const {
  if (!IsValid())
    return false;
  return m_type_system->IsVoidType(m_type);
}

bool CompilerType::IsPointerToScalarType() const {
  if (!IsValid())
    return false;

  return IsPointerType() && GetPointeeType().IsScalarType();
}

bool CompilerType::IsArrayOfScalarType() const {
  CompilerType element_type;
  if (IsArrayType(&element_type, nullptr, nullptr))
    return element_type.IsScalarType();
  return false;
}

bool CompilerType::IsBeingDefined() const {
  if (!IsValid())
    return false;
  return m_type_system->IsBeingDefined(m_type);
}

//----------------------------------------------------------------------
// Type Completion
//----------------------------------------------------------------------

bool CompilerType::GetCompleteType() const {
  if (!IsValid())
    return false;
  return m_type_system->GetCompleteType(m_type);
}

//----------------------------------------------------------------------
// AST related queries
//----------------------------------------------------------------------
size_t CompilerType::GetPointerByteSize() const {
  if (m_type_system)
    return m_type_system->GetPointerByteSize();
  return 0;
}

ConstString CompilerType::GetConstQualifiedTypeName() const {
  return GetConstTypeName();
}

ConstString CompilerType::GetConstTypeName() const {
  if (IsValid()) {
    ConstString type_name(GetTypeName());
    if (type_name)
      return type_name;
  }
  return ConstString("<invalid>");
}

ConstString CompilerType::GetTypeName() const {
  if (IsValid()) {
    return m_type_system->GetTypeName(m_type);
  }
  return ConstString("<invalid>");
}

ConstString CompilerType::GetDisplayTypeName() const { return GetTypeName(); }

uint32_t CompilerType::GetTypeInfo(
    CompilerType *pointee_or_element_compiler_type) const {
  if (!IsValid())
    return 0;

  return m_type_system->GetTypeInfo(m_type, pointee_or_element_compiler_type);
}

lldb::LanguageType CompilerType::GetMinimumLanguage() {
  if (!IsValid())
    return lldb::eLanguageTypeC;

  return m_type_system->GetMinimumLanguage(m_type);
}

lldb::TypeClass CompilerType::GetTypeClass() const {
  if (!IsValid())
    return lldb::eTypeClassInvalid;

  return m_type_system->GetTypeClass(m_type);
}

void CompilerType::SetCompilerType(TypeSystem *type_system,
                                   lldb::opaque_compiler_type_t type) {
  m_type_system = type_system;
  m_type = type;
}

void CompilerType::SetCompilerType(clang::ASTContext *ast,
                                   clang::QualType qual_type) {
  m_type_system = ClangASTContext::GetASTContext(ast);
  m_type = qual_type.getAsOpaquePtr();
}

unsigned CompilerType::GetTypeQualifiers() const {
  if (IsValid())
    return m_type_system->GetTypeQualifiers(m_type);
  return 0;
}

//----------------------------------------------------------------------
// Creating related types
//----------------------------------------------------------------------

CompilerType CompilerType::GetArrayElementType(uint64_t *stride) const {
  if (IsValid()) {
    return m_type_system->GetArrayElementType(m_type, stride);
  }
  return CompilerType();
}

CompilerType CompilerType::GetArrayType(uint64_t size) const {
  if (IsValid()) {
    return m_type_system->GetArrayType(m_type, size);
  }
  return CompilerType();
}

CompilerType CompilerType::GetCanonicalType() const {
  if (IsValid())
    return m_type_system->GetCanonicalType(m_type);
  return CompilerType();
}

CompilerType CompilerType::GetFullyUnqualifiedType() const {
  if (IsValid())
    return m_type_system->GetFullyUnqualifiedType(m_type);
  return CompilerType();
}

int CompilerType::GetFunctionArgumentCount() const {
  if (IsValid()) {
    return m_type_system->GetFunctionArgumentCount(m_type);
  }
  return -1;
}

CompilerType CompilerType::GetFunctionArgumentTypeAtIndex(size_t idx) const {
  if (IsValid()) {
    return m_type_system->GetFunctionArgumentTypeAtIndex(m_type, idx);
  }
  return CompilerType();
}

CompilerType CompilerType::GetFunctionReturnType() const {
  if (IsValid()) {
    return m_type_system->GetFunctionReturnType(m_type);
  }
  return CompilerType();
}

size_t CompilerType::GetNumMemberFunctions() const {
  if (IsValid()) {
    return m_type_system->GetNumMemberFunctions(m_type);
  }
  return 0;
}

TypeMemberFunctionImpl CompilerType::GetMemberFunctionAtIndex(size_t idx) {
  if (IsValid()) {
    return m_type_system->GetMemberFunctionAtIndex(m_type, idx);
  }
  return TypeMemberFunctionImpl();
}

CompilerType CompilerType::GetNonReferenceType() const {
  if (IsValid())
    return m_type_system->GetNonReferenceType(m_type);
  return CompilerType();
}

CompilerType CompilerType::GetPointeeType() const {
  if (IsValid()) {
    return m_type_system->GetPointeeType(m_type);
  }
  return CompilerType();
}

CompilerType CompilerType::GetPointerType() const {
  if (IsValid()) {
    return m_type_system->GetPointerType(m_type);
  }
  return CompilerType();
}

CompilerType CompilerType::GetLValueReferenceType() const {
  if (IsValid())
    return m_type_system->GetLValueReferenceType(m_type);
  else
    return CompilerType();
}

CompilerType CompilerType::GetRValueReferenceType() const {
  if (IsValid())
    return m_type_system->GetRValueReferenceType(m_type);
  else
    return CompilerType();
}

CompilerType CompilerType::AddConstModifier() const {
  if (IsValid())
    return m_type_system->AddConstModifier(m_type);
  else
    return CompilerType();
}

CompilerType CompilerType::AddVolatileModifier() const {
  if (IsValid())
    return m_type_system->AddVolatileModifier(m_type);
  else
    return CompilerType();
}

CompilerType CompilerType::AddRestrictModifier() const {
  if (IsValid())
    return m_type_system->AddRestrictModifier(m_type);
  else
    return CompilerType();
}

CompilerType
CompilerType::CreateTypedef(const char *name,
                            const CompilerDeclContext &decl_ctx) const {
  if (IsValid())
    return m_type_system->CreateTypedef(m_type, name, decl_ctx);
  else
    return CompilerType();
}

CompilerType CompilerType::GetTypedefedType() const {
  if (IsValid())
    return m_type_system->GetTypedefedType(m_type);
  else
    return CompilerType();
}

//----------------------------------------------------------------------
// Create related types using the current type's AST
//----------------------------------------------------------------------

CompilerType
CompilerType::GetBasicTypeFromAST(lldb::BasicType basic_type) const {
  if (IsValid())
    return m_type_system->GetBasicTypeFromAST(basic_type);
  return CompilerType();
}
//----------------------------------------------------------------------
// Exploring the type
//----------------------------------------------------------------------

llvm::Optional<uint64_t>
CompilerType::GetBitSize(ExecutionContextScope *exe_scope) const {
  if (IsValid())
    return m_type_system->GetBitSize(m_type, exe_scope);
  return {};
}

llvm::Optional<uint64_t>
CompilerType::GetByteSize(ExecutionContextScope *exe_scope) const {
  if (llvm::Optional<uint64_t> bit_size = GetBitSize(exe_scope))
    return (*bit_size + 7) / 8;
  return {};
}

size_t CompilerType::GetTypeBitAlign() const {
  if (IsValid())
    return m_type_system->GetTypeBitAlign(m_type);
  return 0;
}

lldb::Encoding CompilerType::GetEncoding(uint64_t &count) const {
  if (!IsValid())
    return lldb::eEncodingInvalid;

  return m_type_system->GetEncoding(m_type, count);
}

lldb::Format CompilerType::GetFormat() const {
  if (!IsValid())
    return lldb::eFormatDefault;

  return m_type_system->GetFormat(m_type);
}

uint32_t CompilerType::GetNumChildren(bool omit_empty_base_classes,
                                      const ExecutionContext *exe_ctx) const {
  if (!IsValid())
    return 0;
  return m_type_system->GetNumChildren(m_type, omit_empty_base_classes,
                                       exe_ctx);
}

lldb::BasicType CompilerType::GetBasicTypeEnumeration() const {
  if (IsValid())
    return m_type_system->GetBasicTypeEnumeration(m_type);
  return eBasicTypeInvalid;
}

void CompilerType::ForEachEnumerator(
    std::function<bool(const CompilerType &integer_type,
                       const ConstString &name,
                       const llvm::APSInt &value)> const &callback) const {
  if (IsValid())
    return m_type_system->ForEachEnumerator(m_type, callback);
}

uint32_t CompilerType::GetNumFields() const {
  if (!IsValid())
    return 0;
  return m_type_system->GetNumFields(m_type);
}

CompilerType CompilerType::GetFieldAtIndex(size_t idx, std::string &name,
                                           uint64_t *bit_offset_ptr,
                                           uint32_t *bitfield_bit_size_ptr,
                                           bool *is_bitfield_ptr) const {
  if (!IsValid())
    return CompilerType();
  return m_type_system->GetFieldAtIndex(m_type, idx, name, bit_offset_ptr,
                                        bitfield_bit_size_ptr, is_bitfield_ptr);
}

uint32_t CompilerType::GetNumDirectBaseClasses() const {
  if (IsValid())
    return m_type_system->GetNumDirectBaseClasses(m_type);
  return 0;
}

uint32_t CompilerType::GetNumVirtualBaseClasses() const {
  if (IsValid())
    return m_type_system->GetNumVirtualBaseClasses(m_type);
  return 0;
}

CompilerType
CompilerType::GetDirectBaseClassAtIndex(size_t idx,
                                        uint32_t *bit_offset_ptr) const {
  if (IsValid())
    return m_type_system->GetDirectBaseClassAtIndex(m_type, idx,
                                                    bit_offset_ptr);
  return CompilerType();
}

CompilerType
CompilerType::GetVirtualBaseClassAtIndex(size_t idx,
                                         uint32_t *bit_offset_ptr) const {
  if (IsValid())
    return m_type_system->GetVirtualBaseClassAtIndex(m_type, idx,
                                                     bit_offset_ptr);
  return CompilerType();
}

uint32_t CompilerType::GetIndexOfFieldWithName(
    const char *name, CompilerType *field_compiler_type_ptr,
    uint64_t *bit_offset_ptr, uint32_t *bitfield_bit_size_ptr,
    bool *is_bitfield_ptr) const {
  unsigned count = GetNumFields();
  std::string field_name;
  for (unsigned index = 0; index < count; index++) {
    CompilerType field_compiler_type(
        GetFieldAtIndex(index, field_name, bit_offset_ptr,
                        bitfield_bit_size_ptr, is_bitfield_ptr));
    if (strcmp(field_name.c_str(), name) == 0) {
      if (field_compiler_type_ptr)
        *field_compiler_type_ptr = field_compiler_type;
      return index;
    }
  }
  return UINT32_MAX;
}

CompilerType CompilerType::GetChildCompilerTypeAtIndex(
    ExecutionContext *exe_ctx, size_t idx, bool transparent_pointers,
    bool omit_empty_base_classes, bool ignore_array_bounds,
    std::string &child_name, uint32_t &child_byte_size,
    int32_t &child_byte_offset, uint32_t &child_bitfield_bit_size,
    uint32_t &child_bitfield_bit_offset, bool &child_is_base_class,
    bool &child_is_deref_of_parent, ValueObject *valobj,
    uint64_t &language_flags) const {
  if (!IsValid())
    return CompilerType();
  return m_type_system->GetChildCompilerTypeAtIndex(
      m_type, exe_ctx, idx, transparent_pointers, omit_empty_base_classes,
      ignore_array_bounds, child_name, child_byte_size, child_byte_offset,
      child_bitfield_bit_size, child_bitfield_bit_offset, child_is_base_class,
      child_is_deref_of_parent, valobj, language_flags);
}

// Look for a child member (doesn't include base classes, but it does include
// their members) in the type hierarchy. Returns an index path into
// "clang_type" on how to reach the appropriate member.
//
//    class A
//    {
//    public:
//        int m_a;
//        int m_b;
//    };
//
//    class B
//    {
//    };
//
//    class C :
//        public B,
//        public A
//    {
//    };
//
// If we have a clang type that describes "class C", and we wanted to looked
// "m_b" in it:
//
// With omit_empty_base_classes == false we would get an integer array back
// with: { 1,  1 } The first index 1 is the child index for "class A" within
// class C The second index 1 is the child index for "m_b" within class A
//
// With omit_empty_base_classes == true we would get an integer array back
// with: { 0,  1 } The first index 0 is the child index for "class A" within
// class C (since class B doesn't have any members it doesn't count) The second
// index 1 is the child index for "m_b" within class A

size_t CompilerType::GetIndexOfChildMemberWithName(
    const char *name, bool omit_empty_base_classes,
    std::vector<uint32_t> &child_indexes) const {
  if (IsValid() && name && name[0]) {
    return m_type_system->GetIndexOfChildMemberWithName(
        m_type, name, omit_empty_base_classes, child_indexes);
  }
  return 0;
}

size_t CompilerType::GetNumTemplateArguments() const {
  if (IsValid()) {
    return m_type_system->GetNumTemplateArguments(m_type);
  }
  return 0;
}

TemplateArgumentKind CompilerType::GetTemplateArgumentKind(size_t idx) const {
  if (IsValid())
    return m_type_system->GetTemplateArgumentKind(m_type, idx);
  return eTemplateArgumentKindNull;
}

CompilerType CompilerType::GetTypeTemplateArgument(size_t idx) const {
  if (IsValid()) {
    return m_type_system->GetTypeTemplateArgument(m_type, idx);
  }
  return CompilerType();
}

llvm::Optional<CompilerType::IntegralTemplateArgument>
CompilerType::GetIntegralTemplateArgument(size_t idx) const {
  if (IsValid())
    return m_type_system->GetIntegralTemplateArgument(m_type, idx);
  return llvm::None;
}

CompilerType CompilerType::GetTypeForFormatters() const {
  if (IsValid())
    return m_type_system->GetTypeForFormatters(m_type);
  return CompilerType();
}

LazyBool CompilerType::ShouldPrintAsOneLiner(ValueObject *valobj) const {
  if (IsValid())
    return m_type_system->ShouldPrintAsOneLiner(m_type, valobj);
  return eLazyBoolCalculate;
}

bool CompilerType::IsMeaninglessWithoutDynamicResolution() const {
  if (IsValid())
    return m_type_system->IsMeaninglessWithoutDynamicResolution(m_type);
  return false;
}

// Get the index of the child of "clang_type" whose name matches. This function
// doesn't descend into the children, but only looks one level deep and name
// matches can include base class names.

uint32_t
CompilerType::GetIndexOfChildWithName(const char *name,
                                      bool omit_empty_base_classes) const {
  if (IsValid() && name && name[0]) {
    return m_type_system->GetIndexOfChildWithName(m_type, name,
                                                  omit_empty_base_classes);
  }
  return UINT32_MAX;
}

size_t CompilerType::ConvertStringToFloatValue(const char *s, uint8_t *dst,
                                               size_t dst_size) const {
  if (IsValid())
    return m_type_system->ConvertStringToFloatValue(m_type, s, dst, dst_size);
  return 0;
}

//----------------------------------------------------------------------
// Dumping types
//----------------------------------------------------------------------
#define DEPTH_INCREMENT 2

void CompilerType::DumpValue(ExecutionContext *exe_ctx, Stream *s,
                             lldb::Format format, const DataExtractor &data,
                             lldb::offset_t data_byte_offset,
                             size_t data_byte_size, uint32_t bitfield_bit_size,
                             uint32_t bitfield_bit_offset, bool show_types,
                             bool show_summary, bool verbose, uint32_t depth) {
  if (!IsValid())
    return;
  m_type_system->DumpValue(m_type, exe_ctx, s, format, data, data_byte_offset,
                           data_byte_size, bitfield_bit_size,
                           bitfield_bit_offset, show_types, show_summary,
                           verbose, depth);
}

bool CompilerType::DumpTypeValue(Stream *s, lldb::Format format,
                                 const DataExtractor &data,
                                 lldb::offset_t byte_offset, size_t byte_size,
                                 uint32_t bitfield_bit_size,
                                 uint32_t bitfield_bit_offset,
                                 ExecutionContextScope *exe_scope) {
  if (!IsValid())
    return false;
  return m_type_system->DumpTypeValue(m_type, s, format, data, byte_offset,
                                      byte_size, bitfield_bit_size,
                                      bitfield_bit_offset, exe_scope);
}

void CompilerType::DumpSummary(ExecutionContext *exe_ctx, Stream *s,
                               const DataExtractor &data,
                               lldb::offset_t data_byte_offset,
                               size_t data_byte_size) {
  if (IsValid())
    m_type_system->DumpSummary(m_type, exe_ctx, s, data, data_byte_offset,
                               data_byte_size);
}

void CompilerType::DumpTypeDescription() const {
  if (IsValid())
    m_type_system->DumpTypeDescription(m_type);
}

void CompilerType::DumpTypeDescription(Stream *s) const {
  if (IsValid()) {
    m_type_system->DumpTypeDescription(m_type, s);
  }
}

bool CompilerType::GetValueAsScalar(const lldb_private::DataExtractor &data,
                                    lldb::offset_t data_byte_offset,
                                    size_t data_byte_size,
                                    Scalar &value) const {
  if (!IsValid())
    return false;

  if (IsAggregateType()) {
    return false; // Aggregate types don't have scalar values
  } else {
    uint64_t count = 0;
    lldb::Encoding encoding = GetEncoding(count);

    if (encoding == lldb::eEncodingInvalid || count != 1)
      return false;

    llvm::Optional<uint64_t> byte_size = GetByteSize(nullptr);
    if (!byte_size)
      return false;
    lldb::offset_t offset = data_byte_offset;
    switch (encoding) {
    case lldb::eEncodingInvalid:
      break;
    case lldb::eEncodingVector:
      break;
    case lldb::eEncodingUint:
      if (*byte_size <= sizeof(unsigned long long)) {
        uint64_t uval64 = data.GetMaxU64(&offset, *byte_size);
        if (*byte_size <= sizeof(unsigned int)) {
          value = (unsigned int)uval64;
          return true;
        } else if (*byte_size <= sizeof(unsigned long)) {
          value = (unsigned long)uval64;
          return true;
        } else if (*byte_size <= sizeof(unsigned long long)) {
          value = (unsigned long long)uval64;
          return true;
        } else
          value.Clear();
      }
      break;

    case lldb::eEncodingSint:
      if (*byte_size <= sizeof(long long)) {
        int64_t sval64 = data.GetMaxS64(&offset, *byte_size);
        if (*byte_size <= sizeof(int)) {
          value = (int)sval64;
          return true;
        } else if (*byte_size <= sizeof(long)) {
          value = (long)sval64;
          return true;
        } else if (*byte_size <= sizeof(long long)) {
          value = (long long)sval64;
          return true;
        } else
          value.Clear();
      }
      break;

    case lldb::eEncodingIEEE754:
      if (*byte_size <= sizeof(long double)) {
        uint32_t u32;
        uint64_t u64;
        if (*byte_size == sizeof(float)) {
          if (sizeof(float) == sizeof(uint32_t)) {
            u32 = data.GetU32(&offset);
            value = *((float *)&u32);
            return true;
          } else if (sizeof(float) == sizeof(uint64_t)) {
            u64 = data.GetU64(&offset);
            value = *((float *)&u64);
            return true;
          }
        } else if (*byte_size == sizeof(double)) {
          if (sizeof(double) == sizeof(uint32_t)) {
            u32 = data.GetU32(&offset);
            value = *((double *)&u32);
            return true;
          } else if (sizeof(double) == sizeof(uint64_t)) {
            u64 = data.GetU64(&offset);
            value = *((double *)&u64);
            return true;
          }
        } else if (*byte_size == sizeof(long double)) {
          if (sizeof(long double) == sizeof(uint32_t)) {
            u32 = data.GetU32(&offset);
            value = *((long double *)&u32);
            return true;
          } else if (sizeof(long double) == sizeof(uint64_t)) {
            u64 = data.GetU64(&offset);
            value = *((long double *)&u64);
            return true;
          }
        }
      }
      break;
    }
  }
  return false;
}

bool CompilerType::SetValueFromScalar(const Scalar &value, Stream &strm) {
  if (!IsValid())
    return false;

  // Aggregate types don't have scalar values
  if (!IsAggregateType()) {
    strm.GetFlags().Set(Stream::eBinary);
    uint64_t count = 0;
    lldb::Encoding encoding = GetEncoding(count);

    if (encoding == lldb::eEncodingInvalid || count != 1)
      return false;

    llvm::Optional<uint64_t> bit_width = GetBitSize(nullptr);
    if (!bit_width)
      return false;

    // This function doesn't currently handle non-byte aligned assignments
    if ((*bit_width % 8) != 0)
      return false;

    const uint64_t byte_size = (*bit_width + 7) / 8;
    switch (encoding) {
    case lldb::eEncodingInvalid:
      break;
    case lldb::eEncodingVector:
      break;
    case lldb::eEncodingUint:
      switch (byte_size) {
      case 1:
        strm.PutHex8(value.UInt());
        return true;
      case 2:
        strm.PutHex16(value.UInt());
        return true;
      case 4:
        strm.PutHex32(value.UInt());
        return true;
      case 8:
        strm.PutHex64(value.ULongLong());
        return true;
      default:
        break;
      }
      break;

    case lldb::eEncodingSint:
      switch (byte_size) {
      case 1:
        strm.PutHex8(value.SInt());
        return true;
      case 2:
        strm.PutHex16(value.SInt());
        return true;
      case 4:
        strm.PutHex32(value.SInt());
        return true;
      case 8:
        strm.PutHex64(value.SLongLong());
        return true;
      default:
        break;
      }
      break;

    case lldb::eEncodingIEEE754:
      if (byte_size <= sizeof(long double)) {
        if (byte_size == sizeof(float)) {
          strm.PutFloat(value.Float());
          return true;
        } else if (byte_size == sizeof(double)) {
          strm.PutDouble(value.Double());
          return true;
        } else if (byte_size == sizeof(long double)) {
          strm.PutDouble(value.LongDouble());
          return true;
        }
      }
      break;
    }
  }
  return false;
}

bool CompilerType::ReadFromMemory(lldb_private::ExecutionContext *exe_ctx,
                                  lldb::addr_t addr, AddressType address_type,
                                  lldb_private::DataExtractor &data) {
  if (!IsValid())
    return false;

  // Can't convert a file address to anything valid without more context (which
  // Module it came from)
  if (address_type == eAddressTypeFile)
    return false;

  if (!GetCompleteType())
    return false;

  auto byte_size =
      GetByteSize(exe_ctx ? exe_ctx->GetBestExecutionContextScope() : NULL);
  if (!byte_size)
    return false;

  if (data.GetByteSize() < *byte_size) {
    lldb::DataBufferSP data_sp(new DataBufferHeap(*byte_size, '\0'));
    data.SetData(data_sp);
  }

  uint8_t *dst = const_cast<uint8_t *>(data.PeekData(0, *byte_size));
  if (dst != nullptr) {
    if (address_type == eAddressTypeHost) {
      if (addr == 0)
        return false;
      // The address is an address in this process, so just copy it
      memcpy(dst, reinterpret_cast<uint8_t *>(addr), *byte_size);
      return true;
    } else {
      Process *process = nullptr;
      if (exe_ctx)
        process = exe_ctx->GetProcessPtr();
      if (process) {
        Status error;
        return process->ReadMemory(addr, dst, *byte_size, error) == *byte_size;
      }
    }
  }
  return false;
}

bool CompilerType::WriteToMemory(lldb_private::ExecutionContext *exe_ctx,
                                 lldb::addr_t addr, AddressType address_type,
                                 StreamString &new_value) {
  if (!IsValid())
    return false;

  // Can't convert a file address to anything valid without more context (which
  // Module it came from)
  if (address_type == eAddressTypeFile)
    return false;

  if (!GetCompleteType())
    return false;

  auto byte_size =
      GetByteSize(exe_ctx ? exe_ctx->GetBestExecutionContextScope() : NULL);
  if (!byte_size)
    return false;

  if (*byte_size > 0) {
    if (address_type == eAddressTypeHost) {
      // The address is an address in this process, so just copy it
      memcpy((void *)addr, new_value.GetData(), *byte_size);
      return true;
    } else {
      Process *process = nullptr;
      if (exe_ctx)
        process = exe_ctx->GetProcessPtr();
      if (process) {
        Status error;
        return process->WriteMemory(addr, new_value.GetData(), *byte_size,
                                    error) == *byte_size;
      }
    }
  }
  return false;
}

bool lldb_private::operator==(const lldb_private::CompilerType &lhs,
                              const lldb_private::CompilerType &rhs) {
  return lhs.GetTypeSystem() == rhs.GetTypeSystem() &&
         lhs.GetOpaqueQualType() == rhs.GetOpaqueQualType();
}

bool lldb_private::operator!=(const lldb_private::CompilerType &lhs,
                              const lldb_private::CompilerType &rhs) {
  return !(lhs == rhs);
}
