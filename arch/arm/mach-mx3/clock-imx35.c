/*
 * Copyright (C) 2009 by Sascha Hauer, Pengutronix
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/clk.h>
#include <linux/io.h>

#include <asm/clkdev.h>

#include <mach/clock.h>
#include <mach/hardware.h>
#include <mach/common.h>

#define CCM_BASE	MX35_IO_ADDRESS(MX35_CCM_BASE_ADDR)

#define CCM_CCMR        0x00
#define CCM_PDR0        0x04
#define CCM_PDR1        0x08
#define CCM_PDR2        0x0C
#define CCM_PDR3        0x10
#define CCM_PDR4        0x14
#define CCM_RCSR        0x18
#define CCM_MPCTL       0x1C
#define CCM_PPCTL       0x20
#define CCM_ACMR        0x24
#define CCM_COSR        0x28
#define CCM_CGR0        0x2C
#define CCM_CGR1        0x30
#define CCM_CGR2        0x34
#define CCM_CGR3        0x38

#ifdef HAVE_SET_RATE_SUPPORT
static void calc_dividers(u32 div, u32 *pre, u32 *post, u32 maxpost)
{
	u32 min_pre, temp_pre, old_err, err;

	min_pre = (div - 1) / maxpost + 1;
	old_err = 8;

	for (temp_pre = 8; temp_pre >= min_pre; temp_pre--) {
		if (div > (temp_pre * maxpost))
			break;

		if (div < (temp_pre * temp_pre))
			continue;

		err = div % temp_pre;

		if (err == 0) {
			*pre = temp_pre;
			break;
		}

		err = temp_pre - err;

		if (err < old_err) {
			old_err = err;
			*pre = temp_pre;
		}
	}

	*post = (div + *pre - 1) / *pre;
}

/* get the best values for a 3-bit divider combined with a 6-bit divider */
static void calc_dividers_3_6(u32 div, u32 *pre, u32 *post)
{
	if (div >= 512) {
		*pre = 8;
		*post = 64;
	} else if (div >= 64) {
		calc_dividers(div, pre, post, 64);
	} else if (div <= 8) {
		*pre = div;
		*post = 1;
	} else {
		*pre = 1;
		*post = div;
	}
}

/* get the best values for two cascaded 3-bit dividers */
static void calc_dividers_3_3(u32 div, u32 *pre, u32 *post)
{
	if (div >= 64) {
		*pre = *post = 8;
	} else if (div > 8) {
		calc_dividers(div, pre, post, 8);
	} else {
		*pre = 1;
		*post = div;
	}
}
#endif

static unsigned long get_rate_mpll(void)
{
	ulong mpctl = __raw_readl(CCM_BASE + CCM_MPCTL);

	return mxc_decode_pll(mpctl, 24000000);
}

static unsigned long get_rate_ppll(void)
{
	ulong ppctl = __raw_readl(CCM_BASE + CCM_PPCTL);

	return mxc_decode_pll(ppctl, 24000000);
}

struct arm_ahb_div {
	unsigned char arm, ahb, sel;
};

static struct arm_ahb_div clk_consumer[] = {
	{ .arm = 1, .ahb = 4, .sel = 0},
	{ .arm = 1, .ahb = 3, .sel = 1},
	{ .arm = 2, .ahb = 2, .sel = 0},
	{ .arm = 0, .ahb = 0, .sel = 0},
	{ .arm = 0, .ahb = 0, .sel = 0},
	{ .arm = 0, .ahb = 0, .sel = 0},
	{ .arm = 4, .ahb = 1, .sel = 0},
	{ .arm = 1, .ahb = 5, .sel = 0},
	{ .arm = 1, .ahb = 8, .sel = 0},
	{ .arm = 1, .ahb = 6, .sel = 1},
	{ .arm = 2, .ahb = 4, .sel = 0},
	{ .arm = 0, .ahb = 0, .sel = 0},
	{ .arm = 0, .ahb = 0, .sel = 0},
	{ .arm = 0, .ahb = 0, .sel = 0},
	{ .arm = 4, .ahb = 2, .sel = 0},
	{ .arm = 0, .ahb = 0, .sel = 0},
};

