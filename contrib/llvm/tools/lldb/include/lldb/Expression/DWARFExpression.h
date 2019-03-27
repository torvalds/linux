//===-- DWARFExpression.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_DWARFExpression_h_
#define liblldb_DWARFExpression_h_

#include "lldb/Core/Address.h"
#include "lldb/Core/Disassembler.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Scalar.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-private.h"
#include <functional>

class DWARFUnit;

namespace lldb_private {

//----------------------------------------------------------------------
/// @class DWARFExpression DWARFExpression.h
/// "lldb/Expression/DWARFExpression.h" Encapsulates a DWARF location
/// expression and interprets it.
///
/// DWARF location expressions are used in two ways by LLDB.  The first
/// use is to find entities specified in the debug information, since their
/// locations are specified in precisely this language.  The second is to
/// interpret expressions without having to run the target in cases where the
/// overhead from copying JIT-compiled code into the target is too high or
/// where the target cannot be run.  This class encapsulates a single DWARF
/// location expression or a location list and interprets it.
//----------------------------------------------------------------------
class DWARFExpression {
public:
  enum LocationListFormat : uint8_t {
    NonLocationList,     // Not a location list
    RegularLocationList, // Location list format used in non-split dwarf files
    SplitDwarfLocationList, // Location list format used in pre-DWARF v5 split
                            // dwarf files (.debug_loc.dwo)
    LocLists,               // Location list format used in DWARF v5
                            // (.debug_loclists/.debug_loclists.dwo).
  };

  //------------------------------------------------------------------
  /// Constructor
  //------------------------------------------------------------------
  explicit DWARFExpression(DWARFUnit *dwarf_cu);

  //------------------------------------------------------------------
  /// Constructor
  ///
  /// @param[in] data
  ///     A data extractor configured to read the DWARF location expression's
  ///     bytecode.
  ///
  /// @param[in] data_offset
  ///     The offset of the location expression in the extractor.
  ///
  /// @param[in] data_length
  ///     The byte length of the location expression.
  //------------------------------------------------------------------
  DWARFExpression(lldb::ModuleSP module, const DataExtractor &data,
                  DWARFUnit *dwarf_cu, lldb::offset_t data_offset,
                  lldb::offset_t data_length);

  //------------------------------------------------------------------
  /// Copy constructor
  //------------------------------------------------------------------
  DWARFExpression(const DWARFExpression &rhs);

  //------------------------------------------------------------------
  /// Destructor
  //------------------------------------------------------------------
  virtual ~DWARFExpression();

  //------------------------------------------------------------------
  /// Print the description of the expression to a stream
  ///
  /// @param[in] s
  ///     The stream to print to.
  ///
  /// @param[in] level
  ///     The level of verbosity to use.
  ///
  /// @param[in] location_list_base_addr
  ///     If this is a location list based expression, this is the
  ///     address of the object that owns it. NOTE: this value is
  ///     different from the DWARF version of the location list base
  ///     address which is compile unit relative. This base address
  ///     is the address of the object that owns the location list.
  ///
  /// @param[in] abi
  ///     An optional ABI plug-in that can be used to resolve register
  ///     names.
  //------------------------------------------------------------------
  void GetDescription(Stream *s, lldb::DescriptionLevel level,
                      lldb::addr_t location_list_base_addr, ABI *abi) const;

  //------------------------------------------------------------------
  /// Return true if the location expression contains data
  //------------------------------------------------------------------
  bool IsValid() const;

  //------------------------------------------------------------------
  /// Return true if a location list was provided
  //------------------------------------------------------------------
  bool IsLocationList() const;

  //------------------------------------------------------------------
  /// Search for a load address in the location list
  ///
  /// @param[in] process
  ///     The process to use when resolving the load address
  ///
  /// @param[in] addr
  ///     The address to resolve
  ///
  /// @return
  ///     True if IsLocationList() is true and the address was found;
  ///     false otherwise.
  //------------------------------------------------------------------
  //    bool
  //    LocationListContainsLoadAddress (Process* process, const Address &addr)
  //    const;
  //
  bool LocationListContainsAddress(lldb::addr_t loclist_base_addr,
                                   lldb::addr_t addr) const;

