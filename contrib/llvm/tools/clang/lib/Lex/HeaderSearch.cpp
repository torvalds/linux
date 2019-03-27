//===- HeaderSearch.cpp - Resolve Header File Locations -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements the DirectoryLookup and HeaderSearch interfaces.
//
//===----------------------------------------------------------------------===//

#include "clang/Lex/HeaderSearch.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/Module.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/DirectoryLookup.h"
#include "clang/Lex/ExternalPreprocessorSource.h"
#include "clang/Lex/HeaderMap.h"
#include "clang/Lex/HeaderSearchOptions.h"
#include "clang/Lex/LexDiagnostic.h"
#include "clang/Lex/ModuleMap.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Capacity.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <system_error>
#include <utility>

using namespace clang;

const IdentifierInfo *
HeaderFileInfo::getControllingMacro(ExternalPreprocessorSource *External) {
  if (ControllingMacro) {
    if (ControllingMacro->isOutOfDate()) {
      assert(External && "We must have an external source if we have a "
                         "controlling macro that is out of date.");
      External->updateOutOfDateIdentifier(
          *const_cast<IdentifierInfo *>(ControllingMacro));
    }
    return ControllingMacro;
  }

  if (!ControllingMacroID || !External)
    return nullptr;

  ControllingMacro = External->GetIdentifier(ControllingMacroID);
  return ControllingMacro;
}

ExternalHeaderFileInfoSource::~ExternalHeaderFileInfoSource() = default;

HeaderSearch::HeaderSearch(std::shared_ptr<HeaderSearchOptions> HSOpts,
                           SourceManager &SourceMgr, DiagnosticsEngine &Diags,
                           const LangOptions &LangOpts,
                           const TargetInfo *Target)
    : HSOpts(std::move(HSOpts)), Diags(Diags),
      FileMgr(SourceMgr.getFileManager()), FrameworkMap(64),
      ModMap(SourceMgr, Diags, LangOpts, Target, *this) {}

void HeaderSearch::PrintStats() {
  fprintf(stderr, "\n*** HeaderSearch Stats:\n");
  fprintf(stderr, "%d files tracked.\n", (int)FileInfo.size());
  unsigned NumOnceOnlyFiles = 0, MaxNumIncludes = 0, NumSingleIncludedFiles = 0;
  for (unsigned i = 0, e = FileInfo.size(); i != e; ++i) {
    NumOnceOnlyFiles += FileInfo[i].isImport;
    if (MaxNumIncludes < FileInfo[i].NumIncludes)
      MaxNumIncludes = FileInfo[i].NumIncludes;
    NumSingleIncludedFiles += FileInfo[i].NumIncludes == 1;
  }
  fprintf(stderr, "  %d #import/#pragma once files.\n", NumOnceOnlyFiles);
  fprintf(stderr, "  %d included exactly once.\n", NumSingleIncludedFiles);
  fprintf(stderr, "  %d max times a file is included.\n", MaxNumIncludes);

  fprintf(stderr, "  %d #include/#include_next/#import.\n", NumIncluded);
  fprintf(stderr, "    %d #includes skipped due to"
          " the multi-include optimization.\n", NumMultiIncludeFileOptzn);

  fprintf(stderr, "%d framework lookups.\n", NumFrameworkLookups);
  fprintf(stderr, "%d subframework lookups.\n", NumSubFrameworkLookups);
}

/// CreateHeaderMap - This method returns a HeaderMap for the specified
/// FileEntry, uniquing them through the 'HeaderMaps' datastructure.
const HeaderMap *HeaderSearch::CreateHeaderMap(const FileEntry *FE) {
  // We expect the number of headermaps to be small, and almost always empty.
  // If it ever grows, use of a linear search should be re-evaluated.
  if (!HeaderMaps.empty()) {
    for (unsigned i = 0, e = HeaderMaps.size(); i != e; ++i)
      // Pointer equality comparison of FileEntries works because they are
      // already uniqued by inode.
      if (HeaderMaps[i].first == FE)
        return HeaderMaps[i].second.get();
  }

  if (std::unique_ptr<HeaderMap> HM = HeaderMap::Create(FE, FileMgr)) {
    HeaderMaps.emplace_back(FE, std::move(HM));
    return HeaderMaps.back().second.get();
  }

  return nullptr;
}

/// Get filenames for all registered header maps.
void HeaderSearch::getHeaderMapFileNames(
    SmallVectorImpl<std::string> &Names) const {
  for (auto &HM : HeaderMaps)
    Names.push_back(HM.first->getName());
}

std::string HeaderSearch::getCachedModuleFileName(Module *Module) {
  const FileEntry *ModuleMap =
      getModuleMap().getModuleMapFileForUniquing(Module);
  return getCachedModuleFileName(Module->Name, ModuleMap->getName());
}

std::string HeaderSearch::getPrebuiltModuleFileName(StringRef ModuleName,
                                                    bool FileMapOnly) {
  // First check the module name to pcm file map.
  auto i (HSOpts->PrebuiltModuleFiles.find(ModuleName));
  if (i != HSOpts->PrebuiltModuleFiles.end())
    return i->second;

  if (FileMapOnly || HSOpts->PrebuiltModulePaths.empty())
    return {};

  // Then go through each prebuilt module directory and try to find the pcm
  // file.
  for (const std::string &Dir : HSOpts->PrebuiltModulePaths) {
    SmallString<256> Result(Dir);
    llvm::sys::fs::make_absolute(Result);
    llvm::sys::path::append(Result, ModuleName + ".pcm");
    if (getFileMgr().getFile(Result.str()))
      return Result.str().str();
  }
  return {};
}

std::string HeaderSearch::getCachedModuleFileName(StringRef ModuleName,
                                                  StringRef ModuleMapPath) {
  // If we don't have a module cache path or aren't supposed to use one, we
  // can't do anything.
  if (getModuleCachePath().empty())
    return {};

  SmallString<256> Result(getModuleCachePath());
  llvm::sys::fs::make_absolute(Result);

  if (HSOpts->DisableModuleHash) {
    llvm::sys::path::append(Result, ModuleName + ".pcm");
  } else {
    // Construct the name <ModuleName>-<hash of ModuleMapPath>.pcm which should
    // ideally be globally unique to this particular module. Name collisions
    // in the hash are safe (because any translation unit can only import one
    // module with each name), but result in a loss of caching.
    //
    // To avoid false-negatives, we form as canonical a path as we can, and map
    // to lower-case in case we're on a case-insensitive file system.
    std::string Parent = llvm::sys::path::parent_path(ModuleMapPath);
    if (Parent.empty())
      Parent = ".";
    auto *Dir = FileMgr.getDirectory(Parent);
    if (!Dir)
      return {};
    auto DirName = FileMgr.getCanonicalName(Dir);
    auto FileName = llvm::sys::path::filename(ModuleMapPath);

    llvm::hash_code Hash =
      llvm::hash_combine(DirName.lower(), FileName.lower());

    SmallString<128> HashStr;
    llvm::APInt(64, size_t(Hash)).toStringUnsigned(HashStr, /*Radix*/36);
    llvm::sys::path::append(Result, ModuleName + "-" + HashStr + ".pcm");
  }
  return Result.str().str();
}

Module *HeaderSearch::lookupModule(StringRef ModuleName, bool AllowSearch,
                                   bool AllowExtraModuleMapSearch) {
  // Look in the module map to determine if there is a module by this name.
  Module *Module = ModMap.findModule(ModuleName);
  if (Module || !AllowSearch || !HSOpts->ImplicitModuleMaps)
    return Module;

  StringRef SearchName = ModuleName;
  Module = lookupModule(ModuleName, SearchName, AllowExtraModuleMapSearch);

  // The facility for "private modules" -- adjacent, optional module maps named
  // module.private.modulemap that are supposed to define private submodules --
  // may have different flavors of names: FooPrivate, Foo_Private and Foo.Private.
  //
  // Foo.Private is now deprecated in favor of Foo_Private. Users of FooPrivate
  // should also rename to Foo_Private. Representing private as submodules
  // could force building unwanted dependencies into the parent module and cause
  // dependency cycles.
  if (!Module && SearchName.consume_back("_Private"))
    Module = lookupModule(ModuleName, SearchName, AllowExtraModuleMapSearch);
  if (!Module && SearchName.consume_back("Private"))
    Module = lookupModule(ModuleName, SearchName, AllowExtraModuleMapSearch);
  return Module;
}

