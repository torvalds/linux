//===-- DomPrinter.h - Dom printer external interface ------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines external functions that can be called to explicitly
// instantiate the dominance tree printer.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_DOMPRINTER_H
#define LLVM_ANALYSIS_DOMPRINTER_H

namespace llvm {
  class FunctionPass;
  FunctionPass *createDomPrinterPass();
  FunctionPass *createDomOnlyPrinterPass();
  FunctionPass *createDomViewerPass();
  FunctionPass *createDomOnlyViewerPass();
  FunctionPass *createPostDomPrinterPass();
  FunctionPass *createPostDomOnlyPrinterPass();
  FunctionPass *createPostDomViewerPass();
  FunctionPass *createPostDomOnlyViewerPass();
} // End llvm namespace

#endif
