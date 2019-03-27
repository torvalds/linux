//===- HexagonMCChecker.h - Instruction bundle checking ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This implements the checking of insns inside a bundle according to the
// packet constraint rules of the Hexagon ISA.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HEXAGON_MCTARGETDESC_HEXAGONMCCHECKER_H
#define LLVM_LIB_TARGET_HEXAGON_MCTARGETDESC_HEXAGONMCCHECKER_H

#include "MCTargetDesc/HexagonMCInstrInfo.h"
#include "MCTargetDesc/HexagonMCTargetDesc.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/SMLoc.h"
#include <set>
#include <utility>

namespace llvm {

class MCContext;
class MCInst;
class MCInstrInfo;
class MCRegisterInfo;
class MCSubtargetInfo;

/// Check for a valid bundle.
class HexagonMCChecker {
  MCContext &Context;
  MCInst &MCB;
  const MCRegisterInfo &RI;
  MCInstrInfo const &MCII;
  MCSubtargetInfo const &STI;
  bool ReportErrors;

  /// Set of definitions: register #, if predicated, if predicated true.
  using PredSense = std::pair<unsigned, bool>;
  static const PredSense Unconditional;
  using PredSet = std::multiset<PredSense>;
  using PredSetIterator = std::multiset<PredSense>::iterator;

  using DefsIterator = DenseMap<unsigned, PredSet>::iterator;
  DenseMap<unsigned, PredSet> Defs;

  /// Set of weak definitions whose clashes should be enforced selectively.
  using SoftDefsIterator = std::set<unsigned>::iterator;
  std::set<unsigned> SoftDefs;

  /// Set of temporary definitions not committed to the register file.
  using TmpDefsIterator = std::set<unsigned>::iterator;
  std::set<unsigned> TmpDefs;

  /// Set of new predicates used.
  using NewPredsIterator = std::set<unsigned>::iterator;
  std::set<unsigned> NewPreds;

  /// Set of predicates defined late.
  using LatePredsIterator = std::multiset<unsigned>::iterator;
  std::multiset<unsigned> LatePreds;

  /// Set of uses.
  using UsesIterator = std::set<unsigned>::iterator;
  std::set<unsigned> Uses;

  /// Pre-defined set of read-only registers.
  using ReadOnlyIterator = std::set<unsigned>::iterator;
  std::set<unsigned> ReadOnly;

  void init();
  void init(MCInst const &);
  void initReg(MCInst const &, unsigned, unsigned &PredReg, bool &isTrue);

  bool registerUsed(unsigned Register);
  std::tuple<MCInst const *, unsigned, HexagonMCInstrInfo::PredicateInfo>
  registerProducer(unsigned Register,
                   HexagonMCInstrInfo::PredicateInfo Predicated);

  // Checks performed.
  bool checkBranches();
  bool checkPredicates();
  bool checkNewValues();
  bool checkRegisters();
  bool checkRegistersReadOnly();
  void checkRegisterCurDefs();
  bool checkSolo();
  bool checkShuffle();
  bool checkSlots();
  bool checkAXOK();
  bool checkHWLoop();
  bool checkCOFMax1();

  static void compoundRegisterMap(unsigned &);

  bool isPredicateRegister(unsigned R) const {
    return (Hexagon::P0 == R || Hexagon::P1 == R || Hexagon::P2 == R ||
            Hexagon::P3 == R);
  }

  bool isLoopRegister(unsigned R) const {
    return (Hexagon::SA0 == R || Hexagon::LC0 == R || Hexagon::SA1 == R ||
            Hexagon::LC1 == R);
  }

public:
  explicit HexagonMCChecker(MCContext &Context, MCInstrInfo const &MCII,
                            MCSubtargetInfo const &STI, MCInst &mcb,
                            const MCRegisterInfo &ri, bool ReportErrors = true);
  explicit HexagonMCChecker(HexagonMCChecker const &Check,
                            MCSubtargetInfo const &STI, bool CopyReportErrors);

  bool check(bool FullCheck = true);
  void reportErrorRegisters(unsigned Register);
  void reportErrorNewValue(unsigned Register);
  void reportError(SMLoc Loc, Twine const &Msg);
  void reportNote(SMLoc Loc, Twine const &Msg);
  void reportError(Twine const &Msg);
  void reportWarning(Twine const &Msg);
  void reportBranchErrors();
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HEXAGON_MCTARGETDESC_HEXAGONMCCHECKER_H
