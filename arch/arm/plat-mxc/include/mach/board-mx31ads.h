/*
 * Copyright 2005-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_MXC_BOARD_MX31ADS_H__
#define __ASM_ARCH_MXC_BOARD_MX31ADS_H__

#include <mach/hardware.h>

/* Base address of PBC controller */
#define PBC_BASE_ADDRESS        IO_ADDRESS(CS4_BASE_ADDR)
/* Offsets for the PBC Controller register */

/* PBC Board status register offset */
#define PBC_BSTAT               0x000002

/* PBC Board control register 1 set address */
#define PBC_BCTRL1_SET          0x000004

/* PBC Board control register 1 clear address */
#define PBC_BCTRL1_CLEAR        0x000006

/* PBC Board control register 2 set address */
#define PBC_BCTRL2_SET          0x000008

/* PBC Board control register 2 clear address */
#define PBC_BCTRL2_CLEAR        0x00000A

/* PBC Board control register 3 set address */
#define PBC_BCTRL3_SET          0x00000C

/* PBC Board control register 3 clear address */
#define PBC_BCTRL3_CLEAR        0x00000E

/* PBC Board control register 4 set address */
#define PBC_BCTRL4_SET          0x000010

/* PBC Board control register 4 clear address */
#define PBC_BCTRL4_CLEAR        0x000012

/* PBC Board status register 1 */
#define PBC_BSTAT1              0x000014

/* PBC Board interrupt status register */
#define PBC_INTSTATUS           0x000016

/* PBC Board interrupt current status register */
#define PBC_INTCURR_STATUS      0x000018

/* PBC Interrupt mask register set address */
#define PBC_INTMASK_SET         0x00001A

/* PBC Interrupt mask register clear address */
#define PBC_INTMASK_CLEAR       0x00001C

/* External UART A */
#define PBC_SC16C652_UARTA      0x010000

/* External UART B */
#define PBC_SC16C652_UARTB      0x010010

/* Ethernet Controller IO base address */
#define PBC_CS8900A_IOBASE      0x020000

/* Ethernet Controller Memory base address */
#define PBC_CS8900A_MEMBASE     0x021000

/* Ethernet Controller DMA base address */
#define PBC_CS8900A_DMABASE     0x022000

/* External chip select 0 */
#define PBC_XCS0                0x040000

/* LCD Display enable */
#define PBC_LCD_EN_B            0x060000

/* Code test debug enable */
#define PBC_CODE_B              0x070000

/* PSRAM memory select */
#define PBC_PSRAM_B             0x5000000

#define PBC_INTSTATUS_REG	(PBC_INTSTATUS + PBC_BASE_ADDRESS)
#define PBC_INTCURR_STATUS_REG	(PBC_INTCURR_STATUS + PBC_BASE_ADDRESS)
#define PBC_INTMASK_SET_REG	(PBC_INTMASK_SET + PBC_BASE_ADDRESS)
#define PBC_INTMASK_CLEAR_REG	(PBC_INTMASK_CLEAR + PBC_BASE_ADDRESS)
#define EXPIO_PARENT_INT	IOMUX_TO_IRQ(MX31_PIN_GPIO1_4)

#define MXC_EXP_IO_BASE		(MXC_BOARD_IRQ_START)
#define MXC_IRQ_TO_EXPIO(irq)	((irq) - MXC_EXP_IO_BASE)

#define EXPIO_INT_LOW_BAT	(MXC_EXP_IO_BASE + 0)
#define EXPIO_INT_PB_IRQ	(MXC_EXP_IO_BASE + 1)
#define EXPIO_INT_OTG_FS_OVR	(MXC_EXP_IO_BASE + 2)
#define EXPIO_INT_FSH_OVR	(MXC_EXP_IO_BASE + 3)
#define EXPIO_INT_RES4		(MXC_EXP_IO_BASE + 4)
#define EXPIO_INT_RES5		(MXC_EXP_IO_BASE + 5)
#define EXPIO_INT_RES6		(MXC_EXP_IO_BASE + 6)
#define EXPIO_INT_RES7		(MXC_EXP_IO_BASE + 7)
#define EXPIO_INT_ENET_INT	(MXC_EXP_IO_BASE + 8)
#define EXPIO_INT_OTG_FS_INT	(MXC_EXP_IO_BASE + 9)
#define EXPIO_INT_XUART_INTA	(MXC_EXP_IO_BASE + 10)
#define EXPIO_INT_XUART_INTB	(MXC_EXP_IO_BASE + 11)
#define EXPIO_INT_SYNTH_IRQ	(MXC_EXP_IO_BASE + 12)
#define EXPIO_INT_CE_INT1	(MXC_EXP_IO_BASE + 13)
#define EXPIO_INT_CE_INT2	(MXC_EXP_IO_BASE + 14)
#define EXPIO_INT_RES15		(MXC_EXP_IO_BASE + 15)

#define MXC_MAX_EXP_IO_LINES	16

/* mandatory for CONFIG_DEBUG_LL */

#define MXC_LL_UART_PADDR	UART1_BASE_ADDR
#define MXC_LL_UART_VADDR	AIPS1_IO_ADDRESS(UART1_BASE_ADDR)

#endif /* __ASM_ARCH_MXC_BOARD_MX31ADS_H__ */
