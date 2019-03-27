//===-- CompilerDeclContext.cpp ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/CompilerDeclContext.h"
#include "lldb/Symbol/CompilerDecl.h"
#include "lldb/Symbol/TypeSystem.h"
#include <vector>

using namespace lldb_private;

std::vector<CompilerDecl>
CompilerDeclContext::FindDeclByName(ConstString name,
                                    const bool ignore_using_decls) {
  if (IsValid())
    return m_type_system->DeclContextFindDeclByName(m_opaque_decl_ctx, name,
                                                    ignore_using_decls);
  else
    return std::vector<CompilerDecl>();
}

bool CompilerDeclContext::IsClang() const {
  return IsValid() && m_type_system->getKind() == TypeSystem::eKindClang;
}

ConstString CompilerDeclContext::GetName() const {
  if (IsValid())
    return m_type_system->DeclContextGetName(m_opaque_decl_ctx);
  else
    return ConstString();
}

ConstString CompilerDeclContext::GetScopeQualifiedName() const {
  if (IsValid())
    return m_type_system->DeclContextGetScopeQualifiedName(m_opaque_decl_ctx);
  else
    return ConstString();
}

bool CompilerDeclContext::IsStructUnionOrClass() const {
  if (IsValid())
    return m_type_system->DeclContextIsStructUnionOrClass(m_opaque_decl_ctx);
  else
    return false;
}

bool CompilerDeclContext::IsClassMethod(lldb::LanguageType *language_ptr,
                                        bool *is_instance_method_ptr,
                                        ConstString *language_object_name_ptr) {
  if (IsValid())
    return m_type_system->DeclContextIsClassMethod(
        m_opaque_decl_ctx, language_ptr, is_instance_method_ptr,
        language_object_name_ptr);
  else
    return false;
}

bool lldb_private::operator==(const lldb_private::CompilerDeclContext &lhs,
                              const lldb_private::CompilerDeclContext &rhs) {
  return lhs.GetTypeSystem() == rhs.GetTypeSystem() &&
         lhs.GetOpaqueDeclContext() == rhs.GetOpaqueDeclContext();
}

bool lldb_private::operator!=(const lldb_private::CompilerDeclContext &lhs,
                              const lldb_private::CompilerDeclContext &rhs) {
  return lhs.GetTypeSystem() != rhs.GetTypeSystem() ||
         lhs.GetOpaqueDeclContext() != rhs.GetOpaqueDeclContext();
}
