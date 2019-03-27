//===- DWARFDebugLine.cpp -------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/DWARF/DWARFDebugLine.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DWARF/DWARFFormValue.h"
#include "llvm/DebugInfo/DWARF/DWARFRelocMap.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <utility>

using namespace llvm;
using namespace dwarf;

using FileLineInfoKind = DILineInfoSpecifier::FileLineInfoKind;

namespace {

struct ContentDescriptor {
  dwarf::LineNumberEntryFormat Type;
  dwarf::Form Form;
};

using ContentDescriptors = SmallVector<ContentDescriptor, 4>;

} // end anonmyous namespace

void DWARFDebugLine::ContentTypeTracker::trackContentType(
    dwarf::LineNumberEntryFormat ContentType) {
  switch (ContentType) {
  case dwarf::DW_LNCT_timestamp:
    HasModTime = true;
    break;
  case dwarf::DW_LNCT_size:
    HasLength = true;
    break;
  case dwarf::DW_LNCT_MD5:
    HasMD5 = true;
    break;
  case dwarf::DW_LNCT_LLVM_source:
    HasSource = true;
    break;
  default:
    // We only care about values we consider optional, and new values may be
    // added in the vendor extension range, so we do not match exhaustively.
    break;
  }
}

DWARFDebugLine::Prologue::Prologue() { clear(); }

void DWARFDebugLine::Prologue::clear() {
  TotalLength = PrologueLength = 0;
  SegSelectorSize = 0;
  MinInstLength = MaxOpsPerInst = DefaultIsStmt = LineBase = LineRange = 0;
  OpcodeBase = 0;
  FormParams = dwarf::FormParams({0, 0, DWARF32});
  ContentTypes = ContentTypeTracker();
  StandardOpcodeLengths.clear();
  IncludeDirectories.clear();
  FileNames.clear();
}

void DWARFDebugLine::Prologue::dump(raw_ostream &OS,
                                    DIDumpOptions DumpOptions) const {
  OS << "Line table prologue:\n"
     << format("    total_length: 0x%8.8" PRIx64 "\n", TotalLength)
     << format("         version: %u\n", getVersion());
  if (getVersion() >= 5)
    OS << format("    address_size: %u\n", getAddressSize())
       << format(" seg_select_size: %u\n", SegSelectorSize);
  OS << format(" prologue_length: 0x%8.8" PRIx64 "\n", PrologueLength)
     << format(" min_inst_length: %u\n", MinInstLength)
     << format(getVersion() >= 4 ? "max_ops_per_inst: %u\n" : "", MaxOpsPerInst)
     << format(" default_is_stmt: %u\n", DefaultIsStmt)
     << format("       line_base: %i\n", LineBase)
     << format("      line_range: %u\n", LineRange)
     << format("     opcode_base: %u\n", OpcodeBase);

  for (uint32_t I = 0; I != StandardOpcodeLengths.size(); ++I)
    OS << format("standard_opcode_lengths[%s] = %u\n",
                 LNStandardString(I + 1).data(), StandardOpcodeLengths[I]);

  if (!IncludeDirectories.empty()) {
    // DWARF v5 starts directory indexes at 0.
    uint32_t DirBase = getVersion() >= 5 ? 0 : 1;
    for (uint32_t I = 0; I != IncludeDirectories.size(); ++I) {
      OS << format("include_directories[%3u] = ", I + DirBase);
      IncludeDirectories[I].dump(OS, DumpOptions);
      OS << '\n';
    }
  }

  if (!FileNames.empty()) {
    // DWARF v5 starts file indexes at 0.
    uint32_t FileBase = getVersion() >= 5 ? 0 : 1;
    for (uint32_t I = 0; I != FileNames.size(); ++I) {
      const FileNameEntry &FileEntry = FileNames[I];
      OS <<   format("file_names[%3u]:\n", I + FileBase);
      OS <<          "           name: ";
      FileEntry.Name.dump(OS, DumpOptions);
      OS << '\n'
         <<   format("      dir_index: %" PRIu64 "\n", FileEntry.DirIdx);
      if (ContentTypes.HasMD5)
        OS <<        "   md5_checksum: " << FileEntry.Checksum.digest() << '\n';
      if (ContentTypes.HasModTime)
        OS << format("       mod_time: 0x%8.8" PRIx64 "\n", FileEntry.ModTime);
      if (ContentTypes.HasLength)
        OS << format("         length: 0x%8.8" PRIx64 "\n", FileEntry.Length);
      if (ContentTypes.HasSource) {
        OS <<        "         source: ";
        FileEntry.Source.dump(OS, DumpOptions);
        OS << '\n';
      }
    }
  }
}

// Parse v2-v4 directory and file tables.
static void
parseV2DirFileTables(const DWARFDataExtractor &DebugLineData,
                     uint32_t *OffsetPtr, uint64_t EndPrologueOffset,
                     DWARFDebugLine::ContentTypeTracker &ContentTypes,
                     std::vector<DWARFFormValue> &IncludeDirectories,
                     std::vector<DWARFDebugLine::FileNameEntry> &FileNames) {
  while (*OffsetPtr < EndPrologueOffset) {
    StringRef S = DebugLineData.getCStrRef(OffsetPtr);
    if (S.empty())
      break;
    DWARFFormValue Dir(dwarf::DW_FORM_string);
    Dir.setPValue(S.data());
    IncludeDirectories.push_back(Dir);
  }

  while (*OffsetPtr < EndPrologueOffset) {
    StringRef Name = DebugLineData.getCStrRef(OffsetPtr);
    if (Name.empty())
      break;
    DWARFDebugLine::FileNameEntry FileEntry;
    FileEntry.Name.setForm(dwarf::DW_FORM_string);
    FileEntry.Name.setPValue(Name.data());
    FileEntry.DirIdx = DebugLineData.getULEB128(OffsetPtr);
    FileEntry.ModTime = DebugLineData.getULEB128(OffsetPtr);
    FileEntry.Length = DebugLineData.getULEB128(OffsetPtr);
    FileNames.push_back(FileEntry);
  }

  ContentTypes.HasModTime = true;
  ContentTypes.HasLength = true;
}

