//===--- ModelConsumer.cpp - ASTConsumer for consuming model files --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements an ASTConsumer for consuming model files.
///
/// This ASTConsumer handles the AST of a parsed model file. All top level
/// function definitions will be collected from that model file for later
/// retrieval during the static analysis. The body of these functions will not
/// be injected into the ASTUnit of the analyzed translation unit. It will be
/// available through the BodyFarm which is utilized by the AnalysisDeclContext
/// class.
///
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Frontend/ModelConsumer.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclGroup.h"

using namespace clang;
using namespace ento;

ModelConsumer::ModelConsumer(llvm::StringMap<Stmt *> &Bodies)
    : Bodies(Bodies) {}

bool ModelConsumer::HandleTopLevelDecl(DeclGroupRef D) {
  for (DeclGroupRef::iterator I = D.begin(), E = D.end(); I != E; ++I) {

    // Only interested in definitions.
    const FunctionDecl *func = llvm::dyn_cast<FunctionDecl>(*I);
    if (func && func->hasBody()) {
      Bodies.insert(std::make_pair(func->getName(), func->getBody()));
    }
  }
  return true;
}
