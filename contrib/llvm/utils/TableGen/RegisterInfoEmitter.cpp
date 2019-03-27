//===- RegisterInfoEmitter.cpp - Generate a Register File Desc. -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This tablegen backend is responsible for emitting a description of a target
// register file for a code generator.  It uses instances of the Register,
// RegisterAliases, and RegisterClass classes to gather this information.
//
//===----------------------------------------------------------------------===//

#include "CodeGenRegisters.h"
#include "CodeGenTarget.h"
#include "SequenceToOffsetTable.h"
#include "Types.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/SparseBitVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MachineValueType.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/SetTheory.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iterator>
#include <set>
#include <string>
#include <vector>

using namespace llvm;

cl::OptionCategory RegisterInfoCat("Options for -gen-register-info");

static cl::opt<bool>
    RegisterInfoDebug("register-info-debug", cl::init(false),
                      cl::desc("Dump register information to help debugging"),
                      cl::cat(RegisterInfoCat));

namespace {

class RegisterInfoEmitter {
  CodeGenTarget Target;
  RecordKeeper &Records;

public:
  RegisterInfoEmitter(RecordKeeper &R) : Target(R), Records(R) {
    CodeGenRegBank &RegBank = Target.getRegBank();
    RegBank.computeDerivedInfo();
  }

  // runEnums - Print out enum values for all of the registers.
  void runEnums(raw_ostream &o, CodeGenTarget &Target, CodeGenRegBank &Bank);

  // runMCDesc - Print out MC register descriptions.
  void runMCDesc(raw_ostream &o, CodeGenTarget &Target, CodeGenRegBank &Bank);

  // runTargetHeader - Emit a header fragment for the register info emitter.
  void runTargetHeader(raw_ostream &o, CodeGenTarget &Target,
                       CodeGenRegBank &Bank);

  // runTargetDesc - Output the target register and register file descriptions.
  void runTargetDesc(raw_ostream &o, CodeGenTarget &Target,
                     CodeGenRegBank &Bank);

  // run - Output the register file description.
  void run(raw_ostream &o);

  void debugDump(raw_ostream &OS);

private:
  void EmitRegMapping(raw_ostream &o, const std::deque<CodeGenRegister> &Regs,
                      bool isCtor);
  void EmitRegMappingTables(raw_ostream &o,
                            const std::deque<CodeGenRegister> &Regs,
                            bool isCtor);
  void EmitRegUnitPressure(raw_ostream &OS, const CodeGenRegBank &RegBank,
                           const std::string &ClassName);
  void emitComposeSubRegIndices(raw_ostream &OS, CodeGenRegBank &RegBank,
                                const std::string &ClassName);
  void emitComposeSubRegIndexLaneMask(raw_ostream &OS, CodeGenRegBank &RegBank,
                                      const std::string &ClassName);
};

} // end anonymous namespace

// runEnums - Print out enum values for all of the registers.
void RegisterInfoEmitter::runEnums(raw_ostream &OS,
                                   CodeGenTarget &Target, CodeGenRegBank &Bank) {
  const auto &Registers = Bank.getRegisters();

  // Register enums are stored as uint16_t in the tables. Make sure we'll fit.
  assert(Registers.size() <= 0xffff && "Too many regs to fit in tables");

  StringRef Namespace = Registers.front().TheDef->getValueAsString("Namespace");

  emitSourceFileHeader("Target Register Enum Values", OS);

  OS << "\n#ifdef GET_REGINFO_ENUM\n";
  OS << "#undef GET_REGINFO_ENUM\n\n";

  OS << "namespace llvm {\n\n";

  OS << "class MCRegisterClass;\n"
     << "extern const MCRegisterClass " << Target.getName()
     << "MCRegisterClasses[];\n\n";

  if (!Namespace.empty())
    OS << "namespace " << Namespace << " {\n";
  OS << "enum {\n  NoRegister,\n";

  for (const auto &Reg : Registers)
    OS << "  " << Reg.getName() << " = " << Reg.EnumValue << ",\n";
  assert(Registers.size() == Registers.back().EnumValue &&
         "Register enum value mismatch!");
  OS << "  NUM_TARGET_REGS \t// " << Registers.size()+1 << "\n";
  OS << "};\n";
  if (!Namespace.empty())
    OS << "} // end namespace " << Namespace << "\n";

  const auto &RegisterClasses = Bank.getRegClasses();
  if (!RegisterClasses.empty()) {

    // RegisterClass enums are stored as uint16_t in the tables.
    assert(RegisterClasses.size() <= 0xffff &&
           "Too many register classes to fit in tables");

    OS << "\n// Register classes\n\n";
    if (!Namespace.empty())
      OS << "namespace " << Namespace << " {\n";
    OS << "enum {\n";
    for (const auto &RC : RegisterClasses)
      OS << "  " << RC.getName() << "RegClassID"
         << " = " << RC.EnumValue << ",\n";
    OS << "\n  };\n";
    if (!Namespace.empty())
      OS << "} // end namespace " << Namespace << "\n\n";
  }

  const std::vector<Record*> &RegAltNameIndices = Target.getRegAltNameIndices();
  // If the only definition is the default NoRegAltName, we don't need to
  // emit anything.
  if (RegAltNameIndices.size() > 1) {
    OS << "\n// Register alternate name indices\n\n";
    if (!Namespace.empty())
      OS << "namespace " << Namespace << " {\n";
    OS << "enum {\n";
    for (unsigned i = 0, e = RegAltNameIndices.size(); i != e; ++i)
      OS << "  " << RegAltNameIndices[i]->getName() << ",\t// " << i << "\n";
    OS << "  NUM_TARGET_REG_ALT_NAMES = " << RegAltNameIndices.size() << "\n";
    OS << "};\n";
    if (!Namespace.empty())
      OS << "} // end namespace " << Namespace << "\n\n";
  }

  auto &SubRegIndices = Bank.getSubRegIndices();
  if (!SubRegIndices.empty()) {
    OS << "\n// Subregister indices\n\n";
    std::string Namespace = SubRegIndices.front().getNamespace();
    if (!Namespace.empty())
      OS << "namespace " << Namespace << " {\n";
    OS << "enum {\n  NoSubRegister,\n";
    unsigned i = 0;
    for (const auto &Idx : SubRegIndices)
      OS << "  " << Idx.getName() << ",\t// " << ++i << "\n";
    OS << "  NUM_TARGET_SUBREGS\n};\n";
    if (!Namespace.empty())
      OS << "} // end namespace " << Namespace << "\n\n";
  }

  OS << "} // end namespace llvm\n\n";
  OS << "#endif // GET_REGINFO_ENUM\n\n";
}

static void printInt(raw_ostream &OS, int Val) {
  OS << Val;
}

