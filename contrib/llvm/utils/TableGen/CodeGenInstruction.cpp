//===- CodeGenInstruction.cpp - CodeGen Instruction Class Wrapper ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the CodeGenInstruction class.
//
//===----------------------------------------------------------------------===//

#include "CodeGenInstruction.h"
#include "CodeGenTarget.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include <set>
using namespace llvm;

//===----------------------------------------------------------------------===//
// CGIOperandList Implementation
//===----------------------------------------------------------------------===//

CGIOperandList::CGIOperandList(Record *R) : TheDef(R) {
  isPredicable = false;
  hasOptionalDef = false;
  isVariadic = false;

  DagInit *OutDI = R->getValueAsDag("OutOperandList");

  if (DefInit *Init = dyn_cast<DefInit>(OutDI->getOperator())) {
    if (Init->getDef()->getName() != "outs")
      PrintFatalError(R->getName() + ": invalid def name for output list: use 'outs'");
  } else
    PrintFatalError(R->getName() + ": invalid output list: use 'outs'");

  NumDefs = OutDI->getNumArgs();

  DagInit *InDI = R->getValueAsDag("InOperandList");
  if (DefInit *Init = dyn_cast<DefInit>(InDI->getOperator())) {
    if (Init->getDef()->getName() != "ins")
      PrintFatalError(R->getName() + ": invalid def name for input list: use 'ins'");
  } else
    PrintFatalError(R->getName() + ": invalid input list: use 'ins'");

  unsigned MIOperandNo = 0;
  std::set<std::string> OperandNames;
  unsigned e = InDI->getNumArgs() + OutDI->getNumArgs();
  OperandList.reserve(e);
  for (unsigned i = 0; i != e; ++i){
    Init *ArgInit;
    StringRef ArgName;
    if (i < NumDefs) {
      ArgInit = OutDI->getArg(i);
      ArgName = OutDI->getArgNameStr(i);
    } else {
      ArgInit = InDI->getArg(i-NumDefs);
      ArgName = InDI->getArgNameStr(i-NumDefs);
    }

    DefInit *Arg = dyn_cast<DefInit>(ArgInit);
    if (!Arg)
      PrintFatalError("Illegal operand for the '" + R->getName() + "' instruction!");

    Record *Rec = Arg->getDef();
    std::string PrintMethod = "printOperand";
    std::string EncoderMethod;
    std::string OperandType = "OPERAND_UNKNOWN";
    std::string OperandNamespace = "MCOI";
    unsigned NumOps = 1;
    DagInit *MIOpInfo = nullptr;
    if (Rec->isSubClassOf("RegisterOperand")) {
      PrintMethod = Rec->getValueAsString("PrintMethod");
      OperandType = Rec->getValueAsString("OperandType");
      OperandNamespace = Rec->getValueAsString("OperandNamespace");
      EncoderMethod = Rec->getValueAsString("EncoderMethod");
    } else if (Rec->isSubClassOf("Operand")) {
      PrintMethod = Rec->getValueAsString("PrintMethod");
      OperandType = Rec->getValueAsString("OperandType");
      OperandNamespace = Rec->getValueAsString("OperandNamespace");
      // If there is an explicit encoder method, use it.
      EncoderMethod = Rec->getValueAsString("EncoderMethod");
      MIOpInfo = Rec->getValueAsDag("MIOperandInfo");

      // Verify that MIOpInfo has an 'ops' root value.
      if (!isa<DefInit>(MIOpInfo->getOperator()) ||
          cast<DefInit>(MIOpInfo->getOperator())->getDef()->getName() != "ops")
        PrintFatalError("Bad value for MIOperandInfo in operand '" + Rec->getName() +
          "'\n");

      // If we have MIOpInfo, then we have #operands equal to number of entries
      // in MIOperandInfo.
      if (unsigned NumArgs = MIOpInfo->getNumArgs())
        NumOps = NumArgs;

      if (Rec->isSubClassOf("PredicateOp"))
        isPredicable = true;
      else if (Rec->isSubClassOf("OptionalDefOperand"))
        hasOptionalDef = true;
    } else if (Rec->getName() == "variable_ops") {
      isVariadic = true;
      continue;
    } else if (Rec->isSubClassOf("RegisterClass")) {
      OperandType = "OPERAND_REGISTER";
    } else if (!Rec->isSubClassOf("PointerLikeRegClass") &&
               !Rec->isSubClassOf("unknown_class"))
      PrintFatalError("Unknown operand class '" + Rec->getName() +
        "' in '" + R->getName() + "' instruction!");

    // Check that the operand has a name and that it's unique.
    if (ArgName.empty())
      PrintFatalError("In instruction '" + R->getName() + "', operand #" +
                      Twine(i) + " has no name!");
    if (!OperandNames.insert(ArgName).second)
      PrintFatalError("In instruction '" + R->getName() + "', operand #" +
                      Twine(i) + " has the same name as a previous operand!");

    OperandList.emplace_back(Rec, ArgName, PrintMethod, EncoderMethod,
                             OperandNamespace + "::" + OperandType, MIOperandNo,
                             NumOps, MIOpInfo);
    MIOperandNo += NumOps;
  }


  // Make sure the constraints list for each operand is large enough to hold
  // constraint info, even if none is present.
  for (OperandInfo &OpInfo : OperandList)
    OpInfo.Constraints.resize(OpInfo.MINumOperands);
}


