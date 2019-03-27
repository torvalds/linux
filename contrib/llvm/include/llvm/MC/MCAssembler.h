//===- MCAssembler.h - Object File Generation -------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCASSEMBLER_H
#define LLVM_MC_MCASSEMBLER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCFragment.h"
#include "llvm/MC/MCLinkerOptimizationHint.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/VersionTuple.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace llvm {

class MCAsmBackend;
class MCAsmLayout;
class MCContext;
class MCCodeEmitter;
class MCFragment;
class MCObjectWriter;
class MCSection;
class MCValue;

// FIXME: This really doesn't belong here. See comments below.
struct IndirectSymbolData {
  MCSymbol *Symbol;
  MCSection *Section;
};

// FIXME: Ditto this. Purely so the Streamer and the ObjectWriter can talk
// to one another.
struct DataRegionData {
  // This enum should be kept in sync w/ the mach-o definition in
  // llvm/Object/MachOFormat.h.
  enum KindTy { Data = 1, JumpTable8, JumpTable16, JumpTable32 } Kind;
  MCSymbol *Start;
  MCSymbol *End;
};

class MCAssembler {
  friend class MCAsmLayout;

public:
  using SectionListType = std::vector<MCSection *>;
  using SymbolDataListType = std::vector<const MCSymbol *>;

  using const_iterator = pointee_iterator<SectionListType::const_iterator>;
  using iterator = pointee_iterator<SectionListType::iterator>;

  using const_symbol_iterator =
      pointee_iterator<SymbolDataListType::const_iterator>;
  using symbol_iterator = pointee_iterator<SymbolDataListType::iterator>;

  using symbol_range = iterator_range<symbol_iterator>;
  using const_symbol_range = iterator_range<const_symbol_iterator>;

  using const_indirect_symbol_iterator =
      std::vector<IndirectSymbolData>::const_iterator;
  using indirect_symbol_iterator = std::vector<IndirectSymbolData>::iterator;

  using const_data_region_iterator =
      std::vector<DataRegionData>::const_iterator;
  using data_region_iterator = std::vector<DataRegionData>::iterator;

  /// MachO specific deployment target version info.
  // A Major version of 0 indicates that no version information was supplied
  // and so the corresponding load command should not be emitted.
  using VersionInfoType = struct {
    bool EmitBuildVersion;
    union {
      MCVersionMinType Type;          ///< Used when EmitBuildVersion==false.
      MachO::PlatformType Platform;   ///< Used when EmitBuildVersion==true.
    } TypeOrPlatform;
    unsigned Major;
    unsigned Minor;
    unsigned Update;
    /// An optional version of the SDK that was used to build the source.
    VersionTuple SDKVersion;
  };

private:
  MCContext &Context;

  std::unique_ptr<MCAsmBackend> Backend;

  std::unique_ptr<MCCodeEmitter> Emitter;

  std::unique_ptr<MCObjectWriter> Writer;

  SectionListType Sections;

  SymbolDataListType Symbols;

  std::vector<IndirectSymbolData> IndirectSymbols;

  std::vector<DataRegionData> DataRegions;

  /// The list of linker options to propagate into the object file.
  std::vector<std::vector<std::string>> LinkerOptions;

  /// List of declared file names
  std::vector<std::string> FileNames;

  MCDwarfLineTableParams LTParams;

  /// The set of function symbols for which a .thumb_func directive has
  /// been seen.
  //
  // FIXME: We really would like this in target specific code rather than
  // here. Maybe when the relocation stuff moves to target specific,
  // this can go with it? The streamer would need some target specific
  // refactoring too.
  mutable SmallPtrSet<const MCSymbol *, 32> ThumbFuncs;

  /// The bundle alignment size currently set in the assembler.
  ///
  /// By default it's 0, which means bundling is disabled.
  unsigned BundleAlignSize;

  bool RelaxAll : 1;
  bool SubsectionsViaSymbols : 1;
  bool IncrementalLinkerCompatible : 1;

  /// ELF specific e_header flags
  // It would be good if there were an MCELFAssembler class to hold this.
  // ELF header flags are used both by the integrated and standalone assemblers.
  // Access to the flags is necessary in cases where assembler directives affect
  // which flags to be set.
  unsigned ELFHeaderEFlags;

