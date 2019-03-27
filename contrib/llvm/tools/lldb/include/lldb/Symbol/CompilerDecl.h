//===-- CompilerDecl.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_CompilerDecl_h_
#define liblldb_CompilerDecl_h_

#include "lldb/Symbol/CompilerType.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

class CompilerDecl {
public:
  //----------------------------------------------------------------------
  // Constructors and Destructors
  //----------------------------------------------------------------------
  CompilerDecl() : m_type_system(nullptr), m_opaque_decl(nullptr) {}

  CompilerDecl(TypeSystem *type_system, void *decl)
      : m_type_system(type_system), m_opaque_decl(decl) {}

  ~CompilerDecl() {}

  //----------------------------------------------------------------------
  // Tests
  //----------------------------------------------------------------------

  explicit operator bool() const { return IsValid(); }

  bool operator<(const CompilerDecl &rhs) const {
    if (m_type_system == rhs.m_type_system)
      return m_opaque_decl < rhs.m_opaque_decl;
    return m_type_system < rhs.m_type_system;
  }

  bool IsValid() const {
    return m_type_system != nullptr && m_opaque_decl != nullptr;
  }

  bool IsClang() const;

  //----------------------------------------------------------------------
  // Accessors
  //----------------------------------------------------------------------

  TypeSystem *GetTypeSystem() const { return m_type_system; }

  void *GetOpaqueDecl() const { return m_opaque_decl; }

  void SetDecl(TypeSystem *type_system, void *decl) {
    m_type_system = type_system;
    m_opaque_decl = decl;
  }

  void Clear() {
    m_type_system = nullptr;
    m_opaque_decl = nullptr;
  }

  ConstString GetName() const;

  ConstString GetMangledName() const;

  CompilerDeclContext GetDeclContext() const;

  // If this decl represents a function, return the return type
  CompilerType GetFunctionReturnType() const;

  // If this decl represents a function, return the number of arguments for the
  // function
  size_t GetNumFunctionArguments() const;

  // If this decl represents a function, return the argument type given a zero
  // based argument index
  CompilerType GetFunctionArgumentType(size_t arg_idx) const;

private:
  TypeSystem *m_type_system;
  void *m_opaque_decl;
};

bool operator==(const CompilerDecl &lhs, const CompilerDecl &rhs);
bool operator!=(const CompilerDecl &lhs, const CompilerDecl &rhs);

} // namespace lldb_private

#endif // #ifndef liblldb_CompilerDecl_h_
