// SPDX-License-Identifier: GPL-2.0
/*
 * BQ27xxx battery driver
 *
 * Copyright (C) 2008 Rodolfo Giometti <giometti@linux.it>
 * Copyright (C) 2008 Eurotech S.p.A. <info@eurotech.it>
 * Copyright (C) 2010-2011 Lars-Peter Clausen <lars@metafoo.de>
 * Copyright (C) 2011 Pali Rohár <pali@kernel.org>
 * Copyright (C) 2017 Liam Breck <kernel@networkimprov.net>
 *
 * Based on a previous work by Copyright (C) 2008 Texas Instruments, Inc.
 *
 * Datasheets:
 * https://www.ti.com/product/bq27000
 * https://www.ti.com/product/bq27200
 * https://www.ti.com/product/bq27010
 * https://www.ti.com/product/bq27210
 * https://www.ti.com/product/bq27500
 * https://www.ti.com/product/bq27510-g1
 * https://www.ti.com/product/bq27510-g2
 * https://www.ti.com/product/bq27510-g3
 * https://www.ti.com/product/bq27520-g1
 * https://www.ti.com/product/bq27520-g2
 * https://www.ti.com/product/bq27520-g3
 * https://www.ti.com/product/bq27520-g4
 * https://www.ti.com/product/bq27530-g1
 * https://www.ti.com/product/bq27531-g1
 * https://www.ti.com/product/bq27541-g1
 * https://www.ti.com/product/bq27542-g1
 * https://www.ti.com/product/bq27546-g1
 * https://www.ti.com/product/bq27742-g1
 * https://www.ti.com/product/bq27545-g1
 * https://www.ti.com/product/bq27421-g1
 * https://www.ti.com/product/bq27425-g1
 * https://www.ti.com/product/bq27426
 * https://www.ti.com/product/bq27411-g1
 * https://www.ti.com/product/bq27441-g1
 * https://www.ti.com/product/bq27621-g1
 * https://www.ti.com/product/bq27z561
 * https://www.ti.com/product/bq28z610
 * https://www.ti.com/product/bq34z100-g1
 * https://www.ti.com/product/bq78z100
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/param.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/of.h>

#include <linux/power/bq27xxx_battery.h>

#define BQ27XXX_MANUFACTURER	"Texas Instruments"

/* BQ27XXX Flags */
#define BQ27XXX_FLAG_DSC	BIT(0)
#define BQ27XXX_FLAG_SOCF	BIT(1) /* State-of-Charge threshold final */
#define BQ27XXX_FLAG_SOC1	BIT(2) /* State-of-Charge threshold 1 */
#define BQ27XXX_FLAG_CFGUP	BIT(4)
#define BQ27XXX_FLAG_FC		BIT(9)
#define BQ27XXX_FLAG_OTD	BIT(14)
#define BQ27XXX_FLAG_OTC	BIT(15)
#define BQ27XXX_FLAG_UT		BIT(14)
#define BQ27XXX_FLAG_OT		BIT(15)

/* BQ27000 has different layout for Flags register */
#define BQ27000_FLAG_EDVF	BIT(0) /* Final End-of-Discharge-Voltage flag */
#define BQ27000_FLAG_EDV1	BIT(1) /* First End-of-Discharge-Voltage flag */
#define BQ27000_FLAG_CI		BIT(4) /* Capacity Inaccurate flag */
#define BQ27000_FLAG_FC		BIT(5)
#define BQ27000_FLAG_CHGS	BIT(7) /* Charge state flag */

/* BQ27Z561 has different layout for Flags register */
#define BQ27Z561_FLAG_FDC	BIT(4) /* Battery fully discharged */
#define BQ27Z561_FLAG_FC	BIT(5) /* Battery fully charged */
#define BQ27Z561_FLAG_DIS_CH	BIT(6) /* Battery is discharging */

/* control register params */
#define BQ27XXX_SEALED			0x20
#define BQ27XXX_SET_CFGUPDATE		0x13
#define BQ27XXX_SOFT_RESET		0x42
#define BQ27XXX_RESET			0x41

#define BQ27XXX_RS			(20) /* Resistor sense mOhm */
#define BQ27XXX_POWER_CONSTANT		(29200) /* 29.2 µV^2 * 1000 */
#define BQ27XXX_CURRENT_CONSTANT	(3570) /* 3.57 µV * 1000 */

#define INVALID_REG_ADDR	0xff

/*
 * bq27xxx_reg_index - Register names
 *
 * These are indexes into a device's register mapping array.
 */

enum bq27xxx_reg_index {
	BQ27XXX_REG_CTRL = 0,	/* Control */
	BQ27XXX_REG_TEMP,	/* Temperature */
	BQ27XXX_REG_INT_TEMP,	/* Internal Temperature */
	BQ27XXX_REG_VOLT,	/* Voltage */
	BQ27XXX_REG_AI,		/* Average Current */
	BQ27XXX_REG_FLAGS,	/* Flags */
	BQ27XXX_REG_TTE,	/* Time-to-Empty */
	BQ27XXX_REG_TTF,	/* Time-to-Full */
	BQ27XXX_REG_TTES,	/* Time-to-Empty Standby */
	BQ27XXX_REG_TTECP,	/* Time-to-Empty at Constant Power */
	BQ27XXX_REG_NAC,	/* Nominal Available Capacity */
	BQ27XXX_REG_RC,		/* Remaining Capacity */
	BQ27XXX_REG_FCC,	/* Full Charge Capacity */
	BQ27XXX_REG_CYCT,	/* Cycle Count */
	BQ27XXX_REG_AE,		/* Available Energy */
	BQ27XXX_REG_SOC,	/* State-of-Charge */
	BQ27XXX_REG_DCAP,	/* Design Capacity */
	BQ27XXX_REG_AP,		/* Average Power */
	BQ27XXX_DM_CTRL,	/* Block Data Control */
	BQ27XXX_DM_CLASS,	/* Data Class */
	BQ27XXX_DM_BLOCK,	/* Data Block */
	BQ27XXX_DM_DATA,	/* Block Data */
	BQ27XXX_DM_CKSUM,	/* Block Data Checksum */
	BQ27XXX_REG_SEDVF,	/* End-of-discharge Voltage */
	BQ27XXX_REG_PKCFG,	/* Pack Configuration */
	BQ27XXX_REG_MAX,	/* sentinel */
};

#define BQ27XXX_DM_REG_ROWS \
	[BQ27XXX_DM_CTRL] = 0x61,  \
	[BQ27XXX_DM_CLASS] = 0x3e, \
	[BQ27XXX_DM_BLOCK] = 0x3f, \
	[BQ27XXX_DM_DATA] = 0x40,  \
	[BQ27XXX_DM_CKSUM] = 0x60

/* Register mappings */
static u8
	bq27000_regs[BQ27XXX_REG_MAX] = {
		[BQ27XXX_REG_CTRL] = 0x00,
		[BQ27XXX_REG_TEMP] = 0x06,
		[BQ27XXX_REG_INT_TEMP] = INVALID_REG_ADDR,
		[BQ27XXX_REG_VOLT] = 0x08,
		[BQ27XXX_REG_AI] = 0x14,
		[BQ27XXX_REG_FLAGS] = 0x0a,
		[BQ27XXX_REG_TTE] = 0x16,
		[BQ27XXX_REG_TTF] = 0x18,
		[BQ27XXX_REG_TTES] = 0x1c,
		[BQ27XXX_REG_TTECP] = 0x26,
		[BQ27XXX_REG_NAC] = 0x0c,
		[BQ27XXX_REG_RC] = INVALID_REG_ADDR,
		[BQ27XXX_REG_FCC] = 0x12,
		[BQ27XXX_REG_CYCT] = 0x2a,
		[BQ27XXX_REG_AE] = 0x22,
		[BQ27XXX_REG_SOC] = 0x0b,
		[BQ27XXX_REG_DCAP] = 0x76,
		[BQ27XXX_REG_AP] = 0x24,
		[BQ27XXX_DM_CTRL] = INVALID_REG_ADDR,
		[BQ27XXX_DM_CLASS] = INVALID_REG_ADDR,
		[BQ27XXX_DM_BLOCK] = INVALID_REG_ADDR,
		[BQ27XXX_DM_DATA] = INVALID_REG_ADDR,
		[BQ27XXX_DM_CKSUM] = INVALID_REG_ADDR,
		[BQ27XXX_REG_SEDVF] = 0x77,
		[BQ27XXX_REG_PKCFG] = 0x7C,
	},
	bq27010_regs[BQ27XXX_REG_MAX] = {
		[BQ27XXX_REG_CTRL] = 0x00,
		[BQ27XXX_REG_TEMP] = 0x06,
		[BQ27XXX_REG_INT_TEMP] = INVALID_REG_ADDR,
		[BQ27XXX_REG_VOLT] = 0x08,
		[BQ27XXX_REG_AI] = 0x14,
		[BQ27XXX_REG_FLAGS] = 0x0a,
		[BQ27XXX_REG_TTE] = 0x16,
		[BQ27XXX_REG_TTF] = 0x18,
		[BQ27XXX_REG_TTES] = 0x1c,
		[BQ27XXX_REG_TTECP] = 0x26,
		[BQ27XXX_REG_NAC] = 0x0c,
		[BQ27XXX_REG_RC] = INVALID_REG_ADDR,
		[BQ27XXX_REG_FCC] = 0x12,
		[BQ27XXX_REG_CYCT] = 0x2a,
		[BQ27XXX_REG_AE] = INVALID_REG_ADDR,
		[BQ27XXX_REG_SOC] = 0x0b,
		[BQ27XXX_REG_DCAP] = 0x76,
		[BQ27XXX_REG_AP] = INVALID_REG_ADDR,
		[BQ27XXX_DM_CTRL] = INVALID_REG_ADDR,
		[BQ27XXX_DM_CLASS] = INVALID_REG_ADDR,
		[BQ27XXX_DM_BLOCK] = INVALID_REG_ADDR,
		[BQ27XXX_DM_DATA] = INVALID_REG_ADDR,
		[BQ27XXX_DM_CKSUM] = INVALID_REG_ADDR,
		[BQ27XXX_REG_SEDVF] = 0x77,
		[BQ27XXX_REG_PKCFG] = 0x7C,
	},
	bq2750x_regs[BQ27XXX_REG_MAX] = {
		[BQ27XXX_REG_CTRL] = 0x00,
		[BQ27XXX_REG_TEMP] = 0x06,
		[BQ27XXX_REG_INT_TEMP] = 0x28,
		[BQ27XXX_REG_VOLT] = 0x08,
		[BQ27XXX_REG_AI] = 0x14,
		[BQ27XXX_REG_FLAGS] = 0x0a,
		[BQ27XXX_REG_TTE] = 0x16,
		[BQ27XXX_REG_TTF] = INVALID_REG_ADDR,
		[BQ27XXX_REG_TTES] = 0x1a,
		[BQ27XXX_REG_TTECP] = INVALID_REG_ADDR,
		[BQ27XXX_REG_NAC] = 0x0c,
		[BQ27XXX_REG_RC] = 0x10,
		[BQ27XXX_REG_FCC] = 0x12,
		[BQ27XXX_REG_CYCT] = 0x2a,
		[BQ27XXX_REG_AE] = INVALID_REG_ADDR,
		[BQ27XXX_REG_SOC] = 0x2c,
		[BQ27XXX_REG_DCAP] = 0x3c,
		[BQ27XXX_REG_AP] = INVALID_REG_ADDR,
		BQ27XXX_DM_REG_ROWS,
	},