// Parse v5 directory/file entry content descriptions.
// Returns the descriptors, or an empty vector if we did not find a path or
// ran off the end of the prologue.
static ContentDescriptors
parseV5EntryFormat(const DWARFDataExtractor &DebugLineData, uint32_t
    *OffsetPtr, uint64_t EndPrologueOffset, DWARFDebugLine::ContentTypeTracker
    *ContentTypes) {
  ContentDescriptors Descriptors;
  int FormatCount = DebugLineData.getU8(OffsetPtr);
  bool HasPath = false;
  for (int I = 0; I != FormatCount; ++I) {
    if (*OffsetPtr >= EndPrologueOffset)
      return ContentDescriptors();
    ContentDescriptor Descriptor;
    Descriptor.Type =
      dwarf::LineNumberEntryFormat(DebugLineData.getULEB128(OffsetPtr));
    Descriptor.Form = dwarf::Form(DebugLineData.getULEB128(OffsetPtr));
    if (Descriptor.Type == dwarf::DW_LNCT_path)
      HasPath = true;
    if (ContentTypes)
      ContentTypes->trackContentType(Descriptor.Type);
    Descriptors.push_back(Descriptor);
  }
  return HasPath ? Descriptors : ContentDescriptors();
}

static bool
parseV5DirFileTables(const DWARFDataExtractor &DebugLineData,
                     uint32_t *OffsetPtr, uint64_t EndPrologueOffset,
                     const dwarf::FormParams &FormParams,
                     const DWARFContext &Ctx, const DWARFUnit *U,
                     DWARFDebugLine::ContentTypeTracker &ContentTypes,
                     std::vector<DWARFFormValue> &IncludeDirectories,
                     std::vector<DWARFDebugLine::FileNameEntry> &FileNames) {
  // Get the directory entry description.
  ContentDescriptors DirDescriptors =
      parseV5EntryFormat(DebugLineData, OffsetPtr, EndPrologueOffset, nullptr);
  if (DirDescriptors.empty())
    return false;

  // Get the directory entries, according to the format described above.
  int DirEntryCount = DebugLineData.getU8(OffsetPtr);
  for (int I = 0; I != DirEntryCount; ++I) {
    if (*OffsetPtr >= EndPrologueOffset)
      return false;
    for (auto Descriptor : DirDescriptors) {
      DWARFFormValue Value(Descriptor.Form);
      switch (Descriptor.Type) {
      case DW_LNCT_path:
        if (!Value.extractValue(DebugLineData, OffsetPtr, FormParams, &Ctx, U))
          return false;
        IncludeDirectories.push_back(Value);
        break;
      default:
        if (!Value.skipValue(DebugLineData, OffsetPtr, FormParams))
          return false;
      }
    }
  }

  // Get the file entry description.
  ContentDescriptors FileDescriptors =
      parseV5EntryFormat(DebugLineData, OffsetPtr, EndPrologueOffset,
          &ContentTypes);
  if (FileDescriptors.empty())
    return false;

  // Get the file entries, according to the format described above.
  int FileEntryCount = DebugLineData.getU8(OffsetPtr);
  for (int I = 0; I != FileEntryCount; ++I) {
    if (*OffsetPtr >= EndPrologueOffset)
      return false;
    DWARFDebugLine::FileNameEntry FileEntry;
    for (auto Descriptor : FileDescriptors) {
      DWARFFormValue Value(Descriptor.Form);
      if (!Value.extractValue(DebugLineData, OffsetPtr, FormParams, &Ctx, U))
        return false;
      switch (Descriptor.Type) {
      case DW_LNCT_path:
        FileEntry.Name = Value;
        break;
      case DW_LNCT_LLVM_source:
        FileEntry.Source = Value;
        break;
      case DW_LNCT_directory_index:
        FileEntry.DirIdx = Value.getAsUnsignedConstant().getValue();
        break;
      case DW_LNCT_timestamp:
        FileEntry.ModTime = Value.getAsUnsignedConstant().getValue();
        break;
      case DW_LNCT_size:
        FileEntry.Length = Value.getAsUnsignedConstant().getValue();
        break;
      case DW_LNCT_MD5:
        assert(Value.getAsBlock().getValue().size() == 16);
        std::uninitialized_copy_n(Value.getAsBlock().getValue().begin(), 16,
                                  FileEntry.Checksum.Bytes.begin());
        break;
      default:
        break;
      }
    }
    FileNames.push_back(FileEntry);
  }
  return true;
}

