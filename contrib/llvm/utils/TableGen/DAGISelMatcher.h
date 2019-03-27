//===- DAGISelMatcher.h - Representation of DAG pattern matcher -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_UTILS_TABLEGEN_DAGISELMATCHER_H
#define LLVM_UTILS_TABLEGEN_DAGISELMATCHER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/MachineValueType.h"

namespace llvm {
  struct CodeGenRegister;
  class CodeGenDAGPatterns;
  class Matcher;
  class PatternToMatch;
  class raw_ostream;
  class ComplexPattern;
  class Record;
  class SDNodeInfo;
  class TreePredicateFn;
  class TreePattern;

Matcher *ConvertPatternToMatcher(const PatternToMatch &Pattern,unsigned Variant,
                                 const CodeGenDAGPatterns &CGP);
void OptimizeMatcher(std::unique_ptr<Matcher> &Matcher,
                     const CodeGenDAGPatterns &CGP);
void EmitMatcherTable(const Matcher *Matcher, const CodeGenDAGPatterns &CGP,
                      raw_ostream &OS);


/// Matcher - Base class for all the DAG ISel Matcher representation
/// nodes.
class Matcher {
  // The next matcher node that is executed after this one.  Null if this is the
  // last stage of a match.
  std::unique_ptr<Matcher> Next;
  virtual void anchor();
public:
  enum KindTy {
    // Matcher state manipulation.
    Scope,                // Push a checking scope.
    RecordNode,           // Record the current node.
    RecordChild,          // Record a child of the current node.
    RecordMemRef,         // Record the memref in the current node.
    CaptureGlueInput,     // If the current node has an input glue, save it.
    MoveChild,            // Move current node to specified child.
    MoveParent,           // Move current node to parent.

    // Predicate checking.
    CheckSame,            // Fail if not same as prev match.
    CheckChildSame,       // Fail if child not same as prev match.
    CheckPatternPredicate,
    CheckPredicate,       // Fail if node predicate fails.
    CheckOpcode,          // Fail if not opcode.
    SwitchOpcode,         // Dispatch based on opcode.
    CheckType,            // Fail if not correct type.
    SwitchType,           // Dispatch based on type.
    CheckChildType,       // Fail if child has wrong type.
    CheckInteger,         // Fail if wrong val.
    CheckChildInteger,    // Fail if child is wrong val.
    CheckCondCode,        // Fail if not condcode.
    CheckValueType,
    CheckComplexPat,
    CheckAndImm,
    CheckOrImm,
    CheckFoldableChainNode,

    // Node creation/emisssion.
    EmitInteger,          // Create a TargetConstant
    EmitStringInteger,    // Create a TargetConstant from a string.
    EmitRegister,         // Create a register.
    EmitConvertToTarget,  // Convert a imm/fpimm to target imm/fpimm
    EmitMergeInputChains, // Merge together a chains for an input.
    EmitCopyToReg,        // Emit a copytoreg into a physreg.
    EmitNode,             // Create a DAG node
    EmitNodeXForm,        // Run a SDNodeXForm
    CompleteMatch,        // Finish a match and update the results.
    MorphNodeTo           // Build a node, finish a match and update results.
  };
  const KindTy Kind;

protected:
  Matcher(KindTy K) : Kind(K) {}
public:
  virtual ~Matcher() {}

  KindTy getKind() const { return Kind; }

  Matcher *getNext() { return Next.get(); }
  const Matcher *getNext() const { return Next.get(); }
  void setNext(Matcher *C) { Next.reset(C); }
  Matcher *takeNext() { return Next.release(); }

  std::unique_ptr<Matcher> &getNextPtr() { return Next; }

  bool isEqual(const Matcher *M) const {
    if (getKind() != M->getKind()) return false;
    return isEqualImpl(M);
  }

  /// isSimplePredicateNode - Return true if this is a simple predicate that
  /// operates on the node or its children without potential side effects or a
  /// change of the current node.
  bool isSimplePredicateNode() const {
    switch (getKind()) {
    default: return false;
    case CheckSame:
    case CheckChildSame:
    case CheckPatternPredicate:
    case CheckPredicate:
    case CheckOpcode:
    case CheckType:
    case CheckChildType:
    case CheckInteger:
    case CheckChildInteger:
    case CheckCondCode:
    case CheckValueType:
    case CheckAndImm:
    case CheckOrImm:
    case CheckFoldableChainNode:
      return true;
    }
  }

  /// isSimplePredicateOrRecordNode - Return true if this is a record node or
  /// a simple predicate.
  bool isSimplePredicateOrRecordNode() const {
    return isSimplePredicateNode() ||
           getKind() == RecordNode || getKind() == RecordChild;
  }

  /// unlinkNode - Unlink the specified node from this chain.  If Other == this,
  /// we unlink the next pointer and return it.  Otherwise we unlink Other from
  /// the list and return this.
  Matcher *unlinkNode(Matcher *Other);

