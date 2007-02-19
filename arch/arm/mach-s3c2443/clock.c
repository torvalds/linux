/* linux/arch/arm/mach-s3c2443/clock.c
 *
 * Copyright (c) 2007 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2443 Clock control support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/sysdev.h>
#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/serial_core.h>

#include <asm/mach/map.h>

#include <asm/hardware.h>
#include <asm/io.h>

#include <asm/arch/regs-s3c2443-clock.h>

#include <asm/plat-s3c24xx/s3c2443.h>
#include <asm/plat-s3c24xx/clock.h>
#include <asm/plat-s3c24xx/cpu.h>

/* We currently have to assume that the system is running
 * from the XTPll input, and that all ***REFCLKs are being
 * fed from it, as we cannot read the state of OM[4] from
 * software.
 *
 * It would be possible for each board initialisation to
 * set the correct muxing at initialisation
*/

static int s3c2443_clkcon_enable_h(struct clk *clk, int enable)
{
	unsigned int clocks = clk->ctrlbit;
	unsigned long clkcon;

	clkcon = __raw_readl(S3C2443_HCLKCON);

	if (enable)
		clkcon |= clocks;
	else
		clkcon &= ~clocks;

	__raw_writel(clkcon, S3C2443_HCLKCON);

	return 0;
}

static int s3c2443_clkcon_enable_p(struct clk *clk, int enable)
{
	unsigned int clocks = clk->ctrlbit;
	unsigned long clkcon;

	clkcon = __raw_readl(S3C2443_PCLKCON);

	if (enable)
		clkcon |= clocks;
	else
		clkcon &= ~clocks;

	__raw_writel(clkcon, S3C2443_HCLKCON);

	return 0;
}

static int s3c2443_clkcon_enable_s(struct clk *clk, int enable)
{
	unsigned int clocks = clk->ctrlbit;
	unsigned long clkcon;

	clkcon = __raw_readl(S3C2443_SCLKCON);

	if (enable)
		clkcon |= clocks;
	else
		clkcon &= ~clocks;

	__raw_writel(clkcon, S3C2443_SCLKCON);

	return 0;
}

static unsigned long s3c2443_roundrate_clksrc(struct clk *clk,
					      unsigned long rate,
					      unsigned int max)
{
	unsigned long parent_rate = clk_get_rate(clk->parent);
	int div;

	if (rate > parent_rate)
		return parent_rate;

	/* note, we remove the +/- 1 calculations as they cancel out */

	div = (rate / parent_rate);

	if (div < 1)
		div = 1;
	else if (div > max)
		div = max;

	return parent_rate / div;
}

static unsigned long s3c2443_roundrate_clksrc4(struct clk *clk,
					       unsigned long rate)
{
	return s3c2443_roundrate_clksrc(clk, rate, 4);
}

static unsigned long s3c2443_roundrate_clksrc16(struct clk *clk,
						unsigned long rate)
{
	return s3c2443_roundrate_clksrc(clk, rate, 16);
}

static unsigned long s3c2443_roundrate_clksrc256(struct clk *clk,
						 unsigned long rate)
{
	return s3c2443_roundrate_clksrc(clk, rate, 256);
}

/* clock selections */

/* CPU EXTCLK input */
static struct clk clk_ext = {
	.name		= "ext",
	.id		= -1,
};

static struct clk clk_mpllref = {
	.name		= "mpllref",
	.parent		= &clk_xtal,
	.id		= -1,
};

#if 0
static struct clk clk_mpll = {
	.name		= "mpll",
	.parent		= &clk_mpllref,
	.id		= -1,
};
#endif

static struct clk clk_epllref;

static struct clk clk_epll = {
	.name		= "epll",
	.parent		= &clk_epllref,
	.id		= -1,
};

static struct clk clk_i2s_ext = {
	.name		= "i2s-ext",
	.id		= -1,
};

static int s3c2443_setparent_epllref(struct clk *clk, struct clk *parent)
{
	unsigned long clksrc = __raw_readl(S3C2443_CLKSRC);

	clksrc &= ~S3C2443_CLKSRC_EPLLREF_MASK;

	if (parent == &clk_xtal)
		clksrc |= S3C2443_CLKSRC_EPLLREF_XTAL;
	else if (parent == &clk_ext)
		clksrc |= S3C2443_CLKSRC_EPLLREF_EXTCLK;
	else if (parent != &clk_mpllref)
		return -EINVAL;

	__raw_writel(clksrc, S3C2443_CLKSRC);
	clk->parent = parent;

	return 0;
}

