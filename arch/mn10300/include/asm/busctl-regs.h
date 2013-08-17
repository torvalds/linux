/* AM33v2 on-board bus controller registers
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#ifndef _ASM_BUSCTL_REGS_H
#define _ASM_BUSCTL_REGS_H

#include <asm/cpu-regs.h>

#ifdef __KERNEL__

/* bus controller registers */
#define BCCR			__SYSREG(0xc0002000, u32)	/* bus controller control reg */
#define BCCR_B0AD		0x00000003	/* block 0 (80000000-83ffffff) bus allocation */
#define BCCR_B1AD		0x0000000c	/* block 1 (84000000-87ffffff) bus allocation */
#define BCCR_B2AD		0x00000030	/* block 2 (88000000-8bffffff) bus allocation */
#define BCCR_B3AD		0x000000c0	/* block 3 (8c000000-8fffffff) bus allocation */
#define BCCR_B4AD		0x00000300	/* block 4 (90000000-93ffffff) bus allocation */
#define BCCR_B5AD		0x00000c00	/* block 5 (94000000-97ffffff) bus allocation */
#define BCCR_B6AD		0x00003000	/* block 6 (98000000-9bffffff) bus allocation */
#define BCCR_B7AD		0x0000c000	/* block 7 (9c000000-9fffffff) bus allocation */
#define BCCR_BxAD_EXBUS		0x0		/* - direct to system bus controller */
#define BCCR_BxAD_OPEXBUS	0x1		/* - direct to memory bus controller */
#define BCCR_BxAD_OCMBUS	0x2		/* - direct to on chip memory */
#define BCCR_API		0x00070000	/* bus arbitration priority */
#define BCCR_API_DMACICD	0x00000000	/* - DMA > CI > CD */
#define BCCR_API_DMACDCI	0x00010000	/* - DMA > CD > CI */
#define BCCR_API_CICDDMA	0x00020000	/* - CI > CD > DMA */
#define BCCR_API_CDCIDMA	0x00030000	/* - CD > CI > DMA */
#define BCCR_API_ROUNDROBIN	0x00040000	/* - round robin */
#define BCCR_BEPRI_DMACICD	0x00c00000	/* bus error address priority */
#define BCCR_BEPRI_DMACDCI	0x00000000	/* - DMA > CI > CD */
#define BCCR_BEPRI_CICDDMA	0x00400000	/* - DMA > CD > CI */
#define BCCR_BEPRI_CDCIDMA	0x00800000	/* - CI > CD > DMA */
#define BCCR_BEPRI		0x00c00000	/* - CD > CI > DMA */
#define BCCR_TMON		0x03000000	/* timeout value settings */
#define BCCR_TMON_16IOCLK	0x00000000	/* - 16 IOCLK cycles */
#define BCCR_TMON_256IOCLK	0x01000000	/* - 256 IOCLK cycles */
#define BCCR_TMON_4096IOCLK	0x02000000	/* - 4096 IOCLK cycles */
#define BCCR_TMON_65536IOCLK	0x03000000	/* - 65536 IOCLK cycles */
#define BCCR_TMOE		0x10000000	/* timeout detection enable */

#define BCBERR			__SYSREG(0xc0002010, u32)	/* bus error source reg */
#define BCBERR_BESB		0x0000001f	/* erroneous access destination space */
#define BCBERR_BESB_MON		0x00000001	/* - monitor space */
#define BCBERR_BESB_IO		0x00000002	/* - IO bus */
#define BCBERR_BESB_EX		0x00000004	/* - EX bus */
#define BCBERR_BESB_OPEX	0x00000008	/* - OpEX bus */
#define BCBERR_BESB_OCM		0x00000010	/* - on chip memory */
#define BCBERR_BERW		0x00000100	/* type of access */
#define BCBERR_BERW_WRITE	0x00000000	/* - write */
#define BCBERR_BERW_READ	0x00000100	/* - read */
#define BCBERR_BESD		0x00000200	/* error detector */
#define BCBERR_BESD_BCU		0x00000000	/* - BCU detected error */
#define BCBERR_BESD_SLAVE_BUS	0x00000200	/* - slave bus detected error */
#define BCBERR_BEBST		0x00000400	/* type of access */
#define BCBERR_BEBST_SINGLE	0x00000000	/* - single */
#define BCBERR_BEBST_BURST	0x00000400	/* - burst */
#define BCBERR_BEME		0x00000800	/* multiple bus error flag */
#define BCBERR_BEMR		0x00007000	/* master bus that caused the error */
#define BCBERR_BEMR_NOERROR	0x00000000	/* - no error */
#define BCBERR_BEMR_CI		0x00001000	/* - CPU instruction fetch bus caused error */
#define BCBERR_BEMR_CD		0x00002000	/* - CPU data bus caused error */
#define BCBERR_BEMR_DMA		0x00004000	/* - DMA bus caused error */

#define BCBEAR			__SYSREGC(0xc0002020, u32)	/* bus error address reg */

/* system bus controller registers */
#define SBBASE(X)		__SYSREG(0xd8c00100 + (X) * 0x10, u32)	/* SBC base addr regs */
#define SBBASE_BE		0x00000001	/* bank enable */
#define SBBASE_BAM		0x0000fffe	/* bank address mask [31:17] */
#define SBBASE_BBA		0xfffe0000	/* bank base address [31:17] */

