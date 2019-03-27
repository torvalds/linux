//===-- BlockPointer.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "BlockPointer.h"

#include "lldb/Core/ValueObject.h"
#include "lldb/DataFormatters/FormattersHelpers.h"
#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Symbol/ClangASTImporter.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/TypeSystem.h"
#include "lldb/Target/Target.h"

#include "lldb/Utility/LLDBAssert.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;

namespace lldb_private {
namespace formatters {

class BlockPointerSyntheticFrontEnd : public SyntheticChildrenFrontEnd {
public:
  BlockPointerSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
      : SyntheticChildrenFrontEnd(*valobj_sp), m_block_struct_type() {
    CompilerType block_pointer_type(m_backend.GetCompilerType());
    CompilerType function_pointer_type;
    block_pointer_type.IsBlockPointerType(&function_pointer_type);

    TargetSP target_sp(m_backend.GetTargetSP());

    if (!target_sp) {
      return;
    }

    Status err;
    TypeSystem *type_system = target_sp->GetScratchTypeSystemForLanguage(
        &err, lldb::eLanguageTypeC_plus_plus);

    if (!err.Success() || !type_system) {
      return;
    }

    ClangASTContext *clang_ast_context =
        llvm::dyn_cast<ClangASTContext>(type_system);

    if (!clang_ast_context) {
      return;
    }

    ClangASTImporterSP clang_ast_importer = target_sp->GetClangASTImporter();

    if (!clang_ast_importer) {
      return;
    }

    const char *const isa_name("__isa");
    const CompilerType isa_type =
        clang_ast_context->GetBasicType(lldb::eBasicTypeObjCClass);
    const char *const flags_name("__flags");
    const CompilerType flags_type =
        clang_ast_context->GetBasicType(lldb::eBasicTypeInt);
    const char *const reserved_name("__reserved");
    const CompilerType reserved_type =
        clang_ast_context->GetBasicType(lldb::eBasicTypeInt);
    const char *const FuncPtr_name("__FuncPtr");
    const CompilerType FuncPtr_type =
        clang_ast_importer->CopyType(*clang_ast_context, function_pointer_type);

    m_block_struct_type = clang_ast_context->CreateStructForIdentifier(
        ConstString(), {{isa_name, isa_type},
                        {flags_name, flags_type},
                        {reserved_name, reserved_type},
                        {FuncPtr_name, FuncPtr_type}});
  }

  ~BlockPointerSyntheticFrontEnd() override = default;

  size_t CalculateNumChildren() override {
    const bool omit_empty_base_classes = false;
    return m_block_struct_type.GetNumChildren(omit_empty_base_classes, nullptr);
  }

  lldb::ValueObjectSP GetChildAtIndex(size_t idx) override {
    if (!m_block_struct_type.IsValid()) {
      return lldb::ValueObjectSP();
    }

    if (idx >= CalculateNumChildren()) {
      return lldb::ValueObjectSP();
    }

    const bool thread_and_frame_only_if_stopped = true;
    ExecutionContext exe_ctx = m_backend.GetExecutionContextRef().Lock(
        thread_and_frame_only_if_stopped);
    const bool transparent_pointers = false;
    const bool omit_empty_base_classes = false;
    const bool ignore_array_bounds = false;
    ValueObject *value_object = nullptr;

    std::string child_name;
    uint32_t child_byte_size = 0;
    int32_t child_byte_offset = 0;
    uint32_t child_bitfield_bit_size = 0;
    uint32_t child_bitfield_bit_offset = 0;
    bool child_is_base_class = false;
    bool child_is_deref_of_parent = false;
    uint64_t language_flags = 0;

    const CompilerType child_type =
        m_block_struct_type.GetChildCompilerTypeAtIndex(
            &exe_ctx, idx, transparent_pointers, omit_empty_base_classes,
            ignore_array_bounds, child_name, child_byte_size, child_byte_offset,
            child_bitfield_bit_size, child_bitfield_bit_offset,
            child_is_base_class, child_is_deref_of_parent, value_object,
            language_flags);

    ValueObjectSP struct_pointer_sp =
        m_backend.Cast(m_block_struct_type.GetPointerType());

    if (!struct_pointer_sp) {
      return lldb::ValueObjectSP();
    }

    Status err;
    ValueObjectSP struct_sp = struct_pointer_sp->Dereference(err);

    if (!struct_sp || !err.Success()) {
      return lldb::ValueObjectSP();
    }

    ValueObjectSP child_sp(struct_sp->GetSyntheticChildAtOffset(
        child_byte_offset, child_type, true,
        ConstString(child_name.c_str(), child_name.size())));

    return child_sp;
  }

  // return true if this object is now safe to use forever without ever
  // updating again; the typical (and tested) answer here is 'false'
  bool Update() override { return false; }

  // maybe return false if the block pointer is, say, null
  bool MightHaveChildren() override { return true; }

  size_t GetIndexOfChildWithName(const ConstString &name) override {
    if (!m_block_struct_type.IsValid())
      return UINT32_MAX;

    const bool omit_empty_base_classes = false;
    return m_block_struct_type.GetIndexOfChildWithName(name.AsCString(),
                                                       omit_empty_base_classes);
  }

private:
  CompilerType m_block_struct_type;
};

} // namespace formatters
} // namespace lldb_private

bool lldb_private::formatters::BlockPointerSummaryProvider(
    ValueObject &valobj, Stream &s, const TypeSummaryOptions &) {
  lldb_private::SyntheticChildrenFrontEnd *synthetic_children =
      BlockPointerSyntheticFrontEndCreator(nullptr, valobj.GetSP());
  if (!synthetic_children) {
    return false;
  }

  synthetic_children->Update();

  static const ConstString s_FuncPtr_name("__FuncPtr");

  lldb::ValueObjectSP child_sp = synthetic_children->GetChildAtIndex(
      synthetic_children->GetIndexOfChildWithName(s_FuncPtr_name));

  if (!child_sp) {
    return false;
  }

  lldb::ValueObjectSP qualified_child_representation_sp =
      child_sp->GetQualifiedRepresentationIfAvailable(
          lldb::eDynamicDontRunTarget, true);

  const char *child_value =
      qualified_child_representation_sp->GetValueAsCString();

  s.Printf("%s", child_value);

  return true;
}

lldb_private::SyntheticChildrenFrontEnd *
lldb_private::formatters::BlockPointerSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  if (!valobj_sp)
    return nullptr;
  return new BlockPointerSyntheticFrontEnd(valobj_sp);
}
