//===- MIRParser.cpp - MIR serialization format parser implementation -----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the class that parses the optional LLVM IR and machine
// functions that are stored in MIR files.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MIRParser/MIRParser.h"
#include "MIParser.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/AsmParser/SlotMapping.h"
#include "llvm/CodeGen/GlobalISel/RegisterBank.h"
#include "llvm/CodeGen/GlobalISel/RegisterBankInfo.h"
#include "llvm/CodeGen/MIRYamlMapping.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ValueSymbolTable.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/YAMLTraits.h"
#include <memory>

using namespace llvm;

namespace llvm {

/// This class implements the parsing of LLVM IR that's embedded inside a MIR
/// file.
class MIRParserImpl {
  SourceMgr SM;
  yaml::Input In;
  StringRef Filename;
  LLVMContext &Context;
  SlotMapping IRSlots;
  /// Maps from register class names to register classes.
  Name2RegClassMap Names2RegClasses;
  /// Maps from register bank names to register banks.
  Name2RegBankMap Names2RegBanks;
  /// True when the MIR file doesn't have LLVM IR. Dummy IR functions are
  /// created and inserted into the given module when this is true.
  bool NoLLVMIR = false;
  /// True when a well formed MIR file does not contain any MIR/machine function
  /// parts.
  bool NoMIRDocuments = false;

public:
  MIRParserImpl(std::unique_ptr<MemoryBuffer> Contents,
                StringRef Filename, LLVMContext &Context);

  void reportDiagnostic(const SMDiagnostic &Diag);

  /// Report an error with the given message at unknown location.
  ///
  /// Always returns true.
  bool error(const Twine &Message);

  /// Report an error with the given message at the given location.
  ///
  /// Always returns true.
  bool error(SMLoc Loc, const Twine &Message);

  /// Report a given error with the location translated from the location in an
  /// embedded string literal to a location in the MIR file.
  ///
  /// Always returns true.
  bool error(const SMDiagnostic &Error, SMRange SourceRange);

  /// Try to parse the optional LLVM module and the machine functions in the MIR
  /// file.
  ///
  /// Return null if an error occurred.
  std::unique_ptr<Module> parseIRModule();

  bool parseMachineFunctions(Module &M, MachineModuleInfo &MMI);

  /// Parse the machine function in the current YAML document.
  ///
  ///
  /// Return true if an error occurred.
  bool parseMachineFunction(Module &M, MachineModuleInfo &MMI);

  /// Initialize the machine function to the state that's described in the MIR
  /// file.
  ///
  /// Return true if error occurred.
  bool initializeMachineFunction(const yaml::MachineFunction &YamlMF,
                                 MachineFunction &MF);

  bool parseRegisterInfo(PerFunctionMIParsingState &PFS,
                         const yaml::MachineFunction &YamlMF);

  bool setupRegisterInfo(const PerFunctionMIParsingState &PFS,
                         const yaml::MachineFunction &YamlMF);

  bool initializeFrameInfo(PerFunctionMIParsingState &PFS,
                           const yaml::MachineFunction &YamlMF);

  bool parseCalleeSavedRegister(PerFunctionMIParsingState &PFS,
                                std::vector<CalleeSavedInfo> &CSIInfo,
                                const yaml::StringValue &RegisterSource,
                                bool IsRestored, int FrameIdx);

  template <typename T>
  bool parseStackObjectsDebugInfo(PerFunctionMIParsingState &PFS,
                                  const T &Object,
                                  int FrameIdx);

  bool initializeConstantPool(PerFunctionMIParsingState &PFS,
                              MachineConstantPool &ConstantPool,
                              const yaml::MachineFunction &YamlMF);

  bool initializeJumpTableInfo(PerFunctionMIParsingState &PFS,
                               const yaml::MachineJumpTable &YamlJTI);

private:
  bool parseMDNode(PerFunctionMIParsingState &PFS, MDNode *&Node,
                   const yaml::StringValue &Source);

  bool parseMBBReference(PerFunctionMIParsingState &PFS,
                         MachineBasicBlock *&MBB,
                         const yaml::StringValue &Source);

  /// Return a MIR diagnostic converted from an MI string diagnostic.
  SMDiagnostic diagFromMIStringDiag(const SMDiagnostic &Error,
                                    SMRange SourceRange);

  /// Return a MIR diagnostic converted from a diagnostic located in a YAML
  /// block scalar string.
  SMDiagnostic diagFromBlockStringDiag(const SMDiagnostic &Error,
                                       SMRange SourceRange);

  void initNames2RegClasses(const MachineFunction &MF);
  void initNames2RegBanks(const MachineFunction &MF);

