//===- DWARFDebugFrame.h - Parsing of .debug_frame --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFDEBUGFRAME_H
#define LLVM_DEBUGINFO_DWARF_DWARFDEBUGFRAME_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Triple.h"
#include "llvm/DebugInfo/DWARF/DWARFDataExtractor.h"
#include "llvm/DebugInfo/DWARF/DWARFExpression.h"
#include "llvm/Support/Error.h"
#include <memory>
#include <vector>

namespace llvm {

class raw_ostream;

namespace dwarf {

/// Represent a sequence of Call Frame Information instructions that, when read
/// in order, construct a table mapping PC to frame state. This can also be
/// referred to as "CFI rules" in DWARF literature to avoid confusion with
/// computer programs in the broader sense, and in this context each instruction
/// would be a rule to establish the mapping. Refer to pg. 172 in the DWARF5
/// manual, "6.4.1 Structure of Call Frame Information".
class CFIProgram {
public:
  typedef SmallVector<uint64_t, 2> Operands;

  /// An instruction consists of a DWARF CFI opcode and an optional sequence of
  /// operands. If it refers to an expression, then this expression has its own
  /// sequence of operations and operands handled separately by DWARFExpression.
  struct Instruction {
    Instruction(uint8_t Opcode) : Opcode(Opcode) {}

    uint8_t Opcode;
    Operands Ops;
    // Associated DWARF expression in case this instruction refers to one
    Optional<DWARFExpression> Expression;
  };

  using InstrList = std::vector<Instruction>;
  using iterator = InstrList::iterator;
  using const_iterator = InstrList::const_iterator;

  iterator begin() { return Instructions.begin(); }
  const_iterator begin() const { return Instructions.begin(); }
  iterator end() { return Instructions.end(); }
  const_iterator end() const { return Instructions.end(); }

  unsigned size() const { return (unsigned)Instructions.size(); }
  bool empty() const { return Instructions.empty(); }

  CFIProgram(uint64_t CodeAlignmentFactor, int64_t DataAlignmentFactor,
             Triple::ArchType Arch)
      : CodeAlignmentFactor(CodeAlignmentFactor),
        DataAlignmentFactor(DataAlignmentFactor),
        Arch(Arch) {}

  /// Parse and store a sequence of CFI instructions from Data,
  /// starting at *Offset and ending at EndOffset. *Offset is updated
  /// to EndOffset upon successful parsing, or indicates the offset
  /// where a problem occurred in case an error is returned.
  Error parse(DataExtractor Data, uint32_t *Offset, uint32_t EndOffset);

  void dump(raw_ostream &OS, const MCRegisterInfo *MRI, bool IsEH,
            unsigned IndentLevel = 1) const;

private:
  std::vector<Instruction> Instructions;
  const uint64_t CodeAlignmentFactor;
  const int64_t DataAlignmentFactor;
  Triple::ArchType Arch;

  /// Convenience method to add a new instruction with the given opcode.
  void addInstruction(uint8_t Opcode) {
    Instructions.push_back(Instruction(Opcode));
  }

  /// Add a new single-operand instruction.
  void addInstruction(uint8_t Opcode, uint64_t Operand1) {
    Instructions.push_back(Instruction(Opcode));
    Instructions.back().Ops.push_back(Operand1);
  }

  /// Add a new instruction that has two operands.
  void addInstruction(uint8_t Opcode, uint64_t Operand1, uint64_t Operand2) {
    Instructions.push_back(Instruction(Opcode));
    Instructions.back().Ops.push_back(Operand1);
    Instructions.back().Ops.push_back(Operand2);
  }

  /// Types of operands to CFI instructions
  /// In DWARF, this type is implicitly tied to a CFI instruction opcode and
  /// thus this type doesn't need to be explictly written to the file (this is
  /// not a DWARF encoding). The relationship of instrs to operand types can
  /// be obtained from getOperandTypes() and is only used to simplify
  /// instruction printing.
  enum OperandType {
    OT_Unset,
    OT_None,
    OT_Address,
    OT_Offset,
    OT_FactoredCodeOffset,
    OT_SignedFactDataOffset,
    OT_UnsignedFactDataOffset,
    OT_Register,
    OT_Expression
  };

