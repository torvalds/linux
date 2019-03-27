//===- DWARFDebugMacro.h ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFDEBUGMACRO_H
#define LLVM_DEBUGINFO_DWARF_DWARFDEBUGMACRO_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/DataExtractor.h"
#include <cstdint>

namespace llvm {

class raw_ostream;

class DWARFDebugMacro {
  /// A single macro entry within a macro list.
  struct Entry {
    /// The type of the macro entry.
    uint32_t Type;
    union {
      /// The source line where the macro is defined.
      uint64_t Line;
      /// Vendor extension constant value.
      uint64_t ExtConstant;
    };

    union {
      /// The string (name, value) of the macro entry.
      const char *MacroStr;
      // An unsigned integer indicating the identity of the source file.
      uint64_t File;
      /// Vendor extension string.
      const char *ExtStr;
    };
  };

  using MacroList = SmallVector<Entry, 4>;

  /// A list of all the macro entries in the debug_macinfo section.
  MacroList Macros;

public:
  DWARFDebugMacro() = default;

  /// Print the macro list found within the debug_macinfo section.
  void dump(raw_ostream &OS) const;

  /// Parse the debug_macinfo section accessible via the 'data' parameter.
  void parse(DataExtractor data);

  /// Return whether the section has any entries.
  bool empty() const { return Macros.empty(); }
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFDEBUGMACRO_H
