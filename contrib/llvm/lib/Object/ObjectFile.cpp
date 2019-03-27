//===- ObjectFile.cpp - File format independent object file ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a file format independent ObjectFile class.
//
//===----------------------------------------------------------------------===//

#include "llvm/Object/ObjectFile.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/Error.h"
#include "llvm/Object/MachO.h"
#include "llvm/Object/Wasm.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <system_error>

using namespace llvm;
using namespace object;

void ObjectFile::anchor() {}

ObjectFile::ObjectFile(unsigned int Type, MemoryBufferRef Source)
    : SymbolicFile(Type, Source) {}

bool SectionRef::containsSymbol(SymbolRef S) const {
  Expected<section_iterator> SymSec = S.getSection();
  if (!SymSec) {
    // TODO: Actually report errors helpfully.
    consumeError(SymSec.takeError());
    return false;
  }
  return *this == **SymSec;
}

uint64_t ObjectFile::getSymbolValue(DataRefImpl Ref) const {
  uint32_t Flags = getSymbolFlags(Ref);
  if (Flags & SymbolRef::SF_Undefined)
    return 0;
  if (Flags & SymbolRef::SF_Common)
    return getCommonSymbolSize(Ref);
  return getSymbolValueImpl(Ref);
}

std::error_code ObjectFile::printSymbolName(raw_ostream &OS,
                                            DataRefImpl Symb) const {
  Expected<StringRef> Name = getSymbolName(Symb);
  if (!Name)
    return errorToErrorCode(Name.takeError());
  OS << *Name;
  return std::error_code();
}

uint32_t ObjectFile::getSymbolAlignment(DataRefImpl DRI) const { return 0; }

bool ObjectFile::isSectionBitcode(DataRefImpl Sec) const {
  StringRef SectName;
  if (!getSectionName(Sec, SectName))
    return SectName == ".llvmbc";
  return false;
}

bool ObjectFile::isSectionStripped(DataRefImpl Sec) const { return false; }

bool ObjectFile::isBerkeleyText(DataRefImpl Sec) const {
  return isSectionText(Sec);
}

bool ObjectFile::isBerkeleyData(DataRefImpl Sec) const {
  return isSectionData(Sec);
}

section_iterator ObjectFile::getRelocatedSection(DataRefImpl Sec) const {
  return section_iterator(SectionRef(Sec, this));
}

Triple ObjectFile::makeTriple() const {
  Triple TheTriple;
  auto Arch = getArch();
  TheTriple.setArch(Triple::ArchType(Arch));

  // For ARM targets, try to use the build attributes to build determine
  // the build target. Target features are also added, but later during
  // disassembly.
  if (Arch == Triple::arm || Arch == Triple::armeb)
    setARMSubArch(TheTriple);

  // TheTriple defaults to ELF, and COFF doesn't have an environment:
  // the best we can do here is indicate that it is mach-o.
  if (isMachO())
    TheTriple.setObjectFormat(Triple::MachO);

  if (isCOFF()) {
    const auto COFFObj = dyn_cast<COFFObjectFile>(this);
    if (COFFObj->getArch() == Triple::thumb)
      TheTriple.setTriple("thumbv7-windows");
  }

  return TheTriple;
}

Expected<std::unique_ptr<ObjectFile>>
ObjectFile::createObjectFile(MemoryBufferRef Object, file_magic Type) {
  StringRef Data = Object.getBuffer();
  if (Type == file_magic::unknown)
    Type = identify_magic(Data);

  switch (Type) {
  case file_magic::unknown:
  case file_magic::bitcode:
  case file_magic::coff_cl_gl_object:
  case file_magic::archive:
  case file_magic::macho_universal_binary:
  case file_magic::windows_resource:
  case file_magic::pdb:
    return errorCodeToError(object_error::invalid_file_type);
  case file_magic::elf:
  case file_magic::elf_relocatable:
  case file_magic::elf_executable:
  case file_magic::elf_shared_object:
  case file_magic::elf_core:
    return createELFObjectFile(Object);
  case file_magic::macho_object:
  case file_magic::macho_executable:
  case file_magic::macho_fixed_virtual_memory_shared_lib:
  case file_magic::macho_core:
  case file_magic::macho_preload_executable:
  case file_magic::macho_dynamically_linked_shared_lib:
  case file_magic::macho_dynamic_linker:
  case file_magic::macho_bundle:
  case file_magic::macho_dynamically_linked_shared_lib_stub:
  case file_magic::macho_dsym_companion:
  case file_magic::macho_kext_bundle:
    return createMachOObjectFile(Object);
  case file_magic::coff_object:
  case file_magic::coff_import_library:
  case file_magic::pecoff_executable:
    return createCOFFObjectFile(Object);
  case file_magic::wasm_object:
    return createWasmObjectFile(Object);
  }
  llvm_unreachable("Unexpected Object File Type");
}

Expected<OwningBinary<ObjectFile>>
ObjectFile::createObjectFile(StringRef ObjectPath) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> FileOrErr =
      MemoryBuffer::getFile(ObjectPath);
  if (std::error_code EC = FileOrErr.getError())
    return errorCodeToError(EC);
  std::unique_ptr<MemoryBuffer> Buffer = std::move(FileOrErr.get());

  Expected<std::unique_ptr<ObjectFile>> ObjOrErr =
      createObjectFile(Buffer->getMemBufferRef());
  if (Error Err = ObjOrErr.takeError())
    return std::move(Err);
  std::unique_ptr<ObjectFile> Obj = std::move(ObjOrErr.get());

  return OwningBinary<ObjectFile>(std::move(Obj), std::move(Buffer));
}
