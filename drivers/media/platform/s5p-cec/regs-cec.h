/* drivers/media/platform/s5p-cec/regs-cec.h
 *
 * Copyright (c) 2010 Samsung Electronics
 *		http://www.samsung.com/
 *
 *  register header file for Samsung TVOUT driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __EXYNOS_REGS__H
#define __EXYNOS_REGS__H

/*
 * Register part
 */
#define S5P_CEC_STATUS_0			(0x0000)
#define S5P_CEC_STATUS_1			(0x0004)
#define S5P_CEC_STATUS_2			(0x0008)
#define S5P_CEC_STATUS_3			(0x000C)
#define S5P_CEC_IRQ_MASK			(0x0010)
#define S5P_CEC_IRQ_CLEAR			(0x0014)
#define S5P_CEC_LOGIC_ADDR			(0x0020)
#define S5P_CEC_DIVISOR_0			(0x0030)
#define S5P_CEC_DIVISOR_1			(0x0034)
#define S5P_CEC_DIVISOR_2			(0x0038)
#define S5P_CEC_DIVISOR_3			(0x003C)

#define S5P_CEC_TX_CTRL				(0x0040)
#define S5P_CEC_TX_BYTES			(0x0044)
#define S5P_CEC_TX_STAT0			(0x0060)
#define S5P_CEC_TX_STAT1			(0x0064)
#define S5P_CEC_TX_BUFF0			(0x0080)
#define S5P_CEC_TX_BUFF1			(0x0084)
#define S5P_CEC_TX_BUFF2			(0x0088)
#define S5P_CEC_TX_BUFF3			(0x008C)
#define S5P_CEC_TX_BUFF4			(0x0090)
#define S5P_CEC_TX_BUFF5			(0x0094)
#define S5P_CEC_TX_BUFF6			(0x0098)
#define S5P_CEC_TX_BUFF7			(0x009C)
#define S5P_CEC_TX_BUFF8			(0x00A0)
#define S5P_CEC_TX_BUFF9			(0x00A4)
#define S5P_CEC_TX_BUFF10			(0x00A8)
#define S5P_CEC_TX_BUFF11			(0x00AC)
#define S5P_CEC_TX_BUFF12			(0x00B0)
#define S5P_CEC_TX_BUFF13			(0x00B4)
#define S5P_CEC_TX_BUFF14			(0x00B8)
#define S5P_CEC_TX_BUFF15			(0x00BC)

#define S5P_CEC_RX_CTRL				(0x00C0)
#define S5P_CEC_RX_STAT0			(0x00E0)
#define S5P_CEC_RX_STAT1			(0x00E4)
#define S5P_CEC_RX_BUFF0			(0x0100)
#define S5P_CEC_RX_BUFF1			(0x0104)
#define S5P_CEC_RX_BUFF2			(0x0108)
#define S5P_CEC_RX_BUFF3			(0x010C)
#define S5P_CEC_RX_BUFF4			(0x0110)
#define S5P_CEC_RX_BUFF5			(0x0114)
#define S5P_CEC_RX_BUFF6			(0x0118)
#define S5P_CEC_RX_BUFF7			(0x011C)
#define S5P_CEC_RX_BUFF8			(0x0120)
#define S5P_CEC_RX_BUFF9			(0x0124)
#define S5P_CEC_RX_BUFF10			(0x0128)
#define S5P_CEC_RX_BUFF11			(0x012C)
#define S5P_CEC_RX_BUFF12			(0x0130)
#define S5P_CEC_RX_BUFF13			(0x0134)
#define S5P_CEC_RX_BUFF14			(0x0138)
#define S5P_CEC_RX_BUFF15			(0x013C)

#define S5P_CEC_RX_FILTER_CTRL			(0x0180)
#define S5P_CEC_RX_FILTER_TH			(0x0184)

/*
 * Bit definition part
 */
#define S5P_CEC_IRQ_TX_DONE			(1<<0)
#define S5P_CEC_IRQ_TX_ERROR			(1<<1)
#define S5P_CEC_IRQ_RX_DONE			(1<<4)
#define S5P_CEC_IRQ_RX_ERROR			(1<<5)

#define S5P_CEC_TX_CTRL_START			(1<<0)
#define S5P_CEC_TX_CTRL_BCAST			(1<<1)
#define S5P_CEC_TX_CTRL_RETRY			(0x04<<4)
#define S5P_CEC_TX_CTRL_RESET			(1<<7)

#define S5P_CEC_RX_CTRL_ENABLE			(1<<0)
#define S5P_CEC_RX_CTRL_RESET			(1<<7)

#define S5P_CEC_LOGIC_ADDR_MASK			(0xF)

/* PMU Registers for PHY */
#define EXYNOS_HDMI_PHY_CONTROL			0x700

#endif	/* __EXYNOS_REGS__H	*/
