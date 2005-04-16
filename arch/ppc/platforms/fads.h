/*
 * A collection of structures, addresses, and values associated with
 * the Motorola 860T FADS board.  Copied from the MBX stuff.
 *
 * Copyright (c) 1998 Dan Malek (dmalek@jlc.net)
 */
#ifdef __KERNEL__
#ifndef __ASM_FADS_H__
#define __ASM_FADS_H__

#include <linux/config.h>

#include <asm/ppcboot.h>

/* Memory map is configured by the PROM startup.
 * I tried to follow the FADS manual, although the startup PROM
 * dictates this and we simply have to move some of the physical
 * addresses for Linux.
 */
#define BCSR_ADDR		((uint)0xff010000)
#define BCSR_SIZE		((uint)(64 * 1024))
#define	BCSR0			((uint)0xff010000)
#define	BCSR1			((uint)0xff010004)
#define	BCSR2			((uint)0xff010008)
#define	BCSR3			((uint)0xff01000c)
#define	BCSR4			((uint)0xff010010)

#define IMAP_ADDR		((uint)0xff000000)
#define IMAP_SIZE		((uint)(64 * 1024))

#define PCMCIA_MEM_ADDR		((uint)0xff020000)
#define PCMCIA_MEM_SIZE		((uint)(64 * 1024))

/* Bits of interest in the BCSRs.
 */
#define BCSR1_ETHEN		((uint)0x20000000)
#define BCSR1_RS232EN_1		((uint)0x01000000)
#define BCSR1_RS232EN_2		((uint)0x00040000)
#define BCSR4_ETHLOOP		((uint)0x80000000)	/* EEST Loopback */
#define BCSR4_EEFDX		((uint)0x40000000)	/* EEST FDX enable */
#define BCSR4_FETH_EN		((uint)0x08000000)	/* PHY enable */
#define BCSR4_FETHCFG0		((uint)0x04000000)	/* PHY autoneg mode */
#define BCSR4_FETHCFG1		((uint)0x00400000)	/* PHY autoneg mode */
#define BCSR4_FETHFDE		((uint)0x02000000)	/* PHY FDX advertise */
#define BCSR4_FETHRST		((uint)0x00200000)	/* PHY Reset */

/* Interrupt level assignments.
 */
#define FEC_INTERRUPT	SIU_LEVEL1	/* FEC interrupt */
#define PHY_INTERRUPT	SIU_IRQ2	/* PHY link change interrupt */

/* We don't use the 8259.
 */
#define NR_8259_INTS	0

#endif /* __ASM_FADS_H__ */
#endif /* __KERNEL__ */
