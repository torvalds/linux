/*
 * Texas Instruments TNETV107X SoC Support
 *
 * Copyright (C) 2010 Texas Instruments
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/platform_device.h>

#include <asm/mach/map.h>

#include <mach/common.h>
#include <mach/time.h>
#include <mach/cputype.h>
#include <mach/psc.h>
#include <mach/cp_intc.h>
#include <mach/irqs.h>
#include <mach/hardware.h>
#include <mach/tnetv107x.h>
#include <mach/gpio-davinci.h>

#include "clock.h"
#include "mux.h"

/* Base addresses for on-chip devices */
#define TNETV107X_INTC_BASE			0x03000000
#define TNETV107X_TIMER0_BASE			0x08086500
#define TNETV107X_TIMER1_BASE			0x08086600
#define TNETV107X_CHIP_CFG_BASE			0x08087000
#define TNETV107X_GPIO_BASE			0x08088000
#define TNETV107X_CLOCK_CONTROL_BASE		0x0808a000
#define TNETV107X_PSC_BASE			0x0808b000

/* Reference clock frequencies */
#define OSC_FREQ_ONCHIP		(24000 * 1000)
#define OSC_FREQ_OFFCHIP_SYS	(25000 * 1000)
#define OSC_FREQ_OFFCHIP_ETH	(25000 * 1000)
#define OSC_FREQ_OFFCHIP_TDM	(19200 * 1000)

#define N_PLLS	3

/* Clock Control Registers */
struct clk_ctrl_regs {
	u32	pll_bypass;
	u32	_reserved0;
	u32	gem_lrst;
	u32	_reserved1;
	u32	pll_unlock_stat;
	u32	sys_unlock;
	u32	eth_unlock;
	u32	tdm_unlock;
};

/* SSPLL Registers */
struct sspll_regs {
	u32	modes;
	u32	post_div;
	u32	pre_div;
	u32	mult_factor;
	u32	divider_range;
	u32	bw_divider;
	u32	spr_amount;
	u32	spr_rate_div;
	u32	diag;
};

/* Watchdog Timer Registers */
struct wdt_regs {
	u32	kick_lock;
	u32	kick;
	u32	change_lock;
	u32	change ;
	u32	disable_lock;
	u32	disable;
	u32	prescale_lock;
	u32	prescale;
};

static struct clk_ctrl_regs __iomem *clk_ctrl_regs;

static struct sspll_regs __iomem *sspll_regs[N_PLLS];
static int sspll_regs_base[N_PLLS] = { 0x40, 0x80, 0xc0 };

/* PLL bypass bit shifts in clk_ctrl_regs->pll_bypass register */
static u32 bypass_mask[N_PLLS] = { BIT(0), BIT(2), BIT(1) };

/* offchip (external) reference clock frequencies */
static u32 pll_ext_freq[] = {
	OSC_FREQ_OFFCHIP_SYS,
	OSC_FREQ_OFFCHIP_TDM,
	OSC_FREQ_OFFCHIP_ETH
};

/* PSC control registers */
static u32 psc_regs[] = { TNETV107X_PSC_BASE };

/* Host map for interrupt controller */
static u32 intc_host_map[] = { 0x01010000, 0x01010101, -1 };

static unsigned long clk_sspll_recalc(struct clk *clk);

/* Level 1 - the PLLs */
#define define_pll_clk(cname, pll, divmask, base)	\
	static struct pll_data pll_##cname##_data = {	\
		.num		= pll,			\
		.div_ratio_mask	= divmask,		\
		.phys_base	= base +		\
			TNETV107X_CLOCK_CONTROL_BASE,	\
	};						\
	static struct clk pll_##cname##_clk = {		\
		.name		= "pll_" #cname "_clk",	\
		.pll_data	= &pll_##cname##_data,	\
		.flags		= CLK_PLL,		\
		.recalc		= clk_sspll_recalc,	\
	}

define_pll_clk(sys, 0, 0x1ff, 0x600);
define_pll_clk(tdm, 1, 0x0ff, 0x200);
define_pll_clk(eth, 2, 0x0ff, 0x400);

/* Level 2 - divided outputs from the PLLs */
#define define_pll_div_clk(pll, cname, div)			\
	static struct clk pll##_##cname##_clk = {		\
		.name		= #pll "_" #cname "_clk",	\
		.parent		= &pll_##pll##_clk,		\
		.flags		= CLK_PLL,			\
		.div_reg	= PLLDIV##div,			\
		.set_rate	= davinci_set_sysclk_rate,	\
	}