#define bq2751x_regs bq27510g3_regs
#define bq2752x_regs bq27510g3_regs
	bq27500_regs[BQ27XXX_REG_MAX] = {
		[BQ27XXX_REG_CTRL] = 0x00,
		[BQ27XXX_REG_TEMP] = 0x06,
		[BQ27XXX_REG_INT_TEMP] = INVALID_REG_ADDR,
		[BQ27XXX_REG_VOLT] = 0x08,
		[BQ27XXX_REG_AI] = 0x14,
		[BQ27XXX_REG_FLAGS] = 0x0a,
		[BQ27XXX_REG_TTE] = 0x16,
		[BQ27XXX_REG_TTF] = 0x18,
		[BQ27XXX_REG_TTES] = 0x1c,
		[BQ27XXX_REG_TTECP] = 0x26,
		[BQ27XXX_REG_NAC] = 0x0c,
		[BQ27XXX_REG_RC] = 0x10,
		[BQ27XXX_REG_FCC] = 0x12,
		[BQ27XXX_REG_CYCT] = 0x2a,
		[BQ27XXX_REG_AE] = 0x22,
		[BQ27XXX_REG_SOC] = 0x2c,
		[BQ27XXX_REG_DCAP] = 0x3c,
		[BQ27XXX_REG_AP] = 0x24,
		BQ27XXX_DM_REG_ROWS,
	},
#define bq27510g1_regs bq27500_regs
#define bq27510g2_regs bq27500_regs
	bq27510g3_regs[BQ27XXX_REG_MAX] = {
		[BQ27XXX_REG_CTRL] = 0x00,
		[BQ27XXX_REG_TEMP] = 0x06,
		[BQ27XXX_REG_INT_TEMP] = 0x28,
		[BQ27XXX_REG_VOLT] = 0x08,
		[BQ27XXX_REG_AI] = 0x14,
		[BQ27XXX_REG_FLAGS] = 0x0a,
		[BQ27XXX_REG_TTE] = 0x16,
		[BQ27XXX_REG_TTF] = INVALID_REG_ADDR,
		[BQ27XXX_REG_TTES] = 0x1a,
		[BQ27XXX_REG_TTECP] = INVALID_REG_ADDR,
		[BQ27XXX_REG_NAC] = 0x0c,
		[BQ27XXX_REG_RC] = 0x10,
		[BQ27XXX_REG_FCC] = 0x12,
		[BQ27XXX_REG_CYCT] = 0x1e,
		[BQ27XXX_REG_AE] = INVALID_REG_ADDR,
		[BQ27XXX_REG_SOC] = 0x20,
		[BQ27XXX_REG_DCAP] = 0x2e,
		[BQ27XXX_REG_AP] = INVALID_REG_ADDR,
		BQ27XXX_DM_REG_ROWS,
	},
	bq27520g1_regs[BQ27XXX_REG_MAX] = {
		[BQ27XXX_REG_CTRL] = 0x00,
		[BQ27XXX_REG_TEMP] = 0x06,
		[BQ27XXX_REG_INT_TEMP] = INVALID_REG_ADDR,
		[BQ27XXX_REG_VOLT] = 0x08,
		[BQ27XXX_REG_AI] = 0x14,
		[BQ27XXX_REG_FLAGS] = 0x0a,
		[BQ27XXX_REG_TTE] = 0x16,
		[BQ27XXX_REG_TTF] = 0x18,
		[BQ27XXX_REG_TTES] = 0x1c,
		[BQ27XXX_REG_TTECP] = 0x26,
		[BQ27XXX_REG_NAC] = 0x0c,
		[BQ27XXX_REG_RC] = 0x10,
		[BQ27XXX_REG_FCC] = 0x12,
		[BQ27XXX_REG_CYCT] = INVALID_REG_ADDR,
		[BQ27XXX_REG_AE] = 0x22,
		[BQ27XXX_REG_SOC] = 0x2c,
		[BQ27XXX_REG_DCAP] = 0x3c,
		[BQ27XXX_REG_AP] = 0x24,
		BQ27XXX_DM_REG_ROWS,
	},
	bq27520g2_regs[BQ27XXX_REG_MAX] = {
		[BQ27XXX_REG_CTRL] = 0x00,
		[BQ27XXX_REG_TEMP] = 0x06,
		[BQ27XXX_REG_INT_TEMP] = 0x36,
		[BQ27XXX_REG_VOLT] = 0x08,
		[BQ27XXX_REG_AI] = 0x14,
		[BQ27XXX_REG_FLAGS] = 0x0a,
		[BQ27XXX_REG_TTE] = 0x16,
		[BQ27XXX_REG_TTF] = 0x18,
		[BQ27XXX_REG_TTES] = 0x1c,
		[BQ27XXX_REG_TTECP] = 0x26,
		[BQ27XXX_REG_NAC] = 0x0c,
		[BQ27XXX_REG_RC] = 0x10,
		[BQ27XXX_REG_FCC] = 0x12,
		[BQ27XXX_REG_CYCT] = 0x2a,
		[BQ27XXX_REG_AE] = 0x22,
		[BQ27XXX_REG_SOC] = 0x2c,
		[BQ27XXX_REG_DCAP] = 0x3c,
		[BQ27XXX_REG_AP] = 0x24,
		BQ27XXX_DM_REG_ROWS,
	},
	bq27520g3_regs[BQ27XXX_REG_MAX] = {
		[BQ27XXX_REG_CTRL] = 0x00,
		[BQ27XXX_REG_TEMP] = 0x06,
		[BQ27XXX_REG_INT_TEMP] = 0x36,
		[BQ27XXX_REG_VOLT] = 0x08,
		[BQ27XXX_REG_AI] = 0x14,
		[BQ27XXX_REG_FLAGS] = 0x0a,
		[BQ27XXX_REG_TTE] = 0x16,
		[BQ27XXX_REG_TTF] = INVALID_REG_ADDR,
		[BQ27XXX_REG_TTES] = 0x1c,
		[BQ27XXX_REG_TTECP] = 0x26,
		[BQ27XXX_REG_NAC] = 0x0c,
		[BQ27XXX_REG_RC] = 0x10,
		[BQ27XXX_REG_FCC] = 0x12,
		[BQ27XXX_REG_CYCT] = 0x2a,
		[BQ27XXX_REG_AE] = 0x22,
		[BQ27XXX_REG_SOC] = 0x2c,
		[BQ27XXX_REG_DCAP] = 0x3c,
		[BQ27XXX_REG_AP] = 0x24,
		BQ27XXX_DM_REG_ROWS,
	},
	bq27520g4_regs[BQ27XXX_REG_MAX] = {
		[BQ27XXX_REG_CTRL] = 0x00,
		[BQ27XXX_REG_TEMP] = 0x06,
		[BQ27XXX_REG_INT_TEMP] = 0x28,
		[BQ27XXX_REG_VOLT] = 0x08,
		[BQ27XXX_REG_AI] = 0x14,
		[BQ27XXX_REG_FLAGS] = 0x0a,
		[BQ27XXX_REG_TTE] = 0x16,
		[BQ27XXX_REG_TTF] = INVALID_REG_ADDR,
		[BQ27XXX_REG_TTES] = 0x1c,
		[BQ27XXX_REG_TTECP] = INVALID_REG_ADDR,
		[BQ27XXX_REG_NAC] = 0x0c,
		[BQ27XXX_REG_RC] = 0x10,
		[BQ27XXX_REG_FCC] = 0x12,
		[BQ27XXX_REG_CYCT] = 0x1e,
		[BQ27XXX_REG_AE] = INVALID_REG_ADDR,
		[BQ27XXX_REG_SOC] = 0x20,
		[BQ27XXX_REG_DCAP] = INVALID_REG_ADDR,
		[BQ27XXX_REG_AP] = INVALID_REG_ADDR,
		BQ27XXX_DM_REG_ROWS,
	},
	bq27521_regs[BQ27XXX_REG_MAX] = {
		[BQ27XXX_REG_CTRL] = 0x02,
		[BQ27XXX_REG_TEMP] = 0x0a,
		[BQ27XXX_REG_INT_TEMP] = INVALID_REG_ADDR,
		[BQ27XXX_REG_VOLT] = 0x0c,
		[BQ27XXX_REG_AI] = 0x0e,
		[BQ27XXX_REG_FLAGS] = 0x08,
		[BQ27XXX_REG_TTE] = INVALID_REG_ADDR,
		[BQ27XXX_REG_TTF] = INVALID_REG_ADDR,
		[BQ27XXX_REG_TTES] = INVALID_REG_ADDR,
		[BQ27XXX_REG_TTECP] = INVALID_REG_ADDR,
		[BQ27XXX_REG_NAC] = INVALID_REG_ADDR,
		[BQ27XXX_REG_RC] = INVALID_REG_ADDR,
		[BQ27XXX_REG_FCC] = INVALID_REG_ADDR,
		[BQ27XXX_REG_CYCT] = INVALID_REG_ADDR,
		[BQ27XXX_REG_AE] = INVALID_REG_ADDR,
		[BQ27XXX_REG_SOC] = INVALID_REG_ADDR,
		[BQ27XXX_REG_DCAP] = INVALID_REG_ADDR,
		[BQ27XXX_REG_AP] = INVALID_REG_ADDR,
		[BQ27XXX_DM_CTRL] = INVALID_REG_ADDR,
		[BQ27XXX_DM_CLASS] = INVALID_REG_ADDR,
		[BQ27XXX_DM_BLOCK] = INVALID_REG_ADDR,
		[BQ27XXX_DM_DATA] = INVALID_REG_ADDR,
		[BQ27XXX_DM_CKSUM] = INVALID_REG_ADDR,
	},
	bq27530_regs[BQ27XXX_REG_MAX] = {
		[BQ27XXX_REG_CTRL] = 0x00,
		[BQ27XXX_REG_TEMP] = 0x06,
		[BQ27XXX_REG_INT_TEMP] = 0x32,
		[BQ27XXX_REG_VOLT] = 0x08,
		[BQ27XXX_REG_AI] = 0x14,
		[BQ27XXX_REG_FLAGS] = 0x0a,
		[BQ27XXX_REG_TTE] = 0x16,
		[BQ27XXX_REG_TTF] = INVALID_REG_ADDR,
		[BQ27XXX_REG_TTES] = INVALID_REG_ADDR,
		[BQ27XXX_REG_TTECP] = INVALID_REG_ADDR,
		[BQ27XXX_REG_NAC] = 0x0c,
		[BQ27XXX_REG_RC] = 0x10,
		[BQ27XXX_REG_FCC] = 0x12,
		[BQ27XXX_REG_CYCT] = 0x2a,
		[BQ27XXX_REG_AE] = INVALID_REG_ADDR,
		[BQ27XXX_REG_SOC] = 0x2c,
		[BQ27XXX_REG_DCAP] = INVALID_REG_ADDR,
		[BQ27XXX_REG_AP] = 0x24,
		BQ27XXX_DM_REG_ROWS,
	},
