//===- llvm/CodeGen/MachineFunction.h ---------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Collect native machine code for a function.  This class contains a list of
// MachineBasicBlock instances that make up the current compiled function.
//
// This class also contains pointers to various classes which hold
// target-specific information about the generated code.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEFUNCTION_H
#define LLVM_CODEGEN_MACHINEFUNCTION_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/ilist.h"
#include "llvm/ADT/iterator.h"
#include "llvm/Analysis/EHPersonalities.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Metadata.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/ArrayRecycler.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Recycler.h"
#include <cassert>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace llvm {

class BasicBlock;
class BlockAddress;
class DataLayout;
class DIExpression;
class DILocalVariable;
class DILocation;
class Function;
class GlobalValue;
class LLVMTargetMachine;
class MachineConstantPool;
class MachineFrameInfo;
class MachineFunction;
class MachineJumpTableInfo;
class MachineModuleInfo;
class MachineRegisterInfo;
class MCContext;
class MCInstrDesc;
class Pass;
class PseudoSourceValueManager;
class raw_ostream;
class SlotIndexes;
class TargetRegisterClass;
class TargetSubtargetInfo;
struct WasmEHFuncInfo;
struct WinEHFuncInfo;

template <> struct ilist_alloc_traits<MachineBasicBlock> {
  void deleteNode(MachineBasicBlock *MBB);
};

template <> struct ilist_callback_traits<MachineBasicBlock> {
  void addNodeToList(MachineBasicBlock* N);
  void removeNodeFromList(MachineBasicBlock* N);

  template <class Iterator>
  void transferNodesFromList(ilist_callback_traits &OldList, Iterator, Iterator) {
    llvm_unreachable("Never transfer between lists");
  }
};

/// MachineFunctionInfo - This class can be derived from and used by targets to
/// hold private target-specific information for each MachineFunction.  Objects
/// of type are accessed/created with MF::getInfo and destroyed when the
/// MachineFunction is destroyed.
struct MachineFunctionInfo {
  virtual ~MachineFunctionInfo();

  /// Factory function: default behavior is to call new using the
  /// supplied allocator.
  ///
  /// This function can be overridden in a derive class.
  template<typename Ty>
  static Ty *create(BumpPtrAllocator &Allocator, MachineFunction &MF) {
    return new (Allocator.Allocate<Ty>()) Ty(MF);
  }
};

/// Properties which a MachineFunction may have at a given point in time.
/// Each of these has checking code in the MachineVerifier, and passes can
/// require that a property be set.
class MachineFunctionProperties {
  // Possible TODO: Allow targets to extend this (perhaps by allowing the
  // constructor to specify the size of the bit vector)
  // Possible TODO: Allow requiring the negative (e.g. VRegsAllocated could be
  // stated as the negative of "has vregs"

public:
  // The properties are stated in "positive" form; i.e. a pass could require
  // that the property hold, but not that it does not hold.

  // Property descriptions:
  // IsSSA: True when the machine function is in SSA form and virtual registers
  //  have a single def.
  // NoPHIs: The machine function does not contain any PHI instruction.
  // TracksLiveness: True when tracking register liveness accurately.
  //  While this property is set, register liveness information in basic block
  //  live-in lists and machine instruction operands (e.g. kill flags, implicit
  //  defs) is accurate. This means it can be used to change the code in ways
  //  that affect the values in registers, for example by the register
  //  scavenger.
  //  When this property is clear, liveness is no longer reliable.
  // NoVRegs: The machine function does not use any virtual registers.
  // Legalized: In GlobalISel: the MachineLegalizer ran and all pre-isel generic
  //  instructions have been legalized; i.e., all instructions are now one of:
  //   - generic and always legal (e.g., COPY)
  //   - target-specific
  //   - legal pre-isel generic instructions.
  // RegBankSelected: In GlobalISel: the RegBankSelect pass ran and all generic
  //  virtual registers have been assigned to a register bank.
  // Selected: In GlobalISel: the InstructionSelect pass ran and all pre-isel
  //  generic instructions have been eliminated; i.e., all instructions are now
  //  target-specific or non-pre-isel generic instructions (e.g., COPY).
  //  Since only pre-isel generic instructions can have generic virtual register
  //  operands, this also means that all generic virtual registers have been
  //  constrained to virtual registers (assigned to register classes) and that
  //  all sizes attached to them have been eliminated.
  enum class Property : unsigned {
    IsSSA,
    NoPHIs,
    TracksLiveness,
    NoVRegs,
    FailedISel,
    Legalized,
    RegBankSelected,
    Selected,
    LastProperty = Selected,
  };

  bool hasProperty(Property P) const {
    return Properties[static_cast<unsigned>(P)];
  }

  MachineFunctionProperties &set(Property P) {
    Properties.set(static_cast<unsigned>(P));
    return *this;
  }

  MachineFunctionProperties &reset(Property P) {
    Properties.reset(static_cast<unsigned>(P));
    return *this;
  }