define_pll_div_clk(sys, arm1176,	1);
define_pll_div_clk(sys, dsp,		2);
define_pll_div_clk(sys, ddr,		3);
define_pll_div_clk(sys, full,		4);
define_pll_div_clk(sys, lcd,		5);
define_pll_div_clk(sys, vlynq_ref,	6);
define_pll_div_clk(sys, tsc,		7);
define_pll_div_clk(sys, half,		8);

define_pll_div_clk(eth, 5mhz,		1);
define_pll_div_clk(eth, 50mhz,		2);
define_pll_div_clk(eth, 125mhz,		3);
define_pll_div_clk(eth, 250mhz,		4);
define_pll_div_clk(eth, 25mhz,		5);

define_pll_div_clk(tdm, 0,		1);
define_pll_div_clk(tdm, extra,		2);
define_pll_div_clk(tdm, 1,		3);


/* Level 3 - LPSC gated clocks */
#define __lpsc_clk(cname, _parent, mod, flg)		\
	static struct clk clk_##cname = {		\
		.name		= #cname,		\
		.parent		= &_parent,		\
		.lpsc		= TNETV107X_LPSC_##mod,\
		.flags		= flg,			\
	}

#define lpsc_clk_enabled(cname, parent, mod)		\
	__lpsc_clk(cname, parent, mod, ALWAYS_ENABLED)

#define lpsc_clk(cname, parent, mod)			\
	__lpsc_clk(cname, parent, mod, 0)

lpsc_clk_enabled(arm,		sys_arm1176_clk, ARM);
lpsc_clk_enabled(gem,		sys_dsp_clk,	GEM);
lpsc_clk_enabled(ddr2_phy,	sys_ddr_clk,	DDR2_PHY);
lpsc_clk_enabled(tpcc,		sys_full_clk,	TPCC);
lpsc_clk_enabled(tptc0,		sys_full_clk,	TPTC0);
lpsc_clk_enabled(tptc1,		sys_full_clk,	TPTC1);
lpsc_clk_enabled(ram,		sys_full_clk,	RAM);
lpsc_clk_enabled(aemif,		sys_full_clk,	AEMIF);
lpsc_clk_enabled(chipcfg,	sys_half_clk,	CHIP_CFG);
lpsc_clk_enabled(rom,		sys_half_clk,	ROM);
lpsc_clk_enabled(secctl,	sys_half_clk,	SECCTL);
lpsc_clk_enabled(keymgr,	sys_half_clk,	KEYMGR);
lpsc_clk_enabled(gpio,		sys_half_clk,	GPIO);
lpsc_clk_enabled(debugss,	sys_half_clk,	DEBUGSS);
lpsc_clk_enabled(system,	sys_half_clk,	SYSTEM);
lpsc_clk_enabled(ddr2_vrst,	sys_ddr_clk,	DDR2_EMIF1_VRST);
lpsc_clk_enabled(ddr2_vctl_rst,	sys_ddr_clk,	DDR2_EMIF2_VCTL_RST);
lpsc_clk_enabled(wdt_arm,	sys_half_clk,	WDT_ARM);
lpsc_clk_enabled(timer1,	sys_half_clk,	TIMER1);

lpsc_clk(mbx_lite,	sys_arm1176_clk,	MBX_LITE);
lpsc_clk(ethss,		eth_125mhz_clk,		ETHSS);
lpsc_clk(tsc,		sys_tsc_clk,		TSC);
lpsc_clk(uart0,		sys_half_clk,		UART0);
lpsc_clk(uart1,		sys_half_clk,		UART1);
lpsc_clk(uart2,		sys_half_clk,		UART2);
lpsc_clk(pktsec,	sys_half_clk,		PKTSEC);
lpsc_clk(keypad,	sys_half_clk,		KEYPAD);
lpsc_clk(mdio,		sys_half_clk,		MDIO);
lpsc_clk(sdio0,		sys_half_clk,		SDIO0);
lpsc_clk(sdio1,		sys_half_clk,		SDIO1);
lpsc_clk(timer0,	sys_half_clk,		TIMER0);
lpsc_clk(wdt_dsp,	sys_half_clk,		WDT_DSP);
lpsc_clk(ssp,		sys_half_clk,		SSP);
lpsc_clk(tdm0,		tdm_0_clk,		TDM0);
lpsc_clk(tdm1,		tdm_1_clk,		TDM1);
lpsc_clk(vlynq,		sys_vlynq_ref_clk,	VLYNQ);
lpsc_clk(mcdma,		sys_half_clk,		MCDMA);
lpsc_clk(usbss,		sys_half_clk,		USBSS);
lpsc_clk(usb0,		clk_usbss,		USB0);
lpsc_clk(usb1,		clk_usbss,		USB1);
lpsc_clk(ethss_rgmii,	eth_250mhz_clk,		ETHSS_RGMII);
lpsc_clk(imcop,		sys_dsp_clk,		IMCOP);
lpsc_clk(spare,		sys_half_clk,		SPARE);

