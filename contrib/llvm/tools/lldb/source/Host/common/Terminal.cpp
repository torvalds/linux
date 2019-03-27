//===-- Terminal.cpp --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/Terminal.h"

#include "lldb/Host/Config.h"
#include "lldb/Host/PosixApi.h"
#include "llvm/ADT/STLExtras.h"

#include <fcntl.h>
#include <signal.h>

#ifdef LLDB_CONFIG_TERMIOS_SUPPORTED
#include <termios.h>
#endif

using namespace lldb_private;

bool Terminal::IsATerminal() const { return m_fd >= 0 && ::isatty(m_fd); }

bool Terminal::SetEcho(bool enabled) {
  if (FileDescriptorIsValid()) {
#ifdef LLDB_CONFIG_TERMIOS_SUPPORTED
    if (IsATerminal()) {
      struct termios fd_termios;
      if (::tcgetattr(m_fd, &fd_termios) == 0) {
        bool set_corectly = false;
        if (enabled) {
          if (fd_termios.c_lflag & ECHO)
            set_corectly = true;
          else
            fd_termios.c_lflag |= ECHO;
        } else {
          if (fd_termios.c_lflag & ECHO)
            fd_termios.c_lflag &= ~ECHO;
          else
            set_corectly = true;
        }

        if (set_corectly)
          return true;
        return ::tcsetattr(m_fd, TCSANOW, &fd_termios) == 0;
      }
    }
#endif // #ifdef LLDB_CONFIG_TERMIOS_SUPPORTED
  }
  return false;
}

bool Terminal::SetCanonical(bool enabled) {
  if (FileDescriptorIsValid()) {
#ifdef LLDB_CONFIG_TERMIOS_SUPPORTED
    if (IsATerminal()) {
      struct termios fd_termios;
      if (::tcgetattr(m_fd, &fd_termios) == 0) {
        bool set_corectly = false;
        if (enabled) {
          if (fd_termios.c_lflag & ICANON)
            set_corectly = true;
          else
            fd_termios.c_lflag |= ICANON;
        } else {
          if (fd_termios.c_lflag & ICANON)
            fd_termios.c_lflag &= ~ICANON;
          else
            set_corectly = true;
        }

        if (set_corectly)
          return true;
        return ::tcsetattr(m_fd, TCSANOW, &fd_termios) == 0;
      }
    }
#endif // #ifdef LLDB_CONFIG_TERMIOS_SUPPORTED
  }
  return false;
}

//----------------------------------------------------------------------
// Default constructor
//----------------------------------------------------------------------
TerminalState::TerminalState()
    : m_tty(), m_tflags(-1),
#ifdef LLDB_CONFIG_TERMIOS_SUPPORTED
      m_termios_ap(),
#endif
      m_process_group(-1) {
}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
TerminalState::~TerminalState() {}

void TerminalState::Clear() {
  m_tty.Clear();
  m_tflags = -1;
#ifdef LLDB_CONFIG_TERMIOS_SUPPORTED
  m_termios_ap.reset();
#endif
  m_process_group = -1;
}

//----------------------------------------------------------------------
// Save the current state of the TTY for the file descriptor "fd" and if
// "save_process_group" is true, attempt to save the process group info for the
// TTY.
//----------------------------------------------------------------------
bool TerminalState::Save(int fd, bool save_process_group) {
  m_tty.SetFileDescriptor(fd);
  if (m_tty.IsATerminal()) {
#ifndef LLDB_DISABLE_POSIX
    m_tflags = ::fcntl(fd, F_GETFL, 0);
#endif
#ifdef LLDB_CONFIG_TERMIOS_SUPPORTED
    if (m_termios_ap.get() == NULL)
      m_termios_ap.reset(new struct termios);
    int err = ::tcgetattr(fd, m_termios_ap.get());
    if (err != 0)
      m_termios_ap.reset();
#endif // #ifdef LLDB_CONFIG_TERMIOS_SUPPORTED
#ifndef LLDB_DISABLE_POSIX
    if (save_process_group)
      m_process_group = ::tcgetpgrp(0);
    else
      m_process_group = -1;
#endif
  } else {
    m_tty.Clear();
    m_tflags = -1;
#ifdef LLDB_CONFIG_TERMIOS_SUPPORTED
    m_termios_ap.reset();
#endif
    m_process_group = -1;
  }
  return IsValid();
}

