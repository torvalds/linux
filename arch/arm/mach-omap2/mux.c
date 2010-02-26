/*
 * linux/arch/arm/mach-omap2/mux.c
 *
 * OMAP2 and OMAP3 pin multiplexing configurations
 *
 * Copyright (C) 2004 - 2008 Texas Instruments Inc.
 * Copyright (C) 2003 - 2008 Nokia Corporation
 *
 * Written by Tony Lindgren
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/ctype.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

#include <asm/system.h>

#include <plat/control.h>
#include <plat/mux.h>

#include "mux.h"

#define OMAP_MUX_BASE_OFFSET		0x30	/* Offset from CTRL_BASE */
#define OMAP_MUX_BASE_SZ		0x5ca

struct omap_mux_entry {
	struct omap_mux		mux;
	struct list_head	node;
};

static unsigned long mux_phys;
static void __iomem *mux_base;

u16 omap_mux_read(u16 reg)
{
	if (cpu_is_omap24xx())
		return __raw_readb(mux_base + reg);
	else
		return __raw_readw(mux_base + reg);
}

void omap_mux_write(u16 val, u16 reg)
{
	if (cpu_is_omap24xx())
		__raw_writeb(val, mux_base + reg);
	else
		__raw_writew(val, mux_base + reg);
}

void omap_mux_write_array(struct omap_board_mux *board_mux)
{
	while (board_mux->reg_offset !=  OMAP_MUX_TERMINATOR) {
		omap_mux_write(board_mux->value, board_mux->reg_offset);
		board_mux++;
	}
}

#if defined(CONFIG_ARCH_OMAP24XX) && defined(CONFIG_OMAP_MUX)

static struct omap_mux_cfg arch_mux_cfg;

/* NOTE: See mux.h for the enumeration */

