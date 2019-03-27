//===- TGParser.h - Parser for TableGen Files -------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This class represents the Parser for tablegen files.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TABLEGEN_TGPARSER_H
#define LLVM_LIB_TABLEGEN_TGPARSER_H

#include "TGLexer.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include <map>

namespace llvm {
  class Record;
  class RecordVal;
  class RecordKeeper;
  class RecTy;
  class Init;
  struct ForeachLoop;
  struct MultiClass;
  struct SubClassReference;
  struct SubMultiClassReference;

  struct LetRecord {
    StringInit *Name;
    std::vector<unsigned> Bits;
    Init *Value;
    SMLoc Loc;
    LetRecord(StringInit *N, ArrayRef<unsigned> B, Init *V, SMLoc L)
      : Name(N), Bits(B), Value(V), Loc(L) {
    }
  };

  /// RecordsEntry - Can be either a record or a foreach loop.
  struct RecordsEntry {
    std::unique_ptr<Record> Rec;
    std::unique_ptr<ForeachLoop> Loop;

    void dump() const;

    RecordsEntry() {}
    RecordsEntry(std::unique_ptr<Record> Rec) : Rec(std::move(Rec)) {}
    RecordsEntry(std::unique_ptr<ForeachLoop> Loop)
      : Loop(std::move(Loop)) {}
  };

  /// ForeachLoop - Record the iteration state associated with a for loop.
  /// This is used to instantiate items in the loop body.
  struct ForeachLoop {
    SMLoc Loc;
    VarInit *IterVar;
    Init *ListValue;
    std::vector<RecordsEntry> Entries;

    void dump() const;

    ForeachLoop(SMLoc Loc, VarInit *IVar, Init *LValue)
      : Loc(Loc), IterVar(IVar), ListValue(LValue) {}
  };

  struct DefsetRecord {
    SMLoc Loc;
    RecTy *EltTy;
    SmallVector<Init *, 16> Elements;
  };

struct MultiClass {
  Record Rec;  // Placeholder for template args and Name.
  std::vector<RecordsEntry> Entries;

  void dump() const;

  MultiClass(StringRef Name, SMLoc Loc, RecordKeeper &Records) :
    Rec(Name, Loc, Records) {}
};

class TGParser {
  TGLexer Lex;
  std::vector<SmallVector<LetRecord, 4>> LetStack;
  std::map<std::string, std::unique_ptr<MultiClass>> MultiClasses;

  /// Loops - Keep track of any foreach loops we are within.
  ///
  std::vector<std::unique_ptr<ForeachLoop>> Loops;

  SmallVector<DefsetRecord *, 2> Defsets;

  /// CurMultiClass - If we are parsing a 'multiclass' definition, this is the
  /// current value.
  MultiClass *CurMultiClass;

  // Record tracker
  RecordKeeper &Records;

  // A "named boolean" indicating how to parse identifiers.  Usually
  // identifiers map to some existing object but in special cases
  // (e.g. parsing def names) no such object exists yet because we are
  // in the middle of creating in.  For those situations, allow the
  // parser to ignore missing object errors.
  enum IDParseMode {
    ParseValueMode,   // We are parsing a value we expect to look up.
    ParseNameMode,    // We are parsing a name of an object that does not yet
                      // exist.
  };

public:
  TGParser(SourceMgr &SrcMgr, ArrayRef<std::string> Macros,
           RecordKeeper &records)
    : Lex(SrcMgr, Macros), CurMultiClass(nullptr), Records(records) {}

  /// ParseFile - Main entrypoint for parsing a tblgen file.  These parser
  /// routines return true on error, or false on success.
  bool ParseFile();

