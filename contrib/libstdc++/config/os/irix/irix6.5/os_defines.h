// Specific definitions for IRIX  -*- C++ -*-

// Copyright (C) 2000, 2002 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING.  If not, write to the Free
// Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
// USA.

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

#ifndef _GLIBCXX_OS_DEFINES
#define _GLIBCXX_OS_DEFINES 1

// System-specific #define, typedefs, corrections, etc, go here.  This
// file will come before all others.

// We need large file support.  There are two ways to turn it on: by
// defining either _LARGEFILE64_SOURCE or _SGI_SOURCE.  However, it
// does not actually work to define only the former, as then
// <sys/stat.h> is invalid: `st_blocks' is defined to be a macro, but
// then used as a field name.  So, we have to turn on _SGI_SOURCE.
// That only works if _POSIX_SOURCE is turned off, so we have to
// explicitly turn it off.  (Some of the libio C files explicitly try
// to turn it on.)  _SGI_SOURCE is actually turned on implicitly via
// the command-line.
#undef _POSIX_SOURCE

// GCC does not use thunks on IRIX. 
#define _G_USING_THUNKS 0

// FINOREAD takes an "off_t *" as argument.
#define _GLIBCXX_FIONREAD_TAKES_OFF_T

#endif

