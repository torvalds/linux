//===-- NVPTXAsmPrinter.h - NVPTX LLVM assembly writer ----------*- C++ -*-===//
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

#ifndef LLVM_LIB_TARGET_NVPTX_NVPTXASMPRINTER_H
#define LLVM_LIB_TARGET_NVPTX_NVPTXASMPRINTER_H

#include "NVPTX.h"
#include "NVPTXSubtarget.h"
#include "NVPTXTargetMachine.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Value.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/PassAnalysisSupport.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <algorithm>
#include <cassert>
#include <map>
#include <memory>
#include <string>
#include <vector>

// The ptx syntax and format is very different from that usually seem in a .s
// file,
// therefore we are not able to use the MCAsmStreamer interface here.
//
// We are handcrafting the output method here.
//
// A better approach is to clone the MCAsmStreamer to a MCPTXAsmStreamer
// (subclass of MCStreamer).

namespace llvm {

class MCOperand;

class LLVM_LIBRARY_VISIBILITY NVPTXAsmPrinter : public AsmPrinter {

  class AggBuffer {
    // Used to buffer the emitted string for initializing global
    // aggregates.
    //
    // Normally an aggregate (array, vector or structure) is emitted
    // as a u8[]. However, if one element/field of the aggregate
    // is a non-NULL address, then the aggregate is emitted as u32[]
    // or u64[].
    //
    // We first layout the aggregate in 'buffer' in bytes, except for
    // those symbol addresses. For the i-th symbol address in the
    //aggregate, its corresponding 4-byte or 8-byte elements in 'buffer'
    // are filled with 0s. symbolPosInBuffer[i-1] records its position
    // in 'buffer', and Symbols[i-1] records the Value*.
    //
    // Once we have this AggBuffer setup, we can choose how to print
    // it out.
  public:
    unsigned numSymbols;   // number of symbol addresses

  private:
    const unsigned size;   // size of the buffer in bytes
    std::vector<unsigned char> buffer; // the buffer
    SmallVector<unsigned, 4> symbolPosInBuffer;
    SmallVector<const Value *, 4> Symbols;
    // SymbolsBeforeStripping[i] is the original form of Symbols[i] before
    // stripping pointer casts, i.e.,
    // Symbols[i] == SymbolsBeforeStripping[i]->stripPointerCasts().
    //
    // We need to keep these values because AggBuffer::print decides whether to
    // emit a "generic()" cast for Symbols[i] depending on the address space of
    // SymbolsBeforeStripping[i].
    SmallVector<const Value *, 4> SymbolsBeforeStripping;
    unsigned curpos;
    raw_ostream &O;
    NVPTXAsmPrinter &AP;
    bool EmitGeneric;

  public:
    AggBuffer(unsigned size, raw_ostream &O, NVPTXAsmPrinter &AP)
        : size(size), buffer(size), O(O), AP(AP) {
      curpos = 0;
      numSymbols = 0;
      EmitGeneric = AP.EmitGeneric;
    }

    unsigned addBytes(unsigned char *Ptr, int Num, int Bytes) {
      assert((curpos + Num) <= size);
      assert((curpos + Bytes) <= size);
      for (int i = 0; i < Num; ++i) {
        buffer[curpos] = Ptr[i];
        curpos++;
      }
      for (int i = Num; i < Bytes; ++i) {
        buffer[curpos] = 0;
        curpos++;
      }
      return curpos;
    }

    unsigned addZeros(int Num) {
      assert((curpos + Num) <= size);
      for (int i = 0; i < Num; ++i) {
        buffer[curpos] = 0;
        curpos++;
      }
      return curpos;
    }

    void addSymbol(const Value *GVar, const Value *GVarBeforeStripping) {
      symbolPosInBuffer.push_back(curpos);
      Symbols.push_back(GVar);
      SymbolsBeforeStripping.push_back(GVarBeforeStripping);
      numSymbols++;
    }

    void print() {
      if (numSymbols == 0) {
        // print out in bytes
        for (unsigned i = 0; i < size; i++) {
          if (i)
            O << ", ";
          O << (unsigned int) buffer[i];
        }
      } else {
        // print out in 4-bytes or 8-bytes
        unsigned int pos = 0;
        unsigned int nSym = 0;
        unsigned int nextSymbolPos = symbolPosInBuffer[nSym];
        unsigned int nBytes = 4;
        if (static_cast<const NVPTXTargetMachine &>(AP.TM).is64Bit())
          nBytes = 8;
        for (pos = 0; pos < size; pos += nBytes) {
          if (pos)
            O << ", ";
          if (pos == nextSymbolPos) {
            const Value *v = Symbols[nSym];
            const Value *v0 = SymbolsBeforeStripping[nSym];
            if (const GlobalValue *GVar = dyn_cast<GlobalValue>(v)) {
              MCSymbol *Name = AP.getSymbol(GVar);
              PointerType *PTy = dyn_cast<PointerType>(v0->getType());
              bool IsNonGenericPointer = false; // Is v0 a non-generic pointer?
              if (PTy && PTy->getAddressSpace() != 0) {
                IsNonGenericPointer = true;
              }
              if (EmitGeneric && !isa<Function>(v) && !IsNonGenericPointer) {
                O << "generic(";
                Name->print(O, AP.MAI);
                O << ")";
              } else {
                Name->print(O, AP.MAI);
              }
            } else if (const ConstantExpr *CExpr = dyn_cast<ConstantExpr>(v0)) {
              const MCExpr *Expr =
                AP.lowerConstantForGV(cast<Constant>(CExpr), false);
              AP.printMCExpr(*Expr, O);
            } else
              llvm_unreachable("symbol type unknown");
            nSym++;
            if (nSym >= numSymbols)
              nextSymbolPos = size + 1;
            else
              nextSymbolPos = symbolPosInBuffer[nSym];
          } else if (nBytes == 4)
            O << *(unsigned int *)(&buffer[pos]);
          else
            O << *(unsigned long long *)(&buffer[pos]);
        }
      }
    }
  };

