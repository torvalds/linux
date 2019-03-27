//===- TemplateBase.cpp - Common template AST class implementation --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements common classes used throughout C++ template
// representations.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/TemplateBase.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/AST/TemplateName.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

using namespace clang;

/// Print a template integral argument value.
///
/// \param TemplArg the TemplateArgument instance to print.
///
/// \param Out the raw_ostream instance to use for printing.
///
/// \param Policy the printing policy for EnumConstantDecl printing.
static void printIntegral(const TemplateArgument &TemplArg,
                          raw_ostream &Out, const PrintingPolicy& Policy) {
  const Type *T = TemplArg.getIntegralType().getTypePtr();
  const llvm::APSInt &Val = TemplArg.getAsIntegral();

  if (const EnumType *ET = T->getAs<EnumType>()) {
    for (const EnumConstantDecl* ECD : ET->getDecl()->enumerators()) {
      // In Sema::CheckTemplateArugment, enum template arguments value are
      // extended to the size of the integer underlying the enum type.  This
      // may create a size difference between the enum value and template
      // argument value, requiring isSameValue here instead of operator==.
      if (llvm::APSInt::isSameValue(ECD->getInitVal(), Val)) {
        ECD->printQualifiedName(Out, Policy);
        return;
      }
    }
  }

  if (T->isBooleanType() && !Policy.MSVCFormatting) {
    Out << (Val.getBoolValue() ? "true" : "false");
  } else if (T->isCharType()) {
    const char Ch = Val.getZExtValue();
    Out << ((Ch == '\'') ? "'\\" : "'");
    Out.write_escaped(StringRef(&Ch, 1), /*UseHexEscapes=*/ true);
    Out << "'";
  } else {
    Out << Val;
  }
}

//===----------------------------------------------------------------------===//
// TemplateArgument Implementation
//===----------------------------------------------------------------------===//

TemplateArgument::TemplateArgument(ASTContext &Ctx, const llvm::APSInt &Value,
                                   QualType Type) {
  Integer.Kind = Integral;
  // Copy the APSInt value into our decomposed form.
  Integer.BitWidth = Value.getBitWidth();
  Integer.IsUnsigned = Value.isUnsigned();
  // If the value is large, we have to get additional memory from the ASTContext
  unsigned NumWords = Value.getNumWords();
  if (NumWords > 1) {
    void *Mem = Ctx.Allocate(NumWords * sizeof(uint64_t));
    std::memcpy(Mem, Value.getRawData(), NumWords * sizeof(uint64_t));
    Integer.pVal = static_cast<uint64_t *>(Mem);
  } else {
    Integer.VAL = Value.getZExtValue();
  }

  Integer.Type = Type.getAsOpaquePtr();
}

TemplateArgument
TemplateArgument::CreatePackCopy(ASTContext &Context,
                                 ArrayRef<TemplateArgument> Args) {
  if (Args.empty())
    return getEmptyPack();

  return TemplateArgument(Args.copy(Context));
}

bool TemplateArgument::isDependent() const {
  switch (getKind()) {
  case Null:
    llvm_unreachable("Should not have a NULL template argument");

  case Type:
    return getAsType()->isDependentType() ||
           isa<PackExpansionType>(getAsType());

  case Template:
    return getAsTemplate().isDependent();

  case TemplateExpansion:
    return true;

  case Declaration:
    if (DeclContext *DC = dyn_cast<DeclContext>(getAsDecl()))
      return DC->isDependentContext();
    return getAsDecl()->getDeclContext()->isDependentContext();

  case NullPtr:
    return false;

  case Integral:
    // Never dependent
    return false;

  case Expression:
    return (getAsExpr()->isTypeDependent() || getAsExpr()->isValueDependent() ||
            isa<PackExpansionExpr>(getAsExpr()));

  case Pack:
    for (const auto &P : pack_elements())
      if (P.isDependent())
        return true;
    return false;
  }

  llvm_unreachable("Invalid TemplateArgument Kind!");
}

bool TemplateArgument::isInstantiationDependent() const {
  switch (getKind()) {
  case Null:
    llvm_unreachable("Should not have a NULL template argument");

  case Type:
    return getAsType()->isInstantiationDependentType();

  case Template:
    return getAsTemplate().isInstantiationDependent();

  case TemplateExpansion:
    return true;

  case Declaration:
    if (DeclContext *DC = dyn_cast<DeclContext>(getAsDecl()))
      return DC->isDependentContext();
    return getAsDecl()->getDeclContext()->isDependentContext();

  case NullPtr:
    return false;

  case Integral:
    // Never dependent
    return false;

  case Expression:
    return getAsExpr()->isInstantiationDependent();

  case Pack:
    for (const auto &P : pack_elements())
      if (P.isInstantiationDependent())
        return true;
    return false;
  }

  llvm_unreachable("Invalid TemplateArgument Kind!");
}

