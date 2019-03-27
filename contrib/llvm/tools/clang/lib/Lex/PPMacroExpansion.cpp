//===--- MacroExpansion.cpp - Top level Macro Expansion -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the top level handling of macro expansion for the
// preprocessor.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/Attributes.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/ObjCRuntime.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Lex/CodeCompletionHandler.h"
#include "clang/Lex/DirectoryLookup.h"
#include "clang/Lex/ExternalPreprocessorSource.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/LexDiagnostic.h"
#include "clang/Lex/MacroArgs.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/PreprocessorLexer.h"
#include "clang/Lex/Token.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <ctime>
#include <string>
#include <tuple>
#include <utility>

using namespace clang;

MacroDirective *
Preprocessor::getLocalMacroDirectiveHistory(const IdentifierInfo *II) const {
  if (!II->hadMacroDefinition())
    return nullptr;
  auto Pos = CurSubmoduleState->Macros.find(II);
  return Pos == CurSubmoduleState->Macros.end() ? nullptr
                                                : Pos->second.getLatest();
}

void Preprocessor::appendMacroDirective(IdentifierInfo *II, MacroDirective *MD){
  assert(MD && "MacroDirective should be non-zero!");
  assert(!MD->getPrevious() && "Already attached to a MacroDirective history.");

  MacroState &StoredMD = CurSubmoduleState->Macros[II];
  auto *OldMD = StoredMD.getLatest();
  MD->setPrevious(OldMD);
  StoredMD.setLatest(MD);
  StoredMD.overrideActiveModuleMacros(*this, II);

  if (needModuleMacros()) {
    // Track that we created a new macro directive, so we know we should
    // consider building a ModuleMacro for it when we get to the end of
    // the module.
    PendingModuleMacroNames.push_back(II);
  }

  // Set up the identifier as having associated macro history.
  II->setHasMacroDefinition(true);
  if (!MD->isDefined() && LeafModuleMacros.find(II) == LeafModuleMacros.end())
    II->setHasMacroDefinition(false);
  if (II->isFromAST())
    II->setChangedSinceDeserialization();
}

void Preprocessor::setLoadedMacroDirective(IdentifierInfo *II,
                                           MacroDirective *ED,
                                           MacroDirective *MD) {
  // Normally, when a macro is defined, it goes through appendMacroDirective()
  // above, which chains a macro to previous defines, undefs, etc.
  // However, in a pch, the whole macro history up to the end of the pch is
  // stored, so ASTReader goes through this function instead.
  // However, built-in macros are already registered in the Preprocessor
  // ctor, and ASTWriter stops writing the macro chain at built-in macros,
  // so in that case the chain from the pch needs to be spliced to the existing
  // built-in.

  assert(II && MD);
  MacroState &StoredMD = CurSubmoduleState->Macros[II];

  if (auto *OldMD = StoredMD.getLatest()) {
    // shouldIgnoreMacro() in ASTWriter also stops at macros from the
    // predefines buffer in module builds. However, in module builds, modules
    // are loaded completely before predefines are processed, so StoredMD
    // will be nullptr for them when they're loaded. StoredMD should only be
    // non-nullptr for builtins read from a pch file.
    assert(OldMD->getMacroInfo()->isBuiltinMacro() &&
           "only built-ins should have an entry here");
    assert(!OldMD->getPrevious() && "builtin should only have a single entry");
    ED->setPrevious(OldMD);
    StoredMD.setLatest(MD);
  } else {
    StoredMD = MD;
  }

  // Setup the identifier as having associated macro history.
  II->setHasMacroDefinition(true);
  if (!MD->isDefined() && LeafModuleMacros.find(II) == LeafModuleMacros.end())
    II->setHasMacroDefinition(false);
}

ModuleMacro *Preprocessor::addModuleMacro(Module *Mod, IdentifierInfo *II,
                                          MacroInfo *Macro,
                                          ArrayRef<ModuleMacro *> Overrides,
                                          bool &New) {
  llvm::FoldingSetNodeID ID;
  ModuleMacro::Profile(ID, Mod, II);

  void *InsertPos;
  if (auto *MM = ModuleMacros.FindNodeOrInsertPos(ID, InsertPos)) {
    New = false;
    return MM;
  }

  auto *MM = ModuleMacro::create(*this, Mod, II, Macro, Overrides);
  ModuleMacros.InsertNode(MM, InsertPos);

  // Each overridden macro is now overridden by one more macro.
  bool HidAny = false;
  for (auto *O : Overrides) {
    HidAny |= (O->NumOverriddenBy == 0);
    ++O->NumOverriddenBy;
  }

  // If we were the first overrider for any macro, it's no longer a leaf.
  auto &LeafMacros = LeafModuleMacros[II];
  if (HidAny) {
    LeafMacros.erase(std::remove_if(LeafMacros.begin(), LeafMacros.end(),
                                    [](ModuleMacro *MM) {
                                      return MM->NumOverriddenBy != 0;
                                    }),
                     LeafMacros.end());
  }

  // The new macro is always a leaf macro.
  LeafMacros.push_back(MM);
  // The identifier now has defined macros (that may or may not be visible).
  II->setHasMacroDefinition(true);

  New = true;
  return MM;
}

ModuleMacro *Preprocessor::getModuleMacro(Module *Mod, IdentifierInfo *II) {
  llvm::FoldingSetNodeID ID;
  ModuleMacro::Profile(ID, Mod, II);

  void *InsertPos;
  return ModuleMacros.FindNodeOrInsertPos(ID, InsertPos);
}

void Preprocessor::updateModuleMacroInfo(const IdentifierInfo *II,
                                         ModuleMacroInfo &Info) {
  assert(Info.ActiveModuleMacrosGeneration !=
             CurSubmoduleState->VisibleModules.getGeneration() &&
         "don't need to update this macro name info");
  Info.ActiveModuleMacrosGeneration =
      CurSubmoduleState->VisibleModules.getGeneration();

  auto Leaf = LeafModuleMacros.find(II);
  if (Leaf == LeafModuleMacros.end()) {
    // No imported macros at all: nothing to do.
    return;
  }

  Info.ActiveModuleMacros.clear();

  // Every macro that's locally overridden is overridden by a visible macro.
  llvm::DenseMap<ModuleMacro *, int> NumHiddenOverrides;
  for (auto *O : Info.OverriddenMacros)
    NumHiddenOverrides[O] = -1;

  // Collect all macros that are not overridden by a visible macro.
  llvm::SmallVector<ModuleMacro *, 16> Worklist;
  for (auto *LeafMM : Leaf->second) {
    assert(LeafMM->getNumOverridingMacros() == 0 && "leaf macro overridden");
    if (NumHiddenOverrides.lookup(LeafMM) == 0)
      Worklist.push_back(LeafMM);
  }
  while (!Worklist.empty()) {
    auto *MM = Worklist.pop_back_val();
    if (CurSubmoduleState->VisibleModules.isVisible(MM->getOwningModule())) {
      // We only care about collecting definitions; undefinitions only act
      // to override other definitions.
      if (MM->getMacroInfo())
        Info.ActiveModuleMacros.push_back(MM);
    } else {
      for (auto *O : MM->overrides())
        if ((unsigned)++NumHiddenOverrides[O] == O->getNumOverridingMacros())
          Worklist.push_back(O);
    }
  }
  // Our reverse postorder walk found the macros in reverse order.
  std::reverse(Info.ActiveModuleMacros.begin(), Info.ActiveModuleMacros.end());

  // Determine whether the macro name is ambiguous.
  MacroInfo *MI = nullptr;
  bool IsSystemMacro = true;
  bool IsAmbiguous = false;
  if (auto *MD = Info.MD) {
    while (MD && isa<VisibilityMacroDirective>(MD))
      MD = MD->getPrevious();
    if (auto *DMD = dyn_cast_or_null<DefMacroDirective>(MD)) {
      MI = DMD->getInfo();
      IsSystemMacro &= SourceMgr.isInSystemHeader(DMD->getLocation());
    }
  }
  for (auto *Active : Info.ActiveModuleMacros) {
    auto *NewMI = Active->getMacroInfo();

    // Before marking the macro as ambiguous, check if this is a case where
    // both macros are in system headers. If so, we trust that the system
    // did not get it wrong. This also handles cases where Clang's own
    // headers have a different spelling of certain system macros:
    //   #define LONG_MAX __LONG_MAX__ (clang's limits.h)
    //   #define LONG_MAX 0x7fffffffffffffffL (system's limits.h)
    //
    // FIXME: Remove the defined-in-system-headers check. clang's limits.h
    // overrides the system limits.h's macros, so there's no conflict here.
    if (MI && NewMI != MI &&
        !MI->isIdenticalTo(*NewMI, *this, /*Syntactically=*/true))
      IsAmbiguous = true;
    IsSystemMacro &= Active->getOwningModule()->IsSystem ||
                     SourceMgr.isInSystemHeader(NewMI->getDefinitionLoc());
    MI = NewMI;
  }
  Info.IsAmbiguous = IsAmbiguous && !IsSystemMacro;
}

