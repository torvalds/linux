//===-- Block.cpp -----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/Block.h"

#include "lldb/Core/Module.h"
#include "lldb/Core/Section.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/SymbolVendor.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Utility/Log.h"

using namespace lldb;
using namespace lldb_private;

Block::Block(lldb::user_id_t uid)
    : UserID(uid), m_parent_scope(nullptr), m_children(), m_ranges(),
      m_inlineInfoSP(), m_variable_list_sp(), m_parsed_block_info(false),
      m_parsed_block_variables(false), m_parsed_child_blocks(false) {}

Block::~Block() {}

void Block::GetDescription(Stream *s, Function *function,
                           lldb::DescriptionLevel level, Target *target) const {
  *s << "id = " << ((const UserID &)*this);

  size_t num_ranges = m_ranges.GetSize();
  if (num_ranges > 0) {

    addr_t base_addr = LLDB_INVALID_ADDRESS;
    if (target)
      base_addr =
          function->GetAddressRange().GetBaseAddress().GetLoadAddress(target);
    if (base_addr == LLDB_INVALID_ADDRESS)
      base_addr = function->GetAddressRange().GetBaseAddress().GetFileAddress();

    s->Printf(", range%s = ", num_ranges > 1 ? "s" : "");
    for (size_t i = 0; i < num_ranges; ++i) {
      const Range &range = m_ranges.GetEntryRef(i);
      s->AddressRange(base_addr + range.GetRangeBase(),
                      base_addr + range.GetRangeEnd(), 4);
    }
  }

  if (m_inlineInfoSP.get() != nullptr) {
    bool show_fullpaths = (level == eDescriptionLevelVerbose);
    m_inlineInfoSP->Dump(s, show_fullpaths);
  }
}

void Block::Dump(Stream *s, addr_t base_addr, int32_t depth,
                 bool show_context) const {
  if (depth < 0) {
    Block *parent = GetParent();
    if (parent) {
      // We have a depth that is less than zero, print our parent blocks first
      parent->Dump(s, base_addr, depth + 1, show_context);
    }
  }

  s->Printf("%p: ", static_cast<const void *>(this));
  s->Indent();
  *s << "Block" << static_cast<const UserID &>(*this);
  const Block *parent_block = GetParent();
  if (parent_block) {
    s->Printf(", parent = {0x%8.8" PRIx64 "}", parent_block->GetID());
  }
  if (m_inlineInfoSP.get() != nullptr) {
    bool show_fullpaths = false;
    m_inlineInfoSP->Dump(s, show_fullpaths);
  }

  if (!m_ranges.IsEmpty()) {
    *s << ", ranges =";

    size_t num_ranges = m_ranges.GetSize();
    for (size_t i = 0; i < num_ranges; ++i) {
      const Range &range = m_ranges.GetEntryRef(i);
      if (parent_block != nullptr && !parent_block->Contains(range))
        *s << '!';
      else
        *s << ' ';
      s->AddressRange(base_addr + range.GetRangeBase(),
                      base_addr + range.GetRangeEnd(), 4);
    }
  }
  s->EOL();

  if (depth > 0) {
    s->IndentMore();

    if (m_variable_list_sp.get()) {
      m_variable_list_sp->Dump(s, show_context);
    }

    collection::const_iterator pos, end = m_children.end();
    for (pos = m_children.begin(); pos != end; ++pos)
      (*pos)->Dump(s, base_addr, depth - 1, show_context);

    s->IndentLess();
  }
}

Block *Block::FindBlockByID(user_id_t block_id) {
  if (block_id == GetID())
    return this;

  Block *matching_block = nullptr;
  collection::const_iterator pos, end = m_children.end();
  for (pos = m_children.begin(); pos != end; ++pos) {
    matching_block = (*pos)->FindBlockByID(block_id);
    if (matching_block)
      break;
  }
  return matching_block;
}

void Block::CalculateSymbolContext(SymbolContext *sc) {
  if (m_parent_scope)
    m_parent_scope->CalculateSymbolContext(sc);
  sc->block = this;
}

lldb::ModuleSP Block::CalculateSymbolContextModule() {
  if (m_parent_scope)
    return m_parent_scope->CalculateSymbolContextModule();
  return lldb::ModuleSP();
}

CompileUnit *Block::CalculateSymbolContextCompileUnit() {
  if (m_parent_scope)
    return m_parent_scope->CalculateSymbolContextCompileUnit();
  return nullptr;
}

