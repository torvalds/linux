//===-- Block.h -------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Block_h_
#define liblldb_Block_h_

#include <vector>

#include "lldb/Core/AddressRange.h"
#include "lldb/Core/RangeMap.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/LineEntry.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/SymbolContextScope.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/UserID.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

//----------------------------------------------------------------------
/// @class Block Block.h "lldb/Symbol/Block.h"
/// A class that describes a single lexical block.
///
/// A Function object owns a BlockList object which owns one or more
/// Block objects. The BlockList object contains a section offset address
/// range, and Block objects contain one or more ranges which are offsets into
/// that range. Blocks are can have discontiguous ranges within the BlockList
/// address range, and each block can contain child blocks each with their own
/// sets of ranges.
///
/// Each block has a variable list that represents local, argument, and static
/// variables that are scoped to the block.
///
/// Inlined functions are represented by attaching a InlineFunctionInfo shared
/// pointer object to a block. Inlined functions are represented as named
/// blocks.
//----------------------------------------------------------------------
class Block : public UserID, public SymbolContextScope {
public:
  typedef RangeArray<uint32_t, uint32_t, 1> RangeList;
  typedef RangeList::Entry Range;

  //------------------------------------------------------------------
  /// Construct with a User ID \a uid, \a depth.
  ///
  /// Initialize this block with the specified UID \a uid. The \a depth in the
  /// \a block_list is used to represent the parent, sibling, and child block
  /// information and also allows for partial parsing at the block level.
  ///
  /// @param[in] uid
  ///     The UID for a given block. This value is given by the
  ///     SymbolFile plug-in and can be any value that helps the
  ///     SymbolFile plug-in to match this block back to the debug
  ///     information data that it parses for further or more in
  ///     depth parsing. Common values would be the index into a
  ///     table, or an offset into the debug information.
  ///
  /// @param[in] depth
  ///     The integer depth of this block in the block list hierarchy.
  ///
  /// @param[in] block_list
  ///     The block list that this object belongs to.
  ///
  /// @see BlockList
  //------------------------------------------------------------------
  Block(lldb::user_id_t uid);

  //------------------------------------------------------------------
  /// Destructor.
  //------------------------------------------------------------------
  ~Block() override;

  //------------------------------------------------------------------
  /// Add a child to this object.
  ///
  /// @param[in] child_block_sp
  ///     A shared pointer to a child block that will get added to
  ///     this block.
  //------------------------------------------------------------------
  void AddChild(const lldb::BlockSP &child_block_sp);

  //------------------------------------------------------------------
  /// Add a new offset range to this block.
  ///
  /// @param[in] start_offset
  ///     An offset into this Function's address range that
  ///     describes the start address of a range for this block.
  ///
  /// @param[in] end_offset
  ///     An offset into this Function's address range that
  ///     describes the end address of a range for this block.
  //------------------------------------------------------------------
  void AddRange(const Range &range);

  void FinalizeRanges();

  //------------------------------------------------------------------
  /// @copydoc SymbolContextScope::CalculateSymbolContext(SymbolContext*)
  ///
  /// @see SymbolContextScope
  //------------------------------------------------------------------
  void CalculateSymbolContext(SymbolContext *sc) override;

  lldb::ModuleSP CalculateSymbolContextModule() override;

  CompileUnit *CalculateSymbolContextCompileUnit() override;

  Function *CalculateSymbolContextFunction() override;

  Block *CalculateSymbolContextBlock() override;

  //------------------------------------------------------------------
  /// Check if an offset is in one of the block offset ranges.
  ///
  /// @param[in] range_offset
  ///     An offset into the Function's address range.
  ///
  /// @return
  ///     Returns \b true if \a range_offset falls in one of this
  ///     block's ranges, \b false otherwise.
  //------------------------------------------------------------------
  bool Contains(lldb::addr_t range_offset) const;

  //------------------------------------------------------------------
  /// Check if a offset range is in one of the block offset ranges.
  ///
  /// @param[in] range
  ///     An offset range into the Function's address range.
  ///
  /// @return
  ///     Returns \b true if \a range falls in one of this
  ///     block's ranges, \b false otherwise.
  //------------------------------------------------------------------
  bool Contains(const Range &range) const;

  //------------------------------------------------------------------
  /// Check if this object contains "block" as a child block at any depth.
  ///
  /// @param[in] block
  ///     A potential child block.
  ///
  /// @return
  ///     Returns \b true if \a block is a child of this block, \b
  ///     false otherwise.
  //------------------------------------------------------------------
  bool Contains(const Block *block) const;

