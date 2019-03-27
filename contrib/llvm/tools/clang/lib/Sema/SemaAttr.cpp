//===--- SemaAttr.cpp - Semantic Analysis for Attributes ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements semantic analysis for non-trivial attributes and
// pragmas.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Expr.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/SemaInternal.h"
using namespace clang;

//===----------------------------------------------------------------------===//
// Pragma 'pack' and 'options align'
//===----------------------------------------------------------------------===//

Sema::PragmaStackSentinelRAII::PragmaStackSentinelRAII(Sema &S,
                                                       StringRef SlotLabel,
                                                       bool ShouldAct)
    : S(S), SlotLabel(SlotLabel), ShouldAct(ShouldAct) {
  if (ShouldAct) {
    S.VtorDispStack.SentinelAction(PSK_Push, SlotLabel);
    S.DataSegStack.SentinelAction(PSK_Push, SlotLabel);
    S.BSSSegStack.SentinelAction(PSK_Push, SlotLabel);
    S.ConstSegStack.SentinelAction(PSK_Push, SlotLabel);
    S.CodeSegStack.SentinelAction(PSK_Push, SlotLabel);
  }
}

Sema::PragmaStackSentinelRAII::~PragmaStackSentinelRAII() {
  if (ShouldAct) {
    S.VtorDispStack.SentinelAction(PSK_Pop, SlotLabel);
    S.DataSegStack.SentinelAction(PSK_Pop, SlotLabel);
    S.BSSSegStack.SentinelAction(PSK_Pop, SlotLabel);
    S.ConstSegStack.SentinelAction(PSK_Pop, SlotLabel);
    S.CodeSegStack.SentinelAction(PSK_Pop, SlotLabel);
  }
}

void Sema::AddAlignmentAttributesForRecord(RecordDecl *RD) {
  // If there is no pack value, we don't need any attributes.
  if (!PackStack.CurrentValue)
    return;

  // Otherwise, check to see if we need a max field alignment attribute.
  if (unsigned Alignment = PackStack.CurrentValue) {
    if (Alignment == Sema::kMac68kAlignmentSentinel)
      RD->addAttr(AlignMac68kAttr::CreateImplicit(Context));
    else
      RD->addAttr(MaxFieldAlignmentAttr::CreateImplicit(Context,
                                                        Alignment * 8));
  }
  if (PackIncludeStack.empty())
    return;
  // The #pragma pack affected a record in an included file,  so Clang should
  // warn when that pragma was written in a file that included the included
  // file.
  for (auto &PackedInclude : llvm::reverse(PackIncludeStack)) {
    if (PackedInclude.CurrentPragmaLocation != PackStack.CurrentPragmaLocation)
      break;
    if (PackedInclude.HasNonDefaultValue)
      PackedInclude.ShouldWarnOnInclude = true;
  }
}

void Sema::AddMsStructLayoutForRecord(RecordDecl *RD) {
  if (MSStructPragmaOn)
    RD->addAttr(MSStructAttr::CreateImplicit(Context));

  // FIXME: We should merge AddAlignmentAttributesForRecord with
  // AddMsStructLayoutForRecord into AddPragmaAttributesForRecord, which takes
  // all active pragmas and applies them as attributes to class definitions.
  if (VtorDispStack.CurrentValue != getLangOpts().VtorDispMode)
    RD->addAttr(
        MSVtorDispAttr::CreateImplicit(Context, VtorDispStack.CurrentValue));
}

void Sema::ActOnPragmaOptionsAlign(PragmaOptionsAlignKind Kind,
                                   SourceLocation PragmaLoc) {
  PragmaMsStackAction Action = Sema::PSK_Reset;
  unsigned Alignment = 0;
  switch (Kind) {
    // For all targets we support native and natural are the same.
    //
    // FIXME: This is not true on Darwin/PPC.
  case POAK_Native:
  case POAK_Power:
  case POAK_Natural:
    Action = Sema::PSK_Push_Set;
    Alignment = 0;
    break;

    // Note that '#pragma options align=packed' is not equivalent to attribute
    // packed, it has a different precedence relative to attribute aligned.
  case POAK_Packed:
    Action = Sema::PSK_Push_Set;
    Alignment = 1;
    break;

  case POAK_Mac68k:
    // Check if the target supports this.
    if (!this->Context.getTargetInfo().hasAlignMac68kSupport()) {
      Diag(PragmaLoc, diag::err_pragma_options_align_mac68k_target_unsupported);
      return;
    }
    Action = Sema::PSK_Push_Set;
    Alignment = Sema::kMac68kAlignmentSentinel;
    break;

  case POAK_Reset:
    // Reset just pops the top of the stack, or resets the current alignment to
    // default.
    Action = Sema::PSK_Pop;
    if (PackStack.Stack.empty()) {
      if (PackStack.CurrentValue) {
        Action = Sema::PSK_Reset;
      } else {
        Diag(PragmaLoc, diag::warn_pragma_options_align_reset_failed)
            << "stack empty";
        return;
      }
    }
    break;
  }

  PackStack.Act(PragmaLoc, Action, StringRef(), Alignment);
}