#define bq27531_regs bq27530_regs
	bq27541_regs[BQ27XXX_REG_MAX] = {
		[BQ27XXX_REG_CTRL] = 0x00,
		[BQ27XXX_REG_TEMP] = 0x06,
		[BQ27XXX_REG_INT_TEMP] = 0x28,
		[BQ27XXX_REG_VOLT] = 0x08,
		[BQ27XXX_REG_AI] = 0x14,
		[BQ27XXX_REG_FLAGS] = 0x0a,
		[BQ27XXX_REG_TTE] = 0x16,
		[BQ27XXX_REG_TTF] = INVALID_REG_ADDR,
		[BQ27XXX_REG_TTES] = INVALID_REG_ADDR,
		[BQ27XXX_REG_TTECP] = INVALID_REG_ADDR,
		[BQ27XXX_REG_NAC] = 0x0c,
		[BQ27XXX_REG_RC] = 0x10,
		[BQ27XXX_REG_FCC] = 0x12,
		[BQ27XXX_REG_CYCT] = 0x2a,
		[BQ27XXX_REG_AE] = INVALID_REG_ADDR,
		[BQ27XXX_REG_SOC] = 0x2c,
		[BQ27XXX_REG_DCAP] = 0x3c,
		[BQ27XXX_REG_AP] = 0x24,
		BQ27XXX_DM_REG_ROWS,
	},
#define bq27542_regs bq27541_regs
#define bq27546_regs bq27541_regs
#define bq27742_regs bq27541_regs
	bq27545_regs[BQ27XXX_REG_MAX] = {
		[BQ27XXX_REG_CTRL] = 0x00,
		[BQ27XXX_REG_TEMP] = 0x06,
		[BQ27XXX_REG_INT_TEMP] = 0x28,
		[BQ27XXX_REG_VOLT] = 0x08,
		[BQ27XXX_REG_AI] = 0x14,
		[BQ27XXX_REG_FLAGS] = 0x0a,
		[BQ27XXX_REG_TTE] = 0x16,
		[BQ27XXX_REG_TTF] = INVALID_REG_ADDR,
		[BQ27XXX_REG_TTES] = INVALID_REG_ADDR,
		[BQ27XXX_REG_TTECP] = INVALID_REG_ADDR,
		[BQ27XXX_REG_NAC] = 0x0c,
		[BQ27XXX_REG_RC] = 0x10,
		[BQ27XXX_REG_FCC] = 0x12,
		[BQ27XXX_REG_CYCT] = 0x2a,
		[BQ27XXX_REG_AE] = INVALID_REG_ADDR,
		[BQ27XXX_REG_SOC] = 0x2c,
		[BQ27XXX_REG_DCAP] = INVALID_REG_ADDR,
		[BQ27XXX_REG_AP] = 0x24,
		BQ27XXX_DM_REG_ROWS,
	},
	bq27421_regs[BQ27XXX_REG_MAX] = {
		[BQ27XXX_REG_CTRL] = 0x00,
		[BQ27XXX_REG_TEMP] = 0x02,
		[BQ27XXX_REG_INT_TEMP] = 0x1e,
		[BQ27XXX_REG_VOLT] = 0x04,
		[BQ27XXX_REG_AI] = 0x10,
		[BQ27XXX_REG_FLAGS] = 0x06,
		[BQ27XXX_REG_TTE] = INVALID_REG_ADDR,
		[BQ27XXX_REG_TTF] = INVALID_REG_ADDR,
		[BQ27XXX_REG_TTES] = INVALID_REG_ADDR,
		[BQ27XXX_REG_TTECP] = INVALID_REG_ADDR,
		[BQ27XXX_REG_NAC] = 0x08,
		[BQ27XXX_REG_RC] = 0x0c,
		[BQ27XXX_REG_FCC] = 0x0e,
		[BQ27XXX_REG_CYCT] = INVALID_REG_ADDR,
		[BQ27XXX_REG_AE] = INVALID_REG_ADDR,
		[BQ27XXX_REG_SOC] = 0x1c,
		[BQ27XXX_REG_DCAP] = 0x3c,
		[BQ27XXX_REG_AP] = 0x18,
		BQ27XXX_DM_REG_ROWS,
	},
	bq27426_regs[BQ27XXX_REG_MAX] = {
		[BQ27XXX_REG_CTRL] = 0x00,
		[BQ27XXX_REG_TEMP] = 0x02,
		[BQ27XXX_REG_INT_TEMP] = 0x1e,
		[BQ27XXX_REG_VOLT] = 0x04,
		[BQ27XXX_REG_AI] = 0x10,
		[BQ27XXX_REG_FLAGS] = 0x06,
		[BQ27XXX_REG_TTE] = INVALID_REG_ADDR,
		[BQ27XXX_REG_TTF] = INVALID_REG_ADDR,
		[BQ27XXX_REG_TTES] = INVALID_REG_ADDR,
		[BQ27XXX_REG_TTECP] = INVALID_REG_ADDR,
		[BQ27XXX_REG_NAC] = 0x08,
		[BQ27XXX_REG_RC] = 0x0c,
		[BQ27XXX_REG_FCC] = 0x0e,
		[BQ27XXX_REG_CYCT] = INVALID_REG_ADDR,
		[BQ27XXX_REG_AE] = INVALID_REG_ADDR,
		[BQ27XXX_REG_SOC] = 0x1c,
		[BQ27XXX_REG_DCAP] = INVALID_REG_ADDR,
		[BQ27XXX_REG_AP] = 0x18,
		BQ27XXX_DM_REG_ROWS,
	},
#define bq27411_regs bq27421_regs
#define bq27425_regs bq27421_regs
#define bq27441_regs bq27421_regs
#define bq27621_regs bq27421_regs
	bq27z561_regs[BQ27XXX_REG_MAX] = {
		[BQ27XXX_REG_CTRL] = 0x00,
		[BQ27XXX_REG_TEMP] = 0x06,
		[BQ27XXX_REG_INT_TEMP] = INVALID_REG_ADDR,
		[BQ27XXX_REG_VOLT] = 0x08,
		[BQ27XXX_REG_AI] = 0x14,
		[BQ27XXX_REG_FLAGS] = 0x0a,
		[BQ27XXX_REG_TTE] = 0x16,
		[BQ27XXX_REG_TTF] = 0x18,
		[BQ27XXX_REG_TTES] = INVALID_REG_ADDR,
		[BQ27XXX_REG_TTECP] = INVALID_REG_ADDR,
		[BQ27XXX_REG_NAC] = INVALID_REG_ADDR,
		[BQ27XXX_REG_RC] = 0x10,
		[BQ27XXX_REG_FCC] = 0x12,
		[BQ27XXX_REG_CYCT] = 0x2a,
		[BQ27XXX_REG_AE] = 0x22,
		[BQ27XXX_REG_SOC] = 0x2c,
		[BQ27XXX_REG_DCAP] = 0x3c,
		[BQ27XXX_REG_AP] = 0x22,
		BQ27XXX_DM_REG_ROWS,
	},
	bq28z610_regs[BQ27XXX_REG_MAX] = {
		[BQ27XXX_REG_CTRL] = 0x00,
		[BQ27XXX_REG_TEMP] = 0x06,
		[BQ27XXX_REG_INT_TEMP] = INVALID_REG_ADDR,
		[BQ27XXX_REG_VOLT] = 0x08,
		[BQ27XXX_REG_AI] = 0x14,
		[BQ27XXX_REG_FLAGS] = 0x0a,
		[BQ27XXX_REG_TTE] = 0x16,
		[BQ27XXX_REG_TTF] = 0x18,
		[BQ27XXX_REG_TTES] = INVALID_REG_ADDR,
		[BQ27XXX_REG_TTECP] = INVALID_REG_ADDR,
		[BQ27XXX_REG_NAC] = INVALID_REG_ADDR,
		[BQ27XXX_REG_RC] = 0x10,
		[BQ27XXX_REG_FCC] = 0x12,
		[BQ27XXX_REG_CYCT] = 0x2a,
		[BQ27XXX_REG_AE] = 0x22,
		[BQ27XXX_REG_SOC] = 0x2c,
		[BQ27XXX_REG_DCAP] = 0x3c,
		[BQ27XXX_REG_AP] = 0x22,
		BQ27XXX_DM_REG_ROWS,
	},
	bq34z100_regs[BQ27XXX_REG_MAX] = {
		[BQ27XXX_REG_CTRL] = 0x00,
		[BQ27XXX_REG_TEMP] = 0x0c,
		[BQ27XXX_REG_INT_TEMP] = 0x2a,
		[BQ27XXX_REG_VOLT] = 0x08,
		[BQ27XXX_REG_AI] = 0x0a,
		[BQ27XXX_REG_FLAGS] = 0x0e,
		[BQ27XXX_REG_TTE] = 0x18,
		[BQ27XXX_REG_TTF] = 0x1a,
		[BQ27XXX_REG_TTES] = 0x1e,
		[BQ27XXX_REG_TTECP] = INVALID_REG_ADDR,
		[BQ27XXX_REG_NAC] = INVALID_REG_ADDR,
		[BQ27XXX_REG_RC] = 0x04,
		[BQ27XXX_REG_FCC] = 0x06,
		[BQ27XXX_REG_CYCT] = 0x2c,
		[BQ27XXX_REG_AE] = 0x24,
		[BQ27XXX_REG_SOC] = 0x02,
		[BQ27XXX_REG_DCAP] = 0x3c,
		[BQ27XXX_REG_AP] = 0x22,
		BQ27XXX_DM_REG_ROWS,
	},
	bq78z100_regs[BQ27XXX_REG_MAX] = {
		[BQ27XXX_REG_CTRL] = 0x00,
		[BQ27XXX_REG_TEMP] = 0x06,
		[BQ27XXX_REG_INT_TEMP] = 0x28,
		[BQ27XXX_REG_VOLT] = 0x08,
		[BQ27XXX_REG_AI] = 0x14,
		[BQ27XXX_REG_FLAGS] = 0x0a,
		[BQ27XXX_REG_TTE] = 0x16,
		[BQ27XXX_REG_TTF] = 0x18,
		[BQ27XXX_REG_TTES] = 0x1c,
		[BQ27XXX_REG_TTECP] = INVALID_REG_ADDR,
		[BQ27XXX_REG_NAC] = INVALID_REG_ADDR,
		[BQ27XXX_REG_RC] = 0x10,
		[BQ27XXX_REG_FCC] = 0x12,
		[BQ27XXX_REG_CYCT] = 0x2a,
		[BQ27XXX_REG_AE] = INVALID_REG_ADDR,
		[BQ27XXX_REG_SOC] = 0x2c,
		[BQ27XXX_REG_DCAP] = 0x3c,
		[BQ27XXX_REG_AP] = 0x22,
		BQ27XXX_DM_REG_ROWS,
	};

