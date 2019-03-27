//===-- PseudoTerminal.cpp --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/PseudoTerminal.h"
#include "lldb/Host/Config.h"

#include "llvm/Support/Errno.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(TIOCSCTTY)
#include <sys/ioctl.h>
#endif

#include "lldb/Host/PosixApi.h"

#if defined(__ANDROID__)
int posix_openpt(int flags);
#endif

using namespace lldb_private;

//----------------------------------------------------------------------
// Write string describing error number
//----------------------------------------------------------------------
static void ErrnoToStr(char *error_str, size_t error_len) {
  std::string strerror = llvm::sys::StrError();
  ::snprintf(error_str, error_len, "%s", strerror.c_str());
}

//----------------------------------------------------------------------
// PseudoTerminal constructor
//----------------------------------------------------------------------
PseudoTerminal::PseudoTerminal()
    : m_master_fd(invalid_fd), m_slave_fd(invalid_fd) {}

//----------------------------------------------------------------------
// Destructor
//
// The destructor will close the master and slave file descriptors if they are
// valid and ownership has not been released using the
// ReleaseMasterFileDescriptor() or the ReleaseSaveFileDescriptor() member
// functions.
//----------------------------------------------------------------------
PseudoTerminal::~PseudoTerminal() {
  CloseMasterFileDescriptor();
  CloseSlaveFileDescriptor();
}

//----------------------------------------------------------------------
// Close the master file descriptor if it is valid.
//----------------------------------------------------------------------
void PseudoTerminal::CloseMasterFileDescriptor() {
  if (m_master_fd >= 0) {
    ::close(m_master_fd);
    m_master_fd = invalid_fd;
  }
}

//----------------------------------------------------------------------
// Close the slave file descriptor if it is valid.
//----------------------------------------------------------------------
void PseudoTerminal::CloseSlaveFileDescriptor() {
  if (m_slave_fd >= 0) {
    ::close(m_slave_fd);
    m_slave_fd = invalid_fd;
  }
}

//----------------------------------------------------------------------
// Open the first available pseudo terminal with OFLAG as the permissions. The
// file descriptor is stored in this object and can be accessed with the
// MasterFileDescriptor() accessor. The ownership of the master file descriptor
// can be released using the ReleaseMasterFileDescriptor() accessor. If this
// object has a valid master files descriptor when its destructor is called, it
// will close the master file descriptor, therefore clients must call
// ReleaseMasterFileDescriptor() if they wish to use the master file descriptor
// after this object is out of scope or destroyed.
//
// RETURNS:
//  True when successful, false indicating an error occurred.
//----------------------------------------------------------------------
bool PseudoTerminal::OpenFirstAvailableMaster(int oflag, char *error_str,
                                              size_t error_len) {
  if (error_str)
    error_str[0] = '\0';

#if !defined(LLDB_DISABLE_POSIX)
  // Open the master side of a pseudo terminal
  m_master_fd = ::posix_openpt(oflag);
  if (m_master_fd < 0) {
    if (error_str)
      ErrnoToStr(error_str, error_len);
    return false;
  }

  // Grant access to the slave pseudo terminal
  if (::grantpt(m_master_fd) < 0) {
    if (error_str)
      ErrnoToStr(error_str, error_len);
    CloseMasterFileDescriptor();
    return false;
  }

  // Clear the lock flag on the slave pseudo terminal
  if (::unlockpt(m_master_fd) < 0) {
    if (error_str)
      ErrnoToStr(error_str, error_len);
    CloseMasterFileDescriptor();
    return false;
  }

  return true;
#else
  if (error_str)
    ::snprintf(error_str, error_len, "%s", "pseudo terminal not supported");
  return false;
#endif
}

//----------------------------------------------------------------------
// Open the slave pseudo terminal for the current master pseudo terminal. A
// master pseudo terminal should already be valid prior to calling this
// function (see OpenFirstAvailableMaster()). The file descriptor is stored
// this object's member variables and can be accessed via the
// GetSlaveFileDescriptor(), or released using the ReleaseSlaveFileDescriptor()
// member function.
//
// RETURNS:
//  True when successful, false indicating an error occurred.
//----------------------------------------------------------------------
bool PseudoTerminal::OpenSlave(int oflag, char *error_str, size_t error_len) {
  if (error_str)
    error_str[0] = '\0';

  CloseSlaveFileDescriptor();

  // Open the master side of a pseudo terminal
  const char *slave_name = GetSlaveName(error_str, error_len);

  if (slave_name == nullptr)
    return false;

  m_slave_fd = ::open(slave_name, oflag);

  if (m_slave_fd < 0) {
    if (error_str)
      ErrnoToStr(error_str, error_len);
    return false;
  }

  return true;
}