static struct clk clk_epllref = {
	.name		= "epllref",
	.id		= -1,
	.set_parent	= s3c2443_setparent_epllref,
};

static unsigned long s3c2443_getrate_mdivclk(struct clk *clk)
{
	unsigned long parent_rate = clk_get_rate(clk->parent);
	unsigned long div = __raw_readl(S3C2443_CLKDIV0);

	div  &= S3C2443_CLKDIV0_EXTDIV_MASK;
	div >>= (S3C2443_CLKDIV0_EXTDIV_SHIFT-1);	/* x2 */

	return parent_rate / (div + 1);
}

static struct clk clk_mdivclk = {
	.name		= "mdivclk",
	.parent		= &clk_mpllref,
	.id		= -1,
	.get_rate	= s3c2443_getrate_mdivclk,
};


static int s3c2443_setparent_msysclk(struct clk *clk, struct clk *parent)
{
	unsigned long clksrc = __raw_readl(S3C2443_CLKSRC);

	clksrc &= ~(S3C2443_CLKSRC_MSYSCLK_MPLL |
		    S3C2443_CLKSRC_EXTCLK_DIV);

	if (parent == &clk_mpll)
		clksrc |= S3C2443_CLKSRC_MSYSCLK_MPLL;
	else if (parent == &clk_mdivclk)
		clksrc |= S3C2443_CLKSRC_EXTCLK_DIV;
	else if (parent != &clk_mpllref)
		return -EINVAL;

	__raw_writel(clksrc, S3C2443_CLKSRC);
	clk->parent = parent;

	return 0;
}

static struct clk clk_msysclk = {
	.name		= "msysclk",
	.parent		= &clk_xtal,
	.id		= -1,
	.set_parent	= s3c2443_setparent_msysclk,
};


/* esysclk
 *
 * this is sourced from either the EPLL or the EPLLref clock
*/

static int s3c2443_setparent_esysclk(struct clk *clk, struct clk *parent)
{
	unsigned long clksrc = __raw_readl(S3C2443_CLKSRC);

	if (parent == &clk_epll)
		clksrc |= S3C2443_CLKSRC_ESYSCLK_EPLL;
	else if (parent == &clk_epllref)
		clksrc &= ~S3C2443_CLKSRC_ESYSCLK_EPLL;
	else
		return -EINVAL;

	__raw_writel(clksrc, S3C2443_CLKSRC);
	clk->parent = parent;

	return 0;
}

static struct clk clk_esysclk = {
	.name		= "esysclk",
	.parent		= &clk_epll,
	.id		= -1,
	.set_parent	= s3c2443_setparent_esysclk,
};

/* uartclk
 *
 * UART baud-rate clock sourced from esysclk via a divisor
*/

static unsigned long s3c2443_getrate_uart(struct clk *clk)
{
	unsigned long parent_rate = clk_get_rate(clk->parent);
	unsigned long div = __raw_readl(S3C2443_CLKDIV1);

	div &= S3C2443_CLKDIV1_UARTDIV_MASK;
	div >>= S3C2443_CLKDIV1_UARTDIV_SHIFT;

	return parent_rate / (div + 1);
}


static int s3c2443_setrate_uart(struct clk *clk, unsigned long rate)
{
	unsigned long parent_rate = clk_get_rate(clk->parent);
	unsigned long clkdivn = __raw_readl(S3C2443_CLKDIV1);

	rate = s3c2443_roundrate_clksrc16(clk, rate);
	rate = parent_rate / rate;

	clkdivn &= ~S3C2443_CLKDIV1_UARTDIV_MASK;
	clkdivn |= (rate - 1) << S3C2443_CLKDIV1_UARTDIV_SHIFT;

	__raw_writel(clkdivn, S3C2443_CLKDIV1);
	return 0;
}

static struct clk clk_uart = {
	.name		= "uartclk",
	.id		= -1,
	.parent		= &clk_esysclk,
	.get_rate	= s3c2443_getrate_uart,
	.set_rate	= s3c2443_setrate_uart,
	.round_rate	= s3c2443_roundrate_clksrc16,
};

