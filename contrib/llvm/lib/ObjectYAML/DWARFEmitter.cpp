//===- DWARFEmitter - Convert YAML to DWARF binary data -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// The DWARF component of yaml2obj. Provided as library code for tests.
///
//===----------------------------------------------------------------------===//

#include "llvm/ObjectYAML/DWARFEmitter.h"
#include "DWARFVisitor.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ObjectYAML/DWARFYAML.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SwapByteOrder.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace llvm;

template <typename T>
static void writeInteger(T Integer, raw_ostream &OS, bool IsLittleEndian) {
  if (IsLittleEndian != sys::IsLittleEndianHost)
    sys::swapByteOrder(Integer);
  OS.write(reinterpret_cast<char *>(&Integer), sizeof(T));
}

static void writeVariableSizedInteger(uint64_t Integer, size_t Size,
                                      raw_ostream &OS, bool IsLittleEndian) {
  if (8 == Size)
    writeInteger((uint64_t)Integer, OS, IsLittleEndian);
  else if (4 == Size)
    writeInteger((uint32_t)Integer, OS, IsLittleEndian);
  else if (2 == Size)
    writeInteger((uint16_t)Integer, OS, IsLittleEndian);
  else if (1 == Size)
    writeInteger((uint8_t)Integer, OS, IsLittleEndian);
  else
    assert(false && "Invalid integer write size.");
}

static void ZeroFillBytes(raw_ostream &OS, size_t Size) {
  std::vector<uint8_t> FillData;
  FillData.insert(FillData.begin(), Size, 0);
  OS.write(reinterpret_cast<char *>(FillData.data()), Size);
}

static void writeInitialLength(const DWARFYAML::InitialLength &Length,
                               raw_ostream &OS, bool IsLittleEndian) {
  writeInteger((uint32_t)Length.TotalLength, OS, IsLittleEndian);
  if (Length.isDWARF64())
    writeInteger((uint64_t)Length.TotalLength64, OS, IsLittleEndian);
}

void DWARFYAML::EmitDebugStr(raw_ostream &OS, const DWARFYAML::Data &DI) {
  for (auto Str : DI.DebugStrings) {
    OS.write(Str.data(), Str.size());
    OS.write('\0');
  }
}

void DWARFYAML::EmitDebugAbbrev(raw_ostream &OS, const DWARFYAML::Data &DI) {
  for (auto AbbrevDecl : DI.AbbrevDecls) {
    encodeULEB128(AbbrevDecl.Code, OS);
    encodeULEB128(AbbrevDecl.Tag, OS);
    OS.write(AbbrevDecl.Children);
    for (auto Attr : AbbrevDecl.Attributes) {
      encodeULEB128(Attr.Attribute, OS);
      encodeULEB128(Attr.Form, OS);
      if (Attr.Form == dwarf::DW_FORM_implicit_const)
        encodeSLEB128(Attr.Value, OS);
    }
    encodeULEB128(0, OS);
    encodeULEB128(0, OS);
  }
}

void DWARFYAML::EmitDebugAranges(raw_ostream &OS, const DWARFYAML::Data &DI) {
  for (auto Range : DI.ARanges) {
    auto HeaderStart = OS.tell();
    writeInitialLength(Range.Length, OS, DI.IsLittleEndian);
    writeInteger((uint16_t)Range.Version, OS, DI.IsLittleEndian);
    writeInteger((uint32_t)Range.CuOffset, OS, DI.IsLittleEndian);
    writeInteger((uint8_t)Range.AddrSize, OS, DI.IsLittleEndian);
    writeInteger((uint8_t)Range.SegSize, OS, DI.IsLittleEndian);

    auto HeaderSize = OS.tell() - HeaderStart;
    auto FirstDescriptor = alignTo(HeaderSize, Range.AddrSize * 2);
    ZeroFillBytes(OS, FirstDescriptor - HeaderSize);

    for (auto Descriptor : Range.Descriptors) {
      writeVariableSizedInteger(Descriptor.Address, Range.AddrSize, OS,
                                DI.IsLittleEndian);
      writeVariableSizedInteger(Descriptor.Length, Range.AddrSize, OS,
                                DI.IsLittleEndian);
    }
    ZeroFillBytes(OS, Range.AddrSize * 2);
  }
}

void DWARFYAML::EmitPubSection(raw_ostream &OS,
                               const DWARFYAML::PubSection &Sect,
                               bool IsLittleEndian) {
  writeInitialLength(Sect.Length, OS, IsLittleEndian);
  writeInteger((uint16_t)Sect.Version, OS, IsLittleEndian);
  writeInteger((uint32_t)Sect.UnitOffset, OS, IsLittleEndian);
  writeInteger((uint32_t)Sect.UnitSize, OS, IsLittleEndian);
  for (auto Entry : Sect.Entries) {
    writeInteger((uint32_t)Entry.DieOffset, OS, IsLittleEndian);
    if (Sect.IsGNUStyle)
      writeInteger((uint32_t)Entry.Descriptor, OS, IsLittleEndian);
    OS.write(Entry.Name.data(), Entry.Name.size());
    OS.write('\0');
  }
}

