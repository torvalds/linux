//===-- SBFrame.h -----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBFrame_h_
#define LLDB_SBFrame_h_

#include "lldb/API/SBDefines.h"
#include "lldb/API/SBValueList.h"

namespace lldb {

class LLDB_API SBFrame {
public:
  SBFrame();

  SBFrame(const lldb::SBFrame &rhs);

  const lldb::SBFrame &operator=(const lldb::SBFrame &rhs);

  ~SBFrame();

  bool IsEqual(const lldb::SBFrame &that) const;

  bool IsValid() const;

  uint32_t GetFrameID() const;

  lldb::addr_t GetCFA() const;

  lldb::addr_t GetPC() const;

  bool SetPC(lldb::addr_t new_pc);

  lldb::addr_t GetSP() const;

  lldb::addr_t GetFP() const;

  lldb::SBAddress GetPCAddress() const;

  lldb::SBSymbolContext GetSymbolContext(uint32_t resolve_scope) const;

  lldb::SBModule GetModule() const;

  lldb::SBCompileUnit GetCompileUnit() const;

  lldb::SBFunction GetFunction() const;

  lldb::SBSymbol GetSymbol() const;

  /// Gets the deepest block that contains the frame PC.
  ///
  /// See also GetFrameBlock().
  lldb::SBBlock GetBlock() const;

  /// Get the appropriate function name for this frame. Inlined functions in
  /// LLDB are represented by Blocks that have inlined function information, so
  /// just looking at the SBFunction or SBSymbol for a frame isn't enough.
  /// This function will return the appropriate function, symbol or inlined
  /// function name for the frame.
  ///
  /// This function returns:
  /// - the name of the inlined function (if there is one)
  /// - the name of the concrete function (if there is one)
  /// - the name of the symbol (if there is one)
  /// - NULL
  ///
  /// See also IsInlined().
  const char *GetFunctionName();

  // Get an appropriate function name for this frame that is suitable for
  // display to a user
  const char *GetDisplayFunctionName();

  const char *GetFunctionName() const;
  
  // Return the frame function's language.  If there isn't a function, then
  // guess the language type from the mangled name.
  lldb::LanguageType GuessLanguage() const;

  /// Return true if this frame represents an inlined function.
  ///
  /// See also GetFunctionName().
  bool IsInlined();

  bool IsInlined() const;

  bool IsArtificial();

  bool IsArtificial() const;

  /// The version that doesn't supply a 'use_dynamic' value will use the
  /// target's default.
  lldb::SBValue EvaluateExpression(const char *expr);

  lldb::SBValue EvaluateExpression(const char *expr,
                                   lldb::DynamicValueType use_dynamic);

  lldb::SBValue EvaluateExpression(const char *expr,
                                   lldb::DynamicValueType use_dynamic,
                                   bool unwind_on_error);

  lldb::SBValue EvaluateExpression(const char *expr,
                                   const SBExpressionOptions &options);

  /// Gets the lexical block that defines the stack frame. Another way to think
  /// of this is it will return the block that contains all of the variables
  /// for a stack frame. Inlined functions are represented as SBBlock objects
  /// that have inlined function information: the name of the inlined function,
  /// where it was called from. The block that is returned will be the first
  /// block at or above the block for the PC (SBFrame::GetBlock()) that defines
  /// the scope of the frame. When a function contains no inlined functions,
  /// this will be the top most lexical block that defines the function.
  /// When a function has inlined functions and the PC is currently
  /// in one of those inlined functions, this method will return the inlined
  /// block that defines this frame. If the PC isn't currently in an inlined
  /// function, the lexical block that defines the function is returned.
  lldb::SBBlock GetFrameBlock() const;

  lldb::SBLineEntry GetLineEntry() const;

  lldb::SBThread GetThread() const;

  const char *Disassemble() const;

  void Clear();

  bool operator==(const lldb::SBFrame &rhs) const;

  bool operator!=(const lldb::SBFrame &rhs) const;

  /// The version that doesn't supply a 'use_dynamic' value will use the
  /// target's default.
  lldb::SBValueList GetVariables(bool arguments, bool locals, bool statics,
                                 bool in_scope_only);

  lldb::SBValueList GetVariables(bool arguments, bool locals, bool statics,
                                 bool in_scope_only,
                                 lldb::DynamicValueType use_dynamic);

  lldb::SBValueList GetVariables(const lldb::SBVariablesOptions &options);

  lldb::SBValueList GetRegisters();

  lldb::SBValue FindRegister(const char *name);

  /// The version that doesn't supply a 'use_dynamic' value will use the
  /// target's default.
  lldb::SBValue FindVariable(const char *var_name);

  lldb::SBValue FindVariable(const char *var_name,
                             lldb::DynamicValueType use_dynamic);

  // Find a value for a variable expression path like "rect.origin.x" or
  // "pt_ptr->x", "*self", "*this->obj_ptr". The returned value is _not_ and
  // expression result and is not a constant object like
  // SBFrame::EvaluateExpression(...) returns, but a child object of the
  // variable value.
  lldb::SBValue GetValueForVariablePath(const char *var_expr_cstr,
                                        DynamicValueType use_dynamic);

  /// The version that doesn't supply a 'use_dynamic' value will use the
  /// target's default.
  lldb::SBValue GetValueForVariablePath(const char *var_path);

  /// Find variables, register sets, registers, or persistent variables using
  /// the frame as the scope.
  ///
  /// NB. This function does not look up ivars in the function object pointer.
  /// To do that use GetValueForVariablePath.
  ///
  /// The version that doesn't supply a 'use_dynamic' value will use the
  /// target's default.
  lldb::SBValue FindValue(const char *name, ValueType value_type);

  lldb::SBValue FindValue(const char *name, ValueType value_type,
                          lldb::DynamicValueType use_dynamic);

  bool GetDescription(lldb::SBStream &description);

  SBFrame(const lldb::StackFrameSP &lldb_object_sp);

protected:
  friend class SBBlock;
  friend class SBExecutionContext;
  friend class SBInstruction;
  friend class SBThread;
  friend class SBValue;

  lldb::StackFrameSP GetFrameSP() const;

  void SetFrameSP(const lldb::StackFrameSP &lldb_object_sp);

  lldb::ExecutionContextRefSP m_opaque_sp;
};

} // namespace lldb

#endif // LLDB_SBFrame_h_