/// getOperandNamed - Return the index of the operand with the specified
/// non-empty name.  If the instruction does not have an operand with the
/// specified name, abort.
///
unsigned CGIOperandList::getOperandNamed(StringRef Name) const {
  unsigned OpIdx;
  if (hasOperandNamed(Name, OpIdx)) return OpIdx;
  PrintFatalError("'" + TheDef->getName() +
                  "' does not have an operand named '$" + Name + "'!");
}

/// hasOperandNamed - Query whether the instruction has an operand of the
/// given name. If so, return true and set OpIdx to the index of the
/// operand. Otherwise, return false.
bool CGIOperandList::hasOperandNamed(StringRef Name, unsigned &OpIdx) const {
  assert(!Name.empty() && "Cannot search for operand with no name!");
  for (unsigned i = 0, e = OperandList.size(); i != e; ++i)
    if (OperandList[i].Name == Name) {
      OpIdx = i;
      return true;
    }
  return false;
}

std::pair<unsigned,unsigned>
CGIOperandList::ParseOperandName(const std::string &Op, bool AllowWholeOp) {
  if (Op.empty() || Op[0] != '$')
    PrintFatalError(TheDef->getName() + ": Illegal operand name: '" + Op + "'");

  std::string OpName = Op.substr(1);
  std::string SubOpName;

  // Check to see if this is $foo.bar.
  std::string::size_type DotIdx = OpName.find_first_of('.');
  if (DotIdx != std::string::npos) {
    SubOpName = OpName.substr(DotIdx+1);
    if (SubOpName.empty())
      PrintFatalError(TheDef->getName() + ": illegal empty suboperand name in '" +Op +"'");
    OpName = OpName.substr(0, DotIdx);
  }

  unsigned OpIdx = getOperandNamed(OpName);

  if (SubOpName.empty()) {  // If no suboperand name was specified:
    // If one was needed, throw.
    if (OperandList[OpIdx].MINumOperands > 1 && !AllowWholeOp &&
        SubOpName.empty())
      PrintFatalError(TheDef->getName() + ": Illegal to refer to"
        " whole operand part of complex operand '" + Op + "'");

    // Otherwise, return the operand.
    return std::make_pair(OpIdx, 0U);
  }

  // Find the suboperand number involved.
  DagInit *MIOpInfo = OperandList[OpIdx].MIOperandInfo;
  if (!MIOpInfo)
    PrintFatalError(TheDef->getName() + ": unknown suboperand name in '" + Op + "'");

  // Find the operand with the right name.
  for (unsigned i = 0, e = MIOpInfo->getNumArgs(); i != e; ++i)
    if (MIOpInfo->getArgNameStr(i) == SubOpName)
      return std::make_pair(OpIdx, i);

  // Otherwise, didn't find it!
  PrintFatalError(TheDef->getName() + ": unknown suboperand name in '" + Op + "'");
  return std::make_pair(0U, 0U);
}

