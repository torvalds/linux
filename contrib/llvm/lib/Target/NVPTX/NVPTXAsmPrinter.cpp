//===-- NVPTXAsmPrinter.cpp - NVPTX LLVM assembly writer ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains a printer that converts from our internal representation
// of machine-dependent LLVM code to NVPTX assembly language.
//
//===----------------------------------------------------------------------===//

#include "NVPTXAsmPrinter.h"
#include "InstPrinter/NVPTXInstPrinter.h"
#include "MCTargetDesc/NVPTXBaseInfo.h"
#include "MCTargetDesc/NVPTXMCAsmInfo.h"
#include "MCTargetDesc/NVPTXTargetStreamer.h"
#include "NVPTX.h"
#include "NVPTXMCExpr.h"
#include "NVPTXMachineFunctionInfo.h"
#include "NVPTXRegisterInfo.h"
#include "NVPTXSubtarget.h"
#include "NVPTXTargetMachine.h"
#include "NVPTXUtilities.h"
#include "cl_common_defines.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Triple.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/CodeGen/Analysis.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MachineValueType.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/Utils/UnrollLoop.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

#define DEPOTNAME "__local_depot"

/// DiscoverDependentGlobals - Return a set of GlobalVariables on which \p V
/// depends.
static void
DiscoverDependentGlobals(const Value *V,
                         DenseSet<const GlobalVariable *> &Globals) {
  if (const GlobalVariable *GV = dyn_cast<GlobalVariable>(V))
    Globals.insert(GV);
  else {
    if (const User *U = dyn_cast<User>(V)) {
      for (unsigned i = 0, e = U->getNumOperands(); i != e; ++i) {
        DiscoverDependentGlobals(U->getOperand(i), Globals);
      }
    }
  }
}

/// VisitGlobalVariableForEmission - Add \p GV to the list of GlobalVariable
/// instances to be emitted, but only after any dependents have been added
/// first.s
static void
VisitGlobalVariableForEmission(const GlobalVariable *GV,
                               SmallVectorImpl<const GlobalVariable *> &Order,
                               DenseSet<const GlobalVariable *> &Visited,
                               DenseSet<const GlobalVariable *> &Visiting) {
  // Have we already visited this one?
  if (Visited.count(GV))
    return;

  // Do we have a circular dependency?
  if (!Visiting.insert(GV).second)
    report_fatal_error("Circular dependency found in global variable set");

  // Make sure we visit all dependents first
  DenseSet<const GlobalVariable *> Others;
  for (unsigned i = 0, e = GV->getNumOperands(); i != e; ++i)
    DiscoverDependentGlobals(GV->getOperand(i), Others);

  for (DenseSet<const GlobalVariable *>::iterator I = Others.begin(),
                                                  E = Others.end();
       I != E; ++I)
    VisitGlobalVariableForEmission(*I, Order, Visited, Visiting);

  // Now we can visit ourself
  Order.push_back(GV);
  Visited.insert(GV);
  Visiting.erase(GV);
}

void NVPTXAsmPrinter::EmitInstruction(const MachineInstr *MI) {
  MCInst Inst;
  lowerToMCInst(MI, Inst);
  EmitToStreamer(*OutStreamer, Inst);
}

// Handle symbol backtracking for targets that do not support image handles
bool NVPTXAsmPrinter::lowerImageHandleOperand(const MachineInstr *MI,
                                           unsigned OpNo, MCOperand &MCOp) {
  const MachineOperand &MO = MI->getOperand(OpNo);
  const MCInstrDesc &MCID = MI->getDesc();

  if (MCID.TSFlags & NVPTXII::IsTexFlag) {
    // This is a texture fetch, so operand 4 is a texref and operand 5 is
    // a samplerref
    if (OpNo == 4 && MO.isImm()) {
      lowerImageHandleSymbol(MO.getImm(), MCOp);
      return true;
    }
    if (OpNo == 5 && MO.isImm() && !(MCID.TSFlags & NVPTXII::IsTexModeUnifiedFlag)) {
      lowerImageHandleSymbol(MO.getImm(), MCOp);
      return true;
    }

    return false;
  } else if (MCID.TSFlags & NVPTXII::IsSuldMask) {
    unsigned VecSize =
      1 << (((MCID.TSFlags & NVPTXII::IsSuldMask) >> NVPTXII::IsSuldShift) - 1);

    // For a surface load of vector size N, the Nth operand will be the surfref
    if (OpNo == VecSize && MO.isImm()) {
      lowerImageHandleSymbol(MO.getImm(), MCOp);
      return true;
    }

    return false;
  } else if (MCID.TSFlags & NVPTXII::IsSustFlag) {
    // This is a surface store, so operand 0 is a surfref
    if (OpNo == 0 && MO.isImm()) {
      lowerImageHandleSymbol(MO.getImm(), MCOp);
      return true;
    }

    return false;
  } else if (MCID.TSFlags & NVPTXII::IsSurfTexQueryFlag) {
    // This is a query, so operand 1 is a surfref/texref
    if (OpNo == 1 && MO.isImm()) {
      lowerImageHandleSymbol(MO.getImm(), MCOp);
      return true;
    }

    return false;
  }

  return false;
}

void NVPTXAsmPrinter::lowerImageHandleSymbol(unsigned Index, MCOperand &MCOp) {
  // Ewwww
  LLVMTargetMachine &TM = const_cast<LLVMTargetMachine&>(MF->getTarget());
  NVPTXTargetMachine &nvTM = static_cast<NVPTXTargetMachine&>(TM);
  const NVPTXMachineFunctionInfo *MFI = MF->getInfo<NVPTXMachineFunctionInfo>();
  const char *Sym = MFI->getImageHandleSymbol(Index);
  std::string *SymNamePtr =
    nvTM.getManagedStrPool()->getManagedString(Sym);
  MCOp = GetSymbolRef(OutContext.getOrCreateSymbol(StringRef(*SymNamePtr)));
}

void NVPTXAsmPrinter::lowerToMCInst(const MachineInstr *MI, MCInst &OutMI) {
  OutMI.setOpcode(MI->getOpcode());
  // Special: Do not mangle symbol operand of CALL_PROTOTYPE
  if (MI->getOpcode() == NVPTX::CALL_PROTOTYPE) {
    const MachineOperand &MO = MI->getOperand(0);
    OutMI.addOperand(GetSymbolRef(
      OutContext.getOrCreateSymbol(Twine(MO.getSymbolName()))));
    return;
  }

  const NVPTXSubtarget &STI = MI->getMF()->getSubtarget<NVPTXSubtarget>();
  for (unsigned i = 0, e = MI->getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = MI->getOperand(i);

    MCOperand MCOp;
    if (!STI.hasImageHandles()) {
      if (lowerImageHandleOperand(MI, i, MCOp)) {
        OutMI.addOperand(MCOp);
        continue;
      }
    }

    if (lowerOperand(MO, MCOp))
      OutMI.addOperand(MCOp);
  }
}

bool NVPTXAsmPrinter::lowerOperand(const MachineOperand &MO,
                                   MCOperand &MCOp) {
  switch (MO.getType()) {
  default: llvm_unreachable("unknown operand type");
  case MachineOperand::MO_Register:
    MCOp = MCOperand::createReg(encodeVirtualRegister(MO.getReg()));
    break;
  case MachineOperand::MO_Immediate:
    MCOp = MCOperand::createImm(MO.getImm());
    break;
  case MachineOperand::MO_MachineBasicBlock:
    MCOp = MCOperand::createExpr(MCSymbolRefExpr::create(
        MO.getMBB()->getSymbol(), OutContext));
    break;
  case MachineOperand::MO_ExternalSymbol:
    MCOp = GetSymbolRef(GetExternalSymbolSymbol(MO.getSymbolName()));
    break;
  case MachineOperand::MO_GlobalAddress:
    MCOp = GetSymbolRef(getSymbol(MO.getGlobal()));
    break;
  case MachineOperand::MO_FPImmediate: {
    const ConstantFP *Cnt = MO.getFPImm();
    const APFloat &Val = Cnt->getValueAPF();

    switch (Cnt->getType()->getTypeID()) {
    default: report_fatal_error("Unsupported FP type"); break;
    case Type::HalfTyID:
      MCOp = MCOperand::createExpr(
        NVPTXFloatMCExpr::createConstantFPHalf(Val, OutContext));
      break;
    case Type::FloatTyID:
      MCOp = MCOperand::createExpr(
        NVPTXFloatMCExpr::createConstantFPSingle(Val, OutContext));
      break;
    case Type::DoubleTyID:
      MCOp = MCOperand::createExpr(
        NVPTXFloatMCExpr::createConstantFPDouble(Val, OutContext));
      break;
    }
    break;
  }
  }
  return true;
}

unsigned NVPTXAsmPrinter::encodeVirtualRegister(unsigned Reg) {
  if (TargetRegisterInfo::isVirtualRegister(Reg)) {
    const TargetRegisterClass *RC = MRI->getRegClass(Reg);

    DenseMap<unsigned, unsigned> &RegMap = VRegMapping[RC];
    unsigned RegNum = RegMap[Reg];

    // Encode the register class in the upper 4 bits
    // Must be kept in sync with NVPTXInstPrinter::printRegName
    unsigned Ret = 0;
    if (RC == &NVPTX::Int1RegsRegClass) {
      Ret = (1 << 28);
    } else if (RC == &NVPTX::Int16RegsRegClass) {
      Ret = (2 << 28);
    } else if (RC == &NVPTX::Int32RegsRegClass) {
      Ret = (3 << 28);
    } else if (RC == &NVPTX::Int64RegsRegClass) {
      Ret = (4 << 28);
    } else if (RC == &NVPTX::Float32RegsRegClass) {
      Ret = (5 << 28);
    } else if (RC == &NVPTX::Float64RegsRegClass) {
      Ret = (6 << 28);
    } else if (RC == &NVPTX::Float16RegsRegClass) {
      Ret = (7 << 28);
    } else if (RC == &NVPTX::Float16x2RegsRegClass) {
      Ret = (8 << 28);
    } else {
      report_fatal_error("Bad register class");
    }

    // Insert the vreg number
    Ret |= (RegNum & 0x0FFFFFFF);
    return Ret;
  } else {
    // Some special-use registers are actually physical registers.
    // Encode this as the register class ID of 0 and the real register ID.
    return Reg & 0x0FFFFFFF;
  }
}

