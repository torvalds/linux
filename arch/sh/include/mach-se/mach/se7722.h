#ifndef __ASM_SH_SE7722_H
#define __ASM_SH_SE7722_H

/*
 * linux/include/asm-sh/se7722.h
 *
 * Copyright (C) 2007  Nobuhiro Iwamatsu
 *
 * Hitachi UL SolutionEngine 7722 Support.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */
#include <linux/sh_intc.h>
#include <asm/addrspace.h>

/* Box specific addresses.  */
#define SE_AREA0_WIDTH	4		/* Area0: 32bit */
#define PA_ROM		0xa0000000	/* EPROM */
#define PA_ROM_SIZE	0x00200000	/* EPROM size 2M byte */
#define PA_FROM		0xa1000000	/* Flash-ROM */
#define PA_FROM_SIZE	0x01000000	/* Flash-ROM size 16M byte */
#define PA_EXT1		0xa4000000
#define PA_EXT1_SIZE	0x04000000
#define PA_SDRAM	0xaC000000	/* DDR-SDRAM(Area3) 64MB */
#define PA_SDRAM_SIZE	0x04000000

#define PA_EXT4		0xb0000000
#define PA_EXT4_SIZE	0x04000000

#define PA_PERIPHERAL	0xB0000000

#define PA_PCIC         PA_PERIPHERAL		/* MR-SHPC-01 PCMCIA */
#define PA_MRSHPC       (PA_PERIPHERAL + 0x003fffe0)    /* MR-SHPC-01 PCMCIA controller */
#define PA_MRSHPC_MW1   (PA_PERIPHERAL + 0x00400000)    /* MR-SHPC-01 memory window base */
#define PA_MRSHPC_MW2   (PA_PERIPHERAL + 0x00500000)    /* MR-SHPC-01 attribute window base */
#define PA_MRSHPC_IO    (PA_PERIPHERAL + 0x00600000)    /* MR-SHPC-01 I/O window base */
#define MRSHPC_OPTION   (PA_MRSHPC + 6)
#define MRSHPC_CSR      (PA_MRSHPC + 8)
#define MRSHPC_ISR      (PA_MRSHPC + 10)
#define MRSHPC_ICR      (PA_MRSHPC + 12)
#define MRSHPC_CPWCR    (PA_MRSHPC + 14)
#define MRSHPC_MW0CR1   (PA_MRSHPC + 16)
#define MRSHPC_MW1CR1   (PA_MRSHPC + 18)
#define MRSHPC_IOWCR1   (PA_MRSHPC + 20)
#define MRSHPC_MW0CR2   (PA_MRSHPC + 22)
#define MRSHPC_MW1CR2   (PA_MRSHPC + 24)
#define MRSHPC_IOWCR2   (PA_MRSHPC + 26)
#define MRSHPC_CDCR     (PA_MRSHPC + 28)
#define MRSHPC_PCIC_INFO (PA_MRSHPC + 30)

#define PA_LED		(PA_PERIPHERAL + 0x00800000)	/* 8bit LED */
#define PA_FPGA		(PA_PERIPHERAL + 0x01800000)	/* FPGA base address */

#define PA_LAN		(PA_AREA6_IO + 0)		/* SMC LAN91C111 */
/* GPIO */
#define FPGA_IN         0xb1840000UL
#define FPGA_OUT        0xb1840004UL

#define PORT_PECR       0xA4050108UL
#define PORT_PJCR       0xA4050110UL
#define PORT_PSELD      0xA4050154UL
#define PORT_PSELB      0xA4050150UL

#define PORT_PSELC      0xA4050152UL
#define PORT_PKCR       0xA4050112UL
#define PORT_PHCR       0xA405010EUL
#define PORT_PLCR       0xA4050114UL
#define PORT_PMCR       0xA4050116UL
#define PORT_PRCR       0xA405011CUL
#define PORT_PXCR       0xA4050148UL
#define PORT_PSELA      0xA405014EUL
#define PORT_PYCR       0xA405014AUL
#define PORT_PZCR       0xA405014CUL
#define PORT_HIZCRA     0xA4050158UL
#define PORT_HIZCRC     0xA405015CUL

/* IRQ */
#define IRQ0_IRQ        evt2irq(0x600)
#define IRQ1_IRQ        evt2irq(0x620)

#define IRQ01_MODE      0xb1800000
#define IRQ01_STS       0xb1800004
#define IRQ01_MASK      0xb1800008

/* Bits in IRQ01_* registers */

#define SE7722_FPGA_IRQ_USB	0 /* IRQ0 */
#define SE7722_FPGA_IRQ_SMC	1 /* IRQ0 */
#define SE7722_FPGA_IRQ_MRSHPC0	2 /* IRQ1 */
#define SE7722_FPGA_IRQ_MRSHPC1	3 /* IRQ1 */
#define SE7722_FPGA_IRQ_MRSHPC2	4 /* IRQ1 */
#define SE7722_FPGA_IRQ_MRSHPC3	5 /* IRQ1 */
#define SE7722_FPGA_IRQ_NR	6

/* arch/sh/boards/se/7722/irq.c */
extern unsigned int se7722_fpga_irq[];

void init_se7722_IRQ(void);

#define __IO_PREFIX		se7722
#include <asm/io_generic.h>

#endif  /* __ASM_SH_SE7722_H */
