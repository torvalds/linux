//=======- PaddingChecker.cpp ------------------------------------*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines a checker that checks for padding that could be
//  removed by re-ordering members.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/AST/CharUnits.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <numeric>

using namespace clang;
using namespace ento;

namespace {
class PaddingChecker : public Checker<check::ASTDecl<TranslationUnitDecl>> {
private:
  mutable std::unique_ptr<BugType> PaddingBug;
  mutable int64_t AllowedPad;
  mutable BugReporter *BR;

public:
  void checkASTDecl(const TranslationUnitDecl *TUD, AnalysisManager &MGR,
                    BugReporter &BRArg) const {
    BR = &BRArg;
    AllowedPad =
        MGR.getAnalyzerOptions()
          .getCheckerIntegerOption("AllowedPad", 24, this);
    assert(AllowedPad >= 0 && "AllowedPad option should be non-negative");

    // The calls to checkAST* from AnalysisConsumer don't
    // visit template instantiations or lambda classes. We
    // want to visit those, so we make our own RecursiveASTVisitor.
    struct LocalVisitor : public RecursiveASTVisitor<LocalVisitor> {
      const PaddingChecker *Checker;
      bool shouldVisitTemplateInstantiations() const { return true; }
      bool shouldVisitImplicitCode() const { return true; }
      explicit LocalVisitor(const PaddingChecker *Checker) : Checker(Checker) {}
      bool VisitRecordDecl(const RecordDecl *RD) {
        Checker->visitRecord(RD);
        return true;
      }
      bool VisitVarDecl(const VarDecl *VD) {
        Checker->visitVariable(VD);
        return true;
      }
      // TODO: Visit array new and mallocs for arrays.
    };

    LocalVisitor visitor(this);
    visitor.TraverseDecl(const_cast<TranslationUnitDecl *>(TUD));
  }

  /// Look for records of overly padded types. If padding *
  /// PadMultiplier exceeds AllowedPad, then generate a report.
  /// PadMultiplier is used to share code with the array padding
  /// checker.
  void visitRecord(const RecordDecl *RD, uint64_t PadMultiplier = 1) const {
    if (shouldSkipDecl(RD))
      return;

    // TODO: Figure out why we are going through declarations and not only
    // definitions.
    if (!(RD = RD->getDefinition()))
      return;

    // This is the simplest correct case: a class with no fields and one base
    // class. Other cases are more complicated because of how the base classes
    // & fields might interact, so we don't bother dealing with them.
    // TODO: Support other combinations of base classes and fields.
    if (auto *CXXRD = dyn_cast<CXXRecordDecl>(RD))
      if (CXXRD->field_empty() && CXXRD->getNumBases() == 1)
        return visitRecord(CXXRD->bases().begin()->getType()->getAsRecordDecl(),
                           PadMultiplier);

    auto &ASTContext = RD->getASTContext();
    const ASTRecordLayout &RL = ASTContext.getASTRecordLayout(RD);
    assert(llvm::isPowerOf2_64(RL.getAlignment().getQuantity()));

    CharUnits BaselinePad = calculateBaselinePad(RD, ASTContext, RL);
    if (BaselinePad.isZero())
      return;

    CharUnits OptimalPad;
    SmallVector<const FieldDecl *, 20> OptimalFieldsOrder;
    std::tie(OptimalPad, OptimalFieldsOrder) =
        calculateOptimalPad(RD, ASTContext, RL);

    CharUnits DiffPad = PadMultiplier * (BaselinePad - OptimalPad);
    if (DiffPad.getQuantity() <= AllowedPad) {
      assert(!DiffPad.isNegative() && "DiffPad should not be negative");
      // There is not enough excess padding to trigger a warning.
      return;
    }
    reportRecord(RD, BaselinePad, OptimalPad, OptimalFieldsOrder);
  }

  /// Look for arrays of overly padded types. If the padding of the
  /// array type exceeds AllowedPad, then generate a report.
  void visitVariable(const VarDecl *VD) const {
    const ArrayType *ArrTy = VD->getType()->getAsArrayTypeUnsafe();
    if (ArrTy == nullptr)
      return;
    uint64_t Elts = 0;
    if (const ConstantArrayType *CArrTy = dyn_cast<ConstantArrayType>(ArrTy))
      Elts = CArrTy->getSize().getZExtValue();
    if (Elts == 0)
      return;
    const RecordType *RT = ArrTy->getElementType()->getAs<RecordType>();
    if (RT == nullptr)
      return;

    // TODO: Recurse into the fields to see if they have excess padding.
    visitRecord(RT->getDecl(), Elts);
  }

