//===-- UnwindPlan.h --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_UnwindPlan_h
#define liblldb_UnwindPlan_h

#include <map>
#include <memory>
#include <vector>

#include "lldb/Core/AddressRange.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Stream.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

// The UnwindPlan object specifies how to unwind out of a function - where this
// function saves the caller's register values before modifying them (for non-
// volatile aka saved registers) and how to find this frame's Canonical Frame
// Address (CFA) or Aligned Frame Address (AFA).

// CFA is a DWARF's Canonical Frame Address.
// Most commonly, registers are saved on the stack, offset some bytes from the
// Canonical Frame Address, or CFA, which is the starting address of this
// function's stack frame (the CFA is same as the eh_frame's CFA, whatever that
// may be on a given architecture). The CFA address for the stack frame does
// not change during the lifetime of the function.

// AFA is an artificially introduced Aligned Frame Address.
// It is used only for stack frames with realignment (e.g. when some of the
// locals has an alignment requirement higher than the stack alignment right
// after the function call). It is used to access register values saved on the
// stack after the realignment (and so they are inaccessible through the CFA).
// AFA usually equals the stack pointer value right after the realignment.

// Internally, the UnwindPlan is structured as a vector of register locations
// organized by code address in the function, showing which registers have been
// saved at that point and where they are saved. It can be thought of as the
// expanded table form of the DWARF CFI encoded information.

// Other unwind information sources will be converted into UnwindPlans before
// being added to a FuncUnwinders object.  The unwind source may be an eh_frame
// FDE, a DWARF debug_frame FDE, or assembly language based prologue analysis.
// The UnwindPlan is the canonical form of this information that the unwinder
// code will use when walking the stack.

class UnwindPlan {
public:
  class Row {
  public:
    class RegisterLocation {
    public:
      enum RestoreType {
        unspecified,       // not specified, we may be able to assume this
                           // is the same register. gcc doesn't specify all
                           // initial values so we really don't know...
        undefined,         // reg is not available, e.g. volatile reg
        same,              // reg is unchanged
        atCFAPlusOffset,   // reg = deref(CFA + offset)
        isCFAPlusOffset,   // reg = CFA + offset
        atAFAPlusOffset,   // reg = deref(AFA + offset)
        isAFAPlusOffset,   // reg = AFA + offset
        inOtherRegister,   // reg = other reg
        atDWARFExpression, // reg = deref(eval(dwarf_expr))
        isDWARFExpression  // reg = eval(dwarf_expr)
      };

      RegisterLocation() : m_type(unspecified), m_location() {}

      bool operator==(const RegisterLocation &rhs) const;

      bool operator!=(const RegisterLocation &rhs) const {
        return !(*this == rhs);
      }

      void SetUnspecified() { m_type = unspecified; }

      void SetUndefined() { m_type = undefined; }

      void SetSame() { m_type = same; }

      bool IsSame() const { return m_type == same; }

      bool IsUnspecified() const { return m_type == unspecified; }

      bool IsUndefined() const { return m_type == undefined; }

      bool IsCFAPlusOffset() const { return m_type == isCFAPlusOffset; }

      bool IsAtCFAPlusOffset() const { return m_type == atCFAPlusOffset; }

      bool IsAFAPlusOffset() const { return m_type == isAFAPlusOffset; }

      bool IsAtAFAPlusOffset() const { return m_type == atAFAPlusOffset; }

      bool IsInOtherRegister() const { return m_type == inOtherRegister; }

      bool IsAtDWARFExpression() const { return m_type == atDWARFExpression; }

      bool IsDWARFExpression() const { return m_type == isDWARFExpression; }

      void SetAtCFAPlusOffset(int32_t offset) {
        m_type = atCFAPlusOffset;
        m_location.offset = offset;
      }

      void SetIsCFAPlusOffset(int32_t offset) {
        m_type = isCFAPlusOffset;
        m_location.offset = offset;
      }

      void SetAtAFAPlusOffset(int32_t offset) {
        m_type = atAFAPlusOffset;
        m_location.offset = offset;
      }

      void SetIsAFAPlusOffset(int32_t offset) {
        m_type = isAFAPlusOffset;
        m_location.offset = offset;
      }

      void SetInRegister(uint32_t reg_num) {
        m_type = inOtherRegister;
        m_location.reg_num = reg_num;
      }

      uint32_t GetRegisterNumber() const {
        if (m_type == inOtherRegister)
          return m_location.reg_num;
        return LLDB_INVALID_REGNUM;
      }