void Sema::ActOnPragmaClangSection(SourceLocation PragmaLoc, PragmaClangSectionAction Action,
                                   PragmaClangSectionKind SecKind, StringRef SecName) {
  PragmaClangSection *CSec;
  switch (SecKind) {
    case PragmaClangSectionKind::PCSK_BSS:
      CSec = &PragmaClangBSSSection;
      break;
    case PragmaClangSectionKind::PCSK_Data:
      CSec = &PragmaClangDataSection;
      break;
    case PragmaClangSectionKind::PCSK_Rodata:
      CSec = &PragmaClangRodataSection;
      break;
    case PragmaClangSectionKind::PCSK_Text:
      CSec = &PragmaClangTextSection;
      break;
    default:
      llvm_unreachable("invalid clang section kind");
  }

  if (Action == PragmaClangSectionAction::PCSA_Clear) {
    CSec->Valid = false;
    return;
  }

  CSec->Valid = true;
  CSec->SectionName = SecName;
  CSec->PragmaLocation = PragmaLoc;
}

void Sema::ActOnPragmaPack(SourceLocation PragmaLoc, PragmaMsStackAction Action,
                           StringRef SlotLabel, Expr *alignment) {
  Expr *Alignment = static_cast<Expr *>(alignment);

  // If specified then alignment must be a "small" power of two.
  unsigned AlignmentVal = 0;
  if (Alignment) {
    llvm::APSInt Val;

    // pack(0) is like pack(), which just works out since that is what
    // we use 0 for in PackAttr.
    if (Alignment->isTypeDependent() ||
        Alignment->isValueDependent() ||
        !Alignment->isIntegerConstantExpr(Val, Context) ||
        !(Val == 0 || Val.isPowerOf2()) ||
        Val.getZExtValue() > 16) {
      Diag(PragmaLoc, diag::warn_pragma_pack_invalid_alignment);
      return; // Ignore
    }

    AlignmentVal = (unsigned) Val.getZExtValue();
  }
  if (Action == Sema::PSK_Show) {
    // Show the current alignment, making sure to show the right value
    // for the default.
    // FIXME: This should come from the target.
    AlignmentVal = PackStack.CurrentValue;
    if (AlignmentVal == 0)
      AlignmentVal = 8;
    if (AlignmentVal == Sema::kMac68kAlignmentSentinel)
      Diag(PragmaLoc, diag::warn_pragma_pack_show) << "mac68k";
    else
      Diag(PragmaLoc, diag::warn_pragma_pack_show) << AlignmentVal;
  }
  // MSDN, C/C++ Preprocessor Reference > Pragma Directives > pack:
  // "#pragma pack(pop, identifier, n) is undefined"
  if (Action & Sema::PSK_Pop) {
    if (Alignment && !SlotLabel.empty())
      Diag(PragmaLoc, diag::warn_pragma_pack_pop_identifier_and_alignment);
    if (PackStack.Stack.empty())
      Diag(PragmaLoc, diag::warn_pragma_pop_failed) << "pack" << "stack empty";
  }

  PackStack.Act(PragmaLoc, Action, SlotLabel, AlignmentVal);
}