/* hsspi
 *
 * high-speed spi clock, sourced from esysclk
*/

static unsigned long s3c2443_getrate_hsspi(struct clk *clk)
{
	unsigned long parent_rate = clk_get_rate(clk->parent);
	unsigned long div = __raw_readl(S3C2443_CLKDIV1);

	div &= S3C2443_CLKDIV1_HSSPIDIV_MASK;
	div >>= S3C2443_CLKDIV1_HSSPIDIV_SHIFT;

	return parent_rate / (div + 1);
}


static int s3c2443_setrate_hsspi(struct clk *clk, unsigned long rate)
{
	unsigned long parent_rate = clk_get_rate(clk->parent);
	unsigned long clkdivn = __raw_readl(S3C2443_CLKDIV1);

	rate = s3c2443_roundrate_clksrc4(clk, rate);
	rate = parent_rate / rate;

	clkdivn &= ~S3C2443_CLKDIV1_HSSPIDIV_MASK;
	clkdivn |= (rate - 1) << S3C2443_CLKDIV1_HSSPIDIV_SHIFT;

	__raw_writel(clkdivn, S3C2443_CLKDIV1);
	return 0;
}

static struct clk clk_hsspi = {
	.name		= "hsspi",
	.id		= -1,
	.parent		= &clk_esysclk,
	.ctrlbit	= S3C2443_SCLKCON_HSSPICLK,
	.enable		= s3c2443_clkcon_enable_s,
	.get_rate	= s3c2443_getrate_hsspi,
	.set_rate	= s3c2443_setrate_hsspi,
	.round_rate	= s3c2443_roundrate_clksrc4,
};

/* usbhost
 *
 * usb host bus-clock, usually 48MHz to provide USB bus clock timing
*/

static unsigned long s3c2443_getrate_usbhost(struct clk *clk)
{
	unsigned long parent_rate = clk_get_rate(clk->parent);
	unsigned long div = __raw_readl(S3C2443_CLKDIV1);

	div &= S3C2443_CLKDIV1_USBHOSTDIV_MASK;
	div >>= S3C2443_CLKDIV1_USBHOSTDIV_SHIFT;

	return parent_rate / (div + 1);
}

static int s3c2443_setrate_usbhost(struct clk *clk, unsigned long rate)
{
	unsigned long parent_rate = clk_get_rate(clk->parent);
	unsigned long clkdivn = __raw_readl(S3C2443_CLKDIV1);

	rate = s3c2443_roundrate_clksrc4(clk, rate);
	rate = parent_rate / rate;

	clkdivn &= ~S3C2443_CLKDIV1_USBHOSTDIV_MASK;
	clkdivn |= (rate - 1) << S3C2443_CLKDIV1_USBHOSTDIV_SHIFT;

	__raw_writel(clkdivn, S3C2443_CLKDIV1);
	return 0;
}

struct clk clk_usb_bus_host = {
	.name		= "usb-bus-host-parent",
	.id		= -1,
	.parent		= &clk_esysclk,
	.ctrlbit	= S3C2443_SCLKCON_USBHOST,
	.enable		= s3c2443_clkcon_enable_s,
	.get_rate	= s3c2443_getrate_usbhost,
	.set_rate	= s3c2443_setrate_usbhost,
	.round_rate	= s3c2443_roundrate_clksrc4,
};

/* clk_hsmcc_div
 *
 * this clock is sourced from epll, and is fed through a divider,
 * to a mux controlled by sclkcon where either it or a extclk can
 * be fed to the hsmmc block
*/

static unsigned long s3c2443_getrate_hsmmc_div(struct clk *clk)
{
	unsigned long parent_rate = clk_get_rate(clk->parent);
	unsigned long div = __raw_readl(S3C2443_CLKDIV1);

	div &= S3C2443_CLKDIV1_HSMMCDIV_MASK;
	div >>= S3C2443_CLKDIV1_HSMMCDIV_SHIFT;

	return parent_rate / (div + 1);
}

