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
#include <mach/mx25.h>

#define CRM_BASE	MX25_IO_ADDRESS(MX25_CRM_BASE_ADDR)

#define CCM_MPCTL	0x00
#define CCM_UPCTL	0x04
#define CCM_CCTL	0x08
#define CCM_CGCR0	0x0C
#define CCM_CGCR1	0x10
#define CCM_CGCR2	0x14
#define CCM_PCDR0	0x18
#define CCM_PCDR1	0x1C
#define CCM_PCDR2	0x20
#define CCM_PCDR3	0x24
#define CCM_RCSR	0x28
#define CCM_CRDR	0x2C
#define CCM_DCVR0	0x30
#define CCM_DCVR1	0x34
#define CCM_DCVR2	0x38
#define CCM_DCVR3	0x3c
#define CCM_LTR0	0x40
#define CCM_LTR1	0x44
#define CCM_LTR2	0x48
#define CCM_LTR3	0x4c

static unsigned long get_rate_mpll(void)
{
	ulong mpctl = __raw_readl(CRM_BASE + CCM_MPCTL);

	return mxc_decode_pll(mpctl, 24000000);
}

static unsigned long get_rate_upll(void)
{
	ulong mpctl = __raw_readl(CRM_BASE + CCM_UPCTL);

	return mxc_decode_pll(mpctl, 24000000);
}

unsigned long get_rate_arm(struct clk *clk)
{
	unsigned long cctl = readl(CRM_BASE + CCM_CCTL);
	unsigned long rate = get_rate_mpll();

	if (cctl & (1 << 14))
		rate = (rate * 3) >> 1;

	return rate / ((cctl >> 30) + 1);
}

static unsigned long get_rate_ahb(struct clk *clk)
{
	unsigned long cctl = readl(CRM_BASE + CCM_CCTL);

	return get_rate_arm(NULL) / (((cctl >> 28) & 0x3) + 1);
}

static unsigned long get_rate_ipg(struct clk *clk)
{
	return get_rate_ahb(NULL) >> 1;
}

static unsigned long get_rate_per(int per)
{
	unsigned long ofs = (per & 0x3) * 8;
	unsigned long reg = per & ~0x3;
	unsigned long val = (readl(CRM_BASE + CCM_PCDR0 + reg) >> ofs) & 0x3f;
	unsigned long fref;

	if (readl(CRM_BASE + 0x64) & (1 << per))
		fref = get_rate_upll();
	else
		fref = get_rate_ipg(NULL);

	return fref / (val + 1);
}

static unsigned long get_rate_uart(struct clk *clk)
{
	return get_rate_per(15);
}

static unsigned long get_rate_ssi2(struct clk *clk)
{
	return get_rate_per(14);
}

static unsigned long get_rate_ssi1(struct clk *clk)
{
	return get_rate_per(13);
}

static unsigned long get_rate_i2c(struct clk *clk)
{
	return get_rate_per(6);
}

static unsigned long get_rate_nfc(struct clk *clk)
{
	return get_rate_per(8);
}

static unsigned long get_rate_gpt(struct clk *clk)
{
	return get_rate_per(5);
}

static unsigned long get_rate_lcdc(struct clk *clk)
{
	return get_rate_per(7);
}

static unsigned long get_rate_csi(struct clk *clk)
{
	return get_rate_per(0);
}

static unsigned long get_rate_otg(struct clk *clk)
{
	unsigned long cctl = readl(CRM_BASE + CCM_CCTL);
	unsigned long rate = get_rate_upll();

	return (cctl & (1 << 23)) ? 0 : rate / ((0x3F & (cctl >> 16)) + 1);
}

static int clk_cgcr_enable(struct clk *clk)
{
	u32 reg;

	reg = __raw_readl(clk->enable_reg);
	reg |= 1 << clk->enable_shift;
	__raw_writel(reg, clk->enable_reg);

	return 0;
}

static void clk_cgcr_disable(struct clk *clk)
{
	u32 reg;

	reg = __raw_readl(clk->enable_reg);
	reg &= ~(1 << clk->enable_shift);
	__raw_writel(reg, clk->enable_reg);
}

#define DEFINE_CLOCK(name, i, er, es, gr, sr, s)	\
	static struct clk name = {			\
		.id		= i,			\
		.enable_reg	= CRM_BASE + er,	\
		.enable_shift	= es,			\
		.get_rate	= gr,			\
		.set_rate	= sr,			\
		.enable		= clk_cgcr_enable,	\
		.disable	= clk_cgcr_disable,	\
		.secondary	= s,			\
	}