static enum power_supply_property bq27000_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_ENERGY_NOW,
	POWER_SUPPLY_PROP_POWER_AVG,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
};

static enum power_supply_property bq27010_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
};

#define bq2750x_props bq27510g3_props
#define bq2751x_props bq27510g3_props
#define bq2752x_props bq27510g3_props

static enum power_supply_property bq27500_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_ENERGY_NOW,
	POWER_SUPPLY_PROP_POWER_AVG,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_MANUFACTURER,
};
#define bq27510g1_props bq27500_props
#define bq27510g2_props bq27500_props

static enum power_supply_property bq27510g3_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_MANUFACTURER,
};

static enum power_supply_property bq27520g1_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_ENERGY_NOW,
	POWER_SUPPLY_PROP_POWER_AVG,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_MANUFACTURER,
};

#define bq27520g2_props bq27500_props

static enum power_supply_property bq27520g3_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_ENERGY_NOW,
	POWER_SUPPLY_PROP_POWER_AVG,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_MANUFACTURER,
};

static enum power_supply_property bq27520g4_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_MANUFACTURER,
};

static enum power_supply_property bq27521_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TECHNOLOGY,
};

static enum power_supply_property bq27530_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_POWER_AVG,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_MANUFACTURER,
};
#define bq27531_props bq27530_props

static enum power_supply_property bq27541_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_POWER_AVG,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_MANUFACTURER,
};
#define bq27542_props bq27541_props
#define bq27546_props bq27541_props
#define bq27742_props bq27541_props

static enum power_supply_property bq27545_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_POWER_AVG,
	POWER_SUPPLY_PROP_MANUFACTURER,
};

static enum power_supply_property bq27421_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_MANUFACTURER,
};
#define bq27411_props bq27421_props
#define bq27425_props bq27421_props
#define bq27441_props bq27421_props
#define bq27621_props bq27421_props

static enum power_supply_property bq27426_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_MANUFACTURER,
};

static enum power_supply_property bq27z561_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_POWER_AVG,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_MANUFACTURER,
};

static enum power_supply_property bq28z610_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_POWER_AVG,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_MANUFACTURER,
};

static enum power_supply_property bq34z100_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_ENERGY_NOW,
	POWER_SUPPLY_PROP_POWER_AVG,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_MANUFACTURER,
};

static enum power_supply_property bq78z100_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_POWER_AVG,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_MANUFACTURER,
};

struct bq27xxx_dm_reg {
	u8 subclass_id;
	u8 offset;
	u8 bytes;
	u16 min, max;
};

enum bq27xxx_dm_reg_id {
	BQ27XXX_DM_DESIGN_CAPACITY = 0,
	BQ27XXX_DM_DESIGN_ENERGY,
	BQ27XXX_DM_TERMINATE_VOLTAGE,
};

#define bq27000_dm_regs NULL
#define bq27010_dm_regs NULL
#define bq2750x_dm_regs NULL
#define bq2751x_dm_regs NULL
#define bq2752x_dm_regs NULL

#if 0 /* not yet tested */
static struct bq27xxx_dm_reg bq27500_dm_regs[] = {
	[BQ27XXX_DM_DESIGN_CAPACITY]   = { 48, 10, 2,    0, 65535 },
	[BQ27XXX_DM_DESIGN_ENERGY]     = { }, /* missing on chip */
	[BQ27XXX_DM_TERMINATE_VOLTAGE] = { 80, 48, 2, 1000, 32767 },
};
#else
#define bq27500_dm_regs NULL
#endif

/* todo create data memory definitions from datasheets and test on chips */
#define bq27510g1_dm_regs NULL
#define bq27510g2_dm_regs NULL
#define bq27510g3_dm_regs NULL
#define bq27520g1_dm_regs NULL
#define bq27520g2_dm_regs NULL
#define bq27520g3_dm_regs NULL
#define bq27520g4_dm_regs NULL
#define bq27521_dm_regs NULL
#define bq27530_dm_regs NULL
#define bq27531_dm_regs NULL
#define bq27541_dm_regs NULL
#define bq27542_dm_regs NULL
#define bq27546_dm_regs NULL
#define bq27742_dm_regs NULL

#if 0 /* not yet tested */
static struct bq27xxx_dm_reg bq27545_dm_regs[] = {
	[BQ27XXX_DM_DESIGN_CAPACITY]   = { 48, 23, 2,    0, 32767 },
	[BQ27XXX_DM_DESIGN_ENERGY]     = { 48, 25, 2,    0, 32767 },
	[BQ27XXX_DM_TERMINATE_VOLTAGE] = { 80, 67, 2, 2800,  3700 },
};
#else
#define bq27545_dm_regs NULL
#endif

static struct bq27xxx_dm_reg bq27411_dm_regs[] = {
	[BQ27XXX_DM_DESIGN_CAPACITY]   = { 82, 10, 2,    0, 32767 },
	[BQ27XXX_DM_DESIGN_ENERGY]     = { 82, 12, 2,    0, 32767 },
	[BQ27XXX_DM_TERMINATE_VOLTAGE] = { 82, 16, 2, 2800,  3700 },
};

static struct bq27xxx_dm_reg bq27421_dm_regs[] = {
	[BQ27XXX_DM_DESIGN_CAPACITY]   = { 82, 10, 2,    0,  8000 },
	[BQ27XXX_DM_DESIGN_ENERGY]     = { 82, 12, 2,    0, 32767 },
	[BQ27XXX_DM_TERMINATE_VOLTAGE] = { 82, 16, 2, 2500,  3700 },
};

static struct bq27xxx_dm_reg bq27425_dm_regs[] = {
	[BQ27XXX_DM_DESIGN_CAPACITY]   = { 82, 12, 2,    0, 32767 },
	[BQ27XXX_DM_DESIGN_ENERGY]     = { 82, 14, 2,    0, 32767 },
	[BQ27XXX_DM_TERMINATE_VOLTAGE] = { 82, 18, 2, 2800,  3700 },
};

static struct bq27xxx_dm_reg bq27426_dm_regs[] = {
	[BQ27XXX_DM_DESIGN_CAPACITY]   = { 82,  6, 2,    0,  8000 },
	[BQ27XXX_DM_DESIGN_ENERGY]     = { 82,  8, 2,    0, 32767 },
	[BQ27XXX_DM_TERMINATE_VOLTAGE] = { 82, 10, 2, 2500,  3700 },
};

#if 0 /* not yet tested */
#define bq27441_dm_regs bq27421_dm_regs
#else
#define bq27441_dm_regs NULL
#endif

#if 0 /* not yet tested */
static struct bq27xxx_dm_reg bq27621_dm_regs[] = {
	[BQ27XXX_DM_DESIGN_CAPACITY]   = { 82, 3, 2,    0,  8000 },
	[BQ27XXX_DM_DESIGN_ENERGY]     = { 82, 5, 2,    0, 32767 },
	[BQ27XXX_DM_TERMINATE_VOLTAGE] = { 82, 9, 2, 2500,  3700 },
};
#else
#define bq27621_dm_regs NULL
#endif

#define bq27z561_dm_regs NULL
#define bq28z610_dm_regs NULL
#define bq34z100_dm_regs NULL
#define bq78z100_dm_regs NULL

#define BQ27XXX_O_ZERO		BIT(0)
#define BQ27XXX_O_OTDC		BIT(1) /* has OTC/OTD overtemperature flags */
#define BQ27XXX_O_UTOT		BIT(2) /* has OT overtemperature flag */
#define BQ27XXX_O_CFGUP		BIT(3)
#define BQ27XXX_O_RAM		BIT(4)
#define BQ27Z561_O_BITS		BIT(5)
#define BQ27XXX_O_SOC_SI	BIT(6) /* SoC is single register */
#define BQ27XXX_O_HAS_CI	BIT(7) /* has Capacity Inaccurate flag */
#define BQ27XXX_O_MUL_CHEM	BIT(8) /* multiple chemistries supported */

