/* unwind-ia64.h -- dump IA-64 unwind info.
   Copyright 2000, 2001, 2002, 2003 Free Software Foundation, Inc.
	Contributed by David Mosberger-Tang <davidm@hpl.hp.com>

This file is part of GNU Binutils.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

#include "elf/ia64.h"
#include "ansidecl.h"

#define UNW_VER(x)		((x) >> 48)
#define UNW_FLAG_MASK		0x0000ffff00000000LL
#define UNW_FLAG_OSMASK		0x0000f00000000000LL
#define UNW_FLAG_EHANDLER(x)	((x) & 0x0000000100000000LL)
#define UNW_FLAG_UHANDLER(x)	((x) & 0x0000000200000000LL)
#define UNW_LENGTH(x)		((x) & 0x00000000ffffffffLL)

extern const unsigned char *unw_decode (const unsigned char *, int, void *);
