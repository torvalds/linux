//===-- HexagonMCTargetDesc.cpp - Hexagon Target Descriptions -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides Hexagon specific target descriptions.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/HexagonMCTargetDesc.h"
#include "Hexagon.h"
#include "HexagonDepArch.h"
#include "HexagonTargetStreamer.h"
#include "MCTargetDesc/HexagonInstPrinter.h"
#include "MCTargetDesc/HexagonMCAsmInfo.h"
#include "MCTargetDesc/HexagonMCELFStreamer.h"
#include "MCTargetDesc/HexagonMCInstrInfo.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCELFStreamer.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>
#include <new>
#include <string>

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#include "HexagonGenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "HexagonGenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "HexagonGenRegisterInfo.inc"

cl::opt<bool> llvm::HexagonDisableCompound
  ("mno-compound",
   cl::desc("Disable looking for compound instructions for Hexagon"));

cl::opt<bool> llvm::HexagonDisableDuplex
  ("mno-pairing",
   cl::desc("Disable looking for duplex instructions for Hexagon"));

namespace { // These flags are to be deprecated
cl::opt<bool> MV5("mv5", cl::Hidden, cl::desc("Build for Hexagon V5"),
                  cl::init(false));
cl::opt<bool> MV55("mv55", cl::Hidden, cl::desc("Build for Hexagon V55"),
                   cl::init(false));
cl::opt<bool> MV60("mv60", cl::Hidden, cl::desc("Build for Hexagon V60"),
                   cl::init(false));
cl::opt<bool> MV62("mv62", cl::Hidden, cl::desc("Build for Hexagon V62"),
                   cl::init(false));
cl::opt<bool> MV65("mv65", cl::Hidden, cl::desc("Build for Hexagon V65"),
                   cl::init(false));
cl::opt<bool> MV66("mv66", cl::Hidden, cl::desc("Build for Hexagon V66"),
                   cl::init(false));
} // namespace

cl::opt<Hexagon::ArchEnum>
    EnableHVX("mhvx",
      cl::desc("Enable Hexagon Vector eXtensions"),
      cl::values(
        clEnumValN(Hexagon::ArchEnum::V60, "v60", "Build for HVX v60"),
        clEnumValN(Hexagon::ArchEnum::V62, "v62", "Build for HVX v62"),
        clEnumValN(Hexagon::ArchEnum::V65, "v65", "Build for HVX v65"),
        clEnumValN(Hexagon::ArchEnum::V66, "v66", "Build for HVX v66"),
        // Sentinel for no value specified.
        clEnumValN(Hexagon::ArchEnum::Generic, "", "")),
      // Sentinel for flag not present.
      cl::init(Hexagon::ArchEnum::NoArch), cl::ValueOptional);

static cl::opt<bool>
  DisableHVX("mno-hvx", cl::Hidden,
             cl::desc("Disable Hexagon Vector eXtensions"));


static StringRef DefaultArch = "hexagonv60";

static StringRef HexagonGetArchVariant() {
  if (MV5)
    return "hexagonv5";
  if (MV55)
    return "hexagonv55";
  if (MV60)
    return "hexagonv60";
  if (MV62)
    return "hexagonv62";
  if (MV65)
    return "hexagonv65";
  if (MV66)
    return "hexagonv66";
  return "";
}

StringRef Hexagon_MC::selectHexagonCPU(StringRef CPU) {
  StringRef ArchV = HexagonGetArchVariant();
  if (!ArchV.empty() && !CPU.empty()) {
    if (ArchV != CPU)
      report_fatal_error("conflicting architectures specified.");
    return CPU;
  }
  if (ArchV.empty()) {
    if (CPU.empty())
      CPU = DefaultArch;
    return CPU;
  }
  return ArchV;
}

unsigned llvm::HexagonGetLastSlot() { return HexagonItinerariesV5FU::SLOT3; }

namespace {

class HexagonTargetAsmStreamer : public HexagonTargetStreamer {
public:
  HexagonTargetAsmStreamer(MCStreamer &S,
                           formatted_raw_ostream &OS,
                           bool isVerboseAsm,
                           MCInstPrinter &IP)
      : HexagonTargetStreamer(S) {}