MCOperand NVPTXAsmPrinter::GetSymbolRef(const MCSymbol *Symbol) {
  const MCExpr *Expr;
  Expr = MCSymbolRefExpr::create(Symbol, MCSymbolRefExpr::VK_None,
                                 OutContext);
  return MCOperand::createExpr(Expr);
}

void NVPTXAsmPrinter::printReturnValStr(const Function *F, raw_ostream &O) {
  const DataLayout &DL = getDataLayout();
  const NVPTXSubtarget &STI = TM.getSubtarget<NVPTXSubtarget>(*F);
  const TargetLowering *TLI = STI.getTargetLowering();

  Type *Ty = F->getReturnType();

  bool isABI = (STI.getSmVersion() >= 20);

  if (Ty->getTypeID() == Type::VoidTyID)
    return;

  O << " (";

  if (isABI) {
    if (Ty->isFloatingPointTy() || (Ty->isIntegerTy() && !Ty->isIntegerTy(128))) {
      unsigned size = 0;
      if (auto *ITy = dyn_cast<IntegerType>(Ty)) {
        size = ITy->getBitWidth();
      } else {
        assert(Ty->isFloatingPointTy() && "Floating point type expected here");
        size = Ty->getPrimitiveSizeInBits();
      }
      // PTX ABI requires all scalar return values to be at least 32
      // bits in size.  fp16 normally uses .b16 as its storage type in
      // PTX, so its size must be adjusted here, too.
      if (size < 32)
        size = 32;

      O << ".param .b" << size << " func_retval0";
    } else if (isa<PointerType>(Ty)) {
      O << ".param .b" << TLI->getPointerTy(DL).getSizeInBits()
        << " func_retval0";
    } else if (Ty->isAggregateType() || Ty->isVectorTy() || Ty->isIntegerTy(128)) {
      unsigned totalsz = DL.getTypeAllocSize(Ty);
      unsigned retAlignment = 0;
      if (!getAlign(*F, 0, retAlignment))
        retAlignment = DL.getABITypeAlignment(Ty);
      O << ".param .align " << retAlignment << " .b8 func_retval0[" << totalsz
        << "]";
    } else
      llvm_unreachable("Unknown return type");
  } else {
    SmallVector<EVT, 16> vtparts;
    ComputeValueVTs(*TLI, DL, Ty, vtparts);
    unsigned idx = 0;
    for (unsigned i = 0, e = vtparts.size(); i != e; ++i) {
      unsigned elems = 1;
      EVT elemtype = vtparts[i];
      if (vtparts[i].isVector()) {
        elems = vtparts[i].getVectorNumElements();
        elemtype = vtparts[i].getVectorElementType();
      }

      for (unsigned j = 0, je = elems; j != je; ++j) {
        unsigned sz = elemtype.getSizeInBits();
        if (elemtype.isInteger() && (sz < 32))
          sz = 32;
        O << ".reg .b" << sz << " func_retval" << idx;
        if (j < je - 1)
          O << ", ";
        ++idx;
      }
      if (i < e - 1)
        O << ", ";
    }
  }
  O << ") ";
}

void NVPTXAsmPrinter::printReturnValStr(const MachineFunction &MF,
                                        raw_ostream &O) {
  const Function &F = MF.getFunction();
  printReturnValStr(&F, O);
}

// Return true if MBB is the header of a loop marked with
// llvm.loop.unroll.disable.
// TODO: consider "#pragma unroll 1" which is equivalent to "#pragma nounroll".
bool NVPTXAsmPrinter::isLoopHeaderOfNoUnroll(
    const MachineBasicBlock &MBB) const {
  MachineLoopInfo &LI = getAnalysis<MachineLoopInfo>();
  // We insert .pragma "nounroll" only to the loop header.
  if (!LI.isLoopHeader(&MBB))
    return false;

  // llvm.loop.unroll.disable is marked on the back edges of a loop. Therefore,
  // we iterate through each back edge of the loop with header MBB, and check
  // whether its metadata contains llvm.loop.unroll.disable.
  for (auto I = MBB.pred_begin(); I != MBB.pred_end(); ++I) {
    const MachineBasicBlock *PMBB = *I;
    if (LI.getLoopFor(PMBB) != LI.getLoopFor(&MBB)) {
      // Edges from other loops to MBB are not back edges.
      continue;
    }
    if (const BasicBlock *PBB = PMBB->getBasicBlock()) {
      if (MDNode *LoopID =
              PBB->getTerminator()->getMetadata(LLVMContext::MD_loop)) {
        if (GetUnrollMetadata(LoopID, "llvm.loop.unroll.disable"))
          return true;
      }
    }
  }
  return false;
}

void NVPTXAsmPrinter::EmitBasicBlockStart(const MachineBasicBlock &MBB) const {
  AsmPrinter::EmitBasicBlockStart(MBB);
  if (isLoopHeaderOfNoUnroll(MBB))
    OutStreamer->EmitRawText(StringRef("\t.pragma \"nounroll\";\n"));
}

void NVPTXAsmPrinter::EmitFunctionEntryLabel() {
  SmallString<128> Str;
  raw_svector_ostream O(Str);

  if (!GlobalsEmitted) {
    emitGlobals(*MF->getFunction().getParent());
    GlobalsEmitted = true;
  }

  // Set up
  MRI = &MF->getRegInfo();
  F = &MF->getFunction();
  emitLinkageDirective(F, O);
  if (isKernelFunction(*F))
    O << ".entry ";
  else {
    O << ".func ";
    printReturnValStr(*MF, O);
  }

  CurrentFnSym->print(O, MAI);

  emitFunctionParamList(*MF, O);

  if (isKernelFunction(*F))
    emitKernelFunctionDirectives(*F, O);

  OutStreamer->EmitRawText(O.str());

  VRegMapping.clear();
  // Emit open brace for function body.
  OutStreamer->EmitRawText(StringRef("{\n"));
  setAndEmitFunctionVirtualRegisters(*MF);
}

bool NVPTXAsmPrinter::runOnMachineFunction(MachineFunction &F) {
  bool Result = AsmPrinter::runOnMachineFunction(F);
  // Emit closing brace for the body of function F.
  // The closing brace must be emitted here because we need to emit additional
  // debug labels/data after the last basic block.
  // We need to emit the closing brace here because we don't have function that
  // finished emission of the function body.
  OutStreamer->EmitRawText(StringRef("}\n"));
  return Result;
}

void NVPTXAsmPrinter::EmitFunctionBodyStart() {
  SmallString<128> Str;
  raw_svector_ostream O(Str);
  emitDemotedVars(&MF->getFunction(), O);
  OutStreamer->EmitRawText(O.str());
}

void NVPTXAsmPrinter::EmitFunctionBodyEnd() {
  VRegMapping.clear();
}

const MCSymbol *NVPTXAsmPrinter::getFunctionFrameSymbol() const {
    SmallString<128> Str;
    raw_svector_ostream(Str) << DEPOTNAME << getFunctionNumber();
    return OutContext.getOrCreateSymbol(Str);
}

void NVPTXAsmPrinter::emitImplicitDef(const MachineInstr *MI) const {
  unsigned RegNo = MI->getOperand(0).getReg();
  if (TargetRegisterInfo::isVirtualRegister(RegNo)) {
    OutStreamer->AddComment(Twine("implicit-def: ") +
                            getVirtualRegisterName(RegNo));
  } else {
    const NVPTXSubtarget &STI = MI->getMF()->getSubtarget<NVPTXSubtarget>();
    OutStreamer->AddComment(Twine("implicit-def: ") +
                            STI.getRegisterInfo()->getName(RegNo));
  }
  OutStreamer->AddBlankLine();
}

void NVPTXAsmPrinter::emitKernelFunctionDirectives(const Function &F,
                                                   raw_ostream &O) const {
  // If the NVVM IR has some of reqntid* specified, then output
  // the reqntid directive, and set the unspecified ones to 1.
  // If none of reqntid* is specified, don't output reqntid directive.
  unsigned reqntidx, reqntidy, reqntidz;
  bool specified = false;
  if (!getReqNTIDx(F, reqntidx))
    reqntidx = 1;
  else
    specified = true;
  if (!getReqNTIDy(F, reqntidy))
    reqntidy = 1;
  else
    specified = true;
  if (!getReqNTIDz(F, reqntidz))
    reqntidz = 1;
  else
    specified = true;

  if (specified)
    O << ".reqntid " << reqntidx << ", " << reqntidy << ", " << reqntidz
      << "\n";

  // If the NVVM IR has some of maxntid* specified, then output
  // the maxntid directive, and set the unspecified ones to 1.
  // If none of maxntid* is specified, don't output maxntid directive.
  unsigned maxntidx, maxntidy, maxntidz;
  specified = false;
  if (!getMaxNTIDx(F, maxntidx))
    maxntidx = 1;
  else
    specified = true;
  if (!getMaxNTIDy(F, maxntidy))
    maxntidy = 1;
  else
    specified = true;
  if (!getMaxNTIDz(F, maxntidz))
    maxntidz = 1;
  else
    specified = true;

  if (specified)
    O << ".maxntid " << maxntidx << ", " << maxntidy << ", " << maxntidz
      << "\n";

  unsigned mincta;
  if (getMinCTASm(F, mincta))
    O << ".minnctapersm " << mincta << "\n";

  unsigned maxnreg;
  if (getMaxNReg(F, maxnreg))
    O << ".maxnreg " << maxnreg << "\n";
}

std::string
NVPTXAsmPrinter::getVirtualRegisterName(unsigned Reg) const {
  const TargetRegisterClass *RC = MRI->getRegClass(Reg);

  std::string Name;
  raw_string_ostream NameStr(Name);

  VRegRCMap::const_iterator I = VRegMapping.find(RC);
  assert(I != VRegMapping.end() && "Bad register class");
  const DenseMap<unsigned, unsigned> &RegMap = I->second;

  VRegMap::const_iterator VI = RegMap.find(Reg);
  assert(VI != RegMap.end() && "Bad virtual register");
  unsigned MappedVR = VI->second;

  NameStr << getNVPTXRegClassStr(RC) << MappedVR;

  NameStr.flush();
  return Name;
}