void RegisterInfoEmitter::
EmitRegUnitPressure(raw_ostream &OS, const CodeGenRegBank &RegBank,
                    const std::string &ClassName) {
  unsigned NumRCs = RegBank.getRegClasses().size();
  unsigned NumSets = RegBank.getNumRegPressureSets();

  OS << "/// Get the weight in units of pressure for this register class.\n"
     << "const RegClassWeight &" << ClassName << "::\n"
     << "getRegClassWeight(const TargetRegisterClass *RC) const {\n"
     << "  static const RegClassWeight RCWeightTable[] = {\n";
  for (const auto &RC : RegBank.getRegClasses()) {
    const CodeGenRegister::Vec &Regs = RC.getMembers();
    if (Regs.empty() || RC.Artificial)
      OS << "    {0, 0";
    else {
      std::vector<unsigned> RegUnits;
      RC.buildRegUnitSet(RegBank, RegUnits);
      OS << "    {" << (*Regs.begin())->getWeight(RegBank)
         << ", " << RegBank.getRegUnitSetWeight(RegUnits);
    }
    OS << "},  \t// " << RC.getName() << "\n";
  }
  OS << "  };\n"
     << "  return RCWeightTable[RC->getID()];\n"
     << "}\n\n";

  // Reasonable targets (not ARMv7) have unit weight for all units, so don't
  // bother generating a table.
  bool RegUnitsHaveUnitWeight = true;
  for (unsigned UnitIdx = 0, UnitEnd = RegBank.getNumNativeRegUnits();
       UnitIdx < UnitEnd; ++UnitIdx) {
    if (RegBank.getRegUnit(UnitIdx).Weight > 1)
      RegUnitsHaveUnitWeight = false;
  }
  OS << "/// Get the weight in units of pressure for this register unit.\n"
     << "unsigned " << ClassName << "::\n"
     << "getRegUnitWeight(unsigned RegUnit) const {\n"
     << "  assert(RegUnit < " << RegBank.getNumNativeRegUnits()
     << " && \"invalid register unit\");\n";
  if (!RegUnitsHaveUnitWeight) {
    OS << "  static const uint8_t RUWeightTable[] = {\n    ";
    for (unsigned UnitIdx = 0, UnitEnd = RegBank.getNumNativeRegUnits();
         UnitIdx < UnitEnd; ++UnitIdx) {
      const RegUnit &RU = RegBank.getRegUnit(UnitIdx);
      assert(RU.Weight < 256 && "RegUnit too heavy");
      OS << RU.Weight << ", ";
    }
    OS << "};\n"
       << "  return RUWeightTable[RegUnit];\n";
  }
  else {
    OS << "  // All register units have unit weight.\n"
       << "  return 1;\n";
  }
  OS << "}\n\n";

  OS << "\n"
     << "// Get the number of dimensions of register pressure.\n"
     << "unsigned " << ClassName << "::getNumRegPressureSets() const {\n"
     << "  return " << NumSets << ";\n}\n\n";

  OS << "// Get the name of this register unit pressure set.\n"
     << "const char *" << ClassName << "::\n"
     << "getRegPressureSetName(unsigned Idx) const {\n"
     << "  static const char *const PressureNameTable[] = {\n";
  unsigned MaxRegUnitWeight = 0;
  for (unsigned i = 0; i < NumSets; ++i ) {
    const RegUnitSet &RegUnits = RegBank.getRegSetAt(i);
    MaxRegUnitWeight = std::max(MaxRegUnitWeight, RegUnits.Weight);
    OS << "    \"" << RegUnits.Name << "\",\n";
  }
  OS << "  };\n"
     << "  return PressureNameTable[Idx];\n"
     << "}\n\n";

  OS << "// Get the register unit pressure limit for this dimension.\n"
     << "// This limit must be adjusted dynamically for reserved registers.\n"
     << "unsigned " << ClassName << "::\n"
     << "getRegPressureSetLimit(const MachineFunction &MF, unsigned Idx) const "
        "{\n"
     << "  static const " << getMinimalTypeForRange(MaxRegUnitWeight, 32)
     << " PressureLimitTable[] = {\n";
  for (unsigned i = 0; i < NumSets; ++i ) {
    const RegUnitSet &RegUnits = RegBank.getRegSetAt(i);
    OS << "    " << RegUnits.Weight << ",  \t// " << i << ": "
       << RegUnits.Name << "\n";
  }
  OS << "  };\n"
     << "  return PressureLimitTable[Idx];\n"
     << "}\n\n";

  SequenceToOffsetTable<std::vector<int>> PSetsSeqs;

  // This table may be larger than NumRCs if some register units needed a list
  // of unit sets that did not correspond to a register class.
  unsigned NumRCUnitSets = RegBank.getNumRegClassPressureSetLists();
  std::vector<std::vector<int>> PSets(NumRCUnitSets);

  for (unsigned i = 0, e = NumRCUnitSets; i != e; ++i) {
    ArrayRef<unsigned> PSetIDs = RegBank.getRCPressureSetIDs(i);
    PSets[i].reserve(PSetIDs.size());
    for (ArrayRef<unsigned>::iterator PSetI = PSetIDs.begin(),
           PSetE = PSetIDs.end(); PSetI != PSetE; ++PSetI) {
      PSets[i].push_back(RegBank.getRegPressureSet(*PSetI).Order);
    }
    llvm::sort(PSets[i]);
    PSetsSeqs.add(PSets[i]);
  }

  PSetsSeqs.layout();

  OS << "/// Table of pressure sets per register class or unit.\n"
     << "static const int RCSetsTable[] = {\n";
  PSetsSeqs.emit(OS, printInt, "-1");
  OS << "};\n\n";

  OS << "/// Get the dimensions of register pressure impacted by this "
     << "register class.\n"
     << "/// Returns a -1 terminated array of pressure set IDs\n"
     << "const int* " << ClassName << "::\n"
     << "getRegClassPressureSets(const TargetRegisterClass *RC) const {\n";
  OS << "  static const " << getMinimalTypeForRange(PSetsSeqs.size() - 1, 32)
     << " RCSetStartTable[] = {\n    ";
  for (unsigned i = 0, e = NumRCs; i != e; ++i) {
    OS << PSetsSeqs.get(PSets[i]) << ",";
  }
  OS << "};\n"
     << "  return &RCSetsTable[RCSetStartTable[RC->getID()]];\n"
     << "}\n\n";

  OS << "/// Get the dimensions of register pressure impacted by this "
     << "register unit.\n"
     << "/// Returns a -1 terminated array of pressure set IDs\n"
     << "const int* " << ClassName << "::\n"
     << "getRegUnitPressureSets(unsigned RegUnit) const {\n"
     << "  assert(RegUnit < " << RegBank.getNumNativeRegUnits()
     << " && \"invalid register unit\");\n";
  OS << "  static const " << getMinimalTypeForRange(PSetsSeqs.size() - 1, 32)
     << " RUSetStartTable[] = {\n    ";
  for (unsigned UnitIdx = 0, UnitEnd = RegBank.getNumNativeRegUnits();
       UnitIdx < UnitEnd; ++UnitIdx) {
    OS << PSetsSeqs.get(PSets[RegBank.getRegUnit(UnitIdx).RegClassUnitSetsIdx])
       << ",";
  }
  OS << "};\n"
     << "  return &RCSetsTable[RUSetStartTable[RegUnit]];\n"
     << "}\n\n";
}

using DwarfRegNumsMapPair = std::pair<Record*, std::vector<int64_t>>;
using DwarfRegNumsVecTy = std::vector<DwarfRegNumsMapPair>;

void finalizeDwarfRegNumsKeys(DwarfRegNumsVecTy &DwarfRegNums) {
  // Sort and unique to get a map-like vector. We want the last assignment to
  // match previous behaviour.
  std::stable_sort(DwarfRegNums.begin(), DwarfRegNums.end(),
                   on_first<LessRecordRegister>());
  // Warn about duplicate assignments.
  const Record *LastSeenReg = nullptr;
  for (const auto &X : DwarfRegNums) {
    const auto &Reg = X.first;
    // The only way LessRecordRegister can return equal is if they're the same
    // string. Use simple equality instead.
    if (LastSeenReg && Reg->getName() == LastSeenReg->getName())
      PrintWarning(Reg->getLoc(), Twine("DWARF numbers for register ") +
                                      getQualifiedName(Reg) +
                                      "specified multiple times");
    LastSeenReg = Reg;
  }
  auto Last = std::unique(
      DwarfRegNums.begin(), DwarfRegNums.end(),
      [](const DwarfRegNumsMapPair &A, const DwarfRegNumsMapPair &B) {
        return A.first->getName() == B.first->getName();
      });
  DwarfRegNums.erase(Last, DwarfRegNums.end());
}