Error DWARFDebugLine::Prologue::parse(const DWARFDataExtractor &DebugLineData,
                                      uint32_t *OffsetPtr,
                                      const DWARFContext &Ctx,
                                      const DWARFUnit *U) {
  const uint64_t PrologueOffset = *OffsetPtr;

  clear();
  TotalLength = DebugLineData.getU32(OffsetPtr);
  if (TotalLength == UINT32_MAX) {
    FormParams.Format = dwarf::DWARF64;
    TotalLength = DebugLineData.getU64(OffsetPtr);
  } else if (TotalLength >= 0xffffff00) {
    return createStringError(errc::invalid_argument,
        "parsing line table prologue at offset 0x%8.8" PRIx64
        " unsupported reserved unit length found of value 0x%8.8" PRIx64,
        PrologueOffset, TotalLength);
  }
  FormParams.Version = DebugLineData.getU16(OffsetPtr);
  if (getVersion() < 2)
    return createStringError(errc::not_supported,
                       "parsing line table prologue at offset 0x%8.8" PRIx64
                       " found unsupported version 0x%2.2" PRIx16,
                       PrologueOffset, getVersion());

  if (getVersion() >= 5) {
    FormParams.AddrSize = DebugLineData.getU8(OffsetPtr);
    assert((DebugLineData.getAddressSize() == 0 ||
            DebugLineData.getAddressSize() == getAddressSize()) &&
           "Line table header and data extractor disagree");
    SegSelectorSize = DebugLineData.getU8(OffsetPtr);
  }

  PrologueLength = DebugLineData.getUnsigned(OffsetPtr, sizeofPrologueLength());
  const uint64_t EndPrologueOffset = PrologueLength + *OffsetPtr;
  MinInstLength = DebugLineData.getU8(OffsetPtr);
  if (getVersion() >= 4)
    MaxOpsPerInst = DebugLineData.getU8(OffsetPtr);
  DefaultIsStmt = DebugLineData.getU8(OffsetPtr);
  LineBase = DebugLineData.getU8(OffsetPtr);
  LineRange = DebugLineData.getU8(OffsetPtr);
  OpcodeBase = DebugLineData.getU8(OffsetPtr);

  StandardOpcodeLengths.reserve(OpcodeBase - 1);
  for (uint32_t I = 1; I < OpcodeBase; ++I) {
    uint8_t OpLen = DebugLineData.getU8(OffsetPtr);
    StandardOpcodeLengths.push_back(OpLen);
  }

  if (getVersion() >= 5) {
    if (!parseV5DirFileTables(DebugLineData, OffsetPtr, EndPrologueOffset,
                              FormParams, Ctx, U, ContentTypes,
                              IncludeDirectories, FileNames)) {
      return createStringError(errc::invalid_argument,
          "parsing line table prologue at 0x%8.8" PRIx64
          " found an invalid directory or file table description at"
          " 0x%8.8" PRIx64,
          PrologueOffset, (uint64_t)*OffsetPtr);
    }
  } else
    parseV2DirFileTables(DebugLineData, OffsetPtr, EndPrologueOffset,
                         ContentTypes, IncludeDirectories, FileNames);

  if (*OffsetPtr != EndPrologueOffset)
    return createStringError(errc::invalid_argument,
                       "parsing line table prologue at 0x%8.8" PRIx64
                       " should have ended at 0x%8.8" PRIx64
                       " but it ended at 0x%8.8" PRIx64,
                       PrologueOffset, EndPrologueOffset, (uint64_t)*OffsetPtr);
  return Error::success();
}

DWARFDebugLine::Row::Row(bool DefaultIsStmt) { reset(DefaultIsStmt); }

void DWARFDebugLine::Row::postAppend() {
  BasicBlock = false;
  PrologueEnd = false;
  EpilogueBegin = false;
}

void DWARFDebugLine::Row::reset(bool DefaultIsStmt) {
  Address = 0;
  Line = 1;
  Column = 0;
  File = 1;
  Isa = 0;
  Discriminator = 0;
  IsStmt = DefaultIsStmt;
  BasicBlock = false;
  EndSequence = false;
  PrologueEnd = false;
  EpilogueBegin = false;
}

void DWARFDebugLine::Row::dumpTableHeader(raw_ostream &OS) {
  OS << "Address            Line   Column File   ISA Discriminator Flags\n"
     << "------------------ ------ ------ ------ --- ------------- "
        "-------------\n";
}

void DWARFDebugLine::Row::dump(raw_ostream &OS) const {
  OS << format("0x%16.16" PRIx64 " %6u %6u", Address, Line, Column)
     << format(" %6u %3u %13u ", File, Isa, Discriminator)
     << (IsStmt ? " is_stmt" : "") << (BasicBlock ? " basic_block" : "")
     << (PrologueEnd ? " prologue_end" : "")
     << (EpilogueBegin ? " epilogue_begin" : "")
     << (EndSequence ? " end_sequence" : "") << '\n';
}

DWARFDebugLine::Sequence::Sequence() { reset(); }

void DWARFDebugLine::Sequence::reset() {
  LowPC = 0;
  HighPC = 0;
  FirstRowIndex = 0;
  LastRowIndex = 0;
  Empty = true;
}

DWARFDebugLine::LineTable::LineTable() { clear(); }

void DWARFDebugLine::LineTable::dump(raw_ostream &OS,
                                     DIDumpOptions DumpOptions) const {
  Prologue.dump(OS, DumpOptions);
  OS << '\n';

  if (!Rows.empty()) {
    Row::dumpTableHeader(OS);
    for (const Row &R : Rows) {
      R.dump(OS);
    }
  }
}

void DWARFDebugLine::LineTable::clear() {
  Prologue.clear();
  Rows.clear();
  Sequences.clear();
}

DWARFDebugLine::ParsingState::ParsingState(struct LineTable *LT)
    : LineTable(LT) {
  resetRowAndSequence();
}

void DWARFDebugLine::ParsingState::resetRowAndSequence() {
  Row.reset(LineTable->Prologue.DefaultIsStmt);
  Sequence.reset();
}

