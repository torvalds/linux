//===- SymbolRemappingReader.cpp - Read symbol remapping file -------------===//
//
//                      The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains definitions needed for reading and applying symbol
// remapping files.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/SymbolRemappingReader.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/LineIterator.h"

using namespace llvm;

char SymbolRemappingParseError::ID;

/// Load a set of name remappings from a text file.
///
/// See the documentation at the top of the file for an explanation of
/// the expected format.
Error SymbolRemappingReader::read(MemoryBuffer &B) {
  line_iterator LineIt(B, /*SkipBlanks=*/true, '#');

  auto ReportError = [&](Twine Msg) {
    return llvm::make_error<SymbolRemappingParseError>(
        B.getBufferIdentifier(), LineIt.line_number(), Msg);
  };

  for (; !LineIt.is_at_eof(); ++LineIt) {
    StringRef Line = *LineIt;
    Line = Line.ltrim(' ');
    // line_iterator only detects comments starting in column 1.
    if (Line.startswith("#") || Line.empty())
      continue;

    SmallVector<StringRef, 4> Parts;
    Line.split(Parts, ' ', /*MaxSplits*/-1, /*KeepEmpty*/false);

    if (Parts.size() != 3)
      return ReportError("Expected 'kind mangled_name mangled_name', "
                         "found '" + Line + "'");

    using FK = ItaniumManglingCanonicalizer::FragmentKind;
    Optional<FK> FragmentKind = StringSwitch<Optional<FK>>(Parts[0])
                                    .Case("name", FK::Name)
                                    .Case("type", FK::Type)
                                    .Case("encoding", FK::Encoding)
                                    .Default(None);
    if (!FragmentKind)
      return ReportError("Invalid kind, expected 'name', 'type', or 'encoding',"
                         " found '" + Parts[0] + "'");

    using EE = ItaniumManglingCanonicalizer::EquivalenceError;
    switch (Canonicalizer.addEquivalence(*FragmentKind, Parts[1], Parts[2])) {
    case EE::Success:
      break;

    case EE::ManglingAlreadyUsed:
      return ReportError("Manglings '" + Parts[1] + "' and '" + Parts[2] + "' "
                         "have both been used in prior remappings. Move this "
                         "remapping earlier in the file.");

    case EE::InvalidFirstMangling:
      return ReportError("Could not demangle '" + Parts[1] + "' "
                         "as a <" + Parts[0] + ">; invalid mangling?");

    case EE::InvalidSecondMangling:
      return ReportError("Could not demangle '" + Parts[2] + "' "
                         "as a <" + Parts[0] + ">; invalid mangling?");
    }
  }

  return Error::success();
}