static unsigned long get_rate_arm(void)
{
	unsigned long pdr0 = __raw_readl(CCM_BASE + CCM_PDR0);
	struct arm_ahb_div *aad;
	unsigned long fref = get_rate_mpll();

	aad = &clk_consumer[(pdr0 >> 16) & 0xf];
	if (aad->sel)
		fref = fref * 3 / 4;

	return fref / aad->arm;
}

static unsigned long get_rate_ahb(struct clk *clk)
{
	unsigned long pdr0 = __raw_readl(CCM_BASE + CCM_PDR0);
	struct arm_ahb_div *aad;
	unsigned long fref = get_rate_arm();

	aad = &clk_consumer[(pdr0 >> 16) & 0xf];

	return fref / aad->ahb;
}

static unsigned long get_rate_ipg(struct clk *clk)
{
	return get_rate_ahb(NULL) >> 1;
}

static unsigned long get_rate_uart(struct clk *clk)
{
	unsigned long pdr3 = __raw_readl(CCM_BASE + CCM_PDR3);
	unsigned long pdr4 = __raw_readl(CCM_BASE + CCM_PDR4);
	unsigned long div = ((pdr4 >> 10) & 0x3f) + 1;

	if (pdr3 & (1 << 14))
		return get_rate_arm() / div;
	else
		return get_rate_ppll() / div;
}

static unsigned long get_rate_sdhc(struct clk *clk)
{
	unsigned long pdr3 = __raw_readl(CCM_BASE + CCM_PDR3);
	unsigned long div, rate;

	if (pdr3 & (1 << 6))
		rate = get_rate_arm();
	else
		rate = get_rate_ppll();

	switch (clk->id) {
	default:
	case 0:
		div = pdr3 & 0x3f;
		break;
	case 1:
		div = (pdr3 >> 8) & 0x3f;
		break;
	case 2:
		div = (pdr3 >> 16) & 0x3f;
		break;
	}

	return rate / (div + 1);
}

static unsigned long get_rate_mshc(struct clk *clk)
{
	unsigned long pdr1 = __raw_readl(CCM_BASE + CCM_PDR1);
	unsigned long div1, div2, rate;

	if (pdr1 & (1 << 7))
		rate = get_rate_arm();
	else
		rate = get_rate_ppll();

	div1 = (pdr1 >> 29) & 0x7;
	div2 = (pdr1 >> 22) & 0x3f;

	return rate / ((div1 + 1) * (div2 + 1));
}

static unsigned long get_rate_ssi(struct clk *clk)
{
	unsigned long pdr2 = __raw_readl(CCM_BASE + CCM_PDR2);
	unsigned long div1, div2, rate;

	if (pdr2 & (1 << 6))
		rate = get_rate_arm();
	else
		rate = get_rate_ppll();

	switch (clk->id) {
	default:
	case 0:
		div1 = pdr2 & 0x3f;
		div2 = (pdr2 >> 24) & 0x7;
		break;
	case 1:
		div1 = (pdr2 >> 8) & 0x3f;
		div2 = (pdr2 >> 27) & 0x7;
		break;
	}

	return rate / ((div1 + 1) * (div2 + 1));
}

static unsigned long get_rate_csi(struct clk *clk)
{
	unsigned long pdr2 = __raw_readl(CCM_BASE + CCM_PDR2);
	unsigned long rate;

	if (pdr2 & (1 << 7))
		rate = get_rate_arm();
	else
		rate = get_rate_ppll();

	return rate / (((pdr2 >> 16) & 0x3f) + 1);
}

