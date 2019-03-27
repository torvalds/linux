//===- MCSymbolMachO.h -  ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_MC_MCSYMBOLMACHO_H
#define LLVM_MC_MCSYMBOLMACHO_H

#include "llvm/ADT/Twine.h"
#include "llvm/MC/MCSymbol.h"

namespace llvm {
class MCSymbolMachO : public MCSymbol {
  /// We store the value for the 'desc' symbol field in the
  /// lowest 16 bits of the implementation defined flags.
  enum MachOSymbolFlags : uint16_t { // See <mach-o/nlist.h>.
    SF_DescFlagsMask                        = 0xFFFF,

    // Reference type flags.
    SF_ReferenceTypeMask                    = 0x0007,
    SF_ReferenceTypeUndefinedNonLazy        = 0x0000,
    SF_ReferenceTypeUndefinedLazy           = 0x0001,
    SF_ReferenceTypeDefined                 = 0x0002,
    SF_ReferenceTypePrivateDefined          = 0x0003,
    SF_ReferenceTypePrivateUndefinedNonLazy = 0x0004,
    SF_ReferenceTypePrivateUndefinedLazy    = 0x0005,

    // Other 'desc' flags.
    SF_ThumbFunc                            = 0x0008,
    SF_NoDeadStrip                          = 0x0020,
    SF_WeakReference                        = 0x0040,
    SF_WeakDefinition                       = 0x0080,
    SF_SymbolResolver                       = 0x0100,
    SF_AltEntry                             = 0x0200,

    // Common alignment
    SF_CommonAlignmentMask                  = 0xF0FF,
    SF_CommonAlignmentShift                 = 8
  };

public:
  MCSymbolMachO(const StringMapEntry<bool> *Name, bool isTemporary)
      : MCSymbol(SymbolKindMachO, Name, isTemporary) {}

  // Reference type methods.

  void clearReferenceType() const {
    modifyFlags(0, SF_ReferenceTypeMask);
  }

  void setReferenceTypeUndefinedLazy(bool Value) const {
    modifyFlags(Value ? SF_ReferenceTypeUndefinedLazy : 0,
                SF_ReferenceTypeUndefinedLazy);
  }

  // Other 'desc' methods.

  void setThumbFunc() const {
    modifyFlags(SF_ThumbFunc, SF_ThumbFunc);
  }

  bool isNoDeadStrip() const {
    return getFlags() & SF_NoDeadStrip;
  }
  void setNoDeadStrip() const {
    modifyFlags(SF_NoDeadStrip, SF_NoDeadStrip);
  }

  bool isWeakReference() const {
    return getFlags() & SF_WeakReference;
  }
  void setWeakReference() const {
    modifyFlags(SF_WeakReference, SF_WeakReference);
  }

  bool isWeakDefinition() const {
    return getFlags() & SF_WeakDefinition;
  }
  void setWeakDefinition() const {
    modifyFlags(SF_WeakDefinition, SF_WeakDefinition);
  }

  bool isSymbolResolver() const {
    return getFlags() & SF_SymbolResolver;
  }
  void setSymbolResolver() const {
    modifyFlags(SF_SymbolResolver, SF_SymbolResolver);
  }

  void setAltEntry() const {
    modifyFlags(SF_AltEntry, SF_AltEntry);
  }

  bool isAltEntry() const {
    return getFlags() & SF_AltEntry;
  }

  void setDesc(unsigned Value) const {
    assert(Value == (Value & SF_DescFlagsMask) &&
           "Invalid .desc value!");
    setFlags(Value & SF_DescFlagsMask);
  }

  /// Get the encoded value of the flags as they will be emitted in to
  /// the MachO binary
  uint16_t getEncodedFlags(bool EncodeAsAltEntry) const {
    uint16_t Flags = getFlags();

    // Common alignment is packed into the 'desc' bits.
    if (isCommon()) {
      if (unsigned Align = getCommonAlignment()) {
        unsigned Log2Size = Log2_32(Align);
        assert((1U << Log2Size) == Align && "Invalid 'common' alignment!");
        if (Log2Size > 15)
          report_fatal_error("invalid 'common' alignment '" +
                             Twine(Align) + "' for '" + getName() + "'",
                             false);
        Flags = (Flags & SF_CommonAlignmentMask) |
                (Log2Size << SF_CommonAlignmentShift);
      }
    }

    if (EncodeAsAltEntry)
      Flags |= SF_AltEntry;

    return Flags;
  }

  static bool classof(const MCSymbol *S) { return S->isMachO(); }
};
}

#endif
