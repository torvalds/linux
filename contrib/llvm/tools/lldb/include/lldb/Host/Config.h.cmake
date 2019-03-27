//===-- Config.h -----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_CONFIG_H
#define LLDB_HOST_CONFIG_H

#cmakedefine LLDB_CONFIG_TERMIOS_SUPPORTED

#cmakedefine01 LLDB_EDITLINE_USE_WCHAR

#cmakedefine01 LLDB_HAVE_EL_RFUNC_T

#cmakedefine LLDB_DISABLE_POSIX

#define LLDB_LIBDIR_SUFFIX "${LLVM_LIBDIR_SUFFIX}"

#cmakedefine01 HAVE_SYS_EVENT_H

#cmakedefine01 HAVE_PPOLL

#cmakedefine01 HAVE_SIGACTION

#cmakedefine01 HAVE_PROCESS_VM_READV

#cmakedefine01 HAVE_NR_PROCESS_VM_READV

#ifndef HAVE_LIBCOMPRESSION
#cmakedefine HAVE_LIBCOMPRESSION
#endif

#endif // #ifndef LLDB_HOST_CONFIG_H