void Sema::DiagnoseNonDefaultPragmaPack(PragmaPackDiagnoseKind Kind,
                                        SourceLocation IncludeLoc) {
  if (Kind == PragmaPackDiagnoseKind::NonDefaultStateAtInclude) {
    SourceLocation PrevLocation = PackStack.CurrentPragmaLocation;
    // Warn about non-default alignment at #includes (without redundant
    // warnings for the same directive in nested includes).
    // The warning is delayed until the end of the file to avoid warnings
    // for files that don't have any records that are affected by the modified
    // alignment.
    bool HasNonDefaultValue =
        PackStack.hasValue() &&
        (PackIncludeStack.empty() ||
         PackIncludeStack.back().CurrentPragmaLocation != PrevLocation);
    PackIncludeStack.push_back(
        {PackStack.CurrentValue,
         PackStack.hasValue() ? PrevLocation : SourceLocation(),
         HasNonDefaultValue, /*ShouldWarnOnInclude*/ false});
    return;
  }

  assert(Kind == PragmaPackDiagnoseKind::ChangedStateAtExit && "invalid kind");
  PackIncludeState PrevPackState = PackIncludeStack.pop_back_val();
  if (PrevPackState.ShouldWarnOnInclude) {
    // Emit the delayed non-default alignment at #include warning.
    Diag(IncludeLoc, diag::warn_pragma_pack_non_default_at_include);
    Diag(PrevPackState.CurrentPragmaLocation, diag::note_pragma_pack_here);
  }
  // Warn about modified alignment after #includes.
  if (PrevPackState.CurrentValue != PackStack.CurrentValue) {
    Diag(IncludeLoc, diag::warn_pragma_pack_modified_after_include);
    Diag(PackStack.CurrentPragmaLocation, diag::note_pragma_pack_here);
  }
}

void Sema::DiagnoseUnterminatedPragmaPack() {
  if (PackStack.Stack.empty())
    return;
  bool IsInnermost = true;
  for (const auto &StackSlot : llvm::reverse(PackStack.Stack)) {
    Diag(StackSlot.PragmaPushLocation, diag::warn_pragma_pack_no_pop_eof);
    // The user might have already reset the alignment, so suggest replacing
    // the reset with a pop.
    if (IsInnermost && PackStack.CurrentValue == PackStack.DefaultValue) {
      DiagnosticBuilder DB = Diag(PackStack.CurrentPragmaLocation,
                                  diag::note_pragma_pack_pop_instead_reset);
      SourceLocation FixItLoc = Lexer::findLocationAfterToken(
          PackStack.CurrentPragmaLocation, tok::l_paren, SourceMgr, LangOpts,
          /*SkipTrailing=*/false);
      if (FixItLoc.isValid())
        DB << FixItHint::CreateInsertion(FixItLoc, "pop");
    }
    IsInnermost = false;
  }
}

void Sema::ActOnPragmaMSStruct(PragmaMSStructKind Kind) {
  MSStructPragmaOn = (Kind == PMSST_ON);
}

void Sema::ActOnPragmaMSComment(SourceLocation CommentLoc,
                                PragmaMSCommentKind Kind, StringRef Arg) {
  auto *PCD = PragmaCommentDecl::Create(
      Context, Context.getTranslationUnitDecl(), CommentLoc, Kind, Arg);
  Context.getTranslationUnitDecl()->addDecl(PCD);
  Consumer.HandleTopLevelDecl(DeclGroupRef(PCD));
}

void Sema::ActOnPragmaDetectMismatch(SourceLocation Loc, StringRef Name,
                                     StringRef Value) {
  auto *PDMD = PragmaDetectMismatchDecl::Create(
      Context, Context.getTranslationUnitDecl(), Loc, Name, Value);
  Context.getTranslationUnitDecl()->addDecl(PDMD);
  Consumer.HandleTopLevelDecl(DeclGroupRef(PDMD));
}

void Sema::ActOnPragmaMSPointersToMembers(
    LangOptions::PragmaMSPointersToMembersKind RepresentationMethod,
    SourceLocation PragmaLoc) {
  MSPointerToMemberRepresentationMethod = RepresentationMethod;
  ImplicitMSInheritanceAttrLoc = PragmaLoc;
}

void Sema::ActOnPragmaMSVtorDisp(PragmaMsStackAction Action,
                                 SourceLocation PragmaLoc,
                                 MSVtorDispAttr::Mode Mode) {
  if (Action & PSK_Pop && VtorDispStack.Stack.empty())
    Diag(PragmaLoc, diag::warn_pragma_pop_failed) << "vtordisp"
                                                  << "stack empty";
  VtorDispStack.Act(PragmaLoc, Action, StringRef(), Mode);
}

