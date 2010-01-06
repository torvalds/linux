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

static unsigned long get_rate_i2c(struct clk *clk)
{
	return get_rate_per(6);
}

static unsigned long get_rate_nfc(struct clk *clk)
{
	return get_rate_per(8);
}

static unsigned long get_rate_otg(struct clk *clk)
{
	return 48000000; /* FIXME */
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

#define DEFINE_CLOCK(name, i, er, es, gr, sr)		\
	static struct clk name = {			\
		.id		= i,			\
		.enable_reg	= CRM_BASE + er,	\
		.enable_shift	= es,			\
		.get_rate	= gr,			\
		.set_rate	= sr,			\
		.enable		= clk_cgcr_enable,	\
		.disable	= clk_cgcr_disable,	\
	}

DEFINE_CLOCK(gpt_clk,    0, CCM_CGCR0,  5, get_rate_ipg, NULL);
DEFINE_CLOCK(cspi1_clk,  0, CCM_CGCR1,  5, get_rate_ipg, NULL);
DEFINE_CLOCK(cspi2_clk,  0, CCM_CGCR1,  6, get_rate_ipg, NULL);
DEFINE_CLOCK(cspi3_clk,  0, CCM_CGCR1,  7, get_rate_ipg, NULL);
DEFINE_CLOCK(uart1_clk,  0, CCM_CGCR2, 14, get_rate_uart, NULL);
DEFINE_CLOCK(uart2_clk,  0, CCM_CGCR2, 15, get_rate_uart, NULL);
DEFINE_CLOCK(uart3_clk,  0, CCM_CGCR2, 16, get_rate_uart, NULL);
DEFINE_CLOCK(uart4_clk,  0, CCM_CGCR2, 17, get_rate_uart, NULL);
DEFINE_CLOCK(uart5_clk,  0, CCM_CGCR2, 18, get_rate_uart, NULL);
DEFINE_CLOCK(nfc_clk,    0, CCM_CGCR0,  8, get_rate_nfc, NULL);
DEFINE_CLOCK(usbotg_clk, 0, CCM_CGCR0, 28, get_rate_otg, NULL);
DEFINE_CLOCK(pwm1_clk,	 0, CCM_CGCR1, 31, get_rate_ipg, NULL);
DEFINE_CLOCK(pwm2_clk,	 0, CCM_CGCR2,  0, get_rate_ipg, NULL);
DEFINE_CLOCK(pwm3_clk,	 0, CCM_CGCR2,  1, get_rate_ipg, NULL);
DEFINE_CLOCK(pwm4_clk,	 0, CCM_CGCR2,  2, get_rate_ipg, NULL);
DEFINE_CLOCK(kpp_clk,	 0, CCM_CGCR1, 28, get_rate_ipg, NULL);
DEFINE_CLOCK(tsc_clk,	 0, CCM_CGCR2, 13, get_rate_ipg, NULL);
DEFINE_CLOCK(i2c_clk,	 0, CCM_CGCR0,  6, get_rate_i2c, NULL);
DEFINE_CLOCK(fec_clk,	 0, CCM_CGCR0, 23, get_rate_ipg, NULL);

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
	_REGISTER_CLOCK("spi_imx.0", NULL, cspi1_clk)
	_REGISTER_CLOCK("spi_imx.1", NULL, cspi2_clk)
	_REGISTER_CLOCK("spi_imx.2", NULL, cspi3_clk)
	_REGISTER_CLOCK("mxc_pwm.0", NULL, pwm1_clk)
	_REGISTER_CLOCK("mxc_pwm.1", NULL, pwm2_clk)
	_REGISTER_CLOCK("mxc_pwm.2", NULL, pwm3_clk)
	_REGISTER_CLOCK("mxc_pwm.3", NULL, pwm4_clk)
	_REGISTER_CLOCK("mxc-keypad", NULL, kpp_clk)
	_REGISTER_CLOCK("mx25-adc", NULL, tsc_clk)
	_REGISTER_CLOCK("imx-i2c.0", NULL, i2c_clk)
	_REGISTER_CLOCK("imx-i2c.1", NULL, i2c_clk)
	_REGISTER_CLOCK("imx-i2c.2", NULL, i2c_clk)
	_REGISTER_CLOCK("fec.0", NULL, fec_clk)
};

int __init mx25_clocks_init(unsigned long fref)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(lookups); i++)
		clkdev_add(&lookups[i]);

	mxc_timer_init(&gpt_clk, MX25_IO_ADDRESS(MX25_GPT1_BASE_ADDR), 54);

	return 0;
}