static int s3c2443_setrate_hsmmc_div(struct clk *clk, unsigned long rate)
{
	unsigned long parent_rate = clk_get_rate(clk->parent);
	unsigned long clkdivn = __raw_readl(S3C2443_CLKDIV1);

	rate = s3c2443_roundrate_clksrc4(clk, rate);
	rate = parent_rate / rate;

	clkdivn &= ~S3C2443_CLKDIV1_HSMMCDIV_MASK;
	clkdivn |= (rate - 1) << S3C2443_CLKDIV1_HSMMCDIV_SHIFT;

	__raw_writel(clkdivn, S3C2443_CLKDIV1);
	return 0;
}

static struct clk clk_hsmmc_div = {
	.name		= "hsmmc-div",
	.id		= -1,
	.parent		= &clk_esysclk,
	.get_rate	= s3c2443_getrate_hsmmc_div,
	.set_rate	= s3c2443_setrate_hsmmc_div,
	.round_rate	= s3c2443_roundrate_clksrc4,
};

static int s3c2443_setparent_hsmmc(struct clk *clk, struct clk *parent)
{
	unsigned long clksrc = __raw_readl(S3C2443_SCLKCON);

	clksrc &= ~(S3C2443_SCLKCON_HSMMCCLK_EXT |
		    S3C2443_SCLKCON_HSMMCCLK_EPLL);

	if (parent == &clk_epll)
		clksrc |= S3C2443_SCLKCON_HSMMCCLK_EPLL;
	else if (parent == &clk_ext)
		clksrc |= S3C2443_SCLKCON_HSMMCCLK_EXT;
	else
		return -EINVAL;

	if (clk->usage > 0) {
		__raw_writel(clksrc, S3C2443_SCLKCON);
	}

	clk->parent = parent;
	return 0;
}

static int s3c2443_enable_hsmmc(struct clk *clk, int enable)
{
	return s3c2443_setparent_hsmmc(clk, clk->parent);
}

static struct clk clk_hsmmc = {
	.name		= "hsmmc-if",
	.id		= -1,
	.parent		= &clk_hsmmc_div,
	.enable		= s3c2443_enable_hsmmc,
	.set_parent	= s3c2443_setparent_hsmmc,
};

/* i2s_eplldiv
 *
 * this clock is the output from the i2s divisor of esysclk
*/

static unsigned long s3c2443_getrate_i2s_eplldiv(struct clk *clk)
{
	unsigned long parent_rate = clk_get_rate(clk->parent);
	unsigned long div = __raw_readl(S3C2443_CLKDIV1);

	div &= S3C2443_CLKDIV1_I2SDIV_MASK;
	div >>= S3C2443_CLKDIV1_I2SDIV_SHIFT;

	return parent_rate / (div + 1);
}

static int s3c2443_setrate_i2s_eplldiv(struct clk *clk, unsigned long rate)
{
	unsigned long parent_rate = clk_get_rate(clk->parent);
	unsigned long clkdivn = __raw_readl(S3C2443_CLKDIV1);

	rate = s3c2443_roundrate_clksrc16(clk, rate);
	rate = parent_rate / rate;

	clkdivn &= ~S3C2443_CLKDIV1_I2SDIV_MASK;
	clkdivn |= (rate - 1) << S3C2443_CLKDIV1_I2SDIV_SHIFT;

	__raw_writel(clkdivn, S3C2443_CLKDIV1);
	return 0;
}

static struct clk clk_i2s_eplldiv = {
	.name		= "i2s-eplldiv",
	.id		= -1,
	.parent		= &clk_esysclk,
	.get_rate	= s3c2443_getrate_i2s_eplldiv,
	.set_rate	= s3c2443_setrate_i2s_eplldiv,
	.round_rate	= s3c2443_roundrate_clksrc16,
};

/* i2s-ref
 *
 * i2s bus reference clock, selectable from external, esysclk or epllref
*/

static int s3c2443_setparent_i2s(struct clk *clk, struct clk *parent)
{
	unsigned long clksrc = __raw_readl(S3C2443_CLKSRC);

	clksrc &= ~S3C2443_CLKSRC_I2S_MASK;

	if (parent == &clk_epllref)
		clksrc |= S3C2443_CLKSRC_I2S_EPLLREF;
	else if (parent == &clk_i2s_ext)
		clksrc |= S3C2443_CLKSRC_I2S_EXT;
	else if (parent != &clk_i2s_eplldiv)
		return -EINVAL;

	clk->parent = parent;
	__raw_writel(clksrc, S3C2443_CLKSRC);

	return 0;
}

