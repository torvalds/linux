//===-- MachineFunctionPass.h - Pass for MachineFunctions --------*-C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the MachineFunctionPass class.  MachineFunctionPass's are
// just FunctionPass's, except they operate on machine code as part of a code
// generator.  Because they operate on machine code, not the LLVM
// representation, MachineFunctionPass's are not allowed to modify the LLVM
// representation.  Due to this limitation, the MachineFunctionPass class takes
// care of declaring that no LLVM passes are invalidated.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEFUNCTIONPASS_H
#define LLVM_CODEGEN_MACHINEFUNCTIONPASS_H

#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/Pass.h"

namespace llvm {

/// MachineFunctionPass - This class adapts the FunctionPass interface to
/// allow convenient creation of passes that operate on the MachineFunction
/// representation. Instead of overriding runOnFunction, subclasses
/// override runOnMachineFunction.
class MachineFunctionPass : public FunctionPass {
public:
  bool doInitialization(Module&) override {
    // Cache the properties info at module-init time so we don't have to
    // construct them for every function.
    RequiredProperties = getRequiredProperties();
    SetProperties = getSetProperties();
    ClearedProperties = getClearedProperties();
    return false;
  }
protected:
  explicit MachineFunctionPass(char &ID) : FunctionPass(ID) {}

  /// runOnMachineFunction - This method must be overloaded to perform the
  /// desired machine code transformation or analysis.
  ///
  virtual bool runOnMachineFunction(MachineFunction &MF) = 0;

  /// getAnalysisUsage - Subclasses that override getAnalysisUsage
  /// must call this.
  ///
  /// For MachineFunctionPasses, calling AU.preservesCFG() indicates that
  /// the pass does not modify the MachineBasicBlock CFG.
  ///
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  virtual MachineFunctionProperties getRequiredProperties() const {
    return MachineFunctionProperties();
  }
  virtual MachineFunctionProperties getSetProperties() const {
    return MachineFunctionProperties();
  }
  virtual MachineFunctionProperties getClearedProperties() const {
    return MachineFunctionProperties();
  }

private:
  MachineFunctionProperties RequiredProperties;
  MachineFunctionProperties SetProperties;
  MachineFunctionProperties ClearedProperties;

  /// createPrinterPass - Get a machine function printer pass.
  Pass *createPrinterPass(raw_ostream &O,
                          const std::string &Banner) const override;

  bool runOnFunction(Function &F) override;
};

} // End llvm namespace

#endif