  /// Retrieve the array describing the types of operands according to the enum
  /// above. This is indexed by opcode.
  static ArrayRef<OperandType[2]> getOperandTypes();

  /// Print \p Opcode's operand number \p OperandIdx which has value \p Operand.
  void printOperand(raw_ostream &OS, const MCRegisterInfo *MRI, bool IsEH,
                    const Instruction &Instr, unsigned OperandIdx,
                    uint64_t Operand) const;
};

/// An entry in either debug_frame or eh_frame. This entry can be a CIE or an
/// FDE.
class FrameEntry {
public:
  enum FrameKind { FK_CIE, FK_FDE };

  FrameEntry(FrameKind K, uint64_t Offset, uint64_t Length, uint64_t CodeAlign,
             int64_t DataAlign, Triple::ArchType Arch)
      : Kind(K), Offset(Offset), Length(Length),
        CFIs(CodeAlign, DataAlign, Arch) {}

  virtual ~FrameEntry() {}

  FrameKind getKind() const { return Kind; }
  uint64_t getOffset() const { return Offset; }
  uint64_t getLength() const { return Length; }
  const CFIProgram &cfis() const { return CFIs; }
  CFIProgram &cfis() { return CFIs; }

  /// Dump the instructions in this CFI fragment
  virtual void dump(raw_ostream &OS, const MCRegisterInfo *MRI,
                    bool IsEH) const = 0;

protected:
  const FrameKind Kind;

  /// Offset of this entry in the section.
  const uint64_t Offset;

  /// Entry length as specified in DWARF.
  const uint64_t Length;

  CFIProgram CFIs;
};

/// DWARF Common Information Entry (CIE)
class CIE : public FrameEntry {
public:
  // CIEs (and FDEs) are simply container classes, so the only sensible way to
  // create them is by providing the full parsed contents in the constructor.
  CIE(uint64_t Offset, uint64_t Length, uint8_t Version,
      SmallString<8> Augmentation, uint8_t AddressSize,
      uint8_t SegmentDescriptorSize, uint64_t CodeAlignmentFactor,
      int64_t DataAlignmentFactor, uint64_t ReturnAddressRegister,
      SmallString<8> AugmentationData, uint32_t FDEPointerEncoding,
      uint32_t LSDAPointerEncoding, Optional<uint64_t> Personality,
      Optional<uint32_t> PersonalityEnc, Triple::ArchType Arch)
      : FrameEntry(FK_CIE, Offset, Length, CodeAlignmentFactor,
                   DataAlignmentFactor, Arch),
        Version(Version), Augmentation(std::move(Augmentation)),
        AddressSize(AddressSize), SegmentDescriptorSize(SegmentDescriptorSize),
        CodeAlignmentFactor(CodeAlignmentFactor),
        DataAlignmentFactor(DataAlignmentFactor),
        ReturnAddressRegister(ReturnAddressRegister),
        AugmentationData(std::move(AugmentationData)),
        FDEPointerEncoding(FDEPointerEncoding),
        LSDAPointerEncoding(LSDAPointerEncoding), Personality(Personality),
        PersonalityEnc(PersonalityEnc) {}

  static bool classof(const FrameEntry *FE) { return FE->getKind() == FK_CIE; }

  StringRef getAugmentationString() const { return Augmentation; }
  uint64_t getCodeAlignmentFactor() const { return CodeAlignmentFactor; }
  int64_t getDataAlignmentFactor() const { return DataAlignmentFactor; }
  uint8_t getVersion() const { return Version; }
  uint64_t getReturnAddressRegister() const { return ReturnAddressRegister; }
  Optional<uint64_t> getPersonalityAddress() const { return Personality; }
  Optional<uint32_t> getPersonalityEncoding() const { return PersonalityEnc; }

  uint32_t getFDEPointerEncoding() const { return FDEPointerEncoding; }

  uint32_t getLSDAPointerEncoding() const { return LSDAPointerEncoding; }

