//===- DAGISelMatcher.cpp - Representation of DAG pattern matcher ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "DAGISelMatcher.h"
#include "CodeGenDAGPatterns.h"
#include "CodeGenTarget.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TableGen/Record.h"
using namespace llvm;

void Matcher::anchor() { }

void Matcher::dump() const {
  print(errs(), 0);
}

void Matcher::print(raw_ostream &OS, unsigned indent) const {
  printImpl(OS, indent);
  if (Next)
    return Next->print(OS, indent);
}

void Matcher::printOne(raw_ostream &OS) const {
  printImpl(OS, 0);
}

/// unlinkNode - Unlink the specified node from this chain.  If Other == this,
/// we unlink the next pointer and return it.  Otherwise we unlink Other from
/// the list and return this.
Matcher *Matcher::unlinkNode(Matcher *Other) {
  if (this == Other)
    return takeNext();

  // Scan until we find the predecessor of Other.
  Matcher *Cur = this;
  for (; Cur && Cur->getNext() != Other; Cur = Cur->getNext())
    /*empty*/;

  if (!Cur) return nullptr;
  Cur->takeNext();
  Cur->setNext(Other->takeNext());
  return this;
}

/// canMoveBefore - Return true if this matcher is the same as Other, or if
/// we can move this matcher past all of the nodes in-between Other and this
/// node.  Other must be equal to or before this.
bool Matcher::canMoveBefore(const Matcher *Other) const {
  for (;; Other = Other->getNext()) {
    assert(Other && "Other didn't come before 'this'?");
    if (this == Other) return true;

    // We have to be able to move this node across the Other node.
    if (!canMoveBeforeNode(Other))
      return false;
  }
}

/// canMoveBeforeNode - Return true if it is safe to move the current matcher
/// across the specified one.
bool Matcher::canMoveBeforeNode(const Matcher *Other) const {
  // We can move simple predicates before record nodes.
  if (isSimplePredicateNode())
    return Other->isSimplePredicateOrRecordNode();

  // We can move record nodes across simple predicates.
  if (isSimplePredicateOrRecordNode())
    return isSimplePredicateNode();

  // We can't move record nodes across each other etc.
  return false;
}


ScopeMatcher::~ScopeMatcher() {
  for (Matcher *C : Children)
    delete C;
}

SwitchOpcodeMatcher::~SwitchOpcodeMatcher() {
  for (auto &C : Cases)
    delete C.second;
}

SwitchTypeMatcher::~SwitchTypeMatcher() {
  for (auto &C : Cases)
    delete C.second;
}

CheckPredicateMatcher::CheckPredicateMatcher(
    const TreePredicateFn &pred, const SmallVectorImpl<unsigned> &Ops)
  : Matcher(CheckPredicate), Pred(pred.getOrigPatFragRecord()),
    Operands(Ops.begin(), Ops.end()) {}

TreePredicateFn CheckPredicateMatcher::getPredicate() const {
  return TreePredicateFn(Pred);
}

unsigned CheckPredicateMatcher::getNumOperands() const {
  return Operands.size();
}

unsigned CheckPredicateMatcher::getOperandNo(unsigned i) const {
  assert(i < Operands.size());
  return Operands[i];
}


// printImpl methods.

void ScopeMatcher::printImpl(raw_ostream &OS, unsigned indent) const {
  OS.indent(indent) << "Scope\n";
  for (const Matcher *C : Children) {
    if (!C)
      OS.indent(indent+1) << "NULL POINTER\n";
    else
      C->print(OS, indent+2);
  }
}

void RecordMatcher::printImpl(raw_ostream &OS, unsigned indent) const {
  OS.indent(indent) << "Record\n";
}

void RecordChildMatcher::printImpl(raw_ostream &OS, unsigned indent) const {
  OS.indent(indent) << "RecordChild: " << ChildNo << '\n';
}

void RecordMemRefMatcher::printImpl(raw_ostream &OS, unsigned indent) const {
  OS.indent(indent) << "RecordMemRef\n";
}

void CaptureGlueInputMatcher::printImpl(raw_ostream &OS, unsigned indent) const{
  OS.indent(indent) << "CaptureGlueInput\n";
}

void MoveChildMatcher::printImpl(raw_ostream &OS, unsigned indent) const {
  OS.indent(indent) << "MoveChild " << ChildNo << '\n';
}

void MoveParentMatcher::printImpl(raw_ostream &OS, unsigned indent) const {
  OS.indent(indent) << "MoveParent\n";
}

void CheckSameMatcher::printImpl(raw_ostream &OS, unsigned indent) const {
  OS.indent(indent) << "CheckSame " << MatchNumber << '\n';
}

void CheckChildSameMatcher::printImpl(raw_ostream &OS, unsigned indent) const {
  OS.indent(indent) << "CheckChild" << ChildNo << "Same\n";
}

void CheckPatternPredicateMatcher::
printImpl(raw_ostream &OS, unsigned indent) const {
  OS.indent(indent) << "CheckPatternPredicate " << Predicate << '\n';
}