Module *HeaderSearch::lookupModule(StringRef ModuleName, StringRef SearchName,
                                   bool AllowExtraModuleMapSearch) {
  Module *Module = nullptr;

  // Look through the various header search paths to load any available module
  // maps, searching for a module map that describes this module.
  for (unsigned Idx = 0, N = SearchDirs.size(); Idx != N; ++Idx) {
    if (SearchDirs[Idx].isFramework()) {
      // Search for or infer a module map for a framework. Here we use
      // SearchName rather than ModuleName, to permit finding private modules
      // named FooPrivate in buggy frameworks named Foo.
      SmallString<128> FrameworkDirName;
      FrameworkDirName += SearchDirs[Idx].getFrameworkDir()->getName();
      llvm::sys::path::append(FrameworkDirName, SearchName + ".framework");
      if (const DirectoryEntry *FrameworkDir
            = FileMgr.getDirectory(FrameworkDirName)) {
        bool IsSystem
          = SearchDirs[Idx].getDirCharacteristic() != SrcMgr::C_User;
        Module = loadFrameworkModule(ModuleName, FrameworkDir, IsSystem);
        if (Module)
          break;
      }
    }

    // FIXME: Figure out how header maps and module maps will work together.

    // Only deal with normal search directories.
    if (!SearchDirs[Idx].isNormalDir())
      continue;

    bool IsSystem = SearchDirs[Idx].isSystemHeaderDirectory();
    // Search for a module map file in this directory.
    if (loadModuleMapFile(SearchDirs[Idx].getDir(), IsSystem,
                          /*IsFramework*/false) == LMM_NewlyLoaded) {
      // We just loaded a module map file; check whether the module is
      // available now.
      Module = ModMap.findModule(ModuleName);
      if (Module)
        break;
    }

    // Search for a module map in a subdirectory with the same name as the
    // module.
    SmallString<128> NestedModuleMapDirName;
    NestedModuleMapDirName = SearchDirs[Idx].getDir()->getName();
    llvm::sys::path::append(NestedModuleMapDirName, ModuleName);
    if (loadModuleMapFile(NestedModuleMapDirName, IsSystem,
                          /*IsFramework*/false) == LMM_NewlyLoaded){
      // If we just loaded a module map file, look for the module again.
      Module = ModMap.findModule(ModuleName);
      if (Module)
        break;
    }

    // If we've already performed the exhaustive search for module maps in this
    // search directory, don't do it again.
    if (SearchDirs[Idx].haveSearchedAllModuleMaps())
      continue;

    // Load all module maps in the immediate subdirectories of this search
    // directory if ModuleName was from @import.
    if (AllowExtraModuleMapSearch)
      loadSubdirectoryModuleMaps(SearchDirs[Idx]);

    // Look again for the module.
    Module = ModMap.findModule(ModuleName);
    if (Module)
      break;
  }

  return Module;
}

//===----------------------------------------------------------------------===//
// File lookup within a DirectoryLookup scope
//===----------------------------------------------------------------------===//

/// getName - Return the directory or filename corresponding to this lookup
/// object.
StringRef DirectoryLookup::getName() const {
  if (isNormalDir())
    return getDir()->getName();
  if (isFramework())
    return getFrameworkDir()->getName();
  assert(isHeaderMap() && "Unknown DirectoryLookup");
  return getHeaderMap()->getFileName();
}

const FileEntry *HeaderSearch::getFileAndSuggestModule(
    StringRef FileName, SourceLocation IncludeLoc, const DirectoryEntry *Dir,
    bool IsSystemHeaderDir, Module *RequestingModule,
    ModuleMap::KnownHeader *SuggestedModule) {
  // If we have a module map that might map this header, load it and
  // check whether we'll have a suggestion for a module.
  const FileEntry *File = getFileMgr().getFile(FileName, /*OpenFile=*/true);
  if (!File)
    return nullptr;

  // If there is a module that corresponds to this header, suggest it.
  if (!findUsableModuleForHeader(File, Dir ? Dir : File->getDir(),
                                 RequestingModule, SuggestedModule,
                                 IsSystemHeaderDir))
    return nullptr;

  return File;
}

/// LookupFile - Lookup the specified file in this search path, returning it
/// if it exists or returning null if not.
const FileEntry *DirectoryLookup::LookupFile(
    StringRef &Filename,
    HeaderSearch &HS,
    SourceLocation IncludeLoc,
    SmallVectorImpl<char> *SearchPath,
    SmallVectorImpl<char> *RelativePath,
    Module *RequestingModule,
    ModuleMap::KnownHeader *SuggestedModule,
    bool &InUserSpecifiedSystemFramework,
    bool &HasBeenMapped,
    SmallVectorImpl<char> &MappedName) const {
  InUserSpecifiedSystemFramework = false;
  HasBeenMapped = false;

  SmallString<1024> TmpDir;
  if (isNormalDir()) {
    // Concatenate the requested file onto the directory.
    TmpDir = getDir()->getName();
    llvm::sys::path::append(TmpDir, Filename);
    if (SearchPath) {
      StringRef SearchPathRef(getDir()->getName());
      SearchPath->clear();
      SearchPath->append(SearchPathRef.begin(), SearchPathRef.end());
    }
    if (RelativePath) {
      RelativePath->clear();
      RelativePath->append(Filename.begin(), Filename.end());
    }

    return HS.getFileAndSuggestModule(TmpDir, IncludeLoc, getDir(),
                                      isSystemHeaderDirectory(),
                                      RequestingModule, SuggestedModule);
  }

  if (isFramework())
    return DoFrameworkLookup(Filename, HS, SearchPath, RelativePath,
                             RequestingModule, SuggestedModule,
                             InUserSpecifiedSystemFramework);

  assert(isHeaderMap() && "Unknown directory lookup");
  const HeaderMap *HM = getHeaderMap();
  SmallString<1024> Path;
  StringRef Dest = HM->lookupFilename(Filename, Path);
  if (Dest.empty())
    return nullptr;

  const FileEntry *Result;

  // Check if the headermap maps the filename to a framework include
  // ("Foo.h" -> "Foo/Foo.h"), in which case continue header lookup using the
  // framework include.
  if (llvm::sys::path::is_relative(Dest)) {
    MappedName.clear();
    MappedName.append(Dest.begin(), Dest.end());
    Filename = StringRef(MappedName.begin(), MappedName.size());
    HasBeenMapped = true;
    Result = HM->LookupFile(Filename, HS.getFileMgr());
  } else {
    Result = HS.getFileMgr().getFile(Dest);
  }

  if (Result) {
    if (SearchPath) {
      StringRef SearchPathRef(getName());
      SearchPath->clear();
      SearchPath->append(SearchPathRef.begin(), SearchPathRef.end());
    }
    if (RelativePath) {
      RelativePath->clear();
      RelativePath->append(Filename.begin(), Filename.end());
    }
  }
  return Result;
}

/// Given a framework directory, find the top-most framework directory.
///
/// \param FileMgr The file manager to use for directory lookups.
/// \param DirName The name of the framework directory.
/// \param SubmodulePath Will be populated with the submodule path from the
/// returned top-level module to the originally named framework.
static const DirectoryEntry *
getTopFrameworkDir(FileManager &FileMgr, StringRef DirName,
                   SmallVectorImpl<std::string> &SubmodulePath) {
  assert(llvm::sys::path::extension(DirName) == ".framework" &&
         "Not a framework directory");

  // Note: as an egregious but useful hack we use the real path here, because
  // frameworks moving between top-level frameworks to embedded frameworks tend
  // to be symlinked, and we base the logical structure of modules on the
  // physical layout. In particular, we need to deal with crazy includes like
  //
  //   #include <Foo/Frameworks/Bar.framework/Headers/Wibble.h>
  //
  // where 'Bar' used to be embedded in 'Foo', is now a top-level framework
  // which one should access with, e.g.,
  //
  //   #include <Bar/Wibble.h>
  //
  // Similar issues occur when a top-level framework has moved into an
  // embedded framework.
  const DirectoryEntry *TopFrameworkDir = FileMgr.getDirectory(DirName);
  DirName = FileMgr.getCanonicalName(TopFrameworkDir);
  do {
    // Get the parent directory name.
    DirName = llvm::sys::path::parent_path(DirName);
    if (DirName.empty())
      break;

    // Determine whether this directory exists.
    const DirectoryEntry *Dir = FileMgr.getDirectory(DirName);
    if (!Dir)
      break;

    // If this is a framework directory, then we're a subframework of this
    // framework.
    if (llvm::sys::path::extension(DirName) == ".framework") {
      SubmodulePath.push_back(llvm::sys::path::stem(DirName));
      TopFrameworkDir = Dir;
    }
  } while (true);

  return TopFrameworkDir;
}