  /// canMoveBefore - Return true if this matcher is the same as Other, or if
  /// we can move this matcher past all of the nodes in-between Other and this
  /// node.  Other must be equal to or before this.
  bool canMoveBefore(const Matcher *Other) const;

  /// canMoveBeforeNode - Return true if it is safe to move the current matcher
  /// across the specified one.
  bool canMoveBeforeNode(const Matcher *Other) const;

  /// isContradictory - Return true of these two matchers could never match on
  /// the same node.
  bool isContradictory(const Matcher *Other) const {
    // Since this predicate is reflexive, we canonicalize the ordering so that
    // we always match a node against nodes with kinds that are greater or equal
    // to them.  For example, we'll pass in a CheckType node as an argument to
    // the CheckOpcode method, not the other way around.
    if (getKind() < Other->getKind())
      return isContradictoryImpl(Other);
    return Other->isContradictoryImpl(this);
  }

  void print(raw_ostream &OS, unsigned indent = 0) const;
  void printOne(raw_ostream &OS) const;
  void dump() const;
protected:
  virtual void printImpl(raw_ostream &OS, unsigned indent) const = 0;
  virtual bool isEqualImpl(const Matcher *M) const = 0;
  virtual bool isContradictoryImpl(const Matcher *M) const { return false; }
};

/// ScopeMatcher - This attempts to match each of its children to find the first
/// one that successfully matches.  If one child fails, it tries the next child.
/// If none of the children match then this check fails.  It never has a 'next'.
class ScopeMatcher : public Matcher {
  SmallVector<Matcher*, 4> Children;
public:
  ScopeMatcher(ArrayRef<Matcher *> children)
    : Matcher(Scope), Children(children.begin(), children.end()) {
  }
  ~ScopeMatcher() override;

  unsigned getNumChildren() const { return Children.size(); }

  Matcher *getChild(unsigned i) { return Children[i]; }
  const Matcher *getChild(unsigned i) const { return Children[i]; }

  void resetChild(unsigned i, Matcher *N) {
    delete Children[i];
    Children[i] = N;
  }

  Matcher *takeChild(unsigned i) {
    Matcher *Res = Children[i];
    Children[i] = nullptr;
    return Res;
  }

  void setNumChildren(unsigned NC) {
    if (NC < Children.size()) {
      // delete any children we're about to lose pointers to.
      for (unsigned i = NC, e = Children.size(); i != e; ++i)
        delete Children[i];
    }
    Children.resize(NC);
  }

  static bool classof(const Matcher *N) {
    return N->getKind() == Scope;
  }

private:
  void printImpl(raw_ostream &OS, unsigned indent) const override;
  bool isEqualImpl(const Matcher *M) const override { return false; }
};

/// RecordMatcher - Save the current node in the operand list.
class RecordMatcher : public Matcher {
  /// WhatFor - This is a string indicating why we're recording this.  This
  /// should only be used for comment generation not anything semantic.
  std::string WhatFor;

  /// ResultNo - The slot number in the RecordedNodes vector that this will be,
  /// just printed as a comment.
  unsigned ResultNo;
public:
  RecordMatcher(const std::string &whatfor, unsigned resultNo)
    : Matcher(RecordNode), WhatFor(whatfor), ResultNo(resultNo) {}

  const std::string &getWhatFor() const { return WhatFor; }
  unsigned getResultNo() const { return ResultNo; }

  static bool classof(const Matcher *N) {
    return N->getKind() == RecordNode;
  }

private:
  void printImpl(raw_ostream &OS, unsigned indent) const override;
  bool isEqualImpl(const Matcher *M) const override { return true; }
};

/// RecordChildMatcher - Save a numbered child of the current node, or fail
/// the match if it doesn't exist.  This is logically equivalent to:
///    MoveChild N + RecordNode + MoveParent.
class RecordChildMatcher : public Matcher {
  unsigned ChildNo;

  /// WhatFor - This is a string indicating why we're recording this.  This
  /// should only be used for comment generation not anything semantic.
  std::string WhatFor;

  /// ResultNo - The slot number in the RecordedNodes vector that this will be,
  /// just printed as a comment.
  unsigned ResultNo;
public:
  RecordChildMatcher(unsigned childno, const std::string &whatfor,
                     unsigned resultNo)
  : Matcher(RecordChild), ChildNo(childno), WhatFor(whatfor),
    ResultNo(resultNo) {}

  unsigned getChildNo() const { return ChildNo; }
  const std::string &getWhatFor() const { return WhatFor; }
  unsigned getResultNo() const { return ResultNo; }

  static bool classof(const Matcher *N) {
    return N->getKind() == RecordChild;
  }

private:
  void printImpl(raw_ostream &OS, unsigned indent) const override;
  bool isEqualImpl(const Matcher *M) const override {
    return cast<RecordChildMatcher>(M)->getChildNo() == getChildNo();
  }
};

/// RecordMemRefMatcher - Save the current node's memref.
class RecordMemRefMatcher : public Matcher {
public:
  RecordMemRefMatcher() : Matcher(RecordMemRef) {}

