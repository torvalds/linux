//===-- AsmPrinterInlineAsm.cpp - AsmPrinter Inline Asm Handling ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the inline assembler pieces of the AsmPrinter class.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
using namespace llvm;

#define DEBUG_TYPE "asm-printer"

/// srcMgrDiagHandler - This callback is invoked when the SourceMgr for an
/// inline asm has an error in it.  diagInfo is a pointer to the SrcMgrDiagInfo
/// struct above.
static void srcMgrDiagHandler(const SMDiagnostic &Diag, void *diagInfo) {
  AsmPrinter::SrcMgrDiagInfo *DiagInfo =
      static_cast<AsmPrinter::SrcMgrDiagInfo *>(diagInfo);
  assert(DiagInfo && "Diagnostic context not passed down?");

  // Look up a LocInfo for the buffer this diagnostic is coming from.
  unsigned BufNum = DiagInfo->SrcMgr.FindBufferContainingLoc(Diag.getLoc());
  const MDNode *LocInfo = nullptr;
  if (BufNum > 0 && BufNum <= DiagInfo->LocInfos.size())
    LocInfo = DiagInfo->LocInfos[BufNum-1];

  // If the inline asm had metadata associated with it, pull out a location
  // cookie corresponding to which line the error occurred on.
  unsigned LocCookie = 0;
  if (LocInfo) {
    unsigned ErrorLine = Diag.getLineNo()-1;
    if (ErrorLine >= LocInfo->getNumOperands())
      ErrorLine = 0;

    if (LocInfo->getNumOperands() != 0)
      if (const ConstantInt *CI =
              mdconst::dyn_extract<ConstantInt>(LocInfo->getOperand(ErrorLine)))
        LocCookie = CI->getZExtValue();
  }

  DiagInfo->DiagHandler(Diag, DiagInfo->DiagContext, LocCookie);
}

unsigned AsmPrinter::addInlineAsmDiagBuffer(StringRef AsmStr,
                                            const MDNode *LocMDNode) const {
  if (!DiagInfo) {
    DiagInfo = make_unique<SrcMgrDiagInfo>();

    MCContext &Context = MMI->getContext();
    Context.setInlineSourceManager(&DiagInfo->SrcMgr);

    LLVMContext &LLVMCtx = MMI->getModule()->getContext();
    if (LLVMCtx.getInlineAsmDiagnosticHandler()) {
      DiagInfo->DiagHandler = LLVMCtx.getInlineAsmDiagnosticHandler();
      DiagInfo->DiagContext = LLVMCtx.getInlineAsmDiagnosticContext();
      DiagInfo->SrcMgr.setDiagHandler(srcMgrDiagHandler, DiagInfo.get());
    }
  }

  SourceMgr &SrcMgr = DiagInfo->SrcMgr;

  std::unique_ptr<MemoryBuffer> Buffer;
  // The inline asm source manager will outlive AsmStr, so make a copy of the
  // string for SourceMgr to own.
  Buffer = MemoryBuffer::getMemBufferCopy(AsmStr, "<inline asm>");

  // Tell SrcMgr about this buffer, it takes ownership of the buffer.
  unsigned BufNum = SrcMgr.AddNewSourceBuffer(std::move(Buffer), SMLoc());

  // Store LocMDNode in DiagInfo, using BufNum as an identifier.
  if (LocMDNode) {
    DiagInfo->LocInfos.resize(BufNum);
    DiagInfo->LocInfos[BufNum - 1] = LocMDNode;
  }

  return BufNum;
}