static bool needModuleLookup(Module *RequestingModule,
                             bool HasSuggestedModule) {
  return HasSuggestedModule ||
         (RequestingModule && RequestingModule->NoUndeclaredIncludes);
}

/// DoFrameworkLookup - Do a lookup of the specified file in the current
/// DirectoryLookup, which is a framework directory.
const FileEntry *DirectoryLookup::DoFrameworkLookup(
    StringRef Filename, HeaderSearch &HS, SmallVectorImpl<char> *SearchPath,
    SmallVectorImpl<char> *RelativePath, Module *RequestingModule,
    ModuleMap::KnownHeader *SuggestedModule,
    bool &InUserSpecifiedSystemFramework) const {
  FileManager &FileMgr = HS.getFileMgr();

  // Framework names must have a '/' in the filename.
  size_t SlashPos = Filename.find('/');
  if (SlashPos == StringRef::npos) return nullptr;

  // Find out if this is the home for the specified framework, by checking
  // HeaderSearch.  Possible answers are yes/no and unknown.
  HeaderSearch::FrameworkCacheEntry &CacheEntry =
    HS.LookupFrameworkCache(Filename.substr(0, SlashPos));

  // If it is known and in some other directory, fail.
  if (CacheEntry.Directory && CacheEntry.Directory != getFrameworkDir())
    return nullptr;

  // Otherwise, construct the path to this framework dir.

  // FrameworkName = "/System/Library/Frameworks/"
  SmallString<1024> FrameworkName;
  FrameworkName += getFrameworkDir()->getName();
  if (FrameworkName.empty() || FrameworkName.back() != '/')
    FrameworkName.push_back('/');

  // FrameworkName = "/System/Library/Frameworks/Cocoa"
  StringRef ModuleName(Filename.begin(), SlashPos);
  FrameworkName += ModuleName;

  // FrameworkName = "/System/Library/Frameworks/Cocoa.framework/"
  FrameworkName += ".framework/";

  // If the cache entry was unresolved, populate it now.
  if (!CacheEntry.Directory) {
    HS.IncrementFrameworkLookupCount();

    // If the framework dir doesn't exist, we fail.
    const DirectoryEntry *Dir = FileMgr.getDirectory(FrameworkName);
    if (!Dir) return nullptr;

    // Otherwise, if it does, remember that this is the right direntry for this
    // framework.
    CacheEntry.Directory = getFrameworkDir();

    // If this is a user search directory, check if the framework has been
    // user-specified as a system framework.
    if (getDirCharacteristic() == SrcMgr::C_User) {
      SmallString<1024> SystemFrameworkMarker(FrameworkName);
      SystemFrameworkMarker += ".system_framework";
      if (llvm::sys::fs::exists(SystemFrameworkMarker)) {
        CacheEntry.IsUserSpecifiedSystemFramework = true;
      }
    }
  }

  // Set the 'user-specified system framework' flag.
  InUserSpecifiedSystemFramework = CacheEntry.IsUserSpecifiedSystemFramework;

  if (RelativePath) {
    RelativePath->clear();
    RelativePath->append(Filename.begin()+SlashPos+1, Filename.end());
  }

  // Check "/System/Library/Frameworks/Cocoa.framework/Headers/file.h"
  unsigned OrigSize = FrameworkName.size();

  FrameworkName += "Headers/";

  if (SearchPath) {
    SearchPath->clear();
    // Without trailing '/'.
    SearchPath->append(FrameworkName.begin(), FrameworkName.end()-1);
  }

  FrameworkName.append(Filename.begin()+SlashPos+1, Filename.end());
  const FileEntry *FE = FileMgr.getFile(FrameworkName,
                                        /*openFile=*/!SuggestedModule);
  if (!FE) {
    // Check "/System/Library/Frameworks/Cocoa.framework/PrivateHeaders/file.h"
    const char *Private = "Private";
    FrameworkName.insert(FrameworkName.begin()+OrigSize, Private,
                         Private+strlen(Private));
    if (SearchPath)
      SearchPath->insert(SearchPath->begin()+OrigSize, Private,
                         Private+strlen(Private));

    FE = FileMgr.getFile(FrameworkName, /*openFile=*/!SuggestedModule);
  }

  // If we found the header and are allowed to suggest a module, do so now.
  if (FE && needModuleLookup(RequestingModule, SuggestedModule)) {
    // Find the framework in which this header occurs.
    StringRef FrameworkPath = FE->getDir()->getName();
    bool FoundFramework = false;
    do {
      // Determine whether this directory exists.
      const DirectoryEntry *Dir = FileMgr.getDirectory(FrameworkPath);
      if (!Dir)
        break;

      // If this is a framework directory, then we're a subframework of this
      // framework.
      if (llvm::sys::path::extension(FrameworkPath) == ".framework") {
        FoundFramework = true;
        break;
      }

      // Get the parent directory name.
      FrameworkPath = llvm::sys::path::parent_path(FrameworkPath);
      if (FrameworkPath.empty())
        break;
    } while (true);

    bool IsSystem = getDirCharacteristic() != SrcMgr::C_User;
    if (FoundFramework) {
      if (!HS.findUsableModuleForFrameworkHeader(
              FE, FrameworkPath, RequestingModule, SuggestedModule, IsSystem))
        return nullptr;
    } else {
      if (!HS.findUsableModuleForHeader(FE, getDir(), RequestingModule,
                                        SuggestedModule, IsSystem))
        return nullptr;
    }
  }
  return FE;
}

void HeaderSearch::setTarget(const TargetInfo &Target) {
  ModMap.setTarget(Target);
}

//===----------------------------------------------------------------------===//
// Header File Location.
//===----------------------------------------------------------------------===//

/// Return true with a diagnostic if the file that MSVC would have found
/// fails to match the one that Clang would have found with MSVC header search
/// disabled.
static bool checkMSVCHeaderSearch(DiagnosticsEngine &Diags,
                                  const FileEntry *MSFE, const FileEntry *FE,
                                  SourceLocation IncludeLoc) {
  if (MSFE && FE != MSFE) {
    Diags.Report(IncludeLoc, diag::ext_pp_include_search_ms) << MSFE->getName();
    return true;
  }
  return false;
}

static const char *copyString(StringRef Str, llvm::BumpPtrAllocator &Alloc) {
  assert(!Str.empty());
  char *CopyStr = Alloc.Allocate<char>(Str.size()+1);
  std::copy(Str.begin(), Str.end(), CopyStr);
  CopyStr[Str.size()] = '\0';
  return CopyStr;
}

static bool isFrameworkStylePath(StringRef Path, bool &IsPrivateHeader,
                                 SmallVectorImpl<char> &FrameworkName) {
  using namespace llvm::sys;
  path::const_iterator I = path::begin(Path);
  path::const_iterator E = path::end(Path);
  IsPrivateHeader = false;

  // Detect different types of framework style paths:
  //
  //   ...Foo.framework/{Headers,PrivateHeaders}
  //   ...Foo.framework/Versions/{A,Current}/{Headers,PrivateHeaders}
  //   ...Foo.framework/Frameworks/Nested.framework/{Headers,PrivateHeaders}
  //   ...<other variations with 'Versions' like in the above path>
  //
  // and some other variations among these lines.
  int FoundComp = 0;
  while (I != E) {
    if (*I == "Headers")
      ++FoundComp;
    if (I->endswith(".framework")) {
      FrameworkName.append(I->begin(), I->end());
      ++FoundComp;
    }
    if (*I == "PrivateHeaders") {
      ++FoundComp;
      IsPrivateHeader = true;
    }
    ++I;
  }

  return !FrameworkName.empty() && FoundComp >= 2;
}

