//===-- LibCxxList.cpp ------------------------------------------*- C++ -*-===//
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

namespace {

class ListEntry {
public:
  ListEntry() = default;
  ListEntry(ValueObjectSP entry_sp) : m_entry_sp(entry_sp) {}
  ListEntry(const ListEntry &rhs) = default;
  ListEntry(ValueObject *entry)
      : m_entry_sp(entry ? entry->GetSP() : ValueObjectSP()) {}

  ListEntry next() {
    static ConstString g_next("__next_");

    if (!m_entry_sp)
      return ListEntry();
    return ListEntry(m_entry_sp->GetChildMemberWithName(g_next, true));
  }

  ListEntry prev() {
    static ConstString g_prev("__prev_");

    if (!m_entry_sp)
      return ListEntry();
    return ListEntry(m_entry_sp->GetChildMemberWithName(g_prev, true));
  }

  uint64_t value() const {
    if (!m_entry_sp)
      return 0;
    return m_entry_sp->GetValueAsUnsigned(0);
  }

  bool null() { return (value() == 0); }

  explicit operator bool() { return GetEntry() && !null(); }

  ValueObjectSP GetEntry() { return m_entry_sp; }

  void SetEntry(ValueObjectSP entry) { m_entry_sp = entry; }

  bool operator==(const ListEntry &rhs) const { return value() == rhs.value(); }

  bool operator!=(const ListEntry &rhs) const { return !(*this == rhs); }

private:
  ValueObjectSP m_entry_sp;
};

class ListIterator {
public:
  ListIterator() = default;
  ListIterator(ListEntry entry) : m_entry(entry) {}
  ListIterator(ValueObjectSP entry) : m_entry(entry) {}
  ListIterator(const ListIterator &rhs) = default;
  ListIterator(ValueObject *entry) : m_entry(entry) {}

  ValueObjectSP value() { return m_entry.GetEntry(); }

  ValueObjectSP advance(size_t count) {
    if (count == 0)
      return m_entry.GetEntry();
    if (count == 1) {
      next();
      return m_entry.GetEntry();
    }
    while (count > 0) {
      next();
      count--;
      if (m_entry.null())
        return lldb::ValueObjectSP();
    }
    return m_entry.GetEntry();
  }

  bool operator==(const ListIterator &rhs) const {
    return (rhs.m_entry == m_entry);
  }

protected:
  void next() { m_entry = m_entry.next(); }

  void prev() { m_entry = m_entry.prev(); }

private:
  ListEntry m_entry;
};

class AbstractListFrontEnd : public SyntheticChildrenFrontEnd {
public:
  size_t GetIndexOfChildWithName(const ConstString &name) override {
    return ExtractIndexFromString(name.GetCString());
  }
  bool MightHaveChildren() override { return true; }
  bool Update() override;

protected:
  AbstractListFrontEnd(ValueObject &valobj)
      : SyntheticChildrenFrontEnd(valobj) {}

  size_t m_count;
  ValueObject *m_head;

  static constexpr bool g_use_loop_detect = true;
  size_t m_loop_detected; // The number of elements that have had loop detection
                          // run over them.
  ListEntry m_slow_runner; // Used for loop detection
  ListEntry m_fast_runner; // Used for loop detection

  size_t m_list_capping_size;
  CompilerType m_element_type;
  std::map<size_t, ListIterator> m_iterators;

  bool HasLoop(size_t count);
  ValueObjectSP GetItem(size_t idx);
};

class ForwardListFrontEnd : public AbstractListFrontEnd {
public:
  ForwardListFrontEnd(ValueObject &valobj);

  size_t CalculateNumChildren() override;
  ValueObjectSP GetChildAtIndex(size_t idx) override;
  bool Update() override;
};

class ListFrontEnd : public AbstractListFrontEnd {
public:
  ListFrontEnd(lldb::ValueObjectSP valobj_sp);

  ~ListFrontEnd() override = default;

  size_t CalculateNumChildren() override;

  lldb::ValueObjectSP GetChildAtIndex(size_t idx) override;

  bool Update() override;

private:
  lldb::addr_t m_node_address;
  ValueObject *m_tail;
};

} // end anonymous namespace