  //------------------------------------------------------------------
  /// Dump the block contents.
  ///
  /// @param[in] s
  ///     The stream to which to dump the object description.
  ///
  /// @param[in] base_addr
  ///     The resolved start address of the Function's address
  ///     range. This should be resolved as the file or load address
  ///     prior to passing the value into this function for dumping.
  ///
  /// @param[in] depth
  ///     Limit the number of levels deep that this function should
  ///     print as this block can contain child blocks. Specify
  ///     INT_MAX to dump all child blocks.
  ///
  /// @param[in] show_context
  ///     If \b true, variables will dump their context information.
  //------------------------------------------------------------------
  void Dump(Stream *s, lldb::addr_t base_addr, int32_t depth,
            bool show_context) const;

  //------------------------------------------------------------------
  /// @copydoc SymbolContextScope::DumpSymbolContext(Stream*)
  ///
  /// @see SymbolContextScope
  //------------------------------------------------------------------
  void DumpSymbolContext(Stream *s) override;

  void DumpAddressRanges(Stream *s, lldb::addr_t base_addr);

  void GetDescription(Stream *s, Function *function,
                      lldb::DescriptionLevel level, Target *target) const;

  //------------------------------------------------------------------
  /// Get the parent block.
  ///
  /// @return
  ///     The parent block pointer, or nullptr if this block has no
  ///     parent.
  //------------------------------------------------------------------
  Block *GetParent() const;

  //------------------------------------------------------------------
  /// Get the inlined block that contains this block.
  ///
  /// @return
  ///     If this block contains inlined function info, it will return
  ///     this block, else parent blocks will be searched to see if
  ///     any contain this block. nullptr will be returned if this block
  ///     nor any parent blocks are inlined function blocks.
  //------------------------------------------------------------------
  Block *GetContainingInlinedBlock();

  //------------------------------------------------------------------
  /// Get the inlined parent block for this block.
  ///
  /// @return
  ///     The parent block pointer, or nullptr if this block has no
  ///     parent.
  //------------------------------------------------------------------
  Block *GetInlinedParent();

  //------------------------------------------------------------------
  /// Get the sibling block for this block.
  ///
  /// @return
  ///     The sibling block pointer, or nullptr if this block has no
  ///     sibling.
  //------------------------------------------------------------------
  Block *GetSibling() const;

  //------------------------------------------------------------------
  /// Get the first child block.
  ///
  /// @return
  ///     The first child block pointer, or nullptr if this block has no
  ///     children.
  //------------------------------------------------------------------
  Block *GetFirstChild() const {
    return (m_children.empty() ? nullptr : m_children.front().get());
  }

  //------------------------------------------------------------------
  /// Get the variable list for this block only.
  ///
  /// @param[in] can_create
  ///     If \b true, the variables can be parsed if they already
  ///     haven't been, else the current state of the block will be
  ///     returned.
  ///
  /// @return
  ///     A variable list shared pointer that contains all variables
  ///     for this block.
  //------------------------------------------------------------------
  lldb::VariableListSP GetBlockVariableList(bool can_create);

  //------------------------------------------------------------------
  /// Get the variable list for this block and optionally all child blocks if
  /// \a get_child_variables is \b true.
  ///
  /// @param[in] get_child_variables
  ///     If \b true, all variables from all child blocks will be
  ///     added to the variable list.
  ///
  /// @param[in] can_create
  ///     If \b true, the variables can be parsed if they already
  ///     haven't been, else the current state of the block will be
  ///     returned. Passing \b true for this parameter can be used
  ///     to see the current state of what has been parsed up to this
  ///     point.
  ///
  /// @param[in] add_inline_child_block_variables
  ///     If this is \b false, no child variables of child blocks
  ///     that are inlined functions will be gotten. If \b true then
  ///     all child variables will be added regardless of whether they
  ///     come from inlined functions or not.
  ///
  /// @return
  ///     A variable list shared pointer that contains all variables
  ///     for this block.
  //------------------------------------------------------------------
  uint32_t AppendBlockVariables(bool can_create, bool get_child_block_variables,
                                bool stop_if_child_block_is_inlined_function,
                                const std::function<bool(Variable *)> &filter,
                                VariableList *variable_list);

  //------------------------------------------------------------------
  /// Appends the variables from this block, and optionally from all parent
  /// blocks, to \a variable_list.
  ///
  /// @param[in] can_create
  ///     If \b true, the variables can be parsed if they already
  ///     haven't been, else the current state of the block will be
  ///     returned. Passing \b true for this parameter can be used
  ///     to see the current state of what has been parsed up to this
  ///     point.
  ///
  /// @param[in] get_parent_variables
  ///     If \b true, all variables from all parent blocks will be
  ///     added to the variable list.
  ///
  /// @param[in] stop_if_block_is_inlined_function
  ///     If \b true, all variables from all parent blocks will be
  ///     added to the variable list until there are no parent blocks
  ///     or the parent block has inlined function info.
  ///
  /// @param[in,out] variable_list
  ///     All variables in this block, and optionally all parent
  ///     blocks will be added to this list.
  ///
  /// @return
  ///     The number of variable that were appended to \a
  ///     variable_list.
  //------------------------------------------------------------------
  uint32_t AppendVariables(bool can_create, bool get_parent_variables,
                           bool stop_if_block_is_inlined_function,
                           const std::function<bool(Variable *)> &filter,
                           VariableList *variable_list);

