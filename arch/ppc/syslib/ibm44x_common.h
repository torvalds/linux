/*
 * arch/ppc/kernel/ibm44x_common.h
 *
 * PPC44x system library
 *
 * Eugene Surovegin <eugene.surovegin@zultys.com> or <ebs@ebshome.net>
 * Copyright (c) 2003, 2004 Zultys Technologies
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#ifdef __KERNEL__
#ifndef __PPC_SYSLIB_IBM44x_COMMON_H
#define __PPC_SYSLIB_IBM44x_COMMON_H

#ifndef __ASSEMBLY__

/*
 * All clocks are in Hz
 */
struct ibm44x_clocks {
	unsigned int vco;	/* VCO, 0 if system PLL is bypassed */
	unsigned int cpu;	/* CPUCoreClk */
	unsigned int plb;	/* PLBClk */
	unsigned int opb;	/* OPBClk */
	unsigned int ebc;	/* PerClk */
	unsigned int uart0;
	unsigned int uart1;
#ifdef CONFIG_440EP
	unsigned int uart2;
	unsigned int uart3;
#endif
};

/* common 44x platform init */
void ibm44x_platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
			  unsigned long r6, unsigned long r7) __init;

/* initialize decrementer and tick-related variables */
void ibm44x_calibrate_decr(unsigned int freq) __init;

#endif /* __ASSEMBLY__ */
#endif /* __PPC_SYSLIB_IBM44x_COMMON_H */
#endif /* __KERNEL__ */