#define SBCNTRL0(X)		__SYSREG(0xd8c00200 + (X) * 0x10, u32)	/* SBC bank ctrl0 regs */
#define SBCNTRL0_WEH		0x00000f00	/* write enable hold */
#define SBCNTRL0_REH		0x0000f000	/* read enable hold */
#define SBCNTRL0_RWH		0x000f0000	/* SRW signal hold */
#define SBCNTRL0_CSH		0x00f00000	/* chip select hold */
#define SBCNTRL0_DAH		0x0f000000	/* data hold */
#define SBCNTRL0_ADH		0xf0000000	/* address hold */

#define SBCNTRL1(X)		__SYSREG(0xd8c00204 + (X) * 0x10, u32)	/* SBC bank ctrl1 regs */
#define SBCNTRL1_WED		0x00000f00	/* write enable delay */
#define SBCNTRL1_RED		0x0000f000	/* read enable delay */
#define SBCNTRL1_RWD		0x000f0000	/* SRW signal delay */
#define SBCNTRL1_ASW		0x00f00000	/* address strobe width */
#define SBCNTRL1_CSD		0x0f000000	/* chip select delay */
#define SBCNTRL1_ASD		0xf0000000	/* address strobe delay */

#define SBCNTRL2(X)		__SYSREG(0xd8c00208 + (X) * 0x10, u32)	/* SBC bank ctrl2 regs */
#define SBCNTRL2_WC		0x000000ff	/* wait count */
#define SBCNTRL2_BWC		0x00000f00	/* burst wait count */
#define SBCNTRL2_WM		0x01000000	/* wait mode setting */
#define SBCNTRL2_WM_FIXEDWAIT	0x00000000	/* - fixed wait access */
#define SBCNTRL2_WM_HANDSHAKE	0x01000000	/* - handshake access */
#define SBCNTRL2_BM		0x02000000	/* bus synchronisation mode */
#define SBCNTRL2_BM_SYNC	0x00000000	/* - synchronous mode */
#define SBCNTRL2_BM_ASYNC	0x02000000	/* - asynchronous mode */
#define SBCNTRL2_BW		0x04000000	/* bus width */
#define SBCNTRL2_BW_32		0x00000000	/* - 32 bits */
#define SBCNTRL2_BW_16		0x04000000	/* - 16 bits */
#define SBCNTRL2_RWINV		0x08000000	/* R/W signal invert polarity */
#define SBCNTRL2_RWINV_NORM	0x00000000	/* - normal (read high) */
#define SBCNTRL2_RWINV_INV	0x08000000	/* - inverted (read low) */
#define SBCNTRL2_BT		0x70000000	/* bus type setting */
#define SBCNTRL2_BT_SRAM	0x00000000	/* - SRAM interface */
#define SBCNTRL2_BT_ADMUX	0x00000000	/* - addr/data multiplexed interface */
#define SBCNTRL2_BT_BROM	0x00000000	/* - burst ROM interface */
#define SBCNTRL2_BTSE		0x80000000	/* burst enable */

/* memory bus controller */
#define SDBASE(X)		__SYSREG(0xda000008 + (X) * 0x4, u32)	/* MBC base addr regs */
#define SDBASE_CE		0x00000001	/* chip enable */
#define SDBASE_CBAM		0x0000fff0	/* chip base address mask [31:20] */
#define SDBASE_CBAM_SHIFT	16
#define SDBASE_CBA		0xfff00000	/* chip base address [31:20] */

#define SDRAMBUS		__SYSREG(0xda000000, u32)	/* bus mode control reg */
#define SDRAMBUS_REFEN		0x00000004	/* refresh enable */
#define SDRAMBUS_TRC		0x00000018	/* refresh command delay time */
#define SDRAMBUS_BSTPT		0x00000020	/* burst stop command enable */
#define SDRAMBUS_PONSEQ		0x00000040	/* power on sequence */
#define SDRAMBUS_SELFREQ	0x00000080	/* self-refresh mode request */
#define SDRAMBUS_SELFON		0x00000100	/* self-refresh mode on */
#define SDRAMBUS_SIZE		0x00030000	/* SDRAM size */
#define SDRAMBUS_SIZE_64Mbit	0x00010000	/* 64Mbit SDRAM (x16) */
#define SDRAMBUS_SIZE_128Mbit	0x00020000	/* 128Mbit SDRAM (x16) */
#define SDRAMBUS_SIZE_256Mbit	0x00030000	/* 256Mbit SDRAM (x16) */
#define SDRAMBUS_TRASWAIT	0x000c0000	/* row address precharge command cycle number */
#define SDRAMBUS_REFNUM		0x00300000	/* refresh command number */
#define SDRAMBUS_BSTWAIT	0x00c00000	/* burst stop command cycle */
#define SDRAMBUS_SETWAIT	0x03000000	/* mode register setting command cycle */
#define SDRAMBUS_PREWAIT	0x0c000000	/* precharge command cycle */
#define SDRAMBUS_RASLATE	0x30000000	/* RAS latency */
#define SDRAMBUS_CASLATE	0xc0000000	/* CAS latency */

#define SDREFCNT		__SYSREG(0xda000004, u32)	/* refresh period reg */
#define SDREFCNT_PERI		0x00000fff	/* refresh period */

#define SDSHDW			__SYSREG(0xda000010, u32)	/* test reg */

#endif /* __KERNEL__ */

#endif /* _ASM_BUSCTL_REGS_H */