void RegisterInfoEmitter::EmitRegMappingTables(
    raw_ostream &OS, const std::deque<CodeGenRegister> &Regs, bool isCtor) {
  // Collect all information about dwarf register numbers
  DwarfRegNumsVecTy DwarfRegNums;

  // First, just pull all provided information to the map
  unsigned maxLength = 0;
  for (auto &RE : Regs) {
    Record *Reg = RE.TheDef;
    std::vector<int64_t> RegNums = Reg->getValueAsListOfInts("DwarfNumbers");
    maxLength = std::max((size_t)maxLength, RegNums.size());
    DwarfRegNums.emplace_back(Reg, std::move(RegNums));
  }
  finalizeDwarfRegNumsKeys(DwarfRegNums);

  if (!maxLength)
    return;

  // Now we know maximal length of number list. Append -1's, where needed
  for (DwarfRegNumsVecTy::iterator I = DwarfRegNums.begin(),
                                   E = DwarfRegNums.end();
       I != E; ++I)
    for (unsigned i = I->second.size(), e = maxLength; i != e; ++i)
      I->second.push_back(-1);

  StringRef Namespace = Regs.front().TheDef->getValueAsString("Namespace");

  OS << "// " << Namespace << " Dwarf<->LLVM register mappings.\n";

  // Emit reverse information about the dwarf register numbers.
  for (unsigned j = 0; j < 2; ++j) {
    for (unsigned i = 0, e = maxLength; i != e; ++i) {
      OS << "extern const MCRegisterInfo::DwarfLLVMRegPair " << Namespace;
      OS << (j == 0 ? "DwarfFlavour" : "EHFlavour");
      OS << i << "Dwarf2L[]";

      if (!isCtor) {
        OS << " = {\n";

        // Store the mapping sorted by the LLVM reg num so lookup can be done
        // with a binary search.
        std::map<uint64_t, Record*> Dwarf2LMap;
        for (DwarfRegNumsVecTy::iterator
               I = DwarfRegNums.begin(), E = DwarfRegNums.end(); I != E; ++I) {
          int DwarfRegNo = I->second[i];
          if (DwarfRegNo < 0)
            continue;
          Dwarf2LMap[DwarfRegNo] = I->first;
        }

        for (std::map<uint64_t, Record*>::iterator
               I = Dwarf2LMap.begin(), E = Dwarf2LMap.end(); I != E; ++I)
          OS << "  { " << I->first << "U, " << getQualifiedName(I->second)
             << " },\n";

        OS << "};\n";
      } else {
        OS << ";\n";
      }

      // We have to store the size in a const global, it's used in multiple
      // places.
      OS << "extern const unsigned " << Namespace
         << (j == 0 ? "DwarfFlavour" : "EHFlavour") << i << "Dwarf2LSize";
      if (!isCtor)
        OS << " = array_lengthof(" << Namespace
           << (j == 0 ? "DwarfFlavour" : "EHFlavour") << i
           << "Dwarf2L);\n\n";
      else
        OS << ";\n\n";
    }
  }

  for (auto &RE : Regs) {
    Record *Reg = RE.TheDef;
    const RecordVal *V = Reg->getValue("DwarfAlias");
    if (!V || !V->getValue())
      continue;

    DefInit *DI = cast<DefInit>(V->getValue());
    Record *Alias = DI->getDef();
    const auto &AliasIter =
        std::lower_bound(DwarfRegNums.begin(), DwarfRegNums.end(), Alias,
                         [](const DwarfRegNumsMapPair &A, const Record *B) {
                           return LessRecordRegister()(A.first, B);
                         });
    assert(AliasIter != DwarfRegNums.end() && AliasIter->first == Alias &&
           "Expected Alias to be present in map");
    const auto &RegIter =
        std::lower_bound(DwarfRegNums.begin(), DwarfRegNums.end(), Reg,
                         [](const DwarfRegNumsMapPair &A, const Record *B) {
                           return LessRecordRegister()(A.first, B);
                         });
    assert(RegIter != DwarfRegNums.end() && RegIter->first == Reg &&
           "Expected Reg to be present in map");
    RegIter->second = AliasIter->second;
  }

  // Emit information about the dwarf register numbers.
  for (unsigned j = 0; j < 2; ++j) {
    for (unsigned i = 0, e = maxLength; i != e; ++i) {
      OS << "extern const MCRegisterInfo::DwarfLLVMRegPair " << Namespace;
      OS << (j == 0 ? "DwarfFlavour" : "EHFlavour");
      OS << i << "L2Dwarf[]";
      if (!isCtor) {
        OS << " = {\n";
        // Store the mapping sorted by the Dwarf reg num so lookup can be done
        // with a binary search.
        for (DwarfRegNumsVecTy::iterator
               I = DwarfRegNums.begin(), E = DwarfRegNums.end(); I != E; ++I) {
          int RegNo = I->second[i];
          if (RegNo == -1) // -1 is the default value, don't emit a mapping.
            continue;

          OS << "  { " << getQualifiedName(I->first) << ", " << RegNo
             << "U },\n";
        }
        OS << "};\n";
      } else {
        OS << ";\n";
      }

      // We have to store the size in a const global, it's used in multiple
      // places.
      OS << "extern const unsigned " << Namespace
         << (j == 0 ? "DwarfFlavour" : "EHFlavour") << i << "L2DwarfSize";
      if (!isCtor)
        OS << " = array_lengthof(" << Namespace
           << (j == 0 ? "DwarfFlavour" : "EHFlavour") << i << "L2Dwarf);\n\n";
      else
        OS << ";\n\n";
    }
  }
}

void RegisterInfoEmitter::EmitRegMapping(
    raw_ostream &OS, const std::deque<CodeGenRegister> &Regs, bool isCtor) {
  // Emit the initializer so the tables from EmitRegMappingTables get wired up
  // to the MCRegisterInfo object.
  unsigned maxLength = 0;
  for (auto &RE : Regs) {
    Record *Reg = RE.TheDef;
    maxLength = std::max((size_t)maxLength,
                         Reg->getValueAsListOfInts("DwarfNumbers").size());
  }

  if (!maxLength)
    return;

  StringRef Namespace = Regs.front().TheDef->getValueAsString("Namespace");

  // Emit reverse information about the dwarf register numbers.
  for (unsigned j = 0; j < 2; ++j) {
    OS << "  switch (";
    if (j == 0)
      OS << "DwarfFlavour";
    else
      OS << "EHFlavour";
    OS << ") {\n"
     << "  default:\n"
     << "    llvm_unreachable(\"Unknown DWARF flavour\");\n";

    for (unsigned i = 0, e = maxLength; i != e; ++i) {
      OS << "  case " << i << ":\n";
      OS << "    ";
      if (!isCtor)
        OS << "RI->";
      std::string Tmp;
      raw_string_ostream(Tmp) << Namespace
                              << (j == 0 ? "DwarfFlavour" : "EHFlavour") << i
                              << "Dwarf2L";
      OS << "mapDwarfRegsToLLVMRegs(" << Tmp << ", " << Tmp << "Size, ";
      if (j == 0)
          OS << "false";
        else
          OS << "true";
      OS << ");\n";
      OS << "    break;\n";
    }
    OS << "  }\n";
  }

  // Emit information about the dwarf register numbers.
  for (unsigned j = 0; j < 2; ++j) {
    OS << "  switch (";
    if (j == 0)
      OS << "DwarfFlavour";
    else
      OS << "EHFlavour";
    OS << ") {\n"
       << "  default:\n"
       << "    llvm_unreachable(\"Unknown DWARF flavour\");\n";

    for (unsigned i = 0, e = maxLength; i != e; ++i) {
      OS << "  case " << i << ":\n";
      OS << "    ";
      if (!isCtor)
        OS << "RI->";
      std::string Tmp;
      raw_string_ostream(Tmp) << Namespace
                              << (j == 0 ? "DwarfFlavour" : "EHFlavour") << i
                              << "L2Dwarf";
      OS << "mapLLVMRegsToDwarfRegs(" << Tmp << ", " << Tmp << "Size, ";
      if (j == 0)
          OS << "false";
        else
          OS << "true";
      OS << ");\n";
      OS << "    break;\n";
    }
    OS << "  }\n";
  }
}

// Print a BitVector as a sequence of hex numbers using a little-endian mapping.
// Width is the number of bits per hex number.
static void printBitVectorAsHex(raw_ostream &OS,
                                const BitVector &Bits,
                                unsigned Width) {
  assert(Width <= 32 && "Width too large");
  unsigned Digits = (Width + 3) / 4;
  for (unsigned i = 0, e = Bits.size(); i < e; i += Width) {
    unsigned Value = 0;
    for (unsigned j = 0; j != Width && i + j != e; ++j)
      Value |= Bits.test(i + j) << j;
    OS << format("0x%0*x, ", Digits, Value);
  }
}

// Helper to emit a set of bits into a constant byte array.
class BitVectorEmitter {
  BitVector Values;
public:
  void add(unsigned v) {
    if (v >= Values.size())
      Values.resize(((v/8)+1)*8); // Round up to the next byte.
    Values[v] = true;
  }

  void print(raw_ostream &OS) {
    printBitVectorAsHex(OS, Values, 8);
  }
};

static void printSimpleValueType(raw_ostream &OS, MVT::SimpleValueType VT) {
  OS << getEnumName(VT);
}

static void printSubRegIndex(raw_ostream &OS, const CodeGenSubRegIndex *Idx) {
  OS << Idx->EnumValue;
}

// Differentially encoded register and regunit lists allow for better
// compression on regular register banks. The sequence is computed from the
// differential list as:
//
//   out[0] = InitVal;
//   out[n+1] = out[n] + diff[n]; // n = 0, 1, ...
//
// The initial value depends on the specific list. The list is terminated by a
// 0 differential which means we can't encode repeated elements.

typedef SmallVector<uint16_t, 4> DiffVec;
typedef SmallVector<LaneBitmask, 4> MaskVec;

// Differentially encode a sequence of numbers into V. The starting value and
// terminating 0 are not added to V, so it will have the same size as List.
static
DiffVec &diffEncode(DiffVec &V, unsigned InitVal, SparseBitVector<> List) {
  assert(V.empty() && "Clear DiffVec before diffEncode.");
  uint16_t Val = uint16_t(InitVal);

  for (uint16_t Cur : List) {
    V.push_back(Cur - Val);
    Val = Cur;
  }
  return V;
}

template<typename Iter>
static
DiffVec &diffEncode(DiffVec &V, unsigned InitVal, Iter Begin, Iter End) {
  assert(V.empty() && "Clear DiffVec before diffEncode.");
  uint16_t Val = uint16_t(InitVal);
  for (Iter I = Begin; I != End; ++I) {
    uint16_t Cur = (*I)->EnumValue;
    V.push_back(Cur - Val);
    Val = Cur;
  }
  return V;
}

static void printDiff16(raw_ostream &OS, uint16_t Val) {
  OS << Val;
}

static void printMask(raw_ostream &OS, LaneBitmask Val) {
  OS << "LaneBitmask(0x" << PrintLaneMask(Val) << ')';
}

// Try to combine Idx's compose map into Vec if it is compatible.
// Return false if it's not possible.
static bool combine(const CodeGenSubRegIndex *Idx,
                    SmallVectorImpl<CodeGenSubRegIndex*> &Vec) {
  const CodeGenSubRegIndex::CompMap &Map = Idx->getComposites();
  for (const auto &I : Map) {
    CodeGenSubRegIndex *&Entry = Vec[I.first->EnumValue - 1];
    if (Entry && Entry != I.second)
      return false;
  }

  // All entries are compatible. Make it so.
  for (const auto &I : Map) {
    auto *&Entry = Vec[I.first->EnumValue - 1];
    assert((!Entry || Entry == I.second) &&
           "Expected EnumValue to be unique");
    Entry = I.second;
  }
  return true;
}

