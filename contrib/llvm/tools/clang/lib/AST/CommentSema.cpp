//===--- CommentSema.cpp - Doxygen comment semantic analysis --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/CommentSema.h"
#include "clang/AST/Attr.h"
#include "clang/AST/CommentCommandTraits.h"
#include "clang/AST/CommentDiagnostic.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringSwitch.h"

namespace clang {
namespace comments {

namespace {
#include "clang/AST/CommentHTMLTagsProperties.inc"
} // end anonymous namespace

Sema::Sema(llvm::BumpPtrAllocator &Allocator, const SourceManager &SourceMgr,
           DiagnosticsEngine &Diags, CommandTraits &Traits,
           const Preprocessor *PP) :
    Allocator(Allocator), SourceMgr(SourceMgr), Diags(Diags), Traits(Traits),
    PP(PP), ThisDeclInfo(nullptr), BriefCommand(nullptr),
    HeaderfileCommand(nullptr) {
}

void Sema::setDecl(const Decl *D) {
  if (!D)
    return;

  ThisDeclInfo = new (Allocator) DeclInfo;
  ThisDeclInfo->CommentDecl = D;
  ThisDeclInfo->IsFilled = false;
}

ParagraphComment *Sema::actOnParagraphComment(
                              ArrayRef<InlineContentComment *> Content) {
  return new (Allocator) ParagraphComment(Content);
}

BlockCommandComment *Sema::actOnBlockCommandStart(
                                      SourceLocation LocBegin,
                                      SourceLocation LocEnd,
                                      unsigned CommandID,
                                      CommandMarkerKind CommandMarker) {
  BlockCommandComment *BC = new (Allocator) BlockCommandComment(LocBegin, LocEnd,
                                                                CommandID,
                                                                CommandMarker);
  checkContainerDecl(BC);
  return BC;
}

void Sema::actOnBlockCommandArgs(BlockCommandComment *Command,
                                 ArrayRef<BlockCommandComment::Argument> Args) {
  Command->setArgs(Args);
}

void Sema::actOnBlockCommandFinish(BlockCommandComment *Command,
                                   ParagraphComment *Paragraph) {
  Command->setParagraph(Paragraph);
  checkBlockCommandEmptyParagraph(Command);
  checkBlockCommandDuplicate(Command);
  if (ThisDeclInfo) {
    // These checks only make sense if the comment is attached to a
    // declaration.
    checkReturnsCommand(Command);
    checkDeprecatedCommand(Command);
  }
}

ParamCommandComment *Sema::actOnParamCommandStart(
                                      SourceLocation LocBegin,
                                      SourceLocation LocEnd,
                                      unsigned CommandID,
                                      CommandMarkerKind CommandMarker) {
  ParamCommandComment *Command =
      new (Allocator) ParamCommandComment(LocBegin, LocEnd, CommandID,
                                          CommandMarker);

  if (!isFunctionDecl() && !isFunctionOrBlockPointerVarLikeDecl())
    Diag(Command->getLocation(),
         diag::warn_doc_param_not_attached_to_a_function_decl)
      << CommandMarker
      << Command->getCommandNameRange(Traits);

  return Command;
}

void Sema::checkFunctionDeclVerbatimLine(const BlockCommandComment *Comment) {
  const CommandInfo *Info = Traits.getCommandInfo(Comment->getCommandID());
  if (!Info->IsFunctionDeclarationCommand)
    return;

  unsigned DiagSelect;
  switch (Comment->getCommandID()) {
    case CommandTraits::KCI_function:
      DiagSelect = (!isAnyFunctionDecl() && !isFunctionTemplateDecl())? 1 : 0;
      break;
    case CommandTraits::KCI_functiongroup:
      DiagSelect = (!isAnyFunctionDecl() && !isFunctionTemplateDecl())? 2 : 0;
      break;
    case CommandTraits::KCI_method:
      DiagSelect = !isObjCMethodDecl() ? 3 : 0;
      break;
    case CommandTraits::KCI_methodgroup:
      DiagSelect = !isObjCMethodDecl() ? 4 : 0;
      break;
    case CommandTraits::KCI_callback:
      DiagSelect = !isFunctionPointerVarDecl() ? 5 : 0;
      break;
    default:
      DiagSelect = 0;
      break;
  }
  if (DiagSelect)
    Diag(Comment->getLocation(), diag::warn_doc_function_method_decl_mismatch)
    << Comment->getCommandMarker()
    << (DiagSelect-1) << (DiagSelect-1)
    << Comment->getSourceRange();
}

void Sema::checkContainerDeclVerbatimLine(const BlockCommandComment *Comment) {
  const CommandInfo *Info = Traits.getCommandInfo(Comment->getCommandID());
  if (!Info->IsRecordLikeDeclarationCommand)
    return;
  unsigned DiagSelect;
  switch (Comment->getCommandID()) {
    case CommandTraits::KCI_class:
      DiagSelect = (!isClassOrStructDecl() && !isClassTemplateDecl()) ? 1 : 0;
      // Allow @class command on @interface declarations.
      // FIXME. Currently, \class and @class are indistinguishable. So,
      // \class is also allowed on an @interface declaration
      if (DiagSelect && Comment->getCommandMarker() && isObjCInterfaceDecl())
        DiagSelect = 0;
      break;
    case CommandTraits::KCI_interface:
      DiagSelect = !isObjCInterfaceDecl() ? 2 : 0;
      break;
    case CommandTraits::KCI_protocol:
      DiagSelect = !isObjCProtocolDecl() ? 3 : 0;
      break;
    case CommandTraits::KCI_struct:
      DiagSelect = !isClassOrStructDecl() ? 4 : 0;
      break;
    case CommandTraits::KCI_union:
      DiagSelect = !isUnionDecl() ? 5 : 0;
      break;
    default:
      DiagSelect = 0;
      break;
  }
  if (DiagSelect)
    Diag(Comment->getLocation(), diag::warn_doc_api_container_decl_mismatch)
    << Comment->getCommandMarker()
    << (DiagSelect-1) << (DiagSelect-1)
    << Comment->getSourceRange();
}

void Sema::checkContainerDecl(const BlockCommandComment *Comment) {
  const CommandInfo *Info = Traits.getCommandInfo(Comment->getCommandID());
  if (!Info->IsRecordLikeDetailCommand || isRecordLikeDecl())
    return;
  unsigned DiagSelect;
  switch (Comment->getCommandID()) {
    case CommandTraits::KCI_classdesign:
      DiagSelect = 1;
      break;
    case CommandTraits::KCI_coclass:
      DiagSelect = 2;
      break;
    case CommandTraits::KCI_dependency:
      DiagSelect = 3;
      break;
    case CommandTraits::KCI_helper:
      DiagSelect = 4;
      break;
    case CommandTraits::KCI_helperclass:
      DiagSelect = 5;
      break;
    case CommandTraits::KCI_helps:
      DiagSelect = 6;
      break;
    case CommandTraits::KCI_instancesize:
      DiagSelect = 7;
      break;
    case CommandTraits::KCI_ownership:
      DiagSelect = 8;
      break;
    case CommandTraits::KCI_performance:
      DiagSelect = 9;
      break;
    case CommandTraits::KCI_security:
      DiagSelect = 10;
      break;
    case CommandTraits::KCI_superclass:
      DiagSelect = 11;
      break;
    default:
      DiagSelect = 0;
      break;
  }
  if (DiagSelect)
    Diag(Comment->getLocation(), diag::warn_doc_container_decl_mismatch)
    << Comment->getCommandMarker()
    << (DiagSelect-1)
    << Comment->getSourceRange();
}

/// Turn a string into the corresponding PassDirection or -1 if it's not
/// valid.
static int getParamPassDirection(StringRef Arg) {
  return llvm::StringSwitch<int>(Arg)
      .Case("[in]", ParamCommandComment::In)
      .Case("[out]", ParamCommandComment::Out)
      .Cases("[in,out]", "[out,in]", ParamCommandComment::InOut)
      .Default(-1);
}

void Sema::actOnParamCommandDirectionArg(ParamCommandComment *Command,
                                         SourceLocation ArgLocBegin,
                                         SourceLocation ArgLocEnd,
                                         StringRef Arg) {
  std::string ArgLower = Arg.lower();
  int Direction = getParamPassDirection(ArgLower);

  if (Direction == -1) {
    // Try again with whitespace removed.
    ArgLower.erase(
        std::remove_if(ArgLower.begin(), ArgLower.end(), clang::isWhitespace),
        ArgLower.end());
    Direction = getParamPassDirection(ArgLower);

    SourceRange ArgRange(ArgLocBegin, ArgLocEnd);
    if (Direction != -1) {
      const char *FixedName = ParamCommandComment::getDirectionAsString(
          (ParamCommandComment::PassDirection)Direction);
      Diag(ArgLocBegin, diag::warn_doc_param_spaces_in_direction)
          << ArgRange << FixItHint::CreateReplacement(ArgRange, FixedName);
    } else {
      Diag(ArgLocBegin, diag::warn_doc_param_invalid_direction) << ArgRange;
      Direction = ParamCommandComment::In; // Sane fall back.
    }
  }
  Command->setDirection((ParamCommandComment::PassDirection)Direction,
                        /*Explicit=*/true);
}

void Sema::actOnParamCommandParamNameArg(ParamCommandComment *Command,
                                         SourceLocation ArgLocBegin,
                                         SourceLocation ArgLocEnd,
                                         StringRef Arg) {
  // Parser will not feed us more arguments than needed.
  assert(Command->getNumArgs() == 0);

  if (!Command->isDirectionExplicit()) {
    // User didn't provide a direction argument.
    Command->setDirection(ParamCommandComment::In, /* Explicit = */ false);
  }
  typedef BlockCommandComment::Argument Argument;
  Argument *A = new (Allocator) Argument(SourceRange(ArgLocBegin,
                                                     ArgLocEnd),
                                         Arg);
  Command->setArgs(llvm::makeArrayRef(A, 1));
}

void Sema::actOnParamCommandFinish(ParamCommandComment *Command,
                                   ParagraphComment *Paragraph) {
  Command->setParagraph(Paragraph);
  checkBlockCommandEmptyParagraph(Command);
}

TParamCommandComment *Sema::actOnTParamCommandStart(
                                      SourceLocation LocBegin,
                                      SourceLocation LocEnd,
                                      unsigned CommandID,
                                      CommandMarkerKind CommandMarker) {
  TParamCommandComment *Command =
      new (Allocator) TParamCommandComment(LocBegin, LocEnd, CommandID,
                                           CommandMarker);

  if (!isTemplateOrSpecialization())
    Diag(Command->getLocation(),
         diag::warn_doc_tparam_not_attached_to_a_template_decl)
      << CommandMarker
      << Command->getCommandNameRange(Traits);

  return Command;
}

void Sema::actOnTParamCommandParamNameArg(TParamCommandComment *Command,
                                          SourceLocation ArgLocBegin,
                                          SourceLocation ArgLocEnd,
                                          StringRef Arg) {
  // Parser will not feed us more arguments than needed.
  assert(Command->getNumArgs() == 0);

  typedef BlockCommandComment::Argument Argument;
  Argument *A = new (Allocator) Argument(SourceRange(ArgLocBegin,
                                                     ArgLocEnd),
                                         Arg);
  Command->setArgs(llvm::makeArrayRef(A, 1));

  if (!isTemplateOrSpecialization()) {
    // We already warned that this \\tparam is not attached to a template decl.
    return;
  }

  const TemplateParameterList *TemplateParameters =
      ThisDeclInfo->TemplateParameters;
  SmallVector<unsigned, 2> Position;
  if (resolveTParamReference(Arg, TemplateParameters, &Position)) {
    Command->setPosition(copyArray(llvm::makeArrayRef(Position)));
    TParamCommandComment *&PrevCommand = TemplateParameterDocs[Arg];
    if (PrevCommand) {
      SourceRange ArgRange(ArgLocBegin, ArgLocEnd);
      Diag(ArgLocBegin, diag::warn_doc_tparam_duplicate)
        << Arg << ArgRange;
      Diag(PrevCommand->getLocation(), diag::note_doc_tparam_previous)
        << PrevCommand->getParamNameRange();
    }
    PrevCommand = Command;
    return;
  }

  SourceRange ArgRange(ArgLocBegin, ArgLocEnd);
  Diag(ArgLocBegin, diag::warn_doc_tparam_not_found)
    << Arg << ArgRange;

  if (!TemplateParameters || TemplateParameters->size() == 0)
    return;

  StringRef CorrectedName;
  if (TemplateParameters->size() == 1) {
    const NamedDecl *Param = TemplateParameters->getParam(0);
    const IdentifierInfo *II = Param->getIdentifier();
    if (II)
      CorrectedName = II->getName();
  } else {
    CorrectedName = correctTypoInTParamReference(Arg, TemplateParameters);
  }

  if (!CorrectedName.empty()) {
    Diag(ArgLocBegin, diag::note_doc_tparam_name_suggestion)
      << CorrectedName
      << FixItHint::CreateReplacement(ArgRange, CorrectedName);
  }
}

void Sema::actOnTParamCommandFinish(TParamCommandComment *Command,
                                    ParagraphComment *Paragraph) {
  Command->setParagraph(Paragraph);
  checkBlockCommandEmptyParagraph(Command);
}

InlineCommandComment *Sema::actOnInlineCommand(SourceLocation CommandLocBegin,
                                               SourceLocation CommandLocEnd,
                                               unsigned CommandID) {
  ArrayRef<InlineCommandComment::Argument> Args;
  StringRef CommandName = Traits.getCommandInfo(CommandID)->Name;
  return new (Allocator) InlineCommandComment(
                                  CommandLocBegin,
                                  CommandLocEnd,
                                  CommandID,
                                  getInlineCommandRenderKind(CommandName),
                                  Args);
}

InlineCommandComment *Sema::actOnInlineCommand(SourceLocation CommandLocBegin,
                                               SourceLocation CommandLocEnd,
                                               unsigned CommandID,
                                               SourceLocation ArgLocBegin,
                                               SourceLocation ArgLocEnd,
                                               StringRef Arg) {
  typedef InlineCommandComment::Argument Argument;
  Argument *A = new (Allocator) Argument(SourceRange(ArgLocBegin,
                                                     ArgLocEnd),
                                         Arg);
  StringRef CommandName = Traits.getCommandInfo(CommandID)->Name;

  return new (Allocator) InlineCommandComment(
                                  CommandLocBegin,
                                  CommandLocEnd,
                                  CommandID,
                                  getInlineCommandRenderKind(CommandName),
                                  llvm::makeArrayRef(A, 1));
}

InlineContentComment *Sema::actOnUnknownCommand(SourceLocation LocBegin,
                                                SourceLocation LocEnd,
                                                StringRef CommandName) {
  unsigned CommandID = Traits.registerUnknownCommand(CommandName)->getID();
  return actOnUnknownCommand(LocBegin, LocEnd, CommandID);
}

InlineContentComment *Sema::actOnUnknownCommand(SourceLocation LocBegin,
                                                SourceLocation LocEnd,
                                                unsigned CommandID) {
  ArrayRef<InlineCommandComment::Argument> Args;
  return new (Allocator) InlineCommandComment(
                                  LocBegin, LocEnd, CommandID,
                                  InlineCommandComment::RenderNormal,
                                  Args);
}

TextComment *Sema::actOnText(SourceLocation LocBegin,
                             SourceLocation LocEnd,
                             StringRef Text) {
  return new (Allocator) TextComment(LocBegin, LocEnd, Text);
}

VerbatimBlockComment *Sema::actOnVerbatimBlockStart(SourceLocation Loc,
                                                    unsigned CommandID) {
  StringRef CommandName = Traits.getCommandInfo(CommandID)->Name;
  return new (Allocator) VerbatimBlockComment(
                                  Loc,
                                  Loc.getLocWithOffset(1 + CommandName.size()),
                                  CommandID);
}

VerbatimBlockLineComment *Sema::actOnVerbatimBlockLine(SourceLocation Loc,
                                                       StringRef Text) {
  return new (Allocator) VerbatimBlockLineComment(Loc, Text);
}

void Sema::actOnVerbatimBlockFinish(
                            VerbatimBlockComment *Block,
                            SourceLocation CloseNameLocBegin,
                            StringRef CloseName,
                            ArrayRef<VerbatimBlockLineComment *> Lines) {
  Block->setCloseName(CloseName, CloseNameLocBegin);
  Block->setLines(Lines);
}

VerbatimLineComment *Sema::actOnVerbatimLine(SourceLocation LocBegin,
                                             unsigned CommandID,
                                             SourceLocation TextBegin,
                                             StringRef Text) {
  VerbatimLineComment *VL = new (Allocator) VerbatimLineComment(
                              LocBegin,
                              TextBegin.getLocWithOffset(Text.size()),
                              CommandID,
                              TextBegin,
                              Text);
  checkFunctionDeclVerbatimLine(VL);
  checkContainerDeclVerbatimLine(VL);
  return VL;
}

HTMLStartTagComment *Sema::actOnHTMLStartTagStart(SourceLocation LocBegin,
                                                  StringRef TagName) {
  return new (Allocator) HTMLStartTagComment(LocBegin, TagName);
}

void Sema::actOnHTMLStartTagFinish(
                              HTMLStartTagComment *Tag,
                              ArrayRef<HTMLStartTagComment::Attribute> Attrs,
                              SourceLocation GreaterLoc,
                              bool IsSelfClosing) {
  Tag->setAttrs(Attrs);
  Tag->setGreaterLoc(GreaterLoc);
  if (IsSelfClosing)
    Tag->setSelfClosing();
  else if (!isHTMLEndTagForbidden(Tag->getTagName()))
    HTMLOpenTags.push_back(Tag);
}

HTMLEndTagComment *Sema::actOnHTMLEndTag(SourceLocation LocBegin,
                                         SourceLocation LocEnd,
                                         StringRef TagName) {
  HTMLEndTagComment *HET =
      new (Allocator) HTMLEndTagComment(LocBegin, LocEnd, TagName);
  if (isHTMLEndTagForbidden(TagName)) {
    Diag(HET->getLocation(), diag::warn_doc_html_end_forbidden)
      << TagName << HET->getSourceRange();
    HET->setIsMalformed();
    return HET;
  }

  bool FoundOpen = false;
  for (SmallVectorImpl<HTMLStartTagComment *>::const_reverse_iterator
       I = HTMLOpenTags.rbegin(), E = HTMLOpenTags.rend();
       I != E; ++I) {
    if ((*I)->getTagName() == TagName) {
      FoundOpen = true;
      break;
    }
  }
  if (!FoundOpen) {
    Diag(HET->getLocation(), diag::warn_doc_html_end_unbalanced)
      << HET->getSourceRange();
    HET->setIsMalformed();
    return HET;
  }

  while (!HTMLOpenTags.empty()) {
    HTMLStartTagComment *HST = HTMLOpenTags.pop_back_val();
    StringRef LastNotClosedTagName = HST->getTagName();
    if (LastNotClosedTagName == TagName) {
      // If the start tag is malformed, end tag is malformed as well.
      if (HST->isMalformed())
        HET->setIsMalformed();
      break;
    }

    if (isHTMLEndTagOptional(LastNotClosedTagName))
      continue;

    bool OpenLineInvalid;
    const unsigned OpenLine = SourceMgr.getPresumedLineNumber(
                                                HST->getLocation(),
                                                &OpenLineInvalid);
    bool CloseLineInvalid;
    const unsigned CloseLine = SourceMgr.getPresumedLineNumber(
                                                HET->getLocation(),
                                                &CloseLineInvalid);

    if (OpenLineInvalid || CloseLineInvalid || OpenLine == CloseLine) {
      Diag(HST->getLocation(), diag::warn_doc_html_start_end_mismatch)
        << HST->getTagName() << HET->getTagName()
        << HST->getSourceRange() << HET->getSourceRange();
      HST->setIsMalformed();
    } else {
      Diag(HST->getLocation(), diag::warn_doc_html_start_end_mismatch)
        << HST->getTagName() << HET->getTagName()
        << HST->getSourceRange();
      Diag(HET->getLocation(), diag::note_doc_html_end_tag)
        << HET->getSourceRange();
      HST->setIsMalformed();
    }
  }

  return HET;
}

FullComment *Sema::actOnFullComment(
                              ArrayRef<BlockContentComment *> Blocks) {
  FullComment *FC = new (Allocator) FullComment(Blocks, ThisDeclInfo);
  resolveParamCommandIndexes(FC);

  // Complain about HTML tags that are not closed.
  while (!HTMLOpenTags.empty()) {
    HTMLStartTagComment *HST = HTMLOpenTags.pop_back_val();
    if (isHTMLEndTagOptional(HST->getTagName()))
      continue;

    Diag(HST->getLocation(), diag::warn_doc_html_missing_end_tag)
      << HST->getTagName() << HST->getSourceRange();
    HST->setIsMalformed();
  }

  return FC;
}

void Sema::checkBlockCommandEmptyParagraph(BlockCommandComment *Command) {
  if (Traits.getCommandInfo(Command->getCommandID())->IsEmptyParagraphAllowed)
    return;

  ParagraphComment *Paragraph = Command->getParagraph();
  if (Paragraph->isWhitespace()) {
    SourceLocation DiagLoc;
    if (Command->getNumArgs() > 0)
      DiagLoc = Command->getArgRange(Command->getNumArgs() - 1).getEnd();
    if (!DiagLoc.isValid())
      DiagLoc = Command->getCommandNameRange(Traits).getEnd();
    Diag(DiagLoc, diag::warn_doc_block_command_empty_paragraph)
      << Command->getCommandMarker()
      << Command->getCommandName(Traits)
      << Command->getSourceRange();
  }
}

void Sema::checkReturnsCommand(const BlockCommandComment *Command) {
  if (!Traits.getCommandInfo(Command->getCommandID())->IsReturnsCommand)
    return;

  assert(ThisDeclInfo && "should not call this check on a bare comment");

  // We allow the return command for all @properties because it can be used
  // to document the value that the property getter returns.
  if (isObjCPropertyDecl())
    return;
  if (isFunctionDecl() || isFunctionOrBlockPointerVarLikeDecl()) {
    if (ThisDeclInfo->ReturnType->isVoidType()) {
      unsigned DiagKind;
      switch (ThisDeclInfo->CommentDecl->getKind()) {
      default:
        if (ThisDeclInfo->IsObjCMethod)
          DiagKind = 3;
        else
          DiagKind = 0;
        break;
      case Decl::CXXConstructor:
        DiagKind = 1;
        break;
      case Decl::CXXDestructor:
        DiagKind = 2;
        break;
      }
      Diag(Command->getLocation(),
           diag::warn_doc_returns_attached_to_a_void_function)
        << Command->getCommandMarker()
        << Command->getCommandName(Traits)
        << DiagKind
        << Command->getSourceRange();
    }
    return;
  }

  Diag(Command->getLocation(),
       diag::warn_doc_returns_not_attached_to_a_function_decl)
    << Command->getCommandMarker()
    << Command->getCommandName(Traits)
    << Command->getSourceRange();
}

void Sema::checkBlockCommandDuplicate(const BlockCommandComment *Command) {
  const CommandInfo *Info = Traits.getCommandInfo(Command->getCommandID());
  const BlockCommandComment *PrevCommand = nullptr;
  if (Info->IsBriefCommand) {
    if (!BriefCommand) {
      BriefCommand = Command;
      return;
    }
    PrevCommand = BriefCommand;
  } else if (Info->IsHeaderfileCommand) {
    if (!HeaderfileCommand) {
      HeaderfileCommand = Command;
      return;
    }
    PrevCommand = HeaderfileCommand;
  } else {
    // We don't want to check this command for duplicates.
    return;
  }
  StringRef CommandName = Command->getCommandName(Traits);
  StringRef PrevCommandName = PrevCommand->getCommandName(Traits);
  Diag(Command->getLocation(), diag::warn_doc_block_command_duplicate)
      << Command->getCommandMarker()
      << CommandName
      << Command->getSourceRange();
  if (CommandName == PrevCommandName)
    Diag(PrevCommand->getLocation(), diag::note_doc_block_command_previous)
        << PrevCommand->getCommandMarker()
        << PrevCommandName
        << PrevCommand->getSourceRange();
  else
    Diag(PrevCommand->getLocation(),
         diag::note_doc_block_command_previous_alias)
        << PrevCommand->getCommandMarker()
        << PrevCommandName
        << CommandName;
}

void Sema::checkDeprecatedCommand(const BlockCommandComment *Command) {
  if (!Traits.getCommandInfo(Command->getCommandID())->IsDeprecatedCommand)
    return;

  assert(ThisDeclInfo && "should not call this check on a bare comment");

  const Decl *D = ThisDeclInfo->CommentDecl;
  if (!D)
    return;

  if (D->hasAttr<DeprecatedAttr>() ||
      D->hasAttr<AvailabilityAttr>() ||
      D->hasAttr<UnavailableAttr>())
    return;

  Diag(Command->getLocation(),
       diag::warn_doc_deprecated_not_sync)
    << Command->getSourceRange();

  // Try to emit a fixit with a deprecation attribute.
  if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
    // Don't emit a Fix-It for non-member function definitions.  GCC does not
    // accept attributes on them.
    const DeclContext *Ctx = FD->getDeclContext();
    if ((!Ctx || !Ctx->isRecord()) &&
        FD->doesThisDeclarationHaveABody())
      return;

    StringRef AttributeSpelling = "__attribute__((deprecated))";
    if (PP) {
      TokenValue Tokens[] = {
        tok::kw___attribute, tok::l_paren, tok::l_paren,
        PP->getIdentifierInfo("deprecated"),
        tok::r_paren, tok::r_paren
      };
      StringRef MacroName = PP->getLastMacroWithSpelling(FD->getLocation(),
                                                         Tokens);
      if (!MacroName.empty())
        AttributeSpelling = MacroName;
    }

    SmallString<64> TextToInsert(" ");
    TextToInsert += AttributeSpelling;
    Diag(FD->getEndLoc(), diag::note_add_deprecation_attr)
        << FixItHint::CreateInsertion(FD->getEndLoc().getLocWithOffset(1),
                                      TextToInsert);
  }
}

void Sema::resolveParamCommandIndexes(const FullComment *FC) {
  if (!isFunctionDecl()) {
    // We already warned that \\param commands are not attached to a function
    // decl.
    return;
  }

  SmallVector<ParamCommandComment *, 8> UnresolvedParamCommands;

  // Comment AST nodes that correspond to \c ParamVars for which we have
  // found a \\param command or NULL if no documentation was found so far.
  SmallVector<ParamCommandComment *, 8> ParamVarDocs;

  ArrayRef<const ParmVarDecl *> ParamVars = getParamVars();
  ParamVarDocs.resize(ParamVars.size(), nullptr);

  // First pass over all \\param commands: resolve all parameter names.
  for (Comment::child_iterator I = FC->child_begin(), E = FC->child_end();
       I != E; ++I) {
    ParamCommandComment *PCC = dyn_cast<ParamCommandComment>(*I);
    if (!PCC || !PCC->hasParamName())
      continue;
    StringRef ParamName = PCC->getParamNameAsWritten();

    // Check that referenced parameter name is in the function decl.
    const unsigned ResolvedParamIndex = resolveParmVarReference(ParamName,
                                                                ParamVars);
    if (ResolvedParamIndex == ParamCommandComment::VarArgParamIndex) {
      PCC->setIsVarArgParam();
      continue;
    }
    if (ResolvedParamIndex == ParamCommandComment::InvalidParamIndex) {
      UnresolvedParamCommands.push_back(PCC);
      continue;
    }
    PCC->setParamIndex(ResolvedParamIndex);
    if (ParamVarDocs[ResolvedParamIndex]) {
      SourceRange ArgRange = PCC->getParamNameRange();
      Diag(ArgRange.getBegin(), diag::warn_doc_param_duplicate)
        << ParamName << ArgRange;
      ParamCommandComment *PrevCommand = ParamVarDocs[ResolvedParamIndex];
      Diag(PrevCommand->getLocation(), diag::note_doc_param_previous)
        << PrevCommand->getParamNameRange();
    }
    ParamVarDocs[ResolvedParamIndex] = PCC;
  }

  // Find parameter declarations that have no corresponding \\param.
  SmallVector<const ParmVarDecl *, 8> OrphanedParamDecls;
  for (unsigned i = 0, e = ParamVarDocs.size(); i != e; ++i) {
    if (!ParamVarDocs[i])
      OrphanedParamDecls.push_back(ParamVars[i]);
  }

  // Second pass over unresolved \\param commands: do typo correction.
  // Suggest corrections from a set of parameter declarations that have no
  // corresponding \\param.
  for (unsigned i = 0, e = UnresolvedParamCommands.size(); i != e; ++i) {
    const ParamCommandComment *PCC = UnresolvedParamCommands[i];

    SourceRange ArgRange = PCC->getParamNameRange();
    StringRef ParamName = PCC->getParamNameAsWritten();
    Diag(ArgRange.getBegin(), diag::warn_doc_param_not_found)
      << ParamName << ArgRange;

    // All parameters documented -- can't suggest a correction.
    if (OrphanedParamDecls.size() == 0)
      continue;

    unsigned CorrectedParamIndex = ParamCommandComment::InvalidParamIndex;
    if (OrphanedParamDecls.size() == 1) {
      // If one parameter is not documented then that parameter is the only
      // possible suggestion.
      CorrectedParamIndex = 0;
    } else {
      // Do typo correction.
      CorrectedParamIndex = correctTypoInParmVarReference(ParamName,
                                                          OrphanedParamDecls);
    }
    if (CorrectedParamIndex != ParamCommandComment::InvalidParamIndex) {
      const ParmVarDecl *CorrectedPVD = OrphanedParamDecls[CorrectedParamIndex];
      if (const IdentifierInfo *CorrectedII = CorrectedPVD->getIdentifier())
        Diag(ArgRange.getBegin(), diag::note_doc_param_name_suggestion)
          << CorrectedII->getName()
          << FixItHint::CreateReplacement(ArgRange, CorrectedII->getName());
    }
  }
}

bool Sema::isFunctionDecl() {
  if (!ThisDeclInfo)
    return false;
  if (!ThisDeclInfo->IsFilled)
    inspectThisDecl();
  return ThisDeclInfo->getKind() == DeclInfo::FunctionKind;
}

bool Sema::isAnyFunctionDecl() {
  return isFunctionDecl() && ThisDeclInfo->CurrentDecl &&
         isa<FunctionDecl>(ThisDeclInfo->CurrentDecl);
}

bool Sema::isFunctionOrMethodVariadic() {
  if (!isFunctionDecl() || !ThisDeclInfo->CurrentDecl)
    return false;
  if (const FunctionDecl *FD =
        dyn_cast<FunctionDecl>(ThisDeclInfo->CurrentDecl))
    return FD->isVariadic();
  if (const FunctionTemplateDecl *FTD =
        dyn_cast<FunctionTemplateDecl>(ThisDeclInfo->CurrentDecl))
    return FTD->getTemplatedDecl()->isVariadic();
  if (const ObjCMethodDecl *MD =
        dyn_cast<ObjCMethodDecl>(ThisDeclInfo->CurrentDecl))
    return MD->isVariadic();
  if (const TypedefNameDecl *TD =
          dyn_cast<TypedefNameDecl>(ThisDeclInfo->CurrentDecl)) {
    QualType Type = TD->getUnderlyingType();
    if (Type->isFunctionPointerType() || Type->isBlockPointerType())
      Type = Type->getPointeeType();
    if (const auto *FT = Type->getAs<FunctionProtoType>())
      return FT->isVariadic();
  }
  return false;
}

bool Sema::isObjCMethodDecl() {
  return isFunctionDecl() && ThisDeclInfo->CurrentDecl &&
         isa<ObjCMethodDecl>(ThisDeclInfo->CurrentDecl);
}

bool Sema::isFunctionPointerVarDecl() {
  if (!ThisDeclInfo)
    return false;
  if (!ThisDeclInfo->IsFilled)
    inspectThisDecl();
  if (ThisDeclInfo->getKind() == DeclInfo::VariableKind) {
    if (const VarDecl *VD = dyn_cast_or_null<VarDecl>(ThisDeclInfo->CurrentDecl)) {
      QualType QT = VD->getType();
      return QT->isFunctionPointerType();
    }
  }
  return false;
}

bool Sema::isFunctionOrBlockPointerVarLikeDecl() {
  if (!ThisDeclInfo)
    return false;
  if (!ThisDeclInfo->IsFilled)
    inspectThisDecl();
  if (ThisDeclInfo->getKind() != DeclInfo::VariableKind ||
      !ThisDeclInfo->CurrentDecl)
    return false;
  QualType QT;
  if (const auto *VD = dyn_cast<DeclaratorDecl>(ThisDeclInfo->CurrentDecl))
    QT = VD->getType();
  else if (const auto *PD =
               dyn_cast<ObjCPropertyDecl>(ThisDeclInfo->CurrentDecl))
    QT = PD->getType();
  else
    return false;
  // We would like to warn about the 'returns'/'param' commands for
  // variables that don't directly specify the function type, so type aliases
  // can be ignored.
  if (QT->getAs<TypedefType>())
    return false;
  return QT->isFunctionPointerType() || QT->isBlockPointerType();
}

bool Sema::isObjCPropertyDecl() {
  if (!ThisDeclInfo)
    return false;
  if (!ThisDeclInfo->IsFilled)
    inspectThisDecl();
  return ThisDeclInfo->CurrentDecl->getKind() == Decl::ObjCProperty;
}

bool Sema::isTemplateOrSpecialization() {
  if (!ThisDeclInfo)
    return false;
  if (!ThisDeclInfo->IsFilled)
    inspectThisDecl();
  return ThisDeclInfo->getTemplateKind() != DeclInfo::NotTemplate;
}

bool Sema::isRecordLikeDecl() {
  if (!ThisDeclInfo)
    return false;
  if (!ThisDeclInfo->IsFilled)
    inspectThisDecl();
  return isUnionDecl() || isClassOrStructDecl() || isObjCInterfaceDecl() ||
         isObjCProtocolDecl();
}

bool Sema::isUnionDecl() {
  if (!ThisDeclInfo)
    return false;
  if (!ThisDeclInfo->IsFilled)
    inspectThisDecl();
  if (const RecordDecl *RD =
        dyn_cast_or_null<RecordDecl>(ThisDeclInfo->CurrentDecl))
    return RD->isUnion();
  return false;
}

bool Sema::isClassOrStructDecl() {
  if (!ThisDeclInfo)
    return false;
  if (!ThisDeclInfo->IsFilled)
    inspectThisDecl();
  return ThisDeclInfo->CurrentDecl &&
         isa<RecordDecl>(ThisDeclInfo->CurrentDecl) &&
         !isUnionDecl();
}

bool Sema::isClassTemplateDecl() {
  if (!ThisDeclInfo)
    return false;
  if (!ThisDeclInfo->IsFilled)
    inspectThisDecl();
  return ThisDeclInfo->CurrentDecl &&
          (isa<ClassTemplateDecl>(ThisDeclInfo->CurrentDecl));
}

bool Sema::isFunctionTemplateDecl() {
  if (!ThisDeclInfo)
    return false;
  if (!ThisDeclInfo->IsFilled)
    inspectThisDecl();
  return ThisDeclInfo->CurrentDecl &&
         (isa<FunctionTemplateDecl>(ThisDeclInfo->CurrentDecl));
}

bool Sema::isObjCInterfaceDecl() {
  if (!ThisDeclInfo)
    return false;
  if (!ThisDeclInfo->IsFilled)
    inspectThisDecl();
  return ThisDeclInfo->CurrentDecl &&
         isa<ObjCInterfaceDecl>(ThisDeclInfo->CurrentDecl);
}

bool Sema::isObjCProtocolDecl() {
  if (!ThisDeclInfo)
    return false;
  if (!ThisDeclInfo->IsFilled)
    inspectThisDecl();
  return ThisDeclInfo->CurrentDecl &&
         isa<ObjCProtocolDecl>(ThisDeclInfo->CurrentDecl);
}

ArrayRef<const ParmVarDecl *> Sema::getParamVars() {
  if (!ThisDeclInfo->IsFilled)
    inspectThisDecl();
  return ThisDeclInfo->ParamVars;
}

void Sema::inspectThisDecl() {
  ThisDeclInfo->fill();
}

unsigned Sema::resolveParmVarReference(StringRef Name,
                                       ArrayRef<const ParmVarDecl *> ParamVars) {
  for (unsigned i = 0, e = ParamVars.size(); i != e; ++i) {
    const IdentifierInfo *II = ParamVars[i]->getIdentifier();
    if (II && II->getName() == Name)
      return i;
  }
  if (Name == "..." && isFunctionOrMethodVariadic())
    return ParamCommandComment::VarArgParamIndex;
  return ParamCommandComment::InvalidParamIndex;
}

namespace {
class SimpleTypoCorrector {
  const NamedDecl *BestDecl;

