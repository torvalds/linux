/*
 * File:         arch/blackfin/include/asm/pda.h
 * Author:       Philippe Gerum <rpm@xenomai.org>
 *
 *               Copyright 2007 Analog Devices Inc.
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

#ifndef _ASM_BLACKFIN_PDA_H
#define _ASM_BLACKFIN_PDA_H

#include <mach/anomaly.h>

#ifndef __ASSEMBLY__

struct blackfin_pda {			/* Per-processor Data Area */
	struct blackfin_pda *next;

	unsigned long syscfg;
#ifdef CONFIG_SMP
	unsigned long imask;		/* Current IMASK value */
#endif

	unsigned long *ipdt;		/* Start of switchable I-CPLB table */
	unsigned long *ipdt_swapcount;	/* Number of swaps in ipdt */
	unsigned long *dpdt;		/* Start of switchable D-CPLB table */
	unsigned long *dpdt_swapcount;	/* Number of swaps in dpdt */

	/*
	 * Single instructions can have multiple faults, which
	 * need to be handled by traps.c, in irq5. We store
	 * the exception cause to ensure we don't miss a
	 * double fault condition
	 */
	unsigned long ex_iptr;
	unsigned long ex_optr;
	unsigned long ex_buf[4];
	unsigned long ex_imask;		/* Saved imask from exception */
	unsigned long *ex_stack;	/* Exception stack space */

#ifdef ANOMALY_05000261
	unsigned long last_cplb_fault_retx;
#endif
	unsigned long dcplb_fault_addr;
	unsigned long icplb_fault_addr;
	unsigned long retx;
	unsigned long seqstat;
	unsigned int __nmi_count;	/* number of times NMI asserted on this CPU */
};

extern struct blackfin_pda cpu_pda[];

void reserve_pda(void);

#endif	/* __ASSEMBLY__ */

#endif /* _ASM_BLACKFIN_PDA_H */
