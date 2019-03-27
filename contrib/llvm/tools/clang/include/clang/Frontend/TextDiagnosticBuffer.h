//===- TextDiagnosticBuffer.h - Buffer Text Diagnostics ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is a concrete diagnostic client, which buffers the diagnostic messages.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FRONTEND_TEXTDIAGNOSTICBUFFER_H
#define LLVM_CLANG_FRONTEND_TEXTDIAGNOSTICBUFFER_H

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/SourceLocation.h"
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace clang {

class TextDiagnosticBuffer : public DiagnosticConsumer {
public:
  using DiagList = std::vector<std::pair<SourceLocation, std::string>>;
  using iterator = DiagList::iterator;
  using const_iterator = DiagList::const_iterator;

private:
  DiagList Errors, Warnings, Remarks, Notes;

  /// All - All diagnostics in the order in which they were generated.  That
  /// order likely doesn't correspond to user input order, but it at least
  /// keeps notes in the right places.  Each pair in the vector is a diagnostic
  /// level and an index into the corresponding DiagList above.
  std::vector<std::pair<DiagnosticsEngine::Level, size_t>> All;

public:
  const_iterator err_begin() const { return Errors.begin(); }
  const_iterator err_end() const { return Errors.end(); }

  const_iterator warn_begin() const { return Warnings.begin(); }
  const_iterator warn_end() const { return Warnings.end(); }

  const_iterator remark_begin() const { return Remarks.begin(); }
  const_iterator remark_end() const { return Remarks.end(); }

  const_iterator note_begin() const { return Notes.begin(); }
  const_iterator note_end() const { return Notes.end(); }

  void HandleDiagnostic(DiagnosticsEngine::Level DiagLevel,
                        const Diagnostic &Info) override;

  /// FlushDiagnostics - Flush the buffered diagnostics to an given
  /// diagnostic engine.
  void FlushDiagnostics(DiagnosticsEngine &Diags) const;
};

} // namespace clang

#endif // LLVM_CLANG_FRONTEND_TEXTDIAGNOSTICBUFFER_H