static void ParseConstraint(const std::string &CStr, CGIOperandList &Ops,
                            Record *Rec) {
  // EARLY_CLOBBER: @early $reg
  std::string::size_type wpos = CStr.find_first_of(" \t");
  std::string::size_type start = CStr.find_first_not_of(" \t");
  std::string Tok = CStr.substr(start, wpos - start);
  if (Tok == "@earlyclobber") {
    std::string Name = CStr.substr(wpos+1);
    wpos = Name.find_first_not_of(" \t");
    if (wpos == std::string::npos)
      PrintFatalError(
        Rec->getLoc(), "Illegal format for @earlyclobber constraint in '" +
        Rec->getName() + "': '" + CStr + "'");
    Name = Name.substr(wpos);
    std::pair<unsigned,unsigned> Op = Ops.ParseOperandName(Name, false);

    // Build the string for the operand
    if (!Ops[Op.first].Constraints[Op.second].isNone())
      PrintFatalError(
        Rec->getLoc(), "Operand '" + Name + "' of '" + Rec->getName() +
        "' cannot have multiple constraints!");
    Ops[Op.first].Constraints[Op.second] =
    CGIOperandList::ConstraintInfo::getEarlyClobber();
    return;
  }

  // Only other constraint is "TIED_TO" for now.
  std::string::size_type pos = CStr.find_first_of('=');
  if (pos == std::string::npos)
    PrintFatalError(
      Rec->getLoc(), "Unrecognized constraint '" + CStr +
      "' in '" + Rec->getName() + "'");
  start = CStr.find_first_not_of(" \t");

  // TIED_TO: $src1 = $dst
  wpos = CStr.find_first_of(" \t", start);
  if (wpos == std::string::npos || wpos > pos)
    PrintFatalError(
      Rec->getLoc(), "Illegal format for tied-to constraint in '" +
      Rec->getName() + "': '" + CStr + "'");
  std::string LHSOpName = StringRef(CStr).substr(start, wpos - start);
  std::pair<unsigned,unsigned> LHSOp = Ops.ParseOperandName(LHSOpName, false);

  wpos = CStr.find_first_not_of(" \t", pos + 1);
  if (wpos == std::string::npos)
    PrintFatalError(
      Rec->getLoc(), "Illegal format for tied-to constraint: '" + CStr + "'");

  std::string RHSOpName = StringRef(CStr).substr(wpos);
  std::pair<unsigned,unsigned> RHSOp = Ops.ParseOperandName(RHSOpName, false);

  // Sort the operands into order, which should put the output one
  // first. But keep the original order, for use in diagnostics.
  bool FirstIsDest = (LHSOp < RHSOp);
  std::pair<unsigned,unsigned> DestOp = (FirstIsDest ? LHSOp : RHSOp);
  StringRef DestOpName = (FirstIsDest ? LHSOpName : RHSOpName);
  std::pair<unsigned,unsigned> SrcOp = (FirstIsDest ? RHSOp : LHSOp);
  StringRef SrcOpName = (FirstIsDest ? RHSOpName : LHSOpName);

  // Ensure one operand is a def and the other is a use.
  if (DestOp.first >= Ops.NumDefs)
    PrintFatalError(
      Rec->getLoc(), "Input operands '" + LHSOpName + "' and '" + RHSOpName +
      "' of '" + Rec->getName() + "' cannot be tied!");
  if (SrcOp.first < Ops.NumDefs)
    PrintFatalError(
      Rec->getLoc(), "Output operands '" + LHSOpName + "' and '" + RHSOpName +
      "' of '" + Rec->getName() + "' cannot be tied!");

  // The constraint has to go on the operand with higher index, i.e.
  // the source one. Check there isn't another constraint there
  // already.
  if (!Ops[SrcOp.first].Constraints[SrcOp.second].isNone())
    PrintFatalError(
      Rec->getLoc(), "Operand '" + SrcOpName + "' of '" + Rec->getName() +
      "' cannot have multiple constraints!");

  unsigned DestFlatOpNo = Ops.getFlattenedOperandNumber(DestOp);
  auto NewConstraint = CGIOperandList::ConstraintInfo::getTied(DestFlatOpNo);

  // Check that the earlier operand is not the target of another tie
  // before making it the target of this one.
  for (const CGIOperandList::OperandInfo &Op : Ops) {
    for (unsigned i = 0; i < Op.MINumOperands; i++)
      if (Op.Constraints[i] == NewConstraint)
        PrintFatalError(
          Rec->getLoc(), "Operand '" + DestOpName + "' of '" + Rec->getName() +
          "' cannot have multiple operands tied to it!");
  }

  Ops[SrcOp.first].Constraints[SrcOp.second] = NewConstraint;
}