//----------------------------------------------------------------------
// Restore the state of the TTY using the cached values from a previous call to
// Save().
//----------------------------------------------------------------------
bool TerminalState::Restore() const {
#ifndef LLDB_DISABLE_POSIX
  if (IsValid()) {
    const int fd = m_tty.GetFileDescriptor();
    if (TFlagsIsValid())
      fcntl(fd, F_SETFL, m_tflags);

#ifdef LLDB_CONFIG_TERMIOS_SUPPORTED
    if (TTYStateIsValid())
      tcsetattr(fd, TCSANOW, m_termios_ap.get());
#endif // #ifdef LLDB_CONFIG_TERMIOS_SUPPORTED

    if (ProcessGroupIsValid()) {
      // Save the original signal handler.
      void (*saved_sigttou_callback)(int) = NULL;
      saved_sigttou_callback = (void (*)(int))signal(SIGTTOU, SIG_IGN);
      // Set the process group
      tcsetpgrp(fd, m_process_group);
      // Restore the original signal handler.
      signal(SIGTTOU, saved_sigttou_callback);
    }
    return true;
  }
#endif
  return false;
}

//----------------------------------------------------------------------
// Returns true if this object has valid saved TTY state settings that can be
// used to restore a previous state.
//----------------------------------------------------------------------
bool TerminalState::IsValid() const {
  return m_tty.FileDescriptorIsValid() &&
         (TFlagsIsValid() || TTYStateIsValid());
}

//----------------------------------------------------------------------
// Returns true if m_tflags is valid
//----------------------------------------------------------------------
bool TerminalState::TFlagsIsValid() const { return m_tflags != -1; }

//----------------------------------------------------------------------
// Returns true if m_ttystate is valid
//----------------------------------------------------------------------
bool TerminalState::TTYStateIsValid() const {
#ifdef LLDB_CONFIG_TERMIOS_SUPPORTED
  return m_termios_ap.get() != 0;
#else
  return false;
#endif
}

//----------------------------------------------------------------------
// Returns true if m_process_group is valid
//----------------------------------------------------------------------
bool TerminalState::ProcessGroupIsValid() const {
  return static_cast<int32_t>(m_process_group) != -1;
}

//------------------------------------------------------------------
// Constructor
//------------------------------------------------------------------
TerminalStateSwitcher::TerminalStateSwitcher() : m_currentState(UINT32_MAX) {}

//------------------------------------------------------------------
// Destructor
//------------------------------------------------------------------
TerminalStateSwitcher::~TerminalStateSwitcher() {}

//------------------------------------------------------------------
// Returns the number of states that this switcher contains
//------------------------------------------------------------------
uint32_t TerminalStateSwitcher::GetNumberOfStates() const {
  return llvm::array_lengthof(m_ttystates);
}

//------------------------------------------------------------------
// Restore the state at index "idx".
//
// Returns true if the restore was successful, false otherwise.
//------------------------------------------------------------------
bool TerminalStateSwitcher::Restore(uint32_t idx) const {
  const uint32_t num_states = GetNumberOfStates();
  if (idx >= num_states)
    return false;

  // See if we already are in this state?
  if (m_currentState < num_states && (idx == m_currentState) &&
      m_ttystates[idx].IsValid())
    return true;

  // Set the state to match the index passed in and only update the current
  // state if there are no errors.
  if (m_ttystates[idx].Restore()) {
    m_currentState = idx;
    return true;
  }

  // We failed to set the state. The tty state was invalid or not initialized.
  return false;
}

//------------------------------------------------------------------
// Save the state at index "idx" for file descriptor "fd" and save the process
// group if requested.
//
// Returns true if the restore was successful, false otherwise.
//------------------------------------------------------------------
bool TerminalStateSwitcher::Save(uint32_t idx, int fd,
                                 bool save_process_group) {
  const uint32_t num_states = GetNumberOfStates();
  if (idx < num_states)
    return m_ttystates[idx].Save(fd, save_process_group);
  return false;
}
