//===- TGParser.cpp - Parser for TableGen Files ---------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Implement the Parser for TableGen.
//
//===----------------------------------------------------------------------===//

#include "TGParser.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TableGen/Record.h"
#include <algorithm>
#include <cassert>
#include <cstdint>

using namespace llvm;

//===----------------------------------------------------------------------===//
// Support Code for the Semantic Actions.
//===----------------------------------------------------------------------===//

namespace llvm {

struct SubClassReference {
  SMRange RefRange;
  Record *Rec;
  SmallVector<Init*, 4> TemplateArgs;

  SubClassReference() : Rec(nullptr) {}

  bool isInvalid() const { return Rec == nullptr; }
};

struct SubMultiClassReference {
  SMRange RefRange;
  MultiClass *MC;
  SmallVector<Init*, 4> TemplateArgs;

  SubMultiClassReference() : MC(nullptr) {}

  bool isInvalid() const { return MC == nullptr; }
  void dump() const;
};

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void SubMultiClassReference::dump() const {
  errs() << "Multiclass:\n";

  MC->dump();

  errs() << "Template args:\n";
  for (Init *TA : TemplateArgs)
    TA->dump();
}
#endif

} // end namespace llvm

static bool checkBitsConcrete(Record &R, const RecordVal &RV) {
  BitsInit *BV = cast<BitsInit>(RV.getValue());
  for (unsigned i = 0, e = BV->getNumBits(); i != e; ++i) {
    Init *Bit = BV->getBit(i);
    bool IsReference = false;
    if (auto VBI = dyn_cast<VarBitInit>(Bit)) {
      if (auto VI = dyn_cast<VarInit>(VBI->getBitVar())) {
        if (R.getValue(VI->getName()))
          IsReference = true;
      }
    } else if (isa<VarInit>(Bit)) {
      IsReference = true;
    }
    if (!(IsReference || Bit->isConcrete()))
      return false;
  }
  return true;
}

static void checkConcrete(Record &R) {
  for (const RecordVal &RV : R.getValues()) {
    // HACK: Disable this check for variables declared with 'field'. This is
    // done merely because existing targets have legitimate cases of
    // non-concrete variables in helper defs. Ideally, we'd introduce a
    // 'maybe' or 'optional' modifier instead of this.
    if (RV.getPrefix())
      continue;

    if (Init *V = RV.getValue()) {
      bool Ok = isa<BitsInit>(V) ? checkBitsConcrete(R, RV) : V->isConcrete();
      if (!Ok) {
        PrintError(R.getLoc(),
                   Twine("Initializer of '") + RV.getNameInitAsString() +
                   "' in '" + R.getNameInitAsString() +
                   "' could not be fully resolved: " +
                   RV.getValue()->getAsString());
      }
    }
  }
}

/// Return an Init with a qualifier prefix referring
/// to CurRec's name.
static Init *QualifyName(Record &CurRec, MultiClass *CurMultiClass,
                        Init *Name, StringRef Scoper) {
  Init *NewName =
      BinOpInit::getStrConcat(CurRec.getNameInit(), StringInit::get(Scoper));
  NewName = BinOpInit::getStrConcat(NewName, Name);
  if (CurMultiClass && Scoper != "::") {
    Init *Prefix = BinOpInit::getStrConcat(CurMultiClass->Rec.getNameInit(),
                                           StringInit::get("::"));
    NewName = BinOpInit::getStrConcat(Prefix, NewName);
  }

  if (BinOpInit *BinOp = dyn_cast<BinOpInit>(NewName))
    NewName = BinOp->Fold(&CurRec);
  return NewName;
}

/// Return the qualified version of the implicit 'NAME' template argument.
static Init *QualifiedNameOfImplicitName(Record &Rec,
                                         MultiClass *MC = nullptr) {
  return QualifyName(Rec, MC, StringInit::get("NAME"), MC ? "::" : ":");
}

static Init *QualifiedNameOfImplicitName(MultiClass *MC) {
  return QualifiedNameOfImplicitName(MC->Rec, MC);
}

bool TGParser::AddValue(Record *CurRec, SMLoc Loc, const RecordVal &RV) {
  if (!CurRec)
    CurRec = &CurMultiClass->Rec;

  if (RecordVal *ERV = CurRec->getValue(RV.getNameInit())) {
    // The value already exists in the class, treat this as a set.
    if (ERV->setValue(RV.getValue()))
      return Error(Loc, "New definition of '" + RV.getName() + "' of type '" +
                   RV.getType()->getAsString() + "' is incompatible with " +
                   "previous definition of type '" +
                   ERV->getType()->getAsString() + "'");
  } else {
    CurRec->addValue(RV);
  }
  return false;
}

/// SetValue -
/// Return true on error, false on success.
bool TGParser::SetValue(Record *CurRec, SMLoc Loc, Init *ValName,
                        ArrayRef<unsigned> BitList, Init *V,
                        bool AllowSelfAssignment) {
  if (!V) return false;

  if (!CurRec) CurRec = &CurMultiClass->Rec;

  RecordVal *RV = CurRec->getValue(ValName);
  if (!RV)
    return Error(Loc, "Value '" + ValName->getAsUnquotedString() +
                 "' unknown!");

  // Do not allow assignments like 'X = X'.  This will just cause infinite loops
  // in the resolution machinery.
  if (BitList.empty())
    if (VarInit *VI = dyn_cast<VarInit>(V))
      if (VI->getNameInit() == ValName && !AllowSelfAssignment)
        return Error(Loc, "Recursion / self-assignment forbidden");

  // If we are assigning to a subset of the bits in the value... then we must be
  // assigning to a field of BitsRecTy, which must have a BitsInit
  // initializer.
  //
  if (!BitList.empty()) {
    BitsInit *CurVal = dyn_cast<BitsInit>(RV->getValue());
    if (!CurVal)
      return Error(Loc, "Value '" + ValName->getAsUnquotedString() +
                   "' is not a bits type");

    // Convert the incoming value to a bits type of the appropriate size...
    Init *BI = V->getCastTo(BitsRecTy::get(BitList.size()));
    if (!BI)
      return Error(Loc, "Initializer is not compatible with bit range");

    SmallVector<Init *, 16> NewBits(CurVal->getNumBits());

    // Loop over bits, assigning values as appropriate.
    for (unsigned i = 0, e = BitList.size(); i != e; ++i) {
      unsigned Bit = BitList[i];
      if (NewBits[Bit])
        return Error(Loc, "Cannot set bit #" + Twine(Bit) + " of value '" +
                     ValName->getAsUnquotedString() + "' more than once");
      NewBits[Bit] = BI->getBit(i);
    }

    for (unsigned i = 0, e = CurVal->getNumBits(); i != e; ++i)
      if (!NewBits[i])
        NewBits[i] = CurVal->getBit(i);

    V = BitsInit::get(NewBits);
  }

  if (RV->setValue(V)) {
    std::string InitType;
    if (BitsInit *BI = dyn_cast<BitsInit>(V))
      InitType = (Twine("' of type bit initializer with length ") +
                  Twine(BI->getNumBits())).str();
    else if (TypedInit *TI = dyn_cast<TypedInit>(V))
      InitType = (Twine("' of type '") + TI->getType()->getAsString()).str();
    return Error(Loc, "Value '" + ValName->getAsUnquotedString() +
                          "' of type '" + RV->getType()->getAsString() +
                          "' is incompatible with initializer '" +
                          V->getAsString() + InitType + "'");
  }
  return false;
}

/// AddSubClass - Add SubClass as a subclass to CurRec, resolving its template
/// args as SubClass's template arguments.
bool TGParser::AddSubClass(Record *CurRec, SubClassReference &SubClass) {
  Record *SC = SubClass.Rec;
  // Add all of the values in the subclass into the current class.
  for (const RecordVal &Val : SC->getValues())
    if (AddValue(CurRec, SubClass.RefRange.Start, Val))
      return true;

  ArrayRef<Init *> TArgs = SC->getTemplateArgs();

  // Ensure that an appropriate number of template arguments are specified.
  if (TArgs.size() < SubClass.TemplateArgs.size())
    return Error(SubClass.RefRange.Start,
                 "More template args specified than expected");

  // Loop over all of the template arguments, setting them to the specified
  // value or leaving them as the default if necessary.
  MapResolver R(CurRec);

  for (unsigned i = 0, e = TArgs.size(); i != e; ++i) {
    if (i < SubClass.TemplateArgs.size()) {
      // If a value is specified for this template arg, set it now.
      if (SetValue(CurRec, SubClass.RefRange.Start, TArgs[i],
                   None, SubClass.TemplateArgs[i]))
        return true;
    } else if (!CurRec->getValue(TArgs[i])->getValue()->isComplete()) {
      return Error(SubClass.RefRange.Start,
                   "Value not specified for template argument #" +
                   Twine(i) + " (" + TArgs[i]->getAsUnquotedString() +
                   ") of subclass '" + SC->getNameInitAsString() + "'!");
    }

    R.set(TArgs[i], CurRec->getValue(TArgs[i])->getValue());

    CurRec->removeValue(TArgs[i]);
  }

  Init *Name;
  if (CurRec->isClass())
    Name =
        VarInit::get(QualifiedNameOfImplicitName(*CurRec), StringRecTy::get());
  else
    Name = CurRec->getNameInit();
  R.set(QualifiedNameOfImplicitName(*SC), Name);

  CurRec->resolveReferences(R);

  // Since everything went well, we can now set the "superclass" list for the
  // current record.
  ArrayRef<std::pair<Record *, SMRange>> SCs = SC->getSuperClasses();
  for (const auto &SCPair : SCs) {
    if (CurRec->isSubClassOf(SCPair.first))
      return Error(SubClass.RefRange.Start,
                   "Already subclass of '" + SCPair.first->getName() + "'!\n");
    CurRec->addSuperClass(SCPair.first, SCPair.second);
  }

  if (CurRec->isSubClassOf(SC))
    return Error(SubClass.RefRange.Start,
                 "Already subclass of '" + SC->getName() + "'!\n");
  CurRec->addSuperClass(SC, SubClass.RefRange);
  return false;
}

bool TGParser::AddSubClass(RecordsEntry &Entry, SubClassReference &SubClass) {
  if (Entry.Rec)
    return AddSubClass(Entry.Rec.get(), SubClass);

  for (auto &E : Entry.Loop->Entries) {
    if (AddSubClass(E, SubClass))
      return true;
  }

  return false;
}

/// AddSubMultiClass - Add SubMultiClass as a subclass to
/// CurMC, resolving its template args as SubMultiClass's
/// template arguments.
bool TGParser::AddSubMultiClass(MultiClass *CurMC,
                                SubMultiClassReference &SubMultiClass) {
  MultiClass *SMC = SubMultiClass.MC;

  ArrayRef<Init *> SMCTArgs = SMC->Rec.getTemplateArgs();
  if (SMCTArgs.size() < SubMultiClass.TemplateArgs.size())
    return Error(SubMultiClass.RefRange.Start,
                 "More template args specified than expected");

  // Prepare the mapping of template argument name to value, filling in default
  // values if necessary.
  SubstStack TemplateArgs;
  for (unsigned i = 0, e = SMCTArgs.size(); i != e; ++i) {
    if (i < SubMultiClass.TemplateArgs.size()) {
      TemplateArgs.emplace_back(SMCTArgs[i], SubMultiClass.TemplateArgs[i]);
    } else {
      Init *Default = SMC->Rec.getValue(SMCTArgs[i])->getValue();
      if (!Default->isComplete()) {
        return Error(SubMultiClass.RefRange.Start,
                     "value not specified for template argument #" + Twine(i) +
                         " (" + SMCTArgs[i]->getAsUnquotedString() +
                         ") of multiclass '" + SMC->Rec.getNameInitAsString() +
                         "'");
      }
      TemplateArgs.emplace_back(SMCTArgs[i], Default);
    }
  }

  TemplateArgs.emplace_back(
      QualifiedNameOfImplicitName(SMC),
      VarInit::get(QualifiedNameOfImplicitName(CurMC), StringRecTy::get()));

  // Add all of the defs in the subclass into the current multiclass.
  return resolve(SMC->Entries, TemplateArgs, false, &CurMC->Entries);
}

/// Add a record or foreach loop to the current context (global record keeper,
/// current inner-most foreach loop, or multiclass).
bool TGParser::addEntry(RecordsEntry E) {
  assert(!E.Rec || !E.Loop);

  if (!Loops.empty()) {
    Loops.back()->Entries.push_back(std::move(E));
    return false;
  }

  if (E.Loop) {
    SubstStack Stack;
    return resolve(*E.Loop, Stack, CurMultiClass == nullptr,
                   CurMultiClass ? &CurMultiClass->Entries : nullptr);
  }

  if (CurMultiClass) {
    CurMultiClass->Entries.push_back(std::move(E));
    return false;
  }

  return addDefOne(std::move(E.Rec));
}