Function *Block::CalculateSymbolContextFunction() {
  if (m_parent_scope)
    return m_parent_scope->CalculateSymbolContextFunction();
  return nullptr;
}

Block *Block::CalculateSymbolContextBlock() { return this; }

void Block::DumpSymbolContext(Stream *s) {
  Function *function = CalculateSymbolContextFunction();
  if (function)
    function->DumpSymbolContext(s);
  s->Printf(", Block{0x%8.8" PRIx64 "}", GetID());
}

void Block::DumpAddressRanges(Stream *s, lldb::addr_t base_addr) {
  if (!m_ranges.IsEmpty()) {
    size_t num_ranges = m_ranges.GetSize();
    for (size_t i = 0; i < num_ranges; ++i) {
      const Range &range = m_ranges.GetEntryRef(i);
      s->AddressRange(base_addr + range.GetRangeBase(),
                      base_addr + range.GetRangeEnd(), 4);
    }
  }
}

bool Block::Contains(addr_t range_offset) const {
  return m_ranges.FindEntryThatContains(range_offset) != nullptr;
}

bool Block::Contains(const Block *block) const {
  if (this == block)
    return false; // This block doesn't contain itself...

  // Walk the parent chain for "block" and see if any if them match this block
  const Block *block_parent;
  for (block_parent = block->GetParent(); block_parent != nullptr;
       block_parent = block_parent->GetParent()) {
    if (this == block_parent)
      return true; // One of the parents of "block" is this object!
  }
  return false;
}

bool Block::Contains(const Range &range) const {
  return m_ranges.FindEntryThatContains(range) != nullptr;
}

Block *Block::GetParent() const {
  if (m_parent_scope)
    return m_parent_scope->CalculateSymbolContextBlock();
  return nullptr;
}

Block *Block::GetContainingInlinedBlock() {
  if (GetInlinedFunctionInfo())
    return this;
  return GetInlinedParent();
}

Block *Block::GetInlinedParent() {
  Block *parent_block = GetParent();
  if (parent_block) {
    if (parent_block->GetInlinedFunctionInfo())
      return parent_block;
    else
      return parent_block->GetInlinedParent();
  }
  return nullptr;
}

bool Block::GetRangeContainingOffset(const addr_t offset, Range &range) {
  const Range *range_ptr = m_ranges.FindEntryThatContains(offset);
  if (range_ptr) {
    range = *range_ptr;
    return true;
  }
  range.Clear();
  return false;
}

bool Block::GetRangeContainingAddress(const Address &addr,
                                      AddressRange &range) {
  Function *function = CalculateSymbolContextFunction();
  if (function) {
    const AddressRange &func_range = function->GetAddressRange();
    if (addr.GetSection() == func_range.GetBaseAddress().GetSection()) {
      const addr_t addr_offset = addr.GetOffset();
      const addr_t func_offset = func_range.GetBaseAddress().GetOffset();
      if (addr_offset >= func_offset &&
          addr_offset < func_offset + func_range.GetByteSize()) {
        addr_t offset = addr_offset - func_offset;

        const Range *range_ptr = m_ranges.FindEntryThatContains(offset);

        if (range_ptr) {
          range.GetBaseAddress() = func_range.GetBaseAddress();
          range.GetBaseAddress().SetOffset(func_offset +
                                           range_ptr->GetRangeBase());
          range.SetByteSize(range_ptr->GetByteSize());
          return true;
        }
      }
    }
  }
  range.Clear();
  return false;
}

bool Block::GetRangeContainingLoadAddress(lldb::addr_t load_addr,
                                          Target &target, AddressRange &range) {
  Address load_address;
  load_address.SetLoadAddress(load_addr, &target);
  AddressRange containing_range;
  return GetRangeContainingAddress(load_address, containing_range);
}

uint32_t Block::GetRangeIndexContainingAddress(const Address &addr) {
  Function *function = CalculateSymbolContextFunction();
  if (function) {
    const AddressRange &func_range = function->GetAddressRange();
    if (addr.GetSection() == func_range.GetBaseAddress().GetSection()) {
      const addr_t addr_offset = addr.GetOffset();
      const addr_t func_offset = func_range.GetBaseAddress().GetOffset();
      if (addr_offset >= func_offset &&
          addr_offset < func_offset + func_range.GetByteSize()) {
        addr_t offset = addr_offset - func_offset;
        return m_ranges.FindEntryIndexThatContains(offset);
      }
    }
  }
  return UINT32_MAX;
}

