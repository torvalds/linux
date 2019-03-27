/* CPP Library.
   Copyright (C) 1986, 1987, 1989, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2003, 2004, 2006 Free Software Foundation, Inc.
   Contributed by Per Bothner, 1994-95.
   Based on CCCP program by Paul Rubin, June 1986
   Adapted to ANSI C, Richard Stallman, Jan 1987

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */

#ifndef GCC_CPPDEFAULT_H
#define GCC_CPPDEFAULT_H

/* This is the default list of directories to search for include files.
   It may be overridden by the various -I and -ixxx options.

   #include "file" looks in the same directory as the current file,
   then this list.
   #include <file> just looks in this list.

   All these directories are treated as `system' include directories
   (they are not subject to pedantic warnings in some cases).  */

struct default_include
{
  const char *const fname;	/* The name of the directory.  */
  const char *const component;	/* The component containing the directory
				   (see update_path in prefix.c) */
  const char cplusplus;		/* Only look here if we're compiling C++.  */
  const char cxx_aware;		/* Includes in this directory don't need to
				   be wrapped in extern "C" when compiling
				   C++.  */
  const char add_sysroot;	/* FNAME should be prefixed by
				   cpp_SYSROOT.  */
  const char multilib;		/* FNAME should have the multilib path
				   specified with -imultilib
				   appended.  */
};

extern const struct default_include cpp_include_defaults[];
extern const char cpp_GCC_INCLUDE_DIR[];
extern const size_t cpp_GCC_INCLUDE_DIR_len;

#endif /* ! GCC_CPPDEFAULT_H */
