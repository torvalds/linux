//===-- ModuleFileExtension.h - Module File Extensions ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SERIALIZATION_MODULEFILEEXTENSION_H
#define LLVM_CLANG_SERIALIZATION_MODULEFILEEXTENSION_H

#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include <memory>
#include <string>

namespace llvm {
class BitstreamCursor;
class BitstreamWriter;
class hash_code;
class raw_ostream;
}

namespace clang {

class ASTReader;
class ASTWriter;
class Sema;

namespace serialization {
  class ModuleFile;
} // end namespace serialization

/// Metadata for a module file extension.
struct ModuleFileExtensionMetadata {
  /// The name used to identify this particular extension block within
  /// the resulting module file. It should be unique to the particular
  /// extension, because this name will be used to match the name of
  /// an extension block to the appropriate reader.
  std::string BlockName;

  /// The major version of the extension data.
  unsigned MajorVersion;

  /// The minor version of the extension data.
  unsigned MinorVersion;

  /// A string containing additional user information that will be
  /// stored with the metadata.
  std::string UserInfo;
};

class ModuleFileExtensionReader;
class ModuleFileExtensionWriter;

/// An abstract superclass that describes a custom extension to the
/// module/precompiled header file format.
///
/// A module file extension can introduce additional information into
/// compiled module files (.pcm) and precompiled headers (.pch) via a
/// custom writer that can then be accessed via a custom reader when
/// the module file or precompiled header is loaded.
class ModuleFileExtension {
public:
  virtual ~ModuleFileExtension();

  /// Retrieves the metadata for this module file extension.
  virtual ModuleFileExtensionMetadata getExtensionMetadata() const = 0;

  /// Hash information about the presence of this extension into the
  /// module hash code.
  ///
  /// The module hash code is used to distinguish different variants
  /// of a module that are incompatible. If the presence, absence, or
  /// version of the module file extension should force the creation
  /// of a separate set of module files, override this method to
  /// combine that distinguishing information into the module hash
  /// code.
  ///
  /// The default implementation of this function simply returns the
  /// hash code as given, so the presence/absence of this extension
  /// does not distinguish module files.
  virtual llvm::hash_code hashExtension(llvm::hash_code c) const;

  /// Create a new module file extension writer, which will be
  /// responsible for writing the extension contents into a particular
  /// module file.
  virtual std::unique_ptr<ModuleFileExtensionWriter>
  createExtensionWriter(ASTWriter &Writer) = 0;

  /// Create a new module file extension reader, given the
  /// metadata read from the block and the cursor into the extension
  /// block.
  ///
  /// May return null to indicate that an extension block with the
  /// given metadata cannot be read.
  virtual std::unique_ptr<ModuleFileExtensionReader>
  createExtensionReader(const ModuleFileExtensionMetadata &Metadata,
                        ASTReader &Reader, serialization::ModuleFile &Mod,
                        const llvm::BitstreamCursor &Stream) = 0;
};

/// Abstract base class that writes a module file extension block into
/// a module file.
class ModuleFileExtensionWriter {
  ModuleFileExtension *Extension;

protected:
  ModuleFileExtensionWriter(ModuleFileExtension *Extension)
    : Extension(Extension) { }

public:
  virtual ~ModuleFileExtensionWriter();

  /// Retrieve the module file extension with which this writer is
  /// associated.
  ModuleFileExtension *getExtension() const { return Extension; }

  /// Write the contents of the extension block into the given bitstream.
  ///
  /// Responsible for writing the contents of the extension into the
  /// given stream. All of the contents should be written into custom
  /// records with IDs >= FIRST_EXTENSION_RECORD_ID.
  virtual void writeExtensionContents(Sema &SemaRef,
                                      llvm::BitstreamWriter &Stream) = 0;
};

/// Abstract base class that reads a module file extension block from
/// a module file.
///
/// Subclasses
class ModuleFileExtensionReader {
  ModuleFileExtension *Extension;

protected:
  ModuleFileExtensionReader(ModuleFileExtension *Extension)
    : Extension(Extension) { }

public:
  /// Retrieve the module file extension with which this reader is
  /// associated.
  ModuleFileExtension *getExtension() const { return Extension; }

  virtual ~ModuleFileExtensionReader();
};

} // end namespace clang

#endif // LLVM_CLANG_FRONTEND_MODULEFILEEXTENSION_H
