//===-- Symbols.cpp ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/Symbols.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/DataBuffer.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/Timer.h"
#include "lldb/Utility/UUID.h"

#include "llvm/Support/FileSystem.h"

// From MacOSX system header "mach/machine.h"
typedef int cpu_type_t;
typedef int cpu_subtype_t;

using namespace lldb;
using namespace lldb_private;

#if defined(__APPLE__)

// Forward declaration of method defined in source/Host/macosx/Symbols.cpp
int LocateMacOSXFilesUsingDebugSymbols(const ModuleSpec &module_spec,
                                       ModuleSpec &return_module_spec);

#else

int LocateMacOSXFilesUsingDebugSymbols(const ModuleSpec &module_spec,
                                       ModuleSpec &return_module_spec) {
  // Cannot find MacOSX files using debug symbols on non MacOSX.
  return 0;
}

#endif

static bool FileAtPathContainsArchAndUUID(const FileSpec &file_fspec,
                                          const ArchSpec *arch,
                                          const lldb_private::UUID *uuid) {
  ModuleSpecList module_specs;
  if (ObjectFile::GetModuleSpecifications(file_fspec, 0, 0, module_specs)) {
    ModuleSpec spec;
    for (size_t i = 0; i < module_specs.GetSize(); ++i) {
      bool got_spec = module_specs.GetModuleSpecAtIndex(i, spec);
      UNUSED_IF_ASSERT_DISABLED(got_spec);
      assert(got_spec);
      if ((uuid == NULL || (spec.GetUUIDPtr() && spec.GetUUID() == *uuid)) &&
          (arch == NULL || (spec.GetArchitecturePtr() &&
                            spec.GetArchitecture().IsCompatibleMatch(*arch)))) {
        return true;
      }
    }
  }
  return false;
}

// Given a binary exec_fspec, and a ModuleSpec with an architecture/uuid,
// return true if there is a matching dSYM bundle next to the exec_fspec,
// and return that value in dsym_fspec.  
// If there is a .dSYM.yaa compressed archive next to the exec_fspec, 
// call through Symbols::DownloadObjectAndSymbolFile to download the
// expanded/uncompressed dSYM and return that filepath in dsym_fspec.

static bool LookForDsymNextToExecutablePath(const ModuleSpec &mod_spec,
                                            const FileSpec &exec_fspec,
                                            FileSpec &dsym_fspec) {
  ConstString filename = exec_fspec.GetFilename();
  FileSpec dsym_directory = exec_fspec;
  dsym_directory.RemoveLastPathComponent();

  std::string dsym_filename = filename.AsCString();
  dsym_filename += ".dSYM";
  dsym_directory.AppendPathComponent(dsym_filename);
  dsym_directory.AppendPathComponent("Contents");
  dsym_directory.AppendPathComponent("Resources");
  dsym_directory.AppendPathComponent("DWARF");

  if (FileSystem::Instance().Exists(dsym_directory)) {

    // See if the binary name exists in the dSYM DWARF
    // subdir.
    dsym_fspec = dsym_directory;
    dsym_fspec.AppendPathComponent(filename.AsCString());
    if (FileSystem::Instance().Exists(dsym_fspec) &&
        FileAtPathContainsArchAndUUID(dsym_fspec, mod_spec.GetArchitecturePtr(),
                                      mod_spec.GetUUIDPtr())) {
      return true;
    }

    // See if we have "../CF.framework" - so we'll look for
    // CF.framework.dSYM/Contents/Resources/DWARF/CF
    // We need to drop the last suffix after '.' to match 
    // 'CF' in the DWARF subdir.
    std::string binary_name (filename.AsCString());
    auto last_dot = binary_name.find_last_of('.');
    if (last_dot != std::string::npos) {
      binary_name.erase(last_dot);
      dsym_fspec = dsym_directory;
      dsym_fspec.AppendPathComponent(binary_name);
      if (FileSystem::Instance().Exists(dsym_fspec) &&
          FileAtPathContainsArchAndUUID(dsym_fspec,
                                        mod_spec.GetArchitecturePtr(),
                                        mod_spec.GetUUIDPtr())) {
        return true;
      }
    }
  }

  // See if we have a .dSYM.yaa next to this executable path.
  FileSpec dsym_yaa_fspec = exec_fspec;
  dsym_yaa_fspec.RemoveLastPathComponent();
  std::string dsym_yaa_filename = filename.AsCString();
  dsym_yaa_filename += ".dSYM.yaa";
  dsym_yaa_fspec.AppendPathComponent(dsym_yaa_filename);

  if (FileSystem::Instance().Exists(dsym_yaa_fspec)) {
    ModuleSpec mutable_mod_spec = mod_spec;
    if (Symbols::DownloadObjectAndSymbolFile(mutable_mod_spec, true) &&
        FileSystem::Instance().Exists(mutable_mod_spec.GetSymbolFileSpec())) {
      dsym_fspec = mutable_mod_spec.GetSymbolFileSpec();
      return true;
    }
  }

  return false;
}

