//===- CodeGenTarget.cpp - CodeGen Target Class Wrapper -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This class wraps target description classes used by the various code
// generation TableGen backends.  This makes it easier to access the data and
// provides a single place that needs to check it for validity.  All of these
// classes abort on error conditions.
//
//===----------------------------------------------------------------------===//

#include "CodeGenTarget.h"
#include "CodeGenDAGPatterns.h"
#include "CodeGenIntrinsics.h"
#include "CodeGenSchedule.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include <algorithm>
using namespace llvm;

cl::OptionCategory AsmParserCat("Options for -gen-asm-parser");
cl::OptionCategory AsmWriterCat("Options for -gen-asm-writer");

static cl::opt<unsigned>
    AsmParserNum("asmparsernum", cl::init(0),
                 cl::desc("Make -gen-asm-parser emit assembly parser #N"),
                 cl::cat(AsmParserCat));

static cl::opt<unsigned>
    AsmWriterNum("asmwriternum", cl::init(0),
                 cl::desc("Make -gen-asm-writer emit assembly writer #N"),
                 cl::cat(AsmWriterCat));

/// getValueType - Return the MVT::SimpleValueType that the specified TableGen
/// record corresponds to.
MVT::SimpleValueType llvm::getValueType(Record *Rec) {
  return (MVT::SimpleValueType)Rec->getValueAsInt("Value");
}

StringRef llvm::getName(MVT::SimpleValueType T) {
  switch (T) {
  case MVT::Other:   return "UNKNOWN";
  case MVT::iPTR:    return "TLI.getPointerTy()";
  case MVT::iPTRAny: return "TLI.getPointerTy()";
  default: return getEnumName(T);
  }
}

