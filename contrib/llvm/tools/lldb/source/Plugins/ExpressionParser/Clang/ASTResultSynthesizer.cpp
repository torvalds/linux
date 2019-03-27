//===-- ASTResultSynthesizer.cpp --------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ASTResultSynthesizer.h"

#include "ClangPersistentVariables.h"

#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Symbol/ClangASTImporter.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/Log.h"
#include "stdlib.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclGroup.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include "clang/Parse/Parser.h"
#include "clang/Sema/SemaDiagnostic.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace clang;
using namespace lldb_private;

ASTResultSynthesizer::ASTResultSynthesizer(ASTConsumer *passthrough,
                                           bool top_level, Target &target)
    : m_ast_context(NULL), m_passthrough(passthrough), m_passthrough_sema(NULL),
      m_target(target), m_sema(NULL), m_top_level(top_level) {
  if (!m_passthrough)
    return;

  m_passthrough_sema = dyn_cast<SemaConsumer>(passthrough);
}

ASTResultSynthesizer::~ASTResultSynthesizer() {}

void ASTResultSynthesizer::Initialize(ASTContext &Context) {
  m_ast_context = &Context;

  if (m_passthrough)
    m_passthrough->Initialize(Context);
}

void ASTResultSynthesizer::TransformTopLevelDecl(Decl *D) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  if (NamedDecl *named_decl = dyn_cast<NamedDecl>(D)) {
    if (log && log->GetVerbose()) {
      if (named_decl->getIdentifier())
        log->Printf("TransformTopLevelDecl(%s)",
                    named_decl->getIdentifier()->getNameStart());
      else if (ObjCMethodDecl *method_decl = dyn_cast<ObjCMethodDecl>(D))
        log->Printf("TransformTopLevelDecl(%s)",
                    method_decl->getSelector().getAsString().c_str());
      else
        log->Printf("TransformTopLevelDecl(<complex>)");
    }

    if (m_top_level) {
      RecordPersistentDecl(named_decl);
    }
  }

  if (LinkageSpecDecl *linkage_spec_decl = dyn_cast<LinkageSpecDecl>(D)) {
    RecordDecl::decl_iterator decl_iterator;

    for (decl_iterator = linkage_spec_decl->decls_begin();
         decl_iterator != linkage_spec_decl->decls_end(); ++decl_iterator) {
      TransformTopLevelDecl(*decl_iterator);
    }
  } else if (!m_top_level) {
    if (ObjCMethodDecl *method_decl = dyn_cast<ObjCMethodDecl>(D)) {
      if (m_ast_context &&
          !method_decl->getSelector().getAsString().compare("$__lldb_expr:")) {
        RecordPersistentTypes(method_decl);
        SynthesizeObjCMethodResult(method_decl);
      }
    } else if (FunctionDecl *function_decl = dyn_cast<FunctionDecl>(D)) {
      // When completing user input the body of the function may be a nullptr.
      if (m_ast_context && function_decl->hasBody() &&
          !function_decl->getNameInfo().getAsString().compare("$__lldb_expr")) {
        RecordPersistentTypes(function_decl);
        SynthesizeFunctionResult(function_decl);
      }
    }
  }
}

bool ASTResultSynthesizer::HandleTopLevelDecl(DeclGroupRef D) {
  DeclGroupRef::iterator decl_iterator;

  for (decl_iterator = D.begin(); decl_iterator != D.end(); ++decl_iterator) {
    Decl *decl = *decl_iterator;

    TransformTopLevelDecl(decl);
  }

  if (m_passthrough)
    return m_passthrough->HandleTopLevelDecl(D);
  return true;
}

bool ASTResultSynthesizer::SynthesizeFunctionResult(FunctionDecl *FunDecl) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  if (!m_sema)
    return false;

  FunctionDecl *function_decl = FunDecl;

  if (!function_decl)
    return false;

  if (log && log->GetVerbose()) {
    std::string s;
    raw_string_ostream os(s);

    function_decl->print(os);

    os.flush();

    log->Printf("Untransformed function AST:\n%s", s.c_str());
  }

  Stmt *function_body = function_decl->getBody();
  CompoundStmt *compound_stmt = dyn_cast<CompoundStmt>(function_body);

  bool ret = SynthesizeBodyResult(compound_stmt, function_decl);

  if (log && log->GetVerbose()) {
    std::string s;
    raw_string_ostream os(s);

    function_decl->print(os);

    os.flush();

    log->Printf("Transformed function AST:\n%s", s.c_str());
  }

  return ret;
}

bool ASTResultSynthesizer::SynthesizeObjCMethodResult(
    ObjCMethodDecl *MethodDecl) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  if (!m_sema)
    return false;

  if (!MethodDecl)
    return false;

  if (log && log->GetVerbose()) {
    std::string s;
    raw_string_ostream os(s);

    MethodDecl->print(os);

    os.flush();

    log->Printf("Untransformed method AST:\n%s", s.c_str());
  }

  Stmt *method_body = MethodDecl->getBody();

  if (!method_body)
    return false;

  CompoundStmt *compound_stmt = dyn_cast<CompoundStmt>(method_body);

  bool ret = SynthesizeBodyResult(compound_stmt, MethodDecl);

  if (log && log->GetVerbose()) {
    std::string s;
    raw_string_ostream os(s);

    MethodDecl->print(os);

    os.flush();

    log->Printf("Transformed method AST:\n%s", s.c_str());
  }

  return ret;
}