void Preprocessor::dumpMacroInfo(const IdentifierInfo *II) {
  ArrayRef<ModuleMacro*> Leaf;
  auto LeafIt = LeafModuleMacros.find(II);
  if (LeafIt != LeafModuleMacros.end())
    Leaf = LeafIt->second;
  const MacroState *State = nullptr;
  auto Pos = CurSubmoduleState->Macros.find(II);
  if (Pos != CurSubmoduleState->Macros.end())
    State = &Pos->second;

  llvm::errs() << "MacroState " << State << " " << II->getNameStart();
  if (State && State->isAmbiguous(*this, II))
    llvm::errs() << " ambiguous";
  if (State && !State->getOverriddenMacros().empty()) {
    llvm::errs() << " overrides";
    for (auto *O : State->getOverriddenMacros())
      llvm::errs() << " " << O->getOwningModule()->getFullModuleName();
  }
  llvm::errs() << "\n";

  // Dump local macro directives.
  for (auto *MD = State ? State->getLatest() : nullptr; MD;
       MD = MD->getPrevious()) {
    llvm::errs() << " ";
    MD->dump();
  }

  // Dump module macros.
  llvm::DenseSet<ModuleMacro*> Active;
  for (auto *MM : State ? State->getActiveModuleMacros(*this, II) : None)
    Active.insert(MM);
  llvm::DenseSet<ModuleMacro*> Visited;
  llvm::SmallVector<ModuleMacro *, 16> Worklist(Leaf.begin(), Leaf.end());
  while (!Worklist.empty()) {
    auto *MM = Worklist.pop_back_val();
    llvm::errs() << " ModuleMacro " << MM << " "
                 << MM->getOwningModule()->getFullModuleName();
    if (!MM->getMacroInfo())
      llvm::errs() << " undef";

    if (Active.count(MM))
      llvm::errs() << " active";
    else if (!CurSubmoduleState->VisibleModules.isVisible(
                 MM->getOwningModule()))
      llvm::errs() << " hidden";
    else if (MM->getMacroInfo())
      llvm::errs() << " overridden";

    if (!MM->overrides().empty()) {
      llvm::errs() << " overrides";
      for (auto *O : MM->overrides()) {
        llvm::errs() << " " << O->getOwningModule()->getFullModuleName();
        if (Visited.insert(O).second)
          Worklist.push_back(O);
      }
    }
    llvm::errs() << "\n";
    if (auto *MI = MM->getMacroInfo()) {
      llvm::errs() << "  ";
      MI->dump();
      llvm::errs() << "\n";
    }
  }
}

/// RegisterBuiltinMacro - Register the specified identifier in the identifier
/// table and mark it as a builtin macro to be expanded.
static IdentifierInfo *RegisterBuiltinMacro(Preprocessor &PP, const char *Name){
  // Get the identifier.
  IdentifierInfo *Id = PP.getIdentifierInfo(Name);

  // Mark it as being a macro that is builtin.
  MacroInfo *MI = PP.AllocateMacroInfo(SourceLocation());
  MI->setIsBuiltinMacro();
  PP.appendDefMacroDirective(Id, MI);
  return Id;
}

/// RegisterBuiltinMacros - Register builtin macros, such as __LINE__ with the
/// identifier table.
void Preprocessor::RegisterBuiltinMacros() {
  Ident__LINE__ = RegisterBuiltinMacro(*this, "__LINE__");
  Ident__FILE__ = RegisterBuiltinMacro(*this, "__FILE__");
  Ident__DATE__ = RegisterBuiltinMacro(*this, "__DATE__");
  Ident__TIME__ = RegisterBuiltinMacro(*this, "__TIME__");
  Ident__COUNTER__ = RegisterBuiltinMacro(*this, "__COUNTER__");
  Ident_Pragma  = RegisterBuiltinMacro(*this, "_Pragma");

  // C++ Standing Document Extensions.
  if (LangOpts.CPlusPlus)
    Ident__has_cpp_attribute =
        RegisterBuiltinMacro(*this, "__has_cpp_attribute");
  else
    Ident__has_cpp_attribute = nullptr;

  // GCC Extensions.
  Ident__BASE_FILE__     = RegisterBuiltinMacro(*this, "__BASE_FILE__");
  Ident__INCLUDE_LEVEL__ = RegisterBuiltinMacro(*this, "__INCLUDE_LEVEL__");
  Ident__TIMESTAMP__     = RegisterBuiltinMacro(*this, "__TIMESTAMP__");

  // Microsoft Extensions.
  if (LangOpts.MicrosoftExt) {
    Ident__identifier = RegisterBuiltinMacro(*this, "__identifier");
    Ident__pragma = RegisterBuiltinMacro(*this, "__pragma");
  } else {
    Ident__identifier = nullptr;
    Ident__pragma = nullptr;
  }

  // Clang Extensions.
  Ident__has_feature      = RegisterBuiltinMacro(*this, "__has_feature");
  Ident__has_extension    = RegisterBuiltinMacro(*this, "__has_extension");
  Ident__has_builtin      = RegisterBuiltinMacro(*this, "__has_builtin");
  Ident__has_attribute    = RegisterBuiltinMacro(*this, "__has_attribute");
  Ident__has_c_attribute  = RegisterBuiltinMacro(*this, "__has_c_attribute");
  Ident__has_declspec = RegisterBuiltinMacro(*this, "__has_declspec_attribute");
  Ident__has_include      = RegisterBuiltinMacro(*this, "__has_include");
  Ident__has_include_next = RegisterBuiltinMacro(*this, "__has_include_next");
  Ident__has_warning      = RegisterBuiltinMacro(*this, "__has_warning");
  Ident__is_identifier    = RegisterBuiltinMacro(*this, "__is_identifier");
  Ident__is_target_arch   = RegisterBuiltinMacro(*this, "__is_target_arch");
  Ident__is_target_vendor = RegisterBuiltinMacro(*this, "__is_target_vendor");
  Ident__is_target_os     = RegisterBuiltinMacro(*this, "__is_target_os");
  Ident__is_target_environment =
      RegisterBuiltinMacro(*this, "__is_target_environment");

  // Modules.
  Ident__building_module  = RegisterBuiltinMacro(*this, "__building_module");
  if (!LangOpts.CurrentModule.empty())
    Ident__MODULE__ = RegisterBuiltinMacro(*this, "__MODULE__");
  else
    Ident__MODULE__ = nullptr;
}

/// isTrivialSingleTokenExpansion - Return true if MI, which has a single token
/// in its expansion, currently expands to that token literally.
static bool isTrivialSingleTokenExpansion(const MacroInfo *MI,
                                          const IdentifierInfo *MacroIdent,
                                          Preprocessor &PP) {
  IdentifierInfo *II = MI->getReplacementToken(0).getIdentifierInfo();

  // If the token isn't an identifier, it's always literally expanded.
  if (!II) return true;

  // If the information about this identifier is out of date, update it from
  // the external source.
  if (II->isOutOfDate())
    PP.getExternalSource()->updateOutOfDateIdentifier(*II);

  // If the identifier is a macro, and if that macro is enabled, it may be
  // expanded so it's not a trivial expansion.
  if (auto *ExpansionMI = PP.getMacroInfo(II))
    if (ExpansionMI->isEnabled() &&
        // Fast expanding "#define X X" is ok, because X would be disabled.
        II != MacroIdent)
      return false;

  // If this is an object-like macro invocation, it is safe to trivially expand
  // it.
  if (MI->isObjectLike()) return true;

  // If this is a function-like macro invocation, it's safe to trivially expand
  // as long as the identifier is not a macro argument.
  return std::find(MI->param_begin(), MI->param_end(), II) == MI->param_end();
}

/// isNextPPTokenLParen - Determine whether the next preprocessor token to be
/// lexed is a '('.  If so, consume the token and return true, if not, this
/// method should have no observable side-effect on the lexed tokens.
bool Preprocessor::isNextPPTokenLParen() {
  // Do some quick tests for rejection cases.
  unsigned Val;
  if (CurLexer)
    Val = CurLexer->isNextPPTokenLParen();
  else
    Val = CurTokenLexer->isNextTokenLParen();

  if (Val == 2) {
    // We have run off the end.  If it's a source file we don't
    // examine enclosing ones (C99 5.1.1.2p4).  Otherwise walk up the
    // macro stack.
    if (CurPPLexer)
      return false;
    for (const IncludeStackInfo &Entry : llvm::reverse(IncludeMacroStack)) {
      if (Entry.TheLexer)
        Val = Entry.TheLexer->isNextPPTokenLParen();
      else
        Val = Entry.TheTokenLexer->isNextTokenLParen();

      if (Val != 2)
        break;

      // Ran off the end of a source file?
      if (Entry.ThePPLexer)
        return false;
    }
  }

  // Okay, if we know that the token is a '(', lex it and return.  Otherwise we
  // have found something that isn't a '(' or we found the end of the
  // translation unit.  In either case, return false.
  return Val == 1;
}

