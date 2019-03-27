//===- VirtualFileSystem.h - Virtual File System Layer ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Defines the virtual file system interface vfs::FileSystem.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_VIRTUALFILESYSTEM_H
#define LLVM_SUPPORT_VIRTUALFILESYSTEM_H

#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Chrono.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SourceMgr.h"
#include <cassert>
#include <cstdint>
#include <ctime>
#include <memory>
#include <stack>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace llvm {

class MemoryBuffer;

namespace vfs {

/// The result of a \p status operation.
class Status {
  std::string Name;
  llvm::sys::fs::UniqueID UID;
  llvm::sys::TimePoint<> MTime;
  uint32_t User;
  uint32_t Group;
  uint64_t Size;
  llvm::sys::fs::file_type Type = llvm::sys::fs::file_type::status_error;
  llvm::sys::fs::perms Perms;

public:
  // FIXME: remove when files support multiple names
  bool IsVFSMapped = false;

  Status() = default;
  Status(const llvm::sys::fs::file_status &Status);
  Status(StringRef Name, llvm::sys::fs::UniqueID UID,
         llvm::sys::TimePoint<> MTime, uint32_t User, uint32_t Group,
         uint64_t Size, llvm::sys::fs::file_type Type,
         llvm::sys::fs::perms Perms);

  /// Get a copy of a Status with a different name.
  static Status copyWithNewName(const Status &In, StringRef NewName);
  static Status copyWithNewName(const llvm::sys::fs::file_status &In,
                                StringRef NewName);

  /// Returns the name that should be used for this file or directory.
  StringRef getName() const { return Name; }

  /// @name Status interface from llvm::sys::fs
  /// @{
  llvm::sys::fs::file_type getType() const { return Type; }
  llvm::sys::fs::perms getPermissions() const { return Perms; }
  llvm::sys::TimePoint<> getLastModificationTime() const { return MTime; }
  llvm::sys::fs::UniqueID getUniqueID() const { return UID; }
  uint32_t getUser() const { return User; }
  uint32_t getGroup() const { return Group; }
  uint64_t getSize() const { return Size; }
  /// @}
  /// @name Status queries
  /// These are static queries in llvm::sys::fs.
  /// @{
  bool equivalent(const Status &Other) const;
  bool isDirectory() const;
  bool isRegularFile() const;
  bool isOther() const;
  bool isSymlink() const;
  bool isStatusKnown() const;
  bool exists() const;
  /// @}
};

/// Represents an open file.
class File {
public:
  /// Destroy the file after closing it (if open).
  /// Sub-classes should generally call close() inside their destructors.  We
  /// cannot do that from the base class, since close is virtual.
  virtual ~File();

  /// Get the status of the file.
  virtual llvm::ErrorOr<Status> status() = 0;

  /// Get the name of the file
  virtual llvm::ErrorOr<std::string> getName() {
    if (auto Status = status())
      return Status->getName().str();
    else
      return Status.getError();
  }

  /// Get the contents of the file as a \p MemoryBuffer.
  virtual llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>>
  getBuffer(const Twine &Name, int64_t FileSize = -1,
            bool RequiresNullTerminator = true, bool IsVolatile = false) = 0;

  /// Closes the file.
  virtual std::error_code close() = 0;
};

/// A member of a directory, yielded by a directory_iterator.
/// Only information available on most platforms is included.
class directory_entry {
  std::string Path;
  llvm::sys::fs::file_type Type;

public:
  directory_entry() = default;
  directory_entry(std::string Path, llvm::sys::fs::file_type Type)
      : Path(std::move(Path)), Type(Type) {}

  llvm::StringRef path() const { return Path; }
  llvm::sys::fs::file_type type() const { return Type; }
};

namespace detail {

/// An interface for virtual file systems to provide an iterator over the
/// (non-recursive) contents of a directory.
struct DirIterImpl {
  virtual ~DirIterImpl();

  /// Sets \c CurrentEntry to the next entry in the directory on success,
  /// to directory_entry() at end,  or returns a system-defined \c error_code.
  virtual std::error_code increment() = 0;

  directory_entry CurrentEntry;
};

} // namespace detail

/// An input iterator over the entries in a virtual path, similar to
/// llvm::sys::fs::directory_iterator.
class directory_iterator {
  std::shared_ptr<detail::DirIterImpl> Impl; // Input iterator semantics on copy

public:
  directory_iterator(std::shared_ptr<detail::DirIterImpl> I)
      : Impl(std::move(I)) {
    assert(Impl.get() != nullptr && "requires non-null implementation");
    if (Impl->CurrentEntry.path().empty())
      Impl.reset(); // Normalize the end iterator to Impl == nullptr.
  }