  /// Check if the given identifier is a name of a register class.
  ///
  /// Return null if the name isn't a register class.
  const TargetRegisterClass *getRegClass(const MachineFunction &MF,
                                         StringRef Name);

  /// Check if the given identifier is a name of a register bank.
  ///
  /// Return null if the name isn't a register bank.
  const RegisterBank *getRegBank(const MachineFunction &MF, StringRef Name);

  void computeFunctionProperties(MachineFunction &MF);
};

} // end namespace llvm

static void handleYAMLDiag(const SMDiagnostic &Diag, void *Context) {
  reinterpret_cast<MIRParserImpl *>(Context)->reportDiagnostic(Diag);
}

MIRParserImpl::MIRParserImpl(std::unique_ptr<MemoryBuffer> Contents,
                             StringRef Filename, LLVMContext &Context)
    : SM(),
      In(SM.getMemoryBuffer(
            SM.AddNewSourceBuffer(std::move(Contents), SMLoc()))->getBuffer(),
            nullptr, handleYAMLDiag, this),
      Filename(Filename),
      Context(Context) {
  In.setContext(&In);
}

bool MIRParserImpl::error(const Twine &Message) {
  Context.diagnose(DiagnosticInfoMIRParser(
      DS_Error, SMDiagnostic(Filename, SourceMgr::DK_Error, Message.str())));
  return true;
}

bool MIRParserImpl::error(SMLoc Loc, const Twine &Message) {
  Context.diagnose(DiagnosticInfoMIRParser(
      DS_Error, SM.GetMessage(Loc, SourceMgr::DK_Error, Message)));
  return true;
}

bool MIRParserImpl::error(const SMDiagnostic &Error, SMRange SourceRange) {
  assert(Error.getKind() == SourceMgr::DK_Error && "Expected an error");
  reportDiagnostic(diagFromMIStringDiag(Error, SourceRange));
  return true;
}

void MIRParserImpl::reportDiagnostic(const SMDiagnostic &Diag) {
  DiagnosticSeverity Kind;
  switch (Diag.getKind()) {
  case SourceMgr::DK_Error:
    Kind = DS_Error;
    break;
  case SourceMgr::DK_Warning:
    Kind = DS_Warning;
    break;
  case SourceMgr::DK_Note:
    Kind = DS_Note;
    break;
  case SourceMgr::DK_Remark:
    llvm_unreachable("remark unexpected");
    break;
  }
  Context.diagnose(DiagnosticInfoMIRParser(Kind, Diag));
}

std::unique_ptr<Module> MIRParserImpl::parseIRModule() {
  if (!In.setCurrentDocument()) {
    if (In.error())
      return nullptr;
    // Create an empty module when the MIR file is empty.
    NoMIRDocuments = true;
    return llvm::make_unique<Module>(Filename, Context);
  }

  std::unique_ptr<Module> M;
  // Parse the block scalar manually so that we can return unique pointer
  // without having to go trough YAML traits.
  if (const auto *BSN =
          dyn_cast_or_null<yaml::BlockScalarNode>(In.getCurrentNode())) {
    SMDiagnostic Error;
    M = parseAssembly(MemoryBufferRef(BSN->getValue(), Filename), Error,
                      Context, &IRSlots, /*UpgradeDebugInfo=*/false);
    if (!M) {
      reportDiagnostic(diagFromBlockStringDiag(Error, BSN->getSourceRange()));
      return nullptr;
    }
    In.nextDocument();
    if (!In.setCurrentDocument())
      NoMIRDocuments = true;
  } else {
    // Create an new, empty module.
    M = llvm::make_unique<Module>(Filename, Context);
    NoLLVMIR = true;
  }
  return M;
}

bool MIRParserImpl::parseMachineFunctions(Module &M, MachineModuleInfo &MMI) {
  if (NoMIRDocuments)
    return false;

  // Parse the machine functions.
  do {
    if (parseMachineFunction(M, MMI))
      return true;
    In.nextDocument();
  } while (In.setCurrentDocument());

  return false;
}

/// Create an empty function with the given name.
static Function *createDummyFunction(StringRef Name, Module &M) {
  auto &Context = M.getContext();
  Function *F = cast<Function>(M.getOrInsertFunction(
      Name, FunctionType::get(Type::getVoidTy(Context), false)));
  BasicBlock *BB = BasicBlock::Create(Context, "entry", F);
  new UnreachableInst(Context, BB);
  return F;
}