/*
 * Note: the following IPG clock gating bits are wrongly marked "Reserved" in
 * the i.MX25 Reference Manual Rev 1, table 15-13. The information below is
 * taken from the Freescale released BSP.
 *
 * bit	reg	offset	clock
 *
 * 0	CGCR1	0	AUDMUX
 * 12	CGCR1	12	ESAI
 * 16	CGCR1	16	GPIO1
 * 17	CGCR1	17	GPIO2
 * 18	CGCR1	18	GPIO3
 * 23	CGCR1	23	I2C1
 * 24	CGCR1	24	I2C2
 * 25	CGCR1	25	I2C3
 * 27	CGCR1	27	IOMUXC
 * 28	CGCR1	28	KPP
 * 30	CGCR1	30	OWIRE
 * 36	CGCR2	4	RTIC
 * 51	CGCR2	19	WDOG
 */

DEFINE_CLOCK(gpt_clk,    0, CCM_CGCR0,  5, get_rate_gpt, NULL, NULL);
DEFINE_CLOCK(uart_per_clk, 0, CCM_CGCR0, 15, get_rate_uart, NULL, NULL);
DEFINE_CLOCK(ssi1_per_clk, 0, CCM_CGCR0, 13, get_rate_ipg, NULL, NULL);
DEFINE_CLOCK(ssi2_per_clk, 0, CCM_CGCR0, 14, get_rate_ipg, NULL, NULL);
DEFINE_CLOCK(cspi1_clk,  0, CCM_CGCR1,  5, get_rate_ipg, NULL, NULL);
DEFINE_CLOCK(cspi2_clk,  0, CCM_CGCR1,  6, get_rate_ipg, NULL, NULL);
DEFINE_CLOCK(cspi3_clk,  0, CCM_CGCR1,  7, get_rate_ipg, NULL, NULL);
DEFINE_CLOCK(fec_ahb_clk, 0, CCM_CGCR0, 23, NULL,	 NULL, NULL);
DEFINE_CLOCK(lcdc_ahb_clk, 0, CCM_CGCR0, 24, NULL,	 NULL, NULL);
DEFINE_CLOCK(lcdc_per_clk, 0, CCM_CGCR0,  7, NULL,	 NULL, &lcdc_ahb_clk);
DEFINE_CLOCK(csi_ahb_clk, 0, CCM_CGCR0, 18, get_rate_csi, NULL, NULL);
DEFINE_CLOCK(csi_per_clk, 0, CCM_CGCR0, 0, get_rate_csi, NULL, &csi_ahb_clk);
DEFINE_CLOCK(uart1_clk,  0, CCM_CGCR2, 14, get_rate_uart, NULL, &uart_per_clk);
DEFINE_CLOCK(uart2_clk,  0, CCM_CGCR2, 15, get_rate_uart, NULL, &uart_per_clk);
DEFINE_CLOCK(uart3_clk,  0, CCM_CGCR2, 16, get_rate_uart, NULL, &uart_per_clk);
DEFINE_CLOCK(uart4_clk,  0, CCM_CGCR2, 17, get_rate_uart, NULL, &uart_per_clk);
DEFINE_CLOCK(uart5_clk,  0, CCM_CGCR2, 18, get_rate_uart, NULL, &uart_per_clk);
DEFINE_CLOCK(nfc_clk,    0, CCM_CGCR0,  8, get_rate_nfc, NULL, NULL);
DEFINE_CLOCK(usbotg_clk, 0, CCM_CGCR0, 28, get_rate_otg, NULL, NULL);
DEFINE_CLOCK(pwm1_clk,	 0, CCM_CGCR1, 31, get_rate_ipg, NULL, NULL);
DEFINE_CLOCK(pwm2_clk,	 0, CCM_CGCR2,  0, get_rate_ipg, NULL, NULL);
DEFINE_CLOCK(pwm3_clk,	 0, CCM_CGCR2,  1, get_rate_ipg, NULL, NULL);
DEFINE_CLOCK(pwm4_clk,	 0, CCM_CGCR2,  2, get_rate_ipg, NULL, NULL);
DEFINE_CLOCK(kpp_clk,	 0, CCM_CGCR1, 28, get_rate_ipg, NULL, NULL);
DEFINE_CLOCK(tsc_clk,	 0, CCM_CGCR2, 13, get_rate_ipg, NULL, NULL);
DEFINE_CLOCK(i2c_clk,	 0, CCM_CGCR0,  6, get_rate_i2c, NULL, NULL);
DEFINE_CLOCK(fec_clk,	 0, CCM_CGCR1, 15, get_rate_ipg, NULL, &fec_ahb_clk);
DEFINE_CLOCK(dryice_clk, 0, CCM_CGCR1,  8, get_rate_ipg, NULL, NULL);
DEFINE_CLOCK(lcdc_clk,	 0, CCM_CGCR1, 29, get_rate_lcdc, NULL, &lcdc_per_clk);
DEFINE_CLOCK(wdt_clk,    0, CCM_CGCR2, 19, get_rate_ipg, NULL,  NULL);
DEFINE_CLOCK(ssi1_clk,  0, CCM_CGCR2, 11, get_rate_ssi1, NULL, &ssi1_per_clk);
DEFINE_CLOCK(ssi2_clk,  1, CCM_CGCR2, 12, get_rate_ssi2, NULL, &ssi2_per_clk);
DEFINE_CLOCK(audmux_clk, 0, CCM_CGCR1, 0, NULL, NULL, NULL);
DEFINE_CLOCK(csi_clk,    0, CCM_CGCR1,  4, get_rate_csi, NULL,  &csi_per_clk);
DEFINE_CLOCK(can1_clk,	 0, CCM_CGCR1,  2, get_rate_ipg, NULL, NULL);
DEFINE_CLOCK(can2_clk,	 0, CCM_CGCR1,  3, get_rate_ipg, NULL, NULL);