void NVPTXAsmPrinter::emitVirtualRegister(unsigned int vr,
                                          raw_ostream &O) {
  O << getVirtualRegisterName(vr);
}

void NVPTXAsmPrinter::printVecModifiedImmediate(
    const MachineOperand &MO, const char *Modifier, raw_ostream &O) {
  static const char vecelem[] = { '0', '1', '2', '3', '0', '1', '2', '3' };
  int Imm = (int) MO.getImm();
  if (0 == strcmp(Modifier, "vecelem"))
    O << "_" << vecelem[Imm];
  else if (0 == strcmp(Modifier, "vecv4comm1")) {
    if ((Imm < 0) || (Imm > 3))
      O << "//";
  } else if (0 == strcmp(Modifier, "vecv4comm2")) {
    if ((Imm < 4) || (Imm > 7))
      O << "//";
  } else if (0 == strcmp(Modifier, "vecv4pos")) {
    if (Imm < 0)
      Imm = 0;
    O << "_" << vecelem[Imm % 4];
  } else if (0 == strcmp(Modifier, "vecv2comm1")) {
    if ((Imm < 0) || (Imm > 1))
      O << "//";
  } else if (0 == strcmp(Modifier, "vecv2comm2")) {
    if ((Imm < 2) || (Imm > 3))
      O << "//";
  } else if (0 == strcmp(Modifier, "vecv2pos")) {
    if (Imm < 0)
      Imm = 0;
    O << "_" << vecelem[Imm % 2];
  } else
    llvm_unreachable("Unknown Modifier on immediate operand");
}

void NVPTXAsmPrinter::emitDeclaration(const Function *F, raw_ostream &O) {
  emitLinkageDirective(F, O);
  if (isKernelFunction(*F))
    O << ".entry ";
  else
    O << ".func ";
  printReturnValStr(F, O);
  getSymbol(F)->print(O, MAI);
  O << "\n";
  emitFunctionParamList(F, O);
  O << ";\n";
}

static bool usedInGlobalVarDef(const Constant *C) {
  if (!C)
    return false;

  if (const GlobalVariable *GV = dyn_cast<GlobalVariable>(C)) {
    return GV->getName() != "llvm.used";
  }

  for (const User *U : C->users())
    if (const Constant *C = dyn_cast<Constant>(U))
      if (usedInGlobalVarDef(C))
        return true;

  return false;
}

static bool usedInOneFunc(const User *U, Function const *&oneFunc) {
  if (const GlobalVariable *othergv = dyn_cast<GlobalVariable>(U)) {
    if (othergv->getName() == "llvm.used")
      return true;
  }

  if (const Instruction *instr = dyn_cast<Instruction>(U)) {
    if (instr->getParent() && instr->getParent()->getParent()) {
      const Function *curFunc = instr->getParent()->getParent();
      if (oneFunc && (curFunc != oneFunc))
        return false;
      oneFunc = curFunc;
      return true;
    } else
      return false;
  }

  for (const User *UU : U->users())
    if (!usedInOneFunc(UU, oneFunc))
      return false;

  return true;
}

/* Find out if a global variable can be demoted to local scope.
 * Currently, this is valid for CUDA shared variables, which have local
 * scope and global lifetime. So the conditions to check are :
 * 1. Is the global variable in shared address space?
 * 2. Does it have internal linkage?
 * 3. Is the global variable referenced only in one function?
 */
static bool canDemoteGlobalVar(const GlobalVariable *gv, Function const *&f) {
  if (!gv->hasInternalLinkage())
    return false;
  PointerType *Pty = gv->getType();
  if (Pty->getAddressSpace() != ADDRESS_SPACE_SHARED)
    return false;

  const Function *oneFunc = nullptr;

  bool flag = usedInOneFunc(gv, oneFunc);
  if (!flag)
    return false;
  if (!oneFunc)
    return false;
  f = oneFunc;
  return true;
}

static bool useFuncSeen(const Constant *C,
                        DenseMap<const Function *, bool> &seenMap) {
  for (const User *U : C->users()) {
    if (const Constant *cu = dyn_cast<Constant>(U)) {
      if (useFuncSeen(cu, seenMap))
        return true;
    } else if (const Instruction *I = dyn_cast<Instruction>(U)) {
      const BasicBlock *bb = I->getParent();
      if (!bb)
        continue;
      const Function *caller = bb->getParent();
      if (!caller)
        continue;
      if (seenMap.find(caller) != seenMap.end())
        return true;
    }
  }
  return false;
}

void NVPTXAsmPrinter::emitDeclarations(const Module &M, raw_ostream &O) {
  DenseMap<const Function *, bool> seenMap;
  for (Module::const_iterator FI = M.begin(), FE = M.end(); FI != FE; ++FI) {
    const Function *F = &*FI;

    if (F->getAttributes().hasFnAttribute("nvptx-libcall-callee")) {
      emitDeclaration(F, O);
      continue;
    }

    if (F->isDeclaration()) {
      if (F->use_empty())
        continue;
      if (F->getIntrinsicID())
        continue;
      emitDeclaration(F, O);
      continue;
    }
    for (const User *U : F->users()) {
      if (const Constant *C = dyn_cast<Constant>(U)) {
        if (usedInGlobalVarDef(C)) {
          // The use is in the initialization of a global variable
          // that is a function pointer, so print a declaration
          // for the original function
          emitDeclaration(F, O);
          break;
        }
        // Emit a declaration of this function if the function that
        // uses this constant expr has already been seen.
        if (useFuncSeen(C, seenMap)) {
          emitDeclaration(F, O);
          break;
        }
      }

      if (!isa<Instruction>(U))
        continue;
      const Instruction *instr = cast<Instruction>(U);
      const BasicBlock *bb = instr->getParent();
      if (!bb)
        continue;
      const Function *caller = bb->getParent();
      if (!caller)
        continue;

      // If a caller has already been seen, then the caller is
      // appearing in the module before the callee. so print out
      // a declaration for the callee.
      if (seenMap.find(caller) != seenMap.end()) {
        emitDeclaration(F, O);
        break;
      }
    }
    seenMap[F] = true;
  }
}

static bool isEmptyXXStructor(GlobalVariable *GV) {
  if (!GV) return true;
  const ConstantArray *InitList = dyn_cast<ConstantArray>(GV->getInitializer());
  if (!InitList) return true;  // Not an array; we don't know how to parse.
  return InitList->getNumOperands() == 0;
}

bool NVPTXAsmPrinter::doInitialization(Module &M) {
  // Construct a default subtarget off of the TargetMachine defaults. The
  // rest of NVPTX isn't friendly to change subtargets per function and
  // so the default TargetMachine will have all of the options.
  const NVPTXTargetMachine &NTM = static_cast<const NVPTXTargetMachine &>(TM);
  const auto* STI = static_cast<const NVPTXSubtarget*>(NTM.getSubtargetImpl());

  if (M.alias_size()) {
    report_fatal_error("Module has aliases, which NVPTX does not support.");
    return true; // error
  }
  if (!isEmptyXXStructor(M.getNamedGlobal("llvm.global_ctors"))) {
    report_fatal_error(
        "Module has a nontrivial global ctor, which NVPTX does not support.");
    return true;  // error
  }
  if (!isEmptyXXStructor(M.getNamedGlobal("llvm.global_dtors"))) {
    report_fatal_error(
        "Module has a nontrivial global dtor, which NVPTX does not support.");
    return true;  // error
  }

  SmallString<128> Str1;
  raw_svector_ostream OS1(Str1);

  // We need to call the parent's one explicitly.
  bool Result = AsmPrinter::doInitialization(M);

  // Emit header before any dwarf directives are emitted below.
  emitHeader(M, OS1, *STI);
  OutStreamer->EmitRawText(OS1.str());

  // Emit module-level inline asm if it exists.
  if (!M.getModuleInlineAsm().empty()) {
    OutStreamer->AddComment("Start of file scope inline assembly");
    OutStreamer->AddBlankLine();
    OutStreamer->EmitRawText(StringRef(M.getModuleInlineAsm()));
    OutStreamer->AddBlankLine();
    OutStreamer->AddComment("End of file scope inline assembly");
    OutStreamer->AddBlankLine();
  }

  GlobalsEmitted = false;

  return Result;
}

void NVPTXAsmPrinter::emitGlobals(const Module &M) {
  SmallString<128> Str2;
  raw_svector_ostream OS2(Str2);

  emitDeclarations(M, OS2);

  // As ptxas does not support forward references of globals, we need to first
  // sort the list of module-level globals in def-use order. We visit each
  // global variable in order, and ensure that we emit it *after* its dependent
  // globals. We use a little extra memory maintaining both a set and a list to
  // have fast searches while maintaining a strict ordering.
  SmallVector<const GlobalVariable *, 8> Globals;
  DenseSet<const GlobalVariable *> GVVisited;
  DenseSet<const GlobalVariable *> GVVisiting;

  // Visit each global variable, in order
  for (const GlobalVariable &I : M.globals())
    VisitGlobalVariableForEmission(&I, Globals, GVVisited, GVVisiting);

  assert(GVVisited.size() == M.getGlobalList().size() &&
         "Missed a global variable");
  assert(GVVisiting.size() == 0 && "Did not fully process a global variable");

  // Print out module-level global variables in proper order
  for (unsigned i = 0, e = Globals.size(); i != e; ++i)
    printModuleLevelGV(Globals[i], OS2);

  OS2 << '\n';

  OutStreamer->EmitRawText(OS2.str());
}