/// HandleMacroExpandedIdentifier - If an identifier token is read that is to be
/// expanded as a macro, handle it and return the next token as 'Identifier'.
bool Preprocessor::HandleMacroExpandedIdentifier(Token &Identifier,
                                                 const MacroDefinition &M) {
  MacroInfo *MI = M.getMacroInfo();

  // If this is a macro expansion in the "#if !defined(x)" line for the file,
  // then the macro could expand to different things in other contexts, we need
  // to disable the optimization in this case.
  if (CurPPLexer) CurPPLexer->MIOpt.ExpandedMacro();

  // If this is a builtin macro, like __LINE__ or _Pragma, handle it specially.
  if (MI->isBuiltinMacro()) {
    if (Callbacks)
      Callbacks->MacroExpands(Identifier, M, Identifier.getLocation(),
                              /*Args=*/nullptr);
    ExpandBuiltinMacro(Identifier);
    return true;
  }

  /// Args - If this is a function-like macro expansion, this contains,
  /// for each macro argument, the list of tokens that were provided to the
  /// invocation.
  MacroArgs *Args = nullptr;

  // Remember where the end of the expansion occurred.  For an object-like
  // macro, this is the identifier.  For a function-like macro, this is the ')'.
  SourceLocation ExpansionEnd = Identifier.getLocation();

  // If this is a function-like macro, read the arguments.
  if (MI->isFunctionLike()) {
    // Remember that we are now parsing the arguments to a macro invocation.
    // Preprocessor directives used inside macro arguments are not portable, and
    // this enables the warning.
    InMacroArgs = true;
    Args = ReadMacroCallArgumentList(Identifier, MI, ExpansionEnd);

    // Finished parsing args.
    InMacroArgs = false;

    // If there was an error parsing the arguments, bail out.
    if (!Args) return true;

    ++NumFnMacroExpanded;
  } else {
    ++NumMacroExpanded;
  }

  // Notice that this macro has been used.
  markMacroAsUsed(MI);

  // Remember where the token is expanded.
  SourceLocation ExpandLoc = Identifier.getLocation();
  SourceRange ExpansionRange(ExpandLoc, ExpansionEnd);

  if (Callbacks) {
    if (InMacroArgs) {
      // We can have macro expansion inside a conditional directive while
      // reading the function macro arguments. To ensure, in that case, that
      // MacroExpands callbacks still happen in source order, queue this
      // callback to have it happen after the function macro callback.
      DelayedMacroExpandsCallbacks.push_back(
          MacroExpandsInfo(Identifier, M, ExpansionRange));
    } else {
      Callbacks->MacroExpands(Identifier, M, ExpansionRange, Args);
      if (!DelayedMacroExpandsCallbacks.empty()) {
        for (const MacroExpandsInfo &Info : DelayedMacroExpandsCallbacks) {
          // FIXME: We lose macro args info with delayed callback.
          Callbacks->MacroExpands(Info.Tok, Info.MD, Info.Range,
                                  /*Args=*/nullptr);
        }
        DelayedMacroExpandsCallbacks.clear();
      }
    }
  }

  // If the macro definition is ambiguous, complain.
  if (M.isAmbiguous()) {
    Diag(Identifier, diag::warn_pp_ambiguous_macro)
      << Identifier.getIdentifierInfo();
    Diag(MI->getDefinitionLoc(), diag::note_pp_ambiguous_macro_chosen)
      << Identifier.getIdentifierInfo();
    M.forAllDefinitions([&](const MacroInfo *OtherMI) {
      if (OtherMI != MI)
        Diag(OtherMI->getDefinitionLoc(), diag::note_pp_ambiguous_macro_other)
          << Identifier.getIdentifierInfo();
    });
  }

  // If we started lexing a macro, enter the macro expansion body.

  // If this macro expands to no tokens, don't bother to push it onto the
  // expansion stack, only to take it right back off.
  if (MI->getNumTokens() == 0) {
    // No need for arg info.
    if (Args) Args->destroy(*this);

    // Propagate whitespace info as if we had pushed, then popped,
    // a macro context.
    Identifier.setFlag(Token::LeadingEmptyMacro);
    PropagateLineStartLeadingSpaceInfo(Identifier);
    ++NumFastMacroExpanded;
    return false;
  } else if (MI->getNumTokens() == 1 &&
             isTrivialSingleTokenExpansion(MI, Identifier.getIdentifierInfo(),
                                           *this)) {
    // Otherwise, if this macro expands into a single trivially-expanded
    // token: expand it now.  This handles common cases like
    // "#define VAL 42".

    // No need for arg info.
    if (Args) Args->destroy(*this);

    // Propagate the isAtStartOfLine/hasLeadingSpace markers of the macro
    // identifier to the expanded token.
    bool isAtStartOfLine = Identifier.isAtStartOfLine();
    bool hasLeadingSpace = Identifier.hasLeadingSpace();

    // Replace the result token.
    Identifier = MI->getReplacementToken(0);

    // Restore the StartOfLine/LeadingSpace markers.
    Identifier.setFlagValue(Token::StartOfLine , isAtStartOfLine);
    Identifier.setFlagValue(Token::LeadingSpace, hasLeadingSpace);

    // Update the tokens location to include both its expansion and physical
    // locations.
    SourceLocation Loc =
      SourceMgr.createExpansionLoc(Identifier.getLocation(), ExpandLoc,
                                   ExpansionEnd,Identifier.getLength());
    Identifier.setLocation(Loc);

    // If this is a disabled macro or #define X X, we must mark the result as
    // unexpandable.
    if (IdentifierInfo *NewII = Identifier.getIdentifierInfo()) {
      if (MacroInfo *NewMI = getMacroInfo(NewII))
        if (!NewMI->isEnabled() || NewMI == MI) {
          Identifier.setFlag(Token::DisableExpand);
          // Don't warn for "#define X X" like "#define bool bool" from
          // stdbool.h.
          if (NewMI != MI || MI->isFunctionLike())
            Diag(Identifier, diag::pp_disabled_macro_expansion);
        }
    }

    // Since this is not an identifier token, it can't be macro expanded, so
    // we're done.
    ++NumFastMacroExpanded;
    return true;
  }

  // Start expanding the macro.
  EnterMacro(Identifier, ExpansionEnd, MI, Args);
  return false;
}

enum Bracket {
  Brace,
  Paren
};

/// CheckMatchedBrackets - Returns true if the braces and parentheses in the
/// token vector are properly nested.
static bool CheckMatchedBrackets(const SmallVectorImpl<Token> &Tokens) {
  SmallVector<Bracket, 8> Brackets;
  for (SmallVectorImpl<Token>::const_iterator I = Tokens.begin(),
                                              E = Tokens.end();
       I != E; ++I) {
    if (I->is(tok::l_paren)) {
      Brackets.push_back(Paren);
    } else if (I->is(tok::r_paren)) {
      if (Brackets.empty() || Brackets.back() == Brace)
        return false;
      Brackets.pop_back();
    } else if (I->is(tok::l_brace)) {
      Brackets.push_back(Brace);
    } else if (I->is(tok::r_brace)) {
      if (Brackets.empty() || Brackets.back() == Paren)
        return false;
      Brackets.pop_back();
    }
  }
  return Brackets.empty();
}

/// GenerateNewArgTokens - Returns true if OldTokens can be converted to a new
/// vector of tokens in NewTokens.  The new number of arguments will be placed
/// in NumArgs and the ranges which need to surrounded in parentheses will be
/// in ParenHints.
/// Returns false if the token stream cannot be changed.  If this is because
/// of an initializer list starting a macro argument, the range of those
/// initializer lists will be place in InitLists.
static bool GenerateNewArgTokens(Preprocessor &PP,
                                 SmallVectorImpl<Token> &OldTokens,
                                 SmallVectorImpl<Token> &NewTokens,
                                 unsigned &NumArgs,
                                 SmallVectorImpl<SourceRange> &ParenHints,
                                 SmallVectorImpl<SourceRange> &InitLists) {
  if (!CheckMatchedBrackets(OldTokens))
    return false;

  // Once it is known that the brackets are matched, only a simple count of the
  // braces is needed.
  unsigned Braces = 0;

  // First token of a new macro argument.
  SmallVectorImpl<Token>::iterator ArgStartIterator = OldTokens.begin();

  // First closing brace in a new macro argument.  Used to generate
  // SourceRanges for InitLists.
  SmallVectorImpl<Token>::iterator ClosingBrace = OldTokens.end();
  NumArgs = 0;
  Token TempToken;
  // Set to true when a macro separator token is found inside a braced list.
  // If true, the fixed argument spans multiple old arguments and ParenHints
  // will be updated.
  bool FoundSeparatorToken = false;
  for (SmallVectorImpl<Token>::iterator I = OldTokens.begin(),
                                        E = OldTokens.end();
       I != E; ++I) {
    if (I->is(tok::l_brace)) {
      ++Braces;
    } else if (I->is(tok::r_brace)) {
      --Braces;
      if (Braces == 0 && ClosingBrace == E && FoundSeparatorToken)
        ClosingBrace = I;
    } else if (I->is(tok::eof)) {
      // EOF token is used to separate macro arguments
      if (Braces != 0) {
        // Assume comma separator is actually braced list separator and change
        // it back to a comma.
        FoundSeparatorToken = true;
        I->setKind(tok::comma);
        I->setLength(1);
      } else { // Braces == 0
        // Separator token still separates arguments.
        ++NumArgs;

        // If the argument starts with a brace, it can't be fixed with
        // parentheses.  A different diagnostic will be given.
        if (FoundSeparatorToken && ArgStartIterator->is(tok::l_brace)) {
          InitLists.push_back(
              SourceRange(ArgStartIterator->getLocation(),
                          PP.getLocForEndOfToken(ClosingBrace->getLocation())));
          ClosingBrace = E;
        }

        // Add left paren
        if (FoundSeparatorToken) {
          TempToken.startToken();
          TempToken.setKind(tok::l_paren);
          TempToken.setLocation(ArgStartIterator->getLocation());
          TempToken.setLength(0);
          NewTokens.push_back(TempToken);
        }

        // Copy over argument tokens
        NewTokens.insert(NewTokens.end(), ArgStartIterator, I);

        // Add right paren and store the paren locations in ParenHints
        if (FoundSeparatorToken) {
          SourceLocation Loc = PP.getLocForEndOfToken((I - 1)->getLocation());
          TempToken.startToken();
          TempToken.setKind(tok::r_paren);
          TempToken.setLocation(Loc);
          TempToken.setLength(0);
          NewTokens.push_back(TempToken);
          ParenHints.push_back(SourceRange(ArgStartIterator->getLocation(),
                                           Loc));
        }

        // Copy separator token
        NewTokens.push_back(*I);

        // Reset values
        ArgStartIterator = I + 1;
        FoundSeparatorToken = false;
      }
    }
  }

  return !ParenHints.empty() && InitLists.empty();
}

