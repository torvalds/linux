//===-- LibCxxUnorderedMap.cpp ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "LibCxx.h"

#include "lldb/Core/ValueObject.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/DataFormatters/FormattersHelpers.h"
#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;

namespace lldb_private {
namespace formatters {
class LibcxxStdUnorderedMapSyntheticFrontEnd
    : public SyntheticChildrenFrontEnd {
public:
  LibcxxStdUnorderedMapSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp);

  ~LibcxxStdUnorderedMapSyntheticFrontEnd() override = default;

  size_t CalculateNumChildren() override;

  lldb::ValueObjectSP GetChildAtIndex(size_t idx) override;

  bool Update() override;

  bool MightHaveChildren() override;

  size_t GetIndexOfChildWithName(const ConstString &name) override;

private:
  CompilerType m_element_type;
  CompilerType m_node_type;
  ValueObject *m_tree;
  size_t m_num_elements;
  ValueObject *m_next_element;
  std::vector<std::pair<ValueObject *, uint64_t>> m_elements_cache;
};
} // namespace formatters
} // namespace lldb_private

lldb_private::formatters::LibcxxStdUnorderedMapSyntheticFrontEnd::
    LibcxxStdUnorderedMapSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp), m_element_type(), m_tree(nullptr),
      m_num_elements(0), m_next_element(nullptr), m_elements_cache() {
  if (valobj_sp)
    Update();
}

size_t lldb_private::formatters::LibcxxStdUnorderedMapSyntheticFrontEnd::
    CalculateNumChildren() {
  if (m_num_elements != UINT32_MAX)
    return m_num_elements;
  return 0;
}

lldb::ValueObjectSP lldb_private::formatters::
    LibcxxStdUnorderedMapSyntheticFrontEnd::GetChildAtIndex(size_t idx) {
  if (idx >= CalculateNumChildren())
    return lldb::ValueObjectSP();
  if (m_tree == nullptr)
    return lldb::ValueObjectSP();

  while (idx >= m_elements_cache.size()) {
    if (m_next_element == nullptr)
      return lldb::ValueObjectSP();

    Status error;
    ValueObjectSP node_sp = m_next_element->Dereference(error);
    if (!node_sp || error.Fail())
      return lldb::ValueObjectSP();

    ValueObjectSP value_sp =
        node_sp->GetChildMemberWithName(ConstString("__value_"), true);
    ValueObjectSP hash_sp =
        node_sp->GetChildMemberWithName(ConstString("__hash_"), true);
    if (!hash_sp || !value_sp) {
      if (!m_element_type) {
        auto p1_sp = m_backend.GetChildAtNamePath({ConstString("__table_"),
                                                   ConstString("__p1_")});
        if (!p1_sp)
          return nullptr;

        ValueObjectSP first_sp = nullptr;
        switch (p1_sp->GetCompilerType().GetNumDirectBaseClasses()) {
        case 1:
          // Assume a pre llvm r300140 __compressed_pair implementation:
          first_sp = p1_sp->GetChildMemberWithName(ConstString("__first_"),
                                                   true);
          break;
        case 2: {
          // Assume a post llvm r300140 __compressed_pair implementation:
          ValueObjectSP first_elem_parent_sp =
            p1_sp->GetChildAtIndex(0, true);
          first_sp = p1_sp->GetChildMemberWithName(ConstString("__value_"),
                                                   true);
          break;
        }
        default:
          return nullptr;
        }

        if (!first_sp)
          return nullptr;
        m_element_type = first_sp->GetCompilerType();
        m_element_type = m_element_type.GetTypeTemplateArgument(0);
        m_element_type = m_element_type.GetPointeeType();
        m_node_type = m_element_type;
        m_element_type = m_element_type.GetTypeTemplateArgument(0);
        std::string name;
        m_element_type =
            m_element_type.GetFieldAtIndex(0, name, nullptr, nullptr, nullptr);
        m_element_type = m_element_type.GetTypedefedType();
      }
      if (!m_node_type)
        return nullptr;
      node_sp = node_sp->Cast(m_node_type);
      value_sp = node_sp->GetChildMemberWithName(ConstString("__value_"), true);
      hash_sp = node_sp->GetChildMemberWithName(ConstString("__hash_"), true);
      if (!value_sp || !hash_sp)
        return nullptr;
    }
    m_elements_cache.push_back(
        {value_sp.get(), hash_sp->GetValueAsUnsigned(0)});
    m_next_element =
        node_sp->GetChildMemberWithName(ConstString("__next_"), true).get();
    if (!m_next_element || m_next_element->GetValueAsUnsigned(0) == 0)
      m_next_element = nullptr;
  }

  std::pair<ValueObject *, uint64_t> val_hash = m_elements_cache[idx];
  if (!val_hash.first)
    return lldb::ValueObjectSP();
  StreamString stream;
  stream.Printf("[%" PRIu64 "]", (uint64_t)idx);
  DataExtractor data;
  Status error;
  val_hash.first->GetData(data, error);
  if (error.Fail())
    return lldb::ValueObjectSP();
  const bool thread_and_frame_only_if_stopped = true;
  ExecutionContext exe_ctx = val_hash.first->GetExecutionContextRef().Lock(
      thread_and_frame_only_if_stopped);
  return CreateValueObjectFromData(stream.GetString(), data, exe_ctx,
                                   val_hash.first->GetCompilerType());
}