void NVPTXAsmPrinter::emitHeader(Module &M, raw_ostream &O,
                                 const NVPTXSubtarget &STI) {
  O << "//\n";
  O << "// Generated by LLVM NVPTX Back-End\n";
  O << "//\n";
  O << "\n";

  unsigned PTXVersion = STI.getPTXVersion();
  O << ".version " << (PTXVersion / 10) << "." << (PTXVersion % 10) << "\n";

  O << ".target ";
  O << STI.getTargetName();

  const NVPTXTargetMachine &NTM = static_cast<const NVPTXTargetMachine &>(TM);
  if (NTM.getDrvInterface() == NVPTX::NVCL)
    O << ", texmode_independent";

  bool HasFullDebugInfo = false;
  for (DICompileUnit *CU : M.debug_compile_units()) {
    switch(CU->getEmissionKind()) {
    case DICompileUnit::NoDebug:
    case DICompileUnit::DebugDirectivesOnly:
      break;
    case DICompileUnit::LineTablesOnly:
    case DICompileUnit::FullDebug:
      HasFullDebugInfo = true;
      break;
    }
    if (HasFullDebugInfo)
      break;
  }
  // FIXME: remove comment once debug info is properly supported.
  if (MMI && MMI->hasDebugInfo() && HasFullDebugInfo)
    O << "//, debug";

  O << "\n";

  O << ".address_size ";
  if (NTM.is64Bit())
    O << "64";
  else
    O << "32";
  O << "\n";

  O << "\n";
}

bool NVPTXAsmPrinter::doFinalization(Module &M) {
  bool HasDebugInfo = MMI && MMI->hasDebugInfo();

  // If we did not emit any functions, then the global declarations have not
  // yet been emitted.
  if (!GlobalsEmitted) {
    emitGlobals(M);
    GlobalsEmitted = true;
  }

  // XXX Temproarily remove global variables so that doFinalization() will not
  // emit them again (global variables are emitted at beginning).

  Module::GlobalListType &global_list = M.getGlobalList();
  int i, n = global_list.size();
  GlobalVariable **gv_array = new GlobalVariable *[n];

  // first, back-up GlobalVariable in gv_array
  i = 0;
  for (Module::global_iterator I = global_list.begin(), E = global_list.end();
       I != E; ++I)
    gv_array[i++] = &*I;

  // second, empty global_list
  while (!global_list.empty())
    global_list.remove(global_list.begin());

  // call doFinalization
  bool ret = AsmPrinter::doFinalization(M);

  // now we restore global variables
  for (i = 0; i < n; i++)
    global_list.insert(global_list.end(), gv_array[i]);

  clearAnnotationCache(&M);

  delete[] gv_array;
  // FIXME: remove comment once debug info is properly supported.
  // Close the last emitted section
  if (HasDebugInfo)
    OutStreamer->EmitRawText("//\t}");

  // Output last DWARF .file directives, if any.
  static_cast<NVPTXTargetStreamer *>(OutStreamer->getTargetStreamer())
      ->outputDwarfFileDirectives();

  return ret;

  //bool Result = AsmPrinter::doFinalization(M);
  // Instead of calling the parents doFinalization, we may
  // clone parents doFinalization and customize here.
  // Currently, we if NVISA out the EmitGlobals() in
  // parent's doFinalization, which is too intrusive.
  //
  // Same for the doInitialization.
  //return Result;
}

// This function emits appropriate linkage directives for
// functions and global variables.
//
// extern function declaration            -> .extern
// extern function definition             -> .visible
// external global variable with init     -> .visible
// external without init                  -> .extern
// appending                              -> not allowed, assert.
// for any linkage other than
// internal, private, linker_private,
// linker_private_weak, linker_private_weak_def_auto,
// we emit                                -> .weak.

void NVPTXAsmPrinter::emitLinkageDirective(const GlobalValue *V,
                                           raw_ostream &O) {
  if (static_cast<NVPTXTargetMachine &>(TM).getDrvInterface() == NVPTX::CUDA) {
    if (V->hasExternalLinkage()) {
      if (isa<GlobalVariable>(V)) {
        const GlobalVariable *GVar = cast<GlobalVariable>(V);
        if (GVar) {
          if (GVar->hasInitializer())
            O << ".visible ";
          else
            O << ".extern ";
        }
      } else if (V->isDeclaration())
        O << ".extern ";
      else
        O << ".visible ";
    } else if (V->hasAppendingLinkage()) {
      std::string msg;
      msg.append("Error: ");
      msg.append("Symbol ");
      if (V->hasName())
        msg.append(V->getName());
      msg.append("has unsupported appending linkage type");
      llvm_unreachable(msg.c_str());
    } else if (!V->hasInternalLinkage() &&
               !V->hasPrivateLinkage()) {
      O << ".weak ";
    }
  }
}

void NVPTXAsmPrinter::printModuleLevelGV(const GlobalVariable *GVar,
                                         raw_ostream &O,
                                         bool processDemoted) {
  // Skip meta data
  if (GVar->hasSection()) {
    if (GVar->getSection() == "llvm.metadata")
      return;
  }

  // Skip LLVM intrinsic global variables
  if (GVar->getName().startswith("llvm.") ||
      GVar->getName().startswith("nvvm."))
    return;

  const DataLayout &DL = getDataLayout();

  // GlobalVariables are always constant pointers themselves.
  PointerType *PTy = GVar->getType();
  Type *ETy = GVar->getValueType();

  if (GVar->hasExternalLinkage()) {
    if (GVar->hasInitializer())
      O << ".visible ";
    else
      O << ".extern ";
  } else if (GVar->hasLinkOnceLinkage() || GVar->hasWeakLinkage() ||
             GVar->hasAvailableExternallyLinkage() ||
             GVar->hasCommonLinkage()) {
    O << ".weak ";
  }

  if (isTexture(*GVar)) {
    O << ".global .texref " << getTextureName(*GVar) << ";\n";
    return;
  }

  if (isSurface(*GVar)) {
    O << ".global .surfref " << getSurfaceName(*GVar) << ";\n";
    return;
  }

  if (GVar->isDeclaration()) {
    // (extern) declarations, no definition or initializer
    // Currently the only known declaration is for an automatic __local
    // (.shared) promoted to global.
    emitPTXGlobalVariable(GVar, O);
    O << ";\n";
    return;
  }

  if (isSampler(*GVar)) {
    O << ".global .samplerref " << getSamplerName(*GVar);

    const Constant *Initializer = nullptr;
    if (GVar->hasInitializer())
      Initializer = GVar->getInitializer();
    const ConstantInt *CI = nullptr;
    if (Initializer)
      CI = dyn_cast<ConstantInt>(Initializer);
    if (CI) {
      unsigned sample = CI->getZExtValue();

      O << " = { ";

      for (int i = 0,
               addr = ((sample & __CLK_ADDRESS_MASK) >> __CLK_ADDRESS_BASE);
           i < 3; i++) {
        O << "addr_mode_" << i << " = ";
        switch (addr) {
        case 0:
          O << "wrap";
          break;
        case 1:
          O << "clamp_to_border";
          break;
        case 2:
          O << "clamp_to_edge";
          break;
        case 3:
          O << "wrap";
          break;
        case 4:
          O << "mirror";
          break;
        }
        O << ", ";
      }
      O << "filter_mode = ";
      switch ((sample & __CLK_FILTER_MASK) >> __CLK_FILTER_BASE) {
      case 0:
        O << "nearest";
        break;
      case 1:
        O << "linear";
        break;
      case 2:
        llvm_unreachable("Anisotropic filtering is not supported");
      default:
        O << "nearest";
        break;
      }
      if (!((sample & __CLK_NORMALIZED_MASK) >> __CLK_NORMALIZED_BASE)) {
        O << ", force_unnormalized_coords = 1";
      }
      O << " }";
    }

    O << ";\n";
    return;
  }

  if (GVar->hasPrivateLinkage()) {
    if (strncmp(GVar->getName().data(), "unrollpragma", 12) == 0)
      return;

    // FIXME - need better way (e.g. Metadata) to avoid generating this global
    if (strncmp(GVar->getName().data(), "filename", 8) == 0)
      return;
    if (GVar->use_empty())
      return;
  }

  const Function *demotedFunc = nullptr;
  if (!processDemoted && canDemoteGlobalVar(GVar, demotedFunc)) {
    O << "// " << GVar->getName() << " has been demoted\n";
    if (localDecls.find(demotedFunc) != localDecls.end())
      localDecls[demotedFunc].push_back(GVar);
    else {
      std::vector<const GlobalVariable *> temp;
      temp.push_back(GVar);
      localDecls[demotedFunc] = temp;
    }
    return;
  }

  O << ".";
  emitPTXAddressSpace(PTy->getAddressSpace(), O);

  if (isManaged(*GVar)) {
    O << " .attribute(.managed)";
  }

  if (GVar->getAlignment() == 0)
    O << " .align " << (int)DL.getPrefTypeAlignment(ETy);
  else
    O << " .align " << GVar->getAlignment();

  if (ETy->isFloatingPointTy() || ETy->isPointerTy() ||
      (ETy->isIntegerTy() && ETy->getScalarSizeInBits() <= 64)) {
    O << " .";
    // Special case: ABI requires that we use .u8 for predicates
    if (ETy->isIntegerTy(1))
      O << "u8";
    else
      O << getPTXFundamentalTypeStr(ETy, false);
    O << " ";
    getSymbol(GVar)->print(O, MAI);

    // Ptx allows variable initilization only for constant and global state
    // spaces.
    if (GVar->hasInitializer()) {
      if ((PTy->getAddressSpace() == ADDRESS_SPACE_GLOBAL) ||
          (PTy->getAddressSpace() == ADDRESS_SPACE_CONST)) {
        const Constant *Initializer = GVar->getInitializer();
        // 'undef' is treated as there is no value specified.
        if (!Initializer->isNullValue() && !isa<UndefValue>(Initializer)) {
          O << " = ";
          printScalarConstant(Initializer, O);
        }
      } else {
        // The frontend adds zero-initializer to device and constant variables
        // that don't have an initial value, and UndefValue to shared
        // variables, so skip warning for this case.
        if (!GVar->getInitializer()->isNullValue() &&
            !isa<UndefValue>(GVar->getInitializer())) {
          report_fatal_error("initial value of '" + GVar->getName() +
                             "' is not allowed in addrspace(" +
                             Twine(PTy->getAddressSpace()) + ")");
        }
      }
    }
  } else {
    unsigned int ElementSize = 0;

    // Although PTX has direct support for struct type and array type and
    // LLVM IR is very similar to PTX, the LLVM CodeGen does not support for
    // targets that support these high level field accesses. Structs, arrays
    // and vectors are lowered into arrays of bytes.
    switch (ETy->getTypeID()) {
    case Type::IntegerTyID: // Integers larger than 64 bits
    case Type::StructTyID:
    case Type::ArrayTyID:
    case Type::VectorTyID:
      ElementSize = DL.getTypeStoreSize(ETy);
      // Ptx allows variable initilization only for constant and
      // global state spaces.
      if (((PTy->getAddressSpace() == ADDRESS_SPACE_GLOBAL) ||
           (PTy->getAddressSpace() == ADDRESS_SPACE_CONST)) &&
          GVar->hasInitializer()) {
        const Constant *Initializer = GVar->getInitializer();
        if (!isa<UndefValue>(Initializer) && !Initializer->isNullValue()) {
          AggBuffer aggBuffer(ElementSize, O, *this);
          bufferAggregateConstant(Initializer, &aggBuffer);
          if (aggBuffer.numSymbols) {
            if (static_cast<const NVPTXTargetMachine &>(TM).is64Bit()) {
              O << " .u64 ";
              getSymbol(GVar)->print(O, MAI);
              O << "[";
              O << ElementSize / 8;
            } else {
              O << " .u32 ";
              getSymbol(GVar)->print(O, MAI);
              O << "[";
              O << ElementSize / 4;
            }
            O << "]";
          } else {
            O << " .b8 ";
            getSymbol(GVar)->print(O, MAI);
            O << "[";
            O << ElementSize;
            O << "]";
          }
          O << " = {";
          aggBuffer.print();
          O << "}";
        } else {
          O << " .b8 ";
          getSymbol(GVar)->print(O, MAI);
          if (ElementSize) {
            O << "[";
            O << ElementSize;
            O << "]";
          }
        }
      } else {
        O << " .b8 ";
        getSymbol(GVar)->print(O, MAI);
        if (ElementSize) {
          O << "[";
          O << ElementSize;
          O << "]";
        }
      }
      break;
    default:
      llvm_unreachable("type not supported yet");
    }
  }
  O << ";\n";
}

