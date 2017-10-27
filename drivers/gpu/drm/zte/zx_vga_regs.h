/*
 * Copyright (C) 2017 Sanechips Technology Co., Ltd.
 * Copyright 2017 Linaro Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ZX_VGA_REGS_H__
#define __ZX_VGA_REGS_H__

#define VGA_CMD_CFG			0x04
#define VGA_CMD_TRANS			BIT(6)
#define VGA_CMD_COMBO			BIT(5)
#define VGA_CMD_RW			BIT(4)
#define VGA_SUB_ADDR			0x0c
#define VGA_DEVICE_ADDR			0x10
#define VGA_CLK_DIV_FS			0x14
#define VGA_RXF_CTRL			0x20
#define VGA_RX_FIFO_CLEAR		BIT(7)
#define VGA_DATA			0x24
#define VGA_I2C_STATUS			0x28
#define VGA_DEVICE_DISCONNECTED		BIT(7)
#define VGA_DEVICE_CONNECTED		BIT(6)
#define VGA_CLEAR_IRQ			BIT(4)
#define VGA_TRANS_DONE			BIT(0)
#define VGA_RXF_STATUS			0x30
#define VGA_RXF_COUNT_SHIFT		2
#define VGA_RXF_COUNT_MASK		GENMASK(7, 2)
#define VGA_AUTO_DETECT_PARA		0x34
#define VGA_AUTO_DETECT_SEL		0x38
#define VGA_DETECT_SEL_HAS_DEVICE	BIT(1)
#define VGA_DETECT_SEL_NO_DEVICE	BIT(0)

#endif /* __ZX_VGA_REGS_H__ */
