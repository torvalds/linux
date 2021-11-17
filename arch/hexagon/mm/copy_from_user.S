/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * User memory copy functions for kernel
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

/*
 * The right way to do this involves valignb
 * The easy way to do this is only speed up src/dest similar alignment.
 */

/*
 * Copy to/from user are the same, except that for packets with a load and
 * a store, I don't know how to tell which kind of exception we got.
 * Therefore, we duplicate the function, and handle faulting addresses
 * differently for each function
 */

/*
 * copy from user: loads can fault
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

#define FUNCNAME raw_copy_from_user
#include "copy_user_template.S"

	/* LOAD FAULTS from COPY_FROM_USER */

	/* Alignment loop.  r2 has been updated. Return it. */
	.falign
1009:
2009:
4009:
	{
		r0 = r2
		jumpr r31
	}
	/* Normal copy loops. Do epilog. Use src-src_sav to compute distance */
	/* X - (A - B) == X + B - A */
	.falign
8089:
	{
		memd(dst) = d_dbuf
		r2 += sub(src_sav,src)
	}
	{
		r0 = r2
		jumpr r31
	}
	.falign
4089:
	{
		memw(dst) = w_dbuf
		r2 += sub(src_sav,src)
	}
	{
		r0 = r2
		jumpr r31
	}
	.falign
2089:
	{
		memh(dst) = w_dbuf
		r2 += sub(src_sav,src)
	}
	{
		r0 = r2
		jumpr r31
	}
	.falign
1089:
	{
		memb(dst) = w_dbuf
		r2 += sub(src_sav,src)
	}
	{
		r0 = r2
		jumpr r31
	}

	/* COPY FROM USER: only loads can fail */

	.section __ex_table,"a"
	.long 1000b,1009b
	.long 2000b,2009b
	.long 4000b,4009b
	.long 8080b,8089b
	.long 4080b,4089b
	.long 2080b,2089b
	.long 1080b,1089b
	.previous