  static bool classof(const Matcher *N) {
    return N->getKind() == RecordMemRef;
  }

private:
  void printImpl(raw_ostream &OS, unsigned indent) const override;
  bool isEqualImpl(const Matcher *M) const override { return true; }
};


/// CaptureGlueInputMatcher - If the current record has a glue input, record
/// it so that it is used as an input to the generated code.
class CaptureGlueInputMatcher : public Matcher {
public:
  CaptureGlueInputMatcher() : Matcher(CaptureGlueInput) {}

  static bool classof(const Matcher *N) {
    return N->getKind() == CaptureGlueInput;
  }

private:
  void printImpl(raw_ostream &OS, unsigned indent) const override;
  bool isEqualImpl(const Matcher *M) const override { return true; }
};

/// MoveChildMatcher - This tells the interpreter to move into the
/// specified child node.
class MoveChildMatcher : public Matcher {
  unsigned ChildNo;
public:
  MoveChildMatcher(unsigned childNo) : Matcher(MoveChild), ChildNo(childNo) {}

  unsigned getChildNo() const { return ChildNo; }

  static bool classof(const Matcher *N) {
    return N->getKind() == MoveChild;
  }

private:
  void printImpl(raw_ostream &OS, unsigned indent) const override;
  bool isEqualImpl(const Matcher *M) const override {
    return cast<MoveChildMatcher>(M)->getChildNo() == getChildNo();
  }
};

/// MoveParentMatcher - This tells the interpreter to move to the parent
/// of the current node.
class MoveParentMatcher : public Matcher {
public:
  MoveParentMatcher() : Matcher(MoveParent) {}

  static bool classof(const Matcher *N) {
    return N->getKind() == MoveParent;
  }

private:
  void printImpl(raw_ostream &OS, unsigned indent) const override;
  bool isEqualImpl(const Matcher *M) const override { return true; }
};

/// CheckSameMatcher - This checks to see if this node is exactly the same
/// node as the specified match that was recorded with 'Record'.  This is used
/// when patterns have the same name in them, like '(mul GPR:$in, GPR:$in)'.
class CheckSameMatcher : public Matcher {
  unsigned MatchNumber;
public:
  CheckSameMatcher(unsigned matchnumber)
    : Matcher(CheckSame), MatchNumber(matchnumber) {}

  unsigned getMatchNumber() const { return MatchNumber; }

  static bool classof(const Matcher *N) {
    return N->getKind() == CheckSame;
  }

private:
  void printImpl(raw_ostream &OS, unsigned indent) const override;
  bool isEqualImpl(const Matcher *M) const override {
    return cast<CheckSameMatcher>(M)->getMatchNumber() == getMatchNumber();
  }
};

/// CheckChildSameMatcher - This checks to see if child node is exactly the same
/// node as the specified match that was recorded with 'Record'.  This is used
/// when patterns have the same name in them, like '(mul GPR:$in, GPR:$in)'.
class CheckChildSameMatcher : public Matcher {
  unsigned ChildNo;
  unsigned MatchNumber;
public:
  CheckChildSameMatcher(unsigned childno, unsigned matchnumber)
    : Matcher(CheckChildSame), ChildNo(childno), MatchNumber(matchnumber) {}

  unsigned getChildNo() const { return ChildNo; }
  unsigned getMatchNumber() const { return MatchNumber; }

  static bool classof(const Matcher *N) {
    return N->getKind() == CheckChildSame;
  }

private:
  void printImpl(raw_ostream &OS, unsigned indent) const override;
  bool isEqualImpl(const Matcher *M) const override {
    return cast<CheckChildSameMatcher>(M)->ChildNo == ChildNo &&
           cast<CheckChildSameMatcher>(M)->MatchNumber == MatchNumber;
  }
};

/// CheckPatternPredicateMatcher - This checks the target-specific predicate
/// to see if the entire pattern is capable of matching.  This predicate does
/// not take a node as input.  This is used for subtarget feature checks etc.
class CheckPatternPredicateMatcher : public Matcher {
  std::string Predicate;
public:
  CheckPatternPredicateMatcher(StringRef predicate)
    : Matcher(CheckPatternPredicate), Predicate(predicate) {}

  StringRef getPredicate() const { return Predicate; }

  static bool classof(const Matcher *N) {
    return N->getKind() == CheckPatternPredicate;
  }

private:
  void printImpl(raw_ostream &OS, unsigned indent) const override;
  bool isEqualImpl(const Matcher *M) const override {
    return cast<CheckPatternPredicateMatcher>(M)->getPredicate() == Predicate;
  }
};

/// CheckPredicateMatcher - This checks the target-specific predicate to
/// see if the node is acceptable.
class CheckPredicateMatcher : public Matcher {
  TreePattern *Pred;
  const SmallVector<unsigned, 4> Operands;
public:
  CheckPredicateMatcher(const TreePredicateFn &pred,
                        const SmallVectorImpl<unsigned> &Operands);