  bool Error(SMLoc L, const Twine &Msg) const {
    PrintError(L, Msg);
    return true;
  }
  bool TokError(const Twine &Msg) const {
    return Error(Lex.getLoc(), Msg);
  }
  const TGLexer::DependenciesMapTy &getDependencies() const {
    return Lex.getDependencies();
  }

private:  // Semantic analysis methods.
  bool AddValue(Record *TheRec, SMLoc Loc, const RecordVal &RV);
  bool SetValue(Record *TheRec, SMLoc Loc, Init *ValName,
                ArrayRef<unsigned> BitList, Init *V,
                bool AllowSelfAssignment = false);
  bool AddSubClass(Record *Rec, SubClassReference &SubClass);
  bool AddSubClass(RecordsEntry &Entry, SubClassReference &SubClass);
  bool AddSubMultiClass(MultiClass *CurMC,
                        SubMultiClassReference &SubMultiClass);

  using SubstStack = SmallVector<std::pair<Init *, Init *>, 8>;

  bool addEntry(RecordsEntry E);
  bool resolve(const ForeachLoop &Loop, SubstStack &Stack, bool Final,
               std::vector<RecordsEntry> *Dest, SMLoc *Loc = nullptr);
  bool resolve(const std::vector<RecordsEntry> &Source, SubstStack &Substs,
               bool Final, std::vector<RecordsEntry> *Dest,
               SMLoc *Loc = nullptr);
  bool addDefOne(std::unique_ptr<Record> Rec);

private:  // Parser methods.
  bool ParseObjectList(MultiClass *MC = nullptr);
  bool ParseObject(MultiClass *MC);
  bool ParseClass();
  bool ParseMultiClass();
  bool ParseDefm(MultiClass *CurMultiClass);
  bool ParseDef(MultiClass *CurMultiClass);
  bool ParseDefset();
  bool ParseForeach(MultiClass *CurMultiClass);
  bool ParseTopLevelLet(MultiClass *CurMultiClass);
  void ParseLetList(SmallVectorImpl<LetRecord> &Result);

  bool ParseObjectBody(Record *CurRec);
  bool ParseBody(Record *CurRec);
  bool ParseBodyItem(Record *CurRec);

  bool ParseTemplateArgList(Record *CurRec);
  Init *ParseDeclaration(Record *CurRec, bool ParsingTemplateArgs);
  VarInit *ParseForeachDeclaration(Init *&ForeachListValue);

  SubClassReference ParseSubClassReference(Record *CurRec, bool isDefm);
  SubMultiClassReference ParseSubMultiClassReference(MultiClass *CurMC);

  Init *ParseIDValue(Record *CurRec, StringInit *Name, SMLoc NameLoc,
                     IDParseMode Mode = ParseValueMode);
  Init *ParseSimpleValue(Record *CurRec, RecTy *ItemType = nullptr,
                         IDParseMode Mode = ParseValueMode);
  Init *ParseValue(Record *CurRec, RecTy *ItemType = nullptr,
                   IDParseMode Mode = ParseValueMode);
  void ParseValueList(SmallVectorImpl<llvm::Init*> &Result, Record *CurRec,
                      Record *ArgsRec = nullptr, RecTy *EltTy = nullptr);
  void ParseDagArgList(
      SmallVectorImpl<std::pair<llvm::Init*, StringInit*>> &Result,
      Record *CurRec);
  bool ParseOptionalRangeList(SmallVectorImpl<unsigned> &Ranges);
  bool ParseOptionalBitList(SmallVectorImpl<unsigned> &Ranges);
  void ParseRangeList(SmallVectorImpl<unsigned> &Result);
  bool ParseRangePiece(SmallVectorImpl<unsigned> &Ranges);
  RecTy *ParseType();
  Init *ParseOperation(Record *CurRec, RecTy *ItemType);
  RecTy *ParseOperatorType();
  Init *ParseObjectName(MultiClass *CurMultiClass);
  Record *ParseClassID();
  MultiClass *ParseMultiClassID();
  bool ApplyLetStack(Record *CurRec);
  bool ApplyLetStack(RecordsEntry &Entry);
};

} // end namespace llvm

#endif