/// ReadFunctionLikeMacroArgs - After reading "MACRO" and knowing that the next
/// token is the '(' of the macro, this method is invoked to read all of the
/// actual arguments specified for the macro invocation.  This returns null on
/// error.
MacroArgs *Preprocessor::ReadMacroCallArgumentList(Token &MacroName,
                                                   MacroInfo *MI,
                                                   SourceLocation &MacroEnd) {
  // The number of fixed arguments to parse.
  unsigned NumFixedArgsLeft = MI->getNumParams();
  bool isVariadic = MI->isVariadic();

  // Outer loop, while there are more arguments, keep reading them.
  Token Tok;

  // Read arguments as unexpanded tokens.  This avoids issues, e.g., where
  // an argument value in a macro could expand to ',' or '(' or ')'.
  LexUnexpandedToken(Tok);
  assert(Tok.is(tok::l_paren) && "Error computing l-paren-ness?");

  // ArgTokens - Build up a list of tokens that make up each argument.  Each
  // argument is separated by an EOF token.  Use a SmallVector so we can avoid
  // heap allocations in the common case.
  SmallVector<Token, 64> ArgTokens;
  bool ContainsCodeCompletionTok = false;
  bool FoundElidedComma = false;

  SourceLocation TooManyArgsLoc;

  unsigned NumActuals = 0;
  while (Tok.isNot(tok::r_paren)) {
    if (ContainsCodeCompletionTok && Tok.isOneOf(tok::eof, tok::eod))
      break;

    assert(Tok.isOneOf(tok::l_paren, tok::comma) &&
           "only expect argument separators here");

    size_t ArgTokenStart = ArgTokens.size();
    SourceLocation ArgStartLoc = Tok.getLocation();

    // C99 6.10.3p11: Keep track of the number of l_parens we have seen.  Note
    // that we already consumed the first one.
    unsigned NumParens = 0;

    while (true) {
      // Read arguments as unexpanded tokens.  This avoids issues, e.g., where
      // an argument value in a macro could expand to ',' or '(' or ')'.
      LexUnexpandedToken(Tok);

      if (Tok.isOneOf(tok::eof, tok::eod)) { // "#if f(<eof>" & "#if f(\n"
        if (!ContainsCodeCompletionTok) {
          Diag(MacroName, diag::err_unterm_macro_invoc);
          Diag(MI->getDefinitionLoc(), diag::note_macro_here)
            << MacroName.getIdentifierInfo();
          // Do not lose the EOF/EOD.  Return it to the client.
          MacroName = Tok;
          return nullptr;
        }
        // Do not lose the EOF/EOD.
        auto Toks = llvm::make_unique<Token[]>(1);
        Toks[0] = Tok;
        EnterTokenStream(std::move(Toks), 1, true);
        break;
      } else if (Tok.is(tok::r_paren)) {
        // If we found the ) token, the macro arg list is done.
        if (NumParens-- == 0) {
          MacroEnd = Tok.getLocation();
          if (!ArgTokens.empty() &&
              ArgTokens.back().commaAfterElided()) {
            FoundElidedComma = true;
          }
          break;
        }
      } else if (Tok.is(tok::l_paren)) {
        ++NumParens;
      } else if (Tok.is(tok::comma) && NumParens == 0 &&
                 !(Tok.getFlags() & Token::IgnoredComma)) {
        // In Microsoft-compatibility mode, single commas from nested macro
        // expansions should not be considered as argument separators. We test
        // for this with the IgnoredComma token flag above.

        // Comma ends this argument if there are more fixed arguments expected.
        // However, if this is a variadic macro, and this is part of the
        // variadic part, then the comma is just an argument token.
        if (!isVariadic) break;
        if (NumFixedArgsLeft > 1)
          break;
      } else if (Tok.is(tok::comment) && !KeepMacroComments) {
        // If this is a comment token in the argument list and we're just in
        // -C mode (not -CC mode), discard the comment.
        continue;
      } else if (!Tok.isAnnotation() && Tok.getIdentifierInfo() != nullptr) {
        // Reading macro arguments can cause macros that we are currently
        // expanding from to be popped off the expansion stack.  Doing so causes
        // them to be reenabled for expansion.  Here we record whether any
        // identifiers we lex as macro arguments correspond to disabled macros.
        // If so, we mark the token as noexpand.  This is a subtle aspect of
        // C99 6.10.3.4p2.
        if (MacroInfo *MI = getMacroInfo(Tok.getIdentifierInfo()))
          if (!MI->isEnabled())
            Tok.setFlag(Token::DisableExpand);
      } else if (Tok.is(tok::code_completion)) {
        ContainsCodeCompletionTok = true;
        if (CodeComplete)
          CodeComplete->CodeCompleteMacroArgument(MacroName.getIdentifierInfo(),
                                                  MI, NumActuals);
        // Don't mark that we reached the code-completion point because the
        // parser is going to handle the token and there will be another
        // code-completion callback.
      }

      ArgTokens.push_back(Tok);
    }

    // If this was an empty argument list foo(), don't add this as an empty
    // argument.
    if (ArgTokens.empty() && Tok.getKind() == tok::r_paren)
      break;

    // If this is not a variadic macro, and too many args were specified, emit
    // an error.
    if (!isVariadic && NumFixedArgsLeft == 0 && TooManyArgsLoc.isInvalid()) {
      if (ArgTokens.size() != ArgTokenStart)
        TooManyArgsLoc = ArgTokens[ArgTokenStart].getLocation();
      else
        TooManyArgsLoc = ArgStartLoc;
    }

    // Empty arguments are standard in C99 and C++0x, and are supported as an
    // extension in other modes.
    if (ArgTokens.size() == ArgTokenStart && !LangOpts.C99)
      Diag(Tok, LangOpts.CPlusPlus11 ?
           diag::warn_cxx98_compat_empty_fnmacro_arg :
           diag::ext_empty_fnmacro_arg);

    // Add a marker EOF token to the end of the token list for this argument.
    Token EOFTok;
    EOFTok.startToken();
    EOFTok.setKind(tok::eof);
    EOFTok.setLocation(Tok.getLocation());
    EOFTok.setLength(0);
    ArgTokens.push_back(EOFTok);
    ++NumActuals;
    if (!ContainsCodeCompletionTok && NumFixedArgsLeft != 0)
      --NumFixedArgsLeft;
  }

  // Okay, we either found the r_paren.  Check to see if we parsed too few
  // arguments.
  unsigned MinArgsExpected = MI->getNumParams();

  // If this is not a variadic macro, and too many args were specified, emit
  // an error.
  if (!isVariadic && NumActuals > MinArgsExpected &&
      !ContainsCodeCompletionTok) {
    // Emit the diagnostic at the macro name in case there is a missing ).
    // Emitting it at the , could be far away from the macro name.
    Diag(TooManyArgsLoc, diag::err_too_many_args_in_macro_invoc);
    Diag(MI->getDefinitionLoc(), diag::note_macro_here)
      << MacroName.getIdentifierInfo();

    // Commas from braced initializer lists will be treated as argument
    // separators inside macros.  Attempt to correct for this with parentheses.
    // TODO: See if this can be generalized to angle brackets for templates
    // inside macro arguments.

    SmallVector<Token, 4> FixedArgTokens;
    unsigned FixedNumArgs = 0;
    SmallVector<SourceRange, 4> ParenHints, InitLists;
    if (!GenerateNewArgTokens(*this, ArgTokens, FixedArgTokens, FixedNumArgs,
                              ParenHints, InitLists)) {
      if (!InitLists.empty()) {
        DiagnosticBuilder DB =
            Diag(MacroName,
                 diag::note_init_list_at_beginning_of_macro_argument);
        for (SourceRange Range : InitLists)
          DB << Range;
      }
      return nullptr;
    }
    if (FixedNumArgs != MinArgsExpected)
      return nullptr;

    DiagnosticBuilder DB = Diag(MacroName, diag::note_suggest_parens_for_macro);
    for (SourceRange ParenLocation : ParenHints) {
      DB << FixItHint::CreateInsertion(ParenLocation.getBegin(), "(");
      DB << FixItHint::CreateInsertion(ParenLocation.getEnd(), ")");
    }
    ArgTokens.swap(FixedArgTokens);
    NumActuals = FixedNumArgs;
  }

  // See MacroArgs instance var for description of this.
  bool isVarargsElided = false;

  if (ContainsCodeCompletionTok) {
    // Recover from not-fully-formed macro invocation during code-completion.
    Token EOFTok;
    EOFTok.startToken();
    EOFTok.setKind(tok::eof);
    EOFTok.setLocation(Tok.getLocation());
    EOFTok.setLength(0);
    for (; NumActuals < MinArgsExpected; ++NumActuals)
      ArgTokens.push_back(EOFTok);
  }

  if (NumActuals < MinArgsExpected) {
    // There are several cases where too few arguments is ok, handle them now.
    if (NumActuals == 0 && MinArgsExpected == 1) {
      // #define A(X)  or  #define A(...)   ---> A()

      // If there is exactly one argument, and that argument is missing,
      // then we have an empty "()" argument empty list.  This is fine, even if
      // the macro expects one argument (the argument is just empty).
      isVarargsElided = MI->isVariadic();
    } else if ((FoundElidedComma || MI->isVariadic()) &&
               (NumActuals+1 == MinArgsExpected ||  // A(x, ...) -> A(X)
                (NumActuals == 0 && MinArgsExpected == 2))) {// A(x,...) -> A()
      // Varargs where the named vararg parameter is missing: OK as extension.
      //   #define A(x, ...)
      //   A("blah")
      //
      // If the macro contains the comma pasting extension, the diagnostic
      // is suppressed; we know we'll get another diagnostic later.
      if (!MI->hasCommaPasting()) {
        Diag(Tok, diag::ext_missing_varargs_arg);
        Diag(MI->getDefinitionLoc(), diag::note_macro_here)
          << MacroName.getIdentifierInfo();
      }

      // Remember this occurred, allowing us to elide the comma when used for
      // cases like:
      //   #define A(x, foo...) blah(a, ## foo)
      //   #define B(x, ...) blah(a, ## __VA_ARGS__)
      //   #define C(...) blah(a, ## __VA_ARGS__)
      //  A(x) B(x) C()
      isVarargsElided = true;
    } else if (!ContainsCodeCompletionTok) {
      // Otherwise, emit the error.
      Diag(Tok, diag::err_too_few_args_in_macro_invoc);
      Diag(MI->getDefinitionLoc(), diag::note_macro_here)
        << MacroName.getIdentifierInfo();
      return nullptr;
    }

    // Add a marker EOF token to the end of the token list for this argument.
    SourceLocation EndLoc = Tok.getLocation();
    Tok.startToken();
    Tok.setKind(tok::eof);
    Tok.setLocation(EndLoc);
    Tok.setLength(0);
    ArgTokens.push_back(Tok);

    // If we expect two arguments, add both as empty.
    if (NumActuals == 0 && MinArgsExpected == 2)
      ArgTokens.push_back(Tok);

  } else if (NumActuals > MinArgsExpected && !MI->isVariadic() &&
             !ContainsCodeCompletionTok) {
    // Emit the diagnostic at the macro name in case there is a missing ).
    // Emitting it at the , could be far away from the macro name.
    Diag(MacroName, diag::err_too_many_args_in_macro_invoc);
    Diag(MI->getDefinitionLoc(), diag::note_macro_here)
      << MacroName.getIdentifierInfo();
    return nullptr;
  }

  return MacroArgs::create(MI, ArgTokens, isVarargsElided, *this);
}