  TreePredicateFn getPredicate() const;
  unsigned getNumOperands() const;
  unsigned getOperandNo(unsigned i) const;

  static bool classof(const Matcher *N) {
    return N->getKind() == CheckPredicate;
  }

private:
  void printImpl(raw_ostream &OS, unsigned indent) const override;
  bool isEqualImpl(const Matcher *M) const override {
    return cast<CheckPredicateMatcher>(M)->Pred == Pred;
  }
};


/// CheckOpcodeMatcher - This checks to see if the current node has the
/// specified opcode, if not it fails to match.
class CheckOpcodeMatcher : public Matcher {
  const SDNodeInfo &Opcode;
public:
  CheckOpcodeMatcher(const SDNodeInfo &opcode)
    : Matcher(CheckOpcode), Opcode(opcode) {}

  const SDNodeInfo &getOpcode() const { return Opcode; }

  static bool classof(const Matcher *N) {
    return N->getKind() == CheckOpcode;
  }

private:
  void printImpl(raw_ostream &OS, unsigned indent) const override;
  bool isEqualImpl(const Matcher *M) const override;
  bool isContradictoryImpl(const Matcher *M) const override;
};

/// SwitchOpcodeMatcher - Switch based on the current node's opcode, dispatching
/// to one matcher per opcode.  If the opcode doesn't match any of the cases,
/// then the match fails.  This is semantically equivalent to a Scope node where
/// every child does a CheckOpcode, but is much faster.
class SwitchOpcodeMatcher : public Matcher {
  SmallVector<std::pair<const SDNodeInfo*, Matcher*>, 8> Cases;
public:
  SwitchOpcodeMatcher(ArrayRef<std::pair<const SDNodeInfo*, Matcher*> > cases)
    : Matcher(SwitchOpcode), Cases(cases.begin(), cases.end()) {}
  ~SwitchOpcodeMatcher() override;

  static bool classof(const Matcher *N) {
    return N->getKind() == SwitchOpcode;
  }

  unsigned getNumCases() const { return Cases.size(); }

  const SDNodeInfo &getCaseOpcode(unsigned i) const { return *Cases[i].first; }
  Matcher *getCaseMatcher(unsigned i) { return Cases[i].second; }
  const Matcher *getCaseMatcher(unsigned i) const { return Cases[i].second; }

private:
  void printImpl(raw_ostream &OS, unsigned indent) const override;
  bool isEqualImpl(const Matcher *M) const override { return false; }
};

/// CheckTypeMatcher - This checks to see if the current node has the
/// specified type at the specified result, if not it fails to match.
class CheckTypeMatcher : public Matcher {
  MVT::SimpleValueType Type;
  unsigned ResNo;
public:
  CheckTypeMatcher(MVT::SimpleValueType type, unsigned resno)
    : Matcher(CheckType), Type(type), ResNo(resno) {}

  MVT::SimpleValueType getType() const { return Type; }
  unsigned getResNo() const { return ResNo; }

  static bool classof(const Matcher *N) {
    return N->getKind() == CheckType;
  }

private:
  void printImpl(raw_ostream &OS, unsigned indent) const override;
  bool isEqualImpl(const Matcher *M) const override {
    return cast<CheckTypeMatcher>(M)->Type == Type;
  }
  bool isContradictoryImpl(const Matcher *M) const override;
};

/// SwitchTypeMatcher - Switch based on the current node's type, dispatching
/// to one matcher per case.  If the type doesn't match any of the cases,
/// then the match fails.  This is semantically equivalent to a Scope node where
/// every child does a CheckType, but is much faster.
class SwitchTypeMatcher : public Matcher {
  SmallVector<std::pair<MVT::SimpleValueType, Matcher*>, 8> Cases;
public:
  SwitchTypeMatcher(ArrayRef<std::pair<MVT::SimpleValueType, Matcher*> > cases)
  : Matcher(SwitchType), Cases(cases.begin(), cases.end()) {}
  ~SwitchTypeMatcher() override;

  static bool classof(const Matcher *N) {
    return N->getKind() == SwitchType;
  }

  unsigned getNumCases() const { return Cases.size(); }

  MVT::SimpleValueType getCaseType(unsigned i) const { return Cases[i].first; }
  Matcher *getCaseMatcher(unsigned i) { return Cases[i].second; }
  const Matcher *getCaseMatcher(unsigned i) const { return Cases[i].second; }

private:
  void printImpl(raw_ostream &OS, unsigned indent) const override;
  bool isEqualImpl(const Matcher *M) const override { return false; }
};


/// CheckChildTypeMatcher - This checks to see if a child node has the
/// specified type, if not it fails to match.
class CheckChildTypeMatcher : public Matcher {
  unsigned ChildNo;
  MVT::SimpleValueType Type;
public:
  CheckChildTypeMatcher(unsigned childno, MVT::SimpleValueType type)
    : Matcher(CheckChildType), ChildNo(childno), Type(type) {}