  /// Construct an 'end' iterator.
  directory_iterator() = default;

  /// Equivalent to operator++, with an error code.
  directory_iterator &increment(std::error_code &EC) {
    assert(Impl && "attempting to increment past end");
    EC = Impl->increment();
    if (Impl->CurrentEntry.path().empty())
      Impl.reset(); // Normalize the end iterator to Impl == nullptr.
    return *this;
  }

  const directory_entry &operator*() const { return Impl->CurrentEntry; }
  const directory_entry *operator->() const { return &Impl->CurrentEntry; }

  bool operator==(const directory_iterator &RHS) const {
    if (Impl && RHS.Impl)
      return Impl->CurrentEntry.path() == RHS.Impl->CurrentEntry.path();
    return !Impl && !RHS.Impl;
  }
  bool operator!=(const directory_iterator &RHS) const {
    return !(*this == RHS);
  }
};

class FileSystem;

namespace detail {

/// Keeps state for the recursive_directory_iterator.
struct RecDirIterState {
  std::stack<directory_iterator, std::vector<directory_iterator>> Stack;
  bool HasNoPushRequest = false;
};

} // end namespace detail

/// An input iterator over the recursive contents of a virtual path,
/// similar to llvm::sys::fs::recursive_directory_iterator.
class recursive_directory_iterator {
  FileSystem *FS;
  std::shared_ptr<detail::RecDirIterState>
      State; // Input iterator semantics on copy.

public:
  recursive_directory_iterator(FileSystem &FS, const Twine &Path,
                               std::error_code &EC);

  /// Construct an 'end' iterator.
  recursive_directory_iterator() = default;

  /// Equivalent to operator++, with an error code.
  recursive_directory_iterator &increment(std::error_code &EC);

  const directory_entry &operator*() const { return *State->Stack.top(); }
  const directory_entry *operator->() const { return &*State->Stack.top(); }

  bool operator==(const recursive_directory_iterator &Other) const {
    return State == Other.State; // identity
  }
  bool operator!=(const recursive_directory_iterator &RHS) const {
    return !(*this == RHS);
  }

  /// Gets the current level. Starting path is at level 0.
  int level() const {
    assert(!State->Stack.empty() &&
           "Cannot get level without any iteration state");
    return State->Stack.size() - 1;
  }

  void no_push() { State->HasNoPushRequest = true; }
};

/// The virtual file system interface.
class FileSystem : public llvm::ThreadSafeRefCountedBase<FileSystem> {
public:
  virtual ~FileSystem();

  /// Get the status of the entry at \p Path, if one exists.
  virtual llvm::ErrorOr<Status> status(const Twine &Path) = 0;

  /// Get a \p File object for the file at \p Path, if one exists.
  virtual llvm::ErrorOr<std::unique_ptr<File>>
  openFileForRead(const Twine &Path) = 0;

  /// This is a convenience method that opens a file, gets its content and then
  /// closes the file.
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>>
  getBufferForFile(const Twine &Name, int64_t FileSize = -1,
                   bool RequiresNullTerminator = true, bool IsVolatile = false);

  /// Get a directory_iterator for \p Dir.
  /// \note The 'end' iterator is directory_iterator().
  virtual directory_iterator dir_begin(const Twine &Dir,
                                       std::error_code &EC) = 0;

  /// Set the working directory. This will affect all following operations on
  /// this file system and may propagate down for nested file systems.
  virtual std::error_code setCurrentWorkingDirectory(const Twine &Path) = 0;

  /// Get the working directory of this file system.
  virtual llvm::ErrorOr<std::string> getCurrentWorkingDirectory() const = 0;

  /// Gets real path of \p Path e.g. collapse all . and .. patterns, resolve
  /// symlinks. For real file system, this uses `llvm::sys::fs::real_path`.
  /// This returns errc::operation_not_permitted if not implemented by subclass.
  virtual std::error_code getRealPath(const Twine &Path,
                                      SmallVectorImpl<char> &Output) const;

  /// Check whether a file exists. Provided for convenience.
  bool exists(const Twine &Path);

  /// Is the file mounted on a local filesystem?
  virtual std::error_code isLocal(const Twine &Path, bool &Result);

