//===- Core/File.cpp - A Container of Atoms -------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lld/Core/File.h"
#include <mutex>

namespace lld {

File::~File() = default;

File::AtomVector<DefinedAtom> File::_noDefinedAtoms;
File::AtomVector<UndefinedAtom> File::_noUndefinedAtoms;
File::AtomVector<SharedLibraryAtom> File::_noSharedLibraryAtoms;
File::AtomVector<AbsoluteAtom> File::_noAbsoluteAtoms;

std::error_code File::parse() {
  std::lock_guard<std::mutex> lock(_parseMutex);
  if (!_lastError.hasValue())
    _lastError = doParse();
  return _lastError.getValue();
}

} // end namespace lld