/// EmitInlineAsm - Emit a blob of inline asm to the output streamer.
void AsmPrinter::EmitInlineAsm(StringRef Str, const MCSubtargetInfo &STI,
                               const MCTargetOptions &MCOptions,
                               const MDNode *LocMDNode,
                               InlineAsm::AsmDialect Dialect) const {
  assert(!Str.empty() && "Can't emit empty inline asm block");

  // Remember if the buffer is nul terminated or not so we can avoid a copy.
  bool isNullTerminated = Str.back() == 0;
  if (isNullTerminated)
    Str = Str.substr(0, Str.size()-1);

  // If the output streamer does not have mature MC support or the integrated
  // assembler has been disabled, just emit the blob textually.
  // Otherwise parse the asm and emit it via MC support.
  // This is useful in case the asm parser doesn't handle something but the
  // system assembler does.
  const MCAsmInfo *MCAI = TM.getMCAsmInfo();
  assert(MCAI && "No MCAsmInfo");
  if (!MCAI->useIntegratedAssembler() &&
      !OutStreamer->isIntegratedAssemblerRequired()) {
    emitInlineAsmStart();
    OutStreamer->EmitRawText(Str);
    emitInlineAsmEnd(STI, nullptr);
    return;
  }

  unsigned BufNum = addInlineAsmDiagBuffer(Str, LocMDNode);
  DiagInfo->SrcMgr.setIncludeDirs(MCOptions.IASSearchPaths);

  std::unique_ptr<MCAsmParser> Parser(createMCAsmParser(
          DiagInfo->SrcMgr, OutContext, *OutStreamer, *MAI, BufNum));

  // Do not use assembler-level information for parsing inline assembly.
  OutStreamer->setUseAssemblerInfoForParsing(false);

  // We create a new MCInstrInfo here since we might be at the module level
  // and not have a MachineFunction to initialize the TargetInstrInfo from and
  // we only need MCInstrInfo for asm parsing. We create one unconditionally
  // because it's not subtarget dependent.
  std::unique_ptr<MCInstrInfo> MII(TM.getTarget().createMCInstrInfo());
  std::unique_ptr<MCTargetAsmParser> TAP(TM.getTarget().createMCAsmParser(
      STI, *Parser, *MII, MCOptions));
  if (!TAP)
    report_fatal_error("Inline asm not supported by this streamer because"
                       " we don't have an asm parser for this target\n");
  Parser->setAssemblerDialect(Dialect);
  Parser->setTargetParser(*TAP.get());
  Parser->setEnablePrintSchedInfo(EnablePrintSchedInfo);
  // Enable lexing Masm binary and hex integer literals in intel inline
  // assembly.
  if (Dialect == InlineAsm::AD_Intel)
    Parser->getLexer().setLexMasmIntegers(true);
  if (MF) {
    const TargetRegisterInfo *TRI = MF->getSubtarget().getRegisterInfo();
    TAP->SetFrameRegister(TRI->getFrameRegister(*MF));
  }

  emitInlineAsmStart();
  // Don't implicitly switch to the text section before the asm.
  int Res = Parser->Run(/*NoInitialTextSection*/ true,
                        /*NoFinalize*/ true);
  emitInlineAsmEnd(STI, &TAP->getSTI());

  if (Res && !DiagInfo->DiagHandler)
    report_fatal_error("Error parsing inline asm\n");
}