void
RegisterInfoEmitter::emitComposeSubRegIndices(raw_ostream &OS,
                                              CodeGenRegBank &RegBank,
                                              const std::string &ClName) {
  const auto &SubRegIndices = RegBank.getSubRegIndices();
  OS << "unsigned " << ClName
     << "::composeSubRegIndicesImpl(unsigned IdxA, unsigned IdxB) const {\n";

  // Many sub-register indexes are composition-compatible, meaning that
  //
  //   compose(IdxA, IdxB) == compose(IdxA', IdxB)
  //
  // for many IdxA, IdxA' pairs. Not all sub-register indexes can be composed.
  // The illegal entries can be use as wildcards to compress the table further.

  // Map each Sub-register index to a compatible table row.
  SmallVector<unsigned, 4> RowMap;
  SmallVector<SmallVector<CodeGenSubRegIndex*, 4>, 4> Rows;

  auto SubRegIndicesSize =
      std::distance(SubRegIndices.begin(), SubRegIndices.end());
  for (const auto &Idx : SubRegIndices) {
    unsigned Found = ~0u;
    for (unsigned r = 0, re = Rows.size(); r != re; ++r) {
      if (combine(&Idx, Rows[r])) {
        Found = r;
        break;
      }
    }
    if (Found == ~0u) {
      Found = Rows.size();
      Rows.resize(Found + 1);
      Rows.back().resize(SubRegIndicesSize);
      combine(&Idx, Rows.back());
    }
    RowMap.push_back(Found);
  }

  // Output the row map if there is multiple rows.
  if (Rows.size() > 1) {
    OS << "  static const " << getMinimalTypeForRange(Rows.size(), 32)
       << " RowMap[" << SubRegIndicesSize << "] = {\n    ";
    for (unsigned i = 0, e = SubRegIndicesSize; i != e; ++i)
      OS << RowMap[i] << ", ";
    OS << "\n  };\n";
  }

  // Output the rows.
  OS << "  static const " << getMinimalTypeForRange(SubRegIndicesSize + 1, 32)
     << " Rows[" << Rows.size() << "][" << SubRegIndicesSize << "] = {\n";
  for (unsigned r = 0, re = Rows.size(); r != re; ++r) {
    OS << "    { ";
    for (unsigned i = 0, e = SubRegIndicesSize; i != e; ++i)
      if (Rows[r][i])
        OS << Rows[r][i]->EnumValue << ", ";
      else
        OS << "0, ";
    OS << "},\n";
  }
  OS << "  };\n\n";

  OS << "  --IdxA; assert(IdxA < " << SubRegIndicesSize << ");\n"
     << "  --IdxB; assert(IdxB < " << SubRegIndicesSize << ");\n";
  if (Rows.size() > 1)
    OS << "  return Rows[RowMap[IdxA]][IdxB];\n";
  else
    OS << "  return Rows[0][IdxB];\n";
  OS << "}\n\n";
}

void
RegisterInfoEmitter::emitComposeSubRegIndexLaneMask(raw_ostream &OS,
                                                    CodeGenRegBank &RegBank,
                                                    const std::string &ClName) {
  // See the comments in computeSubRegLaneMasks() for our goal here.
  const auto &SubRegIndices = RegBank.getSubRegIndices();

  // Create a list of Mask+Rotate operations, with equivalent entries merged.
  SmallVector<unsigned, 4> SubReg2SequenceIndexMap;
  SmallVector<SmallVector<MaskRolPair, 1>, 4> Sequences;
  for (const auto &Idx : SubRegIndices) {
    const SmallVector<MaskRolPair, 1> &IdxSequence
      = Idx.CompositionLaneMaskTransform;

    unsigned Found = ~0u;
    unsigned SIdx = 0;
    unsigned NextSIdx;
    for (size_t s = 0, se = Sequences.size(); s != se; ++s, SIdx = NextSIdx) {
      SmallVectorImpl<MaskRolPair> &Sequence = Sequences[s];
      NextSIdx = SIdx + Sequence.size() + 1;
      if (Sequence == IdxSequence) {
        Found = SIdx;
        break;
      }
    }
    if (Found == ~0u) {
      Sequences.push_back(IdxSequence);
      Found = SIdx;
    }
    SubReg2SequenceIndexMap.push_back(Found);
  }

  OS << "  struct MaskRolOp {\n"
        "    LaneBitmask Mask;\n"
        "    uint8_t  RotateLeft;\n"
        "  };\n"
        "  static const MaskRolOp LaneMaskComposeSequences[] = {\n";
  unsigned Idx = 0;
  for (size_t s = 0, se = Sequences.size(); s != se; ++s) {
    OS << "    ";
    const SmallVectorImpl<MaskRolPair> &Sequence = Sequences[s];
    for (size_t p = 0, pe = Sequence.size(); p != pe; ++p) {
      const MaskRolPair &P = Sequence[p];
      printMask(OS << "{ ", P.Mask);
      OS << format(", %2u }, ", P.RotateLeft);
    }
    OS << "{ LaneBitmask::getNone(), 0 }";
    if (s+1 != se)
      OS << ", ";
    OS << "  // Sequence " << Idx << "\n";
    Idx += Sequence.size() + 1;
  }
  OS << "  };\n"
        "  static const MaskRolOp *const CompositeSequences[] = {\n";
  for (size_t i = 0, e = SubRegIndices.size(); i != e; ++i) {
    OS << "    ";
    unsigned Idx = SubReg2SequenceIndexMap[i];
    OS << format("&LaneMaskComposeSequences[%u]", Idx);
    if (i+1 != e)
      OS << ",";
    OS << " // to " << SubRegIndices[i].getName() << "\n";
  }
  OS << "  };\n\n";

  OS << "LaneBitmask " << ClName
     << "::composeSubRegIndexLaneMaskImpl(unsigned IdxA, LaneBitmask LaneMask)"
        " const {\n"
        "  --IdxA; assert(IdxA < " << SubRegIndices.size()
     << " && \"Subregister index out of bounds\");\n"
        "  LaneBitmask Result;\n"
        "  for (const MaskRolOp *Ops = CompositeSequences[IdxA]; Ops->Mask.any(); ++Ops) {\n"
        "    LaneBitmask::Type M = LaneMask.getAsInteger() & Ops->Mask.getAsInteger();\n"
        "    if (unsigned S = Ops->RotateLeft)\n"
        "      Result |= LaneBitmask((M << S) | (M >> (LaneBitmask::BitWidth - S)));\n"
        "    else\n"
        "      Result |= LaneBitmask(M);\n"
        "  }\n"
        "  return Result;\n"
        "}\n\n";

  OS << "LaneBitmask " << ClName
     << "::reverseComposeSubRegIndexLaneMaskImpl(unsigned IdxA, "
        " LaneBitmask LaneMask) const {\n"
        "  LaneMask &= getSubRegIndexLaneMask(IdxA);\n"
        "  --IdxA; assert(IdxA < " << SubRegIndices.size()
     << " && \"Subregister index out of bounds\");\n"
        "  LaneBitmask Result;\n"
        "  for (const MaskRolOp *Ops = CompositeSequences[IdxA]; Ops->Mask.any(); ++Ops) {\n"
        "    LaneBitmask::Type M = LaneMask.getAsInteger();\n"
        "    if (unsigned S = Ops->RotateLeft)\n"
        "      Result |= LaneBitmask((M >> S) | (M << (LaneBitmask::BitWidth - S)));\n"
        "    else\n"
        "      Result |= LaneBitmask(M);\n"
        "  }\n"
        "  return Result;\n"
        "}\n\n";
}