static unsigned long get_rate_otg(struct clk *clk)
{
	unsigned long pdr4 = __raw_readl(CCM_BASE + CCM_PDR4);
	unsigned long rate;

	if (pdr4 & (1 << 9))
		rate = get_rate_arm();
	else
		rate = get_rate_ppll();

	return rate / (((pdr4 >> 22) & 0x3f) + 1);
}

static unsigned long get_rate_ipg_per(struct clk *clk)
{
	unsigned long pdr0 = __raw_readl(CCM_BASE + CCM_PDR0);
	unsigned long pdr4 = __raw_readl(CCM_BASE + CCM_PDR4);
	unsigned long div;

	if (pdr0 & (1 << 26)) {
		div = (pdr4 >> 16) & 0x3f;
		return get_rate_arm() / (div + 1);
	} else {
		div = (pdr0 >> 12) & 0x7;
		return get_rate_ahb(NULL) / (div + 1);
	}
}

static unsigned long get_rate_hsp(struct clk *clk)
{
	unsigned long hsp_podf = (__raw_readl(CCM_BASE + CCM_PDR0) >> 20) & 0x03;
	unsigned long fref = get_rate_mpll();

	if (fref > 400 * 1000 * 1000) {
		switch (hsp_podf) {
		case 0:
			return fref >> 2;
		case 1:
			return fref >> 3;
		case 2:
			return fref / 3;
		}
	} else {
		switch (hsp_podf) {
		case 0:
		case 2:
			return fref / 3;
		case 1:
			return fref / 6;
		}
	}

	return 0;
}

static int clk_cgr_enable(struct clk *clk)
{
	u32 reg;

	reg = __raw_readl(clk->enable_reg);
	reg |= 3 << clk->enable_shift;
	__raw_writel(reg, clk->enable_reg);

	return 0;
}

static void clk_cgr_disable(struct clk *clk)
{
	u32 reg;

	reg = __raw_readl(clk->enable_reg);
	reg &= ~(3 << clk->enable_shift);
	__raw_writel(reg, clk->enable_reg);
}

#define DEFINE_CLOCK(name, i, er, es, gr, sr)		\
	static struct clk name = {			\
		.id		= i,			\
		.enable_reg	= CCM_BASE + er,	\
		.enable_shift	= es,			\
		.get_rate	= gr,			\
		.set_rate	= sr,			\
		.enable		= clk_cgr_enable,	\
		.disable	= clk_cgr_disable,	\
	}

DEFINE_CLOCK(asrc_clk,   0, CCM_CGR0,  0, NULL, NULL);
DEFINE_CLOCK(ata_clk,    0, CCM_CGR0,  2, get_rate_ipg, NULL);
/* DEFINE_CLOCK(audmux_clk, 0, CCM_CGR0,  4, NULL, NULL); */
DEFINE_CLOCK(can1_clk,   0, CCM_CGR0,  6, get_rate_ipg, NULL);
DEFINE_CLOCK(can2_clk,   1, CCM_CGR0,  8, get_rate_ipg, NULL);
DEFINE_CLOCK(cspi1_clk,  0, CCM_CGR0, 10, get_rate_ipg, NULL);
DEFINE_CLOCK(cspi2_clk,  1, CCM_CGR0, 12, get_rate_ipg, NULL);
DEFINE_CLOCK(ect_clk,    0, CCM_CGR0, 14, get_rate_ipg, NULL);
DEFINE_CLOCK(edio_clk,   0, CCM_CGR0, 16, NULL, NULL);
DEFINE_CLOCK(emi_clk,    0, CCM_CGR0, 18, get_rate_ipg, NULL);
DEFINE_CLOCK(epit1_clk,  0, CCM_CGR0, 20, get_rate_ipg, NULL);
DEFINE_CLOCK(epit2_clk,  1, CCM_CGR0, 22, get_rate_ipg, NULL);
DEFINE_CLOCK(esai_clk,   0, CCM_CGR0, 24, NULL, NULL);
DEFINE_CLOCK(esdhc1_clk, 0, CCM_CGR0, 26, get_rate_sdhc, NULL);
DEFINE_CLOCK(esdhc2_clk, 1, CCM_CGR0, 28, get_rate_sdhc, NULL);
DEFINE_CLOCK(esdhc3_clk, 2, CCM_CGR0, 30, get_rate_sdhc, NULL);