StringRef llvm::getEnumName(MVT::SimpleValueType T) {
  switch (T) {
  case MVT::Other:    return "MVT::Other";
  case MVT::i1:       return "MVT::i1";
  case MVT::i8:       return "MVT::i8";
  case MVT::i16:      return "MVT::i16";
  case MVT::i32:      return "MVT::i32";
  case MVT::i64:      return "MVT::i64";
  case MVT::i128:     return "MVT::i128";
  case MVT::Any:      return "MVT::Any";
  case MVT::iAny:     return "MVT::iAny";
  case MVT::fAny:     return "MVT::fAny";
  case MVT::vAny:     return "MVT::vAny";
  case MVT::f16:      return "MVT::f16";
  case MVT::f32:      return "MVT::f32";
  case MVT::f64:      return "MVT::f64";
  case MVT::f80:      return "MVT::f80";
  case MVT::f128:     return "MVT::f128";
  case MVT::ppcf128:  return "MVT::ppcf128";
  case MVT::x86mmx:   return "MVT::x86mmx";
  case MVT::Glue:     return "MVT::Glue";
  case MVT::isVoid:   return "MVT::isVoid";
  case MVT::v1i1:     return "MVT::v1i1";
  case MVT::v2i1:     return "MVT::v2i1";
  case MVT::v4i1:     return "MVT::v4i1";
  case MVT::v8i1:     return "MVT::v8i1";
  case MVT::v16i1:    return "MVT::v16i1";
  case MVT::v32i1:    return "MVT::v32i1";
  case MVT::v64i1:    return "MVT::v64i1";
  case MVT::v128i1:   return "MVT::v128i1";
  case MVT::v512i1:   return "MVT::v512i1";
  case MVT::v1024i1:  return "MVT::v1024i1";
  case MVT::v1i8:     return "MVT::v1i8";
  case MVT::v2i8:     return "MVT::v2i8";
  case MVT::v4i8:     return "MVT::v4i8";
  case MVT::v8i8:     return "MVT::v8i8";
  case MVT::v16i8:    return "MVT::v16i8";
  case MVT::v32i8:    return "MVT::v32i8";
  case MVT::v64i8:    return "MVT::v64i8";
  case MVT::v128i8:   return "MVT::v128i8";
  case MVT::v256i8:   return "MVT::v256i8";
  case MVT::v1i16:    return "MVT::v1i16";
  case MVT::v2i16:    return "MVT::v2i16";
  case MVT::v4i16:    return "MVT::v4i16";
  case MVT::v8i16:    return "MVT::v8i16";
  case MVT::v16i16:   return "MVT::v16i16";
  case MVT::v32i16:   return "MVT::v32i16";
  case MVT::v64i16:   return "MVT::v64i16";
  case MVT::v128i16:  return "MVT::v128i16";
  case MVT::v1i32:    return "MVT::v1i32";
  case MVT::v2i32:    return "MVT::v2i32";
  case MVT::v4i32:    return "MVT::v4i32";
  case MVT::v8i32:    return "MVT::v8i32";
  case MVT::v16i32:   return "MVT::v16i32";
  case MVT::v32i32:   return "MVT::v32i32";
  case MVT::v64i32:   return "MVT::v64i32";
  case MVT::v1i64:    return "MVT::v1i64";
  case MVT::v2i64:    return "MVT::v2i64";
  case MVT::v4i64:    return "MVT::v4i64";
  case MVT::v8i64:    return "MVT::v8i64";
  case MVT::v16i64:   return "MVT::v16i64";
  case MVT::v32i64:   return "MVT::v32i64";
  case MVT::v1i128:   return "MVT::v1i128";
  case MVT::v2f16:    return "MVT::v2f16";
  case MVT::v4f16:    return "MVT::v4f16";
  case MVT::v8f16:    return "MVT::v8f16";
  case MVT::v1f32:    return "MVT::v1f32";
  case MVT::v2f32:    return "MVT::v2f32";
  case MVT::v4f32:    return "MVT::v4f32";
  case MVT::v8f32:    return "MVT::v8f32";
  case MVT::v16f32:   return "MVT::v16f32";
  case MVT::v1f64:    return "MVT::v1f64";
  case MVT::v2f64:    return "MVT::v2f64";
  case MVT::v4f64:    return "MVT::v4f64";
  case MVT::v8f64:    return "MVT::v8f64";
  case MVT::nxv1i1:   return "MVT::nxv1i1";
  case MVT::nxv2i1:   return "MVT::nxv2i1";
  case MVT::nxv4i1:   return "MVT::nxv4i1";
  case MVT::nxv8i1:   return "MVT::nxv8i1";
  case MVT::nxv16i1:  return "MVT::nxv16i1";
  case MVT::nxv32i1:  return "MVT::nxv32i1";
  case MVT::nxv1i8:   return "MVT::nxv1i8";
  case MVT::nxv2i8:   return "MVT::nxv2i8";
  case MVT::nxv4i8:   return "MVT::nxv4i8";
  case MVT::nxv8i8:   return "MVT::nxv8i8";
  case MVT::nxv16i8:  return "MVT::nxv16i8";
  case MVT::nxv32i8:  return "MVT::nxv32i8";
  case MVT::nxv1i16:  return "MVT::nxv1i16";
  case MVT::nxv2i16:  return "MVT::nxv2i16";
  case MVT::nxv4i16:  return "MVT::nxv4i16";
  case MVT::nxv8i16:  return "MVT::nxv8i16";
  case MVT::nxv16i16: return "MVT::nxv16i16";
  case MVT::nxv32i16: return "MVT::nxv32i16";
  case MVT::nxv1i32:  return "MVT::nxv1i32";
  case MVT::nxv2i32:  return "MVT::nxv2i32";
  case MVT::nxv4i32:  return "MVT::nxv4i32";
  case MVT::nxv8i32:  return "MVT::nxv8i32";
  case MVT::nxv16i32: return "MVT::nxv16i32";
  case MVT::nxv1i64:  return "MVT::nxv1i64";
  case MVT::nxv2i64:  return "MVT::nxv2i64";
  case MVT::nxv4i64:  return "MVT::nxv4i64";
  case MVT::nxv8i64:  return "MVT::nxv8i64";
  case MVT::nxv16i64: return "MVT::nxv16i64";
  case MVT::nxv2f16:  return "MVT::nxv2f16";
  case MVT::nxv4f16:  return "MVT::nxv4f16";
  case MVT::nxv8f16:  return "MVT::nxv8f16";
  case MVT::nxv1f32:  return "MVT::nxv1f32";
  case MVT::nxv2f32:  return "MVT::nxv2f32";
  case MVT::nxv4f32:  return "MVT::nxv4f32";
  case MVT::nxv8f32:  return "MVT::nxv8f32";
  case MVT::nxv16f32: return "MVT::nxv16f32";
  case MVT::nxv1f64:  return "MVT::nxv1f64";
  case MVT::nxv2f64:  return "MVT::nxv2f64";
  case MVT::nxv4f64:  return "MVT::nxv4f64";
  case MVT::nxv8f64:  return "MVT::nxv8f64";
  case MVT::token:    return "MVT::token";
  case MVT::Metadata: return "MVT::Metadata";
  case MVT::iPTR:     return "MVT::iPTR";
  case MVT::iPTRAny:  return "MVT::iPTRAny";
  case MVT::Untyped:  return "MVT::Untyped";
  case MVT::ExceptRef: return "MVT::ExceptRef";
  default: llvm_unreachable("ILLEGAL VALUE TYPE!");
  }
}