//
// runMCDesc - Print out MC register descriptions.
//
void
RegisterInfoEmitter::runMCDesc(raw_ostream &OS, CodeGenTarget &Target,
                               CodeGenRegBank &RegBank) {
  emitSourceFileHeader("MC Register Information", OS);

  OS << "\n#ifdef GET_REGINFO_MC_DESC\n";
  OS << "#undef GET_REGINFO_MC_DESC\n\n";

  const auto &Regs = RegBank.getRegisters();

  auto &SubRegIndices = RegBank.getSubRegIndices();
  // The lists of sub-registers and super-registers go in the same array.  That
  // allows us to share suffixes.
  typedef std::vector<const CodeGenRegister*> RegVec;

  // Differentially encoded lists.
  SequenceToOffsetTable<DiffVec> DiffSeqs;
  SmallVector<DiffVec, 4> SubRegLists(Regs.size());
  SmallVector<DiffVec, 4> SuperRegLists(Regs.size());
  SmallVector<DiffVec, 4> RegUnitLists(Regs.size());
  SmallVector<unsigned, 4> RegUnitInitScale(Regs.size());

  // List of lane masks accompanying register unit sequences.
  SequenceToOffsetTable<MaskVec> LaneMaskSeqs;
  SmallVector<MaskVec, 4> RegUnitLaneMasks(Regs.size());

  // Keep track of sub-register names as well. These are not differentially
  // encoded.
  typedef SmallVector<const CodeGenSubRegIndex*, 4> SubRegIdxVec;
  SequenceToOffsetTable<SubRegIdxVec, deref<llvm::less>> SubRegIdxSeqs;
  SmallVector<SubRegIdxVec, 4> SubRegIdxLists(Regs.size());

  SequenceToOffsetTable<std::string> RegStrings;

  // Precompute register lists for the SequenceToOffsetTable.
  unsigned i = 0;
  for (auto I = Regs.begin(), E = Regs.end(); I != E; ++I, ++i) {
    const auto &Reg = *I;
    RegStrings.add(Reg.getName());

    // Compute the ordered sub-register list.
    SetVector<const CodeGenRegister*> SR;
    Reg.addSubRegsPreOrder(SR, RegBank);
    diffEncode(SubRegLists[i], Reg.EnumValue, SR.begin(), SR.end());
    DiffSeqs.add(SubRegLists[i]);

    // Compute the corresponding sub-register indexes.
    SubRegIdxVec &SRIs = SubRegIdxLists[i];
    for (const CodeGenRegister *S : SR)
      SRIs.push_back(Reg.getSubRegIndex(S));
    SubRegIdxSeqs.add(SRIs);

    // Super-registers are already computed.
    const RegVec &SuperRegList = Reg.getSuperRegs();
    diffEncode(SuperRegLists[i], Reg.EnumValue, SuperRegList.begin(),
               SuperRegList.end());
    DiffSeqs.add(SuperRegLists[i]);

    // Differentially encode the register unit list, seeded by register number.
    // First compute a scale factor that allows more diff-lists to be reused:
    //
    //   D0 -> (S0, S1)
    //   D1 -> (S2, S3)
    //
    // A scale factor of 2 allows D0 and D1 to share a diff-list. The initial
    // value for the differential decoder is the register number multiplied by
    // the scale.
    //
    // Check the neighboring registers for arithmetic progressions.
    unsigned ScaleA = ~0u, ScaleB = ~0u;
    SparseBitVector<> RUs = Reg.getNativeRegUnits();
    if (I != Regs.begin() &&
        std::prev(I)->getNativeRegUnits().count() == RUs.count())
      ScaleB = *RUs.begin() - *std::prev(I)->getNativeRegUnits().begin();
    if (std::next(I) != Regs.end() &&
        std::next(I)->getNativeRegUnits().count() == RUs.count())
      ScaleA = *std::next(I)->getNativeRegUnits().begin() - *RUs.begin();
    unsigned Scale = std::min(ScaleB, ScaleA);
    // Default the scale to 0 if it can't be encoded in 4 bits.
    if (Scale >= 16)
      Scale = 0;
    RegUnitInitScale[i] = Scale;
    DiffSeqs.add(diffEncode(RegUnitLists[i], Scale * Reg.EnumValue, RUs));

    const auto &RUMasks = Reg.getRegUnitLaneMasks();
    MaskVec &LaneMaskVec = RegUnitLaneMasks[i];
    assert(LaneMaskVec.empty());
    LaneMaskVec.insert(LaneMaskVec.begin(), RUMasks.begin(), RUMasks.end());
    // Terminator mask should not be used inside of the list.
#ifndef NDEBUG
    for (LaneBitmask M : LaneMaskVec) {
      assert(!M.all() && "terminator mask should not be part of the list");
    }
#endif
    LaneMaskSeqs.add(LaneMaskVec);
  }

  // Compute the final layout of the sequence table.
  DiffSeqs.layout();
  LaneMaskSeqs.layout();
  SubRegIdxSeqs.layout();

  OS << "namespace llvm {\n\n";

  const std::string &TargetName = Target.getName();

  // Emit the shared table of differential lists.
  OS << "extern const MCPhysReg " << TargetName << "RegDiffLists[] = {\n";
  DiffSeqs.emit(OS, printDiff16);
  OS << "};\n\n";

  // Emit the shared table of regunit lane mask sequences.
  OS << "extern const LaneBitmask " << TargetName << "LaneMaskLists[] = {\n";
  LaneMaskSeqs.emit(OS, printMask, "LaneBitmask::getAll()");
  OS << "};\n\n";

  // Emit the table of sub-register indexes.
  OS << "extern const uint16_t " << TargetName << "SubRegIdxLists[] = {\n";
  SubRegIdxSeqs.emit(OS, printSubRegIndex);
  OS << "};\n\n";

  // Emit the table of sub-register index sizes.
  OS << "extern const MCRegisterInfo::SubRegCoveredBits "
     << TargetName << "SubRegIdxRanges[] = {\n";
  OS << "  { " << (uint16_t)-1 << ", " << (uint16_t)-1 << " },\n";
  for (const auto &Idx : SubRegIndices) {
    OS << "  { " << Idx.Offset << ", " << Idx.Size << " },\t// "
       << Idx.getName() << "\n";
  }
  OS << "};\n\n";

  // Emit the string table.
  RegStrings.layout();
  OS << "extern const char " << TargetName << "RegStrings[] = {\n";
  RegStrings.emit(OS, printChar);
  OS << "};\n\n";

  OS << "extern const MCRegisterDesc " << TargetName
     << "RegDesc[] = { // Descriptors\n";
  OS << "  { " << RegStrings.get("") << ", 0, 0, 0, 0, 0 },\n";

  // Emit the register descriptors now.
  i = 0;
  for (const auto &Reg : Regs) {
    OS << "  { " << RegStrings.get(Reg.getName()) << ", "
       << DiffSeqs.get(SubRegLists[i]) << ", " << DiffSeqs.get(SuperRegLists[i])
       << ", " << SubRegIdxSeqs.get(SubRegIdxLists[i]) << ", "
       << (DiffSeqs.get(RegUnitLists[i]) * 16 + RegUnitInitScale[i]) << ", "
       << LaneMaskSeqs.get(RegUnitLaneMasks[i]) << " },\n";
    ++i;
  }
  OS << "};\n\n";      // End of register descriptors...

  // Emit the table of register unit roots. Each regunit has one or two root
  // registers.
  OS << "extern const MCPhysReg " << TargetName << "RegUnitRoots[][2] = {\n";
  for (unsigned i = 0, e = RegBank.getNumNativeRegUnits(); i != e; ++i) {
    ArrayRef<const CodeGenRegister*> Roots = RegBank.getRegUnit(i).getRoots();
    assert(!Roots.empty() && "All regunits must have a root register.");
    assert(Roots.size() <= 2 && "More than two roots not supported yet.");
    OS << "  { " << getQualifiedName(Roots.front()->TheDef);
    for (unsigned r = 1; r != Roots.size(); ++r)
      OS << ", " << getQualifiedName(Roots[r]->TheDef);
    OS << " },\n";
  }
  OS << "};\n\n";

  const auto &RegisterClasses = RegBank.getRegClasses();

  // Loop over all of the register classes... emitting each one.
  OS << "namespace {     // Register classes...\n";

  SequenceToOffsetTable<std::string> RegClassStrings;

  // Emit the register enum value arrays for each RegisterClass
  for (const auto &RC : RegisterClasses) {
    ArrayRef<Record*> Order = RC.getOrder();

    // Give the register class a legal C name if it's anonymous.
    const std::string &Name = RC.getName();

    RegClassStrings.add(Name);

    // Emit the register list now.
    OS << "  // " << Name << " Register Class...\n"
       << "  const MCPhysReg " << Name
       << "[] = {\n    ";
    for (Record *Reg : Order) {
      OS << getQualifiedName(Reg) << ", ";
    }
    OS << "\n  };\n\n";

    OS << "  // " << Name << " Bit set.\n"
       << "  const uint8_t " << Name
       << "Bits[] = {\n    ";
    BitVectorEmitter BVE;
    for (Record *Reg : Order) {
      BVE.add(Target.getRegBank().getReg(Reg)->EnumValue);
    }
    BVE.print(OS);
    OS << "\n  };\n\n";

  }
  OS << "} // end anonymous namespace\n\n";

  RegClassStrings.layout();
  OS << "extern const char " << TargetName << "RegClassStrings[] = {\n";
  RegClassStrings.emit(OS, printChar);
  OS << "};\n\n";

  OS << "extern const MCRegisterClass " << TargetName
     << "MCRegisterClasses[] = {\n";

  for (const auto &RC : RegisterClasses) {
    assert(isInt<8>(RC.CopyCost) && "Copy cost too large.");
    OS << "  { " << RC.getName() << ", " << RC.getName() << "Bits, "
       << RegClassStrings.get(RC.getName()) << ", "
       << RC.getOrder().size() << ", sizeof(" << RC.getName() << "Bits), "
       << RC.getQualifiedName() + "RegClassID" << ", "
       << RC.CopyCost << ", "
       << ( RC.Allocatable ? "true" : "false" ) << " },\n";
  }

  OS << "};\n\n";

  EmitRegMappingTables(OS, Regs, false);

  // Emit Reg encoding table
  OS << "extern const uint16_t " << TargetName;
  OS << "RegEncodingTable[] = {\n";
  // Add entry for NoRegister
  OS << "  0,\n";
  for (const auto &RE : Regs) {
    Record *Reg = RE.TheDef;
    BitsInit *BI = Reg->getValueAsBitsInit("HWEncoding");
    uint64_t Value = 0;
    for (unsigned b = 0, be = BI->getNumBits(); b != be; ++b) {
      if (BitInit *B = dyn_cast<BitInit>(BI->getBit(b)))
        Value |= (uint64_t)B->getValue() << b;
    }
    OS << "  " << Value << ",\n";
  }
  OS << "};\n";       // End of HW encoding table

  // MCRegisterInfo initialization routine.
  OS << "static inline void Init" << TargetName
     << "MCRegisterInfo(MCRegisterInfo *RI, unsigned RA, "
     << "unsigned DwarfFlavour = 0, unsigned EHFlavour = 0, unsigned PC = 0) "
        "{\n"
     << "  RI->InitMCRegisterInfo(" << TargetName << "RegDesc, "
     << Regs.size() + 1 << ", RA, PC, " << TargetName << "MCRegisterClasses, "
     << RegisterClasses.size() << ", " << TargetName << "RegUnitRoots, "
     << RegBank.getNumNativeRegUnits() << ", " << TargetName << "RegDiffLists, "
     << TargetName << "LaneMaskLists, " << TargetName << "RegStrings, "
     << TargetName << "RegClassStrings, " << TargetName << "SubRegIdxLists, "
     << (std::distance(SubRegIndices.begin(), SubRegIndices.end()) + 1) << ",\n"
     << TargetName << "SubRegIdxRanges, " << TargetName
     << "RegEncodingTable);\n\n";

  EmitRegMapping(OS, Regs, false);

  OS << "}\n\n";

  OS << "} // end namespace llvm\n\n";
  OS << "#endif // GET_REGINFO_MC_DESC\n\n";
}