void CheckPredicateMatcher::printImpl(raw_ostream &OS, unsigned indent) const {
  OS.indent(indent) << "CheckPredicate " << getPredicate().getFnName() << '\n';
}

void CheckOpcodeMatcher::printImpl(raw_ostream &OS, unsigned indent) const {
  OS.indent(indent) << "CheckOpcode " << Opcode.getEnumName() << '\n';
}

void SwitchOpcodeMatcher::printImpl(raw_ostream &OS, unsigned indent) const {
  OS.indent(indent) << "SwitchOpcode: {\n";
  for (const auto &C : Cases) {
    OS.indent(indent) << "case " << C.first->getEnumName() << ":\n";
    C.second->print(OS, indent+2);
  }
  OS.indent(indent) << "}\n";
}


void CheckTypeMatcher::printImpl(raw_ostream &OS, unsigned indent) const {
  OS.indent(indent) << "CheckType " << getEnumName(Type) << ", ResNo="
    << ResNo << '\n';
}

void SwitchTypeMatcher::printImpl(raw_ostream &OS, unsigned indent) const {
  OS.indent(indent) << "SwitchType: {\n";
  for (const auto &C : Cases) {
    OS.indent(indent) << "case " << getEnumName(C.first) << ":\n";
    C.second->print(OS, indent+2);
  }
  OS.indent(indent) << "}\n";
}

void CheckChildTypeMatcher::printImpl(raw_ostream &OS, unsigned indent) const {
  OS.indent(indent) << "CheckChildType " << ChildNo << " "
    << getEnumName(Type) << '\n';
}


void CheckIntegerMatcher::printImpl(raw_ostream &OS, unsigned indent) const {
  OS.indent(indent) << "CheckInteger " << Value << '\n';
}

void CheckChildIntegerMatcher::printImpl(raw_ostream &OS,
                                         unsigned indent) const {
  OS.indent(indent) << "CheckChildInteger " << ChildNo << " " << Value << '\n';
}

void CheckCondCodeMatcher::printImpl(raw_ostream &OS, unsigned indent) const {
  OS.indent(indent) << "CheckCondCode ISD::" << CondCodeName << '\n';
}

void CheckValueTypeMatcher::printImpl(raw_ostream &OS, unsigned indent) const {
  OS.indent(indent) << "CheckValueType MVT::" << TypeName << '\n';
}

void CheckComplexPatMatcher::printImpl(raw_ostream &OS, unsigned indent) const {
  OS.indent(indent) << "CheckComplexPat " << Pattern.getSelectFunc() << '\n';
}

void CheckAndImmMatcher::printImpl(raw_ostream &OS, unsigned indent) const {
  OS.indent(indent) << "CheckAndImm " << Value << '\n';
}

void CheckOrImmMatcher::printImpl(raw_ostream &OS, unsigned indent) const {
  OS.indent(indent) << "CheckOrImm " << Value << '\n';
}

void CheckFoldableChainNodeMatcher::printImpl(raw_ostream &OS,
                                              unsigned indent) const {
  OS.indent(indent) << "CheckFoldableChainNode\n";
}

void EmitIntegerMatcher::printImpl(raw_ostream &OS, unsigned indent) const {
  OS.indent(indent) << "EmitInteger " << Val << " VT=" << getEnumName(VT)
                    << '\n';
}

void EmitStringIntegerMatcher::
printImpl(raw_ostream &OS, unsigned indent) const {
  OS.indent(indent) << "EmitStringInteger " << Val << " VT=" << getEnumName(VT)
                    << '\n';
}

void EmitRegisterMatcher::printImpl(raw_ostream &OS, unsigned indent) const {
  OS.indent(indent) << "EmitRegister ";
  if (Reg)
    OS << Reg->getName();
  else
    OS << "zero_reg";
  OS << " VT=" << getEnumName(VT) << '\n';
}

void EmitConvertToTargetMatcher::
printImpl(raw_ostream &OS, unsigned indent) const {
  OS.indent(indent) << "EmitConvertToTarget " << Slot << '\n';
}

void EmitMergeInputChainsMatcher::
printImpl(raw_ostream &OS, unsigned indent) const {
  OS.indent(indent) << "EmitMergeInputChains <todo: args>\n";
}

void EmitCopyToRegMatcher::printImpl(raw_ostream &OS, unsigned indent) const {
  OS.indent(indent) << "EmitCopyToReg <todo: args>\n";
}

void EmitNodeXFormMatcher::printImpl(raw_ostream &OS, unsigned indent) const {
  OS.indent(indent) << "EmitNodeXForm " << NodeXForm->getName()
     << " Slot=" << Slot << '\n';
}


void EmitNodeMatcherCommon::printImpl(raw_ostream &OS, unsigned indent) const {
  OS.indent(indent);
  OS << (isa<MorphNodeToMatcher>(this) ? "MorphNodeTo: " : "EmitNode: ")
     << OpcodeName << ": <todo flags> ";

  for (unsigned i = 0, e = VTs.size(); i != e; ++i)
    OS << ' ' << getEnumName(VTs[i]);
  OS << '(';
  for (unsigned i = 0, e = Operands.size(); i != e; ++i)
    OS << Operands[i] << ' ';
  OS << ")\n";
}