static struct pin_config __initdata_or_module omap24xx_pins[] = {
/*
 *	description			mux	mux	pull	pull	debug
 *					offset	mode	ena	type
 */

/* 24xx I2C */
MUX_CFG_24XX("M19_24XX_I2C1_SCL",	0x111,	0,	0,	0,	1)
MUX_CFG_24XX("L15_24XX_I2C1_SDA",	0x112,	0,	0,	0,	1)
MUX_CFG_24XX("J15_24XX_I2C2_SCL",	0x113,	0,	0,	1,	1)
MUX_CFG_24XX("H19_24XX_I2C2_SDA",	0x114,	0,	0,	0,	1)

/* Menelaus interrupt */
MUX_CFG_24XX("W19_24XX_SYS_NIRQ",	0x12c,	0,	1,	1,	1)

/* 24xx clocks */
MUX_CFG_24XX("W14_24XX_SYS_CLKOUT",	0x137,	0,	1,	1,	1)

/* 24xx GPMC chipselects, wait pin monitoring */
MUX_CFG_24XX("E2_GPMC_NCS2",		0x08e,	0,	1,	1,	1)
MUX_CFG_24XX("L2_GPMC_NCS7",		0x093,	0,	1,	1,	1)
MUX_CFG_24XX("L3_GPMC_WAIT0",		0x09a,	0,	1,	1,	1)
MUX_CFG_24XX("N7_GPMC_WAIT1",		0x09b,	0,	1,	1,	1)
MUX_CFG_24XX("M1_GPMC_WAIT2",		0x09c,	0,	1,	1,	1)
MUX_CFG_24XX("P1_GPMC_WAIT3",		0x09d,	0,	1,	1,	1)

/* 24xx McBSP */
MUX_CFG_24XX("Y15_24XX_MCBSP2_CLKX",	0x124,	1,	1,	0,	1)
MUX_CFG_24XX("R14_24XX_MCBSP2_FSX",	0x125,	1,	1,	0,	1)
MUX_CFG_24XX("W15_24XX_MCBSP2_DR",	0x126,	1,	1,	0,	1)
MUX_CFG_24XX("V15_24XX_MCBSP2_DX",	0x127,	1,	1,	0,	1)

/* 24xx GPIO */
MUX_CFG_24XX("M21_242X_GPIO11",		0x0c9,	3,	1,	1,	1)
MUX_CFG_24XX("P21_242X_GPIO12",		0x0ca,	3,	0,	0,	1)
MUX_CFG_24XX("AA10_242X_GPIO13",	0x0e5,	3,	0,	0,	1)
MUX_CFG_24XX("AA6_242X_GPIO14",		0x0e6,	3,	0,	0,	1)
MUX_CFG_24XX("AA4_242X_GPIO15",		0x0e7,	3,	0,	0,	1)
MUX_CFG_24XX("Y11_242X_GPIO16",		0x0e8,	3,	0,	0,	1)
MUX_CFG_24XX("AA12_242X_GPIO17",	0x0e9,	3,	0,	0,	1)
MUX_CFG_24XX("AA8_242X_GPIO58",		0x0ea,	3,	0,	0,	1)
MUX_CFG_24XX("Y20_24XX_GPIO60",		0x12c,	3,	0,	0,	1)
MUX_CFG_24XX("W4__24XX_GPIO74",		0x0f2,	3,	0,	0,	1)
MUX_CFG_24XX("N15_24XX_GPIO85",		0x103,	3,	0,	0,	1)
MUX_CFG_24XX("M15_24XX_GPIO92",		0x10a,	3,	0,	0,	1)
MUX_CFG_24XX("P20_24XX_GPIO93",		0x10b,	3,	0,	0,	1)
MUX_CFG_24XX("P18_24XX_GPIO95",		0x10d,	3,	0,	0,	1)
MUX_CFG_24XX("M18_24XX_GPIO96",		0x10e,	3,	0,	0,	1)
MUX_CFG_24XX("L14_24XX_GPIO97",		0x10f,	3,	0,	0,	1)
MUX_CFG_24XX("J15_24XX_GPIO99",		0x113,	3,	1,	1,	1)
MUX_CFG_24XX("V14_24XX_GPIO117",	0x128,	3,	1,	0,	1)
MUX_CFG_24XX("P14_24XX_GPIO125",	0x140,	3,	1,	1,	1)

/* 242x DBG GPIO */
MUX_CFG_24XX("V4_242X_GPIO49",		0xd3,	3,	0,	0,	1)
MUX_CFG_24XX("W2_242X_GPIO50",		0xd4,	3,	0,	0,	1)
MUX_CFG_24XX("U4_242X_GPIO51",		0xd5,	3,	0,	0,	1)
MUX_CFG_24XX("V3_242X_GPIO52",		0xd6,	3,	0,	0,	1)
MUX_CFG_24XX("V2_242X_GPIO53",		0xd7,	3,	0,	0,	1)
MUX_CFG_24XX("V6_242X_GPIO53",		0xcf,	3,	0,	0,	1)
MUX_CFG_24XX("T4_242X_GPIO54",		0xd8,	3,	0,	0,	1)
MUX_CFG_24XX("Y4_242X_GPIO54",		0xd0,	3,	0,	0,	1)
MUX_CFG_24XX("T3_242X_GPIO55",		0xd9,	3,	0,	0,	1)
MUX_CFG_24XX("U2_242X_GPIO56",		0xda,	3,	0,	0,	1)

/* 24xx external DMA requests */
MUX_CFG_24XX("AA10_242X_DMAREQ0",	0x0e5,	2,	0,	0,	1)
MUX_CFG_24XX("AA6_242X_DMAREQ1",	0x0e6,	2,	0,	0,	1)
MUX_CFG_24XX("E4_242X_DMAREQ2",		0x074,	2,	0,	0,	1)
MUX_CFG_24XX("G4_242X_DMAREQ3",		0x073,	2,	0,	0,	1)
MUX_CFG_24XX("D3_242X_DMAREQ4",		0x072,	2,	0,	0,	1)
MUX_CFG_24XX("E3_242X_DMAREQ5",		0x071,	2,	0,	0,	1)

/* UART3 */
MUX_CFG_24XX("K15_24XX_UART3_TX",	0x118,	0,	0,	0,	1)
MUX_CFG_24XX("K14_24XX_UART3_RX",	0x119,	0,	0,	0,	1)

/* MMC/SDIO */
MUX_CFG_24XX("G19_24XX_MMC_CLKO",	0x0f3,	0,	0,	0,	1)
MUX_CFG_24XX("H18_24XX_MMC_CMD",	0x0f4,	0,	0,	0,	1)
MUX_CFG_24XX("F20_24XX_MMC_DAT0",	0x0f5,	0,	0,	0,	1)
MUX_CFG_24XX("H14_24XX_MMC_DAT1",	0x0f6,	0,	0,	0,	1)
MUX_CFG_24XX("E19_24XX_MMC_DAT2",	0x0f7,	0,	0,	0,	1)
MUX_CFG_24XX("D19_24XX_MMC_DAT3",	0x0f8,	0,	0,	0,	1)
MUX_CFG_24XX("F19_24XX_MMC_DAT_DIR0",	0x0f9,	0,	0,	0,	1)
MUX_CFG_24XX("E20_24XX_MMC_DAT_DIR1",	0x0fa,	0,	0,	0,	1)
MUX_CFG_24XX("F18_24XX_MMC_DAT_DIR2",	0x0fb,	0,	0,	0,	1)
MUX_CFG_24XX("E18_24XX_MMC_DAT_DIR3",	0x0fc,	0,	0,	0,	1)
MUX_CFG_24XX("G18_24XX_MMC_CMD_DIR",	0x0fd,	0,	0,	0,	1)
MUX_CFG_24XX("H15_24XX_MMC_CLKI",	0x0fe,	0,	0,	0,	1)

/* Full speed USB */
MUX_CFG_24XX("J20_24XX_USB0_PUEN",	0x11d,	0,	0,	0,	1)
MUX_CFG_24XX("J19_24XX_USB0_VP",	0x11e,	0,	0,	0,	1)
MUX_CFG_24XX("K20_24XX_USB0_VM",	0x11f,	0,	0,	0,	1)
MUX_CFG_24XX("J18_24XX_USB0_RCV",	0x120,	0,	0,	0,	1)
MUX_CFG_24XX("K19_24XX_USB0_TXEN",	0x121,	0,	0,	0,	1)
MUX_CFG_24XX("J14_24XX_USB0_SE0",	0x122,	0,	0,	0,	1)
MUX_CFG_24XX("K18_24XX_USB0_DAT",	0x123,	0,	0,	0,	1)

MUX_CFG_24XX("N14_24XX_USB1_SE0",	0x0ed,	2,	0,	0,	1)
MUX_CFG_24XX("W12_24XX_USB1_SE0",	0x0dd,	3,	0,	0,	1)
MUX_CFG_24XX("P15_24XX_USB1_DAT",	0x0ee,	2,	0,	0,	1)
MUX_CFG_24XX("R13_24XX_USB1_DAT",	0x0e0,	3,	0,	0,	1)
MUX_CFG_24XX("W20_24XX_USB1_TXEN",	0x0ec,	2,	0,	0,	1)
MUX_CFG_24XX("P13_24XX_USB1_TXEN",	0x0df,	3,	0,	0,	1)
MUX_CFG_24XX("V19_24XX_USB1_RCV",	0x0eb,	2,	0,	0,	1)
MUX_CFG_24XX("V12_24XX_USB1_RCV",	0x0de,	3,	0,	0,	1)

MUX_CFG_24XX("AA10_24XX_USB2_SE0",	0x0e5,	2,	0,	0,	1)
MUX_CFG_24XX("Y11_24XX_USB2_DAT",	0x0e8,	2,	0,	0,	1)
MUX_CFG_24XX("AA12_24XX_USB2_TXEN",	0x0e9,	2,	0,	0,	1)
MUX_CFG_24XX("AA6_24XX_USB2_RCV",	0x0e6,	2,	0,	0,	1)
MUX_CFG_24XX("AA4_24XX_USB2_TLLSE0",	0x0e7,	2,	0,	0,	1)

/* Keypad GPIO*/
MUX_CFG_24XX("T19_24XX_KBR0",		0x106,	3,	1,	1,	1)
MUX_CFG_24XX("R19_24XX_KBR1",		0x107,	3,	1,	1,	1)
MUX_CFG_24XX("V18_24XX_KBR2",		0x139,	3,	1,	1,	1)
MUX_CFG_24XX("M21_24XX_KBR3",		0xc9,	3,	1,	1,	1)
MUX_CFG_24XX("E5__24XX_KBR4",		0x138,	3,	1,	1,	1)
MUX_CFG_24XX("M18_24XX_KBR5",		0x10e,	3,	1,	1,	1)
MUX_CFG_24XX("R20_24XX_KBC0",		0x108,	3,	0,	0,	1)
MUX_CFG_24XX("M14_24XX_KBC1",		0x109,	3,	0,	0,	1)
MUX_CFG_24XX("H19_24XX_KBC2",		0x114,	3,	0,	0,	1)
MUX_CFG_24XX("V17_24XX_KBC3",		0x135,	3,	0,	0,	1)
MUX_CFG_24XX("P21_24XX_KBC4",		0xca,	3,	0,	0,	1)
MUX_CFG_24XX("L14_24XX_KBC5",		0x10f,	3,	0,	0,	1)
MUX_CFG_24XX("N19_24XX_KBC6",		0x110,	3,	0,	0,	1)

/* 24xx Menelaus Keypad GPIO */
MUX_CFG_24XX("B3__24XX_KBR5",		0x30,	3,	1,	1,	1)
MUX_CFG_24XX("AA4_24XX_KBC2",		0xe7,	3,	0,	0,	1)
MUX_CFG_24XX("B13_24XX_KBC6",		0x110,	3,	0,	0,	1)

/* 2430 USB */
MUX_CFG_24XX("AD9_2430_USB0_PUEN",	0x133,	4,	0,	0,	1)
MUX_CFG_24XX("Y11_2430_USB0_VP",	0x134,	4,	0,	0,	1)
MUX_CFG_24XX("AD7_2430_USB0_VM",	0x135,	4,	0,	0,	1)
MUX_CFG_24XX("AE7_2430_USB0_RCV",	0x136,	4,	0,	0,	1)
MUX_CFG_24XX("AD4_2430_USB0_TXEN",	0x137,	4,	0,	0,	1)
MUX_CFG_24XX("AF9_2430_USB0_SE0",	0x138,	4,	0,	0,	1)
MUX_CFG_24XX("AE6_2430_USB0_DAT",	0x139,	4,	0,	0,	1)
MUX_CFG_24XX("AD24_2430_USB1_SE0",	0x107,	2,	0,	0,	1)
MUX_CFG_24XX("AB24_2430_USB1_RCV",	0x108,	2,	0,	0,	1)
MUX_CFG_24XX("Y25_2430_USB1_TXEN",	0x109,	2,	0,	0,	1)
MUX_CFG_24XX("AA26_2430_USB1_DAT",	0x10A,	2,	0,	0,	1)

/* 2430 HS-USB */
MUX_CFG_24XX("AD9_2430_USB0HS_DATA3",	0x133,	0,	0,	0,	1)
MUX_CFG_24XX("Y11_2430_USB0HS_DATA4",	0x134,	0,	0,	0,	1)
MUX_CFG_24XX("AD7_2430_USB0HS_DATA5",	0x135,	0,	0,	0,	1)
MUX_CFG_24XX("AE7_2430_USB0HS_DATA6",	0x136,	0,	0,	0,	1)
MUX_CFG_24XX("AD4_2430_USB0HS_DATA2",	0x137,	0,	0,	0,	1)
MUX_CFG_24XX("AF9_2430_USB0HS_DATA0",	0x138,	0,	0,	0,	1)
MUX_CFG_24XX("AE6_2430_USB0HS_DATA1",	0x139,	0,	0,	0,	1)
MUX_CFG_24XX("AE8_2430_USB0HS_CLK",	0x13A,	0,	0,	0,	1)
MUX_CFG_24XX("AD8_2430_USB0HS_DIR",	0x13B,	0,	0,	0,	1)
MUX_CFG_24XX("AE5_2430_USB0HS_STP",	0x13c,	0,	1,	1,	1)
MUX_CFG_24XX("AE9_2430_USB0HS_NXT",	0x13D,	0,	0,	0,	1)
MUX_CFG_24XX("AC7_2430_USB0HS_DATA7",	0x13E,	0,	0,	0,	1)

/* 2430 McBSP */
MUX_CFG_24XX("AD6_2430_MCBSP_CLKS",	0x011E,	0,	0,	0,	1)

MUX_CFG_24XX("AB2_2430_MCBSP1_CLKR",	0x011A,	0,	0,	0,	1)
MUX_CFG_24XX("AD5_2430_MCBSP1_FSR",	0x011B,	0,	0,	0,	1)
MUX_CFG_24XX("AA1_2430_MCBSP1_DX",	0x011C,	0,	0,	0,	1)
MUX_CFG_24XX("AF3_2430_MCBSP1_DR",	0x011D,	0,	0,	0,	1)
MUX_CFG_24XX("AB3_2430_MCBSP1_FSX",	0x011F,	0,	0,	0,	1)
MUX_CFG_24XX("Y9_2430_MCBSP1_CLKX",	0x0120,	0,	0,	0,	1)

MUX_CFG_24XX("AC10_2430_MCBSP2_FSX",	0x012E,	1,	0,	0,	1)
MUX_CFG_24XX("AD16_2430_MCBSP2_CLX",	0x012F,	1,	0,	0,	1)
MUX_CFG_24XX("AE13_2430_MCBSP2_DX",	0x0130,	1,	0,	0,	1)
MUX_CFG_24XX("AD13_2430_MCBSP2_DR",	0x0131,	1,	0,	0,	1)
MUX_CFG_24XX("AC10_2430_MCBSP2_FSX_OFF",0x012E,	0,	0,	0,	1)
MUX_CFG_24XX("AD16_2430_MCBSP2_CLX_OFF",0x012F,	0,	0,	0,	1)
MUX_CFG_24XX("AE13_2430_MCBSP2_DX_OFF",	0x0130,	0,	0,	0,	1)
MUX_CFG_24XX("AD13_2430_MCBSP2_DR_OFF",	0x0131,	0,	0,	0,	1)

MUX_CFG_24XX("AC9_2430_MCBSP3_CLKX",	0x0103,	0,	0,	0,	1)
MUX_CFG_24XX("AE4_2430_MCBSP3_FSX",	0x0104,	0,	0,	0,	1)
MUX_CFG_24XX("AE2_2430_MCBSP3_DR",	0x0105,	0,	0,	0,	1)
MUX_CFG_24XX("AF4_2430_MCBSP3_DX",	0x0106,	0,	0,	0,	1)

MUX_CFG_24XX("N3_2430_MCBSP4_CLKX",	0x010B,	1,	0,	0,	1)
MUX_CFG_24XX("AD23_2430_MCBSP4_DR",	0x010C,	1,	0,	0,	1)
MUX_CFG_24XX("AB25_2430_MCBSP4_DX",	0x010D,	1,	0,	0,	1)
MUX_CFG_24XX("AC25_2430_MCBSP4_FSX",	0x010E,	1,	0,	0,	1)

MUX_CFG_24XX("AE16_2430_MCBSP5_CLKX",	0x00ED,	1,	0,	0,	1)
MUX_CFG_24XX("AF12_2430_MCBSP5_FSX",	0x00ED,	1,	0,	0,	1)
MUX_CFG_24XX("K7_2430_MCBSP5_DX",	0x00EF,	1,	0,	0,	1)
MUX_CFG_24XX("M1_2430_MCBSP5_DR",	0x00F0,	1,	0,	0,	1)

/* 2430 MCSPI1 */
MUX_CFG_24XX("Y18_2430_MCSPI1_CLK",	0x010F,	0,	0,	0,	1)
MUX_CFG_24XX("AD15_2430_MCSPI1_SIMO",	0x0110,	0,	0,	0,	1)
MUX_CFG_24XX("AE17_2430_MCSPI1_SOMI",	0x0111,	0,	0,	0,	1)
MUX_CFG_24XX("U1_2430_MCSPI1_CS0",	0x0112,	0,	0,	0,	1)

/* Touchscreen GPIO */
MUX_CFG_24XX("AF19_2430_GPIO_85",	0x0113,	3,	0,	0,	1)

};

