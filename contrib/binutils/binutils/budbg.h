/* budbg.c -- Interfaces to the generic debugging information routines.
   Copyright 1995, 1996, 2002, 2003 Free Software Foundation, Inc.
   Written by Ian Lance Taylor <ian@cygnus.com>.

   This file is part of GNU Binutils.

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
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#ifndef BUDBG_H
#define BUDBG_H

#include <stdio.h>

/* Routine used to read generic debugging information.  */

extern void *read_debugging_info (bfd *, asymbol **, long);

/* Routine used to print generic debugging information.  */

extern bfd_boolean print_debugging_info
  (FILE *, void *, bfd *, asymbol **, void *, bfd_boolean);

/* Routines used to read and write stabs information.  */

extern void *start_stab (void *, bfd *, bfd_boolean, asymbol **, long);

extern bfd_boolean finish_stab (void *, void *);

extern bfd_boolean parse_stab
  (void *, void *, int, int, bfd_vma, const char *);

extern bfd_boolean write_stabs_in_sections_debugging_info
  (bfd *, void *, bfd_byte **, bfd_size_type *, bfd_byte **, bfd_size_type *);

/* Routines used to read and write IEEE debugging information.  */

extern bfd_boolean parse_ieee (void *, bfd *, const bfd_byte *, bfd_size_type);

extern bfd_boolean write_ieee_debugging_info (bfd *, void *);

/* Routine used to read COFF debugging information.  */

extern bfd_boolean parse_coff (bfd *, asymbol **, long, void *);

#endif
