/* lshrdi3.c extracted from gcc-2.7.2/libgcc2.c which is: */
/* Copyright (C) 1989, 1992, 1993, 1994, 1995 Free Software Foundation, Inc.

This file is part of GNU CC.

GNU CC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public Licence as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GNU CC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public Licence for more details.

You should have received a copy of the GNU General Public Licence
along with GNU CC; see the file COPYING.  If not, write to
the Free Software Foundation, 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  */

#define BITS_PER_UNIT 8

typedef 	 int SItype	__attribute__((mode(SI)));
typedef unsigned int USItype	__attribute__((mode(SI)));
typedef		 int DItype	__attribute__((mode(DI)));
typedef		 int word_type	__attribute__((mode(__word__)));

struct DIstruct {
	SItype	low;
	SItype	high;
};

union DIunion {
	struct DIstruct	s;
	DItype		ll;
};

DItype __lshrdi3(DItype u, word_type b)
{
	union DIunion w;
	word_type bm;
	union DIunion uu;

	if (b == 0)
		return u;

	uu.ll = u;

	bm = (sizeof(SItype) * BITS_PER_UNIT) - b;
	if (bm <= 0) {
		w.s.high = 0;
		w.s.low = (USItype) uu.s.high >> -bm;
	} else {
		USItype carries = (USItype) uu.s.high << bm;
		w.s.high = (USItype) uu.s.high >> b;
		w.s.low = ((USItype) uu.s.low >> b) | carries;
	}

	return w.ll;
}
