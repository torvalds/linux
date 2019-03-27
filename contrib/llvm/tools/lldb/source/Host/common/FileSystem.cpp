//===-- FileSystem.cpp ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/FileSystem.h"

#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/TildeExpressionResolver.h"

#include "llvm/Support/Errno.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/Threading.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>

#ifdef _WIN32
#include "lldb/Host/windows/windows.h"
#else
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <fstream>
#include <vector>

using namespace lldb;
using namespace lldb_private;
using namespace llvm;

FileSystem &FileSystem::Instance() { return *InstanceImpl(); }

void FileSystem::Initialize() {
  lldbassert(!InstanceImpl() && "Already initialized.");
  InstanceImpl().emplace();
}

void FileSystem::Initialize(IntrusiveRefCntPtr<vfs::FileSystem> fs) {
  lldbassert(!InstanceImpl() && "Already initialized.");
  InstanceImpl().emplace(fs);
}

void FileSystem::Terminate() {
  lldbassert(InstanceImpl() && "Already terminated.");
  InstanceImpl().reset();
}

Optional<FileSystem> &FileSystem::InstanceImpl() {
  static Optional<FileSystem> g_fs;
  return g_fs;
}

vfs::directory_iterator FileSystem::DirBegin(const FileSpec &file_spec,
                                             std::error_code &ec) {
  return DirBegin(file_spec.GetPath(), ec);
}

vfs::directory_iterator FileSystem::DirBegin(const Twine &dir,
                                             std::error_code &ec) {
  return m_fs->dir_begin(dir, ec);
}

llvm::ErrorOr<vfs::Status>
FileSystem::GetStatus(const FileSpec &file_spec) const {
  return GetStatus(file_spec.GetPath());
}

llvm::ErrorOr<vfs::Status> FileSystem::GetStatus(const Twine &path) const {
  return m_fs->status(path);
}

sys::TimePoint<>
FileSystem::GetModificationTime(const FileSpec &file_spec) const {
  return GetModificationTime(file_spec.GetPath());
}

sys::TimePoint<> FileSystem::GetModificationTime(const Twine &path) const {
  ErrorOr<vfs::Status> status = m_fs->status(path);
  if (!status)
    return sys::TimePoint<>();
  return status->getLastModificationTime();
}

uint64_t FileSystem::GetByteSize(const FileSpec &file_spec) const {
  return GetByteSize(file_spec.GetPath());
}

uint64_t FileSystem::GetByteSize(const Twine &path) const {
  ErrorOr<vfs::Status> status = m_fs->status(path);
  if (!status)
    return 0;
  return status->getSize();
}

uint32_t FileSystem::GetPermissions(const FileSpec &file_spec) const {
  return GetPermissions(file_spec.GetPath());
}

uint32_t FileSystem::GetPermissions(const FileSpec &file_spec,
                                    std::error_code &ec) const {
  return GetPermissions(file_spec.GetPath(), ec);
}

uint32_t FileSystem::GetPermissions(const Twine &path) const {
  std::error_code ec;
  return GetPermissions(path, ec);
}

uint32_t FileSystem::GetPermissions(const Twine &path,
                                    std::error_code &ec) const {
  ErrorOr<vfs::Status> status = m_fs->status(path);
  if (!status) {
    ec = status.getError();
    return sys::fs::perms::perms_not_known;
  }
  return status->getPermissions();
}

bool FileSystem::Exists(const Twine &path) const { return m_fs->exists(path); }

bool FileSystem::Exists(const FileSpec &file_spec) const {
  return Exists(file_spec.GetPath());
}

bool FileSystem::Readable(const Twine &path) const {
  return GetPermissions(path) & sys::fs::perms::all_read;
}

bool FileSystem::Readable(const FileSpec &file_spec) const {
  return Readable(file_spec.GetPath());
}

bool FileSystem::IsDirectory(const Twine &path) const {
  ErrorOr<vfs::Status> status = m_fs->status(path);
  if (!status)
    return false;
  return status->isDirectory();
}

bool FileSystem::IsDirectory(const FileSpec &file_spec) const {
  return IsDirectory(file_spec.GetPath());
}