  /// Reset all the properties.
  MachineFunctionProperties &reset() {
    Properties.reset();
    return *this;
  }

  MachineFunctionProperties &set(const MachineFunctionProperties &MFP) {
    Properties |= MFP.Properties;
    return *this;
  }

  MachineFunctionProperties &reset(const MachineFunctionProperties &MFP) {
    Properties.reset(MFP.Properties);
    return *this;
  }

  // Returns true if all properties set in V (i.e. required by a pass) are set
  // in this.
  bool verifyRequiredProperties(const MachineFunctionProperties &V) const {
    return !V.Properties.test(Properties);
  }

  /// Print the MachineFunctionProperties in human-readable form.
  void print(raw_ostream &OS) const;

private:
  BitVector Properties =
      BitVector(static_cast<unsigned>(Property::LastProperty)+1);
};

struct SEHHandler {
  /// Filter or finally function. Null indicates a catch-all.
  const Function *FilterOrFinally;

  /// Address of block to recover at. Null for a finally handler.
  const BlockAddress *RecoverBA;
};

/// This structure is used to retain landing pad info for the current function.
struct LandingPadInfo {
  MachineBasicBlock *LandingPadBlock;      // Landing pad block.
  SmallVector<MCSymbol *, 1> BeginLabels;  // Labels prior to invoke.
  SmallVector<MCSymbol *, 1> EndLabels;    // Labels after invoke.
  SmallVector<SEHHandler, 1> SEHHandlers;  // SEH handlers active at this lpad.
  MCSymbol *LandingPadLabel = nullptr;     // Label at beginning of landing pad.
  std::vector<int> TypeIds;                // List of type ids (filters negative).

  explicit LandingPadInfo(MachineBasicBlock *MBB)
      : LandingPadBlock(MBB) {}
};

class MachineFunction {
  const Function &F;
  const LLVMTargetMachine &Target;
  const TargetSubtargetInfo *STI;
  MCContext &Ctx;
  MachineModuleInfo &MMI;

  // RegInfo - Information about each register in use in the function.
  MachineRegisterInfo *RegInfo;

  // Used to keep track of target-specific per-machine function information for
  // the target implementation.
  MachineFunctionInfo *MFInfo;

  // Keep track of objects allocated on the stack.
  MachineFrameInfo *FrameInfo;

  // Keep track of constants which are spilled to memory
  MachineConstantPool *ConstantPool;

  // Keep track of jump tables for switch instructions
  MachineJumpTableInfo *JumpTableInfo;

  // Keeps track of Wasm exception handling related data. This will be null for
  // functions that aren't using a wasm EH personality.
  WasmEHFuncInfo *WasmEHInfo = nullptr;

  // Keeps track of Windows exception handling related data. This will be null
  // for functions that aren't using a funclet-based EH personality.
  WinEHFuncInfo *WinEHInfo = nullptr;

  // Function-level unique numbering for MachineBasicBlocks.  When a
  // MachineBasicBlock is inserted into a MachineFunction is it automatically
  // numbered and this vector keeps track of the mapping from ID's to MBB's.
  std::vector<MachineBasicBlock*> MBBNumbering;

  // Pool-allocate MachineFunction-lifetime and IR objects.
  BumpPtrAllocator Allocator;

  // Allocation management for instructions in function.
  Recycler<MachineInstr> InstructionRecycler;

  // Allocation management for operand arrays on instructions.
  ArrayRecycler<MachineOperand> OperandRecycler;

  // Allocation management for basic blocks in function.
  Recycler<MachineBasicBlock> BasicBlockRecycler;

  // List of machine basic blocks in function
  using BasicBlockListType = ilist<MachineBasicBlock>;
  BasicBlockListType BasicBlocks;

  /// FunctionNumber - This provides a unique ID for each function emitted in
  /// this translation unit.
  ///
  unsigned FunctionNumber;

  /// Alignment - The alignment of the function.
  unsigned Alignment;

  /// ExposesReturnsTwice - True if the function calls setjmp or related
  /// functions with attribute "returns twice", but doesn't have
  /// the attribute itself.
  /// This is used to limit optimizations which cannot reason
  /// about the control flow of such functions.
  bool ExposesReturnsTwice = false;

  /// True if the function includes any inline assembly.
  bool HasInlineAsm = false;

  /// True if any WinCFI instruction have been emitted in this function.
  bool HasWinCFI = false;

  /// Current high-level properties of the IR of the function (e.g. is in SSA
  /// form or whether registers have been allocated)
  MachineFunctionProperties Properties;

  // Allocation management for pseudo source values.
  std::unique_ptr<PseudoSourceValueManager> PSVManager;

  /// List of moves done by a function's prolog.  Used to construct frame maps
  /// by debug and exception handling consumers.
  std::vector<MCCFIInstruction> FrameInstructions;

  /// \name Exception Handling
  /// \{