template<typename ValueType>
void Sema::PragmaStack<ValueType>::Act(SourceLocation PragmaLocation,
                                       PragmaMsStackAction Action,
                                       llvm::StringRef StackSlotLabel,
                                       ValueType Value) {
  if (Action == PSK_Reset) {
    CurrentValue = DefaultValue;
    CurrentPragmaLocation = PragmaLocation;
    return;
  }
  if (Action & PSK_Push)
    Stack.emplace_back(StackSlotLabel, CurrentValue, CurrentPragmaLocation,
                       PragmaLocation);
  else if (Action & PSK_Pop) {
    if (!StackSlotLabel.empty()) {
      // If we've got a label, try to find it and jump there.
      auto I = llvm::find_if(llvm::reverse(Stack), [&](const Slot &x) {
        return x.StackSlotLabel == StackSlotLabel;
      });
      // If we found the label so pop from there.
      if (I != Stack.rend()) {
        CurrentValue = I->Value;
        CurrentPragmaLocation = I->PragmaLocation;
        Stack.erase(std::prev(I.base()), Stack.end());
      }
    } else if (!Stack.empty()) {
      // We do not have a label, just pop the last entry.
      CurrentValue = Stack.back().Value;
      CurrentPragmaLocation = Stack.back().PragmaLocation;
      Stack.pop_back();
    }
  }
  if (Action & PSK_Set) {
    CurrentValue = Value;
    CurrentPragmaLocation = PragmaLocation;
  }
}

bool Sema::UnifySection(StringRef SectionName,
                        int SectionFlags,
                        DeclaratorDecl *Decl) {
  auto Section = Context.SectionInfos.find(SectionName);
  if (Section == Context.SectionInfos.end()) {
    Context.SectionInfos[SectionName] =
        ASTContext::SectionInfo(Decl, SourceLocation(), SectionFlags);
    return false;
  }
  // A pre-declared section takes precedence w/o diagnostic.
  if (Section->second.SectionFlags == SectionFlags ||
      !(Section->second.SectionFlags & ASTContext::PSF_Implicit))
    return false;
  auto OtherDecl = Section->second.Decl;
  Diag(Decl->getLocation(), diag::err_section_conflict)
      << Decl << OtherDecl;
  Diag(OtherDecl->getLocation(), diag::note_declared_at)
      << OtherDecl->getName();
  if (auto A = Decl->getAttr<SectionAttr>())
    if (A->isImplicit())
      Diag(A->getLocation(), diag::note_pragma_entered_here);
  if (auto A = OtherDecl->getAttr<SectionAttr>())
    if (A->isImplicit())
      Diag(A->getLocation(), diag::note_pragma_entered_here);
  return true;
}

bool Sema::UnifySection(StringRef SectionName,
                        int SectionFlags,
                        SourceLocation PragmaSectionLocation) {
  auto Section = Context.SectionInfos.find(SectionName);
  if (Section != Context.SectionInfos.end()) {
    if (Section->second.SectionFlags == SectionFlags)
      return false;
    if (!(Section->second.SectionFlags & ASTContext::PSF_Implicit)) {
      Diag(PragmaSectionLocation, diag::err_section_conflict)
          << "this" << "a prior #pragma section";
      Diag(Section->second.PragmaSectionLocation,
           diag::note_pragma_entered_here);
      return true;
    }
  }
  Context.SectionInfos[SectionName] =
      ASTContext::SectionInfo(nullptr, PragmaSectionLocation, SectionFlags);
  return false;
}

/// Called on well formed \#pragma bss_seg().
void Sema::ActOnPragmaMSSeg(SourceLocation PragmaLocation,
                            PragmaMsStackAction Action,
                            llvm::StringRef StackSlotLabel,
                            StringLiteral *SegmentName,
                            llvm::StringRef PragmaName) {
  PragmaStack<StringLiteral *> *Stack =
    llvm::StringSwitch<PragmaStack<StringLiteral *> *>(PragmaName)
        .Case("data_seg", &DataSegStack)
        .Case("bss_seg", &BSSSegStack)
        .Case("const_seg", &ConstSegStack)
        .Case("code_seg", &CodeSegStack);
  if (Action & PSK_Pop && Stack->Stack.empty())
    Diag(PragmaLocation, diag::warn_pragma_pop_failed) << PragmaName
        << "stack empty";
  if (SegmentName &&
      !checkSectionName(SegmentName->getBeginLoc(), SegmentName->getString()))
    return;
  Stack->Act(PragmaLocation, Action, StackSlotLabel, SegmentName);
}

/// Called on well formed \#pragma bss_seg().
void Sema::ActOnPragmaMSSection(SourceLocation PragmaLocation,
                                int SectionFlags, StringLiteral *SegmentName) {
  UnifySection(SegmentName->getString(), SectionFlags, PragmaLocation);
}

void Sema::ActOnPragmaMSInitSeg(SourceLocation PragmaLocation,
                                StringLiteral *SegmentName) {
  // There's no stack to maintain, so we just have a current section.  When we
  // see the default section, reset our current section back to null so we stop
  // tacking on unnecessary attributes.
  CurInitSeg = SegmentName->getString() == ".CRT$XCU" ? nullptr : SegmentName;
  CurInitSegLoc = PragmaLocation;
}

