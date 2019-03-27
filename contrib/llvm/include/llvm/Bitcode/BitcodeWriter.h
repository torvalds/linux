//===- llvm/Bitcode/BitcodeWriter.h - Bitcode writers -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This header defines interfaces to write LLVM bitcode files/streams.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BITCODE_BITCODEWRITER_H
#define LLVM_BITCODE_BITCODEWRITER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/MC/StringTableBuilder.h"
#include "llvm/Support/Allocator.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace llvm {

class BitstreamWriter;
class Module;
class raw_ostream;

  class BitcodeWriter {
    SmallVectorImpl<char> &Buffer;
    std::unique_ptr<BitstreamWriter> Stream;

    StringTableBuilder StrtabBuilder{StringTableBuilder::RAW};

    // Owns any strings created by the irsymtab writer until we create the
    // string table.
    BumpPtrAllocator Alloc;

    bool WroteStrtab = false, WroteSymtab = false;

    void writeBlob(unsigned Block, unsigned Record, StringRef Blob);

    std::vector<Module *> Mods;

  public:
    /// Create a BitcodeWriter that writes to Buffer.
    BitcodeWriter(SmallVectorImpl<char> &Buffer);

    ~BitcodeWriter();

    /// Attempt to write a symbol table to the bitcode file. This must be called
    /// at most once after all modules have been written.
    ///
    /// A reader does not require a symbol table to interpret a bitcode file;
    /// the symbol table is needed only to improve link-time performance. So
    /// this function may decide not to write a symbol table. It may so decide
    /// if, for example, the target is unregistered or the IR is malformed.
    void writeSymtab();

    /// Write the bitcode file's string table. This must be called exactly once
    /// after all modules and the optional symbol table have been written.
    void writeStrtab();

    /// Copy the string table for another module into this bitcode file. This
    /// should be called after copying the module itself into the bitcode file.
    void copyStrtab(StringRef Strtab);

    /// Write the specified module to the buffer specified at construction time.
    ///
    /// If \c ShouldPreserveUseListOrder, encode the use-list order for each \a
    /// Value in \c M.  These will be reconstructed exactly when \a M is
    /// deserialized.
    ///
    /// If \c Index is supplied, the bitcode will contain the summary index
    /// (currently for use in ThinLTO optimization).
    ///
    /// \p GenerateHash enables hashing the Module and including the hash in the
    /// bitcode (currently for use in ThinLTO incremental build).
    ///
    /// If \p ModHash is non-null, when GenerateHash is true, the resulting
    /// hash is written into ModHash. When GenerateHash is false, that value
    /// is used as the hash instead of computing from the generated bitcode.
    /// Can be used to produce the same module hash for a minimized bitcode
    /// used just for the thin link as in the regular full bitcode that will
    /// be used in the backend.
    void writeModule(const Module &M, bool ShouldPreserveUseListOrder = false,
                     const ModuleSummaryIndex *Index = nullptr,
                     bool GenerateHash = false, ModuleHash *ModHash = nullptr);

    /// Write the specified thin link bitcode file (i.e., the minimized bitcode
    /// file) to the buffer specified at construction time. The thin link
    /// bitcode file is used for thin link, and it only contains the necessary
    /// information for thin link.
    ///
    /// ModHash is for use in ThinLTO incremental build, generated while the
    /// IR bitcode file writing.
    void writeThinLinkBitcode(const Module &M, const ModuleSummaryIndex &Index,
                              const ModuleHash &ModHash);

    void writeIndex(
        const ModuleSummaryIndex *Index,
        const std::map<std::string, GVSummaryMapTy> *ModuleToSummariesForIndex);
  };

  /// Write the specified module to the specified raw output stream.
  ///
  /// For streams where it matters, the given stream should be in "binary"
  /// mode.
  ///
  /// If \c ShouldPreserveUseListOrder, encode the use-list order for each \a
  /// Value in \c M.  These will be reconstructed exactly when \a M is
  /// deserialized.
  ///
  /// If \c Index is supplied, the bitcode will contain the summary index
  /// (currently for use in ThinLTO optimization).
  ///
  /// \p GenerateHash enables hashing the Module and including the hash in the
  /// bitcode (currently for use in ThinLTO incremental build).
  ///
  /// If \p ModHash is non-null, when GenerateHash is true, the resulting
  /// hash is written into ModHash. When GenerateHash is false, that value
  /// is used as the hash instead of computing from the generated bitcode.
  /// Can be used to produce the same module hash for a minimized bitcode
  /// used just for the thin link as in the regular full bitcode that will
  /// be used in the backend.
  void WriteBitcodeToFile(const Module &M, raw_ostream &Out,
                          bool ShouldPreserveUseListOrder = false,
                          const ModuleSummaryIndex *Index = nullptr,
                          bool GenerateHash = false,
                          ModuleHash *ModHash = nullptr);

  /// Write the specified thin link bitcode file (i.e., the minimized bitcode
  /// file) to the given raw output stream, where it will be written in a new
  /// bitcode block. The thin link bitcode file is used for thin link, and it
  /// only contains the necessary information for thin link.
  ///
  /// ModHash is for use in ThinLTO incremental build, generated while the IR
  /// bitcode file writing.
  void WriteThinLinkBitcodeToFile(const Module &M, raw_ostream &Out,
                                  const ModuleSummaryIndex &Index,
                                  const ModuleHash &ModHash);

  /// Write the specified module summary index to the given raw output stream,
  /// where it will be written in a new bitcode block. This is used when
  /// writing the combined index file for ThinLTO. When writing a subset of the
  /// index for a distributed backend, provide the \p ModuleToSummariesForIndex
  /// map.
  void WriteIndexToFile(const ModuleSummaryIndex &Index, raw_ostream &Out,
                        const std::map<std::string, GVSummaryMapTy>
                            *ModuleToSummariesForIndex = nullptr);

} // end namespace llvm

#endif // LLVM_BITCODE_BITCODEWRITER_H