#define OMAP24XX_PINS_SZ	ARRAY_SIZE(omap24xx_pins)

#if defined(CONFIG_OMAP_MUX_DEBUG) || defined(CONFIG_OMAP_MUX_WARNINGS)

static void __init_or_module omap2_cfg_debug(const struct pin_config *cfg, u16 reg)
{
	u16 orig;
	u8 warn = 0, debug = 0;

	orig = omap_mux_read(cfg->mux_reg - OMAP_MUX_BASE_OFFSET);

#ifdef	CONFIG_OMAP_MUX_DEBUG
	debug = cfg->debug;
#endif
	warn = (orig != reg);
	if (debug || warn)
		printk(KERN_WARNING
			"MUX: setup %s (0x%p): 0x%04x -> 0x%04x\n",
			cfg->name, omap_ctrl_base_get() + cfg->mux_reg,
			orig, reg);
}
#else
#define omap2_cfg_debug(x, y)	do {} while (0)
#endif

static int __init_or_module omap24xx_cfg_reg(const struct pin_config *cfg)
{
	static DEFINE_SPINLOCK(mux_spin_lock);
	unsigned long flags;
	u8 reg = 0;

	spin_lock_irqsave(&mux_spin_lock, flags);
	reg |= cfg->mask & 0x7;
	if (cfg->pull_val)
		reg |= OMAP2_PULL_ENA;
	if (cfg->pu_pd_val)
		reg |= OMAP2_PULL_UP;
	omap2_cfg_debug(cfg, reg);
	omap_mux_write(reg, cfg->mux_reg - OMAP_MUX_BASE_OFFSET);
	spin_unlock_irqrestore(&mux_spin_lock, flags);

	return 0;
}

