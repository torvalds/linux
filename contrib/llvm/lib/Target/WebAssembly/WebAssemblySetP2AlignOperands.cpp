//=- WebAssemblySetP2AlignOperands.cpp - Set alignments on loads and stores -=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file sets the p2align operands on load and store instructions.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/WebAssemblyMCTargetDesc.h"
#include "WebAssembly.h"
#include "WebAssemblyMachineFunctionInfo.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "wasm-set-p2align-operands"

namespace {
class WebAssemblySetP2AlignOperands final : public MachineFunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid
  WebAssemblySetP2AlignOperands() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override {
    return "WebAssembly Set p2align Operands";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addPreserved<MachineBlockFrequencyInfo>();
    AU.addPreservedID(MachineDominatorsID);
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // end anonymous namespace

char WebAssemblySetP2AlignOperands::ID = 0;
INITIALIZE_PASS(WebAssemblySetP2AlignOperands, DEBUG_TYPE,
                "Set the p2align operands for WebAssembly loads and stores",
                false, false)

FunctionPass *llvm::createWebAssemblySetP2AlignOperands() {
  return new WebAssemblySetP2AlignOperands();
}

static void RewriteP2Align(MachineInstr &MI, unsigned OperandNo) {
  assert(MI.getOperand(OperandNo).getImm() == 0 &&
         "ISel should set p2align operands to 0");
  assert(MI.hasOneMemOperand() &&
         "Load and store instructions have exactly one mem operand");
  assert((*MI.memoperands_begin())->getSize() ==
             (UINT64_C(1) << WebAssembly::GetDefaultP2Align(MI.getOpcode())) &&
         "Default p2align value should be natural");
  assert(MI.getDesc().OpInfo[OperandNo].OperandType ==
             WebAssembly::OPERAND_P2ALIGN &&
         "Load and store instructions should have a p2align operand");
  uint64_t P2Align = Log2_64((*MI.memoperands_begin())->getAlignment());

  // WebAssembly does not currently support supernatural alignment.
  P2Align = std::min(P2Align,
                     uint64_t(WebAssembly::GetDefaultP2Align(MI.getOpcode())));

  MI.getOperand(OperandNo).setImm(P2Align);
}

bool WebAssemblySetP2AlignOperands::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG({
    dbgs() << "********** Set p2align Operands **********\n"
           << "********** Function: " << MF.getName() << '\n';
  });

  bool Changed = false;

  for (auto &MBB : MF) {
    for (auto &MI : MBB) {
      switch (MI.getOpcode()) {
      case WebAssembly::LOAD_I32:
      case WebAssembly::LOAD_I64:
      case WebAssembly::LOAD_F32:
      case WebAssembly::LOAD_F64:
      case WebAssembly::LOAD_v16i8:
      case WebAssembly::LOAD_v8i16:
      case WebAssembly::LOAD_v4i32:
      case WebAssembly::LOAD_v2i64:
      case WebAssembly::LOAD_v4f32:
      case WebAssembly::LOAD_v2f64:
      case WebAssembly::LOAD8_S_I32:
      case WebAssembly::LOAD8_U_I32:
      case WebAssembly::LOAD16_S_I32:
      case WebAssembly::LOAD16_U_I32:
      case WebAssembly::LOAD8_S_I64:
      case WebAssembly::LOAD8_U_I64:
      case WebAssembly::LOAD16_S_I64:
      case WebAssembly::LOAD16_U_I64:
      case WebAssembly::LOAD32_S_I64:
      case WebAssembly::LOAD32_U_I64:
      case WebAssembly::ATOMIC_LOAD_I32:
      case WebAssembly::ATOMIC_LOAD8_U_I32:
      case WebAssembly::ATOMIC_LOAD16_U_I32:
      case WebAssembly::ATOMIC_LOAD_I64:
      case WebAssembly::ATOMIC_LOAD8_U_I64:
      case WebAssembly::ATOMIC_LOAD16_U_I64:
      case WebAssembly::ATOMIC_LOAD32_U_I64:
      case WebAssembly::ATOMIC_RMW8_U_ADD_I32:
      case WebAssembly::ATOMIC_RMW8_U_ADD_I64:
      case WebAssembly::ATOMIC_RMW8_U_SUB_I32:
      case WebAssembly::ATOMIC_RMW8_U_SUB_I64:
      case WebAssembly::ATOMIC_RMW8_U_AND_I32:
      case WebAssembly::ATOMIC_RMW8_U_AND_I64:
      case WebAssembly::ATOMIC_RMW8_U_OR_I32:
      case WebAssembly::ATOMIC_RMW8_U_OR_I64:
      case WebAssembly::ATOMIC_RMW8_U_XOR_I32:
      case WebAssembly::ATOMIC_RMW8_U_XOR_I64:
      case WebAssembly::ATOMIC_RMW8_U_XCHG_I32:
      case WebAssembly::ATOMIC_RMW8_U_XCHG_I64:
      case WebAssembly::ATOMIC_RMW8_U_CMPXCHG_I32:
      case WebAssembly::ATOMIC_RMW8_U_CMPXCHG_I64:
      case WebAssembly::ATOMIC_RMW16_U_ADD_I32:
      case WebAssembly::ATOMIC_RMW16_U_ADD_I64:
      case WebAssembly::ATOMIC_RMW16_U_SUB_I32:
      case WebAssembly::ATOMIC_RMW16_U_SUB_I64:
      case WebAssembly::ATOMIC_RMW16_U_AND_I32:
      case WebAssembly::ATOMIC_RMW16_U_AND_I64:
      case WebAssembly::ATOMIC_RMW16_U_OR_I32:
      case WebAssembly::ATOMIC_RMW16_U_OR_I64:
      case WebAssembly::ATOMIC_RMW16_U_XOR_I32:
      case WebAssembly::ATOMIC_RMW16_U_XOR_I64:
      case WebAssembly::ATOMIC_RMW16_U_XCHG_I32:
      case WebAssembly::ATOMIC_RMW16_U_XCHG_I64:
      case WebAssembly::ATOMIC_RMW16_U_CMPXCHG_I32:
      case WebAssembly::ATOMIC_RMW16_U_CMPXCHG_I64:
      case WebAssembly::ATOMIC_RMW_ADD_I32:
      case WebAssembly::ATOMIC_RMW32_U_ADD_I64:
      case WebAssembly::ATOMIC_RMW_SUB_I32:
      case WebAssembly::ATOMIC_RMW32_U_SUB_I64:
      case WebAssembly::ATOMIC_RMW_AND_I32:
      case WebAssembly::ATOMIC_RMW32_U_AND_I64:
      case WebAssembly::ATOMIC_RMW_OR_I32:
      case WebAssembly::ATOMIC_RMW32_U_OR_I64:
      case WebAssembly::ATOMIC_RMW_XOR_I32:
      case WebAssembly::ATOMIC_RMW32_U_XOR_I64:
      case WebAssembly::ATOMIC_RMW_XCHG_I32:
      case WebAssembly::ATOMIC_RMW32_U_XCHG_I64:
      case WebAssembly::ATOMIC_RMW_CMPXCHG_I32:
      case WebAssembly::ATOMIC_RMW32_U_CMPXCHG_I64:
      case WebAssembly::ATOMIC_RMW_ADD_I64:
      case WebAssembly::ATOMIC_RMW_SUB_I64:
      case WebAssembly::ATOMIC_RMW_AND_I64:
      case WebAssembly::ATOMIC_RMW_OR_I64:
      case WebAssembly::ATOMIC_RMW_XOR_I64:
      case WebAssembly::ATOMIC_RMW_XCHG_I64:
      case WebAssembly::ATOMIC_RMW_CMPXCHG_I64:
      case WebAssembly::ATOMIC_NOTIFY:
      case WebAssembly::ATOMIC_WAIT_I32:
      case WebAssembly::ATOMIC_WAIT_I64:
        RewriteP2Align(MI, WebAssembly::LoadP2AlignOperandNo);
        break;
      case WebAssembly::STORE_I32:
      case WebAssembly::STORE_I64:
      case WebAssembly::STORE_F32:
      case WebAssembly::STORE_F64:
      case WebAssembly::STORE_v16i8:
      case WebAssembly::STORE_v8i16:
      case WebAssembly::STORE_v4i32:
      case WebAssembly::STORE_v2i64:
      case WebAssembly::STORE_v4f32:
      case WebAssembly::STORE_v2f64:
      case WebAssembly::STORE8_I32:
      case WebAssembly::STORE16_I32:
      case WebAssembly::STORE8_I64:
      case WebAssembly::STORE16_I64:
      case WebAssembly::STORE32_I64:
      case WebAssembly::ATOMIC_STORE_I32:
      case WebAssembly::ATOMIC_STORE8_I32:
      case WebAssembly::ATOMIC_STORE16_I32:
      case WebAssembly::ATOMIC_STORE_I64:
      case WebAssembly::ATOMIC_STORE8_I64:
      case WebAssembly::ATOMIC_STORE16_I64:
      case WebAssembly::ATOMIC_STORE32_I64:
        RewriteP2Align(MI, WebAssembly::StoreP2AlignOperandNo);
        break;
      default:
        break;
      }
    }
  }

  return Changed;
}