  unsigned getChildNo() const { return ChildNo; }
  MVT::SimpleValueType getType() const { return Type; }

  static bool classof(const Matcher *N) {
    return N->getKind() == CheckChildType;
  }

private:
  void printImpl(raw_ostream &OS, unsigned indent) const override;
  bool isEqualImpl(const Matcher *M) const override {
    return cast<CheckChildTypeMatcher>(M)->ChildNo == ChildNo &&
           cast<CheckChildTypeMatcher>(M)->Type == Type;
  }
  bool isContradictoryImpl(const Matcher *M) const override;
};


/// CheckIntegerMatcher - This checks to see if the current node is a
/// ConstantSDNode with the specified integer value, if not it fails to match.
class CheckIntegerMatcher : public Matcher {
  int64_t Value;
public:
  CheckIntegerMatcher(int64_t value)
    : Matcher(CheckInteger), Value(value) {}

  int64_t getValue() const { return Value; }

  static bool classof(const Matcher *N) {
    return N->getKind() == CheckInteger;
  }

private:
  void printImpl(raw_ostream &OS, unsigned indent) const override;
  bool isEqualImpl(const Matcher *M) const override {
    return cast<CheckIntegerMatcher>(M)->Value == Value;
  }
  bool isContradictoryImpl(const Matcher *M) const override;
};

/// CheckChildIntegerMatcher - This checks to see if the child node is a
/// ConstantSDNode with a specified integer value, if not it fails to match.
class CheckChildIntegerMatcher : public Matcher {
  unsigned ChildNo;
  int64_t Value;
public:
  CheckChildIntegerMatcher(unsigned childno, int64_t value)
    : Matcher(CheckChildInteger), ChildNo(childno), Value(value) {}

  unsigned getChildNo() const { return ChildNo; }
  int64_t getValue() const { return Value; }

  static bool classof(const Matcher *N) {
    return N->getKind() == CheckChildInteger;
  }

private:
  void printImpl(raw_ostream &OS, unsigned indent) const override;
  bool isEqualImpl(const Matcher *M) const override {
    return cast<CheckChildIntegerMatcher>(M)->ChildNo == ChildNo &&
           cast<CheckChildIntegerMatcher>(M)->Value == Value;
  }
  bool isContradictoryImpl(const Matcher *M) const override;
};

/// CheckCondCodeMatcher - This checks to see if the current node is a
/// CondCodeSDNode with the specified condition, if not it fails to match.
class CheckCondCodeMatcher : public Matcher {
  StringRef CondCodeName;
public:
  CheckCondCodeMatcher(StringRef condcodename)
    : Matcher(CheckCondCode), CondCodeName(condcodename) {}

  StringRef getCondCodeName() const { return CondCodeName; }

  static bool classof(const Matcher *N) {
    return N->getKind() == CheckCondCode;
  }

private:
  void printImpl(raw_ostream &OS, unsigned indent) const override;
  bool isEqualImpl(const Matcher *M) const override {
    return cast<CheckCondCodeMatcher>(M)->CondCodeName == CondCodeName;
  }
};

/// CheckValueTypeMatcher - This checks to see if the current node is a
/// VTSDNode with the specified type, if not it fails to match.
class CheckValueTypeMatcher : public Matcher {
  StringRef TypeName;
public:
  CheckValueTypeMatcher(StringRef type_name)
    : Matcher(CheckValueType), TypeName(type_name) {}

  StringRef getTypeName() const { return TypeName; }

  static bool classof(const Matcher *N) {
    return N->getKind() == CheckValueType;
  }

private:
  void printImpl(raw_ostream &OS, unsigned indent) const override;
  bool isEqualImpl(const Matcher *M) const override {
    return cast<CheckValueTypeMatcher>(M)->TypeName == TypeName;
  }
  bool isContradictoryImpl(const Matcher *M) const override;
};



/// CheckComplexPatMatcher - This node runs the specified ComplexPattern on
/// the current node.
class CheckComplexPatMatcher : public Matcher {
  const ComplexPattern &Pattern;

  /// MatchNumber - This is the recorded nodes slot that contains the node we
  /// want to match against.
  unsigned MatchNumber;

  /// Name - The name of the node we're matching, for comment emission.
  std::string Name;

  /// FirstResult - This is the first slot in the RecordedNodes list that the
  /// result of the match populates.
  unsigned FirstResult;
public:
  CheckComplexPatMatcher(const ComplexPattern &pattern, unsigned matchnumber,
                         const std::string &name, unsigned firstresult)
    : Matcher(CheckComplexPat), Pattern(pattern), MatchNumber(matchnumber),
      Name(name), FirstResult(firstresult) {}