bool MIRParserImpl::parseMachineFunction(Module &M, MachineModuleInfo &MMI) {
  // Parse the yaml.
  yaml::MachineFunction YamlMF;
  yaml::EmptyContext Ctx;
  yaml::yamlize(In, YamlMF, false, Ctx);
  if (In.error())
    return true;

  // Search for the corresponding IR function.
  StringRef FunctionName = YamlMF.Name;
  Function *F = M.getFunction(FunctionName);
  if (!F) {
    if (NoLLVMIR) {
      F = createDummyFunction(FunctionName, M);
    } else {
      return error(Twine("function '") + FunctionName +
                   "' isn't defined in the provided LLVM IR");
    }
  }
  if (MMI.getMachineFunction(*F) != nullptr)
    return error(Twine("redefinition of machine function '") + FunctionName +
                 "'");

  // Create the MachineFunction.
  MachineFunction &MF = MMI.getOrCreateMachineFunction(*F);
  if (initializeMachineFunction(YamlMF, MF))
    return true;

  return false;
}

static bool isSSA(const MachineFunction &MF) {
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  for (unsigned I = 0, E = MRI.getNumVirtRegs(); I != E; ++I) {
    unsigned Reg = TargetRegisterInfo::index2VirtReg(I);
    if (!MRI.hasOneDef(Reg) && !MRI.def_empty(Reg))
      return false;
  }
  return true;
}

void MIRParserImpl::computeFunctionProperties(MachineFunction &MF) {
  MachineFunctionProperties &Properties = MF.getProperties();

  bool HasPHI = false;
  bool HasInlineAsm = false;
  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineInstr &MI : MBB) {
      if (MI.isPHI())
        HasPHI = true;
      if (MI.isInlineAsm())
        HasInlineAsm = true;
    }
  }
  if (!HasPHI)
    Properties.set(MachineFunctionProperties::Property::NoPHIs);
  MF.setHasInlineAsm(HasInlineAsm);

  if (isSSA(MF))
    Properties.set(MachineFunctionProperties::Property::IsSSA);
  else
    Properties.reset(MachineFunctionProperties::Property::IsSSA);

  const MachineRegisterInfo &MRI = MF.getRegInfo();
  if (MRI.getNumVirtRegs() == 0)
    Properties.set(MachineFunctionProperties::Property::NoVRegs);
}

bool
MIRParserImpl::initializeMachineFunction(const yaml::MachineFunction &YamlMF,
                                         MachineFunction &MF) {
  // TODO: Recreate the machine function.
  initNames2RegClasses(MF);
  initNames2RegBanks(MF);
  if (YamlMF.Alignment)
    MF.setAlignment(YamlMF.Alignment);
  MF.setExposesReturnsTwice(YamlMF.ExposesReturnsTwice);
  MF.setHasWinCFI(YamlMF.HasWinCFI);

  if (YamlMF.Legalized)
    MF.getProperties().set(MachineFunctionProperties::Property::Legalized);
  if (YamlMF.RegBankSelected)
    MF.getProperties().set(
        MachineFunctionProperties::Property::RegBankSelected);
  if (YamlMF.Selected)
    MF.getProperties().set(MachineFunctionProperties::Property::Selected);
  if (YamlMF.FailedISel)
    MF.getProperties().set(MachineFunctionProperties::Property::FailedISel);

  PerFunctionMIParsingState PFS(MF, SM, IRSlots, Names2RegClasses,
                                Names2RegBanks);
  if (parseRegisterInfo(PFS, YamlMF))
    return true;
  if (!YamlMF.Constants.empty()) {
    auto *ConstantPool = MF.getConstantPool();
    assert(ConstantPool && "Constant pool must be created");
    if (initializeConstantPool(PFS, *ConstantPool, YamlMF))
      return true;
  }

  StringRef BlockStr = YamlMF.Body.Value.Value;
  SMDiagnostic Error;
  SourceMgr BlockSM;
  BlockSM.AddNewSourceBuffer(
      MemoryBuffer::getMemBuffer(BlockStr, "",/*RequiresNullTerminator=*/false),
      SMLoc());
  PFS.SM = &BlockSM;
  if (parseMachineBasicBlockDefinitions(PFS, BlockStr, Error)) {
    reportDiagnostic(
        diagFromBlockStringDiag(Error, YamlMF.Body.Value.SourceRange));
    return true;
  }
  PFS.SM = &SM;

  // Initialize the frame information after creating all the MBBs so that the
  // MBB references in the frame information can be resolved.
  if (initializeFrameInfo(PFS, YamlMF))
    return true;
  // Initialize the jump table after creating all the MBBs so that the MBB
  // references can be resolved.
  if (!YamlMF.JumpTableInfo.Entries.empty() &&
      initializeJumpTableInfo(PFS, YamlMF.JumpTableInfo))
    return true;
  // Parse the machine instructions after creating all of the MBBs so that the
  // parser can resolve the MBB references.
  StringRef InsnStr = YamlMF.Body.Value.Value;
  SourceMgr InsnSM;
  InsnSM.AddNewSourceBuffer(
      MemoryBuffer::getMemBuffer(InsnStr, "", /*RequiresNullTerminator=*/false),
      SMLoc());
  PFS.SM = &InsnSM;
  if (parseMachineInstructions(PFS, InsnStr, Error)) {
    reportDiagnostic(
        diagFromBlockStringDiag(Error, YamlMF.Body.Value.SourceRange));
    return true;
  }
  PFS.SM = &SM;

  if (setupRegisterInfo(PFS, YamlMF))
    return true;

  computeFunctionProperties(MF);

  MF.getSubtarget().mirFileLoaded(MF);

  MF.verify();
  return false;
}