  /// Make \a Path an absolute path.
  ///
  /// Makes \a Path absolute using the current directory if it is not already.
  /// An empty \a Path will result in the current directory.
  ///
  /// /absolute/path   => /absolute/path
  /// relative/../path => <current-directory>/relative/../path
  ///
  /// \param Path A path that is modified to be an absolute path.
  /// \returns success if \a path has been made absolute, otherwise a
  ///          platform-specific error_code.
  std::error_code makeAbsolute(SmallVectorImpl<char> &Path) const;
};

/// Gets an \p vfs::FileSystem for the 'real' file system, as seen by
/// the operating system.
IntrusiveRefCntPtr<FileSystem> getRealFileSystem();

/// A file system that allows overlaying one \p AbstractFileSystem on top
/// of another.
///
/// Consists of a stack of >=1 \p FileSystem objects, which are treated as being
/// one merged file system. When there is a directory that exists in more than
/// one file system, the \p OverlayFileSystem contains a directory containing
/// the union of their contents.  The attributes (permissions, etc.) of the
/// top-most (most recently added) directory are used.  When there is a file
/// that exists in more than one file system, the file in the top-most file
/// system overrides the other(s).
class OverlayFileSystem : public FileSystem {
  using FileSystemList = SmallVector<IntrusiveRefCntPtr<FileSystem>, 1>;

  /// The stack of file systems, implemented as a list in order of
  /// their addition.
  FileSystemList FSList;

public:
  OverlayFileSystem(IntrusiveRefCntPtr<FileSystem> Base);

  /// Pushes a file system on top of the stack.
  void pushOverlay(IntrusiveRefCntPtr<FileSystem> FS);

  llvm::ErrorOr<Status> status(const Twine &Path) override;
  llvm::ErrorOr<std::unique_ptr<File>>
  openFileForRead(const Twine &Path) override;
  directory_iterator dir_begin(const Twine &Dir, std::error_code &EC) override;
  llvm::ErrorOr<std::string> getCurrentWorkingDirectory() const override;
  std::error_code setCurrentWorkingDirectory(const Twine &Path) override;
  std::error_code isLocal(const Twine &Path, bool &Result) override;
  std::error_code getRealPath(const Twine &Path,
                              SmallVectorImpl<char> &Output) const override;

  using iterator = FileSystemList::reverse_iterator;
  using const_iterator = FileSystemList::const_reverse_iterator;

  /// Get an iterator pointing to the most recently added file system.
  iterator overlays_begin() { return FSList.rbegin(); }
  const_iterator overlays_begin() const { return FSList.rbegin(); }

  /// Get an iterator pointing one-past the least recently added file
  /// system.
  iterator overlays_end() { return FSList.rend(); }
  const_iterator overlays_end() const { return FSList.rend(); }
};

/// By default, this delegates all calls to the underlying file system. This
/// is useful when derived file systems want to override some calls and still
/// proxy other calls.
class ProxyFileSystem : public FileSystem {
public:
  explicit ProxyFileSystem(IntrusiveRefCntPtr<FileSystem> FS)
      : FS(std::move(FS)) {}

  llvm::ErrorOr<Status> status(const Twine &Path) override {
    return FS->status(Path);
  }
  llvm::ErrorOr<std::unique_ptr<File>>
  openFileForRead(const Twine &Path) override {
    return FS->openFileForRead(Path);
  }
  directory_iterator dir_begin(const Twine &Dir, std::error_code &EC) override {
    return FS->dir_begin(Dir, EC);
  }
  llvm::ErrorOr<std::string> getCurrentWorkingDirectory() const override {
    return FS->getCurrentWorkingDirectory();
  }
  std::error_code setCurrentWorkingDirectory(const Twine &Path) override {
    return FS->setCurrentWorkingDirectory(Path);
  }
  std::error_code getRealPath(const Twine &Path,
                              SmallVectorImpl<char> &Output) const override {
    return FS->getRealPath(Path, Output);
  }
  std::error_code isLocal(const Twine &Path, bool &Result) override {
    return FS->isLocal(Path, Result);
  }

protected:
  FileSystem &getUnderlyingFS() { return *FS; }

private:
  IntrusiveRefCntPtr<FileSystem> FS;

  virtual void anchor();
};

namespace detail {

class InMemoryDirectory;
class InMemoryFile;

} // namespace detail

/// An in-memory file system.
class InMemoryFileSystem : public FileSystem {
  std::unique_ptr<detail::InMemoryDirectory> Root;
  std::string WorkingDirectory;
  bool UseNormalizedPaths = true;