DEFINE_CLOCK(fec_clk,    0, CCM_CGR1,  0, get_rate_ipg, NULL);
DEFINE_CLOCK(gpio1_clk,  0, CCM_CGR1,  2, NULL, NULL);
DEFINE_CLOCK(gpio2_clk,  1, CCM_CGR1,  4, NULL, NULL);
DEFINE_CLOCK(gpio3_clk,  2, CCM_CGR1,  6, NULL, NULL);
DEFINE_CLOCK(gpt_clk,    0, CCM_CGR1,  8, get_rate_ipg, NULL);
DEFINE_CLOCK(i2c1_clk,   0, CCM_CGR1, 10, get_rate_ipg_per, NULL);
DEFINE_CLOCK(i2c2_clk,   1, CCM_CGR1, 12, get_rate_ipg_per, NULL);
DEFINE_CLOCK(i2c3_clk,   2, CCM_CGR1, 14, get_rate_ipg_per, NULL);
DEFINE_CLOCK(iomuxc_clk, 0, CCM_CGR1, 16, NULL, NULL);
DEFINE_CLOCK(ipu_clk,    0, CCM_CGR1, 18, get_rate_hsp, NULL);
DEFINE_CLOCK(kpp_clk,    0, CCM_CGR1, 20, get_rate_ipg, NULL);
DEFINE_CLOCK(mlb_clk,    0, CCM_CGR1, 22, get_rate_ahb, NULL);
DEFINE_CLOCK(mshc_clk,   0, CCM_CGR1, 24, get_rate_mshc, NULL);
DEFINE_CLOCK(owire_clk,  0, CCM_CGR1, 26, get_rate_ipg_per, NULL);
DEFINE_CLOCK(pwm_clk,    0, CCM_CGR1, 28, get_rate_ipg_per, NULL);
DEFINE_CLOCK(rngc_clk,   0, CCM_CGR1, 30, get_rate_ipg, NULL);

DEFINE_CLOCK(rtc_clk,    0, CCM_CGR2,  0, get_rate_ipg, NULL);
DEFINE_CLOCK(rtic_clk,   0, CCM_CGR2,  2, get_rate_ahb, NULL);
DEFINE_CLOCK(scc_clk,    0, CCM_CGR2,  4, get_rate_ipg, NULL);
DEFINE_CLOCK(sdma_clk,   0, CCM_CGR2,  6, NULL, NULL);
DEFINE_CLOCK(spba_clk,   0, CCM_CGR2,  8, get_rate_ipg, NULL);
DEFINE_CLOCK(spdif_clk,  0, CCM_CGR2, 10, NULL, NULL);
DEFINE_CLOCK(ssi1_clk,   0, CCM_CGR2, 12, get_rate_ssi, NULL);
DEFINE_CLOCK(ssi2_clk,   1, CCM_CGR2, 14, get_rate_ssi, NULL);
DEFINE_CLOCK(uart1_clk,  0, CCM_CGR2, 16, get_rate_uart, NULL);
DEFINE_CLOCK(uart2_clk,  1, CCM_CGR2, 18, get_rate_uart, NULL);
DEFINE_CLOCK(uart3_clk,  2, CCM_CGR2, 20, get_rate_uart, NULL);
DEFINE_CLOCK(usbotg_clk, 0, CCM_CGR2, 22, get_rate_otg, NULL);
DEFINE_CLOCK(wdog_clk,   0, CCM_CGR2, 24, NULL, NULL);
DEFINE_CLOCK(max_clk,    0, CCM_CGR2, 26, NULL, NULL);
DEFINE_CLOCK(audmux_clk, 0, CCM_CGR2, 30, NULL, NULL);