// Given a ModuleSpec with a FileSpec and optionally uuid/architecture
// filled in, look for a .dSYM bundle next to that binary.  Returns true
// if a .dSYM bundle is found, and that path is returned in the dsym_fspec
// FileSpec.
//
// This routine looks a few directory layers above the given exec_path -
// exec_path might be /System/Library/Frameworks/CF.framework/CF and the
// dSYM might be /System/Library/Frameworks/CF.framework.dSYM.
//
// If there is a .dSYM.yaa compressed archive found next to the binary,
// we'll call DownloadObjectAndSymbolFile to expand it into a plain .dSYM

static bool LocateDSYMInVincinityOfExecutable(const ModuleSpec &module_spec,
                                              FileSpec &dsym_fspec) {
  Log *log = lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_HOST);
  const FileSpec &exec_fspec = module_spec.GetFileSpec();
  if (exec_fspec) {
    if (::LookForDsymNextToExecutablePath (module_spec, exec_fspec, dsym_fspec)) {
        if (log) {
          log->Printf("dSYM with matching UUID & arch found at %s", dsym_fspec.GetPath().c_str());
        }
        return true;
    } else {
      FileSpec parent_dirs = exec_fspec;

      // Remove the binary name from the FileSpec
      parent_dirs.RemoveLastPathComponent();

      // Add a ".dSYM" name to each directory component of the path,
      // stripping off components.  e.g. we may have a binary like
      // /S/L/F/Foundation.framework/Versions/A/Foundation and
      // /S/L/F/Foundation.framework.dSYM
      //
      // so we'll need to start with
      // /S/L/F/Foundation.framework/Versions/A, add the .dSYM part to the
      // "A", and if that doesn't exist, strip off the "A" and try it again
      // with "Versions", etc., until we find a dSYM bundle or we've
      // stripped off enough path components that there's no need to
      // continue.

      for (int i = 0; i < 4; i++) {
        // Does this part of the path have a "." character - could it be a
        // bundle's top level directory?
        const char *fn = parent_dirs.GetFilename().AsCString();
        if (fn == nullptr)
          break;
        if (::strchr(fn, '.') != nullptr) {
          if (::LookForDsymNextToExecutablePath (module_spec, parent_dirs, dsym_fspec)) {
            if (log) {
              log->Printf("dSYM with matching UUID & arch found at %s",
                          dsym_fspec.GetPath().c_str());
            }
            return true;
          }
        }
        parent_dirs.RemoveLastPathComponent();
      }
    }
  }
  dsym_fspec.Clear();
  return false;
}

static FileSpec LocateExecutableSymbolFileDsym(const ModuleSpec &module_spec) {
  const FileSpec *exec_fspec = module_spec.GetFileSpecPtr();
  const ArchSpec *arch = module_spec.GetArchitecturePtr();
  const UUID *uuid = module_spec.GetUUIDPtr();

  static Timer::Category func_cat(LLVM_PRETTY_FUNCTION);
  Timer scoped_timer(
      func_cat,
      "LocateExecutableSymbolFileDsym (file = %s, arch = %s, uuid = %p)",
      exec_fspec ? exec_fspec->GetFilename().AsCString("<NULL>") : "<NULL>",
      arch ? arch->GetArchitectureName() : "<NULL>", (const void *)uuid);

  FileSpec symbol_fspec;
  ModuleSpec dsym_module_spec;
  // First try and find the dSYM in the same directory as the executable or in
  // an appropriate parent directory
  if (!LocateDSYMInVincinityOfExecutable(module_spec, symbol_fspec)) {
    // We failed to easily find the dSYM above, so use DebugSymbols
    LocateMacOSXFilesUsingDebugSymbols(module_spec, dsym_module_spec);
  } else {
    dsym_module_spec.GetSymbolFileSpec() = symbol_fspec;
  }
  return dsym_module_spec.GetSymbolFileSpec();
}

ModuleSpec Symbols::LocateExecutableObjectFile(const ModuleSpec &module_spec) {
  ModuleSpec result;
  const FileSpec *exec_fspec = module_spec.GetFileSpecPtr();
  const ArchSpec *arch = module_spec.GetArchitecturePtr();
  const UUID *uuid = module_spec.GetUUIDPtr();
  static Timer::Category func_cat(LLVM_PRETTY_FUNCTION);
  Timer scoped_timer(
      func_cat, "LocateExecutableObjectFile (file = %s, arch = %s, uuid = %p)",
      exec_fspec ? exec_fspec->GetFilename().AsCString("<NULL>") : "<NULL>",
      arch ? arch->GetArchitectureName() : "<NULL>", (const void *)uuid);

  ModuleSpecList module_specs;
  ModuleSpec matched_module_spec;
  if (exec_fspec &&
      ObjectFile::GetModuleSpecifications(*exec_fspec, 0, 0, module_specs) &&
      module_specs.FindMatchingModuleSpec(module_spec, matched_module_spec)) {
    result.GetFileSpec() = exec_fspec;
  } else {
    LocateMacOSXFilesUsingDebugSymbols(module_spec, result);
  }
  return result;
}

// Keep "symbols.enable-external-lookup" description in sync with this function.