void
RegisterInfoEmitter::runTargetHeader(raw_ostream &OS, CodeGenTarget &Target,
                                     CodeGenRegBank &RegBank) {
  emitSourceFileHeader("Register Information Header Fragment", OS);

  OS << "\n#ifdef GET_REGINFO_HEADER\n";
  OS << "#undef GET_REGINFO_HEADER\n\n";

  const std::string &TargetName = Target.getName();
  std::string ClassName = TargetName + "GenRegisterInfo";

  OS << "#include \"llvm/CodeGen/TargetRegisterInfo.h\"\n\n";

  OS << "namespace llvm {\n\n";

  OS << "class " << TargetName << "FrameLowering;\n\n";

  OS << "struct " << ClassName << " : public TargetRegisterInfo {\n"
     << "  explicit " << ClassName
     << "(unsigned RA, unsigned D = 0, unsigned E = 0,\n"
     << "      unsigned PC = 0, unsigned HwMode = 0);\n";
  if (!RegBank.getSubRegIndices().empty()) {
    OS << "  unsigned composeSubRegIndicesImpl"
       << "(unsigned, unsigned) const override;\n"
       << "  LaneBitmask composeSubRegIndexLaneMaskImpl"
       << "(unsigned, LaneBitmask) const override;\n"
       << "  LaneBitmask reverseComposeSubRegIndexLaneMaskImpl"
       << "(unsigned, LaneBitmask) const override;\n"
       << "  const TargetRegisterClass *getSubClassWithSubReg"
       << "(const TargetRegisterClass*, unsigned) const override;\n";
  }
  OS << "  const RegClassWeight &getRegClassWeight("
     << "const TargetRegisterClass *RC) const override;\n"
     << "  unsigned getRegUnitWeight(unsigned RegUnit) const override;\n"
     << "  unsigned getNumRegPressureSets() const override;\n"
     << "  const char *getRegPressureSetName(unsigned Idx) const override;\n"
     << "  unsigned getRegPressureSetLimit(const MachineFunction &MF, unsigned "
        "Idx) const override;\n"
     << "  const int *getRegClassPressureSets("
     << "const TargetRegisterClass *RC) const override;\n"
     << "  const int *getRegUnitPressureSets("
     << "unsigned RegUnit) const override;\n"
     << "  ArrayRef<const char *> getRegMaskNames() const override;\n"
     << "  ArrayRef<const uint32_t *> getRegMasks() const override;\n"
     << "  /// Devirtualized TargetFrameLowering.\n"
     << "  static const " << TargetName << "FrameLowering *getFrameLowering(\n"
     << "      const MachineFunction &MF);\n"
     << "};\n\n";

  const auto &RegisterClasses = RegBank.getRegClasses();

  if (!RegisterClasses.empty()) {
    OS << "namespace " << RegisterClasses.front().Namespace
       << " { // Register classes\n";

    for (const auto &RC : RegisterClasses) {
      const std::string &Name = RC.getName();

      // Output the extern for the instance.
      OS << "  extern const TargetRegisterClass " << Name << "RegClass;\n";
    }
    OS << "} // end namespace " << RegisterClasses.front().Namespace << "\n\n";
  }
  OS << "} // end namespace llvm\n\n";
  OS << "#endif // GET_REGINFO_HEADER\n\n";
}

