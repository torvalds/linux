//===--- RuntimeDyldCOFFThumb.h --- COFF/Thumb specific code ---*- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// COFF thumb support for MC-JIT runtime dynamic linker.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_EXECUTIONENGINE_RUNTIMEDYLD_TARGETS_RUNTIMEDYLDCOFFTHUMB_H
#define LLVM_LIB_EXECUTIONENGINE_RUNTIMEDYLD_TARGETS_RUNTIMEDYLDCOFFTHUMB_H

#include "../RuntimeDyldCOFF.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Object/COFF.h"

#define DEBUG_TYPE "dyld"

namespace llvm {

static bool isThumbFunc(symbol_iterator Symbol, const ObjectFile &Obj,
                        section_iterator Section) {
  Expected<SymbolRef::Type> SymTypeOrErr = Symbol->getType();
  if (!SymTypeOrErr) {
    std::string Buf;
    raw_string_ostream OS(Buf);
    logAllUnhandledErrors(SymTypeOrErr.takeError(), OS);
    OS.flush();
    report_fatal_error(Buf);
  }

  if (*SymTypeOrErr != SymbolRef::ST_Function)
    return false;

  // We check the IMAGE_SCN_MEM_16BIT flag in the section of the symbol to tell
  // if it's thumb or not
  return cast<COFFObjectFile>(Obj).getCOFFSection(*Section)->Characteristics &
         COFF::IMAGE_SCN_MEM_16BIT;
}

class RuntimeDyldCOFFThumb : public RuntimeDyldCOFF {
public:
  RuntimeDyldCOFFThumb(RuntimeDyld::MemoryManager &MM,
                       JITSymbolResolver &Resolver)
      : RuntimeDyldCOFF(MM, Resolver) {}

  unsigned getMaxStubSize() override {
    return 16; // 8-byte load instructions, 4-byte jump, 4-byte padding
  }

  unsigned getStubAlignment() override { return 1; }

  Expected<relocation_iterator>
  processRelocationRef(unsigned SectionID,
                       relocation_iterator RelI,
                       const ObjectFile &Obj,
                       ObjSectionToIDMap &ObjSectionToID,
                       StubMap &Stubs) override {
    auto Symbol = RelI->getSymbol();
    if (Symbol == Obj.symbol_end())
      report_fatal_error("Unknown symbol in relocation");

    Expected<StringRef> TargetNameOrErr = Symbol->getName();
    if (!TargetNameOrErr)
      return TargetNameOrErr.takeError();
    StringRef TargetName = *TargetNameOrErr;

    auto SectionOrErr = Symbol->getSection();
    if (!SectionOrErr)
      return SectionOrErr.takeError();
    auto Section = *SectionOrErr;

    uint64_t RelType = RelI->getType();
    uint64_t Offset = RelI->getOffset();

    // Determine the Addend used to adjust the relocation value.
    uint64_t Addend = 0;
    SectionEntry &AddendSection = Sections[SectionID];
    uintptr_t ObjTarget = AddendSection.getObjAddress() + Offset;
    uint8_t *Displacement = (uint8_t *)ObjTarget;

    switch (RelType) {
    case COFF::IMAGE_REL_ARM_ADDR32:
    case COFF::IMAGE_REL_ARM_ADDR32NB:
    case COFF::IMAGE_REL_ARM_SECREL:
      Addend = readBytesUnaligned(Displacement, 4);
      break;
    default:
      break;
    }

#if !defined(NDEBUG)
    SmallString<32> RelTypeName;
    RelI->getTypeName(RelTypeName);
#endif
    LLVM_DEBUG(dbgs() << "\t\tIn Section " << SectionID << " Offset " << Offset
                      << " RelType: " << RelTypeName << " TargetName: "
                      << TargetName << " Addend " << Addend << "\n");

    unsigned TargetSectionID = -1;
    if (Section == Obj.section_end()) {
      RelocationEntry RE(SectionID, Offset, RelType, 0, -1, 0, 0, 0, false, 0);
      addRelocationForSymbol(RE, TargetName);
    } else {
      if (auto TargetSectionIDOrErr =
          findOrEmitSection(Obj, *Section, Section->isText(), ObjSectionToID))
        TargetSectionID = *TargetSectionIDOrErr;
      else
        return TargetSectionIDOrErr.takeError();

      // We need to find out if the relocation is relative to a thumb function
      // so that we include the ISA selection bit when resolve the relocation
      bool IsTargetThumbFunc = isThumbFunc(Symbol, Obj, Section);

      switch (RelType) {
      default: llvm_unreachable("unsupported relocation type");
      case COFF::IMAGE_REL_ARM_ABSOLUTE:
        // This relocation is ignored.
        break;
      case COFF::IMAGE_REL_ARM_ADDR32: {
        RelocationEntry RE = RelocationEntry(
            SectionID, Offset, RelType, Addend, TargetSectionID,
            getSymbolOffset(*Symbol), 0, 0, false, 0, IsTargetThumbFunc);
        addRelocationForSection(RE, TargetSectionID);
        break;
      }
      case COFF::IMAGE_REL_ARM_ADDR32NB: {
        RelocationEntry RE =
            RelocationEntry(SectionID, Offset, RelType, Addend, TargetSectionID,
                            getSymbolOffset(*Symbol), 0, 0, false, 0);
        addRelocationForSection(RE, TargetSectionID);
        break;
      }
      case COFF::IMAGE_REL_ARM_SECTION: {
        RelocationEntry RE =
            RelocationEntry(TargetSectionID, Offset, RelType, 0);
        addRelocationForSection(RE, TargetSectionID);
        break;
      }
      case COFF::IMAGE_REL_ARM_SECREL: {
        RelocationEntry RE = RelocationEntry(SectionID, Offset, RelType,
                                             getSymbolOffset(*Symbol) + Addend);
        addRelocationForSection(RE, TargetSectionID);
        break;
      }
      case COFF::IMAGE_REL_ARM_MOV32T: {
        RelocationEntry RE = RelocationEntry(
            SectionID, Offset, RelType, Addend, TargetSectionID,
            getSymbolOffset(*Symbol), 0, 0, false, 0, IsTargetThumbFunc);
        addRelocationForSection(RE, TargetSectionID);
        break;
      }
      case COFF::IMAGE_REL_ARM_BRANCH20T:
      case COFF::IMAGE_REL_ARM_BRANCH24T:
      case COFF::IMAGE_REL_ARM_BLX23T: {
        RelocationEntry RE =
            RelocationEntry(SectionID, Offset, RelType,
                            getSymbolOffset(*Symbol) + Addend, true, 0);
        addRelocationForSection(RE, TargetSectionID);
        break;
      }
      }
    }

    return ++RelI;
  }

