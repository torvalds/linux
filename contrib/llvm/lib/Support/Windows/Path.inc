//===- llvm/Support/Windows/Path.inc - Windows Path Impl --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Windows specific implementation of the Path API.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//=== WARNING: Implementation here must contain only generic Windows code that
//===          is guaranteed to work on *all* Windows variants.
//===----------------------------------------------------------------------===//

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/WindowsError.h"
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <sys/types.h>

// These two headers must be included last, and make sure shlobj is required
// after Windows.h to make sure it picks up our definition of _WIN32_WINNT
#include "WindowsSupport.h"
#include <shellapi.h>
#include <shlobj.h>

#undef max

// MinGW doesn't define this.
#ifndef _ERRNO_T_DEFINED
#define _ERRNO_T_DEFINED
typedef int errno_t;
#endif

#ifdef _MSC_VER
# pragma comment(lib, "advapi32.lib")  // This provides CryptAcquireContextW.
# pragma comment(lib, "ole32.lib")     // This provides CoTaskMemFree
#endif

using namespace llvm;

using llvm::sys::windows::UTF8ToUTF16;
using llvm::sys::windows::CurCPToUTF16;
using llvm::sys::windows::UTF16ToUTF8;
using llvm::sys::path::widenPath;

static bool is_separator(const wchar_t value) {
  switch (value) {
  case L'\\':
  case L'/':
    return true;
  default:
    return false;
  }
}