void DWARFDebugLine::ParsingState::appendRowToMatrix(uint32_t Offset) {
  if (Sequence.Empty) {
    // Record the beginning of instruction sequence.
    Sequence.Empty = false;
    Sequence.LowPC = Row.Address;
    Sequence.FirstRowIndex = RowNumber;
  }
  ++RowNumber;
  LineTable->appendRow(Row);
  if (Row.EndSequence) {
    // Record the end of instruction sequence.
    Sequence.HighPC = Row.Address;
    Sequence.LastRowIndex = RowNumber;
    if (Sequence.isValid())
      LineTable->appendSequence(Sequence);
    Sequence.reset();
  }
  Row.postAppend();
}

const DWARFDebugLine::LineTable *
DWARFDebugLine::getLineTable(uint32_t Offset) const {
  LineTableConstIter Pos = LineTableMap.find(Offset);
  if (Pos != LineTableMap.end())
    return &Pos->second;
  return nullptr;
}

Expected<const DWARFDebugLine::LineTable *> DWARFDebugLine::getOrParseLineTable(
    DWARFDataExtractor &DebugLineData, uint32_t Offset, const DWARFContext &Ctx,
    const DWARFUnit *U, std::function<void(Error)> RecoverableErrorCallback) {
  if (!DebugLineData.isValidOffset(Offset))
    return createStringError(errc::invalid_argument, "offset 0x%8.8" PRIx32
                       " is not a valid debug line section offset",
                       Offset);

  std::pair<LineTableIter, bool> Pos =
      LineTableMap.insert(LineTableMapTy::value_type(Offset, LineTable()));
  LineTable *LT = &Pos.first->second;
  if (Pos.second) {
    if (Error Err =
            LT->parse(DebugLineData, &Offset, Ctx, U, RecoverableErrorCallback))
      return std::move(Err);
    return LT;
  }
  return LT;
}

