//===--- ARMAttributeParser.cpp - ARM Attribute Information Printer -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/ARMAttributeParser.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/ScopedPrinter.h"

using namespace llvm;
using namespace llvm::ARMBuildAttrs;


static const EnumEntry<unsigned> TagNames[] = {
  { "Tag_File", ARMBuildAttrs::File },
  { "Tag_Section", ARMBuildAttrs::Section },
  { "Tag_Symbol", ARMBuildAttrs::Symbol },
};

namespace llvm {
#define ATTRIBUTE_HANDLER(Attr_)                                                \
  { ARMBuildAttrs::Attr_, &ARMAttributeParser::Attr_ }

const ARMAttributeParser::DisplayHandler
ARMAttributeParser::DisplayRoutines[] = {
  { ARMBuildAttrs::CPU_raw_name, &ARMAttributeParser::StringAttribute, },
  { ARMBuildAttrs::CPU_name, &ARMAttributeParser::StringAttribute },
  ATTRIBUTE_HANDLER(CPU_arch),
  ATTRIBUTE_HANDLER(CPU_arch_profile),
  ATTRIBUTE_HANDLER(ARM_ISA_use),
  ATTRIBUTE_HANDLER(THUMB_ISA_use),
  ATTRIBUTE_HANDLER(FP_arch),
  ATTRIBUTE_HANDLER(WMMX_arch),
  ATTRIBUTE_HANDLER(Advanced_SIMD_arch),
  ATTRIBUTE_HANDLER(PCS_config),
  ATTRIBUTE_HANDLER(ABI_PCS_R9_use),
  ATTRIBUTE_HANDLER(ABI_PCS_RW_data),
  ATTRIBUTE_HANDLER(ABI_PCS_RO_data),
  ATTRIBUTE_HANDLER(ABI_PCS_GOT_use),
  ATTRIBUTE_HANDLER(ABI_PCS_wchar_t),
  ATTRIBUTE_HANDLER(ABI_FP_rounding),
  ATTRIBUTE_HANDLER(ABI_FP_denormal),
  ATTRIBUTE_HANDLER(ABI_FP_exceptions),
  ATTRIBUTE_HANDLER(ABI_FP_user_exceptions),
  ATTRIBUTE_HANDLER(ABI_FP_number_model),
  ATTRIBUTE_HANDLER(ABI_align_needed),
  ATTRIBUTE_HANDLER(ABI_align_preserved),
  ATTRIBUTE_HANDLER(ABI_enum_size),
  ATTRIBUTE_HANDLER(ABI_HardFP_use),
  ATTRIBUTE_HANDLER(ABI_VFP_args),
  ATTRIBUTE_HANDLER(ABI_WMMX_args),
  ATTRIBUTE_HANDLER(ABI_optimization_goals),
  ATTRIBUTE_HANDLER(ABI_FP_optimization_goals),
  ATTRIBUTE_HANDLER(compatibility),
  ATTRIBUTE_HANDLER(CPU_unaligned_access),
  ATTRIBUTE_HANDLER(FP_HP_extension),
  ATTRIBUTE_HANDLER(ABI_FP_16bit_format),
  ATTRIBUTE_HANDLER(MPextension_use),
  ATTRIBUTE_HANDLER(DIV_use),
  ATTRIBUTE_HANDLER(DSP_extension),
  ATTRIBUTE_HANDLER(T2EE_use),
  ATTRIBUTE_HANDLER(Virtualization_use),
  ATTRIBUTE_HANDLER(nodefaults)
};

#undef ATTRIBUTE_HANDLER

uint64_t ARMAttributeParser::ParseInteger(const uint8_t *Data,
                                          uint32_t &Offset) {
  unsigned Length;
  uint64_t Value = decodeULEB128(Data + Offset, &Length);
  Offset = Offset + Length;
  return Value;
}

StringRef ARMAttributeParser::ParseString(const uint8_t *Data,
                                          uint32_t &Offset) {
  const char *String = reinterpret_cast<const char*>(Data + Offset);
  size_t Length = std::strlen(String);
  Offset = Offset + Length + 1;
  return StringRef(String, Length);
}

void ARMAttributeParser::IntegerAttribute(AttrType Tag, const uint8_t *Data,
                                          uint32_t &Offset) {

  uint64_t Value = ParseInteger(Data, Offset);
  Attributes.insert(std::make_pair(Tag, Value));

  if (SW)
    SW->printNumber(ARMBuildAttrs::AttrTypeAsString(Tag), Value);
}

void ARMAttributeParser::StringAttribute(AttrType Tag, const uint8_t *Data,
                                         uint32_t &Offset) {
  StringRef TagName = ARMBuildAttrs::AttrTypeAsString(Tag, /*TagPrefix*/false);
  StringRef ValueDesc = ParseString(Data, Offset);

  if (SW) {
    DictScope AS(*SW, "Attribute");
    SW->printNumber("Tag", Tag);
    if (!TagName.empty())
      SW->printString("TagName", TagName);
    SW->printString("Value", ValueDesc);
  }
}

void ARMAttributeParser::PrintAttribute(unsigned Tag, unsigned Value,
                                        StringRef ValueDesc) {
  Attributes.insert(std::make_pair(Tag, Value));

  if (SW) {
    StringRef TagName = ARMBuildAttrs::AttrTypeAsString(Tag,
                                                        /*TagPrefix*/false);
    DictScope AS(*SW, "Attribute");
    SW->printNumber("Tag", Tag);
    SW->printNumber("Value", Value);
    if (!TagName.empty())
      SW->printString("TagName", TagName);
    if (!ValueDesc.empty())
      SW->printString("Description", ValueDesc);
  }
}

void ARMAttributeParser::CPU_arch(AttrType Tag, const uint8_t *Data,
                                  uint32_t &Offset) {
  static const char *const Strings[] = {
    "Pre-v4", "ARM v4", "ARM v4T", "ARM v5T", "ARM v5TE", "ARM v5TEJ", "ARM v6",
    "ARM v6KZ", "ARM v6T2", "ARM v6K", "ARM v7", "ARM v6-M", "ARM v6S-M",
    "ARM v7E-M", "ARM v8"
  };

  uint64_t Value = ParseInteger(Data, Offset);
  StringRef ValueDesc =
    (Value < array_lengthof(Strings)) ? Strings[Value] : nullptr;
  PrintAttribute(Tag, Value, ValueDesc);
}

void ARMAttributeParser::CPU_arch_profile(AttrType Tag, const uint8_t *Data,
                                          uint32_t &Offset) {
  uint64_t Encoded = ParseInteger(Data, Offset);

  StringRef Profile;
  switch (Encoded) {
  default:  Profile = "Unknown"; break;
  case 'A': Profile = "Application"; break;
  case 'R': Profile = "Real-time"; break;
  case 'M': Profile = "Microcontroller"; break;
  case 'S': Profile = "Classic"; break;
  case 0: Profile = "None"; break;
  }

  PrintAttribute(Tag, Encoded, Profile);
}

void ARMAttributeParser::ARM_ISA_use(AttrType Tag, const uint8_t *Data,
                                     uint32_t &Offset) {
  static const char *const Strings[] = { "Not Permitted", "Permitted" };

  uint64_t Value = ParseInteger(Data, Offset);
  StringRef ValueDesc =
    (Value < array_lengthof(Strings)) ? Strings[Value] : nullptr;
  PrintAttribute(Tag, Value, ValueDesc);
}

void ARMAttributeParser::THUMB_ISA_use(AttrType Tag, const uint8_t *Data,
                                       uint32_t &Offset) {
  static const char *const Strings[] = { "Not Permitted", "Thumb-1", "Thumb-2" };

  uint64_t Value = ParseInteger(Data, Offset);
  StringRef ValueDesc =
    (Value < array_lengthof(Strings)) ? Strings[Value] : nullptr;
  PrintAttribute(Tag, Value, ValueDesc);
}

void ARMAttributeParser::FP_arch(AttrType Tag, const uint8_t *Data,
                                 uint32_t &Offset) {
  static const char *const Strings[] = {
    "Not Permitted", "VFPv1", "VFPv2", "VFPv3", "VFPv3-D16", "VFPv4",
    "VFPv4-D16", "ARMv8-a FP", "ARMv8-a FP-D16"
  };

  uint64_t Value = ParseInteger(Data, Offset);
  StringRef ValueDesc =
    (Value < array_lengthof(Strings)) ? Strings[Value] : nullptr;
  PrintAttribute(Tag, Value, ValueDesc);
}

void ARMAttributeParser::WMMX_arch(AttrType Tag, const uint8_t *Data,
                                   uint32_t &Offset) {
  static const char *const Strings[] = { "Not Permitted", "WMMXv1", "WMMXv2" };

  uint64_t Value = ParseInteger(Data, Offset);
  StringRef ValueDesc =
    (Value < array_lengthof(Strings)) ? Strings[Value] : nullptr;
  PrintAttribute(Tag, Value, ValueDesc);
}

void ARMAttributeParser::Advanced_SIMD_arch(AttrType Tag, const uint8_t *Data,
                                            uint32_t &Offset) {
  static const char *const Strings[] = {
    "Not Permitted", "NEONv1", "NEONv2+FMA", "ARMv8-a NEON", "ARMv8.1-a NEON"
  };

  uint64_t Value = ParseInteger(Data, Offset);
  StringRef ValueDesc =
    (Value < array_lengthof(Strings)) ? Strings[Value] : nullptr;
  PrintAttribute(Tag, Value, ValueDesc);
}

void ARMAttributeParser::PCS_config(AttrType Tag, const uint8_t *Data,
                                    uint32_t &Offset) {
  static const char *const Strings[] = {
    "None", "Bare Platform", "Linux Application", "Linux DSO", "Palm OS 2004",
    "Reserved (Palm OS)", "Symbian OS 2004", "Reserved (Symbian OS)"
  };

  uint64_t Value = ParseInteger(Data, Offset);
  StringRef ValueDesc =
    (Value < array_lengthof(Strings)) ? Strings[Value] : nullptr;
  PrintAttribute(Tag, Value, ValueDesc);
}

void ARMAttributeParser::ABI_PCS_R9_use(AttrType Tag, const uint8_t *Data,
                                        uint32_t &Offset) {
  static const char *const Strings[] = { "v6", "Static Base", "TLS", "Unused" };

  uint64_t Value = ParseInteger(Data, Offset);
  StringRef ValueDesc =
    (Value < array_lengthof(Strings)) ? Strings[Value] : nullptr;
  PrintAttribute(Tag, Value, ValueDesc);
}

void ARMAttributeParser::ABI_PCS_RW_data(AttrType Tag, const uint8_t *Data,
                                         uint32_t &Offset) {
  static const char *const Strings[] = {
    "Absolute", "PC-relative", "SB-relative", "Not Permitted"
  };

  uint64_t Value = ParseInteger(Data, Offset);
  StringRef ValueDesc =
    (Value < array_lengthof(Strings)) ? Strings[Value] : nullptr;
  PrintAttribute(Tag, Value, ValueDesc);
}

void ARMAttributeParser::ABI_PCS_RO_data(AttrType Tag, const uint8_t *Data,
                                         uint32_t &Offset) {
  static const char *const Strings[] = {
    "Absolute", "PC-relative", "Not Permitted"
  };

  uint64_t Value = ParseInteger(Data, Offset);
  StringRef ValueDesc =
    (Value < array_lengthof(Strings)) ? Strings[Value] : nullptr;
  PrintAttribute(Tag, Value, ValueDesc);
}

void ARMAttributeParser::ABI_PCS_GOT_use(AttrType Tag, const uint8_t *Data,
                                         uint32_t &Offset) {
  static const char *const Strings[] = {
    "Not Permitted", "Direct", "GOT-Indirect"
  };

  uint64_t Value = ParseInteger(Data, Offset);
  StringRef ValueDesc =
    (Value < array_lengthof(Strings)) ? Strings[Value] : nullptr;
  PrintAttribute(Tag, Value, ValueDesc);
}

void ARMAttributeParser::ABI_PCS_wchar_t(AttrType Tag, const uint8_t *Data,
                                         uint32_t &Offset) {
  static const char *const Strings[] = {
    "Not Permitted", "Unknown", "2-byte", "Unknown", "4-byte"
  };

  uint64_t Value = ParseInteger(Data, Offset);
  StringRef ValueDesc =
    (Value < array_lengthof(Strings)) ? Strings[Value] : nullptr;
  PrintAttribute(Tag, Value, ValueDesc);
}

void ARMAttributeParser::ABI_FP_rounding(AttrType Tag, const uint8_t *Data,
                                         uint32_t &Offset) {
  static const char *const Strings[] = { "IEEE-754", "Runtime" };

  uint64_t Value = ParseInteger(Data, Offset);
  StringRef ValueDesc =
    (Value < array_lengthof(Strings)) ? Strings[Value] : nullptr;
  PrintAttribute(Tag, Value, ValueDesc);
}

void ARMAttributeParser::ABI_FP_denormal(AttrType Tag, const uint8_t *Data,
                                         uint32_t &Offset) {
  static const char *const Strings[] = {
    "Unsupported", "IEEE-754", "Sign Only"
  };

  uint64_t Value = ParseInteger(Data, Offset);
  StringRef ValueDesc =
    (Value < array_lengthof(Strings)) ? Strings[Value] : nullptr;
  PrintAttribute(Tag, Value, ValueDesc);
}

void ARMAttributeParser::ABI_FP_exceptions(AttrType Tag, const uint8_t *Data,
                                           uint32_t &Offset) {
  static const char *const Strings[] = { "Not Permitted", "IEEE-754" };

  uint64_t Value = ParseInteger(Data, Offset);
  StringRef ValueDesc =
    (Value < array_lengthof(Strings)) ? Strings[Value] : nullptr;
  PrintAttribute(Tag, Value, ValueDesc);
}

void ARMAttributeParser::ABI_FP_user_exceptions(AttrType Tag,
                                                const uint8_t *Data,
                                                uint32_t &Offset) {
  static const char *const Strings[] = { "Not Permitted", "IEEE-754" };

  uint64_t Value = ParseInteger(Data, Offset);
  StringRef ValueDesc =
    (Value < array_lengthof(Strings)) ? Strings[Value] : nullptr;
  PrintAttribute(Tag, Value, ValueDesc);
}

void ARMAttributeParser::ABI_FP_number_model(AttrType Tag, const uint8_t *Data,
                                             uint32_t &Offset) {
  static const char *const Strings[] = {
    "Not Permitted", "Finite Only", "RTABI", "IEEE-754"
  };

  uint64_t Value = ParseInteger(Data, Offset);
  StringRef ValueDesc =
    (Value < array_lengthof(Strings)) ? Strings[Value] : nullptr;
  PrintAttribute(Tag, Value, ValueDesc);
}

void ARMAttributeParser::ABI_align_needed(AttrType Tag, const uint8_t *Data,
                                          uint32_t &Offset) {
  static const char *const Strings[] = {
    "Not Permitted", "8-byte alignment", "4-byte alignment", "Reserved"
  };

  uint64_t Value = ParseInteger(Data, Offset);

  std::string Description;
  if (Value < array_lengthof(Strings))
    Description = std::string(Strings[Value]);
  else if (Value <= 12)
    Description = std::string("8-byte alignment, ") + utostr(1ULL << Value)
                + std::string("-byte extended alignment");
  else
    Description = "Invalid";

  PrintAttribute(Tag, Value, Description);
}

void ARMAttributeParser::ABI_align_preserved(AttrType Tag, const uint8_t *Data,
                                             uint32_t &Offset) {
  static const char *const Strings[] = {
    "Not Required", "8-byte data alignment", "8-byte data and code alignment",
    "Reserved"
  };

  uint64_t Value = ParseInteger(Data, Offset);

  std::string Description;
  if (Value < array_lengthof(Strings))
    Description = std::string(Strings[Value]);
  else if (Value <= 12)
    Description = std::string("8-byte stack alignment, ") +
                  utostr(1ULL << Value) + std::string("-byte data alignment");
  else
    Description = "Invalid";

  PrintAttribute(Tag, Value, Description);
}

void ARMAttributeParser::ABI_enum_size(AttrType Tag, const uint8_t *Data,
                                       uint32_t &Offset) {
  static const char *const Strings[] = {
    "Not Permitted", "Packed", "Int32", "External Int32"
  };

  uint64_t Value = ParseInteger(Data, Offset);
  StringRef ValueDesc =
    (Value < array_lengthof(Strings)) ? Strings[Value] : nullptr;
  PrintAttribute(Tag, Value, ValueDesc);
}

void ARMAttributeParser::ABI_HardFP_use(AttrType Tag, const uint8_t *Data,
                                        uint32_t &Offset) {
  static const char *const Strings[] = {
    "Tag_FP_arch", "Single-Precision", "Reserved", "Tag_FP_arch (deprecated)"
  };

  uint64_t Value = ParseInteger(Data, Offset);
  StringRef ValueDesc =
    (Value < array_lengthof(Strings)) ? Strings[Value] : nullptr;
  PrintAttribute(Tag, Value, ValueDesc);
}

void ARMAttributeParser::ABI_VFP_args(AttrType Tag, const uint8_t *Data,
                                      uint32_t &Offset) {
  static const char *const Strings[] = {
    "AAPCS", "AAPCS VFP", "Custom", "Not Permitted"
  };

  uint64_t Value = ParseInteger(Data, Offset);
  StringRef ValueDesc =
    (Value < array_lengthof(Strings)) ? Strings[Value] : nullptr;
  PrintAttribute(Tag, Value, ValueDesc);
}

void ARMAttributeParser::ABI_WMMX_args(AttrType Tag, const uint8_t *Data,
                                       uint32_t &Offset) {
  static const char *const Strings[] = { "AAPCS", "iWMMX", "Custom" };

  uint64_t Value = ParseInteger(Data, Offset);
  StringRef ValueDesc =
    (Value < array_lengthof(Strings)) ? Strings[Value] : nullptr;
  PrintAttribute(Tag, Value, ValueDesc);
}

void ARMAttributeParser::ABI_optimization_goals(AttrType Tag,
                                                const uint8_t *Data,
                                                uint32_t &Offset) {
  static const char *const Strings[] = {
    "None", "Speed", "Aggressive Speed", "Size", "Aggressive Size", "Debugging",
    "Best Debugging"
  };

  uint64_t Value = ParseInteger(Data, Offset);
  StringRef ValueDesc =
    (Value < array_lengthof(Strings)) ? Strings[Value] : nullptr;
  PrintAttribute(Tag, Value, ValueDesc);
}

void ARMAttributeParser::ABI_FP_optimization_goals(AttrType Tag,
                                                   const uint8_t *Data,
                                                   uint32_t &Offset) {
  static const char *const Strings[] = {
    "None", "Speed", "Aggressive Speed", "Size", "Aggressive Size", "Accuracy",
    "Best Accuracy"
  };

  uint64_t Value = ParseInteger(Data, Offset);
  StringRef ValueDesc =
    (Value < array_lengthof(Strings)) ? Strings[Value] : nullptr;
  PrintAttribute(Tag, Value, ValueDesc);
}

void ARMAttributeParser::compatibility(AttrType Tag, const uint8_t *Data,
                                       uint32_t &Offset) {
  uint64_t Integer = ParseInteger(Data, Offset);
  StringRef String = ParseString(Data, Offset);

  if (SW) {
    DictScope AS(*SW, "Attribute");
    SW->printNumber("Tag", Tag);
    SW->startLine() << "Value: " << Integer << ", " << String << '\n';
    SW->printString("TagName", AttrTypeAsString(Tag, /*TagPrefix*/false));
    switch (Integer) {
    case 0:
      SW->printString("Description", StringRef("No Specific Requirements"));
      break;
    case 1:
      SW->printString("Description", StringRef("AEABI Conformant"));
      break;
    default:
      SW->printString("Description", StringRef("AEABI Non-Conformant"));
      break;
    }
  }
}

void ARMAttributeParser::CPU_unaligned_access(AttrType Tag, const uint8_t *Data,
                                              uint32_t &Offset) {
  static const char *const Strings[] = { "Not Permitted", "v6-style" };

  uint64_t Value = ParseInteger(Data, Offset);
  StringRef ValueDesc =
    (Value < array_lengthof(Strings)) ? Strings[Value] : nullptr;
  PrintAttribute(Tag, Value, ValueDesc);
}

void ARMAttributeParser::FP_HP_extension(AttrType Tag, const uint8_t *Data,
                                         uint32_t &Offset) {
  static const char *const Strings[] = { "If Available", "Permitted" };

  uint64_t Value = ParseInteger(Data, Offset);
  StringRef ValueDesc =
    (Value < array_lengthof(Strings)) ? Strings[Value] : nullptr;
  PrintAttribute(Tag, Value, ValueDesc);
}

void ARMAttributeParser::ABI_FP_16bit_format(AttrType Tag, const uint8_t *Data,
                                             uint32_t &Offset) {
  static const char *const Strings[] = { "Not Permitted", "IEEE-754", "VFPv3" };

  uint64_t Value = ParseInteger(Data, Offset);
  StringRef ValueDesc =
    (Value < array_lengthof(Strings)) ? Strings[Value] : nullptr;
  PrintAttribute(Tag, Value, ValueDesc);
}

void ARMAttributeParser::MPextension_use(AttrType Tag, const uint8_t *Data,
                                         uint32_t &Offset) {
  static const char *const Strings[] = { "Not Permitted", "Permitted" };

  uint64_t Value = ParseInteger(Data, Offset);
  StringRef ValueDesc =
    (Value < array_lengthof(Strings)) ? Strings[Value] : nullptr;
  PrintAttribute(Tag, Value, ValueDesc);
}

void ARMAttributeParser::DIV_use(AttrType Tag, const uint8_t *Data,
                                 uint32_t &Offset) {
  static const char *const Strings[] = {
    "If Available", "Not Permitted", "Permitted"
  };

  uint64_t Value = ParseInteger(Data, Offset);
  StringRef ValueDesc =
    (Value < array_lengthof(Strings)) ? Strings[Value] : nullptr;
  PrintAttribute(Tag, Value, ValueDesc);
}

void ARMAttributeParser::DSP_extension(AttrType Tag, const uint8_t *Data,
                                       uint32_t &Offset) {
  static const char *const Strings[] = { "Not Permitted", "Permitted" };

  uint64_t Value = ParseInteger(Data, Offset);
  StringRef ValueDesc =
    (Value < array_lengthof(Strings)) ? Strings[Value] : nullptr;
  PrintAttribute(Tag, Value, ValueDesc);
}

void ARMAttributeParser::T2EE_use(AttrType Tag, const uint8_t *Data,
                                  uint32_t &Offset) {
  static const char *const Strings[] = { "Not Permitted", "Permitted" };

  uint64_t Value = ParseInteger(Data, Offset);
  StringRef ValueDesc =
    (Value < array_lengthof(Strings)) ? Strings[Value] : nullptr;
  PrintAttribute(Tag, Value, ValueDesc);
}

void ARMAttributeParser::Virtualization_use(AttrType Tag, const uint8_t *Data,
                                            uint32_t &Offset) {
  static const char *const Strings[] = {
    "Not Permitted", "TrustZone", "Virtualization Extensions",
    "TrustZone + Virtualization Extensions"
  };

  uint64_t Value = ParseInteger(Data, Offset);
  StringRef ValueDesc =
    (Value < array_lengthof(Strings)) ? Strings[Value] : nullptr;
  PrintAttribute(Tag, Value, ValueDesc);
}

void ARMAttributeParser::nodefaults(AttrType Tag, const uint8_t *Data,
                                    uint32_t &Offset) {
  uint64_t Value = ParseInteger(Data, Offset);
  PrintAttribute(Tag, Value, "Unspecified Tags UNDEFINED");
}

void ARMAttributeParser::ParseIndexList(const uint8_t *Data, uint32_t &Offset,
                                        SmallVectorImpl<uint8_t> &IndexList) {
  for (;;) {
    unsigned Length;
    uint64_t Value = decodeULEB128(Data + Offset, &Length);
    Offset = Offset + Length;
    if (Value == 0)
      break;
    IndexList.push_back(Value);
  }
}

void ARMAttributeParser::ParseAttributeList(const uint8_t *Data,
                                            uint32_t &Offset, uint32_t Length) {
  while (Offset < Length) {
    unsigned Length;
    uint64_t Tag = decodeULEB128(Data + Offset, &Length);
    Offset += Length;

    bool Handled = false;
    for (unsigned AHI = 0, AHE = array_lengthof(DisplayRoutines);
         AHI != AHE && !Handled; ++AHI) {
      if (uint64_t(DisplayRoutines[AHI].Attribute) == Tag) {
        (this->*DisplayRoutines[AHI].Routine)(ARMBuildAttrs::AttrType(Tag),
                                              Data, Offset);
        Handled = true;
        break;
      }
    }
    if (!Handled) {
      if (Tag < 32) {
        errs() << "unhandled AEABI Tag " << Tag
               << " (" << ARMBuildAttrs::AttrTypeAsString(Tag) << ")\n";
        continue;
      }

      if (Tag % 2 == 0)
        IntegerAttribute(ARMBuildAttrs::AttrType(Tag), Data, Offset);
      else
        StringAttribute(ARMBuildAttrs::AttrType(Tag), Data, Offset);
    }
  }
}

void ARMAttributeParser::ParseSubsection(const uint8_t *Data, uint32_t Length) {
  uint32_t Offset = sizeof(uint32_t); /* SectionLength */

  const char *VendorName = reinterpret_cast<const char*>(Data + Offset);
  size_t VendorNameLength = std::strlen(VendorName);
  Offset = Offset + VendorNameLength + 1;

  if (SW) {
    SW->printNumber("SectionLength", Length);
    SW->printString("Vendor", StringRef(VendorName, VendorNameLength));
  }

  if (StringRef(VendorName, VendorNameLength).lower() != "aeabi") {
    return;
  }

  while (Offset < Length) {
    /// Tag_File | Tag_Section | Tag_Symbol   uleb128:byte-size
    uint8_t Tag = Data[Offset];
    Offset = Offset + sizeof(Tag);

    uint32_t Size =
      *reinterpret_cast<const support::ulittle32_t*>(Data + Offset);
    Offset = Offset + sizeof(Size);

    if (SW) {
      SW->printEnum("Tag", Tag, makeArrayRef(TagNames));
      SW->printNumber("Size", Size);
    }

    if (Size > Length) {
      errs() << "subsection length greater than section length\n";
      return;
    }

    StringRef ScopeName, IndexName;
    SmallVector<uint8_t, 8> Indicies;
    switch (Tag) {
    case ARMBuildAttrs::File:
      ScopeName = "FileAttributes";
      break;
    case ARMBuildAttrs::Section:
      ScopeName = "SectionAttributes";
      IndexName = "Sections";
      ParseIndexList(Data, Offset, Indicies);
      break;
    case ARMBuildAttrs::Symbol:
      ScopeName = "SymbolAttributes";
      IndexName = "Symbols";
      ParseIndexList(Data, Offset, Indicies);
      break;
    default:
      errs() << "unrecognised tag: 0x" << Twine::utohexstr(Tag) << '\n';
      return;
    }

    if (SW) {
      DictScope ASS(*SW, ScopeName);
      if (!Indicies.empty())
        SW->printList(IndexName, Indicies);
      ParseAttributeList(Data, Offset, Length);
    } else {
      ParseAttributeList(Data, Offset, Length);
    }
  }
}

void ARMAttributeParser::Parse(ArrayRef<uint8_t> Section, bool isLittle) {
  size_t Offset = 1;
  unsigned SectionNumber = 0;

  while (Offset < Section.size()) {
    uint32_t SectionLength = isLittle ?
      support::endian::read32le(Section.data() + Offset) :
      support::endian::read32be(Section.data() + Offset);

    if (SW) {
      SW->startLine() << "Section " << ++SectionNumber << " {\n";
      SW->indent();
    }

    ParseSubsection(Section.data() + Offset, SectionLength);
    Offset = Offset + SectionLength;

    if (SW) {
      SW->unindent();
      SW->startLine() << "}\n";
    }
  }
}
}
