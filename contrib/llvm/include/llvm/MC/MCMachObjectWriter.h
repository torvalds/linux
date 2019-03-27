//===- llvm/MC/MCMachObjectWriter.h - Mach Object Writer --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCMACHOBJECTWRITER_H
#define LLVM_MC_MCMACHOBJECTWRITER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/StringTableBuilder.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace llvm {

class MachObjectWriter;

class MCMachObjectTargetWriter : public MCObjectTargetWriter {
  const unsigned Is64Bit : 1;
  const uint32_t CPUType;
  const uint32_t CPUSubtype;
  unsigned LocalDifference_RIT;

protected:
  MCMachObjectTargetWriter(bool Is64Bit_, uint32_t CPUType_,
                           uint32_t CPUSubtype_);

  void setLocalDifferenceRelocationType(unsigned Type) {
    LocalDifference_RIT = Type;
  }

public:
  virtual ~MCMachObjectTargetWriter();

  virtual Triple::ObjectFormatType getFormat() const { return Triple::MachO; }
  static bool classof(const MCObjectTargetWriter *W) {
    return W->getFormat() == Triple::MachO;
  }

  /// \name Lifetime Management
  /// @{

  virtual void reset() {}

  /// @}

  /// \name Accessors
  /// @{

  bool is64Bit() const { return Is64Bit; }
  uint32_t getCPUType() const { return CPUType; }
  uint32_t getCPUSubtype() const { return CPUSubtype; }
  unsigned getLocalDifferenceRelocationType() const {
    return LocalDifference_RIT;
  }

  /// @}

  /// \name API
  /// @{

  virtual void recordRelocation(MachObjectWriter *Writer, MCAssembler &Asm,
                                const MCAsmLayout &Layout,
                                const MCFragment *Fragment,
                                const MCFixup &Fixup, MCValue Target,
                                uint64_t &FixedValue) = 0;

  /// @}
};

class MachObjectWriter : public MCObjectWriter {
  /// Helper struct for containing some precomputed information on symbols.
  struct MachSymbolData {
    const MCSymbol *Symbol;
    uint64_t StringIndex;
    uint8_t SectionIndex;

    // Support lexicographic sorting.
    bool operator<(const MachSymbolData &RHS) const;
  };

  /// The target specific Mach-O writer instance.
  std::unique_ptr<MCMachObjectTargetWriter> TargetObjectWriter;

  /// \name Relocation Data
  /// @{

  struct RelAndSymbol {
    const MCSymbol *Sym;
    MachO::any_relocation_info MRE;
    RelAndSymbol(const MCSymbol *Sym, const MachO::any_relocation_info &MRE)
        : Sym(Sym), MRE(MRE) {}
  };

  DenseMap<const MCSection *, std::vector<RelAndSymbol>> Relocations;
  DenseMap<const MCSection *, unsigned> IndirectSymBase;

  SectionAddrMap SectionAddress;

  /// @}
  /// \name Symbol Table Data
  /// @{

  StringTableBuilder StringTable{StringTableBuilder::MachO};
  std::vector<MachSymbolData> LocalSymbolData;
  std::vector<MachSymbolData> ExternalSymbolData;
  std::vector<MachSymbolData> UndefinedSymbolData;

  /// @}

  MachSymbolData *findSymbolData(const MCSymbol &Sym);

  void writeWithPadding(StringRef Str, uint64_t Size);

public:
  MachObjectWriter(std::unique_ptr<MCMachObjectTargetWriter> MOTW,
                   raw_pwrite_stream &OS, bool IsLittleEndian)
      : TargetObjectWriter(std::move(MOTW)),
        W(OS, IsLittleEndian ? support::little : support::big) {}

  support::endian::Writer W;

  const MCSymbol &findAliasedSymbol(const MCSymbol &Sym) const;

  /// \name Lifetime management Methods
  /// @{

  void reset() override;

  /// @}

  /// \name Utility Methods
  /// @{

  bool isFixupKindPCRel(const MCAssembler &Asm, unsigned Kind);

  SectionAddrMap &getSectionAddressMap() { return SectionAddress; }

  uint64_t getSectionAddress(const MCSection *Sec) const {
    return SectionAddress.lookup(Sec);
  }
  uint64_t getSymbolAddress(const MCSymbol &S, const MCAsmLayout &Layout) const;

  uint64_t getFragmentAddress(const MCFragment *Fragment,
                              const MCAsmLayout &Layout) const;

  uint64_t getPaddingSize(const MCSection *SD, const MCAsmLayout &Layout) const;

  bool doesSymbolRequireExternRelocation(const MCSymbol &S);

  /// @}

  /// \name Target Writer Proxy Accessors
  /// @{

  bool is64Bit() const { return TargetObjectWriter->is64Bit(); }
  bool isX86_64() const {
    uint32_t CPUType = TargetObjectWriter->getCPUType();
    return CPUType == MachO::CPU_TYPE_X86_64;
  }

  /// @}

  void writeHeader(MachO::HeaderFileType Type, unsigned NumLoadCommands,
                   unsigned LoadCommandsSize, bool SubsectionsViaSymbols);

