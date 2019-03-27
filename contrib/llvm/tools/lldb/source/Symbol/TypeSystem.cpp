//===-- TypeSystem.cpp ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

//
//  TypeSystem.cpp
//  lldb
//
//  Created by Ryan Brown on 3/29/15.
//
//

#include "lldb/Symbol/TypeSystem.h"

#include <set>

#include "lldb/Core/PluginManager.h"
#include "lldb/Symbol/CompilerType.h"

using namespace lldb_private;
using namespace lldb;

TypeSystem::TypeSystem(LLVMCastKind kind) : m_kind(kind), m_sym_file(nullptr) {}

TypeSystem::~TypeSystem() {}

static lldb::TypeSystemSP CreateInstanceHelper(lldb::LanguageType language,
                                               Module *module, Target *target) {
  uint32_t i = 0;
  TypeSystemCreateInstance create_callback;
  while ((create_callback = PluginManager::GetTypeSystemCreateCallbackAtIndex(
              i++)) != nullptr) {
    lldb::TypeSystemSP type_system_sp =
        create_callback(language, module, target);
    if (type_system_sp)
      return type_system_sp;
  }

  return lldb::TypeSystemSP();
}

lldb::TypeSystemSP TypeSystem::CreateInstance(lldb::LanguageType language,
                                              Module *module) {
  return CreateInstanceHelper(language, module, nullptr);
}

lldb::TypeSystemSP TypeSystem::CreateInstance(lldb::LanguageType language,
                                              Target *target) {
  return CreateInstanceHelper(language, nullptr, target);
}

bool TypeSystem::IsAnonymousType(lldb::opaque_compiler_type_t type) {
  return false;
}

CompilerType TypeSystem::GetArrayType(lldb::opaque_compiler_type_t type,
                                      uint64_t size) {
  return CompilerType();
}

CompilerType
TypeSystem::GetLValueReferenceType(lldb::opaque_compiler_type_t type) {
  return CompilerType();
}

CompilerType
TypeSystem::GetRValueReferenceType(lldb::opaque_compiler_type_t type) {
  return CompilerType();
}

CompilerType TypeSystem::AddConstModifier(lldb::opaque_compiler_type_t type) {
  return CompilerType();
}

CompilerType
TypeSystem::AddVolatileModifier(lldb::opaque_compiler_type_t type) {
  return CompilerType();
}

CompilerType
TypeSystem::AddRestrictModifier(lldb::opaque_compiler_type_t type) {
  return CompilerType();
}

CompilerType TypeSystem::CreateTypedef(lldb::opaque_compiler_type_t type,
                                       const char *name,
                                       const CompilerDeclContext &decl_ctx) {
  return CompilerType();
}

CompilerType TypeSystem::GetBuiltinTypeByName(const ConstString &name) {
  return CompilerType();
}

CompilerType TypeSystem::GetTypeForFormatters(void *type) {
  return CompilerType(this, type);
}

size_t TypeSystem::GetNumTemplateArguments(lldb::opaque_compiler_type_t type) {
  return 0;
}

TemplateArgumentKind
TypeSystem::GetTemplateArgumentKind(opaque_compiler_type_t type, size_t idx) {
  return eTemplateArgumentKindNull;
}

CompilerType TypeSystem::GetTypeTemplateArgument(opaque_compiler_type_t type,
                                                 size_t idx) {
  return CompilerType();
}

llvm::Optional<CompilerType::IntegralTemplateArgument>
TypeSystem::GetIntegralTemplateArgument(opaque_compiler_type_t type,
                                        size_t idx) {
  return llvm::None;
}

LazyBool TypeSystem::ShouldPrintAsOneLiner(void *type, ValueObject *valobj) {
  return eLazyBoolCalculate;
}

bool TypeSystem::IsMeaninglessWithoutDynamicResolution(void *type) {
  return false;
}

ConstString TypeSystem::DeclGetMangledName(void *opaque_decl) {
  return ConstString();
}

CompilerDeclContext TypeSystem::DeclGetDeclContext(void *opaque_decl) {
  return CompilerDeclContext();
}