/// Resolve the entries in \p Loop, going over inner loops recursively
/// and making the given subsitutions of (name, value) pairs.
///
/// The resulting records are stored in \p Dest if non-null. Otherwise, they
/// are added to the global record keeper.
bool TGParser::resolve(const ForeachLoop &Loop, SubstStack &Substs,
                       bool Final, std::vector<RecordsEntry> *Dest,
                       SMLoc *Loc) {
  MapResolver R;
  for (const auto &S : Substs)
    R.set(S.first, S.second);
  Init *List = Loop.ListValue->resolveReferences(R);
  auto LI = dyn_cast<ListInit>(List);
  if (!LI) {
    if (!Final) {
      Dest->emplace_back(make_unique<ForeachLoop>(Loop.Loc, Loop.IterVar,
                                                  List));
      return resolve(Loop.Entries, Substs, Final, &Dest->back().Loop->Entries,
                     Loc);
    }

    PrintError(Loop.Loc, Twine("attempting to loop over '") +
                              List->getAsString() + "', expected a list");
    return true;
  }

  bool Error = false;
  for (auto Elt : *LI) {
    Substs.emplace_back(Loop.IterVar->getNameInit(), Elt);
    Error = resolve(Loop.Entries, Substs, Final, Dest);
    Substs.pop_back();
    if (Error)
      break;
  }
  return Error;
}

/// Resolve the entries in \p Source, going over loops recursively and
/// making the given substitutions of (name, value) pairs.
///
/// The resulting records are stored in \p Dest if non-null. Otherwise, they
/// are added to the global record keeper.
bool TGParser::resolve(const std::vector<RecordsEntry> &Source,
                       SubstStack &Substs, bool Final,
                       std::vector<RecordsEntry> *Dest, SMLoc *Loc) {
  bool Error = false;
  for (auto &E : Source) {
    if (E.Loop) {
      Error = resolve(*E.Loop, Substs, Final, Dest);
    } else {
      auto Rec = make_unique<Record>(*E.Rec);
      if (Loc)
        Rec->appendLoc(*Loc);

      MapResolver R(Rec.get());
      for (const auto &S : Substs)
        R.set(S.first, S.second);
      Rec->resolveReferences(R);

      if (Dest)
        Dest->push_back(std::move(Rec));
      else
        Error = addDefOne(std::move(Rec));
    }
    if (Error)
      break;
  }
  return Error;
}

/// Resolve the record fully and add it to the record keeper.
bool TGParser::addDefOne(std::unique_ptr<Record> Rec) {
  if (Record *Prev = Records.getDef(Rec->getNameInitAsString())) {
    if (!Rec->isAnonymous()) {
      PrintError(Rec->getLoc(),
                 "def already exists: " + Rec->getNameInitAsString());
      PrintNote(Prev->getLoc(), "location of previous definition");
      return true;
    }
    Rec->setName(Records.getNewAnonymousName());
  }

  Rec->resolveReferences();
  checkConcrete(*Rec);

  if (!isa<StringInit>(Rec->getNameInit())) {
    PrintError(Rec->getLoc(), Twine("record name '") +
                                  Rec->getNameInit()->getAsString() +
                                  "' could not be fully resolved");
    return true;
  }

  // If ObjectBody has template arguments, it's an error.
  assert(Rec->getTemplateArgs().empty() && "How'd this get template args?");

  for (DefsetRecord *Defset : Defsets) {
    DefInit *I = Rec->getDefInit();
    if (!I->getType()->typeIsA(Defset->EltTy)) {
      PrintError(Rec->getLoc(), Twine("adding record of incompatible type '") +
                                    I->getType()->getAsString() +
                                     "' to defset");
      PrintNote(Defset->Loc, "location of defset declaration");
      return true;
    }
    Defset->Elements.push_back(I);
  }

  Records.addDef(std::move(Rec));
  return false;
}

//===----------------------------------------------------------------------===//
// Parser Code
//===----------------------------------------------------------------------===//

/// isObjectStart - Return true if this is a valid first token for an Object.
static bool isObjectStart(tgtok::TokKind K) {
  return K == tgtok::Class || K == tgtok::Def || K == tgtok::Defm ||
         K == tgtok::Let || K == tgtok::MultiClass || K == tgtok::Foreach ||
         K == tgtok::Defset;
}

/// ParseObjectName - If a valid object name is specified, return it. If no
/// name is specified, return the unset initializer. Return nullptr on parse
/// error.
///   ObjectName ::= Value [ '#' Value ]*
///   ObjectName ::= /*empty*/
///
Init *TGParser::ParseObjectName(MultiClass *CurMultiClass) {
  switch (Lex.getCode()) {
  case tgtok::colon:
  case tgtok::semi:
  case tgtok::l_brace:
    // These are all of the tokens that can begin an object body.
    // Some of these can also begin values but we disallow those cases
    // because they are unlikely to be useful.
    return UnsetInit::get();
  default:
    break;
  }

  Record *CurRec = nullptr;
  if (CurMultiClass)
    CurRec = &CurMultiClass->Rec;

  Init *Name = ParseValue(CurRec, StringRecTy::get(), ParseNameMode);
  if (!Name)
    return nullptr;

  if (CurMultiClass) {
    Init *NameStr = QualifiedNameOfImplicitName(CurMultiClass);
    HasReferenceResolver R(NameStr);
    Name->resolveReferences(R);
    if (!R.found())
      Name = BinOpInit::getStrConcat(VarInit::get(NameStr, StringRecTy::get()),
                                     Name);
  }

  return Name;
}

/// ParseClassID - Parse and resolve a reference to a class name.  This returns
/// null on error.
///
///    ClassID ::= ID
///
Record *TGParser::ParseClassID() {
  if (Lex.getCode() != tgtok::Id) {
    TokError("expected name for ClassID");
    return nullptr;
  }

  Record *Result = Records.getClass(Lex.getCurStrVal());
  if (!Result)
    TokError("Couldn't find class '" + Lex.getCurStrVal() + "'");

  Lex.Lex();
  return Result;
}

/// ParseMultiClassID - Parse and resolve a reference to a multiclass name.
/// This returns null on error.
///
///    MultiClassID ::= ID
///
MultiClass *TGParser::ParseMultiClassID() {
  if (Lex.getCode() != tgtok::Id) {
    TokError("expected name for MultiClassID");
    return nullptr;
  }

  MultiClass *Result = MultiClasses[Lex.getCurStrVal()].get();
  if (!Result)
    TokError("Couldn't find multiclass '" + Lex.getCurStrVal() + "'");

  Lex.Lex();
  return Result;
}

/// ParseSubClassReference - Parse a reference to a subclass or to a templated
/// subclass.  This returns a SubClassRefTy with a null Record* on error.
///
///  SubClassRef ::= ClassID
///  SubClassRef ::= ClassID '<' ValueList '>'
///
SubClassReference TGParser::
ParseSubClassReference(Record *CurRec, bool isDefm) {
  SubClassReference Result;
  Result.RefRange.Start = Lex.getLoc();

  if (isDefm) {
    if (MultiClass *MC = ParseMultiClassID())
      Result.Rec = &MC->Rec;
  } else {
    Result.Rec = ParseClassID();
  }
  if (!Result.Rec) return Result;

  // If there is no template arg list, we're done.
  if (Lex.getCode() != tgtok::less) {
    Result.RefRange.End = Lex.getLoc();
    return Result;
  }
  Lex.Lex();  // Eat the '<'

  if (Lex.getCode() == tgtok::greater) {
    TokError("subclass reference requires a non-empty list of template values");
    Result.Rec = nullptr;
    return Result;
  }

  ParseValueList(Result.TemplateArgs, CurRec, Result.Rec);
  if (Result.TemplateArgs.empty()) {
    Result.Rec = nullptr;   // Error parsing value list.
    return Result;
  }

  if (Lex.getCode() != tgtok::greater) {
    TokError("expected '>' in template value list");
    Result.Rec = nullptr;
    return Result;
  }
  Lex.Lex();
  Result.RefRange.End = Lex.getLoc();

  return Result;
}

/// ParseSubMultiClassReference - Parse a reference to a subclass or to a
/// templated submulticlass.  This returns a SubMultiClassRefTy with a null
/// Record* on error.
///
///  SubMultiClassRef ::= MultiClassID
///  SubMultiClassRef ::= MultiClassID '<' ValueList '>'
///
SubMultiClassReference TGParser::
ParseSubMultiClassReference(MultiClass *CurMC) {
  SubMultiClassReference Result;
  Result.RefRange.Start = Lex.getLoc();

  Result.MC = ParseMultiClassID();
  if (!Result.MC) return Result;

  // If there is no template arg list, we're done.
  if (Lex.getCode() != tgtok::less) {
    Result.RefRange.End = Lex.getLoc();
    return Result;
  }
  Lex.Lex();  // Eat the '<'

  if (Lex.getCode() == tgtok::greater) {
    TokError("subclass reference requires a non-empty list of template values");
    Result.MC = nullptr;
    return Result;
  }

  ParseValueList(Result.TemplateArgs, &CurMC->Rec, &Result.MC->Rec);
  if (Result.TemplateArgs.empty()) {
    Result.MC = nullptr;   // Error parsing value list.
    return Result;
  }

  if (Lex.getCode() != tgtok::greater) {
    TokError("expected '>' in template value list");
    Result.MC = nullptr;
    return Result;
  }
  Lex.Lex();
  Result.RefRange.End = Lex.getLoc();

  return Result;
}

/// ParseRangePiece - Parse a bit/value range.
///   RangePiece ::= INTVAL
///   RangePiece ::= INTVAL '-' INTVAL
///   RangePiece ::= INTVAL INTVAL
bool TGParser::ParseRangePiece(SmallVectorImpl<unsigned> &Ranges) {
  if (Lex.getCode() != tgtok::IntVal) {
    TokError("expected integer or bitrange");
    return true;
  }
  int64_t Start = Lex.getCurIntVal();
  int64_t End;

  if (Start < 0)
    return TokError("invalid range, cannot be negative");

  switch (Lex.Lex()) {  // eat first character.
  default:
    Ranges.push_back(Start);
    return false;
  case tgtok::minus:
    if (Lex.Lex() != tgtok::IntVal) {
      TokError("expected integer value as end of range");
      return true;
    }
    End = Lex.getCurIntVal();
    break;
  case tgtok::IntVal:
    End = -Lex.getCurIntVal();
    break;
  }
  if (End < 0)
    return TokError("invalid range, cannot be negative");
  Lex.Lex();

  // Add to the range.
  if (Start < End)
    for (; Start <= End; ++Start)
      Ranges.push_back(Start);
  else
    for (; Start >= End; --Start)
      Ranges.push_back(Start);
  return false;
}

/// ParseRangeList - Parse a list of scalars and ranges into scalar values.
///
///   RangeList ::= RangePiece (',' RangePiece)*
///
void TGParser::ParseRangeList(SmallVectorImpl<unsigned> &Result) {
  // Parse the first piece.
  if (ParseRangePiece(Result)) {
    Result.clear();
    return;
  }
  while (Lex.getCode() == tgtok::comma) {
    Lex.Lex();  // Eat the comma.

    // Parse the next range piece.
    if (ParseRangePiece(Result)) {
      Result.clear();
      return;
    }
  }
}

/// ParseOptionalRangeList - Parse either a range list in <>'s or nothing.
///   OptionalRangeList ::= '<' RangeList '>'
///   OptionalRangeList ::= /*empty*/
bool TGParser::ParseOptionalRangeList(SmallVectorImpl<unsigned> &Ranges) {
  if (Lex.getCode() != tgtok::less)
    return false;

  SMLoc StartLoc = Lex.getLoc();
  Lex.Lex(); // eat the '<'

  // Parse the range list.
  ParseRangeList(Ranges);
  if (Ranges.empty()) return true;

  if (Lex.getCode() != tgtok::greater) {
    TokError("expected '>' at end of range list");
    return Error(StartLoc, "to match this '<'");
  }
  Lex.Lex();   // eat the '>'.
  return false;
}

/// ParseOptionalBitList - Parse either a bit list in {}'s or nothing.
///   OptionalBitList ::= '{' RangeList '}'
///   OptionalBitList ::= /*empty*/
bool TGParser::ParseOptionalBitList(SmallVectorImpl<unsigned> &Ranges) {
  if (Lex.getCode() != tgtok::l_brace)
    return false;

  SMLoc StartLoc = Lex.getLoc();
  Lex.Lex(); // eat the '{'

  // Parse the range list.
  ParseRangeList(Ranges);
  if (Ranges.empty()) return true;

  if (Lex.getCode() != tgtok::r_brace) {
    TokError("expected '}' at end of bit list");
    return Error(StartLoc, "to match this '{'");
  }
  Lex.Lex();   // eat the '}'.
  return false;
}