  /// List of LandingPadInfo describing the landing pad information.
  std::vector<LandingPadInfo> LandingPads;

  /// Map a landing pad's EH symbol to the call site indexes.
  DenseMap<MCSymbol*, SmallVector<unsigned, 4>> LPadToCallSiteMap;

  /// Map a landing pad to its index.
  DenseMap<const MachineBasicBlock *, unsigned> WasmLPadToIndexMap;

  /// Map of invoke call site index values to associated begin EH_LABEL.
  DenseMap<MCSymbol*, unsigned> CallSiteMap;

  /// CodeView label annotations.
  std::vector<std::pair<MCSymbol *, MDNode *>> CodeViewAnnotations;

  bool CallsEHReturn = false;
  bool CallsUnwindInit = false;
  bool HasEHScopes = false;
  bool HasEHFunclets = false;
  bool HasLocalEscape = false;

  /// List of C++ TypeInfo used.
  std::vector<const GlobalValue *> TypeInfos;

  /// List of typeids encoding filters used.
  std::vector<unsigned> FilterIds;

  /// List of the indices in FilterIds corresponding to filter terminators.
  std::vector<unsigned> FilterEnds;

  EHPersonality PersonalityTypeCache = EHPersonality::Unknown;

  /// \}

  /// Clear all the members of this MachineFunction, but the ones used
  /// to initialize again the MachineFunction.
  /// More specifically, this deallocates all the dynamically allocated
  /// objects and get rid of all the XXXInfo data structure, but keep
  /// unchanged the references to Fn, Target, MMI, and FunctionNumber.
  void clear();
  /// Allocate and initialize the different members.
  /// In particular, the XXXInfo data structure.
  /// \pre Fn, Target, MMI, and FunctionNumber are properly set.
  void init();

public:
  struct VariableDbgInfo {
    const DILocalVariable *Var;
    const DIExpression *Expr;
    // The Slot can be negative for fixed stack objects.
    int Slot;
    const DILocation *Loc;

    VariableDbgInfo(const DILocalVariable *Var, const DIExpression *Expr,
                    int Slot, const DILocation *Loc)
        : Var(Var), Expr(Expr), Slot(Slot), Loc(Loc) {}
  };

  class Delegate {
    virtual void anchor();

  public:
    virtual ~Delegate() = default;
    /// Callback after an insertion. This should not modify the MI directly.
    virtual void MF_HandleInsertion(MachineInstr &MI) = 0;
    /// Callback before a removal. This should not modify the MI directly.
    virtual void MF_HandleRemoval(MachineInstr &MI) = 0;
  };

private:
  Delegate *TheDelegate = nullptr;

  // Callbacks for insertion and removal.
  void handleInsertion(MachineInstr &MI);
  void handleRemoval(MachineInstr &MI);
  friend struct ilist_traits<MachineInstr>;

public:
  using VariableDbgInfoMapTy = SmallVector<VariableDbgInfo, 4>;
  VariableDbgInfoMapTy VariableDbgInfos;

  MachineFunction(const Function &F, const LLVMTargetMachine &Target,
                  const TargetSubtargetInfo &STI, unsigned FunctionNum,
                  MachineModuleInfo &MMI);
  MachineFunction(const MachineFunction &) = delete;
  MachineFunction &operator=(const MachineFunction &) = delete;
  ~MachineFunction();

  /// Reset the instance as if it was just created.
  void reset() {
    clear();
    init();
  }

  /// Reset the currently registered delegate - otherwise assert.
  void resetDelegate(Delegate *delegate) {
    assert(TheDelegate == delegate &&
           "Only the current delegate can perform reset!");
    TheDelegate = nullptr;
  }

  /// Set the delegate. resetDelegate must be called before attempting
  /// to set.
  void setDelegate(Delegate *delegate) {
    assert(delegate && !TheDelegate &&
           "Attempted to set delegate to null, or to change it without "
           "first resetting it!");

    TheDelegate = delegate;
  }

  MachineModuleInfo &getMMI() const { return MMI; }
  MCContext &getContext() const { return Ctx; }

  PseudoSourceValueManager &getPSVManager() const { return *PSVManager; }

  /// Return the DataLayout attached to the Module associated to this MF.
  const DataLayout &getDataLayout() const;

  /// Return the LLVM function that this machine code represents
  const Function &getFunction() const { return F; }

  /// getName - Return the name of the corresponding LLVM function.
  StringRef getName() const;

  /// getFunctionNumber - Return a unique ID for the current function.
  unsigned getFunctionNumber() const { return FunctionNumber; }

  /// getTarget - Return the target machine this machine code is compiled with
  const LLVMTargetMachine &getTarget() const { return Target; }

  /// getSubtarget - Return the subtarget for which this machine code is being
  /// compiled.
  const TargetSubtargetInfo &getSubtarget() const { return *STI; }
  void setSubtarget(const TargetSubtargetInfo *ST) { STI = ST; }

