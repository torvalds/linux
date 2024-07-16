/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2008 Renesas Solutions Corp.
 *
 * Hitachi UL SolutionEngine 7721 Support.
 */

#ifndef __ASM_SH_SE7721_H
#define __ASM_SH_SE7721_H

#include <linux/sh_intc.h>
#include <asm/addrspace.h>

/* Box specific addresses. */
#define SE_AREA0_WIDTH	2		/* Area0: 32bit */
#define PA_ROM		0xa0000000	/* EPROM */
#define PA_ROM_SIZE	0x00200000	/* EPROM size 2M byte */
#define PA_FROM		0xa1000000	/* Flash-ROM */
#define PA_FROM_SIZE	0x01000000	/* Flash-ROM size 16M byte */
#define PA_EXT1		0xa4000000
#define PA_EXT1_SIZE	0x04000000
#define PA_SDRAM	0xaC000000	/* SDRAM(Area3) 64MB */
#define PA_SDRAM_SIZE	0x04000000

#define PA_EXT4		0xb0000000
#define PA_EXT4_SIZE	0x04000000

#define PA_PERIPHERAL	0xB8000000

#define PA_PCIC		PA_PERIPHERAL
#define PA_MRSHPC	(PA_PERIPHERAL + 0x003fffe0)
#define PA_MRSHPC_MW1	(PA_PERIPHERAL + 0x00400000)
#define PA_MRSHPC_MW2	(PA_PERIPHERAL + 0x00500000)
#define PA_MRSHPC_IO	(PA_PERIPHERAL + 0x00600000)
#define MRSHPC_OPTION	(PA_MRSHPC + 6)
#define MRSHPC_CSR	(PA_MRSHPC + 8)
#define MRSHPC_ISR	(PA_MRSHPC + 10)
#define MRSHPC_ICR	(PA_MRSHPC + 12)
#define MRSHPC_CPWCR	(PA_MRSHPC + 14)
#define MRSHPC_MW0CR1	(PA_MRSHPC + 16)
#define MRSHPC_MW1CR1	(PA_MRSHPC + 18)
#define MRSHPC_IOWCR1	(PA_MRSHPC + 20)
#define MRSHPC_MW0CR2	(PA_MRSHPC + 22)
#define MRSHPC_MW1CR2	(PA_MRSHPC + 24)
#define MRSHPC_IOWCR2	(PA_MRSHPC + 26)
#define MRSHPC_CDCR	(PA_MRSHPC + 28)
#define MRSHPC_PCIC_INFO	(PA_MRSHPC + 30)

#define PA_LED		0xB6800000	/* 8bit LED */
#define PA_FPGA		0xB7000000	/* FPGA base address */

#define MRSHPC_IRQ0	evt2irq(0x340)

#define FPGA_ILSR1	(PA_FPGA + 0x02)
#define FPGA_ILSR2	(PA_FPGA + 0x03)
#define FPGA_ILSR3	(PA_FPGA + 0x04)
#define FPGA_ILSR4	(PA_FPGA + 0x05)
#define FPGA_ILSR5	(PA_FPGA + 0x06)
#define FPGA_ILSR6	(PA_FPGA + 0x07)
#define FPGA_ILSR7	(PA_FPGA + 0x08)
#define FPGA_ILSR8	(PA_FPGA + 0x09)

void init_se7721_IRQ(void);

#define __IO_PREFIX		se7721
#include <asm/io_generic.h>

#endif  /* __ASM_SH_SE7721_H */
