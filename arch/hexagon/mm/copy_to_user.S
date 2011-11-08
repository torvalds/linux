/*
 * User memory copying routines for the Hexagon Kernel
 *
 * Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/* The right way to do this involves valignb
 * The easy way to do this is only speed up src/dest similar alignment.
 */

/*
 * Copy to/from user are the same, except that for packets with a load and
 * a store, I don't know how to tell which kind of exception we got.
 * Therefore, we duplicate the function, and handle faulting addresses
 * differently for each function
 */

/*
 * copy to user: stores can fault
 */
#define src_sav r13
#define dst_sav r12
#define src_dst_sav r13:12
#define d_dbuf r15:14
#define w_dbuf r15

#define dst r0
#define src r1
#define bytes r2
#define loopcount r5

#define FUNCNAME __copy_to_user_hexagon
#include "copy_user_template.S"

	/* STORE FAULTS from COPY_TO_USER */
	.falign
1109:
2109:
4109:
	/* Alignment loop.  r2 has been updated.  Return it. */
	{
		r0 = r2
		jumpr r31
	}
	/* Normal copy loops.  Use dst-dst_sav to compute distance */
	/* dst holds best write, no need to unwind any loops */
	/* X - (A - B) == X + B - A */
	.falign
8189:
8199:
4189:
4199:
2189:
2199:
1189:
1199:
	{
		r2 += sub(dst_sav,dst)
	}
	{
		r0 = r2
		jumpr r31
	}

	/* COPY TO USER: only stores can fail */
	.section __ex_table,"a"
	.long 1100b,1109b
	.long 2100b,2109b
	.long 4100b,4109b
	.long 8180b,8189b
	.long 8190b,8199b
	.long 4180b,4189b
	.long 4190b,4199b
	.long 2180b,2189b
	.long 2190b,2199b
	.long 1180b,1189b
	.long 1190b,1199b
	.previous
