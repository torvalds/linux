//===--- CommentCommandTraits.h - Comment command properties ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the class that provides information about comment
//  commands.
//
//===----------------------------------------------------------------------===//


#ifndef LLVM_CLANG_AST_COMMENTCOMMANDTRAITS_H
#define LLVM_CLANG_AST_COMMENTCOMMANDTRAITS_H

#include "clang/Basic/CommentOptions.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/ErrorHandling.h"

namespace clang {
namespace comments {

/// Information about a single command.
///
/// When reordering, adding or removing members please update the corresponding
/// TableGen backend.
struct CommandInfo {
  unsigned getID() const {
    return ID;
  }

  const char *Name;

  /// Name of the command that ends the verbatim block.
  const char *EndCommandName;

  /// DRY definition of the number of bits used for a command ID.
  enum { NumCommandIDBits = 20 };

  /// The ID of the command.
  unsigned ID : NumCommandIDBits;

  /// Number of word-like arguments for a given block command, except for
  /// \\param and \\tparam commands -- these have special argument parsers.
  unsigned NumArgs : 4;

  /// True if this command is a inline command (of any kind).
  unsigned IsInlineCommand : 1;

  /// True if this command is a block command (of any kind).
  unsigned IsBlockCommand : 1;

  /// True if this command is introducing a brief documentation
  /// paragraph (\or an alias).
  unsigned IsBriefCommand : 1;

  /// True if this command is \\returns or an alias.
  unsigned IsReturnsCommand : 1;

  /// True if this command is introducing documentation for a function
  /// parameter (\\param or an alias).
  unsigned IsParamCommand : 1;

  /// True if this command is introducing documentation for
  /// a template parameter (\\tparam or an alias).
  unsigned IsTParamCommand : 1;

  /// True if this command is \\throws or an alias.
  unsigned IsThrowsCommand : 1;

  /// True if this command is \\deprecated or an alias.
  unsigned IsDeprecatedCommand : 1;

  /// True if this is a \\headerfile-like command.
  unsigned IsHeaderfileCommand : 1;

  /// True if we don't want to warn about this command being passed an empty
  /// paragraph.  Meaningful only for block commands.
  unsigned IsEmptyParagraphAllowed : 1;

  /// True if this command is a verbatim-like block command.
  ///
  /// A verbatim-like block command eats every character (except line starting
  /// decorations) until matching end command is seen or comment end is hit.
  unsigned IsVerbatimBlockCommand : 1;

  /// True if this command is an end command for a verbatim-like block.
  unsigned IsVerbatimBlockEndCommand : 1;

  /// True if this command is a verbatim line command.
  ///
  /// A verbatim-like line command eats everything until a newline is seen or
  /// comment end is hit.
  unsigned IsVerbatimLineCommand : 1;

  /// True if this command contains a declaration for the entity being
  /// documented.
  ///
  /// For example:
  /// \code
  ///   \fn void f(int a);
  /// \endcode
  unsigned IsDeclarationCommand : 1;

  /// True if verbatim-like line command is a function declaration.
  unsigned IsFunctionDeclarationCommand : 1;

  /// True if block command is further describing a container API; such
  /// as \@coclass, \@classdesign, etc.
  unsigned IsRecordLikeDetailCommand : 1;

  /// True if block command is a container API; such as \@interface.
  unsigned IsRecordLikeDeclarationCommand : 1;

  /// True if this command is unknown.  This \c CommandInfo object was
  /// created during parsing.
  unsigned IsUnknownCommand : 1;
};

/// This class provides information about commands that can be used
/// in comments.
class CommandTraits {
public:
  enum KnownCommandIDs {
#define COMMENT_COMMAND(NAME) KCI_##NAME,
#include "clang/AST/CommentCommandList.inc"
#undef COMMENT_COMMAND
    KCI_Last
  };

  CommandTraits(llvm::BumpPtrAllocator &Allocator,
                const CommentOptions &CommentOptions);

  void registerCommentOptions(const CommentOptions &CommentOptions);

  /// \returns a CommandInfo object for a given command name or
  /// NULL if no CommandInfo object exists for this command.
  const CommandInfo *getCommandInfoOrNULL(StringRef Name) const;

  const CommandInfo *getCommandInfo(StringRef Name) const {
    if (const CommandInfo *Info = getCommandInfoOrNULL(Name))
      return Info;
    llvm_unreachable("the command should be known");
  }

  const CommandInfo *getTypoCorrectCommandInfo(StringRef Typo) const;

  const CommandInfo *getCommandInfo(unsigned CommandID) const;

  const CommandInfo *registerUnknownCommand(StringRef CommandName);

  const CommandInfo *registerBlockCommand(StringRef CommandName);

  /// \returns a CommandInfo object for a given command name or
  /// NULL if \c Name is not a builtin command.
  static const CommandInfo *getBuiltinCommandInfo(StringRef Name);

  /// \returns a CommandInfo object for a given command ID or
  /// NULL if \c CommandID is not a builtin command.
  static const CommandInfo *getBuiltinCommandInfo(unsigned CommandID);

private:
  CommandTraits(const CommandTraits &) = delete;
  void operator=(const CommandTraits &) = delete;

  const CommandInfo *getRegisteredCommandInfo(StringRef Name) const;
  const CommandInfo *getRegisteredCommandInfo(unsigned CommandID) const;

  CommandInfo *createCommandInfoWithName(StringRef CommandName);

  unsigned NextID;

  /// Allocator for CommandInfo objects.
  llvm::BumpPtrAllocator &Allocator;

  SmallVector<CommandInfo *, 4> RegisteredCommands;
};

} // end namespace comments
} // end namespace clang

#endif