static struct clk clk_i2s = {
	.name		= "i2s-if",
	.id		= -1,
	.parent		= &clk_i2s_eplldiv,
	.ctrlbit	= S3C2443_SCLKCON_I2SCLK,
	.enable		= s3c2443_clkcon_enable_s,
	.set_parent	= s3c2443_setparent_i2s,
};

/* cam-if
 *
 * camera interface bus-clock, divided down from esysclk
*/

static unsigned long s3c2443_getrate_cam(struct clk *clk)
{
	unsigned long parent_rate = clk_get_rate(clk->parent);
	unsigned long div = __raw_readl(S3C2443_CLKDIV1);

	div  &= S3C2443_CLKDIV1_CAMDIV_MASK;
	div >>= S3C2443_CLKDIV1_CAMDIV_SHIFT;

	return parent_rate / (div + 1);
}

static int s3c2443_setrate_cam(struct clk *clk, unsigned long rate)
{
	unsigned long parent_rate = clk_get_rate(clk->parent);
	unsigned long clkdiv1 = __raw_readl(S3C2443_CLKDIV1);

	rate = s3c2443_roundrate_clksrc16(clk, rate);
	rate = parent_rate / rate;

	clkdiv1 &= ~S3C2443_CLKDIV1_CAMDIV_MASK;
	clkdiv1 |= (rate - 1) << S3C2443_CLKDIV1_CAMDIV_SHIFT;

	__raw_writel(clkdiv1, S3C2443_CLKDIV1);
	return 0;
}

static struct clk clk_cam = {
	.name		= "camif-upll",		/* same as 2440 name */
	.id		= -1,
	.parent		= &clk_esysclk,
	.ctrlbit	= S3C2443_SCLKCON_CAMCLK,
	.enable		= s3c2443_clkcon_enable_s,
	.get_rate	= s3c2443_getrate_cam,
	.set_rate	= s3c2443_setrate_cam,
	.round_rate	= s3c2443_roundrate_clksrc16,
};

/* display-if
 *
 * display interface clock, divided from esysclk
*/

static unsigned long s3c2443_getrate_display(struct clk *clk)
{
	unsigned long parent_rate = clk_get_rate(clk->parent);
	unsigned long div = __raw_readl(S3C2443_CLKDIV1);

	div &= S3C2443_CLKDIV1_DISPDIV_MASK;
	div >>= S3C2443_CLKDIV1_DISPDIV_SHIFT;

	return parent_rate / (div + 1);
}

static int s3c2443_setrate_display(struct clk *clk, unsigned long rate)
{
	unsigned long parent_rate = clk_get_rate(clk->parent);
	unsigned long clkdivn = __raw_readl(S3C2443_CLKDIV1);

	rate = s3c2443_roundrate_clksrc256(clk, rate);
	rate = parent_rate / rate;

	clkdivn &= ~S3C2443_CLKDIV1_UARTDIV_MASK;
	clkdivn |= (rate - 1) << S3C2443_CLKDIV1_UARTDIV_SHIFT;

	__raw_writel(clkdivn, S3C2443_CLKDIV1);
	return 0;
}

static struct clk clk_display = {
	.name		= "display-if",
	.id		= -1,
	.parent		= &clk_esysclk,
	.ctrlbit	= S3C2443_SCLKCON_DISPCLK,
	.enable		= s3c2443_clkcon_enable_s,
	.get_rate	= s3c2443_getrate_display,
	.set_rate	= s3c2443_setrate_display,
	.round_rate	= s3c2443_roundrate_clksrc256,
};

/* standard clock definitions */

static struct clk init_clocks_disable[] = {
	{
		.name		= "nand",
		.id		= -1,
		.parent		= &clk_h,
	}, {
		.name		= "sdi",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s3c2443_clkcon_enable_p,
		.ctrlbit	= S3C2443_PCLKCON_SDI,
	}, {
		.name		= "adc",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s3c2443_clkcon_enable_p,
		.ctrlbit	= S3C2443_PCLKCON_ADC,
	}, {
		.name		= "i2c",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s3c2443_clkcon_enable_p,
		.ctrlbit	= S3C2443_PCLKCON_IIC,
	}, {
		.name		= "iis",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s3c2443_clkcon_enable_p,
		.ctrlbit	= S3C2443_PCLKCON_IIS,
	}, {
		.name		= "spi",
		.id		= 0,
		.parent		= &clk_p,
		.enable		= s3c2443_clkcon_enable_p,
		.ctrlbit	= S3C2443_PCLKCON_SPI0,
	}, {
		.name		= "spi",
		.id		= 1,
		.parent		= &clk_p,
		.enable		= s3c2443_clkcon_enable_p,
		.ctrlbit	= S3C2443_PCLKCON_SPI1,
	}
};