/* LCD needs a full power down to clear controller state */
__lpsc_clk(lcd, sys_lcd_clk, LCD, PSC_SWRSTDISABLE);


/* Level 4 - leaf clocks for LPSC modules shared across drivers */
static struct clk clk_rng = { .name = "rng", .parent = &clk_pktsec };
static struct clk clk_pka = { .name = "pka", .parent = &clk_pktsec };

static struct clk_lookup clks[] = {
	CLK(NULL,		"pll_sys_clk",		&pll_sys_clk),
	CLK(NULL,		"pll_eth_clk",		&pll_eth_clk),
	CLK(NULL,		"pll_tdm_clk",		&pll_tdm_clk),
	CLK(NULL,		"sys_arm1176_clk",	&sys_arm1176_clk),
	CLK(NULL,		"sys_dsp_clk",		&sys_dsp_clk),
	CLK(NULL,		"sys_ddr_clk",		&sys_ddr_clk),
	CLK(NULL,		"sys_full_clk",		&sys_full_clk),
	CLK(NULL,		"sys_lcd_clk",		&sys_lcd_clk),
	CLK(NULL,		"sys_vlynq_ref_clk",	&sys_vlynq_ref_clk),
	CLK(NULL,		"sys_tsc_clk",		&sys_tsc_clk),
	CLK(NULL,		"sys_half_clk",		&sys_half_clk),
	CLK(NULL,		"eth_5mhz_clk",		&eth_5mhz_clk),
	CLK(NULL,		"eth_50mhz_clk",	&eth_50mhz_clk),
	CLK(NULL,		"eth_125mhz_clk",	&eth_125mhz_clk),
	CLK(NULL,		"eth_250mhz_clk",	&eth_250mhz_clk),
	CLK(NULL,		"eth_25mhz_clk",	&eth_25mhz_clk),
	CLK(NULL,		"tdm_0_clk",		&tdm_0_clk),
	CLK(NULL,		"tdm_extra_clk",	&tdm_extra_clk),
	CLK(NULL,		"tdm_1_clk",		&tdm_1_clk),
	CLK(NULL,		"clk_arm",		&clk_arm),
	CLK(NULL,		"clk_gem",		&clk_gem),
	CLK(NULL,		"clk_ddr2_phy",		&clk_ddr2_phy),
	CLK(NULL,		"clk_tpcc",		&clk_tpcc),
	CLK(NULL,		"clk_tptc0",		&clk_tptc0),
	CLK(NULL,		"clk_tptc1",		&clk_tptc1),
	CLK(NULL,		"clk_ram",		&clk_ram),
	CLK(NULL,		"clk_mbx_lite",		&clk_mbx_lite),
	CLK("tnetv107x-fb.0",	NULL,			&clk_lcd),
	CLK(NULL,		"clk_ethss",		&clk_ethss),
	CLK(NULL,		"aemif",		&clk_aemif),
	CLK(NULL,		"clk_chipcfg",		&clk_chipcfg),
	CLK("tnetv107x-ts.0",	NULL,			&clk_tsc),
	CLK(NULL,		"clk_rom",		&clk_rom),
	CLK(NULL,		"uart2",		&clk_uart2),
	CLK(NULL,		"clk_pktsec",		&clk_pktsec),
	CLK("tnetv107x-rng.0",	NULL,			&clk_rng),
	CLK("tnetv107x-pka.0",	NULL,			&clk_pka),
	CLK(NULL,		"clk_secctl",		&clk_secctl),
	CLK(NULL,		"clk_keymgr",		&clk_keymgr),
	CLK("tnetv107x-keypad.0", NULL,			&clk_keypad),
	CLK(NULL,		"clk_gpio",		&clk_gpio),
	CLK(NULL,		"clk_mdio",		&clk_mdio),
	CLK("davinci_mmc.0",	NULL,			&clk_sdio0),
	CLK(NULL,		"uart0",		&clk_uart0),
	CLK(NULL,		"uart1",		&clk_uart1),
	CLK(NULL,		"timer0",		&clk_timer0),
	CLK(NULL,		"timer1",		&clk_timer1),
	CLK("tnetv107x_wdt.0",	NULL,			&clk_wdt_arm),
	CLK(NULL,		"clk_wdt_dsp",		&clk_wdt_dsp),
	CLK("ti-ssp",		NULL,			&clk_ssp),
	CLK(NULL,		"clk_tdm0",		&clk_tdm0),
	CLK(NULL,		"clk_vlynq",		&clk_vlynq),
	CLK(NULL,		"clk_mcdma",		&clk_mcdma),
	CLK(NULL,		"clk_usbss",		&clk_usbss),
	CLK(NULL,		"clk_usb0",		&clk_usb0),
	CLK(NULL,		"clk_usb1",		&clk_usb1),
	CLK(NULL,		"clk_tdm1",		&clk_tdm1),
	CLK(NULL,		"clk_debugss",		&clk_debugss),
	CLK(NULL,		"clk_ethss_rgmii",	&clk_ethss_rgmii),
	CLK(NULL,		"clk_system",		&clk_system),
	CLK(NULL,		"clk_imcop",		&clk_imcop),
	CLK(NULL,		"clk_spare",		&clk_spare),
	CLK("davinci_mmc.1",	NULL,			&clk_sdio1),
	CLK(NULL,		"clk_ddr2_vrst",	&clk_ddr2_vrst),
	CLK(NULL,		"clk_ddr2_vctl_rst",	&clk_ddr2_vctl_rst),
	CLK(NULL,		NULL,			NULL),
};

