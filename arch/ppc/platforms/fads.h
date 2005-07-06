/*
 * A collection of structures, addresses, and values associated with
 * the Motorola 860T FADS board.  Copied from the MBX stuff.
 *
 * Copyright (c) 1998 Dan Malek (dmalek@jlc.net)
 *
 * Added MPC86XADS support.
 * The MPC86xADS manual says the board "is compatible with the MPC8xxFADS
 * for SW point of view". This is 99% correct.
 *
 * Author: MontaVista Software, Inc.
 *         source@mvista.com
 * 2005 (c) MontaVista Software, Inc.  This file is licensed under the
 * terms of the GNU General Public License version 2.  This program is licensed
 * "as is" without any warranty of any kind, whether express or implied.
 */

#ifdef __KERNEL__
#ifndef __ASM_FADS_H__
#define __ASM_FADS_H__

#include <linux/config.h>

#include <asm/ppcboot.h>

#if defined(CONFIG_MPC86XADS)

/* U-Boot maps BCSR to 0xff080000 */
#define BCSR_ADDR		((uint)0xff080000)

/* MPC86XADS has one more CPLD and an additional BCSR.
 */
#define CFG_PHYDEV_ADDR		((uint)0xff0a0000)
#define BCSR5			((uint)(CFG_PHYDEV_ADDR + 0x300))

#define BCSR5_T1_RST		0x10
#define BCSR5_ATM155_RST	0x08
#define BCSR5_ATM25_RST		0x04
#define BCSR5_MII1_EN		0x02
#define BCSR5_MII1_RST		0x01

/* There is no PHY link change interrupt */
#define PHY_INTERRUPT	(-1)

#else /* FADS */

/* Memory map is configured by the PROM startup.
 * I tried to follow the FADS manual, although the startup PROM
 * dictates this and we simply have to move some of the physical
 * addresses for Linux.
 */
#define BCSR_ADDR		((uint)0xff010000)

/* PHY link change interrupt */
#define PHY_INTERRUPT	SIU_IRQ2

#endif /* CONFIG_MPC86XADS */

#define BCSR_SIZE		((uint)(64 * 1024))
#define BCSR0			((uint)(BCSR_ADDR + 0x00))
#define BCSR1			((uint)(BCSR_ADDR + 0x04))
#define BCSR2			((uint)(BCSR_ADDR + 0x08))
#define BCSR3			((uint)(BCSR_ADDR + 0x0c))
#define BCSR4			((uint)(BCSR_ADDR + 0x10))

#define IMAP_ADDR		((uint)0xff000000)
#define IMAP_SIZE		((uint)(64 * 1024))

#define PCMCIA_MEM_ADDR		((uint)0xff020000)
#define PCMCIA_MEM_SIZE		((uint)(64 * 1024))

/* Bits of interest in the BCSRs.
 */
#define BCSR1_ETHEN		((uint)0x20000000)
#define BCSR1_IRDAEN		((uint)0x10000000)
#define BCSR1_RS232EN_1		((uint)0x01000000)
#define BCSR1_PCCEN		((uint)0x00800000)
#define BCSR1_PCCVCC0		((uint)0x00400000)
#define BCSR1_PCCVPP0		((uint)0x00200000)
#define BCSR1_PCCVPP1		((uint)0x00100000)
#define BCSR1_PCCVPP_MASK	(BCSR1_PCCVPP0 | BCSR1_PCCVPP1)
#define BCSR1_RS232EN_2		((uint)0x00040000)
#define BCSR1_PCCVCC1		((uint)0x00010000)
#define BCSR1_PCCVCC_MASK	(BCSR1_PCCVCC0 | BCSR1_PCCVCC1)

#define BCSR4_ETHLOOP		((uint)0x80000000)	/* EEST Loopback */
#define BCSR4_EEFDX		((uint)0x40000000)	/* EEST FDX enable */
#define BCSR4_FETH_EN		((uint)0x08000000)	/* PHY enable */
#define BCSR4_FETHCFG0		((uint)0x04000000)	/* PHY autoneg mode */
#define BCSR4_FETHCFG1		((uint)0x00400000)	/* PHY autoneg mode */
#define BCSR4_FETHFDE		((uint)0x02000000)	/* PHY FDX advertise */
#define BCSR4_FETHRST		((uint)0x00200000)	/* PHY Reset */

/* IO_BASE definition for pcmcia.
 */
#define _IO_BASE	0x80000000
#define _IO_BASE_SIZE	0x1000

#ifdef CONFIG_IDE
#define MAX_HWIFS 1
#endif

/* Interrupt level assignments.
 */
#define FEC_INTERRUPT	SIU_LEVEL1	/* FEC interrupt */

/* We don't use the 8259.
 */
#define NR_8259_INTS	0

/* CPM Ethernet through SCC1 or SCC2 */

#ifdef CONFIG_SCC1_ENET		/* Probably 860 variant */
/* Bits in parallel I/O port registers that have to be set/cleared
 * to configure the pins for SCC1 use.
 * TCLK - CLK1, RCLK - CLK2.
 */
#define PA_ENET_RXD	((ushort)0x0001)
#define PA_ENET_TXD	((ushort)0x0002)
#define PA_ENET_TCLK	((ushort)0x0100)
#define PA_ENET_RCLK	((ushort)0x0200)
#define PB_ENET_TENA	((uint)0x00001000)
#define PC_ENET_CLSN	((ushort)0x0010)
#define PC_ENET_RENA	((ushort)0x0020)

/* Control bits in the SICR to route TCLK (CLK1) and RCLK (CLK2) to
 * SCC1.  Also, make sure GR1 (bit 24) and SC1 (bit 25) are zero.
 */
#define SICR_ENET_MASK	((uint)0x000000ff)
#define SICR_ENET_CLKRT	((uint)0x0000002c)
#endif /* CONFIG_SCC1_ENET */

#ifdef CONFIG_SCC2_ENET		/* Probably 823/850 variant */
/* Bits in parallel I/O port registers that have to be set/cleared
 * to configure the pins for SCC1 use.
 * TCLK - CLK1, RCLK - CLK2.
 */
#define PA_ENET_RXD	((ushort)0x0004)
#define PA_ENET_TXD	((ushort)0x0008)
#define PA_ENET_TCLK	((ushort)0x0400)
#define PA_ENET_RCLK	((ushort)0x0200)
#define PB_ENET_TENA	((uint)0x00002000)
#define PC_ENET_CLSN	((ushort)0x0040)
#define PC_ENET_RENA	((ushort)0x0080)

/* Control bits in the SICR to route TCLK and RCLK to
 * SCC2.  Also, make sure GR1 (bit 24) and SC1 (bit 25) are zero.
 */
#define SICR_ENET_MASK	((uint)0x0000ff00)
#define SICR_ENET_CLKRT	((uint)0x00002e00)
#endif /* CONFIG_SCC2_ENET */

#endif /* __ASM_FADS_H__ */
#endif /* __KERNEL__ */