static struct clk init_clocks[] = {
	{
		.name		= "dma",
		.id		= 0,
		.parent		= &clk_h,
		.enable		= s3c2443_clkcon_enable_h,
		.ctrlbit	= S3C2443_HCLKCON_DMA0,
	}, {
		.name		= "dma",
		.id		= 1,
		.parent		= &clk_h,
		.enable		= s3c2443_clkcon_enable_h,
		.ctrlbit	= S3C2443_HCLKCON_DMA1,
	}, {
		.name		= "dma",
		.id		= 2,
		.parent		= &clk_h,
		.enable		= s3c2443_clkcon_enable_h,
		.ctrlbit	= S3C2443_HCLKCON_DMA2,
	}, {
		.name		= "dma",
		.id		= 3,
		.parent		= &clk_h,
		.enable		= s3c2443_clkcon_enable_h,
		.ctrlbit	= S3C2443_HCLKCON_DMA3,
	}, {
		.name		= "dma",
		.id		= 4,
		.parent		= &clk_h,
		.enable		= s3c2443_clkcon_enable_h,
		.ctrlbit	= S3C2443_HCLKCON_DMA4,
	}, {
		.name		= "dma",
		.id		= 5,
		.parent		= &clk_h,
		.enable		= s3c2443_clkcon_enable_h,
		.ctrlbit	= S3C2443_HCLKCON_DMA5,
	}, {
		.name		= "lcd",
		.id		= -1,
		.parent		= &clk_h,
		.enable		= s3c2443_clkcon_enable_h,
		.ctrlbit	= S3C2443_HCLKCON_LCDC,
	}, {
		.name		= "gpio",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s3c2443_clkcon_enable_p,
		.ctrlbit	= S3C2443_PCLKCON_GPIO,
	}, {
		.name		= "usb-host",
		.id		= -1,
		.parent		= &clk_h,
		.enable		= s3c2443_clkcon_enable_h,
		.ctrlbit	= S3C2443_HCLKCON_USBH,
	}, {
		.name		= "usb-device",
		.id		= -1,
		.parent		= &clk_h,
		.enable		= s3c2443_clkcon_enable_h,
		.ctrlbit	= S3C2443_HCLKCON_USBD,
	}, {
		.name		= "timers",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s3c2443_clkcon_enable_p,
		.ctrlbit	= S3C2443_PCLKCON_PWMT,
	}, {
		.name		= "uart",
		.id		= 0,
		.parent		= &clk_p,
		.enable		= s3c2443_clkcon_enable_p,
		.ctrlbit	= S3C2443_PCLKCON_UART0,
	}, {
		.name		= "uart",
		.id		= 1,
		.parent		= &clk_p,
		.enable		= s3c2443_clkcon_enable_p,
		.ctrlbit	= S3C2443_PCLKCON_UART1,
	}, {
		.name		= "uart",
		.id		= 2,
		.parent		= &clk_p,
		.enable		= s3c2443_clkcon_enable_p,
		.ctrlbit	= S3C2443_PCLKCON_UART2,
	}, {
		.name		= "uart",
		.id		= 3,
		.parent		= &clk_p,
		.enable		= s3c2443_clkcon_enable_p,
		.ctrlbit	= S3C2443_PCLKCON_UART3,
	}, {
		.name		= "rtc",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s3c2443_clkcon_enable_p,
		.ctrlbit	= S3C2443_PCLKCON_RTC,
	}, {
		.name		= "watchdog",
		.id		= -1,
		.parent		= &clk_p,
		.ctrlbit	= S3C2443_PCLKCON_WDT,
	}, {
		.name		= "usb-bus-host",
		.id		= -1,
		.parent		= &clk_usb_bus_host,
	}
};