static const struct mux_config pins[] = {
#ifdef CONFIG_DAVINCI_MUX
	MUX_CFG(TNETV107X, ASR_A00,		0, 0, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO32,		0, 0, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, ASR_A01,		0, 5, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO33,		0, 5, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, ASR_A02,		0, 10, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO34,		0, 10, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, ASR_A03,		0, 15, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO35,		0, 15, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, ASR_A04,		0, 20, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO36,		0, 20, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, ASR_A05,		0, 25, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO37,		0, 25, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, ASR_A06,		1, 0, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO38,		1, 0, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, ASR_A07,		1, 5, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO39,		1, 5, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, ASR_A08,		1, 10, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO40,		1, 10, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, ASR_A09,		1, 15, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO41,		1, 15, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, ASR_A10,		1, 20, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO42,		1, 20, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, ASR_A11,		1, 25, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, BOOT_STRP_0,		1, 25, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, ASR_A12,		2, 0, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, BOOT_STRP_1,		2, 0, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, ASR_A13,		2, 5, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO43,		2, 5, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, ASR_A14,		2, 10, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO44,		2, 10, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, ASR_A15,		2, 15, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO45,		2, 15, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, ASR_A16,		2, 20, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO46,		2, 20, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, ASR_A17,		2, 25, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO47,		2, 25, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, ASR_A18,		3, 0, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO48,		3, 0, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, SDIO1_DATA3_0,	3, 0, 0x1f, 0x1c, false)
	MUX_CFG(TNETV107X, ASR_A19,		3, 5, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO49,		3, 5, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, SDIO1_DATA2_0,	3, 5, 0x1f, 0x1c, false)
	MUX_CFG(TNETV107X, ASR_A20,		3, 10, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO50,		3, 10, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, SDIO1_DATA1_0,	3, 10, 0x1f, 0x1c, false)
	MUX_CFG(TNETV107X, ASR_A21,		3, 15, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO51,		3, 15, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, SDIO1_DATA0_0,	3, 15, 0x1f, 0x1c, false)
	MUX_CFG(TNETV107X, ASR_A22,		3, 20, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO52,		3, 20, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, SDIO1_CMD_0,		3, 20, 0x1f, 0x1c, false)
	MUX_CFG(TNETV107X, ASR_A23,		3, 25, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO53,		3, 25, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, SDIO1_CLK_0,		3, 25, 0x1f, 0x1c, false)
	MUX_CFG(TNETV107X, ASR_BA_1,		4, 0, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO54,		4, 0, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, SYS_PLL_CLK,		4, 0, 0x1f, 0x1c, false)
	MUX_CFG(TNETV107X, ASR_CS0,		4, 5, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, ASR_CS1,		4, 10, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, ASR_CS2,		4, 15, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, TDM_PLL_CLK,		4, 15, 0x1f, 0x1c, false)
	MUX_CFG(TNETV107X, ASR_CS3,		4, 20, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, ETH_PHY_CLK,		4, 20, 0x1f, 0x0c, false)
	MUX_CFG(TNETV107X, ASR_D00,		4, 25, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO55,		4, 25, 0x1f, 0x1c, false)
	MUX_CFG(TNETV107X, ASR_D01,		5, 0, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO56,		5, 0, 0x1f, 0x1c, false)
	MUX_CFG(TNETV107X, ASR_D02,		5, 5, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO57,		5, 5, 0x1f, 0x1c, false)
	MUX_CFG(TNETV107X, ASR_D03,		5, 10, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO58,		5, 10, 0x1f, 0x1c, false)
	MUX_CFG(TNETV107X, ASR_D04,		5, 15, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO59_0,		5, 15, 0x1f, 0x1c, false)
	MUX_CFG(TNETV107X, ASR_D05,		5, 20, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO60_0,		5, 20, 0x1f, 0x1c, false)
	MUX_CFG(TNETV107X, ASR_D06,		5, 25, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO61_0,		5, 25, 0x1f, 0x1c, false)
	MUX_CFG(TNETV107X, ASR_D07,		6, 0, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO62_0,		6, 0, 0x1f, 0x1c, false)
	MUX_CFG(TNETV107X, ASR_D08,		6, 5, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO63_0,		6, 5, 0x1f, 0x1c, false)
	MUX_CFG(TNETV107X, ASR_D09,		6, 10, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO64_0,		6, 10, 0x1f, 0x1c, false)
	MUX_CFG(TNETV107X, ASR_D10,		6, 15, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, SDIO1_DATA3_1,	6, 15, 0x1f, 0x1c, false)
	MUX_CFG(TNETV107X, ASR_D11,		6, 20, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, SDIO1_DATA2_1,	6, 20, 0x1f, 0x1c, false)
	MUX_CFG(TNETV107X, ASR_D12,		6, 25, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, SDIO1_DATA1_1,	6, 25, 0x1f, 0x1c, false)
	MUX_CFG(TNETV107X, ASR_D13,		7, 0, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, SDIO1_DATA0_1,	7, 0, 0x1f, 0x1c, false)
	MUX_CFG(TNETV107X, ASR_D14,		7, 5, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, SDIO1_CMD_1,		7, 5, 0x1f, 0x1c, false)
	MUX_CFG(TNETV107X, ASR_D15,		7, 10, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, SDIO1_CLK_1,		7, 10, 0x1f, 0x1c, false)
	MUX_CFG(TNETV107X, ASR_OE,		7, 15, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, BOOT_STRP_2,		7, 15, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, ASR_RNW,		7, 20, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO29_0,		7, 20, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, ASR_WAIT,		7, 25, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO30_0,		7, 25, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, ASR_WE,		8, 0, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, BOOT_STRP_3,		8, 0, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, ASR_WE_DQM0,		8, 5, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO31,		8, 5, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, LCD_PD17_0,		8, 5, 0x1f, 0x1c, false)
	MUX_CFG(TNETV107X, ASR_WE_DQM1,		8, 10, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, ASR_BA0_0,		8, 10, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, VLYNQ_CLK,		9, 0, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO14,		9, 0, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, LCD_PD19_0,		9, 0, 0x1f, 0x1c, false)
	MUX_CFG(TNETV107X, VLYNQ_RXD0,		9, 5, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO15,		9, 5, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, LCD_PD20_0,		9, 5, 0x1f, 0x1c, false)
	MUX_CFG(TNETV107X, VLYNQ_RXD1,		9, 10, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO16,		9, 10, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, LCD_PD21_0,		9, 10, 0x1f, 0x1c, false)
	MUX_CFG(TNETV107X, VLYNQ_TXD0,		9, 15, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO17,		9, 15, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, LCD_PD22_0,		9, 15, 0x1f, 0x1c, false)
	MUX_CFG(TNETV107X, VLYNQ_TXD1,		9, 20, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO18,		9, 20, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, LCD_PD23_0,		9, 20, 0x1f, 0x1c, false)
	MUX_CFG(TNETV107X, SDIO0_CLK,		10, 0, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO19,		10, 0, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, SDIO0_CMD,		10, 5, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO20,		10, 5, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, SDIO0_DATA0,		10, 10, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO21,		10, 10, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, SDIO0_DATA1,		10, 15, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO22,		10, 15, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, SDIO0_DATA2,		10, 20, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO23,		10, 20, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, SDIO0_DATA3,		10, 25, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO24,		10, 25, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, EMU0,		11, 0, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, EMU1,		11, 5, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, RTCK,		12, 0, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, TRST_N,		12, 5, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, TCK,			12, 10, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, TDI,			12, 15, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, TDO,			12, 20, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, TMS,			12, 25, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, TDM1_CLK,		13, 0, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, TDM1_RX,		13, 5, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, TDM1_TX,		13, 10, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, TDM1_FS,		13, 15, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, KEYPAD_R0,		14, 0, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, KEYPAD_R1,		14, 5, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, KEYPAD_R2,		14, 10, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, KEYPAD_R3,		14, 15, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, KEYPAD_R4,		14, 20, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, KEYPAD_R5,		14, 25, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, KEYPAD_R6,		15, 0, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO12,		15, 0, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, KEYPAD_R7,		15, 5, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO10,		15, 5, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, KEYPAD_C0,		15, 10, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, KEYPAD_C1,		15, 15, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, KEYPAD_C2,		15, 20, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, KEYPAD_C3,		15, 25, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, KEYPAD_C4,		16, 0, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, KEYPAD_C5,		16, 5, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, KEYPAD_C6,		16, 10, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO13,		16, 10, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, TEST_CLK_IN,		16, 10, 0x1f, 0x0c, false)
	MUX_CFG(TNETV107X, KEYPAD_C7,		16, 15, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO11,		16, 15, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, SSP0_0,		17, 0, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, SCC_DCLK,		17, 0, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, LCD_PD20_1,		17, 0, 0x1f, 0x0c, false)
	MUX_CFG(TNETV107X, SSP0_1,		17, 5, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, SCC_CS_N,		17, 5, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, LCD_PD21_1,		17, 5, 0x1f, 0x0c, false)
	MUX_CFG(TNETV107X, SSP0_2,		17, 10, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, SCC_D,		17, 10, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, LCD_PD22_1,		17, 10, 0x1f, 0x0c, false)
	MUX_CFG(TNETV107X, SSP0_3,		17, 15, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, SCC_RESETN,		17, 15, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, LCD_PD23_1,		17, 15, 0x1f, 0x0c, false)
	MUX_CFG(TNETV107X, SSP1_0,		18, 0, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO25,		18, 0, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, UART2_CTS,		18, 0, 0x1f, 0x0c, false)
	MUX_CFG(TNETV107X, SSP1_1,		18, 5, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO26,		18, 5, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, UART2_RD,		18, 5, 0x1f, 0x0c, false)
	MUX_CFG(TNETV107X, SSP1_2,		18, 10, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO27,		18, 10, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, UART2_RTS,		18, 10, 0x1f, 0x0c, false)
	MUX_CFG(TNETV107X, SSP1_3,		18, 15, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO28,		18, 15, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, UART2_TD,		18, 15, 0x1f, 0x0c, false)
	MUX_CFG(TNETV107X, UART0_CTS,		19, 0, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, UART0_RD,		19, 5, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, UART0_RTS,		19, 10, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, UART0_TD,		19, 15, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, UART1_RD,		19, 20, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, UART1_TD,		19, 25, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, LCD_AC_NCS,		20, 0, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, LCD_HSYNC_RNW,	20, 5, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, LCD_VSYNC_A0,	20, 10, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, LCD_MCLK,		20, 15, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, LCD_PD16_0,		20, 15, 0x1f, 0x0c, false)
	MUX_CFG(TNETV107X, LCD_PCLK_E,		20, 20, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, LCD_PD00,		20, 25, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, LCD_PD01,		21, 0, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, LCD_PD02,		21, 5, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, LCD_PD03,		21, 10, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, LCD_PD04,		21, 15, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, LCD_PD05,		21, 20, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, LCD_PD06,		21, 25, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, LCD_PD07,		22, 0, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, LCD_PD08,		22, 5, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO59_1,		22, 5, 0x1f, 0x0c, false)
	MUX_CFG(TNETV107X, LCD_PD09,		22, 10, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO60_1,		22, 10, 0x1f, 0x0c, false)
	MUX_CFG(TNETV107X, LCD_PD10,		22, 15, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, ASR_BA0_1,		22, 15, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, GPIO61_1,		22, 15, 0x1f, 0x0c, false)
	MUX_CFG(TNETV107X, LCD_PD11,		22, 20, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO62_1,		22, 20, 0x1f, 0x0c, false)
	MUX_CFG(TNETV107X, LCD_PD12,		22, 25, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO63_1,		22, 25, 0x1f, 0x0c, false)
	MUX_CFG(TNETV107X, LCD_PD13,		23, 0, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO64_1,		23, 0, 0x1f, 0x0c, false)
	MUX_CFG(TNETV107X, LCD_PD14,		23, 5, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO29_1,		23, 5, 0x1f, 0x0c, false)
	MUX_CFG(TNETV107X, LCD_PD15,		23, 10, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO30_1,		23, 10, 0x1f, 0x0c, false)
	MUX_CFG(TNETV107X, EINT0,		24, 0, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO08,		24, 0, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, EINT1,		24, 5, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, GPIO09,		24, 5, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, GPIO00,		24, 10, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, LCD_PD20_2,		24, 10, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, TDM_CLK_IN_2,	24, 10, 0x1f, 0x0c, false)
	MUX_CFG(TNETV107X, GPIO01,		24, 15, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, LCD_PD21_2,		24, 15, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, 24M_CLK_OUT_1,	24, 15, 0x1f, 0x0c, false)
	MUX_CFG(TNETV107X, GPIO02,		24, 20, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, LCD_PD22_2,		24, 20, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, GPIO03,		24, 25, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, LCD_PD23_2,		24, 25, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, GPIO04,		25, 0, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, LCD_PD16_1,		25, 0, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, USB0_RXERR,		25, 0, 0x1f, 0x0c, false)
	MUX_CFG(TNETV107X, GPIO05,		25, 5, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, LCD_PD17_1,		25, 5, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, TDM_CLK_IN_1,	25, 5, 0x1f, 0x0c, false)
	MUX_CFG(TNETV107X, GPIO06,		25, 10, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, LCD_PD18,		25, 10, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, 24M_CLK_OUT_2,	25, 10, 0x1f, 0x0c, false)
	MUX_CFG(TNETV107X, GPIO07,		25, 15, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, LCD_PD19_1,		25, 15, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, USB1_RXERR,		25, 15, 0x1f, 0x0c, false)
	MUX_CFG(TNETV107X, ETH_PLL_CLK,		25, 15, 0x1f, 0x1c, false)
	MUX_CFG(TNETV107X, MDIO,		26, 0, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, MDC,			26, 5, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, AIC_MUTE_STAT_N,	26, 10, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, TDM0_CLK,		26, 10, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, AIC_HNS_EN_N,	26, 15, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, TDM0_FS,		26, 15, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, AIC_HDS_EN_STAT_N,	26, 20, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, TDM0_TX,		26, 20, 0x1f, 0x04, false)
	MUX_CFG(TNETV107X, AIC_HNF_EN_STAT_N,	26, 25, 0x1f, 0x00, false)
	MUX_CFG(TNETV107X, TDM0_RX,		26, 25, 0x1f, 0x04, false)
#endif
};