  /// getSubtarget - This method returns a pointer to the specified type of
  /// TargetSubtargetInfo.  In debug builds, it verifies that the object being
  /// returned is of the correct type.
  template<typename STC> const STC &getSubtarget() const {
    return *static_cast<const STC *>(STI);
  }

  /// getRegInfo - Return information about the registers currently in use.
  MachineRegisterInfo &getRegInfo() { return *RegInfo; }
  const MachineRegisterInfo &getRegInfo() const { return *RegInfo; }

  /// getFrameInfo - Return the frame info object for the current function.
  /// This object contains information about objects allocated on the stack
  /// frame of the current function in an abstract way.
  MachineFrameInfo &getFrameInfo() { return *FrameInfo; }
  const MachineFrameInfo &getFrameInfo() const { return *FrameInfo; }

  /// getJumpTableInfo - Return the jump table info object for the current
  /// function.  This object contains information about jump tables in the
  /// current function.  If the current function has no jump tables, this will
  /// return null.
  const MachineJumpTableInfo *getJumpTableInfo() const { return JumpTableInfo; }
  MachineJumpTableInfo *getJumpTableInfo() { return JumpTableInfo; }

  /// getOrCreateJumpTableInfo - Get the JumpTableInfo for this function, if it
  /// does already exist, allocate one.
  MachineJumpTableInfo *getOrCreateJumpTableInfo(unsigned JTEntryKind);

  /// getConstantPool - Return the constant pool object for the current
  /// function.
  MachineConstantPool *getConstantPool() { return ConstantPool; }
  const MachineConstantPool *getConstantPool() const { return ConstantPool; }

  /// getWasmEHFuncInfo - Return information about how the current function uses
  /// Wasm exception handling. Returns null for functions that don't use wasm
  /// exception handling.
  const WasmEHFuncInfo *getWasmEHFuncInfo() const { return WasmEHInfo; }
  WasmEHFuncInfo *getWasmEHFuncInfo() { return WasmEHInfo; }

  /// getWinEHFuncInfo - Return information about how the current function uses
  /// Windows exception handling. Returns null for functions that don't use
  /// funclets for exception handling.
  const WinEHFuncInfo *getWinEHFuncInfo() const { return WinEHInfo; }
  WinEHFuncInfo *getWinEHFuncInfo() { return WinEHInfo; }

  /// getAlignment - Return the alignment (log2, not bytes) of the function.
  unsigned getAlignment() const { return Alignment; }

  /// setAlignment - Set the alignment (log2, not bytes) of the function.
  void setAlignment(unsigned A) { Alignment = A; }

  /// ensureAlignment - Make sure the function is at least 1 << A bytes aligned.
  void ensureAlignment(unsigned A) {
    if (Alignment < A) Alignment = A;
  }

  /// exposesReturnsTwice - Returns true if the function calls setjmp or
  /// any other similar functions with attribute "returns twice" without
  /// having the attribute itself.
  bool exposesReturnsTwice() const {
    return ExposesReturnsTwice;
  }

  /// setCallsSetJmp - Set a flag that indicates if there's a call to
  /// a "returns twice" function.
  void setExposesReturnsTwice(bool B) {
    ExposesReturnsTwice = B;
  }

  /// Returns true if the function contains any inline assembly.
  bool hasInlineAsm() const {
    return HasInlineAsm;
  }

  /// Set a flag that indicates that the function contains inline assembly.
  void setHasInlineAsm(bool B) {
    HasInlineAsm = B;
  }

  bool hasWinCFI() const {
    return HasWinCFI;
  }
  void setHasWinCFI(bool v) { HasWinCFI = v; }

  /// Get the function properties
  const MachineFunctionProperties &getProperties() const { return Properties; }
  MachineFunctionProperties &getProperties() { return Properties; }

  /// getInfo - Keep track of various per-function pieces of information for
  /// backends that would like to do so.
  ///
  template<typename Ty>
  Ty *getInfo() {
    if (!MFInfo)
      MFInfo = Ty::template create<Ty>(Allocator, *this);
    return static_cast<Ty*>(MFInfo);
  }

  template<typename Ty>
  const Ty *getInfo() const {
     return const_cast<MachineFunction*>(this)->getInfo<Ty>();
  }

  /// getBlockNumbered - MachineBasicBlocks are automatically numbered when they
  /// are inserted into the machine function.  The block number for a machine
  /// basic block can be found by using the MBB::getNumber method, this method
  /// provides the inverse mapping.
  MachineBasicBlock *getBlockNumbered(unsigned N) const {
    assert(N < MBBNumbering.size() && "Illegal block number");
    assert(MBBNumbering[N] && "Block was removed from the machine function!");
    return MBBNumbering[N];
  }

  /// Should we be emitting segmented stack stuff for the function
  bool shouldSplitStack() const;

