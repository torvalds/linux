/*
 * linux/arch/unicore32/include/asm/timex.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __UNICORE_TIMEX_H__
#define __UNICORE_TIMEX_H__

#ifdef	CONFIG_ARCH_FPGA

/* in FPGA, APB clock is 33M, and OST clock is 32K, */
/* so, 1M is selected for timer interrupt correctly */
#define	CLOCK_TICK_RATE		(32*1024)

#endif

#if defined(CONFIG_PUV3_DB0913)		\
	|| defined(CONFIG_PUV3_NB0916)	\
	|| defined(CONFIG_PUV3_SMW0919)

#define  CLOCK_TICK_RATE         (14318000)

#endif

#include <asm-generic/timex.h>

#endif