bool MIRParserImpl::parseRegisterInfo(PerFunctionMIParsingState &PFS,
                                      const yaml::MachineFunction &YamlMF) {
  MachineFunction &MF = PFS.MF;
  MachineRegisterInfo &RegInfo = MF.getRegInfo();
  assert(RegInfo.tracksLiveness());
  if (!YamlMF.TracksRegLiveness)
    RegInfo.invalidateLiveness();

  SMDiagnostic Error;
  // Parse the virtual register information.
  for (const auto &VReg : YamlMF.VirtualRegisters) {
    VRegInfo &Info = PFS.getVRegInfo(VReg.ID.Value);
    if (Info.Explicit)
      return error(VReg.ID.SourceRange.Start,
                   Twine("redefinition of virtual register '%") +
                       Twine(VReg.ID.Value) + "'");
    Info.Explicit = true;

    if (StringRef(VReg.Class.Value).equals("_")) {
      Info.Kind = VRegInfo::GENERIC;
      Info.D.RegBank = nullptr;
    } else {
      const auto *RC = getRegClass(MF, VReg.Class.Value);
      if (RC) {
        Info.Kind = VRegInfo::NORMAL;
        Info.D.RC = RC;
      } else {
        const RegisterBank *RegBank = getRegBank(MF, VReg.Class.Value);
        if (!RegBank)
          return error(
              VReg.Class.SourceRange.Start,
              Twine("use of undefined register class or register bank '") +
                  VReg.Class.Value + "'");
        Info.Kind = VRegInfo::REGBANK;
        Info.D.RegBank = RegBank;
      }
    }

    if (!VReg.PreferredRegister.Value.empty()) {
      if (Info.Kind != VRegInfo::NORMAL)
        return error(VReg.Class.SourceRange.Start,
              Twine("preferred register can only be set for normal vregs"));

      if (parseRegisterReference(PFS, Info.PreferredReg,
                                 VReg.PreferredRegister.Value, Error))
        return error(Error, VReg.PreferredRegister.SourceRange);
    }
  }

  // Parse the liveins.
  for (const auto &LiveIn : YamlMF.LiveIns) {
    unsigned Reg = 0;
    if (parseNamedRegisterReference(PFS, Reg, LiveIn.Register.Value, Error))
      return error(Error, LiveIn.Register.SourceRange);
    unsigned VReg = 0;
    if (!LiveIn.VirtualRegister.Value.empty()) {
      VRegInfo *Info;
      if (parseVirtualRegisterReference(PFS, Info, LiveIn.VirtualRegister.Value,
                                        Error))
        return error(Error, LiveIn.VirtualRegister.SourceRange);
      VReg = Info->VReg;
    }
    RegInfo.addLiveIn(Reg, VReg);
  }

  // Parse the callee saved registers (Registers that will
  // be saved for the caller).
  if (YamlMF.CalleeSavedRegisters) {
    SmallVector<MCPhysReg, 16> CalleeSavedRegisters;
    for (const auto &RegSource : YamlMF.CalleeSavedRegisters.getValue()) {
      unsigned Reg = 0;
      if (parseNamedRegisterReference(PFS, Reg, RegSource.Value, Error))
        return error(Error, RegSource.SourceRange);
      CalleeSavedRegisters.push_back(Reg);
    }
    RegInfo.setCalleeSavedRegs(CalleeSavedRegisters);
  }

  return false;
}