  const ComplexPattern &getPattern() const { return Pattern; }
  unsigned getMatchNumber() const { return MatchNumber; }

  const std::string getName() const { return Name; }
  unsigned getFirstResult() const { return FirstResult; }

  static bool classof(const Matcher *N) {
    return N->getKind() == CheckComplexPat;
  }

private:
  void printImpl(raw_ostream &OS, unsigned indent) const override;
  bool isEqualImpl(const Matcher *M) const override {
    return &cast<CheckComplexPatMatcher>(M)->Pattern == &Pattern &&
           cast<CheckComplexPatMatcher>(M)->MatchNumber == MatchNumber;
  }
};

/// CheckAndImmMatcher - This checks to see if the current node is an 'and'
/// with something equivalent to the specified immediate.
class CheckAndImmMatcher : public Matcher {
  int64_t Value;
public:
  CheckAndImmMatcher(int64_t value)
    : Matcher(CheckAndImm), Value(value) {}

  int64_t getValue() const { return Value; }

  static bool classof(const Matcher *N) {
    return N->getKind() == CheckAndImm;
  }

private:
  void printImpl(raw_ostream &OS, unsigned indent) const override;
  bool isEqualImpl(const Matcher *M) const override {
    return cast<CheckAndImmMatcher>(M)->Value == Value;
  }
};

/// CheckOrImmMatcher - This checks to see if the current node is an 'and'
/// with something equivalent to the specified immediate.
class CheckOrImmMatcher : public Matcher {
  int64_t Value;
public:
  CheckOrImmMatcher(int64_t value)
    : Matcher(CheckOrImm), Value(value) {}

  int64_t getValue() const { return Value; }

  static bool classof(const Matcher *N) {
    return N->getKind() == CheckOrImm;
  }

private:
  void printImpl(raw_ostream &OS, unsigned indent) const override;
  bool isEqualImpl(const Matcher *M) const override {
    return cast<CheckOrImmMatcher>(M)->Value == Value;
  }
};

/// CheckFoldableChainNodeMatcher - This checks to see if the current node
/// (which defines a chain operand) is safe to fold into a larger pattern.
class CheckFoldableChainNodeMatcher : public Matcher {
public:
  CheckFoldableChainNodeMatcher()
    : Matcher(CheckFoldableChainNode) {}

  static bool classof(const Matcher *N) {
    return N->getKind() == CheckFoldableChainNode;
  }

private:
  void printImpl(raw_ostream &OS, unsigned indent) const override;
  bool isEqualImpl(const Matcher *M) const override { return true; }
};

/// EmitIntegerMatcher - This creates a new TargetConstant.
class EmitIntegerMatcher : public Matcher {
  int64_t Val;
  MVT::SimpleValueType VT;
public:
  EmitIntegerMatcher(int64_t val, MVT::SimpleValueType vt)
    : Matcher(EmitInteger), Val(val), VT(vt) {}

  int64_t getValue() const { return Val; }
  MVT::SimpleValueType getVT() const { return VT; }

  static bool classof(const Matcher *N) {
    return N->getKind() == EmitInteger;
  }

private:
  void printImpl(raw_ostream &OS, unsigned indent) const override;
  bool isEqualImpl(const Matcher *M) const override {
    return cast<EmitIntegerMatcher>(M)->Val == Val &&
           cast<EmitIntegerMatcher>(M)->VT == VT;
  }
};

/// EmitStringIntegerMatcher - A target constant whose value is represented
/// by a string.
class EmitStringIntegerMatcher : public Matcher {
  std::string Val;
  MVT::SimpleValueType VT;
public:
  EmitStringIntegerMatcher(const std::string &val, MVT::SimpleValueType vt)
    : Matcher(EmitStringInteger), Val(val), VT(vt) {}

  const std::string &getValue() const { return Val; }
  MVT::SimpleValueType getVT() const { return VT; }

  static bool classof(const Matcher *N) {
    return N->getKind() == EmitStringInteger;
  }

private:
  void printImpl(raw_ostream &OS, unsigned indent) const override;
  bool isEqualImpl(const Matcher *M) const override {
    return cast<EmitStringIntegerMatcher>(M)->Val == Val &&
           cast<EmitStringIntegerMatcher>(M)->VT == VT;
  }
};

/// EmitRegisterMatcher - This creates a new TargetConstant.
class EmitRegisterMatcher : public Matcher {
  /// Reg - The def for the register that we're emitting.  If this is null, then
  /// this is a reference to zero_reg.
  const CodeGenRegister *Reg;
  MVT::SimpleValueType VT;
public:
  EmitRegisterMatcher(const CodeGenRegister *reg, MVT::SimpleValueType vt)
    : Matcher(EmitRegister), Reg(reg), VT(vt) {}

  const CodeGenRegister *getReg() const { return Reg; }
  MVT::SimpleValueType getVT() const { return VT; }

