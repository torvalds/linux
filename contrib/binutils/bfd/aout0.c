/* BFD backend for SunOS style a.out with flags set to 0
   Copyright 1990, 1991, 1992, 1993, 1994, 1995, 2001
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

#define TARGETNAME "a.out-zero-big"

/* Do not "beautify" the CONCAT* macro args.  Traditional C will not
   remove whitespace added here, and thus will fail to concatenate
   the tokens.  */
#define MY(OP) CONCAT2 (aout0_big_,OP)

#include "bfd.h"

#define MY_exec_hdr_flags 0

#define MACHTYPE_OK(mtype) \
  ((mtype) == M_UNKNOWN || (mtype) == M_68010 || (mtype) == M_68020)

/* Include the usual a.out support.  */
#include "aoutf1.h"
