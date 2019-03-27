//===- EhFrame.cpp -------------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// .eh_frame section contains information on how to unwind the stack when
// an exception is thrown. The section consists of sequence of CIE and FDE
// records. The linker needs to merge CIEs and associate FDEs to CIEs.
// That means the linker has to understand the format of the section.
//
// This file contains a few utility functions to read .eh_frame contents.
//
//===----------------------------------------------------------------------===//

#include "EhFrame.h"
#include "Config.h"
#include "InputSection.h"
#include "Relocations.h"
#include "Target.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Strings.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/Object/ELF.h"

using namespace llvm;
using namespace llvm::ELF;
using namespace llvm::dwarf;
using namespace llvm::object;

using namespace lld;
using namespace lld::elf;

namespace {
class EhReader {
public:
  EhReader(InputSectionBase *S, ArrayRef<uint8_t> D) : IS(S), D(D) {}
  size_t readEhRecordSize();
  uint8_t getFdeEncoding();

private:
  template <class P> void failOn(const P *Loc, const Twine &Msg) {
    fatal("corrupted .eh_frame: " + Msg + "\n>>> defined in " +
          IS->getObjMsg((const uint8_t *)Loc - IS->data().data()));
  }

  uint8_t readByte();
  void skipBytes(size_t Count);
  StringRef readString();
  void skipLeb128();
  void skipAugP();

  InputSectionBase *IS;
  ArrayRef<uint8_t> D;
};
}

size_t elf::readEhRecordSize(InputSectionBase *S, size_t Off) {
  return EhReader(S, S->data().slice(Off)).readEhRecordSize();
}

// .eh_frame section is a sequence of records. Each record starts with
// a 4 byte length field. This function reads the length.
size_t EhReader::readEhRecordSize() {
  if (D.size() < 4)
    failOn(D.data(), "CIE/FDE too small");

  // First 4 bytes of CIE/FDE is the size of the record.
  // If it is 0xFFFFFFFF, the next 8 bytes contain the size instead,
  // but we do not support that format yet.
  uint64_t V = read32(D.data());
  if (V == UINT32_MAX)
    failOn(D.data(), "CIE/FDE too large");
  uint64_t Size = V + 4;
  if (Size > D.size())
    failOn(D.data(), "CIE/FDE ends past the end of the section");
  return Size;
}

// Read a byte and advance D by one byte.
uint8_t EhReader::readByte() {
  if (D.empty())
    failOn(D.data(), "unexpected end of CIE");
  uint8_t B = D.front();
  D = D.slice(1);
  return B;
}

void EhReader::skipBytes(size_t Count) {
  if (D.size() < Count)
    failOn(D.data(), "CIE is too small");
  D = D.slice(Count);
}

// Read a null-terminated string.
StringRef EhReader::readString() {
  const uint8_t *End = std::find(D.begin(), D.end(), '\0');
  if (End == D.end())
    failOn(D.data(), "corrupted CIE (failed to read string)");
  StringRef S = toStringRef(D.slice(0, End - D.begin()));
  D = D.slice(S.size() + 1);
  return S;
}

// Skip an integer encoded in the LEB128 format.
// Actual number is not of interest because only the runtime needs it.
// But we need to be at least able to skip it so that we can read
// the field that follows a LEB128 number.
void EhReader::skipLeb128() {
  const uint8_t *ErrPos = D.data();
  while (!D.empty()) {
    uint8_t Val = D.front();
    D = D.slice(1);
    if ((Val & 0x80) == 0)
      return;
  }
  failOn(ErrPos, "corrupted CIE (failed to read LEB128)");
}

static size_t getAugPSize(unsigned Enc) {
  switch (Enc & 0x0f) {
  case DW_EH_PE_absptr:
  case DW_EH_PE_signed:
    return Config->Wordsize;
  case DW_EH_PE_udata2:
  case DW_EH_PE_sdata2:
    return 2;
  case DW_EH_PE_udata4:
  case DW_EH_PE_sdata4:
    return 4;
  case DW_EH_PE_udata8:
  case DW_EH_PE_sdata8:
    return 8;
  }
  return 0;
}

void EhReader::skipAugP() {
  uint8_t Enc = readByte();
  if ((Enc & 0xf0) == DW_EH_PE_aligned)
    failOn(D.data() - 1, "DW_EH_PE_aligned encoding is not supported");
  size_t Size = getAugPSize(Enc);
  if (Size == 0)
    failOn(D.data() - 1, "unknown FDE encoding");
  if (Size >= D.size())
    failOn(D.data() - 1, "corrupted CIE");
  D = D.slice(Size);
}

uint8_t elf::getFdeEncoding(EhSectionPiece *P) {
  return EhReader(P->Sec, P->data()).getFdeEncoding();
}

uint8_t EhReader::getFdeEncoding() {
  skipBytes(8);
  int Version = readByte();
  if (Version != 1 && Version != 3)
    failOn(D.data() - 1,
           "FDE version 1 or 3 expected, but got " + Twine(Version));

  StringRef Aug = readString();

  // Skip code and data alignment factors.
  skipLeb128();
  skipLeb128();

  // Skip the return address register. In CIE version 1 this is a single
  // byte. In CIE version 3 this is an unsigned LEB128.
  if (Version == 1)
    readByte();
  else
    skipLeb128();

  // We only care about an 'R' value, but other records may precede an 'R'
  // record. Unfortunately records are not in TLV (type-length-value) format,
  // so we need to teach the linker how to skip records for each type.
  for (char C : Aug) {
    if (C == 'R')
      return readByte();
    if (C == 'z') {
      skipLeb128();
      continue;
    }
    if (C == 'P') {
      skipAugP();
      continue;
    }
    if (C == 'L') {
      readByte();
      continue;
    }
    failOn(Aug.data(), "unknown .eh_frame augmentation string: " + Aug);
  }
  return DW_EH_PE_absptr;
}