bool AbstractListFrontEnd::Update() {
  m_loop_detected = 0;
  m_count = UINT32_MAX;
  m_head = nullptr;
  m_list_capping_size = 0;
  m_slow_runner.SetEntry(nullptr);
  m_fast_runner.SetEntry(nullptr);
  m_iterators.clear();

  if (m_backend.GetTargetSP())
    m_list_capping_size =
        m_backend.GetTargetSP()->GetMaximumNumberOfChildrenToDisplay();
  if (m_list_capping_size == 0)
    m_list_capping_size = 255;

  CompilerType list_type = m_backend.GetCompilerType();
  if (list_type.IsReferenceType())
    list_type = list_type.GetNonReferenceType();

  if (list_type.GetNumTemplateArguments() == 0)
    return false;
  m_element_type = list_type.GetTypeTemplateArgument(0);

  return false;
}

bool AbstractListFrontEnd::HasLoop(size_t count) {
  if (!g_use_loop_detect)
    return false;
  // don't bother checking for a loop if we won't actually need to jump nodes
  if (m_count < 2)
    return false;

  if (m_loop_detected == 0) {
    // This is the first time we are being run (after the last update). Set up
    // the loop invariant for the first element.
    m_slow_runner = ListEntry(m_head).next();
    m_fast_runner = m_slow_runner.next();
    m_loop_detected = 1;
  }

  // Loop invariant:
  // Loop detection has been run over the first m_loop_detected elements. If
  // m_slow_runner == m_fast_runner then the loop has been detected after
  // m_loop_detected elements.
  const size_t steps_to_run = std::min(count, m_count);
  while (m_loop_detected < steps_to_run && m_slow_runner && m_fast_runner &&
         m_slow_runner != m_fast_runner) {

    m_slow_runner = m_slow_runner.next();
    m_fast_runner = m_fast_runner.next().next();
    m_loop_detected++;
  }
  if (count <= m_loop_detected)
    return false; // No loop in the first m_loop_detected elements.
  if (!m_slow_runner || !m_fast_runner)
    return false; // Reached the end of the list. Definitely no loops.
  return m_slow_runner == m_fast_runner;
}

ValueObjectSP AbstractListFrontEnd::GetItem(size_t idx) {
  size_t advance = idx;
  ListIterator current(m_head);
  if (idx > 0) {
    auto cached_iterator = m_iterators.find(idx - 1);
    if (cached_iterator != m_iterators.end()) {
      current = cached_iterator->second;
      advance = 1;
    }
  }
  ValueObjectSP value_sp = current.advance(advance);
  m_iterators[idx] = current;
  return value_sp;
}

ForwardListFrontEnd::ForwardListFrontEnd(ValueObject &valobj)
    : AbstractListFrontEnd(valobj) {
  Update();
}

size_t ForwardListFrontEnd::CalculateNumChildren() {
  if (m_count != UINT32_MAX)
    return m_count;

  ListEntry current(m_head);
  m_count = 0;
  while (current && m_count < m_list_capping_size) {
    ++m_count;
    current = current.next();
  }
  return m_count;
}

ValueObjectSP ForwardListFrontEnd::GetChildAtIndex(size_t idx) {
  if (idx >= CalculateNumChildren())
    return nullptr;

  if (!m_head)
    return nullptr;

  if (HasLoop(idx + 1))
    return nullptr;

  ValueObjectSP current_sp = GetItem(idx);
  if (!current_sp)
    return nullptr;

  current_sp = current_sp->GetChildAtIndex(1, true); // get the __value_ child
  if (!current_sp)
    return nullptr;

  // we need to copy current_sp into a new object otherwise we will end up with
  // all items named __value_
  DataExtractor data;
  Status error;
  current_sp->GetData(data, error);
  if (error.Fail())
    return nullptr;

  return CreateValueObjectFromData(llvm::formatv("[{0}]", idx).str(), data,
                                   m_backend.GetExecutionContextRef(),
                                   m_element_type);
}

static ValueObjectSP GetValueOfCompressedPair(ValueObject &pair) {
  ValueObjectSP value = pair.GetChildMemberWithName(ConstString("__value_"), true);
  if (! value) {
    // pre-r300140 member name
    value = pair.GetChildMemberWithName(ConstString("__first_"), true);
  }
  return value;
}

bool ForwardListFrontEnd::Update() {
  AbstractListFrontEnd::Update();

  Status err;
  ValueObjectSP backend_addr(m_backend.AddressOf(err));
  if (err.Fail() || !backend_addr)
    return false;

  ValueObjectSP impl_sp(
      m_backend.GetChildMemberWithName(ConstString("__before_begin_"), true));
  if (!impl_sp)
    return false;
  impl_sp = GetValueOfCompressedPair(*impl_sp);
  if (!impl_sp)
    return false;
  m_head = impl_sp->GetChildMemberWithName(ConstString("__next_"), true).get();
  return false;
}

