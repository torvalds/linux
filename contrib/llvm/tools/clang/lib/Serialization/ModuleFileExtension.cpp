//===-- ModuleFileExtension.cpp - Module File Extensions ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "clang/Serialization/ModuleFileExtension.h"
#include "llvm/ADT/Hashing.h"
using namespace clang;

ModuleFileExtension::~ModuleFileExtension() { }

llvm::hash_code ModuleFileExtension::hashExtension(llvm::hash_code Code) const {
  return Code;
}

ModuleFileExtensionWriter::~ModuleFileExtensionWriter() { }

ModuleFileExtensionReader::~ModuleFileExtensionReader() { }