int __init omap2_mux_init(void)
{
	u32 mux_pbase;

	if (cpu_is_omap2420())
		mux_pbase = OMAP2420_CTRL_BASE + OMAP_MUX_BASE_OFFSET;
	else if (cpu_is_omap2430())
		mux_pbase = OMAP243X_CTRL_BASE + OMAP_MUX_BASE_OFFSET;
	else
		return -ENODEV;

	mux_base = ioremap(mux_pbase, OMAP_MUX_BASE_SZ);
	if (!mux_base) {
		printk(KERN_ERR "mux: Could not ioremap\n");
		return -ENODEV;
	}

	if (cpu_is_omap24xx()) {
		arch_mux_cfg.pins	= omap24xx_pins;
		arch_mux_cfg.size	= OMAP24XX_PINS_SZ;
		arch_mux_cfg.cfg_reg	= omap24xx_cfg_reg;

		return omap_mux_register(&arch_mux_cfg);
	}

	return 0;
}

#else
int __init omap2_mux_init(void)
{
	return 0;
}
#endif	/* CONFIG_OMAP_MUX */

/*----------------------------------------------------------------------------*/

#ifdef CONFIG_ARCH_OMAP34XX
static LIST_HEAD(muxmodes);
static DEFINE_MUTEX(muxmode_mutex);