  /// getNumBlockIDs - Return the number of MBB ID's allocated.
  unsigned getNumBlockIDs() const { return (unsigned)MBBNumbering.size(); }

  /// RenumberBlocks - This discards all of the MachineBasicBlock numbers and
  /// recomputes them.  This guarantees that the MBB numbers are sequential,
  /// dense, and match the ordering of the blocks within the function.  If a
  /// specific MachineBasicBlock is specified, only that block and those after
  /// it are renumbered.
  void RenumberBlocks(MachineBasicBlock *MBBFrom = nullptr);

  /// print - Print out the MachineFunction in a format suitable for debugging
  /// to the specified stream.
  void print(raw_ostream &OS, const SlotIndexes* = nullptr) const;

  /// viewCFG - This function is meant for use from the debugger.  You can just
  /// say 'call F->viewCFG()' and a ghostview window should pop up from the
  /// program, displaying the CFG of the current function with the code for each
  /// basic block inside.  This depends on there being a 'dot' and 'gv' program
  /// in your path.
  void viewCFG() const;

  /// viewCFGOnly - This function is meant for use from the debugger.  It works
  /// just like viewCFG, but it does not include the contents of basic blocks
  /// into the nodes, just the label.  If you are only interested in the CFG
  /// this can make the graph smaller.
  ///
  void viewCFGOnly() const;

  /// dump - Print the current MachineFunction to cerr, useful for debugger use.
  void dump() const;

  /// Run the current MachineFunction through the machine code verifier, useful
  /// for debugger use.
  /// \returns true if no problems were found.
  bool verify(Pass *p = nullptr, const char *Banner = nullptr,
              bool AbortOnError = true) const;

  // Provide accessors for the MachineBasicBlock list...
  using iterator = BasicBlockListType::iterator;
  using const_iterator = BasicBlockListType::const_iterator;
  using const_reverse_iterator = BasicBlockListType::const_reverse_iterator;
  using reverse_iterator = BasicBlockListType::reverse_iterator;

  /// Support for MachineBasicBlock::getNextNode().
  static BasicBlockListType MachineFunction::*
  getSublistAccess(MachineBasicBlock *) {
    return &MachineFunction::BasicBlocks;
  }

  /// addLiveIn - Add the specified physical register as a live-in value and
  /// create a corresponding virtual register for it.
  unsigned addLiveIn(unsigned PReg, const TargetRegisterClass *RC);

  //===--------------------------------------------------------------------===//
  // BasicBlock accessor functions.
  //
  iterator                 begin()       { return BasicBlocks.begin(); }
  const_iterator           begin() const { return BasicBlocks.begin(); }
  iterator                 end  ()       { return BasicBlocks.end();   }
  const_iterator           end  () const { return BasicBlocks.end();   }

  reverse_iterator        rbegin()       { return BasicBlocks.rbegin(); }
  const_reverse_iterator  rbegin() const { return BasicBlocks.rbegin(); }
  reverse_iterator        rend  ()       { return BasicBlocks.rend();   }
  const_reverse_iterator  rend  () const { return BasicBlocks.rend();   }

  unsigned                  size() const { return (unsigned)BasicBlocks.size();}
  bool                     empty() const { return BasicBlocks.empty(); }
  const MachineBasicBlock &front() const { return BasicBlocks.front(); }
        MachineBasicBlock &front()       { return BasicBlocks.front(); }
  const MachineBasicBlock & back() const { return BasicBlocks.back(); }
        MachineBasicBlock & back()       { return BasicBlocks.back(); }

  void push_back (MachineBasicBlock *MBB) { BasicBlocks.push_back (MBB); }
  void push_front(MachineBasicBlock *MBB) { BasicBlocks.push_front(MBB); }
  void insert(iterator MBBI, MachineBasicBlock *MBB) {
    BasicBlocks.insert(MBBI, MBB);
  }
  void splice(iterator InsertPt, iterator MBBI) {
    BasicBlocks.splice(InsertPt, BasicBlocks, MBBI);
  }
  void splice(iterator InsertPt, MachineBasicBlock *MBB) {
    BasicBlocks.splice(InsertPt, BasicBlocks, MBB);
  }
  void splice(iterator InsertPt, iterator MBBI, iterator MBBE) {
    BasicBlocks.splice(InsertPt, BasicBlocks, MBBI, MBBE);
  }

  void remove(iterator MBBI) { BasicBlocks.remove(MBBI); }
  void remove(MachineBasicBlock *MBBI) { BasicBlocks.remove(MBBI); }
  void erase(iterator MBBI) { BasicBlocks.erase(MBBI); }
  void erase(MachineBasicBlock *MBBI) { BasicBlocks.erase(MBBI); }

  template <typename Comp>
  void sort(Comp comp) {
    BasicBlocks.sort(comp);
  }

  /// Return the number of \p MachineInstrs in this \p MachineFunction.
  unsigned getInstructionCount() const {
    unsigned InstrCount = 0;
    for (const MachineBasicBlock &MBB : BasicBlocks)
      InstrCount += MBB.size();
    return InstrCount;
  }

