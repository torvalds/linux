//===-- ObjDumper.cpp - Base dumper class -----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements ObjDumper.
///
//===----------------------------------------------------------------------===//

#include "ObjDumper.h"
#include "Error.h"
#include "llvm-readobj.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

ObjDumper::ObjDumper(ScopedPrinter &Writer) : W(Writer) {}

ObjDumper::~ObjDumper() {
}

static void printAsPrintable(raw_ostream &W, const uint8_t *Start, size_t Len) {
  for (size_t i = 0; i < Len; i++)
    W << (isPrint(Start[i]) ? static_cast<char>(Start[i]) : '.');
}

static Expected<object::SectionRef>
getSecNameOrIndexAsSecRef(const object::ObjectFile *Obj, StringRef SecName) {
  char *StrPtr;
  long SectionIndex = strtol(SecName.data(), &StrPtr, 10);
  object::SectionRef Section;
  long SecIndex;
  if (Obj->isELF())
    SecIndex = 0;
  else
    SecIndex = 1;
  for (object::SectionRef SecRef : Obj->sections()) {
    if (*StrPtr) {
      StringRef SectionName;

      if (std::error_code E = SecRef.getName(SectionName))
        return errorCodeToError(E);

      if (SectionName == SecName)
        return SecRef;
    } else if (SecIndex == SectionIndex)
      return SecRef;

    SecIndex++;
  }
  return make_error<StringError>("invalid section reference",
                                 object::object_error::parse_failed);
}

void ObjDumper::printSectionAsString(const object::ObjectFile *Obj,
                                     StringRef SecName) {
  Expected<object::SectionRef> SectionRefOrError =
      getSecNameOrIndexAsSecRef(Obj, SecName);
  if (!SectionRefOrError)
    error(std::move(SectionRefOrError));
  object::SectionRef Section = *SectionRefOrError;
  StringRef SectionName;

  if (std::error_code E = Section.getName(SectionName))
    error(E);
  W.startLine() << "String dump of section '" << SectionName << "':\n";

  StringRef SectionContent;
  Section.getContents(SectionContent);

  const uint8_t *SecContent = SectionContent.bytes_begin();
  const uint8_t *CurrentWord = SecContent;
  const uint8_t *SecEnd = SectionContent.bytes_end();

  while (CurrentWord <= SecEnd) {
    size_t WordSize = strnlen(reinterpret_cast<const char *>(CurrentWord),
                              SecEnd - CurrentWord);
    if (!WordSize) {
      CurrentWord++;
      continue;
    }
    W.startLine() << format("[%6tx] ", CurrentWord - SecContent);
    printAsPrintable(W.startLine(), CurrentWord, WordSize);
    W.startLine() << '\n';
    CurrentWord += WordSize + 1;
  }
}

void ObjDumper::printSectionAsHex(const object::ObjectFile *Obj,
                                  StringRef SecName) {
  Expected<object::SectionRef> SectionRefOrError =
      getSecNameOrIndexAsSecRef(Obj, SecName);
  if (!SectionRefOrError)
    error(std::move(SectionRefOrError));
  object::SectionRef Section = *SectionRefOrError;
  StringRef SectionName;

  if (std::error_code E = Section.getName(SectionName))
    error(E);
  W.startLine() << "Hex dump of section '" << SectionName << "':\n";

  StringRef SectionContent;
  Section.getContents(SectionContent);
  const uint8_t *SecContent = SectionContent.bytes_begin();
  const uint8_t *SecEnd = SecContent + SectionContent.size();

  for (const uint8_t *SecPtr = SecContent; SecPtr < SecEnd; SecPtr += 16) {
    const uint8_t *TmpSecPtr = SecPtr;
    uint8_t i;
    uint8_t k;

    W.startLine() << format_hex(SecPtr - SecContent, 10);
    W.startLine() << ' ';
    for (i = 0; TmpSecPtr < SecEnd && i < 4; ++i) {
      for (k = 0; TmpSecPtr < SecEnd && k < 4; k++, TmpSecPtr++) {
        uint8_t Val = *(reinterpret_cast<const uint8_t *>(TmpSecPtr));
        W.startLine() << format_hex_no_prefix(Val, 2);
      }
      W.startLine() << ' ';
    }

    // We need to print the correct amount of spaces to match the format.
    // We are adding the (4 - i) last rows that are 8 characters each.
    // Then, the (4 - i) spaces that are in between the rows.
    // Least, if we cut in a middle of a row, we add the remaining characters,
    // which is (8 - (k * 2))
    if (i < 4)
      W.startLine() << format("%*c", (4 - i) * 8 + (4 - i) + (8 - (k * 2)),
                              ' ');

    TmpSecPtr = SecPtr;
    for (i = 0; TmpSecPtr + i < SecEnd && i < 16; ++i)
      W.startLine() << (isPrint(TmpSecPtr[i]) ? static_cast<char>(TmpSecPtr[i])
                                              : '.');

    W.startLine() << '\n';
  }
}

} // namespace llvm