bool TemplateArgument::isPackExpansion() const {
  switch (getKind()) {
  case Null:
  case Declaration:
  case Integral:
  case Pack:
  case Template:
  case NullPtr:
    return false;

  case TemplateExpansion:
    return true;

  case Type:
    return isa<PackExpansionType>(getAsType());

  case Expression:
    return isa<PackExpansionExpr>(getAsExpr());
  }

  llvm_unreachable("Invalid TemplateArgument Kind!");
}

bool TemplateArgument::containsUnexpandedParameterPack() const {
  switch (getKind()) {
  case Null:
  case Declaration:
  case Integral:
  case TemplateExpansion:
  case NullPtr:
    break;

  case Type:
    if (getAsType()->containsUnexpandedParameterPack())
      return true;
    break;

  case Template:
    if (getAsTemplate().containsUnexpandedParameterPack())
      return true;
    break;

  case Expression:
    if (getAsExpr()->containsUnexpandedParameterPack())
      return true;
    break;

  case Pack:
    for (const auto &P : pack_elements())
      if (P.containsUnexpandedParameterPack())
        return true;

    break;
  }

  return false;
}

Optional<unsigned> TemplateArgument::getNumTemplateExpansions() const {
  assert(getKind() == TemplateExpansion);
  if (TemplateArg.NumExpansions)
    return TemplateArg.NumExpansions - 1;

  return None;
}

QualType TemplateArgument::getNonTypeTemplateArgumentType() const {
  switch (getKind()) {
  case TemplateArgument::Null:
  case TemplateArgument::Type:
  case TemplateArgument::Template:
  case TemplateArgument::TemplateExpansion:
  case TemplateArgument::Pack:
    return QualType();

  case TemplateArgument::Integral:
    return getIntegralType();

  case TemplateArgument::Expression:
    return getAsExpr()->getType();

  case TemplateArgument::Declaration:
    return getParamTypeForDecl();

  case TemplateArgument::NullPtr:
    return getNullPtrType();
  }

  llvm_unreachable("Invalid TemplateArgument Kind!");
}

void TemplateArgument::Profile(llvm::FoldingSetNodeID &ID,
                               const ASTContext &Context) const {
  ID.AddInteger(getKind());
  switch (getKind()) {
  case Null:
    break;

  case Type:
    getAsType().Profile(ID);
    break;

  case NullPtr:
    getNullPtrType().Profile(ID);
    break;

  case Declaration:
    ID.AddPointer(getAsDecl()? getAsDecl()->getCanonicalDecl() : nullptr);
    break;

  case Template:
  case TemplateExpansion: {
    TemplateName Template = getAsTemplateOrTemplatePattern();
    if (TemplateTemplateParmDecl *TTP
          = dyn_cast_or_null<TemplateTemplateParmDecl>(
                                                Template.getAsTemplateDecl())) {
      ID.AddBoolean(true);
      ID.AddInteger(TTP->getDepth());
      ID.AddInteger(TTP->getPosition());
      ID.AddBoolean(TTP->isParameterPack());
    } else {
      ID.AddBoolean(false);
      ID.AddPointer(Context.getCanonicalTemplateName(Template)
                                                          .getAsVoidPointer());
    }
    break;
  }

  case Integral:
    getAsIntegral().Profile(ID);
    getIntegralType().Profile(ID);
    break;

  case Expression:
    getAsExpr()->Profile(ID, Context, true);
    break;

  case Pack:
    ID.AddInteger(Args.NumArgs);
    for (unsigned I = 0; I != Args.NumArgs; ++I)
      Args.Args[I].Profile(ID, Context);
  }
}

bool TemplateArgument::structurallyEquals(const TemplateArgument &Other) const {
  if (getKind() != Other.getKind()) return false;

  switch (getKind()) {
  case Null:
  case Type:
  case Expression:
  case Template:
  case TemplateExpansion:
  case NullPtr:
    return TypeOrValue.V == Other.TypeOrValue.V;

  case Declaration:
    return getAsDecl() == Other.getAsDecl();

  case Integral:
    return getIntegralType() == Other.getIntegralType() &&
           getAsIntegral() == Other.getAsIntegral();

  case Pack:
    if (Args.NumArgs != Other.Args.NumArgs) return false;
    for (unsigned I = 0, E = Args.NumArgs; I != E; ++I)
      if (!Args.Args[I].structurallyEquals(Other.Args.Args[I]))
        return false;
    return true;
  }

  llvm_unreachable("Invalid TemplateArgument Kind!");
}