  void prettyPrintAsm(MCInstPrinter &InstPrinter, raw_ostream &OS,
                      const MCInst &Inst, const MCSubtargetInfo &STI) override {
    assert(HexagonMCInstrInfo::isBundle(Inst));
    assert(HexagonMCInstrInfo::bundleSize(Inst) <= HEXAGON_PACKET_SIZE);
    std::string Buffer;
    {
      raw_string_ostream TempStream(Buffer);
      InstPrinter.printInst(&Inst, TempStream, "", STI);
    }
    StringRef Contents(Buffer);
    auto PacketBundle = Contents.rsplit('\n');
    auto HeadTail = PacketBundle.first.split('\n');
    StringRef Separator = "\n";
    StringRef Indent = "\t";
    OS << "\t{\n";
    while (!HeadTail.first.empty()) {
      StringRef InstTxt;
      auto Duplex = HeadTail.first.split('\v');
      if (!Duplex.second.empty()) {
        OS << Indent << Duplex.first << Separator;
        InstTxt = Duplex.second;
      } else if (!HeadTail.first.trim().startswith("immext")) {
        InstTxt = Duplex.first;
      }
      if (!InstTxt.empty())
        OS << Indent << InstTxt << Separator;
      HeadTail = HeadTail.second.split('\n');
    }

    if (HexagonMCInstrInfo::isMemReorderDisabled(Inst))
      OS << "\n\t} :mem_noshuf" << PacketBundle.second;
    else
      OS << "\t}" << PacketBundle.second;
  }
};

class HexagonTargetELFStreamer : public HexagonTargetStreamer {
public:
  MCELFStreamer &getStreamer() {
    return static_cast<MCELFStreamer &>(Streamer);
  }
  HexagonTargetELFStreamer(MCStreamer &S, MCSubtargetInfo const &STI)
      : HexagonTargetStreamer(S) {
    MCAssembler &MCA = getStreamer().getAssembler();
    MCA.setELFHeaderEFlags(Hexagon_MC::GetELFFlags(STI));
  }


  void EmitCommonSymbolSorted(MCSymbol *Symbol, uint64_t Size,
                              unsigned ByteAlignment,
                              unsigned AccessSize) override {
    HexagonMCELFStreamer &HexagonELFStreamer =
        static_cast<HexagonMCELFStreamer &>(getStreamer());
    HexagonELFStreamer.HexagonMCEmitCommonSymbol(Symbol, Size, ByteAlignment,
                                                 AccessSize);
  }

  void EmitLocalCommonSymbolSorted(MCSymbol *Symbol, uint64_t Size,
                                   unsigned ByteAlignment,
                                   unsigned AccessSize) override {
    HexagonMCELFStreamer &HexagonELFStreamer =
        static_cast<HexagonMCELFStreamer &>(getStreamer());
    HexagonELFStreamer.HexagonMCEmitLocalCommonSymbol(
        Symbol, Size, ByteAlignment, AccessSize);
  }
};

} // end anonymous namespace

llvm::MCInstrInfo *llvm::createHexagonMCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitHexagonMCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createHexagonMCRegisterInfo(const Triple &TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitHexagonMCRegisterInfo(X, Hexagon::R31);
  return X;
}

static MCAsmInfo *createHexagonMCAsmInfo(const MCRegisterInfo &MRI,
                                         const Triple &TT) {
  MCAsmInfo *MAI = new HexagonMCAsmInfo(TT);

  // VirtualFP = (R30 + #0).
  MCCFIInstruction Inst =
      MCCFIInstruction::createDefCfa(nullptr,
          MRI.getDwarfRegNum(Hexagon::R30, true), 0);
  MAI->addInitialFrameState(Inst);

  return MAI;
}

static MCInstPrinter *createHexagonMCInstPrinter(const Triple &T,
                                                 unsigned SyntaxVariant,
                                                 const MCAsmInfo &MAI,
                                                 const MCInstrInfo &MII,
                                                 const MCRegisterInfo &MRI)
{
  if (SyntaxVariant == 0)
    return new HexagonInstPrinter(MAI, MII, MRI);
  else
    return nullptr;
}

static MCTargetStreamer *
createMCAsmTargetStreamer(MCStreamer &S, formatted_raw_ostream &OS,
                          MCInstPrinter *IP, bool IsVerboseAsm) {
  return new HexagonTargetAsmStreamer(S, OS, IsVerboseAsm, *IP);
}

static MCStreamer *createMCStreamer(Triple const &T, MCContext &Context,
                                    std::unique_ptr<MCAsmBackend> &&MAB,
                                    std::unique_ptr<MCObjectWriter> &&OW,
                                    std::unique_ptr<MCCodeEmitter> &&Emitter,
                                    bool RelaxAll) {
  return createHexagonELFStreamer(T, Context, std::move(MAB), std::move(OW),
                                  std::move(Emitter));
}

static MCTargetStreamer *
createHexagonObjectTargetStreamer(MCStreamer &S, const MCSubtargetInfo &STI) {
  return new HexagonTargetELFStreamer(S, STI);
}