bool MIRParserImpl::setupRegisterInfo(const PerFunctionMIParsingState &PFS,
                                      const yaml::MachineFunction &YamlMF) {
  MachineFunction &MF = PFS.MF;
  MachineRegisterInfo &MRI = MF.getRegInfo();
  bool Error = false;
  // Create VRegs
  auto populateVRegInfo = [&] (const VRegInfo &Info, Twine Name) {
    unsigned Reg = Info.VReg;
    switch (Info.Kind) {
    case VRegInfo::UNKNOWN:
      error(Twine("Cannot determine class/bank of virtual register ") +
            Name + " in function '" + MF.getName() + "'");
      Error = true;
      break;
    case VRegInfo::NORMAL:
      MRI.setRegClass(Reg, Info.D.RC);
      if (Info.PreferredReg != 0)
        MRI.setSimpleHint(Reg, Info.PreferredReg);
      break;
    case VRegInfo::GENERIC:
      break;
    case VRegInfo::REGBANK:
      MRI.setRegBank(Reg, *Info.D.RegBank);
      break;
    }
  };

  for (auto I = PFS.VRegInfosNamed.begin(), E = PFS.VRegInfosNamed.end();
       I != E; I++) {
    const VRegInfo &Info = *I->second;
    populateVRegInfo(Info, Twine(I->first()));
  }

  for (auto P : PFS.VRegInfos) {
    const VRegInfo &Info = *P.second;
    populateVRegInfo(Info, Twine(P.first));
  }

  // Compute MachineRegisterInfo::UsedPhysRegMask
  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineInstr &MI : MBB) {
      for (const MachineOperand &MO : MI.operands()) {
        if (!MO.isRegMask())
          continue;
        MRI.addPhysRegsUsedFromRegMask(MO.getRegMask());
      }
    }
  }

  // FIXME: This is a temporary workaround until the reserved registers can be
  // serialized.
  MRI.freezeReservedRegs(MF);
  return Error;
}

bool MIRParserImpl::initializeFrameInfo(PerFunctionMIParsingState &PFS,
                                        const yaml::MachineFunction &YamlMF) {
  MachineFunction &MF = PFS.MF;
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const Function &F = MF.getFunction();
  const yaml::MachineFrameInfo &YamlMFI = YamlMF.FrameInfo;
  MFI.setFrameAddressIsTaken(YamlMFI.IsFrameAddressTaken);
  MFI.setReturnAddressIsTaken(YamlMFI.IsReturnAddressTaken);
  MFI.setHasStackMap(YamlMFI.HasStackMap);
  MFI.setHasPatchPoint(YamlMFI.HasPatchPoint);
  MFI.setStackSize(YamlMFI.StackSize);
  MFI.setOffsetAdjustment(YamlMFI.OffsetAdjustment);
  if (YamlMFI.MaxAlignment)
    MFI.ensureMaxAlignment(YamlMFI.MaxAlignment);
  MFI.setAdjustsStack(YamlMFI.AdjustsStack);
  MFI.setHasCalls(YamlMFI.HasCalls);
  if (YamlMFI.MaxCallFrameSize != ~0u)
    MFI.setMaxCallFrameSize(YamlMFI.MaxCallFrameSize);
  MFI.setCVBytesOfCalleeSavedRegisters(YamlMFI.CVBytesOfCalleeSavedRegisters);
  MFI.setHasOpaqueSPAdjustment(YamlMFI.HasOpaqueSPAdjustment);
  MFI.setHasVAStart(YamlMFI.HasVAStart);
  MFI.setHasMustTailInVarArgFunc(YamlMFI.HasMustTailInVarArgFunc);
  MFI.setLocalFrameSize(YamlMFI.LocalFrameSize);
  if (!YamlMFI.SavePoint.Value.empty()) {
    MachineBasicBlock *MBB = nullptr;
    if (parseMBBReference(PFS, MBB, YamlMFI.SavePoint))
      return true;
    MFI.setSavePoint(MBB);
  }
  if (!YamlMFI.RestorePoint.Value.empty()) {
    MachineBasicBlock *MBB = nullptr;
    if (parseMBBReference(PFS, MBB, YamlMFI.RestorePoint))
      return true;
    MFI.setRestorePoint(MBB);
  }

  std::vector<CalleeSavedInfo> CSIInfo;
  // Initialize the fixed frame objects.
  for (const auto &Object : YamlMF.FixedStackObjects) {
    int ObjectIdx;
    if (Object.Type != yaml::FixedMachineStackObject::SpillSlot)
      ObjectIdx = MFI.CreateFixedObject(Object.Size, Object.Offset,
                                        Object.IsImmutable, Object.IsAliased);
    else
      ObjectIdx = MFI.CreateFixedSpillStackObject(Object.Size, Object.Offset);
    MFI.setObjectAlignment(ObjectIdx, Object.Alignment);
    MFI.setStackID(ObjectIdx, Object.StackID);
    if (!PFS.FixedStackObjectSlots.insert(std::make_pair(Object.ID.Value,
                                                         ObjectIdx))
             .second)
      return error(Object.ID.SourceRange.Start,
                   Twine("redefinition of fixed stack object '%fixed-stack.") +
                       Twine(Object.ID.Value) + "'");
    if (parseCalleeSavedRegister(PFS, CSIInfo, Object.CalleeSavedRegister,
                                 Object.CalleeSavedRestored, ObjectIdx))
      return true;
    if (parseStackObjectsDebugInfo(PFS, Object, ObjectIdx))
      return true;
  }

  // Initialize the ordinary frame objects.
  for (const auto &Object : YamlMF.StackObjects) {
    int ObjectIdx;
    const AllocaInst *Alloca = nullptr;
    const yaml::StringValue &Name = Object.Name;
    if (!Name.Value.empty()) {
      Alloca = dyn_cast_or_null<AllocaInst>(
          F.getValueSymbolTable()->lookup(Name.Value));
      if (!Alloca)
        return error(Name.SourceRange.Start,
                     "alloca instruction named '" + Name.Value +
                         "' isn't defined in the function '" + F.getName() +
                         "'");
    }
    if (Object.Type == yaml::MachineStackObject::VariableSized)
      ObjectIdx = MFI.CreateVariableSizedObject(Object.Alignment, Alloca);
    else
      ObjectIdx = MFI.CreateStackObject(
          Object.Size, Object.Alignment,
          Object.Type == yaml::MachineStackObject::SpillSlot, Alloca);
    MFI.setObjectOffset(ObjectIdx, Object.Offset);
    MFI.setStackID(ObjectIdx, Object.StackID);

    if (!PFS.StackObjectSlots.insert(std::make_pair(Object.ID.Value, ObjectIdx))
             .second)
      return error(Object.ID.SourceRange.Start,
                   Twine("redefinition of stack object '%stack.") +
                       Twine(Object.ID.Value) + "'");
    if (parseCalleeSavedRegister(PFS, CSIInfo, Object.CalleeSavedRegister,
                                 Object.CalleeSavedRestored, ObjectIdx))
      return true;
    if (Object.LocalOffset)
      MFI.mapLocalFrameObject(ObjectIdx, Object.LocalOffset.getValue());
    if (parseStackObjectsDebugInfo(PFS, Object, ObjectIdx))
      return true;
  }
  MFI.setCalleeSavedInfo(CSIInfo);
  if (!CSIInfo.empty())
    MFI.setCalleeSavedInfoValid(true);

  // Initialize the various stack object references after initializing the
  // stack objects.
  if (!YamlMFI.StackProtector.Value.empty()) {
    SMDiagnostic Error;
    int FI;
    if (parseStackObjectReference(PFS, FI, YamlMFI.StackProtector.Value, Error))
      return error(Error, YamlMFI.StackProtector.SourceRange);
    MFI.setStackProtectorIndex(FI);
  }
  return false;
}