FileSpec Symbols::LocateExecutableSymbolFile(const ModuleSpec &module_spec) {
  FileSpec symbol_file_spec = module_spec.GetSymbolFileSpec();
  if (symbol_file_spec.IsAbsolute() &&
      FileSystem::Instance().Exists(symbol_file_spec))
    return symbol_file_spec;

  const char *symbol_filename = symbol_file_spec.GetFilename().AsCString();
  if (symbol_filename && symbol_filename[0]) {
    FileSpecList debug_file_search_paths(
        Target::GetDefaultDebugFileSearchPaths());

    // Add module directory.
    FileSpec module_file_spec = module_spec.GetFileSpec();
    // We keep the unresolved pathname if it fails.
    FileSystem::Instance().ResolveSymbolicLink(module_file_spec, module_file_spec);

    const ConstString &file_dir = module_file_spec.GetDirectory();
    {
      FileSpec file_spec(file_dir.AsCString("."));
      FileSystem::Instance().Resolve(file_spec);
      debug_file_search_paths.AppendIfUnique(file_spec);
    }

    if (ModuleList::GetGlobalModuleListProperties().GetEnableExternalLookup()) {

      // Add current working directory.
      {
        FileSpec file_spec(".");
        FileSystem::Instance().Resolve(file_spec);
        debug_file_search_paths.AppendIfUnique(file_spec);
      }

#ifndef _WIN32
#if defined(__NetBSD__)
      // Add /usr/libdata/debug directory.
      {
        FileSpec file_spec("/usr/libdata/debug");
        FileSystem::Instance().Resolve(file_spec);
        debug_file_search_paths.AppendIfUnique(file_spec);
      }
#else
      // Add /usr/lib/debug directory.
      {
        FileSpec file_spec("/usr/lib/debug");
        FileSystem::Instance().Resolve(file_spec);
        debug_file_search_paths.AppendIfUnique(file_spec);
      }
#endif
#endif // _WIN32
    }

    std::string uuid_str;
    const UUID &module_uuid = module_spec.GetUUID();
    if (module_uuid.IsValid()) {
      // Some debug files are stored in the .build-id directory like this:
      //   /usr/lib/debug/.build-id/ff/e7fe727889ad82bb153de2ad065b2189693315.debug
      uuid_str = module_uuid.GetAsString("");
      std::transform(uuid_str.begin(), uuid_str.end(), uuid_str.begin(),
          ::tolower);
      uuid_str.insert(2, 1, '/');
      uuid_str = uuid_str + ".debug";
    }

    size_t num_directories = debug_file_search_paths.GetSize();
    for (size_t idx = 0; idx < num_directories; ++idx) {
      FileSpec dirspec = debug_file_search_paths.GetFileSpecAtIndex(idx);
      FileSystem::Instance().Resolve(dirspec);
      if (!FileSystem::Instance().IsDirectory(dirspec))
        continue;

      std::vector<std::string> files;
      std::string dirname = dirspec.GetPath();

      files.push_back(dirname + "/" + symbol_filename);
      files.push_back(dirname + "/.debug/" + symbol_filename);
      files.push_back(dirname + "/.build-id/" + uuid_str);

      // Some debug files may stored in the module directory like this:
      //   /usr/lib/debug/usr/lib/library.so.debug
      if (!file_dir.IsEmpty())
        files.push_back(dirname + file_dir.AsCString() + "/" + symbol_filename);

      const uint32_t num_files = files.size();
      for (size_t idx_file = 0; idx_file < num_files; ++idx_file) {
        const std::string &filename = files[idx_file];
        FileSpec file_spec(filename);
        FileSystem::Instance().Resolve(file_spec);

        if (llvm::sys::fs::equivalent(file_spec.GetPath(),
                                      module_file_spec.GetPath()))
          continue;

        if (FileSystem::Instance().Exists(file_spec)) {
          lldb_private::ModuleSpecList specs;
          const size_t num_specs =
              ObjectFile::GetModuleSpecifications(file_spec, 0, 0, specs);
          assert(num_specs <= 1 &&
                 "Symbol Vendor supports only a single architecture");
          if (num_specs == 1) {
            ModuleSpec mspec;
            if (specs.GetModuleSpecAtIndex(0, mspec)) {
              // Skip the uuids check if module_uuid is invalid. For example,
              // this happens for *.dwp files since at the moment llvm-dwp
              // doesn't output build ids, nor does binutils dwp.
              if (!module_uuid.IsValid() || module_uuid == mspec.GetUUID())
                return file_spec;
            }
          }
        }
      }
    }
  }

  return LocateExecutableSymbolFileDsym(module_spec);
}

#if !defined(__APPLE__)

FileSpec Symbols::FindSymbolFileInBundle(const FileSpec &symfile_bundle,
                                         const lldb_private::UUID *uuid,
                                         const ArchSpec *arch) {
  // FIXME
  return FileSpec();
}

bool Symbols::DownloadObjectAndSymbolFile(ModuleSpec &module_spec,
                                          bool force_lookup) {
  // Fill in the module_spec.GetFileSpec() for the object file and/or the
  // module_spec.GetSymbolFileSpec() for the debug symbols file.
  return false;
}

#endif
