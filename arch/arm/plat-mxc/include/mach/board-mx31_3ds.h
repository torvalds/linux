/*
 * Copyright 2008 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_MXC_BOARD_MX31_3DS_H__
#define __ASM_ARCH_MXC_BOARD_MX31_3DS_H__

/* Definitions for components on the Debug board */

/* Base address of CPLD controller on the Debug board */
#define DEBUG_BASE_ADDRESS		CS5_IO_ADDRESS(CS5_BASE_ADDR)

/* LAN9217 ethernet base address */
#define LAN9217_BASE_ADDR		CS5_BASE_ADDR

/* CPLD config and interrupt base address */
#define CPLD_ADDR			(DEBUG_BASE_ADDRESS + 0x20000)

/* LED switchs */
#define CPLD_LED_REG			(CPLD_ADDR + 0x00)
/* buttons */
#define CPLD_SWITCH_BUTTONS_REG	(EXPIO_ADDR + 0x08)
/* status, interrupt */
#define CPLD_INT_STATUS_REG		(CPLD_ADDR + 0x10)
#define CPLD_INT_MASK_REG		(CPLD_ADDR + 0x38)
#define CPLD_INT_RESET_REG		(CPLD_ADDR + 0x20)
/* magic word for debug CPLD */
#define CPLD_MAGIC_NUMBER1_REG		(CPLD_ADDR + 0x40)
#define CPLD_MAGIC_NUMBER2_REG		(CPLD_ADDR + 0x48)
/* CPLD code version */
#define CPLD_CODE_VER_REG		(CPLD_ADDR + 0x50)
/* magic word for debug CPLD */
#define CPLD_MAGIC_NUMBER3_REG		(CPLD_ADDR + 0x58)
/* module reset register */
#define CPLD_MODULE_RESET_REG		(CPLD_ADDR + 0x60)
/* CPU ID and Personality ID */
#define CPLD_MCU_BOARD_ID_REG		(CPLD_ADDR + 0x68)

/* CPLD IRQ line for external uart, external ethernet etc */
#define EXPIO_PARENT_INT	IOMUX_TO_IRQ(MX31_PIN_GPIO1_1)

#define MXC_EXP_IO_BASE		(MXC_BOARD_IRQ_START)
#define MXC_IRQ_TO_EXPIO(irq)	((irq) - MXC_EXP_IO_BASE)

#define EXPIO_INT_ENET		(MXC_EXP_IO_BASE + 0)
#define EXPIO_INT_XUART_A	(MXC_EXP_IO_BASE + 1)
#define EXPIO_INT_XUART_B	(MXC_EXP_IO_BASE + 2)
#define EXPIO_INT_BUTTON_A	(MXC_EXP_IO_BASE + 3)
#define EXPIO_INT_BUTTON_B	(MXC_EXP_IO_BASE + 4)

#define MXC_MAX_EXP_IO_LINES	16

#endif /* __ASM_ARCH_MXC_BOARD_MX31_3DS_H__ */