bool MIRParserImpl::parseCalleeSavedRegister(PerFunctionMIParsingState &PFS,
    std::vector<CalleeSavedInfo> &CSIInfo,
    const yaml::StringValue &RegisterSource, bool IsRestored, int FrameIdx) {
  if (RegisterSource.Value.empty())
    return false;
  unsigned Reg = 0;
  SMDiagnostic Error;
  if (parseNamedRegisterReference(PFS, Reg, RegisterSource.Value, Error))
    return error(Error, RegisterSource.SourceRange);
  CalleeSavedInfo CSI(Reg, FrameIdx);
  CSI.setRestored(IsRestored);
  CSIInfo.push_back(CSI);
  return false;
}

/// Verify that given node is of a certain type. Return true on error.
template <typename T>
static bool typecheckMDNode(T *&Result, MDNode *Node,
                            const yaml::StringValue &Source,
                            StringRef TypeString, MIRParserImpl &Parser) {
  if (!Node)
    return false;
  Result = dyn_cast<T>(Node);
  if (!Result)
    return Parser.error(Source.SourceRange.Start,
                        "expected a reference to a '" + TypeString +
                            "' metadata node");
  return false;
}

template <typename T>
bool MIRParserImpl::parseStackObjectsDebugInfo(PerFunctionMIParsingState &PFS,
    const T &Object, int FrameIdx) {
  // Debug information can only be attached to stack objects; Fixed stack
  // objects aren't supported.
  MDNode *Var = nullptr, *Expr = nullptr, *Loc = nullptr;
  if (parseMDNode(PFS, Var, Object.DebugVar) ||
      parseMDNode(PFS, Expr, Object.DebugExpr) ||
      parseMDNode(PFS, Loc, Object.DebugLoc))
    return true;
  if (!Var && !Expr && !Loc)
    return false;
  DILocalVariable *DIVar = nullptr;
  DIExpression *DIExpr = nullptr;
  DILocation *DILoc = nullptr;
  if (typecheckMDNode(DIVar, Var, Object.DebugVar, "DILocalVariable", *this) ||
      typecheckMDNode(DIExpr, Expr, Object.DebugExpr, "DIExpression", *this) ||
      typecheckMDNode(DILoc, Loc, Object.DebugLoc, "DILocation", *this))
    return true;
  PFS.MF.setVariableDbgInfo(DIVar, DIExpr, FrameIdx, DILoc);
  return false;
}