void NVPTXAsmPrinter::emitDemotedVars(const Function *f, raw_ostream &O) {
  if (localDecls.find(f) == localDecls.end())
    return;

  std::vector<const GlobalVariable *> &gvars = localDecls[f];

  for (unsigned i = 0, e = gvars.size(); i != e; ++i) {
    O << "\t// demoted variable\n\t";
    printModuleLevelGV(gvars[i], O, true);
  }
}

void NVPTXAsmPrinter::emitPTXAddressSpace(unsigned int AddressSpace,
                                          raw_ostream &O) const {
  switch (AddressSpace) {
  case ADDRESS_SPACE_LOCAL:
    O << "local";
    break;
  case ADDRESS_SPACE_GLOBAL:
    O << "global";
    break;
  case ADDRESS_SPACE_CONST:
    O << "const";
    break;
  case ADDRESS_SPACE_SHARED:
    O << "shared";
    break;
  default:
    report_fatal_error("Bad address space found while emitting PTX: " +
                       llvm::Twine(AddressSpace));
    break;
  }
}

std::string
NVPTXAsmPrinter::getPTXFundamentalTypeStr(Type *Ty, bool useB4PTR) const {
  switch (Ty->getTypeID()) {
  default:
    llvm_unreachable("unexpected type");
    break;
  case Type::IntegerTyID: {
    unsigned NumBits = cast<IntegerType>(Ty)->getBitWidth();
    if (NumBits == 1)
      return "pred";
    else if (NumBits <= 64) {
      std::string name = "u";
      return name + utostr(NumBits);
    } else {
      llvm_unreachable("Integer too large");
      break;
    }
    break;
  }
  case Type::HalfTyID:
    // fp16 is stored as .b16 for compatibility with pre-sm_53 PTX assembly.
    return "b16";
  case Type::FloatTyID:
    return "f32";
  case Type::DoubleTyID:
    return "f64";
  case Type::PointerTyID:
    if (static_cast<const NVPTXTargetMachine &>(TM).is64Bit())
      if (useB4PTR)
        return "b64";
      else
        return "u64";
    else if (useB4PTR)
      return "b32";
    else
      return "u32";
  }
  llvm_unreachable("unexpected type");
  return nullptr;
}

void NVPTXAsmPrinter::emitPTXGlobalVariable(const GlobalVariable *GVar,
                                            raw_ostream &O) {
  const DataLayout &DL = getDataLayout();

  // GlobalVariables are always constant pointers themselves.
  Type *ETy = GVar->getValueType();

  O << ".";
  emitPTXAddressSpace(GVar->getType()->getAddressSpace(), O);
  if (GVar->getAlignment() == 0)
    O << " .align " << (int)DL.getPrefTypeAlignment(ETy);
  else
    O << " .align " << GVar->getAlignment();

  // Special case for i128
  if (ETy->isIntegerTy(128)) {
    O << " .b8 ";
    getSymbol(GVar)->print(O, MAI);
    O << "[16]";
    return;
  }

  if (ETy->isFloatingPointTy() || ETy->isIntOrPtrTy()) {
    O << " .";
    O << getPTXFundamentalTypeStr(ETy);
    O << " ";
    getSymbol(GVar)->print(O, MAI);
    return;
  }

  int64_t ElementSize = 0;

  // Although PTX has direct support for struct type and array type and LLVM IR
  // is very similar to PTX, the LLVM CodeGen does not support for targets that
  // support these high level field accesses. Structs and arrays are lowered
  // into arrays of bytes.
  switch (ETy->getTypeID()) {
  case Type::StructTyID:
  case Type::ArrayTyID:
  case Type::VectorTyID:
    ElementSize = DL.getTypeStoreSize(ETy);
    O << " .b8 ";
    getSymbol(GVar)->print(O, MAI);
    O << "[";
    if (ElementSize) {
      O << ElementSize;
    }
    O << "]";
    break;
  default:
    llvm_unreachable("type not supported yet");
  }
}

static unsigned int getOpenCLAlignment(const DataLayout &DL, Type *Ty) {
  if (Ty->isSingleValueType())
    return DL.getPrefTypeAlignment(Ty);

  auto *ATy = dyn_cast<ArrayType>(Ty);
  if (ATy)
    return getOpenCLAlignment(DL, ATy->getElementType());

  auto *STy = dyn_cast<StructType>(Ty);
  if (STy) {
    unsigned int alignStruct = 1;
    // Go through each element of the struct and find the
    // largest alignment.
    for (unsigned i = 0, e = STy->getNumElements(); i != e; i++) {
      Type *ETy = STy->getElementType(i);
      unsigned int align = getOpenCLAlignment(DL, ETy);
      if (align > alignStruct)
        alignStruct = align;
    }
    return alignStruct;
  }

  auto *FTy = dyn_cast<FunctionType>(Ty);
  if (FTy)
    return DL.getPointerPrefAlignment();
  return DL.getPrefTypeAlignment(Ty);
}

void NVPTXAsmPrinter::printParamName(Function::const_arg_iterator I,
                                     int paramIndex, raw_ostream &O) {
  getSymbol(I->getParent())->print(O, MAI);
  O << "_param_" << paramIndex;
}