  //------------------------------------------------------------------
  /// Get const accessor for any inlined function information.
  ///
  /// @return
  ///     A const pointer to any inlined function information, or nullptr
  ///     if this is a regular block.
  //------------------------------------------------------------------
  const InlineFunctionInfo *GetInlinedFunctionInfo() const {
    return m_inlineInfoSP.get();
  }

  //------------------------------------------------------------------
  /// Get the symbol file which contains debug info for this block's
  /// symbol context module.
  ///
  /// @return A pointer to the symbol file or nullptr.
  //------------------------------------------------------------------
  SymbolFile *GetSymbolFile();

  CompilerDeclContext GetDeclContext();

  //------------------------------------------------------------------
  /// Get the memory cost of this object.
  ///
  /// Returns the cost of this object plus any owned objects from the ranges,
  /// variables, and inline function information.
  ///
  /// @return
  ///     The number of bytes that this object occupies in memory.
  //------------------------------------------------------------------
  size_t MemorySize() const;

  //------------------------------------------------------------------
  /// Set accessor for any inlined function information.
  ///
  /// @param[in] name
  ///     The method name for the inlined function. This value should
  ///     not be nullptr.
  ///
  /// @param[in] mangled
  ///     The mangled method name for the inlined function. This can
  ///     be nullptr if there is no mangled name for an inlined function
  ///     or if the name is the same as \a name.
  ///
  /// @param[in] decl_ptr
  ///     A optional pointer to declaration information for the
  ///     inlined function information. This value can be nullptr to
  ///     indicate that no declaration information is available.
  ///
  /// @param[in] call_decl_ptr
  ///     Optional calling location declaration information that
  ///     describes from where this inlined function was called.
  //------------------------------------------------------------------
  void SetInlinedFunctionInfo(const char *name, const char *mangled,
                              const Declaration *decl_ptr,
                              const Declaration *call_decl_ptr);

  void SetParentScope(SymbolContextScope *parent_scope) {
    m_parent_scope = parent_scope;
  }

  //------------------------------------------------------------------
  /// Set accessor for the variable list.
  ///
  /// Called by the SymbolFile plug-ins after they have parsed the variable
  /// lists and are ready to hand ownership of the list over to this object.
  ///
  /// @param[in] variable_list_sp
  ///     A shared pointer to a VariableList.
  //------------------------------------------------------------------
  void SetVariableList(lldb::VariableListSP &variable_list_sp) {
    m_variable_list_sp = variable_list_sp;
  }

  bool BlockInfoHasBeenParsed() const { return m_parsed_block_info; }

  void SetBlockInfoHasBeenParsed(bool b, bool set_children);

  Block *FindBlockByID(lldb::user_id_t block_id);

  size_t GetNumRanges() const { return m_ranges.GetSize(); }

  bool GetRangeContainingOffset(const lldb::addr_t offset, Range &range);

  bool GetRangeContainingAddress(const Address &addr, AddressRange &range);

  bool GetRangeContainingLoadAddress(lldb::addr_t load_addr, Target &target,
                                     AddressRange &range);

  uint32_t GetRangeIndexContainingAddress(const Address &addr);

  //------------------------------------------------------------------
  // Since blocks might have multiple discontiguous address ranges, we need to
  // be able to get at any of the address ranges in a block.
  //------------------------------------------------------------------
  bool GetRangeAtIndex(uint32_t range_idx, AddressRange &range);

  bool GetStartAddress(Address &addr);

  void SetDidParseVariables(bool b, bool set_children);

protected:
  typedef std::vector<lldb::BlockSP> collection;
  //------------------------------------------------------------------
  // Member variables.
  //------------------------------------------------------------------
  SymbolContextScope *m_parent_scope;
  collection m_children;
  RangeList m_ranges;
  lldb::InlineFunctionInfoSP m_inlineInfoSP; ///< Inlined function information.
  lldb::VariableListSP m_variable_list_sp; ///< The variable list for all local,
                                           ///static and parameter variables
                                           ///scoped to this block.
  bool m_parsed_block_info : 1, ///< Set to true if this block and it's children
                                ///have all been parsed
      m_parsed_block_variables : 1, m_parsed_child_blocks : 1;

  // A parent of child blocks can be asked to find a sibling block given
  // one of its child blocks
  Block *GetSiblingForChild(const Block *child_block) const;

private:
  DISALLOW_COPY_AND_ASSIGN(Block);
};

} // namespace lldb_private

#endif // liblldb_Block_h_