static void ParseConstraints(const std::string &CStr, CGIOperandList &Ops,
                             Record *Rec) {
  if (CStr.empty()) return;

  const std::string delims(",");
  std::string::size_type bidx, eidx;

  bidx = CStr.find_first_not_of(delims);
  while (bidx != std::string::npos) {
    eidx = CStr.find_first_of(delims, bidx);
    if (eidx == std::string::npos)
      eidx = CStr.length();

    ParseConstraint(CStr.substr(bidx, eidx - bidx), Ops, Rec);
    bidx = CStr.find_first_not_of(delims, eidx);
  }
}

void CGIOperandList::ProcessDisableEncoding(std::string DisableEncoding) {
  while (1) {
    std::pair<StringRef, StringRef> P = getToken(DisableEncoding, " ,\t");
    std::string OpName = P.first;
    DisableEncoding = P.second;
    if (OpName.empty()) break;

    // Figure out which operand this is.
    std::pair<unsigned,unsigned> Op = ParseOperandName(OpName, false);

    // Mark the operand as not-to-be encoded.
    if (Op.second >= OperandList[Op.first].DoNotEncode.size())
      OperandList[Op.first].DoNotEncode.resize(Op.second+1);
    OperandList[Op.first].DoNotEncode[Op.second] = true;
  }

}

//===----------------------------------------------------------------------===//
// CodeGenInstruction Implementation
//===----------------------------------------------------------------------===//

CodeGenInstruction::CodeGenInstruction(Record *R)
  : TheDef(R), Operands(R), InferredFrom(nullptr) {
  Namespace = R->getValueAsString("Namespace");
  AsmString = R->getValueAsString("AsmString");

  isReturn     = R->getValueAsBit("isReturn");
  isEHScopeReturn = R->getValueAsBit("isEHScopeReturn");
  isBranch     = R->getValueAsBit("isBranch");
  isIndirectBranch = R->getValueAsBit("isIndirectBranch");
  isCompare    = R->getValueAsBit("isCompare");
  isMoveImm    = R->getValueAsBit("isMoveImm");
  isMoveReg    = R->getValueAsBit("isMoveReg");
  isBitcast    = R->getValueAsBit("isBitcast");
  isSelect     = R->getValueAsBit("isSelect");
  isBarrier    = R->getValueAsBit("isBarrier");
  isCall       = R->getValueAsBit("isCall");
  isAdd        = R->getValueAsBit("isAdd");
  isTrap       = R->getValueAsBit("isTrap");
  canFoldAsLoad = R->getValueAsBit("canFoldAsLoad");
  isPredicable = Operands.isPredicable || R->getValueAsBit("isPredicable");
  isConvertibleToThreeAddress = R->getValueAsBit("isConvertibleToThreeAddress");
  isCommutable = R->getValueAsBit("isCommutable");
  isTerminator = R->getValueAsBit("isTerminator");
  isReMaterializable = R->getValueAsBit("isReMaterializable");
  hasDelaySlot = R->getValueAsBit("hasDelaySlot");
  usesCustomInserter = R->getValueAsBit("usesCustomInserter");
  hasPostISelHook = R->getValueAsBit("hasPostISelHook");
  hasCtrlDep   = R->getValueAsBit("hasCtrlDep");
  isNotDuplicable = R->getValueAsBit("isNotDuplicable");
  isRegSequence = R->getValueAsBit("isRegSequence");
  isExtractSubreg = R->getValueAsBit("isExtractSubreg");
  isInsertSubreg = R->getValueAsBit("isInsertSubreg");
  isConvergent = R->getValueAsBit("isConvergent");
  hasNoSchedulingInfo = R->getValueAsBit("hasNoSchedulingInfo");
  FastISelShouldIgnore = R->getValueAsBit("FastISelShouldIgnore");
  variadicOpsAreDefs = R->getValueAsBit("variadicOpsAreDefs");

  bool Unset;
  mayLoad      = R->getValueAsBitOrUnset("mayLoad", Unset);
  mayLoad_Unset = Unset;
  mayStore     = R->getValueAsBitOrUnset("mayStore", Unset);
  mayStore_Unset = Unset;
  hasSideEffects = R->getValueAsBitOrUnset("hasSideEffects", Unset);
  hasSideEffects_Unset = Unset;

  isAsCheapAsAMove = R->getValueAsBit("isAsCheapAsAMove");
  hasExtraSrcRegAllocReq = R->getValueAsBit("hasExtraSrcRegAllocReq");
  hasExtraDefRegAllocReq = R->getValueAsBit("hasExtraDefRegAllocReq");
  isCodeGenOnly = R->getValueAsBit("isCodeGenOnly");
  isPseudo = R->getValueAsBit("isPseudo");
  ImplicitDefs = R->getValueAsListOfDefs("Defs");
  ImplicitUses = R->getValueAsListOfDefs("Uses");

  // This flag is only inferred from the pattern.
  hasChain = false;
  hasChain_Inferred = false;

  // Parse Constraints.
  ParseConstraints(R->getValueAsString("Constraints"), Operands, R);

  // Parse the DisableEncoding field.
  Operands.ProcessDisableEncoding(R->getValueAsString("DisableEncoding"));

  // First check for a ComplexDeprecationPredicate.
  if (R->getValue("ComplexDeprecationPredicate")) {
    HasComplexDeprecationPredicate = true;
    DeprecatedReason = R->getValueAsString("ComplexDeprecationPredicate");
  } else if (RecordVal *Dep = R->getValue("DeprecatedFeatureMask")) {
    // Check if we have a Subtarget feature mask.
    HasComplexDeprecationPredicate = false;
    DeprecatedReason = Dep->getValue()->getAsString();
  } else {
    // This instruction isn't deprecated.
    HasComplexDeprecationPredicate = false;
    DeprecatedReason = "";
  }
}

