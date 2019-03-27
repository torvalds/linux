//===--- ASTConsumers.h - ASTConsumer implementations -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// AST Consumers.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FRONTEND_ASTCONSUMERS_H
#define LLVM_CLANG_FRONTEND_ASTCONSUMERS_H

#include "clang/Basic/LLVM.h"
#include <memory>

namespace clang {

class ASTConsumer;
class CodeGenOptions;
class DiagnosticsEngine;
class FileManager;
class LangOptions;
class Preprocessor;
class TargetOptions;

// AST pretty-printer: prints out the AST in a format that is close to the
// original C code.  The output is intended to be in a format such that
// clang could re-parse the output back into the same AST, but the
// implementation is still incomplete.
std::unique_ptr<ASTConsumer> CreateASTPrinter(std::unique_ptr<raw_ostream> OS,
                                              StringRef FilterString);

// AST dumper: dumps the raw AST in human-readable form to the given output
// stream, or stdout if OS is nullptr.
std::unique_ptr<ASTConsumer> CreateASTDumper(std::unique_ptr<raw_ostream> OS,
                                             StringRef FilterString,
                                             bool DumpDecls, bool Deserialize,
                                             bool DumpLookups);

// AST Decl node lister: prints qualified names of all filterable AST Decl
// nodes.
std::unique_ptr<ASTConsumer> CreateASTDeclNodeLister();

// Graphical AST viewer: for each function definition, creates a graph of
// the AST and displays it with the graph viewer "dotty".  Also outputs
// function declarations to stderr.
std::unique_ptr<ASTConsumer> CreateASTViewer();

} // end clang namespace

#endif
