/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Port on Texas Instruments TMS320C6x architecture
 *
 *  Copyright (C) 2004, 2009, 2010, 2011 Texas Instruments Incorporated
 *  Author: Aurelien Jacquiot (aurelien.jacquiot@jaluna.com)
 */
#ifndef _ASM_C6X_DELAY_H
#define _ASM_C6X_DELAY_H

#include <linux/kernel.h>

extern unsigned int ticks_per_ns_scaled;

static inline void __delay(unsigned long loops)
{
	uint32_t tmp;

	/* 6 cycles per loop */
	asm volatile ("        mv    .s1  %0,%1\n"
		      "0: [%1] b     .s1  0b\n"
		      "        add   .l1  -6,%0,%0\n"
		      "        cmplt .l1  1,%0,%1\n"
		      "        nop   3\n"
		      : "+a"(loops), "=A"(tmp));
}

static inline void _c6x_tickdelay(unsigned int x)
{
	uint32_t cnt, endcnt;

	asm volatile ("        mvc   .s2   TSCL,%0\n"
		      "        add   .s2x  %0,%1,%2\n"
		      " ||     mvk   .l2   1,B0\n"
		      "0: [B0] b     .s2   0b\n"
		      "        mvc   .s2   TSCL,%0\n"
		      "        sub   .s2   %0,%2,%0\n"
		      "        cmpgt .l2   0,%0,B0\n"
		      "        nop   2\n"
		      : "=b"(cnt), "+a"(x), "=b"(endcnt) : : "B0");
}

/* use scaled math to avoid slow division */
#define C6X_NDELAY_SCALE 10

static inline void _ndelay(unsigned int n)
{
	_c6x_tickdelay((ticks_per_ns_scaled * n) >> C6X_NDELAY_SCALE);
}

static inline void _udelay(unsigned int n)
{
	while (n >= 10) {
		_ndelay(10000);
		n -= 10;
	}
	while (n-- > 0)
		_ndelay(1000);
}

#define udelay(x) _udelay((unsigned int)(x))
#define ndelay(x) _ndelay((unsigned int)(x))

#endif /* _ASM_C6X_DELAY_H */
