/* Base configuration file for all FreeBSD targets.
   Copyright (C) 1999, 2000, 2001, 2002 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

/* Common FreeBSD configuration. 
   All FreeBSD architectures should include this file, which will specify
   their commonalities.
   Adapted from gcc/config/i386/freebsd-elf.h by 
   David O'Brien <obrien@FreeBSD.org>.  
   Further work by David O'Brien <obrien@FreeBSD.org> and
   Loren J. Rittle <ljrittle@acm.org>.  */

/* $FreeBSD$ */

/* In case we need to know.  */
#define USING_CONFIG_FREEBSD 1

/* This defines which switch letters take arguments.  On FreeBSD, most of
   the normal cases (defined in gcc.c) apply, and we also have -h* and
   -z* options (for the linker) (coming from SVR4).
   We also have -R (alias --rpath), no -z, --soname (-h), --assert etc.  */

#undef  SWITCH_TAKES_ARG
#define SWITCH_TAKES_ARG(CHAR) (FBSD_SWITCH_TAKES_ARG(CHAR))

#undef  WORD_SWITCH_TAKES_ARG
#define WORD_SWITCH_TAKES_ARG(STR) (FBSD_WORD_SWITCH_TAKES_ARG(STR))

#undef  TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS() FBSD_TARGET_OS_CPP_BUILTINS()

#undef  CPP_SPEC
#define CPP_SPEC FBSD_CPP_SPEC

#undef  STARTFILE_SPEC
#define STARTFILE_SPEC FBSD_STARTFILE_SPEC

#undef  ENDFILE_SPEC
#define ENDFILE_SPEC FBSD_ENDFILE_SPEC

#undef  LIB_SPEC
#define LIB_SPEC FBSD_LIB_SPEC


/************************[  Target stuff  ]***********************************/

/* All FreeBSD Architectures support the ELF object file format.  */
#undef  OBJECT_FORMAT_ELF
#define OBJECT_FORMAT_ELF	1
#undef  TARGET_ELF
#define TARGET_ELF		1

/* Don't assume anything about the header files.  */
#undef  NO_IMPLICIT_EXTERN_C
#define NO_IMPLICIT_EXTERN_C	1

/* Make gcc agree with FreeBSD's standard headers (<machine/ansi.h>, etc...)  */
#undef  SIZE_TYPE
#define SIZE_TYPE FBSD_SIZE_TYPE
#undef  PTRDIFF_TYPE
#define PTRDIFF_TYPE FBSD_PTRDIFF_TYPE

#undef  WCHAR_TYPE
#define WCHAR_TYPE "int"
#undef  WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE 32
#undef  WINT_TYPE
#define WINT_TYPE "int"
#undef  WINT_TYPE_SIZE
#define WINT_TYPE_SIZE 32

#ifdef FREEBSD_NATIVE
#define LIBSTDCXX_PROFILE       "-lstdc++_p"
#endif
#define MATH_LIBRARY_PROFILE    "-lm_p"

/* Code generation parameters.  */

/* Writing `int' for a bitfield forces int alignment for the structure.  */
/* XXX: ok for Alpha??  */
#undef  PCC_BITFIELD_TYPE_MATTERS
#define PCC_BITFIELD_TYPE_MATTERS 1

/* Use periods rather than dollar signs in special g++ assembler names.
   This ensures the configuration knows our system correctly so we can link
   with libraries compiled with the native cc.  */
#undef NO_DOLLAR_IN_LABEL

/* Define this so we can compile MS code for use with WINE.  */
#define HANDLE_PRAGMA_PACK_PUSH_POP 1

#define TARGET_POSIX_IO