  friend class AggBuffer;

private:
  StringRef getPassName() const override { return "NVPTX Assembly Printer"; }

  const Function *F;
  std::string CurrentFnName;

  void EmitBasicBlockStart(const MachineBasicBlock &MBB) const override;
  void EmitFunctionEntryLabel() override;
  void EmitFunctionBodyStart() override;
  void EmitFunctionBodyEnd() override;
  void emitImplicitDef(const MachineInstr *MI) const override;

  void EmitInstruction(const MachineInstr *) override;
  void lowerToMCInst(const MachineInstr *MI, MCInst &OutMI);
  bool lowerOperand(const MachineOperand &MO, MCOperand &MCOp);
  MCOperand GetSymbolRef(const MCSymbol *Symbol);
  unsigned encodeVirtualRegister(unsigned Reg);

  void printVecModifiedImmediate(const MachineOperand &MO, const char *Modifier,
                                 raw_ostream &O);
  void printMemOperand(const MachineInstr *MI, int opNum, raw_ostream &O,
                       const char *Modifier = nullptr);
  void printModuleLevelGV(const GlobalVariable *GVar, raw_ostream &O,
                          bool = false);
  void printParamName(Function::const_arg_iterator I, int paramIndex,
                      raw_ostream &O);
  void emitGlobals(const Module &M);
  void emitHeader(Module &M, raw_ostream &O, const NVPTXSubtarget &STI);
  void emitKernelFunctionDirectives(const Function &F, raw_ostream &O) const;
  void emitVirtualRegister(unsigned int vr, raw_ostream &);
  void emitFunctionParamList(const Function *, raw_ostream &O);
  void emitFunctionParamList(const MachineFunction &MF, raw_ostream &O);
  void setAndEmitFunctionVirtualRegisters(const MachineFunction &MF);
  void printReturnValStr(const Function *, raw_ostream &O);
  void printReturnValStr(const MachineFunction &MF, raw_ostream &O);
  bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                       unsigned AsmVariant, const char *ExtraCode,
                       raw_ostream &) override;
  void printOperand(const MachineInstr *MI, int opNum, raw_ostream &O,
                    const char *Modifier = nullptr);
  bool PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNo,
                             unsigned AsmVariant, const char *ExtraCode,
                             raw_ostream &) override;

  const MCExpr *lowerConstantForGV(const Constant *CV, bool ProcessingGeneric);
  void printMCExpr(const MCExpr &Expr, raw_ostream &OS);

protected:
  bool doInitialization(Module &M) override;
  bool doFinalization(Module &M) override;

private:
  bool GlobalsEmitted;

  // This is specific per MachineFunction.
  const MachineRegisterInfo *MRI;
  // The contents are specific for each
  // MachineFunction. But the size of the
  // array is not.
  typedef DenseMap<unsigned, unsigned> VRegMap;
  typedef DenseMap<const TargetRegisterClass *, VRegMap> VRegRCMap;
  VRegRCMap VRegMapping;

  // List of variables demoted to a function scope.
  std::map<const Function *, std::vector<const GlobalVariable *>> localDecls;

  void emitPTXGlobalVariable(const GlobalVariable *GVar, raw_ostream &O);
  void emitPTXAddressSpace(unsigned int AddressSpace, raw_ostream &O) const;
  std::string getPTXFundamentalTypeStr(Type *Ty, bool = true) const;
  void printScalarConstant(const Constant *CPV, raw_ostream &O);
  void printFPConstant(const ConstantFP *Fp, raw_ostream &O);
  void bufferLEByte(const Constant *CPV, int Bytes, AggBuffer *aggBuffer);
  void bufferAggregateConstant(const Constant *CV, AggBuffer *aggBuffer);

  void emitLinkageDirective(const GlobalValue *V, raw_ostream &O);
  void emitDeclarations(const Module &, raw_ostream &O);
  void emitDeclaration(const Function *, raw_ostream &O);
  void emitDemotedVars(const Function *, raw_ostream &);

  bool lowerImageHandleOperand(const MachineInstr *MI, unsigned OpNo,
                               MCOperand &MCOp);
  void lowerImageHandleSymbol(unsigned Index, MCOperand &MCOp);

  bool isLoopHeaderOfNoUnroll(const MachineBasicBlock &MBB) const;

  // Used to control the need to emit .generic() in the initializer of
  // module scope variables.
  // Although ptx supports the hybrid mode like the following,
  //    .global .u32 a;
  //    .global .u32 b;
  //    .global .u32 addr[] = {a, generic(b)}
  // we have difficulty representing the difference in the NVVM IR.
  //
  // Since the address value should always be generic in CUDA C and always
  // be specific in OpenCL, we use this simple control here.
  //
  bool EmitGeneric;

public:
  NVPTXAsmPrinter(TargetMachine &TM, std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer)),
        EmitGeneric(static_cast<NVPTXTargetMachine &>(TM).getDrvInterface() ==
                    NVPTX::CUDA) {}

  bool runOnMachineFunction(MachineFunction &F) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineLoopInfo>();
    AsmPrinter::getAnalysisUsage(AU);
  }

  std::string getVirtualRegisterName(unsigned) const;

  const MCSymbol *getFunctionFrameSymbol() const override;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_NVPTX_NVPTXASMPRINTER_H
