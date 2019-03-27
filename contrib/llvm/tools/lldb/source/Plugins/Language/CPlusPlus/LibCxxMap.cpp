//===-- LibCxxMap.cpp -------------------------------------------*- C++ -*-===//
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

class MapEntry {
public:
  MapEntry() = default;
  explicit MapEntry(ValueObjectSP entry_sp) : m_entry_sp(entry_sp) {}
  MapEntry(const MapEntry &rhs) = default;
  explicit MapEntry(ValueObject *entry)
      : m_entry_sp(entry ? entry->GetSP() : ValueObjectSP()) {}

  ValueObjectSP left() const {
    static ConstString g_left("__left_");
    if (!m_entry_sp)
      return m_entry_sp;
    return m_entry_sp->GetSyntheticChildAtOffset(
        0, m_entry_sp->GetCompilerType(), true);
  }

  ValueObjectSP right() const {
    static ConstString g_right("__right_");
    if (!m_entry_sp)
      return m_entry_sp;
    return m_entry_sp->GetSyntheticChildAtOffset(
        m_entry_sp->GetProcessSP()->GetAddressByteSize(),
        m_entry_sp->GetCompilerType(), true);
  }

  ValueObjectSP parent() const {
    static ConstString g_parent("__parent_");
    if (!m_entry_sp)
      return m_entry_sp;
    return m_entry_sp->GetSyntheticChildAtOffset(
        2 * m_entry_sp->GetProcessSP()->GetAddressByteSize(),
        m_entry_sp->GetCompilerType(), true);
  }

  uint64_t value() const {
    if (!m_entry_sp)
      return 0;
    return m_entry_sp->GetValueAsUnsigned(0);
  }

  bool error() const {
    if (!m_entry_sp)
      return true;
    return m_entry_sp->GetError().Fail();
  }

  bool null() const { return (value() == 0); }

  ValueObjectSP GetEntry() const { return m_entry_sp; }

  void SetEntry(ValueObjectSP entry) { m_entry_sp = entry; }

  bool operator==(const MapEntry &rhs) const {
    return (rhs.m_entry_sp.get() == m_entry_sp.get());
  }

private:
  ValueObjectSP m_entry_sp;
};

class MapIterator {
public:
  MapIterator() = default;
  MapIterator(MapEntry entry, size_t depth = 0)
      : m_entry(entry), m_max_depth(depth), m_error(false) {}
  MapIterator(ValueObjectSP entry, size_t depth = 0)
      : m_entry(entry), m_max_depth(depth), m_error(false) {}
  MapIterator(const MapIterator &rhs)
      : m_entry(rhs.m_entry), m_max_depth(rhs.m_max_depth), m_error(false) {}
  MapIterator(ValueObject *entry, size_t depth = 0)
      : m_entry(entry), m_max_depth(depth), m_error(false) {}

  ValueObjectSP value() { return m_entry.GetEntry(); }

  ValueObjectSP advance(size_t count) {
    ValueObjectSP fail;
    if (m_error)
      return fail;
    size_t steps = 0;
    while (count > 0) {
      next();
      count--, steps++;
      if (m_error || m_entry.null() || (steps > m_max_depth))
        return fail;
    }
    return m_entry.GetEntry();
  }

protected:
  void next() {
    if (m_entry.null())
      return;
    MapEntry right(m_entry.right());
    if (!right.null()) {
      m_entry = tree_min(std::move(right));
      return;
    }
    size_t steps = 0;
    while (!is_left_child(m_entry)) {
      if (m_entry.error()) {
        m_error = true;
        return;
      }
      m_entry.SetEntry(m_entry.parent());
      steps++;
      if (steps > m_max_depth) {
        m_entry = MapEntry();
        return;
      }
    }
    m_entry = MapEntry(m_entry.parent());
  }

private:
  MapEntry tree_min(MapEntry &&x) {
    if (x.null())
      return MapEntry();
    MapEntry left(x.left());
    size_t steps = 0;
    while (!left.null()) {
      if (left.error()) {
        m_error = true;
        return MapEntry();
      }
      x = left;
      left.SetEntry(x.left());
      steps++;
      if (steps > m_max_depth)
        return MapEntry();
    }
    return x;
  }

  bool is_left_child(const MapEntry &x) {
    if (x.null())
      return false;
    MapEntry rhs(x.parent());
    rhs.SetEntry(rhs.left());
    return x.value() == rhs.value();
  }

  MapEntry m_entry;
  size_t m_max_depth;
  bool m_error;
};

namespace lldb_private {
namespace formatters {
class LibcxxStdMapSyntheticFrontEnd : public SyntheticChildrenFrontEnd {
public:
  LibcxxStdMapSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp);

  ~LibcxxStdMapSyntheticFrontEnd() override = default;

  size_t CalculateNumChildren() override;

  lldb::ValueObjectSP GetChildAtIndex(size_t idx) override;

  bool Update() override;

  bool MightHaveChildren() override;

  size_t GetIndexOfChildWithName(const ConstString &name) override;

private:
  bool GetDataType();

  void GetValueOffset(const lldb::ValueObjectSP &node);