#ifdef CONFIG_OMAP_MUX

static char *omap_mux_options;

int __init omap_mux_init_gpio(int gpio, int val)
{
	struct omap_mux_entry *e;
	int found = 0;

	if (!gpio)
		return -EINVAL;

	list_for_each_entry(e, &muxmodes, node) {
		struct omap_mux *m = &e->mux;
		if (gpio == m->gpio) {
			u16 old_mode;
			u16 mux_mode;

			old_mode = omap_mux_read(m->reg_offset);
			mux_mode = val & ~(OMAP_MUX_NR_MODES - 1);
			mux_mode |= OMAP_MUX_MODE4;
			printk(KERN_DEBUG "mux: Setting signal "
				"%s.gpio%i 0x%04x -> 0x%04x\n",
				m->muxnames[0], gpio, old_mode, mux_mode);
			omap_mux_write(mux_mode, m->reg_offset);
			found++;
		}
	}

	if (found == 1)
		return 0;

	if (found > 1) {
		printk(KERN_ERR "mux: Multiple gpio paths for gpio%i\n", gpio);
		return -EINVAL;
	}

	printk(KERN_ERR "mux: Could not set gpio%i\n", gpio);

	return -ENODEV;
}

int __init omap_mux_init_signal(char *muxname, int val)
{
	struct omap_mux_entry *e;
	char *m0_name = NULL, *mode_name = NULL;
	int found = 0;

	mode_name = strchr(muxname, '.');
	if (mode_name) {
		*mode_name = '\0';
		mode_name++;
		m0_name = muxname;
	} else {
		mode_name = muxname;
	}

	list_for_each_entry(e, &muxmodes, node) {
		struct omap_mux *m = &e->mux;
		char *m0_entry = m->muxnames[0];
		int i;

		if (m0_name && strcmp(m0_name, m0_entry))
			continue;

		for (i = 0; i < OMAP_MUX_NR_MODES; i++) {
			char *mode_cur = m->muxnames[i];

			if (!mode_cur)
				continue;

			if (!strcmp(mode_name, mode_cur)) {
				u16 old_mode;
				u16 mux_mode;

				old_mode = omap_mux_read(m->reg_offset);
				mux_mode = val | i;
				printk(KERN_DEBUG "mux: Setting signal "
					"%s.%s 0x%04x -> 0x%04x\n",
					m0_entry, muxname, old_mode, mux_mode);
				omap_mux_write(mux_mode, m->reg_offset);
				found++;
			}
		}
	}

	if (found == 1)
		return 0;

	if (found > 1) {
		printk(KERN_ERR "mux: Multiple signal paths (%i) for %s\n",
				found, muxname);
		return -EINVAL;
	}

	printk(KERN_ERR "mux: Could not set signal %s\n", muxname);

	return -ENODEV;
}