namespace {
/// An extension of the DWARFYAML::ConstVisitor which writes compile
/// units and DIEs to a stream.
class DumpVisitor : public DWARFYAML::ConstVisitor {
  raw_ostream &OS;

protected:
  void onStartCompileUnit(const DWARFYAML::Unit &CU) override {
    writeInitialLength(CU.Length, OS, DebugInfo.IsLittleEndian);
    writeInteger((uint16_t)CU.Version, OS, DebugInfo.IsLittleEndian);
    if(CU.Version >= 5) {
      writeInteger((uint8_t)CU.Type, OS, DebugInfo.IsLittleEndian);
      writeInteger((uint8_t)CU.AddrSize, OS, DebugInfo.IsLittleEndian);
      writeInteger((uint32_t)CU.AbbrOffset, OS, DebugInfo.IsLittleEndian);
    }else {
      writeInteger((uint32_t)CU.AbbrOffset, OS, DebugInfo.IsLittleEndian);
      writeInteger((uint8_t)CU.AddrSize, OS, DebugInfo.IsLittleEndian);
    }
  }

  void onStartDIE(const DWARFYAML::Unit &CU,
                  const DWARFYAML::Entry &DIE) override {
    encodeULEB128(DIE.AbbrCode, OS);
  }

  void onValue(const uint8_t U) override {
    writeInteger(U, OS, DebugInfo.IsLittleEndian);
  }

  void onValue(const uint16_t U) override {
    writeInteger(U, OS, DebugInfo.IsLittleEndian);
  }

  void onValue(const uint32_t U) override {
    writeInteger(U, OS, DebugInfo.IsLittleEndian);
  }

  void onValue(const uint64_t U, const bool LEB = false) override {
    if (LEB)
      encodeULEB128(U, OS);
    else
      writeInteger(U, OS, DebugInfo.IsLittleEndian);
  }

  void onValue(const int64_t S, const bool LEB = false) override {
    if (LEB)
      encodeSLEB128(S, OS);
    else
      writeInteger(S, OS, DebugInfo.IsLittleEndian);
  }

  void onValue(const StringRef String) override {
    OS.write(String.data(), String.size());
    OS.write('\0');
  }

  void onValue(const MemoryBufferRef MBR) override {
    OS.write(MBR.getBufferStart(), MBR.getBufferSize());
  }

public:
  DumpVisitor(const DWARFYAML::Data &DI, raw_ostream &Out)
      : DWARFYAML::ConstVisitor(DI), OS(Out) {}
};
} // namespace

void DWARFYAML::EmitDebugInfo(raw_ostream &OS, const DWARFYAML::Data &DI) {
  DumpVisitor Visitor(DI, OS);
  Visitor.traverseDebugInfo();
}

static void EmitFileEntry(raw_ostream &OS, const DWARFYAML::File &File) {
  OS.write(File.Name.data(), File.Name.size());
  OS.write('\0');
  encodeULEB128(File.DirIdx, OS);
  encodeULEB128(File.ModTime, OS);
  encodeULEB128(File.Length, OS);
}

void DWARFYAML::EmitDebugLine(raw_ostream &OS, const DWARFYAML::Data &DI) {
  for (const auto &LineTable : DI.DebugLines) {
    writeInitialLength(LineTable.Length, OS, DI.IsLittleEndian);
    uint64_t SizeOfPrologueLength = LineTable.Length.isDWARF64() ? 8 : 4;
    writeInteger((uint16_t)LineTable.Version, OS, DI.IsLittleEndian);
    writeVariableSizedInteger(LineTable.PrologueLength, SizeOfPrologueLength,
                              OS, DI.IsLittleEndian);
    writeInteger((uint8_t)LineTable.MinInstLength, OS, DI.IsLittleEndian);
    if (LineTable.Version >= 4)
      writeInteger((uint8_t)LineTable.MaxOpsPerInst, OS, DI.IsLittleEndian);
    writeInteger((uint8_t)LineTable.DefaultIsStmt, OS, DI.IsLittleEndian);
    writeInteger((uint8_t)LineTable.LineBase, OS, DI.IsLittleEndian);
    writeInteger((uint8_t)LineTable.LineRange, OS, DI.IsLittleEndian);
    writeInteger((uint8_t)LineTable.OpcodeBase, OS, DI.IsLittleEndian);

    for (auto OpcodeLength : LineTable.StandardOpcodeLengths)
      writeInteger((uint8_t)OpcodeLength, OS, DI.IsLittleEndian);

    for (auto IncludeDir : LineTable.IncludeDirs) {
      OS.write(IncludeDir.data(), IncludeDir.size());
      OS.write('\0');
    }
    OS.write('\0');

    for (auto File : LineTable.Files)
      EmitFileEntry(OS, File);
    OS.write('\0');

    for (auto Op : LineTable.Opcodes) {
      writeInteger((uint8_t)Op.Opcode, OS, DI.IsLittleEndian);
      if (Op.Opcode == 0) {
        encodeULEB128(Op.ExtLen, OS);
        writeInteger((uint8_t)Op.SubOpcode, OS, DI.IsLittleEndian);
        switch (Op.SubOpcode) {
        case dwarf::DW_LNE_set_address:
        case dwarf::DW_LNE_set_discriminator:
          writeVariableSizedInteger(Op.Data, DI.CompileUnits[0].AddrSize, OS,
                                    DI.IsLittleEndian);
          break;
        case dwarf::DW_LNE_define_file:
          EmitFileEntry(OS, Op.FileEntry);
          break;
        case dwarf::DW_LNE_end_sequence:
          break;
        default:
          for (auto OpByte : Op.UnknownOpcodeData)
            writeInteger((uint8_t)OpByte, OS, DI.IsLittleEndian);
        }
      } else if (Op.Opcode < LineTable.OpcodeBase) {
        switch (Op.Opcode) {
        case dwarf::DW_LNS_copy:
        case dwarf::DW_LNS_negate_stmt:
        case dwarf::DW_LNS_set_basic_block:
        case dwarf::DW_LNS_const_add_pc:
        case dwarf::DW_LNS_set_prologue_end:
        case dwarf::DW_LNS_set_epilogue_begin:
          break;

        case dwarf::DW_LNS_advance_pc:
        case dwarf::DW_LNS_set_file:
        case dwarf::DW_LNS_set_column:
        case dwarf::DW_LNS_set_isa:
          encodeULEB128(Op.Data, OS);
          break;

        case dwarf::DW_LNS_advance_line:
          encodeSLEB128(Op.SData, OS);
          break;

        case dwarf::DW_LNS_fixed_advance_pc:
          writeInteger((uint16_t)Op.Data, OS, DI.IsLittleEndian);
          break;

        default:
          for (auto OpData : Op.StandardOpcodeData) {
            encodeULEB128(OpData, OS);
          }
        }
      }
    }
  }
}