ListFrontEnd::ListFrontEnd(lldb::ValueObjectSP valobj_sp)
    : AbstractListFrontEnd(*valobj_sp), m_node_address(), m_tail(nullptr) {
  if (valobj_sp)
    Update();
}

size_t ListFrontEnd::CalculateNumChildren() {
  if (m_count != UINT32_MAX)
    return m_count;
  if (!m_head || !m_tail || m_node_address == 0)
    return 0;
  ValueObjectSP size_alloc(
      m_backend.GetChildMemberWithName(ConstString("__size_alloc_"), true));
  if (size_alloc) {
    ValueObjectSP value = GetValueOfCompressedPair(*size_alloc);
    if (value) {
      m_count = value->GetValueAsUnsigned(UINT32_MAX);
    }
  }
  if (m_count != UINT32_MAX) {
    return m_count;
  } else {
    uint64_t next_val = m_head->GetValueAsUnsigned(0);
    uint64_t prev_val = m_tail->GetValueAsUnsigned(0);
    if (next_val == 0 || prev_val == 0)
      return 0;
    if (next_val == m_node_address)
      return 0;
    if (next_val == prev_val)
      return 1;
    uint64_t size = 2;
    ListEntry current(m_head);
    while (current.next() && current.next().value() != m_node_address) {
      size++;
      current = current.next();
      if (size > m_list_capping_size)
        break;
    }
    return m_count = (size - 1);
  }
}

lldb::ValueObjectSP ListFrontEnd::GetChildAtIndex(size_t idx) {
  static ConstString g_value("__value_");
  static ConstString g_next("__next_");

  if (idx >= CalculateNumChildren())
    return lldb::ValueObjectSP();

  if (!m_head || !m_tail || m_node_address == 0)
    return lldb::ValueObjectSP();

  if (HasLoop(idx + 1))
    return lldb::ValueObjectSP();

  ValueObjectSP current_sp = GetItem(idx);
  if (!current_sp)
    return lldb::ValueObjectSP();

  current_sp = current_sp->GetChildAtIndex(1, true); // get the __value_ child
  if (!current_sp)
    return lldb::ValueObjectSP();

  if (current_sp->GetName() == g_next) {
    ProcessSP process_sp(current_sp->GetProcessSP());
    if (!process_sp)
      return nullptr;

    // if we grabbed the __next_ pointer, then the child is one pointer deep-er
    lldb::addr_t addr = current_sp->GetParent()->GetPointerValue();
    addr = addr + 2 * process_sp->GetAddressByteSize();
    ExecutionContext exe_ctx(process_sp);
    current_sp =
        CreateValueObjectFromAddress("__value_", addr, exe_ctx, m_element_type);
  }

  // we need to copy current_sp into a new object otherwise we will end up with
  // all items named __value_
  DataExtractor data;
  Status error;
  current_sp->GetData(data, error);
  if (error.Fail())
    return lldb::ValueObjectSP();

  StreamString name;
  name.Printf("[%" PRIu64 "]", (uint64_t)idx);
  return CreateValueObjectFromData(name.GetString(), data,
                                   m_backend.GetExecutionContextRef(),
                                   m_element_type);
}

bool ListFrontEnd::Update() {
  AbstractListFrontEnd::Update();
  m_tail = nullptr;
  m_node_address = 0;

  Status err;
  ValueObjectSP backend_addr(m_backend.AddressOf(err));
  if (err.Fail() || !backend_addr)
    return false;
  m_node_address = backend_addr->GetValueAsUnsigned(0);
  if (!m_node_address || m_node_address == LLDB_INVALID_ADDRESS)
    return false;
  ValueObjectSP impl_sp(
      m_backend.GetChildMemberWithName(ConstString("__end_"), true));
  if (!impl_sp)
    return false;
  m_head = impl_sp->GetChildMemberWithName(ConstString("__next_"), true).get();
  m_tail = impl_sp->GetChildMemberWithName(ConstString("__prev_"), true).get();
  return false;
}

SyntheticChildrenFrontEnd *formatters::LibcxxStdListSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  return (valobj_sp ? new ListFrontEnd(valobj_sp) : nullptr);
}

SyntheticChildrenFrontEnd *
formatters::LibcxxStdForwardListSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  return valobj_sp ? new ForwardListFrontEnd(*valobj_sp) : nullptr;
}