/// Keeps macro expanded tokens for TokenLexers.
//
/// Works like a stack; a TokenLexer adds the macro expanded tokens that is
/// going to lex in the cache and when it finishes the tokens are removed
/// from the end of the cache.
Token *Preprocessor::cacheMacroExpandedTokens(TokenLexer *tokLexer,
                                              ArrayRef<Token> tokens) {
  assert(tokLexer);
  if (tokens.empty())
    return nullptr;

  size_t newIndex = MacroExpandedTokens.size();
  bool cacheNeedsToGrow = tokens.size() >
                      MacroExpandedTokens.capacity()-MacroExpandedTokens.size();
  MacroExpandedTokens.append(tokens.begin(), tokens.end());

  if (cacheNeedsToGrow) {
    // Go through all the TokenLexers whose 'Tokens' pointer points in the
    // buffer and update the pointers to the (potential) new buffer array.
    for (const auto &Lexer : MacroExpandingLexersStack) {
      TokenLexer *prevLexer;
      size_t tokIndex;
      std::tie(prevLexer, tokIndex) = Lexer;
      prevLexer->Tokens = MacroExpandedTokens.data() + tokIndex;
    }
  }

  MacroExpandingLexersStack.push_back(std::make_pair(tokLexer, newIndex));
  return MacroExpandedTokens.data() + newIndex;
}

void Preprocessor::removeCachedMacroExpandedTokensOfLastLexer() {
  assert(!MacroExpandingLexersStack.empty());
  size_t tokIndex = MacroExpandingLexersStack.back().second;
  assert(tokIndex < MacroExpandedTokens.size());
  // Pop the cached macro expanded tokens from the end.
  MacroExpandedTokens.resize(tokIndex);
  MacroExpandingLexersStack.pop_back();
}

/// ComputeDATE_TIME - Compute the current time, enter it into the specified
/// scratch buffer, then return DATELoc/TIMELoc locations with the position of
/// the identifier tokens inserted.
static void ComputeDATE_TIME(SourceLocation &DATELoc, SourceLocation &TIMELoc,
                             Preprocessor &PP) {
  time_t TT = time(nullptr);
  struct tm *TM = localtime(&TT);

  static const char * const Months[] = {
    "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
  };

  {
    SmallString<32> TmpBuffer;
    llvm::raw_svector_ostream TmpStream(TmpBuffer);
    TmpStream << llvm::format("\"%s %2d %4d\"", Months[TM->tm_mon],
                              TM->tm_mday, TM->tm_year + 1900);
    Token TmpTok;
    TmpTok.startToken();
    PP.CreateString(TmpStream.str(), TmpTok);
    DATELoc = TmpTok.getLocation();
  }

  {
    SmallString<32> TmpBuffer;
    llvm::raw_svector_ostream TmpStream(TmpBuffer);
    TmpStream << llvm::format("\"%02d:%02d:%02d\"",
                              TM->tm_hour, TM->tm_min, TM->tm_sec);
    Token TmpTok;
    TmpTok.startToken();
    PP.CreateString(TmpStream.str(), TmpTok);
    TIMELoc = TmpTok.getLocation();
  }
}

/// HasFeature - Return true if we recognize and implement the feature
/// specified by the identifier as a standard language feature.
static bool HasFeature(const Preprocessor &PP, StringRef Feature) {
  const LangOptions &LangOpts = PP.getLangOpts();

  // Normalize the feature name, __foo__ becomes foo.
  if (Feature.startswith("__") && Feature.endswith("__") && Feature.size() >= 4)
    Feature = Feature.substr(2, Feature.size() - 4);

#define FEATURE(Name, Predicate) .Case(#Name, Predicate)
  return llvm::StringSwitch<bool>(Feature)
#include "clang/Basic/Features.def"
      .Default(false);
#undef FEATURE
}

/// HasExtension - Return true if we recognize and implement the feature
/// specified by the identifier, either as an extension or a standard language
/// feature.
static bool HasExtension(const Preprocessor &PP, StringRef Extension) {
  if (HasFeature(PP, Extension))
    return true;

  // If the use of an extension results in an error diagnostic, extensions are
  // effectively unavailable, so just return false here.
  if (PP.getDiagnostics().getExtensionHandlingBehavior() >=
      diag::Severity::Error)
    return false;

  const LangOptions &LangOpts = PP.getLangOpts();

  // Normalize the extension name, __foo__ becomes foo.
  if (Extension.startswith("__") && Extension.endswith("__") &&
      Extension.size() >= 4)
    Extension = Extension.substr(2, Extension.size() - 4);

    // Because we inherit the feature list from HasFeature, this string switch
    // must be less restrictive than HasFeature's.
#define EXTENSION(Name, Predicate) .Case(#Name, Predicate)
  return llvm::StringSwitch<bool>(Extension)
#include "clang/Basic/Features.def"
      .Default(false);
#undef EXTENSION
}