void Sema::ActOnPragmaUnused(const Token &IdTok, Scope *curScope,
                             SourceLocation PragmaLoc) {

  IdentifierInfo *Name = IdTok.getIdentifierInfo();
  LookupResult Lookup(*this, Name, IdTok.getLocation(), LookupOrdinaryName);
  LookupParsedName(Lookup, curScope, nullptr, true);

  if (Lookup.empty()) {
    Diag(PragmaLoc, diag::warn_pragma_unused_undeclared_var)
      << Name << SourceRange(IdTok.getLocation());
    return;
  }

  VarDecl *VD = Lookup.getAsSingle<VarDecl>();
  if (!VD) {
    Diag(PragmaLoc, diag::warn_pragma_unused_expected_var_arg)
      << Name << SourceRange(IdTok.getLocation());
    return;
  }

  // Warn if this was used before being marked unused.
  if (VD->isUsed())
    Diag(PragmaLoc, diag::warn_used_but_marked_unused) << Name;

  VD->addAttr(UnusedAttr::CreateImplicit(Context, UnusedAttr::GNU_unused,
                                         IdTok.getLocation()));
}

void Sema::AddCFAuditedAttribute(Decl *D) {
  SourceLocation Loc = PP.getPragmaARCCFCodeAuditedLoc();
  if (!Loc.isValid()) return;

  // Don't add a redundant or conflicting attribute.
  if (D->hasAttr<CFAuditedTransferAttr>() ||
      D->hasAttr<CFUnknownTransferAttr>())
    return;

  D->addAttr(CFAuditedTransferAttr::CreateImplicit(Context, Loc));
}

namespace {

Optional<attr::SubjectMatchRule>
getParentAttrMatcherRule(attr::SubjectMatchRule Rule) {
  using namespace attr;
  switch (Rule) {
  default:
    return None;
#define ATTR_MATCH_RULE(Value, Spelling, IsAbstract)
#define ATTR_MATCH_SUB_RULE(Value, Spelling, IsAbstract, Parent, IsNegated)    \
  case Value:                                                                  \
    return Parent;
#include "clang/Basic/AttrSubMatchRulesList.inc"
  }
}

bool isNegatedAttrMatcherSubRule(attr::SubjectMatchRule Rule) {
  using namespace attr;
  switch (Rule) {
  default:
    return false;
#define ATTR_MATCH_RULE(Value, Spelling, IsAbstract)
#define ATTR_MATCH_SUB_RULE(Value, Spelling, IsAbstract, Parent, IsNegated)    \
  case Value:                                                                  \
    return IsNegated;
#include "clang/Basic/AttrSubMatchRulesList.inc"
  }
}

CharSourceRange replacementRangeForListElement(const Sema &S,
                                               SourceRange Range) {
  // Make sure that the ',' is removed as well.
  SourceLocation AfterCommaLoc = Lexer::findLocationAfterToken(
      Range.getEnd(), tok::comma, S.getSourceManager(), S.getLangOpts(),
      /*SkipTrailingWhitespaceAndNewLine=*/false);
  if (AfterCommaLoc.isValid())
    return CharSourceRange::getCharRange(Range.getBegin(), AfterCommaLoc);
  else
    return CharSourceRange::getTokenRange(Range);
}

std::string
attrMatcherRuleListToString(ArrayRef<attr::SubjectMatchRule> Rules) {
  std::string Result;
  llvm::raw_string_ostream OS(Result);
  for (const auto &I : llvm::enumerate(Rules)) {
    if (I.index())
      OS << (I.index() == Rules.size() - 1 ? ", and " : ", ");
    OS << "'" << attr::getSubjectMatchRuleSpelling(I.value()) << "'";
  }
  return OS.str();
}

} // end anonymous namespace