#define BQ27XXX_DATA(ref, key, opt) {		\
	.opts = (opt),				\
	.unseal_key = key,			\
	.regs  = ref##_regs,			\
	.dm_regs = ref##_dm_regs,		\
	.props = ref##_props,			\
	.props_size = ARRAY_SIZE(ref##_props) }

static struct {
	u32 opts;
	u32 unseal_key;
	u8 *regs;
	struct bq27xxx_dm_reg *dm_regs;
	enum power_supply_property *props;
	size_t props_size;
} bq27xxx_chip_data[] = {
	[BQ27000]   = BQ27XXX_DATA(bq27000,   0         , BQ27XXX_O_ZERO | BQ27XXX_O_SOC_SI | BQ27XXX_O_HAS_CI),
	[BQ27010]   = BQ27XXX_DATA(bq27010,   0         , BQ27XXX_O_ZERO | BQ27XXX_O_SOC_SI | BQ27XXX_O_HAS_CI),
	[BQ2750X]   = BQ27XXX_DATA(bq2750x,   0         , BQ27XXX_O_OTDC),
	[BQ2751X]   = BQ27XXX_DATA(bq2751x,   0         , BQ27XXX_O_OTDC),
	[BQ2752X]   = BQ27XXX_DATA(bq2752x,   0         , BQ27XXX_O_OTDC),
	[BQ27500]   = BQ27XXX_DATA(bq27500,   0x04143672, BQ27XXX_O_OTDC),
	[BQ27510G1] = BQ27XXX_DATA(bq27510g1, 0         , BQ27XXX_O_OTDC),
	[BQ27510G2] = BQ27XXX_DATA(bq27510g2, 0         , BQ27XXX_O_OTDC),
	[BQ27510G3] = BQ27XXX_DATA(bq27510g3, 0         , BQ27XXX_O_OTDC),
	[BQ27520G1] = BQ27XXX_DATA(bq27520g1, 0         , BQ27XXX_O_OTDC),
	[BQ27520G2] = BQ27XXX_DATA(bq27520g2, 0         , BQ27XXX_O_OTDC),
	[BQ27520G3] = BQ27XXX_DATA(bq27520g3, 0         , BQ27XXX_O_OTDC),
	[BQ27520G4] = BQ27XXX_DATA(bq27520g4, 0         , BQ27XXX_O_OTDC),
	[BQ27521]   = BQ27XXX_DATA(bq27521,   0         , 0),
	[BQ27530]   = BQ27XXX_DATA(bq27530,   0         , BQ27XXX_O_UTOT),
	[BQ27531]   = BQ27XXX_DATA(bq27531,   0         , BQ27XXX_O_UTOT),
	[BQ27541]   = BQ27XXX_DATA(bq27541,   0         , BQ27XXX_O_OTDC),
	[BQ27542]   = BQ27XXX_DATA(bq27542,   0         , BQ27XXX_O_OTDC),
	[BQ27546]   = BQ27XXX_DATA(bq27546,   0         , BQ27XXX_O_OTDC),
	[BQ27742]   = BQ27XXX_DATA(bq27742,   0         , BQ27XXX_O_OTDC),
	[BQ27545]   = BQ27XXX_DATA(bq27545,   0x04143672, BQ27XXX_O_OTDC),
	[BQ27411]   = BQ27XXX_DATA(bq27411,   0x80008000, BQ27XXX_O_UTOT | BQ27XXX_O_CFGUP | BQ27XXX_O_RAM),
	[BQ27421]   = BQ27XXX_DATA(bq27421,   0x80008000, BQ27XXX_O_UTOT | BQ27XXX_O_CFGUP | BQ27XXX_O_RAM),
	[BQ27425]   = BQ27XXX_DATA(bq27425,   0x04143672, BQ27XXX_O_UTOT | BQ27XXX_O_CFGUP),
	[BQ27426]   = BQ27XXX_DATA(bq27426,   0x80008000, BQ27XXX_O_UTOT | BQ27XXX_O_CFGUP | BQ27XXX_O_RAM),
	[BQ27441]   = BQ27XXX_DATA(bq27441,   0x80008000, BQ27XXX_O_UTOT | BQ27XXX_O_CFGUP | BQ27XXX_O_RAM),
	[BQ27621]   = BQ27XXX_DATA(bq27621,   0x80008000, BQ27XXX_O_UTOT | BQ27XXX_O_CFGUP | BQ27XXX_O_RAM),
	[BQ27Z561]  = BQ27XXX_DATA(bq27z561,  0         , BQ27Z561_O_BITS),
	[BQ28Z610]  = BQ27XXX_DATA(bq28z610,  0         , BQ27Z561_O_BITS),
	[BQ34Z100]  = BQ27XXX_DATA(bq34z100,  0         , BQ27XXX_O_OTDC | BQ27XXX_O_SOC_SI | \
							  BQ27XXX_O_HAS_CI | BQ27XXX_O_MUL_CHEM),
	[BQ78Z100]  = BQ27XXX_DATA(bq78z100,  0         , BQ27Z561_O_BITS),
};

static DEFINE_MUTEX(bq27xxx_list_lock);
static LIST_HEAD(bq27xxx_battery_devices);

#define BQ27XXX_MSLEEP(i) usleep_range((i)*1000, (i)*1000+500)

#define BQ27XXX_DM_SZ	32

/**
 * struct bq27xxx_dm_buf - chip data memory buffer
 * @class: data memory subclass_id
 * @block: data memory block number
 * @data: data from/for the block
 * @has_data: true if data has been filled by read
 * @dirty: true if data has changed since last read/write
 *
 * Encapsulates info required to manage chip data memory blocks.
 */
struct bq27xxx_dm_buf {
	u8 class;
	u8 block;
	u8 data[BQ27XXX_DM_SZ];
	bool has_data, dirty;
};

#define BQ27XXX_DM_BUF(di, i) { \
	.class = (di)->dm_regs[i].subclass_id, \
	.block = (di)->dm_regs[i].offset / BQ27XXX_DM_SZ, \
}

static inline __be16 *bq27xxx_dm_reg_ptr(struct bq27xxx_dm_buf *buf,
				      struct bq27xxx_dm_reg *reg)
{
	if (buf->class == reg->subclass_id &&
	    buf->block == reg->offset / BQ27XXX_DM_SZ)
		return (__be16 *) (buf->data + reg->offset % BQ27XXX_DM_SZ);

	return NULL;
}

static const char * const bq27xxx_dm_reg_name[] = {
	[BQ27XXX_DM_DESIGN_CAPACITY] = "design-capacity",
	[BQ27XXX_DM_DESIGN_ENERGY] = "design-energy",
	[BQ27XXX_DM_TERMINATE_VOLTAGE] = "terminate-voltage",
};


static bool bq27xxx_dt_to_nvm = true;
module_param_named(dt_monitored_battery_updates_nvm, bq27xxx_dt_to_nvm, bool, 0444);
MODULE_PARM_DESC(dt_monitored_battery_updates_nvm,
	"Devicetree monitored-battery config updates data memory on NVM/flash chips.\n"
	"Users must set this =0 when installing a different type of battery!\n"
	"Default is =1."
#ifndef CONFIG_BATTERY_BQ27XXX_DT_UPDATES_NVM
	"\nSetting this affects future kernel updates, not the current configuration."
#endif
);

static int poll_interval_param_set(const char *val, const struct kernel_param *kp)
{
	struct bq27xxx_device_info *di;
	unsigned int prev_val = *(unsigned int *) kp->arg;
	int ret;

	ret = param_set_uint(val, kp);
	if (ret < 0 || prev_val == *(unsigned int *) kp->arg)
		return ret;

	mutex_lock(&bq27xxx_list_lock);
	list_for_each_entry(di, &bq27xxx_battery_devices, list)
		mod_delayed_work(system_percpu_wq, &di->work, 0);
	mutex_unlock(&bq27xxx_list_lock);

	return ret;
}

static const struct kernel_param_ops param_ops_poll_interval = {
	.get = param_get_uint,
	.set = poll_interval_param_set,
};

static unsigned int poll_interval = 360;
module_param_cb(poll_interval, &param_ops_poll_interval, &poll_interval, 0644);
MODULE_PARM_DESC(poll_interval,
		 "battery poll interval in seconds - 0 disables polling");

/*
 * Common code for BQ27xxx devices
 */

static inline int bq27xxx_read(struct bq27xxx_device_info *di, int reg_index,
			       bool single)
{
	int ret;

	if (!di || di->regs[reg_index] == INVALID_REG_ADDR)
		return -EINVAL;

	ret = di->bus.read(di, di->regs[reg_index], single);
	if (ret < 0)
		dev_dbg(di->dev, "failed to read register 0x%02x (index %d)\n",
			di->regs[reg_index], reg_index);

	return ret;
}

static inline int bq27xxx_write(struct bq27xxx_device_info *di, int reg_index,
				u16 value, bool single)
{
	int ret;

	if (!di || di->regs[reg_index] == INVALID_REG_ADDR)
		return -EINVAL;

	if (!di->bus.write)
		return -EPERM;

	ret = di->bus.write(di, di->regs[reg_index], value, single);
	if (ret < 0)
		dev_dbg(di->dev, "failed to write register 0x%02x (index %d)\n",
			di->regs[reg_index], reg_index);

	return ret;
}

static inline int bq27xxx_read_block(struct bq27xxx_device_info *di, int reg_index,
				     u8 *data, int len)
{
	int ret;

	if (!di || di->regs[reg_index] == INVALID_REG_ADDR)
		return -EINVAL;

	if (!di->bus.read_bulk)
		return -EPERM;

	ret = di->bus.read_bulk(di, di->regs[reg_index], data, len);
	if (ret < 0)
		dev_dbg(di->dev, "failed to read_bulk register 0x%02x (index %d)\n",
			di->regs[reg_index], reg_index);

	return ret;
}

static inline int bq27xxx_write_block(struct bq27xxx_device_info *di, int reg_index,
				      u8 *data, int len)
{
	int ret;

	if (!di || di->regs[reg_index] == INVALID_REG_ADDR)
		return -EINVAL;

	if (!di->bus.write_bulk)
		return -EPERM;

	ret = di->bus.write_bulk(di, di->regs[reg_index], data, len);
	if (ret < 0)
		dev_dbg(di->dev, "failed to write_bulk register 0x%02x (index %d)\n",
			di->regs[reg_index], reg_index);

	return ret;
}

static int bq27xxx_battery_seal(struct bq27xxx_device_info *di)
{
	int ret;

	ret = bq27xxx_write(di, BQ27XXX_REG_CTRL, BQ27XXX_SEALED, false);
	if (ret < 0) {
		dev_err(di->dev, "bus error on seal: %d\n", ret);
		return ret;
	}

	return 0;
}

static int bq27xxx_battery_unseal(struct bq27xxx_device_info *di)
{
	int ret;

	if (di->unseal_key == 0) {
		dev_err(di->dev, "unseal failed due to missing key\n");
		return -EINVAL;
	}

	ret = bq27xxx_write(di, BQ27XXX_REG_CTRL, (u16)(di->unseal_key >> 16), false);
	if (ret < 0)
		goto out;

	ret = bq27xxx_write(di, BQ27XXX_REG_CTRL, (u16)di->unseal_key, false);
	if (ret < 0)
		goto out;

	return 0;

out:
	dev_err(di->dev, "bus error on unseal: %d\n", ret);
	return ret;
}

static u8 bq27xxx_battery_checksum_dm_block(struct bq27xxx_dm_buf *buf)
{
	u16 sum = 0;
	int i;

	for (i = 0; i < BQ27XXX_DM_SZ; i++)
		sum += buf->data[i];
	sum &= 0xff;

	return 0xff - sum;
}

static int bq27xxx_battery_read_dm_block(struct bq27xxx_device_info *di,
					 struct bq27xxx_dm_buf *buf)
{
	int ret;

	buf->has_data = false;

	ret = bq27xxx_write(di, BQ27XXX_DM_CLASS, buf->class, true);
	if (ret < 0)
		goto out;

	ret = bq27xxx_write(di, BQ27XXX_DM_BLOCK, buf->block, true);
	if (ret < 0)
		goto out;

	BQ27XXX_MSLEEP(1);

	ret = bq27xxx_read_block(di, BQ27XXX_DM_DATA, buf->data, BQ27XXX_DM_SZ);
	if (ret < 0)
		goto out;

	ret = bq27xxx_read(di, BQ27XXX_DM_CKSUM, true);
	if (ret < 0)
		goto out;

	if ((u8)ret != bq27xxx_battery_checksum_dm_block(buf)) {
		ret = -EINVAL;
		goto out;
	}

	buf->has_data = true;
	buf->dirty = false;

