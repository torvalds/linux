//===--- ModuleDependencyCollector.cpp - Collect module dependencies ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Collect the dependencies of a set of modules.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/CharInfo.h"
#include "clang/Frontend/Utils.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Serialization/ASTReader.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;

namespace {
/// Private implementations for ModuleDependencyCollector
class ModuleDependencyListener : public ASTReaderListener {
  ModuleDependencyCollector &Collector;
public:
  ModuleDependencyListener(ModuleDependencyCollector &Collector)
      : Collector(Collector) {}
  bool needsInputFileVisitation() override { return true; }
  bool needsSystemInputFileVisitation() override { return true; }
  bool visitInputFile(StringRef Filename, bool IsSystem, bool IsOverridden,
                      bool IsExplicitModule) override {
    Collector.addFile(Filename);
    return true;
  }
};

struct ModuleDependencyPPCallbacks : public PPCallbacks {
  ModuleDependencyCollector &Collector;
  SourceManager &SM;
  ModuleDependencyPPCallbacks(ModuleDependencyCollector &Collector,
                              SourceManager &SM)
      : Collector(Collector), SM(SM) {}

  void InclusionDirective(SourceLocation HashLoc, const Token &IncludeTok,
                          StringRef FileName, bool IsAngled,
                          CharSourceRange FilenameRange, const FileEntry *File,
                          StringRef SearchPath, StringRef RelativePath,
                          const Module *Imported,
                          SrcMgr::CharacteristicKind FileType) override {
    if (!File)
      return;
    Collector.addFile(File->getName());
  }
};

struct ModuleDependencyMMCallbacks : public ModuleMapCallbacks {
  ModuleDependencyCollector &Collector;
  ModuleDependencyMMCallbacks(ModuleDependencyCollector &Collector)
      : Collector(Collector) {}

  void moduleMapAddHeader(StringRef HeaderPath) override {
    if (llvm::sys::path::is_absolute(HeaderPath))
      Collector.addFile(HeaderPath);
  }
  void moduleMapAddUmbrellaHeader(FileManager *FileMgr,
                                  const FileEntry *Header) override {
    StringRef HeaderFilename = Header->getName();
    moduleMapAddHeader(HeaderFilename);
    // The FileManager can find and cache the symbolic link for a framework
    // header before its real path, this means a module can have some of its
    // headers to use other paths. Although this is usually not a problem, it's
    // inconsistent, and not collecting the original path header leads to
    // umbrella clashes while rebuilding modules in the crash reproducer. For
    // example:
    //    ApplicationServices.framework/Frameworks/ImageIO.framework/ImageIO.h
    // instead of:
    //    ImageIO.framework/ImageIO.h
    //
    // FIXME: this shouldn't be necessary once we have FileName instances
    // around instead of FileEntry ones. For now, make sure we collect all
    // that we need for the reproducer to work correctly.
    StringRef UmbreallDirFromHeader =
        llvm::sys::path::parent_path(HeaderFilename);
    StringRef UmbrellaDir = Header->getDir()->getName();
    if (!UmbrellaDir.equals(UmbreallDirFromHeader)) {
      SmallString<128> AltHeaderFilename;
      llvm::sys::path::append(AltHeaderFilename, UmbrellaDir,
                              llvm::sys::path::filename(HeaderFilename));
      if (FileMgr->getFile(AltHeaderFilename))
        moduleMapAddHeader(AltHeaderFilename);
    }
  }
};

}

// TODO: move this to Support/Path.h and check for HAVE_REALPATH?
static bool real_path(StringRef SrcPath, SmallVectorImpl<char> &RealPath) {
#ifdef LLVM_ON_UNIX
  char CanonicalPath[PATH_MAX];

  // TODO: emit a warning in case this fails...?
  if (!realpath(SrcPath.str().c_str(), CanonicalPath))
    return false;

  SmallString<256> RPath(CanonicalPath);
  RealPath.swap(RPath);
  return true;
#else
  // FIXME: Add support for systems without realpath.
  return false;
#endif
}

void ModuleDependencyCollector::attachToASTReader(ASTReader &R) {
  R.addListener(llvm::make_unique<ModuleDependencyListener>(*this));
}

void ModuleDependencyCollector::attachToPreprocessor(Preprocessor &PP) {
  PP.addPPCallbacks(llvm::make_unique<ModuleDependencyPPCallbacks>(
      *this, PP.getSourceManager()));
  PP.getHeaderSearchInfo().getModuleMap().addModuleMapCallbacks(
      llvm::make_unique<ModuleDependencyMMCallbacks>(*this));
}