static void LLVM_ATTRIBUTE_UNUSED clearFeature(MCSubtargetInfo* STI, uint64_t F) {
  uint64_t FB = STI->getFeatureBits().to_ullong();
  if (FB & (1ULL << F))
    STI->ToggleFeature(F);
}

static bool LLVM_ATTRIBUTE_UNUSED checkFeature(MCSubtargetInfo* STI, uint64_t F) {
  uint64_t FB = STI->getFeatureBits().to_ullong();
  return (FB & (1ULL << F)) != 0;
}

namespace {
std::string selectHexagonFS(StringRef CPU, StringRef FS) {
  SmallVector<StringRef, 3> Result;
  if (!FS.empty())
    Result.push_back(FS);

  switch (EnableHVX) {
  case Hexagon::ArchEnum::V5:
  case Hexagon::ArchEnum::V55:
    break;
  case Hexagon::ArchEnum::V60:
    Result.push_back("+hvxv60");
    break;
  case Hexagon::ArchEnum::V62:
    Result.push_back("+hvxv62");
    break;
  case Hexagon::ArchEnum::V65:
    Result.push_back("+hvxv65");
    break;
  case Hexagon::ArchEnum::V66:
    Result.push_back("+hvxv66");
    break;
  case Hexagon::ArchEnum::Generic:{
    Result.push_back(StringSwitch<StringRef>(CPU)
             .Case("hexagonv60", "+hvxv60")
             .Case("hexagonv62", "+hvxv62")
             .Case("hexagonv65", "+hvxv65")
             .Case("hexagonv66", "+hvxv66"));
    break;
  }
  case Hexagon::ArchEnum::NoArch:
    // Sentinal if -mhvx isn't specified
    break;
  }
  return join(Result.begin(), Result.end(), ",");
}
}

static bool isCPUValid(std::string CPU)
{
  std::vector<std::string> table {
    "generic",    "hexagonv5",  "hexagonv55", "hexagonv60",
    "hexagonv62", "hexagonv65", "hexagonv66",
  };

  return std::find(table.begin(), table.end(), CPU) != table.end();
}

namespace {
std::pair<std::string, std::string> selectCPUAndFS(StringRef CPU,
                                                   StringRef FS) {
  std::pair<std::string, std::string> Result;
  Result.first = Hexagon_MC::selectHexagonCPU(CPU);
  Result.second = selectHexagonFS(Result.first, FS);
  return Result;
}
}

FeatureBitset Hexagon_MC::completeHVXFeatures(const FeatureBitset &S) {
  using namespace Hexagon;
  // Make sure that +hvx-length turns hvx on, and that "hvx" alone
  // turns on hvxvNN, corresponding to the existing ArchVNN.
  FeatureBitset FB = S;
  unsigned CpuArch = ArchV5;
  for (unsigned F : {ArchV66, ArchV65, ArchV62, ArchV60, ArchV55, ArchV5}) {
    if (!FB.test(F))
      continue;
    CpuArch = F;
    break;
  }
  bool UseHvx = false;
  for (unsigned F : {ExtensionHVX, ExtensionHVX64B, ExtensionHVX128B}) {
    if (!FB.test(F))
      continue;
    UseHvx = true;
    break;
  }
  bool HasHvxVer = false;
  for (unsigned F : {ExtensionHVXV60, ExtensionHVXV62, ExtensionHVXV65,
                     ExtensionHVXV66}) {
    if (!FB.test(F))
      continue;
    HasHvxVer = true;
    UseHvx = true;
    break;
  }

  if (!UseHvx || HasHvxVer)
    return FB;

  // HasHvxVer is false, and UseHvx is true.
  switch (CpuArch) {
    case ArchV66:
      FB.set(ExtensionHVXV66);
      LLVM_FALLTHROUGH;
    case ArchV65:
      FB.set(ExtensionHVXV65);
      LLVM_FALLTHROUGH;
    case ArchV62:
      FB.set(ExtensionHVXV62);
      LLVM_FALLTHROUGH;
    case ArchV60:
      FB.set(ExtensionHVXV60);
      break;
  }
  return FB;
}

MCSubtargetInfo *Hexagon_MC::createHexagonMCSubtargetInfo(const Triple &TT,
                                                          StringRef CPU,
                                                          StringRef FS) {
  std::pair<std::string, std::string> Features = selectCPUAndFS(CPU, FS);
  StringRef CPUName = Features.first;
  StringRef ArchFS = Features.second;

  if (!isCPUValid(CPUName.str())) {
    errs() << "error: invalid CPU \"" << CPUName.str().c_str()
           << "\" specified\n";
    return nullptr;
  }

  MCSubtargetInfo *X = createHexagonMCSubtargetInfoImpl(TT, CPUName, ArchFS);
  if (HexagonDisableDuplex) {
    llvm::FeatureBitset Features = X->getFeatureBits();
    X->setFeatureBits(Features.set(Hexagon::FeatureDuplex, false));
  }

  X->setFeatureBits(completeHVXFeatures(X->getFeatureBits()));
  return X;
}

