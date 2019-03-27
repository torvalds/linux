//===-- llvm/CodeGen/DebugLocEntry.h - Entry in debug_loc list -*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_ASMPRINTER_DEBUGLOCENTRY_H
#define LLVM_LIB_CODEGEN_ASMPRINTER_DEBUGLOCENTRY_H

#include "DebugLocStream.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MachineLocation.h"
#include "llvm/Support/Debug.h"

namespace llvm {
class AsmPrinter;

/// This struct describes location entries emitted in the .debug_loc
/// section.
class DebugLocEntry {
  /// Begin and end symbols for the address range that this location is valid.
  const MCSymbol *Begin;
  const MCSymbol *End;

public:
  /// A single location or constant.
  struct Value {
    Value(const DIExpression *Expr, int64_t i)
        : Expression(Expr), EntryKind(E_Integer) {
      Constant.Int = i;
    }
    Value(const DIExpression *Expr, const ConstantFP *CFP)
        : Expression(Expr), EntryKind(E_ConstantFP) {
      Constant.CFP = CFP;
    }
    Value(const DIExpression *Expr, const ConstantInt *CIP)
        : Expression(Expr), EntryKind(E_ConstantInt) {
      Constant.CIP = CIP;
    }
    Value(const DIExpression *Expr, MachineLocation Loc)
        : Expression(Expr), EntryKind(E_Location), Loc(Loc) {
      assert(cast<DIExpression>(Expr)->isValid());
    }

    /// Any complex address location expression for this Value.
    const DIExpression *Expression;

    /// Type of entry that this represents.
    enum EntryType { E_Location, E_Integer, E_ConstantFP, E_ConstantInt };
    enum EntryType EntryKind;

    /// Either a constant,
    union {
      int64_t Int;
      const ConstantFP *CFP;
      const ConstantInt *CIP;
    } Constant;

    // Or a location in the machine frame.
    MachineLocation Loc;

    bool isLocation() const { return EntryKind == E_Location; }
    bool isInt() const { return EntryKind == E_Integer; }
    bool isConstantFP() const { return EntryKind == E_ConstantFP; }
    bool isConstantInt() const { return EntryKind == E_ConstantInt; }
    int64_t getInt() const { return Constant.Int; }
    const ConstantFP *getConstantFP() const { return Constant.CFP; }
    const ConstantInt *getConstantInt() const { return Constant.CIP; }
    MachineLocation getLoc() const { return Loc; }
    bool isFragment() const { return getExpression()->isFragment(); }
    const DIExpression *getExpression() const { return Expression; }
    friend bool operator==(const Value &, const Value &);
    friend bool operator<(const Value &, const Value &);
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
    LLVM_DUMP_METHOD void dump() const {
      if (isLocation()) {
        llvm::dbgs() << "Loc = { reg=" << Loc.getReg() << " ";
        if (Loc.isIndirect())
          llvm::dbgs() << "+0";
        llvm::dbgs() << "} ";
      }
      else if (isConstantInt())
        Constant.CIP->dump();
      else if (isConstantFP())
        Constant.CFP->dump();
      if (Expression)
        Expression->dump();
    }
#endif
  };

private:
  /// A nonempty list of locations/constants belonging to this entry,
  /// sorted by offset.
  SmallVector<Value, 1> Values;

public:
  DebugLocEntry(const MCSymbol *B, const MCSymbol *E, Value Val)
      : Begin(B), End(E) {
    Values.push_back(std::move(Val));
  }

  /// If this and Next are describing different pieces of the same
  /// variable, merge them by appending Next's values to the current
  /// list of values.
  /// Return true if the merge was successful.
  bool MergeValues(const DebugLocEntry &Next);

  /// Attempt to merge this DebugLocEntry with Next and return
  /// true if the merge was successful. Entries can be merged if they
  /// share the same Loc/Constant and if Next immediately follows this
  /// Entry.
  bool MergeRanges(const DebugLocEntry &Next) {
    // If this and Next are describing the same variable, merge them.
    if ((End == Next.Begin && Values == Next.Values)) {
      End = Next.End;
      return true;
    }
    return false;
  }

  const MCSymbol *getBeginSym() const { return Begin; }
  const MCSymbol *getEndSym() const { return End; }
  ArrayRef<Value> getValues() const { return Values; }
  void addValues(ArrayRef<DebugLocEntry::Value> Vals) {
    Values.append(Vals.begin(), Vals.end());
    sortUniqueValues();
    assert(all_of(Values, [](DebugLocEntry::Value V) {
          return V.isFragment();
        }) && "value must be a piece");
  }

  // Sort the pieces by offset.
  // Remove any duplicate entries by dropping all but the first.
  void sortUniqueValues() {
    llvm::sort(Values);
    Values.erase(
        std::unique(
            Values.begin(), Values.end(), [](const Value &A, const Value &B) {
              return A.getExpression() == B.getExpression();
            }),
        Values.end());
  }

  /// Lower this entry into a DWARF expression.
  void finalize(const AsmPrinter &AP, DebugLocStream::ListBuilder &List,
                const DIBasicType *BT);
};

/// Compare two Values for equality.
inline bool operator==(const DebugLocEntry::Value &A,
                       const DebugLocEntry::Value &B) {
  if (A.EntryKind != B.EntryKind)
    return false;

  if (A.Expression != B.Expression)
    return false;

  switch (A.EntryKind) {
  case DebugLocEntry::Value::E_Location:
    return A.Loc == B.Loc;
  case DebugLocEntry::Value::E_Integer:
    return A.Constant.Int == B.Constant.Int;
  case DebugLocEntry::Value::E_ConstantFP:
    return A.Constant.CFP == B.Constant.CFP;
  case DebugLocEntry::Value::E_ConstantInt:
    return A.Constant.CIP == B.Constant.CIP;
  }
  llvm_unreachable("unhandled EntryKind");
}

/// Compare two fragments based on their offset.
inline bool operator<(const DebugLocEntry::Value &A,
                      const DebugLocEntry::Value &B) {
  return A.getExpression()->getFragmentInfo()->OffsetInBits <
         B.getExpression()->getFragmentInfo()->OffsetInBits;
}

}

#endif