  ValueObject *m_tree;
  ValueObject *m_root_node;
  CompilerType m_element_type;
  uint32_t m_skip_size;
  size_t m_count;
  std::map<size_t, MapIterator> m_iterators;
};
} // namespace formatters
} // namespace lldb_private

lldb_private::formatters::LibcxxStdMapSyntheticFrontEnd::
    LibcxxStdMapSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp), m_tree(nullptr),
      m_root_node(nullptr), m_element_type(), m_skip_size(UINT32_MAX),
      m_count(UINT32_MAX), m_iterators() {
  if (valobj_sp)
    Update();
}

size_t lldb_private::formatters::LibcxxStdMapSyntheticFrontEnd::
    CalculateNumChildren() {
  static ConstString g___pair3_("__pair3_");
  static ConstString g___first_("__first_");
  static ConstString g___value_("__value_");

  if (m_count != UINT32_MAX)
    return m_count;
  if (m_tree == nullptr)
    return 0;
  ValueObjectSP m_item(m_tree->GetChildMemberWithName(g___pair3_, true));
  if (!m_item)
    return 0;

  switch (m_item->GetCompilerType().GetNumDirectBaseClasses()) {
  case 1:
    // Assume a pre llvm r300140 __compressed_pair implementation:
    m_item = m_item->GetChildMemberWithName(g___first_, true);
    break;
  case 2: {
    // Assume a post llvm r300140 __compressed_pair implementation:
    ValueObjectSP first_elem_parent = m_item->GetChildAtIndex(0, true);
    m_item = first_elem_parent->GetChildMemberWithName(g___value_, true);
    break;
  }
  default:
    return false;
  }

  if (!m_item)
    return 0;
  m_count = m_item->GetValueAsUnsigned(0);
  return m_count;
}

bool lldb_private::formatters::LibcxxStdMapSyntheticFrontEnd::GetDataType() {
  static ConstString g___value_("__value_");
  static ConstString g_tree_("__tree_");
  static ConstString g_pair3("__pair3_");

  if (m_element_type.GetOpaqueQualType() && m_element_type.GetTypeSystem())
    return true;
  m_element_type.Clear();
  ValueObjectSP deref;
  Status error;
  deref = m_root_node->Dereference(error);
  if (!deref || error.Fail())
    return false;
  deref = deref->GetChildMemberWithName(g___value_, true);
  if (deref) {
    m_element_type = deref->GetCompilerType();
    return true;
  }
  deref = m_backend.GetChildAtNamePath({g_tree_, g_pair3});
  if (!deref)
    return false;
  m_element_type = deref->GetCompilerType()
                       .GetTypeTemplateArgument(1)
                       .GetTypeTemplateArgument(1);
  if (m_element_type) {
    std::string name;
    uint64_t bit_offset_ptr;
    uint32_t bitfield_bit_size_ptr;
    bool is_bitfield_ptr;
    m_element_type = m_element_type.GetFieldAtIndex(
        0, name, &bit_offset_ptr, &bitfield_bit_size_ptr, &is_bitfield_ptr);
    m_element_type = m_element_type.GetTypedefedType();
    return m_element_type.IsValid();
  } else {
    m_element_type = m_backend.GetCompilerType().GetTypeTemplateArgument(0);
    return m_element_type.IsValid();
  }
}

void lldb_private::formatters::LibcxxStdMapSyntheticFrontEnd::GetValueOffset(
    const lldb::ValueObjectSP &node) {
  if (m_skip_size != UINT32_MAX)
    return;
  if (!node)
    return;
  CompilerType node_type(node->GetCompilerType());
  uint64_t bit_offset;
  if (node_type.GetIndexOfFieldWithName("__value_", nullptr, &bit_offset) !=
      UINT32_MAX) {
    m_skip_size = bit_offset / 8u;
  } else {
    ClangASTContext *ast_ctx =
        llvm::dyn_cast_or_null<ClangASTContext>(node_type.GetTypeSystem());
    if (!ast_ctx)
      return;
    CompilerType tree_node_type = ast_ctx->CreateStructForIdentifier(
        ConstString(),
        {{"ptr0", ast_ctx->GetBasicType(lldb::eBasicTypeVoid).GetPointerType()},
         {"ptr1", ast_ctx->GetBasicType(lldb::eBasicTypeVoid).GetPointerType()},
         {"ptr2", ast_ctx->GetBasicType(lldb::eBasicTypeVoid).GetPointerType()},
         {"cw", ast_ctx->GetBasicType(lldb::eBasicTypeBool)},
         {"payload", (m_element_type.GetCompleteType(), m_element_type)}});
    std::string child_name;
    uint32_t child_byte_size;
    int32_t child_byte_offset = 0;
    uint32_t child_bitfield_bit_size;
    uint32_t child_bitfield_bit_offset;
    bool child_is_base_class;
    bool child_is_deref_of_parent;
    uint64_t language_flags;
    if (tree_node_type
            .GetChildCompilerTypeAtIndex(
                nullptr, 4, true, true, true, child_name, child_byte_size,
                child_byte_offset, child_bitfield_bit_size,
                child_bitfield_bit_offset, child_is_base_class,
                child_is_deref_of_parent, nullptr, language_flags)
            .IsValid())
      m_skip_size = (uint32_t)child_byte_offset;
  }
}

