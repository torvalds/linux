/*
 * Copyright 2004-2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef _ASM_BLACKFIN_SIGCONTEXT_H
#define _ASM_BLACKFIN_SIGCONTEXT_H

/* Add new entries at the end of the structure only.  */
struct sigcontext {
	unsigned long sc_r0;
	unsigned long sc_r1;
	unsigned long sc_r2;
	unsigned long sc_r3;
	unsigned long sc_r4;
	unsigned long sc_r5;
	unsigned long sc_r6;
	unsigned long sc_r7;
	unsigned long sc_p0;
	unsigned long sc_p1;
	unsigned long sc_p2;
	unsigned long sc_p3;
	unsigned long sc_p4;
	unsigned long sc_p5;
	unsigned long sc_usp;
	unsigned long sc_a0w;
	unsigned long sc_a1w;
	unsigned long sc_a0x;
	unsigned long sc_a1x;
	unsigned long sc_astat;
	unsigned long sc_rets;
	unsigned long sc_pc;
	unsigned long sc_retx;
	unsigned long sc_fp;
	unsigned long sc_i0;
	unsigned long sc_i1;
	unsigned long sc_i2;
	unsigned long sc_i3;
	unsigned long sc_m0;
	unsigned long sc_m1;
	unsigned long sc_m2;
	unsigned long sc_m3;
	unsigned long sc_l0;
	unsigned long sc_l1;
	unsigned long sc_l2;
	unsigned long sc_l3;
	unsigned long sc_b0;
	unsigned long sc_b1;
	unsigned long sc_b2;
	unsigned long sc_b3;
	unsigned long sc_lc0;
	unsigned long sc_lc1;
	unsigned long sc_lt0;
	unsigned long sc_lt1;
	unsigned long sc_lb0;
	unsigned long sc_lb1;
	unsigned long sc_seqstat;
};

#endif