Error DWARFDebugLine::LineTable::parse(
    DWARFDataExtractor &DebugLineData, uint32_t *OffsetPtr,
    const DWARFContext &Ctx, const DWARFUnit *U,
    std::function<void(Error)> RecoverableErrorCallback, raw_ostream *OS) {
  const uint32_t DebugLineOffset = *OffsetPtr;

  clear();

  Error PrologueErr = Prologue.parse(DebugLineData, OffsetPtr, Ctx, U);

  if (OS) {
    // The presence of OS signals verbose dumping.
    DIDumpOptions DumpOptions;
    DumpOptions.Verbose = true;
    Prologue.dump(*OS, DumpOptions);
  }

  if (PrologueErr)
    return PrologueErr;

  const uint32_t EndOffset =
      DebugLineOffset + Prologue.TotalLength + Prologue.sizeofTotalLength();

  // See if we should tell the data extractor the address size.
  if (DebugLineData.getAddressSize() == 0)
    DebugLineData.setAddressSize(Prologue.getAddressSize());
  else
    assert(Prologue.getAddressSize() == 0 ||
           Prologue.getAddressSize() == DebugLineData.getAddressSize());

  ParsingState State(this);

  while (*OffsetPtr < EndOffset) {
    if (OS)
      *OS << format("0x%08.08" PRIx32 ": ", *OffsetPtr);

    uint8_t Opcode = DebugLineData.getU8(OffsetPtr);

    if (OS)
      *OS << format("%02.02" PRIx8 " ", Opcode);

    if (Opcode == 0) {
      // Extended Opcodes always start with a zero opcode followed by
      // a uleb128 length so you can skip ones you don't know about
      uint64_t Len = DebugLineData.getULEB128(OffsetPtr);
      uint32_t ExtOffset = *OffsetPtr;

      // Tolerate zero-length; assume length is correct and soldier on.
      if (Len == 0) {
        if (OS)
          *OS << "Badly formed extended line op (length 0)\n";
        continue;
      }

      uint8_t SubOpcode = DebugLineData.getU8(OffsetPtr);
      if (OS)
        *OS << LNExtendedString(SubOpcode);
      switch (SubOpcode) {
      case DW_LNE_end_sequence:
        // Set the end_sequence register of the state machine to true and
        // append a row to the matrix using the current values of the
        // state-machine registers. Then reset the registers to the initial
        // values specified above. Every statement program sequence must end
        // with a DW_LNE_end_sequence instruction which creates a row whose
        // address is that of the byte after the last target machine instruction
        // of the sequence.
        State.Row.EndSequence = true;
        State.appendRowToMatrix(*OffsetPtr);
        if (OS) {
          *OS << "\n";
          OS->indent(12);
          State.Row.dump(*OS);
        }
        State.resetRowAndSequence();
        break;

      case DW_LNE_set_address:
        // Takes a single relocatable address as an operand. The size of the
        // operand is the size appropriate to hold an address on the target
        // machine. Set the address register to the value given by the
        // relocatable address. All of the other statement program opcodes
        // that affect the address register add a delta to it. This instruction
        // stores a relocatable value into it instead.
        //
        // Make sure the extractor knows the address size.  If not, infer it
        // from the size of the operand.
        if (DebugLineData.getAddressSize() == 0)
          DebugLineData.setAddressSize(Len - 1);
        else if (DebugLineData.getAddressSize() != Len - 1) {
          return createStringError(errc::invalid_argument,
                             "mismatching address size at offset 0x%8.8" PRIx32
                             " expected 0x%2.2" PRIx8 " found 0x%2.2" PRIx64,
                             ExtOffset, DebugLineData.getAddressSize(),
                             Len - 1);
        }
        State.Row.Address = DebugLineData.getRelocatedAddress(OffsetPtr);
        if (OS)
          *OS << format(" (0x%16.16" PRIx64 ")", State.Row.Address);
        break;

      case DW_LNE_define_file:
        // Takes 4 arguments. The first is a null terminated string containing
        // a source file name. The second is an unsigned LEB128 number
        // representing the directory index of the directory in which the file
        // was found. The third is an unsigned LEB128 number representing the
        // time of last modification of the file. The fourth is an unsigned
        // LEB128 number representing the length in bytes of the file. The time
        // and length fields may contain LEB128(0) if the information is not
        // available.
        //
        // The directory index represents an entry in the include_directories
        // section of the statement program prologue. The index is LEB128(0)
        // if the file was found in the current directory of the compilation,
        // LEB128(1) if it was found in the first directory in the
        // include_directories section, and so on. The directory index is
        // ignored for file names that represent full path names.
        //
        // The files are numbered, starting at 1, in the order in which they
        // appear; the names in the prologue come before names defined by
        // the DW_LNE_define_file instruction. These numbers are used in the
        // the file register of the state machine.
        {
          FileNameEntry FileEntry;
          const char *Name = DebugLineData.getCStr(OffsetPtr);
          FileEntry.Name.setForm(dwarf::DW_FORM_string);
          FileEntry.Name.setPValue(Name);
          FileEntry.DirIdx = DebugLineData.getULEB128(OffsetPtr);
          FileEntry.ModTime = DebugLineData.getULEB128(OffsetPtr);
          FileEntry.Length = DebugLineData.getULEB128(OffsetPtr);
          Prologue.FileNames.push_back(FileEntry);
          if (OS)
            *OS << " (" << Name << ", dir=" << FileEntry.DirIdx << ", mod_time="
                << format("(0x%16.16" PRIx64 ")", FileEntry.ModTime)
                << ", length=" << FileEntry.Length << ")";
        }
        break;

      case DW_LNE_set_discriminator:
        State.Row.Discriminator = DebugLineData.getULEB128(OffsetPtr);
        if (OS)
          *OS << " (" << State.Row.Discriminator << ")";
        break;

      default:
        if (OS)
          *OS << format("Unrecognized extended op 0x%02.02" PRIx8, SubOpcode)
              << format(" length %" PRIx64, Len);
        // Len doesn't include the zero opcode byte or the length itself, but
        // it does include the sub_opcode, so we have to adjust for that.
        (*OffsetPtr) += Len - 1;
        break;
      }
      // Make sure the stated and parsed lengths are the same.
      // Otherwise we have an unparseable line-number program.
      if (*OffsetPtr - ExtOffset != Len)
        return createStringError(errc::illegal_byte_sequence,
                           "unexpected line op length at offset 0x%8.8" PRIx32
                           " expected 0x%2.2" PRIx64 " found 0x%2.2" PRIx32,
                           ExtOffset, Len, *OffsetPtr - ExtOffset);
    } else if (Opcode < Prologue.OpcodeBase) {
      if (OS)
        *OS << LNStandardString(Opcode);
      switch (Opcode) {
      // Standard Opcodes
      case DW_LNS_copy:
        // Takes no arguments. Append a row to the matrix using the
        // current values of the state-machine registers. Then set
        // the basic_block register to false.
        State.appendRowToMatrix(*OffsetPtr);
        if (OS) {
          *OS << "\n";
          OS->indent(12);
          State.Row.dump(*OS);
          *OS << "\n";
        }
        break;

      case DW_LNS_advance_pc:
        // Takes a single unsigned LEB128 operand, multiplies it by the
        // min_inst_length field of the prologue, and adds the
        // result to the address register of the state machine.
        {
          uint64_t AddrOffset =
              DebugLineData.getULEB128(OffsetPtr) * Prologue.MinInstLength;
          State.Row.Address += AddrOffset;
          if (OS)
            *OS << " (" << AddrOffset << ")";
        }
        break;

      case DW_LNS_advance_line:
        // Takes a single signed LEB128 operand and adds that value to
        // the line register of the state machine.
        State.Row.Line += DebugLineData.getSLEB128(OffsetPtr);
        if (OS)
          *OS << " (" << State.Row.Line << ")";
        break;

      case DW_LNS_set_file:
        // Takes a single unsigned LEB128 operand and stores it in the file
        // register of the state machine.
        State.Row.File = DebugLineData.getULEB128(OffsetPtr);
        if (OS)
          *OS << " (" << State.Row.File << ")";
        break;

      case DW_LNS_set_column:
        // Takes a single unsigned LEB128 operand and stores it in the
        // column register of the state machine.
        State.Row.Column = DebugLineData.getULEB128(OffsetPtr);
        if (OS)
          *OS << " (" << State.Row.Column << ")";
        break;

      case DW_LNS_negate_stmt:
        // Takes no arguments. Set the is_stmt register of the state
        // machine to the logical negation of its current value.
        State.Row.IsStmt = !State.Row.IsStmt;
        break;

      case DW_LNS_set_basic_block:
        // Takes no arguments. Set the basic_block register of the
        // state machine to true
        State.Row.BasicBlock = true;
        break;

      case DW_LNS_const_add_pc:
        // Takes no arguments. Add to the address register of the state
        // machine the address increment value corresponding to special
        // opcode 255. The motivation for DW_LNS_const_add_pc is this:
        // when the statement program needs to advance the address by a
        // small amount, it can use a single special opcode, which occupies
        // a single byte. When it needs to advance the address by up to
        // twice the range of the last special opcode, it can use
        // DW_LNS_const_add_pc followed by a special opcode, for a total
        // of two bytes. Only if it needs to advance the address by more
        // than twice that range will it need to use both DW_LNS_advance_pc
        // and a special opcode, requiring three or more bytes.
        {
          uint8_t AdjustOpcode = 255 - Prologue.OpcodeBase;
          uint64_t AddrOffset =
              (AdjustOpcode / Prologue.LineRange) * Prologue.MinInstLength;
          State.Row.Address += AddrOffset;
          if (OS)
            *OS
                << format(" (0x%16.16" PRIx64 ")", AddrOffset);
        }
        break;

      case DW_LNS_fixed_advance_pc:
        // Takes a single uhalf operand. Add to the address register of
        // the state machine the value of the (unencoded) operand. This
        // is the only extended opcode that takes an argument that is not
        // a variable length number. The motivation for DW_LNS_fixed_advance_pc
        // is this: existing assemblers cannot emit DW_LNS_advance_pc or
        // special opcodes because they cannot encode LEB128 numbers or
        // judge when the computation of a special opcode overflows and
        // requires the use of DW_LNS_advance_pc. Such assemblers, however,
        // can use DW_LNS_fixed_advance_pc instead, sacrificing compression.
        {
          uint16_t PCOffset = DebugLineData.getU16(OffsetPtr);
          State.Row.Address += PCOffset;
          if (OS)
            *OS
                << format(" (0x%16.16" PRIx64 ")", PCOffset);
        }
        break;

      case DW_LNS_set_prologue_end:
        // Takes no arguments. Set the prologue_end register of the
        // state machine to true
        State.Row.PrologueEnd = true;
        break;

      case DW_LNS_set_epilogue_begin:
        // Takes no arguments. Set the basic_block register of the
        // state machine to true
        State.Row.EpilogueBegin = true;
        break;

      case DW_LNS_set_isa:
        // Takes a single unsigned LEB128 operand and stores it in the
        // column register of the state machine.
        State.Row.Isa = DebugLineData.getULEB128(OffsetPtr);
        if (OS)
          *OS << " (" << State.Row.Isa << ")";
        break;

      default:
        // Handle any unknown standard opcodes here. We know the lengths
        // of such opcodes because they are specified in the prologue
        // as a multiple of LEB128 operands for each opcode.
        {
          assert(Opcode - 1U < Prologue.StandardOpcodeLengths.size());
          uint8_t OpcodeLength = Prologue.StandardOpcodeLengths[Opcode - 1];
          for (uint8_t I = 0; I < OpcodeLength; ++I) {
            uint64_t Value = DebugLineData.getULEB128(OffsetPtr);
            if (OS)
              *OS << format("Skipping ULEB128 value: 0x%16.16" PRIx64 ")\n",
                            Value);
          }
        }
        break;
      }
    } else {
      // Special Opcodes

      // A special opcode value is chosen based on the amount that needs
      // to be added to the line and address registers. The maximum line
      // increment for a special opcode is the value of the line_base
      // field in the header, plus the value of the line_range field,
      // minus 1 (line base + line range - 1). If the desired line
      // increment is greater than the maximum line increment, a standard
      // opcode must be used instead of a special opcode. The "address
      // advance" is calculated by dividing the desired address increment
      // by the minimum_instruction_length field from the header. The
      // special opcode is then calculated using the following formula:
      //
      //  opcode = (desired line increment - line_base) +
      //           (line_range * address advance) + opcode_base
      //
      // If the resulting opcode is greater than 255, a standard opcode
      // must be used instead.
      //
      // To decode a special opcode, subtract the opcode_base from the
      // opcode itself to give the adjusted opcode. The amount to
      // increment the address register is the result of the adjusted
      // opcode divided by the line_range multiplied by the
      // minimum_instruction_length field from the header. That is:
      //
      //  address increment = (adjusted opcode / line_range) *
      //                      minimum_instruction_length
      //
      // The amount to increment the line register is the line_base plus
      // the result of the adjusted opcode modulo the line_range. That is:
      //
      // line increment = line_base + (adjusted opcode % line_range)

      uint8_t AdjustOpcode = Opcode - Prologue.OpcodeBase;
      uint64_t AddrOffset =
          (AdjustOpcode / Prologue.LineRange) * Prologue.MinInstLength;
      int32_t LineOffset =
          Prologue.LineBase + (AdjustOpcode % Prologue.LineRange);
      State.Row.Line += LineOffset;
      State.Row.Address += AddrOffset;

      if (OS) {
        *OS << "address += " << ((uint32_t)AdjustOpcode)
            << ",  line += " << LineOffset << "\n";
        OS->indent(12);
        State.Row.dump(*OS);
      }

      State.appendRowToMatrix(*OffsetPtr);
      // Reset discriminator to 0.
      State.Row.Discriminator = 0;
    }
    if(OS)
      *OS << "\n";
  }

  if (!State.Sequence.Empty)
    RecoverableErrorCallback(
        createStringError(errc::illegal_byte_sequence,
                    "last sequence in debug line table is not terminated!"));

  // Sort all sequences so that address lookup will work faster.
  if (!Sequences.empty()) {
    llvm::sort(Sequences, Sequence::orderByLowPC);
    // Note: actually, instruction address ranges of sequences should not
    // overlap (in shared objects and executables). If they do, the address
    // lookup would still work, though, but result would be ambiguous.
    // We don't report warning in this case. For example,
    // sometimes .so compiled from multiple object files contains a few
    // rudimentary sequences for address ranges [0x0, 0xsomething).
  }

  return Error::success();
}

