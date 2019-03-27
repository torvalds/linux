//===- CodeGenTarget.h - Target Class Wrapper -------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines wrappers for the Target class and related global
// functionality.  This makes it easier to access the data and provides a single
// place that needs to check it for validity.  All of these classes abort
// on error conditions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_UTILS_TABLEGEN_CODEGENTARGET_H
#define LLVM_UTILS_TABLEGEN_CODEGENTARGET_H

#include "CodeGenHwModes.h"
#include "CodeGenInstruction.h"
#include "CodeGenRegisters.h"
#include "InfoByHwMode.h"
#include "SDNodeProperties.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TableGen/Record.h"
#include <algorithm>

namespace llvm {

struct CodeGenRegister;
class CodeGenSchedModels;
class CodeGenTarget;

/// getValueType - Return the MVT::SimpleValueType that the specified TableGen
/// record corresponds to.
MVT::SimpleValueType getValueType(Record *Rec);

StringRef getName(MVT::SimpleValueType T);
StringRef getEnumName(MVT::SimpleValueType T);

/// getQualifiedName - Return the name of the specified record, with a
/// namespace qualifier if the record contains one.
std::string getQualifiedName(const Record *R);

/// CodeGenTarget - This class corresponds to the Target class in the .td files.
///
class CodeGenTarget {
  RecordKeeper &Records;
  Record *TargetRec;

  mutable DenseMap<const Record*,
                   std::unique_ptr<CodeGenInstruction>> Instructions;
  mutable std::unique_ptr<CodeGenRegBank> RegBank;
  mutable std::vector<Record*> RegAltNameIndices;
  mutable SmallVector<ValueTypeByHwMode, 8> LegalValueTypes;
  CodeGenHwModes CGH;
  void ReadRegAltNameIndices() const;
  void ReadInstructions() const;
  void ReadLegalValueTypes() const;

  mutable std::unique_ptr<CodeGenSchedModels> SchedModels;

  mutable std::vector<const CodeGenInstruction*> InstrsByEnum;
  mutable unsigned NumPseudoInstructions = 0;
public:
  CodeGenTarget(RecordKeeper &Records);
  ~CodeGenTarget();

  Record *getTargetRecord() const { return TargetRec; }
  const StringRef getName() const;

  /// getInstNamespace - Return the target-specific instruction namespace.
  ///
  StringRef getInstNamespace() const;

  /// getInstructionSet - Return the InstructionSet object.
  ///
  Record *getInstructionSet() const;

  /// getAllowRegisterRenaming - Return the AllowRegisterRenaming flag value for
  /// this target.
  ///
  bool getAllowRegisterRenaming() const;

  /// getAsmParser - Return the AssemblyParser definition for this target.
  ///
  Record *getAsmParser() const;

  /// getAsmParserVariant - Return the AssmblyParserVariant definition for
  /// this target.
  ///
  Record *getAsmParserVariant(unsigned i) const;

  /// getAsmParserVariantCount - Return the AssmblyParserVariant definition
  /// available for this target.
  ///
  unsigned getAsmParserVariantCount() const;

  /// getAsmWriter - Return the AssemblyWriter definition for this target.
  ///
  Record *getAsmWriter() const;

  /// getRegBank - Return the register bank description.
  CodeGenRegBank &getRegBank() const;

  /// getRegisterByName - If there is a register with the specific AsmName,
  /// return it.
  const CodeGenRegister *getRegisterByName(StringRef Name) const;

  const std::vector<Record*> &getRegAltNameIndices() const {
    if (RegAltNameIndices.empty()) ReadRegAltNameIndices();
    return RegAltNameIndices;
  }

  const CodeGenRegisterClass &getRegisterClass(Record *R) const {
    return *getRegBank().getRegClass(R);
  }

  /// getRegisterVTs - Find the union of all possible SimpleValueTypes for the
  /// specified physical register.
  std::vector<ValueTypeByHwMode> getRegisterVTs(Record *R) const;

  ArrayRef<ValueTypeByHwMode> getLegalValueTypes() const {
    if (LegalValueTypes.empty())
      ReadLegalValueTypes();
    return LegalValueTypes;
  }

  CodeGenSchedModels &getSchedModels() const;

  const CodeGenHwModes &getHwModes() const { return CGH; }

private:
  DenseMap<const Record*, std::unique_ptr<CodeGenInstruction>> &
  getInstructions() const {
    if (Instructions.empty()) ReadInstructions();
    return Instructions;
  }
public:

  CodeGenInstruction &getInstruction(const Record *InstRec) const {
    if (Instructions.empty()) ReadInstructions();
    auto I = Instructions.find(InstRec);
    assert(I != Instructions.end() && "Not an instruction");
    return *I->second;
  }

  /// Returns the number of predefined instructions.
  static unsigned getNumFixedInstructions();

  /// Returns the number of pseudo instructions.
  unsigned getNumPseudoInstructions() const {
    if (InstrsByEnum.empty())
      ComputeInstrsByEnum();
    return NumPseudoInstructions;
  }

  /// Return all of the instructions defined by the target, ordered by their
  /// enum value.
  /// The following order of instructions is also guaranteed:
  /// - fixed / generic instructions as declared in TargetOpcodes.def, in order;
  /// - pseudo instructions in lexicographical order sorted by name;
  /// - other instructions in lexicographical order sorted by name.
  ArrayRef<const CodeGenInstruction *> getInstructionsByEnumValue() const {
    if (InstrsByEnum.empty())
      ComputeInstrsByEnum();
    return InstrsByEnum;
  }

  typedef ArrayRef<const CodeGenInstruction *>::const_iterator inst_iterator;
  inst_iterator inst_begin() const{return getInstructionsByEnumValue().begin();}
  inst_iterator inst_end() const { return getInstructionsByEnumValue().end(); }


  /// isLittleEndianEncoding - are instruction bit patterns defined as  [0..n]?
  ///
  bool isLittleEndianEncoding() const;

  /// reverseBitsForLittleEndianEncoding - For little-endian instruction bit
  /// encodings, reverse the bit order of all instructions.
  void reverseBitsForLittleEndianEncoding();

  /// guessInstructionProperties - should we just guess unset instruction
  /// properties?
  bool guessInstructionProperties() const;

private:
  void ComputeInstrsByEnum() const;
};

/// ComplexPattern - ComplexPattern info, corresponding to the ComplexPattern
/// tablegen class in TargetSelectionDAG.td
class ComplexPattern {
  MVT::SimpleValueType Ty;
  unsigned NumOperands;
  std::string SelectFunc;
  std::vector<Record*> RootNodes;
  unsigned Properties; // Node properties
  unsigned Complexity;
public:
  ComplexPattern(Record *R);

  MVT::SimpleValueType getValueType() const { return Ty; }
  unsigned getNumOperands() const { return NumOperands; }
  const std::string &getSelectFunc() const { return SelectFunc; }
  const std::vector<Record*> &getRootNodes() const {
    return RootNodes;
  }
  bool hasProperty(enum SDNP Prop) const { return Properties & (1 << Prop); }
  unsigned getComplexity() const { return Complexity; }
};

} // End llvm namespace

#endif