bool FileSystem::IsLocal(const Twine &path) const {
  bool b = false;
  m_fs->isLocal(path, b);
  return b;
}

bool FileSystem::IsLocal(const FileSpec &file_spec) const {
  return IsLocal(file_spec.GetPath());
}

void FileSystem::EnumerateDirectory(Twine path, bool find_directories,
                                    bool find_files, bool find_other,
                                    EnumerateDirectoryCallbackType callback,
                                    void *callback_baton) {
  std::error_code EC;
  vfs::recursive_directory_iterator Iter(*m_fs, path, EC);
  vfs::recursive_directory_iterator End;
  for (; Iter != End && !EC; Iter.increment(EC)) {
    const auto &Item = *Iter;
    ErrorOr<vfs::Status> Status = m_fs->status(Item.path());
    if (!Status)
      break;
    if (!find_files && Status->isRegularFile())
      continue;
    if (!find_directories && Status->isDirectory())
      continue;
    if (!find_other && Status->isOther())
      continue;

    auto Result = callback(callback_baton, Status->getType(), Item.path());
    if (Result == eEnumerateDirectoryResultQuit)
      return;
    if (Result == eEnumerateDirectoryResultNext) {
      // Default behavior is to recurse. Opt out if the callback doesn't want
      // this behavior.
      Iter.no_push();
    }
  }
}

std::error_code FileSystem::MakeAbsolute(SmallVectorImpl<char> &path) const {
  return m_fs->makeAbsolute(path);
}

std::error_code FileSystem::MakeAbsolute(FileSpec &file_spec) const {
  SmallString<128> path;
  file_spec.GetPath(path, false);

  auto EC = MakeAbsolute(path);
  if (EC)
    return EC;

  FileSpec new_file_spec(path, file_spec.GetPathStyle());
  file_spec = new_file_spec;
  return {};
}

std::error_code FileSystem::GetRealPath(const Twine &path,
                                        SmallVectorImpl<char> &output) const {
  return m_fs->getRealPath(path, output);
}

void FileSystem::Resolve(SmallVectorImpl<char> &path) {
  if (path.empty())
    return;

  // Resolve tilde.
  SmallString<128> original_path(path.begin(), path.end());
  StandardTildeExpressionResolver Resolver;
  Resolver.ResolveFullPath(original_path, path);

  // Try making the path absolute if it exists.
  SmallString<128> absolute_path(path.begin(), path.end());
  MakeAbsolute(path);
  if (!Exists(path)) {
    path.clear();
    path.append(original_path.begin(), original_path.end());
  }
}

void FileSystem::Resolve(FileSpec &file_spec) {
  // Extract path from the FileSpec.
  SmallString<128> path;
  file_spec.GetPath(path);

  // Resolve the path.
  Resolve(path);

  // Update the FileSpec with the resolved path.
  file_spec.SetPath(path);
  file_spec.SetIsResolved(true);
}

std::shared_ptr<DataBufferLLVM>
FileSystem::CreateDataBuffer(const llvm::Twine &path, uint64_t size,
                             uint64_t offset) {
  const bool is_volatile = !IsLocal(path);

  std::unique_ptr<llvm::WritableMemoryBuffer> buffer;
  if (size == 0) {
    auto buffer_or_error =
        llvm::WritableMemoryBuffer::getFile(path, -1, is_volatile);
    if (!buffer_or_error)
      return nullptr;
    buffer = std::move(*buffer_or_error);
  } else {
    auto buffer_or_error = llvm::WritableMemoryBuffer::getFileSlice(
        path, size, offset, is_volatile);
    if (!buffer_or_error)
      return nullptr;
    buffer = std::move(*buffer_or_error);
  }
  return std::shared_ptr<DataBufferLLVM>(new DataBufferLLVM(std::move(buffer)));
}

std::shared_ptr<DataBufferLLVM>
FileSystem::CreateDataBuffer(const FileSpec &file_spec, uint64_t size,
                             uint64_t offset) {
  return CreateDataBuffer(file_spec.GetPath(), size, offset);
}