	return 0;

out:
	dev_err(di->dev, "bus error reading chip memory: %d\n", ret);
	return ret;
}

static void bq27xxx_battery_update_dm_block(struct bq27xxx_device_info *di,
					    struct bq27xxx_dm_buf *buf,
					    enum bq27xxx_dm_reg_id reg_id,
					    unsigned int val)
{
	struct bq27xxx_dm_reg *reg = &di->dm_regs[reg_id];
	const char *str = bq27xxx_dm_reg_name[reg_id];
	__be16 *prev = bq27xxx_dm_reg_ptr(buf, reg);

	if (prev == NULL) {
		dev_warn(di->dev, "buffer does not match %s dm spec\n", str);
		return;
	}

	if (reg->bytes != 2) {
		dev_warn(di->dev, "%s dm spec has unsupported byte size\n", str);
		return;
	}

	if (!buf->has_data)
		return;

	if (be16_to_cpup(prev) == val) {
		dev_info(di->dev, "%s has %u\n", str, val);
		return;
	}

#ifdef CONFIG_BATTERY_BQ27XXX_DT_UPDATES_NVM
	if (!(di->opts & BQ27XXX_O_RAM) && !bq27xxx_dt_to_nvm) {
#else
	if (!(di->opts & BQ27XXX_O_RAM)) {
#endif
		/* devicetree and NVM differ; defer to NVM */
		dev_warn(di->dev, "%s has %u; update to %u disallowed "
#ifdef CONFIG_BATTERY_BQ27XXX_DT_UPDATES_NVM
			 "by dt_monitored_battery_updates_nvm=0"
#else
			 "for flash/NVM data memory"
#endif
			 "\n", str, be16_to_cpup(prev), val);
		return;
	}

	dev_info(di->dev, "update %s to %u\n", str, val);

	*prev = cpu_to_be16(val);
	buf->dirty = true;
}

static int bq27xxx_battery_cfgupdate_priv(struct bq27xxx_device_info *di, bool active)
{
	const int limit = 100;
	u16 cmd = active ? BQ27XXX_SET_CFGUPDATE : BQ27XXX_SOFT_RESET;
	int ret, try = limit;

	ret = bq27xxx_write(di, BQ27XXX_REG_CTRL, cmd, false);
	if (ret < 0)
		return ret;

	do {
		BQ27XXX_MSLEEP(25);
		ret = bq27xxx_read(di, BQ27XXX_REG_FLAGS, false);
		if (ret < 0)
			return ret;
	} while (!!(ret & BQ27XXX_FLAG_CFGUP) != active && --try);

	if (!try && di->chip != BQ27425) { // 425 has a bug
		dev_err(di->dev, "timed out waiting for cfgupdate flag %d\n", active);
		return -EINVAL;
	}

	if (limit - try > 3)
		dev_warn(di->dev, "cfgupdate %d, retries %d\n", active, limit - try);

	return 0;
}

static inline int bq27xxx_battery_set_cfgupdate(struct bq27xxx_device_info *di)
{
	int ret = bq27xxx_battery_cfgupdate_priv(di, true);
	if (ret < 0 && ret != -EINVAL)
		dev_err(di->dev, "bus error on set_cfgupdate: %d\n", ret);

	return ret;
}

static inline int bq27xxx_battery_soft_reset(struct bq27xxx_device_info *di)
{
	int ret = bq27xxx_battery_cfgupdate_priv(di, false);
	if (ret < 0 && ret != -EINVAL)
		dev_err(di->dev, "bus error on soft_reset: %d\n", ret);

	return ret;
}

static int bq27xxx_battery_write_dm_block(struct bq27xxx_device_info *di,
					  struct bq27xxx_dm_buf *buf)
{
	bool cfgup = di->opts & BQ27XXX_O_CFGUP;
	int ret;

	if (!buf->dirty)
		return 0;

	if (cfgup) {
		ret = bq27xxx_battery_set_cfgupdate(di);
		if (ret < 0)
			return ret;
	}

	ret = bq27xxx_write(di, BQ27XXX_DM_CTRL, 0, true);
	if (ret < 0)
		goto out;

	ret = bq27xxx_write(di, BQ27XXX_DM_CLASS, buf->class, true);
	if (ret < 0)
		goto out;

	ret = bq27xxx_write(di, BQ27XXX_DM_BLOCK, buf->block, true);
	if (ret < 0)
		goto out;

	BQ27XXX_MSLEEP(1);

	ret = bq27xxx_write_block(di, BQ27XXX_DM_DATA, buf->data, BQ27XXX_DM_SZ);
	if (ret < 0)
		goto out;

	ret = bq27xxx_write(di, BQ27XXX_DM_CKSUM,
			    bq27xxx_battery_checksum_dm_block(buf), true);
	if (ret < 0)
		goto out;

	/* DO NOT read BQ27XXX_DM_CKSUM here to verify it! That may cause NVM
	 * corruption on the '425 chip (and perhaps others), which can damage
	 * the chip.
	 */

	if (cfgup) {
		BQ27XXX_MSLEEP(1);
		ret = bq27xxx_battery_soft_reset(di);
		if (ret < 0)
			return ret;
	} else {
		BQ27XXX_MSLEEP(100); /* flash DM updates in <100ms */
	}

	buf->dirty = false;

	return 0;

out:
	if (cfgup)
		bq27xxx_battery_soft_reset(di);

	dev_err(di->dev, "bus error writing chip memory: %d\n", ret);
	return ret;
}

static void bq27xxx_battery_set_config(struct bq27xxx_device_info *di,
				       struct power_supply_battery_info *info)
{
	struct bq27xxx_dm_buf bd = BQ27XXX_DM_BUF(di, BQ27XXX_DM_DESIGN_CAPACITY);
	struct bq27xxx_dm_buf bt = BQ27XXX_DM_BUF(di, BQ27XXX_DM_TERMINATE_VOLTAGE);
	bool updated;

	if (bq27xxx_battery_unseal(di) < 0)
		return;

	if (info->charge_full_design_uah != -EINVAL &&
	    info->energy_full_design_uwh != -EINVAL) {
		bq27xxx_battery_read_dm_block(di, &bd);
		/* assume design energy & capacity are in same block */
		bq27xxx_battery_update_dm_block(di, &bd,
					BQ27XXX_DM_DESIGN_CAPACITY,
					info->charge_full_design_uah / 1000);
		bq27xxx_battery_update_dm_block(di, &bd,
					BQ27XXX_DM_DESIGN_ENERGY,
					info->energy_full_design_uwh / 1000);
	}

	if (info->voltage_min_design_uv != -EINVAL) {
		bool same = bd.class == bt.class && bd.block == bt.block;
		if (!same)
			bq27xxx_battery_read_dm_block(di, &bt);
		bq27xxx_battery_update_dm_block(di, same ? &bd : &bt,
					BQ27XXX_DM_TERMINATE_VOLTAGE,
					info->voltage_min_design_uv / 1000);
	}

	updated = bd.dirty || bt.dirty;

	bq27xxx_battery_write_dm_block(di, &bd);
	bq27xxx_battery_write_dm_block(di, &bt);

	bq27xxx_battery_seal(di);

	if (updated && !(di->opts & BQ27XXX_O_CFGUP)) {
		bq27xxx_write(di, BQ27XXX_REG_CTRL, BQ27XXX_RESET, false);
		BQ27XXX_MSLEEP(300); /* reset time is not documented */
	}
	/* assume bq27xxx_battery_update() is called hereafter */
}

static void bq27xxx_battery_settings(struct bq27xxx_device_info *di)
{
	struct power_supply_battery_info *info;
	unsigned int min, max;

	if (power_supply_get_battery_info(di->bat, &info) < 0)
		return;

	if (!di->dm_regs) {
		dev_warn(di->dev, "data memory update not supported for chip\n");
		return;
	}

	if (info->energy_full_design_uwh != info->charge_full_design_uah) {
		if (info->energy_full_design_uwh == -EINVAL)
			dev_warn(di->dev, "missing battery:energy-full-design-microwatt-hours\n");
		else if (info->charge_full_design_uah == -EINVAL)
			dev_warn(di->dev, "missing battery:charge-full-design-microamp-hours\n");
	}

	/* assume min == 0 */
	max = di->dm_regs[BQ27XXX_DM_DESIGN_ENERGY].max;
	if (info->energy_full_design_uwh > max * 1000) {
		dev_err(di->dev, "invalid battery:energy-full-design-microwatt-hours %d\n",
			info->energy_full_design_uwh);
		info->energy_full_design_uwh = -EINVAL;
	}

	/* assume min == 0 */
	max = di->dm_regs[BQ27XXX_DM_DESIGN_CAPACITY].max;
	if (info->charge_full_design_uah > max * 1000) {
		dev_err(di->dev, "invalid battery:charge-full-design-microamp-hours %d\n",
			info->charge_full_design_uah);
		info->charge_full_design_uah = -EINVAL;
	}

	min = di->dm_regs[BQ27XXX_DM_TERMINATE_VOLTAGE].min;
	max = di->dm_regs[BQ27XXX_DM_TERMINATE_VOLTAGE].max;
	if ((info->voltage_min_design_uv < min * 1000 ||
	     info->voltage_min_design_uv > max * 1000) &&
	     info->voltage_min_design_uv != -EINVAL) {
		dev_err(di->dev, "invalid battery:voltage-min-design-microvolt %d\n",
			info->voltage_min_design_uv);
		info->voltage_min_design_uv = -EINVAL;
	}

	if ((info->energy_full_design_uwh != -EINVAL &&
	     info->charge_full_design_uah != -EINVAL) ||
	     info->voltage_min_design_uv  != -EINVAL)
		bq27xxx_battery_set_config(di, info);
}

/*
 * Return the battery State-of-Charge
 * Or < 0 if something fails.
 */
static int bq27xxx_battery_read_soc(struct bq27xxx_device_info *di)
{
	int soc;

	if (di->opts & BQ27XXX_O_SOC_SI)
		soc = bq27xxx_read(di, BQ27XXX_REG_SOC, true);
	else
		soc = bq27xxx_read(di, BQ27XXX_REG_SOC, false);

	if (soc < 0)
		dev_dbg(di->dev, "error reading State-of-Charge\n");

	return soc;
}

/*
 * Return a battery charge value in µAh
 * Or < 0 if something fails.
 */
static int bq27xxx_battery_read_charge(struct bq27xxx_device_info *di, u8 reg,
				       union power_supply_propval *val)
{
	int charge;

	charge = bq27xxx_read(di, reg, false);
	if (charge < 0) {
		dev_dbg(di->dev, "error reading charge register %02x: %d\n",
			reg, charge);
		return charge;
	}

	if (di->opts & BQ27XXX_O_ZERO)
		charge *= BQ27XXX_CURRENT_CONSTANT / BQ27XXX_RS;
	else
		charge *= 1000;

	val->intval = charge;

	return 0;
}

/*
 * Return the battery Nominal available capacity in µAh
 * Or < 0 if something fails.
 */
static inline int bq27xxx_battery_read_nac(struct bq27xxx_device_info *di,
					   union power_supply_propval *val)
{
	return bq27xxx_battery_read_charge(di, BQ27XXX_REG_NAC, val);
}

/*
 * Return the battery Remaining Capacity in µAh
 * Or < 0 if something fails.
 */
static inline int bq27xxx_battery_read_rc(struct bq27xxx_device_info *di,
					  union power_supply_propval *val)
{
	return bq27xxx_battery_read_charge(di, BQ27XXX_REG_RC, val);
}

/*
 * Return the battery Full Charge Capacity in µAh
 * Or < 0 if something fails.
 */
static inline int bq27xxx_battery_read_fcc(struct bq27xxx_device_info *di,
					   union power_supply_propval *val)
{
	return bq27xxx_battery_read_charge(di, BQ27XXX_REG_FCC, val);
}

/*
 * Return the Design Capacity in µAh
 * Or < 0 if something fails.
 */
static int bq27xxx_battery_read_dcap(struct bq27xxx_device_info *di,
				     union power_supply_propval *val)
{
	int dcap;

	/* We only have to read charge design full once */
	if (di->charge_design_full > 0) {
		val->intval = di->charge_design_full;
		return 0;
	}

	if (di->opts & BQ27XXX_O_ZERO)
		dcap = bq27xxx_read(di, BQ27XXX_REG_DCAP, true);
	else
		dcap = bq27xxx_read(di, BQ27XXX_REG_DCAP, false);

	if (dcap < 0) {
		dev_dbg(di->dev, "error reading design capacity\n");
		return dcap;
	}

	if (di->opts & BQ27XXX_O_ZERO)
		dcap = (dcap << 8) * BQ27XXX_CURRENT_CONSTANT / BQ27XXX_RS;
	else
		dcap *= 1000;

	/* Save for later reads */
	di->charge_design_full = dcap;

	val->intval = dcap;

	return 0;
}

/*
 * Return the battery Available energy in µWh
 * Or < 0 if something fails.
 */
static int bq27xxx_battery_read_energy(struct bq27xxx_device_info *di,
				       union power_supply_propval *val)
{
	int ae;

	ae = bq27xxx_read(di, BQ27XXX_REG_AE, false);
	if (ae < 0) {
		dev_dbg(di->dev, "error reading available energy\n");
		return ae;
	}

	if (di->opts & BQ27XXX_O_ZERO)
		ae *= BQ27XXX_POWER_CONSTANT / BQ27XXX_RS;
	else
		ae *= 1000;

	val->intval = ae;

	return 0;
}

/*
 * Return the battery temperature in tenths of degree Celsius
 * Or < 0 if something fails.
 */
static int bq27xxx_battery_read_temperature(struct bq27xxx_device_info *di,
					    union power_supply_propval *val)
{
	int temp;

	temp = bq27xxx_read(di, BQ27XXX_REG_TEMP, false);
	if (temp < 0) {
		dev_err(di->dev, "error reading temperature\n");
		return temp;
	}

	if (di->opts & BQ27XXX_O_ZERO)
		temp = 5 * temp / 2;

	/* Convert decidegree Kelvin to Celsius */
	temp -= 2731;

	val->intval = temp;

	return 0;
}

/*
 * Return the battery Cycle count total
 * Or < 0 if something fails.
 */
static int bq27xxx_battery_read_cyct(struct bq27xxx_device_info *di,
				     union power_supply_propval *val)
{
	int cyct;

	cyct = bq27xxx_read(di, BQ27XXX_REG_CYCT, false);
	if (cyct < 0)
		dev_err(di->dev, "error reading cycle count total\n");

	val->intval = cyct;

	return 0;
}

/*
 * Read a time register.
 * Return < 0 if something fails.
 */
static int bq27xxx_battery_read_time(struct bq27xxx_device_info *di, u8 reg,
				     union power_supply_propval *val)
{
	int tval;

	tval = bq27xxx_read(di, reg, false);
	if (tval < 0) {
		dev_dbg(di->dev, "error reading time register %02x: %d\n",
			reg, tval);
		return tval;
	}

	if (tval == 65535)
		return -ENODATA;

	val->intval = tval * 60;

	return 0;
}

/*
 * Returns true if a battery over temperature condition is detected
 */
static bool bq27xxx_battery_overtemp(struct bq27xxx_device_info *di, u16 flags)
{
	if (di->opts & BQ27XXX_O_OTDC)
		return flags & (BQ27XXX_FLAG_OTC | BQ27XXX_FLAG_OTD);
        if (di->opts & BQ27XXX_O_UTOT)
		return flags & BQ27XXX_FLAG_OT;

	return false;
}

/*
 * Returns true if a battery under temperature condition is detected
 */
static bool bq27xxx_battery_undertemp(struct bq27xxx_device_info *di, u16 flags)
{
	if (di->opts & BQ27XXX_O_UTOT)
		return flags & BQ27XXX_FLAG_UT;

	return false;
}

/*
 * Returns true if a low state of charge condition is detected
 */
static bool bq27xxx_battery_dead(struct bq27xxx_device_info *di, u16 flags)
{
	if (di->opts & BQ27XXX_O_ZERO)
		return flags & (BQ27000_FLAG_EDV1 | BQ27000_FLAG_EDVF);
	else if (di->opts & BQ27Z561_O_BITS)
		return flags & BQ27Z561_FLAG_FDC;
	else
		return flags & (BQ27XXX_FLAG_SOC1 | BQ27XXX_FLAG_SOCF);
}

/*
 * Returns true if reported battery capacity is inaccurate
 */
static bool bq27xxx_battery_capacity_inaccurate(struct bq27xxx_device_info *di,
						 u16 flags)
{
	if (di->opts & BQ27XXX_O_HAS_CI)
		return (flags & BQ27000_FLAG_CI);
	else
		return false;
}

static int bq27xxx_battery_read_health(struct bq27xxx_device_info *di,
				       union power_supply_propval *val)
{
	int health;

	/* Unlikely but important to return first */
	if (unlikely(bq27xxx_battery_overtemp(di, di->cache.flags)))
		health = POWER_SUPPLY_HEALTH_OVERHEAT;
	else if (unlikely(bq27xxx_battery_undertemp(di, di->cache.flags)))
		health = POWER_SUPPLY_HEALTH_COLD;
	else if (unlikely(bq27xxx_battery_dead(di, di->cache.flags)))
		health = POWER_SUPPLY_HEALTH_DEAD;
	else if (unlikely(bq27xxx_battery_capacity_inaccurate(di, di->cache.flags)))
		health = POWER_SUPPLY_HEALTH_CALIBRATION_REQUIRED;
	else
		health = POWER_SUPPLY_HEALTH_GOOD;

	val->intval = health;

	return 0;
}

static bool bq27xxx_battery_is_full(struct bq27xxx_device_info *di, int flags)
{
	if (di->opts & BQ27XXX_O_ZERO)
		return (flags & BQ27000_FLAG_FC);
	else if (di->opts & BQ27Z561_O_BITS)
		return (flags & BQ27Z561_FLAG_FC);
	else
		return (flags & BQ27XXX_FLAG_FC);
}

/*
 * Return the battery average current in µA and the status
 * Note that current can be negative signed as well
 * Or 0 if something fails.
 */
static int bq27xxx_battery_current_and_status(
	struct bq27xxx_device_info *di,
	union power_supply_propval *val_curr,
	union power_supply_propval *val_status,
	struct bq27xxx_reg_cache *cache)
{
	bool single_flags = (di->opts & BQ27XXX_O_ZERO);
	int curr;
	int flags;

	curr = bq27xxx_read(di, BQ27XXX_REG_AI, false);
	if (curr < 0) {
		dev_err(di->dev, "error reading current\n");
		return curr;
	}

	if (cache) {
		flags = cache->flags;
	} else {
		flags = bq27xxx_read(di, BQ27XXX_REG_FLAGS, single_flags);
		if (flags < 0) {
			dev_err(di->dev, "error reading flags\n");
			return flags;
		}
	}

	if (di->opts & BQ27XXX_O_ZERO) {
		if (!(flags & BQ27000_FLAG_CHGS)) {
			dev_dbg(di->dev, "negative current!\n");
			curr = -curr;
		}

		curr = curr * BQ27XXX_CURRENT_CONSTANT / BQ27XXX_RS;
	} else {
		/* Other gauges return signed value */
		curr = (int)((s16)curr) * 1000;
	}

	if (val_curr)
		val_curr->intval = curr;

	if (val_status) {
		if (bq27xxx_battery_is_full(di, flags))
			val_status->intval = POWER_SUPPLY_STATUS_FULL;
		else if (curr > 0)
			val_status->intval = POWER_SUPPLY_STATUS_CHARGING;
		else if (curr < 0)
			val_status->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		else
			val_status->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
	}

	return 0;
}

static void bq27xxx_battery_update_unlocked(struct bq27xxx_device_info *di)
{
	union power_supply_propval status = di->last_status;
	struct bq27xxx_reg_cache cache = {0, };
	bool has_singe_flag = di->opts & BQ27XXX_O_ZERO;

	cache.flags = bq27xxx_read(di, BQ27XXX_REG_FLAGS, has_singe_flag);
	if (di->chip == BQ27000 && (cache.flags & 0xff) == 0xff)
		cache.flags = -ENODEV; /* bq27000 hdq read error */
	if (cache.flags >= 0) {
		cache.capacity = bq27xxx_battery_read_soc(di);

		/*
		 * On gauges with signed current reporting the current must be
		 * checked to detect charging <-> discharging status changes.
		 */
		if (!(di->opts & BQ27XXX_O_ZERO))
			bq27xxx_battery_current_and_status(di, NULL, &status, &cache);
	}

	if ((di->cache.capacity != cache.capacity) ||
	    (di->cache.flags != cache.flags) ||
	    (di->last_status.intval != status.intval)) {
		di->last_status.intval = status.intval;
		power_supply_changed(di->bat);
	}

	if (memcmp(&di->cache, &cache, sizeof(cache)) != 0)
		di->cache = cache;

	di->last_update = jiffies;

	if (!di->removed && poll_interval > 0)
		mod_delayed_work(system_percpu_wq, &di->work, poll_interval * HZ);
}

void bq27xxx_battery_update(struct bq27xxx_device_info *di)
{
	mutex_lock(&di->lock);
	bq27xxx_battery_update_unlocked(di);
	mutex_unlock(&di->lock);
}
EXPORT_SYMBOL_GPL(bq27xxx_battery_update);

static void bq27xxx_battery_poll(struct work_struct *work)
{
	struct bq27xxx_device_info *di =
			container_of(work, struct bq27xxx_device_info,
				     work.work);

	bq27xxx_battery_update(di);
}

/*
 * Get the average power in µW
 * Return < 0 if something fails.
 */
static int bq27xxx_battery_pwr_avg(struct bq27xxx_device_info *di,
				   union power_supply_propval *val)
{
	int power;

	power = bq27xxx_read(di, BQ27XXX_REG_AP, false);
	if (power < 0) {
		dev_err(di->dev,
			"error reading average power register %02x: %d\n",
			BQ27XXX_REG_AP, power);
		return power;
	}

	if (di->opts & BQ27XXX_O_ZERO)
		val->intval = (power * BQ27XXX_POWER_CONSTANT) / BQ27XXX_RS;
	else
		/* Other gauges return a signed value in units of 10mW */
		val->intval = (int)((s16)power) * 10000;

	return 0;
}

static int bq27xxx_battery_capacity_level(struct bq27xxx_device_info *di,
					  union power_supply_propval *val)
{
	int level;

	if (di->opts & BQ27XXX_O_ZERO) {
		if (di->cache.flags & BQ27000_FLAG_FC)
			level = POWER_SUPPLY_CAPACITY_LEVEL_FULL;
		else if (di->cache.flags & BQ27000_FLAG_EDVF)
			level = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
		else if (di->cache.flags & BQ27000_FLAG_EDV1)
			level = POWER_SUPPLY_CAPACITY_LEVEL_LOW;
		else
			level = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
	} else if (di->opts & BQ27Z561_O_BITS) {
		if (di->cache.flags & BQ27Z561_FLAG_FC)
			level = POWER_SUPPLY_CAPACITY_LEVEL_FULL;
		else if (di->cache.flags & BQ27Z561_FLAG_FDC)
			level = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
		else
			level = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
	} else {
		if (di->cache.flags & BQ27XXX_FLAG_FC)
			level = POWER_SUPPLY_CAPACITY_LEVEL_FULL;
		else if (di->cache.flags & BQ27XXX_FLAG_SOCF)
			level = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
		else if (di->cache.flags & BQ27XXX_FLAG_SOC1)
			level = POWER_SUPPLY_CAPACITY_LEVEL_LOW;
		else
			level = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
	}

	val->intval = level;

	return 0;
}

/*
 * Return the battery Voltage in millivolts
 * Or < 0 if something fails.
 */
static int bq27xxx_battery_voltage(struct bq27xxx_device_info *di,
				   union power_supply_propval *val)
{
	int volt;

	volt = bq27xxx_read(di, BQ27XXX_REG_VOLT, false);
	if (volt < 0) {
		dev_err(di->dev, "error reading voltage\n");
		return volt;
	}

	val->intval = volt * 1000;

	return 0;
}

/*
 * Return the design maximum battery Voltage in microvolts, or < 0 if something
 * fails. The programmed value of the maximum battery voltage is determined by
 * QV0 and QV1 (bits 5 and 6) in the Pack Configuration register.
 */
static int bq27xxx_battery_read_dmax_volt(struct bq27xxx_device_info *di,
					  union power_supply_propval *val)
{
	int reg_val, qv;

	if (di->voltage_max_design > 0) {
		val->intval = di->voltage_max_design;
		return 0;
	}

	reg_val = bq27xxx_read(di, BQ27XXX_REG_PKCFG, true);
	if (reg_val < 0) {
		dev_err(di->dev, "error reading design max voltage\n");
		return reg_val;
	}

	qv = (reg_val >> 5) & 0x3;
	val->intval = 3968000 + 48000 * qv;

	di->voltage_max_design = val->intval;

	return 0;
}

/*
 * Return the design minimum battery Voltage in microvolts
 * Or < 0 if something fails.
 */
static int bq27xxx_battery_read_dmin_volt(struct bq27xxx_device_info *di,
					  union power_supply_propval *val)
{
	int volt;

	/* We only have to read design minimum voltage once */
	if (di->voltage_min_design > 0) {
		val->intval = di->voltage_min_design;
		return 0;
	}

	volt = bq27xxx_read(di, BQ27XXX_REG_SEDVF, true);
	if (volt < 0) {
		dev_err(di->dev, "error reading design min voltage\n");
		return volt;
	}

	/* SEDVF = Design EDVF / 8 - 256 */
	val->intval = volt * 8000 + 2048000;

	/* Save for later reads */
	di->voltage_min_design = val->intval;

	return 0;
}

static int bq27xxx_simple_value(int value,
				union power_supply_propval *val)
{
	if (value < 0)
		return value;

	val->intval = value;

	return 0;
}

static int bq27xxx_battery_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	int ret = 0;
	struct bq27xxx_device_info *di = power_supply_get_drvdata(psy);

	mutex_lock(&di->lock);
	if (time_is_before_jiffies(di->last_update + 5 * HZ))
		bq27xxx_battery_update_unlocked(di);
	mutex_unlock(&di->lock);

	if (psp != POWER_SUPPLY_PROP_PRESENT && di->cache.flags < 0)
		return di->cache.flags;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		ret = bq27xxx_battery_current_and_status(di, NULL, val, NULL);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = bq27xxx_battery_voltage(di, val);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = di->cache.flags < 0 ? 0 : 1;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = bq27xxx_battery_current_and_status(di, val, NULL, NULL);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		ret = bq27xxx_simple_value(di->cache.capacity, val);
		break;
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		ret = bq27xxx_battery_capacity_level(di, val);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		ret = bq27xxx_battery_read_temperature(di, val);
		break;
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW:
		ret = bq27xxx_battery_read_time(di, BQ27XXX_REG_TTE, val);
		break;
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG:
		ret = bq27xxx_battery_read_time(di, BQ27XXX_REG_TTECP, val);
		break;
	case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
		ret = bq27xxx_battery_read_time(di, BQ27XXX_REG_TTF, val);
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		if (di->opts & BQ27XXX_O_MUL_CHEM)
			val->intval = POWER_SUPPLY_TECHNOLOGY_UNKNOWN;
		else
			val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		if (di->regs[BQ27XXX_REG_NAC] != INVALID_REG_ADDR)
			ret = bq27xxx_battery_read_nac(di, val);
		else
			ret = bq27xxx_battery_read_rc(di, val);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		ret = bq27xxx_battery_read_fcc(di, val);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		ret = bq27xxx_battery_read_dcap(di, val);
		break;
	/*
	 * TODO: Implement these to make registers set from
	 * power_supply_battery_info visible in sysfs.
	 */
	case POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN:
		return -EINVAL;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		ret = bq27xxx_battery_read_dmin_volt(di, val);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		ret = bq27xxx_battery_read_dmax_volt(di, val);
		break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		ret = bq27xxx_battery_read_cyct(di, val);
		break;
	case POWER_SUPPLY_PROP_ENERGY_NOW:
		ret = bq27xxx_battery_read_energy(di, val);
		break;
	case POWER_SUPPLY_PROP_POWER_AVG:
		ret = bq27xxx_battery_pwr_avg(di, val);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		ret = bq27xxx_battery_read_health(di, val);
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = BQ27XXX_MANUFACTURER;
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static void bq27xxx_external_power_changed(struct power_supply *psy)
{
	struct bq27xxx_device_info *di = power_supply_get_drvdata(psy);

	/* After charger plug in/out wait 0.5s for things to stabilize */
	mod_delayed_work(system_percpu_wq, &di->work, HZ / 2);
}

int bq27xxx_battery_setup(struct bq27xxx_device_info *di)
{
	struct power_supply_desc *psy_desc;
	struct power_supply_config psy_cfg = {
		.fwnode = dev_fwnode(di->dev),
		.drv_data = di,
		.no_wakeup_source = true,
	};
	int ret;

	INIT_DELAYED_WORK(&di->work, bq27xxx_battery_poll);
	ret = devm_mutex_init(di->dev, &di->lock);
	if (ret)
		return ret;

	di->regs       = bq27xxx_chip_data[di->chip].regs;
	di->unseal_key = bq27xxx_chip_data[di->chip].unseal_key;
	di->dm_regs    = bq27xxx_chip_data[di->chip].dm_regs;
	di->opts       = bq27xxx_chip_data[di->chip].opts;

	psy_desc = devm_kzalloc(di->dev, sizeof(*psy_desc), GFP_KERNEL);
	if (!psy_desc)
		return -ENOMEM;

	psy_desc->name = di->name;
	psy_desc->type = POWER_SUPPLY_TYPE_BATTERY;
	psy_desc->properties = bq27xxx_chip_data[di->chip].props;
	psy_desc->num_properties = bq27xxx_chip_data[di->chip].props_size;
	psy_desc->get_property = bq27xxx_battery_get_property;
	psy_desc->external_power_changed = bq27xxx_external_power_changed;

	di->bat = devm_power_supply_register(di->dev, psy_desc, &psy_cfg);
	if (IS_ERR(di->bat))
		return dev_err_probe(di->dev, PTR_ERR(di->bat),
				     "failed to register battery\n");

	bq27xxx_battery_settings(di);
	bq27xxx_battery_update(di);

	mutex_lock(&bq27xxx_list_lock);
	list_add(&di->list, &bq27xxx_battery_devices);
	mutex_unlock(&bq27xxx_list_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(bq27xxx_battery_setup);

void bq27xxx_battery_teardown(struct bq27xxx_device_info *di)
{
	mutex_lock(&bq27xxx_list_lock);
	list_del(&di->list);
	mutex_unlock(&bq27xxx_list_lock);

	/* Set removed to avoid bq27xxx_battery_update() re-queuing the work */
	mutex_lock(&di->lock);
	di->removed = true;
	mutex_unlock(&di->lock);

	cancel_delayed_work_sync(&di->work);
}
EXPORT_SYMBOL_GPL(bq27xxx_battery_teardown);

#ifdef CONFIG_PM_SLEEP
static int bq27xxx_battery_suspend(struct device *dev)
{
	struct bq27xxx_device_info *di = dev_get_drvdata(dev);

	cancel_delayed_work(&di->work);
	return 0;
}

static int bq27xxx_battery_resume(struct device *dev)
{
	struct bq27xxx_device_info *di = dev_get_drvdata(dev);

	schedule_delayed_work(&di->work, 0);
	return 0;
}
#endif /* CONFIG_PM_SLEEP */

SIMPLE_DEV_PM_OPS(bq27xxx_battery_battery_pm_ops,
		  bq27xxx_battery_suspend, bq27xxx_battery_resume);
EXPORT_SYMBOL_GPL(bq27xxx_battery_battery_pm_ops);

MODULE_AUTHOR("Rodolfo Giometti <giometti@linux.it>");
MODULE_DESCRIPTION("BQ27xxx battery monitor driver");
MODULE_LICENSE("GPL");