DEFINE_CLOCK(csi_clk,    0, CCM_CGR3,  0, get_rate_csi, NULL);
DEFINE_CLOCK(iim_clk,    0, CCM_CGR3,  2, NULL, NULL);
DEFINE_CLOCK(gpu2d_clk,  0, CCM_CGR3,  4, NULL, NULL);

DEFINE_CLOCK(usbahb_clk, 0, 0,         0, get_rate_ahb, NULL);

static int clk_dummy_enable(struct clk *clk)
{
	return 0;
}

static void clk_dummy_disable(struct clk *clk)
{
}

static unsigned long get_rate_nfc(struct clk *clk)
{
	unsigned long div1;

	div1 = (__raw_readl(CCM_BASE + CCM_PDR4) >> 28) + 1;

	return get_rate_ahb(NULL) / div1;
}

/* NAND Controller: It seems it can't be disabled */
static struct clk nfc_clk = {
	.id		= 0,
	.enable_reg	= 0,
	.enable_shift	= 0,
	.get_rate	= get_rate_nfc,
	.set_rate	= NULL, /* set_rate_nfc, */
	.enable		= clk_dummy_enable,
	.disable	= clk_dummy_disable
};

#define _REGISTER_CLOCK(d, n, c)	\
	{				\
		.dev_id = d,		\
		.con_id = n,		\
		.clk = &c,		\
	},

static struct clk_lookup lookups[] = {
	_REGISTER_CLOCK(NULL, "asrc", asrc_clk)
	_REGISTER_CLOCK(NULL, "ata", ata_clk)
	_REGISTER_CLOCK("flexcan.0", NULL, can1_clk)
	_REGISTER_CLOCK("flexcan.1", NULL, can2_clk)
	_REGISTER_CLOCK("imx35-cspi.0", NULL, cspi1_clk)
	_REGISTER_CLOCK("imx35-cspi.1", NULL, cspi2_clk)
	_REGISTER_CLOCK(NULL, "ect", ect_clk)
	_REGISTER_CLOCK(NULL, "edio", edio_clk)
	_REGISTER_CLOCK(NULL, "emi", emi_clk)
	_REGISTER_CLOCK("imx-epit.0", NULL, epit1_clk)
	_REGISTER_CLOCK("imx-epit.1", NULL, epit2_clk)
	_REGISTER_CLOCK(NULL, "esai", esai_clk)
	_REGISTER_CLOCK("sdhci-esdhc-imx.0", NULL, esdhc1_clk)
	_REGISTER_CLOCK("sdhci-esdhc-imx.1", NULL, esdhc2_clk)
	_REGISTER_CLOCK("sdhci-esdhc-imx.2", NULL, esdhc3_clk)
	_REGISTER_CLOCK("fec.0", NULL, fec_clk)
	_REGISTER_CLOCK(NULL, "gpio", gpio1_clk)
	_REGISTER_CLOCK(NULL, "gpio", gpio2_clk)
	_REGISTER_CLOCK(NULL, "gpio", gpio3_clk)
	_REGISTER_CLOCK("gpt.0", NULL, gpt_clk)
	_REGISTER_CLOCK("imx-i2c.0", NULL, i2c1_clk)
	_REGISTER_CLOCK("imx-i2c.1", NULL, i2c2_clk)
	_REGISTER_CLOCK("imx-i2c.2", NULL, i2c3_clk)
	_REGISTER_CLOCK(NULL, "iomuxc", iomuxc_clk)
	_REGISTER_CLOCK("ipu-core", NULL, ipu_clk)
	_REGISTER_CLOCK("mx3_sdc_fb", NULL, ipu_clk)
	_REGISTER_CLOCK(NULL, "kpp", kpp_clk)
	_REGISTER_CLOCK(NULL, "mlb", mlb_clk)
	_REGISTER_CLOCK(NULL, "mshc", mshc_clk)
	_REGISTER_CLOCK("mxc_w1", NULL, owire_clk)
	_REGISTER_CLOCK(NULL, "pwm", pwm_clk)
	_REGISTER_CLOCK(NULL, "rngc", rngc_clk)
	_REGISTER_CLOCK(NULL, "rtc", rtc_clk)
	_REGISTER_CLOCK(NULL, "rtic", rtic_clk)
	_REGISTER_CLOCK(NULL, "scc", scc_clk)
	_REGISTER_CLOCK("imx-sdma", NULL, sdma_clk)
	_REGISTER_CLOCK(NULL, "spba", spba_clk)
	_REGISTER_CLOCK(NULL, "spdif", spdif_clk)
	_REGISTER_CLOCK("imx-ssi.0", NULL, ssi1_clk)
	_REGISTER_CLOCK("imx-ssi.1", NULL, ssi2_clk)
	_REGISTER_CLOCK("imx-uart.0", NULL, uart1_clk)
	_REGISTER_CLOCK("imx-uart.1", NULL, uart2_clk)
	_REGISTER_CLOCK("imx-uart.2", NULL, uart3_clk)
	_REGISTER_CLOCK("mxc-ehci.0", "usb", usbotg_clk)
	_REGISTER_CLOCK("mxc-ehci.1", "usb", usbotg_clk)
	_REGISTER_CLOCK("mxc-ehci.2", "usb", usbotg_clk)
	_REGISTER_CLOCK("fsl-usb2-udc", "usb", usbotg_clk)
	_REGISTER_CLOCK("fsl-usb2-udc", "usb_ahb", usbahb_clk)
	_REGISTER_CLOCK("imx2-wdt.0", NULL, wdog_clk)
	_REGISTER_CLOCK(NULL, "max", max_clk)
	_REGISTER_CLOCK(NULL, "audmux", audmux_clk)
	_REGISTER_CLOCK(NULL, "csi", csi_clk)
	_REGISTER_CLOCK(NULL, "iim", iim_clk)
	_REGISTER_CLOCK(NULL, "gpu2d", gpu2d_clk)
	_REGISTER_CLOCK("mxc_nand.0", NULL, nfc_clk)
};