  /// If HardLinkTarget is non-null, a hardlink is created to the To path which
  /// must be a file. If it is null then it adds the file as the public addFile.
  bool addFile(const Twine &Path, time_t ModificationTime,
               std::unique_ptr<llvm::MemoryBuffer> Buffer,
               Optional<uint32_t> User, Optional<uint32_t> Group,
               Optional<llvm::sys::fs::file_type> Type,
               Optional<llvm::sys::fs::perms> Perms,
               const detail::InMemoryFile *HardLinkTarget);

public:
  explicit InMemoryFileSystem(bool UseNormalizedPaths = true);
  ~InMemoryFileSystem() override;

  /// Add a file containing a buffer or a directory to the VFS with a
  /// path. The VFS owns the buffer.  If present, User, Group, Type
  /// and Perms apply to the newly-created file or directory.
  /// \return true if the file or directory was successfully added,
  /// false if the file or directory already exists in the file system with
  /// different contents.
  bool addFile(const Twine &Path, time_t ModificationTime,
               std::unique_ptr<llvm::MemoryBuffer> Buffer,
               Optional<uint32_t> User = None, Optional<uint32_t> Group = None,
               Optional<llvm::sys::fs::file_type> Type = None,
               Optional<llvm::sys::fs::perms> Perms = None);

  /// Add a hard link to a file.
  /// Here hard links are not intended to be fully equivalent to the classical
  /// filesystem. Both the hard link and the file share the same buffer and
  /// status (and thus have the same UniqueID). Because of this there is no way
  /// to distinguish between the link and the file after the link has been
  /// added.
  ///
  /// The To path must be an existing file or a hardlink. The From file must not
  /// have been added before. The To Path must not be a directory. The From Node
  /// is added as a hard link which points to the resolved file of To Node.
  /// \return true if the above condition is satisfied and hardlink was
  /// successfully created, false otherwise.
  bool addHardLink(const Twine &From, const Twine &To);

  /// Add a buffer to the VFS with a path. The VFS does not own the buffer.
  /// If present, User, Group, Type and Perms apply to the newly-created file
  /// or directory.
  /// \return true if the file or directory was successfully added,
  /// false if the file or directory already exists in the file system with
  /// different contents.
  bool addFileNoOwn(const Twine &Path, time_t ModificationTime,
                    llvm::MemoryBuffer *Buffer, Optional<uint32_t> User = None,
                    Optional<uint32_t> Group = None,
                    Optional<llvm::sys::fs::file_type> Type = None,
                    Optional<llvm::sys::fs::perms> Perms = None);

  std::string toString() const;

  /// Return true if this file system normalizes . and .. in paths.
  bool useNormalizedPaths() const { return UseNormalizedPaths; }

  llvm::ErrorOr<Status> status(const Twine &Path) override;
  llvm::ErrorOr<std::unique_ptr<File>>
  openFileForRead(const Twine &Path) override;
  directory_iterator dir_begin(const Twine &Dir, std::error_code &EC) override;