  //------------------------------------------------------------------
  /// If a location is not a location list, return true if the location
  /// contains a DW_OP_addr () opcode in the stream that matches \a file_addr.
  /// If file_addr is LLDB_INVALID_ADDRESS, the this function will return true
  /// if the variable there is any DW_OP_addr in a location that (yet still is
  /// NOT a location list). This helps us detect if a variable is a global or
  /// static variable since there is no other indication from DWARF debug
  /// info.
  ///
  /// @param[in] op_addr_idx
  ///     The DW_OP_addr index to retrieve in case there is more than
  ///     one DW_OP_addr opcode in the location byte stream.
  ///
  /// @param[out] error
  ///     If the location stream contains unknown DW_OP opcodes or the
  ///     data is missing, \a error will be set to \b true.
  ///
  /// @return
  ///     LLDB_INVALID_ADDRESS if the location doesn't contain a
  ///     DW_OP_addr for \a op_addr_idx, otherwise a valid file address
  //------------------------------------------------------------------
  lldb::addr_t GetLocation_DW_OP_addr(uint32_t op_addr_idx, bool &error) const;

  bool Update_DW_OP_addr(lldb::addr_t file_addr);

  void SetModule(const lldb::ModuleSP &module) { m_module_wp = module; }

  bool ContainsThreadLocalStorage() const;

  bool LinkThreadLocalStorage(
      lldb::ModuleSP new_module_sp,
      std::function<lldb::addr_t(lldb::addr_t file_addr)> const
          &link_address_callback);

  //------------------------------------------------------------------
  /// Make the expression parser read its location information from a given
  /// data source.  Does not change the offset and length
  ///
  /// @param[in] data
  ///     A data extractor configured to read the DWARF location expression's
  ///     bytecode.
  //------------------------------------------------------------------
  void SetOpcodeData(const DataExtractor &data);

  //------------------------------------------------------------------
  /// Make the expression parser read its location information from a given
  /// data source
  ///
  /// @param[in] module_sp
  ///     The module that defines the DWARF expression.
  ///
  /// @param[in] data
  ///     A data extractor configured to read the DWARF location expression's
  ///     bytecode.
  ///
  /// @param[in] data_offset
  ///     The offset of the location expression in the extractor.
  ///
  /// @param[in] data_length
  ///     The byte length of the location expression.
  //------------------------------------------------------------------
  void SetOpcodeData(lldb::ModuleSP module_sp, const DataExtractor &data,
                     lldb::offset_t data_offset, lldb::offset_t data_length);

  //------------------------------------------------------------------
  /// Copy the DWARF location expression into a local buffer.
  ///
  /// It is a good idea to copy the data so we don't keep the entire object
  /// file worth of data around just for a few bytes of location expression.
  /// LLDB typically will mmap the entire contents of debug information files,
  /// and if we use SetOpcodeData, it will get a shared reference to all of
  /// this data for the and cause the object file to have to stay around. Even
  /// worse, a very very large ".a" that contains one or more .o files could
  /// end up being referenced. Location lists are typically small so even
  /// though we are copying the data, it shouldn't amount to that much for the
  /// variables we end up parsing.
  ///
  /// @param[in] module_sp
  ///     The module that defines the DWARF expression.
  ///
  /// @param[in] data
  ///     A data extractor configured to read and copy the DWARF
  ///     location expression's bytecode.
  ///
  /// @param[in] data_offset
  ///     The offset of the location expression in the extractor.
  ///
  /// @param[in] data_length
  ///     The byte length of the location expression.
  //------------------------------------------------------------------
  void CopyOpcodeData(lldb::ModuleSP module_sp, const DataExtractor &data,
                      lldb::offset_t data_offset, lldb::offset_t data_length);

