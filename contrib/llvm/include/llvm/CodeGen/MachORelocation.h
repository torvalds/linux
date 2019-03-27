//=== MachORelocation.h - Mach-O Relocation Info ----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the MachORelocation class.
//
//===----------------------------------------------------------------------===//


#ifndef LLVM_CODEGEN_MACHORELOCATION_H
#define LLVM_CODEGEN_MACHORELOCATION_H

#include "llvm/Support/DataTypes.h"

namespace llvm {

  /// MachORelocation - This struct contains information about each relocation
  /// that needs to be emitted to the file.
  /// see <mach-o/reloc.h>
  class MachORelocation {
    uint32_t r_address;   // offset in the section to what is being  relocated
    uint32_t r_symbolnum; // symbol index if r_extern == 1 else section index
    bool     r_pcrel;     // was relocated pc-relative already
    uint8_t  r_length;    // length = 2 ^ r_length
    bool     r_extern;    //
    uint8_t  r_type;      // if not 0, machine-specific relocation type.
    bool     r_scattered; // 1 = scattered, 0 = non-scattered
    int32_t  r_value;     // the value the item to be relocated is referring
                          // to.
  public:
    uint32_t getPackedFields() const {
      if (r_scattered)
        return (1 << 31) | (r_pcrel << 30) | ((r_length & 3) << 28) |
          ((r_type & 15) << 24) | (r_address & 0x00FFFFFF);
      else
        return (r_symbolnum << 8) | (r_pcrel << 7) | ((r_length & 3) << 5) |
          (r_extern << 4) | (r_type & 15);
    }
    uint32_t getAddress() const { return r_scattered ? r_value : r_address; }
    uint32_t getRawAddress() const { return r_address; }

    MachORelocation(uint32_t addr, uint32_t index, bool pcrel, uint8_t len,
                    bool ext, uint8_t type, bool scattered = false,
                    int32_t value = 0) :
      r_address(addr), r_symbolnum(index), r_pcrel(pcrel), r_length(len),
      r_extern(ext), r_type(type), r_scattered(scattered), r_value(value) {}
  };

} // end llvm namespace

#endif // LLVM_CODEGEN_MACHORELOCATION_H