namespace llvm {
namespace sys  {
namespace path {

// Convert a UTF-8 path to UTF-16.  Also, if the absolute equivalent of the
// path is longer than CreateDirectory can tolerate, make it absolute and
// prefixed by '\\?\'.
std::error_code widenPath(const Twine &Path8,
                          SmallVectorImpl<wchar_t> &Path16) {
  const size_t MaxDirLen = MAX_PATH - 12; // Must leave room for 8.3 filename.

  // Several operations would convert Path8 to SmallString; more efficient to
  // do it once up front.
  SmallString<128> Path8Str;
  Path8.toVector(Path8Str);

  // If we made this path absolute, how much longer would it get?
  size_t CurPathLen;
  if (llvm::sys::path::is_absolute(Twine(Path8Str)))
    CurPathLen = 0; // No contribution from current_path needed.
  else {
    CurPathLen = ::GetCurrentDirectoryW(0, NULL);
    if (CurPathLen == 0)
      return mapWindowsError(::GetLastError());
  }

  // Would the absolute path be longer than our limit?
  if ((Path8Str.size() + CurPathLen) >= MaxDirLen &&
      !Path8Str.startswith("\\\\?\\")) {
    SmallString<2*MAX_PATH> FullPath("\\\\?\\");
    if (CurPathLen) {
      SmallString<80> CurPath;
      if (std::error_code EC = llvm::sys::fs::current_path(CurPath))
        return EC;
      FullPath.append(CurPath);
    }
    // Traverse the requested path, canonicalizing . and .. (because the \\?\
    // prefix is documented to treat them as real components).  Ignore
    // separators, which can be returned from the iterator if the path has a
    // drive name.  We don't need to call native() on the result since append()
    // always attaches preferred_separator.
    for (llvm::sys::path::const_iterator I = llvm::sys::path::begin(Path8Str),
                                         E = llvm::sys::path::end(Path8Str);
                                         I != E; ++I) {
      if (I->size() == 1 && is_separator((*I)[0]))
        continue;
      if (I->size() == 1 && *I == ".")
        continue;
      if (I->size() == 2 && *I == "..")
        llvm::sys::path::remove_filename(FullPath);
      else
        llvm::sys::path::append(FullPath, *I);
    }
    return UTF8ToUTF16(FullPath, Path16);
  }

  // Just use the caller's original path.
  return UTF8ToUTF16(Path8Str, Path16);
}
} // end namespace path

namespace fs {

const file_t kInvalidFile = INVALID_HANDLE_VALUE;

std::string getMainExecutable(const char *argv0, void *MainExecAddr) {
  SmallVector<wchar_t, MAX_PATH> PathName;
  DWORD Size = ::GetModuleFileNameW(NULL, PathName.data(), PathName.capacity());

  // A zero return value indicates a failure other than insufficient space.
  if (Size == 0)
    return "";

  // Insufficient space is determined by a return value equal to the size of
  // the buffer passed in.
  if (Size == PathName.capacity())
    return "";

  // On success, GetModuleFileNameW returns the number of characters written to
  // the buffer not including the NULL terminator.
  PathName.set_size(Size);

  // Convert the result from UTF-16 to UTF-8.
  SmallVector<char, MAX_PATH> PathNameUTF8;
  if (UTF16ToUTF8(PathName.data(), PathName.size(), PathNameUTF8))
    return "";

  return std::string(PathNameUTF8.data());
}

UniqueID file_status::getUniqueID() const {
  // The file is uniquely identified by the volume serial number along
  // with the 64-bit file identifier.
  uint64_t FileID = (static_cast<uint64_t>(FileIndexHigh) << 32ULL) |
                    static_cast<uint64_t>(FileIndexLow);

  return UniqueID(VolumeSerialNumber, FileID);
}

ErrorOr<space_info> disk_space(const Twine &Path) {
  ULARGE_INTEGER Avail, Total, Free;
  if (!::GetDiskFreeSpaceExA(Path.str().c_str(), &Avail, &Total, &Free))
    return mapWindowsError(::GetLastError());
  space_info SpaceInfo;
  SpaceInfo.capacity =
      (static_cast<uint64_t>(Total.HighPart) << 32) + Total.LowPart;
  SpaceInfo.free = (static_cast<uint64_t>(Free.HighPart) << 32) + Free.LowPart;
  SpaceInfo.available =
      (static_cast<uint64_t>(Avail.HighPart) << 32) + Avail.LowPart;
  return SpaceInfo;
}

TimePoint<> basic_file_status::getLastAccessedTime() const {
  FILETIME Time;
  Time.dwLowDateTime = LastAccessedTimeLow;
  Time.dwHighDateTime = LastAccessedTimeHigh;
  return toTimePoint(Time);
}

TimePoint<> basic_file_status::getLastModificationTime() const {
  FILETIME Time;
  Time.dwLowDateTime = LastWriteTimeLow;
  Time.dwHighDateTime = LastWriteTimeHigh;
  return toTimePoint(Time);
}

uint32_t file_status::getLinkCount() const {
  return NumLinks;
}

std::error_code current_path(SmallVectorImpl<char> &result) {
  SmallVector<wchar_t, MAX_PATH> cur_path;
  DWORD len = MAX_PATH;

  do {
    cur_path.reserve(len);
    len = ::GetCurrentDirectoryW(cur_path.capacity(), cur_path.data());

    // A zero return value indicates a failure other than insufficient space.
    if (len == 0)
      return mapWindowsError(::GetLastError());

    // If there's insufficient space, the len returned is larger than the len
    // given.
  } while (len > cur_path.capacity());

  // On success, GetCurrentDirectoryW returns the number of characters not
  // including the null-terminator.
  cur_path.set_size(len);
  return UTF16ToUTF8(cur_path.begin(), cur_path.size(), result);
}

std::error_code set_current_path(const Twine &path) {
  // Convert to utf-16.
  SmallVector<wchar_t, 128> wide_path;
  if (std::error_code ec = widenPath(path, wide_path))
    return ec;

  if (!::SetCurrentDirectoryW(wide_path.begin()))
    return mapWindowsError(::GetLastError());

  return std::error_code();
}

std::error_code create_directory(const Twine &path, bool IgnoreExisting,
                                 perms Perms) {
  SmallVector<wchar_t, 128> path_utf16;

  if (std::error_code ec = widenPath(path, path_utf16))
    return ec;

  if (!::CreateDirectoryW(path_utf16.begin(), NULL)) {
    DWORD LastError = ::GetLastError();
    if (LastError != ERROR_ALREADY_EXISTS || !IgnoreExisting)
      return mapWindowsError(LastError);
  }

  return std::error_code();
}

// We can't use symbolic links for windows.
std::error_code create_link(const Twine &to, const Twine &from) {
  // Convert to utf-16.
  SmallVector<wchar_t, 128> wide_from;
  SmallVector<wchar_t, 128> wide_to;
  if (std::error_code ec = widenPath(from, wide_from))
    return ec;
  if (std::error_code ec = widenPath(to, wide_to))
    return ec;

  if (!::CreateHardLinkW(wide_from.begin(), wide_to.begin(), NULL))
    return mapWindowsError(::GetLastError());

  return std::error_code();
}

std::error_code create_hard_link(const Twine &to, const Twine &from) {
  return create_link(to, from);
}

std::error_code remove(const Twine &path, bool IgnoreNonExisting) {
  SmallVector<wchar_t, 128> path_utf16;

  if (std::error_code ec = widenPath(path, path_utf16))
    return ec;

  // We don't know whether this is a file or a directory, and remove() can
  // accept both. The usual way to delete a file or directory is to use one of
  // the DeleteFile or RemoveDirectory functions, but that requires you to know
  // which one it is. We could stat() the file to determine that, but that would
  // cost us additional system calls, which can be slow in a directory
  // containing a large number of files. So instead we call CreateFile directly.
  // The important part is the FILE_FLAG_DELETE_ON_CLOSE flag, which causes the
  // file to be deleted once it is closed. We also use the flags
  // FILE_FLAG_BACKUP_SEMANTICS (which allows us to open directories), and
  // FILE_FLAG_OPEN_REPARSE_POINT (don't follow symlinks).
  ScopedFileHandle h(::CreateFileW(
      c_str(path_utf16), DELETE,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
      OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS |
          FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_DELETE_ON_CLOSE,
      NULL));
  if (!h) {
    std::error_code EC = mapWindowsError(::GetLastError());
    if (EC != errc::no_such_file_or_directory || !IgnoreNonExisting)
      return EC;
  }

  return std::error_code();
}

static std::error_code is_local_internal(SmallVectorImpl<wchar_t> &Path,
                                         bool &Result) {
  SmallVector<wchar_t, 128> VolumePath;
  size_t Len = 128;
  while (true) {
    VolumePath.resize(Len);
    BOOL Success =
        ::GetVolumePathNameW(Path.data(), VolumePath.data(), VolumePath.size());

    if (Success)
      break;

    DWORD Err = ::GetLastError();
    if (Err != ERROR_INSUFFICIENT_BUFFER)
      return mapWindowsError(Err);

    Len *= 2;
  }
  // If the output buffer has exactly enough space for the path name, but not
  // the null terminator, it will leave the output unterminated.  Push a null
  // terminator onto the end to ensure that this never happens.
  VolumePath.push_back(L'\0');
  VolumePath.set_size(wcslen(VolumePath.data()));
  const wchar_t *P = VolumePath.data();

  UINT Type = ::GetDriveTypeW(P);
  switch (Type) {
  case DRIVE_FIXED:
    Result = true;
    return std::error_code();
  case DRIVE_REMOTE:
  case DRIVE_CDROM:
  case DRIVE_RAMDISK:
  case DRIVE_REMOVABLE:
    Result = false;
    return std::error_code();
  default:
    return make_error_code(errc::no_such_file_or_directory);
  }
  llvm_unreachable("Unreachable!");
}

std::error_code is_local(const Twine &path, bool &result) {
  if (!llvm::sys::fs::exists(path) || !llvm::sys::path::has_root_path(path))
    return make_error_code(errc::no_such_file_or_directory);

  SmallString<128> Storage;
  StringRef P = path.toStringRef(Storage);

  // Convert to utf-16.
  SmallVector<wchar_t, 128> WidePath;
  if (std::error_code ec = widenPath(P, WidePath))
    return ec;
  return is_local_internal(WidePath, result);
}

static std::error_code realPathFromHandle(HANDLE H,
                                          SmallVectorImpl<wchar_t> &Buffer) {
  DWORD CountChars = ::GetFinalPathNameByHandleW(
      H, Buffer.begin(), Buffer.capacity() - 1, FILE_NAME_NORMALIZED);
  if (CountChars > Buffer.capacity()) {
    // The buffer wasn't big enough, try again.  In this case the return value
    // *does* indicate the size of the null terminator.
    Buffer.reserve(CountChars);
    CountChars = ::GetFinalPathNameByHandleW(
        H, Buffer.data(), Buffer.capacity() - 1, FILE_NAME_NORMALIZED);
  }
  if (CountChars == 0)
    return mapWindowsError(GetLastError());
  Buffer.set_size(CountChars);
  return std::error_code();
}

static std::error_code realPathFromHandle(HANDLE H,
                                          SmallVectorImpl<char> &RealPath) {
  RealPath.clear();
  SmallVector<wchar_t, MAX_PATH> Buffer;
  if (std::error_code EC = realPathFromHandle(H, Buffer))
    return EC;

  const wchar_t *Data = Buffer.data();
  DWORD CountChars = Buffer.size();
  if (CountChars >= 4) {
    if (0 == ::memcmp(Data, L"\\\\?\\", 8)) {
      CountChars -= 4;
      Data += 4;
    }
  }

  // Convert the result from UTF-16 to UTF-8.
  return UTF16ToUTF8(Data, CountChars, RealPath);
}

std::error_code is_local(int FD, bool &Result) {
  SmallVector<wchar_t, 128> FinalPath;
  HANDLE Handle = reinterpret_cast<HANDLE>(_get_osfhandle(FD));

  if (std::error_code EC = realPathFromHandle(Handle, FinalPath))
    return EC;

  return is_local_internal(FinalPath, Result);
}

static std::error_code setDeleteDisposition(HANDLE Handle, bool Delete) {
  FILE_DISPOSITION_INFO Disposition;
  Disposition.DeleteFile = Delete;
  if (!SetFileInformationByHandle(Handle, FileDispositionInfo, &Disposition,
                                  sizeof(Disposition)))
    return mapWindowsError(::GetLastError());
  return std::error_code();
}

static std::error_code rename_internal(HANDLE FromHandle, const Twine &To,
                                       bool ReplaceIfExists) {
  SmallVector<wchar_t, 0> ToWide;
  if (auto EC = widenPath(To, ToWide))
    return EC;

  std::vector<char> RenameInfoBuf(sizeof(FILE_RENAME_INFO) - sizeof(wchar_t) +
                                  (ToWide.size() * sizeof(wchar_t)));
  FILE_RENAME_INFO &RenameInfo =
      *reinterpret_cast<FILE_RENAME_INFO *>(RenameInfoBuf.data());
  RenameInfo.ReplaceIfExists = ReplaceIfExists;
  RenameInfo.RootDirectory = 0;
  RenameInfo.FileNameLength = ToWide.size() * sizeof(wchar_t);
  std::copy(ToWide.begin(), ToWide.end(), &RenameInfo.FileName[0]);

  SetLastError(ERROR_SUCCESS);
  if (!SetFileInformationByHandle(FromHandle, FileRenameInfo, &RenameInfo,
                                  RenameInfoBuf.size())) {
    unsigned Error = GetLastError();
    if (Error == ERROR_SUCCESS)
      Error = ERROR_CALL_NOT_IMPLEMENTED; // Wine doesn't always set error code.
    return mapWindowsError(Error);
  }

  return std::error_code();
}

static std::error_code rename_handle(HANDLE FromHandle, const Twine &To) {
  SmallVector<wchar_t, 128> WideTo;
  if (std::error_code EC = widenPath(To, WideTo))
    return EC;

  // We normally expect this loop to succeed after a few iterations. If it
  // requires more than 200 tries, it's more likely that the failures are due to
  // a true error, so stop trying.
  for (unsigned Retry = 0; Retry != 200; ++Retry) {
    auto EC = rename_internal(FromHandle, To, true);

    if (EC ==
        std::error_code(ERROR_CALL_NOT_IMPLEMENTED, std::system_category())) {
      // Wine doesn't support SetFileInformationByHandle in rename_internal.
      // Fall back to MoveFileEx.
      SmallVector<wchar_t, MAX_PATH> WideFrom;
      if (std::error_code EC2 = realPathFromHandle(FromHandle, WideFrom))
        return EC2;
      if (::MoveFileExW(WideFrom.begin(), WideTo.begin(),
                        MOVEFILE_REPLACE_EXISTING))
        return std::error_code();
      return mapWindowsError(GetLastError());
    }

    if (!EC || EC != errc::permission_denied)
      return EC;

    // The destination file probably exists and is currently open in another
    // process, either because the file was opened without FILE_SHARE_DELETE or
    // it is mapped into memory (e.g. using MemoryBuffer). Rename it in order to
    // move it out of the way of the source file. Use FILE_FLAG_DELETE_ON_CLOSE
    // to arrange for the destination file to be deleted when the other process
    // closes it.
    ScopedFileHandle ToHandle(
        ::CreateFileW(WideTo.begin(), GENERIC_READ | DELETE,
                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                      NULL, OPEN_EXISTING,
                      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, NULL));
    if (!ToHandle) {
      auto EC = mapWindowsError(GetLastError());
      // Another process might have raced with us and moved the existing file
      // out of the way before we had a chance to open it. If that happens, try
      // to rename the source file again.
      if (EC == errc::no_such_file_or_directory)
        continue;
      return EC;
    }

    BY_HANDLE_FILE_INFORMATION FI;
    if (!GetFileInformationByHandle(ToHandle, &FI))
      return mapWindowsError(GetLastError());

    // Try to find a unique new name for the destination file.
    for (unsigned UniqueId = 0; UniqueId != 200; ++UniqueId) {
      std::string TmpFilename = (To + ".tmp" + utostr(UniqueId)).str();
      if (auto EC = rename_internal(ToHandle, TmpFilename, false)) {
        if (EC == errc::file_exists || EC == errc::permission_denied) {
          // Again, another process might have raced with us and moved the file
          // before we could move it. Check whether this is the case, as it
          // might have caused the permission denied error. If that was the
          // case, we don't need to move it ourselves.
          ScopedFileHandle ToHandle2(::CreateFileW(
              WideTo.begin(), 0,
              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL));
          if (!ToHandle2) {
            auto EC = mapWindowsError(GetLastError());
            if (EC == errc::no_such_file_or_directory)
              break;
            return EC;
          }
          BY_HANDLE_FILE_INFORMATION FI2;
          if (!GetFileInformationByHandle(ToHandle2, &FI2))
            return mapWindowsError(GetLastError());
          if (FI.nFileIndexHigh != FI2.nFileIndexHigh ||
              FI.nFileIndexLow != FI2.nFileIndexLow ||
              FI.dwVolumeSerialNumber != FI2.dwVolumeSerialNumber)
            break;
          continue;
        }
        return EC;
      }
      break;
    }

    // Okay, the old destination file has probably been moved out of the way at
    // this point, so try to rename the source file again. Still, another
    // process might have raced with us to create and open the destination
    // file, so we need to keep doing this until we succeed.
  }

  // The most likely root cause.
  return errc::permission_denied;
}

static std::error_code rename_fd(int FromFD, const Twine &To) {
  HANDLE FromHandle = reinterpret_cast<HANDLE>(_get_osfhandle(FromFD));
  return rename_handle(FromHandle, To);
}

std::error_code rename(const Twine &From, const Twine &To) {
  // Convert to utf-16.
  SmallVector<wchar_t, 128> WideFrom;
  if (std::error_code EC = widenPath(From, WideFrom))
    return EC;

  ScopedFileHandle FromHandle;
  // Retry this a few times to defeat badly behaved file system scanners.
  for (unsigned Retry = 0; Retry != 200; ++Retry) {
    if (Retry != 0)
      ::Sleep(10);
    FromHandle =
        ::CreateFileW(WideFrom.begin(), GENERIC_READ | DELETE,
                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                      NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (FromHandle)
      break;
  }
  if (!FromHandle)
    return mapWindowsError(GetLastError());

  return rename_handle(FromHandle, To);
}

std::error_code resize_file(int FD, uint64_t Size) {
#ifdef HAVE__CHSIZE_S
  errno_t error = ::_chsize_s(FD, Size);
#else
  errno_t error = ::_chsize(FD, Size);
#endif
  return std::error_code(error, std::generic_category());
}

std::error_code access(const Twine &Path, AccessMode Mode) {
  SmallVector<wchar_t, 128> PathUtf16;

  if (std::error_code EC = widenPath(Path, PathUtf16))
    return EC;

  DWORD Attributes = ::GetFileAttributesW(PathUtf16.begin());

  if (Attributes == INVALID_FILE_ATTRIBUTES) {
    // See if the file didn't actually exist.
    DWORD LastError = ::GetLastError();
    if (LastError != ERROR_FILE_NOT_FOUND &&
        LastError != ERROR_PATH_NOT_FOUND)
      return mapWindowsError(LastError);
    return errc::no_such_file_or_directory;
  }

  if (Mode == AccessMode::Write && (Attributes & FILE_ATTRIBUTE_READONLY))
    return errc::permission_denied;

  return std::error_code();
}

bool can_execute(const Twine &Path) {
  return !access(Path, AccessMode::Execute) ||
         !access(Path + ".exe", AccessMode::Execute);
}

bool equivalent(file_status A, file_status B) {
  assert(status_known(A) && status_known(B));
  return A.FileIndexHigh         == B.FileIndexHigh &&
         A.FileIndexLow          == B.FileIndexLow &&
         A.FileSizeHigh          == B.FileSizeHigh &&
         A.FileSizeLow           == B.FileSizeLow &&
         A.LastAccessedTimeHigh  == B.LastAccessedTimeHigh &&
         A.LastAccessedTimeLow   == B.LastAccessedTimeLow &&
         A.LastWriteTimeHigh     == B.LastWriteTimeHigh &&
         A.LastWriteTimeLow      == B.LastWriteTimeLow &&
         A.VolumeSerialNumber    == B.VolumeSerialNumber;
}

std::error_code equivalent(const Twine &A, const Twine &B, bool &result) {
  file_status fsA, fsB;
  if (std::error_code ec = status(A, fsA))
    return ec;
  if (std::error_code ec = status(B, fsB))
    return ec;
  result = equivalent(fsA, fsB);
  return std::error_code();
}

static bool isReservedName(StringRef path) {
  // This list of reserved names comes from MSDN, at:
  // http://msdn.microsoft.com/en-us/library/aa365247%28v=vs.85%29.aspx
  static const char *const sReservedNames[] = { "nul", "con", "prn", "aux",
                                                "com1", "com2", "com3", "com4",
                                                "com5", "com6", "com7", "com8",
                                                "com9", "lpt1", "lpt2", "lpt3",
                                                "lpt4", "lpt5", "lpt6", "lpt7",
                                                "lpt8", "lpt9" };

  // First, check to see if this is a device namespace, which always
  // starts with \\.\, since device namespaces are not legal file paths.
  if (path.startswith("\\\\.\\"))
    return true;

  // Then compare against the list of ancient reserved names.
  for (size_t i = 0; i < array_lengthof(sReservedNames); ++i) {
    if (path.equals_lower(sReservedNames[i]))
      return true;
  }

  // The path isn't what we consider reserved.
  return false;
}

static file_type file_type_from_attrs(DWORD Attrs) {
  return (Attrs & FILE_ATTRIBUTE_DIRECTORY) ? file_type::directory_file
                                            : file_type::regular_file;
}

static perms perms_from_attrs(DWORD Attrs) {
  return (Attrs & FILE_ATTRIBUTE_READONLY) ? (all_read | all_exe) : all_all;
}

static std::error_code getStatus(HANDLE FileHandle, file_status &Result) {
  if (FileHandle == INVALID_HANDLE_VALUE)
    goto handle_status_error;

  switch (::GetFileType(FileHandle)) {
  default:
    llvm_unreachable("Don't know anything about this file type");
  case FILE_TYPE_UNKNOWN: {
    DWORD Err = ::GetLastError();
    if (Err != NO_ERROR)
      return mapWindowsError(Err);
    Result = file_status(file_type::type_unknown);
    return std::error_code();
  }
  case FILE_TYPE_DISK:
    break;
  case FILE_TYPE_CHAR:
    Result = file_status(file_type::character_file);
    return std::error_code();
  case FILE_TYPE_PIPE:
    Result = file_status(file_type::fifo_file);
    return std::error_code();
  }

  BY_HANDLE_FILE_INFORMATION Info;
  if (!::GetFileInformationByHandle(FileHandle, &Info))
    goto handle_status_error;

  Result = file_status(
      file_type_from_attrs(Info.dwFileAttributes),
      perms_from_attrs(Info.dwFileAttributes), Info.nNumberOfLinks,
      Info.ftLastAccessTime.dwHighDateTime, Info.ftLastAccessTime.dwLowDateTime,
      Info.ftLastWriteTime.dwHighDateTime, Info.ftLastWriteTime.dwLowDateTime,
      Info.dwVolumeSerialNumber, Info.nFileSizeHigh, Info.nFileSizeLow,
      Info.nFileIndexHigh, Info.nFileIndexLow);
  return std::error_code();

handle_status_error:
  DWORD LastError = ::GetLastError();
  if (LastError == ERROR_FILE_NOT_FOUND ||
      LastError == ERROR_PATH_NOT_FOUND)
    Result = file_status(file_type::file_not_found);
  else if (LastError == ERROR_SHARING_VIOLATION)
    Result = file_status(file_type::type_unknown);
  else
    Result = file_status(file_type::status_error);
  return mapWindowsError(LastError);
}

std::error_code status(const Twine &path, file_status &result, bool Follow) {
  SmallString<128> path_storage;
  SmallVector<wchar_t, 128> path_utf16;

  StringRef path8 = path.toStringRef(path_storage);
  if (isReservedName(path8)) {
    result = file_status(file_type::character_file);
    return std::error_code();
  }

  if (std::error_code ec = widenPath(path8, path_utf16))
    return ec;

  DWORD attr = ::GetFileAttributesW(path_utf16.begin());
  if (attr == INVALID_FILE_ATTRIBUTES)
    return getStatus(INVALID_HANDLE_VALUE, result);

  DWORD Flags = FILE_FLAG_BACKUP_SEMANTICS;
  // Handle reparse points.
  if (!Follow && (attr & FILE_ATTRIBUTE_REPARSE_POINT))
    Flags |= FILE_FLAG_OPEN_REPARSE_POINT;

  ScopedFileHandle h(
      ::CreateFileW(path_utf16.begin(), 0, // Attributes only.
                    FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, Flags, 0));
  if (!h)
    return getStatus(INVALID_HANDLE_VALUE, result);

  return getStatus(h, result);
}

std::error_code status(int FD, file_status &Result) {
  HANDLE FileHandle = reinterpret_cast<HANDLE>(_get_osfhandle(FD));
  return getStatus(FileHandle, Result);
}

std::error_code setPermissions(const Twine &Path, perms Permissions) {
  SmallVector<wchar_t, 128> PathUTF16;
  if (std::error_code EC = widenPath(Path, PathUTF16))
    return EC;

  DWORD Attributes = ::GetFileAttributesW(PathUTF16.begin());
  if (Attributes == INVALID_FILE_ATTRIBUTES)
    return mapWindowsError(GetLastError());

  // There are many Windows file attributes that are not to do with the file
  // permissions (e.g. FILE_ATTRIBUTE_HIDDEN). We need to be careful to preserve
  // them.
  if (Permissions & all_write) {
    Attributes &= ~FILE_ATTRIBUTE_READONLY;
    if (Attributes == 0)
      // FILE_ATTRIBUTE_NORMAL indicates no other attributes are set.
      Attributes |= FILE_ATTRIBUTE_NORMAL;
  }
  else {
    Attributes |= FILE_ATTRIBUTE_READONLY;
    // FILE_ATTRIBUTE_NORMAL is not compatible with any other attributes, so
    // remove it, if it is present.
    Attributes &= ~FILE_ATTRIBUTE_NORMAL;
  }

  if (!::SetFileAttributesW(PathUTF16.begin(), Attributes))
    return mapWindowsError(GetLastError());

  return std::error_code();
}

std::error_code setLastAccessAndModificationTime(int FD, TimePoint<> AccessTime,
                                                 TimePoint<> ModificationTime) {
  FILETIME AccessFT = toFILETIME(AccessTime);
  FILETIME ModifyFT = toFILETIME(ModificationTime);
  HANDLE FileHandle = reinterpret_cast<HANDLE>(_get_osfhandle(FD));
  if (!SetFileTime(FileHandle, NULL, &AccessFT, &ModifyFT))
    return mapWindowsError(::GetLastError());
  return std::error_code();
}

std::error_code mapped_file_region::init(int FD, uint64_t Offset,
                                         mapmode Mode) {
  this->Mode = Mode;
  HANDLE OrigFileHandle = reinterpret_cast<HANDLE>(_get_osfhandle(FD));
  if (OrigFileHandle == INVALID_HANDLE_VALUE)
    return make_error_code(errc::bad_file_descriptor);

  DWORD flprotect;
  switch (Mode) {
  case readonly:  flprotect = PAGE_READONLY; break;
  case readwrite: flprotect = PAGE_READWRITE; break;
  case priv:      flprotect = PAGE_WRITECOPY; break;
  }

  HANDLE FileMappingHandle =
      ::CreateFileMappingW(OrigFileHandle, 0, flprotect,
                           Hi_32(Size),
                           Lo_32(Size),
                           0);
  if (FileMappingHandle == NULL) {
    std::error_code ec = mapWindowsError(GetLastError());
    return ec;
  }

  DWORD dwDesiredAccess;
  switch (Mode) {
  case readonly:  dwDesiredAccess = FILE_MAP_READ; break;
  case readwrite: dwDesiredAccess = FILE_MAP_WRITE; break;
  case priv:      dwDesiredAccess = FILE_MAP_COPY; break;
  }
  Mapping = ::MapViewOfFile(FileMappingHandle,
                            dwDesiredAccess,
                            Offset >> 32,
                            Offset & 0xffffffff,
                            Size);
  if (Mapping == NULL) {
    std::error_code ec = mapWindowsError(GetLastError());
    ::CloseHandle(FileMappingHandle);
    return ec;
  }

  if (Size == 0) {
    MEMORY_BASIC_INFORMATION mbi;
    SIZE_T Result = VirtualQuery(Mapping, &mbi, sizeof(mbi));
    if (Result == 0) {
      std::error_code ec = mapWindowsError(GetLastError());
      ::UnmapViewOfFile(Mapping);
      ::CloseHandle(FileMappingHandle);
      return ec;
    }
    Size = mbi.RegionSize;
  }

  // Close the file mapping handle, as it's kept alive by the file mapping. But
  // neither the file mapping nor the file mapping handle keep the file handle
  // alive, so we need to keep a reference to the file in case all other handles
  // are closed and the file is deleted, which may cause invalid data to be read
  // from the file.
  ::CloseHandle(FileMappingHandle);
  if (!::DuplicateHandle(::GetCurrentProcess(), OrigFileHandle,
                         ::GetCurrentProcess(), &FileHandle, 0, 0,
                         DUPLICATE_SAME_ACCESS)) {
    std::error_code ec = mapWindowsError(GetLastError());
    ::UnmapViewOfFile(Mapping);
    return ec;
  }

  return std::error_code();
}

mapped_file_region::mapped_file_region(int fd, mapmode mode, size_t length,
                                       uint64_t offset, std::error_code &ec)
    : Size(length), Mapping() {
  ec = init(fd, offset, mode);
  if (ec)
    Mapping = 0;
}

static bool hasFlushBufferKernelBug() {
  static bool Ret{GetWindowsOSVersion() < llvm::VersionTuple(10, 0, 0, 17763)};
  return Ret;
}

static bool isEXE(StringRef Magic) {
  static const char PEMagic[] = {'P', 'E', '\0', '\0'};
  if (Magic.startswith(StringRef("MZ")) && Magic.size() >= 0x3c + 4) {
    uint32_t off = read32le(Magic.data() + 0x3c);
    // PE/COFF file, either EXE or DLL.
    if (Magic.substr(off).startswith(StringRef(PEMagic, sizeof(PEMagic))))
      return true;
  }
  return false;
}

mapped_file_region::~mapped_file_region() {
  if (Mapping) {

    bool Exe = isEXE(StringRef((char *)Mapping, Size));

    ::UnmapViewOfFile(Mapping);

    if (Mode == mapmode::readwrite && Exe && hasFlushBufferKernelBug()) {
      // There is a Windows kernel bug, the exact trigger conditions of which
      // are not well understood.  When triggered, dirty pages are not properly
      // flushed and subsequent process's attempts to read a file can return
      // invalid data.  Calling FlushFileBuffers on the write handle is
      // sufficient to ensure that this bug is not triggered.
      // The bug only occurs when writing an executable and executing it right
      // after, under high I/O pressure.
      ::FlushFileBuffers(FileHandle);
    }

    ::CloseHandle(FileHandle);
  }
}

size_t mapped_file_region::size() const {
  assert(Mapping && "Mapping failed but used anyway!");
  return Size;
}

char *mapped_file_region::data() const {
  assert(Mapping && "Mapping failed but used anyway!");
  return reinterpret_cast<char*>(Mapping);
}

const char *mapped_file_region::const_data() const {
  assert(Mapping && "Mapping failed but used anyway!");
  return reinterpret_cast<const char*>(Mapping);
}

int mapped_file_region::alignment() {
  SYSTEM_INFO SysInfo;
  ::GetSystemInfo(&SysInfo);
  return SysInfo.dwAllocationGranularity;
}

static basic_file_status status_from_find_data(WIN32_FIND_DATAW *FindData) {
  return basic_file_status(file_type_from_attrs(FindData->dwFileAttributes),
                           perms_from_attrs(FindData->dwFileAttributes),
                           FindData->ftLastAccessTime.dwHighDateTime,
                           FindData->ftLastAccessTime.dwLowDateTime,
                           FindData->ftLastWriteTime.dwHighDateTime,
                           FindData->ftLastWriteTime.dwLowDateTime,
                           FindData->nFileSizeHigh, FindData->nFileSizeLow);
}

std::error_code detail::directory_iterator_construct(detail::DirIterState &IT,
                                                     StringRef Path,
                                                     bool FollowSymlinks) {
  SmallVector<wchar_t, 128> PathUTF16;

  if (std::error_code EC = widenPath(Path, PathUTF16))
    return EC;

  // Convert path to the format that Windows is happy with.
  if (PathUTF16.size() > 0 &&
      !is_separator(PathUTF16[Path.size() - 1]) &&
      PathUTF16[Path.size() - 1] != L':') {
    PathUTF16.push_back(L'\\');
    PathUTF16.push_back(L'*');
  } else {
    PathUTF16.push_back(L'*');
  }

  //  Get the first directory entry.
  WIN32_FIND_DATAW FirstFind;
  ScopedFindHandle FindHandle(::FindFirstFileExW(
      c_str(PathUTF16), FindExInfoBasic, &FirstFind, FindExSearchNameMatch,
      NULL, FIND_FIRST_EX_LARGE_FETCH));
  if (!FindHandle)
    return mapWindowsError(::GetLastError());

  size_t FilenameLen = ::wcslen(FirstFind.cFileName);
  while ((FilenameLen == 1 && FirstFind.cFileName[0] == L'.') ||
         (FilenameLen == 2 && FirstFind.cFileName[0] == L'.' &&
                              FirstFind.cFileName[1] == L'.'))
    if (!::FindNextFileW(FindHandle, &FirstFind)) {
      DWORD LastError = ::GetLastError();
      // Check for end.
      if (LastError == ERROR_NO_MORE_FILES)
        return detail::directory_iterator_destruct(IT);
      return mapWindowsError(LastError);
    } else
      FilenameLen = ::wcslen(FirstFind.cFileName);

  // Construct the current directory entry.
  SmallString<128> DirectoryEntryNameUTF8;
  if (std::error_code EC =
          UTF16ToUTF8(FirstFind.cFileName, ::wcslen(FirstFind.cFileName),
                      DirectoryEntryNameUTF8))
    return EC;

  IT.IterationHandle = intptr_t(FindHandle.take());
  SmallString<128> DirectoryEntryPath(Path);
  path::append(DirectoryEntryPath, DirectoryEntryNameUTF8);
  IT.CurrentEntry =
      directory_entry(DirectoryEntryPath, FollowSymlinks,
                      file_type_from_attrs(FirstFind.dwFileAttributes),
                      status_from_find_data(&FirstFind));

  return std::error_code();
}

std::error_code detail::directory_iterator_destruct(detail::DirIterState &IT) {
  if (IT.IterationHandle != 0)
    // Closes the handle if it's valid.
    ScopedFindHandle close(HANDLE(IT.IterationHandle));
  IT.IterationHandle = 0;
  IT.CurrentEntry = directory_entry();
  return std::error_code();
}

std::error_code detail::directory_iterator_increment(detail::DirIterState &IT) {
  WIN32_FIND_DATAW FindData;
  if (!::FindNextFileW(HANDLE(IT.IterationHandle), &FindData)) {
    DWORD LastError = ::GetLastError();
    // Check for end.
    if (LastError == ERROR_NO_MORE_FILES)
      return detail::directory_iterator_destruct(IT);
    return mapWindowsError(LastError);
  }

  size_t FilenameLen = ::wcslen(FindData.cFileName);
  if ((FilenameLen == 1 && FindData.cFileName[0] == L'.') ||
      (FilenameLen == 2 && FindData.cFileName[0] == L'.' &&
                           FindData.cFileName[1] == L'.'))
    return directory_iterator_increment(IT);

  SmallString<128> DirectoryEntryPathUTF8;
  if (std::error_code EC =
          UTF16ToUTF8(FindData.cFileName, ::wcslen(FindData.cFileName),
                      DirectoryEntryPathUTF8))
    return EC;

  IT.CurrentEntry.replace_filename(
      Twine(DirectoryEntryPathUTF8),
      file_type_from_attrs(FindData.dwFileAttributes),
      status_from_find_data(&FindData));
  return std::error_code();
}

ErrorOr<basic_file_status> directory_entry::status() const {
  return Status;
}

static std::error_code nativeFileToFd(Expected<HANDLE> H, int &ResultFD,
                                      OpenFlags Flags) {
  int CrtOpenFlags = 0;
  if (Flags & OF_Append)
    CrtOpenFlags |= _O_APPEND;

  if (Flags & OF_Text)
    CrtOpenFlags |= _O_TEXT;

  ResultFD = -1;
  if (!H)
    return errorToErrorCode(H.takeError());

  ResultFD = ::_open_osfhandle(intptr_t(*H), CrtOpenFlags);
  if (ResultFD == -1) {
    ::CloseHandle(*H);
    return mapWindowsError(ERROR_INVALID_HANDLE);
  }
  return std::error_code();
}

static DWORD nativeDisposition(CreationDisposition Disp, OpenFlags Flags) {
  // This is a compatibility hack.  Really we should respect the creation
  // disposition, but a lot of old code relied on the implicit assumption that
  // OF_Append implied it would open an existing file.  Since the disposition is
  // now explicit and defaults to CD_CreateAlways, this assumption would cause
  // any usage of OF_Append to append to a new file, even if the file already
  // existed.  A better solution might have two new creation dispositions:
  // CD_AppendAlways and CD_AppendNew.  This would also address the problem of
  // OF_Append being used on a read-only descriptor, which doesn't make sense.
  if (Flags & OF_Append)
    return OPEN_ALWAYS;

  switch (Disp) {
  case CD_CreateAlways:
    return CREATE_ALWAYS;
  case CD_CreateNew:
    return CREATE_NEW;
  case CD_OpenAlways:
    return OPEN_ALWAYS;
  case CD_OpenExisting:
    return OPEN_EXISTING;
  }
  llvm_unreachable("unreachable!");
}

static DWORD nativeAccess(FileAccess Access, OpenFlags Flags) {
  DWORD Result = 0;
  if (Access & FA_Read)
    Result |= GENERIC_READ;
  if (Access & FA_Write)
    Result |= GENERIC_WRITE;
  if (Flags & OF_Delete)
    Result |= DELETE;
  if (Flags & OF_UpdateAtime)
    Result |= FILE_WRITE_ATTRIBUTES;
  return Result;
}

static std::error_code openNativeFileInternal(const Twine &Name,
                                              file_t &ResultFile, DWORD Disp,
                                              DWORD Access, DWORD Flags,
                                              bool Inherit = false) {
  SmallVector<wchar_t, 128> PathUTF16;
  if (std::error_code EC = widenPath(Name, PathUTF16))
    return EC;

  SECURITY_ATTRIBUTES SA;
  SA.nLength = sizeof(SA);
  SA.lpSecurityDescriptor = nullptr;
  SA.bInheritHandle = Inherit;

  HANDLE H =
      ::CreateFileW(PathUTF16.begin(), Access,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &SA,
                    Disp, Flags, NULL);
  if (H == INVALID_HANDLE_VALUE) {
    DWORD LastError = ::GetLastError();
    std::error_code EC = mapWindowsError(LastError);
    // Provide a better error message when trying to open directories.
    // This only runs if we failed to open the file, so there is probably
    // no performances issues.
    if (LastError != ERROR_ACCESS_DENIED)
      return EC;
    if (is_directory(Name))
      return make_error_code(errc::is_a_directory);
    return EC;
  }
  ResultFile = H;
  return std::error_code();
}

Expected<file_t> openNativeFile(const Twine &Name, CreationDisposition Disp,
                                FileAccess Access, OpenFlags Flags,
                                unsigned Mode) {
  // Verify that we don't have both "append" and "excl".
  assert((!(Disp == CD_CreateNew) || !(Flags & OF_Append)) &&
         "Cannot specify both 'CreateNew' and 'Append' file creation flags!");

  DWORD NativeDisp = nativeDisposition(Disp, Flags);
  DWORD NativeAccess = nativeAccess(Access, Flags);

  bool Inherit = false;
  if (Flags & OF_ChildInherit)
    Inherit = true;

  file_t Result;
  std::error_code EC = openNativeFileInternal(
      Name, Result, NativeDisp, NativeAccess, FILE_ATTRIBUTE_NORMAL, Inherit);
  if (EC)
    return errorCodeToError(EC);

  if (Flags & OF_UpdateAtime) {
    FILETIME FileTime;
    SYSTEMTIME SystemTime;
    GetSystemTime(&SystemTime);
    if (SystemTimeToFileTime(&SystemTime, &FileTime) == 0 ||
        SetFileTime(Result, NULL, &FileTime, NULL) == 0) {
      DWORD LastError = ::GetLastError();
      ::CloseHandle(Result);
      return errorCodeToError(mapWindowsError(LastError));
    }
  }

  if (Flags & OF_Delete) {
    if ((EC = setDeleteDisposition(Result, true))) {
      ::CloseHandle(Result);
      return errorCodeToError(EC);
    }
  }
  return Result;
}

std::error_code openFile(const Twine &Name, int &ResultFD,
                         CreationDisposition Disp, FileAccess Access,
                         OpenFlags Flags, unsigned int Mode) {
  Expected<file_t> Result = openNativeFile(Name, Disp, Access, Flags);
  if (!Result)
    return errorToErrorCode(Result.takeError());

  return nativeFileToFd(*Result, ResultFD, Flags);
}

static std::error_code directoryRealPath(const Twine &Name,
                                         SmallVectorImpl<char> &RealPath) {
  file_t File;
  std::error_code EC = openNativeFileInternal(
      Name, File, OPEN_EXISTING, GENERIC_READ, FILE_FLAG_BACKUP_SEMANTICS);
  if (EC)
    return EC;

  EC = realPathFromHandle(File, RealPath);
  ::CloseHandle(File);
  return EC;
}

std::error_code openFileForRead(const Twine &Name, int &ResultFD,
                                OpenFlags Flags,
                                SmallVectorImpl<char> *RealPath) {
  Expected<HANDLE> NativeFile = openNativeFileForRead(Name, Flags, RealPath);
  return nativeFileToFd(std::move(NativeFile), ResultFD, OF_None);
}

Expected<file_t> openNativeFileForRead(const Twine &Name, OpenFlags Flags,
                                       SmallVectorImpl<char> *RealPath) {
  Expected<file_t> Result =
      openNativeFile(Name, CD_OpenExisting, FA_Read, Flags);

  // Fetch the real name of the file, if the user asked
  if (Result && RealPath)
    realPathFromHandle(*Result, *RealPath);

  return Result;
}

void closeFile(file_t &F) {
  ::CloseHandle(F);
  F = kInvalidFile;
}

std::error_code remove_directories(const Twine &path, bool IgnoreErrors) {
  // Convert to utf-16.
  SmallVector<wchar_t, 128> Path16;
  std::error_code EC = widenPath(path, Path16);
  if (EC && !IgnoreErrors)
    return EC;

  // SHFileOperation() accepts a list of paths, and so must be double null-
  // terminated to indicate the end of the list.  The buffer is already null
  // terminated, but since that null character is not considered part of the
  // vector's size, pushing another one will just consume that byte.  So we
  // need to push 2 null terminators.
  Path16.push_back(0);
  Path16.push_back(0);

  SHFILEOPSTRUCTW shfos = {};
  shfos.wFunc = FO_DELETE;
  shfos.pFrom = Path16.data();
  shfos.fFlags = FOF_NO_UI;

  int result = ::SHFileOperationW(&shfos);
  if (result != 0 && !IgnoreErrors)
    return mapWindowsError(result);
  return std::error_code();
}

static void expandTildeExpr(SmallVectorImpl<char> &Path) {
  // Path does not begin with a tilde expression.
  if (Path.empty() || Path[0] != '~')
    return;

  StringRef PathStr(Path.begin(), Path.size());
  PathStr = PathStr.drop_front();
  StringRef Expr = PathStr.take_until([](char c) { return path::is_separator(c); });

  if (!Expr.empty()) {
    // This is probably a ~username/ expression.  Don't support this on Windows.
    return;
  }

  SmallString<128> HomeDir;
  if (!path::home_directory(HomeDir)) {
    // For some reason we couldn't get the home directory.  Just exit.
    return;
  }

  // Overwrite the first character and insert the rest.
  Path[0] = HomeDir[0];
  Path.insert(Path.begin() + 1, HomeDir.begin() + 1, HomeDir.end());
}

void expand_tilde(const Twine &path, SmallVectorImpl<char> &dest) {
  dest.clear();
  if (path.isTriviallyEmpty())
    return;

  path.toVector(dest);
  expandTildeExpr(dest);

  return;
}

std::error_code real_path(const Twine &path, SmallVectorImpl<char> &dest,
                          bool expand_tilde) {
  dest.clear();
  if (path.isTriviallyEmpty())
    return std::error_code();

  if (expand_tilde) {
    SmallString<128> Storage;
    path.toVector(Storage);
    expandTildeExpr(Storage);
    return real_path(Storage, dest, false);
  }

  if (is_directory(path))
    return directoryRealPath(path, dest);

  int fd;
  if (std::error_code EC =
          llvm::sys::fs::openFileForRead(path, fd, OF_None, &dest))
    return EC;
  ::close(fd);
  return std::error_code();
}

} // end namespace fs

namespace path {
static bool getKnownFolderPath(KNOWNFOLDERID folderId,
                               SmallVectorImpl<char> &result) {
  wchar_t *path = nullptr;
  if (::SHGetKnownFolderPath(folderId, KF_FLAG_CREATE, nullptr, &path) != S_OK)
    return false;

  bool ok = !UTF16ToUTF8(path, ::wcslen(path), result);
  ::CoTaskMemFree(path);
  return ok;
}

bool home_directory(SmallVectorImpl<char> &result) {
  return getKnownFolderPath(FOLDERID_Profile, result);
}

static bool getTempDirEnvVar(const wchar_t *Var, SmallVectorImpl<char> &Res) {
  SmallVector<wchar_t, 1024> Buf;
  size_t Size = 1024;
  do {
    Buf.reserve(Size);
    Size = GetEnvironmentVariableW(Var, Buf.data(), Buf.capacity());
    if (Size == 0)
      return false;

    // Try again with larger buffer.
  } while (Size > Buf.capacity());
  Buf.set_size(Size);

  return !windows::UTF16ToUTF8(Buf.data(), Size, Res);
}

static bool getTempDirEnvVar(SmallVectorImpl<char> &Res) {
  const wchar_t *EnvironmentVariables[] = {L"TMP", L"TEMP", L"USERPROFILE"};
  for (auto *Env : EnvironmentVariables) {
    if (getTempDirEnvVar(Env, Res))
      return true;
  }
  return false;
}

void system_temp_directory(bool ErasedOnReboot, SmallVectorImpl<char> &Result) {
  (void)ErasedOnReboot;
  Result.clear();

  // Check whether the temporary directory is specified by an environment var.
  // This matches GetTempPath logic to some degree. GetTempPath is not used
  // directly as it cannot handle evn var longer than 130 chars on Windows 7
  // (fixed on Windows 8).
  if (getTempDirEnvVar(Result)) {
    assert(!Result.empty() && "Unexpected empty path");
    native(Result); // Some Unix-like shells use Unix path separator in $TMP.
    fs::make_absolute(Result); // Make it absolute if not already.
    return;
  }

  // Fall back to a system default.
  const char *DefaultResult = "C:\\Temp";
  Result.append(DefaultResult, DefaultResult + strlen(DefaultResult));
}
} // end namespace path

namespace windows {
std::error_code CodePageToUTF16(unsigned codepage,
                                llvm::StringRef original,
                                llvm::SmallVectorImpl<wchar_t> &utf16) {
  if (!original.empty()) {
    int len = ::MultiByteToWideChar(codepage, MB_ERR_INVALID_CHARS, original.begin(),
                                    original.size(), utf16.begin(), 0);

    if (len == 0) {
      return mapWindowsError(::GetLastError());
    }

    utf16.reserve(len + 1);
    utf16.set_size(len);

    len = ::MultiByteToWideChar(codepage, MB_ERR_INVALID_CHARS, original.begin(),
                                original.size(), utf16.begin(), utf16.size());

    if (len == 0) {
      return mapWindowsError(::GetLastError());
    }
  }

  // Make utf16 null terminated.
  utf16.push_back(0);
  utf16.pop_back();

  return std::error_code();
}

std::error_code UTF8ToUTF16(llvm::StringRef utf8,
                            llvm::SmallVectorImpl<wchar_t> &utf16) {
  return CodePageToUTF16(CP_UTF8, utf8, utf16);
}

std::error_code CurCPToUTF16(llvm::StringRef curcp,
                            llvm::SmallVectorImpl<wchar_t> &utf16) {
  return CodePageToUTF16(CP_ACP, curcp, utf16);
}

static
std::error_code UTF16ToCodePage(unsigned codepage, const wchar_t *utf16,
                                size_t utf16_len,
                                llvm::SmallVectorImpl<char> &converted) {
  if (utf16_len) {
    // Get length.
    int len = ::WideCharToMultiByte(codepage, 0, utf16, utf16_len, converted.begin(),
                                    0, NULL, NULL);

    if (len == 0) {
      return mapWindowsError(::GetLastError());
    }

    converted.reserve(len);
    converted.set_size(len);

    // Now do the actual conversion.
    len = ::WideCharToMultiByte(codepage, 0, utf16, utf16_len, converted.data(),
                                converted.size(), NULL, NULL);

    if (len == 0) {
      return mapWindowsError(::GetLastError());
    }
  }

  // Make the new string null terminated.
  converted.push_back(0);
  converted.pop_back();

  return std::error_code();
}

std::error_code UTF16ToUTF8(const wchar_t *utf16, size_t utf16_len,
                            llvm::SmallVectorImpl<char> &utf8) {
  return UTF16ToCodePage(CP_UTF8, utf16, utf16_len, utf8);
}

std::error_code UTF16ToCurCP(const wchar_t *utf16, size_t utf16_len,
                             llvm::SmallVectorImpl<char> &curcp) {
  return UTF16ToCodePage(CP_ACP, utf16, utf16_len, curcp);
}

} // end namespace windows
} // end namespace sys
} // end namespace llvm
