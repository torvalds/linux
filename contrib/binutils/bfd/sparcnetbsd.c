/* BFD back-end for NetBSD/sparc a.out-ish binaries.
   Copyright 1990, 1991, 1992, 1994, 1995, 1997, 1998, 2000, 2001, 2003
   Free Software Foundation, Inc.

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

#define TARGET_IS_BIG_ENDIAN_P

/* SPARC chips use either 4K or 8K pages, but object files always
   assume 8K page alignment so they will work on either one.  */
#define TARGET_PAGE_SIZE 0x2000

#define DEFAULT_ARCH	bfd_arch_sparc
#define DEFAULT_MID 	M_SPARC_NETBSD

/* Do not "beautify" the CONCAT* macro args.  Traditional C will not
   remove whitespace added here, and thus will fail to concatenate
   the tokens.  */
#define MY(OP) CONCAT2 (sparcnetbsd_,OP)

/* This needs to start with a.out so GDB knows it is an a.out variant.  */
#define TARGETNAME "a.out-sparc-netbsd"

#include "netbsd.h"
