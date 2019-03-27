//===-- ASTStructExtractor.h ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ASTStructExtractor_h_
#define liblldb_ASTStructExtractor_h_

#include "ClangExpressionVariable.h"
#include "ClangFunctionCaller.h"

#include "lldb/Core/ClangForward.h"
#include "clang/Sema/SemaConsumer.h"

namespace lldb_private {

//----------------------------------------------------------------------
/// @class ASTStructExtractor ASTStructExtractor.h
/// "lldb/Expression/ASTStructExtractor.h" Extracts and describes the argument
/// structure for a wrapped function.
///
/// This pass integrates with ClangFunctionCaller, which calls functions with
/// custom sets of arguments.  To avoid having to implement the full calling
/// convention for the target's architecture, ClangFunctionCaller writes a
/// simple wrapper function that takes a pointer to an argument structure that
/// contains room for the address of the function to be called, the values of
/// all its arguments, and room for the function's return value.
///
/// The definition of this struct is itself in the body of the wrapper
/// function, so Clang does the structure layout itself.  ASTStructExtractor
/// reads through the AST for the wrapper function and finds the struct.
//----------------------------------------------------------------------
class ASTStructExtractor : public clang::SemaConsumer {
public:
  //----------------------------------------------------------------------
  /// Constructor
  ///
  /// @param[in] passthrough
  ///     Since the ASTs must typically go through to the Clang code generator
  ///     in order to produce LLVM IR, this SemaConsumer must allow them to
  ///     pass to the next step in the chain after processing.  Passthrough is
  ///     the next ASTConsumer, or NULL if none is required.
  ///
  /// @param[in] struct_name
  ///     The name of the structure to extract from the wrapper function.
  ///
  /// @param[in] function
  ///     The caller object whose members should be populated with information
  ///     about the argument struct.  ClangFunctionCaller friends
  ///     ASTStructExtractor
  ///     for this purpose.
  //----------------------------------------------------------------------
  ASTStructExtractor(clang::ASTConsumer *passthrough, const char *struct_name,
                     ClangFunctionCaller &function);

  //----------------------------------------------------------------------
  /// Destructor
  //----------------------------------------------------------------------
  ~ASTStructExtractor() override;

  //----------------------------------------------------------------------
  /// Link this consumer with a particular AST context
  ///
  /// @param[in] Context
  ///     This AST context will be used for types and identifiers, and also
  ///     forwarded to the passthrough consumer, if one exists.
  //----------------------------------------------------------------------
  void Initialize(clang::ASTContext &Context) override;

  //----------------------------------------------------------------------
  /// Examine a list of Decls to find the function $__lldb_expr and transform
  /// its code
  ///
  /// @param[in] D
  ///     The list of Decls to search.  These may contain LinkageSpecDecls,
  ///     which need to be searched recursively.  That job falls to
  ///     TransformTopLevelDecl.
  //----------------------------------------------------------------------
  bool HandleTopLevelDecl(clang::DeclGroupRef D) override;

  //----------------------------------------------------------------------
  /// Passthrough stub
  //----------------------------------------------------------------------
  void HandleTranslationUnit(clang::ASTContext &Ctx) override;

  //----------------------------------------------------------------------
  /// Passthrough stub
  //----------------------------------------------------------------------
  void HandleTagDeclDefinition(clang::TagDecl *D) override;

  //----------------------------------------------------------------------
  /// Passthrough stub
  //----------------------------------------------------------------------
  void CompleteTentativeDefinition(clang::VarDecl *D) override;

  //----------------------------------------------------------------------
  /// Passthrough stub
  //----------------------------------------------------------------------
  void HandleVTable(clang::CXXRecordDecl *RD) override;

  //----------------------------------------------------------------------
  /// Passthrough stub
  //----------------------------------------------------------------------
  void PrintStats() override;

  //----------------------------------------------------------------------
  /// Set the Sema object to use when performing transforms, and pass it on
  ///
  /// @param[in] S
  ///     The Sema to use.  Because Sema isn't externally visible, this class
  ///     casts it to an Action for actual use.
  //----------------------------------------------------------------------
  void InitializeSema(clang::Sema &S) override;

  //----------------------------------------------------------------------
  /// Reset the Sema to NULL now that transformations are done
  //----------------------------------------------------------------------
  void ForgetSema() override;

private:
  //----------------------------------------------------------------------
  /// Hunt the given FunctionDecl for the argument struct and place
  /// information about it into m_function
  ///
  /// @param[in] F
  ///     The FunctionDecl to hunt.
  //----------------------------------------------------------------------
  void ExtractFromFunctionDecl(clang::FunctionDecl *F);

  //----------------------------------------------------------------------
  /// Hunt the given Decl for FunctionDecls named the same as the wrapper
  /// function name, recursing as necessary through LinkageSpecDecls, and
  /// calling ExtractFromFunctionDecl on anything that was found
  ///
  /// @param[in] D
  ///     The Decl to hunt.
  //----------------------------------------------------------------------
  void ExtractFromTopLevelDecl(clang::Decl *D);

  clang::ASTContext
      *m_ast_context; ///< The AST context to use for identifiers and types.
  clang::ASTConsumer *m_passthrough; ///< The ASTConsumer down the chain, for
                                     ///passthrough.  NULL if it's a
                                     ///SemaConsumer.
  clang::SemaConsumer *m_passthrough_sema; ///< The SemaConsumer down the chain,
                                           ///for passthrough.  NULL if it's an
                                           ///ASTConsumer.
  clang::Sema *m_sema;                     ///< The Sema to use.
  clang::Action
      *m_action; ///< The Sema to use, cast to an Action so it's usable.

  ClangFunctionCaller &m_function; ///< The function to populate with
                                   ///information about the argument structure.
  std::string m_struct_name;       ///< The name of the structure to extract.
};

} // namespace lldb_private

#endif // liblldb_ASTStructExtractor_h_