  //===--------------------------------------------------------------------===//
  // Internal functions used to automatically number MachineBasicBlocks

  /// Adds the MBB to the internal numbering. Returns the unique number
  /// assigned to the MBB.
  unsigned addToMBBNumbering(MachineBasicBlock *MBB) {
    MBBNumbering.push_back(MBB);
    return (unsigned)MBBNumbering.size()-1;
  }

  /// removeFromMBBNumbering - Remove the specific machine basic block from our
  /// tracker, this is only really to be used by the MachineBasicBlock
  /// implementation.
  void removeFromMBBNumbering(unsigned N) {
    assert(N < MBBNumbering.size() && "Illegal basic block #");
    MBBNumbering[N] = nullptr;
  }

  /// CreateMachineInstr - Allocate a new MachineInstr. Use this instead
  /// of `new MachineInstr'.
  MachineInstr *CreateMachineInstr(const MCInstrDesc &MCID, const DebugLoc &DL,
                                   bool NoImp = false);

  /// Create a new MachineInstr which is a copy of \p Orig, identical in all
  /// ways except the instruction has no parent, prev, or next. Bundling flags
  /// are reset.
  ///
  /// Note: Clones a single instruction, not whole instruction bundles.
  /// Does not perform target specific adjustments; consider using
  /// TargetInstrInfo::duplicate() instead.
  MachineInstr *CloneMachineInstr(const MachineInstr *Orig);

  /// Clones instruction or the whole instruction bundle \p Orig and insert
  /// into \p MBB before \p InsertBefore.
  ///
  /// Note: Does not perform target specific adjustments; consider using
  /// TargetInstrInfo::duplicate() intead.
  MachineInstr &CloneMachineInstrBundle(MachineBasicBlock &MBB,
      MachineBasicBlock::iterator InsertBefore, const MachineInstr &Orig);

  /// DeleteMachineInstr - Delete the given MachineInstr.
  void DeleteMachineInstr(MachineInstr *MI);

  /// CreateMachineBasicBlock - Allocate a new MachineBasicBlock. Use this
  /// instead of `new MachineBasicBlock'.
  MachineBasicBlock *CreateMachineBasicBlock(const BasicBlock *bb = nullptr);

  /// DeleteMachineBasicBlock - Delete the given MachineBasicBlock.
  void DeleteMachineBasicBlock(MachineBasicBlock *MBB);

  /// getMachineMemOperand - Allocate a new MachineMemOperand.
  /// MachineMemOperands are owned by the MachineFunction and need not be
  /// explicitly deallocated.
  MachineMemOperand *getMachineMemOperand(
      MachinePointerInfo PtrInfo, MachineMemOperand::Flags f, uint64_t s,
      unsigned base_alignment, const AAMDNodes &AAInfo = AAMDNodes(),
      const MDNode *Ranges = nullptr,
      SyncScope::ID SSID = SyncScope::System,
      AtomicOrdering Ordering = AtomicOrdering::NotAtomic,
      AtomicOrdering FailureOrdering = AtomicOrdering::NotAtomic);

  /// getMachineMemOperand - Allocate a new MachineMemOperand by copying
  /// an existing one, adjusting by an offset and using the given size.
  /// MachineMemOperands are owned by the MachineFunction and need not be
  /// explicitly deallocated.
  MachineMemOperand *getMachineMemOperand(const MachineMemOperand *MMO,
                                          int64_t Offset, uint64_t Size);

  /// Allocate a new MachineMemOperand by copying an existing one,
  /// replacing only AliasAnalysis information. MachineMemOperands are owned
  /// by the MachineFunction and need not be explicitly deallocated.
  MachineMemOperand *getMachineMemOperand(const MachineMemOperand *MMO,
                                          const AAMDNodes &AAInfo);

  using OperandCapacity = ArrayRecycler<MachineOperand>::Capacity;

  /// Allocate an array of MachineOperands. This is only intended for use by
  /// internal MachineInstr functions.
  MachineOperand *allocateOperandArray(OperandCapacity Cap) {
    return OperandRecycler.allocate(Cap, Allocator);
  }

  /// Dellocate an array of MachineOperands and recycle the memory. This is
  /// only intended for use by internal MachineInstr functions.
  /// Cap must be the same capacity that was used to allocate the array.
  void deallocateOperandArray(OperandCapacity Cap, MachineOperand *Array) {
    OperandRecycler.deallocate(Cap, Array);
  }

  /// Allocate and initialize a register mask with @p NumRegister bits.
  uint32_t *allocateRegMask();

  /// Allocate and construct an extra info structure for a `MachineInstr`.
  ///
  /// This is allocated on the function's allocator and so lives the life of
  /// the function.
  MachineInstr::ExtraInfo *
  createMIExtraInfo(ArrayRef<MachineMemOperand *> MMOs,
                    MCSymbol *PreInstrSymbol = nullptr,
                    MCSymbol *PostInstrSymbol = nullptr);