  static bool classof(const Matcher *N) {
    return N->getKind() == EmitRegister;
  }

private:
  void printImpl(raw_ostream &OS, unsigned indent) const override;
  bool isEqualImpl(const Matcher *M) const override {
    return cast<EmitRegisterMatcher>(M)->Reg == Reg &&
           cast<EmitRegisterMatcher>(M)->VT == VT;
  }
};

/// EmitConvertToTargetMatcher - Emit an operation that reads a specified
/// recorded node and converts it from being a ISD::Constant to
/// ISD::TargetConstant, likewise for ConstantFP.
class EmitConvertToTargetMatcher : public Matcher {
  unsigned Slot;
public:
  EmitConvertToTargetMatcher(unsigned slot)
    : Matcher(EmitConvertToTarget), Slot(slot) {}

  unsigned getSlot() const { return Slot; }

  static bool classof(const Matcher *N) {
    return N->getKind() == EmitConvertToTarget;
  }

private:
  void printImpl(raw_ostream &OS, unsigned indent) const override;
  bool isEqualImpl(const Matcher *M) const override {
    return cast<EmitConvertToTargetMatcher>(M)->Slot == Slot;
  }
};

/// EmitMergeInputChainsMatcher - Emit a node that merges a list of input
/// chains together with a token factor.  The list of nodes are the nodes in the
/// matched pattern that have chain input/outputs.  This node adds all input
/// chains of these nodes if they are not themselves a node in the pattern.
class EmitMergeInputChainsMatcher : public Matcher {
  SmallVector<unsigned, 3> ChainNodes;
public:
  EmitMergeInputChainsMatcher(ArrayRef<unsigned> nodes)
    : Matcher(EmitMergeInputChains), ChainNodes(nodes.begin(), nodes.end()) {}

  unsigned getNumNodes() const { return ChainNodes.size(); }

  unsigned getNode(unsigned i) const {
    assert(i < ChainNodes.size());
    return ChainNodes[i];
  }

  static bool classof(const Matcher *N) {
    return N->getKind() == EmitMergeInputChains;
  }

private:
  void printImpl(raw_ostream &OS, unsigned indent) const override;
  bool isEqualImpl(const Matcher *M) const override {
    return cast<EmitMergeInputChainsMatcher>(M)->ChainNodes == ChainNodes;
  }
};

/// EmitCopyToRegMatcher - Emit a CopyToReg node from a value to a physreg,
/// pushing the chain and glue results.
///
class EmitCopyToRegMatcher : public Matcher {
  unsigned SrcSlot; // Value to copy into the physreg.
  Record *DestPhysReg;
public:
  EmitCopyToRegMatcher(unsigned srcSlot, Record *destPhysReg)
    : Matcher(EmitCopyToReg), SrcSlot(srcSlot), DestPhysReg(destPhysReg) {}

  unsigned getSrcSlot() const { return SrcSlot; }
  Record *getDestPhysReg() const { return DestPhysReg; }

  static bool classof(const Matcher *N) {
    return N->getKind() == EmitCopyToReg;
  }

private:
  void printImpl(raw_ostream &OS, unsigned indent) const override;
  bool isEqualImpl(const Matcher *M) const override {
    return cast<EmitCopyToRegMatcher>(M)->SrcSlot == SrcSlot &&
           cast<EmitCopyToRegMatcher>(M)->DestPhysReg == DestPhysReg;
  }
};



/// EmitNodeXFormMatcher - Emit an operation that runs an SDNodeXForm on a
/// recorded node and records the result.
class EmitNodeXFormMatcher : public Matcher {
  unsigned Slot;
  Record *NodeXForm;
public:
  EmitNodeXFormMatcher(unsigned slot, Record *nodeXForm)
    : Matcher(EmitNodeXForm), Slot(slot), NodeXForm(nodeXForm) {}

  unsigned getSlot() const { return Slot; }
  Record *getNodeXForm() const { return NodeXForm; }

  static bool classof(const Matcher *N) {
    return N->getKind() == EmitNodeXForm;
  }

private:
  void printImpl(raw_ostream &OS, unsigned indent) const override;
  bool isEqualImpl(const Matcher *M) const override {
    return cast<EmitNodeXFormMatcher>(M)->Slot == Slot &&
           cast<EmitNodeXFormMatcher>(M)->NodeXForm == NodeXForm;
  }
};

/// EmitNodeMatcherCommon - Common class shared between EmitNode and
/// MorphNodeTo.
class EmitNodeMatcherCommon : public Matcher {
  std::string OpcodeName;
  const SmallVector<MVT::SimpleValueType, 3> VTs;
  const SmallVector<unsigned, 6> Operands;
  bool HasChain, HasInGlue, HasOutGlue, HasMemRefs;

