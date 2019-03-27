//===-- ASTDumper.cpp -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ASTDumper.h"

#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Symbol/ClangUtil.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Utility/Log.h"

#include "llvm/Support/raw_ostream.h"

using namespace lldb_private;

ASTDumper::ASTDumper(clang::Decl *decl) {
  clang::DeclContext *decl_ctx = llvm::dyn_cast<clang::DeclContext>(decl);

  bool has_external_lexical_storage;
  bool has_external_visible_storage;

  if (decl_ctx) {
    has_external_lexical_storage = decl_ctx->hasExternalLexicalStorage();
    has_external_visible_storage = decl_ctx->hasExternalVisibleStorage();
    decl_ctx->setHasExternalLexicalStorage(false);
    decl_ctx->setHasExternalVisibleStorage(false);
  }

  llvm::raw_string_ostream os(m_dump);
  decl->print(os);
  os.flush();

  if (decl_ctx) {
    decl_ctx->setHasExternalLexicalStorage(has_external_lexical_storage);
    decl_ctx->setHasExternalVisibleStorage(has_external_visible_storage);
  }
}

ASTDumper::ASTDumper(clang::DeclContext *decl_ctx) {
  bool has_external_lexical_storage = decl_ctx->hasExternalLexicalStorage();
  bool has_external_visible_storage = decl_ctx->hasExternalVisibleStorage();

  decl_ctx->setHasExternalLexicalStorage(false);
  decl_ctx->setHasExternalVisibleStorage(false);

  if (clang::Decl *decl = llvm::dyn_cast<clang::Decl>(decl_ctx)) {
    llvm::raw_string_ostream os(m_dump);
    decl->print(os);
    os.flush();
  } else {
    m_dump.assign("<DeclContext is not a Decl>");
  }

  decl_ctx->setHasExternalLexicalStorage(has_external_lexical_storage);
  decl_ctx->setHasExternalVisibleStorage(has_external_visible_storage);
}

ASTDumper::ASTDumper(const clang::Type *type) {
  m_dump = clang::QualType(type, 0).getAsString();
}

ASTDumper::ASTDumper(clang::QualType type) { m_dump = type.getAsString(); }

ASTDumper::ASTDumper(lldb::opaque_compiler_type_t type) {
  m_dump = clang::QualType::getFromOpaquePtr(type).getAsString();
}

ASTDumper::ASTDumper(const CompilerType &compiler_type) {
  m_dump = ClangUtil::GetQualType(compiler_type).getAsString();
}

const char *ASTDumper::GetCString() { return m_dump.c_str(); }

void ASTDumper::ToSTDERR() { fprintf(stderr, "%s\n", m_dump.c_str()); }

void ASTDumper::ToLog(Log *log, const char *prefix) {
  size_t len = m_dump.length() + 1;

  char *alloc = (char *)malloc(len);
  char *str = alloc;

  memcpy(str, m_dump.c_str(), len);

  char *end = NULL;

  end = strchr(str, '\n');

  while (end) {
    *end = '\0';

    log->Printf("%s%s", prefix, str);

    *end = '\n';

    str = end + 1;
    end = strchr(str, '\n');
  }

  log->Printf("%s%s", prefix, str);

  free(alloc);
}

void ASTDumper::ToStream(lldb::StreamSP &stream) { stream->PutCString(m_dump); }