/// ParseType - Parse and return a tblgen type.  This returns null on error.
///
///   Type ::= STRING                       // string type
///   Type ::= CODE                         // code type
///   Type ::= BIT                          // bit type
///   Type ::= BITS '<' INTVAL '>'          // bits<x> type
///   Type ::= INT                          // int type
///   Type ::= LIST '<' Type '>'            // list<x> type
///   Type ::= DAG                          // dag type
///   Type ::= ClassID                      // Record Type
///
RecTy *TGParser::ParseType() {
  switch (Lex.getCode()) {
  default: TokError("Unknown token when expecting a type"); return nullptr;
  case tgtok::String: Lex.Lex(); return StringRecTy::get();
  case tgtok::Code:   Lex.Lex(); return CodeRecTy::get();
  case tgtok::Bit:    Lex.Lex(); return BitRecTy::get();
  case tgtok::Int:    Lex.Lex(); return IntRecTy::get();
  case tgtok::Dag:    Lex.Lex(); return DagRecTy::get();
  case tgtok::Id:
    if (Record *R = ParseClassID()) return RecordRecTy::get(R);
    TokError("unknown class name");
    return nullptr;
  case tgtok::Bits: {
    if (Lex.Lex() != tgtok::less) { // Eat 'bits'
      TokError("expected '<' after bits type");
      return nullptr;
    }
    if (Lex.Lex() != tgtok::IntVal) {  // Eat '<'
      TokError("expected integer in bits<n> type");
      return nullptr;
    }
    uint64_t Val = Lex.getCurIntVal();
    if (Lex.Lex() != tgtok::greater) {  // Eat count.
      TokError("expected '>' at end of bits<n> type");
      return nullptr;
    }
    Lex.Lex();  // Eat '>'
    return BitsRecTy::get(Val);
  }
  case tgtok::List: {
    if (Lex.Lex() != tgtok::less) { // Eat 'bits'
      TokError("expected '<' after list type");
      return nullptr;
    }
    Lex.Lex();  // Eat '<'
    RecTy *SubType = ParseType();
    if (!SubType) return nullptr;

    if (Lex.getCode() != tgtok::greater) {
      TokError("expected '>' at end of list<ty> type");
      return nullptr;
    }
    Lex.Lex();  // Eat '>'
    return ListRecTy::get(SubType);
  }
  }
}

/// ParseIDValue - This is just like ParseIDValue above, but it assumes the ID
/// has already been read.
Init *TGParser::ParseIDValue(Record *CurRec, StringInit *Name, SMLoc NameLoc,
                             IDParseMode Mode) {
  if (CurRec) {
    if (const RecordVal *RV = CurRec->getValue(Name))
      return VarInit::get(Name, RV->getType());
  }

  if ((CurRec && CurRec->isClass()) || CurMultiClass) {
    Init *TemplateArgName;
    if (CurMultiClass) {
      TemplateArgName =
          QualifyName(CurMultiClass->Rec, CurMultiClass, Name, "::");
    } else
      TemplateArgName = QualifyName(*CurRec, CurMultiClass, Name, ":");

    Record *TemplateRec = CurMultiClass ? &CurMultiClass->Rec : CurRec;
    if (TemplateRec->isTemplateArg(TemplateArgName)) {
      const RecordVal *RV = TemplateRec->getValue(TemplateArgName);
      assert(RV && "Template arg doesn't exist??");
      return VarInit::get(TemplateArgName, RV->getType());
    } else if (Name->getValue() == "NAME") {
      return VarInit::get(TemplateArgName, StringRecTy::get());
    }
  }

  // If this is in a foreach loop, make sure it's not a loop iterator
  for (const auto &L : Loops) {
    VarInit *IterVar = dyn_cast<VarInit>(L->IterVar);
    if (IterVar && IterVar->getNameInit() == Name)
      return IterVar;
  }

  if (Mode == ParseNameMode)
    return Name;

  if (Init *I = Records.getGlobal(Name->getValue()))
    return I;

  // Allow self-references of concrete defs, but delay the lookup so that we
  // get the correct type.
  if (CurRec && !CurRec->isClass() && !CurMultiClass &&
      CurRec->getNameInit() == Name)
    return UnOpInit::get(UnOpInit::CAST, Name, CurRec->getType());

  Error(NameLoc, "Variable not defined: '" + Name->getValue() + "'");
  return nullptr;
}

