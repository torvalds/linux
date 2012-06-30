#ifndef __ASM_SH_RENESAS_SDK7780_H
#define __ASM_SH_RENESAS_SDK7780_H

/*
 * linux/include/asm-sh/sdk7780.h
 *
 * Renesas Solutions SH7780 SDK Support
 * Copyright (C) 2008 Nicholas Beck <nbeck@mpc-data.co.uk>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/sh_intc.h>
#include <asm/addrspace.h>

/* Box specific addresses.  */
#define SE_AREA0_WIDTH	4		/* Area0: 32bit */
#define PA_ROM			0xa0000000	/* EPROM */
#define PA_ROM_SIZE		0x00400000	/* EPROM size 4M byte */
#define PA_FROM			0xa0800000	/* Flash-ROM */
#define PA_FROM_SIZE	0x00400000	/* Flash-ROM size 4M byte */
#define PA_EXT1			0xa4000000
#define PA_EXT1_SIZE	0x04000000
#define PA_SDRAM		0xa8000000	/* DDR-SDRAM(Area2/3) 128MB */
#define PA_SDRAM_SIZE	0x08000000

#define PA_EXT4			0xb0000000
#define PA_EXT4_SIZE	0x04000000
#define PA_EXT_USER		PA_EXT4		/* User Expansion Space */

#define PA_PERIPHERAL	PA_AREA5_IO

/* SRAM/Reserved */
#define PA_RESERVED	(PA_PERIPHERAL + 0)
/* FPGA base address */
#define PA_FPGA		(PA_PERIPHERAL + 0x01000000)
/* SMC LAN91C111 */
#define PA_LAN		(PA_PERIPHERAL + 0x01800000)


#define FPGA_SRSTR      (PA_FPGA + 0x000)	/* System reset */
#define FPGA_IRQ0SR     (PA_FPGA + 0x010)	/* IRQ0 status */
#define FPGA_IRQ0MR     (PA_FPGA + 0x020)	/* IRQ0 mask */
#define FPGA_BDMR       (PA_FPGA + 0x030)	/* Board operating mode */
#define FPGA_INTT0PRTR  (PA_FPGA + 0x040)	/* Interrupt test mode0 port */
#define FPGA_INTT0SELR  (PA_FPGA + 0x050)	/* Int. test mode0 select */
#define FPGA_INTT1POLR  (PA_FPGA + 0x060)	/* Int. test mode0 polarity */
#define FPGA_NMIR       (PA_FPGA + 0x070)	/* NMI source */
#define FPGA_NMIMR      (PA_FPGA + 0x080)	/* NMI mask */
#define FPGA_IRQR       (PA_FPGA + 0x090)	/* IRQX source */
#define FPGA_IRQMR      (PA_FPGA + 0x0A0)	/* IRQX mask */
#define FPGA_SLEDR      (PA_FPGA + 0x0B0)	/* LED control */
#define PA_LED			FPGA_SLEDR
#define FPGA_MAPSWR     (PA_FPGA + 0x0C0)	/* Map switch */
#define FPGA_FPVERR     (PA_FPGA + 0x0D0)	/* FPGA version */
#define FPGA_FPDATER    (PA_FPGA + 0x0E0)	/* FPGA date */
#define FPGA_RSE        (PA_FPGA + 0x100)	/* Reset source */
#define FPGA_EASR       (PA_FPGA + 0x110)	/* External area select */
#define FPGA_SPER       (PA_FPGA + 0x120)	/* Serial port enable */
#define FPGA_IMSR       (PA_FPGA + 0x130)	/* Interrupt mode select */
#define FPGA_PCIMR      (PA_FPGA + 0x140)	/* PCI Mode */
#define FPGA_DIPSWMR    (PA_FPGA + 0x150)	/* DIPSW monitor */
#define FPGA_FPODR      (PA_FPGA + 0x160)	/* Output port data */
#define FPGA_ATAESR     (PA_FPGA + 0x170)	/* ATA extended bus status */
#define FPGA_IRQPOLR    (PA_FPGA + 0x180)	/* IRQx polarity */


#define SDK7780_NR_IRL			15
/* IDE/ATA interrupt */
#define IRQ_CFCARD			evt2irq(0x3c0)
/* SMC interrupt */
#define IRQ_ETHERNET			evt2irq(0x2c0)


/* arch/sh/boards/renesas/sdk7780/irq.c */
void init_sdk7780_IRQ(void);

#define __IO_PREFIX		sdk7780
#include <asm/io_generic.h>

#endif  /* __ASM_SH_RENESAS_SDK7780_H */
