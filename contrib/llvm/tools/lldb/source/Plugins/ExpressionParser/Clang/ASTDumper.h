//===-- ASTDumper.h ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ASTDumper_h_
#define liblldb_ASTDumper_h_

#include "clang/AST/DeclVisitor.h"
#include "clang/AST/TypeVisitor.h"

#include "lldb/Utility/Stream.h"
#include "llvm/ADT/DenseSet.h"

namespace lldb_private {

class ASTDumper {
public:
  ASTDumper(clang::Decl *decl);
  ASTDumper(clang::DeclContext *decl_ctx);
  ASTDumper(const clang::Type *type);
  ASTDumper(clang::QualType type);
  ASTDumper(lldb::opaque_compiler_type_t type);
  ASTDumper(const CompilerType &compiler_type);

  const char *GetCString();
  void ToSTDERR();
  void ToLog(Log *log, const char *prefix);
  void ToStream(lldb::StreamSP &stream);

private:
  std::string m_dump;
};

} // namespace lldb_private

#endif
