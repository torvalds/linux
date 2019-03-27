//===- llvm/Support/DiagnosticPrinter.h - Diagnostic Printer ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the main interface for printer backend diagnostic.
//
// Clients of the backend diagnostics should overload this interface based
// on their needs.
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_DIAGNOSTICPRINTER_H
#define LLVM_IR_DIAGNOSTICPRINTER_H

#include <string>

namespace llvm {

// Forward declarations.
class Module;
class raw_ostream;
class SMDiagnostic;
class StringRef;
class Twine;
class Value;

/// Interface for custom diagnostic printing.
class DiagnosticPrinter {
public:
  virtual ~DiagnosticPrinter() = default;

  // Simple types.
  virtual DiagnosticPrinter &operator<<(char C) = 0;
  virtual DiagnosticPrinter &operator<<(unsigned char C) = 0;
  virtual DiagnosticPrinter &operator<<(signed char C) = 0;
  virtual DiagnosticPrinter &operator<<(StringRef Str) = 0;
  virtual DiagnosticPrinter &operator<<(const char *Str) = 0;
  virtual DiagnosticPrinter &operator<<(const std::string &Str) = 0;
  virtual DiagnosticPrinter &operator<<(unsigned long N) = 0;
  virtual DiagnosticPrinter &operator<<(long N) = 0;
  virtual DiagnosticPrinter &operator<<(unsigned long long N) = 0;
  virtual DiagnosticPrinter &operator<<(long long N) = 0;
  virtual DiagnosticPrinter &operator<<(const void *P) = 0;
  virtual DiagnosticPrinter &operator<<(unsigned int N) = 0;
  virtual DiagnosticPrinter &operator<<(int N) = 0;
  virtual DiagnosticPrinter &operator<<(double N) = 0;
  virtual DiagnosticPrinter &operator<<(const Twine &Str) = 0;

  // IR related types.
  virtual DiagnosticPrinter &operator<<(const Value &V) = 0;
  virtual DiagnosticPrinter &operator<<(const Module &M) = 0;

  // Other types.
  virtual DiagnosticPrinter &operator<<(const SMDiagnostic &Diag) = 0;
};

/// Basic diagnostic printer that uses an underlying raw_ostream.
class DiagnosticPrinterRawOStream : public DiagnosticPrinter {
protected:
  raw_ostream &Stream;

public:
  DiagnosticPrinterRawOStream(raw_ostream &Stream) : Stream(Stream) {}

  // Simple types.
  DiagnosticPrinter &operator<<(char C) override;
  DiagnosticPrinter &operator<<(unsigned char C) override;
  DiagnosticPrinter &operator<<(signed char C) override;
  DiagnosticPrinter &operator<<(StringRef Str) override;
  DiagnosticPrinter &operator<<(const char *Str) override;
  DiagnosticPrinter &operator<<(const std::string &Str) override;
  DiagnosticPrinter &operator<<(unsigned long N) override;
  DiagnosticPrinter &operator<<(long N) override;
  DiagnosticPrinter &operator<<(unsigned long long N) override;
  DiagnosticPrinter &operator<<(long long N) override;
  DiagnosticPrinter &operator<<(const void *P) override;
  DiagnosticPrinter &operator<<(unsigned int N) override;
  DiagnosticPrinter &operator<<(int N) override;
  DiagnosticPrinter &operator<<(double N) override;
  DiagnosticPrinter &operator<<(const Twine &Str) override;

  // IR related types.
  DiagnosticPrinter &operator<<(const Value &V) override;
  DiagnosticPrinter &operator<<(const Module &M) override;

  // Other types.
  DiagnosticPrinter &operator<<(const SMDiagnostic &Diag) override;
};

} // end namespace llvm

#endif // LLVM_IR_DIAGNOSTICPRINTER_H