//
// runTargetDesc - Output the target register and register file descriptions.
//
void
RegisterInfoEmitter::runTargetDesc(raw_ostream &OS, CodeGenTarget &Target,
                                   CodeGenRegBank &RegBank){
  emitSourceFileHeader("Target Register and Register Classes Information", OS);

  OS << "\n#ifdef GET_REGINFO_TARGET_DESC\n";
  OS << "#undef GET_REGINFO_TARGET_DESC\n\n";

  OS << "namespace llvm {\n\n";

  // Get access to MCRegisterClass data.
  OS << "extern const MCRegisterClass " << Target.getName()
     << "MCRegisterClasses[];\n";

  // Start out by emitting each of the register classes.
  const auto &RegisterClasses = RegBank.getRegClasses();
  const auto &SubRegIndices = RegBank.getSubRegIndices();

  // Collect all registers belonging to any allocatable class.
  std::set<Record*> AllocatableRegs;

  // Collect allocatable registers.
  for (const auto &RC : RegisterClasses) {
    ArrayRef<Record*> Order = RC.getOrder();

    if (RC.Allocatable)
      AllocatableRegs.insert(Order.begin(), Order.end());
  }

  const CodeGenHwModes &CGH = Target.getHwModes();
  unsigned NumModes = CGH.getNumModeIds();

  // Build a shared array of value types.
  SequenceToOffsetTable<std::vector<MVT::SimpleValueType>> VTSeqs;
  for (unsigned M = 0; M < NumModes; ++M) {
    for (const auto &RC : RegisterClasses) {
      std::vector<MVT::SimpleValueType> S;
      for (const ValueTypeByHwMode &VVT : RC.VTs)
        S.push_back(VVT.get(M).SimpleTy);
      VTSeqs.add(S);
    }
  }
  VTSeqs.layout();
  OS << "\nstatic const MVT::SimpleValueType VTLists[] = {\n";
  VTSeqs.emit(OS, printSimpleValueType, "MVT::Other");
  OS << "};\n";

  // Emit SubRegIndex names, skipping 0.
  OS << "\nstatic const char *const SubRegIndexNameTable[] = { \"";

  for (const auto &Idx : SubRegIndices) {
    OS << Idx.getName();
    OS << "\", \"";
  }
  OS << "\" };\n\n";

  // Emit SubRegIndex lane masks, including 0.
  OS << "\nstatic const LaneBitmask SubRegIndexLaneMaskTable[] = {\n  "
        "LaneBitmask::getAll(),\n";
  for (const auto &Idx : SubRegIndices) {
    printMask(OS << "  ", Idx.LaneMask);
    OS << ", // " << Idx.getName() << '\n';
  }
  OS << " };\n\n";

  OS << "\n";

  // Now that all of the structs have been emitted, emit the instances.
  if (!RegisterClasses.empty()) {
    OS << "\nstatic const TargetRegisterInfo::RegClassInfo RegClassInfos[]"
       << " = {\n";
    for (unsigned M = 0; M < NumModes; ++M) {
      unsigned EV = 0;
      OS << "  // Mode = " << M << " (";
      if (M == 0)
        OS << "Default";
      else
        OS << CGH.getMode(M).Name;
      OS << ")\n";
      for (const auto &RC : RegisterClasses) {
        assert(RC.EnumValue == EV++ && "Unexpected order of register classes");
        (void)EV;
        const RegSizeInfo &RI = RC.RSI.get(M);
        OS << "  { " << RI.RegSize << ", " << RI.SpillSize << ", "
           << RI.SpillAlignment;
        std::vector<MVT::SimpleValueType> VTs;
        for (const ValueTypeByHwMode &VVT : RC.VTs)
          VTs.push_back(VVT.get(M).SimpleTy);
        OS << ", VTLists+" << VTSeqs.get(VTs) << " },    // "
           << RC.getName() << '\n';
      }
    }
    OS << "};\n";


    OS << "\nstatic const TargetRegisterClass *const "
       << "NullRegClasses[] = { nullptr };\n\n";

    // Emit register class bit mask tables. The first bit mask emitted for a
    // register class, RC, is the set of sub-classes, including RC itself.
    //
    // If RC has super-registers, also create a list of subreg indices and bit
    // masks, (Idx, Mask). The bit mask has a bit for every superreg regclass,
    // SuperRC, that satisfies:
    //
    //   For all SuperReg in SuperRC: SuperReg:Idx in RC
    //
    // The 0-terminated list of subreg indices starts at:
    //
    //   RC->getSuperRegIndices() = SuperRegIdxSeqs + ...
    //
    // The corresponding bitmasks follow the sub-class mask in memory. Each
    // mask has RCMaskWords uint32_t entries.
    //
    // Every bit mask present in the list has at least one bit set.

    // Compress the sub-reg index lists.
    typedef std::vector<const CodeGenSubRegIndex*> IdxList;
    SmallVector<IdxList, 8> SuperRegIdxLists(RegisterClasses.size());
    SequenceToOffsetTable<IdxList, deref<llvm::less>> SuperRegIdxSeqs;
    BitVector MaskBV(RegisterClasses.size());

    for (const auto &RC : RegisterClasses) {
      OS << "static const uint32_t " << RC.getName()
         << "SubClassMask[] = {\n  ";
      printBitVectorAsHex(OS, RC.getSubClasses(), 32);

      // Emit super-reg class masks for any relevant SubRegIndices that can
      // project into RC.
      IdxList &SRIList = SuperRegIdxLists[RC.EnumValue];
      for (auto &Idx : SubRegIndices) {
        MaskBV.reset();
        RC.getSuperRegClasses(&Idx, MaskBV);
        if (MaskBV.none())
          continue;
        SRIList.push_back(&Idx);
        OS << "\n  ";
        printBitVectorAsHex(OS, MaskBV, 32);
        OS << "// " << Idx.getName();
      }
      SuperRegIdxSeqs.add(SRIList);
      OS << "\n};\n\n";
    }

    OS << "static const uint16_t SuperRegIdxSeqs[] = {\n";
    SuperRegIdxSeqs.layout();
    SuperRegIdxSeqs.emit(OS, printSubRegIndex);
    OS << "};\n\n";

    // Emit NULL terminated super-class lists.
    for (const auto &RC : RegisterClasses) {
      ArrayRef<CodeGenRegisterClass*> Supers = RC.getSuperClasses();

      // Skip classes without supers.  We can reuse NullRegClasses.
      if (Supers.empty())
        continue;

      OS << "static const TargetRegisterClass *const "
         << RC.getName() << "Superclasses[] = {\n";
      for (const auto *Super : Supers)
        OS << "  &" << Super->getQualifiedName() << "RegClass,\n";
      OS << "  nullptr\n};\n\n";
    }

    // Emit methods.
    for (const auto &RC : RegisterClasses) {
      if (!RC.AltOrderSelect.empty()) {
        OS << "\nstatic inline unsigned " << RC.getName()
           << "AltOrderSelect(const MachineFunction &MF) {"
           << RC.AltOrderSelect << "}\n\n"
           << "static ArrayRef<MCPhysReg> " << RC.getName()
           << "GetRawAllocationOrder(const MachineFunction &MF) {\n";
        for (unsigned oi = 1 , oe = RC.getNumOrders(); oi != oe; ++oi) {
          ArrayRef<Record*> Elems = RC.getOrder(oi);
          if (!Elems.empty()) {
            OS << "  static const MCPhysReg AltOrder" << oi << "[] = {";
            for (unsigned elem = 0; elem != Elems.size(); ++elem)
              OS << (elem ? ", " : " ") << getQualifiedName(Elems[elem]);
            OS << " };\n";
          }
        }
        OS << "  const MCRegisterClass &MCR = " << Target.getName()
           << "MCRegisterClasses[" << RC.getQualifiedName() + "RegClassID];\n"
           << "  const ArrayRef<MCPhysReg> Order[] = {\n"
           << "    makeArrayRef(MCR.begin(), MCR.getNumRegs()";
        for (unsigned oi = 1, oe = RC.getNumOrders(); oi != oe; ++oi)
          if (RC.getOrder(oi).empty())
            OS << "),\n    ArrayRef<MCPhysReg>(";
          else
            OS << "),\n    makeArrayRef(AltOrder" << oi;
        OS << ")\n  };\n  const unsigned Select = " << RC.getName()
           << "AltOrderSelect(MF);\n  assert(Select < " << RC.getNumOrders()
           << ");\n  return Order[Select];\n}\n";
      }
    }

    // Now emit the actual value-initialized register class instances.
    OS << "\nnamespace " << RegisterClasses.front().Namespace
       << " {   // Register class instances\n";

    for (const auto &RC : RegisterClasses) {
      OS << "  extern const TargetRegisterClass " << RC.getName()
         << "RegClass = {\n    " << '&' << Target.getName()
         << "MCRegisterClasses[" << RC.getName() << "RegClassID],\n    "
         << RC.getName() << "SubClassMask,\n    SuperRegIdxSeqs + "
         << SuperRegIdxSeqs.get(SuperRegIdxLists[RC.EnumValue]) << ",\n    ";
      printMask(OS, RC.LaneMask);
      OS << ",\n    " << (unsigned)RC.AllocationPriority << ",\n    "
         << (RC.HasDisjunctSubRegs?"true":"false")
         << ", /* HasDisjunctSubRegs */\n    "
         << (RC.CoveredBySubRegs?"true":"false")
         << ", /* CoveredBySubRegs */\n    ";
      if (RC.getSuperClasses().empty())
        OS << "NullRegClasses,\n    ";
      else
        OS << RC.getName() << "Superclasses,\n    ";
      if (RC.AltOrderSelect.empty())
        OS << "nullptr\n";
      else
        OS << RC.getName() << "GetRawAllocationOrder\n";
      OS << "  };\n\n";
    }

    OS << "} // end namespace " << RegisterClasses.front().Namespace << "\n";
  }

  OS << "\nnamespace {\n";
  OS << "  const TargetRegisterClass* const RegisterClasses[] = {\n";
  for (const auto &RC : RegisterClasses)
    OS << "    &" << RC.getQualifiedName() << "RegClass,\n";
  OS << "  };\n";
  OS << "} // end anonymous namespace\n";

  // Emit extra information about registers.
  const std::string &TargetName = Target.getName();
  OS << "\nstatic const TargetRegisterInfoDesc "
     << TargetName << "RegInfoDesc[] = { // Extra Descriptors\n";
  OS << "  { 0, false },\n";

  const auto &Regs = RegBank.getRegisters();
  for (const auto &Reg : Regs) {
    OS << "  { ";
    OS << Reg.CostPerUse << ", "
       << ( AllocatableRegs.count(Reg.TheDef) != 0 ? "true" : "false" )
       << " },\n";
  }
  OS << "};\n";      // End of register descriptors...


  std::string ClassName = Target.getName().str() + "GenRegisterInfo";

  auto SubRegIndicesSize =
      std::distance(SubRegIndices.begin(), SubRegIndices.end());

  if (!SubRegIndices.empty()) {
    emitComposeSubRegIndices(OS, RegBank, ClassName);
    emitComposeSubRegIndexLaneMask(OS, RegBank, ClassName);
  }

  // Emit getSubClassWithSubReg.
  if (!SubRegIndices.empty()) {
    OS << "const TargetRegisterClass *" << ClassName
       << "::getSubClassWithSubReg(const TargetRegisterClass *RC, unsigned Idx)"
       << " const {\n";
    // Use the smallest type that can hold a regclass ID with room for a
    // sentinel.
    if (RegisterClasses.size() < UINT8_MAX)
      OS << "  static const uint8_t Table[";
    else if (RegisterClasses.size() < UINT16_MAX)
      OS << "  static const uint16_t Table[";
    else
      PrintFatalError("Too many register classes.");
    OS << RegisterClasses.size() << "][" << SubRegIndicesSize << "] = {\n";
    for (const auto &RC : RegisterClasses) {
      OS << "    {\t// " << RC.getName() << "\n";
      for (auto &Idx : SubRegIndices) {
        if (CodeGenRegisterClass *SRC = RC.getSubClassWithSubReg(&Idx))
          OS << "      " << SRC->EnumValue + 1 << ",\t// " << Idx.getName()
             << " -> " << SRC->getName() << "\n";
        else
          OS << "      0,\t// " << Idx.getName() << "\n";
      }
      OS << "    },\n";
    }
    OS << "  };\n  assert(RC && \"Missing regclass\");\n"
       << "  if (!Idx) return RC;\n  --Idx;\n"
       << "  assert(Idx < " << SubRegIndicesSize << " && \"Bad subreg\");\n"
       << "  unsigned TV = Table[RC->getID()][Idx];\n"
       << "  return TV ? getRegClass(TV - 1) : nullptr;\n}\n\n";
  }

  EmitRegUnitPressure(OS, RegBank, ClassName);

  // Emit the constructor of the class...
  OS << "extern const MCRegisterDesc " << TargetName << "RegDesc[];\n";
  OS << "extern const MCPhysReg " << TargetName << "RegDiffLists[];\n";
  OS << "extern const LaneBitmask " << TargetName << "LaneMaskLists[];\n";
  OS << "extern const char " << TargetName << "RegStrings[];\n";
  OS << "extern const char " << TargetName << "RegClassStrings[];\n";
  OS << "extern const MCPhysReg " << TargetName << "RegUnitRoots[][2];\n";
  OS << "extern const uint16_t " << TargetName << "SubRegIdxLists[];\n";
  OS << "extern const MCRegisterInfo::SubRegCoveredBits "
     << TargetName << "SubRegIdxRanges[];\n";
  OS << "extern const uint16_t " << TargetName << "RegEncodingTable[];\n";

  EmitRegMappingTables(OS, Regs, true);

  OS << ClassName << "::\n" << ClassName
     << "(unsigned RA, unsigned DwarfFlavour, unsigned EHFlavour,\n"
        "      unsigned PC, unsigned HwMode)\n"
     << "  : TargetRegisterInfo(" << TargetName << "RegInfoDesc"
     << ", RegisterClasses, RegisterClasses+" << RegisterClasses.size() << ",\n"
     << "             SubRegIndexNameTable, SubRegIndexLaneMaskTable,\n"
     << "             ";
  printMask(OS, RegBank.CoveringLanes);
  OS << ", RegClassInfos, HwMode) {\n"
     << "  InitMCRegisterInfo(" << TargetName << "RegDesc, " << Regs.size() + 1
     << ", RA, PC,\n                     " << TargetName
     << "MCRegisterClasses, " << RegisterClasses.size() << ",\n"
     << "                     " << TargetName << "RegUnitRoots,\n"
     << "                     " << RegBank.getNumNativeRegUnits() << ",\n"
     << "                     " << TargetName << "RegDiffLists,\n"
     << "                     " << TargetName << "LaneMaskLists,\n"
     << "                     " << TargetName << "RegStrings,\n"
     << "                     " << TargetName << "RegClassStrings,\n"
     << "                     " << TargetName << "SubRegIdxLists,\n"
     << "                     " << SubRegIndicesSize + 1 << ",\n"
     << "                     " << TargetName << "SubRegIdxRanges,\n"
     << "                     " << TargetName << "RegEncodingTable);\n\n";

  EmitRegMapping(OS, Regs, true);

  OS << "}\n\n";

  // Emit CalleeSavedRegs information.
  std::vector<Record*> CSRSets =
    Records.getAllDerivedDefinitions("CalleeSavedRegs");
  for (unsigned i = 0, e = CSRSets.size(); i != e; ++i) {
    Record *CSRSet = CSRSets[i];
    const SetTheory::RecVec *Regs = RegBank.getSets().expand(CSRSet);
    assert(Regs && "Cannot expand CalleeSavedRegs instance");

    // Emit the *_SaveList list of callee-saved registers.
    OS << "static const MCPhysReg " << CSRSet->getName()
       << "_SaveList[] = { ";
    for (unsigned r = 0, re = Regs->size(); r != re; ++r)
      OS << getQualifiedName((*Regs)[r]) << ", ";
    OS << "0 };\n";

    // Emit the *_RegMask bit mask of call-preserved registers.
    BitVector Covered = RegBank.computeCoveredRegisters(*Regs);

    // Check for an optional OtherPreserved set.
    // Add those registers to RegMask, but not to SaveList.
    if (DagInit *OPDag =
        dyn_cast<DagInit>(CSRSet->getValueInit("OtherPreserved"))) {
      SetTheory::RecSet OPSet;
      RegBank.getSets().evaluate(OPDag, OPSet, CSRSet->getLoc());
      Covered |= RegBank.computeCoveredRegisters(
        ArrayRef<Record*>(OPSet.begin(), OPSet.end()));
    }

    OS << "static const uint32_t " << CSRSet->getName()
       << "_RegMask[] = { ";
    printBitVectorAsHex(OS, Covered, 32);
    OS << "};\n";
  }
  OS << "\n\n";

  OS << "ArrayRef<const uint32_t *> " << ClassName
     << "::getRegMasks() const {\n";
  if (!CSRSets.empty()) {
    OS << "  static const uint32_t *const Masks[] = {\n";
    for (Record *CSRSet : CSRSets)
      OS << "    " << CSRSet->getName() << "_RegMask,\n";
    OS << "  };\n";
    OS << "  return makeArrayRef(Masks);\n";
  } else {
    OS << "  return None;\n";
  }
  OS << "}\n\n";

  OS << "ArrayRef<const char *> " << ClassName
     << "::getRegMaskNames() const {\n";
  if (!CSRSets.empty()) {
  OS << "  static const char *const Names[] = {\n";
    for (Record *CSRSet : CSRSets)
      OS << "    " << '"' << CSRSet->getName() << '"' << ",\n";
    OS << "  };\n";
    OS << "  return makeArrayRef(Names);\n";
  } else {
    OS << "  return None;\n";
  }
  OS << "}\n\n";

  OS << "const " << TargetName << "FrameLowering *\n" << TargetName
     << "GenRegisterInfo::getFrameLowering(const MachineFunction &MF) {\n"
     << "  return static_cast<const " << TargetName << "FrameLowering *>(\n"
     << "      MF.getSubtarget().getFrameLowering());\n"
     << "}\n\n";

  OS << "} // end namespace llvm\n\n";
  OS << "#endif // GET_REGINFO_TARGET_DESC\n\n";
}