      RestoreType GetLocationType() const { return m_type; }

      int32_t GetOffset() const {
        switch(m_type)
        {
        case atCFAPlusOffset:
        case isCFAPlusOffset:
        case atAFAPlusOffset:
        case isAFAPlusOffset:
          return m_location.offset;
        default:
          return 0;
        }
      }

      void GetDWARFExpr(const uint8_t **opcodes, uint16_t &len) const {
        if (m_type == atDWARFExpression || m_type == isDWARFExpression) {
          *opcodes = m_location.expr.opcodes;
          len = m_location.expr.length;
        } else {
          *opcodes = nullptr;
          len = 0;
        }
      }

      void SetAtDWARFExpression(const uint8_t *opcodes, uint32_t len);

      void SetIsDWARFExpression(const uint8_t *opcodes, uint32_t len);

      const uint8_t *GetDWARFExpressionBytes() {
        if (m_type == atDWARFExpression || m_type == isDWARFExpression)
          return m_location.expr.opcodes;
        return nullptr;
      }

      int GetDWARFExpressionLength() {
        if (m_type == atDWARFExpression || m_type == isDWARFExpression)
          return m_location.expr.length;
        return 0;
      }

      void Dump(Stream &s, const UnwindPlan *unwind_plan,
                const UnwindPlan::Row *row, Thread *thread, bool verbose) const;

    private:
      RestoreType m_type; // How do we locate this register?
      union {
        // For m_type == atCFAPlusOffset or m_type == isCFAPlusOffset
        int32_t offset;
        // For m_type == inOtherRegister
        uint32_t reg_num; // The register number
        // For m_type == atDWARFExpression or m_type == isDWARFExpression
        struct {
          const uint8_t *opcodes;
          uint16_t length;
        } expr;
      } m_location;
    };

    class FAValue {
    public:
      enum ValueType {
        unspecified,            // not specified
        isRegisterPlusOffset,   // FA = register + offset
        isRegisterDereferenced, // FA = [reg]
        isDWARFExpression       // FA = eval(dwarf_expr)
      };

      FAValue() : m_type(unspecified), m_value() {}

      bool operator==(const FAValue &rhs) const;

      bool operator!=(const FAValue &rhs) const { return !(*this == rhs); }

      void SetUnspecified() { m_type = unspecified; }

      bool IsUnspecified() const { return m_type == unspecified; }

      bool IsRegisterPlusOffset() const {
        return m_type == isRegisterPlusOffset;
      }

      void SetIsRegisterPlusOffset(uint32_t reg_num, int32_t offset) {
        m_type = isRegisterPlusOffset;
        m_value.reg.reg_num = reg_num;
        m_value.reg.offset = offset;
      }

      bool IsRegisterDereferenced() const {
        return m_type == isRegisterDereferenced;
      }

      void SetIsRegisterDereferenced(uint32_t reg_num) {
        m_type = isRegisterDereferenced;
        m_value.reg.reg_num = reg_num;
      }

      bool IsDWARFExpression() const { return m_type == isDWARFExpression; }

      void SetIsDWARFExpression(const uint8_t *opcodes, uint32_t len) {
        m_type = isDWARFExpression;
        m_value.expr.opcodes = opcodes;
        m_value.expr.length = len;
      }

      uint32_t GetRegisterNumber() const {
        if (m_type == isRegisterDereferenced || m_type == isRegisterPlusOffset)
          return m_value.reg.reg_num;
        return LLDB_INVALID_REGNUM;
      }

      ValueType GetValueType() const { return m_type; }

      int32_t GetOffset() const {
        if (m_type == isRegisterPlusOffset)
          return m_value.reg.offset;
        return 0;
      }

      void IncOffset(int32_t delta) {
        if (m_type == isRegisterPlusOffset)
          m_value.reg.offset += delta;
      }

      void SetOffset(int32_t offset) {
        if (m_type == isRegisterPlusOffset)
          m_value.reg.offset = offset;
      }

      void GetDWARFExpr(const uint8_t **opcodes, uint16_t &len) const {
        if (m_type == isDWARFExpression) {
          *opcodes = m_value.expr.opcodes;
          len = m_value.expr.length;
        } else {
          *opcodes = nullptr;
          len = 0;
        }
      }

      const uint8_t *GetDWARFExpressionBytes() {
        if (m_type == isDWARFExpression)
          return m_value.expr.opcodes;
        return nullptr;
      }