  /// NumFixedArityOperands - If this is a fixed arity node, this is set to -1.
  /// If this is a varidic node, this is set to the number of fixed arity
  /// operands in the root of the pattern.  The rest are appended to this node.
  int NumFixedArityOperands;
public:
  EmitNodeMatcherCommon(const std::string &opcodeName,
                        ArrayRef<MVT::SimpleValueType> vts,
                        ArrayRef<unsigned> operands,
                        bool hasChain, bool hasInGlue, bool hasOutGlue,
                        bool hasmemrefs,
                        int numfixedarityoperands, bool isMorphNodeTo)
    : Matcher(isMorphNodeTo ? MorphNodeTo : EmitNode), OpcodeName(opcodeName),
      VTs(vts.begin(), vts.end()), Operands(operands.begin(), operands.end()),
      HasChain(hasChain), HasInGlue(hasInGlue), HasOutGlue(hasOutGlue),
      HasMemRefs(hasmemrefs), NumFixedArityOperands(numfixedarityoperands) {}

  const std::string &getOpcodeName() const { return OpcodeName; }

  unsigned getNumVTs() const { return VTs.size(); }
  MVT::SimpleValueType getVT(unsigned i) const {
    assert(i < VTs.size());
    return VTs[i];
  }

  unsigned getNumOperands() const { return Operands.size(); }
  unsigned getOperand(unsigned i) const {
    assert(i < Operands.size());
    return Operands[i];
  }

  const SmallVectorImpl<MVT::SimpleValueType> &getVTList() const { return VTs; }
  const SmallVectorImpl<unsigned> &getOperandList() const { return Operands; }


  bool hasChain() const { return HasChain; }
  bool hasInFlag() const { return HasInGlue; }
  bool hasOutFlag() const { return HasOutGlue; }
  bool hasMemRefs() const { return HasMemRefs; }
  int getNumFixedArityOperands() const { return NumFixedArityOperands; }

  static bool classof(const Matcher *N) {
    return N->getKind() == EmitNode || N->getKind() == MorphNodeTo;
  }

private:
  void printImpl(raw_ostream &OS, unsigned indent) const override;
  bool isEqualImpl(const Matcher *M) const override;
};

/// EmitNodeMatcher - This signals a successful match and generates a node.
class EmitNodeMatcher : public EmitNodeMatcherCommon {
  void anchor() override;
  unsigned FirstResultSlot;
public:
  EmitNodeMatcher(const std::string &opcodeName,
                  ArrayRef<MVT::SimpleValueType> vts,
                  ArrayRef<unsigned> operands,
                  bool hasChain, bool hasInFlag, bool hasOutFlag,
                  bool hasmemrefs,
                  int numfixedarityoperands, unsigned firstresultslot)
  : EmitNodeMatcherCommon(opcodeName, vts, operands, hasChain,
                          hasInFlag, hasOutFlag, hasmemrefs,
                          numfixedarityoperands, false),
    FirstResultSlot(firstresultslot) {}

  unsigned getFirstResultSlot() const { return FirstResultSlot; }

  static bool classof(const Matcher *N) {
    return N->getKind() == EmitNode;
  }

};

class MorphNodeToMatcher : public EmitNodeMatcherCommon {
  void anchor() override;
  const PatternToMatch &Pattern;
public:
  MorphNodeToMatcher(const std::string &opcodeName,
                     ArrayRef<MVT::SimpleValueType> vts,
                     ArrayRef<unsigned> operands,
                     bool hasChain, bool hasInFlag, bool hasOutFlag,
                     bool hasmemrefs,
                     int numfixedarityoperands, const PatternToMatch &pattern)
    : EmitNodeMatcherCommon(opcodeName, vts, operands, hasChain,
                            hasInFlag, hasOutFlag, hasmemrefs,
                            numfixedarityoperands, true),
      Pattern(pattern) {
  }

  const PatternToMatch &getPattern() const { return Pattern; }

  static bool classof(const Matcher *N) {
    return N->getKind() == MorphNodeTo;
  }
};

/// CompleteMatchMatcher - Complete a match by replacing the results of the
/// pattern with the newly generated nodes.  This also prints a comment
/// indicating the source and dest patterns.
class CompleteMatchMatcher : public Matcher {
  SmallVector<unsigned, 2> Results;
  const PatternToMatch &Pattern;
public:
  CompleteMatchMatcher(ArrayRef<unsigned> results,
                       const PatternToMatch &pattern)
  : Matcher(CompleteMatch), Results(results.begin(), results.end()),
    Pattern(pattern) {}

  unsigned getNumResults() const { return Results.size(); }
  unsigned getResult(unsigned R) const { return Results[R]; }
  const PatternToMatch &getPattern() const { return Pattern; }

  static bool classof(const Matcher *N) {
    return N->getKind() == CompleteMatch;
  }

private:
  void printImpl(raw_ostream &OS, unsigned indent) const override;
  bool isEqualImpl(const Matcher *M) const override {
    return cast<CompleteMatchMatcher>(M)->Results == Results &&
          &cast<CompleteMatchMatcher>(M)->Pattern == &Pattern;
  }
};

} // end namespace llvm

#endif
