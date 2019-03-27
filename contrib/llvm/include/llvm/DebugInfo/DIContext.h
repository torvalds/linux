//===- DIContext.h ----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines DIContext, an abstract data structure that holds
// debug information data.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DICONTEXT_H
#define LLVM_DEBUGINFO_DICONTEXT_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <utility>

namespace llvm {

/// A format-neutral container for source line information.
struct DILineInfo {
  std::string FileName;
  std::string FunctionName;
  Optional<StringRef> Source;
  uint32_t Line = 0;
  uint32_t Column = 0;
  uint32_t StartLine = 0;

  // DWARF-specific.
  uint32_t Discriminator = 0;

  DILineInfo() : FileName("<invalid>"), FunctionName("<invalid>") {}

  bool operator==(const DILineInfo &RHS) const {
    return Line == RHS.Line && Column == RHS.Column &&
           FileName == RHS.FileName && FunctionName == RHS.FunctionName &&
           StartLine == RHS.StartLine && Discriminator == RHS.Discriminator;
  }

  bool operator!=(const DILineInfo &RHS) const {
    return !(*this == RHS);
  }

  bool operator<(const DILineInfo &RHS) const {
    return std::tie(FileName, FunctionName, Line, Column, StartLine,
                    Discriminator) <
           std::tie(RHS.FileName, RHS.FunctionName, RHS.Line, RHS.Column,
                    RHS.StartLine, RHS.Discriminator);
  }

  explicit operator bool() const { return *this != DILineInfo(); }

  void dump(raw_ostream &OS) {
    OS << "Line info: ";
    if (FileName != "<invalid>")
      OS << "file '" << FileName << "', ";
    if (FunctionName != "<invalid>")
      OS << "function '" << FunctionName << "', ";
    OS << "line " << Line << ", ";
    OS << "column " << Column << ", ";
    OS << "start line " << StartLine << '\n';
  }
};

using DILineInfoTable = SmallVector<std::pair<uint64_t, DILineInfo>, 16>;

/// A format-neutral container for inlined code description.
class DIInliningInfo {
  SmallVector<DILineInfo, 4> Frames;

public:
  DIInliningInfo() = default;

  const DILineInfo & getFrame(unsigned Index) const {
    assert(Index < Frames.size());
    return Frames[Index];
  }

  DILineInfo *getMutableFrame(unsigned Index) {
    assert(Index < Frames.size());
    return &Frames[Index];
  }

  uint32_t getNumberOfFrames() const {
    return Frames.size();
  }

  void addFrame(const DILineInfo &Frame) {
    Frames.push_back(Frame);
  }
  
  void resize(unsigned i) {
    Frames.resize(i);
  }
  
};

/// Container for description of a global variable.
struct DIGlobal {
  std::string Name;
  uint64_t Start = 0;
  uint64_t Size = 0;

  DIGlobal() : Name("<invalid>") {}
};

/// A DINameKind is passed to name search methods to specify a
/// preference regarding the type of name resolution the caller wants.
enum class DINameKind { None, ShortName, LinkageName };

/// Controls which fields of DILineInfo container should be filled
/// with data.
struct DILineInfoSpecifier {
  enum class FileLineInfoKind { None, Default, AbsoluteFilePath };
  using FunctionNameKind = DINameKind;

  FileLineInfoKind FLIKind;
  FunctionNameKind FNKind;

  DILineInfoSpecifier(FileLineInfoKind FLIKind = FileLineInfoKind::Default,
                      FunctionNameKind FNKind = FunctionNameKind::None)
      : FLIKind(FLIKind), FNKind(FNKind) {}
};

/// This is just a helper to programmatically construct DIDumpType.
enum DIDumpTypeCounter {
#define HANDLE_DWARF_SECTION(ENUM_NAME, ELF_NAME, CMDLINE_NAME) \
  DIDT_ID_##ENUM_NAME,
#include "llvm/BinaryFormat/Dwarf.def"
#undef HANDLE_DWARF_SECTION
  DIDT_ID_UUID,
  DIDT_ID_Count
};
static_assert(DIDT_ID_Count <= 32, "section types overflow storage");

/// Selects which debug sections get dumped.
enum DIDumpType : unsigned {
  DIDT_Null,
  DIDT_All             = ~0U,
#define HANDLE_DWARF_SECTION(ENUM_NAME, ELF_NAME, CMDLINE_NAME) \
  DIDT_##ENUM_NAME = 1U << DIDT_ID_##ENUM_NAME,
#include "llvm/BinaryFormat/Dwarf.def"
#undef HANDLE_DWARF_SECTION
  DIDT_UUID = 1 << DIDT_ID_UUID,
};

/// Container for dump options that control which debug information will be
/// dumped.
struct DIDumpOptions {
  unsigned DumpType = DIDT_All;
  unsigned RecurseDepth = -1U;
  uint16_t Version = 0; // DWARF version to assume when extracting.
  uint8_t AddrSize = 4; // Address byte size to assume when extracting.
  bool ShowAddresses = true;
  bool ShowChildren = false;
  bool ShowParents = false;
  bool ShowForm = false;
  bool SummarizeTypes = false;
  bool Verbose = false;
  bool DisplayRawContents = false;