unsigned Hexagon_MC::GetELFFlags(const MCSubtargetInfo &STI) {
  static std::map<StringRef,unsigned> ElfFlags = {
    {"hexagonv5",  ELF::EF_HEXAGON_MACH_V5},
    {"hexagonv55", ELF::EF_HEXAGON_MACH_V55},
    {"hexagonv60", ELF::EF_HEXAGON_MACH_V60},
    {"hexagonv62", ELF::EF_HEXAGON_MACH_V62},
    {"hexagonv65", ELF::EF_HEXAGON_MACH_V65},
    {"hexagonv66", ELF::EF_HEXAGON_MACH_V66},
  };

  auto F = ElfFlags.find(STI.getCPU());
  assert(F != ElfFlags.end() && "Unrecognized Architecture");
  return F->second;
}

namespace {
class HexagonMCInstrAnalysis : public MCInstrAnalysis {
public:
  HexagonMCInstrAnalysis(MCInstrInfo const *Info) : MCInstrAnalysis(Info) {}

  bool isUnconditionalBranch(MCInst const &Inst) const override {
    //assert(!HexagonMCInstrInfo::isBundle(Inst));
    return MCInstrAnalysis::isUnconditionalBranch(Inst);
  }

  bool isConditionalBranch(MCInst const &Inst) const override {
    //assert(!HexagonMCInstrInfo::isBundle(Inst));
    return MCInstrAnalysis::isConditionalBranch(Inst);
  }

  bool evaluateBranch(MCInst const &Inst, uint64_t Addr,
                      uint64_t Size, uint64_t &Target) const override {
    //assert(!HexagonMCInstrInfo::isBundle(Inst));
    if(!HexagonMCInstrInfo::isExtendable(*Info, Inst))
      return false;
    auto const &Extended(HexagonMCInstrInfo::getExtendableOperand(*Info, Inst));
    assert(Extended.isExpr());
    int64_t Value;
    if(!Extended.getExpr()->evaluateAsAbsolute(Value))
      return false;
    Target = Value;
    return true;
  }
};
}

static MCInstrAnalysis *createHexagonMCInstrAnalysis(const MCInstrInfo *Info) {
  return new HexagonMCInstrAnalysis(Info);
}

// Force static initialization.
extern "C" void LLVMInitializeHexagonTargetMC() {
  // Register the MC asm info.
  RegisterMCAsmInfoFn X(getTheHexagonTarget(), createHexagonMCAsmInfo);

  // Register the MC instruction info.
  TargetRegistry::RegisterMCInstrInfo(getTheHexagonTarget(),
                                      createHexagonMCInstrInfo);

  // Register the MC register info.
  TargetRegistry::RegisterMCRegInfo(getTheHexagonTarget(),
                                    createHexagonMCRegisterInfo);

  // Register the MC subtarget info.
  TargetRegistry::RegisterMCSubtargetInfo(getTheHexagonTarget(),
    Hexagon_MC::createHexagonMCSubtargetInfo);

  // Register the MC Code Emitter
  TargetRegistry::RegisterMCCodeEmitter(getTheHexagonTarget(),
                                        createHexagonMCCodeEmitter);

  // Register the asm backend
  TargetRegistry::RegisterMCAsmBackend(getTheHexagonTarget(),
                                       createHexagonAsmBackend);


  // Register the MC instruction analyzer.
  TargetRegistry::RegisterMCInstrAnalysis(getTheHexagonTarget(),
                                          createHexagonMCInstrAnalysis);

  // Register the obj streamer
  TargetRegistry::RegisterELFStreamer(getTheHexagonTarget(),
                                      createMCStreamer);

  // Register the obj target streamer
  TargetRegistry::RegisterObjectTargetStreamer(getTheHexagonTarget(),
                                      createHexagonObjectTargetStreamer);

  // Register the asm streamer
  TargetRegistry::RegisterAsmTargetStreamer(getTheHexagonTarget(),
                                            createMCAsmTargetStreamer);

  // Register the MC Inst Printer
  TargetRegistry::RegisterMCInstPrinter(getTheHexagonTarget(),
                                        createHexagonMCInstPrinter);
}