      int GetDWARFExpressionLength() {
        if (m_type == isDWARFExpression)
          return m_value.expr.length;
        return 0;
      }

      void Dump(Stream &s, const UnwindPlan *unwind_plan, Thread *thread) const;

    private:
      ValueType m_type; // How do we compute CFA value?
      union {
        struct {
          // For m_type == isRegisterPlusOffset or m_type ==
          // isRegisterDereferenced
          uint32_t reg_num; // The register number
          // For m_type == isRegisterPlusOffset
          int32_t offset;
        } reg;
        // For m_type == isDWARFExpression
        struct {
          const uint8_t *opcodes;
          uint16_t length;
        } expr;
      } m_value;
    }; // class FAValue

  public:
    Row();

    Row(const UnwindPlan::Row &rhs) = default;

    bool operator==(const Row &rhs) const;

    bool GetRegisterInfo(uint32_t reg_num,
                         RegisterLocation &register_location) const;

    void SetRegisterInfo(uint32_t reg_num,
                         const RegisterLocation register_location);

    void RemoveRegisterInfo(uint32_t reg_num);

    lldb::addr_t GetOffset() const { return m_offset; }

    void SetOffset(lldb::addr_t offset) { m_offset = offset; }

    void SlideOffset(lldb::addr_t offset) { m_offset += offset; }

    FAValue &GetCFAValue() { return m_cfa_value; }

    FAValue &GetAFAValue() { return m_afa_value; }

    bool SetRegisterLocationToAtCFAPlusOffset(uint32_t reg_num, int32_t offset,
                                              bool can_replace);

    bool SetRegisterLocationToIsCFAPlusOffset(uint32_t reg_num, int32_t offset,
                                              bool can_replace);

    bool SetRegisterLocationToUndefined(uint32_t reg_num, bool can_replace,
                                        bool can_replace_only_if_unspecified);

    bool SetRegisterLocationToUnspecified(uint32_t reg_num, bool can_replace);

    bool SetRegisterLocationToRegister(uint32_t reg_num, uint32_t other_reg_num,
                                       bool can_replace);

    bool SetRegisterLocationToSame(uint32_t reg_num, bool must_replace);

    void Clear();

    void Dump(Stream &s, const UnwindPlan *unwind_plan, Thread *thread,
              lldb::addr_t base_addr) const;

  protected:
    typedef std::map<uint32_t, RegisterLocation> collection;
    lldb::addr_t m_offset; // Offset into the function for this row

    FAValue m_cfa_value;
    FAValue m_afa_value;
    collection m_register_locations;
  }; // class Row

public:
  typedef std::shared_ptr<Row> RowSP;

  UnwindPlan(lldb::RegisterKind reg_kind)
      : m_row_list(), m_plan_valid_address_range(), m_register_kind(reg_kind),
        m_return_addr_register(LLDB_INVALID_REGNUM), m_source_name(),
        m_plan_is_sourced_from_compiler(eLazyBoolCalculate),
        m_plan_is_valid_at_all_instruction_locations(eLazyBoolCalculate),
        m_lsda_address(), m_personality_func_addr() {}

  // Performs a deep copy of the plan, including all the rows (expensive).
  UnwindPlan(const UnwindPlan &rhs)
      : m_plan_valid_address_range(rhs.m_plan_valid_address_range),
        m_register_kind(rhs.m_register_kind),
        m_return_addr_register(rhs.m_return_addr_register),
        m_source_name(rhs.m_source_name),
        m_plan_is_sourced_from_compiler(rhs.m_plan_is_sourced_from_compiler),
        m_plan_is_valid_at_all_instruction_locations(
            rhs.m_plan_is_valid_at_all_instruction_locations),
        m_lsda_address(rhs.m_lsda_address),
        m_personality_func_addr(rhs.m_personality_func_addr) {
    m_row_list.reserve(rhs.m_row_list.size());
    for (const RowSP &row_sp : rhs.m_row_list)
      m_row_list.emplace_back(new Row(*row_sp));
  }

  ~UnwindPlan() = default;

  void Dump(Stream &s, Thread *thread, lldb::addr_t base_addr) const;

  void AppendRow(const RowSP &row_sp);

  void InsertRow(const RowSP &row_sp, bool replace_existing = false);