uint32_t
DWARFDebugLine::LineTable::findRowInSeq(const DWARFDebugLine::Sequence &Seq,
                                        uint64_t Address) const {
  if (!Seq.containsPC(Address))
    return UnknownRowIndex;
  // Search for instruction address in the rows describing the sequence.
  // Rows are stored in a vector, so we may use arithmetical operations with
  // iterators.
  DWARFDebugLine::Row Row;
  Row.Address = Address;
  RowIter FirstRow = Rows.begin() + Seq.FirstRowIndex;
  RowIter LastRow = Rows.begin() + Seq.LastRowIndex;
  LineTable::RowIter RowPos = std::lower_bound(
      FirstRow, LastRow, Row, DWARFDebugLine::Row::orderByAddress);
  if (RowPos == LastRow) {
    return Seq.LastRowIndex - 1;
  }
  uint32_t Index = Seq.FirstRowIndex + (RowPos - FirstRow);
  if (RowPos->Address > Address) {
    if (RowPos == FirstRow)
      return UnknownRowIndex;
    else
      Index--;
  }
  return Index;
}

uint32_t DWARFDebugLine::LineTable::lookupAddress(uint64_t Address) const {
  if (Sequences.empty())
    return UnknownRowIndex;
  // First, find an instruction sequence containing the given address.
  DWARFDebugLine::Sequence Sequence;
  Sequence.LowPC = Address;
  SequenceIter FirstSeq = Sequences.begin();
  SequenceIter LastSeq = Sequences.end();
  SequenceIter SeqPos = std::lower_bound(
      FirstSeq, LastSeq, Sequence, DWARFDebugLine::Sequence::orderByLowPC);
  DWARFDebugLine::Sequence FoundSeq;
  if (SeqPos == LastSeq) {
    FoundSeq = Sequences.back();
  } else if (SeqPos->LowPC == Address) {
    FoundSeq = *SeqPos;
  } else {
    if (SeqPos == FirstSeq)
      return UnknownRowIndex;
    FoundSeq = *(SeqPos - 1);
  }
  return findRowInSeq(FoundSeq, Address);
}