/// getQualifiedName - Return the name of the specified record, with a
/// namespace qualifier if the record contains one.
///
std::string llvm::getQualifiedName(const Record *R) {
  std::string Namespace;
  if (R->getValue("Namespace"))
     Namespace = R->getValueAsString("Namespace");
  if (Namespace.empty()) return R->getName();
  return Namespace + "::" + R->getName().str();
}


/// getTarget - Return the current instance of the Target class.
///
CodeGenTarget::CodeGenTarget(RecordKeeper &records)
  : Records(records), CGH(records) {
  std::vector<Record*> Targets = Records.getAllDerivedDefinitions("Target");
  if (Targets.size() == 0)
    PrintFatalError("ERROR: No 'Target' subclasses defined!");
  if (Targets.size() != 1)
    PrintFatalError("ERROR: Multiple subclasses of Target defined!");
  TargetRec = Targets[0];
}

CodeGenTarget::~CodeGenTarget() {
}

const StringRef CodeGenTarget::getName() const {
  return TargetRec->getName();
}

StringRef CodeGenTarget::getInstNamespace() const {
  for (const CodeGenInstruction *Inst : getInstructionsByEnumValue()) {
    // Make sure not to pick up "TargetOpcode" by accidentally getting
    // the namespace off the PHI instruction or something.
    if (Inst->Namespace != "TargetOpcode")
      return Inst->Namespace;
  }

  return "";
}

Record *CodeGenTarget::getInstructionSet() const {
  return TargetRec->getValueAsDef("InstructionSet");
}

bool CodeGenTarget::getAllowRegisterRenaming() const {
  return TargetRec->getValueAsInt("AllowRegisterRenaming");
}

/// getAsmParser - Return the AssemblyParser definition for this target.
///
Record *CodeGenTarget::getAsmParser() const {
  std::vector<Record*> LI = TargetRec->getValueAsListOfDefs("AssemblyParsers");
  if (AsmParserNum >= LI.size())
    PrintFatalError("Target does not have an AsmParser #" +
                    Twine(AsmParserNum) + "!");
  return LI[AsmParserNum];
}

/// getAsmParserVariant - Return the AssmblyParserVariant definition for
/// this target.
///
Record *CodeGenTarget::getAsmParserVariant(unsigned i) const {
  std::vector<Record*> LI =
    TargetRec->getValueAsListOfDefs("AssemblyParserVariants");
  if (i >= LI.size())
    PrintFatalError("Target does not have an AsmParserVariant #" + Twine(i) +
                    "!");
  return LI[i];
}

/// getAsmParserVariantCount - Return the AssmblyParserVariant definition
/// available for this target.
///
unsigned CodeGenTarget::getAsmParserVariantCount() const {
  std::vector<Record*> LI =
    TargetRec->getValueAsListOfDefs("AssemblyParserVariants");
  return LI.size();
}