static void EmitMSInlineAsmStr(const char *AsmStr, const MachineInstr *MI,
                               MachineModuleInfo *MMI, int InlineAsmVariant,
                               AsmPrinter *AP, unsigned LocCookie,
                               raw_ostream &OS) {
  // Switch to the inline assembly variant.
  OS << "\t.intel_syntax\n\t";

  const char *LastEmitted = AsmStr; // One past the last character emitted.
  unsigned NumOperands = MI->getNumOperands();

  while (*LastEmitted) {
    switch (*LastEmitted) {
    default: {
      // Not a special case, emit the string section literally.
      const char *LiteralEnd = LastEmitted+1;
      while (*LiteralEnd && *LiteralEnd != '{' && *LiteralEnd != '|' &&
             *LiteralEnd != '}' && *LiteralEnd != '$' && *LiteralEnd != '\n')
        ++LiteralEnd;

      OS.write(LastEmitted, LiteralEnd-LastEmitted);
      LastEmitted = LiteralEnd;
      break;
    }
    case '\n':
      ++LastEmitted;   // Consume newline character.
      OS << '\n';      // Indent code with newline.
      break;
    case '$': {
      ++LastEmitted;   // Consume '$' character.
      bool Done = true;

      // Handle escapes.
      switch (*LastEmitted) {
      default: Done = false; break;
      case '$':
        ++LastEmitted;  // Consume second '$' character.
        break;
      }
      if (Done) break;

      // If we have ${:foo}, then this is not a real operand reference, it is a
      // "magic" string reference, just like in .td files.  Arrange to call
      // PrintSpecial.
      if (LastEmitted[0] == '{' && LastEmitted[1] == ':') {
        LastEmitted += 2;
        const char *StrStart = LastEmitted;
        const char *StrEnd = strchr(StrStart, '}');
        if (!StrEnd)
          report_fatal_error("Unterminated ${:foo} operand in inline asm"
                             " string: '" + Twine(AsmStr) + "'");

        std::string Val(StrStart, StrEnd);
        AP->PrintSpecial(MI, OS, Val.c_str());
        LastEmitted = StrEnd+1;
        break;
      }

      const char *IDStart = LastEmitted;
      const char *IDEnd = IDStart;
      while (*IDEnd >= '0' && *IDEnd <= '9') ++IDEnd;

      unsigned Val;
      if (StringRef(IDStart, IDEnd-IDStart).getAsInteger(10, Val))
        report_fatal_error("Bad $ operand number in inline asm string: '" +
                           Twine(AsmStr) + "'");
      LastEmitted = IDEnd;

      if (Val >= NumOperands-1)
        report_fatal_error("Invalid $ operand number in inline asm string: '" +
                           Twine(AsmStr) + "'");

      // Okay, we finally have a value number.  Ask the target to print this
      // operand!
      unsigned OpNo = InlineAsm::MIOp_FirstOperand;

      bool Error = false;

      // Scan to find the machine operand number for the operand.
      for (; Val; --Val) {
        if (OpNo >= MI->getNumOperands()) break;
        unsigned OpFlags = MI->getOperand(OpNo).getImm();
        OpNo += InlineAsm::getNumOperandRegisters(OpFlags) + 1;
      }

      // We may have a location metadata attached to the end of the
      // instruction, and at no point should see metadata at any
      // other point while processing. It's an error if so.
      if (OpNo >= MI->getNumOperands() ||
          MI->getOperand(OpNo).isMetadata()) {
        Error = true;
      } else {
        unsigned OpFlags = MI->getOperand(OpNo).getImm();
        ++OpNo;  // Skip over the ID number.

        if (InlineAsm::isMemKind(OpFlags)) {
          Error = AP->PrintAsmMemoryOperand(MI, OpNo, InlineAsmVariant,
                                            /*Modifier*/ nullptr, OS);
        } else {
          Error = AP->PrintAsmOperand(MI, OpNo, InlineAsmVariant,
                                      /*Modifier*/ nullptr, OS);
        }
      }
      if (Error) {
        std::string msg;
        raw_string_ostream Msg(msg);
        Msg << "invalid operand in inline asm: '" << AsmStr << "'";
        MMI->getModule()->getContext().emitError(LocCookie, Msg.str());
      }
      break;
    }
    }
  }
  OS << "\n\t.att_syntax\n" << (char)0;  // null terminate string.
}