TemplateArgument TemplateArgument::getPackExpansionPattern() const {
  assert(isPackExpansion());

  switch (getKind()) {
  case Type:
    return getAsType()->getAs<PackExpansionType>()->getPattern();

  case Expression:
    return cast<PackExpansionExpr>(getAsExpr())->getPattern();

  case TemplateExpansion:
    return TemplateArgument(getAsTemplateOrTemplatePattern());

  case Declaration:
  case Integral:
  case Pack:
  case Null:
  case Template:
  case NullPtr:
    return TemplateArgument();
  }

  llvm_unreachable("Invalid TemplateArgument Kind!");
}

void TemplateArgument::print(const PrintingPolicy &Policy,
                             raw_ostream &Out) const {
  switch (getKind()) {
  case Null:
    Out << "(no value)";
    break;

  case Type: {
    PrintingPolicy SubPolicy(Policy);
    SubPolicy.SuppressStrongLifetime = true;
    getAsType().print(Out, SubPolicy);
    break;
  }

  case Declaration: {
    NamedDecl *ND = getAsDecl();
    Out << '&';
    if (ND->getDeclName()) {
      // FIXME: distinguish between pointer and reference args?
      ND->printQualifiedName(Out);
    } else {
      Out << "(anonymous)";
    }
    break;
  }

  case NullPtr:
    Out << "nullptr";
    break;

  case Template:
    getAsTemplate().print(Out, Policy);
    break;

  case TemplateExpansion:
    getAsTemplateOrTemplatePattern().print(Out, Policy);
    Out << "...";
    break;

  case Integral:
    printIntegral(*this, Out, Policy);
    break;

  case Expression:
    getAsExpr()->printPretty(Out, nullptr, Policy);
    break;

  case Pack:
    Out << "<";
    bool First = true;
    for (const auto &P : pack_elements()) {
      if (First)
        First = false;
      else
        Out << ", ";

      P.print(Policy, Out);
    }
    Out << ">";
    break;
  }
}

void TemplateArgument::dump(raw_ostream &Out) const {
  LangOptions LO; // FIXME! see also TemplateName::dump().
  LO.CPlusPlus = true;
  LO.Bool = true;
  print(PrintingPolicy(LO), Out);
}

LLVM_DUMP_METHOD void TemplateArgument::dump() const { dump(llvm::errs()); }

//===----------------------------------------------------------------------===//
// TemplateArgumentLoc Implementation
//===----------------------------------------------------------------------===//

SourceRange TemplateArgumentLoc::getSourceRange() const {
  switch (Argument.getKind()) {
  case TemplateArgument::Expression:
    return getSourceExpression()->getSourceRange();

  case TemplateArgument::Declaration:
    return getSourceDeclExpression()->getSourceRange();

  case TemplateArgument::NullPtr:
    return getSourceNullPtrExpression()->getSourceRange();

  case TemplateArgument::Type:
    if (TypeSourceInfo *TSI = getTypeSourceInfo())
      return TSI->getTypeLoc().getSourceRange();
    else
      return SourceRange();

  case TemplateArgument::Template:
    if (getTemplateQualifierLoc())
      return SourceRange(getTemplateQualifierLoc().getBeginLoc(),
                         getTemplateNameLoc());
    return SourceRange(getTemplateNameLoc());

  case TemplateArgument::TemplateExpansion:
    if (getTemplateQualifierLoc())
      return SourceRange(getTemplateQualifierLoc().getBeginLoc(),
                         getTemplateEllipsisLoc());
    return SourceRange(getTemplateNameLoc(), getTemplateEllipsisLoc());

  case TemplateArgument::Integral:
    return getSourceIntegralExpression()->getSourceRange();

  case TemplateArgument::Pack:
  case TemplateArgument::Null:
    return SourceRange();
  }

  llvm_unreachable("Invalid TemplateArgument Kind!");
}

