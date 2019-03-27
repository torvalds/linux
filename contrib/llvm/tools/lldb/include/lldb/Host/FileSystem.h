//===-- FileSystem.h --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Host_FileSystem_h
#define liblldb_Host_FileSystem_h

#include "lldb/Host/File.h"
#include "lldb/Utility/DataBufferLLVM.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Status.h"

#include "llvm/ADT/Optional.h"
#include "llvm/Support/Chrono.h"
#include "llvm/Support/VirtualFileSystem.h"

#include "lldb/lldb-types.h"

#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>

namespace lldb_private {
class FileSystem {
public:
  static const char *DEV_NULL;
  static const char *PATH_CONVERSION_ERROR;

  FileSystem() : m_fs(llvm::vfs::getRealFileSystem()) {}
  FileSystem(llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> fs) : m_fs(fs) {}

  FileSystem(const FileSystem &fs) = delete;
  FileSystem &operator=(const FileSystem &fs) = delete;

  static FileSystem &Instance();

  static void Initialize();
  static void Initialize(llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> fs);
  static void Terminate();

  Status Symlink(const FileSpec &src, const FileSpec &dst);
  Status Readlink(const FileSpec &src, FileSpec &dst);

  Status ResolveSymbolicLink(const FileSpec &src, FileSpec &dst);

  /// Wraps ::fopen in a platform-independent way.
  FILE *Fopen(const char *path, const char *mode);

  /// Wraps ::open in a platform-independent way.
  int Open(const char *path, int flags, int mode);

  Status Open(File &File, const FileSpec &file_spec, uint32_t options,
              uint32_t permissions = lldb::eFilePermissionsFileDefault);

  /// Get a directory iterator.
  /// @{
  llvm::vfs::directory_iterator DirBegin(const FileSpec &file_spec,
                                         std::error_code &ec);
  llvm::vfs::directory_iterator DirBegin(const llvm::Twine &dir,
                                         std::error_code &ec);
  /// @}

  /// Returns the Status object for the given file.
  /// @{
  llvm::ErrorOr<llvm::vfs::Status> GetStatus(const FileSpec &file_spec) const;
  llvm::ErrorOr<llvm::vfs::Status> GetStatus(const llvm::Twine &path) const;
  /// @}

  /// Returns the modification time of the given file.
  /// @{
  llvm::sys::TimePoint<> GetModificationTime(const FileSpec &file_spec) const;
  llvm::sys::TimePoint<> GetModificationTime(const llvm::Twine &path) const;
  /// @}

  /// Returns the on-disk size of the given file in bytes.
  /// @{
  uint64_t GetByteSize(const FileSpec &file_spec) const;
  uint64_t GetByteSize(const llvm::Twine &path) const;
  /// @}

  /// Return the current permissions of the given file.
  ///
  /// Returns a bitmask for the current permissions of the file (zero or more
  /// of the permission bits defined in File::Permissions).
  /// @{
  uint32_t GetPermissions(const FileSpec &file_spec) const;
  uint32_t GetPermissions(const llvm::Twine &path) const;
  uint32_t GetPermissions(const FileSpec &file_spec, std::error_code &ec) const;
  uint32_t GetPermissions(const llvm::Twine &path, std::error_code &ec) const;
  /// @}

  /// Returns whether the given file exists.
  /// @{
  bool Exists(const FileSpec &file_spec) const;
  bool Exists(const llvm::Twine &path) const;
  /// @}

  /// Returns whether the given file is readable.
  /// @{
  bool Readable(const FileSpec &file_spec) const;
  bool Readable(const llvm::Twine &path) const;
  /// @}

  /// Returns whether the given path is a directory.
  /// @{
  bool IsDirectory(const FileSpec &file_spec) const;
  bool IsDirectory(const llvm::Twine &path) const;
  /// @}

  /// Returns whether the given path is local to the file system.
  /// @{
  bool IsLocal(const FileSpec &file_spec) const;
  bool IsLocal(const llvm::Twine &path) const;
  /// @}

  /// Make the given file path absolute.
  /// @{
  std::error_code MakeAbsolute(llvm::SmallVectorImpl<char> &path) const;
  std::error_code MakeAbsolute(FileSpec &file_spec) const;
  /// @}

  /// Resolve path to make it canonical.
  /// @{
  void Resolve(llvm::SmallVectorImpl<char> &path);
  void Resolve(FileSpec &file_spec);
  /// @}

  //// Create memory buffer from path.
  /// @{
  std::shared_ptr<DataBufferLLVM> CreateDataBuffer(const llvm::Twine &path,
                                                   uint64_t size = 0,
                                                   uint64_t offset = 0);
  std::shared_ptr<DataBufferLLVM> CreateDataBuffer(const FileSpec &file_spec,
                                                   uint64_t size = 0,
                                                   uint64_t offset = 0);
  /// @}

  /// Call into the Host to see if it can help find the file.
  bool ResolveExecutableLocation(FileSpec &file_spec);

  enum EnumerateDirectoryResult {
    /// Enumerate next entry in the current directory.
    eEnumerateDirectoryResultNext,
    /// Recurse into the current entry if it is a directory or symlink, or next
    /// if not.
    eEnumerateDirectoryResultEnter,
    /// Stop directory enumerations at any level.
    eEnumerateDirectoryResultQuit
  };

  typedef EnumerateDirectoryResult (*EnumerateDirectoryCallbackType)(
      void *baton, llvm::sys::fs::file_type file_type, llvm::StringRef);

  typedef std::function<EnumerateDirectoryResult(
      llvm::sys::fs::file_type file_type, llvm::StringRef)>
      DirectoryCallback;

  void EnumerateDirectory(llvm::Twine path, bool find_directories,
                          bool find_files, bool find_other,
                          EnumerateDirectoryCallbackType callback,
                          void *callback_baton);

  std::error_code GetRealPath(const llvm::Twine &path,
                              llvm::SmallVectorImpl<char> &output) const;

private:
  static llvm::Optional<FileSystem> &InstanceImpl();
  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> m_fs;
};
} // namespace lldb_private

#endif