  /// Allocate a string and populate it with the given external symbol name.
  const char *createExternalSymbolName(StringRef Name);

  //===--------------------------------------------------------------------===//
  // Label Manipulation.

  /// getJTISymbol - Return the MCSymbol for the specified non-empty jump table.
  /// If isLinkerPrivate is specified, an 'l' label is returned, otherwise a
  /// normal 'L' label is returned.
  MCSymbol *getJTISymbol(unsigned JTI, MCContext &Ctx,
                         bool isLinkerPrivate = false) const;

  /// getPICBaseSymbol - Return a function-local symbol to represent the PIC
  /// base.
  MCSymbol *getPICBaseSymbol() const;

  /// Returns a reference to a list of cfi instructions in the function's
  /// prologue.  Used to construct frame maps for debug and exception handling
  /// comsumers.
  const std::vector<MCCFIInstruction> &getFrameInstructions() const {
    return FrameInstructions;
  }

  LLVM_NODISCARD unsigned addFrameInst(const MCCFIInstruction &Inst) {
    FrameInstructions.push_back(Inst);
    return FrameInstructions.size() - 1;
  }

  /// \name Exception Handling
  /// \{

  bool callsEHReturn() const { return CallsEHReturn; }
  void setCallsEHReturn(bool b) { CallsEHReturn = b; }

  bool callsUnwindInit() const { return CallsUnwindInit; }
  void setCallsUnwindInit(bool b) { CallsUnwindInit = b; }

  bool hasEHScopes() const { return HasEHScopes; }
  void setHasEHScopes(bool V) { HasEHScopes = V; }

  bool hasEHFunclets() const { return HasEHFunclets; }
  void setHasEHFunclets(bool V) { HasEHFunclets = V; }

  bool hasLocalEscape() const { return HasLocalEscape; }
  void setHasLocalEscape(bool V) { HasLocalEscape = V; }

  /// Find or create an LandingPadInfo for the specified MachineBasicBlock.
  LandingPadInfo &getOrCreateLandingPadInfo(MachineBasicBlock *LandingPad);

  /// Remap landing pad labels and remove any deleted landing pads.
  void tidyLandingPads(DenseMap<MCSymbol *, uintptr_t> *LPMap = nullptr,
                       bool TidyIfNoBeginLabels = true);

  /// Return a reference to the landing pad info for the current function.
  const std::vector<LandingPadInfo> &getLandingPads() const {
    return LandingPads;
  }

  /// Provide the begin and end labels of an invoke style call and associate it
  /// with a try landing pad block.
  void addInvoke(MachineBasicBlock *LandingPad,
                 MCSymbol *BeginLabel, MCSymbol *EndLabel);

  /// Add a new panding pad, and extract the exception handling information from
  /// the landingpad instruction. Returns the label ID for the landing pad
  /// entry.
  MCSymbol *addLandingPad(MachineBasicBlock *LandingPad);

  /// Provide the catch typeinfo for a landing pad.
  void addCatchTypeInfo(MachineBasicBlock *LandingPad,
                        ArrayRef<const GlobalValue *> TyInfo);

  /// Provide the filter typeinfo for a landing pad.
  void addFilterTypeInfo(MachineBasicBlock *LandingPad,
                         ArrayRef<const GlobalValue *> TyInfo);

  /// Add a cleanup action for a landing pad.
  void addCleanup(MachineBasicBlock *LandingPad);

  void addSEHCatchHandler(MachineBasicBlock *LandingPad, const Function *Filter,
                          const BlockAddress *RecoverBA);

  void addSEHCleanupHandler(MachineBasicBlock *LandingPad,
                            const Function *Cleanup);

  /// Return the type id for the specified typeinfo.  This is function wide.
  unsigned getTypeIDFor(const GlobalValue *TI);

  /// Return the id of the filter encoded by TyIds.  This is function wide.
  int getFilterIDFor(std::vector<unsigned> &TyIds);

  /// Map the landing pad's EH symbol to the call site indexes.
  void setCallSiteLandingPad(MCSymbol *Sym, ArrayRef<unsigned> Sites);

  /// Map the landing pad to its index. Used for Wasm exception handling.
  void setWasmLandingPadIndex(const MachineBasicBlock *LPad, unsigned Index) {
    WasmLPadToIndexMap[LPad] = Index;
  }

  /// Returns true if the landing pad has an associate index in wasm EH.
  bool hasWasmLandingPadIndex(const MachineBasicBlock *LPad) const {
    return WasmLPadToIndexMap.count(LPad);
  }

  /// Get the index in wasm EH for a given landing pad.
  unsigned getWasmLandingPadIndex(const MachineBasicBlock *LPad) const {
    assert(hasWasmLandingPadIndex(LPad));
    return WasmLPadToIndexMap.lookup(LPad);
  }

