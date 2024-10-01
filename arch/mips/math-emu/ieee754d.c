// SPDX-License-Identifier: GPL-2.0-only
/*
 * Some debug functions
 *
 * MIPS floating point support
 *
 * Copyright (C) 1994-2000 Algorithmics Ltd.
 *
 *  Nov 7, 2000
 *  Modified to build and operate in Linux kernel environment.
 *
 *  Kevin D. Kissell, kevink@mips.com and Carsten Langgaard, carstenl@mips.com
 *  Copyright (C) 2000 MIPS Technologies, Inc. All rights reserved.
 */

#include <linux/types.h>
#include <linux/printk.h>
#include "ieee754.h"
#include "ieee754sp.h"
#include "ieee754dp.h"

union ieee754dp ieee754dp_dump(char *m, union ieee754dp x)
{
	int i;

	printk("%s", m);
	printk("<%08x,%08x>\n", (unsigned) (x.bits >> 32),
	       (unsigned) x.bits);
	printk("\t=");
	switch (ieee754dp_class(x)) {
	case IEEE754_CLASS_QNAN:
	case IEEE754_CLASS_SNAN:
		printk("Nan %c", DPSIGN(x) ? '-' : '+');
		for (i = DP_FBITS - 1; i >= 0; i--)
			printk("%c", DPMANT(x) & DP_MBIT(i) ? '1' : '0');
		break;
	case IEEE754_CLASS_INF:
		printk("%cInfinity", DPSIGN(x) ? '-' : '+');
		break;
	case IEEE754_CLASS_ZERO:
		printk("%cZero", DPSIGN(x) ? '-' : '+');
		break;
	case IEEE754_CLASS_DNORM:
		printk("%c0.", DPSIGN(x) ? '-' : '+');
		for (i = DP_FBITS - 1; i >= 0; i--)
			printk("%c", DPMANT(x) & DP_MBIT(i) ? '1' : '0');
		printk("e%d", DPBEXP(x) - DP_EBIAS);
		break;
	case IEEE754_CLASS_NORM:
		printk("%c1.", DPSIGN(x) ? '-' : '+');
		for (i = DP_FBITS - 1; i >= 0; i--)
			printk("%c", DPMANT(x) & DP_MBIT(i) ? '1' : '0');
		printk("e%d", DPBEXP(x) - DP_EBIAS);
		break;
	default:
		printk("Illegal/Unknown IEEE754 value class");
	}
	printk("\n");
	return x;
}

union ieee754sp ieee754sp_dump(char *m, union ieee754sp x)
{
	int i;

	printk("%s=", m);
	printk("<%08x>\n", (unsigned) x.bits);
	printk("\t=");
	switch (ieee754sp_class(x)) {
	case IEEE754_CLASS_QNAN:
	case IEEE754_CLASS_SNAN:
		printk("Nan %c", SPSIGN(x) ? '-' : '+');
		for (i = SP_FBITS - 1; i >= 0; i--)
			printk("%c", SPMANT(x) & SP_MBIT(i) ? '1' : '0');
		break;
	case IEEE754_CLASS_INF:
		printk("%cInfinity", SPSIGN(x) ? '-' : '+');
		break;
	case IEEE754_CLASS_ZERO:
		printk("%cZero", SPSIGN(x) ? '-' : '+');
		break;
	case IEEE754_CLASS_DNORM:
		printk("%c0.", SPSIGN(x) ? '-' : '+');
		for (i = SP_FBITS - 1; i >= 0; i--)
			printk("%c", SPMANT(x) & SP_MBIT(i) ? '1' : '0');
		printk("e%d", SPBEXP(x) - SP_EBIAS);
		break;
	case IEEE754_CLASS_NORM:
		printk("%c1.", SPSIGN(x) ? '-' : '+');
		for (i = SP_FBITS - 1; i >= 0; i--)
			printk("%c", SPMANT(x) & SP_MBIT(i) ? '1' : '0');
		printk("e%d", SPBEXP(x) - SP_EBIAS);
		break;
	default:
		printk("Illegal/Unknown IEEE754 value class");
	}
	printk("\n");
	return x;
}
