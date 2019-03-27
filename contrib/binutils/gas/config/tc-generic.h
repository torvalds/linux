/* This file is tc-generic.h

   Copyright 1987, 1991, 1992, 1995, 1997 Free Software Foundation, Inc.

   This file is part of GAS, the GNU Assembler.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GAS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with GAS; see the file COPYING.  If not, write to the Free Software
   Foundation, 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

/*
 * This file is tc-generic.h and is intended to be a template for target cpu
 * specific header files.  It is my intent that this file compile.  It is also
 * my intent that this file grow into something that can be used as both a
 * template for porting, and a stub for testing.  xoxorich.
 */

#define TC_GENERIC 1

#define TARGET_BYTES_BIG_ENDIAN 0

/*
 * Local Variables:
 * comment-column: 0
 * fill-column: 131
 * End:
 */

/* end of tc-generic.h */
