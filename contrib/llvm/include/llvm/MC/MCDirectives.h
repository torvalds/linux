//===- MCDirectives.h - Enums for directives on various targets -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines various enums that represent target-specific directives.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCDIRECTIVES_H
#define LLVM_MC_MCDIRECTIVES_H

namespace llvm {

enum MCSymbolAttr {
  MCSA_Invalid = 0,    ///< Not a valid directive.

  // Various directives in alphabetical order.
  MCSA_ELF_TypeFunction,    ///< .type _foo, STT_FUNC  # aka @function
  MCSA_ELF_TypeIndFunction, ///< .type _foo, STT_GNU_IFUNC
  MCSA_ELF_TypeObject,      ///< .type _foo, STT_OBJECT  # aka @object
  MCSA_ELF_TypeTLS,         ///< .type _foo, STT_TLS     # aka @tls_object
  MCSA_ELF_TypeCommon,      ///< .type _foo, STT_COMMON  # aka @common
  MCSA_ELF_TypeNoType,      ///< .type _foo, STT_NOTYPE  # aka @notype
  MCSA_ELF_TypeGnuUniqueObject, /// .type _foo, @gnu_unique_object
  MCSA_Global,              ///< .globl
  MCSA_Hidden,              ///< .hidden (ELF)
  MCSA_IndirectSymbol,      ///< .indirect_symbol (MachO)
  MCSA_Internal,            ///< .internal (ELF)
  MCSA_LazyReference,       ///< .lazy_reference (MachO)
  MCSA_Local,               ///< .local (ELF)
  MCSA_NoDeadStrip,         ///< .no_dead_strip (MachO)
  MCSA_SymbolResolver,      ///< .symbol_resolver (MachO)
  MCSA_AltEntry,            ///< .alt_entry (MachO)
  MCSA_PrivateExtern,       ///< .private_extern (MachO)
  MCSA_Protected,           ///< .protected (ELF)
  MCSA_Reference,           ///< .reference (MachO)
  MCSA_Weak,                ///< .weak
  MCSA_WeakDefinition,      ///< .weak_definition (MachO)
  MCSA_WeakReference,       ///< .weak_reference (MachO)
  MCSA_WeakDefAutoPrivate   ///< .weak_def_can_be_hidden (MachO)
};

enum MCAssemblerFlag {
  MCAF_SyntaxUnified,         ///< .syntax (ARM/ELF)
  MCAF_SubsectionsViaSymbols, ///< .subsections_via_symbols (MachO)
  MCAF_Code16,                ///< .code16 (X86) / .code 16 (ARM)
  MCAF_Code32,                ///< .code32 (X86) / .code 32 (ARM)
  MCAF_Code64                 ///< .code64 (X86)
};

enum MCDataRegionType {
  MCDR_DataRegion,            ///< .data_region
  MCDR_DataRegionJT8,         ///< .data_region jt8
  MCDR_DataRegionJT16,        ///< .data_region jt16
  MCDR_DataRegionJT32,        ///< .data_region jt32
  MCDR_DataRegionEnd          ///< .end_data_region
};

enum MCVersionMinType {
  MCVM_IOSVersionMin,         ///< .ios_version_min
  MCVM_OSXVersionMin,         ///< .macosx_version_min
  MCVM_TvOSVersionMin,        ///< .tvos_version_min
  MCVM_WatchOSVersionMin,     ///< .watchos_version_min
};

} // end namespace llvm

#endif