bool ASTResultSynthesizer::SynthesizeBodyResult(CompoundStmt *Body,
                                                DeclContext *DC) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  ASTContext &Ctx(*m_ast_context);

  if (!Body)
    return false;

  if (Body->body_empty())
    return false;

  Stmt **last_stmt_ptr = Body->body_end() - 1;
  Stmt *last_stmt = *last_stmt_ptr;

  while (dyn_cast<NullStmt>(last_stmt)) {
    if (last_stmt_ptr != Body->body_begin()) {
      last_stmt_ptr--;
      last_stmt = *last_stmt_ptr;
    } else {
      return false;
    }
  }

  Expr *last_expr = dyn_cast<Expr>(last_stmt);

  if (!last_expr)
    // No auxiliary variable necessary; expression returns void
    return true;

  // In C++11, last_expr can be a LValueToRvalue implicit cast.  Strip that off
  // if that's the case.

  do {
    ImplicitCastExpr *implicit_cast = dyn_cast<ImplicitCastExpr>(last_expr);

    if (!implicit_cast)
      break;

    if (implicit_cast->getCastKind() != CK_LValueToRValue)
      break;

    last_expr = implicit_cast->getSubExpr();
  } while (0);

  // is_lvalue is used to record whether the expression returns an assignable
  // Lvalue or an Rvalue.  This is relevant because they are handled
  // differently.
  //
  // For Lvalues
  //
  //   - In AST result synthesis (here!) the expression E is transformed into an
  //   initialization
  //     T *$__lldb_expr_result_ptr = &E.
  //
  //   - In structure allocation, a pointer-sized slot is allocated in the
  //   struct that is to be
  //     passed into the expression.
  //
  //   - In IR transformations, reads and writes to $__lldb_expr_result_ptr are
  //   redirected at
  //     an entry in the struct ($__lldb_arg) passed into the expression.
  //     (Other persistent
  //     variables are treated similarly, having been materialized as
  //     references, but in those
  //     cases the value of the reference itself is never modified.)
  //
  //   - During materialization, $0 (the result persistent variable) is ignored.
  //
  //   - During dematerialization, $0 is marked up as a load address with value
  //   equal to the
  //     contents of the structure entry.
  //
  // For Rvalues
  //
  //   - In AST result synthesis the expression E is transformed into an
  //   initialization
  //     static T $__lldb_expr_result = E.
  //
  //   - In structure allocation, a pointer-sized slot is allocated in the
  //   struct that is to be
  //     passed into the expression.
  //
  //   - In IR transformations, an instruction is inserted at the beginning of
  //   the function to
  //     dereference the pointer resident in the slot.  Reads and writes to
  //     $__lldb_expr_result
  //     are redirected at that dereferenced version.  Guard variables for the
  //     static variable
  //     are excised.
  //
  //   - During materialization, $0 (the result persistent variable) is
  //   populated with the location
  //     of a newly-allocated area of memory.
  //
  //   - During dematerialization, $0 is ignored.

  bool is_lvalue = last_expr->getValueKind() == VK_LValue &&
                   last_expr->getObjectKind() == OK_Ordinary;

  QualType expr_qual_type = last_expr->getType();
  const clang::Type *expr_type = expr_qual_type.getTypePtr();

  if (!expr_type)
    return false;

  if (expr_type->isVoidType())
    return true;

  if (log) {
    std::string s = expr_qual_type.getAsString();

    log->Printf("Last statement is an %s with type: %s",
                (is_lvalue ? "lvalue" : "rvalue"), s.c_str());
  }

  clang::VarDecl *result_decl = NULL;

  if (is_lvalue) {
    IdentifierInfo *result_ptr_id;

    if (expr_type->isFunctionType())
      result_ptr_id =
          &Ctx.Idents.get("$__lldb_expr_result"); // functions actually should
                                                  // be treated like function
                                                  // pointers
    else
      result_ptr_id = &Ctx.Idents.get("$__lldb_expr_result_ptr");

    m_sema->RequireCompleteType(SourceLocation(), expr_qual_type,
                                clang::diag::err_incomplete_type);

    QualType ptr_qual_type;

    if (expr_qual_type->getAs<ObjCObjectType>() != NULL)
      ptr_qual_type = Ctx.getObjCObjectPointerType(expr_qual_type);
    else
      ptr_qual_type = Ctx.getPointerType(expr_qual_type);

    result_decl =
        VarDecl::Create(Ctx, DC, SourceLocation(), SourceLocation(),
                        result_ptr_id, ptr_qual_type, NULL, SC_Static);

    if (!result_decl)
      return false;

    ExprResult address_of_expr =
        m_sema->CreateBuiltinUnaryOp(SourceLocation(), UO_AddrOf, last_expr);
    if (address_of_expr.get())
      m_sema->AddInitializerToDecl(result_decl, address_of_expr.get(), true);
    else
      return false;
  } else {
    IdentifierInfo &result_id = Ctx.Idents.get("$__lldb_expr_result");

    result_decl = VarDecl::Create(Ctx, DC, SourceLocation(), SourceLocation(),
                                  &result_id, expr_qual_type, NULL, SC_Static);

    if (!result_decl)
      return false;

    m_sema->AddInitializerToDecl(result_decl, last_expr, true);
  }

  DC->addDecl(result_decl);

  ///////////////////////////////
  // call AddInitializerToDecl
  //

  // m_sema->AddInitializerToDecl(result_decl, last_expr);

  /////////////////////////////////
  // call ConvertDeclToDeclGroup
  //

  Sema::DeclGroupPtrTy result_decl_group_ptr;

  result_decl_group_ptr = m_sema->ConvertDeclToDeclGroup(result_decl);

  ////////////////////////
  // call ActOnDeclStmt
  //

  StmtResult result_initialization_stmt_result(m_sema->ActOnDeclStmt(
      result_decl_group_ptr, SourceLocation(), SourceLocation()));

  ////////////////////////////////////////////////
  // replace the old statement with the new one
  //

  *last_stmt_ptr =
      reinterpret_cast<Stmt *>(result_initialization_stmt_result.get());

  return true;
}