const DiagnosticBuilder &clang::operator<<(const DiagnosticBuilder &DB,
                                           const TemplateArgument &Arg) {
  switch (Arg.getKind()) {
  case TemplateArgument::Null:
    // This is bad, but not as bad as crashing because of argument
    // count mismatches.
    return DB << "(null template argument)";

  case TemplateArgument::Type:
    return DB << Arg.getAsType();

  case TemplateArgument::Declaration:
    return DB << Arg.getAsDecl();

  case TemplateArgument::NullPtr:
    return DB << "nullptr";

  case TemplateArgument::Integral:
    return DB << Arg.getAsIntegral().toString(10);

  case TemplateArgument::Template:
    return DB << Arg.getAsTemplate();

  case TemplateArgument::TemplateExpansion:
    return DB << Arg.getAsTemplateOrTemplatePattern() << "...";

  case TemplateArgument::Expression: {
    // This shouldn't actually ever happen, so it's okay that we're
    // regurgitating an expression here.
    // FIXME: We're guessing at LangOptions!
    SmallString<32> Str;
    llvm::raw_svector_ostream OS(Str);
    LangOptions LangOpts;
    LangOpts.CPlusPlus = true;
    PrintingPolicy Policy(LangOpts);
    Arg.getAsExpr()->printPretty(OS, nullptr, Policy);
    return DB << OS.str();
  }

  case TemplateArgument::Pack: {
    // FIXME: We're guessing at LangOptions!
    SmallString<32> Str;
    llvm::raw_svector_ostream OS(Str);
    LangOptions LangOpts;
    LangOpts.CPlusPlus = true;
    PrintingPolicy Policy(LangOpts);
    Arg.print(Policy, OS);
    return DB << OS.str();
  }
  }

  llvm_unreachable("Invalid TemplateArgument Kind!");
}

const ASTTemplateArgumentListInfo *
ASTTemplateArgumentListInfo::Create(ASTContext &C,
                                    const TemplateArgumentListInfo &List) {
  std::size_t size = totalSizeToAlloc<TemplateArgumentLoc>(List.size());
  void *Mem = C.Allocate(size, alignof(ASTTemplateArgumentListInfo));
  return new (Mem) ASTTemplateArgumentListInfo(List);
}

ASTTemplateArgumentListInfo::ASTTemplateArgumentListInfo(
    const TemplateArgumentListInfo &Info) {
  LAngleLoc = Info.getLAngleLoc();
  RAngleLoc = Info.getRAngleLoc();
  NumTemplateArgs = Info.size();

  TemplateArgumentLoc *ArgBuffer = getTrailingObjects<TemplateArgumentLoc>();
  for (unsigned i = 0; i != NumTemplateArgs; ++i)
    new (&ArgBuffer[i]) TemplateArgumentLoc(Info[i]);
}

void ASTTemplateKWAndArgsInfo::initializeFrom(
    SourceLocation TemplateKWLoc, const TemplateArgumentListInfo &Info,
    TemplateArgumentLoc *OutArgArray) {
  this->TemplateKWLoc = TemplateKWLoc;
  LAngleLoc = Info.getLAngleLoc();
  RAngleLoc = Info.getRAngleLoc();
  NumTemplateArgs = Info.size();

  for (unsigned i = 0; i != NumTemplateArgs; ++i)
    new (&OutArgArray[i]) TemplateArgumentLoc(Info[i]);
}

void ASTTemplateKWAndArgsInfo::initializeFrom(SourceLocation TemplateKWLoc) {
  assert(TemplateKWLoc.isValid());
  LAngleLoc = SourceLocation();
  RAngleLoc = SourceLocation();
  this->TemplateKWLoc = TemplateKWLoc;
  NumTemplateArgs = 0;
}

void ASTTemplateKWAndArgsInfo::initializeFrom(
    SourceLocation TemplateKWLoc, const TemplateArgumentListInfo &Info,
    TemplateArgumentLoc *OutArgArray, bool &Dependent,
    bool &InstantiationDependent, bool &ContainsUnexpandedParameterPack) {
  this->TemplateKWLoc = TemplateKWLoc;
  LAngleLoc = Info.getLAngleLoc();
  RAngleLoc = Info.getRAngleLoc();
  NumTemplateArgs = Info.size();

  for (unsigned i = 0; i != NumTemplateArgs; ++i) {
    Dependent = Dependent || Info[i].getArgument().isDependent();
    InstantiationDependent = InstantiationDependent ||
                             Info[i].getArgument().isInstantiationDependent();
    ContainsUnexpandedParameterPack =
        ContainsUnexpandedParameterPack ||
        Info[i].getArgument().containsUnexpandedParameterPack();

    new (&OutArgArray[i]) TemplateArgumentLoc(Info[i]);
  }
}

void ASTTemplateKWAndArgsInfo::copyInto(const TemplateArgumentLoc *ArgArray,
                                        TemplateArgumentListInfo &Info) const {
  Info.setLAngleLoc(LAngleLoc);
  Info.setRAngleLoc(RAngleLoc);
  for (unsigned I = 0; I != NumTemplateArgs; ++I)
    Info.addArgument(ArgArray[I]);
}
