/* BFD backend for local host's a.out binaries
   Copyright 1990, 1991, 1992, 1994, 1995, 2001, 2007
   Free Software Foundation, Inc.
   Written by Cygnus Support.  Probably John Gilmore's fault.

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

#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"

#define	ARCH_SIZE	32

/* When porting to a new system, you must supply:

	HOST_PAGE_SIZE		(optional)
	HOST_SEGMENT_SIZE	(optional -- defaults to page size)
	HOST_MACHINE_ARCH	(optional)
	HOST_MACHINE_MACHINE	(optional)
	HOST_TEXT_START_ADDR	(optional)
	HOST_STACK_END_ADDR	(not used, except by trad-core ???)
	HOST_BIG_ENDIAN_P	(required -- define if big-endian)

   in the ./hosts/h-systemname.h file.  */

#ifdef TRAD_HEADER
#include TRAD_HEADER
#endif

#ifdef			HOST_PAGE_SIZE
#define	TARGET_PAGE_SIZE	HOST_PAGE_SIZE
#endif

#ifdef			HOST_SEGMENT_SIZE
#define	SEGMENT_SIZE	HOST_SEGMENT_SIZE
#else
#define	SEGMENT_SIZE	TARGET_PAGE_SIZE
#endif

#ifdef			HOST_TEXT_START_ADDR
#define	TEXT_START_ADDR	HOST_TEXT_START_ADDR
#endif

#ifdef			HOST_STACK_END_ADDR
#define	STACK_END_ADDR	HOST_STACK_END_ADDR
#endif

#ifdef			HOST_BIG_ENDIAN_P
#define	TARGET_IS_BIG_ENDIAN_P
#else
#undef  TARGET_IS_BIG_ENDIAN_P
#endif

#include "libaout.h"           /* BFD a.out internal data structures */
#include "aout/aout64.h"

#ifdef HOST_MACHINE_ARCH
#ifdef HOST_MACHINE_MACHINE
#define SET_ARCH_MACH(abfd, execp) \
  bfd_default_set_arch_mach(abfd, HOST_MACHINE_ARCH, HOST_MACHINE_MACHINE)
#else
#define SET_ARCH_MACH(abfd, execp) \
  bfd_default_set_arch_mach(abfd, HOST_MACHINE_ARCH, 0)
#endif
#endif /* HOST_MACHINE_ARCH */

/* Do not "beautify" the CONCAT* macro args.  Traditional C will not
   remove whitespace added here, and thus will fail to concatenate
   the tokens.  */
#define MY(OP) CONCAT2 (host_aout_,OP)
#define TARGETNAME "a.out"

#include "aout-target.h"
