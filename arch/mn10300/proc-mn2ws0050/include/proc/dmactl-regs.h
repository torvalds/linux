/* MN2WS0050 on-board DMA controller registers
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#ifndef _ASM_PROC_DMACTL_REGS_H
#define _ASM_PROC_DMACTL_REGS_H

#include <asm/cpu-regs.h>

#ifdef __KERNEL__

/* DMA registers */
#define	DMxCTR(N)		__SYSREG(0xd4005000+(N*0x100), u32)	/* control reg */
#define	DMxCTR_BG		0x0000001f	/* transfer request source */
#define	DMxCTR_BG_SOFT		0x00000000	/* - software source */
#define	DMxCTR_BG_SC0TX		0x00000002	/* - serial port 0 transmission */
#define	DMxCTR_BG_SC0RX		0x00000003	/* - serial port 0 reception */
#define	DMxCTR_BG_SC1TX		0x00000004	/* - serial port 1 transmission */
#define	DMxCTR_BG_SC1RX		0x00000005	/* - serial port 1 reception */
#define	DMxCTR_BG_SC2TX		0x00000006	/* - serial port 2 transmission */
#define	DMxCTR_BG_SC2RX		0x00000007	/* - serial port 2 reception */
#define	DMxCTR_BG_TM0UFLOW	0x00000008	/* - timer 0 underflow */
#define	DMxCTR_BG_TM1UFLOW	0x00000009	/* - timer 1 underflow */
#define	DMxCTR_BG_TM2UFLOW	0x0000000a	/* - timer 2 underflow */
#define	DMxCTR_BG_TM3UFLOW	0x0000000b	/* - timer 3 underflow */
#define	DMxCTR_BG_TM6ACMPCAP	0x0000000c	/* - timer 6A compare/capture */
#define	DMxCTR_BG_RYBY		0x0000000d	/* - NAND Flash RY/BY request source */
#define	DMxCTR_BG_RMC		0x0000000e	/* - remote controller output */
#define	DMxCTR_BG_XIRQ12	0x00000011	/* - XIRQ12 pin interrupt source */
#define	DMxCTR_BG_XIRQ13	0x00000012	/* - XIRQ13 pin interrupt source */
#define	DMxCTR_BG_TCK		0x00000014	/* - tick timer underflow */
#define	DMxCTR_BG_SC4TX		0x00000019	/* - serial port4 transmission */
#define	DMxCTR_BG_SC4RX		0x0000001a	/* - serial port4 reception */
#define	DMxCTR_BG_SC5TX		0x0000001b	/* - serial port5 transmission */
#define	DMxCTR_BG_SC5RX		0x0000001c	/* - serial port5 reception */
#define	DMxCTR_BG_SC6TX		0x0000001d	/* - serial port6 transmission */
#define	DMxCTR_BG_SC6RX		0x0000001e	/* - serial port6 reception */
#define	DMxCTR_BG_TMSUFLOW	0x0000001f	/* - timestamp timer underflow */
#define	DMxCTR_SAM		0x00000060	/* DMA transfer src addr mode */
#define	DMxCTR_SAM_INCR		0x00000000	/* - increment */
#define	DMxCTR_SAM_DECR		0x00000020	/* - decrement */
#define	DMxCTR_SAM_FIXED	0x00000040	/* - fixed */
#define	DMxCTR_DAM		0x00000300	/* DMA transfer dest addr mode */
#define	DMxCTR_DAM_INCR		0x00000000	/* - increment */
#define	DMxCTR_DAM_DECR		0x00000100	/* - decrement */
#define	DMxCTR_DAM_FIXED	0x00000200	/* - fixed */
#define	DMxCTR_UT		0x00006000	/* DMA transfer unit */
#define	DMxCTR_UT_1		0x00000000	/* - 1 byte */
#define	DMxCTR_UT_2		0x00002000	/* - 2 byte */
#define	DMxCTR_UT_4		0x00004000	/* - 4 byte */
#define	DMxCTR_UT_16		0x00006000	/* - 16 byte */
#define DMxCTR_RRE		0x00008000	/* DMA round robin enable */
#define	DMxCTR_TEN		0x00010000	/* DMA channel transfer enable */
#define	DMxCTR_RQM		0x00060000	/* external request input source mode */
#define	DMxCTR_RQM_FALLEDGE	0x00000000	/* - falling edge */
#define	DMxCTR_RQM_RISEEDGE	0x00020000	/* - rising edge */
#define	DMxCTR_RQM_LOLEVEL	0x00040000	/* - low level */
#define	DMxCTR_RQM_HILEVEL	0x00060000	/* - high level */
#define	DMxCTR_RQF		0x01000000	/* DMA transfer request flag */
#define	DMxCTR_PERR		0x40000000	/* DMA transfer parameter error flag */
#define	DMxCTR_XEND		0x80000000	/* DMA transfer end flag */

#define	DMxSRC(N)		__SYSREG(0xd4005004+(N*0x100), u32)	/* control reg */

#define	DMxDST(N)		__SYSREG(0xd4005008+(N*0x100), u32)	/* source addr reg */

#define	DMxSIZ(N)		__SYSREG(0xd400500c+(N*0x100), u32)	/* dest addr reg */
#define DMxSIZ_CT		0x000fffff	/* number of bytes to transfer */

#define	DMxCYC(N)		__SYSREG(0xd4005010+(N*0x100), u32)	/* intermittent size reg */
#define DMxCYC_CYC		0x000000ff	/* number of interrmittent transfers -1 */

#define DM0IRQ			16		/* DMA channel 0 complete IRQ */
#define DM1IRQ			17		/* DMA channel 1 complete IRQ */
#define DM2IRQ			18		/* DMA channel 2 complete IRQ */
#define DM3IRQ			19		/* DMA channel 3 complete IRQ */

#define	DM0ICR			GxICR(DM0IRQ)	/* DMA channel 0 complete intr ctrl reg */
#define	DM1ICR			GxICR(DM0IR1)	/* DMA channel 1 complete intr ctrl reg */
#define	DM2ICR			GxICR(DM0IR2)	/* DMA channel 2 complete intr ctrl reg */
#define	DM3ICR			GxICR(DM0IR3)	/* DMA channel 3 complete intr ctrl reg */

#ifndef __ASSEMBLY__

struct mn10300_dmactl_regs {
	u32		ctr;
	const void	*src;
	void		*dst;
	u32		siz;
	u32		cyc;
} __attribute__((aligned(0x100)));

#endif /* __ASSEMBLY__ */

#endif /* __KERNEL__ */

#endif /* _ASM_PROC_DMACTL_REGS_H */