static bool isCaseSensitivePath(StringRef Path) {
  SmallString<256> TmpDest = Path, UpperDest, RealDest;
  // Remove component traversals, links, etc.
  if (!real_path(Path, TmpDest))
    return true; // Current default value in vfs.yaml
  Path = TmpDest;

  // Change path to all upper case and ask for its real path, if the latter
  // exists and is equal to Path, it's not case sensitive. Default to case
  // sensitive in the absence of realpath, since this is what the VFSWriter
  // already expects when sensitivity isn't setup.
  for (auto &C : Path)
    UpperDest.push_back(toUppercase(C));
  if (real_path(UpperDest, RealDest) && Path.equals(RealDest))
    return false;
  return true;
}

void ModuleDependencyCollector::writeFileMap() {
  if (Seen.empty())
    return;

  StringRef VFSDir = getDest();

  // Default to use relative overlay directories in the VFS yaml file. This
  // allows crash reproducer scripts to work across machines.
  VFSWriter.setOverlayDir(VFSDir);

  // Explicitly set case sensitivity for the YAML writer. For that, find out
  // the sensitivity at the path where the headers all collected to.
  VFSWriter.setCaseSensitivity(isCaseSensitivePath(VFSDir));

  // Do not rely on real path names when executing the crash reproducer scripts
  // since we only want to actually use the files we have on the VFS cache.
  VFSWriter.setUseExternalNames(false);

  std::error_code EC;
  SmallString<256> YAMLPath = VFSDir;
  llvm::sys::path::append(YAMLPath, "vfs.yaml");
  llvm::raw_fd_ostream OS(YAMLPath, EC, llvm::sys::fs::F_Text);
  if (EC) {
    HasErrors = true;
    return;
  }
  VFSWriter.write(OS);
}

bool ModuleDependencyCollector::getRealPath(StringRef SrcPath,
                                            SmallVectorImpl<char> &Result) {
  using namespace llvm::sys;
  SmallString<256> RealPath;
  StringRef FileName = path::filename(SrcPath);
  std::string Dir = path::parent_path(SrcPath).str();
  auto DirWithSymLink = SymLinkMap.find(Dir);

  // Use real_path to fix any symbolic link component present in a path.
  // Computing the real path is expensive, cache the search through the
  // parent path directory.
  if (DirWithSymLink == SymLinkMap.end()) {
    if (!real_path(Dir, RealPath))
      return false;
    SymLinkMap[Dir] = RealPath.str();
  } else {
    RealPath = DirWithSymLink->second;
  }

  path::append(RealPath, FileName);
  Result.swap(RealPath);
  return true;
}

std::error_code ModuleDependencyCollector::copyToRoot(StringRef Src,
                                                      StringRef Dst) {
  using namespace llvm::sys;

  // We need an absolute src path to append to the root.
  SmallString<256> AbsoluteSrc = Src;
  fs::make_absolute(AbsoluteSrc);
  // Canonicalize src to a native path to avoid mixed separator styles.
  path::native(AbsoluteSrc);
  // Remove redundant leading "./" pieces and consecutive separators.
  AbsoluteSrc = path::remove_leading_dotslash(AbsoluteSrc);

  // Canonicalize the source path by removing "..", "." components.
  SmallString<256> VirtualPath = AbsoluteSrc;
  path::remove_dots(VirtualPath, /*remove_dot_dot=*/true);

  // If a ".." component is present after a symlink component, remove_dots may
  // lead to the wrong real destination path. Let the source be canonicalized
  // like that but make sure we always use the real path for the destination.
  SmallString<256> CopyFrom;
  if (!getRealPath(AbsoluteSrc, CopyFrom))
    CopyFrom = VirtualPath;
  SmallString<256> CacheDst = getDest();

  if (Dst.empty()) {
    // The common case is to map the virtual path to the same path inside the
    // cache.
    path::append(CacheDst, path::relative_path(CopyFrom));
  } else {
    // When collecting entries from input vfsoverlays, copy the external
    // contents into the cache but still map from the source.
    if (!fs::exists(Dst))
      return std::error_code();
    path::append(CacheDst, Dst);
    CopyFrom = Dst;
  }

  // Copy the file into place.
  if (std::error_code EC = fs::create_directories(path::parent_path(CacheDst),
                                                  /*IgnoreExisting=*/true))
    return EC;
  if (std::error_code EC = fs::copy_file(CopyFrom, CacheDst))
    return EC;

  // Always map a canonical src path to its real path into the YAML, by doing
  // this we map different virtual src paths to the same entry in the VFS
  // overlay, which is a way to emulate symlink inside the VFS; this is also
  // needed for correctness, not doing that can lead to module redefinition
  // errors.
  addFileMapping(VirtualPath, CacheDst);
  return std::error_code();
}

void ModuleDependencyCollector::addFile(StringRef Filename, StringRef FileDst) {
  if (insertSeen(Filename))
    if (copyToRoot(Filename, FileDst))
      HasErrors = true;
}
