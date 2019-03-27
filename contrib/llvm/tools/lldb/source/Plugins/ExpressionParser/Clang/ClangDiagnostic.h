//===-- ClangDiagnostic.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_ClangDiagnostic_h
#define lldb_ClangDiagnostic_h

#include <vector>

#include "clang/Basic/Diagnostic.h"

#include "lldb/lldb-defines.h"
#include "lldb/lldb-types.h"

#include "lldb/Expression/DiagnosticManager.h"

namespace lldb_private {

class ClangDiagnostic : public Diagnostic {
public:
  typedef std::vector<clang::FixItHint> FixItList;

  static inline bool classof(const ClangDiagnostic *) { return true; }
  static inline bool classof(const Diagnostic *diag) {
    return diag->getKind() == eDiagnosticOriginClang;
  }

  ClangDiagnostic(const char *message, DiagnosticSeverity severity,
                  uint32_t compiler_id)
      : Diagnostic(message, severity, eDiagnosticOriginClang, compiler_id) {}

  virtual ~ClangDiagnostic() = default;

  bool HasFixIts() const override { return !m_fixit_vec.empty(); }

  void AddFixitHint(const clang::FixItHint &fixit) {
    m_fixit_vec.push_back(fixit);
  }

  const FixItList &FixIts() const { return m_fixit_vec; }
  FixItList m_fixit_vec;
};

} // namespace lldb_private
#endif /* lldb_ClangDiagnostic_h */