  void dump(raw_ostream &OS, const MCRegisterInfo *MRI,
            bool IsEH) const override;

private:
  /// The following fields are defined in section 6.4.1 of the DWARF standard v4
  const uint8_t Version;
  const SmallString<8> Augmentation;
  const uint8_t AddressSize;
  const uint8_t SegmentDescriptorSize;
  const uint64_t CodeAlignmentFactor;
  const int64_t DataAlignmentFactor;
  const uint64_t ReturnAddressRegister;

  // The following are used when the CIE represents an EH frame entry.
  const SmallString<8> AugmentationData;
  const uint32_t FDEPointerEncoding;
  const uint32_t LSDAPointerEncoding;
  const Optional<uint64_t> Personality;
  const Optional<uint32_t> PersonalityEnc;
};

/// DWARF Frame Description Entry (FDE)
class FDE : public FrameEntry {
public:
  // Each FDE has a CIE it's "linked to". Our FDE contains is constructed with
  // an offset to the CIE (provided by parsing the FDE header). The CIE itself
  // is obtained lazily once it's actually required.
  FDE(uint64_t Offset, uint64_t Length, int64_t LinkedCIEOffset,
      uint64_t InitialLocation, uint64_t AddressRange, CIE *Cie,
      Optional<uint64_t> LSDAAddress, Triple::ArchType Arch)
      : FrameEntry(FK_FDE, Offset, Length,
                   Cie ? Cie->getCodeAlignmentFactor() : 0,
                   Cie ? Cie->getDataAlignmentFactor() : 0,
                   Arch),
        LinkedCIEOffset(LinkedCIEOffset), InitialLocation(InitialLocation),
        AddressRange(AddressRange), LinkedCIE(Cie), LSDAAddress(LSDAAddress) {}

  ~FDE() override = default;

  const CIE *getLinkedCIE() const { return LinkedCIE; }
  uint64_t getInitialLocation() const { return InitialLocation; }
  uint64_t getAddressRange() const { return AddressRange; }
  Optional<uint64_t> getLSDAAddress() const { return LSDAAddress; }

  void dump(raw_ostream &OS, const MCRegisterInfo *MRI,
            bool IsEH) const override;

  static bool classof(const FrameEntry *FE) { return FE->getKind() == FK_FDE; }

private:
  /// The following fields are defined in section 6.4.1 of the DWARF standard v3
  const uint64_t LinkedCIEOffset;
  const uint64_t InitialLocation;
  const uint64_t AddressRange;
  const CIE *LinkedCIE;
  const Optional<uint64_t> LSDAAddress;
};

} // end namespace dwarf

/// A parsed .debug_frame or .eh_frame section
class DWARFDebugFrame {
  const Triple::ArchType Arch;
  // True if this is parsing an eh_frame section.
  const bool IsEH;
  // Not zero for sane pointer values coming out of eh_frame
  const uint64_t EHFrameAddress;

  std::vector<std::unique_ptr<dwarf::FrameEntry>> Entries;
  using iterator = pointee_iterator<decltype(Entries)::const_iterator>;

  /// Return the entry at the given offset or nullptr.
  dwarf::FrameEntry *getEntryAtOffset(uint64_t Offset) const;

public:
  // If IsEH is true, assume it is a .eh_frame section. Otherwise,
  // it is a .debug_frame section. EHFrameAddress should be different
  // than zero for correct parsing of .eh_frame addresses when they
  // use a PC-relative encoding.
  DWARFDebugFrame(Triple::ArchType Arch,
                  bool IsEH = false, uint64_t EHFrameAddress = 0);
  ~DWARFDebugFrame();

  /// Dump the section data into the given stream.
  void dump(raw_ostream &OS, const MCRegisterInfo *MRI,
            Optional<uint64_t> Offset) const;

  /// Parse the section from raw data. \p Data is assumed to contain the whole
  /// frame section contents to be parsed.
  void parse(DWARFDataExtractor Data);

  /// Return whether the section has any entries.
  bool empty() const { return Entries.empty(); }

  /// DWARF Frame entries accessors
  iterator begin() const { return Entries.begin(); }
  iterator end() const { return Entries.end(); }
  iterator_range<iterator> entries() const {
    return iterator_range<iterator>(Entries.begin(), Entries.end());
  }

  uint64_t getEHFrameAddress() const { return EHFrameAddress; }
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFDEBUGFRAME_H