CompilerType TypeSystem::DeclGetFunctionReturnType(void *opaque_decl) {
  return CompilerType();
}

size_t TypeSystem::DeclGetFunctionNumArguments(void *opaque_decl) { return 0; }

CompilerType TypeSystem::DeclGetFunctionArgumentType(void *opaque_decl,
                                                     size_t arg_idx) {
  return CompilerType();
}

std::vector<CompilerDecl>
TypeSystem::DeclContextFindDeclByName(void *opaque_decl_ctx, ConstString name,
                                      bool ignore_imported_decls) {
  return std::vector<CompilerDecl>();
}

#pragma mark TypeSystemMap

TypeSystemMap::TypeSystemMap()
    : m_mutex(), m_map(), m_clear_in_progress(false) {}

TypeSystemMap::~TypeSystemMap() {}

void TypeSystemMap::Clear() {
  collection map;
  {
    std::lock_guard<std::mutex> guard(m_mutex);
    map = m_map;
    m_clear_in_progress = true;
  }
  std::set<TypeSystem *> visited;
  for (auto pair : map) {
    TypeSystem *type_system = pair.second.get();
    if (type_system && !visited.count(type_system)) {
      visited.insert(type_system);
      type_system->Finalize();
    }
  }
  map.clear();
  {
    std::lock_guard<std::mutex> guard(m_mutex);
    m_map.clear();
    m_clear_in_progress = false;
  }
}

void TypeSystemMap::ForEach(std::function<bool(TypeSystem *)> const &callback) {
  std::lock_guard<std::mutex> guard(m_mutex);
  // Use a std::set so we only call the callback once for each unique
  // TypeSystem instance
  std::set<TypeSystem *> visited;
  for (auto pair : m_map) {
    TypeSystem *type_system = pair.second.get();
    if (type_system && !visited.count(type_system)) {
      visited.insert(type_system);
      if (!callback(type_system))
        break;
    }
  }
}

TypeSystem *TypeSystemMap::GetTypeSystemForLanguage(lldb::LanguageType language,
                                                    Module *module,
                                                    bool can_create) {
  std::lock_guard<std::mutex> guard(m_mutex);
  collection::iterator pos = m_map.find(language);
  if (pos != m_map.end())
    return pos->second.get();

  for (const auto &pair : m_map) {
    if (pair.second && pair.second->SupportsLanguage(language)) {
      // Add a new mapping for "language" to point to an already existing
      // TypeSystem that supports this language
      AddToMap(language, pair.second);
      return pair.second.get();
    }
  }

  if (!can_create)
    return nullptr;

  // Cache even if we get a shared pointer that contains null type system back
  lldb::TypeSystemSP type_system_sp =
      TypeSystem::CreateInstance(language, module);
  AddToMap(language, type_system_sp);
  return type_system_sp.get();
}

TypeSystem *TypeSystemMap::GetTypeSystemForLanguage(lldb::LanguageType language,
                                                    Target *target,
                                                    bool can_create) {
  std::lock_guard<std::mutex> guard(m_mutex);
  collection::iterator pos = m_map.find(language);
  if (pos != m_map.end())
    return pos->second.get();

  for (const auto &pair : m_map) {
    if (pair.second && pair.second->SupportsLanguage(language)) {
      // Add a new mapping for "language" to point to an already existing
      // TypeSystem that supports this language

      AddToMap(language, pair.second);
      return pair.second.get();
    }
  }

  if (!can_create)
    return nullptr;

  // Cache even if we get a shared pointer that contains null type system back
  lldb::TypeSystemSP type_system_sp;
  if (!m_clear_in_progress)
    type_system_sp = TypeSystem::CreateInstance(language, target);

  AddToMap(language, type_system_sp);
  return type_system_sp.get();
}

void TypeSystemMap::AddToMap(lldb::LanguageType language,
                             lldb::TypeSystemSP const &type_system_sp) {
  if (!m_clear_in_progress)
    m_map[language] = type_system_sp;
}
