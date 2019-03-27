//===-- Terminal.h ----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Terminal_h_
#define liblldb_Terminal_h_
#if defined(__cplusplus)

#include "lldb/Host/Config.h"
#include "lldb/lldb-private.h"

struct termios;

namespace lldb_private {

class Terminal {
public:
  Terminal(int fd = -1) : m_fd(fd) {}

  ~Terminal() {}

  bool IsATerminal() const;

  int GetFileDescriptor() const { return m_fd; }

  void SetFileDescriptor(int fd) { m_fd = fd; }

  bool FileDescriptorIsValid() const { return m_fd != -1; }

  void Clear() { m_fd = -1; }

  bool SetEcho(bool enabled);

  bool SetCanonical(bool enabled);

protected:
  int m_fd; // This may or may not be a terminal file descriptor
};

//----------------------------------------------------------------------
/// @class State Terminal.h "lldb/Host/Terminal.h"
/// A terminal state saving/restoring class.
///
/// This class can be used to remember the terminal state for a file
/// descriptor and later restore that state as it originally was.
//----------------------------------------------------------------------
class TerminalState {
public:
  //------------------------------------------------------------------
  /// Default constructor
  //------------------------------------------------------------------
  TerminalState();

  //------------------------------------------------------------------
  /// Destructor
  //------------------------------------------------------------------
  ~TerminalState();

  //------------------------------------------------------------------
  /// Save the TTY state for \a fd.
  ///
  /// Save the current state of the TTY for the file descriptor "fd" and if
  /// "save_process_group" is true, attempt to save the process group info for
  /// the TTY.
  ///
  /// @param[in] fd
  ///     The file descriptor to save the state of.
  ///
  /// @param[in] save_process_group
  ///     If \b true, save the process group settings, else do not
  ///     save the process group settings for a TTY.
  ///
  /// @return
  ///     Returns \b true if \a fd describes a TTY and if the state
  ///     was able to be saved, \b false otherwise.
  //------------------------------------------------------------------
  bool Save(int fd, bool save_process_group);

  //------------------------------------------------------------------
  /// Restore the TTY state to the cached state.
  ///
  /// Restore the state of the TTY using the cached values from a previous
  /// call to TerminalState::Save(int,bool).
  ///
  /// @return
  ///     Returns \b true if the TTY state was successfully restored,
  ///     \b false otherwise.
  //------------------------------------------------------------------
  bool Restore() const;

  //------------------------------------------------------------------
  /// Test for valid cached TTY state information.
  ///
  /// @return
  ///     Returns \b true if this object has valid saved TTY state
  ///     settings that can be used to restore a previous state,
  ///     \b false otherwise.
  //------------------------------------------------------------------
  bool IsValid() const;

  void Clear();

protected:
  //------------------------------------------------------------------
  /// Test if tflags is valid.
  ///
  /// @return
  ///     Returns \b true if \a m_tflags is valid and can be restored,
  ///     \b false otherwise.
  //------------------------------------------------------------------
  bool TFlagsIsValid() const;

  //------------------------------------------------------------------
  /// Test if ttystate is valid.
  ///
  /// @return
  ///     Returns \b true if \a m_ttystate is valid and can be
  ///     restored, \b false otherwise.
  //------------------------------------------------------------------
  bool TTYStateIsValid() const;

  //------------------------------------------------------------------
  /// Test if the process group information is valid.
  ///
  /// @return
  ///     Returns \b true if \a m_process_group is valid and can be
  ///     restored, \b false otherwise.
  //------------------------------------------------------------------
  bool ProcessGroupIsValid() const;

  //------------------------------------------------------------------
  // Member variables
  //------------------------------------------------------------------
  Terminal m_tty; ///< A terminal
  int m_tflags;   ///< Cached tflags information.
#ifdef LLDB_CONFIG_TERMIOS_SUPPORTED
  std::unique_ptr<struct termios>
      m_termios_ap; ///< Cached terminal state information.
#endif
  lldb::pid_t m_process_group; ///< Cached process group information.
};

//----------------------------------------------------------------------
/// @class TerminalStateSwitcher Terminal.h "lldb/Host/Terminal.h"
/// A TTY state switching class.
///
/// This class can be used to remember 2 TTY states for a given file
/// descriptor and switch between the two states.
//----------------------------------------------------------------------
class TerminalStateSwitcher {
public:
  //------------------------------------------------------------------
  /// Constructor
  //------------------------------------------------------------------
  TerminalStateSwitcher();

  //------------------------------------------------------------------
  /// Destructor
  //------------------------------------------------------------------
  ~TerminalStateSwitcher();

  //------------------------------------------------------------------
  /// Get the number of possible states to save.
  ///
  /// @return
  ///     The number of states that this TTY switcher object contains.
  //------------------------------------------------------------------
  uint32_t GetNumberOfStates() const;

  //------------------------------------------------------------------
  /// Restore the TTY state for state at index \a idx.
  ///
  /// @return
  ///     Returns \b true if the TTY state was successfully restored,
  ///     \b false otherwise.
  //------------------------------------------------------------------
  bool Restore(uint32_t idx) const;

  //------------------------------------------------------------------
  /// Save the TTY state information for the state at index \a idx. The TTY
  /// state is saved for the file descriptor \a fd and the process group
  /// information will also be saved if requested by \a save_process_group.
  ///
  /// @param[in] idx
  ///     The index into the state array where the state should be
  ///     saved.
  ///
  /// @param[in] fd
  ///     The file descriptor for which to save the settings.
  ///
  /// @param[in] save_process_group
  ///     If \b true, save the process group information for the TTY.
  ///
  /// @return
  ///     Returns \b true if the save was successful, \b false
  ///     otherwise.
  //------------------------------------------------------------------
  bool Save(uint32_t idx, int fd, bool save_process_group);

protected:
  //------------------------------------------------------------------
  // Member variables
  //------------------------------------------------------------------
  mutable uint32_t m_currentState; ///< The currently active TTY state index.
  TerminalState
      m_ttystates[2]; ///< The array of TTY states that holds saved TTY info.
};

} // namespace lldb_private

#endif // #if defined(__cplusplus)
#endif // #ifndef liblldb_Terminal_h_