/// getAsmWriter - Return the AssemblyWriter definition for this target.
///
Record *CodeGenTarget::getAsmWriter() const {
  std::vector<Record*> LI = TargetRec->getValueAsListOfDefs("AssemblyWriters");
  if (AsmWriterNum >= LI.size())
    PrintFatalError("Target does not have an AsmWriter #" +
                    Twine(AsmWriterNum) + "!");
  return LI[AsmWriterNum];
}

CodeGenRegBank &CodeGenTarget::getRegBank() const {
  if (!RegBank)
    RegBank = llvm::make_unique<CodeGenRegBank>(Records, getHwModes());
  return *RegBank;
}

void CodeGenTarget::ReadRegAltNameIndices() const {
  RegAltNameIndices = Records.getAllDerivedDefinitions("RegAltNameIndex");
  llvm::sort(RegAltNameIndices, LessRecord());
}

/// getRegisterByName - If there is a register with the specific AsmName,
/// return it.
const CodeGenRegister *CodeGenTarget::getRegisterByName(StringRef Name) const {
  const StringMap<CodeGenRegister*> &Regs = getRegBank().getRegistersByName();
  StringMap<CodeGenRegister*>::const_iterator I = Regs.find(Name);
  if (I == Regs.end())
    return nullptr;
  return I->second;
}

std::vector<ValueTypeByHwMode> CodeGenTarget::getRegisterVTs(Record *R)
      const {
  const CodeGenRegister *Reg = getRegBank().getReg(R);
  std::vector<ValueTypeByHwMode> Result;
  for (const auto &RC : getRegBank().getRegClasses()) {
    if (RC.contains(Reg)) {
      ArrayRef<ValueTypeByHwMode> InVTs = RC.getValueTypes();
      Result.insert(Result.end(), InVTs.begin(), InVTs.end());
    }
  }

  // Remove duplicates.
  llvm::sort(Result);
  Result.erase(std::unique(Result.begin(), Result.end()), Result.end());
  return Result;
}


void CodeGenTarget::ReadLegalValueTypes() const {
  for (const auto &RC : getRegBank().getRegClasses())
    LegalValueTypes.insert(LegalValueTypes.end(), RC.VTs.begin(), RC.VTs.end());

  // Remove duplicates.
  llvm::sort(LegalValueTypes);
  LegalValueTypes.erase(std::unique(LegalValueTypes.begin(),
                                    LegalValueTypes.end()),
                        LegalValueTypes.end());
}

CodeGenSchedModels &CodeGenTarget::getSchedModels() const {
  if (!SchedModels)
    SchedModels = llvm::make_unique<CodeGenSchedModels>(Records, *this);
  return *SchedModels;
}

void CodeGenTarget::ReadInstructions() const {
  std::vector<Record*> Insts = Records.getAllDerivedDefinitions("Instruction");
  if (Insts.size() <= 2)
    PrintFatalError("No 'Instruction' subclasses defined!");

  // Parse the instructions defined in the .td file.
  for (unsigned i = 0, e = Insts.size(); i != e; ++i)
    Instructions[Insts[i]] = llvm::make_unique<CodeGenInstruction>(Insts[i]);
}

static const CodeGenInstruction *
GetInstByName(const char *Name,
              const DenseMap<const Record*,
                             std::unique_ptr<CodeGenInstruction>> &Insts,
              RecordKeeper &Records) {
  const Record *Rec = Records.getDef(Name);

  const auto I = Insts.find(Rec);
  if (!Rec || I == Insts.end())
    PrintFatalError(Twine("Could not find '") + Name + "' instruction!");
  return I->second.get();
}

static const char *const FixedInstrs[] = {
#define HANDLE_TARGET_OPCODE(OPC) #OPC,
#include "llvm/Support/TargetOpcodes.def"
    nullptr};

unsigned CodeGenTarget::getNumFixedInstructions() {
  return array_lengthof(FixedInstrs) - 1;
}