void RegisterInfoEmitter::run(raw_ostream &OS) {
  CodeGenRegBank &RegBank = Target.getRegBank();
  runEnums(OS, Target, RegBank);
  runMCDesc(OS, Target, RegBank);
  runTargetHeader(OS, Target, RegBank);
  runTargetDesc(OS, Target, RegBank);

  if (RegisterInfoDebug)
    debugDump(errs());
}

void RegisterInfoEmitter::debugDump(raw_ostream &OS) {
  CodeGenRegBank &RegBank = Target.getRegBank();
  const CodeGenHwModes &CGH = Target.getHwModes();
  unsigned NumModes = CGH.getNumModeIds();
  auto getModeName = [CGH] (unsigned M) -> StringRef {
    if (M == 0)
      return "Default";
    return CGH.getMode(M).Name;
  };

  for (const CodeGenRegisterClass &RC : RegBank.getRegClasses()) {
    OS << "RegisterClass " << RC.getName() << ":\n";
    OS << "\tSpillSize: {";
    for (unsigned M = 0; M != NumModes; ++M)
      OS << ' ' << getModeName(M) << ':' << RC.RSI.get(M).SpillSize;
    OS << " }\n\tSpillAlignment: {";
    for (unsigned M = 0; M != NumModes; ++M)
      OS << ' ' << getModeName(M) << ':' << RC.RSI.get(M).SpillAlignment;
    OS << " }\n\tNumRegs: " << RC.getMembers().size() << '\n';
    OS << "\tLaneMask: " << PrintLaneMask(RC.LaneMask) << '\n';
    OS << "\tHasDisjunctSubRegs: " << RC.HasDisjunctSubRegs << '\n';
    OS << "\tCoveredBySubRegs: " << RC.CoveredBySubRegs << '\n';
    OS << "\tRegs:";
    for (const CodeGenRegister *R : RC.getMembers()) {
      OS << " " << R->getName();
    }
    OS << '\n';
    OS << "\tSubClasses:";
    const BitVector &SubClasses = RC.getSubClasses();
    for (const CodeGenRegisterClass &SRC : RegBank.getRegClasses()) {
      if (!SubClasses.test(SRC.EnumValue))
        continue;
      OS << " " << SRC.getName();
    }
    OS << '\n';
    OS << "\tSuperClasses:";
    for (const CodeGenRegisterClass *SRC : RC.getSuperClasses()) {
      OS << " " << SRC->getName();
    }
    OS << '\n';
  }

  for (const CodeGenSubRegIndex &SRI : RegBank.getSubRegIndices()) {
    OS << "SubRegIndex " << SRI.getName() << ":\n";
    OS << "\tLaneMask: " << PrintLaneMask(SRI.LaneMask) << '\n';
    OS << "\tAllSuperRegsCovered: " << SRI.AllSuperRegsCovered << '\n';
  }

  for (const CodeGenRegister &R : RegBank.getRegisters()) {
    OS << "Register " << R.getName() << ":\n";
    OS << "\tCostPerUse: " << R.CostPerUse << '\n';
    OS << "\tCoveredBySubregs: " << R.CoveredBySubRegs << '\n';
    OS << "\tHasDisjunctSubRegs: " << R.HasDisjunctSubRegs << '\n';
    for (std::pair<CodeGenSubRegIndex*,CodeGenRegister*> P : R.getSubRegs()) {
      OS << "\tSubReg " << P.first->getName()
         << " = " << P.second->getName() << '\n';
    }
  }
}

namespace llvm {

void EmitRegisterInfo(RecordKeeper &RK, raw_ostream &OS) {
  RegisterInfoEmitter(RK).run(OS);
}

} // end namespace llvm