void NVPTXAsmPrinter::emitFunctionParamList(const Function *F, raw_ostream &O) {
  const DataLayout &DL = getDataLayout();
  const AttributeList &PAL = F->getAttributes();
  const NVPTXSubtarget &STI = TM.getSubtarget<NVPTXSubtarget>(*F);
  const TargetLowering *TLI = STI.getTargetLowering();
  Function::const_arg_iterator I, E;
  unsigned paramIndex = 0;
  bool first = true;
  bool isKernelFunc = isKernelFunction(*F);
  bool isABI = (STI.getSmVersion() >= 20);
  bool hasImageHandles = STI.hasImageHandles();
  MVT thePointerTy = TLI->getPointerTy(DL);

  if (F->arg_empty()) {
    O << "()\n";
    return;
  }

  O << "(\n";

  for (I = F->arg_begin(), E = F->arg_end(); I != E; ++I, paramIndex++) {
    Type *Ty = I->getType();

    if (!first)
      O << ",\n";

    first = false;

    // Handle image/sampler parameters
    if (isKernelFunction(*F)) {
      if (isSampler(*I) || isImage(*I)) {
        if (isImage(*I)) {
          std::string sname = I->getName();
          if (isImageWriteOnly(*I) || isImageReadWrite(*I)) {
            if (hasImageHandles)
              O << "\t.param .u64 .ptr .surfref ";
            else
              O << "\t.param .surfref ";
            CurrentFnSym->print(O, MAI);
            O << "_param_" << paramIndex;
          }
          else { // Default image is read_only
            if (hasImageHandles)
              O << "\t.param .u64 .ptr .texref ";
            else
              O << "\t.param .texref ";
            CurrentFnSym->print(O, MAI);
            O << "_param_" << paramIndex;
          }
        } else {
          if (hasImageHandles)
            O << "\t.param .u64 .ptr .samplerref ";
          else
            O << "\t.param .samplerref ";
          CurrentFnSym->print(O, MAI);
          O << "_param_" << paramIndex;
        }
        continue;
      }
    }

    if (!PAL.hasParamAttribute(paramIndex, Attribute::ByVal)) {
      if (Ty->isAggregateType() || Ty->isVectorTy() || Ty->isIntegerTy(128)) {
        // Just print .param .align <a> .b8 .param[size];
        // <a> = PAL.getparamalignment
        // size = typeallocsize of element type
        unsigned align = PAL.getParamAlignment(paramIndex);
        if (align == 0)
          align = DL.getABITypeAlignment(Ty);

        unsigned sz = DL.getTypeAllocSize(Ty);
        O << "\t.param .align " << align << " .b8 ";
        printParamName(I, paramIndex, O);
        O << "[" << sz << "]";

        continue;
      }
      // Just a scalar
      auto *PTy = dyn_cast<PointerType>(Ty);
      if (isKernelFunc) {
        if (PTy) {
          // Special handling for pointer arguments to kernel
          O << "\t.param .u" << thePointerTy.getSizeInBits() << " ";

          if (static_cast<NVPTXTargetMachine &>(TM).getDrvInterface() !=
              NVPTX::CUDA) {
            Type *ETy = PTy->getElementType();
            int addrSpace = PTy->getAddressSpace();
            switch (addrSpace) {
            default:
              O << ".ptr ";
              break;
            case ADDRESS_SPACE_CONST:
              O << ".ptr .const ";
              break;
            case ADDRESS_SPACE_SHARED:
              O << ".ptr .shared ";
              break;
            case ADDRESS_SPACE_GLOBAL:
              O << ".ptr .global ";
              break;
            }
            O << ".align " << (int)getOpenCLAlignment(DL, ETy) << " ";
          }
          printParamName(I, paramIndex, O);
          continue;
        }

        // non-pointer scalar to kernel func
        O << "\t.param .";
        // Special case: predicate operands become .u8 types
        if (Ty->isIntegerTy(1))
          O << "u8";
        else
          O << getPTXFundamentalTypeStr(Ty);
        O << " ";
        printParamName(I, paramIndex, O);
        continue;
      }
      // Non-kernel function, just print .param .b<size> for ABI
      // and .reg .b<size> for non-ABI
      unsigned sz = 0;
      if (isa<IntegerType>(Ty)) {
        sz = cast<IntegerType>(Ty)->getBitWidth();
        if (sz < 32)
          sz = 32;
      } else if (isa<PointerType>(Ty))
        sz = thePointerTy.getSizeInBits();
      else if (Ty->isHalfTy())
        // PTX ABI requires all scalar parameters to be at least 32
        // bits in size.  fp16 normally uses .b16 as its storage type
        // in PTX, so its size must be adjusted here, too.
        sz = 32;
      else
        sz = Ty->getPrimitiveSizeInBits();
      if (isABI)
        O << "\t.param .b" << sz << " ";
      else
        O << "\t.reg .b" << sz << " ";
      printParamName(I, paramIndex, O);
      continue;
    }

    // param has byVal attribute. So should be a pointer
    auto *PTy = dyn_cast<PointerType>(Ty);
    assert(PTy && "Param with byval attribute should be a pointer type");
    Type *ETy = PTy->getElementType();

    if (isABI || isKernelFunc) {
      // Just print .param .align <a> .b8 .param[size];
      // <a> = PAL.getparamalignment
      // size = typeallocsize of element type
      unsigned align = PAL.getParamAlignment(paramIndex);
      if (align == 0)
        align = DL.getABITypeAlignment(ETy);
      // Work around a bug in ptxas. When PTX code takes address of
      // byval parameter with alignment < 4, ptxas generates code to
      // spill argument into memory. Alas on sm_50+ ptxas generates
      // SASS code that fails with misaligned access. To work around
      // the problem, make sure that we align byval parameters by at
      // least 4. Matching change must be made in LowerCall() where we
      // prepare parameters for the call.
      //
      // TODO: this will need to be undone when we get to support multi-TU
      // device-side compilation as it breaks ABI compatibility with nvcc.
      // Hopefully ptxas bug is fixed by then.
      if (!isKernelFunc && align < 4)
        align = 4;
      unsigned sz = DL.getTypeAllocSize(ETy);
      O << "\t.param .align " << align << " .b8 ";
      printParamName(I, paramIndex, O);
      O << "[" << sz << "]";
      continue;
    } else {
      // Split the ETy into constituent parts and
      // print .param .b<size> <name> for each part.
      // Further, if a part is vector, print the above for
      // each vector element.
      SmallVector<EVT, 16> vtparts;
      ComputeValueVTs(*TLI, DL, ETy, vtparts);
      for (unsigned i = 0, e = vtparts.size(); i != e; ++i) {
        unsigned elems = 1;
        EVT elemtype = vtparts[i];
        if (vtparts[i].isVector()) {
          elems = vtparts[i].getVectorNumElements();
          elemtype = vtparts[i].getVectorElementType();
        }

        for (unsigned j = 0, je = elems; j != je; ++j) {
          unsigned sz = elemtype.getSizeInBits();
          if (elemtype.isInteger() && (sz < 32))
            sz = 32;
          O << "\t.reg .b" << sz << " ";
          printParamName(I, paramIndex, O);
          if (j < je - 1)
            O << ",\n";
          ++paramIndex;
        }
        if (i < e - 1)
          O << ",\n";
      }
      --paramIndex;
      continue;
    }
  }

  O << "\n)\n";
}

void NVPTXAsmPrinter::emitFunctionParamList(const MachineFunction &MF,
                                            raw_ostream &O) {
  const Function &F = MF.getFunction();
  emitFunctionParamList(&F, O);
}

void NVPTXAsmPrinter::setAndEmitFunctionVirtualRegisters(
    const MachineFunction &MF) {
  SmallString<128> Str;
  raw_svector_ostream O(Str);

  // Map the global virtual register number to a register class specific
  // virtual register number starting from 1 with that class.
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  //unsigned numRegClasses = TRI->getNumRegClasses();

  // Emit the Fake Stack Object
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  int NumBytes = (int) MFI.getStackSize();
  if (NumBytes) {
    O << "\t.local .align " << MFI.getMaxAlignment() << " .b8 \t" << DEPOTNAME
      << getFunctionNumber() << "[" << NumBytes << "];\n";
    if (static_cast<const NVPTXTargetMachine &>(MF.getTarget()).is64Bit()) {
      O << "\t.reg .b64 \t%SP;\n";
      O << "\t.reg .b64 \t%SPL;\n";
    } else {
      O << "\t.reg .b32 \t%SP;\n";
      O << "\t.reg .b32 \t%SPL;\n";
    }
  }

  // Go through all virtual registers to establish the mapping between the
  // global virtual
  // register number and the per class virtual register number.
  // We use the per class virtual register number in the ptx output.
  unsigned int numVRs = MRI->getNumVirtRegs();
  for (unsigned i = 0; i < numVRs; i++) {
    unsigned int vr = TRI->index2VirtReg(i);
    const TargetRegisterClass *RC = MRI->getRegClass(vr);
    DenseMap<unsigned, unsigned> &regmap = VRegMapping[RC];
    int n = regmap.size();
    regmap.insert(std::make_pair(vr, n + 1));
  }

  // Emit register declarations
  // @TODO: Extract out the real register usage
  // O << "\t.reg .pred %p<" << NVPTXNumRegisters << ">;\n";
  // O << "\t.reg .s16 %rc<" << NVPTXNumRegisters << ">;\n";
  // O << "\t.reg .s16 %rs<" << NVPTXNumRegisters << ">;\n";
  // O << "\t.reg .s32 %r<" << NVPTXNumRegisters << ">;\n";
  // O << "\t.reg .s64 %rd<" << NVPTXNumRegisters << ">;\n";
  // O << "\t.reg .f32 %f<" << NVPTXNumRegisters << ">;\n";
  // O << "\t.reg .f64 %fd<" << NVPTXNumRegisters << ">;\n";

  // Emit declaration of the virtual registers or 'physical' registers for
  // each register class
  for (unsigned i=0; i< TRI->getNumRegClasses(); i++) {
    const TargetRegisterClass *RC = TRI->getRegClass(i);
    DenseMap<unsigned, unsigned> &regmap = VRegMapping[RC];
    std::string rcname = getNVPTXRegClassName(RC);
    std::string rcStr = getNVPTXRegClassStr(RC);
    int n = regmap.size();

    // Only declare those registers that may be used.
    if (n) {
       O << "\t.reg " << rcname << " \t" << rcStr << "<" << (n+1)
         << ">;\n";
    }
  }

  OutStreamer->EmitRawText(O.str());
}

void NVPTXAsmPrinter::printFPConstant(const ConstantFP *Fp, raw_ostream &O) {
  APFloat APF = APFloat(Fp->getValueAPF()); // make a copy
  bool ignored;
  unsigned int numHex;
  const char *lead;

  if (Fp->getType()->getTypeID() == Type::FloatTyID) {
    numHex = 8;
    lead = "0f";
    APF.convert(APFloat::IEEEsingle(), APFloat::rmNearestTiesToEven, &ignored);
  } else if (Fp->getType()->getTypeID() == Type::DoubleTyID) {
    numHex = 16;
    lead = "0d";
    APF.convert(APFloat::IEEEdouble(), APFloat::rmNearestTiesToEven, &ignored);
  } else
    llvm_unreachable("unsupported fp type");

  APInt API = APF.bitcastToAPInt();
  O << lead << format_hex_no_prefix(API.getZExtValue(), numHex, /*Upper=*/true);
}

void NVPTXAsmPrinter::printScalarConstant(const Constant *CPV, raw_ostream &O) {
  if (const ConstantInt *CI = dyn_cast<ConstantInt>(CPV)) {
    O << CI->getValue();
    return;
  }
  if (const ConstantFP *CFP = dyn_cast<ConstantFP>(CPV)) {
    printFPConstant(CFP, O);
    return;
  }
  if (isa<ConstantPointerNull>(CPV)) {
    O << "0";
    return;
  }
  if (const GlobalValue *GVar = dyn_cast<GlobalValue>(CPV)) {
    bool IsNonGenericPointer = false;
    if (GVar->getType()->getAddressSpace() != 0) {
      IsNonGenericPointer = true;
    }
    if (EmitGeneric && !isa<Function>(CPV) && !IsNonGenericPointer) {
      O << "generic(";
      getSymbol(GVar)->print(O, MAI);
      O << ")";
    } else {
      getSymbol(GVar)->print(O, MAI);
    }
    return;
  }
  if (const ConstantExpr *Cexpr = dyn_cast<ConstantExpr>(CPV)) {
    const Value *v = Cexpr->stripPointerCasts();
    PointerType *PTy = dyn_cast<PointerType>(Cexpr->getType());
    bool IsNonGenericPointer = false;
    if (PTy && PTy->getAddressSpace() != 0) {
      IsNonGenericPointer = true;
    }
    if (const GlobalValue *GVar = dyn_cast<GlobalValue>(v)) {
      if (EmitGeneric && !isa<Function>(v) && !IsNonGenericPointer) {
        O << "generic(";
        getSymbol(GVar)->print(O, MAI);
        O << ")";
      } else {
        getSymbol(GVar)->print(O, MAI);
      }
      return;
    } else {
      lowerConstant(CPV)->print(O, MAI);
      return;
    }
  }
  llvm_unreachable("Not scalar type found in printScalarConstant()");
}