  /// Return default option set for printing a single DIE without children.
  static DIDumpOptions getForSingleDIE() {
    DIDumpOptions Opts;
    Opts.RecurseDepth = 0;
    return Opts;
  }

  /// Return the options with RecurseDepth set to 0 unless explicitly required.
  DIDumpOptions noImplicitRecursion() const {
    DIDumpOptions Opts = *this;
    if (RecurseDepth == -1U && !ShowChildren)
      Opts.RecurseDepth = 0;
    return Opts;
  }
};

class DIContext {
public:
  enum DIContextKind {
    CK_DWARF,
    CK_PDB
  };

  DIContext(DIContextKind K) : Kind(K) {}
  virtual ~DIContext() = default;

  DIContextKind getKind() const { return Kind; }

  virtual void dump(raw_ostream &OS, DIDumpOptions DumpOpts) = 0;

  virtual bool verify(raw_ostream &OS, DIDumpOptions DumpOpts = {}) {
    // No verifier? Just say things went well.
    return true;
  }

  virtual DILineInfo getLineInfoForAddress(uint64_t Address,
      DILineInfoSpecifier Specifier = DILineInfoSpecifier()) = 0;
  virtual DILineInfoTable getLineInfoForAddressRange(uint64_t Address,
      uint64_t Size, DILineInfoSpecifier Specifier = DILineInfoSpecifier()) = 0;
  virtual DIInliningInfo getInliningInfoForAddress(uint64_t Address,
      DILineInfoSpecifier Specifier = DILineInfoSpecifier()) = 0;

private:
  const DIContextKind Kind;
};

/// An inferface for inquiring the load address of a loaded object file
/// to be used by the DIContext implementations when applying relocations
/// on the fly.
class LoadedObjectInfo {
protected:
  LoadedObjectInfo() = default;
  LoadedObjectInfo(const LoadedObjectInfo &) = default;

public:
  virtual ~LoadedObjectInfo() = default;

  /// Obtain the Load Address of a section by SectionRef.
  ///
  /// Calculate the address of the given section.
  /// The section need not be present in the local address space. The addresses
  /// need to be consistent with the addresses used to query the DIContext and
  /// the output of this function should be deterministic, i.e. repeated calls
  /// with the same Sec should give the same address.
  virtual uint64_t getSectionLoadAddress(const object::SectionRef &Sec) const {
    return 0;
  }

  /// If conveniently available, return the content of the given Section.
  ///
  /// When the section is available in the local address space, in relocated
  /// (loaded) form, e.g. because it was relocated by a JIT for execution, this
  /// function should provide the contents of said section in `Data`. If the
  /// loaded section is not available, or the cost of retrieving it would be
  /// prohibitive, this function should return false. In that case, relocations
  /// will be read from the local (unrelocated) object file and applied on the
  /// fly. Note that this method is used purely for optimzation purposes in the
  /// common case of JITting in the local address space, so returning false
  /// should always be correct.
  virtual bool getLoadedSectionContents(const object::SectionRef &Sec,
                                        StringRef &Data) const {
    return false;
  }

  // FIXME: This is untested and unused anywhere in the LLVM project, it's
  // used/needed by Julia (an external project). It should have some coverage
  // (at least tests, but ideally example functionality).
  /// Obtain a copy of this LoadedObjectInfo.
  virtual std::unique_ptr<LoadedObjectInfo> clone() const = 0;
};

template <typename Derived, typename Base = LoadedObjectInfo>
struct LoadedObjectInfoHelper : Base {
protected:
  LoadedObjectInfoHelper(const LoadedObjectInfoHelper &) = default;
  LoadedObjectInfoHelper() = default;

public:
  template <typename... Ts>
  LoadedObjectInfoHelper(Ts &&... Args) : Base(std::forward<Ts>(Args)...) {}

  std::unique_ptr<llvm::LoadedObjectInfo> clone() const override {
    return llvm::make_unique<Derived>(static_cast<const Derived &>(*this));
  }
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DICONTEXT_H