/// ParseOperation - Parse an operator.  This returns null on error.
///
/// Operation ::= XOperator ['<' Type '>'] '(' Args ')'
///
Init *TGParser::ParseOperation(Record *CurRec, RecTy *ItemType) {
  switch (Lex.getCode()) {
  default:
    TokError("unknown operation");
    return nullptr;
  case tgtok::XHead:
  case tgtok::XTail:
  case tgtok::XSize:
  case tgtok::XEmpty:
  case tgtok::XCast: {  // Value ::= !unop '(' Value ')'
    UnOpInit::UnaryOp Code;
    RecTy *Type = nullptr;

    switch (Lex.getCode()) {
    default: llvm_unreachable("Unhandled code!");
    case tgtok::XCast:
      Lex.Lex();  // eat the operation
      Code = UnOpInit::CAST;

      Type = ParseOperatorType();

      if (!Type) {
        TokError("did not get type for unary operator");
        return nullptr;
      }

      break;
    case tgtok::XHead:
      Lex.Lex();  // eat the operation
      Code = UnOpInit::HEAD;
      break;
    case tgtok::XTail:
      Lex.Lex();  // eat the operation
      Code = UnOpInit::TAIL;
      break;
    case tgtok::XSize:
      Lex.Lex();
      Code = UnOpInit::SIZE;
      Type = IntRecTy::get();
      break;
    case tgtok::XEmpty:
      Lex.Lex();  // eat the operation
      Code = UnOpInit::EMPTY;
      Type = IntRecTy::get();
      break;
    }
    if (Lex.getCode() != tgtok::l_paren) {
      TokError("expected '(' after unary operator");
      return nullptr;
    }
    Lex.Lex();  // eat the '('

    Init *LHS = ParseValue(CurRec);
    if (!LHS) return nullptr;

    if (Code == UnOpInit::HEAD ||
        Code == UnOpInit::TAIL ||
        Code == UnOpInit::EMPTY) {
      ListInit *LHSl = dyn_cast<ListInit>(LHS);
      StringInit *LHSs = dyn_cast<StringInit>(LHS);
      TypedInit *LHSt = dyn_cast<TypedInit>(LHS);
      if (!LHSl && !LHSs && !LHSt) {
        TokError("expected list or string type argument in unary operator");
        return nullptr;
      }
      if (LHSt) {
        ListRecTy *LType = dyn_cast<ListRecTy>(LHSt->getType());
        StringRecTy *SType = dyn_cast<StringRecTy>(LHSt->getType());
        if (!LType && !SType) {
          TokError("expected list or string type argument in unary operator");
          return nullptr;
        }
      }

      if (Code == UnOpInit::HEAD || Code == UnOpInit::TAIL ||
          Code == UnOpInit::SIZE) {
        if (!LHSl && !LHSt) {
          TokError("expected list type argument in unary operator");
          return nullptr;
        }
      }

      if (Code == UnOpInit::HEAD || Code == UnOpInit::TAIL) {
        if (LHSl && LHSl->empty()) {
          TokError("empty list argument in unary operator");
          return nullptr;
        }
        if (LHSl) {
          Init *Item = LHSl->getElement(0);
          TypedInit *Itemt = dyn_cast<TypedInit>(Item);
          if (!Itemt) {
            TokError("untyped list element in unary operator");
            return nullptr;
          }
          Type = (Code == UnOpInit::HEAD) ? Itemt->getType()
                                          : ListRecTy::get(Itemt->getType());
        } else {
          assert(LHSt && "expected list type argument in unary operator");
          ListRecTy *LType = dyn_cast<ListRecTy>(LHSt->getType());
          if (!LType) {
            TokError("expected list type argument in unary operator");
            return nullptr;
          }
          Type = (Code == UnOpInit::HEAD) ? LType->getElementType() : LType;
        }
      }
    }

    if (Lex.getCode() != tgtok::r_paren) {
      TokError("expected ')' in unary operator");
      return nullptr;
    }
    Lex.Lex();  // eat the ')'
    return (UnOpInit::get(Code, LHS, Type))->Fold(CurRec);
  }

  case tgtok::XIsA: {
    // Value ::= !isa '<' Type '>' '(' Value ')'
    Lex.Lex(); // eat the operation

    RecTy *Type = ParseOperatorType();
    if (!Type)
      return nullptr;

    if (Lex.getCode() != tgtok::l_paren) {
      TokError("expected '(' after type of !isa");
      return nullptr;
    }
    Lex.Lex(); // eat the '('

    Init *LHS = ParseValue(CurRec);
    if (!LHS)
      return nullptr;

    if (Lex.getCode() != tgtok::r_paren) {
      TokError("expected ')' in !isa");
      return nullptr;
    }
    Lex.Lex(); // eat the ')'

    return (IsAOpInit::get(Type, LHS))->Fold();
  }

  case tgtok::XConcat:
  case tgtok::XADD:
  case tgtok::XAND:
  case tgtok::XOR:
  case tgtok::XSRA:
  case tgtok::XSRL:
  case tgtok::XSHL:
  case tgtok::XEq:
  case tgtok::XNe:
  case tgtok::XLe:
  case tgtok::XLt:
  case tgtok::XGe:
  case tgtok::XGt:
  case tgtok::XListConcat:
  case tgtok::XStrConcat: {  // Value ::= !binop '(' Value ',' Value ')'
    tgtok::TokKind OpTok = Lex.getCode();
    SMLoc OpLoc = Lex.getLoc();
    Lex.Lex();  // eat the operation

    BinOpInit::BinaryOp Code;
    switch (OpTok) {
    default: llvm_unreachable("Unhandled code!");
    case tgtok::XConcat: Code = BinOpInit::CONCAT; break;
    case tgtok::XADD:    Code = BinOpInit::ADD; break;
    case tgtok::XAND:    Code = BinOpInit::AND; break;
    case tgtok::XOR:     Code = BinOpInit::OR; break;
    case tgtok::XSRA:    Code = BinOpInit::SRA; break;
    case tgtok::XSRL:    Code = BinOpInit::SRL; break;
    case tgtok::XSHL:    Code = BinOpInit::SHL; break;
    case tgtok::XEq:     Code = BinOpInit::EQ; break;
    case tgtok::XNe:     Code = BinOpInit::NE; break;
    case tgtok::XLe:     Code = BinOpInit::LE; break;
    case tgtok::XLt:     Code = BinOpInit::LT; break;
    case tgtok::XGe:     Code = BinOpInit::GE; break;
    case tgtok::XGt:     Code = BinOpInit::GT; break;
    case tgtok::XListConcat: Code = BinOpInit::LISTCONCAT; break;
    case tgtok::XStrConcat: Code = BinOpInit::STRCONCAT; break;
    }

    RecTy *Type = nullptr;
    RecTy *ArgType = nullptr;
    switch (OpTok) {
    default:
      llvm_unreachable("Unhandled code!");
    case tgtok::XConcat:
      Type = DagRecTy::get();
      ArgType = DagRecTy::get();
      break;
    case tgtok::XAND:
    case tgtok::XOR:
    case tgtok::XSRA:
    case tgtok::XSRL:
    case tgtok::XSHL:
    case tgtok::XADD:
      Type = IntRecTy::get();
      ArgType = IntRecTy::get();
      break;
    case tgtok::XEq:
    case tgtok::XNe:
      Type = BitRecTy::get();
      // ArgType for Eq / Ne is not known at this point
      break;
    case tgtok::XLe:
    case tgtok::XLt:
    case tgtok::XGe:
    case tgtok::XGt:
      Type = BitRecTy::get();
      ArgType = IntRecTy::get();
      break;
    case tgtok::XListConcat:
      // We don't know the list type until we parse the first argument
      ArgType = ItemType;
      break;
    case tgtok::XStrConcat:
      Type = StringRecTy::get();
      ArgType = StringRecTy::get();
      break;
    }

    if (Type && ItemType && !Type->typeIsConvertibleTo(ItemType)) {
      Error(OpLoc, Twine("expected value of type '") +
                   ItemType->getAsString() + "', got '" +
                   Type->getAsString() + "'");
      return nullptr;
    }

    if (Lex.getCode() != tgtok::l_paren) {
      TokError("expected '(' after binary operator");
      return nullptr;
    }
    Lex.Lex();  // eat the '('

    SmallVector<Init*, 2> InitList;

    for (;;) {
      SMLoc InitLoc = Lex.getLoc();
      InitList.push_back(ParseValue(CurRec, ArgType));
      if (!InitList.back()) return nullptr;

      // All BinOps require their arguments to be of compatible types.
      TypedInit *TI = dyn_cast<TypedInit>(InitList.back());
      if (!ArgType) {
        ArgType = TI->getType();

        switch (Code) {
        case BinOpInit::LISTCONCAT:
          if (!isa<ListRecTy>(ArgType)) {
            Error(InitLoc, Twine("expected a list, got value of type '") +
                           ArgType->getAsString() + "'");
            return nullptr;
          }
          break;
        case BinOpInit::EQ:
        case BinOpInit::NE:
          if (!ArgType->typeIsConvertibleTo(IntRecTy::get()) &&
              !ArgType->typeIsConvertibleTo(StringRecTy::get())) {
            Error(InitLoc, Twine("expected int, bits, or string; got value of "
                                 "type '") + ArgType->getAsString() + "'");
            return nullptr;
          }
          break;
        default: llvm_unreachable("other ops have fixed argument types");
        }
      } else {
        RecTy *Resolved = resolveTypes(ArgType, TI->getType());
        if (!Resolved) {
          Error(InitLoc, Twine("expected value of type '") +
                         ArgType->getAsString() + "', got '" +
                         TI->getType()->getAsString() + "'");
          return nullptr;
        }
        if (Code != BinOpInit::ADD && Code != BinOpInit::AND &&
            Code != BinOpInit::OR && Code != BinOpInit::SRA &&
            Code != BinOpInit::SRL && Code != BinOpInit::SHL)
          ArgType = Resolved;
      }

      if (Lex.getCode() != tgtok::comma)
        break;
      Lex.Lex();  // eat the ','
    }

    if (Lex.getCode() != tgtok::r_paren) {
      TokError("expected ')' in operator");
      return nullptr;
    }
    Lex.Lex();  // eat the ')'

    if (Code == BinOpInit::LISTCONCAT)
      Type = ArgType;

    // We allow multiple operands to associative operators like !strconcat as
    // shorthand for nesting them.
    if (Code == BinOpInit::STRCONCAT || Code == BinOpInit::LISTCONCAT ||
        Code == BinOpInit::CONCAT || Code == BinOpInit::ADD ||
        Code == BinOpInit::AND || Code == BinOpInit::OR) {
      while (InitList.size() > 2) {
        Init *RHS = InitList.pop_back_val();
        RHS = (BinOpInit::get(Code, InitList.back(), RHS, Type))->Fold(CurRec);
        InitList.back() = RHS;
      }
    }

    if (InitList.size() == 2)
      return (BinOpInit::get(Code, InitList[0], InitList[1], Type))
          ->Fold(CurRec);

    Error(OpLoc, "expected two operands to operator");
    return nullptr;
  }

  case tgtok::XForEach: { // Value ::= !foreach '(' Id ',' Value ',' Value ')'
    SMLoc OpLoc = Lex.getLoc();
    Lex.Lex(); // eat the operation
    if (Lex.getCode() != tgtok::l_paren) {
      TokError("expected '(' after !foreach");
      return nullptr;
    }

    if (Lex.Lex() != tgtok::Id) { // eat the '('
      TokError("first argument of !foreach must be an identifier");
      return nullptr;
    }

    Init *LHS = StringInit::get(Lex.getCurStrVal());

    if (CurRec && CurRec->getValue(LHS)) {
      TokError((Twine("iteration variable '") + LHS->getAsString() +
                "' already defined")
                   .str());
      return nullptr;
    }

    if (Lex.Lex() != tgtok::comma) { // eat the id
      TokError("expected ',' in ternary operator");
      return nullptr;
    }
    Lex.Lex();  // eat the ','

    Init *MHS = ParseValue(CurRec);
    if (!MHS)
      return nullptr;

    if (Lex.getCode() != tgtok::comma) {
      TokError("expected ',' in ternary operator");
      return nullptr;
    }
    Lex.Lex();  // eat the ','

    TypedInit *MHSt = dyn_cast<TypedInit>(MHS);
    if (!MHSt) {
      TokError("could not get type of !foreach input");
      return nullptr;
    }

    RecTy *InEltType = nullptr;
    RecTy *OutEltType = nullptr;
    bool IsDAG = false;

    if (ListRecTy *InListTy = dyn_cast<ListRecTy>(MHSt->getType())) {
      InEltType = InListTy->getElementType();
      if (ItemType) {
        if (ListRecTy *OutListTy = dyn_cast<ListRecTy>(ItemType)) {
          OutEltType = OutListTy->getElementType();
        } else {
          Error(OpLoc,
                "expected value of type '" + Twine(ItemType->getAsString()) +
                "', but got !foreach of list type");
          return nullptr;
        }
      }
    } else if (DagRecTy *InDagTy = dyn_cast<DagRecTy>(MHSt->getType())) {
      InEltType = InDagTy;
      if (ItemType && !isa<DagRecTy>(ItemType)) {
        Error(OpLoc,
              "expected value of type '" + Twine(ItemType->getAsString()) +
              "', but got !foreach of dag type");
        return nullptr;
      }
      IsDAG = true;
    } else {
      TokError("!foreach must have list or dag input");
      return nullptr;
    }

    // We need to create a temporary record to provide a scope for the iteration
    // variable while parsing top-level foreach's.
    std::unique_ptr<Record> ParseRecTmp;
    Record *ParseRec = CurRec;
    if (!ParseRec) {
      ParseRecTmp = make_unique<Record>(".parse", ArrayRef<SMLoc>{}, Records);
      ParseRec = ParseRecTmp.get();
    }

    ParseRec->addValue(RecordVal(LHS, InEltType, false));
    Init *RHS = ParseValue(ParseRec, OutEltType);
    ParseRec->removeValue(LHS);
    if (!RHS)
      return nullptr;

    if (Lex.getCode() != tgtok::r_paren) {
      TokError("expected ')' in binary operator");
      return nullptr;
    }
    Lex.Lex();  // eat the ')'

    RecTy *OutType;
    if (IsDAG) {
      OutType = InEltType;
    } else {
      TypedInit *RHSt = dyn_cast<TypedInit>(RHS);
      if (!RHSt) {
        TokError("could not get type of !foreach result");
        return nullptr;
      }
      OutType = RHSt->getType()->getListTy();
    }

    return (TernOpInit::get(TernOpInit::FOREACH, LHS, MHS, RHS, OutType))
        ->Fold(CurRec);
  }

  case tgtok::XDag:
  case tgtok::XIf:
  case tgtok::XSubst: {  // Value ::= !ternop '(' Value ',' Value ',' Value ')'
    TernOpInit::TernaryOp Code;
    RecTy *Type = nullptr;

    tgtok::TokKind LexCode = Lex.getCode();
    Lex.Lex();  // eat the operation
    switch (LexCode) {
    default: llvm_unreachable("Unhandled code!");
    case tgtok::XDag:
      Code = TernOpInit::DAG;
      Type = DagRecTy::get();
      ItemType = nullptr;
      break;
    case tgtok::XIf:
      Code = TernOpInit::IF;
      break;
    case tgtok::XSubst:
      Code = TernOpInit::SUBST;
      break;
    }
    if (Lex.getCode() != tgtok::l_paren) {
      TokError("expected '(' after ternary operator");
      return nullptr;
    }
    Lex.Lex();  // eat the '('

    Init *LHS = ParseValue(CurRec);
    if (!LHS) return nullptr;

    if (Lex.getCode() != tgtok::comma) {
      TokError("expected ',' in ternary operator");
      return nullptr;
    }
    Lex.Lex();  // eat the ','

    SMLoc MHSLoc = Lex.getLoc();
    Init *MHS = ParseValue(CurRec, ItemType);
    if (!MHS)
      return nullptr;

    if (Lex.getCode() != tgtok::comma) {
      TokError("expected ',' in ternary operator");
      return nullptr;
    }
    Lex.Lex();  // eat the ','

    SMLoc RHSLoc = Lex.getLoc();
    Init *RHS = ParseValue(CurRec, ItemType);
    if (!RHS)
      return nullptr;

    if (Lex.getCode() != tgtok::r_paren) {
      TokError("expected ')' in binary operator");
      return nullptr;
    }
    Lex.Lex();  // eat the ')'

    switch (LexCode) {
    default: llvm_unreachable("Unhandled code!");
    case tgtok::XDag: {
      TypedInit *MHSt = dyn_cast<TypedInit>(MHS);
      if (!MHSt && !isa<UnsetInit>(MHS)) {
        Error(MHSLoc, "could not determine type of the child list in !dag");
        return nullptr;
      }
      if (MHSt && !isa<ListRecTy>(MHSt->getType())) {
        Error(MHSLoc, Twine("expected list of children, got type '") +
                          MHSt->getType()->getAsString() + "'");
        return nullptr;
      }

      TypedInit *RHSt = dyn_cast<TypedInit>(RHS);
      if (!RHSt && !isa<UnsetInit>(RHS)) {
        Error(RHSLoc, "could not determine type of the name list in !dag");
        return nullptr;
      }
      if (RHSt && StringRecTy::get()->getListTy() != RHSt->getType()) {
        Error(RHSLoc, Twine("expected list<string>, got type '") +
                          RHSt->getType()->getAsString() + "'");
        return nullptr;
      }

      if (!MHSt && !RHSt) {
        Error(MHSLoc,
              "cannot have both unset children and unset names in !dag");
        return nullptr;
      }
      break;
    }
    case tgtok::XIf: {
      RecTy *MHSTy = nullptr;
      RecTy *RHSTy = nullptr;

      if (TypedInit *MHSt = dyn_cast<TypedInit>(MHS))
        MHSTy = MHSt->getType();
      if (BitsInit *MHSbits = dyn_cast<BitsInit>(MHS))
        MHSTy = BitsRecTy::get(MHSbits->getNumBits());
      if (isa<BitInit>(MHS))
        MHSTy = BitRecTy::get();

      if (TypedInit *RHSt = dyn_cast<TypedInit>(RHS))
        RHSTy = RHSt->getType();
      if (BitsInit *RHSbits = dyn_cast<BitsInit>(RHS))
        RHSTy = BitsRecTy::get(RHSbits->getNumBits());
      if (isa<BitInit>(RHS))
        RHSTy = BitRecTy::get();

      // For UnsetInit, it's typed from the other hand.
      if (isa<UnsetInit>(MHS))
        MHSTy = RHSTy;
      if (isa<UnsetInit>(RHS))
        RHSTy = MHSTy;

      if (!MHSTy || !RHSTy) {
        TokError("could not get type for !if");
        return nullptr;
      }

      Type = resolveTypes(MHSTy, RHSTy);
      if (!Type) {
        TokError(Twine("inconsistent types '") + MHSTy->getAsString() +
                 "' and '" + RHSTy->getAsString() + "' for !if");
        return nullptr;
      }
      break;
    }
    case tgtok::XSubst: {
      TypedInit *RHSt = dyn_cast<TypedInit>(RHS);
      if (!RHSt) {
        TokError("could not get type for !subst");
        return nullptr;
      }
      Type = RHSt->getType();
      break;
    }
    }
    return (TernOpInit::get(Code, LHS, MHS, RHS, Type))->Fold(CurRec);
  }

  case tgtok::XFoldl: {
    // Value ::= !foldl '(' Id ',' Id ',' Value ',' Value ',' Value ')'
    Lex.Lex(); // eat the operation
    if (Lex.getCode() != tgtok::l_paren) {
      TokError("expected '(' after !foldl");
      return nullptr;
    }
    Lex.Lex(); // eat the '('

    Init *StartUntyped = ParseValue(CurRec);
    if (!StartUntyped)
      return nullptr;

    TypedInit *Start = dyn_cast<TypedInit>(StartUntyped);
    if (!Start) {
      TokError(Twine("could not get type of !foldl start: '") +
               StartUntyped->getAsString() + "'");
      return nullptr;
    }

    if (Lex.getCode() != tgtok::comma) {
      TokError("expected ',' in !foldl");
      return nullptr;
    }
    Lex.Lex(); // eat the ','

    Init *ListUntyped = ParseValue(CurRec);
    if (!ListUntyped)
      return nullptr;

    TypedInit *List = dyn_cast<TypedInit>(ListUntyped);
    if (!List) {
      TokError(Twine("could not get type of !foldl list: '") +
               ListUntyped->getAsString() + "'");
      return nullptr;
    }

    ListRecTy *ListType = dyn_cast<ListRecTy>(List->getType());
    if (!ListType) {
      TokError(Twine("!foldl list must be a list, but is of type '") +
               List->getType()->getAsString());
      return nullptr;
    }

    if (Lex.getCode() != tgtok::comma) {
      TokError("expected ',' in !foldl");
      return nullptr;
    }

    if (Lex.Lex() != tgtok::Id) { // eat the ','
      TokError("third argument of !foldl must be an identifier");
      return nullptr;
    }

    Init *A = StringInit::get(Lex.getCurStrVal());
    if (CurRec && CurRec->getValue(A)) {
      TokError((Twine("left !foldl variable '") + A->getAsString() +
                "' already defined")
                   .str());
      return nullptr;
    }

    if (Lex.Lex() != tgtok::comma) { // eat the id
      TokError("expected ',' in !foldl");
      return nullptr;
    }

    if (Lex.Lex() != tgtok::Id) { // eat the ','
      TokError("fourth argument of !foldl must be an identifier");
      return nullptr;
    }

    Init *B = StringInit::get(Lex.getCurStrVal());
    if (CurRec && CurRec->getValue(B)) {
      TokError((Twine("right !foldl variable '") + B->getAsString() +
                "' already defined")
                   .str());
      return nullptr;
    }

    if (Lex.Lex() != tgtok::comma) { // eat the id
      TokError("expected ',' in !foldl");
      return nullptr;
    }
    Lex.Lex(); // eat the ','

    // We need to create a temporary record to provide a scope for the iteration
    // variable while parsing top-level foreach's.
    std::unique_ptr<Record> ParseRecTmp;
    Record *ParseRec = CurRec;
    if (!ParseRec) {
      ParseRecTmp = make_unique<Record>(".parse", ArrayRef<SMLoc>{}, Records);
      ParseRec = ParseRecTmp.get();
    }

    ParseRec->addValue(RecordVal(A, Start->getType(), false));
    ParseRec->addValue(RecordVal(B, ListType->getElementType(), false));
    Init *ExprUntyped = ParseValue(ParseRec);
    ParseRec->removeValue(A);
    ParseRec->removeValue(B);
    if (!ExprUntyped)
      return nullptr;

    TypedInit *Expr = dyn_cast<TypedInit>(ExprUntyped);
    if (!Expr) {
      TokError("could not get type of !foldl expression");
      return nullptr;
    }

    if (Expr->getType() != Start->getType()) {
      TokError(Twine("!foldl expression must be of same type as start (") +
               Start->getType()->getAsString() + "), but is of type " +
               Expr->getType()->getAsString());
      return nullptr;
    }

    if (Lex.getCode() != tgtok::r_paren) {
      TokError("expected ')' in fold operator");
      return nullptr;
    }
    Lex.Lex(); // eat the ')'

    return FoldOpInit::get(Start, List, A, B, Expr, Start->getType())
        ->Fold(CurRec);
  }
  }
}