// These utility functions assure we get the right sequence of bytes for a given
// type even for big-endian machines
template <typename T> static void ConvertIntToBytes(unsigned char *p, T val) {
  int64_t vp = (int64_t)val;
  for (unsigned i = 0; i < sizeof(T); ++i) {
    p[i] = (unsigned char)vp;
    vp >>= 8;
  }
}
static void ConvertFloatToBytes(unsigned char *p, float val) {
  int32_t *vp = (int32_t *)&val;
  for (unsigned i = 0; i < sizeof(int32_t); ++i) {
    p[i] = (unsigned char)*vp;
    *vp >>= 8;
  }
}
static void ConvertDoubleToBytes(unsigned char *p, double val) {
  int64_t *vp = (int64_t *)&val;
  for (unsigned i = 0; i < sizeof(int64_t); ++i) {
    p[i] = (unsigned char)*vp;
    *vp >>= 8;
  }
}

void NVPTXAsmPrinter::bufferLEByte(const Constant *CPV, int Bytes,
                                   AggBuffer *aggBuffer) {
  const DataLayout &DL = getDataLayout();

  if (isa<UndefValue>(CPV) || CPV->isNullValue()) {
    int s = DL.getTypeAllocSize(CPV->getType());
    if (s < Bytes)
      s = Bytes;
    aggBuffer->addZeros(s);
    return;
  }

  unsigned char ptr[8];
  switch (CPV->getType()->getTypeID()) {

  case Type::IntegerTyID: {
    Type *ETy = CPV->getType();
    if (ETy == Type::getInt8Ty(CPV->getContext())) {
      unsigned char c = (unsigned char)cast<ConstantInt>(CPV)->getZExtValue();
      ConvertIntToBytes<>(ptr, c);
      aggBuffer->addBytes(ptr, 1, Bytes);
    } else if (ETy == Type::getInt16Ty(CPV->getContext())) {
      short int16 = (short)cast<ConstantInt>(CPV)->getZExtValue();
      ConvertIntToBytes<>(ptr, int16);
      aggBuffer->addBytes(ptr, 2, Bytes);
    } else if (ETy == Type::getInt32Ty(CPV->getContext())) {
      if (const ConstantInt *constInt = dyn_cast<ConstantInt>(CPV)) {
        int int32 = (int)(constInt->getZExtValue());
        ConvertIntToBytes<>(ptr, int32);
        aggBuffer->addBytes(ptr, 4, Bytes);
        break;
      } else if (const auto *Cexpr = dyn_cast<ConstantExpr>(CPV)) {
        if (const auto *constInt = dyn_cast_or_null<ConstantInt>(
                ConstantFoldConstant(Cexpr, DL))) {
          int int32 = (int)(constInt->getZExtValue());
          ConvertIntToBytes<>(ptr, int32);
          aggBuffer->addBytes(ptr, 4, Bytes);
          break;
        }
        if (Cexpr->getOpcode() == Instruction::PtrToInt) {
          Value *v = Cexpr->getOperand(0)->stripPointerCasts();
          aggBuffer->addSymbol(v, Cexpr->getOperand(0));
          aggBuffer->addZeros(4);
          break;
        }
      }
      llvm_unreachable("unsupported integer const type");
    } else if (ETy == Type::getInt64Ty(CPV->getContext())) {
      if (const ConstantInt *constInt = dyn_cast<ConstantInt>(CPV)) {
        long long int64 = (long long)(constInt->getZExtValue());
        ConvertIntToBytes<>(ptr, int64);
        aggBuffer->addBytes(ptr, 8, Bytes);
        break;
      } else if (const ConstantExpr *Cexpr = dyn_cast<ConstantExpr>(CPV)) {
        if (const auto *constInt = dyn_cast_or_null<ConstantInt>(
                ConstantFoldConstant(Cexpr, DL))) {
          long long int64 = (long long)(constInt->getZExtValue());
          ConvertIntToBytes<>(ptr, int64);
          aggBuffer->addBytes(ptr, 8, Bytes);
          break;
        }
        if (Cexpr->getOpcode() == Instruction::PtrToInt) {
          Value *v = Cexpr->getOperand(0)->stripPointerCasts();
          aggBuffer->addSymbol(v, Cexpr->getOperand(0));
          aggBuffer->addZeros(8);
          break;
        }
      }
      llvm_unreachable("unsupported integer const type");
    } else
      llvm_unreachable("unsupported integer const type");
    break;
  }
  case Type::HalfTyID:
  case Type::FloatTyID:
  case Type::DoubleTyID: {
    const ConstantFP *CFP = dyn_cast<ConstantFP>(CPV);
    Type *Ty = CFP->getType();
    if (Ty == Type::getHalfTy(CPV->getContext())) {
      APInt API = CFP->getValueAPF().bitcastToAPInt();
      uint16_t float16 = API.getLoBits(16).getZExtValue();
      ConvertIntToBytes<>(ptr, float16);
      aggBuffer->addBytes(ptr, 2, Bytes);
    } else if (Ty == Type::getFloatTy(CPV->getContext())) {
      float float32 = (float) CFP->getValueAPF().convertToFloat();
      ConvertFloatToBytes(ptr, float32);
      aggBuffer->addBytes(ptr, 4, Bytes);
    } else if (Ty == Type::getDoubleTy(CPV->getContext())) {
      double float64 = CFP->getValueAPF().convertToDouble();
      ConvertDoubleToBytes(ptr, float64);
      aggBuffer->addBytes(ptr, 8, Bytes);
    } else {
      llvm_unreachable("unsupported fp const type");
    }
    break;
  }
  case Type::PointerTyID: {
    if (const GlobalValue *GVar = dyn_cast<GlobalValue>(CPV)) {
      aggBuffer->addSymbol(GVar, GVar);
    } else if (const ConstantExpr *Cexpr = dyn_cast<ConstantExpr>(CPV)) {
      const Value *v = Cexpr->stripPointerCasts();
      aggBuffer->addSymbol(v, Cexpr);
    }
    unsigned int s = DL.getTypeAllocSize(CPV->getType());
    aggBuffer->addZeros(s);
    break;
  }

  case Type::ArrayTyID:
  case Type::VectorTyID:
  case Type::StructTyID: {
    if (isa<ConstantAggregate>(CPV) || isa<ConstantDataSequential>(CPV)) {
      int ElementSize = DL.getTypeAllocSize(CPV->getType());
      bufferAggregateConstant(CPV, aggBuffer);
      if (Bytes > ElementSize)
        aggBuffer->addZeros(Bytes - ElementSize);
    } else if (isa<ConstantAggregateZero>(CPV))
      aggBuffer->addZeros(Bytes);
    else
      llvm_unreachable("Unexpected Constant type");
    break;
  }

  default:
    llvm_unreachable("unsupported type");
  }
}

void NVPTXAsmPrinter::bufferAggregateConstant(const Constant *CPV,
                                              AggBuffer *aggBuffer) {
  const DataLayout &DL = getDataLayout();
  int Bytes;

  // Integers of arbitrary width
  if (const ConstantInt *CI = dyn_cast<ConstantInt>(CPV)) {
    APInt Val = CI->getValue();
    for (unsigned I = 0, E = DL.getTypeAllocSize(CPV->getType()); I < E; ++I) {
      uint8_t Byte = Val.getLoBits(8).getZExtValue();
      aggBuffer->addBytes(&Byte, 1, 1);
      Val.lshrInPlace(8);
    }
    return;
  }

  // Old constants
  if (isa<ConstantArray>(CPV) || isa<ConstantVector>(CPV)) {
    if (CPV->getNumOperands())
      for (unsigned i = 0, e = CPV->getNumOperands(); i != e; ++i)
        bufferLEByte(cast<Constant>(CPV->getOperand(i)), 0, aggBuffer);
    return;
  }

  if (const ConstantDataSequential *CDS =
          dyn_cast<ConstantDataSequential>(CPV)) {
    if (CDS->getNumElements())
      for (unsigned i = 0; i < CDS->getNumElements(); ++i)
        bufferLEByte(cast<Constant>(CDS->getElementAsConstant(i)), 0,
                     aggBuffer);
    return;
  }

  if (isa<ConstantStruct>(CPV)) {
    if (CPV->getNumOperands()) {
      StructType *ST = cast<StructType>(CPV->getType());
      for (unsigned i = 0, e = CPV->getNumOperands(); i != e; ++i) {
        if (i == (e - 1))
          Bytes = DL.getStructLayout(ST)->getElementOffset(0) +
                  DL.getTypeAllocSize(ST) -
                  DL.getStructLayout(ST)->getElementOffset(i);
        else
          Bytes = DL.getStructLayout(ST)->getElementOffset(i + 1) -
                  DL.getStructLayout(ST)->getElementOffset(i);
        bufferLEByte(cast<Constant>(CPV->getOperand(i)), Bytes, aggBuffer);
      }
    }
    return;
  }
  llvm_unreachable("unsupported constant type in printAggregateConstant()");
}