  void resolveRelocation(const RelocationEntry &RE, uint64_t Value) override {
    const auto Section = Sections[RE.SectionID];
    uint8_t *Target = Section.getAddressWithOffset(RE.Offset);
    int ISASelectionBit = RE.IsTargetThumbFunc ? 1 : 0;

    switch (RE.RelType) {
    default: llvm_unreachable("unsupported relocation type");
    case COFF::IMAGE_REL_ARM_ABSOLUTE:
      // This relocation is ignored.
      break;
    case COFF::IMAGE_REL_ARM_ADDR32: {
      // The target's 32-bit VA.
      uint64_t Result =
          RE.Sections.SectionA == static_cast<uint32_t>(-1)
              ? Value
              : Sections[RE.Sections.SectionA].getLoadAddressWithOffset(RE.Addend);
      Result |= ISASelectionBit;
      assert(Result <= UINT32_MAX && "relocation overflow");
      LLVM_DEBUG(dbgs() << "\t\tOffset: " << RE.Offset
                        << " RelType: IMAGE_REL_ARM_ADDR32"
                        << " TargetSection: " << RE.Sections.SectionA
                        << " Value: " << format("0x%08" PRIx32, Result)
                        << '\n');
      writeBytesUnaligned(Result, Target, 4);
      break;
    }
    case COFF::IMAGE_REL_ARM_ADDR32NB: {
      // The target's 32-bit RVA.
      // NOTE: use Section[0].getLoadAddress() as an approximation of ImageBase
      uint64_t Result = Sections[RE.Sections.SectionA].getLoadAddress() -
                        Sections[0].getLoadAddress() + RE.Addend;
      assert(Result <= UINT32_MAX && "relocation overflow");
      LLVM_DEBUG(dbgs() << "\t\tOffset: " << RE.Offset
                        << " RelType: IMAGE_REL_ARM_ADDR32NB"
                        << " TargetSection: " << RE.Sections.SectionA
                        << " Value: " << format("0x%08" PRIx32, Result)
                        << '\n');
      Result |= ISASelectionBit;
      writeBytesUnaligned(Result, Target, 4);
      break;
    }
    case COFF::IMAGE_REL_ARM_SECTION:
      // 16-bit section index of the section that contains the target.
      assert(static_cast<uint32_t>(RE.SectionID) <= UINT16_MAX &&
             "relocation overflow");
      LLVM_DEBUG(dbgs() << "\t\tOffset: " << RE.Offset
                        << " RelType: IMAGE_REL_ARM_SECTION Value: "
                        << RE.SectionID << '\n');
      writeBytesUnaligned(RE.SectionID, Target, 2);
      break;
    case COFF::IMAGE_REL_ARM_SECREL:
      // 32-bit offset of the target from the beginning of its section.
      assert(static_cast<uint64_t>(RE.Addend) <= UINT32_MAX &&
             "relocation overflow");
      LLVM_DEBUG(dbgs() << "\t\tOffset: " << RE.Offset
                        << " RelType: IMAGE_REL_ARM_SECREL Value: " << RE.Addend
                        << '\n');
      writeBytesUnaligned(RE.Addend, Target, 2);
      break;
    case COFF::IMAGE_REL_ARM_MOV32T: {
      // 32-bit VA of the target applied to a contiguous MOVW+MOVT pair.
      uint64_t Result =
          Sections[RE.Sections.SectionA].getLoadAddressWithOffset(RE.Addend);
      assert(Result <= UINT32_MAX && "relocation overflow");
      LLVM_DEBUG(dbgs() << "\t\tOffset: " << RE.Offset
                        << " RelType: IMAGE_REL_ARM_MOV32T"
                        << " TargetSection: " << RE.Sections.SectionA
                        << " Value: " << format("0x%08" PRIx32, Result)
                        << '\n');

      // MOVW(T3): |11110|i|10|0|1|0|0|imm4|0|imm3|Rd|imm8|
      //            imm32 = zext imm4:i:imm3:imm8
      // MOVT(T1): |11110|i|10|1|1|0|0|imm4|0|imm3|Rd|imm8|
      //            imm16 =      imm4:i:imm3:imm8

      auto EncodeImmediate = [](uint8_t *Bytes, uint16_t Immediate)  {
        Bytes[0] |= ((Immediate & 0xf000) >> 12);
        Bytes[1] |= ((Immediate & 0x0800) >> 11);
        Bytes[2] |= ((Immediate & 0x00ff) >>  0);
        Bytes[3] |= (((Immediate & 0x0700) >>  8) << 4);
      };

      EncodeImmediate(&Target[0],
                      (static_cast<uint32_t>(Result) >> 00) | ISASelectionBit);
      EncodeImmediate(&Target[4], static_cast<uint32_t>(Result) >> 16);

      break;
    }
    case COFF::IMAGE_REL_ARM_BRANCH20T: {
      // The most significant 20-bits of the signed 21-bit relative displacement
      uint64_t Value =
          RE.Addend - (Sections[RE.SectionID].getLoadAddress() + RE.Offset) - 4;
      assert(static_cast<int64_t>(RE.Addend) <= INT32_MAX &&
             "relocation overflow");
      assert(static_cast<int64_t>(RE.Addend) >= INT32_MIN &&
             "relocation underflow");
      LLVM_DEBUG(dbgs() << "\t\tOffset: " << RE.Offset
                        << " RelType: IMAGE_REL_ARM_BRANCH20T"
                        << " Value: " << static_cast<int32_t>(Value) << '\n');
      static_cast<void>(Value);
      llvm_unreachable("unimplemented relocation");
      break;
    }
    case COFF::IMAGE_REL_ARM_BRANCH24T: {
      // The most significant 24-bits of the signed 25-bit relative displacement
      uint64_t Value =
          RE.Addend - (Sections[RE.SectionID].getLoadAddress() + RE.Offset) - 4;
      assert(static_cast<int64_t>(RE.Addend) <= INT32_MAX &&
             "relocation overflow");
      assert(static_cast<int64_t>(RE.Addend) >= INT32_MIN &&
             "relocation underflow");
      LLVM_DEBUG(dbgs() << "\t\tOffset: " << RE.Offset
                        << " RelType: IMAGE_REL_ARM_BRANCH24T"
                        << " Value: " << static_cast<int32_t>(Value) << '\n');
      static_cast<void>(Value);
      llvm_unreachable("unimplemented relocation");
      break;
    }
    case COFF::IMAGE_REL_ARM_BLX23T: {
      // The most significant 24-bits of the signed 25-bit relative displacement
      uint64_t Value =
          RE.Addend - (Sections[RE.SectionID].getLoadAddress() + RE.Offset) - 4;
      assert(static_cast<int64_t>(RE.Addend) <= INT32_MAX &&
             "relocation overflow");
      assert(static_cast<int64_t>(RE.Addend) >= INT32_MIN &&
             "relocation underflow");
      LLVM_DEBUG(dbgs() << "\t\tOffset: " << RE.Offset
                        << " RelType: IMAGE_REL_ARM_BLX23T"
                        << " Value: " << static_cast<int32_t>(Value) << '\n');
      static_cast<void>(Value);
      llvm_unreachable("unimplemented relocation");
      break;
    }
    }
  }

  void registerEHFrames() override {}
};

}

#endif