int __init mx35_clocks_init()
{
	unsigned int cgr2 = 3 << 26, cgr3 = 0;

#if defined(CONFIG_DEBUG_LL) && !defined(CONFIG_DEBUG_ICEDCC)
	cgr2 |= 3 << 16;
#endif

	clkdev_add_table(lookups, ARRAY_SIZE(lookups));

	/* Turn off all clocks except the ones we need to survive, namely:
	 * EMI, GPIO1/2/3, GPT, IOMUX, MAX and eventually uart
	 */
	__raw_writel((3 << 18), CCM_BASE + CCM_CGR0);
	__raw_writel((3 << 2) | (3 << 4) | (3 << 6) | (3 << 8) | (3 << 16),
			CCM_BASE + CCM_CGR1);

	/*
	 * Check if we came up in internal boot mode. If yes, we need some
	 * extra clocks turned on, otherwise the MX35 boot ROM code will
	 * hang after a watchdog reset.
	 */
	if (!(__raw_readl(CCM_BASE + CCM_RCSR) & (3 << 10))) {
		/* Additionally turn on UART1, SCC, and IIM clocks */
		cgr2 |= 3 << 16 | 3 << 4;
		cgr3 |= 3 << 2;
	}

	__raw_writel(cgr2, CCM_BASE + CCM_CGR2);
	__raw_writel(cgr3, CCM_BASE + CCM_CGR3);

	clk_enable(&iim_clk);
	mx35_read_cpu_rev();

#ifdef CONFIG_MXC_USE_EPIT
	epit_timer_init(&epit1_clk,
			MX35_IO_ADDRESS(MX35_EPIT1_BASE_ADDR), MX35_INT_EPIT1);
#else
	mxc_timer_init(&gpt_clk,
			MX35_IO_ADDRESS(MX35_GPT1_BASE_ADDR), MX35_INT_GPT);
#endif

	return 0;
}