/// ParseOperatorType - Parse a type for an operator.  This returns
/// null on error.
///
/// OperatorType ::= '<' Type '>'
///
RecTy *TGParser::ParseOperatorType() {
  RecTy *Type = nullptr;

  if (Lex.getCode() != tgtok::less) {
    TokError("expected type name for operator");
    return nullptr;
  }
  Lex.Lex();  // eat the <

  Type = ParseType();

  if (!Type) {
    TokError("expected type name for operator");
    return nullptr;
  }

  if (Lex.getCode() != tgtok::greater) {
    TokError("expected type name for operator");
    return nullptr;
  }
  Lex.Lex();  // eat the >

  return Type;
}

/// ParseSimpleValue - Parse a tblgen value.  This returns null on error.
///
///   SimpleValue ::= IDValue
///   SimpleValue ::= INTVAL
///   SimpleValue ::= STRVAL+
///   SimpleValue ::= CODEFRAGMENT
///   SimpleValue ::= '?'
///   SimpleValue ::= '{' ValueList '}'
///   SimpleValue ::= ID '<' ValueListNE '>'
///   SimpleValue ::= '[' ValueList ']'
///   SimpleValue ::= '(' IDValue DagArgList ')'
///   SimpleValue ::= CONCATTOK '(' Value ',' Value ')'
///   SimpleValue ::= ADDTOK '(' Value ',' Value ')'
///   SimpleValue ::= SHLTOK '(' Value ',' Value ')'
///   SimpleValue ::= SRATOK '(' Value ',' Value ')'
///   SimpleValue ::= SRLTOK '(' Value ',' Value ')'
///   SimpleValue ::= LISTCONCATTOK '(' Value ',' Value ')'
///   SimpleValue ::= STRCONCATTOK '(' Value ',' Value ')'
///
Init *TGParser::ParseSimpleValue(Record *CurRec, RecTy *ItemType,
                                 IDParseMode Mode) {
  Init *R = nullptr;
  switch (Lex.getCode()) {
  default: TokError("Unknown token when parsing a value"); break;
  case tgtok::paste:
    // This is a leading paste operation.  This is deprecated but
    // still exists in some .td files.  Ignore it.
    Lex.Lex();  // Skip '#'.
    return ParseSimpleValue(CurRec, ItemType, Mode);
  case tgtok::IntVal: R = IntInit::get(Lex.getCurIntVal()); Lex.Lex(); break;
  case tgtok::BinaryIntVal: {
    auto BinaryVal = Lex.getCurBinaryIntVal();
    SmallVector<Init*, 16> Bits(BinaryVal.second);
    for (unsigned i = 0, e = BinaryVal.second; i != e; ++i)
      Bits[i] = BitInit::get(BinaryVal.first & (1LL << i));
    R = BitsInit::get(Bits);
    Lex.Lex();
    break;
  }
  case tgtok::StrVal: {
    std::string Val = Lex.getCurStrVal();
    Lex.Lex();

    // Handle multiple consecutive concatenated strings.
    while (Lex.getCode() == tgtok::StrVal) {
      Val += Lex.getCurStrVal();
      Lex.Lex();
    }

    R = StringInit::get(Val);
    break;
  }
  case tgtok::CodeFragment:
    R = CodeInit::get(Lex.getCurStrVal());
    Lex.Lex();
    break;
  case tgtok::question:
    R = UnsetInit::get();
    Lex.Lex();
    break;
  case tgtok::Id: {
    SMLoc NameLoc = Lex.getLoc();
    StringInit *Name = StringInit::get(Lex.getCurStrVal());
    if (Lex.Lex() != tgtok::less)  // consume the Id.
      return ParseIDValue(CurRec, Name, NameLoc, Mode);    // Value ::= IDValue

    // Value ::= ID '<' ValueListNE '>'
    if (Lex.Lex() == tgtok::greater) {
      TokError("expected non-empty value list");
      return nullptr;
    }

    // This is a CLASS<initvalslist> expression.  This is supposed to synthesize
    // a new anonymous definition, deriving from CLASS<initvalslist> with no
    // body.
    Record *Class = Records.getClass(Name->getValue());
    if (!Class) {
      Error(NameLoc, "Expected a class name, got '" + Name->getValue() + "'");
      return nullptr;
    }

    SmallVector<Init *, 8> Args;
    ParseValueList(Args, CurRec, Class);
    if (Args.empty()) return nullptr;

    if (Lex.getCode() != tgtok::greater) {
      TokError("expected '>' at end of value list");
      return nullptr;
    }
    Lex.Lex();  // eat the '>'

    // Typecheck the template arguments list
    ArrayRef<Init *> ExpectedArgs = Class->getTemplateArgs();
    if (ExpectedArgs.size() < Args.size()) {
      Error(NameLoc,
            "More template args specified than expected");
      return nullptr;
    }

    for (unsigned i = 0, e = ExpectedArgs.size(); i != e; ++i) {
      RecordVal *ExpectedArg = Class->getValue(ExpectedArgs[i]);
      if (i < Args.size()) {
        if (TypedInit *TI = dyn_cast<TypedInit>(Args[i])) {
          RecTy *ExpectedType = ExpectedArg->getType();
          if (!TI->getType()->typeIsConvertibleTo(ExpectedType)) {
            Error(NameLoc,
                  "Value specified for template argument #" + Twine(i) + " (" +
                  ExpectedArg->getNameInitAsString() + ") is of type '" +
                  TI->getType()->getAsString() + "', expected '" +
                  ExpectedType->getAsString() + "': " + TI->getAsString());
            return nullptr;
          }
          continue;
        }
      } else if (ExpectedArg->getValue()->isComplete())
        continue;

      Error(NameLoc,
            "Value not specified for template argument #" + Twine(i) + " (" +
            ExpectedArgs[i]->getAsUnquotedString() + ")");
      return nullptr;
    }

    return VarDefInit::get(Class, Args)->Fold();
  }
  case tgtok::l_brace: {           // Value ::= '{' ValueList '}'
    SMLoc BraceLoc = Lex.getLoc();
    Lex.Lex(); // eat the '{'
    SmallVector<Init*, 16> Vals;

    if (Lex.getCode() != tgtok::r_brace) {
      ParseValueList(Vals, CurRec);
      if (Vals.empty()) return nullptr;
    }
    if (Lex.getCode() != tgtok::r_brace) {
      TokError("expected '}' at end of bit list value");
      return nullptr;
    }
    Lex.Lex();  // eat the '}'

    SmallVector<Init *, 16> NewBits;

    // As we parse { a, b, ... }, 'a' is the highest bit, but we parse it
    // first.  We'll first read everything in to a vector, then we can reverse
    // it to get the bits in the correct order for the BitsInit value.
    for (unsigned i = 0, e = Vals.size(); i != e; ++i) {
      // FIXME: The following two loops would not be duplicated
      //        if the API was a little more orthogonal.

      // bits<n> values are allowed to initialize n bits.
      if (BitsInit *BI = dyn_cast<BitsInit>(Vals[i])) {
        for (unsigned i = 0, e = BI->getNumBits(); i != e; ++i)
          NewBits.push_back(BI->getBit((e - i) - 1));
        continue;
      }
      // bits<n> can also come from variable initializers.
      if (VarInit *VI = dyn_cast<VarInit>(Vals[i])) {
        if (BitsRecTy *BitsRec = dyn_cast<BitsRecTy>(VI->getType())) {
          for (unsigned i = 0, e = BitsRec->getNumBits(); i != e; ++i)
            NewBits.push_back(VI->getBit((e - i) - 1));
          continue;
        }
        // Fallthrough to try convert this to a bit.
      }
      // All other values must be convertible to just a single bit.
      Init *Bit = Vals[i]->getCastTo(BitRecTy::get());
      if (!Bit) {
        Error(BraceLoc, "Element #" + Twine(i) + " (" + Vals[i]->getAsString() +
              ") is not convertable to a bit");
        return nullptr;
      }
      NewBits.push_back(Bit);
    }
    std::reverse(NewBits.begin(), NewBits.end());
    return BitsInit::get(NewBits);
  }
  case tgtok::l_square: {          // Value ::= '[' ValueList ']'
    Lex.Lex(); // eat the '['
    SmallVector<Init*, 16> Vals;

    RecTy *DeducedEltTy = nullptr;
    ListRecTy *GivenListTy = nullptr;

    if (ItemType) {
      ListRecTy *ListType = dyn_cast<ListRecTy>(ItemType);
      if (!ListType) {
        TokError(Twine("Type mismatch for list, expected list type, got ") +
                 ItemType->getAsString());
        return nullptr;
      }
      GivenListTy = ListType;
    }

    if (Lex.getCode() != tgtok::r_square) {
      ParseValueList(Vals, CurRec, nullptr,
                     GivenListTy ? GivenListTy->getElementType() : nullptr);
      if (Vals.empty()) return nullptr;
    }
    if (Lex.getCode() != tgtok::r_square) {
      TokError("expected ']' at end of list value");
      return nullptr;
    }
    Lex.Lex();  // eat the ']'

    RecTy *GivenEltTy = nullptr;
    if (Lex.getCode() == tgtok::less) {
      // Optional list element type
      Lex.Lex();  // eat the '<'

      GivenEltTy = ParseType();
      if (!GivenEltTy) {
        // Couldn't parse element type
        return nullptr;
      }

      if (Lex.getCode() != tgtok::greater) {
        TokError("expected '>' at end of list element type");
        return nullptr;
      }
      Lex.Lex();  // eat the '>'
    }

    // Check elements
    RecTy *EltTy = nullptr;
    for (Init *V : Vals) {
      TypedInit *TArg = dyn_cast<TypedInit>(V);
      if (TArg) {
        if (EltTy) {
          EltTy = resolveTypes(EltTy, TArg->getType());
          if (!EltTy) {
            TokError("Incompatible types in list elements");
            return nullptr;
          }
        } else {
          EltTy = TArg->getType();
        }
      }
    }

    if (GivenEltTy) {
      if (EltTy) {
        // Verify consistency
        if (!EltTy->typeIsConvertibleTo(GivenEltTy)) {
          TokError("Incompatible types in list elements");
          return nullptr;
        }
      }
      EltTy = GivenEltTy;
    }

    if (!EltTy) {
      if (!ItemType) {
        TokError("No type for list");
        return nullptr;
      }
      DeducedEltTy = GivenListTy->getElementType();
    } else {
      // Make sure the deduced type is compatible with the given type
      if (GivenListTy) {
        if (!EltTy->typeIsConvertibleTo(GivenListTy->getElementType())) {
          TokError(Twine("Element type mismatch for list: element type '") +
                   EltTy->getAsString() + "' not convertible to '" +
                   GivenListTy->getElementType()->getAsString());
          return nullptr;
        }
      }
      DeducedEltTy = EltTy;
    }

    return ListInit::get(Vals, DeducedEltTy);
  }
  case tgtok::l_paren: {         // Value ::= '(' IDValue DagArgList ')'
    Lex.Lex();   // eat the '('
    if (Lex.getCode() != tgtok::Id && Lex.getCode() != tgtok::XCast) {
      TokError("expected identifier in dag init");
      return nullptr;
    }

    Init *Operator = ParseValue(CurRec);
    if (!Operator) return nullptr;

    // If the operator name is present, parse it.
    StringInit *OperatorName = nullptr;
    if (Lex.getCode() == tgtok::colon) {
      if (Lex.Lex() != tgtok::VarName) { // eat the ':'
        TokError("expected variable name in dag operator");
        return nullptr;
      }
      OperatorName = StringInit::get(Lex.getCurStrVal());
      Lex.Lex();  // eat the VarName.
    }

    SmallVector<std::pair<llvm::Init*, StringInit*>, 8> DagArgs;
    if (Lex.getCode() != tgtok::r_paren) {
      ParseDagArgList(DagArgs, CurRec);
      if (DagArgs.empty()) return nullptr;
    }

    if (Lex.getCode() != tgtok::r_paren) {
      TokError("expected ')' in dag init");
      return nullptr;
    }
    Lex.Lex();  // eat the ')'

    return DagInit::get(Operator, OperatorName, DagArgs);
  }

  case tgtok::XHead:
  case tgtok::XTail:
  case tgtok::XSize:
  case tgtok::XEmpty:
  case tgtok::XCast:  // Value ::= !unop '(' Value ')'
  case tgtok::XIsA:
  case tgtok::XConcat:
  case tgtok::XDag:
  case tgtok::XADD:
  case tgtok::XAND:
  case tgtok::XOR:
  case tgtok::XSRA:
  case tgtok::XSRL:
  case tgtok::XSHL:
  case tgtok::XEq:
  case tgtok::XNe:
  case tgtok::XLe:
  case tgtok::XLt:
  case tgtok::XGe:
  case tgtok::XGt:
  case tgtok::XListConcat:
  case tgtok::XStrConcat:   // Value ::= !binop '(' Value ',' Value ')'
  case tgtok::XIf:
  case tgtok::XFoldl:
  case tgtok::XForEach:
  case tgtok::XSubst: {  // Value ::= !ternop '(' Value ',' Value ',' Value ')'
    return ParseOperation(CurRec, ItemType);
  }
  }

  return R;
}