static void
diagnoseFrameworkInclude(DiagnosticsEngine &Diags, SourceLocation IncludeLoc,
                         StringRef Includer, StringRef IncludeFilename,
                         const FileEntry *IncludeFE, bool isAngled = false,
                         bool FoundByHeaderMap = false) {
  bool IsIncluderPrivateHeader = false;
  SmallString<128> FromFramework, ToFramework;
  if (!isFrameworkStylePath(Includer, IsIncluderPrivateHeader, FromFramework))
    return;
  bool IsIncludeePrivateHeader = false;
  bool IsIncludeeInFramework = isFrameworkStylePath(
      IncludeFE->getName(), IsIncludeePrivateHeader, ToFramework);

  if (!isAngled && !FoundByHeaderMap) {
    SmallString<128> NewInclude("<");
    if (IsIncludeeInFramework) {
      NewInclude += StringRef(ToFramework).drop_back(10); // drop .framework
      NewInclude += "/";
    }
    NewInclude += IncludeFilename;
    NewInclude += ">";
    Diags.Report(IncludeLoc, diag::warn_quoted_include_in_framework_header)
        << IncludeFilename
        << FixItHint::CreateReplacement(IncludeLoc, NewInclude);
  }

  // Headers in Foo.framework/Headers should not include headers
  // from Foo.framework/PrivateHeaders, since this violates public/private
  // API boundaries and can cause modular dependency cycles.
  if (!IsIncluderPrivateHeader && IsIncludeeInFramework &&
      IsIncludeePrivateHeader && FromFramework == ToFramework)
    Diags.Report(IncludeLoc, diag::warn_framework_include_private_from_public)
        << IncludeFilename;
}

/// LookupFile - Given a "foo" or \<foo> reference, look up the indicated file,
/// return null on failure.  isAngled indicates whether the file reference is
/// for system \#include's or not (i.e. using <> instead of ""). Includers, if
/// non-empty, indicates where the \#including file(s) are, in case a relative
/// search is needed. Microsoft mode will pass all \#including files.
const FileEntry *HeaderSearch::LookupFile(
    StringRef Filename, SourceLocation IncludeLoc, bool isAngled,
    const DirectoryLookup *FromDir, const DirectoryLookup *&CurDir,
    ArrayRef<std::pair<const FileEntry *, const DirectoryEntry *>> Includers,
    SmallVectorImpl<char> *SearchPath, SmallVectorImpl<char> *RelativePath,
    Module *RequestingModule, ModuleMap::KnownHeader *SuggestedModule,
    bool *IsMapped, bool SkipCache, bool BuildSystemModule) {
  if (IsMapped)
    *IsMapped = false;

  if (SuggestedModule)
    *SuggestedModule = ModuleMap::KnownHeader();

  // If 'Filename' is absolute, check to see if it exists and no searching.
  if (llvm::sys::path::is_absolute(Filename)) {
    CurDir = nullptr;

    // If this was an #include_next "/absolute/file", fail.
    if (FromDir) return nullptr;

    if (SearchPath)
      SearchPath->clear();
    if (RelativePath) {
      RelativePath->clear();
      RelativePath->append(Filename.begin(), Filename.end());
    }
    // Otherwise, just return the file.
    return getFileAndSuggestModule(Filename, IncludeLoc, nullptr,
                                   /*IsSystemHeaderDir*/false,
                                   RequestingModule, SuggestedModule);
  }

  // This is the header that MSVC's header search would have found.
  const FileEntry *MSFE = nullptr;
  ModuleMap::KnownHeader MSSuggestedModule;

  // Unless disabled, check to see if the file is in the #includer's
  // directory.  This cannot be based on CurDir, because each includer could be
  // a #include of a subdirectory (#include "foo/bar.h") and a subsequent
  // include of "baz.h" should resolve to "whatever/foo/baz.h".
  // This search is not done for <> headers.
  if (!Includers.empty() && !isAngled && !NoCurDirSearch) {
    SmallString<1024> TmpDir;
    bool First = true;
    for (const auto &IncluderAndDir : Includers) {
      const FileEntry *Includer = IncluderAndDir.first;

      // Concatenate the requested file onto the directory.
      // FIXME: Portability.  Filename concatenation should be in sys::Path.
      TmpDir = IncluderAndDir.second->getName();
      TmpDir.push_back('/');
      TmpDir.append(Filename.begin(), Filename.end());

      // FIXME: We don't cache the result of getFileInfo across the call to
      // getFileAndSuggestModule, because it's a reference to an element of
      // a container that could be reallocated across this call.
      //
      // If we have no includer, that means we're processing a #include
      // from a module build. We should treat this as a system header if we're
      // building a [system] module.
      bool IncluderIsSystemHeader =
          Includer ? getFileInfo(Includer).DirInfo != SrcMgr::C_User :
          BuildSystemModule;
      if (const FileEntry *FE = getFileAndSuggestModule(
              TmpDir, IncludeLoc, IncluderAndDir.second, IncluderIsSystemHeader,
              RequestingModule, SuggestedModule)) {
        if (!Includer) {
          assert(First && "only first includer can have no file");
          return FE;
        }

        // Leave CurDir unset.
        // This file is a system header or C++ unfriendly if the old file is.
        //
        // Note that we only use one of FromHFI/ToHFI at once, due to potential
        // reallocation of the underlying vector potentially making the first
        // reference binding dangling.
        HeaderFileInfo &FromHFI = getFileInfo(Includer);
        unsigned DirInfo = FromHFI.DirInfo;
        bool IndexHeaderMapHeader = FromHFI.IndexHeaderMapHeader;
        StringRef Framework = FromHFI.Framework;

        HeaderFileInfo &ToHFI = getFileInfo(FE);
        ToHFI.DirInfo = DirInfo;
        ToHFI.IndexHeaderMapHeader = IndexHeaderMapHeader;
        ToHFI.Framework = Framework;

        if (SearchPath) {
          StringRef SearchPathRef(IncluderAndDir.second->getName());
          SearchPath->clear();
          SearchPath->append(SearchPathRef.begin(), SearchPathRef.end());
        }
        if (RelativePath) {
          RelativePath->clear();
          RelativePath->append(Filename.begin(), Filename.end());
        }
        if (First) {
          diagnoseFrameworkInclude(Diags, IncludeLoc,
                                   IncluderAndDir.second->getName(), Filename,
                                   FE);
          return FE;
        }

        // Otherwise, we found the path via MSVC header search rules.  If
        // -Wmsvc-include is enabled, we have to keep searching to see if we
        // would've found this header in -I or -isystem directories.
        if (Diags.isIgnored(diag::ext_pp_include_search_ms, IncludeLoc)) {
          return FE;
        } else {
          MSFE = FE;
          if (SuggestedModule) {
            MSSuggestedModule = *SuggestedModule;
            *SuggestedModule = ModuleMap::KnownHeader();
          }
          break;
        }
      }
      First = false;
    }
  }

  CurDir = nullptr;

  // If this is a system #include, ignore the user #include locs.
  unsigned i = isAngled ? AngledDirIdx : 0;

  // If this is a #include_next request, start searching after the directory the
  // file was found in.
  if (FromDir)
    i = FromDir-&SearchDirs[0];

  // Cache all of the lookups performed by this method.  Many headers are
  // multiply included, and the "pragma once" optimization prevents them from
  // being relex/pp'd, but they would still have to search through a
  // (potentially huge) series of SearchDirs to find it.
  LookupFileCacheInfo &CacheLookup = LookupFileCache[Filename];

  // If the entry has been previously looked up, the first value will be
  // non-zero.  If the value is equal to i (the start point of our search), then
  // this is a matching hit.
  if (!SkipCache && CacheLookup.StartIdx == i+1) {
    // Skip querying potentially lots of directories for this lookup.
    i = CacheLookup.HitIdx;
    if (CacheLookup.MappedName) {
      Filename = CacheLookup.MappedName;
      if (IsMapped)
        *IsMapped = true;
    }
  } else {
    // Otherwise, this is the first query, or the previous query didn't match
    // our search start.  We will fill in our found location below, so prime the
    // start point value.
    CacheLookup.reset(/*StartIdx=*/i+1);
  }

  SmallString<64> MappedName;

  // Check each directory in sequence to see if it contains this file.
  for (; i != SearchDirs.size(); ++i) {
    bool InUserSpecifiedSystemFramework = false;
    bool HasBeenMapped = false;
    const FileEntry *FE = SearchDirs[i].LookupFile(
        Filename, *this, IncludeLoc, SearchPath, RelativePath, RequestingModule,
        SuggestedModule, InUserSpecifiedSystemFramework, HasBeenMapped,
        MappedName);
    if (HasBeenMapped) {
      CacheLookup.MappedName =
          copyString(Filename, LookupFileCache.getAllocator());
      if (IsMapped)
        *IsMapped = true;
    }
    if (!FE) continue;

    CurDir = &SearchDirs[i];

    // This file is a system header or C++ unfriendly if the dir is.
    HeaderFileInfo &HFI = getFileInfo(FE);
    HFI.DirInfo = CurDir->getDirCharacteristic();

    // If the directory characteristic is User but this framework was
    // user-specified to be treated as a system framework, promote the
    // characteristic.
    if (HFI.DirInfo == SrcMgr::C_User && InUserSpecifiedSystemFramework)
      HFI.DirInfo = SrcMgr::C_System;

    // If the filename matches a known system header prefix, override
    // whether the file is a system header.
    for (unsigned j = SystemHeaderPrefixes.size(); j; --j) {
      if (Filename.startswith(SystemHeaderPrefixes[j-1].first)) {
        HFI.DirInfo = SystemHeaderPrefixes[j-1].second ? SrcMgr::C_System
                                                       : SrcMgr::C_User;
        break;
      }
    }

    // If this file is found in a header map and uses the framework style of
    // includes, then this header is part of a framework we're building.
    if (CurDir->isIndexHeaderMap()) {
      size_t SlashPos = Filename.find('/');
      if (SlashPos != StringRef::npos) {
        HFI.IndexHeaderMapHeader = 1;
        HFI.Framework = getUniqueFrameworkName(StringRef(Filename.begin(),
                                                         SlashPos));
      }
    }

    if (checkMSVCHeaderSearch(Diags, MSFE, FE, IncludeLoc)) {
      if (SuggestedModule)
        *SuggestedModule = MSSuggestedModule;
      return MSFE;
    }

    bool FoundByHeaderMap = !IsMapped ? false : *IsMapped;
    if (!Includers.empty())
      diagnoseFrameworkInclude(Diags, IncludeLoc,
                               Includers.front().second->getName(), Filename,
                               FE, isAngled, FoundByHeaderMap);

    // Remember this location for the next lookup we do.
    CacheLookup.HitIdx = i;
    return FE;
  }

  // If we are including a file with a quoted include "foo.h" from inside
  // a header in a framework that is currently being built, and we couldn't
  // resolve "foo.h" any other way, change the include to <Foo/foo.h>, where
  // "Foo" is the name of the framework in which the including header was found.
  if (!Includers.empty() && Includers.front().first && !isAngled &&
      Filename.find('/') == StringRef::npos) {
    HeaderFileInfo &IncludingHFI = getFileInfo(Includers.front().first);
    if (IncludingHFI.IndexHeaderMapHeader) {
      SmallString<128> ScratchFilename;
      ScratchFilename += IncludingHFI.Framework;
      ScratchFilename += '/';
      ScratchFilename += Filename;

      const FileEntry *FE =
          LookupFile(ScratchFilename, IncludeLoc, /*isAngled=*/true, FromDir,
                     CurDir, Includers.front(), SearchPath, RelativePath,
                     RequestingModule, SuggestedModule, IsMapped);

      if (checkMSVCHeaderSearch(Diags, MSFE, FE, IncludeLoc)) {
        if (SuggestedModule)
          *SuggestedModule = MSSuggestedModule;
        return MSFE;
      }

      LookupFileCacheInfo &CacheLookup = LookupFileCache[Filename];
      CacheLookup.HitIdx = LookupFileCache[ScratchFilename].HitIdx;
      // FIXME: SuggestedModule.
      return FE;
    }
  }

  if (checkMSVCHeaderSearch(Diags, MSFE, nullptr, IncludeLoc)) {
    if (SuggestedModule)
      *SuggestedModule = MSSuggestedModule;
    return MSFE;
  }

  // Otherwise, didn't find it. Remember we didn't find this.
  CacheLookup.HitIdx = SearchDirs.size();
  return nullptr;
}