/* clocks to add where we need to check their parentage */

/* s3c2443_clk_initparents
 *
 * Initialise the parents for the clocks that we get at start-time
*/

static int __init clk_init_set_parent(struct clk *clk, struct clk *parent)
{
	printk(KERN_DEBUG "clock %s: parent %s\n", clk->name, parent->name);
	return clk_set_parent(clk, parent);
}

static void __init s3c2443_clk_initparents(void)
{
	unsigned long clksrc = __raw_readl(S3C2443_CLKSRC);
	struct clk *parent;

	switch (clksrc & S3C2443_CLKSRC_EPLLREF_MASK) {
	case S3C2443_CLKSRC_EPLLREF_EXTCLK:
		parent = &clk_ext;
		break;

	case S3C2443_CLKSRC_EPLLREF_XTAL:
	default:
		parent = &clk_xtal;
		break;

	case S3C2443_CLKSRC_EPLLREF_MPLLREF:
	case S3C2443_CLKSRC_EPLLREF_MPLLREF2:
		parent = &clk_mpllref;
		break;
	}

	clk_init_set_parent(&clk_epllref, parent);

	switch (clksrc & S3C2443_CLKSRC_I2S_MASK) {
	case S3C2443_CLKSRC_I2S_EXT:
		parent = &clk_i2s_ext;
		break;

	case S3C2443_CLKSRC_I2S_EPLLDIV:
	default:
		parent = &clk_i2s_eplldiv;
		break;

	case S3C2443_CLKSRC_I2S_EPLLREF:
	case S3C2443_CLKSRC_I2S_EPLLREF3:
		parent = &clk_epllref;
	}

	clk_init_set_parent(&clk_i2s, &clk_epllref);

	/* esysclk source */

	parent = (clksrc & S3C2443_CLKSRC_ESYSCLK_EPLL) ?
		&clk_epll : &clk_epllref;

	clk_init_set_parent(&clk_esysclk, parent);

	/* msysclk source */

	if (clksrc & S3C2443_CLKSRC_MSYSCLK_MPLL) {
		parent = &clk_mpll;
	} else {
		parent = (clksrc & S3C2443_CLKSRC_EXTCLK_DIV) ?
			&clk_mdivclk : &clk_mpllref;
	}

	clk_init_set_parent(&clk_msysclk, parent);
}

/* armdiv divisor table */

static unsigned int armdiv[16] = {
	[S3C2443_CLKDIV0_ARMDIV_1 >> S3C2443_CLKDIV0_ARMDIV_SHIFT]	= 1,
	[S3C2443_CLKDIV0_ARMDIV_2 >> S3C2443_CLKDIV0_ARMDIV_SHIFT]	= 2,
	[S3C2443_CLKDIV0_ARMDIV_3 >> S3C2443_CLKDIV0_ARMDIV_SHIFT]	= 3,
	[S3C2443_CLKDIV0_ARMDIV_4 >> S3C2443_CLKDIV0_ARMDIV_SHIFT]	= 4,
	[S3C2443_CLKDIV0_ARMDIV_6 >> S3C2443_CLKDIV0_ARMDIV_SHIFT]	= 6,
	[S3C2443_CLKDIV0_ARMDIV_8 >> S3C2443_CLKDIV0_ARMDIV_SHIFT]	= 8,
	[S3C2443_CLKDIV0_ARMDIV_12 >> S3C2443_CLKDIV0_ARMDIV_SHIFT]	= 12,
	[S3C2443_CLKDIV0_ARMDIV_16 >> S3C2443_CLKDIV0_ARMDIV_SHIFT]	= 16,
};

static inline unsigned int s3c2443_fclk_div(unsigned long clkcon0)
{
	clkcon0 &= S3C2443_CLKDIV0_ARMDIV_MASK;

	return armdiv[clkcon0 >> S3C2443_CLKDIV0_ARMDIV_SHIFT];
}

static inline unsigned long s3c2443_get_prediv(unsigned long clkcon0)
{
	clkcon0 &= S3C2443_CLKDIV0_PREDIV_MASK;
	clkcon0 >>= S3C2443_CLKDIV0_PREDIV_SHIFT;

	return clkcon0 + 1;
}

/* clocks to add straight away */