/* FIQ are pri 0-1; otherwise 2-7, with 7 lowest priority */
static u8 irq_prios[TNETV107X_N_CP_INTC_IRQ] = {
	/* fill in default priority 7 */
	[0 ... (TNETV107X_N_CP_INTC_IRQ - 1)]	= 7,
	/* now override as needed, e.g. [xxx] = 5 */
};

/* Contents of JTAG ID register used to identify exact cpu type */
static struct davinci_id ids[] = {
	{
		.variant	= 0x0,
		.part_no	= 0xb8a1,
		.manufacturer	= 0x017,
		.cpu_id		= DAVINCI_CPU_ID_TNETV107X,
		.name		= "tnetv107x rev 1.0",
	},
	{
		.variant	= 0x1,
		.part_no	= 0xb8a1,
		.manufacturer	= 0x017,
		.cpu_id		= DAVINCI_CPU_ID_TNETV107X,
		.name		= "tnetv107x rev 1.1/1.2",
	},
};

static struct davinci_timer_instance timer_instance[2] = {
	{
		.base		= TNETV107X_TIMER0_BASE,
		.bottom_irq	= IRQ_TNETV107X_TIMER_0_TINT12,
		.top_irq	= IRQ_TNETV107X_TIMER_0_TINT34,
	},
	{
		.base		= TNETV107X_TIMER1_BASE,
		.bottom_irq	= IRQ_TNETV107X_TIMER_1_TINT12,
		.top_irq	= IRQ_TNETV107X_TIMER_1_TINT34,
	},
};

