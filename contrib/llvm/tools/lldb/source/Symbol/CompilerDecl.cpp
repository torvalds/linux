//===-- CompilerDecl.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/CompilerDecl.h"
#include "lldb/Symbol/CompilerDeclContext.h"
#include "lldb/Symbol/TypeSystem.h"

using namespace lldb_private;

bool CompilerDecl::IsClang() const {
  return IsValid() && m_type_system->getKind() == TypeSystem::eKindClang;
}

ConstString CompilerDecl::GetName() const {
  return m_type_system->DeclGetName(m_opaque_decl);
}

ConstString CompilerDecl::GetMangledName() const {
  return m_type_system->DeclGetMangledName(m_opaque_decl);
}

CompilerDeclContext CompilerDecl::GetDeclContext() const {
  return m_type_system->DeclGetDeclContext(m_opaque_decl);
}

CompilerType CompilerDecl::GetFunctionReturnType() const {
  return m_type_system->DeclGetFunctionReturnType(m_opaque_decl);
}

size_t CompilerDecl::GetNumFunctionArguments() const {
  return m_type_system->DeclGetFunctionNumArguments(m_opaque_decl);
}

CompilerType CompilerDecl::GetFunctionArgumentType(size_t arg_idx) const {
  return m_type_system->DeclGetFunctionArgumentType(m_opaque_decl, arg_idx);
}

bool lldb_private::operator==(const lldb_private::CompilerDecl &lhs,
                              const lldb_private::CompilerDecl &rhs) {
  return lhs.GetTypeSystem() == rhs.GetTypeSystem() &&
         lhs.GetOpaqueDecl() == rhs.GetOpaqueDecl();
}

bool lldb_private::operator!=(const lldb_private::CompilerDecl &lhs,
                              const lldb_private::CompilerDecl &rhs) {
  return lhs.GetTypeSystem() != rhs.GetTypeSystem() ||
         lhs.GetOpaqueDecl() != rhs.GetOpaqueDecl();
}