bool lldb_private::formatters::LibcxxStdUnorderedMapSyntheticFrontEnd::
    Update() {
  m_num_elements = UINT32_MAX;
  m_next_element = nullptr;
  m_elements_cache.clear();
  ValueObjectSP table_sp =
      m_backend.GetChildMemberWithName(ConstString("__table_"), true);
  if (!table_sp)
    return false;

  ValueObjectSP p2_sp = table_sp->GetChildMemberWithName(
    ConstString("__p2_"), true);
  ValueObjectSP num_elements_sp = nullptr;
  llvm::SmallVector<ConstString, 3> next_path;
  switch (p2_sp->GetCompilerType().GetNumDirectBaseClasses()) {
  case 1:
    // Assume a pre llvm r300140 __compressed_pair implementation:
    num_elements_sp = p2_sp->GetChildMemberWithName(
      ConstString("__first_"), true);
    next_path.append({ConstString("__p1_"), ConstString("__first_"),
                      ConstString("__next_")});
    break;
  case 2: {
    // Assume a post llvm r300140 __compressed_pair implementation:
    ValueObjectSP first_elem_parent = p2_sp->GetChildAtIndex(0, true);
    num_elements_sp = first_elem_parent->GetChildMemberWithName(
      ConstString("__value_"), true);
    next_path.append({ConstString("__p1_"), ConstString("__value_"),
                      ConstString("__next_")});
    break;
  }
  default:
    return false;
  }

  if (!num_elements_sp)
    return false;
  m_num_elements = num_elements_sp->GetValueAsUnsigned(0);
  m_tree = table_sp->GetChildAtNamePath(next_path).get();
  if (m_num_elements > 0)
    m_next_element =
        table_sp->GetChildAtNamePath(next_path).get();
  return false;
}

bool lldb_private::formatters::LibcxxStdUnorderedMapSyntheticFrontEnd::
    MightHaveChildren() {
  return true;
}

size_t lldb_private::formatters::LibcxxStdUnorderedMapSyntheticFrontEnd::
    GetIndexOfChildWithName(const ConstString &name) {
  return ExtractIndexFromString(name.GetCString());
}

SyntheticChildrenFrontEnd *
lldb_private::formatters::LibcxxStdUnorderedMapSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  return (valobj_sp ? new LibcxxStdUnorderedMapSyntheticFrontEnd(valobj_sp)
                    : nullptr);
}