/// LookupSubframeworkHeader - Look up a subframework for the specified
/// \#include file.  For example, if \#include'ing <HIToolbox/HIToolbox.h> from
/// within ".../Carbon.framework/Headers/Carbon.h", check to see if HIToolbox
/// is a subframework within Carbon.framework.  If so, return the FileEntry
/// for the designated file, otherwise return null.
const FileEntry *HeaderSearch::
LookupSubframeworkHeader(StringRef Filename,
                         const FileEntry *ContextFileEnt,
                         SmallVectorImpl<char> *SearchPath,
                         SmallVectorImpl<char> *RelativePath,
                         Module *RequestingModule,
                         ModuleMap::KnownHeader *SuggestedModule) {
  assert(ContextFileEnt && "No context file?");

  // Framework names must have a '/' in the filename.  Find it.
  // FIXME: Should we permit '\' on Windows?
  size_t SlashPos = Filename.find('/');
  if (SlashPos == StringRef::npos) return nullptr;

  // Look up the base framework name of the ContextFileEnt.
  StringRef ContextName = ContextFileEnt->getName();

  // If the context info wasn't a framework, couldn't be a subframework.
  const unsigned DotFrameworkLen = 10;
  auto FrameworkPos = ContextName.find(".framework");
  if (FrameworkPos == StringRef::npos ||
      (ContextName[FrameworkPos + DotFrameworkLen] != '/' &&
       ContextName[FrameworkPos + DotFrameworkLen] != '\\'))
    return nullptr;

  SmallString<1024> FrameworkName(ContextName.data(), ContextName.data() +
                                                          FrameworkPos +
                                                          DotFrameworkLen + 1);

  // Append Frameworks/HIToolbox.framework/
  FrameworkName += "Frameworks/";
  FrameworkName.append(Filename.begin(), Filename.begin()+SlashPos);
  FrameworkName += ".framework/";

  auto &CacheLookup =
      *FrameworkMap.insert(std::make_pair(Filename.substr(0, SlashPos),
                                          FrameworkCacheEntry())).first;

  // Some other location?
  if (CacheLookup.second.Directory &&
      CacheLookup.first().size() == FrameworkName.size() &&
      memcmp(CacheLookup.first().data(), &FrameworkName[0],
             CacheLookup.first().size()) != 0)
    return nullptr;

  // Cache subframework.
  if (!CacheLookup.second.Directory) {
    ++NumSubFrameworkLookups;

    // If the framework dir doesn't exist, we fail.
    const DirectoryEntry *Dir = FileMgr.getDirectory(FrameworkName);
    if (!Dir) return nullptr;

    // Otherwise, if it does, remember that this is the right direntry for this
    // framework.
    CacheLookup.second.Directory = Dir;
  }

  const FileEntry *FE = nullptr;

  if (RelativePath) {
    RelativePath->clear();
    RelativePath->append(Filename.begin()+SlashPos+1, Filename.end());
  }

  // Check ".../Frameworks/HIToolbox.framework/Headers/HIToolbox.h"
  SmallString<1024> HeadersFilename(FrameworkName);
  HeadersFilename += "Headers/";
  if (SearchPath) {
    SearchPath->clear();
    // Without trailing '/'.
    SearchPath->append(HeadersFilename.begin(), HeadersFilename.end()-1);
  }

  HeadersFilename.append(Filename.begin()+SlashPos+1, Filename.end());
  if (!(FE = FileMgr.getFile(HeadersFilename, /*openFile=*/true))) {
    // Check ".../Frameworks/HIToolbox.framework/PrivateHeaders/HIToolbox.h"
    HeadersFilename = FrameworkName;
    HeadersFilename += "PrivateHeaders/";
    if (SearchPath) {
      SearchPath->clear();
      // Without trailing '/'.
      SearchPath->append(HeadersFilename.begin(), HeadersFilename.end()-1);
    }

    HeadersFilename.append(Filename.begin()+SlashPos+1, Filename.end());
    if (!(FE = FileMgr.getFile(HeadersFilename, /*openFile=*/true)))
      return nullptr;
  }

  // This file is a system header or C++ unfriendly if the old file is.
  //
  // Note that the temporary 'DirInfo' is required here, as either call to
  // getFileInfo could resize the vector and we don't want to rely on order
  // of evaluation.
  unsigned DirInfo = getFileInfo(ContextFileEnt).DirInfo;
  getFileInfo(FE).DirInfo = DirInfo;

  FrameworkName.pop_back(); // remove the trailing '/'
  if (!findUsableModuleForFrameworkHeader(FE, FrameworkName, RequestingModule,
                                          SuggestedModule, /*IsSystem*/ false))
    return nullptr;

  return FE;
}