/// Return all of the instructions defined by the target, ordered by
/// their enum value.
void CodeGenTarget::ComputeInstrsByEnum() const {
  const auto &Insts = getInstructions();
  for (const char *const *p = FixedInstrs; *p; ++p) {
    const CodeGenInstruction *Instr = GetInstByName(*p, Insts, Records);
    assert(Instr && "Missing target independent instruction");
    assert(Instr->Namespace == "TargetOpcode" && "Bad namespace");
    InstrsByEnum.push_back(Instr);
  }
  unsigned EndOfPredefines = InstrsByEnum.size();
  assert(EndOfPredefines == getNumFixedInstructions() &&
         "Missing generic opcode");

  for (const auto &I : Insts) {
    const CodeGenInstruction *CGI = I.second.get();
    if (CGI->Namespace != "TargetOpcode") {
      InstrsByEnum.push_back(CGI);
      if (CGI->TheDef->getValueAsBit("isPseudo"))
        ++NumPseudoInstructions;
    }
  }

  assert(InstrsByEnum.size() == Insts.size() && "Missing predefined instr");

  // All of the instructions are now in random order based on the map iteration.
  llvm::sort(
      InstrsByEnum.begin() + EndOfPredefines, InstrsByEnum.end(),
      [](const CodeGenInstruction *Rec1, const CodeGenInstruction *Rec2) {
        const auto &D1 = *Rec1->TheDef;
        const auto &D2 = *Rec2->TheDef;
        return std::make_tuple(!D1.getValueAsBit("isPseudo"), D1.getName()) <
               std::make_tuple(!D2.getValueAsBit("isPseudo"), D2.getName());
      });
}


/// isLittleEndianEncoding - Return whether this target encodes its instruction
/// in little-endian format, i.e. bits laid out in the order [0..n]
///
bool CodeGenTarget::isLittleEndianEncoding() const {
  return getInstructionSet()->getValueAsBit("isLittleEndianEncoding");
}

/// reverseBitsForLittleEndianEncoding - For little-endian instruction bit
/// encodings, reverse the bit order of all instructions.
void CodeGenTarget::reverseBitsForLittleEndianEncoding() {
  if (!isLittleEndianEncoding())
    return;

  std::vector<Record*> Insts = Records.getAllDerivedDefinitions("Instruction");
  for (Record *R : Insts) {
    if (R->getValueAsString("Namespace") == "TargetOpcode" ||
        R->getValueAsBit("isPseudo"))
      continue;

    BitsInit *BI = R->getValueAsBitsInit("Inst");

    unsigned numBits = BI->getNumBits();

    SmallVector<Init *, 16> NewBits(numBits);

    for (unsigned bit = 0, end = numBits / 2; bit != end; ++bit) {
      unsigned bitSwapIdx = numBits - bit - 1;
      Init *OrigBit = BI->getBit(bit);
      Init *BitSwap = BI->getBit(bitSwapIdx);
      NewBits[bit]        = BitSwap;
      NewBits[bitSwapIdx] = OrigBit;
    }
    if (numBits % 2) {
      unsigned middle = (numBits + 1) / 2;
      NewBits[middle] = BI->getBit(middle);
    }

    BitsInit *NewBI = BitsInit::get(NewBits);

    // Update the bits in reversed order so that emitInstrOpBits will get the
    // correct endianness.
    R->getValue("Inst")->setValue(NewBI);
  }
}

/// guessInstructionProperties - Return true if it's OK to guess instruction
/// properties instead of raising an error.
///
/// This is configurable as a temporary migration aid. It will eventually be
/// permanently false.
bool CodeGenTarget::guessInstructionProperties() const {
  return getInstructionSet()->getValueAsBit("guessInstructionProperties");
}