  /// Get the call site indexes for a landing pad EH symbol.
  SmallVectorImpl<unsigned> &getCallSiteLandingPad(MCSymbol *Sym) {
    assert(hasCallSiteLandingPad(Sym) &&
           "missing call site number for landing pad!");
    return LPadToCallSiteMap[Sym];
  }

  /// Return true if the landing pad Eh symbol has an associated call site.
  bool hasCallSiteLandingPad(MCSymbol *Sym) {
    return !LPadToCallSiteMap[Sym].empty();
  }

  /// Map the begin label for a call site.
  void setCallSiteBeginLabel(MCSymbol *BeginLabel, unsigned Site) {
    CallSiteMap[BeginLabel] = Site;
  }

  /// Get the call site number for a begin label.
  unsigned getCallSiteBeginLabel(MCSymbol *BeginLabel) const {
    assert(hasCallSiteBeginLabel(BeginLabel) &&
           "Missing call site number for EH_LABEL!");
    return CallSiteMap.lookup(BeginLabel);
  }

  /// Return true if the begin label has a call site number associated with it.
  bool hasCallSiteBeginLabel(MCSymbol *BeginLabel) const {
    return CallSiteMap.count(BeginLabel);
  }

  /// Record annotations associated with a particular label.
  void addCodeViewAnnotation(MCSymbol *Label, MDNode *MD) {
    CodeViewAnnotations.push_back({Label, MD});
  }

  ArrayRef<std::pair<MCSymbol *, MDNode *>> getCodeViewAnnotations() const {
    return CodeViewAnnotations;
  }

  /// Return a reference to the C++ typeinfo for the current function.
  const std::vector<const GlobalValue *> &getTypeInfos() const {
    return TypeInfos;
  }

  /// Return a reference to the typeids encoding filters used in the current
  /// function.
  const std::vector<unsigned> &getFilterIds() const {
    return FilterIds;
  }

  /// \}

  /// Collect information used to emit debugging information of a variable.
  void setVariableDbgInfo(const DILocalVariable *Var, const DIExpression *Expr,
                          int Slot, const DILocation *Loc) {
    VariableDbgInfos.emplace_back(Var, Expr, Slot, Loc);
  }

  VariableDbgInfoMapTy &getVariableDbgInfo() { return VariableDbgInfos; }
  const VariableDbgInfoMapTy &getVariableDbgInfo() const {
    return VariableDbgInfos;
  }
};

//===--------------------------------------------------------------------===//
// GraphTraits specializations for function basic block graphs (CFGs)
//===--------------------------------------------------------------------===//

// Provide specializations of GraphTraits to be able to treat a
// machine function as a graph of machine basic blocks... these are
// the same as the machine basic block iterators, except that the root
// node is implicitly the first node of the function.
//
template <> struct GraphTraits<MachineFunction*> :
  public GraphTraits<MachineBasicBlock*> {
  static NodeRef getEntryNode(MachineFunction *F) { return &F->front(); }

  // nodes_iterator/begin/end - Allow iteration over all nodes in the graph
  using nodes_iterator = pointer_iterator<MachineFunction::iterator>;

  static nodes_iterator nodes_begin(MachineFunction *F) {
    return nodes_iterator(F->begin());
  }

  static nodes_iterator nodes_end(MachineFunction *F) {
    return nodes_iterator(F->end());
  }

  static unsigned       size       (MachineFunction *F) { return F->size(); }
};
template <> struct GraphTraits<const MachineFunction*> :
  public GraphTraits<const MachineBasicBlock*> {
  static NodeRef getEntryNode(const MachineFunction *F) { return &F->front(); }

  // nodes_iterator/begin/end - Allow iteration over all nodes in the graph
  using nodes_iterator = pointer_iterator<MachineFunction::const_iterator>;

  static nodes_iterator nodes_begin(const MachineFunction *F) {
    return nodes_iterator(F->begin());
  }

  static nodes_iterator nodes_end  (const MachineFunction *F) {
    return nodes_iterator(F->end());
  }

  static unsigned       size       (const MachineFunction *F)  {
    return F->size();
  }
};

// Provide specializations of GraphTraits to be able to treat a function as a
// graph of basic blocks... and to walk it in inverse order.  Inverse order for
// a function is considered to be when traversing the predecessor edges of a BB
// instead of the successor edges.
//
template <> struct GraphTraits<Inverse<MachineFunction*>> :
  public GraphTraits<Inverse<MachineBasicBlock*>> {
  static NodeRef getEntryNode(Inverse<MachineFunction *> G) {
    return &G.Graph->front();
  }
};
template <> struct GraphTraits<Inverse<const MachineFunction*>> :
  public GraphTraits<Inverse<const MachineBasicBlock*>> {
  static NodeRef getEntryNode(Inverse<const MachineFunction *> G) {
    return &G.Graph->front();
  }
};

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINEFUNCTION_H
