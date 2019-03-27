//===- OcamlGCPrinter.cpp - Ocaml frametable emitter ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements printing the assembly code for an Ocaml frametable.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/BuiltinGCs.h"
#include "llvm/CodeGen/GCMetadata.h"
#include "llvm/CodeGen/GCMetadataPrinter.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>

using namespace llvm;

namespace {

class OcamlGCMetadataPrinter : public GCMetadataPrinter {
public:
  void beginAssembly(Module &M, GCModuleInfo &Info, AsmPrinter &AP) override;
  void finishAssembly(Module &M, GCModuleInfo &Info, AsmPrinter &AP) override;
};

} // end anonymous namespace

static GCMetadataPrinterRegistry::Add<OcamlGCMetadataPrinter>
    Y("ocaml", "ocaml 3.10-compatible collector");

void llvm::linkOcamlGCPrinter() {}

static void EmitCamlGlobal(const Module &M, AsmPrinter &AP, const char *Id) {
  const std::string &MId = M.getModuleIdentifier();

  std::string SymName;
  SymName += "caml";
  size_t Letter = SymName.size();
  SymName.append(MId.begin(), llvm::find(MId, '.'));
  SymName += "__";
  SymName += Id;

  // Capitalize the first letter of the module name.
  SymName[Letter] = toupper(SymName[Letter]);

  SmallString<128> TmpStr;
  Mangler::getNameWithPrefix(TmpStr, SymName, M.getDataLayout());

  MCSymbol *Sym = AP.OutContext.getOrCreateSymbol(TmpStr);

  AP.OutStreamer->EmitSymbolAttribute(Sym, MCSA_Global);
  AP.OutStreamer->EmitLabel(Sym);
}

void OcamlGCMetadataPrinter::beginAssembly(Module &M, GCModuleInfo &Info,
                                           AsmPrinter &AP) {
  AP.OutStreamer->SwitchSection(AP.getObjFileLowering().getTextSection());
  EmitCamlGlobal(M, AP, "code_begin");

  AP.OutStreamer->SwitchSection(AP.getObjFileLowering().getDataSection());
  EmitCamlGlobal(M, AP, "data_begin");
}

/// emitAssembly - Print the frametable. The ocaml frametable format is thus:
///
///   extern "C" struct align(sizeof(intptr_t)) {
///     uint16_t NumDescriptors;
///     struct align(sizeof(intptr_t)) {
///       void *ReturnAddress;
///       uint16_t FrameSize;
///       uint16_t NumLiveOffsets;
///       uint16_t LiveOffsets[NumLiveOffsets];
///     } Descriptors[NumDescriptors];
///   } caml${module}__frametable;
///
/// Note that this precludes programs from stack frames larger than 64K
/// (FrameSize and LiveOffsets would overflow). FrameTablePrinter will abort if
/// either condition is detected in a function which uses the GC.
///
void OcamlGCMetadataPrinter::finishAssembly(Module &M, GCModuleInfo &Info,
                                            AsmPrinter &AP) {
  unsigned IntPtrSize = M.getDataLayout().getPointerSize();

  AP.OutStreamer->SwitchSection(AP.getObjFileLowering().getTextSection());
  EmitCamlGlobal(M, AP, "code_end");

  AP.OutStreamer->SwitchSection(AP.getObjFileLowering().getDataSection());
  EmitCamlGlobal(M, AP, "data_end");

  // FIXME: Why does ocaml emit this??
  AP.OutStreamer->EmitIntValue(0, IntPtrSize);

  AP.OutStreamer->SwitchSection(AP.getObjFileLowering().getDataSection());
  EmitCamlGlobal(M, AP, "frametable");

  int NumDescriptors = 0;
  for (GCModuleInfo::FuncInfoVec::iterator I = Info.funcinfo_begin(),
                                           IE = Info.funcinfo_end();
       I != IE; ++I) {
    GCFunctionInfo &FI = **I;
    if (FI.getStrategy().getName() != getStrategy().getName())
      // this function is managed by some other GC
      continue;
    for (GCFunctionInfo::iterator J = FI.begin(), JE = FI.end(); J != JE; ++J) {
      NumDescriptors++;
    }
  }

  if (NumDescriptors >= 1 << 16) {
    // Very rude!
    report_fatal_error(" Too much descriptor for ocaml GC");
  }
  AP.emitInt16(NumDescriptors);
  AP.EmitAlignment(IntPtrSize == 4 ? 2 : 3);

  for (GCModuleInfo::FuncInfoVec::iterator I = Info.funcinfo_begin(),
                                           IE = Info.funcinfo_end();
       I != IE; ++I) {
    GCFunctionInfo &FI = **I;
    if (FI.getStrategy().getName() != getStrategy().getName())
      // this function is managed by some other GC
      continue;

    uint64_t FrameSize = FI.getFrameSize();
    if (FrameSize >= 1 << 16) {
      // Very rude!
      report_fatal_error("Function '" + FI.getFunction().getName() +
                         "' is too large for the ocaml GC! "
                         "Frame size " +
                         Twine(FrameSize) + ">= 65536.\n"
                                            "(" +
                         Twine(uintptr_t(&FI)) + ")");
    }

    AP.OutStreamer->AddComment("live roots for " +
                               Twine(FI.getFunction().getName()));
    AP.OutStreamer->AddBlankLine();

    for (GCFunctionInfo::iterator J = FI.begin(), JE = FI.end(); J != JE; ++J) {
      size_t LiveCount = FI.live_size(J);
      if (LiveCount >= 1 << 16) {
        // Very rude!
        report_fatal_error("Function '" + FI.getFunction().getName() +
                           "' is too large for the ocaml GC! "
                           "Live root count " +
                           Twine(LiveCount) + " >= 65536.");
      }

      AP.OutStreamer->EmitSymbolValue(J->Label, IntPtrSize);
      AP.emitInt16(FrameSize);
      AP.emitInt16(LiveCount);

      for (GCFunctionInfo::live_iterator K = FI.live_begin(J),
                                         KE = FI.live_end(J);
           K != KE; ++K) {
        if (K->StackOffset >= 1 << 16) {
          // Very rude!
          report_fatal_error(
              "GC root stack offset is outside of fixed stack frame and out "
              "of range for ocaml GC!");
        }
        AP.emitInt16(K->StackOffset);
      }

      AP.EmitAlignment(IntPtrSize == 4 ? 2 : 3);
    }
  }
}