void Sema::ActOnPragmaAttributeAttribute(
    ParsedAttr &Attribute, SourceLocation PragmaLoc,
    attr::ParsedSubjectMatchRuleSet Rules) {
  SmallVector<attr::SubjectMatchRule, 4> SubjectMatchRules;
  // Gather the subject match rules that are supported by the attribute.
  SmallVector<std::pair<attr::SubjectMatchRule, bool>, 4>
      StrictSubjectMatchRuleSet;
  Attribute.getMatchRules(LangOpts, StrictSubjectMatchRuleSet);

  // Figure out which subject matching rules are valid.
  if (StrictSubjectMatchRuleSet.empty()) {
    // Check for contradicting match rules. Contradicting match rules are
    // either:
    //  - a top-level rule and one of its sub-rules. E.g. variable and
    //    variable(is_parameter).
    //  - a sub-rule and a sibling that's negated. E.g.
    //    variable(is_thread_local) and variable(unless(is_parameter))
    llvm::SmallDenseMap<int, std::pair<int, SourceRange>, 2>
        RulesToFirstSpecifiedNegatedSubRule;
    for (const auto &Rule : Rules) {
      attr::SubjectMatchRule MatchRule = attr::SubjectMatchRule(Rule.first);
      Optional<attr::SubjectMatchRule> ParentRule =
          getParentAttrMatcherRule(MatchRule);
      if (!ParentRule)
        continue;
      auto It = Rules.find(*ParentRule);
      if (It != Rules.end()) {
        // A sub-rule contradicts a parent rule.
        Diag(Rule.second.getBegin(),
             diag::err_pragma_attribute_matcher_subrule_contradicts_rule)
            << attr::getSubjectMatchRuleSpelling(MatchRule)
            << attr::getSubjectMatchRuleSpelling(*ParentRule) << It->second
            << FixItHint::CreateRemoval(
                   replacementRangeForListElement(*this, Rule.second));
        // Keep going without removing this rule as it won't change the set of
        // declarations that receive the attribute.
        continue;
      }
      if (isNegatedAttrMatcherSubRule(MatchRule))
        RulesToFirstSpecifiedNegatedSubRule.insert(
            std::make_pair(*ParentRule, Rule));
    }
    bool IgnoreNegatedSubRules = false;
    for (const auto &Rule : Rules) {
      attr::SubjectMatchRule MatchRule = attr::SubjectMatchRule(Rule.first);
      Optional<attr::SubjectMatchRule> ParentRule =
          getParentAttrMatcherRule(MatchRule);
      if (!ParentRule)
        continue;
      auto It = RulesToFirstSpecifiedNegatedSubRule.find(*ParentRule);
      if (It != RulesToFirstSpecifiedNegatedSubRule.end() &&
          It->second != Rule) {
        // Negated sub-rule contradicts another sub-rule.
        Diag(
            It->second.second.getBegin(),
            diag::
                err_pragma_attribute_matcher_negated_subrule_contradicts_subrule)
            << attr::getSubjectMatchRuleSpelling(
                   attr::SubjectMatchRule(It->second.first))
            << attr::getSubjectMatchRuleSpelling(MatchRule) << Rule.second
            << FixItHint::CreateRemoval(
                   replacementRangeForListElement(*this, It->second.second));
        // Keep going but ignore all of the negated sub-rules.
        IgnoreNegatedSubRules = true;
        RulesToFirstSpecifiedNegatedSubRule.erase(It);
      }
    }

    if (!IgnoreNegatedSubRules) {
      for (const auto &Rule : Rules)
        SubjectMatchRules.push_back(attr::SubjectMatchRule(Rule.first));
    } else {
      for (const auto &Rule : Rules) {
        if (!isNegatedAttrMatcherSubRule(attr::SubjectMatchRule(Rule.first)))
          SubjectMatchRules.push_back(attr::SubjectMatchRule(Rule.first));
      }
    }
    Rules.clear();
  } else {
    for (const auto &Rule : StrictSubjectMatchRuleSet) {
      if (Rules.erase(Rule.first)) {
        // Add the rule to the set of attribute receivers only if it's supported
        // in the current language mode.
        if (Rule.second)
          SubjectMatchRules.push_back(Rule.first);
      }
    }
  }

  if (!Rules.empty()) {
    auto Diagnostic =
        Diag(PragmaLoc, diag::err_pragma_attribute_invalid_matchers)
        << Attribute.getName();
    SmallVector<attr::SubjectMatchRule, 2> ExtraRules;
    for (const auto &Rule : Rules) {
      ExtraRules.push_back(attr::SubjectMatchRule(Rule.first));
      Diagnostic << FixItHint::CreateRemoval(
          replacementRangeForListElement(*this, Rule.second));
    }
    Diagnostic << attrMatcherRuleListToString(ExtraRules);
  }

  if (PragmaAttributeStack.empty()) {
    Diag(PragmaLoc, diag::err_pragma_attr_attr_no_push);
    return;
  }

  PragmaAttributeStack.back().Entries.push_back(
      {PragmaLoc, &Attribute, std::move(SubjectMatchRules), /*IsUsed=*/false});
}

