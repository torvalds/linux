/* More subroutines needed by GCC output code on some machines.  */
/* Compile this one with gcc.  */
/* Copyright (C) 1989, 92-98, 1999 Free Software Foundation, Inc.

This file is part of GNU CC.

GNU CC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GNU CC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU CC; see the file COPYING.  If not, write to
the Free Software Foundation, 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  */

/* As a special exception, if you link this library with other files,
   some of which are compiled with GCC, to produce an executable,
   this library does not by itself cause the resulting executable
   to be covered by the GNU General Public License.
   This exception does not however invalidate any other reasons why
   the executable file might be covered by the GNU General Public License.
 */
/* support functions required by the kernel. based on code from gcc-2.95.3 */
/* I Molton	29/07/01 */

#include "gcclib.h"

s64 __ashldi3(s64 u, int b)
{
	DIunion w;
	int bm;
	DIunion uu;

	if (b == 0)
		return u;

	uu.ll = u;

	bm = (sizeof(s32) * BITS_PER_UNIT) - b;
	if (bm <= 0) {
		w.s.low = 0;
		w.s.high = (u32) uu.s.low << -bm;
	} else {
		u32 carries = (u32) uu.s.low >> bm;
		w.s.low = (u32) uu.s.low << b;
		w.s.high = ((u32) uu.s.high << b) | carries;
	}

	return w.ll;
}