static struct davinci_timer_info timer_info = {
	.timers		= timer_instance,
	.clockevent_id	= T0_BOT,
	.clocksource_id	= T0_TOP,
};

/*
 * TNETV107X platforms do not use the static mappings from Davinci
 * IO_PHYS/IO_VIRT. This SOC's interesting MMRs are at different addresses,
 * and changing IO_PHYS would break away from existing Davinci SOCs.
 *
 * The primary impact of the current model is that IO_ADDRESS() is not to be
 * used to map registers on TNETV107X.
 *
 * 1.	The first chunk is for INTC:  This needs to be mapped in via iotable
 *	because ioremap() does not seem to be operational at the time when
 *	irqs are initialized.  Without this, consistent dma init bombs.
 *
 * 2.	The second chunk maps in register areas that need to be populated into
 *	davinci_soc_info.  Note that alignment restrictions come into play if
 *	low-level debug is enabled (see note in <mach/tnetv107x.h>).
 */
static struct map_desc io_desc[] = {
	{	/* INTC */
		.virtual	= IO_VIRT,
		.pfn		= __phys_to_pfn(TNETV107X_INTC_BASE),
		.length		= SZ_16K,
		.type		= MT_DEVICE
	},
	{	/* Most of the rest */
		.virtual	= TNETV107X_IO_VIRT,
		.pfn		= __phys_to_pfn(TNETV107X_IO_BASE),
		.length		= IO_SIZE - SZ_1M,
		.type		= MT_DEVICE
	},
};