#define _REGISTER_CLOCK(d, n, c)	\
	{				\
		.dev_id = d,		\
		.con_id = n,		\
		.clk = &c,		\
	},

static struct clk_lookup lookups[] = {
	_REGISTER_CLOCK("imx-uart.0", NULL, uart1_clk)
	_REGISTER_CLOCK("imx-uart.1", NULL, uart2_clk)
	_REGISTER_CLOCK("imx-uart.2", NULL, uart3_clk)
	_REGISTER_CLOCK("imx-uart.3", NULL, uart4_clk)
	_REGISTER_CLOCK("imx-uart.4", NULL, uart5_clk)
	_REGISTER_CLOCK("mxc-ehci.0", "usb", usbotg_clk)
	_REGISTER_CLOCK("mxc-ehci.1", "usb", usbotg_clk)
	_REGISTER_CLOCK("mxc-ehci.2", "usb", usbotg_clk)
	_REGISTER_CLOCK("fsl-usb2-udc", "usb", usbotg_clk)
	_REGISTER_CLOCK("mxc_nand.0", NULL, nfc_clk)
	_REGISTER_CLOCK("imx25-cspi.0", NULL, cspi1_clk)
	_REGISTER_CLOCK("imx25-cspi.1", NULL, cspi2_clk)
	_REGISTER_CLOCK("imx25-cspi.2", NULL, cspi3_clk)
	_REGISTER_CLOCK("mxc_pwm.0", NULL, pwm1_clk)
	_REGISTER_CLOCK("mxc_pwm.1", NULL, pwm2_clk)
	_REGISTER_CLOCK("mxc_pwm.2", NULL, pwm3_clk)
	_REGISTER_CLOCK("mxc_pwm.3", NULL, pwm4_clk)
	_REGISTER_CLOCK("imx-keypad", NULL, kpp_clk)
	_REGISTER_CLOCK("mx25-adc", NULL, tsc_clk)
	_REGISTER_CLOCK("imx-i2c.0", NULL, i2c_clk)
	_REGISTER_CLOCK("imx-i2c.1", NULL, i2c_clk)
	_REGISTER_CLOCK("imx-i2c.2", NULL, i2c_clk)
	_REGISTER_CLOCK("fec.0", NULL, fec_clk)
	_REGISTER_CLOCK("imxdi_rtc.0", NULL, dryice_clk)
	_REGISTER_CLOCK("imx-fb.0", NULL, lcdc_clk)
	_REGISTER_CLOCK("imx-wdt.0", NULL, wdt_clk)
	_REGISTER_CLOCK("imx-ssi.0", NULL, ssi1_clk)
	_REGISTER_CLOCK("imx-ssi.1", NULL, ssi2_clk)
	_REGISTER_CLOCK("mx2-camera.0", NULL, csi_clk)
	_REGISTER_CLOCK(NULL, "audmux", audmux_clk)
	_REGISTER_CLOCK("flexcan.0", NULL, can1_clk)
	_REGISTER_CLOCK("flexcan.1", NULL, can2_clk)
};

int __init mx25_clocks_init(void)
{
	clkdev_add_table(lookups, ARRAY_SIZE(lookups));

	/* Turn off all clocks except the ones we need to survive, namely:
	 * EMI, GPIO1-3 (CCM_CGCR1[18:16]), GPT1, IOMUXC (CCM_CGCR1[27]), IIM,
	 * SCC
	 */
	__raw_writel((1 << 19), CRM_BASE + CCM_CGCR0);
	__raw_writel((0xf << 16) | (3 << 26), CRM_BASE + CCM_CGCR1);
	__raw_writel((1 << 5), CRM_BASE + CCM_CGCR2);
#if defined(CONFIG_DEBUG_LL) && !defined(CONFIG_DEBUG_ICEDCC)
	clk_enable(&uart1_clk);
#endif

	/* Clock source for lcdc and csi is upll */
	__raw_writel(__raw_readl(CRM_BASE+0x64) | (1 << 7) | (1 << 0),
			CRM_BASE + 0x64);

	mxc_timer_init(&gpt_clk, MX25_IO_ADDRESS(MX25_GPT1_BASE_ADDR), 54);

	return 0;
}
