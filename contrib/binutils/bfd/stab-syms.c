/* Table of stab names for the BFD library.
   Copyright 1990, 1991, 1992, 1994, 1995, 1996, 2000
   Free Software Foundation, Inc.
   Written by Cygnus Support.

This file is part of BFD, the Binary File Descriptor library.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

#include "bfd.h"

#define ARCH_SIZE 32		/* Value doesn't matter.  */
#include "libaout.h"
#include "aout/aout64.h"

/* Ignore duplicate stab codes; just return the string for the first
   one.  */
#define __define_stab(NAME, CODE, STRING) __define_name(CODE, STRING)
#define __define_stab_duplicate(NAME, CODE, STRING)

/* These are not really stab symbols, but it is
   convenient to have them here for the sake of nm.
   For completeness, we could also add N_TEXT etc, but those
   are never needed, since nm treats those specially.  */
#define EXTRA_SYMBOLS \
  __define_name (N_SETA, "SETA")/* Absolute set element symbol */ \
  __define_name (N_SETT, "SETT")/* Text set element symbol */ \
  __define_name (N_SETD, "SETD")/* Data set element symbol */ \
  __define_name (N_SETB, "SETB")/* Bss set element symbol */ \
  __define_name (N_SETV, "SETV")/* Pointer to set vector in data area.  */ \
  __define_name (N_INDR, "INDR") \
  __define_name (N_WARNING, "WARNING")

const char *
bfd_get_stab_name (code)
     int code;
{
  switch (code)
    {
#define __define_name(val, str) case val: return str;
#include "aout/stab.def"
      EXTRA_SYMBOLS
    }

  return (const char *) 0;
}