/// EvaluateHasIncludeCommon - Process a '__has_include("path")'
/// or '__has_include_next("path")' expression.
/// Returns true if successful.
static bool EvaluateHasIncludeCommon(Token &Tok,
                                     IdentifierInfo *II, Preprocessor &PP,
                                     const DirectoryLookup *LookupFrom,
                                     const FileEntry *LookupFromFile) {
  // Save the location of the current token.  If a '(' is later found, use
  // that location.  If not, use the end of this location instead.
  SourceLocation LParenLoc = Tok.getLocation();

  // These expressions are only allowed within a preprocessor directive.
  if (!PP.isParsingIfOrElifDirective()) {
    PP.Diag(LParenLoc, diag::err_pp_directive_required) << II;
    // Return a valid identifier token.
    assert(Tok.is(tok::identifier));
    Tok.setIdentifierInfo(II);
    return false;
  }

  // Get '('.
  PP.LexNonComment(Tok);

  // Ensure we have a '('.
  if (Tok.isNot(tok::l_paren)) {
    // No '(', use end of last token.
    LParenLoc = PP.getLocForEndOfToken(LParenLoc);
    PP.Diag(LParenLoc, diag::err_pp_expected_after) << II << tok::l_paren;
    // If the next token looks like a filename or the start of one,
    // assume it is and process it as such.
    if (!Tok.is(tok::angle_string_literal) && !Tok.is(tok::string_literal) &&
        !Tok.is(tok::less))
      return false;
  } else {
    // Save '(' location for possible missing ')' message.
    LParenLoc = Tok.getLocation();

    if (PP.getCurrentLexer()) {
      // Get the file name.
      PP.getCurrentLexer()->LexIncludeFilename(Tok);
    } else {
      // We're in a macro, so we can't use LexIncludeFilename; just
      // grab the next token.
      PP.Lex(Tok);
    }
  }

  // Reserve a buffer to get the spelling.
  SmallString<128> FilenameBuffer;
  StringRef Filename;
  SourceLocation EndLoc;

  switch (Tok.getKind()) {
  case tok::eod:
    // If the token kind is EOD, the error has already been diagnosed.
    return false;

  case tok::angle_string_literal:
  case tok::string_literal: {
    bool Invalid = false;
    Filename = PP.getSpelling(Tok, FilenameBuffer, &Invalid);
    if (Invalid)
      return false;
    break;
  }

  case tok::less:
    // This could be a <foo/bar.h> file coming from a macro expansion.  In this
    // case, glue the tokens together into FilenameBuffer and interpret those.
    FilenameBuffer.push_back('<');
    if (PP.ConcatenateIncludeName(FilenameBuffer, EndLoc)) {
      // Let the caller know a <eod> was found by changing the Token kind.
      Tok.setKind(tok::eod);
      return false;   // Found <eod> but no ">"?  Diagnostic already emitted.
    }
    Filename = FilenameBuffer;
    break;
  default:
    PP.Diag(Tok.getLocation(), diag::err_pp_expects_filename);
    return false;
  }

  SourceLocation FilenameLoc = Tok.getLocation();

  // Get ')'.
  PP.LexNonComment(Tok);

  // Ensure we have a trailing ).
  if (Tok.isNot(tok::r_paren)) {
    PP.Diag(PP.getLocForEndOfToken(FilenameLoc), diag::err_pp_expected_after)
        << II << tok::r_paren;
    PP.Diag(LParenLoc, diag::note_matching) << tok::l_paren;
    return false;
  }

  bool isAngled = PP.GetIncludeFilenameSpelling(Tok.getLocation(), Filename);
  // If GetIncludeFilenameSpelling set the start ptr to null, there was an
  // error.
  if (Filename.empty())
    return false;

  // Search include directories.
  const DirectoryLookup *CurDir;
  const FileEntry *File =
      PP.LookupFile(FilenameLoc, Filename, isAngled, LookupFrom, LookupFromFile,
                    CurDir, nullptr, nullptr, nullptr, nullptr);

  if (PPCallbacks *Callbacks = PP.getPPCallbacks()) {
    SrcMgr::CharacteristicKind FileType = SrcMgr::C_User;
    if (File)
      FileType = PP.getHeaderSearchInfo().getFileDirFlavor(File);
    Callbacks->HasInclude(FilenameLoc, Filename, isAngled, File, FileType);
  }

  // Get the result value.  A result of true means the file exists.
  return File != nullptr;
}

/// EvaluateHasInclude - Process a '__has_include("path")' expression.
/// Returns true if successful.
static bool EvaluateHasInclude(Token &Tok, IdentifierInfo *II,
                               Preprocessor &PP) {
  return EvaluateHasIncludeCommon(Tok, II, PP, nullptr, nullptr);
}

/// EvaluateHasIncludeNext - Process '__has_include_next("path")' expression.
/// Returns true if successful.
static bool EvaluateHasIncludeNext(Token &Tok,
                                   IdentifierInfo *II, Preprocessor &PP) {
  // __has_include_next is like __has_include, except that we start
  // searching after the current found directory.  If we can't do this,
  // issue a diagnostic.
  // FIXME: Factor out duplication with
  // Preprocessor::HandleIncludeNextDirective.
  const DirectoryLookup *Lookup = PP.GetCurDirLookup();
  const FileEntry *LookupFromFile = nullptr;
  if (PP.isInPrimaryFile() && PP.getLangOpts().IsHeaderFile) {
    // If the main file is a header, then it's either for PCH/AST generation,
    // or libclang opened it. Either way, handle it as a normal include below
    // and do not complain about __has_include_next.
  } else if (PP.isInPrimaryFile()) {
    Lookup = nullptr;
    PP.Diag(Tok, diag::pp_include_next_in_primary);
  } else if (PP.getCurrentLexerSubmodule()) {
    // Start looking up in the directory *after* the one in which the current
    // file would be found, if any.
    assert(PP.getCurrentLexer() && "#include_next directive in macro?");
    LookupFromFile = PP.getCurrentLexer()->getFileEntry();
    Lookup = nullptr;
  } else if (!Lookup) {
    PP.Diag(Tok, diag::pp_include_next_absolute_path);
  } else {
    // Start looking up in the next directory.
    ++Lookup;
  }

  return EvaluateHasIncludeCommon(Tok, II, PP, Lookup, LookupFromFile);
}

/// Process single-argument builtin feature-like macros that return
/// integer values.
static void EvaluateFeatureLikeBuiltinMacro(llvm::raw_svector_ostream& OS,
                                            Token &Tok, IdentifierInfo *II,
                                            Preprocessor &PP,
                                            llvm::function_ref<
                                              int(Token &Tok,
                                                  bool &HasLexedNextTok)> Op) {
  // Parse the initial '('.
  PP.LexUnexpandedToken(Tok);
  if (Tok.isNot(tok::l_paren)) {
    PP.Diag(Tok.getLocation(), diag::err_pp_expected_after) << II
                                                            << tok::l_paren;

    // Provide a dummy '0' value on output stream to elide further errors.
    if (!Tok.isOneOf(tok::eof, tok::eod)) {
      OS << 0;
      Tok.setKind(tok::numeric_constant);
    }
    return;
  }

  unsigned ParenDepth = 1;
  SourceLocation LParenLoc = Tok.getLocation();
  llvm::Optional<int> Result;

  Token ResultTok;
  bool SuppressDiagnostic = false;
  while (true) {
    // Parse next token.
    PP.LexUnexpandedToken(Tok);

already_lexed:
    switch (Tok.getKind()) {
      case tok::eof:
      case tok::eod:
        // Don't provide even a dummy value if the eod or eof marker is
        // reached.  Simply provide a diagnostic.
        PP.Diag(Tok.getLocation(), diag::err_unterm_macro_invoc);
        return;

      case tok::comma:
        if (!SuppressDiagnostic) {
          PP.Diag(Tok.getLocation(), diag::err_too_many_args_in_macro_invoc);
          SuppressDiagnostic = true;
        }
        continue;

      case tok::l_paren:
        ++ParenDepth;
        if (Result.hasValue())
          break;
        if (!SuppressDiagnostic) {
          PP.Diag(Tok.getLocation(), diag::err_pp_nested_paren) << II;
          SuppressDiagnostic = true;
        }
        continue;

      case tok::r_paren:
        if (--ParenDepth > 0)
          continue;

        // The last ')' has been reached; return the value if one found or
        // a diagnostic and a dummy value.
        if (Result.hasValue())
          OS << Result.getValue();
        else {
          OS << 0;
          if (!SuppressDiagnostic)
            PP.Diag(Tok.getLocation(), diag::err_too_few_args_in_macro_invoc);
        }
        Tok.setKind(tok::numeric_constant);
        return;

      default: {
        // Parse the macro argument, if one not found so far.
        if (Result.hasValue())
          break;

        bool HasLexedNextToken = false;
        Result = Op(Tok, HasLexedNextToken);
        ResultTok = Tok;
        if (HasLexedNextToken)
          goto already_lexed;
        continue;
      }
    }

    // Diagnose missing ')'.
    if (!SuppressDiagnostic) {
      if (auto Diag = PP.Diag(Tok.getLocation(), diag::err_pp_expected_after)) {
        if (IdentifierInfo *LastII = ResultTok.getIdentifierInfo())
          Diag << LastII;
        else
          Diag << ResultTok.getKind();
        Diag << tok::r_paren << ResultTok.getLocation();
      }
      PP.Diag(LParenLoc, diag::note_matching) << tok::l_paren;
      SuppressDiagnostic = true;
    }
  }
}

/// Helper function to return the IdentifierInfo structure of a Token
/// or generate a diagnostic if none available.
static IdentifierInfo *ExpectFeatureIdentifierInfo(Token &Tok,
                                                   Preprocessor &PP,
                                                   signed DiagID) {
  IdentifierInfo *II;
  if (!Tok.isAnnotation() && (II = Tok.getIdentifierInfo()))
    return II;

  PP.Diag(Tok.getLocation(), DiagID);
  return nullptr;
}

/// Implements the __is_target_arch builtin macro.
static bool isTargetArch(const TargetInfo &TI, const IdentifierInfo *II) {
  std::string ArchName = II->getName().lower() + "--";
  llvm::Triple Arch(ArchName);
  const llvm::Triple &TT = TI.getTriple();
  if (TT.isThumb()) {
    // arm matches thumb or thumbv7. armv7 matches thumbv7.
    if ((Arch.getSubArch() == llvm::Triple::NoSubArch ||
         Arch.getSubArch() == TT.getSubArch()) &&
        ((TT.getArch() == llvm::Triple::thumb &&
          Arch.getArch() == llvm::Triple::arm) ||
         (TT.getArch() == llvm::Triple::thumbeb &&
          Arch.getArch() == llvm::Triple::armeb)))
      return true;
  }
  // Check the parsed arch when it has no sub arch to allow Clang to
  // match thumb to thumbv7 but to prohibit matching thumbv6 to thumbv7.
  return (Arch.getSubArch() == llvm::Triple::NoSubArch ||
          Arch.getSubArch() == TT.getSubArch()) &&
         Arch.getArch() == TT.getArch();
}