  llvm::ErrorOr<std::string> getCurrentWorkingDirectory() const override {
    return WorkingDirectory;
  }
  /// Canonicalizes \p Path by combining with the current working
  /// directory and normalizing the path (e.g. remove dots). If the current
  /// working directory is not set, this returns errc::operation_not_permitted.
  ///
  /// This doesn't resolve symlinks as they are not supported in in-memory file
  /// system.
  std::error_code getRealPath(const Twine &Path,
                              SmallVectorImpl<char> &Output) const override;
  std::error_code isLocal(const Twine &Path, bool &Result) override;
  std::error_code setCurrentWorkingDirectory(const Twine &Path) override;
};

/// Get a globally unique ID for a virtual file or directory.
llvm::sys::fs::UniqueID getNextVirtualUniqueID();

/// Gets a \p FileSystem for a virtual file system described in YAML
/// format.
IntrusiveRefCntPtr<FileSystem>
getVFSFromYAML(std::unique_ptr<llvm::MemoryBuffer> Buffer,
               llvm::SourceMgr::DiagHandlerTy DiagHandler,
               StringRef YAMLFilePath, void *DiagContext = nullptr,
               IntrusiveRefCntPtr<FileSystem> ExternalFS = getRealFileSystem());

struct YAMLVFSEntry {
  template <typename T1, typename T2>
  YAMLVFSEntry(T1 &&VPath, T2 &&RPath)
      : VPath(std::forward<T1>(VPath)), RPath(std::forward<T2>(RPath)) {}
  std::string VPath;
  std::string RPath;
};

class VFSFromYamlDirIterImpl;
class RedirectingFileSystemParser;

/// A virtual file system parsed from a YAML file.
///
/// Currently, this class allows creating virtual directories and mapping
/// virtual file paths to existing external files, available in \c ExternalFS.
///
/// The basic structure of the parsed file is:
/// \verbatim
/// {
///   'version': <version number>,
///   <optional configuration>
///   'roots': [
///              <directory entries>
///            ]
/// }
/// \endverbatim
///
/// All configuration options are optional.
///   'case-sensitive': <boolean, default=true>
///   'use-external-names': <boolean, default=true>
///   'overlay-relative': <boolean, default=false>
///   'fallthrough': <boolean, default=true>
///
/// Virtual directories are represented as
/// \verbatim
/// {
///   'type': 'directory',
///   'name': <string>,
///   'contents': [ <file or directory entries> ]
/// }
/// \endverbatim
///
/// The default attributes for virtual directories are:
/// \verbatim
/// MTime = now() when created
/// Perms = 0777
/// User = Group = 0
/// Size = 0
/// UniqueID = unspecified unique value
/// \endverbatim
///
/// Re-mapped files are represented as
/// \verbatim
/// {
///   'type': 'file',
///   'name': <string>,
///   'use-external-name': <boolean> # Optional
///   'external-contents': <path to external file>
/// }
/// \endverbatim
///
/// and inherit their attributes from the external contents.
///
/// In both cases, the 'name' field may contain multiple path components (e.g.
/// /path/to/file). However, any directory that contains more than one child
/// must be uniquely represented by a directory entry.
class RedirectingFileSystem : public vfs::FileSystem {
public:
  enum EntryKind { EK_Directory, EK_File };

  /// A single file or directory in the VFS.
  class Entry {
    EntryKind Kind;
    std::string Name;

  public:
    Entry(EntryKind K, StringRef Name) : Kind(K), Name(Name) {}
    virtual ~Entry() = default;

    StringRef getName() const { return Name; }
    EntryKind getKind() const { return Kind; }
  };

  class RedirectingDirectoryEntry : public Entry {
    std::vector<std::unique_ptr<Entry>> Contents;
    Status S;

  public:
    RedirectingDirectoryEntry(StringRef Name,
                              std::vector<std::unique_ptr<Entry>> Contents,
                              Status S)
        : Entry(EK_Directory, Name), Contents(std::move(Contents)),
          S(std::move(S)) {}
    RedirectingDirectoryEntry(StringRef Name, Status S)
        : Entry(EK_Directory, Name), S(std::move(S)) {}

    Status getStatus() { return S; }

    void addContent(std::unique_ptr<Entry> Content) {
      Contents.push_back(std::move(Content));
    }

    Entry *getLastContent() const { return Contents.back().get(); }

    using iterator = decltype(Contents)::iterator;

    iterator contents_begin() { return Contents.begin(); }
    iterator contents_end() { return Contents.end(); }

    static bool classof(const Entry *E) { return E->getKind() == EK_Directory; }
  };

  class RedirectingFileEntry : public Entry {
  public:
    enum NameKind { NK_NotSet, NK_External, NK_Virtual };

  private:
    std::string ExternalContentsPath;
    NameKind UseName;

  public:
    RedirectingFileEntry(StringRef Name, StringRef ExternalContentsPath,
                         NameKind UseName)
        : Entry(EK_File, Name), ExternalContentsPath(ExternalContentsPath),
          UseName(UseName) {}

    StringRef getExternalContentsPath() const { return ExternalContentsPath; }

    /// whether to use the external path as the name for this file.
    bool useExternalName(bool GlobalUseExternalName) const {
      return UseName == NK_NotSet ? GlobalUseExternalName
                                  : (UseName == NK_External);
    }

    NameKind getUseName() const { return UseName; }

    static bool classof(const Entry *E) { return E->getKind() == EK_File; }
  };

private:
  friend class VFSFromYamlDirIterImpl;
  friend class RedirectingFileSystemParser;

  /// The root(s) of the virtual file system.
  std::vector<std::unique_ptr<Entry>> Roots;

  /// The file system to use for external references.
  IntrusiveRefCntPtr<FileSystem> ExternalFS;

  /// If IsRelativeOverlay is set, this represents the directory
  /// path that should be prefixed to each 'external-contents' entry
  /// when reading from YAML files.
  std::string ExternalContentsPrefixDir;

  /// @name Configuration
  /// @{

  /// Whether to perform case-sensitive comparisons.
  ///
  /// Currently, case-insensitive matching only works correctly with ASCII.
  bool CaseSensitive = true;

