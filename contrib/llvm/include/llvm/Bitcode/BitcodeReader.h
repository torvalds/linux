//===- llvm/Bitcode/BitcodeReader.h - Bitcode reader ------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This header defines interfaces to read LLVM bitcode files/streams.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BITCODE_BITCODEREADER_H
#define LLVM_BITCODE_BITCODEREADER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Bitcode/BitCodes.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/MemoryBuffer.h"
#include <cstdint>
#include <memory>
#include <string>
#include <system_error>
#include <vector>
namespace llvm {

class LLVMContext;
class Module;

  // These functions are for converting Expected/Error values to
  // ErrorOr/std::error_code for compatibility with legacy clients. FIXME:
  // Remove these functions once no longer needed by the C and libLTO APIs.

  std::error_code errorToErrorCodeAndEmitErrors(LLVMContext &Ctx, Error Err);

  template <typename T>
  ErrorOr<T> expectedToErrorOrAndEmitErrors(LLVMContext &Ctx, Expected<T> Val) {
    if (!Val)
      return errorToErrorCodeAndEmitErrors(Ctx, Val.takeError());
    return std::move(*Val);
  }

  struct BitcodeFileContents;

  /// Basic information extracted from a bitcode module to be used for LTO.
  struct BitcodeLTOInfo {
    bool IsThinLTO;
    bool HasSummary;
    bool EnableSplitLTOUnit;
  };

  /// Represents a module in a bitcode file.
  class BitcodeModule {
    // This covers the identification (if present) and module blocks.
    ArrayRef<uint8_t> Buffer;
    StringRef ModuleIdentifier;

    // The string table used to interpret this module.
    StringRef Strtab;

    // The bitstream location of the IDENTIFICATION_BLOCK.
    uint64_t IdentificationBit;

    // The bitstream location of this module's MODULE_BLOCK.
    uint64_t ModuleBit;

    BitcodeModule(ArrayRef<uint8_t> Buffer, StringRef ModuleIdentifier,
                  uint64_t IdentificationBit, uint64_t ModuleBit)
        : Buffer(Buffer), ModuleIdentifier(ModuleIdentifier),
          IdentificationBit(IdentificationBit), ModuleBit(ModuleBit) {}

    // Calls the ctor.
    friend Expected<BitcodeFileContents>
    getBitcodeFileContents(MemoryBufferRef Buffer);

    Expected<std::unique_ptr<Module>> getModuleImpl(LLVMContext &Context,
                                                    bool MaterializeAll,
                                                    bool ShouldLazyLoadMetadata,
                                                    bool IsImporting);

  public:
    StringRef getBuffer() const {
      return StringRef((const char *)Buffer.begin(), Buffer.size());
    }

    StringRef getStrtab() const { return Strtab; }

    StringRef getModuleIdentifier() const { return ModuleIdentifier; }

    /// Read the bitcode module and prepare for lazy deserialization of function
    /// bodies. If ShouldLazyLoadMetadata is true, lazily load metadata as well.
    /// If IsImporting is true, this module is being parsed for ThinLTO
    /// importing into another module.
    Expected<std::unique_ptr<Module>> getLazyModule(LLVMContext &Context,
                                                    bool ShouldLazyLoadMetadata,
                                                    bool IsImporting);

    /// Read the entire bitcode module and return it.
    Expected<std::unique_ptr<Module>> parseModule(LLVMContext &Context);

    /// Returns information about the module to be used for LTO: whether to
    /// compile with ThinLTO, and whether it has a summary.
    Expected<BitcodeLTOInfo> getLTOInfo();

    /// Parse the specified bitcode buffer, returning the module summary index.
    Expected<std::unique_ptr<ModuleSummaryIndex>> getSummary();

    /// Parse the specified bitcode buffer and merge its module summary index
    /// into CombinedIndex.
    Error readSummary(ModuleSummaryIndex &CombinedIndex, StringRef ModulePath,
                      uint64_t ModuleId);
  };

  struct BitcodeFileContents {
    std::vector<BitcodeModule> Mods;
    StringRef Symtab, StrtabForSymtab;
  };

  /// Returns the contents of a bitcode file. This includes the raw contents of
  /// the symbol table embedded in the bitcode file. Clients which require a
  /// symbol table should prefer to use irsymtab::read instead of this function
  /// because it creates a reader for the irsymtab and handles upgrading bitcode
  /// files without a symbol table or with an old symbol table.
  Expected<BitcodeFileContents> getBitcodeFileContents(MemoryBufferRef Buffer);

  /// Returns a list of modules in the specified bitcode buffer.
  Expected<std::vector<BitcodeModule>>
  getBitcodeModuleList(MemoryBufferRef Buffer);

  /// Read the header of the specified bitcode buffer and prepare for lazy
  /// deserialization of function bodies. If ShouldLazyLoadMetadata is true,
  /// lazily load metadata as well. If IsImporting is true, this module is
  /// being parsed for ThinLTO importing into another module.
  Expected<std::unique_ptr<Module>>
  getLazyBitcodeModule(MemoryBufferRef Buffer, LLVMContext &Context,
                       bool ShouldLazyLoadMetadata = false,
                       bool IsImporting = false);

  /// Like getLazyBitcodeModule, except that the module takes ownership of
  /// the memory buffer if successful. If successful, this moves Buffer. On
  /// error, this *does not* move Buffer. If IsImporting is true, this module is
  /// being parsed for ThinLTO importing into another module.
  Expected<std::unique_ptr<Module>> getOwningLazyBitcodeModule(
      std::unique_ptr<MemoryBuffer> &&Buffer, LLVMContext &Context,
      bool ShouldLazyLoadMetadata = false, bool IsImporting = false);

