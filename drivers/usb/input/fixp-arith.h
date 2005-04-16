#ifndef _FIXP_ARITH_H
#define _FIXP_ARITH_H

/*
 * $$
 *
 * Simplistic fixed-point arithmetics.
 * Hmm, I'm probably duplicating some code :(
 *
 * Copyright (c) 2002 Johann Deneux
 */

/*
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Should you need to contact me, the author, you can do so by
 * e-mail - mail your message to <deneux@ifrance.com>
 */

#include <linux/types.h>

// The type representing fixed-point values
typedef s16 fixp_t;

#define FRAC_N 8
#define FRAC_MASK ((1<<FRAC_N)-1)

// Not to be used directly. Use fixp_{cos,sin}
static fixp_t cos_table[45] = {
	0x0100,	0x00FF,	0x00FF,	0x00FE,	0x00FD,	0x00FC,	0x00FA,	0x00F8,
	0x00F6,	0x00F3,	0x00F0,	0x00ED,	0x00E9,	0x00E6,	0x00E2,	0x00DD,
	0x00D9,	0x00D4,	0x00CF,	0x00C9,	0x00C4,	0x00BE,	0x00B8,	0x00B1,
	0x00AB,	0x00A4,	0x009D,	0x0096,	0x008F,	0x0087,	0x0080,	0x0078,
	0x0070,	0x0068,	0x005F,	0x0057,	0x004F,	0x0046,	0x003D,	0x0035,
	0x002C,	0x0023,	0x001A,	0x0011,	0x0008
};


/* a: 123 -> 123.0 */
static inline fixp_t fixp_new(s16 a)
{
	return a<<FRAC_N;
}

/* a: 0xFFFF -> -1.0
      0x8000 -> 1.0
      0x0000 -> 0.0
*/
static inline fixp_t fixp_new16(s16 a)
{
	return ((s32)a)>>(16-FRAC_N);
}

static inline fixp_t fixp_cos(unsigned int degrees)
{
	int quadrant = (degrees / 90) & 3;
	unsigned int i = degrees % 90;

	if (quadrant == 1 || quadrant == 3) {
		i = 89 - i;
	}

	i >>= 1;

	return (quadrant == 1 || quadrant == 2)? -cos_table[i] : cos_table[i];
}

static inline fixp_t fixp_sin(unsigned int degrees)
{
	return -fixp_cos(degrees + 90);
}

static inline fixp_t fixp_mult(fixp_t a, fixp_t b)
{
	return ((s32)(a*b))>>FRAC_N;
}

#endif