bool Block::GetRangeAtIndex(uint32_t range_idx, AddressRange &range) {
  if (range_idx < m_ranges.GetSize()) {
    Function *function = CalculateSymbolContextFunction();
    if (function) {
      const Range &vm_range = m_ranges.GetEntryRef(range_idx);
      range.GetBaseAddress() = function->GetAddressRange().GetBaseAddress();
      range.GetBaseAddress().Slide(vm_range.GetRangeBase());
      range.SetByteSize(vm_range.GetByteSize());
      return true;
    }
  }
  return false;
}

bool Block::GetStartAddress(Address &addr) {
  if (m_ranges.IsEmpty())
    return false;

  Function *function = CalculateSymbolContextFunction();
  if (function) {
    addr = function->GetAddressRange().GetBaseAddress();
    addr.Slide(m_ranges.GetEntryRef(0).GetRangeBase());
    return true;
  }
  return false;
}

void Block::FinalizeRanges() {
  m_ranges.Sort();
  m_ranges.CombineConsecutiveRanges();
}

void Block::AddRange(const Range &range) {
  Block *parent_block = GetParent();
  if (parent_block && !parent_block->Contains(range)) {
    Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_SYMBOLS));
    if (log) {
      ModuleSP module_sp(m_parent_scope->CalculateSymbolContextModule());
      Function *function = m_parent_scope->CalculateSymbolContextFunction();
      const addr_t function_file_addr =
          function->GetAddressRange().GetBaseAddress().GetFileAddress();
      const addr_t block_start_addr = function_file_addr + range.GetRangeBase();
      const addr_t block_end_addr = function_file_addr + range.GetRangeEnd();
      Type *func_type = function->GetType();

      const Declaration &func_decl = func_type->GetDeclaration();
      if (func_decl.GetLine()) {
        log->Printf("warning: %s:%u block {0x%8.8" PRIx64
                    "} has range[%u] [0x%" PRIx64 " - 0x%" PRIx64
                    ") which is not contained in parent block {0x%8.8" PRIx64
                    "} in function {0x%8.8" PRIx64 "} from %s",
                    func_decl.GetFile().GetPath().c_str(), func_decl.GetLine(),
                    GetID(), (uint32_t)m_ranges.GetSize(), block_start_addr,
                    block_end_addr, parent_block->GetID(), function->GetID(),
                    module_sp->GetFileSpec().GetPath().c_str());
      } else {
        log->Printf("warning: block {0x%8.8" PRIx64
                    "} has range[%u] [0x%" PRIx64 " - 0x%" PRIx64
                    ") which is not contained in parent block {0x%8.8" PRIx64
                    "} in function {0x%8.8" PRIx64 "} from %s",
                    GetID(), (uint32_t)m_ranges.GetSize(), block_start_addr,
                    block_end_addr, parent_block->GetID(), function->GetID(),
                    module_sp->GetFileSpec().GetPath().c_str());
      }
    }
    parent_block->AddRange(range);
  }
  m_ranges.Append(range);
}

// Return the current number of bytes that this object occupies in memory
size_t Block::MemorySize() const {
  size_t mem_size = sizeof(Block) + m_ranges.GetSize() * sizeof(Range);
  if (m_inlineInfoSP.get())
    mem_size += m_inlineInfoSP->MemorySize();
  if (m_variable_list_sp.get())
    mem_size += m_variable_list_sp->MemorySize();
  return mem_size;
}

void Block::AddChild(const BlockSP &child_block_sp) {
  if (child_block_sp) {
    child_block_sp->SetParentScope(this);
    m_children.push_back(child_block_sp);
  }
}

void Block::SetInlinedFunctionInfo(const char *name, const char *mangled,
                                   const Declaration *decl_ptr,
                                   const Declaration *call_decl_ptr) {
  m_inlineInfoSP.reset(
      new InlineFunctionInfo(name, mangled, decl_ptr, call_decl_ptr));
}

VariableListSP Block::GetBlockVariableList(bool can_create) {
  if (!m_parsed_block_variables) {
    if (m_variable_list_sp.get() == nullptr && can_create) {
      m_parsed_block_variables = true;
      SymbolContext sc;
      CalculateSymbolContext(&sc);
      assert(sc.module_sp);
      sc.module_sp->GetSymbolVendor()->ParseVariablesForContext(sc);
    }
  }
  return m_variable_list_sp;
}

