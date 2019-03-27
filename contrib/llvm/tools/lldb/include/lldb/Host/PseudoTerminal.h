//===-- PseudoTerminal.h ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_PSEUDOTERMINAL_H
#define LLDB_HOST_PSEUDOTERMINAL_H

#include <fcntl.h>
#include <string>

#include "lldb/lldb-defines.h"

namespace lldb_private {

//----------------------------------------------------------------------
/// @class PseudoTerminal PseudoTerminal.h "lldb/Host/PseudoTerminal.h"
/// A pseudo terminal helper class.
///
/// The pseudo terminal class abstracts the use of pseudo terminals on the
/// host system.
//----------------------------------------------------------------------
class PseudoTerminal {
public:
  enum {
    invalid_fd = -1 ///< Invalid file descriptor value
  };

  //------------------------------------------------------------------
  /// Default constructor
  ///
  /// Constructs this object with invalid master and slave file descriptors.
  //------------------------------------------------------------------
  PseudoTerminal();

  //------------------------------------------------------------------
  /// Destructor
  ///
  /// The destructor will close the master and slave file descriptors if they
  /// are valid and ownership has not been released using one of: @li
  /// PseudoTerminal::ReleaseMasterFileDescriptor() @li
  /// PseudoTerminal::ReleaseSaveFileDescriptor()
  //------------------------------------------------------------------
  ~PseudoTerminal();

  //------------------------------------------------------------------
  /// Close the master file descriptor if it is valid.
  //------------------------------------------------------------------
  void CloseMasterFileDescriptor();

  //------------------------------------------------------------------
  /// Close the slave file descriptor if it is valid.
  //------------------------------------------------------------------
  void CloseSlaveFileDescriptor();

  //------------------------------------------------------------------
  /// Fork a child process that uses pseudo terminals for its stdio.
  ///
  /// In the parent process, a call to this function results in a pid being
  /// returned. If the pid is valid, the master file descriptor can be used
  /// for read/write access to stdio of the child process.
  ///
  /// In the child process the stdin/stdout/stderr will already be routed to
  /// the slave pseudo terminal and the master file descriptor will be closed
  /// as it is no longer needed by the child process.
  ///
  /// This class will close the file descriptors for the master/slave when the
  /// destructor is called. The file handles can be released using either: @li
  /// PseudoTerminal::ReleaseMasterFileDescriptor() @li
  /// PseudoTerminal::ReleaseSaveFileDescriptor()
  ///
  /// @param[out] error
  ///     An pointer to an error that can describe any errors that
  ///     occur. This can be NULL if no error status is desired.
  ///
  /// @return
  ///     @li \b Parent process: a child process ID that is greater
  ///         than zero, or -1 if the fork fails.
  ///     @li \b Child process: zero.
  //------------------------------------------------------------------
  lldb::pid_t Fork(char *error_str, size_t error_len);

  //------------------------------------------------------------------
  /// The master file descriptor accessor.
  ///
  /// This object retains ownership of the master file descriptor when this
  /// accessor is used. Users can call the member function
  /// PseudoTerminal::ReleaseMasterFileDescriptor() if this object should
  /// release ownership of the slave file descriptor.
  ///
  /// @return
  ///     The master file descriptor, or PseudoTerminal::invalid_fd
  ///     if the master file  descriptor is not currently valid.
  ///
  /// @see PseudoTerminal::ReleaseMasterFileDescriptor()
  //------------------------------------------------------------------
  int GetMasterFileDescriptor() const;

  //------------------------------------------------------------------
  /// The slave file descriptor accessor.
  ///
  /// This object retains ownership of the slave file descriptor when this
  /// accessor is used. Users can call the member function
  /// PseudoTerminal::ReleaseSlaveFileDescriptor() if this object should
  /// release ownership of the slave file descriptor.
  ///
  /// @return
  ///     The slave file descriptor, or PseudoTerminal::invalid_fd
  ///     if the slave file descriptor is not currently valid.
  ///
  /// @see PseudoTerminal::ReleaseSlaveFileDescriptor()
  //------------------------------------------------------------------
  int GetSlaveFileDescriptor() const;

  //------------------------------------------------------------------
  /// Get the name of the slave pseudo terminal.
  ///
  /// A master pseudo terminal should already be valid prior to
  /// calling this function.
  ///
  /// @param[out] error
  ///     An pointer to an error that can describe any errors that
  ///     occur. This can be NULL if no error status is desired.
  ///
  /// @return
  ///     The name of the slave pseudo terminal as a NULL terminated
  ///     C. This string that comes from static memory, so a copy of
  ///     the string should be made as subsequent calls can change
  ///     this value. NULL is returned if this object doesn't have
  ///     a valid master pseudo terminal opened or if the call to
  ///     \c ptsname() fails.
  ///
  /// @see PseudoTerminal::OpenFirstAvailableMaster()
  //------------------------------------------------------------------
  const char *GetSlaveName(char *error_str, size_t error_len) const;