//===----------------------------------------------------------------------===//
// File Info Management.
//===----------------------------------------------------------------------===//

/// Merge the header file info provided by \p OtherHFI into the current
/// header file info (\p HFI)
static void mergeHeaderFileInfo(HeaderFileInfo &HFI,
                                const HeaderFileInfo &OtherHFI) {
  assert(OtherHFI.External && "expected to merge external HFI");

  HFI.isImport |= OtherHFI.isImport;
  HFI.isPragmaOnce |= OtherHFI.isPragmaOnce;
  HFI.isModuleHeader |= OtherHFI.isModuleHeader;
  HFI.NumIncludes += OtherHFI.NumIncludes;

  if (!HFI.ControllingMacro && !HFI.ControllingMacroID) {
    HFI.ControllingMacro = OtherHFI.ControllingMacro;
    HFI.ControllingMacroID = OtherHFI.ControllingMacroID;
  }

  HFI.DirInfo = OtherHFI.DirInfo;
  HFI.External = (!HFI.IsValid || HFI.External);
  HFI.IsValid = true;
  HFI.IndexHeaderMapHeader = OtherHFI.IndexHeaderMapHeader;

  if (HFI.Framework.empty())
    HFI.Framework = OtherHFI.Framework;
}

/// getFileInfo - Return the HeaderFileInfo structure for the specified
/// FileEntry.
HeaderFileInfo &HeaderSearch::getFileInfo(const FileEntry *FE) {
  if (FE->getUID() >= FileInfo.size())
    FileInfo.resize(FE->getUID() + 1);

  HeaderFileInfo *HFI = &FileInfo[FE->getUID()];
  // FIXME: Use a generation count to check whether this is really up to date.
  if (ExternalSource && !HFI->Resolved) {
    HFI->Resolved = true;
    auto ExternalHFI = ExternalSource->GetHeaderFileInfo(FE);

    HFI = &FileInfo[FE->getUID()];
    if (ExternalHFI.External)
      mergeHeaderFileInfo(*HFI, ExternalHFI);
  }

  HFI->IsValid = true;
  // We have local information about this header file, so it's no longer
  // strictly external.
  HFI->External = false;
  return *HFI;
}

const HeaderFileInfo *
HeaderSearch::getExistingFileInfo(const FileEntry *FE,
                                  bool WantExternal) const {
  // If we have an external source, ensure we have the latest information.
  // FIXME: Use a generation count to check whether this is really up to date.
  HeaderFileInfo *HFI;
  if (ExternalSource) {
    if (FE->getUID() >= FileInfo.size()) {
      if (!WantExternal)
        return nullptr;
      FileInfo.resize(FE->getUID() + 1);
    }

    HFI = &FileInfo[FE->getUID()];
    if (!WantExternal && (!HFI->IsValid || HFI->External))
      return nullptr;
    if (!HFI->Resolved) {
      HFI->Resolved = true;
      auto ExternalHFI = ExternalSource->GetHeaderFileInfo(FE);

      HFI = &FileInfo[FE->getUID()];
      if (ExternalHFI.External)
        mergeHeaderFileInfo(*HFI, ExternalHFI);
    }
  } else if (FE->getUID() >= FileInfo.size()) {
    return nullptr;
  } else {
    HFI = &FileInfo[FE->getUID()];
  }

  if (!HFI->IsValid || (HFI->External && !WantExternal))
    return nullptr;

  return HFI;
}

bool HeaderSearch::isFileMultipleIncludeGuarded(const FileEntry *File) {
  // Check if we've ever seen this file as a header.
  if (auto *HFI = getExistingFileInfo(File))
    return HFI->isPragmaOnce || HFI->isImport || HFI->ControllingMacro ||
           HFI->ControllingMacroID;
  return false;
}

void HeaderSearch::MarkFileModuleHeader(const FileEntry *FE,
                                        ModuleMap::ModuleHeaderRole Role,
                                        bool isCompilingModuleHeader) {
  bool isModularHeader = !(Role & ModuleMap::TextualHeader);

  // Don't mark the file info as non-external if there's nothing to change.
  if (!isCompilingModuleHeader) {
    if (!isModularHeader)
      return;
    auto *HFI = getExistingFileInfo(FE);
    if (HFI && HFI->isModuleHeader)
      return;
  }

  auto &HFI = getFileInfo(FE);
  HFI.isModuleHeader |= isModularHeader;
  HFI.isCompilingModuleHeader |= isCompilingModuleHeader;
}

bool HeaderSearch::ShouldEnterIncludeFile(Preprocessor &PP,
                                          const FileEntry *File, bool isImport,
                                          bool ModulesEnabled, Module *M) {
  ++NumIncluded; // Count # of attempted #includes.

  // Get information about this file.
  HeaderFileInfo &FileInfo = getFileInfo(File);

  // FIXME: this is a workaround for the lack of proper modules-aware support
  // for #import / #pragma once
  auto TryEnterImported = [&]() -> bool {
    if (!ModulesEnabled)
      return false;
    // Ensure FileInfo bits are up to date.
    ModMap.resolveHeaderDirectives(File);
    // Modules with builtins are special; multiple modules use builtins as
    // modular headers, example:
    //
    //    module stddef { header "stddef.h" export * }
    //
    // After module map parsing, this expands to:
    //
    //    module stddef {
    //      header "/path_to_builtin_dirs/stddef.h"
    //      textual "stddef.h"
    //    }
    //
    // It's common that libc++ and system modules will both define such
    // submodules. Make sure cached results for a builtin header won't
    // prevent other builtin modules to potentially enter the builtin header.
    // Note that builtins are header guarded and the decision to actually
    // enter them is postponed to the controlling macros logic below.
    bool TryEnterHdr = false;
    if (FileInfo.isCompilingModuleHeader && FileInfo.isModuleHeader)
      TryEnterHdr = File->getDir() == ModMap.getBuiltinDir() &&
                    ModuleMap::isBuiltinHeader(
                        llvm::sys::path::filename(File->getName()));

    // Textual headers can be #imported from different modules. Since ObjC
    // headers find in the wild might rely only on #import and do not contain
    // controlling macros, be conservative and only try to enter textual headers
    // if such macro is present.
    if (!FileInfo.isModuleHeader &&
        FileInfo.getControllingMacro(ExternalLookup))
      TryEnterHdr = true;
    return TryEnterHdr;
  };

  // If this is a #import directive, check that we have not already imported
  // this header.
  if (isImport) {
    // If this has already been imported, don't import it again.
    FileInfo.isImport = true;

    // Has this already been #import'ed or #include'd?
    if (FileInfo.NumIncludes && !TryEnterImported())
      return false;
  } else {
    // Otherwise, if this is a #include of a file that was previously #import'd
    // or if this is the second #include of a #pragma once file, ignore it.
    if (FileInfo.isImport && !TryEnterImported())
      return false;
  }

  // Next, check to see if the file is wrapped with #ifndef guards.  If so, and
  // if the macro that guards it is defined, we know the #include has no effect.
  if (const IdentifierInfo *ControllingMacro
      = FileInfo.getControllingMacro(ExternalLookup)) {
    // If the header corresponds to a module, check whether the macro is already
    // defined in that module rather than checking in the current set of visible
    // modules.
    if (M ? PP.isMacroDefinedInLocalModule(ControllingMacro, M)
          : PP.isMacroDefined(ControllingMacro)) {
      ++NumMultiIncludeFileOptzn;
      return false;
    }
  }

  // Increment the number of times this file has been included.
  ++FileInfo.NumIncludes;

  return true;
}

size_t HeaderSearch::getTotalMemory() const {
  return SearchDirs.capacity()
    + llvm::capacity_in_bytes(FileInfo)
    + llvm::capacity_in_bytes(HeaderMaps)
    + LookupFileCache.getAllocator().getTotalMemory()
    + FrameworkMap.getAllocator().getTotalMemory();
}

StringRef HeaderSearch::getUniqueFrameworkName(StringRef Framework) {
  return FrameworkNames.insert(Framework).first->first();
}