uint32_t
Block::AppendBlockVariables(bool can_create, bool get_child_block_variables,
                            bool stop_if_child_block_is_inlined_function,
                            const std::function<bool(Variable *)> &filter,
                            VariableList *variable_list) {
  uint32_t num_variables_added = 0;
  VariableList *block_var_list = GetBlockVariableList(can_create).get();
  if (block_var_list) {
    for (size_t i = 0; i < block_var_list->GetSize(); ++i) {
      VariableSP variable = block_var_list->GetVariableAtIndex(i);
      if (filter(variable.get())) {
        num_variables_added++;
        variable_list->AddVariable(variable);
      }
    }
  }

  if (get_child_block_variables) {
    collection::const_iterator pos, end = m_children.end();
    for (pos = m_children.begin(); pos != end; ++pos) {
      Block *child_block = pos->get();
      if (!stop_if_child_block_is_inlined_function ||
          child_block->GetInlinedFunctionInfo() == nullptr) {
        num_variables_added += child_block->AppendBlockVariables(
            can_create, get_child_block_variables,
            stop_if_child_block_is_inlined_function, filter, variable_list);
      }
    }
  }
  return num_variables_added;
}

uint32_t Block::AppendVariables(bool can_create, bool get_parent_variables,
                                bool stop_if_block_is_inlined_function,
                                const std::function<bool(Variable *)> &filter,
                                VariableList *variable_list) {
  uint32_t num_variables_added = 0;
  VariableListSP variable_list_sp(GetBlockVariableList(can_create));

  bool is_inlined_function = GetInlinedFunctionInfo() != nullptr;
  if (variable_list_sp) {
    for (size_t i = 0; i < variable_list_sp->GetSize(); ++i) {
      VariableSP variable = variable_list_sp->GetVariableAtIndex(i);
      if (filter(variable.get())) {
        num_variables_added++;
        variable_list->AddVariable(variable);
      }
    }
  }

  if (get_parent_variables) {
    if (stop_if_block_is_inlined_function && is_inlined_function)
      return num_variables_added;

    Block *parent_block = GetParent();
    if (parent_block)
      num_variables_added += parent_block->AppendVariables(
          can_create, get_parent_variables, stop_if_block_is_inlined_function,
          filter, variable_list);
  }
  return num_variables_added;
}

SymbolFile *Block::GetSymbolFile() {
  if (ModuleSP module_sp = CalculateSymbolContextModule())
    if (SymbolVendor *sym_vendor = module_sp->GetSymbolVendor())
      return sym_vendor->GetSymbolFile();
  return nullptr;
}

CompilerDeclContext Block::GetDeclContext() {
  if (SymbolFile *sym_file = GetSymbolFile())
    return sym_file->GetDeclContextForUID(GetID());
  return CompilerDeclContext();
}

void Block::SetBlockInfoHasBeenParsed(bool b, bool set_children) {
  m_parsed_block_info = b;
  if (set_children) {
    m_parsed_child_blocks = true;
    collection::const_iterator pos, end = m_children.end();
    for (pos = m_children.begin(); pos != end; ++pos)
      (*pos)->SetBlockInfoHasBeenParsed(b, true);
  }
}

void Block::SetDidParseVariables(bool b, bool set_children) {
  m_parsed_block_variables = b;
  if (set_children) {
    collection::const_iterator pos, end = m_children.end();
    for (pos = m_children.begin(); pos != end; ++pos)
      (*pos)->SetDidParseVariables(b, true);
  }
}

Block *Block::GetSibling() const {
  if (m_parent_scope) {
    Block *parent_block = GetParent();
    if (parent_block)
      return parent_block->GetSiblingForChild(this);
  }
  return nullptr;
}
// A parent of child blocks can be asked to find a sibling block given
// one of its child blocks
Block *Block::GetSiblingForChild(const Block *child_block) const {
  if (!m_children.empty()) {
    collection::const_iterator pos, end = m_children.end();
    for (pos = m_children.begin(); pos != end; ++pos) {
      if (pos->get() == child_block) {
        if (++pos != end)
          return pos->get();
        break;
      }
    }
  }
  return nullptr;
}