static void EmitGCCInlineAsmStr(const char *AsmStr, const MachineInstr *MI,
                                MachineModuleInfo *MMI, int InlineAsmVariant,
                                int AsmPrinterVariant, AsmPrinter *AP,
                                unsigned LocCookie, raw_ostream &OS) {
  int CurVariant = -1;            // The number of the {.|.|.} region we are in.
  const char *LastEmitted = AsmStr; // One past the last character emitted.
  unsigned NumOperands = MI->getNumOperands();

  OS << '\t';

  while (*LastEmitted) {
    switch (*LastEmitted) {
    default: {
      // Not a special case, emit the string section literally.
      const char *LiteralEnd = LastEmitted+1;
      while (*LiteralEnd && *LiteralEnd != '{' && *LiteralEnd != '|' &&
             *LiteralEnd != '}' && *LiteralEnd != '$' && *LiteralEnd != '\n')
        ++LiteralEnd;
      if (CurVariant == -1 || CurVariant == AsmPrinterVariant)
        OS.write(LastEmitted, LiteralEnd-LastEmitted);
      LastEmitted = LiteralEnd;
      break;
    }
    case '\n':
      ++LastEmitted;   // Consume newline character.
      OS << '\n';      // Indent code with newline.
      break;
    case '$': {
      ++LastEmitted;   // Consume '$' character.
      bool Done = true;

      // Handle escapes.
      switch (*LastEmitted) {
      default: Done = false; break;
      case '$':     // $$ -> $
        if (CurVariant == -1 || CurVariant == AsmPrinterVariant)
          OS << '$';
        ++LastEmitted;  // Consume second '$' character.
        break;
      case '(':             // $( -> same as GCC's { character.
        ++LastEmitted;      // Consume '(' character.
        if (CurVariant != -1)
          report_fatal_error("Nested variants found in inline asm string: '" +
                             Twine(AsmStr) + "'");
        CurVariant = 0;     // We're in the first variant now.
        break;
      case '|':
        ++LastEmitted;  // consume '|' character.
        if (CurVariant == -1)
          OS << '|';       // this is gcc's behavior for | outside a variant
        else
          ++CurVariant;   // We're in the next variant.
        break;
      case ')':         // $) -> same as GCC's } char.
        ++LastEmitted;  // consume ')' character.
        if (CurVariant == -1)
          OS << '}';     // this is gcc's behavior for } outside a variant
        else
          CurVariant = -1;
        break;
      }
      if (Done) break;

      bool HasCurlyBraces = false;
      if (*LastEmitted == '{') {     // ${variable}
        ++LastEmitted;               // Consume '{' character.
        HasCurlyBraces = true;
      }

      // If we have ${:foo}, then this is not a real operand reference, it is a
      // "magic" string reference, just like in .td files.  Arrange to call
      // PrintSpecial.
      if (HasCurlyBraces && *LastEmitted == ':') {
        ++LastEmitted;
        const char *StrStart = LastEmitted;
        const char *StrEnd = strchr(StrStart, '}');
        if (!StrEnd)
          report_fatal_error("Unterminated ${:foo} operand in inline asm"
                             " string: '" + Twine(AsmStr) + "'");

        std::string Val(StrStart, StrEnd);
        AP->PrintSpecial(MI, OS, Val.c_str());
        LastEmitted = StrEnd+1;
        break;
      }

      const char *IDStart = LastEmitted;
      const char *IDEnd = IDStart;
      while (*IDEnd >= '0' && *IDEnd <= '9') ++IDEnd;

      unsigned Val;
      if (StringRef(IDStart, IDEnd-IDStart).getAsInteger(10, Val))
        report_fatal_error("Bad $ operand number in inline asm string: '" +
                           Twine(AsmStr) + "'");
      LastEmitted = IDEnd;

      char Modifier[2] = { 0, 0 };

      if (HasCurlyBraces) {
        // If we have curly braces, check for a modifier character.  This
        // supports syntax like ${0:u}, which correspond to "%u0" in GCC asm.
        if (*LastEmitted == ':') {
          ++LastEmitted;    // Consume ':' character.
          if (*LastEmitted == 0)
            report_fatal_error("Bad ${:} expression in inline asm string: '" +
                               Twine(AsmStr) + "'");

          Modifier[0] = *LastEmitted;
          ++LastEmitted;    // Consume modifier character.
        }

        if (*LastEmitted != '}')
          report_fatal_error("Bad ${} expression in inline asm string: '" +
                             Twine(AsmStr) + "'");
        ++LastEmitted;    // Consume '}' character.
      }

      if (Val >= NumOperands-1)
        report_fatal_error("Invalid $ operand number in inline asm string: '" +
                           Twine(AsmStr) + "'");

      // Okay, we finally have a value number.  Ask the target to print this
      // operand!
      if (CurVariant == -1 || CurVariant == AsmPrinterVariant) {
        unsigned OpNo = InlineAsm::MIOp_FirstOperand;

        bool Error = false;

        // Scan to find the machine operand number for the operand.
        for (; Val; --Val) {
          if (OpNo >= MI->getNumOperands()) break;
          unsigned OpFlags = MI->getOperand(OpNo).getImm();
          OpNo += InlineAsm::getNumOperandRegisters(OpFlags) + 1;
        }

        // We may have a location metadata attached to the end of the
        // instruction, and at no point should see metadata at any
        // other point while processing. It's an error if so.
        if (OpNo >= MI->getNumOperands() ||
            MI->getOperand(OpNo).isMetadata()) {
          Error = true;
        } else {
          unsigned OpFlags = MI->getOperand(OpNo).getImm();
          ++OpNo;  // Skip over the ID number.

          if (Modifier[0] == 'l') { // Labels are target independent.
            // FIXME: What if the operand isn't an MBB, report error?
            const MCSymbol *Sym = MI->getOperand(OpNo).getMBB()->getSymbol();
            Sym->print(OS, AP->MAI);
          } else {
            if (InlineAsm::isMemKind(OpFlags)) {
              Error = AP->PrintAsmMemoryOperand(MI, OpNo, InlineAsmVariant,
                                                Modifier[0] ? Modifier : nullptr,
                                                OS);
            } else {
              Error = AP->PrintAsmOperand(MI, OpNo, InlineAsmVariant,
                                          Modifier[0] ? Modifier : nullptr, OS);
            }
          }
        }
        if (Error) {
          std::string msg;
          raw_string_ostream Msg(msg);
          Msg << "invalid operand in inline asm: '" << AsmStr << "'";
          MMI->getModule()->getContext().emitError(LocCookie, Msg.str());
        }
      }
      break;
    }
    }
  }
  OS << '\n' << (char)0;  // null terminate string.
}

