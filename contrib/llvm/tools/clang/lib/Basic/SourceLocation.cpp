//===- SourceLocation.cpp - Compact identifier for Source Files -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines accessor methods for the FullSourceLoc class.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/PrettyStackTrace.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <string>
#include <utility>

using namespace clang;

//===----------------------------------------------------------------------===//
// PrettyStackTraceLoc
//===----------------------------------------------------------------------===//

void PrettyStackTraceLoc::print(raw_ostream &OS) const {
  if (Loc.isValid()) {
    Loc.print(OS, SM);
    OS << ": ";
  }
  OS << Message << '\n';
}

//===----------------------------------------------------------------------===//
// SourceLocation
//===----------------------------------------------------------------------===//

void SourceLocation::print(raw_ostream &OS, const SourceManager &SM)const{
  if (!isValid()) {
    OS << "<invalid loc>";
    return;
  }

  if (isFileID()) {
    PresumedLoc PLoc = SM.getPresumedLoc(*this);

    if (PLoc.isInvalid()) {
      OS << "<invalid>";
      return;
    }
    // The macro expansion and spelling pos is identical for file locs.
    OS << PLoc.getFilename() << ':' << PLoc.getLine()
       << ':' << PLoc.getColumn();
    return;
  }

  SM.getExpansionLoc(*this).print(OS, SM);

  OS << " <Spelling=";
  SM.getSpellingLoc(*this).print(OS, SM);
  OS << '>';
}

LLVM_DUMP_METHOD std::string
SourceLocation::printToString(const SourceManager &SM) const {
  std::string S;
  llvm::raw_string_ostream OS(S);
  print(OS, SM);
  return OS.str();
}

LLVM_DUMP_METHOD void SourceLocation::dump(const SourceManager &SM) const {
  print(llvm::errs(), SM);
  llvm::errs() << '\n';
}

LLVM_DUMP_METHOD void SourceRange::dump(const SourceManager &SM) const {
  print(llvm::errs(), SM);
  llvm::errs() << '\n';
}

static PresumedLoc PrintDifference(raw_ostream &OS, const SourceManager &SM,
                                   SourceLocation Loc, PresumedLoc Previous) {
  if (Loc.isFileID()) {

    PresumedLoc PLoc = SM.getPresumedLoc(Loc);

    if (PLoc.isInvalid()) {
      OS << "<invalid sloc>";
      return Previous;
    }

    if (Previous.isInvalid() ||
        strcmp(PLoc.getFilename(), Previous.getFilename()) != 0) {
      OS << PLoc.getFilename() << ':' << PLoc.getLine() << ':'
         << PLoc.getColumn();
    } else if (Previous.isInvalid() || PLoc.getLine() != Previous.getLine()) {
      OS << "line" << ':' << PLoc.getLine() << ':' << PLoc.getColumn();
    } else {
      OS << "col" << ':' << PLoc.getColumn();
    }
    return PLoc;
  }
  auto PrintedLoc = PrintDifference(OS, SM, SM.getExpansionLoc(Loc), Previous);

  OS << " <Spelling=";
  PrintedLoc = PrintDifference(OS, SM, SM.getSpellingLoc(Loc), PrintedLoc);
  OS << '>';
  return PrintedLoc;
}

void SourceRange::print(raw_ostream &OS, const SourceManager &SM) const {

  OS << '<';
  auto PrintedLoc = PrintDifference(OS, SM, B, {});
  if (B != E) {
    OS << ", ";
    PrintDifference(OS, SM, E, PrintedLoc);
  }
  OS << '>';
}

LLVM_DUMP_METHOD std::string
SourceRange::printToString(const SourceManager &SM) const {
  std::string S;
  llvm::raw_string_ostream OS(S);
  print(OS, SM);
  return OS.str();
}

//===----------------------------------------------------------------------===//
// FullSourceLoc
//===----------------------------------------------------------------------===//

FileID FullSourceLoc::getFileID() const {
  assert(isValid());
  return SrcMgr->getFileID(*this);
}