bool DWARFDebugLine::LineTable::lookupAddressRange(
    uint64_t Address, uint64_t Size, std::vector<uint32_t> &Result) const {
  if (Sequences.empty())
    return false;
  uint64_t EndAddr = Address + Size;
  // First, find an instruction sequence containing the given address.
  DWARFDebugLine::Sequence Sequence;
  Sequence.LowPC = Address;
  SequenceIter FirstSeq = Sequences.begin();
  SequenceIter LastSeq = Sequences.end();
  SequenceIter SeqPos = std::lower_bound(
      FirstSeq, LastSeq, Sequence, DWARFDebugLine::Sequence::orderByLowPC);
  if (SeqPos == LastSeq || SeqPos->LowPC != Address) {
    if (SeqPos == FirstSeq)
      return false;
    SeqPos--;
  }
  if (!SeqPos->containsPC(Address))
    return false;

  SequenceIter StartPos = SeqPos;

  // Add the rows from the first sequence to the vector, starting with the
  // index we just calculated

  while (SeqPos != LastSeq && SeqPos->LowPC < EndAddr) {
    const DWARFDebugLine::Sequence &CurSeq = *SeqPos;
    // For the first sequence, we need to find which row in the sequence is the
    // first in our range.
    uint32_t FirstRowIndex = CurSeq.FirstRowIndex;
    if (SeqPos == StartPos)
      FirstRowIndex = findRowInSeq(CurSeq, Address);

    // Figure out the last row in the range.
    uint32_t LastRowIndex = findRowInSeq(CurSeq, EndAddr - 1);
    if (LastRowIndex == UnknownRowIndex)
      LastRowIndex = CurSeq.LastRowIndex - 1;

    assert(FirstRowIndex != UnknownRowIndex);
    assert(LastRowIndex != UnknownRowIndex);

    for (uint32_t I = FirstRowIndex; I <= LastRowIndex; ++I) {
      Result.push_back(I);
    }

    ++SeqPos;
  }

  return true;
}

bool DWARFDebugLine::LineTable::hasFileAtIndex(uint64_t FileIndex) const {
  return FileIndex != 0 && FileIndex <= Prologue.FileNames.size();
}

Optional<StringRef> DWARFDebugLine::LineTable::getSourceByIndex(uint64_t FileIndex,
                                                                FileLineInfoKind Kind) const {
  if (Kind == FileLineInfoKind::None || !hasFileAtIndex(FileIndex))
    return None;
  const FileNameEntry &Entry = Prologue.FileNames[FileIndex - 1];
  if (Optional<const char *> source = Entry.Source.getAsCString())
    return StringRef(*source);
  return None;
}

static bool isPathAbsoluteOnWindowsOrPosix(const Twine &Path) {
  // Debug info can contain paths from any OS, not necessarily
  // an OS we're currently running on. Moreover different compilation units can
  // be compiled on different operating systems and linked together later.
  return sys::path::is_absolute(Path, sys::path::Style::posix) ||
         sys::path::is_absolute(Path, sys::path::Style::windows);
}

