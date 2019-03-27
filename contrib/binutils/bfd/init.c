/* bfd initialization stuff
   Copyright 1990, 1991, 1992, 1993, 1994, 1995, 2003, 2007
   Free Software Foundation, Inc.
   Written by Steve Chamberlain of Cygnus Support.

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

/*
SECTION
	Initialization

SUBSECTION
	Initialization functions

	These are the functions that handle initializing a BFD.
*/

/*
FUNCTION
	bfd_init

SYNOPSIS
	void bfd_init (void);

DESCRIPTION
	This routine must be called before any other BFD function to
	initialize magical internal data structures.
*/

/* Actually, there is currently nothing for this function to do.
   However, someday it may be needed, so keep it around.  */

void
bfd_init (void)
{
}