FullSourceLoc FullSourceLoc::getExpansionLoc() const {
  assert(isValid());
  return FullSourceLoc(SrcMgr->getExpansionLoc(*this), *SrcMgr);
}

FullSourceLoc FullSourceLoc::getSpellingLoc() const {
  assert(isValid());
  return FullSourceLoc(SrcMgr->getSpellingLoc(*this), *SrcMgr);
}

FullSourceLoc FullSourceLoc::getFileLoc() const {
  assert(isValid());
  return FullSourceLoc(SrcMgr->getFileLoc(*this), *SrcMgr);
}

PresumedLoc FullSourceLoc::getPresumedLoc(bool UseLineDirectives) const {
  if (!isValid())
    return PresumedLoc();

  return SrcMgr->getPresumedLoc(*this, UseLineDirectives);
}

bool FullSourceLoc::isMacroArgExpansion(FullSourceLoc *StartLoc) const {
  assert(isValid());
  return SrcMgr->isMacroArgExpansion(*this, StartLoc);
}

FullSourceLoc FullSourceLoc::getImmediateMacroCallerLoc() const {
  assert(isValid());
  return FullSourceLoc(SrcMgr->getImmediateMacroCallerLoc(*this), *SrcMgr);
}

std::pair<FullSourceLoc, StringRef> FullSourceLoc::getModuleImportLoc() const {
  if (!isValid())
    return std::make_pair(FullSourceLoc(), StringRef());

  std::pair<SourceLocation, StringRef> ImportLoc =
      SrcMgr->getModuleImportLoc(*this);
  return std::make_pair(FullSourceLoc(ImportLoc.first, *SrcMgr),
                        ImportLoc.second);
}

unsigned FullSourceLoc::getFileOffset() const {
  assert(isValid());
  return SrcMgr->getFileOffset(*this);
}

unsigned FullSourceLoc::getLineNumber(bool *Invalid) const {
  assert(isValid());
  return SrcMgr->getLineNumber(getFileID(), getFileOffset(), Invalid);
}

unsigned FullSourceLoc::getColumnNumber(bool *Invalid) const {
  assert(isValid());
  return SrcMgr->getColumnNumber(getFileID(), getFileOffset(), Invalid);
}

const FileEntry *FullSourceLoc::getFileEntry() const {
  assert(isValid());
  return SrcMgr->getFileEntryForID(getFileID());
}

unsigned FullSourceLoc::getExpansionLineNumber(bool *Invalid) const {
  assert(isValid());
  return SrcMgr->getExpansionLineNumber(*this, Invalid);
}

unsigned FullSourceLoc::getExpansionColumnNumber(bool *Invalid) const {
  assert(isValid());
  return SrcMgr->getExpansionColumnNumber(*this, Invalid);
}

unsigned FullSourceLoc::getSpellingLineNumber(bool *Invalid) const {
  assert(isValid());
  return SrcMgr->getSpellingLineNumber(*this, Invalid);
}

unsigned FullSourceLoc::getSpellingColumnNumber(bool *Invalid) const {
  assert(isValid());
  return SrcMgr->getSpellingColumnNumber(*this, Invalid);
}

bool FullSourceLoc::isInSystemHeader() const {
  assert(isValid());
  return SrcMgr->isInSystemHeader(*this);
}

bool FullSourceLoc::isBeforeInTranslationUnitThan(SourceLocation Loc) const {
  assert(isValid());
  return SrcMgr->isBeforeInTranslationUnit(*this, Loc);
}

LLVM_DUMP_METHOD void FullSourceLoc::dump() const {
  SourceLocation::dump(*SrcMgr);
}

const char *FullSourceLoc::getCharacterData(bool *Invalid) const {
  assert(isValid());
  return SrcMgr->getCharacterData(*this, Invalid);
}

StringRef FullSourceLoc::getBufferData(bool *Invalid) const {
  assert(isValid());
  return SrcMgr->getBuffer(SrcMgr->getFileID(*this), Invalid)->getBuffer();
}

std::pair<FileID, unsigned> FullSourceLoc::getDecomposedLoc() const {
  return SrcMgr->getDecomposedLoc(*this);
}
