//===-- DiagnosticManager.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_DiagnosticManager_h
#define lldb_DiagnosticManager_h

#include "lldb/lldb-defines.h"
#include "lldb/lldb-types.h"

#include "llvm/ADT/StringRef.h"

#include <string>
#include <vector>

namespace lldb_private {

enum DiagnosticOrigin {
  eDiagnosticOriginUnknown = 0,
  eDiagnosticOriginLLDB,
  eDiagnosticOriginClang,
  eDiagnosticOriginGo,
  eDiagnosticOriginSwift,
  eDiagnosticOriginLLVM
};

enum DiagnosticSeverity {
  eDiagnosticSeverityError,
  eDiagnosticSeverityWarning,
  eDiagnosticSeverityRemark
};

const uint32_t LLDB_INVALID_COMPILER_ID = UINT32_MAX;

class Diagnostic {
  friend class DiagnosticManager;

public:
  DiagnosticOrigin getKind() const { return m_origin; }

  static bool classof(const Diagnostic *diag) {
    DiagnosticOrigin kind = diag->getKind();
    switch (kind) {
    case eDiagnosticOriginUnknown:
    case eDiagnosticOriginLLDB:
    case eDiagnosticOriginGo:
    case eDiagnosticOriginLLVM:
      return true;
    case eDiagnosticOriginClang:
    case eDiagnosticOriginSwift:
      return false;
    }
  }

  Diagnostic(llvm::StringRef message, DiagnosticSeverity severity,
             DiagnosticOrigin origin, uint32_t compiler_id)
      : m_message(message), m_severity(severity), m_origin(origin),
        m_compiler_id(compiler_id) {}

  Diagnostic(const Diagnostic &rhs)
      : m_message(rhs.m_message), m_severity(rhs.m_severity),
        m_origin(rhs.m_origin), m_compiler_id(rhs.m_compiler_id) {}

  virtual ~Diagnostic() = default;

  virtual bool HasFixIts() const { return false; }

  DiagnosticSeverity GetSeverity() const { return m_severity; }

  uint32_t GetCompilerID() const { return m_compiler_id; }

  llvm::StringRef GetMessage() const { return m_message; }

  void AppendMessage(llvm::StringRef message,
                     bool precede_with_newline = true) {
    if (precede_with_newline)
      m_message.push_back('\n');
    m_message.append(message);
  }

protected:
  std::string m_message;
  DiagnosticSeverity m_severity;
  DiagnosticOrigin m_origin;
  uint32_t m_compiler_id; // Compiler-specific diagnostic ID
};

typedef std::vector<Diagnostic *> DiagnosticList;

class DiagnosticManager {
public:
  void Clear() {
    m_diagnostics.clear();
    m_fixed_expression.clear();
  }

  // The diagnostic manager holds a list of diagnostics, which are owned by the
  // manager.
  const DiagnosticList &Diagnostics() { return m_diagnostics; }

  ~DiagnosticManager() {
    for (Diagnostic *diag : m_diagnostics) {
      delete diag;
    }
  }

  bool HasFixIts() {
    for (Diagnostic *diag : m_diagnostics) {
      if (diag->HasFixIts())
        return true;
    }
    return false;
  }

  void AddDiagnostic(llvm::StringRef message, DiagnosticSeverity severity,
                     DiagnosticOrigin origin,
                     uint32_t compiler_id = LLDB_INVALID_COMPILER_ID) {
    m_diagnostics.push_back(
        new Diagnostic(message, severity, origin, compiler_id));
  }

  void AddDiagnostic(Diagnostic *diagnostic) {
    m_diagnostics.push_back(diagnostic);
  }

  void CopyDiagnostics(DiagnosticManager &otherDiagnostics);

  size_t Printf(DiagnosticSeverity severity, const char *format, ...)
      __attribute__((format(printf, 3, 4)));
  size_t PutString(DiagnosticSeverity severity, llvm::StringRef str);

  void AppendMessageToDiagnostic(llvm::StringRef str) {
    if (!m_diagnostics.empty()) {
      m_diagnostics.back()->AppendMessage(str);
    }
  }

  // Returns a string containing errors in this format:
  //
  // "error: error text\n
  // warning: warning text\n
  // remark text\n"
  std::string GetString(char separator = '\n');

  void Dump(Log *log);

  const std::string &GetFixedExpression() { return m_fixed_expression; }

  // Moves fixed_expression to the internal storage.
  void SetFixedExpression(std::string fixed_expression) {
    m_fixed_expression = std::move(fixed_expression);
    fixed_expression.clear();
  }

protected:
  DiagnosticList m_diagnostics;
  std::string m_fixed_expression;
};
}

#endif /* lldb_DiagnosticManager_h */