static unsigned long clk_sspll_recalc(struct clk *clk)
{
	int		pll;
	unsigned long	mult = 0, prediv = 1, postdiv = 1;
	unsigned long	ref = OSC_FREQ_ONCHIP, ret;
	u32		tmp;

	if (WARN_ON(!clk->pll_data))
		return clk->rate;

	if (!clk_ctrl_regs) {
		void __iomem *tmp;

		tmp = ioremap(TNETV107X_CLOCK_CONTROL_BASE, SZ_4K);

		if (WARN(!tmp, "failed ioremap for clock control regs\n"))
			return clk->parent ? clk->parent->rate : 0;

		for (pll = 0; pll < N_PLLS; pll++)
			sspll_regs[pll] = tmp + sspll_regs_base[pll];

		clk_ctrl_regs = tmp;
	}

	pll = clk->pll_data->num;

	tmp = __raw_readl(&clk_ctrl_regs->pll_bypass);
	if (!(tmp & bypass_mask[pll])) {
		mult	= __raw_readl(&sspll_regs[pll]->mult_factor);
		prediv	= __raw_readl(&sspll_regs[pll]->pre_div) + 1;
		postdiv	= __raw_readl(&sspll_regs[pll]->post_div) + 1;
	}

	tmp = __raw_readl(clk->pll_data->base + PLLCTL);
	if (tmp & PLLCTL_CLKMODE)
		ref = pll_ext_freq[pll];

	clk->pll_data->input_rate = ref;

	tmp = __raw_readl(clk->pll_data->base + PLLCTL);
	if (!(tmp & PLLCTL_PLLEN))
		return ref;

	ret = ref;
	if (mult)
		ret += ((unsigned long long)ref * mult) / 256;

	ret /= (prediv * postdiv);

	return ret;
}