/// HasOneImplicitDefWithKnownVT - If the instruction has at least one
/// implicit def and it has a known VT, return the VT, otherwise return
/// MVT::Other.
MVT::SimpleValueType CodeGenInstruction::
HasOneImplicitDefWithKnownVT(const CodeGenTarget &TargetInfo) const {
  if (ImplicitDefs.empty()) return MVT::Other;

  // Check to see if the first implicit def has a resolvable type.
  Record *FirstImplicitDef = ImplicitDefs[0];
  assert(FirstImplicitDef->isSubClassOf("Register"));
  const std::vector<ValueTypeByHwMode> &RegVTs =
    TargetInfo.getRegisterVTs(FirstImplicitDef);
  if (RegVTs.size() == 1 && RegVTs[0].isSimple())
    return RegVTs[0].getSimple().SimpleTy;
  return MVT::Other;
}


/// FlattenAsmStringVariants - Flatten the specified AsmString to only
/// include text from the specified variant, returning the new string.
std::string CodeGenInstruction::
FlattenAsmStringVariants(StringRef Cur, unsigned Variant) {
  std::string Res = "";

  for (;;) {
    // Find the start of the next variant string.
    size_t VariantsStart = 0;
    for (size_t e = Cur.size(); VariantsStart != e; ++VariantsStart)
      if (Cur[VariantsStart] == '{' &&
          (VariantsStart == 0 || (Cur[VariantsStart-1] != '$' &&
                                  Cur[VariantsStart-1] != '\\')))
        break;

    // Add the prefix to the result.
    Res += Cur.slice(0, VariantsStart);
    if (VariantsStart == Cur.size())
      break;

    ++VariantsStart; // Skip the '{'.

    // Scan to the end of the variants string.
    size_t VariantsEnd = VariantsStart;
    unsigned NestedBraces = 1;
    for (size_t e = Cur.size(); VariantsEnd != e; ++VariantsEnd) {
      if (Cur[VariantsEnd] == '}' && Cur[VariantsEnd-1] != '\\') {
        if (--NestedBraces == 0)
          break;
      } else if (Cur[VariantsEnd] == '{')
        ++NestedBraces;
    }

    // Select the Nth variant (or empty).
    StringRef Selection = Cur.slice(VariantsStart, VariantsEnd);
    for (unsigned i = 0; i != Variant; ++i)
      Selection = Selection.split('|').second;
    Res += Selection.split('|').first;

    assert(VariantsEnd != Cur.size() &&
           "Unterminated variants in assembly string!");
    Cur = Cur.substr(VariantsEnd + 1);
  }

  return Res;
}

