/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * Copyright (C) 2015 John Crispin <john@phrozen.org>
 */

#ifndef _MT7621_REGS_H_
#define _MT7621_REGS_H_

#define MT7621_PALMBUS_BASE		0x1C000000
#define MT7621_PALMBUS_SIZE		0x03FFFFFF

#define MT7621_SYSC_BASE		0x1E000000

#define SYSC_REG_CHIP_NAME0		0x00
#define SYSC_REG_CHIP_NAME1		0x04
#define SYSC_REG_CHIP_REV		0x0c
#define SYSC_REG_SYSTEM_CONFIG0		0x10
#define SYSC_REG_SYSTEM_CONFIG1		0x14

#define CHIP_REV_PKG_MASK		0x1
#define CHIP_REV_PKG_SHIFT		16
#define CHIP_REV_VER_MASK		0xf
#define CHIP_REV_VER_SHIFT		8
#define CHIP_REV_ECO_MASK		0xf

#define MT7621_DRAM_BASE                0x0
#define MT7621_DDR2_SIZE_MIN		32
#define MT7621_DDR2_SIZE_MAX		256

#define MT7621_CHIP_NAME0		0x3637544D
#define MT7621_CHIP_NAME1		0x20203132

#define MIPS_GIC_IRQ_BASE           (MIPS_CPU_IRQ_BASE + 8)

#endif