/// lowerConstantForGV - Return an MCExpr for the given Constant.  This is mostly
/// a copy from AsmPrinter::lowerConstant, except customized to only handle
/// expressions that are representable in PTX and create
/// NVPTXGenericMCSymbolRefExpr nodes for addrspacecast instructions.
const MCExpr *
NVPTXAsmPrinter::lowerConstantForGV(const Constant *CV, bool ProcessingGeneric) {
  MCContext &Ctx = OutContext;

  if (CV->isNullValue() || isa<UndefValue>(CV))
    return MCConstantExpr::create(0, Ctx);

  if (const ConstantInt *CI = dyn_cast<ConstantInt>(CV))
    return MCConstantExpr::create(CI->getZExtValue(), Ctx);

  if (const GlobalValue *GV = dyn_cast<GlobalValue>(CV)) {
    const MCSymbolRefExpr *Expr =
      MCSymbolRefExpr::create(getSymbol(GV), Ctx);
    if (ProcessingGeneric) {
      return NVPTXGenericMCSymbolRefExpr::create(Expr, Ctx);
    } else {
      return Expr;
    }
  }

  const ConstantExpr *CE = dyn_cast<ConstantExpr>(CV);
  if (!CE) {
    llvm_unreachable("Unknown constant value to lower!");
  }

  switch (CE->getOpcode()) {
  default:
    // If the code isn't optimized, there may be outstanding folding
    // opportunities. Attempt to fold the expression using DataLayout as a
    // last resort before giving up.
    if (Constant *C = ConstantFoldConstant(CE, getDataLayout()))
      if (C && C != CE)
        return lowerConstantForGV(C, ProcessingGeneric);

    // Otherwise report the problem to the user.
    {
      std::string S;
      raw_string_ostream OS(S);
      OS << "Unsupported expression in static initializer: ";
      CE->printAsOperand(OS, /*PrintType=*/false,
                     !MF ? nullptr : MF->getFunction().getParent());
      report_fatal_error(OS.str());
    }

  case Instruction::AddrSpaceCast: {
    // Strip the addrspacecast and pass along the operand
    PointerType *DstTy = cast<PointerType>(CE->getType());
    if (DstTy->getAddressSpace() == 0) {
      return lowerConstantForGV(cast<const Constant>(CE->getOperand(0)), true);
    }
    std::string S;
    raw_string_ostream OS(S);
    OS << "Unsupported expression in static initializer: ";
    CE->printAsOperand(OS, /*PrintType=*/ false,
                       !MF ? nullptr : MF->getFunction().getParent());
    report_fatal_error(OS.str());
  }

  case Instruction::GetElementPtr: {
    const DataLayout &DL = getDataLayout();

    // Generate a symbolic expression for the byte address
    APInt OffsetAI(DL.getPointerTypeSizeInBits(CE->getType()), 0);
    cast<GEPOperator>(CE)->accumulateConstantOffset(DL, OffsetAI);

    const MCExpr *Base = lowerConstantForGV(CE->getOperand(0),
                                            ProcessingGeneric);
    if (!OffsetAI)
      return Base;

    int64_t Offset = OffsetAI.getSExtValue();
    return MCBinaryExpr::createAdd(Base, MCConstantExpr::create(Offset, Ctx),
                                   Ctx);
  }

  case Instruction::Trunc:
    // We emit the value and depend on the assembler to truncate the generated
    // expression properly.  This is important for differences between
    // blockaddress labels.  Since the two labels are in the same function, it
    // is reasonable to treat their delta as a 32-bit value.
    LLVM_FALLTHROUGH;
  case Instruction::BitCast:
    return lowerConstantForGV(CE->getOperand(0), ProcessingGeneric);

  case Instruction::IntToPtr: {
    const DataLayout &DL = getDataLayout();

    // Handle casts to pointers by changing them into casts to the appropriate
    // integer type.  This promotes constant folding and simplifies this code.
    Constant *Op = CE->getOperand(0);
    Op = ConstantExpr::getIntegerCast(Op, DL.getIntPtrType(CV->getType()),
                                      false/*ZExt*/);
    return lowerConstantForGV(Op, ProcessingGeneric);
  }

  case Instruction::PtrToInt: {
    const DataLayout &DL = getDataLayout();

    // Support only foldable casts to/from pointers that can be eliminated by
    // changing the pointer to the appropriately sized integer type.
    Constant *Op = CE->getOperand(0);
    Type *Ty = CE->getType();

    const MCExpr *OpExpr = lowerConstantForGV(Op, ProcessingGeneric);

    // We can emit the pointer value into this slot if the slot is an
    // integer slot equal to the size of the pointer.
    if (DL.getTypeAllocSize(Ty) == DL.getTypeAllocSize(Op->getType()))
      return OpExpr;

    // Otherwise the pointer is smaller than the resultant integer, mask off
    // the high bits so we are sure to get a proper truncation if the input is
    // a constant expr.
    unsigned InBits = DL.getTypeAllocSizeInBits(Op->getType());
    const MCExpr *MaskExpr = MCConstantExpr::create(~0ULL >> (64-InBits), Ctx);
    return MCBinaryExpr::createAnd(OpExpr, MaskExpr, Ctx);
  }

  // The MC library also has a right-shift operator, but it isn't consistently
  // signed or unsigned between different targets.
  case Instruction::Add: {
    const MCExpr *LHS = lowerConstantForGV(CE->getOperand(0), ProcessingGeneric);
    const MCExpr *RHS = lowerConstantForGV(CE->getOperand(1), ProcessingGeneric);
    switch (CE->getOpcode()) {
    default: llvm_unreachable("Unknown binary operator constant cast expr");
    case Instruction::Add: return MCBinaryExpr::createAdd(LHS, RHS, Ctx);
    }
  }
  }
}

// Copy of MCExpr::print customized for NVPTX
void NVPTXAsmPrinter::printMCExpr(const MCExpr &Expr, raw_ostream &OS) {
  switch (Expr.getKind()) {
  case MCExpr::Target:
    return cast<MCTargetExpr>(&Expr)->printImpl(OS, MAI);
  case MCExpr::Constant:
    OS << cast<MCConstantExpr>(Expr).getValue();
    return;

  case MCExpr::SymbolRef: {
    const MCSymbolRefExpr &SRE = cast<MCSymbolRefExpr>(Expr);
    const MCSymbol &Sym = SRE.getSymbol();
    Sym.print(OS, MAI);
    return;
  }

  case MCExpr::Unary: {
    const MCUnaryExpr &UE = cast<MCUnaryExpr>(Expr);
    switch (UE.getOpcode()) {
    case MCUnaryExpr::LNot:  OS << '!'; break;
    case MCUnaryExpr::Minus: OS << '-'; break;
    case MCUnaryExpr::Not:   OS << '~'; break;
    case MCUnaryExpr::Plus:  OS << '+'; break;
    }
    printMCExpr(*UE.getSubExpr(), OS);
    return;
  }

  case MCExpr::Binary: {
    const MCBinaryExpr &BE = cast<MCBinaryExpr>(Expr);

    // Only print parens around the LHS if it is non-trivial.
    if (isa<MCConstantExpr>(BE.getLHS()) || isa<MCSymbolRefExpr>(BE.getLHS()) ||
        isa<NVPTXGenericMCSymbolRefExpr>(BE.getLHS())) {
      printMCExpr(*BE.getLHS(), OS);
    } else {
      OS << '(';
      printMCExpr(*BE.getLHS(), OS);
      OS<< ')';
    }

    switch (BE.getOpcode()) {
    case MCBinaryExpr::Add:
      // Print "X-42" instead of "X+-42".
      if (const MCConstantExpr *RHSC = dyn_cast<MCConstantExpr>(BE.getRHS())) {
        if (RHSC->getValue() < 0) {
          OS << RHSC->getValue();
          return;
        }
      }

      OS <<  '+';
      break;
    default: llvm_unreachable("Unhandled binary operator");
    }

    // Only print parens around the LHS if it is non-trivial.
    if (isa<MCConstantExpr>(BE.getRHS()) || isa<MCSymbolRefExpr>(BE.getRHS())) {
      printMCExpr(*BE.getRHS(), OS);
    } else {
      OS << '(';
      printMCExpr(*BE.getRHS(), OS);
      OS << ')';
    }
    return;
  }
  }

  llvm_unreachable("Invalid expression kind!");
}

/// PrintAsmOperand - Print out an operand for an inline asm expression.
///
bool NVPTXAsmPrinter::PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                                      unsigned AsmVariant,
                                      const char *ExtraCode, raw_ostream &O) {
  if (ExtraCode && ExtraCode[0]) {
    if (ExtraCode[1] != 0)
      return true; // Unknown modifier.

    switch (ExtraCode[0]) {
    default:
      // See if this is a generic print operand
      return AsmPrinter::PrintAsmOperand(MI, OpNo, AsmVariant, ExtraCode, O);
    case 'r':
      break;
    }
  }

  printOperand(MI, OpNo, O);

  return false;
}

bool NVPTXAsmPrinter::PrintAsmMemoryOperand(
    const MachineInstr *MI, unsigned OpNo, unsigned AsmVariant,
    const char *ExtraCode, raw_ostream &O) {
  if (ExtraCode && ExtraCode[0])
    return true; // Unknown modifier

  O << '[';
  printMemOperand(MI, OpNo, O);
  O << ']';

  return false;
}

void NVPTXAsmPrinter::printOperand(const MachineInstr *MI, int opNum,
                                   raw_ostream &O, const char *Modifier) {
  const MachineOperand &MO = MI->getOperand(opNum);
  switch (MO.getType()) {
  case MachineOperand::MO_Register:
    if (TargetRegisterInfo::isPhysicalRegister(MO.getReg())) {
      if (MO.getReg() == NVPTX::VRDepot)
        O << DEPOTNAME << getFunctionNumber();
      else
        O << NVPTXInstPrinter::getRegisterName(MO.getReg());
    } else {
      emitVirtualRegister(MO.getReg(), O);
    }
    return;

  case MachineOperand::MO_Immediate:
    if (!Modifier)
      O << MO.getImm();
    else if (strstr(Modifier, "vec") == Modifier)
      printVecModifiedImmediate(MO, Modifier, O);
    else
      llvm_unreachable(
          "Don't know how to handle modifier on immediate operand");
    return;

  case MachineOperand::MO_FPImmediate:
    printFPConstant(MO.getFPImm(), O);
    break;

  case MachineOperand::MO_GlobalAddress:
    getSymbol(MO.getGlobal())->print(O, MAI);
    break;

  case MachineOperand::MO_MachineBasicBlock:
    MO.getMBB()->getSymbol()->print(O, MAI);
    return;

  default:
    llvm_unreachable("Operand type not supported.");
  }
}

void NVPTXAsmPrinter::printMemOperand(const MachineInstr *MI, int opNum,
                                      raw_ostream &O, const char *Modifier) {
  printOperand(MI, opNum, O);

  if (Modifier && strcmp(Modifier, "add") == 0) {
    O << ", ";
    printOperand(MI, opNum + 1, O);
  } else {
    if (MI->getOperand(opNum + 1).isImm() &&
        MI->getOperand(opNum + 1).getImm() == 0)
      return; // don't print ',0' or '+0'
    O << "+";
    printOperand(MI, opNum + 1, O);
  }
}

// Force static initialization.
extern "C" void LLVMInitializeNVPTXAsmPrinter() {
  RegisterAsmPrinter<NVPTXAsmPrinter> X(getTheNVPTXTarget32());
  RegisterAsmPrinter<NVPTXAsmPrinter> Y(getTheNVPTXTarget64());
}
