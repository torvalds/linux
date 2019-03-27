//===- LexicalScopes.cpp - Collecting lexical scope info --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements LexicalScopes analysis.
//
// This pass collects lexical scope information and maps machine instructions
// to respective lexical scopes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_LEXICALSCOPES_H
#define LLVM_CODEGEN_LEXICALSCOPES_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include <cassert>
#include <unordered_map>
#include <utility>

namespace llvm {

class MachineBasicBlock;
class MachineFunction;
class MachineInstr;
class MDNode;

//===----------------------------------------------------------------------===//
/// InsnRange - This is used to track range of instructions with identical
/// lexical scope.
///
using InsnRange = std::pair<const MachineInstr *, const MachineInstr *>;

//===----------------------------------------------------------------------===//
/// LexicalScope - This class is used to track scope information.
///
class LexicalScope {
public:
  LexicalScope(LexicalScope *P, const DILocalScope *D, const DILocation *I,
               bool A)
      : Parent(P), Desc(D), InlinedAtLocation(I), AbstractScope(A) {
    assert(D);
    assert(D->getSubprogram()->getUnit()->getEmissionKind() !=
           DICompileUnit::NoDebug &&
           "Don't build lexical scopes for non-debug locations");
    assert(D->isResolved() && "Expected resolved node");
    assert((!I || I->isResolved()) && "Expected resolved node");
    if (Parent)
      Parent->addChild(this);
  }

  // Accessors.
  LexicalScope *getParent() const { return Parent; }
  const MDNode *getDesc() const { return Desc; }
  const DILocation *getInlinedAt() const { return InlinedAtLocation; }
  const DILocalScope *getScopeNode() const { return Desc; }
  bool isAbstractScope() const { return AbstractScope; }
  SmallVectorImpl<LexicalScope *> &getChildren() { return Children; }
  SmallVectorImpl<InsnRange> &getRanges() { return Ranges; }

  /// addChild - Add a child scope.
  void addChild(LexicalScope *S) { Children.push_back(S); }

  /// openInsnRange - This scope covers instruction range starting from MI.
  void openInsnRange(const MachineInstr *MI) {
    if (!FirstInsn)
      FirstInsn = MI;

    if (Parent)
      Parent->openInsnRange(MI);
  }

  /// extendInsnRange - Extend the current instruction range covered by
  /// this scope.
  void extendInsnRange(const MachineInstr *MI) {
    assert(FirstInsn && "MI Range is not open!");
    LastInsn = MI;
    if (Parent)
      Parent->extendInsnRange(MI);
  }

  /// closeInsnRange - Create a range based on FirstInsn and LastInsn collected
  /// until now. This is used when a new scope is encountered while walking
  /// machine instructions.
  void closeInsnRange(LexicalScope *NewScope = nullptr) {
    assert(LastInsn && "Last insn missing!");
    Ranges.push_back(InsnRange(FirstInsn, LastInsn));
    FirstInsn = nullptr;
    LastInsn = nullptr;
    // If Parent dominates NewScope then do not close Parent's instruction
    // range.
    if (Parent && (!NewScope || !Parent->dominates(NewScope)))
      Parent->closeInsnRange(NewScope);
  }

  /// dominates - Return true if current scope dominates given lexical scope.
  bool dominates(const LexicalScope *S) const {
    if (S == this)
      return true;
    if (DFSIn < S->getDFSIn() && DFSOut > S->getDFSOut())
      return true;
    return false;
  }

  // Depth First Search support to walk and manipulate LexicalScope hierarchy.
  unsigned getDFSOut() const { return DFSOut; }
  void setDFSOut(unsigned O) { DFSOut = O; }
  unsigned getDFSIn() const { return DFSIn; }
  void setDFSIn(unsigned I) { DFSIn = I; }

  /// dump - print lexical scope.
  void dump(unsigned Indent = 0) const;

private:
  LexicalScope *Parent;                        // Parent to this scope.
  const DILocalScope *Desc;                    // Debug info descriptor.
  const DILocation *InlinedAtLocation;         // Location at which this
                                               // scope is inlined.
  bool AbstractScope;                          // Abstract Scope
  SmallVector<LexicalScope *, 4> Children;     // Scopes defined in scope.
                                               // Contents not owned.
  SmallVector<InsnRange, 4> Ranges;

  const MachineInstr *LastInsn = nullptr;  // Last instruction of this scope.
  const MachineInstr *FirstInsn = nullptr; // First instruction of this scope.
  unsigned DFSIn = 0; // In & Out Depth use to determine scope nesting.
  unsigned DFSOut = 0;
};

//===----------------------------------------------------------------------===//
/// LexicalScopes -  This class provides interface to collect and use lexical
/// scoping information from machine instruction.
///
class LexicalScopes {
public:
  LexicalScopes() = default;

  /// initialize - Scan machine function and constuct lexical scope nest, resets
  /// the instance if necessary.
  void initialize(const MachineFunction &);