void CompleteMatchMatcher::printImpl(raw_ostream &OS, unsigned indent) const {
  OS.indent(indent) << "CompleteMatch <todo args>\n";
  OS.indent(indent) << "Src = " << *Pattern.getSrcPattern() << "\n";
  OS.indent(indent) << "Dst = " << *Pattern.getDstPattern() << "\n";
}

bool CheckOpcodeMatcher::isEqualImpl(const Matcher *M) const {
  // Note: pointer equality isn't enough here, we have to check the enum names
  // to ensure that the nodes are for the same opcode.
  return cast<CheckOpcodeMatcher>(M)->Opcode.getEnumName() ==
          Opcode.getEnumName();
}

bool EmitNodeMatcherCommon::isEqualImpl(const Matcher *m) const {
  const EmitNodeMatcherCommon *M = cast<EmitNodeMatcherCommon>(m);
  return M->OpcodeName == OpcodeName && M->VTs == VTs &&
         M->Operands == Operands && M->HasChain == HasChain &&
         M->HasInGlue == HasInGlue && M->HasOutGlue == HasOutGlue &&
         M->HasMemRefs == HasMemRefs &&
         M->NumFixedArityOperands == NumFixedArityOperands;
}

void EmitNodeMatcher::anchor() { }

void MorphNodeToMatcher::anchor() { }

// isContradictoryImpl Implementations.

static bool TypesAreContradictory(MVT::SimpleValueType T1,
                                  MVT::SimpleValueType T2) {
  // If the two types are the same, then they are the same, so they don't
  // contradict.
  if (T1 == T2) return false;

  // If either type is about iPtr, then they don't conflict unless the other
  // one is not a scalar integer type.
  if (T1 == MVT::iPTR)
    return !MVT(T2).isInteger() || MVT(T2).isVector();

  if (T2 == MVT::iPTR)
    return !MVT(T1).isInteger() || MVT(T1).isVector();

  // Otherwise, they are two different non-iPTR types, they conflict.
  return true;
}

bool CheckOpcodeMatcher::isContradictoryImpl(const Matcher *M) const {
  if (const CheckOpcodeMatcher *COM = dyn_cast<CheckOpcodeMatcher>(M)) {
    // One node can't have two different opcodes!
    // Note: pointer equality isn't enough here, we have to check the enum names
    // to ensure that the nodes are for the same opcode.
    return COM->getOpcode().getEnumName() != getOpcode().getEnumName();
  }

  // If the node has a known type, and if the type we're checking for is
  // different, then we know they contradict.  For example, a check for
  // ISD::STORE will never be true at the same time a check for Type i32 is.
  if (const CheckTypeMatcher *CT = dyn_cast<CheckTypeMatcher>(M)) {
    // If checking for a result the opcode doesn't have, it can't match.
    if (CT->getResNo() >= getOpcode().getNumResults())
      return true;

    MVT::SimpleValueType NodeType = getOpcode().getKnownType(CT->getResNo());
    if (NodeType != MVT::Other)
      return TypesAreContradictory(NodeType, CT->getType());
  }

  return false;
}

bool CheckTypeMatcher::isContradictoryImpl(const Matcher *M) const {
  if (const CheckTypeMatcher *CT = dyn_cast<CheckTypeMatcher>(M))
    return TypesAreContradictory(getType(), CT->getType());
  return false;
}

bool CheckChildTypeMatcher::isContradictoryImpl(const Matcher *M) const {
  if (const CheckChildTypeMatcher *CC = dyn_cast<CheckChildTypeMatcher>(M)) {
    // If the two checks are about different nodes, we don't know if they
    // conflict!
    if (CC->getChildNo() != getChildNo())
      return false;

    return TypesAreContradictory(getType(), CC->getType());
  }
  return false;
}

bool CheckIntegerMatcher::isContradictoryImpl(const Matcher *M) const {
  if (const CheckIntegerMatcher *CIM = dyn_cast<CheckIntegerMatcher>(M))
    return CIM->getValue() != getValue();
  return false;
}

bool CheckChildIntegerMatcher::isContradictoryImpl(const Matcher *M) const {
  if (const CheckChildIntegerMatcher *CCIM = dyn_cast<CheckChildIntegerMatcher>(M)) {
    // If the two checks are about different nodes, we don't know if they
    // conflict!
    if (CCIM->getChildNo() != getChildNo())
      return false;

    return CCIM->getValue() != getValue();
  }
  return false;
}

bool CheckValueTypeMatcher::isContradictoryImpl(const Matcher *M) const {
  if (const CheckValueTypeMatcher *CVT = dyn_cast<CheckValueTypeMatcher>(M))
    return CVT->getTypeName() != getTypeName();
  return false;
}