  // Returns a pointer to the best row for the given offset into the function's
  // instructions. If offset is -1 it indicates that the function start is
  // unknown - the final row in the UnwindPlan is returned. In practice, the
  // UnwindPlan for a function with no known start address will be the
  // architectural default UnwindPlan which will only have one row.
  UnwindPlan::RowSP GetRowForFunctionOffset(int offset) const;

  lldb::RegisterKind GetRegisterKind() const { return m_register_kind; }

  void SetRegisterKind(lldb::RegisterKind kind) { m_register_kind = kind; }

  void SetReturnAddressRegister(uint32_t regnum) {
    m_return_addr_register = regnum;
  }

  uint32_t GetReturnAddressRegister(void) { return m_return_addr_register; }

  uint32_t GetInitialCFARegister() const {
    if (m_row_list.empty())
      return LLDB_INVALID_REGNUM;
    return m_row_list.front()->GetCFAValue().GetRegisterNumber();
  }

  // This UnwindPlan may not be valid at every address of the function span.
  // For instance, a FastUnwindPlan will not be valid at the prologue setup
  // instructions - only in the body of the function.
  void SetPlanValidAddressRange(const AddressRange &range);

  const AddressRange &GetAddressRange() const {
    return m_plan_valid_address_range;
  }

  bool PlanValidAtAddress(Address addr);

  bool IsValidRowIndex(uint32_t idx) const;

  const UnwindPlan::RowSP GetRowAtIndex(uint32_t idx) const;

  const UnwindPlan::RowSP GetLastRow() const;

  lldb_private::ConstString GetSourceName() const;

  void SetSourceName(const char *);

  // Was this UnwindPlan emitted by a compiler?
  lldb_private::LazyBool GetSourcedFromCompiler() const {
    return m_plan_is_sourced_from_compiler;
  }

  // Was this UnwindPlan emitted by a compiler?
  void SetSourcedFromCompiler(lldb_private::LazyBool from_compiler) {
    m_plan_is_sourced_from_compiler = from_compiler;
  }

  // Is this UnwindPlan valid at all instructions?  If not, then it is assumed
  // valid at call sites, e.g. for exception handling.
  lldb_private::LazyBool GetUnwindPlanValidAtAllInstructions() const {
    return m_plan_is_valid_at_all_instruction_locations;
  }

  // Is this UnwindPlan valid at all instructions?  If not, then it is assumed
  // valid at call sites, e.g. for exception handling.
  void SetUnwindPlanValidAtAllInstructions(
      lldb_private::LazyBool valid_at_all_insn) {
    m_plan_is_valid_at_all_instruction_locations = valid_at_all_insn;
  }

  int GetRowCount() const;

  void Clear() {
    m_row_list.clear();
    m_plan_valid_address_range.Clear();
    m_register_kind = lldb::eRegisterKindDWARF;
    m_source_name.Clear();
    m_plan_is_sourced_from_compiler = eLazyBoolCalculate;
    m_plan_is_valid_at_all_instruction_locations = eLazyBoolCalculate;
    m_lsda_address.Clear();
    m_personality_func_addr.Clear();
  }

  const RegisterInfo *GetRegisterInfo(Thread *thread, uint32_t reg_num) const;

  Address GetLSDAAddress() const { return m_lsda_address; }

  void SetLSDAAddress(Address lsda_addr) { m_lsda_address = lsda_addr; }

  Address GetPersonalityFunctionPtr() const { return m_personality_func_addr; }

  void SetPersonalityFunctionPtr(Address presonality_func_ptr) {
    m_personality_func_addr = presonality_func_ptr;
  }

private:
  typedef std::vector<RowSP> collection;
  collection m_row_list;
  AddressRange m_plan_valid_address_range;
  lldb::RegisterKind m_register_kind; // The RegisterKind these register numbers
                                      // are in terms of - will need to be
  // translated to lldb native reg nums at unwind time
  uint32_t m_return_addr_register; // The register that has the return address
                                   // for the caller frame
                                   // e.g. the lr on arm
  lldb_private::ConstString
      m_source_name; // for logging, where this UnwindPlan originated from
  lldb_private::LazyBool m_plan_is_sourced_from_compiler;
  lldb_private::LazyBool m_plan_is_valid_at_all_instruction_locations;

  Address m_lsda_address; // Where the language specific data area exists in the
                          // module - used
                          // in exception handling.
  Address m_personality_func_addr; // The address of a pointer to the
                                   // personality function - used in
                                   // exception handling.
};                                 // class UnwindPlan

} // namespace lldb_private

#endif // liblldb_UnwindPlan_h