  //------------------------------------------------------------------
  /// Open the first available pseudo terminal.
  ///
  /// Opens the first available pseudo terminal with \a oflag as the
  /// permissions. The opened master file descriptor is stored in this object
  /// and can be accessed by calling the
  /// PseudoTerminal::GetMasterFileDescriptor() accessor. Clients can call the
  /// PseudoTerminal::ReleaseMasterFileDescriptor() accessor function if they
  /// wish to use the master file descriptor beyond the lifespan of this
  /// object.
  ///
  /// If this object still has a valid master file descriptor when its
  /// destructor is called, it will close it.
  ///
  /// @param[in] oflag
  ///     Flags to use when calling \c posix_openpt(\a oflag).
  ///     A value of "O_RDWR|O_NOCTTY" is suggested.
  ///
  /// @param[out] error
  ///     An pointer to an error that can describe any errors that
  ///     occur. This can be NULL if no error status is desired.
  ///
  /// @return
  ///     @li \b true when the master files descriptor is
  ///         successfully opened.
  ///     @li \b false if anything goes wrong.
  ///
  /// @see PseudoTerminal::GetMasterFileDescriptor() @see
  /// PseudoTerminal::ReleaseMasterFileDescriptor()
  //------------------------------------------------------------------
  bool OpenFirstAvailableMaster(int oflag, char *error_str, size_t error_len);

  //------------------------------------------------------------------
  /// Open the slave for the current master pseudo terminal.
  ///
  /// A master pseudo terminal should already be valid prior to
  /// calling this function. The opened slave file descriptor is stored in
  /// this object and can be accessed by calling the
  /// PseudoTerminal::GetSlaveFileDescriptor() accessor. Clients can call the
  /// PseudoTerminal::ReleaseSlaveFileDescriptor() accessor function if they
  /// wish to use the slave file descriptor beyond the lifespan of this
  /// object.
  ///
  /// If this object still has a valid slave file descriptor when its
  /// destructor is called, it will close it.
  ///
  /// @param[in] oflag
  ///     Flags to use when calling \c open(\a oflag).
  ///
  /// @param[out] error
  ///     An pointer to an error that can describe any errors that
  ///     occur. This can be NULL if no error status is desired.
  ///
  /// @return
  ///     @li \b true when the master files descriptor is
  ///         successfully opened.
  ///     @li \b false if anything goes wrong.
  ///
  /// @see PseudoTerminal::OpenFirstAvailableMaster() @see
  /// PseudoTerminal::GetSlaveFileDescriptor() @see
  /// PseudoTerminal::ReleaseSlaveFileDescriptor()
  //------------------------------------------------------------------
  bool OpenSlave(int oflag, char *error_str, size_t error_len);

  //------------------------------------------------------------------
  /// Release the master file descriptor.
  ///
  /// Releases ownership of the master pseudo terminal file descriptor without
  /// closing it. The destructor for this class will close the master file
  /// descriptor if the ownership isn't released using this call and the
  /// master file descriptor has been opened.
  ///
  /// @return
  ///     The master file descriptor, or PseudoTerminal::invalid_fd
  ///     if the mast file descriptor is not currently valid.
  //------------------------------------------------------------------
  int ReleaseMasterFileDescriptor();

  //------------------------------------------------------------------
  /// Release the slave file descriptor.
  ///
  /// Release ownership of the slave pseudo terminal file descriptor without
  /// closing it. The destructor for this class will close the slave file
  /// descriptor if the ownership isn't released using this call and the slave
  /// file descriptor has been opened.
  ///
  /// @return
  ///     The slave file descriptor, or PseudoTerminal::invalid_fd
  ///     if the slave file descriptor is not currently valid.
  //------------------------------------------------------------------
  int ReleaseSlaveFileDescriptor();

protected:
  //------------------------------------------------------------------
  // Member variables
  //------------------------------------------------------------------
  int m_master_fd; ///< The file descriptor for the master.
  int m_slave_fd;  ///< The file descriptor for the slave.

private:
  DISALLOW_COPY_AND_ASSIGN(PseudoTerminal);
};

} // namespace lldb_private

#endif // #ifndef liblldb_PseudoTerminal_h_