/// ParseValue - Parse a tblgen value.  This returns null on error.
///
///   Value       ::= SimpleValue ValueSuffix*
///   ValueSuffix ::= '{' BitList '}'
///   ValueSuffix ::= '[' BitList ']'
///   ValueSuffix ::= '.' ID
///
Init *TGParser::ParseValue(Record *CurRec, RecTy *ItemType, IDParseMode Mode) {
  Init *Result = ParseSimpleValue(CurRec, ItemType, Mode);
  if (!Result) return nullptr;

  // Parse the suffixes now if present.
  while (true) {
    switch (Lex.getCode()) {
    default: return Result;
    case tgtok::l_brace: {
      if (Mode == ParseNameMode)
        // This is the beginning of the object body.
        return Result;

      SMLoc CurlyLoc = Lex.getLoc();
      Lex.Lex(); // eat the '{'
      SmallVector<unsigned, 16> Ranges;
      ParseRangeList(Ranges);
      if (Ranges.empty()) return nullptr;

      // Reverse the bitlist.
      std::reverse(Ranges.begin(), Ranges.end());
      Result = Result->convertInitializerBitRange(Ranges);
      if (!Result) {
        Error(CurlyLoc, "Invalid bit range for value");
        return nullptr;
      }

      // Eat the '}'.
      if (Lex.getCode() != tgtok::r_brace) {
        TokError("expected '}' at end of bit range list");
        return nullptr;
      }
      Lex.Lex();
      break;
    }
    case tgtok::l_square: {
      SMLoc SquareLoc = Lex.getLoc();
      Lex.Lex(); // eat the '['
      SmallVector<unsigned, 16> Ranges;
      ParseRangeList(Ranges);
      if (Ranges.empty()) return nullptr;

      Result = Result->convertInitListSlice(Ranges);
      if (!Result) {
        Error(SquareLoc, "Invalid range for list slice");
        return nullptr;
      }

      // Eat the ']'.
      if (Lex.getCode() != tgtok::r_square) {
        TokError("expected ']' at end of list slice");
        return nullptr;
      }
      Lex.Lex();
      break;
    }
    case tgtok::period: {
      if (Lex.Lex() != tgtok::Id) {  // eat the .
        TokError("expected field identifier after '.'");
        return nullptr;
      }
      StringInit *FieldName = StringInit::get(Lex.getCurStrVal());
      if (!Result->getFieldType(FieldName)) {
        TokError("Cannot access field '" + Lex.getCurStrVal() + "' of value '" +
                 Result->getAsString() + "'");
        return nullptr;
      }
      Result = FieldInit::get(Result, FieldName)->Fold(CurRec);
      Lex.Lex();  // eat field name
      break;
    }

    case tgtok::paste:
      SMLoc PasteLoc = Lex.getLoc();

      // Create a !strconcat() operation, first casting each operand to
      // a string if necessary.

      TypedInit *LHS = dyn_cast<TypedInit>(Result);
      if (!LHS) {
        Error(PasteLoc, "LHS of paste is not typed!");
        return nullptr;
      }

      if (LHS->getType() != StringRecTy::get()) {
        LHS = dyn_cast<TypedInit>(
            UnOpInit::get(UnOpInit::CAST, LHS, StringRecTy::get())
                ->Fold(CurRec));
        if (!LHS) {
          Error(PasteLoc, Twine("can't cast '") + LHS->getAsString() +
                              "' to string");
          return nullptr;
        }
      }

      TypedInit *RHS = nullptr;

      Lex.Lex();  // Eat the '#'.
      switch (Lex.getCode()) {
      case tgtok::colon:
      case tgtok::semi:
      case tgtok::l_brace:
        // These are all of the tokens that can begin an object body.
        // Some of these can also begin values but we disallow those cases
        // because they are unlikely to be useful.

        // Trailing paste, concat with an empty string.
        RHS = StringInit::get("");
        break;

      default:
        Init *RHSResult = ParseValue(CurRec, nullptr, ParseNameMode);
        RHS = dyn_cast<TypedInit>(RHSResult);
        if (!RHS) {
          Error(PasteLoc, "RHS of paste is not typed!");
          return nullptr;
        }

        if (RHS->getType() != StringRecTy::get()) {
          RHS = dyn_cast<TypedInit>(
              UnOpInit::get(UnOpInit::CAST, RHS, StringRecTy::get())
                  ->Fold(CurRec));
          if (!RHS) {
            Error(PasteLoc, Twine("can't cast '") + RHS->getAsString() +
                                "' to string");
            return nullptr;
          }
        }

        break;
      }

      Result = BinOpInit::getStrConcat(LHS, RHS);
      break;
    }
  }
}

/// ParseDagArgList - Parse the argument list for a dag literal expression.
///
///    DagArg     ::= Value (':' VARNAME)?
///    DagArg     ::= VARNAME
///    DagArgList ::= DagArg
///    DagArgList ::= DagArgList ',' DagArg
void TGParser::ParseDagArgList(
    SmallVectorImpl<std::pair<llvm::Init*, StringInit*>> &Result,
    Record *CurRec) {

  while (true) {
    // DagArg ::= VARNAME
    if (Lex.getCode() == tgtok::VarName) {
      // A missing value is treated like '?'.
      StringInit *VarName = StringInit::get(Lex.getCurStrVal());
      Result.emplace_back(UnsetInit::get(), VarName);
      Lex.Lex();
    } else {
      // DagArg ::= Value (':' VARNAME)?
      Init *Val = ParseValue(CurRec);
      if (!Val) {
        Result.clear();
        return;
      }

      // If the variable name is present, add it.
      StringInit *VarName = nullptr;
      if (Lex.getCode() == tgtok::colon) {
        if (Lex.Lex() != tgtok::VarName) { // eat the ':'
          TokError("expected variable name in dag literal");
          Result.clear();
          return;
        }
        VarName = StringInit::get(Lex.getCurStrVal());
        Lex.Lex();  // eat the VarName.
      }

      Result.push_back(std::make_pair(Val, VarName));
    }
    if (Lex.getCode() != tgtok::comma) break;
    Lex.Lex(); // eat the ','
  }
}

/// ParseValueList - Parse a comma separated list of values, returning them as a
/// vector.  Note that this always expects to be able to parse at least one
/// value.  It returns an empty list if this is not possible.
///
///   ValueList ::= Value (',' Value)
///
void TGParser::ParseValueList(SmallVectorImpl<Init*> &Result, Record *CurRec,
                              Record *ArgsRec, RecTy *EltTy) {
  RecTy *ItemType = EltTy;
  unsigned int ArgN = 0;
  if (ArgsRec && !EltTy) {
    ArrayRef<Init *> TArgs = ArgsRec->getTemplateArgs();
    if (TArgs.empty()) {
      TokError("template argument provided to non-template class");
      Result.clear();
      return;
    }
    const RecordVal *RV = ArgsRec->getValue(TArgs[ArgN]);
    if (!RV) {
      errs() << "Cannot find template arg " << ArgN << " (" << TArgs[ArgN]
        << ")\n";
    }
    assert(RV && "Template argument record not found??");
    ItemType = RV->getType();
    ++ArgN;
  }
  Result.push_back(ParseValue(CurRec, ItemType));
  if (!Result.back()) {
    Result.clear();
    return;
  }

  while (Lex.getCode() == tgtok::comma) {
    Lex.Lex();  // Eat the comma

    if (ArgsRec && !EltTy) {
      ArrayRef<Init *> TArgs = ArgsRec->getTemplateArgs();
      if (ArgN >= TArgs.size()) {
        TokError("too many template arguments");
        Result.clear();
        return;
      }
      const RecordVal *RV = ArgsRec->getValue(TArgs[ArgN]);
      assert(RV && "Template argument record not found??");
      ItemType = RV->getType();
      ++ArgN;
    }
    Result.push_back(ParseValue(CurRec, ItemType));
    if (!Result.back()) {
      Result.clear();
      return;
    }
  }
}

/// ParseDeclaration - Read a declaration, returning the name of field ID, or an
/// empty string on error.  This can happen in a number of different context's,
/// including within a def or in the template args for a def (which which case
/// CurRec will be non-null) and within the template args for a multiclass (in
/// which case CurRec will be null, but CurMultiClass will be set).  This can
/// also happen within a def that is within a multiclass, which will set both
/// CurRec and CurMultiClass.
///
///  Declaration ::= FIELD? Type ID ('=' Value)?
///
Init *TGParser::ParseDeclaration(Record *CurRec,
                                       bool ParsingTemplateArgs) {
  // Read the field prefix if present.
  bool HasField = Lex.getCode() == tgtok::Field;
  if (HasField) Lex.Lex();

  RecTy *Type = ParseType();
  if (!Type) return nullptr;

  if (Lex.getCode() != tgtok::Id) {
    TokError("Expected identifier in declaration");
    return nullptr;
  }

  std::string Str = Lex.getCurStrVal();
  if (Str == "NAME") {
    TokError("'" + Str + "' is a reserved variable name");
    return nullptr;
  }

  SMLoc IdLoc = Lex.getLoc();
  Init *DeclName = StringInit::get(Str);
  Lex.Lex();

  if (ParsingTemplateArgs) {
    if (CurRec)
      DeclName = QualifyName(*CurRec, CurMultiClass, DeclName, ":");
    else
      assert(CurMultiClass);
    if (CurMultiClass)
      DeclName = QualifyName(CurMultiClass->Rec, CurMultiClass, DeclName,
                             "::");
  }

  // Add the value.
  if (AddValue(CurRec, IdLoc, RecordVal(DeclName, Type, HasField)))
    return nullptr;

  // If a value is present, parse it.
  if (Lex.getCode() == tgtok::equal) {
    Lex.Lex();
    SMLoc ValLoc = Lex.getLoc();
    Init *Val = ParseValue(CurRec, Type);
    if (!Val ||
        SetValue(CurRec, ValLoc, DeclName, None, Val))
      // Return the name, even if an error is thrown.  This is so that we can
      // continue to make some progress, even without the value having been
      // initialized.
      return DeclName;
  }

  return DeclName;
}