bool CodeGenInstruction::isOperandAPointer(unsigned i) const {
  if (DagInit *ConstraintList = TheDef->getValueAsDag("InOperandList")) {
    if (i < ConstraintList->getNumArgs()) {
      if (DefInit *Constraint = dyn_cast<DefInit>(ConstraintList->getArg(i))) {
        return Constraint->getDef()->isSubClassOf("TypedOperand") &&
               Constraint->getDef()->getValueAsBit("IsPointer");
      }
    }
  }
  return false;
}

//===----------------------------------------------------------------------===//
/// CodeGenInstAlias Implementation
//===----------------------------------------------------------------------===//

/// tryAliasOpMatch - This is a helper function for the CodeGenInstAlias
/// constructor.  It checks if an argument in an InstAlias pattern matches
/// the corresponding operand of the instruction.  It returns true on a
/// successful match, with ResOp set to the result operand to be used.
bool CodeGenInstAlias::tryAliasOpMatch(DagInit *Result, unsigned AliasOpNo,
                                       Record *InstOpRec, bool hasSubOps,
                                       ArrayRef<SMLoc> Loc, CodeGenTarget &T,
                                       ResultOperand &ResOp) {
  Init *Arg = Result->getArg(AliasOpNo);
  DefInit *ADI = dyn_cast<DefInit>(Arg);
  Record *ResultRecord = ADI ? ADI->getDef() : nullptr;

  if (ADI && ADI->getDef() == InstOpRec) {
    // If the operand is a record, it must have a name, and the record type
    // must match up with the instruction's argument type.
    if (!Result->getArgName(AliasOpNo))
      PrintFatalError(Loc, "result argument #" + Twine(AliasOpNo) +
                           " must have a name!");
    ResOp = ResultOperand(Result->getArgNameStr(AliasOpNo), ResultRecord);
    return true;
  }

  // For register operands, the source register class can be a subclass
  // of the instruction register class, not just an exact match.
  if (InstOpRec->isSubClassOf("RegisterOperand"))
    InstOpRec = InstOpRec->getValueAsDef("RegClass");

  if (ADI && ADI->getDef()->isSubClassOf("RegisterOperand"))
    ADI = ADI->getDef()->getValueAsDef("RegClass")->getDefInit();

  if (ADI && ADI->getDef()->isSubClassOf("RegisterClass")) {
    if (!InstOpRec->isSubClassOf("RegisterClass"))
      return false;
    if (!T.getRegisterClass(InstOpRec)
              .hasSubClass(&T.getRegisterClass(ADI->getDef())))
      return false;
    ResOp = ResultOperand(Result->getArgNameStr(AliasOpNo), ResultRecord);
    return true;
  }

  // Handle explicit registers.
  if (ADI && ADI->getDef()->isSubClassOf("Register")) {
    if (InstOpRec->isSubClassOf("OptionalDefOperand")) {
      DagInit *DI = InstOpRec->getValueAsDag("MIOperandInfo");
      // The operand info should only have a single (register) entry. We
      // want the register class of it.
      InstOpRec = cast<DefInit>(DI->getArg(0))->getDef();
    }

    if (!InstOpRec->isSubClassOf("RegisterClass"))
      return false;

    if (!T.getRegisterClass(InstOpRec)
        .contains(T.getRegBank().getReg(ADI->getDef())))
      PrintFatalError(Loc, "fixed register " + ADI->getDef()->getName() +
                      " is not a member of the " + InstOpRec->getName() +
                      " register class!");

    if (Result->getArgName(AliasOpNo))
      PrintFatalError(Loc, "result fixed register argument must "
                      "not have a name!");

    ResOp = ResultOperand(ResultRecord);
    return true;
  }

  // Handle "zero_reg" for optional def operands.
  if (ADI && ADI->getDef()->getName() == "zero_reg") {

    // Check if this is an optional def.
    // Tied operands where the source is a sub-operand of a complex operand
    // need to represent both operands in the alias destination instruction.
    // Allow zero_reg for the tied portion. This can and should go away once
    // the MC representation of things doesn't use tied operands at all.
    //if (!InstOpRec->isSubClassOf("OptionalDefOperand"))
    //  throw TGError(Loc, "reg0 used for result that is not an "
    //                "OptionalDefOperand!");

    ResOp = ResultOperand(static_cast<Record*>(nullptr));
    return true;
  }

  // Literal integers.
  if (IntInit *II = dyn_cast<IntInit>(Arg)) {
    if (hasSubOps || !InstOpRec->isSubClassOf("Operand"))
      return false;
    // Integer arguments can't have names.
    if (Result->getArgName(AliasOpNo))
      PrintFatalError(Loc, "result argument #" + Twine(AliasOpNo) +
                      " must not have a name!");
    ResOp = ResultOperand(II->getValue());
    return true;
  }

  // Bits<n> (also used for 0bxx literals)
  if (BitsInit *BI = dyn_cast<BitsInit>(Arg)) {
    if (hasSubOps || !InstOpRec->isSubClassOf("Operand"))
      return false;
    if (!BI->isComplete())
      return false;
    // Convert the bits init to an integer and use that for the result.
    IntInit *II =
      dyn_cast_or_null<IntInit>(BI->convertInitializerTo(IntRecTy::get()));
    if (!II)
      return false;
    ResOp = ResultOperand(II->getValue());
    return true;
  }

  // If both are Operands with the same MVT, allow the conversion. It's
  // up to the user to make sure the values are appropriate, just like
  // for isel Pat's.
  if (InstOpRec->isSubClassOf("Operand") && ADI &&
      ADI->getDef()->isSubClassOf("Operand")) {
    // FIXME: What other attributes should we check here? Identical
    // MIOperandInfo perhaps?
    if (InstOpRec->getValueInit("Type") != ADI->getDef()->getValueInit("Type"))
      return false;
    ResOp = ResultOperand(Result->getArgNameStr(AliasOpNo), ADI->getDef());
    return true;
  }

  return false;
}

