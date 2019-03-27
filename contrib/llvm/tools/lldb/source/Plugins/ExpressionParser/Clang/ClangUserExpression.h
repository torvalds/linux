//===-- ClangUserExpression.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ClangUserExpression_h_
#define liblldb_ClangUserExpression_h_

#include <vector>

#include "ASTResultSynthesizer.h"
#include "ASTStructExtractor.h"
#include "ClangExpressionDeclMap.h"
#include "ClangExpressionHelper.h"
#include "ClangExpressionVariable.h"
#include "IRForTarget.h"

#include "lldb/Core/Address.h"
#include "lldb/Core/ClangForward.h"
#include "lldb/Expression/LLVMUserExpression.h"
#include "lldb/Expression/Materializer.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

//----------------------------------------------------------------------
/// @class ClangUserExpression ClangUserExpression.h
/// "lldb/Expression/ClangUserExpression.h" Encapsulates a single expression
/// for use with Clang
///
/// LLDB uses expressions for various purposes, notably to call functions
/// and as a backend for the expr command.  ClangUserExpression encapsulates
/// the objects needed to parse and interpret or JIT an expression.  It uses
/// the Clang parser to produce LLVM IR from the expression.
//----------------------------------------------------------------------
class ClangUserExpression : public LLVMUserExpression {
public:
  enum { kDefaultTimeout = 500000u };

  class ClangUserExpressionHelper : public ClangExpressionHelper {
  public:
    ClangUserExpressionHelper(Target &target, bool top_level)
        : m_target(target), m_top_level(top_level) {}

    ~ClangUserExpressionHelper() override = default;

    //------------------------------------------------------------------
    /// Return the object that the parser should use when resolving external
    /// values.  May be NULL if everything should be self-contained.
    //------------------------------------------------------------------
    ClangExpressionDeclMap *DeclMap() override {
      return m_expr_decl_map_up.get();
    }

    void ResetDeclMap() { m_expr_decl_map_up.reset(); }

    void ResetDeclMap(ExecutionContext &exe_ctx,
                      Materializer::PersistentVariableDelegate &result_delegate,
                      bool keep_result_in_memory);

    //------------------------------------------------------------------
    /// Return the object that the parser should allow to access ASTs. May be
    /// NULL if the ASTs do not need to be transformed.
    ///
    /// @param[in] passthrough
    ///     The ASTConsumer that the returned transformer should send
    ///     the ASTs to after transformation.
    //------------------------------------------------------------------
    clang::ASTConsumer *
    ASTTransformer(clang::ASTConsumer *passthrough) override;

    void CommitPersistentDecls() override;

  private:
    Target &m_target;
    std::unique_ptr<ClangExpressionDeclMap> m_expr_decl_map_up;
    std::unique_ptr<ASTStructExtractor> m_struct_extractor_up; ///< The class
                                                               ///that generates
                                                               ///the argument
                                                               ///struct layout.
    std::unique_ptr<ASTResultSynthesizer> m_result_synthesizer_up;
    bool m_top_level;
  };

  //------------------------------------------------------------------
  /// Constructor
  ///
  /// @param[in] expr
  ///     The expression to parse.
  ///
  /// @param[in] expr_prefix
  ///     If non-NULL, a C string containing translation-unit level
  ///     definitions to be included when the expression is parsed.
  ///
  /// @param[in] language
  ///     If not eLanguageTypeUnknown, a language to use when parsing
  ///     the expression.  Currently restricted to those languages
  ///     supported by Clang.
  ///
  /// @param[in] desired_type
  ///     If not eResultTypeAny, the type to use for the expression
  ///     result.
  //------------------------------------------------------------------
  ClangUserExpression(ExecutionContextScope &exe_scope, llvm::StringRef expr,
                      llvm::StringRef prefix, lldb::LanguageType language,
                      ResultType desired_type,
                      const EvaluateExpressionOptions &options);

  ~ClangUserExpression() override;

  //------------------------------------------------------------------
  /// Parse the expression
  ///
  /// @param[in] diagnostic_manager
  ///     A diagnostic manager to report parse errors and warnings to.
  ///
  /// @param[in] exe_ctx
  ///     The execution context to use when looking up entities that
  ///     are needed for parsing (locations of functions, types of
  ///     variables, persistent variables, etc.)
  ///
  /// @param[in] execution_policy
  ///     Determines whether interpretation is possible or mandatory.
  ///
  /// @param[in] keep_result_in_memory
  ///     True if the resulting persistent variable should reside in
  ///     target memory, if applicable.
  ///
  /// @return
  ///     True on success (no errors); false otherwise.
  //------------------------------------------------------------------
  bool Parse(DiagnosticManager &diagnostic_manager, ExecutionContext &exe_ctx,
             lldb_private::ExecutionPolicy execution_policy,
             bool keep_result_in_memory, bool generate_debug_info) override;

  bool Complete(ExecutionContext &exe_ctx, CompletionRequest &request,
                unsigned complete_pos) override;

  ExpressionTypeSystemHelper *GetTypeSystemHelper() override {
    return &m_type_system_helper;
  }

  ClangExpressionDeclMap *DeclMap() { return m_type_system_helper.DeclMap(); }

  void ResetDeclMap() { m_type_system_helper.ResetDeclMap(); }

  void ResetDeclMap(ExecutionContext &exe_ctx,
                    Materializer::PersistentVariableDelegate &result_delegate,
                    bool keep_result_in_memory) {
    m_type_system_helper.ResetDeclMap(exe_ctx, result_delegate,
                                      keep_result_in_memory);
  }

  lldb::ExpressionVariableSP
  GetResultAfterDematerialization(ExecutionContextScope *exe_scope) override;

private:
  //------------------------------------------------------------------
  /// Populate m_in_cplusplus_method and m_in_objectivec_method based on the
  /// environment.
  //------------------------------------------------------------------

  void ScanContext(ExecutionContext &exe_ctx,
                   lldb_private::Status &err) override;

  bool AddArguments(ExecutionContext &exe_ctx, std::vector<lldb::addr_t> &args,
                    lldb::addr_t struct_address,
                    DiagnosticManager &diagnostic_manager) override;

  void UpdateLanguageForExpr(DiagnosticManager &diagnostic_manager,
                             ExecutionContext &exe_ctx);
  bool SetupPersistentState(DiagnosticManager &diagnostic_manager,
                                   ExecutionContext &exe_ctx);
  bool PrepareForParsing(DiagnosticManager &diagnostic_manager,
                         ExecutionContext &exe_ctx);

  ClangUserExpressionHelper m_type_system_helper;

  class ResultDelegate : public Materializer::PersistentVariableDelegate {
  public:
    ResultDelegate(lldb::TargetSP target) : m_target_sp(target) {}
    ConstString GetName() override;
    void DidDematerialize(lldb::ExpressionVariableSP &variable) override;

    void RegisterPersistentState(PersistentExpressionState *persistent_state);
    lldb::ExpressionVariableSP &GetVariable();

  private:
    PersistentExpressionState *m_persistent_state;
    lldb::ExpressionVariableSP m_variable;
    lldb::TargetSP m_target_sp;
  };

  /// The language type of the current expression.
  lldb::LanguageType m_expr_lang = lldb::eLanguageTypeUnknown;

  /// The absolute character position in the transformed source code where the
  /// user code (as typed by the user) starts. If the variable is empty, then we
  /// were not able to calculate this position.
  llvm::Optional<size_t> m_user_expression_start_pos;
  ResultDelegate m_result_delegate;
};

} // namespace lldb_private

#endif // liblldb_ClangUserExpression_h_