  /// releaseMemory - release memory.
  void reset();

  /// empty - Return true if there is any lexical scope information available.
  bool empty() { return CurrentFnLexicalScope == nullptr; }

  /// getCurrentFunctionScope - Return lexical scope for the current function.
  LexicalScope *getCurrentFunctionScope() const {
    return CurrentFnLexicalScope;
  }

  /// getMachineBasicBlocks - Populate given set using machine basic blocks
  /// which have machine instructions that belong to lexical scope identified by
  /// DebugLoc.
  void getMachineBasicBlocks(const DILocation *DL,
                             SmallPtrSetImpl<const MachineBasicBlock *> &MBBs);

  /// dominates - Return true if DebugLoc's lexical scope dominates at least one
  /// machine instruction's lexical scope in a given machine basic block.
  bool dominates(const DILocation *DL, MachineBasicBlock *MBB);

  /// findLexicalScope - Find lexical scope, either regular or inlined, for the
  /// given DebugLoc. Return NULL if not found.
  LexicalScope *findLexicalScope(const DILocation *DL);

  /// getAbstractScopesList - Return a reference to list of abstract scopes.
  ArrayRef<LexicalScope *> getAbstractScopesList() const {
    return AbstractScopesList;
  }

  /// findAbstractScope - Find an abstract scope or return null.
  LexicalScope *findAbstractScope(const DILocalScope *N) {
    auto I = AbstractScopeMap.find(N);
    return I != AbstractScopeMap.end() ? &I->second : nullptr;
  }

  /// findInlinedScope - Find an inlined scope for the given scope/inlined-at.
  LexicalScope *findInlinedScope(const DILocalScope *N, const DILocation *IA) {
    auto I = InlinedLexicalScopeMap.find(std::make_pair(N, IA));
    return I != InlinedLexicalScopeMap.end() ? &I->second : nullptr;
  }

  /// findLexicalScope - Find regular lexical scope or return null.
  LexicalScope *findLexicalScope(const DILocalScope *N) {
    auto I = LexicalScopeMap.find(N);
    return I != LexicalScopeMap.end() ? &I->second : nullptr;
  }

  /// dump - Print data structures to dbgs().
  void dump() const;

  /// getOrCreateAbstractScope - Find or create an abstract lexical scope.
  LexicalScope *getOrCreateAbstractScope(const DILocalScope *Scope);

private:
  /// getOrCreateLexicalScope - Find lexical scope for the given Scope/IA. If
  /// not available then create new lexical scope.
  LexicalScope *getOrCreateLexicalScope(const DILocalScope *Scope,
                                        const DILocation *IA = nullptr);
  LexicalScope *getOrCreateLexicalScope(const DILocation *DL) {
    return DL ? getOrCreateLexicalScope(DL->getScope(), DL->getInlinedAt())
              : nullptr;
  }

  /// getOrCreateRegularScope - Find or create a regular lexical scope.
  LexicalScope *getOrCreateRegularScope(const DILocalScope *Scope);

  /// getOrCreateInlinedScope - Find or create an inlined lexical scope.
  LexicalScope *getOrCreateInlinedScope(const DILocalScope *Scope,
                                        const DILocation *InlinedAt);

  /// extractLexicalScopes - Extract instruction ranges for each lexical scopes
  /// for the given machine function.
  void extractLexicalScopes(SmallVectorImpl<InsnRange> &MIRanges,
                            DenseMap<const MachineInstr *, LexicalScope *> &M);
  void constructScopeNest(LexicalScope *Scope);
  void
  assignInstructionRanges(SmallVectorImpl<InsnRange> &MIRanges,
                          DenseMap<const MachineInstr *, LexicalScope *> &M);

  const MachineFunction *MF = nullptr;

  /// LexicalScopeMap - Tracks the scopes in the current function.
  // Use an unordered_map to ensure value pointer validity over insertion.
  std::unordered_map<const DILocalScope *, LexicalScope> LexicalScopeMap;

  /// InlinedLexicalScopeMap - Tracks inlined function scopes in current
  /// function.
  std::unordered_map<std::pair<const DILocalScope *, const DILocation *>,
                     LexicalScope,
                     pair_hash<const DILocalScope *, const DILocation *>>
      InlinedLexicalScopeMap;

  /// AbstractScopeMap - These scopes are  not included LexicalScopeMap.
  // Use an unordered_map to ensure value pointer validity over insertion.
  std::unordered_map<const DILocalScope *, LexicalScope> AbstractScopeMap;

  /// AbstractScopesList - Tracks abstract scopes constructed while processing
  /// a function.
  SmallVector<LexicalScope *, 4> AbstractScopesList;

  /// CurrentFnLexicalScope - Top level scope for the current function.
  ///
  LexicalScope *CurrentFnLexicalScope = nullptr;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_LEXICALSCOPES_H