void ASTResultSynthesizer::HandleTranslationUnit(ASTContext &Ctx) {
  if (m_passthrough)
    m_passthrough->HandleTranslationUnit(Ctx);
}

void ASTResultSynthesizer::RecordPersistentTypes(DeclContext *FunDeclCtx) {
  typedef DeclContext::specific_decl_iterator<TypeDecl> TypeDeclIterator;

  for (TypeDeclIterator i = TypeDeclIterator(FunDeclCtx->decls_begin()),
                        e = TypeDeclIterator(FunDeclCtx->decls_end());
       i != e; ++i) {
    MaybeRecordPersistentType(*i);
  }
}

void ASTResultSynthesizer::MaybeRecordPersistentType(TypeDecl *D) {
  if (!D->getIdentifier())
    return;

  StringRef name = D->getName();

  if (name.size() == 0 || name[0] != '$')
    return;

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  ConstString name_cs(name.str().c_str());

  if (log)
    log->Printf("Recording persistent type %s\n", name_cs.GetCString());

  m_decls.push_back(D);
}

void ASTResultSynthesizer::RecordPersistentDecl(NamedDecl *D) {
  lldbassert(m_top_level);

  if (!D->getIdentifier())
    return;

  StringRef name = D->getName();

  if (name.size() == 0)
    return;

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  ConstString name_cs(name.str().c_str());

  if (log)
    log->Printf("Recording persistent decl %s\n", name_cs.GetCString());

  m_decls.push_back(D);
}

void ASTResultSynthesizer::CommitPersistentDecls() {
  for (clang::NamedDecl *decl : m_decls) {
    StringRef name = decl->getName();
    ConstString name_cs(name.str().c_str());

    Decl *D_scratch = m_target.GetClangASTImporter()->DeportDecl(
        m_target.GetScratchClangASTContext()->getASTContext(), m_ast_context,
        decl);

    if (!D_scratch) {
      Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

      if (log) {
        std::string s;
        llvm::raw_string_ostream ss(s);
        decl->dump(ss);
        ss.flush();

        log->Printf("Couldn't commit persistent  decl: %s\n", s.c_str());
      }

      continue;
    }

    if (NamedDecl *NamedDecl_scratch = dyn_cast<NamedDecl>(D_scratch))
      llvm::cast<ClangPersistentVariables>(
          m_target.GetPersistentExpressionStateForLanguage(
              lldb::eLanguageTypeC))
          ->RegisterPersistentDecl(name_cs, NamedDecl_scratch);
  }
}

void ASTResultSynthesizer::HandleTagDeclDefinition(TagDecl *D) {
  if (m_passthrough)
    m_passthrough->HandleTagDeclDefinition(D);
}

void ASTResultSynthesizer::CompleteTentativeDefinition(VarDecl *D) {
  if (m_passthrough)
    m_passthrough->CompleteTentativeDefinition(D);
}

void ASTResultSynthesizer::HandleVTable(CXXRecordDecl *RD) {
  if (m_passthrough)
    m_passthrough->HandleVTable(RD);
}

void ASTResultSynthesizer::PrintStats() {
  if (m_passthrough)
    m_passthrough->PrintStats();
}

void ASTResultSynthesizer::InitializeSema(Sema &S) {
  m_sema = &S;

  if (m_passthrough_sema)
    m_passthrough_sema->InitializeSema(S);
}

void ASTResultSynthesizer::ForgetSema() {
  m_sema = NULL;

  if (m_passthrough_sema)
    m_passthrough_sema->ForgetSema();
}