using EmitFuncType = void (*)(raw_ostream &, const DWARFYAML::Data &);

static void
EmitDebugSectionImpl(const DWARFYAML::Data &DI, EmitFuncType EmitFunc,
                     StringRef Sec,
                     StringMap<std::unique_ptr<MemoryBuffer>> &OutputBuffers) {
  std::string Data;
  raw_string_ostream DebugInfoStream(Data);
  EmitFunc(DebugInfoStream, DI);
  DebugInfoStream.flush();
  if (!Data.empty())
    OutputBuffers[Sec] = MemoryBuffer::getMemBufferCopy(Data);
}

namespace {
class DIEFixupVisitor : public DWARFYAML::Visitor {
  uint64_t Length;

public:
  DIEFixupVisitor(DWARFYAML::Data &DI) : DWARFYAML::Visitor(DI){};

private:
  virtual void onStartCompileUnit(DWARFYAML::Unit &CU) { Length = 7; }

  virtual void onEndCompileUnit(DWARFYAML::Unit &CU) {
    CU.Length.setLength(Length);
  }

  virtual void onStartDIE(DWARFYAML::Unit &CU, DWARFYAML::Entry &DIE) {
    Length += getULEB128Size(DIE.AbbrCode);
  }

  virtual void onValue(const uint8_t U) { Length += 1; }
  virtual void onValue(const uint16_t U) { Length += 2; }
  virtual void onValue(const uint32_t U) { Length += 4; }
  virtual void onValue(const uint64_t U, const bool LEB = false) {
    if (LEB)
      Length += getULEB128Size(U);
    else
      Length += 8;
  }
  virtual void onValue(const int64_t S, const bool LEB = false) {
    if (LEB)
      Length += getSLEB128Size(S);
    else
      Length += 8;
  }
  virtual void onValue(const StringRef String) { Length += String.size() + 1; }

  virtual void onValue(const MemoryBufferRef MBR) {
    Length += MBR.getBufferSize();
  }
};
} // namespace

Expected<StringMap<std::unique_ptr<MemoryBuffer>>>
DWARFYAML::EmitDebugSections(StringRef YAMLString, bool ApplyFixups,
                             bool IsLittleEndian) {
  yaml::Input YIn(YAMLString);

  DWARFYAML::Data DI;
  DI.IsLittleEndian = IsLittleEndian;
  YIn >> DI;
  if (YIn.error())
    return errorCodeToError(YIn.error());

  if (ApplyFixups) {
    DIEFixupVisitor DIFixer(DI);
    DIFixer.traverseDebugInfo();
  }

  StringMap<std::unique_ptr<MemoryBuffer>> DebugSections;
  EmitDebugSectionImpl(DI, &DWARFYAML::EmitDebugInfo, "debug_info",
                       DebugSections);
  EmitDebugSectionImpl(DI, &DWARFYAML::EmitDebugLine, "debug_line",
                       DebugSections);
  EmitDebugSectionImpl(DI, &DWARFYAML::EmitDebugStr, "debug_str",
                       DebugSections);
  EmitDebugSectionImpl(DI, &DWARFYAML::EmitDebugAbbrev, "debug_abbrev",
                       DebugSections);
  EmitDebugSectionImpl(DI, &DWARFYAML::EmitDebugAranges, "debug_aranges",
                       DebugSections);
  return std::move(DebugSections);
}