  /// Used to communicate Linker Optimization Hint information between
  /// the Streamer and the .o writer
  MCLOHContainer LOHContainer;

  VersionInfoType VersionInfo;

  /// Evaluate a fixup to a relocatable expression and the value which should be
  /// placed into the fixup.
  ///
  /// \param Layout The layout to use for evaluation.
  /// \param Fixup The fixup to evaluate.
  /// \param DF The fragment the fixup is inside.
  /// \param Target [out] On return, the relocatable expression the fixup
  /// evaluates to.
  /// \param Value [out] On return, the value of the fixup as currently laid
  /// out.
  /// \param WasForced [out] On return, the value in the fixup is set to the
  /// correct value if WasForced is true, even if evaluateFixup returns false.
  /// \return Whether the fixup value was fully resolved. This is true if the
  /// \p Value result is fixed, otherwise the value may change due to
  /// relocation.
  bool evaluateFixup(const MCAsmLayout &Layout, const MCFixup &Fixup,
                     const MCFragment *DF, MCValue &Target,
                     uint64_t &Value, bool &WasForced) const;

  /// Check whether a fixup can be satisfied, or whether it needs to be relaxed
  /// (increased in size, in order to hold its value correctly).
  bool fixupNeedsRelaxation(const MCFixup &Fixup, const MCRelaxableFragment *DF,
                            const MCAsmLayout &Layout) const;

  /// Check whether the given fragment needs relaxation.
  bool fragmentNeedsRelaxation(const MCRelaxableFragment *IF,
                               const MCAsmLayout &Layout) const;

  /// Perform one layout iteration and return true if any offsets
  /// were adjusted.
  bool layoutOnce(MCAsmLayout &Layout);

  /// Perform one layout iteration of the given section and return true
  /// if any offsets were adjusted.
  bool layoutSectionOnce(MCAsmLayout &Layout, MCSection &Sec);

  bool relaxInstruction(MCAsmLayout &Layout, MCRelaxableFragment &IF);

  bool relaxPaddingFragment(MCAsmLayout &Layout, MCPaddingFragment &PF);

  bool relaxLEB(MCAsmLayout &Layout, MCLEBFragment &IF);

  bool relaxDwarfLineAddr(MCAsmLayout &Layout, MCDwarfLineAddrFragment &DF);
  bool relaxDwarfCallFrameFragment(MCAsmLayout &Layout,
                                   MCDwarfCallFrameFragment &DF);
  bool relaxCVInlineLineTable(MCAsmLayout &Layout,
                              MCCVInlineLineTableFragment &DF);
  bool relaxCVDefRange(MCAsmLayout &Layout, MCCVDefRangeFragment &DF);

  /// finishLayout - Finalize a layout, including fragment lowering.
  void finishLayout(MCAsmLayout &Layout);

  std::tuple<MCValue, uint64_t, bool>
  handleFixup(const MCAsmLayout &Layout, MCFragment &F, const MCFixup &Fixup);

public:
  std::vector<std::pair<StringRef, const MCSymbol *>> Symvers;

  /// Construct a new assembler instance.
  //
  // FIXME: How are we going to parameterize this? Two obvious options are stay
  // concrete and require clients to pass in a target like object. The other
  // option is to make this abstract, and have targets provide concrete
  // implementations as we do with AsmParser.
  MCAssembler(MCContext &Context, std::unique_ptr<MCAsmBackend> Backend,
              std::unique_ptr<MCCodeEmitter> Emitter,
              std::unique_ptr<MCObjectWriter> Writer);
  MCAssembler(const MCAssembler &) = delete;
  MCAssembler &operator=(const MCAssembler &) = delete;
  ~MCAssembler();

  /// Compute the effective fragment size assuming it is laid out at the given
  /// \p SectionAddress and \p FragmentOffset.
  uint64_t computeFragmentSize(const MCAsmLayout &Layout,
                               const MCFragment &F) const;

  /// Find the symbol which defines the atom containing the given symbol, or
  /// null if there is no such symbol.
  const MCSymbol *getAtom(const MCSymbol &S) const;