/// ParseForeachDeclaration - Read a foreach declaration, returning
/// the name of the declared object or a NULL Init on error.  Return
/// the name of the parsed initializer list through ForeachListName.
///
///  ForeachDeclaration ::= ID '=' '{' RangeList '}'
///  ForeachDeclaration ::= ID '=' RangePiece
///  ForeachDeclaration ::= ID '=' Value
///
VarInit *TGParser::ParseForeachDeclaration(Init *&ForeachListValue) {
  if (Lex.getCode() != tgtok::Id) {
    TokError("Expected identifier in foreach declaration");
    return nullptr;
  }

  Init *DeclName = StringInit::get(Lex.getCurStrVal());
  Lex.Lex();

  // If a value is present, parse it.
  if (Lex.getCode() != tgtok::equal) {
    TokError("Expected '=' in foreach declaration");
    return nullptr;
  }
  Lex.Lex();  // Eat the '='

  RecTy *IterType = nullptr;
  SmallVector<unsigned, 16> Ranges;

  switch (Lex.getCode()) {
  case tgtok::IntVal: { // RangePiece.
    if (ParseRangePiece(Ranges))
      return nullptr;
    break;
  }

  case tgtok::l_brace: { // '{' RangeList '}'
    Lex.Lex(); // eat the '{'
    ParseRangeList(Ranges);
    if (Lex.getCode() != tgtok::r_brace) {
      TokError("expected '}' at end of bit range list");
      return nullptr;
    }
    Lex.Lex();
    break;
  }

  default: {
    SMLoc ValueLoc = Lex.getLoc();
    Init *I = ParseValue(nullptr);
    TypedInit *TI = dyn_cast<TypedInit>(I);
    if (!TI || !isa<ListRecTy>(TI->getType())) {
      std::string Type;
      if (TI)
        Type = (Twine("' of type '") + TI->getType()->getAsString()).str();
      Error(ValueLoc, "expected a list, got '" + I->getAsString() + Type + "'");
      if (CurMultiClass)
        PrintNote({}, "references to multiclass template arguments cannot be "
                      "resolved at this time");
      return nullptr;
    }
    ForeachListValue = I;
    IterType = cast<ListRecTy>(TI->getType())->getElementType();
    break;
  }
  }

  if (!Ranges.empty()) {
    assert(!IterType && "Type already initialized?");
    IterType = IntRecTy::get();
    std::vector<Init*> Values;
    for (unsigned R : Ranges)
      Values.push_back(IntInit::get(R));
    ForeachListValue = ListInit::get(Values, IterType);
  }

  if (!IterType)
    return nullptr;

  return VarInit::get(DeclName, IterType);
}

/// ParseTemplateArgList - Read a template argument list, which is a non-empty
/// sequence of template-declarations in <>'s.  If CurRec is non-null, these are
/// template args for a def, which may or may not be in a multiclass.  If null,
/// these are the template args for a multiclass.
///
///    TemplateArgList ::= '<' Declaration (',' Declaration)* '>'
///
bool TGParser::ParseTemplateArgList(Record *CurRec) {
  assert(Lex.getCode() == tgtok::less && "Not a template arg list!");
  Lex.Lex(); // eat the '<'

  Record *TheRecToAddTo = CurRec ? CurRec : &CurMultiClass->Rec;

  // Read the first declaration.
  Init *TemplArg = ParseDeclaration(CurRec, true/*templateargs*/);
  if (!TemplArg)
    return true;

  TheRecToAddTo->addTemplateArg(TemplArg);

  while (Lex.getCode() == tgtok::comma) {
    Lex.Lex(); // eat the ','

    // Read the following declarations.
    SMLoc Loc = Lex.getLoc();
    TemplArg = ParseDeclaration(CurRec, true/*templateargs*/);
    if (!TemplArg)
      return true;

    if (TheRecToAddTo->isTemplateArg(TemplArg))
      return Error(Loc, "template argument with the same name has already been "
                        "defined");

    TheRecToAddTo->addTemplateArg(TemplArg);
  }

  if (Lex.getCode() != tgtok::greater)
    return TokError("expected '>' at end of template argument list");
  Lex.Lex(); // eat the '>'.
  return false;
}

/// ParseBodyItem - Parse a single item at within the body of a def or class.
///
///   BodyItem ::= Declaration ';'
///   BodyItem ::= LET ID OptionalBitList '=' Value ';'
bool TGParser::ParseBodyItem(Record *CurRec) {
  if (Lex.getCode() != tgtok::Let) {
    if (!ParseDeclaration(CurRec, false))
      return true;

    if (Lex.getCode() != tgtok::semi)
      return TokError("expected ';' after declaration");
    Lex.Lex();
    return false;
  }

  // LET ID OptionalRangeList '=' Value ';'
  if (Lex.Lex() != tgtok::Id)
    return TokError("expected field identifier after let");

  SMLoc IdLoc = Lex.getLoc();
  StringInit *FieldName = StringInit::get(Lex.getCurStrVal());
  Lex.Lex();  // eat the field name.

  SmallVector<unsigned, 16> BitList;
  if (ParseOptionalBitList(BitList))
    return true;
  std::reverse(BitList.begin(), BitList.end());

  if (Lex.getCode() != tgtok::equal)
    return TokError("expected '=' in let expression");
  Lex.Lex();  // eat the '='.

  RecordVal *Field = CurRec->getValue(FieldName);
  if (!Field)
    return TokError("Value '" + FieldName->getValue() + "' unknown!");

  RecTy *Type = Field->getType();

  Init *Val = ParseValue(CurRec, Type);
  if (!Val) return true;

  if (Lex.getCode() != tgtok::semi)
    return TokError("expected ';' after let expression");
  Lex.Lex();

  return SetValue(CurRec, IdLoc, FieldName, BitList, Val);
}

/// ParseBody - Read the body of a class or def.  Return true on error, false on
/// success.
///
///   Body     ::= ';'
///   Body     ::= '{' BodyList '}'
///   BodyList BodyItem*
///
bool TGParser::ParseBody(Record *CurRec) {
  // If this is a null definition, just eat the semi and return.
  if (Lex.getCode() == tgtok::semi) {
    Lex.Lex();
    return false;
  }

  if (Lex.getCode() != tgtok::l_brace)
    return TokError("Expected ';' or '{' to start body");
  // Eat the '{'.
  Lex.Lex();

  while (Lex.getCode() != tgtok::r_brace)
    if (ParseBodyItem(CurRec))
      return true;

  // Eat the '}'.
  Lex.Lex();
  return false;
}

/// Apply the current let bindings to \a CurRec.
/// \returns true on error, false otherwise.
bool TGParser::ApplyLetStack(Record *CurRec) {
  for (SmallVectorImpl<LetRecord> &LetInfo : LetStack)
    for (LetRecord &LR : LetInfo)
      if (SetValue(CurRec, LR.Loc, LR.Name, LR.Bits, LR.Value))
        return true;
  return false;
}

bool TGParser::ApplyLetStack(RecordsEntry &Entry) {
  if (Entry.Rec)
    return ApplyLetStack(Entry.Rec.get());

  for (auto &E : Entry.Loop->Entries) {
    if (ApplyLetStack(E))
      return true;
  }

  return false;
}

/// ParseObjectBody - Parse the body of a def or class.  This consists of an
/// optional ClassList followed by a Body.  CurRec is the current def or class
/// that is being parsed.
///
///   ObjectBody      ::= BaseClassList Body
///   BaseClassList   ::= /*empty*/
///   BaseClassList   ::= ':' BaseClassListNE
///   BaseClassListNE ::= SubClassRef (',' SubClassRef)*
///
bool TGParser::ParseObjectBody(Record *CurRec) {
  // If there is a baseclass list, read it.
  if (Lex.getCode() == tgtok::colon) {
    Lex.Lex();

    // Read all of the subclasses.
    SubClassReference SubClass = ParseSubClassReference(CurRec, false);
    while (true) {
      // Check for error.
      if (!SubClass.Rec) return true;

      // Add it.
      if (AddSubClass(CurRec, SubClass))
        return true;

      if (Lex.getCode() != tgtok::comma) break;
      Lex.Lex(); // eat ','.
      SubClass = ParseSubClassReference(CurRec, false);
    }
  }

  if (ApplyLetStack(CurRec))
    return true;

  return ParseBody(CurRec);
}

/// ParseDef - Parse and return a top level or multiclass def, return the record
/// corresponding to it.  This returns null on error.
///
///   DefInst ::= DEF ObjectName ObjectBody
///
bool TGParser::ParseDef(MultiClass *CurMultiClass) {
  SMLoc DefLoc = Lex.getLoc();
  assert(Lex.getCode() == tgtok::Def && "Unknown tok");
  Lex.Lex();  // Eat the 'def' token.

  // Parse ObjectName and make a record for it.
  std::unique_ptr<Record> CurRec;
  Init *Name = ParseObjectName(CurMultiClass);
  if (!Name)
    return true;

  if (isa<UnsetInit>(Name))
    CurRec = make_unique<Record>(Records.getNewAnonymousName(), DefLoc, Records,
                                 /*Anonymous=*/true);
  else
    CurRec = make_unique<Record>(Name, DefLoc, Records);

  if (ParseObjectBody(CurRec.get()))
    return true;

  return addEntry(std::move(CurRec));
}

/// ParseDefset - Parse a defset statement.
///
///   Defset ::= DEFSET Type Id '=' '{' ObjectList '}'
///
bool TGParser::ParseDefset() {
  assert(Lex.getCode() == tgtok::Defset);
  Lex.Lex(); // Eat the 'defset' token

  DefsetRecord Defset;
  Defset.Loc = Lex.getLoc();
  RecTy *Type = ParseType();
  if (!Type)
    return true;
  if (!isa<ListRecTy>(Type))
    return Error(Defset.Loc, "expected list type");
  Defset.EltTy = cast<ListRecTy>(Type)->getElementType();

  if (Lex.getCode() != tgtok::Id)
    return TokError("expected identifier");
  StringInit *DeclName = StringInit::get(Lex.getCurStrVal());
  if (Records.getGlobal(DeclName->getValue()))
    return TokError("def or global variable of this name already exists");

  if (Lex.Lex() != tgtok::equal) // Eat the identifier
    return TokError("expected '='");
  if (Lex.Lex() != tgtok::l_brace) // Eat the '='
    return TokError("expected '{'");
  SMLoc BraceLoc = Lex.getLoc();
  Lex.Lex(); // Eat the '{'

  Defsets.push_back(&Defset);
  bool Err = ParseObjectList(nullptr);
  Defsets.pop_back();
  if (Err)
    return true;

  if (Lex.getCode() != tgtok::r_brace) {
    TokError("expected '}' at end of defset");
    return Error(BraceLoc, "to match this '{'");
  }
  Lex.Lex();  // Eat the '}'

  Records.addExtraGlobal(DeclName->getValue(),
                         ListInit::get(Defset.Elements, Defset.EltTy));
  return false;
}

/// ParseForeach - Parse a for statement.  Return the record corresponding
/// to it.  This returns true on error.
///
///   Foreach ::= FOREACH Declaration IN '{ ObjectList '}'
///   Foreach ::= FOREACH Declaration IN Object
///
bool TGParser::ParseForeach(MultiClass *CurMultiClass) {
  SMLoc Loc = Lex.getLoc();
  assert(Lex.getCode() == tgtok::Foreach && "Unknown tok");
  Lex.Lex();  // Eat the 'for' token.

  // Make a temporary object to record items associated with the for
  // loop.
  Init *ListValue = nullptr;
  VarInit *IterName = ParseForeachDeclaration(ListValue);
  if (!IterName)
    return TokError("expected declaration in for");

  if (Lex.getCode() != tgtok::In)
    return TokError("Unknown tok");
  Lex.Lex();  // Eat the in

  // Create a loop object and remember it.
  Loops.push_back(llvm::make_unique<ForeachLoop>(Loc, IterName, ListValue));

  if (Lex.getCode() != tgtok::l_brace) {
    // FOREACH Declaration IN Object
    if (ParseObject(CurMultiClass))
      return true;
  } else {
    SMLoc BraceLoc = Lex.getLoc();
    // Otherwise, this is a group foreach.
    Lex.Lex();  // eat the '{'.

    // Parse the object list.
    if (ParseObjectList(CurMultiClass))
      return true;

    if (Lex.getCode() != tgtok::r_brace) {
      TokError("expected '}' at end of foreach command");
      return Error(BraceLoc, "to match this '{'");
    }
    Lex.Lex();  // Eat the }
  }

  // Resolve the loop or store it for later resolution.
  std::unique_ptr<ForeachLoop> Loop = std::move(Loops.back());
  Loops.pop_back();

  return addEntry(std::move(Loop));
}

/// ParseClass - Parse a tblgen class definition.
///
///   ClassInst ::= CLASS ID TemplateArgList? ObjectBody
///
bool TGParser::ParseClass() {
  assert(Lex.getCode() == tgtok::Class && "Unexpected token!");
  Lex.Lex();

  if (Lex.getCode() != tgtok::Id)
    return TokError("expected class name after 'class' keyword");

  Record *CurRec = Records.getClass(Lex.getCurStrVal());
  if (CurRec) {
    // If the body was previously defined, this is an error.
    if (!CurRec->getValues().empty() ||
        !CurRec->getSuperClasses().empty() ||
        !CurRec->getTemplateArgs().empty())
      return TokError("Class '" + CurRec->getNameInitAsString() +
                      "' already defined");
  } else {
    // If this is the first reference to this class, create and add it.
    auto NewRec =
        llvm::make_unique<Record>(Lex.getCurStrVal(), Lex.getLoc(), Records,
                                  /*Class=*/true);
    CurRec = NewRec.get();
    Records.addClass(std::move(NewRec));
  }
  Lex.Lex(); // eat the name.

  // If there are template args, parse them.
  if (Lex.getCode() == tgtok::less)
    if (ParseTemplateArgList(CurRec))
      return true;

  return ParseObjectBody(CurRec);
}

