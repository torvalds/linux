//===- FrontendPluginRegistry.h ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Pluggable Frontend Action Interface
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FRONTEND_FRONTENDPLUGINREGISTRY_H
#define LLVM_CLANG_FRONTEND_FRONTENDPLUGINREGISTRY_H

#include "clang/Frontend/FrontendAction.h"
#include "llvm/Support/Registry.h"

namespace clang {

/// The frontend plugin registry.
using FrontendPluginRegistry = llvm::Registry<PluginASTAction>;

} // namespace clang

#endif // LLVM_CLANG_FRONTEND_FRONTENDPLUGINREGISTRY_H
