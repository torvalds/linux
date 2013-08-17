/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997 Silicon Graphics, Inc.
 */
#ifndef __ASM_SN_INTR_H
#define __ASM_SN_INTR_H

/* Number of interrupt levels associated with each interrupt register. */
#define N_INTPEND_BITS		64

#define INT_PEND0_BASELVL	0
#define INT_PEND1_BASELVL	64

#define	N_INTPENDJUNK_BITS	8
#define	INTPENDJUNK_CLRBIT	0x80

/*
 * Macros to manipulate the interrupt register on the calling hub chip.
 */

#define LOCAL_HUB_SEND_INTR(level)				\
	LOCAL_HUB_S(PI_INT_PEND_MOD, (0x100 | (level)))
#define REMOTE_HUB_SEND_INTR(hub, level)			\
	REMOTE_HUB_S((hub), PI_INT_PEND_MOD, (0x100 | (level)))

/*
 * When clearing the interrupt, make sure this clear does make it
 * to the hub. Otherwise we could end up losing interrupts.
 * We do an uncached load of the int_pend0 register to ensure this.
 */

#define LOCAL_HUB_CLR_INTR(level)	  			\
do {								\
	LOCAL_HUB_S(PI_INT_PEND_MOD, (level));			\
	LOCAL_HUB_L(PI_INT_PEND0);				\
} while (0);

#define REMOTE_HUB_CLR_INTR(hub, level)				\
do {								\
	nasid_t  __hub = (hub);					\
								\
	REMOTE_HUB_S(__hub, PI_INT_PEND_MOD, (level));		\
	REMOTE_HUB_L(__hub, PI_INT_PEND0);			\
} while (0);

/*
 * Hard-coded interrupt levels:
 */

/*
 *	L0 = SW1
 *	L1 = SW2
 *	L2 = INT_PEND0
 *	L3 = INT_PEND1
 *	L4 = RTC
 *	L5 = Profiling Timer
 *	L6 = Hub Errors
 *	L7 = Count/Compare (T5 counters)
 */


/*
 * INT_PEND0 hard-coded bits.
 */

/*
 * INT_PEND0 bits determined by hardware:
 */
#define RESERVED_INTR		 0	/* What is this bit? */
#define GFX_INTR_A		 1
#define GFX_INTR_B		 2
#define PG_MIG_INTR		 3
#define UART_INTR		 4
#define CC_PEND_A		 5
#define CC_PEND_B		 6

/*
 * INT_PEND0 used by the kernel for itself ...
 */
#define CPU_RESCHED_A_IRQ	 7
#define CPU_RESCHED_B_IRQ	 8
#define CPU_CALL_A_IRQ		 9
#define CPU_CALL_B_IRQ		10
#define MSC_MESG_INTR		11
#define BASE_PCI_IRQ		12

/*
 * INT_PEND0 again, bits determined by hardware / hardcoded:
 */
#define SDISK_INTR		63	/* SABLE name */
#define IP_PEND0_6_63		63	/* What is this bit? */

/*
 * INT_PEND1 hard-coded bits:
 */
#define NI_BRDCAST_ERR_A	39
#define NI_BRDCAST_ERR_B	40

#define LLP_PFAIL_INTR_A	41	/* see ml/SN/SN0/sysctlr.c */
#define LLP_PFAIL_INTR_B	42

#define	TLB_INTR_A		43	/* used for tlb flush random */
#define	TLB_INTR_B		44

#define IP27_INTR_0		45	/* Reserved for PROM use */
#define IP27_INTR_1		46	/* do not use in Kernel */
#define IP27_INTR_2		47
#define IP27_INTR_3		48
#define IP27_INTR_4		49
#define IP27_INTR_5		50
#define IP27_INTR_6		51
#define IP27_INTR_7		52

#define BRIDGE_ERROR_INTR	53	/* Setup by PROM to catch	*/
					/* Bridge Errors */
#define	DEBUG_INTR_A		54
#define	DEBUG_INTR_B		55	/* Used by symmon to stop all cpus */
#define IO_ERROR_INTR		57	/* Setup by PROM */
#define CLK_ERR_INTR		58
#define COR_ERR_INTR_A		59
#define COR_ERR_INTR_B		60
#define MD_COR_ERR_INTR		61
#define NI_ERROR_INTR		62
#define MSC_PANIC_INTR		63

#endif /* __ASM_SN_INTR_H */
