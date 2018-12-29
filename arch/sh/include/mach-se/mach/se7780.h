/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_SE7780_H
#define __ASM_SH_SE7780_H

/*
 * linux/include/asm-sh/se7780.h
 *
 * Copyright (C) 2006,2007  Nobuhiro Iwamatsu
 *
 * Hitachi UL SolutionEngine 7780 Support.
 */
#include <linux/sh_intc.h>
#include <asm/addrspace.h>

/* Box specific addresses.  */
#define SE_AREA0_WIDTH	4		/* Area0: 32bit */
#define PA_ROM		0xa0000000	/* EPROM */
#define PA_ROM_SIZE	0x00400000	/* EPROM size 4M byte */
#define PA_FROM		0xa1000000	/* Flash-ROM */
#define PA_FROM_SIZE	0x01000000	/* Flash-ROM size 16M byte */
#define PA_EXT1		0xa4000000
#define PA_EXT1_SIZE	0x04000000
#define PA_SM501	PA_EXT1		/* Graphic IC (SM501) */
#define PA_SM501_SIZE	PA_EXT1_SIZE	/* Graphic IC (SM501) */
#define PA_SDRAM	0xa8000000	/* DDR-SDRAM(Area2/3) 128MB */
#define PA_SDRAM_SIZE	0x08000000

#define PA_EXT4		0xb0000000
#define PA_EXT4_SIZE	0x04000000
#define PA_EXT_FLASH	PA_EXT4		/* Expansion Flash-ROM */

#define PA_PERIPHERAL	PA_AREA6_IO	/* SW6-6=ON */

#define PA_LAN		(PA_PERIPHERAL + 0)		/* SMC LAN91C111 */
#define PA_LED_DISP	(PA_PERIPHERAL + 0x02000000)	/* 8words LED Display */
#define DISP_CHAR_RAM	(7 << 3)
#define DISP_SEL0_ADDR	(DISP_CHAR_RAM + 0)
#define DISP_SEL1_ADDR	(DISP_CHAR_RAM + 1)
#define DISP_SEL2_ADDR	(DISP_CHAR_RAM + 2)
#define DISP_SEL3_ADDR	(DISP_CHAR_RAM + 3)
#define DISP_SEL4_ADDR	(DISP_CHAR_RAM + 4)
#define DISP_SEL5_ADDR	(DISP_CHAR_RAM + 5)
#define DISP_SEL6_ADDR	(DISP_CHAR_RAM + 6)
#define DISP_SEL7_ADDR	(DISP_CHAR_RAM + 7)

#define DISP_UDC_RAM	(5 << 3)
#define PA_FPGA		(PA_PERIPHERAL + 0x03000000) /* FPGA base address */

/* FPGA register address and bit */
#define FPGA_SFTRST		(PA_FPGA + 0)	/* Soft reset register */
#define FPGA_INTMSK1		(PA_FPGA + 2)	/* Interrupt Mask register 1 */
#define FPGA_INTMSK2		(PA_FPGA + 4)	/* Interrupt Mask register 2 */
#define FPGA_INTSEL1		(PA_FPGA + 6)	/* Interrupt select register 1 */
#define FPGA_INTSEL2		(PA_FPGA + 8)	/* Interrupt select register 2 */
#define FPGA_INTSEL3		(PA_FPGA + 10)	/* Interrupt select register 3 */
#define FPGA_PCI_INTSEL1	(PA_FPGA + 12)	/* PCI Interrupt select register 1 */
#define FPGA_PCI_INTSEL2	(PA_FPGA + 14)	/* PCI Interrupt select register 2 */
#define FPGA_INTSET		(PA_FPGA + 16)	/* IRQ/IRL select register */
#define FPGA_INTSTS1		(PA_FPGA + 18)	/* Interrupt status register 1 */
#define FPGA_INTSTS2		(PA_FPGA + 20)	/* Interrupt status register 2 */
#define FPGA_REQSEL		(PA_FPGA + 22)	/* REQ/GNT select register */
#define FPGA_DBG_LED		(PA_FPGA + 32)	/* Debug LED(D-LED[8:1] */
#define PA_LED			FPGA_DBG_LED
#define FPGA_IVDRID		(PA_FPGA + 36)	/* iVDR ID Register */
#define FPGA_IVDRPW		(PA_FPGA + 38)	/* iVDR Power ON Register */
#define FPGA_MMCID		(PA_FPGA + 40)	/* MMC ID Register */

/* FPGA INTSEL position */
/* INTSEL1 */
#define IRQPOS_SMC91CX          (0 * 4)
#define IRQPOS_SM501            (1 * 4)
/* INTSEL2 */
#define IRQPOS_EXTINT1          (0 * 4)
#define IRQPOS_EXTINT2          (1 * 4)
#define IRQPOS_EXTINT3          (2 * 4)
#define IRQPOS_EXTINT4          (3 * 4)
/* INTSEL3 */
#define IRQPOS_PCCPW            (0 * 4)

/* IDE interrupt */
#define IRQ_IDE0                evt2irq(0xa60) /* iVDR */

/* SMC interrupt */
#define SMC_IRQ                 evt2irq(0x300)

/* SM501 interrupt */
#define SM501_IRQ               evt2irq(0x200)

/* interrupt pin */
#define IRQPIN_EXTINT1          0 /* IRQ0 pin */
#define IRQPIN_EXTINT2          1 /* IRQ1 pin */
#define IRQPIN_EXTINT3          2 /* IRQ2 pin */
#define IRQPIN_SMC91CX          3 /* IRQ3 pin */
#define IRQPIN_EXTINT4          4 /* IRQ4 pin */
#define IRQPIN_PCC0             5 /* IRQ5 pin */
#define IRQPIN_PCC2             6 /* IRQ6 pin */
#define IRQPIN_SM501            7 /* IRQ7 pin */
#define IRQPIN_PCCPW            7 /* IRQ7 pin */

/* arch/sh/boards/se/7780/irq.c */
void init_se7780_IRQ(void);

#define __IO_PREFIX		se7780
#include <asm/io_generic.h>

#endif  /* __ASM_SH_SE7780_H */