/// EmitInlineAsm - This method formats and emits the specified machine
/// instruction that is an inline asm.
void AsmPrinter::EmitInlineAsm(const MachineInstr *MI) const {
  assert(MI->isInlineAsm() && "printInlineAsm only works on inline asms");

  // Count the number of register definitions to find the asm string.
  unsigned NumDefs = 0;
  for (; MI->getOperand(NumDefs).isReg() && MI->getOperand(NumDefs).isDef();
       ++NumDefs)
    assert(NumDefs != MI->getNumOperands()-2 && "No asm string?");

  assert(MI->getOperand(NumDefs).isSymbol() && "No asm string?");

  // Disassemble the AsmStr, printing out the literal pieces, the operands, etc.
  const char *AsmStr = MI->getOperand(NumDefs).getSymbolName();

  // If this asmstr is empty, just print the #APP/#NOAPP markers.
  // These are useful to see where empty asm's wound up.
  if (AsmStr[0] == 0) {
    OutStreamer->emitRawComment(MAI->getInlineAsmStart());
    OutStreamer->emitRawComment(MAI->getInlineAsmEnd());
    return;
  }

  // Emit the #APP start marker.  This has to happen even if verbose-asm isn't
  // enabled, so we use emitRawComment.
  OutStreamer->emitRawComment(MAI->getInlineAsmStart());

  // Get the !srcloc metadata node if we have it, and decode the loc cookie from
  // it.
  unsigned LocCookie = 0;
  const MDNode *LocMD = nullptr;
  for (unsigned i = MI->getNumOperands(); i != 0; --i) {
    if (MI->getOperand(i-1).isMetadata() &&
        (LocMD = MI->getOperand(i-1).getMetadata()) &&
        LocMD->getNumOperands() != 0) {
      if (const ConstantInt *CI =
              mdconst::dyn_extract<ConstantInt>(LocMD->getOperand(0))) {
        LocCookie = CI->getZExtValue();
        break;
      }
    }
  }

  // Emit the inline asm to a temporary string so we can emit it through
  // EmitInlineAsm.
  SmallString<256> StringData;
  raw_svector_ostream OS(StringData);

  // The variant of the current asmprinter.
  int AsmPrinterVariant = MAI->getAssemblerDialect();
  InlineAsm::AsmDialect InlineAsmVariant = MI->getInlineAsmDialect();
  AsmPrinter *AP = const_cast<AsmPrinter*>(this);
  if (InlineAsmVariant == InlineAsm::AD_ATT)
    EmitGCCInlineAsmStr(AsmStr, MI, MMI, InlineAsmVariant, AsmPrinterVariant,
                        AP, LocCookie, OS);
  else
    EmitMSInlineAsmStr(AsmStr, MI, MMI, InlineAsmVariant, AP, LocCookie, OS);

  // Reset SanitizeAddress based on the function's attribute.
  MCTargetOptions MCOptions = TM.Options.MCOptions;
  MCOptions.SanitizeAddress =
      MF->getFunction().hasFnAttribute(Attribute::SanitizeAddress);

  // Emit warnings if we use reserved registers on the clobber list, as
  // that might give surprising results.
  std::vector<std::string> RestrRegs;
  // Start with the first operand descriptor, and iterate over them.
  for (unsigned I = InlineAsm::MIOp_FirstOperand, NumOps = MI->getNumOperands();
       I < NumOps; ++I) {
    const MachineOperand &MO = MI->getOperand(I);
    if (MO.isImm()) {
      unsigned Flags = MO.getImm();
      const TargetRegisterInfo *TRI = MF->getSubtarget().getRegisterInfo();
      if (InlineAsm::getKind(Flags) == InlineAsm::Kind_Clobber &&
          !TRI->isAsmClobberable(*MF, MI->getOperand(I + 1).getReg())) {
        RestrRegs.push_back(TRI->getName(MI->getOperand(I + 1).getReg()));
      }
      // Skip to one before the next operand descriptor, if it exists.
      I += InlineAsm::getNumOperandRegisters(Flags);
    }
  }

  if (!RestrRegs.empty()) {
    unsigned BufNum = addInlineAsmDiagBuffer(OS.str(), LocMD);
    auto &SrcMgr = DiagInfo->SrcMgr;
    SMLoc Loc = SMLoc::getFromPointer(
        SrcMgr.getMemoryBuffer(BufNum)->getBuffer().begin());

    std::string Msg = "inline asm clobber list contains reserved registers: ";
    for (auto I = RestrRegs.begin(), E = RestrRegs.end(); I != E; I++) {
      if(I != RestrRegs.begin())
        Msg += ", ";
      Msg += *I;
    }
    std::string Note = "Reserved registers on the clobber list may not be "
                "preserved across the asm statement, and clobbering them may "
                "lead to undefined behaviour.";
    SrcMgr.PrintMessage(Loc, SourceMgr::DK_Warning, Msg);
    SrcMgr.PrintMessage(Loc, SourceMgr::DK_Note, Note);
  }

  EmitInlineAsm(OS.str(), getSubtargetInfo(), MCOptions, LocMD,
                MI->getInlineAsmDialect());

  // Emit the #NOAPP end marker.  This has to happen even if verbose-asm isn't
  // enabled, so we use emitRawComment.
  OutStreamer->emitRawComment(MAI->getInlineAsmEnd());
}


