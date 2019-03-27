//===--- LockFileManager.h - File-level locking utility ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_SUPPORT_LOCKFILEMANAGER_H
#define LLVM_SUPPORT_LOCKFILEMANAGER_H

#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallString.h"
#include <system_error>
#include <utility> // for std::pair

namespace llvm {
class StringRef;

/// Class that manages the creation of a lock file to aid
/// implicit coordination between different processes.
///
/// The implicit coordination works by creating a ".lock" file alongside
/// the file that we're coordinating for, using the atomicity of the file
/// system to ensure that only a single process can create that ".lock" file.
/// When the lock file is removed, the owning process has finished the
/// operation.
class LockFileManager {
public:
  /// Describes the state of a lock file.
  enum LockFileState {
    /// The lock file has been created and is owned by this instance
    /// of the object.
    LFS_Owned,
    /// The lock file already exists and is owned by some other
    /// instance.
    LFS_Shared,
    /// An error occurred while trying to create or find the lock
    /// file.
    LFS_Error
  };

  /// Describes the result of waiting for the owner to release the lock.
  enum WaitForUnlockResult {
    /// The lock was released successfully.
    Res_Success,
    /// Owner died while holding the lock.
    Res_OwnerDied,
    /// Reached timeout while waiting for the owner to release the lock.
    Res_Timeout
  };

private:
  SmallString<128> FileName;
  SmallString<128> LockFileName;
  SmallString<128> UniqueLockFileName;

  Optional<std::pair<std::string, int> > Owner;
  std::error_code ErrorCode;
  std::string ErrorDiagMsg;

  LockFileManager(const LockFileManager &) = delete;
  LockFileManager &operator=(const LockFileManager &) = delete;

  static Optional<std::pair<std::string, int> >
  readLockFile(StringRef LockFileName);

  static bool processStillExecuting(StringRef Hostname, int PID);

public:

  LockFileManager(StringRef FileName);
  ~LockFileManager();

  /// Determine the state of the lock file.
  LockFileState getState() const;

  operator LockFileState() const { return getState(); }

  /// For a shared lock, wait until the owner releases the lock.
  WaitForUnlockResult waitForUnlock();

  /// Remove the lock file.  This may delete a different lock file than
  /// the one previously read if there is a race.
  std::error_code unsafeRemoveLockFile();

  /// Get error message, or "" if there is no error.
  std::string getErrorMessage() const;

  /// Set error and error message
  void setError(const std::error_code &EC, StringRef ErrorMsg = "") {
    ErrorCode = EC;
    ErrorDiagMsg = ErrorMsg.str();
  }
};

} // end namespace llvm

#endif // LLVM_SUPPORT_LOCKFILEMANAGER_H