/// Implements the __is_target_vendor builtin macro.
static bool isTargetVendor(const TargetInfo &TI, const IdentifierInfo *II) {
  StringRef VendorName = TI.getTriple().getVendorName();
  if (VendorName.empty())
    VendorName = "unknown";
  return VendorName.equals_lower(II->getName());
}

/// Implements the __is_target_os builtin macro.
static bool isTargetOS(const TargetInfo &TI, const IdentifierInfo *II) {
  std::string OSName =
      (llvm::Twine("unknown-unknown-") + II->getName().lower()).str();
  llvm::Triple OS(OSName);
  if (OS.getOS() == llvm::Triple::Darwin) {
    // Darwin matches macos, ios, etc.
    return TI.getTriple().isOSDarwin();
  }
  return TI.getTriple().getOS() == OS.getOS();
}

/// Implements the __is_target_environment builtin macro.
static bool isTargetEnvironment(const TargetInfo &TI,
                                const IdentifierInfo *II) {
  std::string EnvName = (llvm::Twine("---") + II->getName().lower()).str();
  llvm::Triple Env(EnvName);
  return TI.getTriple().getEnvironment() == Env.getEnvironment();
}

/// ExpandBuiltinMacro - If an identifier token is read that is to be expanded
/// as a builtin macro, handle it and return the next token as 'Tok'.
void Preprocessor::ExpandBuiltinMacro(Token &Tok) {
  // Figure out which token this is.
  IdentifierInfo *II = Tok.getIdentifierInfo();
  assert(II && "Can't be a macro without id info!");

  // If this is an _Pragma or Microsoft __pragma directive, expand it,
  // invoke the pragma handler, then lex the token after it.
  if (II == Ident_Pragma)
    return Handle_Pragma(Tok);
  else if (II == Ident__pragma) // in non-MS mode this is null
    return HandleMicrosoft__pragma(Tok);

  ++NumBuiltinMacroExpanded;

  SmallString<128> TmpBuffer;
  llvm::raw_svector_ostream OS(TmpBuffer);

  // Set up the return result.
  Tok.setIdentifierInfo(nullptr);
  Tok.clearFlag(Token::NeedsCleaning);

  if (II == Ident__LINE__) {
    // C99 6.10.8: "__LINE__: The presumed line number (within the current
    // source file) of the current source line (an integer constant)".  This can
    // be affected by #line.
    SourceLocation Loc = Tok.getLocation();

    // Advance to the location of the first _, this might not be the first byte
    // of the token if it starts with an escaped newline.
    Loc = AdvanceToTokenCharacter(Loc, 0);

    // One wrinkle here is that GCC expands __LINE__ to location of the *end* of
    // a macro expansion.  This doesn't matter for object-like macros, but
    // can matter for a function-like macro that expands to contain __LINE__.
    // Skip down through expansion points until we find a file loc for the
    // end of the expansion history.
    Loc = SourceMgr.getExpansionRange(Loc).getEnd();
    PresumedLoc PLoc = SourceMgr.getPresumedLoc(Loc);

    // __LINE__ expands to a simple numeric value.
    OS << (PLoc.isValid()? PLoc.getLine() : 1);
    Tok.setKind(tok::numeric_constant);
  } else if (II == Ident__FILE__ || II == Ident__BASE_FILE__) {
    // C99 6.10.8: "__FILE__: The presumed name of the current source file (a
    // character string literal)". This can be affected by #line.
    PresumedLoc PLoc = SourceMgr.getPresumedLoc(Tok.getLocation());

    // __BASE_FILE__ is a GNU extension that returns the top of the presumed
    // #include stack instead of the current file.
    if (II == Ident__BASE_FILE__ && PLoc.isValid()) {
      SourceLocation NextLoc = PLoc.getIncludeLoc();
      while (NextLoc.isValid()) {
        PLoc = SourceMgr.getPresumedLoc(NextLoc);
        if (PLoc.isInvalid())
          break;

        NextLoc = PLoc.getIncludeLoc();
      }
    }

    // Escape this filename.  Turn '\' -> '\\' '"' -> '\"'
    SmallString<128> FN;
    if (PLoc.isValid()) {
      FN += PLoc.getFilename();
      Lexer::Stringify(FN);
      OS << '"' << FN << '"';
    }
    Tok.setKind(tok::string_literal);
  } else if (II == Ident__DATE__) {
    Diag(Tok.getLocation(), diag::warn_pp_date_time);
    if (!DATELoc.isValid())
      ComputeDATE_TIME(DATELoc, TIMELoc, *this);
    Tok.setKind(tok::string_literal);
    Tok.setLength(strlen("\"Mmm dd yyyy\""));
    Tok.setLocation(SourceMgr.createExpansionLoc(DATELoc, Tok.getLocation(),
                                                 Tok.getLocation(),
                                                 Tok.getLength()));
    return;
  } else if (II == Ident__TIME__) {
    Diag(Tok.getLocation(), diag::warn_pp_date_time);
    if (!TIMELoc.isValid())
      ComputeDATE_TIME(DATELoc, TIMELoc, *this);
    Tok.setKind(tok::string_literal);
    Tok.setLength(strlen("\"hh:mm:ss\""));
    Tok.setLocation(SourceMgr.createExpansionLoc(TIMELoc, Tok.getLocation(),
                                                 Tok.getLocation(),
                                                 Tok.getLength()));
    return;
  } else if (II == Ident__INCLUDE_LEVEL__) {
    // Compute the presumed include depth of this token.  This can be affected
    // by GNU line markers.
    unsigned Depth = 0;

    PresumedLoc PLoc = SourceMgr.getPresumedLoc(Tok.getLocation());
    if (PLoc.isValid()) {
      PLoc = SourceMgr.getPresumedLoc(PLoc.getIncludeLoc());
      for (; PLoc.isValid(); ++Depth)
        PLoc = SourceMgr.getPresumedLoc(PLoc.getIncludeLoc());
    }

    // __INCLUDE_LEVEL__ expands to a simple numeric value.
    OS << Depth;
    Tok.setKind(tok::numeric_constant);
  } else if (II == Ident__TIMESTAMP__) {
    Diag(Tok.getLocation(), diag::warn_pp_date_time);
    // MSVC, ICC, GCC, VisualAge C++ extension.  The generated string should be
    // of the form "Ddd Mmm dd hh::mm::ss yyyy", which is returned by asctime.

    // Get the file that we are lexing out of.  If we're currently lexing from
    // a macro, dig into the include stack.
    const FileEntry *CurFile = nullptr;
    PreprocessorLexer *TheLexer = getCurrentFileLexer();

    if (TheLexer)
      CurFile = SourceMgr.getFileEntryForID(TheLexer->getFileID());

    const char *Result;
    if (CurFile) {
      time_t TT = CurFile->getModificationTime();
      struct tm *TM = localtime(&TT);
      Result = asctime(TM);
    } else {
      Result = "??? ??? ?? ??:??:?? ????\n";
    }
    // Surround the string with " and strip the trailing newline.
    OS << '"' << StringRef(Result).drop_back() << '"';
    Tok.setKind(tok::string_literal);
  } else if (II == Ident__COUNTER__) {
    // __COUNTER__ expands to a simple numeric value.
    OS << CounterValue++;
    Tok.setKind(tok::numeric_constant);
  } else if (II == Ident__has_feature) {
    EvaluateFeatureLikeBuiltinMacro(OS, Tok, II, *this,
      [this](Token &Tok, bool &HasLexedNextToken) -> int {
        IdentifierInfo *II = ExpectFeatureIdentifierInfo(Tok, *this,
                                           diag::err_feature_check_malformed);
        return II && HasFeature(*this, II->getName());
      });
  } else if (II == Ident__has_extension) {
    EvaluateFeatureLikeBuiltinMacro(OS, Tok, II, *this,
      [this](Token &Tok, bool &HasLexedNextToken) -> int {
        IdentifierInfo *II = ExpectFeatureIdentifierInfo(Tok, *this,
                                           diag::err_feature_check_malformed);
        return II && HasExtension(*this, II->getName());
      });
  } else if (II == Ident__has_builtin) {
    EvaluateFeatureLikeBuiltinMacro(OS, Tok, II, *this,
      [this](Token &Tok, bool &HasLexedNextToken) -> int {
        IdentifierInfo *II = ExpectFeatureIdentifierInfo(Tok, *this,
                                           diag::err_feature_check_malformed);
        const LangOptions &LangOpts = getLangOpts();
        if (!II)
          return false;
        else if (II->getBuiltinID() != 0) {
          switch (II->getBuiltinID()) {
          case Builtin::BI__builtin_operator_new:
          case Builtin::BI__builtin_operator_delete:
            // denotes date of behavior change to support calling arbitrary
            // usual allocation and deallocation functions. Required by libc++
            return 201802;
          default:
            return true;
          }
          return true;
        } else {
          return llvm::StringSwitch<bool>(II->getName())
                      .Case("__make_integer_seq", LangOpts.CPlusPlus)
                      .Case("__type_pack_element", LangOpts.CPlusPlus)
                      .Case("__builtin_available", true)
                      .Case("__is_target_arch", true)
                      .Case("__is_target_vendor", true)
                      .Case("__is_target_os", true)
                      .Case("__is_target_environment", true)
                      .Default(false);
        }
      });
  } else if (II == Ident__is_identifier) {
    EvaluateFeatureLikeBuiltinMacro(OS, Tok, II, *this,
      [](Token &Tok, bool &HasLexedNextToken) -> int {
        return Tok.is(tok::identifier);
      });
  } else if (II == Ident__has_attribute) {
    EvaluateFeatureLikeBuiltinMacro(OS, Tok, II, *this,
      [this](Token &Tok, bool &HasLexedNextToken) -> int {
        IdentifierInfo *II = ExpectFeatureIdentifierInfo(Tok, *this,
                                           diag::err_feature_check_malformed);
        return II ? hasAttribute(AttrSyntax::GNU, nullptr, II,
                                 getTargetInfo(), getLangOpts()) : 0;
      });
  } else if (II == Ident__has_declspec) {
    EvaluateFeatureLikeBuiltinMacro(OS, Tok, II, *this,
      [this](Token &Tok, bool &HasLexedNextToken) -> int {
        IdentifierInfo *II = ExpectFeatureIdentifierInfo(Tok, *this,
                                           diag::err_feature_check_malformed);
        return II ? hasAttribute(AttrSyntax::Declspec, nullptr, II,
                                 getTargetInfo(), getLangOpts()) : 0;
      });
  } else if (II == Ident__has_cpp_attribute ||
             II == Ident__has_c_attribute) {
    bool IsCXX = II == Ident__has_cpp_attribute;
    EvaluateFeatureLikeBuiltinMacro(
        OS, Tok, II, *this, [&](Token &Tok, bool &HasLexedNextToken) -> int {
          IdentifierInfo *ScopeII = nullptr;
          IdentifierInfo *II = ExpectFeatureIdentifierInfo(
              Tok, *this, diag::err_feature_check_malformed);
          if (!II)
            return false;

          // It is possible to receive a scope token.  Read the "::", if it is
          // available, and the subsequent identifier.
          LexUnexpandedToken(Tok);
          if (Tok.isNot(tok::coloncolon))
            HasLexedNextToken = true;
          else {
            ScopeII = II;
            LexUnexpandedToken(Tok);
            II = ExpectFeatureIdentifierInfo(Tok, *this,
                                             diag::err_feature_check_malformed);
          }

          AttrSyntax Syntax = IsCXX ? AttrSyntax::CXX : AttrSyntax::C;
          return II ? hasAttribute(Syntax, ScopeII, II, getTargetInfo(),
                                   getLangOpts())
                    : 0;
        });
  } else if (II == Ident__has_include ||
             II == Ident__has_include_next) {
    // The argument to these two builtins should be a parenthesized
    // file name string literal using angle brackets (<>) or
    // double-quotes ("").
    bool Value;
    if (II == Ident__has_include)
      Value = EvaluateHasInclude(Tok, II, *this);
    else
      Value = EvaluateHasIncludeNext(Tok, II, *this);

    if (Tok.isNot(tok::r_paren))
      return;
    OS << (int)Value;
    Tok.setKind(tok::numeric_constant);
  } else if (II == Ident__has_warning) {
    // The argument should be a parenthesized string literal.
    EvaluateFeatureLikeBuiltinMacro(OS, Tok, II, *this,
      [this](Token &Tok, bool &HasLexedNextToken) -> int {
        std::string WarningName;
        SourceLocation StrStartLoc = Tok.getLocation();

        HasLexedNextToken = Tok.is(tok::string_literal);
        if (!FinishLexStringLiteral(Tok, WarningName, "'__has_warning'",
                                    /*MacroExpansion=*/false))
          return false;

        // FIXME: Should we accept "-R..." flags here, or should that be
        // handled by a separate __has_remark?
        if (WarningName.size() < 3 || WarningName[0] != '-' ||
            WarningName[1] != 'W') {
          Diag(StrStartLoc, diag::warn_has_warning_invalid_option);
          return false;
        }

        // Finally, check if the warning flags maps to a diagnostic group.
        // We construct a SmallVector here to talk to getDiagnosticIDs().
        // Although we don't use the result, this isn't a hot path, and not
        // worth special casing.
        SmallVector<diag::kind, 10> Diags;
        return !getDiagnostics().getDiagnosticIDs()->
                getDiagnosticsInGroup(diag::Flavor::WarningOrError,
                                      WarningName.substr(2), Diags);
      });
  } else if (II == Ident__building_module) {
    // The argument to this builtin should be an identifier. The
    // builtin evaluates to 1 when that identifier names the module we are
    // currently building.
    EvaluateFeatureLikeBuiltinMacro(OS, Tok, II, *this,
      [this](Token &Tok, bool &HasLexedNextToken) -> int {
        IdentifierInfo *II = ExpectFeatureIdentifierInfo(Tok, *this,
                                       diag::err_expected_id_building_module);
        return getLangOpts().isCompilingModule() && II &&
               (II->getName() == getLangOpts().CurrentModule);
      });
  } else if (II == Ident__MODULE__) {
    // The current module as an identifier.
    OS << getLangOpts().CurrentModule;
    IdentifierInfo *ModuleII = getIdentifierInfo(getLangOpts().CurrentModule);
    Tok.setIdentifierInfo(ModuleII);
    Tok.setKind(ModuleII->getTokenID());
  } else if (II == Ident__identifier) {
    SourceLocation Loc = Tok.getLocation();

    // We're expecting '__identifier' '(' identifier ')'. Try to recover
    // if the parens are missing.
    LexNonComment(Tok);
    if (Tok.isNot(tok::l_paren)) {
      // No '(', use end of last token.
      Diag(getLocForEndOfToken(Loc), diag::err_pp_expected_after)
        << II << tok::l_paren;
      // If the next token isn't valid as our argument, we can't recover.
      if (!Tok.isAnnotation() && Tok.getIdentifierInfo())
        Tok.setKind(tok::identifier);
      return;
    }

    SourceLocation LParenLoc = Tok.getLocation();
    LexNonComment(Tok);

    if (!Tok.isAnnotation() && Tok.getIdentifierInfo())
      Tok.setKind(tok::identifier);
    else {
      Diag(Tok.getLocation(), diag::err_pp_identifier_arg_not_identifier)
        << Tok.getKind();
      // Don't walk past anything that's not a real token.
      if (Tok.isOneOf(tok::eof, tok::eod) || Tok.isAnnotation())
        return;
    }

    // Discard the ')', preserving 'Tok' as our result.
    Token RParen;
    LexNonComment(RParen);
    if (RParen.isNot(tok::r_paren)) {
      Diag(getLocForEndOfToken(Tok.getLocation()), diag::err_pp_expected_after)
        << Tok.getKind() << tok::r_paren;
      Diag(LParenLoc, diag::note_matching) << tok::l_paren;
    }
    return;
  } else if (II == Ident__is_target_arch) {
    EvaluateFeatureLikeBuiltinMacro(
        OS, Tok, II, *this, [this](Token &Tok, bool &HasLexedNextToken) -> int {
          IdentifierInfo *II = ExpectFeatureIdentifierInfo(
              Tok, *this, diag::err_feature_check_malformed);
          return II && isTargetArch(getTargetInfo(), II);
        });
  } else if (II == Ident__is_target_vendor) {
    EvaluateFeatureLikeBuiltinMacro(
        OS, Tok, II, *this, [this](Token &Tok, bool &HasLexedNextToken) -> int {
          IdentifierInfo *II = ExpectFeatureIdentifierInfo(
              Tok, *this, diag::err_feature_check_malformed);
          return II && isTargetVendor(getTargetInfo(), II);
        });
  } else if (II == Ident__is_target_os) {
    EvaluateFeatureLikeBuiltinMacro(
        OS, Tok, II, *this, [this](Token &Tok, bool &HasLexedNextToken) -> int {
          IdentifierInfo *II = ExpectFeatureIdentifierInfo(
              Tok, *this, diag::err_feature_check_malformed);
          return II && isTargetOS(getTargetInfo(), II);
        });
  } else if (II == Ident__is_target_environment) {
    EvaluateFeatureLikeBuiltinMacro(
        OS, Tok, II, *this, [this](Token &Tok, bool &HasLexedNextToken) -> int {
          IdentifierInfo *II = ExpectFeatureIdentifierInfo(
              Tok, *this, diag::err_feature_check_malformed);
          return II && isTargetEnvironment(getTargetInfo(), II);
        });
  } else {
    llvm_unreachable("Unknown identifier!");
  }
  CreateString(OS.str(), Tok, Tok.getLocation(), Tok.getLocation());
}

void Preprocessor::markMacroAsUsed(MacroInfo *MI) {
  // If the 'used' status changed, and the macro requires 'unused' warning,
  // remove its SourceLocation from the warn-for-unused-macro locations.
  if (MI->isWarnIfUnused() && !MI->isUsed())
    WarnUnusedMacroLocs.erase(MI->getDefinitionLoc());
  MI->setIsUsed(true);
}