void Sema::ActOnPragmaAttributeEmptyPush(SourceLocation PragmaLoc,
                                         const IdentifierInfo *Namespace) {
  PragmaAttributeStack.emplace_back();
  PragmaAttributeStack.back().Loc = PragmaLoc;
  PragmaAttributeStack.back().Namespace = Namespace;
}

void Sema::ActOnPragmaAttributePop(SourceLocation PragmaLoc,
                                   const IdentifierInfo *Namespace) {
  if (PragmaAttributeStack.empty()) {
    Diag(PragmaLoc, diag::err_pragma_attribute_stack_mismatch) << 1;
    return;
  }

  // Dig back through the stack trying to find the most recently pushed group
  // that in Namespace. Note that this works fine if no namespace is present,
  // think of push/pops without namespaces as having an implicit "nullptr"
  // namespace.
  for (size_t Index = PragmaAttributeStack.size(); Index;) {
    --Index;
    if (PragmaAttributeStack[Index].Namespace == Namespace) {
      for (const PragmaAttributeEntry &Entry :
           PragmaAttributeStack[Index].Entries) {
        if (!Entry.IsUsed) {
          assert(Entry.Attribute && "Expected an attribute");
          Diag(Entry.Attribute->getLoc(), diag::warn_pragma_attribute_unused)
              << *Entry.Attribute;
          Diag(PragmaLoc, diag::note_pragma_attribute_region_ends_here);
        }
      }
      PragmaAttributeStack.erase(PragmaAttributeStack.begin() + Index);
      return;
    }
  }

  if (Namespace)
    Diag(PragmaLoc, diag::err_pragma_attribute_stack_mismatch)
        << 0 << Namespace->getName();
  else
    Diag(PragmaLoc, diag::err_pragma_attribute_stack_mismatch) << 1;
}

void Sema::AddPragmaAttributes(Scope *S, Decl *D) {
  if (PragmaAttributeStack.empty())
    return;
  for (auto &Group : PragmaAttributeStack) {
    for (auto &Entry : Group.Entries) {
      ParsedAttr *Attribute = Entry.Attribute;
      assert(Attribute && "Expected an attribute");

      // Ensure that the attribute can be applied to the given declaration.
      bool Applies = false;
      for (const auto &Rule : Entry.MatchRules) {
        if (Attribute->appliesToDecl(D, Rule)) {
          Applies = true;
          break;
        }
      }
      if (!Applies)
        continue;
      Entry.IsUsed = true;
      PragmaAttributeCurrentTargetDecl = D;
      ParsedAttributesView Attrs;
      Attrs.addAtEnd(Attribute);
      ProcessDeclAttributeList(S, D, Attrs);
      PragmaAttributeCurrentTargetDecl = nullptr;
    }
  }
}

void Sema::PrintPragmaAttributeInstantiationPoint() {
  assert(PragmaAttributeCurrentTargetDecl && "Expected an active declaration");
  Diags.Report(PragmaAttributeCurrentTargetDecl->getBeginLoc(),
               diag::note_pragma_attribute_applied_decl_here);
}

void Sema::DiagnoseUnterminatedPragmaAttribute() {
  if (PragmaAttributeStack.empty())
    return;
  Diag(PragmaAttributeStack.back().Loc, diag::err_pragma_attribute_no_pop_eof);
}

void Sema::ActOnPragmaOptimize(bool On, SourceLocation PragmaLoc) {
  if(On)
    OptimizeOffPragmaLocation = SourceLocation();
  else
    OptimizeOffPragmaLocation = PragmaLoc;
}

void Sema::AddRangeBasedOptnone(FunctionDecl *FD) {
  // In the future, check other pragmas if they're implemented (e.g. pragma
  // optimize 0 will probably map to this functionality too).
  if(OptimizeOffPragmaLocation.isValid())
    AddOptnoneAttributeIfNoConflicts(FD, OptimizeOffPragmaLocation);
}

void Sema::AddOptnoneAttributeIfNoConflicts(FunctionDecl *FD,
                                            SourceLocation Loc) {
  // Don't add a conflicting attribute. No diagnostic is needed.
  if (FD->hasAttr<MinSizeAttr>() || FD->hasAttr<AlwaysInlineAttr>())
    return;

  // Add attributes only if required. Optnone requires noinline as well, but if
  // either is already present then don't bother adding them.
  if (!FD->hasAttr<OptimizeNoneAttr>())
    FD->addAttr(OptimizeNoneAttr::CreateImplicit(Context, Loc));
  if (!FD->hasAttr<NoInlineAttr>())
    FD->addAttr(NoInlineAttr::CreateImplicit(Context, Loc));
}