/// PrintSpecial - Print information related to the specified machine instr
/// that is independent of the operand, and may be independent of the instr
/// itself.  This can be useful for portably encoding the comment character
/// or other bits of target-specific knowledge into the asmstrings.  The
/// syntax used is ${:comment}.  Targets can override this to add support
/// for their own strange codes.
void AsmPrinter::PrintSpecial(const MachineInstr *MI, raw_ostream &OS,
                              const char *Code) const {
  if (!strcmp(Code, "private")) {
    const DataLayout &DL = MF->getDataLayout();
    OS << DL.getPrivateGlobalPrefix();
  } else if (!strcmp(Code, "comment")) {
    OS << MAI->getCommentString();
  } else if (!strcmp(Code, "uid")) {
    // Comparing the address of MI isn't sufficient, because machineinstrs may
    // be allocated to the same address across functions.

    // If this is a new LastFn instruction, bump the counter.
    if (LastMI != MI || LastFn != getFunctionNumber()) {
      ++Counter;
      LastMI = MI;
      LastFn = getFunctionNumber();
    }
    OS << Counter;
  } else {
    std::string msg;
    raw_string_ostream Msg(msg);
    Msg << "Unknown special formatter '" << Code
         << "' for machine instr: " << *MI;
    report_fatal_error(Msg.str());
  }
}

/// PrintAsmOperand - Print the specified operand of MI, an INLINEASM
/// instruction, using the specified assembler variant.  Targets should
/// override this to format as appropriate.
bool AsmPrinter::PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                                 unsigned AsmVariant, const char *ExtraCode,
                                 raw_ostream &O) {
  // Does this asm operand have a single letter operand modifier?
  if (ExtraCode && ExtraCode[0]) {
    if (ExtraCode[1] != 0) return true; // Unknown modifier.

    const MachineOperand &MO = MI->getOperand(OpNo);
    switch (ExtraCode[0]) {
    default:
      return true;  // Unknown modifier.
    case 'c': // Substitute immediate value without immediate syntax
      if (MO.getType() != MachineOperand::MO_Immediate)
        return true;
      O << MO.getImm();
      return false;
    case 'n':  // Negate the immediate constant.
      if (MO.getType() != MachineOperand::MO_Immediate)
        return true;
      O << -MO.getImm();
      return false;
    case 's':  // The GCC deprecated s modifier
      if (MO.getType() != MachineOperand::MO_Immediate)
        return true;
      O << ((32 - MO.getImm()) & 31);
      return false;
    }
  }
  return true;
}

bool AsmPrinter::PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNo,
                                       unsigned AsmVariant,
                                       const char *ExtraCode, raw_ostream &O) {
  // Target doesn't support this yet!
  return true;
}

void AsmPrinter::emitInlineAsmStart() const {}

void AsmPrinter::emitInlineAsmEnd(const MCSubtargetInfo &StartInfo,
                                  const MCSubtargetInfo *EndInfo) const {}