#ifdef CONFIG_DEBUG_FS

#define OMAP_MUX_MAX_NR_FLAGS	10
#define OMAP_MUX_TEST_FLAG(val, mask)				\
	if (((val) & (mask)) == (mask)) {			\
		i++;						\
		flags[i] =  #mask;				\
	}

/* REVISIT: Add checking for non-optimal mux settings */
static inline void omap_mux_decode(struct seq_file *s, u16 val)
{
	char *flags[OMAP_MUX_MAX_NR_FLAGS];
	char mode[sizeof("OMAP_MUX_MODE") + 1];
	int i = -1;

	sprintf(mode, "OMAP_MUX_MODE%d", val & 0x7);
	i++;
	flags[i] = mode;

	OMAP_MUX_TEST_FLAG(val, OMAP_PIN_OFF_WAKEUPENABLE);
	if (val & OMAP_OFF_EN) {
		if (!(val & OMAP_OFFOUT_EN)) {
			if (!(val & OMAP_OFF_PULL_UP)) {
				OMAP_MUX_TEST_FLAG(val,
					OMAP_PIN_OFF_INPUT_PULLDOWN);
			} else {
				OMAP_MUX_TEST_FLAG(val,
					OMAP_PIN_OFF_INPUT_PULLUP);
			}
		} else {
			if (!(val & OMAP_OFFOUT_VAL)) {
				OMAP_MUX_TEST_FLAG(val,
					OMAP_PIN_OFF_OUTPUT_LOW);
			} else {
				OMAP_MUX_TEST_FLAG(val,
					OMAP_PIN_OFF_OUTPUT_HIGH);
			}
		}
	}

	if (val & OMAP_INPUT_EN) {
		if (val & OMAP_PULL_ENA) {
			if (!(val & OMAP_PULL_UP)) {
				OMAP_MUX_TEST_FLAG(val,
					OMAP_PIN_INPUT_PULLDOWN);
			} else {
				OMAP_MUX_TEST_FLAG(val, OMAP_PIN_INPUT_PULLUP);
			}
		} else {
			OMAP_MUX_TEST_FLAG(val, OMAP_PIN_INPUT);
		}
	} else {
		i++;
		flags[i] = "OMAP_PIN_OUTPUT";
	}

	do {
		seq_printf(s, "%s", flags[i]);
		if (i > 0)
			seq_printf(s, " | ");
	} while (i-- > 0);
}

#define OMAP_MUX_DEFNAME_LEN	16

static int omap_mux_dbg_board_show(struct seq_file *s, void *unused)
{
	struct omap_mux_entry *e;

	list_for_each_entry(e, &muxmodes, node) {
		struct omap_mux *m = &e->mux;
		char m0_def[OMAP_MUX_DEFNAME_LEN];
		char *m0_name = m->muxnames[0];
		u16 val;
		int i, mode;

		if (!m0_name)
			continue;

		/* REVISIT: Needs to be updated if mode0 names get longer */
		for (i = 0; i < OMAP_MUX_DEFNAME_LEN; i++) {
			if (m0_name[i] == '\0') {
				m0_def[i] = m0_name[i];
				break;
			}
			m0_def[i] = toupper(m0_name[i]);
		}
		val = omap_mux_read(m->reg_offset);
		mode = val & OMAP_MUX_MODE7;

		seq_printf(s, "OMAP%i_MUX(%s, ",
					cpu_is_omap34xx() ? 3 : 0, m0_def);
		omap_mux_decode(s, val);
		seq_printf(s, "),\n");
	}

	return 0;
}

static int omap_mux_dbg_board_open(struct inode *inode, struct file *file)
{
	return single_open(file, omap_mux_dbg_board_show, &inode->i_private);
}

static const struct file_operations omap_mux_dbg_board_fops = {
	.open		= omap_mux_dbg_board_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int omap_mux_dbg_signal_show(struct seq_file *s, void *unused)
{
	struct omap_mux *m = s->private;
	const char *none = "NA";
	u16 val;
	int mode;

	val = omap_mux_read(m->reg_offset);
	mode = val & OMAP_MUX_MODE7;

	seq_printf(s, "name: %s.%s (0x%08lx/0x%03x = 0x%04x), b %s, t %s\n",
			m->muxnames[0], m->muxnames[mode],
			mux_phys + m->reg_offset, m->reg_offset, val,
			m->balls[0] ? m->balls[0] : none,
			m->balls[1] ? m->balls[1] : none);
	seq_printf(s, "mode: ");
	omap_mux_decode(s, val);
	seq_printf(s, "\n");
	seq_printf(s, "signals: %s | %s | %s | %s | %s | %s | %s | %s\n",
			m->muxnames[0] ? m->muxnames[0] : none,
			m->muxnames[1] ? m->muxnames[1] : none,
			m->muxnames[2] ? m->muxnames[2] : none,
			m->muxnames[3] ? m->muxnames[3] : none,
			m->muxnames[4] ? m->muxnames[4] : none,
			m->muxnames[5] ? m->muxnames[5] : none,
			m->muxnames[6] ? m->muxnames[6] : none,
			m->muxnames[7] ? m->muxnames[7] : none);

	return 0;
}

#define OMAP_MUX_MAX_ARG_CHAR  7

static ssize_t omap_mux_dbg_signal_write(struct file *file,
						const char __user *user_buf,
						size_t count, loff_t *ppos)
{
	char buf[OMAP_MUX_MAX_ARG_CHAR];
	struct seq_file *seqf;
	struct omap_mux *m;
	unsigned long val;
	int buf_size, ret;

	if (count > OMAP_MUX_MAX_ARG_CHAR)
		return -EINVAL;

	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) - 1);

	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	ret = strict_strtoul(buf, 0x10, &val);
	if (ret < 0)
		return ret;

	if (val > 0xffff)
		return -EINVAL;

	seqf = file->private_data;
	m = seqf->private;

	omap_mux_write((u16)val, m->reg_offset);
	*ppos += count;

	return count;
}