//----------------------------------------------------------------------
// Get the name of the slave pseudo terminal. A master pseudo terminal should
// already be valid prior to calling this function (see
// OpenFirstAvailableMaster()).
//
// RETURNS:
//  NULL if no valid master pseudo terminal or if ptsname() fails.
//  The name of the slave pseudo terminal as a NULL terminated C string
//  that comes from static memory, so a copy of the string should be
//  made as subsequent calls can change this value.
//----------------------------------------------------------------------
const char *PseudoTerminal::GetSlaveName(char *error_str,
                                         size_t error_len) const {
  if (error_str)
    error_str[0] = '\0';

  if (m_master_fd < 0) {
    if (error_str)
      ::snprintf(error_str, error_len, "%s",
                 "master file descriptor is invalid");
    return nullptr;
  }
  const char *slave_name = ::ptsname(m_master_fd);

  if (error_str && slave_name == nullptr)
    ErrnoToStr(error_str, error_len);

  return slave_name;
}

//----------------------------------------------------------------------
// Fork a child process and have its stdio routed to a pseudo terminal.
//
// In the parent process when a valid pid is returned, the master file
// descriptor can be used as a read/write access to stdio of the child process.
//
// In the child process the stdin/stdout/stderr will already be routed to the
// slave pseudo terminal and the master file descriptor will be closed as it is
// no longer needed by the child process.
//
// This class will close the file descriptors for the master/slave when the
// destructor is called, so be sure to call ReleaseMasterFileDescriptor() or
// ReleaseSlaveFileDescriptor() if any file descriptors are going to be used
// past the lifespan of this object.
//
// RETURNS:
//  in the parent process: the pid of the child, or -1 if fork fails
//  in the child process: zero
//----------------------------------------------------------------------
lldb::pid_t PseudoTerminal::Fork(char *error_str, size_t error_len) {
  if (error_str)
    error_str[0] = '\0';
  pid_t pid = LLDB_INVALID_PROCESS_ID;
#if !defined(LLDB_DISABLE_POSIX)
  int flags = O_RDWR;
  flags |= O_CLOEXEC;
  if (OpenFirstAvailableMaster(flags, error_str, error_len)) {
    // Successfully opened our master pseudo terminal

    pid = ::fork();
    if (pid < 0) {
      // Fork failed
      if (error_str)
        ErrnoToStr(error_str, error_len);
    } else if (pid == 0) {
      // Child Process
      ::setsid();

      if (OpenSlave(O_RDWR, error_str, error_len)) {
        // Successfully opened slave

        // Master FD should have O_CLOEXEC set, but let's close it just in
        // case...
        CloseMasterFileDescriptor();

#if defined(TIOCSCTTY)
        // Acquire the controlling terminal
        if (::ioctl(m_slave_fd, TIOCSCTTY, (char *)0) < 0) {
          if (error_str)
            ErrnoToStr(error_str, error_len);
        }
#endif
        // Duplicate all stdio file descriptors to the slave pseudo terminal
        if (::dup2(m_slave_fd, STDIN_FILENO) != STDIN_FILENO) {
          if (error_str && !error_str[0])
            ErrnoToStr(error_str, error_len);
        }

        if (::dup2(m_slave_fd, STDOUT_FILENO) != STDOUT_FILENO) {
          if (error_str && !error_str[0])
            ErrnoToStr(error_str, error_len);
        }

        if (::dup2(m_slave_fd, STDERR_FILENO) != STDERR_FILENO) {
          if (error_str && !error_str[0])
            ErrnoToStr(error_str, error_len);
        }
      }
    } else {
      // Parent Process
      // Do nothing and let the pid get returned!
    }
  }
#endif
  return pid;
}

//----------------------------------------------------------------------
// The master file descriptor accessor. This object retains ownership of the
// master file descriptor when this accessor is used. Use
// ReleaseMasterFileDescriptor() if you wish this object to release ownership
// of the master file descriptor.
//
// Returns the master file descriptor, or -1 if the master file descriptor is
// not currently valid.
//----------------------------------------------------------------------
int PseudoTerminal::GetMasterFileDescriptor() const { return m_master_fd; }

//----------------------------------------------------------------------
// The slave file descriptor accessor.
//
// Returns the slave file descriptor, or -1 if the slave file descriptor is not
// currently valid.
//----------------------------------------------------------------------
int PseudoTerminal::GetSlaveFileDescriptor() const { return m_slave_fd; }

//----------------------------------------------------------------------
// Release ownership of the master pseudo terminal file descriptor without
// closing it. The destructor for this class will close the master file
// descriptor if the ownership isn't released using this call and the master
// file descriptor has been opened.
//----------------------------------------------------------------------
int PseudoTerminal::ReleaseMasterFileDescriptor() {
  // Release ownership of the master pseudo terminal file descriptor without
  // closing it. (the destructor for this class will close it otherwise!)
  int fd = m_master_fd;
  m_master_fd = invalid_fd;
  return fd;
}

//----------------------------------------------------------------------
// Release ownership of the slave pseudo terminal file descriptor without
// closing it. The destructor for this class will close the slave file
// descriptor if the ownership isn't released using this call and the slave
// file descriptor has been opened.
//----------------------------------------------------------------------
int PseudoTerminal::ReleaseSlaveFileDescriptor() {
  // Release ownership of the slave pseudo terminal file descriptor without
  // closing it (the destructor for this class will close it otherwise!)
  int fd = m_slave_fd;
  m_slave_fd = invalid_fd;
  return fd;
}