  /// Read the header of the specified bitcode buffer and extract just the
  /// triple information. If successful, this returns a string. On error, this
  /// returns "".
  Expected<std::string> getBitcodeTargetTriple(MemoryBufferRef Buffer);

  /// Return true if \p Buffer contains a bitcode file with ObjC code (category
  /// or class) in it.
  Expected<bool> isBitcodeContainingObjCCategory(MemoryBufferRef Buffer);

  /// Read the header of the specified bitcode buffer and extract just the
  /// producer string information. If successful, this returns a string. On
  /// error, this returns "".
  Expected<std::string> getBitcodeProducerString(MemoryBufferRef Buffer);

  /// Read the specified bitcode file, returning the module.
  Expected<std::unique_ptr<Module>> parseBitcodeFile(MemoryBufferRef Buffer,
                                                     LLVMContext &Context);

  /// Returns LTO information for the specified bitcode file.
  Expected<BitcodeLTOInfo> getBitcodeLTOInfo(MemoryBufferRef Buffer);

  /// Parse the specified bitcode buffer, returning the module summary index.
  Expected<std::unique_ptr<ModuleSummaryIndex>>
  getModuleSummaryIndex(MemoryBufferRef Buffer);

  /// Parse the specified bitcode buffer and merge the index into CombinedIndex.
  Error readModuleSummaryIndex(MemoryBufferRef Buffer,
                               ModuleSummaryIndex &CombinedIndex,
                               uint64_t ModuleId);

  /// Parse the module summary index out of an IR file and return the module
  /// summary index object if found, or an empty summary if not. If Path refers
  /// to an empty file and IgnoreEmptyThinLTOIndexFile is true, then
  /// this function will return nullptr.
  Expected<std::unique_ptr<ModuleSummaryIndex>>
  getModuleSummaryIndexForFile(StringRef Path,
                               bool IgnoreEmptyThinLTOIndexFile = false);

  /// isBitcodeWrapper - Return true if the given bytes are the magic bytes
  /// for an LLVM IR bitcode wrapper.
  inline bool isBitcodeWrapper(const unsigned char *BufPtr,
                               const unsigned char *BufEnd) {
    // See if you can find the hidden message in the magic bytes :-).
    // (Hint: it's a little-endian encoding.)
    return BufPtr != BufEnd &&
           BufPtr[0] == 0xDE &&
           BufPtr[1] == 0xC0 &&
           BufPtr[2] == 0x17 &&
           BufPtr[3] == 0x0B;
  }

  /// isRawBitcode - Return true if the given bytes are the magic bytes for
  /// raw LLVM IR bitcode (without a wrapper).
  inline bool isRawBitcode(const unsigned char *BufPtr,
                           const unsigned char *BufEnd) {
    // These bytes sort of have a hidden message, but it's not in
    // little-endian this time, and it's a little redundant.
    return BufPtr != BufEnd &&
           BufPtr[0] == 'B' &&
           BufPtr[1] == 'C' &&
           BufPtr[2] == 0xc0 &&
           BufPtr[3] == 0xde;
  }

  /// isBitcode - Return true if the given bytes are the magic bytes for
  /// LLVM IR bitcode, either with or without a wrapper.
  inline bool isBitcode(const unsigned char *BufPtr,
                        const unsigned char *BufEnd) {
    return isBitcodeWrapper(BufPtr, BufEnd) ||
           isRawBitcode(BufPtr, BufEnd);
  }

  /// SkipBitcodeWrapperHeader - Some systems wrap bc files with a special
  /// header for padding or other reasons.  The format of this header is:
  ///
  /// struct bc_header {
  ///   uint32_t Magic;         // 0x0B17C0DE
  ///   uint32_t Version;       // Version, currently always 0.
  ///   uint32_t BitcodeOffset; // Offset to traditional bitcode file.
  ///   uint32_t BitcodeSize;   // Size of traditional bitcode file.
  ///   ... potentially other gunk ...
  /// };
  ///
  /// This function is called when we find a file with a matching magic number.
  /// In this case, skip down to the subsection of the file that is actually a
  /// BC file.
  /// If 'VerifyBufferSize' is true, check that the buffer is large enough to
  /// contain the whole bitcode file.
  inline bool SkipBitcodeWrapperHeader(const unsigned char *&BufPtr,
                                       const unsigned char *&BufEnd,
                                       bool VerifyBufferSize) {
    // Must contain the offset and size field!
    if (unsigned(BufEnd - BufPtr) < BWH_SizeField + 4)
      return true;

    unsigned Offset = support::endian::read32le(&BufPtr[BWH_OffsetField]);
    unsigned Size = support::endian::read32le(&BufPtr[BWH_SizeField]);
    uint64_t BitcodeOffsetEnd = (uint64_t)Offset + (uint64_t)Size;

    // Verify that Offset+Size fits in the file.
    if (VerifyBufferSize && BitcodeOffsetEnd > uint64_t(BufEnd-BufPtr))
      return true;
    BufPtr += Offset;
    BufEnd = BufPtr+Size;
    return false;
  }

  const std::error_category &BitcodeErrorCategory();
  enum class BitcodeError { CorruptedBitcode = 1 };
  inline std::error_code make_error_code(BitcodeError E) {
    return std::error_code(static_cast<int>(E), BitcodeErrorCategory());
  }

} // end namespace llvm

namespace std {

template <> struct is_error_code_enum<llvm::BitcodeError> : std::true_type {};

} // end namespace std

#endif // LLVM_BITCODE_BITCODEREADER_H
