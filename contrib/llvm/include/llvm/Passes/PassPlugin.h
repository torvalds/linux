//===- llvm/Passes/PassPlugin.h - Public Plugin API -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This defines the public entry point for new-PM pass plugins.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_PASSES_PASSPLUGIN_H
#define LLVM_PASSES_PASSPLUGIN_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <string>

namespace llvm {
class PassBuilder;

/// \macro LLVM_PLUGIN_API_VERSION
/// Identifies the API version understood by this plugin.
///
/// When a plugin is loaded, the driver will check it's supported plugin version
/// against that of the plugin. A mismatch is an error. The supported version
/// will be incremented for ABI-breaking changes to the \c PassPluginLibraryInfo
/// struct, i.e. when callbacks are added, removed, or reordered.
#define LLVM_PLUGIN_API_VERSION 1

extern "C" {
/// Information about the plugin required to load its passes
///
/// This struct defines the core interface for pass plugins and is supposed to
/// be filled out by plugin implementors. LLVM-side users of a plugin are
/// expected to use the \c PassPlugin class below to interface with it.
struct PassPluginLibraryInfo {
  /// The API version understood by this plugin, usually \c
  /// LLVM_PLUGIN_API_VERSION
  uint32_t APIVersion;
  /// A meaningful name of the plugin.
  const char *PluginName;
  /// The version of the plugin.
  const char *PluginVersion;

  /// The callback for registering plugin passes with a \c PassBuilder
  /// instance
  void (*RegisterPassBuilderCallbacks)(PassBuilder &);
};
}

/// A loaded pass plugin.
///
/// An instance of this class wraps a loaded pass plugin and gives access to
/// its interface defined by the \c PassPluginLibraryInfo it exposes.
class PassPlugin {
public:
  /// Attempts to load a pass plugin from a given file.
  ///
  /// \returns Returns an error if either the library cannot be found or loaded,
  /// there is no public entry point, or the plugin implements the wrong API
  /// version.
  static Expected<PassPlugin> Load(const std::string &Filename);

  /// Get the filename of the loaded plugin.
  StringRef getFilename() const { return Filename; }

  /// Get the plugin name
  StringRef getPluginName() const { return Info.PluginName; }

  /// Get the plugin version
  StringRef getPluginVersion() const { return Info.PluginVersion; }

  /// Get the plugin API version
  uint32_t getAPIVersion() const { return Info.APIVersion; }

  /// Invoke the PassBuilder callback registration
  void registerPassBuilderCallbacks(PassBuilder &PB) const {
    Info.RegisterPassBuilderCallbacks(PB);
  }

private:
  PassPlugin(const std::string &Filename, const sys::DynamicLibrary &Library)
      : Filename(Filename), Library(Library), Info() {}

  std::string Filename;
  sys::DynamicLibrary Library;
  PassPluginLibraryInfo Info;
};
}

/// The public entry point for a pass plugin.
///
/// When a plugin is loaded by the driver, it will call this entry point to
/// obtain information about this plugin and about how to register its passes.
/// This function needs to be implemented by the plugin, see the example below:
///
/// ```
/// extern "C" ::llvm::PassPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
/// llvmGetPassPluginInfo() {
///   return {
///     LLVM_PLUGIN_API_VERSION, "MyPlugin", "v0.1", [](PassBuilder &PB) { ... }
///   };
/// }
/// ```
extern "C" ::llvm::PassPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
llvmGetPassPluginInfo();

#endif /* LLVM_PASSES_PASSPLUGIN_H */