//===----------------------------------------------------------------------===//
// ComplexPattern implementation
//
ComplexPattern::ComplexPattern(Record *R) {
  Ty          = ::getValueType(R->getValueAsDef("Ty"));
  NumOperands = R->getValueAsInt("NumOperands");
  SelectFunc  = R->getValueAsString("SelectFunc");
  RootNodes   = R->getValueAsListOfDefs("RootNodes");

  // FIXME: This is a hack to statically increase the priority of patterns which
  // maps a sub-dag to a complex pattern. e.g. favors LEA over ADD. To get best
  // possible pattern match we'll need to dynamically calculate the complexity
  // of all patterns a dag can potentially map to.
  int64_t RawComplexity = R->getValueAsInt("Complexity");
  if (RawComplexity == -1)
    Complexity = NumOperands * 3;
  else
    Complexity = RawComplexity;

  // FIXME: Why is this different from parseSDPatternOperatorProperties?
  // Parse the properties.
  Properties = 0;
  std::vector<Record*> PropList = R->getValueAsListOfDefs("Properties");
  for (unsigned i = 0, e = PropList.size(); i != e; ++i)
    if (PropList[i]->getName() == "SDNPHasChain") {
      Properties |= 1 << SDNPHasChain;
    } else if (PropList[i]->getName() == "SDNPOptInGlue") {
      Properties |= 1 << SDNPOptInGlue;
    } else if (PropList[i]->getName() == "SDNPMayStore") {
      Properties |= 1 << SDNPMayStore;
    } else if (PropList[i]->getName() == "SDNPMayLoad") {
      Properties |= 1 << SDNPMayLoad;
    } else if (PropList[i]->getName() == "SDNPSideEffect") {
      Properties |= 1 << SDNPSideEffect;
    } else if (PropList[i]->getName() == "SDNPMemOperand") {
      Properties |= 1 << SDNPMemOperand;
    } else if (PropList[i]->getName() == "SDNPVariadic") {
      Properties |= 1 << SDNPVariadic;
    } else if (PropList[i]->getName() == "SDNPWantRoot") {
      Properties |= 1 << SDNPWantRoot;
    } else if (PropList[i]->getName() == "SDNPWantParent") {
      Properties |= 1 << SDNPWantParent;
    } else {
      PrintFatalError("Unsupported SD Node property '" +
                      PropList[i]->getName() + "' on ComplexPattern '" +
                      R->getName() + "'!");
    }
}

//===----------------------------------------------------------------------===//
// CodeGenIntrinsic Implementation
//===----------------------------------------------------------------------===//

CodeGenIntrinsicTable::CodeGenIntrinsicTable(const RecordKeeper &RC,
                                             bool TargetOnly) {
  std::vector<Record*> Defs = RC.getAllDerivedDefinitions("Intrinsic");

  Intrinsics.reserve(Defs.size());

  for (unsigned I = 0, e = Defs.size(); I != e; ++I) {
    bool isTarget = Defs[I]->getValueAsBit("isTarget");
    if (isTarget == TargetOnly)
      Intrinsics.push_back(CodeGenIntrinsic(Defs[I]));
  }
  llvm::sort(Intrinsics,
             [](const CodeGenIntrinsic &LHS, const CodeGenIntrinsic &RHS) {
               return std::tie(LHS.TargetPrefix, LHS.Name) <
                      std::tie(RHS.TargetPrefix, RHS.Name);
             });
  Targets.push_back({"", 0, 0});
  for (size_t I = 0, E = Intrinsics.size(); I < E; ++I)
    if (Intrinsics[I].TargetPrefix != Targets.back().Name) {
      Targets.back().Count = I - Targets.back().Offset;
      Targets.push_back({Intrinsics[I].TargetPrefix, I, 0});
    }
  Targets.back().Count = Intrinsics.size() - Targets.back().Offset;
}