bool FileSystem::ResolveExecutableLocation(FileSpec &file_spec) {
  // If the directory is set there's nothing to do.
  const ConstString &directory = file_spec.GetDirectory();
  if (directory)
    return false;

  // We cannot look for a file if there's no file name.
  const ConstString &filename = file_spec.GetFilename();
  if (!filename)
    return false;

  // Search for the file on the host.
  const std::string filename_str(filename.GetCString());
  llvm::ErrorOr<std::string> error_or_path =
      llvm::sys::findProgramByName(filename_str);
  if (!error_or_path)
    return false;

  // findProgramByName returns "." if it can't find the file.
  llvm::StringRef path = *error_or_path;
  llvm::StringRef parent = llvm::sys::path::parent_path(path);
  if (parent.empty() || parent == ".")
    return false;

  // Make sure that the result exists.
  FileSpec result(*error_or_path);
  if (!Exists(result))
    return false;

  file_spec = result;
  return true;
}

static int OpenWithFS(const FileSystem &fs, const char *path, int flags,
                      int mode) {
  return const_cast<FileSystem &>(fs).Open(path, flags, mode);
}

static int GetOpenFlags(uint32_t options) {
  const bool read = options & File::eOpenOptionRead;
  const bool write = options & File::eOpenOptionWrite;

  int open_flags = 0;
  if (write) {
    if (read)
      open_flags |= O_RDWR;
    else
      open_flags |= O_WRONLY;

    if (options & File::eOpenOptionAppend)
      open_flags |= O_APPEND;

    if (options & File::eOpenOptionTruncate)
      open_flags |= O_TRUNC;

    if (options & File::eOpenOptionCanCreate)
      open_flags |= O_CREAT;

    if (options & File::eOpenOptionCanCreateNewOnly)
      open_flags |= O_CREAT | O_EXCL;
  } else if (read) {
    open_flags |= O_RDONLY;

#ifndef _WIN32
    if (options & File::eOpenOptionDontFollowSymlinks)
      open_flags |= O_NOFOLLOW;
#endif
  }

#ifndef _WIN32
  if (options & File::eOpenOptionNonBlocking)
    open_flags |= O_NONBLOCK;
  if (options & File::eOpenOptionCloseOnExec)
    open_flags |= O_CLOEXEC;
#else
  open_flags |= O_BINARY;
#endif

  return open_flags;
}

static mode_t GetOpenMode(uint32_t permissions) {
  mode_t mode = 0;
  if (permissions & lldb::eFilePermissionsUserRead)
    mode |= S_IRUSR;
  if (permissions & lldb::eFilePermissionsUserWrite)
    mode |= S_IWUSR;
  if (permissions & lldb::eFilePermissionsUserExecute)
    mode |= S_IXUSR;
  if (permissions & lldb::eFilePermissionsGroupRead)
    mode |= S_IRGRP;
  if (permissions & lldb::eFilePermissionsGroupWrite)
    mode |= S_IWGRP;
  if (permissions & lldb::eFilePermissionsGroupExecute)
    mode |= S_IXGRP;
  if (permissions & lldb::eFilePermissionsWorldRead)
    mode |= S_IROTH;
  if (permissions & lldb::eFilePermissionsWorldWrite)
    mode |= S_IWOTH;
  if (permissions & lldb::eFilePermissionsWorldExecute)
    mode |= S_IXOTH;
  return mode;
}

Status FileSystem::Open(File &File, const FileSpec &file_spec, uint32_t options,
                        uint32_t permissions) {
  if (File.IsValid())
    File.Close();

  const int open_flags = GetOpenFlags(options);
  const mode_t open_mode =
      (open_flags & O_CREAT) ? GetOpenMode(permissions) : 0;
  const std::string path = file_spec.GetPath();

  int descriptor = llvm::sys::RetryAfterSignal(
      -1, OpenWithFS, *this, path.c_str(), open_flags, open_mode);

  Status error;
  if (!File::DescriptorIsValid(descriptor)) {
    File.SetDescriptor(descriptor, false);
    error.SetErrorToErrno();
  } else {
    File.SetDescriptor(descriptor, true);
    File.SetOptions(options);
  }
  return error;
}