  bool shouldSkipDecl(const RecordDecl *RD) const {
    // TODO: Figure out why we are going through declarations and not only
    // definitions.
    if (!(RD = RD->getDefinition()))
      return true;
    auto Location = RD->getLocation();
    // If the construct doesn't have a source file, then it's not something
    // we want to diagnose.
    if (!Location.isValid())
      return true;
    SrcMgr::CharacteristicKind Kind =
        BR->getSourceManager().getFileCharacteristic(Location);
    // Throw out all records that come from system headers.
    if (Kind != SrcMgr::C_User)
      return true;

    // Not going to attempt to optimize unions.
    if (RD->isUnion())
      return true;
    if (auto *CXXRD = dyn_cast<CXXRecordDecl>(RD)) {
      // Tail padding with base classes ends up being very complicated.
      // We will skip objects with base classes for now, unless they do not
      // have fields.
      // TODO: Handle more base class scenarios.
      if (!CXXRD->field_empty() && CXXRD->getNumBases() != 0)
        return true;
      if (CXXRD->field_empty() && CXXRD->getNumBases() != 1)
        return true;
      // Virtual bases are complicated, skipping those for now.
      if (CXXRD->getNumVBases() != 0)
        return true;
      // Can't layout a template, so skip it. We do still layout the
      // instantiations though.
      if (CXXRD->getTypeForDecl()->isDependentType())
        return true;
      if (CXXRD->getTypeForDecl()->isInstantiationDependentType())
        return true;
    }
    // How do you reorder fields if you haven't got any?
    else if (RD->field_empty())
      return true;

    auto IsTrickyField = [](const FieldDecl *FD) -> bool {
      // Bitfield layout is hard.
      if (FD->isBitField())
        return true;

      // Variable length arrays are tricky too.
      QualType Ty = FD->getType();
      if (Ty->isIncompleteArrayType())
        return true;
      return false;
    };

    if (std::any_of(RD->field_begin(), RD->field_end(), IsTrickyField))
      return true;
    return false;
  }

  static CharUnits calculateBaselinePad(const RecordDecl *RD,
                                        const ASTContext &ASTContext,
                                        const ASTRecordLayout &RL) {
    CharUnits PaddingSum;
    CharUnits Offset = ASTContext.toCharUnitsFromBits(RL.getFieldOffset(0));
    for (const FieldDecl *FD : RD->fields()) {
      // This checker only cares about the padded size of the
      // field, and not the data size. If the field is a record
      // with tail padding, then we won't put that number in our
      // total because reordering fields won't fix that problem.
      CharUnits FieldSize = ASTContext.getTypeSizeInChars(FD->getType());
      auto FieldOffsetBits = RL.getFieldOffset(FD->getFieldIndex());
      CharUnits FieldOffset = ASTContext.toCharUnitsFromBits(FieldOffsetBits);
      PaddingSum += (FieldOffset - Offset);
      Offset = FieldOffset + FieldSize;
    }
    PaddingSum += RL.getSize() - Offset;
    return PaddingSum;
  }

  /// Optimal padding overview:
  /// 1.  Find a close approximation to where we can place our first field.
  ///     This will usually be at offset 0.
  /// 2.  Try to find the best field that can legally be placed at the current
  ///     offset.
  ///   a.  "Best" is the largest alignment that is legal, but smallest size.
  ///       This is to account for overly aligned types.
  /// 3.  If no fields can fit, pad by rounding the current offset up to the
  ///     smallest alignment requirement of our fields. Measure and track the
  //      amount of padding added. Go back to 2.
  /// 4.  Increment the current offset by the size of the chosen field.
  /// 5.  Remove the chosen field from the set of future possibilities.
  /// 6.  Go back to 2 if there are still unplaced fields.
  /// 7.  Add tail padding by rounding the current offset up to the structure
  ///     alignment. Track the amount of padding added.

