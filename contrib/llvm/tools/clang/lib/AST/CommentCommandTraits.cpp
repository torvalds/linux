//===--- CommentCommandTraits.cpp - Comment command properties --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/CommentCommandTraits.h"
#include "llvm/ADT/STLExtras.h"

namespace clang {
namespace comments {

#include "clang/AST/CommentCommandInfo.inc"

CommandTraits::CommandTraits(llvm::BumpPtrAllocator &Allocator,
                             const CommentOptions &CommentOptions) :
    NextID(llvm::array_lengthof(Commands)), Allocator(Allocator) {
  registerCommentOptions(CommentOptions);
}

void CommandTraits::registerCommentOptions(
    const CommentOptions &CommentOptions) {
  for (CommentOptions::BlockCommandNamesTy::const_iterator
           I = CommentOptions.BlockCommandNames.begin(),
           E = CommentOptions.BlockCommandNames.end();
       I != E; I++) {
    registerBlockCommand(*I);
  }
}

const CommandInfo *CommandTraits::getCommandInfoOrNULL(StringRef Name) const {
  if (const CommandInfo *Info = getBuiltinCommandInfo(Name))
    return Info;
  return getRegisteredCommandInfo(Name);
}

const CommandInfo *CommandTraits::getCommandInfo(unsigned CommandID) const {
  if (const CommandInfo *Info = getBuiltinCommandInfo(CommandID))
    return Info;
  return getRegisteredCommandInfo(CommandID);
}

const CommandInfo *
CommandTraits::getTypoCorrectCommandInfo(StringRef Typo) const {
  // Single-character command impostures, such as \t or \n, should not go
  // through the fixit logic.
  if (Typo.size() <= 1)
    return nullptr;

  // The maximum edit distance we're prepared to accept.
  const unsigned MaxEditDistance = 1;

  unsigned BestEditDistance = MaxEditDistance;
  SmallVector<const CommandInfo *, 2> BestCommand;

  auto ConsiderCorrection = [&](const CommandInfo *Command) {
    StringRef Name = Command->Name;

    unsigned MinPossibleEditDistance = abs((int)Name.size() - (int)Typo.size());
    if (MinPossibleEditDistance <= BestEditDistance) {
      unsigned EditDistance = Typo.edit_distance(Name, true, BestEditDistance);
      if (EditDistance < BestEditDistance) {
        BestEditDistance = EditDistance;
        BestCommand.clear();
      }
      if (EditDistance == BestEditDistance)
        BestCommand.push_back(Command);
    }
  };

  for (const auto &Command : Commands)
    ConsiderCorrection(&Command);

  for (const auto *Command : RegisteredCommands)
    if (!Command->IsUnknownCommand)
      ConsiderCorrection(Command);

  return BestCommand.size() == 1 ? BestCommand[0] : nullptr;
}

CommandInfo *CommandTraits::createCommandInfoWithName(StringRef CommandName) {
  char *Name = Allocator.Allocate<char>(CommandName.size() + 1);
  memcpy(Name, CommandName.data(), CommandName.size());
  Name[CommandName.size()] = '\0';

  // Value-initialize (=zero-initialize in this case) a new CommandInfo.
  CommandInfo *Info = new (Allocator) CommandInfo();
  Info->Name = Name;
  // We only have a limited number of bits to encode command IDs in the
  // CommandInfo structure, so the ID numbers can potentially wrap around.
  assert((NextID < (1 << CommandInfo::NumCommandIDBits))
         && "Too many commands. We have limited bits for the command ID.");
  Info->ID = NextID++;

  RegisteredCommands.push_back(Info);

  return Info;
}

const CommandInfo *CommandTraits::registerUnknownCommand(
                                                  StringRef CommandName) {
  CommandInfo *Info = createCommandInfoWithName(CommandName);
  Info->IsUnknownCommand = true;
  return Info;
}

const CommandInfo *CommandTraits::registerBlockCommand(StringRef CommandName) {
  CommandInfo *Info = createCommandInfoWithName(CommandName);
  Info->IsBlockCommand = true;
  return Info;
}

const CommandInfo *CommandTraits::getBuiltinCommandInfo(
                                                  unsigned CommandID) {
  if (CommandID < llvm::array_lengthof(Commands))
    return &Commands[CommandID];
  return nullptr;
}

const CommandInfo *CommandTraits::getRegisteredCommandInfo(
                                                  StringRef Name) const {
  for (unsigned i = 0, e = RegisteredCommands.size(); i != e; ++i) {
    if (RegisteredCommands[i]->Name == Name)
      return RegisteredCommands[i];
  }
  return nullptr;
}

const CommandInfo *CommandTraits::getRegisteredCommandInfo(
                                                  unsigned CommandID) const {
  return RegisteredCommands[CommandID - llvm::array_lengthof(Commands)];
}

} // end namespace comments
} // end namespace clang