bool MIRParserImpl::parseMDNode(PerFunctionMIParsingState &PFS,
    MDNode *&Node, const yaml::StringValue &Source) {
  if (Source.Value.empty())
    return false;
  SMDiagnostic Error;
  if (llvm::parseMDNode(PFS, Node, Source.Value, Error))
    return error(Error, Source.SourceRange);
  return false;
}

bool MIRParserImpl::initializeConstantPool(PerFunctionMIParsingState &PFS,
    MachineConstantPool &ConstantPool, const yaml::MachineFunction &YamlMF) {
  DenseMap<unsigned, unsigned> &ConstantPoolSlots = PFS.ConstantPoolSlots;
  const MachineFunction &MF = PFS.MF;
  const auto &M = *MF.getFunction().getParent();
  SMDiagnostic Error;
  for (const auto &YamlConstant : YamlMF.Constants) {
    if (YamlConstant.IsTargetSpecific)
      // FIXME: Support target-specific constant pools
      return error(YamlConstant.Value.SourceRange.Start,
                   "Can't parse target-specific constant pool entries yet");
    const Constant *Value = dyn_cast_or_null<Constant>(
        parseConstantValue(YamlConstant.Value.Value, Error, M));
    if (!Value)
      return error(Error, YamlConstant.Value.SourceRange);
    unsigned Alignment =
        YamlConstant.Alignment
            ? YamlConstant.Alignment
            : M.getDataLayout().getPrefTypeAlignment(Value->getType());
    unsigned Index = ConstantPool.getConstantPoolIndex(Value, Alignment);
    if (!ConstantPoolSlots.insert(std::make_pair(YamlConstant.ID.Value, Index))
             .second)
      return error(YamlConstant.ID.SourceRange.Start,
                   Twine("redefinition of constant pool item '%const.") +
                       Twine(YamlConstant.ID.Value) + "'");
  }
  return false;
}

bool MIRParserImpl::initializeJumpTableInfo(PerFunctionMIParsingState &PFS,
    const yaml::MachineJumpTable &YamlJTI) {
  MachineJumpTableInfo *JTI = PFS.MF.getOrCreateJumpTableInfo(YamlJTI.Kind);
  for (const auto &Entry : YamlJTI.Entries) {
    std::vector<MachineBasicBlock *> Blocks;
    for (const auto &MBBSource : Entry.Blocks) {
      MachineBasicBlock *MBB = nullptr;
      if (parseMBBReference(PFS, MBB, MBBSource.Value))
        return true;
      Blocks.push_back(MBB);
    }
    unsigned Index = JTI->createJumpTableIndex(Blocks);
    if (!PFS.JumpTableSlots.insert(std::make_pair(Entry.ID.Value, Index))
             .second)
      return error(Entry.ID.SourceRange.Start,
                   Twine("redefinition of jump table entry '%jump-table.") +
                       Twine(Entry.ID.Value) + "'");
  }
  return false;
}

bool MIRParserImpl::parseMBBReference(PerFunctionMIParsingState &PFS,
                                      MachineBasicBlock *&MBB,
                                      const yaml::StringValue &Source) {
  SMDiagnostic Error;
  if (llvm::parseMBBReference(PFS, MBB, Source.Value, Error))
    return error(Error, Source.SourceRange);
  return false;
}

SMDiagnostic MIRParserImpl::diagFromMIStringDiag(const SMDiagnostic &Error,
                                                 SMRange SourceRange) {
  assert(SourceRange.isValid() && "Invalid source range");
  SMLoc Loc = SourceRange.Start;
  bool HasQuote = Loc.getPointer() < SourceRange.End.getPointer() &&
                  *Loc.getPointer() == '\'';
  // Translate the location of the error from the location in the MI string to
  // the corresponding location in the MIR file.
  Loc = Loc.getFromPointer(Loc.getPointer() + Error.getColumnNo() +
                           (HasQuote ? 1 : 0));

  // TODO: Translate any source ranges as well.
  return SM.GetMessage(Loc, Error.getKind(), Error.getMessage(), None,
                       Error.getFixIts());
}