  /// Check whether a particular symbol is visible to the linker and is required
  /// in the symbol table, or whether it can be discarded by the assembler. This
  /// also effects whether the assembler treats the label as potentially
  /// defining a separate atom.
  bool isSymbolLinkerVisible(const MCSymbol &SD) const;

  /// Emit the section contents to \p OS.
  void writeSectionData(raw_ostream &OS, const MCSection *Section,
                        const MCAsmLayout &Layout) const;

  /// Check whether a given symbol has been flagged with .thumb_func.
  bool isThumbFunc(const MCSymbol *Func) const;

  /// Flag a function symbol as the target of a .thumb_func directive.
  void setIsThumbFunc(const MCSymbol *Func) { ThumbFuncs.insert(Func); }

  /// ELF e_header flags
  unsigned getELFHeaderEFlags() const { return ELFHeaderEFlags; }
  void setELFHeaderEFlags(unsigned Flags) { ELFHeaderEFlags = Flags; }

  /// MachO deployment target version information.
  const VersionInfoType &getVersionInfo() const { return VersionInfo; }
  void setVersionMin(MCVersionMinType Type, unsigned Major, unsigned Minor,
                     unsigned Update,
                     VersionTuple SDKVersion = VersionTuple()) {
    VersionInfo.EmitBuildVersion = false;
    VersionInfo.TypeOrPlatform.Type = Type;
    VersionInfo.Major = Major;
    VersionInfo.Minor = Minor;
    VersionInfo.Update = Update;
    VersionInfo.SDKVersion = SDKVersion;
  }
  void setBuildVersion(MachO::PlatformType Platform, unsigned Major,
                       unsigned Minor, unsigned Update,
                       VersionTuple SDKVersion = VersionTuple()) {
    VersionInfo.EmitBuildVersion = true;
    VersionInfo.TypeOrPlatform.Platform = Platform;
    VersionInfo.Major = Major;
    VersionInfo.Minor = Minor;
    VersionInfo.Update = Update;
    VersionInfo.SDKVersion = SDKVersion;
  }

  /// Reuse an assembler instance
  ///
  void reset();

  MCContext &getContext() const { return Context; }

  MCAsmBackend *getBackendPtr() const { return Backend.get(); }

  MCCodeEmitter *getEmitterPtr() const { return Emitter.get(); }

  MCObjectWriter *getWriterPtr() const { return Writer.get(); }

  MCAsmBackend &getBackend() const { return *Backend; }

  MCCodeEmitter &getEmitter() const { return *Emitter; }

  MCObjectWriter &getWriter() const { return *Writer; }

  MCDwarfLineTableParams getDWARFLinetableParams() const { return LTParams; }
  void setDWARFLinetableParams(MCDwarfLineTableParams P) { LTParams = P; }

  /// Finish - Do final processing and write the object to the output stream.
  /// \p Writer is used for custom object writer (as the MCJIT does),
  /// if not specified it is automatically created from backend.
  void Finish();

  // Layout all section and prepare them for emission.
  void layout(MCAsmLayout &Layout);

  // FIXME: This does not belong here.
  bool getSubsectionsViaSymbols() const { return SubsectionsViaSymbols; }
  void setSubsectionsViaSymbols(bool Value) { SubsectionsViaSymbols = Value; }

  bool isIncrementalLinkerCompatible() const {
    return IncrementalLinkerCompatible;
  }
  void setIncrementalLinkerCompatible(bool Value) {
    IncrementalLinkerCompatible = Value;
  }

  bool getRelaxAll() const { return RelaxAll; }
  void setRelaxAll(bool Value) { RelaxAll = Value; }

  bool isBundlingEnabled() const { return BundleAlignSize != 0; }

  unsigned getBundleAlignSize() const { return BundleAlignSize; }

  void setBundleAlignSize(unsigned Size) {
    assert((Size == 0 || !(Size & (Size - 1))) &&
           "Expect a power-of-two bundle align size");
    BundleAlignSize = Size;
  }

  /// \name Section List Access
  /// @{

  iterator begin() { return Sections.begin(); }
  const_iterator begin() const { return Sections.begin(); }

