//===--- Serialization/PCHContainerOperations.h - PCH Containers --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SERIALIZATION_PCHCONTAINEROPERATIONS_H
#define LLVM_CLANG_SERIALIZATION_PCHCONTAINEROPERATIONS_H

#include "clang/Basic/Module.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/MemoryBuffer.h"
#include <memory>

namespace llvm {
class raw_pwrite_stream;
}

namespace clang {

class ASTConsumer;
class CodeGenOptions;
class DiagnosticsEngine;
class CompilerInstance;

struct PCHBuffer {
  ASTFileSignature Signature;
  llvm::SmallVector<char, 0> Data;
  bool IsComplete;
};

/// This abstract interface provides operations for creating
/// containers for serialized ASTs (precompiled headers and clang
/// modules).
class PCHContainerWriter {
public:
  virtual ~PCHContainerWriter() = 0;
  virtual llvm::StringRef getFormat() const = 0;

  /// Return an ASTConsumer that can be chained with a
  /// PCHGenerator that produces a wrapper file format containing a
  /// serialized AST bitstream.
  virtual std::unique_ptr<ASTConsumer>
  CreatePCHContainerGenerator(CompilerInstance &CI,
                              const std::string &MainFileName,
                              const std::string &OutputFileName,
                              std::unique_ptr<llvm::raw_pwrite_stream> OS,
                              std::shared_ptr<PCHBuffer> Buffer) const = 0;
};

/// This abstract interface provides operations for unwrapping
/// containers for serialized ASTs (precompiled headers and clang
/// modules).
class PCHContainerReader {
public:
  virtual ~PCHContainerReader() = 0;
  /// Equivalent to the format passed to -fmodule-format=
  virtual llvm::StringRef getFormat() const = 0;

  /// Returns the serialized AST inside the PCH container Buffer.
  virtual llvm::StringRef ExtractPCH(llvm::MemoryBufferRef Buffer) const = 0;
};

/// Implements write operations for a raw pass-through PCH container.
class RawPCHContainerWriter : public PCHContainerWriter {
  llvm::StringRef getFormat() const override { return "raw"; }

  /// Return an ASTConsumer that can be chained with a
  /// PCHGenerator that writes the module to a flat file.
  std::unique_ptr<ASTConsumer>
  CreatePCHContainerGenerator(CompilerInstance &CI,
                              const std::string &MainFileName,
                              const std::string &OutputFileName,
                              std::unique_ptr<llvm::raw_pwrite_stream> OS,
                              std::shared_ptr<PCHBuffer> Buffer) const override;
};

/// Implements read operations for a raw pass-through PCH container.
class RawPCHContainerReader : public PCHContainerReader {
  llvm::StringRef getFormat() const override { return "raw"; }

  /// Simply returns the buffer contained in Buffer.
  llvm::StringRef ExtractPCH(llvm::MemoryBufferRef Buffer) const override;
};

/// A registry of PCHContainerWriter and -Reader objects for different formats.
class PCHContainerOperations {
  llvm::StringMap<std::unique_ptr<PCHContainerWriter>> Writers;
  llvm::StringMap<std::unique_ptr<PCHContainerReader>> Readers;
public:
  /// Automatically registers a RawPCHContainerWriter and
  /// RawPCHContainerReader.
  PCHContainerOperations();
  void registerWriter(std::unique_ptr<PCHContainerWriter> Writer) {
    Writers[Writer->getFormat()] = std::move(Writer);
  }
  void registerReader(std::unique_ptr<PCHContainerReader> Reader) {
    Readers[Reader->getFormat()] = std::move(Reader);
  }
  const PCHContainerWriter *getWriterOrNull(llvm::StringRef Format) {
    return Writers[Format].get();
  }
  const PCHContainerReader *getReaderOrNull(llvm::StringRef Format) {
    return Readers[Format].get();
  }
  const PCHContainerReader &getRawReader() {
    return *getReaderOrNull("raw");
  }
};

}

#endif