static struct clk *clks[] __initdata = {
	&clk_ext,
	&clk_epll,
	&clk_usb_bus_host,
	&clk_usb_bus,
	&clk_esysclk,
	&clk_epllref,
	&clk_mpllref,
	&clk_msysclk,
	&clk_uart,
	&clk_display,
	&clk_cam,
	&clk_i2s_eplldiv,
	&clk_i2s,
	&clk_hsspi,
	&clk_hsmmc_div,
	&clk_hsmmc,
};

void __init s3c2443_init_clocks(int xtal)
{
	unsigned long epllcon = __raw_readl(S3C2443_EPLLCON);
	unsigned long mpllcon = __raw_readl(S3C2443_MPLLCON);
	unsigned long clkdiv0 = __raw_readl(S3C2443_CLKDIV0);
	unsigned long pll;
	unsigned long fclk;
	unsigned long hclk;
	unsigned long pclk;
	struct clk *clkp;
	int ret;
	int ptr;

	pll = s3c2443_get_mpll(mpllcon, xtal);

	fclk = pll / s3c2443_fclk_div(clkdiv0);
	hclk = fclk / s3c2443_get_prediv(clkdiv0);
	hclk = hclk / ((clkdiv0 & S3C2443_CLKDIV0_HALF_HCLK) ? 2 : 1);
 	pclk = hclk / ((clkdiv0 & S3C2443_CLKDIV0_HALF_PCLK) ? 2 : 1);

	s3c24xx_setup_clocks(xtal, fclk, hclk, pclk);

	printk("S3C2443: mpll %s %ld.%03ld MHz, cpu %ld.%03ld MHz, mem %ld.%03ld MHz, pclk %ld.%03ld MHz\n",
	       (mpllcon & S3C2443_PLLCON_OFF) ? "off":"on",
	       print_mhz(pll), print_mhz(fclk),
	       print_mhz(hclk), print_mhz(pclk));

	s3c2443_clk_initparents();

	for (ptr = 0; ptr < ARRAY_SIZE(clks); ptr++) {
		clkp = clks[ptr];

		ret = s3c24xx_register_clock(clkp);
		if (ret < 0) {
			printk(KERN_ERR "Failed to register clock %s (%d)\n",
			       clkp->name, ret);
		}
	}

	clk_epll.rate = s3c2443_get_epll(epllcon, xtal);

	clk_usb_bus.parent = &clk_usb_bus_host;

	/* ensure usb bus clock is within correct rate of 48MHz */

	if (clk_get_rate(&clk_usb_bus_host) != (48 * 1000 * 1000)) {
		printk(KERN_INFO "Warning: USB host bus not at 48MHz\n");
		clk_set_rate(&clk_usb_bus_host, 48*1000*1000);
	}

	printk("S3C2443: epll %s %ld.%03ld MHz, usb-bus %ld.%03ld MHz\n",
	       (epllcon & S3C2443_PLLCON_OFF) ? "off":"on",
	       print_mhz(clk_get_rate(&clk_epll)),
	       print_mhz(clk_get_rate(&clk_usb_bus)));

	/* register clocks from clock array */

	clkp = init_clocks;
	for (ptr = 0; ptr < ARRAY_SIZE(init_clocks); ptr++, clkp++) {
		ret = s3c24xx_register_clock(clkp);
		if (ret < 0) {
			printk(KERN_ERR "Failed to register clock %s (%d)\n",
			       clkp->name, ret);
		}
	}

	/* We must be careful disabling the clocks we are not intending to
	 * be using at boot time, as subsytems such as the LCD which do
	 * their own DMA requests to the bus can cause the system to lockup
	 * if they where in the middle of requesting bus access.
	 *
	 * Disabling the LCD clock if the LCD is active is very dangerous,
	 * and therefore the bootloader should be careful to not enable
	 * the LCD clock if it is not needed.
	*/

	/* install (and disable) the clocks we do not need immediately */

	clkp = init_clocks_disable;
	for (ptr = 0; ptr < ARRAY_SIZE(init_clocks_disable); ptr++, clkp++) {

		ret = s3c24xx_register_clock(clkp);
		if (ret < 0) {
			printk(KERN_ERR "Failed to register clock %s (%d)\n",
			       clkp->name, ret);
		}

		(clkp->enable)(clkp, 0);
	}
}