static int omap_mux_dbg_signal_open(struct inode *inode, struct file *file)
{
	return single_open(file, omap_mux_dbg_signal_show, inode->i_private);
}

static const struct file_operations omap_mux_dbg_signal_fops = {
	.open		= omap_mux_dbg_signal_open,
	.read		= seq_read,
	.write		= omap_mux_dbg_signal_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static struct dentry *mux_dbg_dir;

static void __init omap_mux_dbg_init(void)
{
	struct omap_mux_entry *e;

	mux_dbg_dir = debugfs_create_dir("omap_mux", NULL);
	if (!mux_dbg_dir)
		return;

	(void)debugfs_create_file("board", S_IRUGO, mux_dbg_dir,
					NULL, &omap_mux_dbg_board_fops);

	list_for_each_entry(e, &muxmodes, node) {
		struct omap_mux *m = &e->mux;

		(void)debugfs_create_file(m->muxnames[0], S_IWUGO, mux_dbg_dir,
					m, &omap_mux_dbg_signal_fops);
	}
}

#else
static inline void omap_mux_dbg_init(void)
{
}
#endif	/* CONFIG_DEBUG_FS */

static void __init omap_mux_free_names(struct omap_mux *m)
{
	int i;

	for (i = 0; i < OMAP_MUX_NR_MODES; i++)
		kfree(m->muxnames[i]);

#ifdef CONFIG_DEBUG_FS
	for (i = 0; i < OMAP_MUX_NR_SIDES; i++)
		kfree(m->balls[i]);
#endif

}

/* Free all data except for GPIO pins unless CONFIG_DEBUG_FS is set */
static int __init omap_mux_late_init(void)
{
	struct omap_mux_entry *e, *tmp;

	list_for_each_entry_safe(e, tmp, &muxmodes, node) {
		struct omap_mux *m = &e->mux;
		u16 mode = omap_mux_read(m->reg_offset);

		if (OMAP_MODE_GPIO(mode))
			continue;

#ifndef CONFIG_DEBUG_FS
		mutex_lock(&muxmode_mutex);
		list_del(&e->node);
		mutex_unlock(&muxmode_mutex);
		omap_mux_free_names(m);
		kfree(m);
#endif

	}

	omap_mux_dbg_init();

	return 0;
}
late_initcall(omap_mux_late_init);

static void __init omap_mux_package_fixup(struct omap_mux *p,
					struct omap_mux *superset)
{
	while (p->reg_offset !=  OMAP_MUX_TERMINATOR) {
		struct omap_mux *s = superset;
		int found = 0;

		while (s->reg_offset != OMAP_MUX_TERMINATOR) {
			if (s->reg_offset == p->reg_offset) {
				*s = *p;
				found++;
				break;
			}
			s++;
		}
		if (!found)
			printk(KERN_ERR "mux: Unknown entry offset 0x%x\n",
					p->reg_offset);
		p++;
	}
}

#ifdef CONFIG_DEBUG_FS

static void __init omap_mux_package_init_balls(struct omap_ball *b,
				struct omap_mux *superset)
{
	while (b->reg_offset != OMAP_MUX_TERMINATOR) {
		struct omap_mux *s = superset;
		int found = 0;

		while (s->reg_offset != OMAP_MUX_TERMINATOR) {
			if (s->reg_offset == b->reg_offset) {
				s->balls[0] = b->balls[0];
				s->balls[1] = b->balls[1];
				found++;
				break;
			}
			s++;
		}
		if (!found)
			printk(KERN_ERR "mux: Unknown ball offset 0x%x\n",
					b->reg_offset);
		b++;
	}
}

#else	/* CONFIG_DEBUG_FS */

static inline void omap_mux_package_init_balls(struct omap_ball *b,
					struct omap_mux *superset)
{
}

#endif	/* CONFIG_DEBUG_FS */

static int __init omap_mux_setup(char *options)
{
	if (!options)
		return 0;

	omap_mux_options = options;

	return 1;
}
__setup("omap_mux=", omap_mux_setup);

/*
 * Note that the omap_mux=some.signal1=0x1234,some.signal2=0x1234
 * cmdline options only override the bootloader values.
 * During development, please enable CONFIG_DEBUG_FS, and use the
 * signal specific entries under debugfs.
 */
static void __init omap_mux_set_cmdline_signals(void)
{
	char *options, *next_opt, *token;

	if (!omap_mux_options)
		return;

	options = kmalloc(strlen(omap_mux_options) + 1, GFP_KERNEL);
	if (!options)
		return;

	strcpy(options, omap_mux_options);
	next_opt = options;

	while ((token = strsep(&next_opt, ",")) != NULL) {
		char *keyval, *name;
		unsigned long val;

		keyval = token;
		name = strsep(&keyval, "=");
		if (name) {
			int res;

			res = strict_strtoul(keyval, 0x10, &val);
			if (res < 0)
				continue;

			omap_mux_init_signal(name, (u16)val);
		}
	}

	kfree(options);
}

static int __init omap_mux_copy_names(struct omap_mux *src,
					struct omap_mux *dst)
{
	int i;

	for (i = 0; i < OMAP_MUX_NR_MODES; i++) {
		if (src->muxnames[i]) {
			dst->muxnames[i] =
				kmalloc(strlen(src->muxnames[i]) + 1,
					GFP_KERNEL);
			if (!dst->muxnames[i])
				goto free;
			strcpy(dst->muxnames[i], src->muxnames[i]);
		}
	}

#ifdef CONFIG_DEBUG_FS
	for (i = 0; i < OMAP_MUX_NR_SIDES; i++) {
		if (src->balls[i]) {
			dst->balls[i] =
				kmalloc(strlen(src->balls[i]) + 1,
					GFP_KERNEL);
			if (!dst->balls[i])
				goto free;
			strcpy(dst->balls[i], src->balls[i]);
		}
	}
#endif

	return 0;

free:
	omap_mux_free_names(dst);
	return -ENOMEM;

}

#endif	/* CONFIG_OMAP_MUX */

static u16 omap_mux_get_by_gpio(int gpio)
{
	struct omap_mux_entry *e;
	u16 offset = OMAP_MUX_TERMINATOR;

	list_for_each_entry(e, &muxmodes, node) {
		struct omap_mux *m = &e->mux;
		if (m->gpio == gpio) {
			offset = m->reg_offset;
			break;
		}
	}

	return offset;
}

/* Needed for dynamic muxing of GPIO pins for off-idle */
u16 omap_mux_get_gpio(int gpio)
{
	u16 offset;

	offset = omap_mux_get_by_gpio(gpio);
	if (offset == OMAP_MUX_TERMINATOR) {
		printk(KERN_ERR "mux: Could not get gpio%i\n", gpio);
		return offset;
	}

	return omap_mux_read(offset);
}

/* Needed for dynamic muxing of GPIO pins for off-idle */
void omap_mux_set_gpio(u16 val, int gpio)
{
	u16 offset;

	offset = omap_mux_get_by_gpio(gpio);
	if (offset == OMAP_MUX_TERMINATOR) {
		printk(KERN_ERR "mux: Could not set gpio%i\n", gpio);
		return;
	}

	omap_mux_write(val, offset);
}

static struct omap_mux * __init omap_mux_list_add(struct omap_mux *src)
{
	struct omap_mux_entry *entry;
	struct omap_mux *m;

	entry = kzalloc(sizeof(struct omap_mux_entry), GFP_KERNEL);
	if (!entry)
		return NULL;

	m = &entry->mux;
	memcpy(m, src, sizeof(struct omap_mux_entry));

#ifdef CONFIG_OMAP_MUX
	if (omap_mux_copy_names(src, m)) {
		kfree(entry);
		return NULL;
	}
#endif

	mutex_lock(&muxmode_mutex);
	list_add_tail(&entry->node, &muxmodes);
	mutex_unlock(&muxmode_mutex);

	return m;
}

/*
 * Note if CONFIG_OMAP_MUX is not selected, we will only initialize
 * the GPIO to mux offset mapping that is needed for dynamic muxing
 * of GPIO pins for off-idle.
 */
static void __init omap_mux_init_list(struct omap_mux *superset)
{
	while (superset->reg_offset !=  OMAP_MUX_TERMINATOR) {
		struct omap_mux *entry;

#ifdef CONFIG_OMAP_MUX
		if (!superset->muxnames || !superset->muxnames[0]) {
			superset++;
			continue;
		}
#else
		/* Skip pins that are not muxed as GPIO by bootloader */
		if (!OMAP_MODE_GPIO(omap_mux_read(superset->reg_offset))) {
			superset++;
			continue;
		}
#endif

		entry = omap_mux_list_add(superset);
		if (!entry) {
			printk(KERN_ERR "mux: Could not add entry\n");
			return;
		}
		superset++;
	}
}

int __init omap_mux_init(u32 mux_pbase, u32 mux_size,
				struct omap_mux *superset,
				struct omap_mux *package_subset,
				struct omap_board_mux *board_mux,
				struct omap_ball *package_balls)
{
	if (mux_base)
		return -EBUSY;

	mux_phys = mux_pbase;
	mux_base = ioremap(mux_pbase, mux_size);
	if (!mux_base) {
		printk(KERN_ERR "mux: Could not ioremap\n");
		return -ENODEV;
	}

#ifdef CONFIG_OMAP_MUX
	if (package_subset)
		omap_mux_package_fixup(package_subset, superset);
	if (package_balls)
		omap_mux_package_init_balls(package_balls, superset);
#endif

	omap_mux_init_list(superset);

#ifdef CONFIG_OMAP_MUX
	omap_mux_set_cmdline_signals();
	omap_mux_write_array(board_mux);
#endif

	return 0;
}

#endif	/* CONFIG_ARCH_OMAP34XX */

