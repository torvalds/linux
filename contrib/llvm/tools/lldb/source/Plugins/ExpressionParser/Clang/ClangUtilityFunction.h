//===-- ClangUtilityFunction.h ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ClangUtilityFunction_h_
#define liblldb_ClangUtilityFunction_h_

#include <map>
#include <string>
#include <vector>

#include "ClangExpressionHelper.h"

#include "lldb/Core/ClangForward.h"
#include "lldb/Expression/UtilityFunction.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

//----------------------------------------------------------------------
/// @class ClangUtilityFunction ClangUtilityFunction.h
/// "lldb/Expression/ClangUtilityFunction.h" Encapsulates a single expression
/// for use with Clang
///
/// LLDB uses expressions for various purposes, notably to call functions
/// and as a backend for the expr command.  ClangUtilityFunction encapsulates
/// a self-contained function meant to be used from other code.  Utility
/// functions can perform error-checking for ClangUserExpressions, or can
/// simply provide a way to push a function into the target for the debugger
/// to call later on.
//----------------------------------------------------------------------
class ClangUtilityFunction : public UtilityFunction {
public:
  class ClangUtilityFunctionHelper : public ClangExpressionHelper {
  public:
    ClangUtilityFunctionHelper() {}

    ~ClangUtilityFunctionHelper() override {}

    //------------------------------------------------------------------
    /// Return the object that the parser should use when resolving external
    /// values.  May be NULL if everything should be self-contained.
    //------------------------------------------------------------------
    ClangExpressionDeclMap *DeclMap() override {
      return m_expr_decl_map_up.get();
    }

    void ResetDeclMap() { m_expr_decl_map_up.reset(); }

    void ResetDeclMap(ExecutionContext &exe_ctx, bool keep_result_in_memory);

    //------------------------------------------------------------------
    /// Return the object that the parser should allow to access ASTs. May be
    /// NULL if the ASTs do not need to be transformed.
    ///
    /// @param[in] passthrough
    ///     The ASTConsumer that the returned transformer should send
    ///     the ASTs to after transformation.
    //------------------------------------------------------------------
    clang::ASTConsumer *
    ASTTransformer(clang::ASTConsumer *passthrough) override {
      return nullptr;
    }

  private:
    std::unique_ptr<ClangExpressionDeclMap> m_expr_decl_map_up;
  };
  //------------------------------------------------------------------
  /// Constructor
  ///
  /// @param[in] text
  ///     The text of the function.  Must be a full translation unit.
  ///
  /// @param[in] name
  ///     The name of the function, as used in the text.
  //------------------------------------------------------------------
  ClangUtilityFunction(ExecutionContextScope &exe_scope, const char *text,
                       const char *name);

  ~ClangUtilityFunction() override;

  ExpressionTypeSystemHelper *GetTypeSystemHelper() override {
    return &m_type_system_helper;
  }

  ClangExpressionDeclMap *DeclMap() { return m_type_system_helper.DeclMap(); }

  void ResetDeclMap() { m_type_system_helper.ResetDeclMap(); }

  void ResetDeclMap(ExecutionContext &exe_ctx, bool keep_result_in_memory) {
    m_type_system_helper.ResetDeclMap(exe_ctx, keep_result_in_memory);
  }

  bool Install(DiagnosticManager &diagnostic_manager,
               ExecutionContext &exe_ctx) override;

private:
  ClangUtilityFunctionHelper m_type_system_helper; ///< The map to use when
                                                   ///parsing and materializing
                                                   ///the expression.
};

} // namespace lldb_private

#endif // liblldb_ClangUtilityFunction_h_