  iterator end() { return Sections.end(); }
  const_iterator end() const { return Sections.end(); }

  size_t size() const { return Sections.size(); }

  /// @}
  /// \name Symbol List Access
  /// @{
  symbol_iterator symbol_begin() { return Symbols.begin(); }
  const_symbol_iterator symbol_begin() const { return Symbols.begin(); }

  symbol_iterator symbol_end() { return Symbols.end(); }
  const_symbol_iterator symbol_end() const { return Symbols.end(); }

  symbol_range symbols() { return make_range(symbol_begin(), symbol_end()); }
  const_symbol_range symbols() const {
    return make_range(symbol_begin(), symbol_end());
  }

  size_t symbol_size() const { return Symbols.size(); }

  /// @}
  /// \name Indirect Symbol List Access
  /// @{

  // FIXME: This is a total hack, this should not be here. Once things are
  // factored so that the streamer has direct access to the .o writer, it can
  // disappear.
  std::vector<IndirectSymbolData> &getIndirectSymbols() {
    return IndirectSymbols;
  }

  indirect_symbol_iterator indirect_symbol_begin() {
    return IndirectSymbols.begin();
  }
  const_indirect_symbol_iterator indirect_symbol_begin() const {
    return IndirectSymbols.begin();
  }

  indirect_symbol_iterator indirect_symbol_end() {
    return IndirectSymbols.end();
  }
  const_indirect_symbol_iterator indirect_symbol_end() const {
    return IndirectSymbols.end();
  }

  size_t indirect_symbol_size() const { return IndirectSymbols.size(); }

  /// @}
  /// \name Linker Option List Access
  /// @{

  std::vector<std::vector<std::string>> &getLinkerOptions() {
    return LinkerOptions;
  }

  /// @}
  /// \name Data Region List Access
  /// @{

  // FIXME: This is a total hack, this should not be here. Once things are
  // factored so that the streamer has direct access to the .o writer, it can
  // disappear.
  std::vector<DataRegionData> &getDataRegions() { return DataRegions; }

  data_region_iterator data_region_begin() { return DataRegions.begin(); }
  const_data_region_iterator data_region_begin() const {
    return DataRegions.begin();
  }

  data_region_iterator data_region_end() { return DataRegions.end(); }
  const_data_region_iterator data_region_end() const {
    return DataRegions.end();
  }

  size_t data_region_size() const { return DataRegions.size(); }

  /// @}
  /// \name Data Region List Access
  /// @{

  // FIXME: This is a total hack, this should not be here. Once things are
  // factored so that the streamer has direct access to the .o writer, it can
  // disappear.
  MCLOHContainer &getLOHContainer() { return LOHContainer; }
  const MCLOHContainer &getLOHContainer() const {
    return const_cast<MCAssembler *>(this)->getLOHContainer();
  }

  struct CGProfileEntry {
    const MCSymbolRefExpr *From;
    const MCSymbolRefExpr *To;
    uint64_t Count;
  };
  std::vector<CGProfileEntry> CGProfile;
  /// @}
  /// \name Backend Data Access
  /// @{

  bool registerSection(MCSection &Section);

  void registerSymbol(const MCSymbol &Symbol, bool *Created = nullptr);

  ArrayRef<std::string> getFileNames() { return FileNames; }

  void addFileName(StringRef FileName) {
    if (!is_contained(FileNames, FileName))
      FileNames.push_back(FileName);
  }

  /// Write the necessary bundle padding to \p OS.
  /// Expects a fragment \p F containing instructions and its size \p FSize.
  void writeFragmentPadding(raw_ostream &OS, const MCEncodedFragment &F,
                            uint64_t FSize) const;

  /// @}

  void dump() const;
};

/// Compute the amount of padding required before the fragment \p F to
/// obey bundling restrictions, where \p FOffset is the fragment's offset in
/// its section and \p FSize is the fragment's size.
uint64_t computeBundlePadding(const MCAssembler &Assembler,
                              const MCEncodedFragment *F, uint64_t FOffset,
                              uint64_t FSize);

} // end namespace llvm

#endif // LLVM_MC_MCASSEMBLER_H