typedef std::vector<std::pair<unsigned, SourceLocation> > VisStack;
enum : unsigned { NoVisibility = ~0U };

void Sema::AddPushedVisibilityAttribute(Decl *D) {
  if (!VisContext)
    return;

  NamedDecl *ND = dyn_cast<NamedDecl>(D);
  if (ND && ND->getExplicitVisibility(NamedDecl::VisibilityForValue))
    return;

  VisStack *Stack = static_cast<VisStack*>(VisContext);
  unsigned rawType = Stack->back().first;
  if (rawType == NoVisibility) return;

  VisibilityAttr::VisibilityType type
    = (VisibilityAttr::VisibilityType) rawType;
  SourceLocation loc = Stack->back().second;

  D->addAttr(VisibilityAttr::CreateImplicit(Context, type, loc));
}

/// FreeVisContext - Deallocate and null out VisContext.
void Sema::FreeVisContext() {
  delete static_cast<VisStack*>(VisContext);
  VisContext = nullptr;
}

static void PushPragmaVisibility(Sema &S, unsigned type, SourceLocation loc) {
  // Put visibility on stack.
  if (!S.VisContext)
    S.VisContext = new VisStack;

  VisStack *Stack = static_cast<VisStack*>(S.VisContext);
  Stack->push_back(std::make_pair(type, loc));
}

void Sema::ActOnPragmaVisibility(const IdentifierInfo* VisType,
                                 SourceLocation PragmaLoc) {
  if (VisType) {
    // Compute visibility to use.
    VisibilityAttr::VisibilityType T;
    if (!VisibilityAttr::ConvertStrToVisibilityType(VisType->getName(), T)) {
      Diag(PragmaLoc, diag::warn_attribute_unknown_visibility) << VisType;
      return;
    }
    PushPragmaVisibility(*this, T, PragmaLoc);
  } else {
    PopPragmaVisibility(false, PragmaLoc);
  }
}

void Sema::ActOnPragmaFPContract(LangOptions::FPContractModeKind FPC) {
  switch (FPC) {
  case LangOptions::FPC_On:
    FPFeatures.setAllowFPContractWithinStatement();
    break;
  case LangOptions::FPC_Fast:
    FPFeatures.setAllowFPContractAcrossStatement();
    break;
  case LangOptions::FPC_Off:
    FPFeatures.setDisallowFPContract();
    break;
  }
}

void Sema::ActOnPragmaFEnvAccess(LangOptions::FEnvAccessModeKind FPC) {
  switch (FPC) {
  case LangOptions::FEA_On:
    FPFeatures.setAllowFEnvAccess();
    break;
  case LangOptions::FEA_Off:
    FPFeatures.setDisallowFEnvAccess();
    break;
  }
}


void Sema::PushNamespaceVisibilityAttr(const VisibilityAttr *Attr,
                                       SourceLocation Loc) {
  // Visibility calculations will consider the namespace's visibility.
  // Here we just want to note that we're in a visibility context
  // which overrides any enclosing #pragma context, but doesn't itself
  // contribute visibility.
  PushPragmaVisibility(*this, NoVisibility, Loc);
}

void Sema::PopPragmaVisibility(bool IsNamespaceEnd, SourceLocation EndLoc) {
  if (!VisContext) {
    Diag(EndLoc, diag::err_pragma_pop_visibility_mismatch);
    return;
  }

  // Pop visibility from stack
  VisStack *Stack = static_cast<VisStack*>(VisContext);

  const std::pair<unsigned, SourceLocation> *Back = &Stack->back();
  bool StartsWithPragma = Back->first != NoVisibility;
  if (StartsWithPragma && IsNamespaceEnd) {
    Diag(Back->second, diag::err_pragma_push_visibility_mismatch);
    Diag(EndLoc, diag::note_surrounding_namespace_ends_here);

    // For better error recovery, eat all pushes inside the namespace.
    do {
      Stack->pop_back();
      Back = &Stack->back();
      StartsWithPragma = Back->first != NoVisibility;
    } while (StartsWithPragma);
  } else if (!StartsWithPragma && !IsNamespaceEnd) {
    Diag(EndLoc, diag::err_pragma_pop_visibility_mismatch);
    Diag(Back->second, diag::note_surrounding_namespace_starts_here);
    return;
  }

  Stack->pop_back();
  // To simplify the implementation, never keep around an empty stack.
  if (Stack->empty())
    FreeVisContext();
}
