//===----- llvm/CodeGen/GlobalISel/GISelChangeObserver.h ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// This contains common code to allow clients to notify changes to machine
/// instr.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CODEGEN_GLOBALISEL_GISELCHANGEOBSERVER_H
#define LLVM_CODEGEN_GLOBALISEL_GISELCHANGEOBSERVER_H

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/CodeGen/MachineFunction.h"

namespace llvm {
class MachineInstr;
class MachineRegisterInfo;

/// Abstract class that contains various methods for clients to notify about
/// changes. This should be the preferred way for APIs to notify changes.
/// Typically calling erasingInstr/createdInstr multiple times should not affect
/// the result. The observer would likely need to check if it was already
/// notified earlier (consider using GISelWorkList).
class GISelChangeObserver {
  SmallPtrSet<MachineInstr *, 4> ChangingAllUsesOfReg;

public:
  virtual ~GISelChangeObserver() {}

  /// An instruction is about to be erased.
  virtual void erasingInstr(MachineInstr &MI) = 0;
  /// An instruction was created and inserted into the function.
  virtual void createdInstr(MachineInstr &MI) = 0;
  /// This instruction is about to be mutated in some way.
  virtual void changingInstr(MachineInstr &MI) = 0;
  /// This instruction was mutated in some way.
  virtual void changedInstr(MachineInstr &MI) = 0;

  /// All the instructions using the given register are being changed.
  /// For convenience, finishedChangingAllUsesOfReg() will report the completion
  /// of the changes. The use list may change between this call and
  /// finishedChangingAllUsesOfReg().
  void changingAllUsesOfReg(const MachineRegisterInfo &MRI, unsigned Reg);
  /// All instructions reported as changing by changingAllUsesOfReg() have
  /// finished being changed.
  void finishedChangingAllUsesOfReg();

};

/// Simple wrapper observer that takes several observers, and calls
/// each one for each event. If there are multiple observers (say CSE,
/// Legalizer, Combiner), it's sufficient to register this to the machine
/// function as the delegate.
class GISelObserverWrapper : public MachineFunction::Delegate,
                             public GISelChangeObserver {
  SmallVector<GISelChangeObserver *, 4> Observers;

public:
  GISelObserverWrapper() = default;
  GISelObserverWrapper(ArrayRef<GISelChangeObserver *> Obs)
      : Observers(Obs.begin(), Obs.end()) {}
  // Adds an observer.
  void addObserver(GISelChangeObserver *O) { Observers.push_back(O); }
  // Removes an observer from the list and does nothing if observer is not
  // present.
  void removeObserver(GISelChangeObserver *O) {
    auto It = std::find(Observers.begin(), Observers.end(), O);
    if (It != Observers.end())
      Observers.erase(It);
  }
  // API for Observer.
  void erasingInstr(MachineInstr &MI) override {
    for (auto &O : Observers)
      O->erasingInstr(MI);
  }
  void createdInstr(MachineInstr &MI) override {
    for (auto &O : Observers)
      O->createdInstr(MI);
  }
  void changingInstr(MachineInstr &MI) override {
    for (auto &O : Observers)
      O->changingInstr(MI);
  }
  void changedInstr(MachineInstr &MI) override {
    for (auto &O : Observers)
      O->changedInstr(MI);
  }
  // API for MachineFunction::Delegate
  void MF_HandleInsertion(MachineInstr &MI) override { createdInstr(MI); }
  void MF_HandleRemoval(MachineInstr &MI) override { erasingInstr(MI); }
};

/// A simple RAII based CSEInfo installer.
/// Use this in a scope to install a delegate to the MachineFunction and reset
/// it at the end of the scope.
class RAIIDelegateInstaller {
  MachineFunction &MF;
  MachineFunction::Delegate *Delegate;

public:
  RAIIDelegateInstaller(MachineFunction &MF, MachineFunction::Delegate *Del);
  ~RAIIDelegateInstaller();
};

} // namespace llvm
#endif
