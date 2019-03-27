//===--- ARMAttributeParser.h - ARM Attribute Information Printer ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_ARMATTRIBUTEPARSER_H
#define LLVM_SUPPORT_ARMATTRIBUTEPARSER_H

#include "ARMBuildAttributes.h"
#include "ScopedPrinter.h"

#include <map>

namespace llvm {
class StringRef;

class ARMAttributeParser {
  ScopedPrinter *SW;

  std::map<unsigned, unsigned> Attributes;

  struct DisplayHandler {
    ARMBuildAttrs::AttrType Attribute;
    void (ARMAttributeParser::*Routine)(ARMBuildAttrs::AttrType,
                                        const uint8_t *, uint32_t &);
  };
  static const DisplayHandler DisplayRoutines[];

  uint64_t ParseInteger(const uint8_t *Data, uint32_t &Offset);
  StringRef ParseString(const uint8_t *Data, uint32_t &Offset);

  void IntegerAttribute(ARMBuildAttrs::AttrType Tag, const uint8_t *Data,
                        uint32_t &Offset);
  void StringAttribute(ARMBuildAttrs::AttrType Tag, const uint8_t *Data,
                       uint32_t &Offset);

  void PrintAttribute(unsigned Tag, unsigned Value, StringRef ValueDesc);

  void CPU_arch(ARMBuildAttrs::AttrType Tag, const uint8_t *Data,
                uint32_t &Offset);
  void CPU_arch_profile(ARMBuildAttrs::AttrType Tag, const uint8_t *Data,
                        uint32_t &Offset);
  void ARM_ISA_use(ARMBuildAttrs::AttrType Tag, const uint8_t *Data,
                   uint32_t &Offset);
  void THUMB_ISA_use(ARMBuildAttrs::AttrType Tag, const uint8_t *Data,
                     uint32_t &Offset);
  void FP_arch(ARMBuildAttrs::AttrType Tag, const uint8_t *Data,
               uint32_t &Offset);
  void WMMX_arch(ARMBuildAttrs::AttrType Tag, const uint8_t *Data,
                 uint32_t &Offset);
  void Advanced_SIMD_arch(ARMBuildAttrs::AttrType Tag, const uint8_t *Data,
                          uint32_t &Offset);
  void PCS_config(ARMBuildAttrs::AttrType Tag, const uint8_t *Data,
                  uint32_t &Offset);
  void ABI_PCS_R9_use(ARMBuildAttrs::AttrType Tag, const uint8_t *Data,
                      uint32_t &Offset);
  void ABI_PCS_RW_data(ARMBuildAttrs::AttrType Tag, const uint8_t *Data,
                       uint32_t &Offset);
  void ABI_PCS_RO_data(ARMBuildAttrs::AttrType Tag, const uint8_t *Data,
                       uint32_t &Offset);
  void ABI_PCS_GOT_use(ARMBuildAttrs::AttrType Tag, const uint8_t *Data,
                       uint32_t &Offset);
  void ABI_PCS_wchar_t(ARMBuildAttrs::AttrType Tag, const uint8_t *Data,
                       uint32_t &Offset);
  void ABI_FP_rounding(ARMBuildAttrs::AttrType Tag, const uint8_t *Data,
                       uint32_t &Offset);
  void ABI_FP_denormal(ARMBuildAttrs::AttrType Tag, const uint8_t *Data,
                       uint32_t &Offset);
  void ABI_FP_exceptions(ARMBuildAttrs::AttrType Tag, const uint8_t *Data,
                         uint32_t &Offset);
  void ABI_FP_user_exceptions(ARMBuildAttrs::AttrType Tag, const uint8_t *Data,
                              uint32_t &Offset);
  void ABI_FP_number_model(ARMBuildAttrs::AttrType Tag, const uint8_t *Data,
                           uint32_t &Offset);
  void ABI_align_needed(ARMBuildAttrs::AttrType Tag, const uint8_t *Data,
                        uint32_t &Offset);
  void ABI_align_preserved(ARMBuildAttrs::AttrType Tag, const uint8_t *Data,
                           uint32_t &Offset);
  void ABI_enum_size(ARMBuildAttrs::AttrType Tag, const uint8_t *Data,
                     uint32_t &Offset);
  void ABI_HardFP_use(ARMBuildAttrs::AttrType Tag, const uint8_t *Data,
                      uint32_t &Offset);
  void ABI_VFP_args(ARMBuildAttrs::AttrType Tag, const uint8_t *Data,
                    uint32_t &Offset);
  void ABI_WMMX_args(ARMBuildAttrs::AttrType Tag, const uint8_t *Data,
                     uint32_t &Offset);
  void ABI_optimization_goals(ARMBuildAttrs::AttrType Tag, const uint8_t *Data,
                              uint32_t &Offset);
  void ABI_FP_optimization_goals(ARMBuildAttrs::AttrType Tag,
                                 const uint8_t *Data, uint32_t &Offset);
  void compatibility(ARMBuildAttrs::AttrType Tag, const uint8_t *Data,
                     uint32_t &Offset);
  void CPU_unaligned_access(ARMBuildAttrs::AttrType Tag, const uint8_t *Data,
                            uint32_t &Offset);
  void FP_HP_extension(ARMBuildAttrs::AttrType Tag, const uint8_t *Data,
                       uint32_t &Offset);
  void ABI_FP_16bit_format(ARMBuildAttrs::AttrType Tag, const uint8_t *Data,
                           uint32_t &Offset);
  void MPextension_use(ARMBuildAttrs::AttrType Tag, const uint8_t *Data,
                       uint32_t &Offset);
  void DIV_use(ARMBuildAttrs::AttrType Tag, const uint8_t *Data,
               uint32_t &Offset);
  void DSP_extension(ARMBuildAttrs::AttrType Tag, const uint8_t *Data,
                     uint32_t &Offset);
  void T2EE_use(ARMBuildAttrs::AttrType Tag, const uint8_t *Data,
                uint32_t &Offset);
  void Virtualization_use(ARMBuildAttrs::AttrType Tag, const uint8_t *Data,
                          uint32_t &Offset);
  void nodefaults(ARMBuildAttrs::AttrType Tag, const uint8_t *Data,
                  uint32_t &Offset);

  void ParseAttributeList(const uint8_t *Data, uint32_t &Offset,
                          uint32_t Length);
  void ParseIndexList(const uint8_t *Data, uint32_t &Offset,
                      SmallVectorImpl<uint8_t> &IndexList);
  void ParseSubsection(const uint8_t *Data, uint32_t Length);
public:
  ARMAttributeParser(ScopedPrinter *SW) : SW(SW) {}

  ARMAttributeParser() : SW(nullptr) { }

  void Parse(ArrayRef<uint8_t> Section, bool isLittle);

  bool hasAttribute(unsigned Tag) const {
    return Attributes.count(Tag);
  }

  unsigned getAttributeValue(unsigned Tag) const {
    return Attributes.find(Tag)->second;
  }
};

}

#endif

