//===-- ModelConsumer.h -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements clang::ento::ModelConsumer which is an
/// ASTConsumer for model files.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_GR_MODELCONSUMER_H
#define LLVM_CLANG_GR_MODELCONSUMER_H

#include "clang/AST/ASTConsumer.h"
#include "llvm/ADT/StringMap.h"

namespace clang {

class Stmt;

namespace ento {

/// ASTConsumer to consume model files' AST.
///
/// This consumer collects the bodies of function definitions into a StringMap
/// from a model file.
class ModelConsumer : public ASTConsumer {
public:
  ModelConsumer(llvm::StringMap<Stmt *> &Bodies);

  bool HandleTopLevelDecl(DeclGroupRef D) override;

private:
  llvm::StringMap<Stmt *> &Bodies;
};
}
}

#endif