  void CopyOpcodeData(const void *data, lldb::offset_t data_length,
                      lldb::ByteOrder byte_order, uint8_t addr_byte_size);

  void CopyOpcodeData(uint64_t const_value,
                      lldb::offset_t const_value_byte_size,
                      uint8_t addr_byte_size);

  //------------------------------------------------------------------
  /// Tells the expression that it refers to a location list.
  ///
  /// @param[in] slide
  ///     This value should be a slide that is applied to any values
  ///     in the location list data so the values become zero based
  ///     offsets into the object that owns the location list. We need
  ///     to make location lists relative to the objects that own them
  ///     so we can relink addresses on the fly.
  //------------------------------------------------------------------
  void SetLocationListSlide(lldb::addr_t slide);

  //------------------------------------------------------------------
  /// Return the call-frame-info style register kind
  //------------------------------------------------------------------
  int GetRegisterKind();

  //------------------------------------------------------------------
  /// Set the call-frame-info style register kind
  ///
  /// @param[in] reg_kind
  ///     The register kind.
  //------------------------------------------------------------------
  void SetRegisterKind(lldb::RegisterKind reg_kind);

  //------------------------------------------------------------------
  /// Wrapper for the static evaluate function that accepts an
  /// ExecutionContextScope instead of an ExecutionContext and uses member
  /// variables to populate many operands
  //------------------------------------------------------------------
  bool Evaluate(ExecutionContextScope *exe_scope,
                lldb::addr_t loclist_base_load_addr,
                const Value *initial_value_ptr, const Value *object_address_ptr,
                Value &result, Status *error_ptr) const;

  //------------------------------------------------------------------
  /// Wrapper for the static evaluate function that uses member variables to
  /// populate many operands
  //------------------------------------------------------------------
  bool Evaluate(ExecutionContext *exe_ctx, RegisterContext *reg_ctx,
                lldb::addr_t loclist_base_load_addr,
                const Value *initial_value_ptr, const Value *object_address_ptr,
                Value &result, Status *error_ptr) const;

  //------------------------------------------------------------------
  /// Evaluate a DWARF location expression in a particular context
  ///
  /// @param[in] exe_ctx
  ///     The execution context in which to evaluate the location
  ///     expression.  The location expression may access the target's
  ///     memory, especially if it comes from the expression parser.
  ///
  /// @param[in] opcode_ctx
  ///     The module which defined the expression.
  ///
  /// @param[in] opcodes
  ///     This is a static method so the opcodes need to be provided
  ///     explicitly.
  ///
  /// @param[in] expr_locals
  ///     If the location expression was produced by the expression parser,
  ///     the list of local variables referenced by the DWARF expression.
  ///     This list should already have been populated during parsing;
  ///     the DWARF expression refers to variables by index.  Can be NULL if
  ///     the location expression uses no locals.
  ///
  /// @param[in] decl_map
  ///     If the location expression was produced by the expression parser,
  ///     the list of external variables referenced by the location
  ///     expression.  Can be NULL if the location expression uses no
  ///     external variables.
  ///
  ///  @param[in] reg_ctx
  ///     An optional parameter which provides a RegisterContext for use
  ///     when evaluating the expression (i.e. for fetching register values).
  ///     Normally this will come from the ExecutionContext's StackFrame but
  ///     in the case where an expression needs to be evaluated while building
  ///     the stack frame list, this short-cut is available.
  ///
  /// @param[in] offset
  ///     The offset of the location expression in the data extractor.
  ///
  /// @param[in] length
  ///     The length in bytes of the location expression.
  ///
  /// @param[in] reg_set
  ///     The call-frame-info style register kind.
  ///
  /// @param[in] initial_value_ptr
  ///     A value to put on top of the interpreter stack before evaluating
  ///     the expression, if the expression is parametrized.  Can be NULL.
  ///
  /// @param[in] result
  ///     A value into which the result of evaluating the expression is
  ///     to be placed.
  ///
  /// @param[in] error_ptr
  ///     If non-NULL, used to report errors in expression evaluation.
  ///
  /// @return
  ///     True on success; false otherwise.  If error_ptr is non-NULL,
  ///     details of the failure are provided through it.
  //------------------------------------------------------------------
  static bool Evaluate(ExecutionContext *exe_ctx, RegisterContext *reg_ctx,
                       lldb::ModuleSP opcode_ctx, const DataExtractor &opcodes,
                       DWARFUnit *dwarf_cu, const lldb::offset_t offset,
                       const lldb::offset_t length,
                       const lldb::RegisterKind reg_set,
                       const Value *initial_value_ptr,
                       const Value *object_address_ptr, Value &result,
                       Status *error_ptr);