bool HeaderSearch::hasModuleMap(StringRef FileName,
                                const DirectoryEntry *Root,
                                bool IsSystem) {
  if (!HSOpts->ImplicitModuleMaps)
    return false;

  SmallVector<const DirectoryEntry *, 2> FixUpDirectories;

  StringRef DirName = FileName;
  do {
    // Get the parent directory name.
    DirName = llvm::sys::path::parent_path(DirName);
    if (DirName.empty())
      return false;

    // Determine whether this directory exists.
    const DirectoryEntry *Dir = FileMgr.getDirectory(DirName);
    if (!Dir)
      return false;

    // Try to load the module map file in this directory.
    switch (loadModuleMapFile(Dir, IsSystem,
                              llvm::sys::path::extension(Dir->getName()) ==
                                  ".framework")) {
    case LMM_NewlyLoaded:
    case LMM_AlreadyLoaded:
      // Success. All of the directories we stepped through inherit this module
      // map file.
      for (unsigned I = 0, N = FixUpDirectories.size(); I != N; ++I)
        DirectoryHasModuleMap[FixUpDirectories[I]] = true;
      return true;

    case LMM_NoDirectory:
    case LMM_InvalidModuleMap:
      break;
    }

    // If we hit the top of our search, we're done.
    if (Dir == Root)
      return false;

    // Keep track of all of the directories we checked, so we can mark them as
    // having module maps if we eventually do find a module map.
    FixUpDirectories.push_back(Dir);
  } while (true);
}

ModuleMap::KnownHeader
HeaderSearch::findModuleForHeader(const FileEntry *File,
                                  bool AllowTextual) const {
  if (ExternalSource) {
    // Make sure the external source has handled header info about this file,
    // which includes whether the file is part of a module.
    (void)getExistingFileInfo(File);
  }
  return ModMap.findModuleForHeader(File, AllowTextual);
}

static bool suggestModule(HeaderSearch &HS, const FileEntry *File,
                          Module *RequestingModule,
                          ModuleMap::KnownHeader *SuggestedModule) {
  ModuleMap::KnownHeader Module =
      HS.findModuleForHeader(File, /*AllowTextual*/true);
  if (SuggestedModule)
    *SuggestedModule = (Module.getRole() & ModuleMap::TextualHeader)
                           ? ModuleMap::KnownHeader()
                           : Module;

  // If this module specifies [no_undeclared_includes], we cannot find any
  // file that's in a non-dependency module.
  if (RequestingModule && Module && RequestingModule->NoUndeclaredIncludes) {
    HS.getModuleMap().resolveUses(RequestingModule, /*Complain*/false);
    if (!RequestingModule->directlyUses(Module.getModule())) {
      return false;
    }
  }

  return true;
}

bool HeaderSearch::findUsableModuleForHeader(
    const FileEntry *File, const DirectoryEntry *Root, Module *RequestingModule,
    ModuleMap::KnownHeader *SuggestedModule, bool IsSystemHeaderDir) {
  if (File && needModuleLookup(RequestingModule, SuggestedModule)) {
    // If there is a module that corresponds to this header, suggest it.
    hasModuleMap(File->getName(), Root, IsSystemHeaderDir);
    return suggestModule(*this, File, RequestingModule, SuggestedModule);
  }
  return true;
}

bool HeaderSearch::findUsableModuleForFrameworkHeader(
    const FileEntry *File, StringRef FrameworkName, Module *RequestingModule,
    ModuleMap::KnownHeader *SuggestedModule, bool IsSystemFramework) {
  // If we're supposed to suggest a module, look for one now.
  if (needModuleLookup(RequestingModule, SuggestedModule)) {
    // Find the top-level framework based on this framework.
    SmallVector<std::string, 4> SubmodulePath;
    const DirectoryEntry *TopFrameworkDir
      = ::getTopFrameworkDir(FileMgr, FrameworkName, SubmodulePath);

    // Determine the name of the top-level framework.
    StringRef ModuleName = llvm::sys::path::stem(TopFrameworkDir->getName());

    // Load this framework module. If that succeeds, find the suggested module
    // for this header, if any.
    loadFrameworkModule(ModuleName, TopFrameworkDir, IsSystemFramework);

    // FIXME: This can find a module not part of ModuleName, which is
    // important so that we're consistent about whether this header
    // corresponds to a module. Possibly we should lock down framework modules
    // so that this is not possible.
    return suggestModule(*this, File, RequestingModule, SuggestedModule);
  }
  return true;
}

static const FileEntry *getPrivateModuleMap(const FileEntry *File,
                                            FileManager &FileMgr) {
  StringRef Filename = llvm::sys::path::filename(File->getName());
  SmallString<128>  PrivateFilename(File->getDir()->getName());
  if (Filename == "module.map")
    llvm::sys::path::append(PrivateFilename, "module_private.map");
  else if (Filename == "module.modulemap")
    llvm::sys::path::append(PrivateFilename, "module.private.modulemap");
  else
    return nullptr;
  return FileMgr.getFile(PrivateFilename);
}

bool HeaderSearch::loadModuleMapFile(const FileEntry *File, bool IsSystem,
                                     FileID ID, unsigned *Offset,
                                     StringRef OriginalModuleMapFile) {
  // Find the directory for the module. For frameworks, that may require going
  // up from the 'Modules' directory.
  const DirectoryEntry *Dir = nullptr;
  if (getHeaderSearchOpts().ModuleMapFileHomeIsCwd)
    Dir = FileMgr.getDirectory(".");
  else {
    if (!OriginalModuleMapFile.empty()) {
      // We're building a preprocessed module map. Find or invent the directory
      // that it originally occupied.
      Dir = FileMgr.getDirectory(
          llvm::sys::path::parent_path(OriginalModuleMapFile));
      if (!Dir) {
        auto *FakeFile = FileMgr.getVirtualFile(OriginalModuleMapFile, 0, 0);
        Dir = FakeFile->getDir();
      }
    } else {
      Dir = File->getDir();
    }

    StringRef DirName(Dir->getName());
    if (llvm::sys::path::filename(DirName) == "Modules") {
      DirName = llvm::sys::path::parent_path(DirName);
      if (DirName.endswith(".framework"))
        Dir = FileMgr.getDirectory(DirName);
      // FIXME: This assert can fail if there's a race between the above check
      // and the removal of the directory.
      assert(Dir && "parent must exist");
    }
  }

  switch (loadModuleMapFileImpl(File, IsSystem, Dir, ID, Offset)) {
  case LMM_AlreadyLoaded:
  case LMM_NewlyLoaded:
    return false;
  case LMM_NoDirectory:
  case LMM_InvalidModuleMap:
    return true;
  }
  llvm_unreachable("Unknown load module map result");
}

HeaderSearch::LoadModuleMapResult
HeaderSearch::loadModuleMapFileImpl(const FileEntry *File, bool IsSystem,
                                    const DirectoryEntry *Dir, FileID ID,
                                    unsigned *Offset) {
  assert(File && "expected FileEntry");

  // Check whether we've already loaded this module map, and mark it as being
  // loaded in case we recursively try to load it from itself.
  auto AddResult = LoadedModuleMaps.insert(std::make_pair(File, true));
  if (!AddResult.second)
    return AddResult.first->second ? LMM_AlreadyLoaded : LMM_InvalidModuleMap;

  if (ModMap.parseModuleMapFile(File, IsSystem, Dir, ID, Offset)) {
    LoadedModuleMaps[File] = false;
    return LMM_InvalidModuleMap;
  }

  // Try to load a corresponding private module map.
  if (const FileEntry *PMMFile = getPrivateModuleMap(File, FileMgr)) {
    if (ModMap.parseModuleMapFile(PMMFile, IsSystem, Dir)) {
      LoadedModuleMaps[File] = false;
      return LMM_InvalidModuleMap;
    }
  }

  // This directory has a module map.
  return LMM_NewlyLoaded;
}

const FileEntry *
HeaderSearch::lookupModuleMapFile(const DirectoryEntry *Dir, bool IsFramework) {
  if (!HSOpts->ImplicitModuleMaps)
    return nullptr;
  // For frameworks, the preferred spelling is Modules/module.modulemap, but
  // module.map at the framework root is also accepted.
  SmallString<128> ModuleMapFileName(Dir->getName());
  if (IsFramework)
    llvm::sys::path::append(ModuleMapFileName, "Modules");
  llvm::sys::path::append(ModuleMapFileName, "module.modulemap");
  if (const FileEntry *F = FileMgr.getFile(ModuleMapFileName))
    return F;

  // Continue to allow module.map
  ModuleMapFileName = Dir->getName();
  llvm::sys::path::append(ModuleMapFileName, "module.map");
  return FileMgr.getFile(ModuleMapFileName);
}

