/*
 * Copyright (C) 2012 Samsung Electronics Co., Ltd.
 *
 * Exynos5 series HS-I2C Controller
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_REGS_HS_IIC_H
#define __ASM_ARCH_REGS_HS_IIC_H __FILE__

/*****************************************************************
	Register Map
*/
#define HSI2C_CTL				0x00
#define HSI2C_FIFO_CTL				0x04
#define HSI2C_TRAILIG_CTL			0x08
#define HSI2C_CLK_CTL				0x0C
#define HSI2C_CLK_SLOT				0x10
#define HSI2C_INT_ENABLE			0x20
#define HSI2C_INT_STATUS			0x24
#define HSI2C_ERR_STATUS			0x2C
#define HSI2C_FIFO_STATUS			0x30
#define HSI2C_TX_DATA				0x34
#define HSI2C_RX_DATA				0x38
#define HSI2C_CONF				0x40
#define HSI2C_AUTO_CONFING			0x44
#define HSI2C_TIMEOUT				0x48
#define HSI2C_MANUAL_CMD			0x4C
#define HSI2C_TRANS_STATUS			0x50
#define HSI2C_TIMING_HS1			0x54
#define HSI2C_TIMING_HS2			0x58
#define HSI2C_TIMING_HS3			0x5C
#define HSI2C_TIMING_FS1			0x60
#define HSI2C_TIMING_FS2			0x64
#define HSI2C_TIMING_FS3			0x68
#define HSI2C_TIMING_SLA			0x6C
#define HSI2C_ADDR				0x70

#define HSI2C_FUNC_MODE_I2C			(1u << 0)
#define HSI2C_MASTER				(1u << 3)
#define HSI2C_RXCHON				(1u << 6)
#define HSI2C_TXCHON				(1u << 7)
#define HSI2C_RXFIFO_EN				(1u << 0)
#define HSI2C_TXFIFO_EN				(1u << 1)
#define HSI2C_TXFIFO_TRIGGER_LEVEL		(0x20 << 16)
#define HSI2C_RXFIFO_TRIGGER_LEVEL		(0x20 << 4)
#define HSI2C_TRAILING_COUNT			(0xf)
#define HSI2C_INT_TX_ALMOSTEMPTY_EN		(1u << 0)
#define HSI2C_INT_RX_ALMOSTFULL_EN		(1u << 1)
#define HSI2C_INT_TRAILING_EN			(1u << 6)
#define HSI2C_READ_WRITE			(1u << 16)
#define HSI2C_STOP_AFTER_TRANS			(1u << 17)
#define HSI2C_MASTER_RUN			(1u << 31)
#define HSI2C_TIMEOUT_EN			(1u << 31)
#define HSI2C_FIFO_EMPTY			(0x1000100)

#endif /* __ASM_ARCH_REGS_HS_IIC_H */