  bool GetExpressionData(DataExtractor &data) const {
    data = m_data;
    return data.GetByteSize() > 0;
  }

  bool DumpLocationForAddress(Stream *s, lldb::DescriptionLevel level,
                              lldb::addr_t loclist_base_load_addr,
                              lldb::addr_t address, ABI *abi);

  static size_t LocationListSize(const DWARFUnit *dwarf_cu,
                                 const DataExtractor &debug_loc_data,
                                 lldb::offset_t offset);

  static bool PrintDWARFExpression(Stream &s, const DataExtractor &data,
                                   int address_size, int dwarf_ref_size,
                                   bool location_expression);

  static void PrintDWARFLocationList(Stream &s, const DWARFUnit *cu,
                                     const DataExtractor &debug_loc_data,
                                     lldb::offset_t offset);

  bool MatchesOperand(StackFrame &frame, const Instruction::Operand &op);

protected:
  //------------------------------------------------------------------
  /// Pretty-prints the location expression to a stream
  ///
  /// @param[in] stream
  ///     The stream to use for pretty-printing.
  ///
  /// @param[in] offset
  ///     The offset into the data buffer of the opcodes to be printed.
  ///
  /// @param[in] length
  ///     The length in bytes of the opcodes to be printed.
  ///
  /// @param[in] level
  ///     The level of detail to use in pretty-printing.
  ///
  /// @param[in] abi
  ///     An optional ABI plug-in that can be used to resolve register
  ///     names.
  //------------------------------------------------------------------
  void DumpLocation(Stream *s, lldb::offset_t offset, lldb::offset_t length,
                    lldb::DescriptionLevel level, ABI *abi) const;

  bool GetLocation(lldb::addr_t base_addr, lldb::addr_t pc,
                   lldb::offset_t &offset, lldb::offset_t &len);

  static bool AddressRangeForLocationListEntry(
      const DWARFUnit *dwarf_cu, const DataExtractor &debug_loc_data,
      lldb::offset_t *offset_ptr, lldb::addr_t &low_pc, lldb::addr_t &high_pc);

  bool GetOpAndEndOffsets(StackFrame &frame, lldb::offset_t &op_offset,
                          lldb::offset_t &end_offset);

  //------------------------------------------------------------------
  /// Classes that inherit from DWARFExpression can see and modify these
  //------------------------------------------------------------------

  lldb::ModuleWP m_module_wp; ///< Module which defined this expression.
  DataExtractor m_data; ///< A data extractor capable of reading opcode bytes
  DWARFUnit *m_dwarf_cu; ///< The DWARF compile unit this expression
                                ///belongs to. It is used
  ///< to evaluate values indexing into the .debug_addr section (e.g.
  ///< DW_OP_GNU_addr_index, DW_OP_GNU_const_index)
  lldb::RegisterKind
      m_reg_kind; ///< One of the defines that starts with LLDB_REGKIND_
  lldb::addr_t m_loclist_slide; ///< A value used to slide the location list
                                ///offsets so that
  ///< they are relative to the object that owns the location list
  ///< (the function for frame base and variable location lists)
};

} // namespace lldb_private

#endif // liblldb_DWARFExpression_h_