/// ParseLetList - Parse a non-empty list of assignment expressions into a list
/// of LetRecords.
///
///   LetList ::= LetItem (',' LetItem)*
///   LetItem ::= ID OptionalRangeList '=' Value
///
void TGParser::ParseLetList(SmallVectorImpl<LetRecord> &Result) {
  while (true) {
    if (Lex.getCode() != tgtok::Id) {
      TokError("expected identifier in let definition");
      Result.clear();
      return;
    }

    StringInit *Name = StringInit::get(Lex.getCurStrVal());
    SMLoc NameLoc = Lex.getLoc();
    Lex.Lex();  // Eat the identifier.

    // Check for an optional RangeList.
    SmallVector<unsigned, 16> Bits;
    if (ParseOptionalRangeList(Bits)) {
      Result.clear();
      return;
    }
    std::reverse(Bits.begin(), Bits.end());

    if (Lex.getCode() != tgtok::equal) {
      TokError("expected '=' in let expression");
      Result.clear();
      return;
    }
    Lex.Lex();  // eat the '='.

    Init *Val = ParseValue(nullptr);
    if (!Val) {
      Result.clear();
      return;
    }

    // Now that we have everything, add the record.
    Result.emplace_back(Name, Bits, Val, NameLoc);

    if (Lex.getCode() != tgtok::comma)
      return;
    Lex.Lex();  // eat the comma.
  }
}

/// ParseTopLevelLet - Parse a 'let' at top level.  This can be a couple of
/// different related productions. This works inside multiclasses too.
///
///   Object ::= LET LetList IN '{' ObjectList '}'
///   Object ::= LET LetList IN Object
///
bool TGParser::ParseTopLevelLet(MultiClass *CurMultiClass) {
  assert(Lex.getCode() == tgtok::Let && "Unexpected token");
  Lex.Lex();

  // Add this entry to the let stack.
  SmallVector<LetRecord, 8> LetInfo;
  ParseLetList(LetInfo);
  if (LetInfo.empty()) return true;
  LetStack.push_back(std::move(LetInfo));

  if (Lex.getCode() != tgtok::In)
    return TokError("expected 'in' at end of top-level 'let'");
  Lex.Lex();

  // If this is a scalar let, just handle it now
  if (Lex.getCode() != tgtok::l_brace) {
    // LET LetList IN Object
    if (ParseObject(CurMultiClass))
      return true;
  } else {   // Object ::= LETCommand '{' ObjectList '}'
    SMLoc BraceLoc = Lex.getLoc();
    // Otherwise, this is a group let.
    Lex.Lex();  // eat the '{'.

    // Parse the object list.
    if (ParseObjectList(CurMultiClass))
      return true;

    if (Lex.getCode() != tgtok::r_brace) {
      TokError("expected '}' at end of top level let command");
      return Error(BraceLoc, "to match this '{'");
    }
    Lex.Lex();
  }

  // Outside this let scope, this let block is not active.
  LetStack.pop_back();
  return false;
}

/// ParseMultiClass - Parse a multiclass definition.
///
///  MultiClassInst ::= MULTICLASS ID TemplateArgList?
///                     ':' BaseMultiClassList '{' MultiClassObject+ '}'
///  MultiClassObject ::= DefInst
///  MultiClassObject ::= MultiClassInst
///  MultiClassObject ::= DefMInst
///  MultiClassObject ::= LETCommand '{' ObjectList '}'
///  MultiClassObject ::= LETCommand Object
///
bool TGParser::ParseMultiClass() {
  assert(Lex.getCode() == tgtok::MultiClass && "Unexpected token");
  Lex.Lex();  // Eat the multiclass token.

  if (Lex.getCode() != tgtok::Id)
    return TokError("expected identifier after multiclass for name");
  std::string Name = Lex.getCurStrVal();

  auto Result =
    MultiClasses.insert(std::make_pair(Name,
                    llvm::make_unique<MultiClass>(Name, Lex.getLoc(),Records)));

  if (!Result.second)
    return TokError("multiclass '" + Name + "' already defined");

  CurMultiClass = Result.first->second.get();
  Lex.Lex();  // Eat the identifier.

  // If there are template args, parse them.
  if (Lex.getCode() == tgtok::less)
    if (ParseTemplateArgList(nullptr))
      return true;

  bool inherits = false;

  // If there are submulticlasses, parse them.
  if (Lex.getCode() == tgtok::colon) {
    inherits = true;

    Lex.Lex();

    // Read all of the submulticlasses.
    SubMultiClassReference SubMultiClass =
      ParseSubMultiClassReference(CurMultiClass);
    while (true) {
      // Check for error.
      if (!SubMultiClass.MC) return true;

      // Add it.
      if (AddSubMultiClass(CurMultiClass, SubMultiClass))
        return true;

      if (Lex.getCode() != tgtok::comma) break;
      Lex.Lex(); // eat ','.
      SubMultiClass = ParseSubMultiClassReference(CurMultiClass);
    }
  }

  if (Lex.getCode() != tgtok::l_brace) {
    if (!inherits)
      return TokError("expected '{' in multiclass definition");
    if (Lex.getCode() != tgtok::semi)
      return TokError("expected ';' in multiclass definition");
    Lex.Lex();  // eat the ';'.
  } else {
    if (Lex.Lex() == tgtok::r_brace)  // eat the '{'.
      return TokError("multiclass must contain at least one def");

    while (Lex.getCode() != tgtok::r_brace) {
      switch (Lex.getCode()) {
      default:
        return TokError("expected 'let', 'def', 'defm' or 'foreach' in "
                        "multiclass body");
      case tgtok::Let:
      case tgtok::Def:
      case tgtok::Defm:
      case tgtok::Foreach:
        if (ParseObject(CurMultiClass))
          return true;
        break;
      }
    }
    Lex.Lex();  // eat the '}'.
  }

  CurMultiClass = nullptr;
  return false;
}

/// ParseDefm - Parse the instantiation of a multiclass.
///
///   DefMInst ::= DEFM ID ':' DefmSubClassRef ';'
///
bool TGParser::ParseDefm(MultiClass *CurMultiClass) {
  assert(Lex.getCode() == tgtok::Defm && "Unexpected token!");
  Lex.Lex(); // eat the defm

  Init *DefmName = ParseObjectName(CurMultiClass);
  if (!DefmName)
    return true;
  if (isa<UnsetInit>(DefmName)) {
    DefmName = Records.getNewAnonymousName();
    if (CurMultiClass)
      DefmName = BinOpInit::getStrConcat(
          VarInit::get(QualifiedNameOfImplicitName(CurMultiClass),
                       StringRecTy::get()),
          DefmName);
  }

  if (Lex.getCode() != tgtok::colon)
    return TokError("expected ':' after defm identifier");

  // Keep track of the new generated record definitions.
  std::vector<RecordsEntry> NewEntries;

  // This record also inherits from a regular class (non-multiclass)?
  bool InheritFromClass = false;

  // eat the colon.
  Lex.Lex();

  SMLoc SubClassLoc = Lex.getLoc();
  SubClassReference Ref = ParseSubClassReference(nullptr, true);

  while (true) {
    if (!Ref.Rec) return true;

    // To instantiate a multiclass, we need to first get the multiclass, then
    // instantiate each def contained in the multiclass with the SubClassRef
    // template parameters.
    MultiClass *MC = MultiClasses[Ref.Rec->getName()].get();
    assert(MC && "Didn't lookup multiclass correctly?");
    ArrayRef<Init*> TemplateVals = Ref.TemplateArgs;

    // Verify that the correct number of template arguments were specified.
    ArrayRef<Init *> TArgs = MC->Rec.getTemplateArgs();
    if (TArgs.size() < TemplateVals.size())
      return Error(SubClassLoc,
                   "more template args specified than multiclass expects");

    SubstStack Substs;
    for (unsigned i = 0, e = TArgs.size(); i != e; ++i) {
      if (i < TemplateVals.size()) {
        Substs.emplace_back(TArgs[i], TemplateVals[i]);
      } else {
        Init *Default = MC->Rec.getValue(TArgs[i])->getValue();
        if (!Default->isComplete()) {
          return Error(SubClassLoc,
                       "value not specified for template argument #" +
                           Twine(i) + " (" + TArgs[i]->getAsUnquotedString() +
                           ") of multiclass '" + MC->Rec.getNameInitAsString() +
                           "'");
        }
        Substs.emplace_back(TArgs[i], Default);
      }
    }

    Substs.emplace_back(QualifiedNameOfImplicitName(MC), DefmName);

    if (resolve(MC->Entries, Substs, CurMultiClass == nullptr, &NewEntries,
                &SubClassLoc))
      return true;

    if (Lex.getCode() != tgtok::comma) break;
    Lex.Lex(); // eat ','.

    if (Lex.getCode() != tgtok::Id)
      return TokError("expected identifier");

    SubClassLoc = Lex.getLoc();

    // A defm can inherit from regular classes (non-multiclass) as
    // long as they come in the end of the inheritance list.
    InheritFromClass = (Records.getClass(Lex.getCurStrVal()) != nullptr);

    if (InheritFromClass)
      break;

    Ref = ParseSubClassReference(nullptr, true);
  }

  if (InheritFromClass) {
    // Process all the classes to inherit as if they were part of a
    // regular 'def' and inherit all record values.
    SubClassReference SubClass = ParseSubClassReference(nullptr, false);
    while (true) {
      // Check for error.
      if (!SubClass.Rec) return true;

      // Get the expanded definition prototypes and teach them about
      // the record values the current class to inherit has
      for (auto &E : NewEntries) {
        // Add it.
        if (AddSubClass(E, SubClass))
          return true;
      }

      if (Lex.getCode() != tgtok::comma) break;
      Lex.Lex(); // eat ','.
      SubClass = ParseSubClassReference(nullptr, false);
    }
  }

  for (auto &E : NewEntries) {
    if (ApplyLetStack(E))
      return true;

    addEntry(std::move(E));
  }

  if (Lex.getCode() != tgtok::semi)
    return TokError("expected ';' at end of defm");
  Lex.Lex();

  return false;
}

/// ParseObject
///   Object ::= ClassInst
///   Object ::= DefInst
///   Object ::= MultiClassInst
///   Object ::= DefMInst
///   Object ::= LETCommand '{' ObjectList '}'
///   Object ::= LETCommand Object
bool TGParser::ParseObject(MultiClass *MC) {
  switch (Lex.getCode()) {
  default:
    return TokError("Expected class, def, defm, defset, multiclass, let or "
                    "foreach");
  case tgtok::Let:   return ParseTopLevelLet(MC);
  case tgtok::Def:   return ParseDef(MC);
  case tgtok::Foreach:   return ParseForeach(MC);
  case tgtok::Defm:  return ParseDefm(MC);
  case tgtok::Defset:
    if (MC)
      return TokError("defset is not allowed inside multiclass");
    return ParseDefset();
  case tgtok::Class:
    if (MC)
      return TokError("class is not allowed inside multiclass");
    if (!Loops.empty())
      return TokError("class is not allowed inside foreach loop");
    return ParseClass();
  case tgtok::MultiClass:
    if (!Loops.empty())
      return TokError("multiclass is not allowed inside foreach loop");
    return ParseMultiClass();
  }
}

/// ParseObjectList
///   ObjectList :== Object*
bool TGParser::ParseObjectList(MultiClass *MC) {
  while (isObjectStart(Lex.getCode())) {
    if (ParseObject(MC))
      return true;
  }
  return false;
}

bool TGParser::ParseFile() {
  Lex.Lex(); // Prime the lexer.
  if (ParseObjectList()) return true;

  // If we have unread input at the end of the file, report it.
  if (Lex.getCode() == tgtok::Eof)
    return false;

  return TokError("Unexpected input at top level");
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void RecordsEntry::dump() const {
  if (Loop)
    Loop->dump();
  if (Rec)
    Rec->dump();
}

LLVM_DUMP_METHOD void ForeachLoop::dump() const {
  errs() << "foreach " << IterVar->getAsString() << " = "
         << ListValue->getAsString() << " in {\n";

  for (const auto &E : Entries)
    E.dump();

  errs() << "}\n";
}

LLVM_DUMP_METHOD void MultiClass::dump() const {
  errs() << "Record:\n";
  Rec.dump();

  errs() << "Defs:\n";
  for (const auto &E : Entries)
    E.dump();
}
#endif
