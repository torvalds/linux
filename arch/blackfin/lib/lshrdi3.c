/*
 * File:         arch/blackfin/lib/lshrdi3.c
 * Based on:
 * Author:
 *
 * Created:
 * Description:
 *
 * Modified:
 *               Copyright 2004-2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#define BITS_PER_UNIT 8

typedef int SItype __attribute__ ((mode(SI)));
typedef unsigned int USItype __attribute__ ((mode(SI)));
typedef int DItype __attribute__ ((mode(DI)));
typedef int word_type __attribute__ ((mode(__word__)));

struct DIstruct {
	SItype high, low;
};

typedef union {
	struct DIstruct s;
	DItype ll;
} DIunion;

#ifdef CONFIG_ARITHMETIC_OPS_L1
DItype __lshrdi3(DItype u, word_type b)__attribute__((l1_text));
#endif

DItype __lshrdi3(DItype u, word_type b)
{
	DIunion w;
	word_type bm;
	DIunion uu;

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