Module *HeaderSearch::loadFrameworkModule(StringRef Name,
                                          const DirectoryEntry *Dir,
                                          bool IsSystem) {
  if (Module *Module = ModMap.findModule(Name))
    return Module;

  // Try to load a module map file.
  switch (loadModuleMapFile(Dir, IsSystem, /*IsFramework*/true)) {
  case LMM_InvalidModuleMap:
    // Try to infer a module map from the framework directory.
    if (HSOpts->ImplicitModuleMaps)
      ModMap.inferFrameworkModule(Dir, IsSystem, /*Parent=*/nullptr);
    break;

  case LMM_AlreadyLoaded:
  case LMM_NoDirectory:
    return nullptr;

  case LMM_NewlyLoaded:
    break;
  }

  return ModMap.findModule(Name);
}

HeaderSearch::LoadModuleMapResult
HeaderSearch::loadModuleMapFile(StringRef DirName, bool IsSystem,
                                bool IsFramework) {
  if (const DirectoryEntry *Dir = FileMgr.getDirectory(DirName))
    return loadModuleMapFile(Dir, IsSystem, IsFramework);

  return LMM_NoDirectory;
}

HeaderSearch::LoadModuleMapResult
HeaderSearch::loadModuleMapFile(const DirectoryEntry *Dir, bool IsSystem,
                                bool IsFramework) {
  auto KnownDir = DirectoryHasModuleMap.find(Dir);
  if (KnownDir != DirectoryHasModuleMap.end())
    return KnownDir->second ? LMM_AlreadyLoaded : LMM_InvalidModuleMap;

  if (const FileEntry *ModuleMapFile = lookupModuleMapFile(Dir, IsFramework)) {
    LoadModuleMapResult Result =
        loadModuleMapFileImpl(ModuleMapFile, IsSystem, Dir);
    // Add Dir explicitly in case ModuleMapFile is in a subdirectory.
    // E.g. Foo.framework/Modules/module.modulemap
    //      ^Dir                  ^ModuleMapFile
    if (Result == LMM_NewlyLoaded)
      DirectoryHasModuleMap[Dir] = true;
    else if (Result == LMM_InvalidModuleMap)
      DirectoryHasModuleMap[Dir] = false;
    return Result;
  }
  return LMM_InvalidModuleMap;
}

void HeaderSearch::collectAllModules(SmallVectorImpl<Module *> &Modules) {
  Modules.clear();

  if (HSOpts->ImplicitModuleMaps) {
    // Load module maps for each of the header search directories.
    for (unsigned Idx = 0, N = SearchDirs.size(); Idx != N; ++Idx) {
      bool IsSystem = SearchDirs[Idx].isSystemHeaderDirectory();
      if (SearchDirs[Idx].isFramework()) {
        std::error_code EC;
        SmallString<128> DirNative;
        llvm::sys::path::native(SearchDirs[Idx].getFrameworkDir()->getName(),
                                DirNative);

        // Search each of the ".framework" directories to load them as modules.
        llvm::vfs::FileSystem &FS = *FileMgr.getVirtualFileSystem();
        for (llvm::vfs::directory_iterator Dir = FS.dir_begin(DirNative, EC),
                                           DirEnd;
             Dir != DirEnd && !EC; Dir.increment(EC)) {
          if (llvm::sys::path::extension(Dir->path()) != ".framework")
            continue;

          const DirectoryEntry *FrameworkDir =
              FileMgr.getDirectory(Dir->path());
          if (!FrameworkDir)
            continue;

          // Load this framework module.
          loadFrameworkModule(llvm::sys::path::stem(Dir->path()), FrameworkDir,
                              IsSystem);
        }
        continue;
      }

      // FIXME: Deal with header maps.
      if (SearchDirs[Idx].isHeaderMap())
        continue;

      // Try to load a module map file for the search directory.
      loadModuleMapFile(SearchDirs[Idx].getDir(), IsSystem,
                        /*IsFramework*/ false);

      // Try to load module map files for immediate subdirectories of this
      // search directory.
      loadSubdirectoryModuleMaps(SearchDirs[Idx]);
    }
  }

  // Populate the list of modules.
  for (ModuleMap::module_iterator M = ModMap.module_begin(),
                               MEnd = ModMap.module_end();
       M != MEnd; ++M) {
    Modules.push_back(M->getValue());
  }
}

void HeaderSearch::loadTopLevelSystemModules() {
  if (!HSOpts->ImplicitModuleMaps)
    return;

  // Load module maps for each of the header search directories.
  for (unsigned Idx = 0, N = SearchDirs.size(); Idx != N; ++Idx) {
    // We only care about normal header directories.
    if (!SearchDirs[Idx].isNormalDir()) {
      continue;
    }

    // Try to load a module map file for the search directory.
    loadModuleMapFile(SearchDirs[Idx].getDir(),
                      SearchDirs[Idx].isSystemHeaderDirectory(),
                      SearchDirs[Idx].isFramework());
  }
}

void HeaderSearch::loadSubdirectoryModuleMaps(DirectoryLookup &SearchDir) {
  assert(HSOpts->ImplicitModuleMaps &&
         "Should not be loading subdirectory module maps");

  if (SearchDir.haveSearchedAllModuleMaps())
    return;

  std::error_code EC;
  SmallString<128> Dir = SearchDir.getDir()->getName();
  FileMgr.makeAbsolutePath(Dir);
  SmallString<128> DirNative;
  llvm::sys::path::native(Dir, DirNative);
  llvm::vfs::FileSystem &FS = *FileMgr.getVirtualFileSystem();
  for (llvm::vfs::directory_iterator Dir = FS.dir_begin(DirNative, EC), DirEnd;
       Dir != DirEnd && !EC; Dir.increment(EC)) {
    bool IsFramework = llvm::sys::path::extension(Dir->path()) == ".framework";
    if (IsFramework == SearchDir.isFramework())
      loadModuleMapFile(Dir->path(), SearchDir.isSystemHeaderDirectory(),
                        SearchDir.isFramework());
  }

  SearchDir.setSearchedAllModuleMaps(true);
}

std::string HeaderSearch::suggestPathToFileForDiagnostics(const FileEntry *File,
                                                          bool *IsSystem) {
  // FIXME: We assume that the path name currently cached in the FileEntry is
  // the most appropriate one for this analysis (and that it's spelled the
  // same way as the corresponding header search path).
  return suggestPathToFileForDiagnostics(File->getName(), /*BuildDir=*/"",
                                         IsSystem);
}

std::string HeaderSearch::suggestPathToFileForDiagnostics(
    llvm::StringRef File, llvm::StringRef WorkingDir, bool *IsSystem) {
  using namespace llvm::sys;

  unsigned BestPrefixLength = 0;
  unsigned BestSearchDir;

  for (unsigned I = 0; I != SearchDirs.size(); ++I) {
    // FIXME: Support this search within frameworks and header maps.
    if (!SearchDirs[I].isNormalDir())
      continue;

    StringRef Dir = SearchDirs[I].getDir()->getName();
    llvm::SmallString<32> DirPath(Dir.begin(), Dir.end());
    if (!WorkingDir.empty() && !path::is_absolute(Dir)) {
      fs::make_absolute(WorkingDir, DirPath);
      path::remove_dots(DirPath, /*remove_dot_dot=*/true);
      Dir = DirPath;
    }
    for (auto NI = path::begin(File), NE = path::end(File),
              DI = path::begin(Dir), DE = path::end(Dir);
         /*termination condition in loop*/; ++NI, ++DI) {
      // '.' components in File are ignored.
      while (NI != NE && *NI == ".")
        ++NI;
      if (NI == NE)
        break;

      // '.' components in Dir are ignored.
      while (DI != DE && *DI == ".")
        ++DI;
      if (DI == DE) {
        // Dir is a prefix of File, up to '.' components and choice of path
        // separators.
        unsigned PrefixLength = NI - path::begin(File);
        if (PrefixLength > BestPrefixLength) {
          BestPrefixLength = PrefixLength;
          BestSearchDir = I;
        }
        break;
      }

      if (*NI != *DI)
        break;
    }
  }

  if (IsSystem)
    *IsSystem = BestPrefixLength ? BestSearchDir >= SystemDirIdx : false;
  return File.drop_front(BestPrefixLength);
}
