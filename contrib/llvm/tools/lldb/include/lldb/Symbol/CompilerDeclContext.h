//===-- CompilerDeclContext.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_CompilerDeclContext_h_
#define liblldb_CompilerDeclContext_h_

#include <vector>

#include "lldb/Utility/ConstString.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

class CompilerDeclContext {
public:
  //----------------------------------------------------------------------
  // Constructors and Destructors
  //----------------------------------------------------------------------
  CompilerDeclContext() : m_type_system(nullptr), m_opaque_decl_ctx(nullptr) {}

  CompilerDeclContext(TypeSystem *type_system, void *decl_ctx)
      : m_type_system(type_system), m_opaque_decl_ctx(decl_ctx) {}

  ~CompilerDeclContext() {}

  //----------------------------------------------------------------------
  // Tests
  //----------------------------------------------------------------------

  explicit operator bool() const { return IsValid(); }

  bool operator<(const CompilerDeclContext &rhs) const {
    if (m_type_system == rhs.m_type_system)
      return m_opaque_decl_ctx < rhs.m_opaque_decl_ctx;
    return m_type_system < rhs.m_type_system;
  }

  bool IsValid() const {
    return m_type_system != nullptr && m_opaque_decl_ctx != nullptr;
  }

  bool IsClang() const;

  std::vector<CompilerDecl> FindDeclByName(ConstString name,
                                           const bool ignore_using_decls);

  //----------------------------------------------------------------------
  /// Checks if this decl context represents a method of a class.
  ///
  /// @param[out] language_ptr
  ///     If non NULL and \b true is returned from this function,
  ///     this will indicate if the language that respresents the method.
  ///
  /// @param[out] is_instance_method_ptr
  ///     If non NULL and \b true is returned from this function,
  ///     this will indicate if the method is an instance function (true)
  ///     or a class method (false indicating the function is static, or
  ///     doesn't require an instance of the class to be called).
  ///
  /// @param[out] language_object_name_ptr
  ///     If non NULL and \b true is returned from this function,
  ///     this will indicate if implicit object name for the language
  ///     like "this" for C++, and "self" for Objective C.
  ///
  /// @return
  ///     Returns true if this is a decl context that represents a method
  ///     in a struct, union or class.
  //----------------------------------------------------------------------
  bool IsClassMethod(lldb::LanguageType *language_ptr,
                     bool *is_instance_method_ptr,
                     ConstString *language_object_name_ptr);

  //----------------------------------------------------------------------
  // Accessors
  //----------------------------------------------------------------------

  TypeSystem *GetTypeSystem() const { return m_type_system; }

  void *GetOpaqueDeclContext() const { return m_opaque_decl_ctx; }

  void SetDeclContext(TypeSystem *type_system, void *decl_ctx) {
    m_type_system = type_system;
    m_opaque_decl_ctx = decl_ctx;
  }

  void Clear() {
    m_type_system = nullptr;
    m_opaque_decl_ctx = nullptr;
  }

  ConstString GetName() const;

  ConstString GetScopeQualifiedName() const;

  bool IsStructUnionOrClass() const;

private:
  TypeSystem *m_type_system;
  void *m_opaque_decl_ctx;
};

bool operator==(const CompilerDeclContext &lhs, const CompilerDeclContext &rhs);
bool operator!=(const CompilerDeclContext &lhs, const CompilerDeclContext &rhs);

} // namespace lldb_private

#endif // #ifndef liblldb_CompilerDeclContext_h_