SMDiagnostic MIRParserImpl::diagFromBlockStringDiag(const SMDiagnostic &Error,
                                                    SMRange SourceRange) {
  assert(SourceRange.isValid());

  // Translate the location of the error from the location in the llvm IR string
  // to the corresponding location in the MIR file.
  auto LineAndColumn = SM.getLineAndColumn(SourceRange.Start);
  unsigned Line = LineAndColumn.first + Error.getLineNo() - 1;
  unsigned Column = Error.getColumnNo();
  StringRef LineStr = Error.getLineContents();
  SMLoc Loc = Error.getLoc();

  // Get the full line and adjust the column number by taking the indentation of
  // LLVM IR into account.
  for (line_iterator L(*SM.getMemoryBuffer(SM.getMainFileID()), false), E;
       L != E; ++L) {
    if (L.line_number() == Line) {
      LineStr = *L;
      Loc = SMLoc::getFromPointer(LineStr.data());
      auto Indent = LineStr.find(Error.getLineContents());
      if (Indent != StringRef::npos)
        Column += Indent;
      break;
    }
  }

  return SMDiagnostic(SM, Loc, Filename, Line, Column, Error.getKind(),
                      Error.getMessage(), LineStr, Error.getRanges(),
                      Error.getFixIts());
}

void MIRParserImpl::initNames2RegClasses(const MachineFunction &MF) {
  if (!Names2RegClasses.empty())
    return;
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  for (unsigned I = 0, E = TRI->getNumRegClasses(); I < E; ++I) {
    const auto *RC = TRI->getRegClass(I);
    Names2RegClasses.insert(
        std::make_pair(StringRef(TRI->getRegClassName(RC)).lower(), RC));
  }
}

void MIRParserImpl::initNames2RegBanks(const MachineFunction &MF) {
  if (!Names2RegBanks.empty())
    return;
  const RegisterBankInfo *RBI = MF.getSubtarget().getRegBankInfo();
  // If the target does not support GlobalISel, we may not have a
  // register bank info.
  if (!RBI)
    return;
  for (unsigned I = 0, E = RBI->getNumRegBanks(); I < E; ++I) {
    const auto &RegBank = RBI->getRegBank(I);
    Names2RegBanks.insert(
        std::make_pair(StringRef(RegBank.getName()).lower(), &RegBank));
  }
}

const TargetRegisterClass *MIRParserImpl::getRegClass(const MachineFunction &MF,
                                                      StringRef Name) {
  auto RegClassInfo = Names2RegClasses.find(Name);
  if (RegClassInfo == Names2RegClasses.end())
    return nullptr;
  return RegClassInfo->getValue();
}

const RegisterBank *MIRParserImpl::getRegBank(const MachineFunction &MF,
                                              StringRef Name) {
  auto RegBankInfo = Names2RegBanks.find(Name);
  if (RegBankInfo == Names2RegBanks.end())
    return nullptr;
  return RegBankInfo->getValue();
}

MIRParser::MIRParser(std::unique_ptr<MIRParserImpl> Impl)
    : Impl(std::move(Impl)) {}

MIRParser::~MIRParser() {}

std::unique_ptr<Module> MIRParser::parseIRModule() {
  return Impl->parseIRModule();
}

bool MIRParser::parseMachineFunctions(Module &M, MachineModuleInfo &MMI) {
  return Impl->parseMachineFunctions(M, MMI);
}

std::unique_ptr<MIRParser> llvm::createMIRParserFromFile(StringRef Filename,
                                                         SMDiagnostic &Error,
                                                         LLVMContext &Context) {
  auto FileOrErr = MemoryBuffer::getFileOrSTDIN(Filename);
  if (std::error_code EC = FileOrErr.getError()) {
    Error = SMDiagnostic(Filename, SourceMgr::DK_Error,
                         "Could not open input file: " + EC.message());
    return nullptr;
  }
  return createMIRParser(std::move(FileOrErr.get()), Context);
}

std::unique_ptr<MIRParser>
llvm::createMIRParser(std::unique_ptr<MemoryBuffer> Contents,
                      LLVMContext &Context) {
  auto Filename = Contents->getBufferIdentifier();
  if (Context.shouldDiscardValueNames()) {
    Context.diagnose(DiagnosticInfoMIRParser(
        DS_Error,
        SMDiagnostic(
            Filename, SourceMgr::DK_Error,
            "Can't read MIR with a Context that discards named Values")));
    return nullptr;
  }
  return llvm::make_unique<MIRParser>(
      llvm::make_unique<MIRParserImpl>(std::move(Contents), Filename, Context));
}
