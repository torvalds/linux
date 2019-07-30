/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ARC PGU DRM driver.
 *
 * Copyright (C) 2016 Synopsys, Inc. (www.synopsys.com)
 */

#ifndef _ARC_PGU_REGS_H_
#define _ARC_PGU_REGS_H_

#define ARCPGU_REG_CTRL		0x00
#define ARCPGU_REG_STAT		0x04
#define ARCPGU_REG_FMT		0x10
#define ARCPGU_REG_HSYNC	0x14
#define ARCPGU_REG_VSYNC	0x18
#define ARCPGU_REG_ACTIVE	0x1c
#define ARCPGU_REG_BUF0_ADDR	0x40
#define ARCPGU_REG_STRIDE	0x50
#define ARCPGU_REG_START_SET	0x84

#define ARCPGU_REG_ID		0x3FC

#define ARCPGU_CTRL_ENABLE_MASK	0x02
#define ARCPGU_CTRL_VS_POL_MASK	0x1
#define ARCPGU_CTRL_VS_POL_OFST	0x3
#define ARCPGU_CTRL_HS_POL_MASK	0x1
#define ARCPGU_CTRL_HS_POL_OFST	0x4
#define ARCPGU_MODE_RGB888_MASK	0x04
#define ARCPGU_STAT_BUSY_MASK	0x02

#endif