static void tnetv107x_watchdog_reset(struct platform_device *pdev)
{
	struct wdt_regs __iomem *regs;

	regs = ioremap(pdev->resource[0].start, SZ_4K);

	/* disable watchdog */
	__raw_writel(0x7777, &regs->disable_lock);
	__raw_writel(0xcccc, &regs->disable_lock);
	__raw_writel(0xdddd, &regs->disable_lock);
	__raw_writel(0, &regs->disable);

	/* program prescale */
	__raw_writel(0x5a5a, &regs->prescale_lock);
	__raw_writel(0xa5a5, &regs->prescale_lock);
	__raw_writel(0, &regs->prescale);

	/* program countdown */
	__raw_writel(0x6666, &regs->change_lock);
	__raw_writel(0xbbbb, &regs->change_lock);
	__raw_writel(1, &regs->change);

	/* enable watchdog */
	__raw_writel(0x7777, &regs->disable_lock);
	__raw_writel(0xcccc, &regs->disable_lock);
	__raw_writel(0xdddd, &regs->disable_lock);
	__raw_writel(1, &regs->disable);

	/* kick */
	__raw_writel(0x5555, &regs->kick_lock);
	__raw_writel(0xaaaa, &regs->kick_lock);
	__raw_writel(1, &regs->kick);
}

static struct davinci_soc_info tnetv107x_soc_info = {
	.io_desc		= io_desc,
	.io_desc_num		= ARRAY_SIZE(io_desc),
	.ids			= ids,
	.ids_num		= ARRAY_SIZE(ids),
	.jtag_id_reg		= TNETV107X_CHIP_CFG_BASE + 0x018,
	.cpu_clks		= clks,
	.psc_bases		= psc_regs,
	.psc_bases_num		= ARRAY_SIZE(psc_regs),
	.pinmux_base		= TNETV107X_CHIP_CFG_BASE + 0x150,
	.pinmux_pins		= pins,
	.pinmux_pins_num	= ARRAY_SIZE(pins),
	.intc_type		= DAVINCI_INTC_TYPE_CP_INTC,
	.intc_base		= TNETV107X_INTC_BASE,
	.intc_irq_prios		= irq_prios,
	.intc_irq_num		= TNETV107X_N_CP_INTC_IRQ,
	.intc_host_map		= intc_host_map,
	.gpio_base		= TNETV107X_GPIO_BASE,
	.gpio_type		= GPIO_TYPE_TNETV107X,
	.gpio_num		= TNETV107X_N_GPIO,
	.timer_info		= &timer_info,
	.serial_dev		= &tnetv107x_serial_device,
	.reset			= tnetv107x_watchdog_reset,
	.reset_device		= &tnetv107x_wdt_device,
};

void __init tnetv107x_init(void)
{
	davinci_common_init(&tnetv107x_soc_info);
}