  /// Write a segment load command.
  ///
  /// \param NumSections The number of sections in this segment.
  /// \param SectionDataSize The total size of the sections.
  void writeSegmentLoadCommand(StringRef Name, unsigned NumSections,
                               uint64_t VMAddr, uint64_t VMSize,
                               uint64_t SectionDataStartOffset,
                               uint64_t SectionDataSize, uint32_t MaxProt,
                               uint32_t InitProt);

  void writeSection(const MCAsmLayout &Layout, const MCSection &Sec,
                    uint64_t VMAddr, uint64_t FileOffset, unsigned Flags,
                    uint64_t RelocationsStart, unsigned NumRelocations);

  void writeSymtabLoadCommand(uint32_t SymbolOffset, uint32_t NumSymbols,
                              uint32_t StringTableOffset,
                              uint32_t StringTableSize);

  void writeDysymtabLoadCommand(
      uint32_t FirstLocalSymbol, uint32_t NumLocalSymbols,
      uint32_t FirstExternalSymbol, uint32_t NumExternalSymbols,
      uint32_t FirstUndefinedSymbol, uint32_t NumUndefinedSymbols,
      uint32_t IndirectSymbolOffset, uint32_t NumIndirectSymbols);

  void writeNlist(MachSymbolData &MSD, const MCAsmLayout &Layout);

  void writeLinkeditLoadCommand(uint32_t Type, uint32_t DataOffset,
                                uint32_t DataSize);

  void writeLinkerOptionsLoadCommand(const std::vector<std::string> &Options);

  // FIXME: We really need to improve the relocation validation. Basically, we
  // want to implement a separate computation which evaluates the relocation
  // entry as the linker would, and verifies that the resultant fixup value is
  // exactly what the encoder wanted. This will catch several classes of
  // problems:
  //
  //  - Relocation entry bugs, the two algorithms are unlikely to have the same
  //    exact bug.
  //
  //  - Relaxation issues, where we forget to relax something.
  //
  //  - Input errors, where something cannot be correctly encoded. 'as' allows
  //    these through in many cases.

  // Add a relocation to be output in the object file. At the time this is
  // called, the symbol indexes are not know, so if the relocation refers
  // to a symbol it should be passed as \p RelSymbol so that it can be updated
  // afterwards. If the relocation doesn't refer to a symbol, nullptr should be
  // used.
  void addRelocation(const MCSymbol *RelSymbol, const MCSection *Sec,
                     MachO::any_relocation_info &MRE) {
    RelAndSymbol P(RelSymbol, MRE);
    Relocations[Sec].push_back(P);
  }

  void recordScatteredRelocation(const MCAssembler &Asm,
                                 const MCAsmLayout &Layout,
                                 const MCFragment *Fragment,
                                 const MCFixup &Fixup, MCValue Target,
                                 unsigned Log2Size, uint64_t &FixedValue);

  void recordTLVPRelocation(const MCAssembler &Asm, const MCAsmLayout &Layout,
                            const MCFragment *Fragment, const MCFixup &Fixup,
                            MCValue Target, uint64_t &FixedValue);

  void recordRelocation(MCAssembler &Asm, const MCAsmLayout &Layout,
                        const MCFragment *Fragment, const MCFixup &Fixup,
                        MCValue Target, uint64_t &FixedValue) override;

  void bindIndirectSymbols(MCAssembler &Asm);

  /// Compute the symbol table data.
  void computeSymbolTable(MCAssembler &Asm,
                          std::vector<MachSymbolData> &LocalSymbolData,
                          std::vector<MachSymbolData> &ExternalSymbolData,
                          std::vector<MachSymbolData> &UndefinedSymbolData);

  void computeSectionAddresses(const MCAssembler &Asm,
                               const MCAsmLayout &Layout);

  void executePostLayoutBinding(MCAssembler &Asm,
                                const MCAsmLayout &Layout) override;

  bool isSymbolRefDifferenceFullyResolvedImpl(const MCAssembler &Asm,
                                              const MCSymbol &A,
                                              const MCSymbol &B,
                                              bool InSet) const override;

  bool isSymbolRefDifferenceFullyResolvedImpl(const MCAssembler &Asm,
                                              const MCSymbol &SymA,
                                              const MCFragment &FB, bool InSet,
                                              bool IsPCRel) const override;

  uint64_t writeObject(MCAssembler &Asm, const MCAsmLayout &Layout) override;
};

/// Construct a new Mach-O writer instance.
///
/// This routine takes ownership of the target writer subclass.
///
/// \param MOTW - The target specific Mach-O writer subclass.
/// \param OS - The stream to write to.
/// \returns The constructed object writer.
std::unique_ptr<MCObjectWriter>
createMachObjectWriter(std::unique_ptr<MCMachObjectTargetWriter> MOTW,
                       raw_pwrite_stream &OS, bool IsLittleEndian);

} // end namespace llvm

#endif // LLVM_MC_MCMACHOBJECTWRITER_H