  /// IsRelativeOverlay marks whether a ExternalContentsPrefixDir path must
  /// be prefixed in every 'external-contents' when reading from YAML files.
  bool IsRelativeOverlay = false;

  /// Whether to use to use the value of 'external-contents' for the
  /// names of files.  This global value is overridable on a per-file basis.
  bool UseExternalNames = true;

  /// Whether to attempt a file lookup in external file system after it wasn't
  /// found in VFS.
  bool IsFallthrough = true;
  /// @}

  /// Virtual file paths and external files could be canonicalized without "..",
  /// "." and "./" in their paths. FIXME: some unittests currently fail on
  /// win32 when using remove_dots and remove_leading_dotslash on paths.
  bool UseCanonicalizedPaths =
#ifdef _WIN32
      false;
#else
      true;
#endif

  RedirectingFileSystem(IntrusiveRefCntPtr<FileSystem> ExternalFS)
      : ExternalFS(std::move(ExternalFS)) {}

  /// Looks up the path <tt>[Start, End)</tt> in \p From, possibly
  /// recursing into the contents of \p From if it is a directory.
  ErrorOr<Entry *> lookupPath(llvm::sys::path::const_iterator Start,
                              llvm::sys::path::const_iterator End,
                              Entry *From) const;

  /// Get the status of a given an \c Entry.
  ErrorOr<Status> status(const Twine &Path, Entry *E);

public:
  /// Looks up \p Path in \c Roots.
  ErrorOr<Entry *> lookupPath(const Twine &Path) const;

  /// Parses \p Buffer, which is expected to be in YAML format and
  /// returns a virtual file system representing its contents.
  static RedirectingFileSystem *
  create(std::unique_ptr<MemoryBuffer> Buffer,
         SourceMgr::DiagHandlerTy DiagHandler, StringRef YAMLFilePath,
         void *DiagContext, IntrusiveRefCntPtr<FileSystem> ExternalFS);

  ErrorOr<Status> status(const Twine &Path) override;
  ErrorOr<std::unique_ptr<File>> openFileForRead(const Twine &Path) override;

  std::error_code getRealPath(const Twine &Path,
                              SmallVectorImpl<char> &Output) const override;

  llvm::ErrorOr<std::string> getCurrentWorkingDirectory() const override;

  std::error_code setCurrentWorkingDirectory(const Twine &Path) override;

  std::error_code isLocal(const Twine &Path, bool &Result) override;

  directory_iterator dir_begin(const Twine &Dir, std::error_code &EC) override;

  void setExternalContentsPrefixDir(StringRef PrefixDir);

  StringRef getExternalContentsPrefixDir() const;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  LLVM_DUMP_METHOD void dump() const;
  LLVM_DUMP_METHOD void dumpEntry(Entry *E, int NumSpaces = 0) const;
#endif
};

/// Collect all pairs of <virtual path, real path> entries from the
/// \p YAMLFilePath. This is used by the module dependency collector to forward
/// the entries into the reproducer output VFS YAML file.
void collectVFSFromYAML(
    std::unique_ptr<llvm::MemoryBuffer> Buffer,
    llvm::SourceMgr::DiagHandlerTy DiagHandler, StringRef YAMLFilePath,
    SmallVectorImpl<YAMLVFSEntry> &CollectedEntries,
    void *DiagContext = nullptr,
    IntrusiveRefCntPtr<FileSystem> ExternalFS = getRealFileSystem());

class YAMLVFSWriter {
  std::vector<YAMLVFSEntry> Mappings;
  Optional<bool> IsCaseSensitive;
  Optional<bool> IsOverlayRelative;
  Optional<bool> UseExternalNames;
  std::string OverlayDir;

public:
  YAMLVFSWriter() = default;

  void addFileMapping(StringRef VirtualPath, StringRef RealPath);

  void setCaseSensitivity(bool CaseSensitive) {
    IsCaseSensitive = CaseSensitive;
  }

  void setUseExternalNames(bool UseExtNames) { UseExternalNames = UseExtNames; }

  void setOverlayDir(StringRef OverlayDirectory) {
    IsOverlayRelative = true;
    OverlayDir.assign(OverlayDirectory.str());
  }

  const std::vector<YAMLVFSEntry> &getMappings() const { return Mappings; }

  void write(llvm::raw_ostream &OS);
};

} // namespace vfs
} // namespace llvm

#endif // LLVM_SUPPORT_VIRTUALFILESYSTEM_H