bool DWARFDebugLine::LineTable::getFileNameByIndex(uint64_t FileIndex,
                                                   const char *CompDir,
                                                   FileLineInfoKind Kind,
                                                   std::string &Result) const {
  if (Kind == FileLineInfoKind::None || !hasFileAtIndex(FileIndex))
    return false;
  const FileNameEntry &Entry = Prologue.FileNames[FileIndex - 1];
  StringRef FileName = Entry.Name.getAsCString().getValue();
  if (Kind != FileLineInfoKind::AbsoluteFilePath ||
      isPathAbsoluteOnWindowsOrPosix(FileName)) {
    Result = FileName;
    return true;
  }

  SmallString<16> FilePath;
  uint64_t IncludeDirIndex = Entry.DirIdx;
  StringRef IncludeDir;
  // Be defensive about the contents of Entry.
  if (IncludeDirIndex > 0 &&
      IncludeDirIndex <= Prologue.IncludeDirectories.size())
    IncludeDir = Prologue.IncludeDirectories[IncludeDirIndex - 1]
                     .getAsCString()
                     .getValue();

  // We may still need to append compilation directory of compile unit.
  // We know that FileName is not absolute, the only way to have an
  // absolute path at this point would be if IncludeDir is absolute.
  if (CompDir && Kind == FileLineInfoKind::AbsoluteFilePath &&
      !isPathAbsoluteOnWindowsOrPosix(IncludeDir))
    sys::path::append(FilePath, CompDir);

  // sys::path::append skips empty strings.
  sys::path::append(FilePath, IncludeDir, FileName);
  Result = FilePath.str();
  return true;
}

bool DWARFDebugLine::LineTable::getFileLineInfoForAddress(
    uint64_t Address, const char *CompDir, FileLineInfoKind Kind,
    DILineInfo &Result) const {
  // Get the index of row we're looking for in the line table.
  uint32_t RowIndex = lookupAddress(Address);
  if (RowIndex == -1U)
    return false;
  // Take file number and line/column from the row.
  const auto &Row = Rows[RowIndex];
  if (!getFileNameByIndex(Row.File, CompDir, Kind, Result.FileName))
    return false;
  Result.Line = Row.Line;
  Result.Column = Row.Column;
  Result.Discriminator = Row.Discriminator;
  Result.Source = getSourceByIndex(Row.File, Kind);
  return true;
}

// We want to supply the Unit associated with a .debug_line[.dwo] table when
// we dump it, if possible, but still dump the table even if there isn't a Unit.
// Therefore, collect up handles on all the Units that point into the
// line-table section.
static DWARFDebugLine::SectionParser::LineToUnitMap
buildLineToUnitMap(DWARFDebugLine::SectionParser::cu_range CUs,
                   DWARFDebugLine::SectionParser::tu_range TUs) {
  DWARFDebugLine::SectionParser::LineToUnitMap LineToUnit;
  for (const auto &CU : CUs)
    if (auto CUDIE = CU->getUnitDIE())
      if (auto StmtOffset = toSectionOffset(CUDIE.find(DW_AT_stmt_list)))
        LineToUnit.insert(std::make_pair(*StmtOffset, &*CU));
  for (const auto &TU : TUs)
    if (auto TUDIE = TU->getUnitDIE())
      if (auto StmtOffset = toSectionOffset(TUDIE.find(DW_AT_stmt_list)))
        LineToUnit.insert(std::make_pair(*StmtOffset, &*TU));
  return LineToUnit;
}

DWARFDebugLine::SectionParser::SectionParser(DWARFDataExtractor &Data,
                                             const DWARFContext &C,
                                             cu_range CUs, tu_range TUs)
    : DebugLineData(Data), Context(C) {
  LineToUnit = buildLineToUnitMap(CUs, TUs);
  if (!DebugLineData.isValidOffset(Offset))
    Done = true;
}

bool DWARFDebugLine::Prologue::totalLengthIsValid() const {
  return TotalLength == 0xffffffff || TotalLength < 0xffffff00;
}

DWARFDebugLine::LineTable DWARFDebugLine::SectionParser::parseNext(
    function_ref<void(Error)> RecoverableErrorCallback,
    function_ref<void(Error)> UnrecoverableErrorCallback, raw_ostream *OS) {
  assert(DebugLineData.isValidOffset(Offset) &&
         "parsing should have terminated");
  DWARFUnit *U = prepareToParse(Offset);
  uint32_t OldOffset = Offset;
  LineTable LT;
  if (Error Err = LT.parse(DebugLineData, &Offset, Context, U,
                           RecoverableErrorCallback, OS))
    UnrecoverableErrorCallback(std::move(Err));
  moveToNextTable(OldOffset, LT.Prologue);
  return LT;
}

void DWARFDebugLine::SectionParser::skip(
    function_ref<void(Error)> ErrorCallback) {
  assert(DebugLineData.isValidOffset(Offset) &&
         "parsing should have terminated");
  DWARFUnit *U = prepareToParse(Offset);
  uint32_t OldOffset = Offset;
  LineTable LT;
  if (Error Err = LT.Prologue.parse(DebugLineData, &Offset, Context, U))
    ErrorCallback(std::move(Err));
  moveToNextTable(OldOffset, LT.Prologue);
}

DWARFUnit *DWARFDebugLine::SectionParser::prepareToParse(uint32_t Offset) {
  DWARFUnit *U = nullptr;
  auto It = LineToUnit.find(Offset);
  if (It != LineToUnit.end())
    U = It->second;
  DebugLineData.setAddressSize(U ? U->getAddressByteSize() : 0);
  return U;
}

void DWARFDebugLine::SectionParser::moveToNextTable(uint32_t OldOffset,
                                                    const Prologue &P) {
  // If the length field is not valid, we don't know where the next table is, so
  // cannot continue to parse. Mark the parser as done, and leave the Offset
  // value as it currently is. This will be the end of the bad length field.
  if (!P.totalLengthIsValid()) {
    Done = true;
    return;
  }

  Offset = OldOffset + P.TotalLength + P.sizeofTotalLength();
  if (!DebugLineData.isValidOffset(Offset)) {
    Done = true;
  }
}
