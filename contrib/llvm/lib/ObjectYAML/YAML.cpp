//===- YAML.cpp - YAMLIO utilities for object files -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines utility classes for handling the YAML representation of
// object files.
//
//===----------------------------------------------------------------------===//

#include "llvm/ObjectYAML/YAML.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <cctype>
#include <cstdint>

using namespace llvm;

void yaml::ScalarTraits<yaml::BinaryRef>::output(
    const yaml::BinaryRef &Val, void *, raw_ostream &Out) {
  Val.writeAsHex(Out);
}

StringRef yaml::ScalarTraits<yaml::BinaryRef>::input(StringRef Scalar, void *,
                                                     yaml::BinaryRef &Val) {
  if (Scalar.size() % 2 != 0)
    return "BinaryRef hex string must contain an even number of nybbles.";
  // TODO: Can we improve YAMLIO to permit a more accurate diagnostic here?
  // (e.g. a caret pointing to the offending character).
  for (unsigned I = 0, N = Scalar.size(); I != N; ++I)
    if (!isxdigit(Scalar[I]))
      return "BinaryRef hex string must contain only hex digits.";
  Val = yaml::BinaryRef(Scalar);
  return {};
}

void yaml::BinaryRef::writeAsBinary(raw_ostream &OS) const {
  if (!DataIsHexString) {
    OS.write((const char *)Data.data(), Data.size());
    return;
  }
  for (unsigned I = 0, N = Data.size(); I != N; I += 2) {
    uint8_t Byte;
    StringRef((const char *)&Data[I],  2).getAsInteger(16, Byte);
    OS.write(Byte);
  }
}

void yaml::BinaryRef::writeAsHex(raw_ostream &OS) const {
  if (binary_size() == 0)
    return;
  if (DataIsHexString) {
    OS.write((const char *)Data.data(), Data.size());
    return;
  }
  for (uint8_t Byte : Data)
    OS << hexdigit(Byte >> 4) << hexdigit(Byte & 0xf);
}