unsigned CodeGenInstAlias::ResultOperand::getMINumOperands() const {
  if (!isRecord())
    return 1;

  Record *Rec = getRecord();
  if (!Rec->isSubClassOf("Operand"))
    return 1;

  DagInit *MIOpInfo = Rec->getValueAsDag("MIOperandInfo");
  if (MIOpInfo->getNumArgs() == 0) {
    // Unspecified, so it defaults to 1
    return 1;
  }

  return MIOpInfo->getNumArgs();
}

CodeGenInstAlias::CodeGenInstAlias(Record *R, CodeGenTarget &T)
    : TheDef(R) {
  Result = R->getValueAsDag("ResultInst");
  AsmString = R->getValueAsString("AsmString");


  // Verify that the root of the result is an instruction.
  DefInit *DI = dyn_cast<DefInit>(Result->getOperator());
  if (!DI || !DI->getDef()->isSubClassOf("Instruction"))
    PrintFatalError(R->getLoc(),
                    "result of inst alias should be an instruction");

  ResultInst = &T.getInstruction(DI->getDef());

  // NameClass - If argument names are repeated, we need to verify they have
  // the same class.
  StringMap<Record*> NameClass;
  for (unsigned i = 0, e = Result->getNumArgs(); i != e; ++i) {
    DefInit *ADI = dyn_cast<DefInit>(Result->getArg(i));
    if (!ADI || !Result->getArgName(i))
      continue;
    // Verify we don't have something like: (someinst GR16:$foo, GR32:$foo)
    // $foo can exist multiple times in the result list, but it must have the
    // same type.
    Record *&Entry = NameClass[Result->getArgNameStr(i)];
    if (Entry && Entry != ADI->getDef())
      PrintFatalError(R->getLoc(), "result value $" + Result->getArgNameStr(i) +
                      " is both " + Entry->getName() + " and " +
                      ADI->getDef()->getName() + "!");
    Entry = ADI->getDef();
  }

  // Decode and validate the arguments of the result.
  unsigned AliasOpNo = 0;
  for (unsigned i = 0, e = ResultInst->Operands.size(); i != e; ++i) {

    // Tied registers don't have an entry in the result dag unless they're part
    // of a complex operand, in which case we include them anyways, as we
    // don't have any other way to specify the whole operand.
    if (ResultInst->Operands[i].MINumOperands == 1 &&
        ResultInst->Operands[i].getTiedRegister() != -1) {
      // Tied operands of different RegisterClass should be explicit within an
      // instruction's syntax and so cannot be skipped.
      int TiedOpNum = ResultInst->Operands[i].getTiedRegister();
      if (ResultInst->Operands[i].Rec->getName() ==
          ResultInst->Operands[TiedOpNum].Rec->getName())
        continue;
    }

    if (AliasOpNo >= Result->getNumArgs())
      PrintFatalError(R->getLoc(), "not enough arguments for instruction!");

    Record *InstOpRec = ResultInst->Operands[i].Rec;
    unsigned NumSubOps = ResultInst->Operands[i].MINumOperands;
    ResultOperand ResOp(static_cast<int64_t>(0));
    if (tryAliasOpMatch(Result, AliasOpNo, InstOpRec, (NumSubOps > 1),
                        R->getLoc(), T, ResOp)) {
      // If this is a simple operand, or a complex operand with a custom match
      // class, then we can match is verbatim.
      if (NumSubOps == 1 ||
          (InstOpRec->getValue("ParserMatchClass") &&
           InstOpRec->getValueAsDef("ParserMatchClass")
             ->getValueAsString("Name") != "Imm")) {
        ResultOperands.push_back(ResOp);
        ResultInstOperandIndex.push_back(std::make_pair(i, -1));
        ++AliasOpNo;

      // Otherwise, we need to match each of the suboperands individually.
      } else {
         DagInit *MIOI = ResultInst->Operands[i].MIOperandInfo;
         for (unsigned SubOp = 0; SubOp != NumSubOps; ++SubOp) {
          Record *SubRec = cast<DefInit>(MIOI->getArg(SubOp))->getDef();

          // Take care to instantiate each of the suboperands with the correct
          // nomenclature: $foo.bar
          ResultOperands.emplace_back(
            Result->getArgName(AliasOpNo)->getAsUnquotedString() + "." +
            MIOI->getArgName(SubOp)->getAsUnquotedString(), SubRec);
          ResultInstOperandIndex.push_back(std::make_pair(i, SubOp));
         }
         ++AliasOpNo;
      }
      continue;
    }

    // If the argument did not match the instruction operand, and the operand
    // is composed of multiple suboperands, try matching the suboperands.
    if (NumSubOps > 1) {
      DagInit *MIOI = ResultInst->Operands[i].MIOperandInfo;
      for (unsigned SubOp = 0; SubOp != NumSubOps; ++SubOp) {
        if (AliasOpNo >= Result->getNumArgs())
          PrintFatalError(R->getLoc(), "not enough arguments for instruction!");
        Record *SubRec = cast<DefInit>(MIOI->getArg(SubOp))->getDef();
        if (tryAliasOpMatch(Result, AliasOpNo, SubRec, false,
                            R->getLoc(), T, ResOp)) {
          ResultOperands.push_back(ResOp);
          ResultInstOperandIndex.push_back(std::make_pair(i, SubOp));
          ++AliasOpNo;
        } else {
          PrintFatalError(R->getLoc(), "result argument #" + Twine(AliasOpNo) +
                        " does not match instruction operand class " +
                        (SubOp == 0 ? InstOpRec->getName() :SubRec->getName()));
        }
      }
      continue;
    }
    PrintFatalError(R->getLoc(), "result argument #" + Twine(AliasOpNo) +
                    " does not match instruction operand class " +
                    InstOpRec->getName());
  }

  if (AliasOpNo != Result->getNumArgs())
    PrintFatalError(R->getLoc(), "too many operands for instruction!");
}