  static std::pair<CharUnits, SmallVector<const FieldDecl *, 20>>
  calculateOptimalPad(const RecordDecl *RD, const ASTContext &ASTContext,
                      const ASTRecordLayout &RL) {
    struct FieldInfo {
      CharUnits Align;
      CharUnits Size;
      const FieldDecl *Field;
      bool operator<(const FieldInfo &RHS) const {
        // Order from small alignments to large alignments,
        // then large sizes to small sizes.
        // then large field indices to small field indices
        return std::make_tuple(Align, -Size,
                               Field ? -static_cast<int>(Field->getFieldIndex())
                                     : 0) <
               std::make_tuple(
                   RHS.Align, -RHS.Size,
                   RHS.Field ? -static_cast<int>(RHS.Field->getFieldIndex())
                             : 0);
      }
    };
    SmallVector<FieldInfo, 20> Fields;
    auto GatherSizesAndAlignments = [](const FieldDecl *FD) {
      FieldInfo RetVal;
      RetVal.Field = FD;
      auto &Ctx = FD->getASTContext();
      std::tie(RetVal.Size, RetVal.Align) =
          Ctx.getTypeInfoInChars(FD->getType());
      assert(llvm::isPowerOf2_64(RetVal.Align.getQuantity()));
      if (auto Max = FD->getMaxAlignment())
        RetVal.Align = std::max(Ctx.toCharUnitsFromBits(Max), RetVal.Align);
      return RetVal;
    };
    std::transform(RD->field_begin(), RD->field_end(),
                   std::back_inserter(Fields), GatherSizesAndAlignments);
    llvm::sort(Fields);
    // This lets us skip over vptrs and non-virtual bases,
    // so that we can just worry about the fields in our object.
    // Note that this does cause us to miss some cases where we
    // could pack more bytes in to a base class's tail padding.
    CharUnits NewOffset = ASTContext.toCharUnitsFromBits(RL.getFieldOffset(0));
    CharUnits NewPad;
    SmallVector<const FieldDecl *, 20> OptimalFieldsOrder;
    while (!Fields.empty()) {
      unsigned TrailingZeros =
          llvm::countTrailingZeros((unsigned long long)NewOffset.getQuantity());
      // If NewOffset is zero, then countTrailingZeros will be 64. Shifting
      // 64 will overflow our unsigned long long. Shifting 63 will turn
      // our long long (and CharUnits internal type) negative. So shift 62.
      long long CurAlignmentBits = 1ull << (std::min)(TrailingZeros, 62u);
      CharUnits CurAlignment = CharUnits::fromQuantity(CurAlignmentBits);
      FieldInfo InsertPoint = {CurAlignment, CharUnits::Zero(), nullptr};
      auto CurBegin = Fields.begin();
      auto CurEnd = Fields.end();

      // In the typical case, this will find the last element
      // of the vector. We won't find a middle element unless
      // we started on a poorly aligned address or have an overly
      // aligned field.
      auto Iter = std::upper_bound(CurBegin, CurEnd, InsertPoint);
      if (Iter != CurBegin) {
        // We found a field that we can layout with the current alignment.
        --Iter;
        NewOffset += Iter->Size;
        OptimalFieldsOrder.push_back(Iter->Field);
        Fields.erase(Iter);
      } else {
        // We are poorly aligned, and we need to pad in order to layout another
        // field. Round up to at least the smallest field alignment that we
        // currently have.
        CharUnits NextOffset = NewOffset.alignTo(Fields[0].Align);
        NewPad += NextOffset - NewOffset;
        NewOffset = NextOffset;
      }
    }
    // Calculate tail padding.
    CharUnits NewSize = NewOffset.alignTo(RL.getAlignment());
    NewPad += NewSize - NewOffset;
    return {NewPad, std::move(OptimalFieldsOrder)};
  }

  void reportRecord(
      const RecordDecl *RD, CharUnits BaselinePad, CharUnits OptimalPad,
      const SmallVector<const FieldDecl *, 20> &OptimalFieldsOrder) const {
    if (!PaddingBug)
      PaddingBug =
          llvm::make_unique<BugType>(this, "Excessive Padding", "Performance");

    SmallString<100> Buf;
    llvm::raw_svector_ostream Os(Buf);
    Os << "Excessive padding in '";
    Os << QualType::getAsString(RD->getTypeForDecl(), Qualifiers(),
                                LangOptions())
       << "'";

    if (auto *TSD = dyn_cast<ClassTemplateSpecializationDecl>(RD)) {
      // TODO: make this show up better in the console output and in
      // the HTML. Maybe just make it show up in HTML like the path
      // diagnostics show.
      SourceLocation ILoc = TSD->getPointOfInstantiation();
      if (ILoc.isValid())
        Os << " instantiated here: "
           << ILoc.printToString(BR->getSourceManager());
    }

    Os << " (" << BaselinePad.getQuantity() << " padding bytes, where "
       << OptimalPad.getQuantity() << " is optimal). \n"
       << "Optimal fields order: \n";
    for (const auto *FD : OptimalFieldsOrder)
      Os << FD->getName() << ", \n";
    Os << "consider reordering the fields or adding explicit padding "
          "members.";

    PathDiagnosticLocation CELoc =
        PathDiagnosticLocation::create(RD, BR->getSourceManager());
    auto Report = llvm::make_unique<BugReport>(*PaddingBug, Os.str(), CELoc);
    Report->setDeclWithIssue(RD);
    Report->addRange(RD->getSourceRange());
    BR->emitReport(std::move(Report));
  }
};
} // namespace

void ento::registerPaddingChecker(CheckerManager &Mgr) {
  Mgr.registerChecker<PaddingChecker>();
}