CodeGenIntrinsic::CodeGenIntrinsic(Record *R) {
  TheDef = R;
  std::string DefName = R->getName();
  ModRef = ReadWriteMem;
  Properties = 0;
  isOverloaded = false;
  isCommutative = false;
  canThrow = false;
  isNoReturn = false;
  isCold = false;
  isNoDuplicate = false;
  isConvergent = false;
  isSpeculatable = false;
  hasSideEffects = false;

  if (DefName.size() <= 4 ||
      std::string(DefName.begin(), DefName.begin() + 4) != "int_")
    PrintFatalError("Intrinsic '" + DefName + "' does not start with 'int_'!");

  EnumName = std::string(DefName.begin()+4, DefName.end());

  if (R->getValue("GCCBuiltinName"))  // Ignore a missing GCCBuiltinName field.
    GCCBuiltinName = R->getValueAsString("GCCBuiltinName");
  if (R->getValue("MSBuiltinName"))   // Ignore a missing MSBuiltinName field.
    MSBuiltinName = R->getValueAsString("MSBuiltinName");

  TargetPrefix = R->getValueAsString("TargetPrefix");
  Name = R->getValueAsString("LLVMName");

  if (Name == "") {
    // If an explicit name isn't specified, derive one from the DefName.
    Name = "llvm.";

    for (unsigned i = 0, e = EnumName.size(); i != e; ++i)
      Name += (EnumName[i] == '_') ? '.' : EnumName[i];
  } else {
    // Verify it starts with "llvm.".
    if (Name.size() <= 5 ||
        std::string(Name.begin(), Name.begin() + 5) != "llvm.")
      PrintFatalError("Intrinsic '" + DefName + "'s name does not start with 'llvm.'!");
  }

  // If TargetPrefix is specified, make sure that Name starts with
  // "llvm.<targetprefix>.".
  if (!TargetPrefix.empty()) {
    if (Name.size() < 6+TargetPrefix.size() ||
        std::string(Name.begin() + 5, Name.begin() + 6 + TargetPrefix.size())
        != (TargetPrefix + "."))
      PrintFatalError("Intrinsic '" + DefName + "' does not start with 'llvm." +
        TargetPrefix + ".'!");
  }

  // Parse the list of return types.
  std::vector<MVT::SimpleValueType> OverloadedVTs;
  ListInit *TypeList = R->getValueAsListInit("RetTypes");
  for (unsigned i = 0, e = TypeList->size(); i != e; ++i) {
    Record *TyEl = TypeList->getElementAsRecord(i);
    assert(TyEl->isSubClassOf("LLVMType") && "Expected a type!");
    MVT::SimpleValueType VT;
    if (TyEl->isSubClassOf("LLVMMatchType")) {
      unsigned MatchTy = TyEl->getValueAsInt("Number");
      assert(MatchTy < OverloadedVTs.size() &&
             "Invalid matching number!");
      VT = OverloadedVTs[MatchTy];
      // It only makes sense to use the extended and truncated vector element
      // variants with iAny types; otherwise, if the intrinsic is not
      // overloaded, all the types can be specified directly.
      assert(((!TyEl->isSubClassOf("LLVMExtendedType") &&
               !TyEl->isSubClassOf("LLVMTruncatedType")) ||
              VT == MVT::iAny || VT == MVT::vAny) &&
             "Expected iAny or vAny type");
    } else {
      VT = getValueType(TyEl->getValueAsDef("VT"));
    }
    if (MVT(VT).isOverloaded()) {
      OverloadedVTs.push_back(VT);
      isOverloaded = true;
    }

    // Reject invalid types.
    if (VT == MVT::isVoid)
      PrintFatalError("Intrinsic '" + DefName + " has void in result type list!");

    IS.RetVTs.push_back(VT);
    IS.RetTypeDefs.push_back(TyEl);
  }

  // Parse the list of parameter types.
  TypeList = R->getValueAsListInit("ParamTypes");
  for (unsigned i = 0, e = TypeList->size(); i != e; ++i) {
    Record *TyEl = TypeList->getElementAsRecord(i);
    assert(TyEl->isSubClassOf("LLVMType") && "Expected a type!");
    MVT::SimpleValueType VT;
    if (TyEl->isSubClassOf("LLVMMatchType")) {
      unsigned MatchTy = TyEl->getValueAsInt("Number");
      if (MatchTy >= OverloadedVTs.size()) {
        PrintError(R->getLoc(),
                   "Parameter #" + Twine(i) + " has out of bounds matching "
                   "number " + Twine(MatchTy));
        PrintFatalError(Twine("ParamTypes is ") + TypeList->getAsString());
      }
      VT = OverloadedVTs[MatchTy];
      // It only makes sense to use the extended and truncated vector element
      // variants with iAny types; otherwise, if the intrinsic is not
      // overloaded, all the types can be specified directly.
      assert(((!TyEl->isSubClassOf("LLVMExtendedType") &&
               !TyEl->isSubClassOf("LLVMTruncatedType") &&
               !TyEl->isSubClassOf("LLVMVectorSameWidth")) ||
              VT == MVT::iAny || VT == MVT::vAny) &&
             "Expected iAny or vAny type");
    } else
      VT = getValueType(TyEl->getValueAsDef("VT"));

    if (MVT(VT).isOverloaded()) {
      OverloadedVTs.push_back(VT);
      isOverloaded = true;
    }

    // Reject invalid types.
    if (VT == MVT::isVoid && i != e-1 /*void at end means varargs*/)
      PrintFatalError("Intrinsic '" + DefName + " has void in result type list!");

    IS.ParamVTs.push_back(VT);
    IS.ParamTypeDefs.push_back(TyEl);
  }

  // Parse the intrinsic properties.
  ListInit *PropList = R->getValueAsListInit("IntrProperties");
  for (unsigned i = 0, e = PropList->size(); i != e; ++i) {
    Record *Property = PropList->getElementAsRecord(i);
    assert(Property->isSubClassOf("IntrinsicProperty") &&
           "Expected a property!");

    if (Property->getName() == "IntrNoMem")
      ModRef = NoMem;
    else if (Property->getName() == "IntrReadMem")
      ModRef = ModRefBehavior(ModRef & ~MR_Mod);
    else if (Property->getName() == "IntrWriteMem")
      ModRef = ModRefBehavior(ModRef & ~MR_Ref);
    else if (Property->getName() == "IntrArgMemOnly")
      ModRef = ModRefBehavior((ModRef & ~MR_Anywhere) | MR_ArgMem);
    else if (Property->getName() == "IntrInaccessibleMemOnly")
      ModRef = ModRefBehavior((ModRef & ~MR_Anywhere) | MR_InaccessibleMem);
    else if (Property->getName() == "IntrInaccessibleMemOrArgMemOnly")
      ModRef = ModRefBehavior((ModRef & ~MR_Anywhere) | MR_ArgMem |
                              MR_InaccessibleMem);
    else if (Property->getName() == "Commutative")
      isCommutative = true;
    else if (Property->getName() == "Throws")
      canThrow = true;
    else if (Property->getName() == "IntrNoDuplicate")
      isNoDuplicate = true;
    else if (Property->getName() == "IntrConvergent")
      isConvergent = true;
    else if (Property->getName() == "IntrNoReturn")
      isNoReturn = true;
    else if (Property->getName() == "IntrCold")
      isCold = true;
    else if (Property->getName() == "IntrSpeculatable")
      isSpeculatable = true;
    else if (Property->getName() == "IntrHasSideEffects")
      hasSideEffects = true;
    else if (Property->isSubClassOf("NoCapture")) {
      unsigned ArgNo = Property->getValueAsInt("ArgNo");
      ArgumentAttributes.push_back(std::make_pair(ArgNo, NoCapture));
    } else if (Property->isSubClassOf("Returned")) {
      unsigned ArgNo = Property->getValueAsInt("ArgNo");
      ArgumentAttributes.push_back(std::make_pair(ArgNo, Returned));
    } else if (Property->isSubClassOf("ReadOnly")) {
      unsigned ArgNo = Property->getValueAsInt("ArgNo");
      ArgumentAttributes.push_back(std::make_pair(ArgNo, ReadOnly));
    } else if (Property->isSubClassOf("WriteOnly")) {
      unsigned ArgNo = Property->getValueAsInt("ArgNo");
      ArgumentAttributes.push_back(std::make_pair(ArgNo, WriteOnly));
    } else if (Property->isSubClassOf("ReadNone")) {
      unsigned ArgNo = Property->getValueAsInt("ArgNo");
      ArgumentAttributes.push_back(std::make_pair(ArgNo, ReadNone));
    } else
      llvm_unreachable("Unknown property!");
  }

  // Also record the SDPatternOperator Properties.
  Properties = parseSDPatternOperatorProperties(R);

  // Sort the argument attributes for later benefit.
  llvm::sort(ArgumentAttributes);
}
