//===-- Symbols.h -----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Symbols_h_
#define liblldb_Symbols_h_

#include <stdint.h>

#include "lldb/Utility/FileSpec.h"

namespace lldb_private {

class ArchSpec;
class ModuleSpec;
class UUID;

class Symbols {
public:
  //----------------------------------------------------------------------
  // Locate the executable file given a module specification.
  //
  // Locating the file should happen only on the local computer or using the
  // current computers global settings.
  //----------------------------------------------------------------------
  static ModuleSpec LocateExecutableObjectFile(const ModuleSpec &module_spec);

  //----------------------------------------------------------------------
  // Locate the symbol file given a module specification.
  //
  // Locating the file should happen only on the local computer or using the
  // current computers global settings.
  //----------------------------------------------------------------------
  static FileSpec LocateExecutableSymbolFile(const ModuleSpec &module_spec);

  static FileSpec FindSymbolFileInBundle(const FileSpec &dsym_bundle_fspec,
                                         const lldb_private::UUID *uuid,
                                         const ArchSpec *arch);

  //----------------------------------------------------------------------
  // Locate the object and symbol file given a module specification.
  //
  // Locating the file can try to download the file from a corporate build
  // repository, or using any other means necessary to locate both the
  // unstripped object file and the debug symbols. The force_lookup argument
  // controls whether the external program is called unconditionally to find
  // the symbol file, or if the user's settings are checked to see if they've
  // enabled the external program before calling.
  //
  //----------------------------------------------------------------------
  static bool DownloadObjectAndSymbolFile(ModuleSpec &module_spec,
                                          bool force_lookup = true);
};

} // namespace lldb_private

#endif // liblldb_Symbols_h_
