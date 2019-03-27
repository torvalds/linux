//===-- ARMBuildAttrs.cpp - ARM Build Attributes --------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ARMBuildAttributes.h"

using namespace llvm;

namespace {
const struct {
  ARMBuildAttrs::AttrType Attr;
  StringRef TagName;
} ARMAttributeTags[] = {
  { ARMBuildAttrs::File, "Tag_File" },
  { ARMBuildAttrs::Section, "Tag_Section" },
  { ARMBuildAttrs::Symbol, "Tag_Symbol" },
  { ARMBuildAttrs::CPU_raw_name, "Tag_CPU_raw_name" },
  { ARMBuildAttrs::CPU_name, "Tag_CPU_name" },
  { ARMBuildAttrs::CPU_arch, "Tag_CPU_arch" },
  { ARMBuildAttrs::CPU_arch_profile, "Tag_CPU_arch_profile" },
  { ARMBuildAttrs::ARM_ISA_use, "Tag_ARM_ISA_use" },
  { ARMBuildAttrs::THUMB_ISA_use, "Tag_THUMB_ISA_use" },
  { ARMBuildAttrs::FP_arch, "Tag_FP_arch" },
  { ARMBuildAttrs::WMMX_arch, "Tag_WMMX_arch" },
  { ARMBuildAttrs::Advanced_SIMD_arch, "Tag_Advanced_SIMD_arch" },
  { ARMBuildAttrs::PCS_config, "Tag_PCS_config" },
  { ARMBuildAttrs::ABI_PCS_R9_use, "Tag_ABI_PCS_R9_use" },
  { ARMBuildAttrs::ABI_PCS_RW_data, "Tag_ABI_PCS_RW_data" },
  { ARMBuildAttrs::ABI_PCS_RO_data, "Tag_ABI_PCS_RO_data" },
  { ARMBuildAttrs::ABI_PCS_GOT_use, "Tag_ABI_PCS_GOT_use" },
  { ARMBuildAttrs::ABI_PCS_wchar_t, "Tag_ABI_PCS_wchar_t" },
  { ARMBuildAttrs::ABI_FP_rounding, "Tag_ABI_FP_rounding" },
  { ARMBuildAttrs::ABI_FP_denormal, "Tag_ABI_FP_denormal" },
  { ARMBuildAttrs::ABI_FP_exceptions, "Tag_ABI_FP_exceptions" },
  { ARMBuildAttrs::ABI_FP_user_exceptions, "Tag_ABI_FP_user_exceptions" },
  { ARMBuildAttrs::ABI_FP_number_model, "Tag_ABI_FP_number_model" },
  { ARMBuildAttrs::ABI_align_needed, "Tag_ABI_align_needed" },
  { ARMBuildAttrs::ABI_align_preserved, "Tag_ABI_align_preserved" },
  { ARMBuildAttrs::ABI_enum_size, "Tag_ABI_enum_size" },
  { ARMBuildAttrs::ABI_HardFP_use, "Tag_ABI_HardFP_use" },
  { ARMBuildAttrs::ABI_VFP_args, "Tag_ABI_VFP_args" },
  { ARMBuildAttrs::ABI_WMMX_args, "Tag_ABI_WMMX_args" },
  { ARMBuildAttrs::ABI_optimization_goals, "Tag_ABI_optimization_goals" },
  { ARMBuildAttrs::ABI_FP_optimization_goals, "Tag_ABI_FP_optimization_goals" },
  { ARMBuildAttrs::compatibility, "Tag_compatibility" },
  { ARMBuildAttrs::CPU_unaligned_access, "Tag_CPU_unaligned_access" },
  { ARMBuildAttrs::FP_HP_extension, "Tag_FP_HP_extension" },
  { ARMBuildAttrs::ABI_FP_16bit_format, "Tag_ABI_FP_16bit_format" },
  { ARMBuildAttrs::MPextension_use, "Tag_MPextension_use" },
  { ARMBuildAttrs::DIV_use, "Tag_DIV_use" },
  { ARMBuildAttrs::DSP_extension, "Tag_DSP_extension" },
  { ARMBuildAttrs::nodefaults, "Tag_nodefaults" },
  { ARMBuildAttrs::also_compatible_with, "Tag_also_compatible_with" },
  { ARMBuildAttrs::T2EE_use, "Tag_T2EE_use" },
  { ARMBuildAttrs::conformance, "Tag_conformance" },
  { ARMBuildAttrs::Virtualization_use, "Tag_Virtualization_use" },

  // Legacy Names
  { ARMBuildAttrs::FP_arch, "Tag_VFP_arch" },
  { ARMBuildAttrs::FP_HP_extension, "Tag_VFP_HP_extension" },
  { ARMBuildAttrs::ABI_align_needed, "Tag_ABI_align8_needed" },
  { ARMBuildAttrs::ABI_align_preserved, "Tag_ABI_align8_preserved" },
};
}

namespace llvm {
namespace ARMBuildAttrs {
StringRef AttrTypeAsString(unsigned Attr, bool HasTagPrefix) {
  return AttrTypeAsString(static_cast<AttrType>(Attr), HasTagPrefix);
}

StringRef AttrTypeAsString(AttrType Attr, bool HasTagPrefix) {
  for (unsigned TI = 0, TE = sizeof(ARMAttributeTags) / sizeof(*ARMAttributeTags);
       TI != TE; ++TI)
    if (ARMAttributeTags[TI].Attr == Attr) {
      auto TagName = ARMAttributeTags[TI].TagName;
      return HasTagPrefix ? TagName : TagName.drop_front(4);
    }
  return "";
}

int AttrTypeFromString(StringRef Tag) {
  bool HasTagPrefix = Tag.startswith("Tag_");
  for (unsigned TI = 0,
                TE = sizeof(ARMAttributeTags) / sizeof(*ARMAttributeTags);
       TI != TE; ++TI) {
    auto TagName = ARMAttributeTags[TI].TagName;
    if (TagName.drop_front(HasTagPrefix ? 0 : 4) == Tag) {
      return ARMAttributeTags[TI].Attr;
    }
  }
  return -1;
}
}
}