lldb::ValueObjectSP
lldb_private::formatters::LibcxxStdMapSyntheticFrontEnd::GetChildAtIndex(
    size_t idx) {
  static ConstString g___cc("__cc");
  static ConstString g___nc("__nc");
  static ConstString g___value_("__value_");

  if (idx >= CalculateNumChildren())
    return lldb::ValueObjectSP();
  if (m_tree == nullptr || m_root_node == nullptr)
    return lldb::ValueObjectSP();

  MapIterator iterator(m_root_node, CalculateNumChildren());

  const bool need_to_skip = (idx > 0);
  size_t actual_advancde = idx;
  if (need_to_skip) {
    auto cached_iterator = m_iterators.find(idx - 1);
    if (cached_iterator != m_iterators.end()) {
      iterator = cached_iterator->second;
      actual_advancde = 1;
    }
  }

  ValueObjectSP iterated_sp(iterator.advance(actual_advancde));
  if (!iterated_sp) {
    // this tree is garbage - stop
    m_tree =
        nullptr; // this will stop all future searches until an Update() happens
    return iterated_sp;
  }
  if (GetDataType()) {
    if (!need_to_skip) {
      Status error;
      iterated_sp = iterated_sp->Dereference(error);
      if (!iterated_sp || error.Fail()) {
        m_tree = nullptr;
        return lldb::ValueObjectSP();
      }
      GetValueOffset(iterated_sp);
      auto child_sp = iterated_sp->GetChildMemberWithName(g___value_, true);
      if (child_sp)
        iterated_sp = child_sp;
      else
        iterated_sp = iterated_sp->GetSyntheticChildAtOffset(
            m_skip_size, m_element_type, true);
      if (!iterated_sp) {
        m_tree = nullptr;
        return lldb::ValueObjectSP();
      }
    } else {
      // because of the way our debug info is made, we need to read item 0
      // first so that we can cache information used to generate other elements
      if (m_skip_size == UINT32_MAX)
        GetChildAtIndex(0);
      if (m_skip_size == UINT32_MAX) {
        m_tree = nullptr;
        return lldb::ValueObjectSP();
      }
      iterated_sp = iterated_sp->GetSyntheticChildAtOffset(
          m_skip_size, m_element_type, true);
      if (!iterated_sp) {
        m_tree = nullptr;
        return lldb::ValueObjectSP();
      }
    }
  } else {
    m_tree = nullptr;
    return lldb::ValueObjectSP();
  }
  // at this point we have a valid
  // we need to copy current_sp into a new object otherwise we will end up with
  // all items named __value_
  DataExtractor data;
  Status error;
  iterated_sp->GetData(data, error);
  if (error.Fail()) {
    m_tree = nullptr;
    return lldb::ValueObjectSP();
  }
  StreamString name;
  name.Printf("[%" PRIu64 "]", (uint64_t)idx);
  auto potential_child_sp = CreateValueObjectFromData(
      name.GetString(), data, m_backend.GetExecutionContextRef(),
      m_element_type);
  if (potential_child_sp) {
    switch (potential_child_sp->GetNumChildren()) {
    case 1: {
      auto child0_sp = potential_child_sp->GetChildAtIndex(0, true);
      if (child0_sp && child0_sp->GetName() == g___cc)
        potential_child_sp = child0_sp->Clone(ConstString(name.GetString()));
      break;
    }
    case 2: {
      auto child0_sp = potential_child_sp->GetChildAtIndex(0, true);
      auto child1_sp = potential_child_sp->GetChildAtIndex(1, true);
      if (child0_sp && child0_sp->GetName() == g___cc && child1_sp &&
          child1_sp->GetName() == g___nc)
        potential_child_sp = child0_sp->Clone(ConstString(name.GetString()));
      break;
    }
    }
  }
  m_iterators[idx] = iterator;
  return potential_child_sp;
}

bool lldb_private::formatters::LibcxxStdMapSyntheticFrontEnd::Update() {
  static ConstString g___tree_("__tree_");
  static ConstString g___begin_node_("__begin_node_");
  m_count = UINT32_MAX;
  m_tree = m_root_node = nullptr;
  m_iterators.clear();
  m_tree = m_backend.GetChildMemberWithName(g___tree_, true).get();
  if (!m_tree)
    return false;
  m_root_node = m_tree->GetChildMemberWithName(g___begin_node_, true).get();
  return false;
}

bool lldb_private::formatters::LibcxxStdMapSyntheticFrontEnd::
    MightHaveChildren() {
  return true;
}

size_t lldb_private::formatters::LibcxxStdMapSyntheticFrontEnd::
    GetIndexOfChildWithName(const ConstString &name) {
  return ExtractIndexFromString(name.GetCString());
}

SyntheticChildrenFrontEnd *
lldb_private::formatters::LibcxxStdMapSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  return (valobj_sp ? new LibcxxStdMapSyntheticFrontEnd(valobj_sp) : nullptr);
}