  StringRef Typo;
  const unsigned MaxEditDistance;

  unsigned BestEditDistance;
  unsigned BestIndex;
  unsigned NextIndex;

public:
  explicit SimpleTypoCorrector(StringRef Typo)
      : BestDecl(nullptr), Typo(Typo), MaxEditDistance((Typo.size() + 2) / 3),
        BestEditDistance(MaxEditDistance + 1), BestIndex(0), NextIndex(0) {}

  void addDecl(const NamedDecl *ND);

  const NamedDecl *getBestDecl() const {
    if (BestEditDistance > MaxEditDistance)
      return nullptr;

    return BestDecl;
  }

  unsigned getBestDeclIndex() const {
    assert(getBestDecl());
    return BestIndex;
  }
};

void SimpleTypoCorrector::addDecl(const NamedDecl *ND) {
  unsigned CurrIndex = NextIndex++;

  const IdentifierInfo *II = ND->getIdentifier();
  if (!II)
    return;

  StringRef Name = II->getName();
  unsigned MinPossibleEditDistance = abs((int)Name.size() - (int)Typo.size());
  if (MinPossibleEditDistance > 0 &&
      Typo.size() / MinPossibleEditDistance < 3)
    return;

  unsigned EditDistance = Typo.edit_distance(Name, true, MaxEditDistance);
  if (EditDistance < BestEditDistance) {
    BestEditDistance = EditDistance;
    BestDecl = ND;
    BestIndex = CurrIndex;
  }
}
} // end anonymous namespace

unsigned Sema::correctTypoInParmVarReference(
                                    StringRef Typo,
                                    ArrayRef<const ParmVarDecl *> ParamVars) {
  SimpleTypoCorrector Corrector(Typo);
  for (unsigned i = 0, e = ParamVars.size(); i != e; ++i)
    Corrector.addDecl(ParamVars[i]);
  if (Corrector.getBestDecl())
    return Corrector.getBestDeclIndex();
  else
    return ParamCommandComment::InvalidParamIndex;
}

namespace {
bool ResolveTParamReferenceHelper(
                            StringRef Name,
                            const TemplateParameterList *TemplateParameters,
                            SmallVectorImpl<unsigned> *Position) {
  for (unsigned i = 0, e = TemplateParameters->size(); i != e; ++i) {
    const NamedDecl *Param = TemplateParameters->getParam(i);
    const IdentifierInfo *II = Param->getIdentifier();
    if (II && II->getName() == Name) {
      Position->push_back(i);
      return true;
    }

    if (const TemplateTemplateParmDecl *TTP =
            dyn_cast<TemplateTemplateParmDecl>(Param)) {
      Position->push_back(i);
      if (ResolveTParamReferenceHelper(Name, TTP->getTemplateParameters(),
                                       Position))
        return true;
      Position->pop_back();
    }
  }
  return false;
}
} // end anonymous namespace

bool Sema::resolveTParamReference(
                            StringRef Name,
                            const TemplateParameterList *TemplateParameters,
                            SmallVectorImpl<unsigned> *Position) {
  Position->clear();
  if (!TemplateParameters)
    return false;

  return ResolveTParamReferenceHelper(Name, TemplateParameters, Position);
}

namespace {
void CorrectTypoInTParamReferenceHelper(
                            const TemplateParameterList *TemplateParameters,
                            SimpleTypoCorrector &Corrector) {
  for (unsigned i = 0, e = TemplateParameters->size(); i != e; ++i) {
    const NamedDecl *Param = TemplateParameters->getParam(i);
    Corrector.addDecl(Param);

    if (const TemplateTemplateParmDecl *TTP =
            dyn_cast<TemplateTemplateParmDecl>(Param))
      CorrectTypoInTParamReferenceHelper(TTP->getTemplateParameters(),
                                         Corrector);
  }
}
} // end anonymous namespace

StringRef Sema::correctTypoInTParamReference(
                            StringRef Typo,
                            const TemplateParameterList *TemplateParameters) {
  SimpleTypoCorrector Corrector(Typo);
  CorrectTypoInTParamReferenceHelper(TemplateParameters, Corrector);
  if (const NamedDecl *ND = Corrector.getBestDecl()) {
    const IdentifierInfo *II = ND->getIdentifier();
    assert(II && "SimpleTypoCorrector should not return this decl");
    return II->getName();
  }
  return StringRef();
}

InlineCommandComment::RenderKind
Sema::getInlineCommandRenderKind(StringRef Name) const {
  assert(Traits.getCommandInfo(Name)->IsInlineCommand);

  return llvm::StringSwitch<InlineCommandComment::RenderKind>(Name)
      .Case("b", InlineCommandComment::RenderBold)
      .Cases("c", "p", InlineCommandComment::RenderMonospaced)
      .Cases("a", "e", "em", InlineCommandComment::RenderEmphasized)
      .Default(InlineCommandComment::RenderNormal);
}

} // end namespace comments
} // end namespace clang
