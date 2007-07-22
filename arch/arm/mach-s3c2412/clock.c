/* linux/arch/arm/mach-s3c2412/clock.c
 *
 * Copyright (c) 2006 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2412,S3C2413 Clock control support
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

#include <asm/plat-s3c/regs-serial.h>
#include <asm/arch/regs-clock.h>
#include <asm/arch/regs-gpio.h>

#include <asm/plat-s3c24xx/s3c2412.h>
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

static int s3c2412_clkcon_enable(struct clk *clk, int enable)
{
	unsigned int clocks = clk->ctrlbit;
	unsigned long clkcon;

	clkcon = __raw_readl(S3C2410_CLKCON);

	if (enable)
		clkcon |= clocks;
	else
		clkcon &= ~clocks;

	__raw_writel(clkcon, S3C2410_CLKCON);

	return 0;
}

static int s3c2412_upll_enable(struct clk *clk, int enable)
{
	unsigned long upllcon = __raw_readl(S3C2410_UPLLCON);
	unsigned long orig = upllcon;

	if (!enable)
		upllcon |= S3C2412_PLLCON_OFF;
	else
		upllcon &= ~S3C2412_PLLCON_OFF;

	__raw_writel(upllcon, S3C2410_UPLLCON);

	/* allow ~150uS for the PLL to settle and lock */

	if (enable && (orig & S3C2412_PLLCON_OFF))
		udelay(150);

	return 0;
}

/* clock selections */

/* CPU EXTCLK input */
static struct clk clk_ext = {
	.name		= "extclk",
	.id		= -1,
};

static struct clk clk_erefclk = {
	.name		= "erefclk",
	.id		= -1,
};

static struct clk clk_urefclk = {
	.name		= "urefclk",
	.id		= -1,
};

static int s3c2412_setparent_usysclk(struct clk *clk, struct clk *parent)
{
	unsigned long clksrc = __raw_readl(S3C2412_CLKSRC);

	if (parent == &clk_urefclk)
		clksrc &= ~S3C2412_CLKSRC_USYSCLK_UPLL;
	else if (parent == &clk_upll)
		clksrc |= S3C2412_CLKSRC_USYSCLK_UPLL;
	else
		return -EINVAL;

	clk->parent = parent;

	__raw_writel(clksrc, S3C2412_CLKSRC);
	return 0;
}

static struct clk clk_usysclk = {
	.name		= "usysclk",
	.id		= -1,
	.parent		= &clk_xtal,
	.set_parent	= s3c2412_setparent_usysclk,
};

static struct clk clk_mrefclk = {
	.name		= "mrefclk",
	.parent		= &clk_xtal,
	.id		= -1,
};

static struct clk clk_mdivclk = {
	.name		= "mdivclk",
	.parent		= &clk_xtal,
	.id		= -1,
};

static int s3c2412_setparent_usbsrc(struct clk *clk, struct clk *parent)
{
	unsigned long clksrc = __raw_readl(S3C2412_CLKSRC);

	if (parent == &clk_usysclk)
		clksrc &= ~S3C2412_CLKSRC_USBCLK_HCLK;
	else if (parent == &clk_h)
		clksrc |= S3C2412_CLKSRC_USBCLK_HCLK;
	else
		return -EINVAL;

	clk->parent = parent;

	__raw_writel(clksrc, S3C2412_CLKSRC);
	return 0;
}

static unsigned long s3c2412_roundrate_usbsrc(struct clk *clk,
					      unsigned long rate)
{
	unsigned long parent_rate = clk_get_rate(clk->parent);
	int div;

	if (rate > parent_rate)
		return parent_rate;

	div = parent_rate / rate;
	if (div > 2)
		div = 2;

	return parent_rate / div;
}

static unsigned long s3c2412_getrate_usbsrc(struct clk *clk)
{
	unsigned long parent_rate = clk_get_rate(clk->parent);
	unsigned long div = __raw_readl(S3C2410_CLKDIVN);

	return parent_rate / ((div & S3C2412_CLKDIVN_USB48DIV) ? 2 : 1);
}

static int s3c2412_setrate_usbsrc(struct clk *clk, unsigned long rate)
{
	unsigned long parent_rate = clk_get_rate(clk->parent);
	unsigned long clkdivn = __raw_readl(S3C2410_CLKDIVN);

	rate = s3c2412_roundrate_usbsrc(clk, rate);

	if ((parent_rate / rate) == 2)
		clkdivn |= S3C2412_CLKDIVN_USB48DIV;
	else
		clkdivn &= ~S3C2412_CLKDIVN_USB48DIV;

	__raw_writel(clkdivn, S3C2410_CLKDIVN);
	return 0;
}

static struct clk clk_usbsrc = {
	.name		= "usbsrc",
	.id		= -1,
	.get_rate	= s3c2412_getrate_usbsrc,
	.set_rate	= s3c2412_setrate_usbsrc,
	.round_rate	= s3c2412_roundrate_usbsrc,
	.set_parent	= s3c2412_setparent_usbsrc,
};

static int s3c2412_setparent_msysclk(struct clk *clk, struct clk *parent)
{
	unsigned long clksrc = __raw_readl(S3C2412_CLKSRC);

	if (parent == &clk_mdivclk)
		clksrc &= ~S3C2412_CLKSRC_MSYSCLK_MPLL;
	else if (parent == &clk_upll)
		clksrc |= S3C2412_CLKSRC_MSYSCLK_MPLL;
	else
		return -EINVAL;

	clk->parent = parent;

	__raw_writel(clksrc, S3C2412_CLKSRC);
	return 0;
}

static struct clk clk_msysclk = {
	.name		= "msysclk",
	.id		= -1,
	.set_parent	= s3c2412_setparent_msysclk,
};

/* these next clocks have an divider immediately after them,
 * so we can register them with their divider and leave out the
 * intermediate clock stage
*/
static unsigned long s3c2412_roundrate_clksrc(struct clk *clk,
					      unsigned long rate)
{
	unsigned long parent_rate = clk_get_rate(clk->parent);
	int div;

	if (rate > parent_rate)
		return parent_rate;

	/* note, we remove the +/- 1 calculations as they cancel out */

	div = (rate / parent_rate);

	if (div < 1)
		div = 1;
	else if (div > 16)
		div = 16;

	return parent_rate / div;
}

static int s3c2412_setparent_uart(struct clk *clk, struct clk *parent)
{
	unsigned long clksrc = __raw_readl(S3C2412_CLKSRC);

	if (parent == &clk_erefclk)
		clksrc &= ~S3C2412_CLKSRC_UARTCLK_MPLL;
	else if (parent == &clk_mpll)
		clksrc |= S3C2412_CLKSRC_UARTCLK_MPLL;
	else
		return -EINVAL;

	clk->parent = parent;

	__raw_writel(clksrc, S3C2412_CLKSRC);
	return 0;
}

static unsigned long s3c2412_getrate_uart(struct clk *clk)
{
	unsigned long parent_rate = clk_get_rate(clk->parent);
	unsigned long div = __raw_readl(S3C2410_CLKDIVN);

	div &= S3C2412_CLKDIVN_UARTDIV_MASK;
	div >>= S3C2412_CLKDIVN_UARTDIV_SHIFT;

	return parent_rate / (div + 1);
}

static int s3c2412_setrate_uart(struct clk *clk, unsigned long rate)
{
	unsigned long parent_rate = clk_get_rate(clk->parent);
	unsigned long clkdivn = __raw_readl(S3C2410_CLKDIVN);

	rate = s3c2412_roundrate_clksrc(clk, rate);

	clkdivn &= ~S3C2412_CLKDIVN_UARTDIV_MASK;
	clkdivn |= ((parent_rate / rate) - 1) << S3C2412_CLKDIVN_UARTDIV_SHIFT;

	__raw_writel(clkdivn, S3C2410_CLKDIVN);
	return 0;
}

static struct clk clk_uart = {
	.name		= "uartclk",
	.id		= -1,
	.get_rate	= s3c2412_getrate_uart,
	.set_rate	= s3c2412_setrate_uart,
	.set_parent	= s3c2412_setparent_uart,
	.round_rate	= s3c2412_roundrate_clksrc,
};

static int s3c2412_setparent_i2s(struct clk *clk, struct clk *parent)
{
	unsigned long clksrc = __raw_readl(S3C2412_CLKSRC);

	if (parent == &clk_erefclk)
		clksrc &= ~S3C2412_CLKSRC_I2SCLK_MPLL;
	else if (parent == &clk_mpll)
		clksrc |= S3C2412_CLKSRC_I2SCLK_MPLL;
	else
		return -EINVAL;

	clk->parent = parent;

	__raw_writel(clksrc, S3C2412_CLKSRC);
	return 0;
}

static unsigned long s3c2412_getrate_i2s(struct clk *clk)
{
	unsigned long parent_rate = clk_get_rate(clk->parent);
	unsigned long div = __raw_readl(S3C2410_CLKDIVN);

	div &= S3C2412_CLKDIVN_I2SDIV_MASK;
	div >>= S3C2412_CLKDIVN_I2SDIV_SHIFT;

	return parent_rate / (div + 1);
}

static int s3c2412_setrate_i2s(struct clk *clk, unsigned long rate)
{
	unsigned long parent_rate = clk_get_rate(clk->parent);
	unsigned long clkdivn = __raw_readl(S3C2410_CLKDIVN);

	rate = s3c2412_roundrate_clksrc(clk, rate);

	clkdivn &= ~S3C2412_CLKDIVN_I2SDIV_MASK;
	clkdivn |= ((parent_rate / rate) - 1) << S3C2412_CLKDIVN_I2SDIV_SHIFT;

	__raw_writel(clkdivn, S3C2410_CLKDIVN);
	return 0;
}

static struct clk clk_i2s = {
	.name		= "i2sclk",
	.id		= -1,
	.get_rate	= s3c2412_getrate_i2s,
	.set_rate	= s3c2412_setrate_i2s,
	.set_parent	= s3c2412_setparent_i2s,
	.round_rate	= s3c2412_roundrate_clksrc,
};

static int s3c2412_setparent_cam(struct clk *clk, struct clk *parent)
{
	unsigned long clksrc = __raw_readl(S3C2412_CLKSRC);

	if (parent == &clk_usysclk)
		clksrc &= ~S3C2412_CLKSRC_CAMCLK_HCLK;
	else if (parent == &clk_h)
		clksrc |= S3C2412_CLKSRC_CAMCLK_HCLK;
	else
		return -EINVAL;

	clk->parent = parent;

	__raw_writel(clksrc, S3C2412_CLKSRC);
	return 0;
}
static unsigned long s3c2412_getrate_cam(struct clk *clk)
{
	unsigned long parent_rate = clk_get_rate(clk->parent);
	unsigned long div = __raw_readl(S3C2410_CLKDIVN);

	div &= S3C2412_CLKDIVN_CAMDIV_MASK;
	div >>= S3C2412_CLKDIVN_CAMDIV_SHIFT;

	return parent_rate / (div + 1);
}

static int s3c2412_setrate_cam(struct clk *clk, unsigned long rate)
{
	unsigned long parent_rate = clk_get_rate(clk->parent);
	unsigned long clkdivn = __raw_readl(S3C2410_CLKDIVN);

	rate = s3c2412_roundrate_clksrc(clk, rate);

	clkdivn &= ~S3C2412_CLKDIVN_CAMDIV_MASK;
	clkdivn |= ((parent_rate / rate) - 1) << S3C2412_CLKDIVN_CAMDIV_SHIFT;

	__raw_writel(clkdivn, S3C2410_CLKDIVN);
	return 0;
}

static struct clk clk_cam = {
	.name		= "camif-upll",	/* same as 2440 name */
	.id		= -1,
	.get_rate	= s3c2412_getrate_cam,
	.set_rate	= s3c2412_setrate_cam,
	.set_parent	= s3c2412_setparent_cam,
	.round_rate	= s3c2412_roundrate_clksrc,
};

/* standard clock definitions */

static struct clk init_clocks_disable[] = {
	{
		.name		= "nand",
		.id		= -1,
		.parent		= &clk_h,
		.enable		= s3c2412_clkcon_enable,
		.ctrlbit	= S3C2412_CLKCON_NAND,
	}, {
		.name		= "sdi",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s3c2412_clkcon_enable,
		.ctrlbit	= S3C2412_CLKCON_SDI,
	}, {
		.name		= "adc",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s3c2412_clkcon_enable,
		.ctrlbit	= S3C2412_CLKCON_ADC,
	}, {
		.name		= "i2c",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s3c2412_clkcon_enable,
		.ctrlbit	= S3C2412_CLKCON_IIC,
	}, {
		.name		= "iis",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s3c2412_clkcon_enable,
		.ctrlbit	= S3C2412_CLKCON_IIS,
	}, {
		.name		= "spi",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s3c2412_clkcon_enable,
		.ctrlbit	= S3C2412_CLKCON_SPI,
	}
};

static struct clk init_clocks[] = {
	{
		.name		= "dma",
		.id		= 0,
		.parent		= &clk_h,
		.enable		= s3c2412_clkcon_enable,
		.ctrlbit	= S3C2412_CLKCON_DMA0,
	}, {
		.name		= "dma",
		.id		= 1,
		.parent		= &clk_h,
		.enable		= s3c2412_clkcon_enable,
		.ctrlbit	= S3C2412_CLKCON_DMA1,
	}, {
		.name		= "dma",
		.id		= 2,
		.parent		= &clk_h,
		.enable		= s3c2412_clkcon_enable,
		.ctrlbit	= S3C2412_CLKCON_DMA2,
	}, {
		.name		= "dma",
		.id		= 3,
		.parent		= &clk_h,
		.enable		= s3c2412_clkcon_enable,
		.ctrlbit	= S3C2412_CLKCON_DMA3,
	}, {
		.name		= "lcd",
		.id		= -1,
		.parent		= &clk_h,
		.enable		= s3c2412_clkcon_enable,
		.ctrlbit	= S3C2412_CLKCON_LCDC,
	}, {
		.name		= "gpio",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s3c2412_clkcon_enable,
		.ctrlbit	= S3C2412_CLKCON_GPIO,
	}, {
		.name		= "usb-host",
		.id		= -1,
		.parent		= &clk_h,
		.enable		= s3c2412_clkcon_enable,
		.ctrlbit	= S3C2412_CLKCON_USBH,
	}, {
		.name		= "usb-device",
		.id		= -1,
		.parent		= &clk_h,
		.enable		= s3c2412_clkcon_enable,
		.ctrlbit	= S3C2412_CLKCON_USBD,
	}, {
		.name		= "timers",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s3c2412_clkcon_enable,
		.ctrlbit	= S3C2412_CLKCON_PWMT,
	}, {
		.name		= "uart",
		.id		= 0,
		.parent		= &clk_p,
		.enable		= s3c2412_clkcon_enable,
		.ctrlbit	= S3C2412_CLKCON_UART0,
	}, {
		.name		= "uart",
		.id		= 1,
		.parent		= &clk_p,
		.enable		= s3c2412_clkcon_enable,
		.ctrlbit	= S3C2412_CLKCON_UART1,
	}, {
		.name		= "uart",
		.id		= 2,
		.parent		= &clk_p,
		.enable		= s3c2412_clkcon_enable,
		.ctrlbit	= S3C2412_CLKCON_UART2,
	}, {
		.name		= "rtc",
		.id		= -1,
		.parent		= &clk_p,
		.enable		= s3c2412_clkcon_enable,
		.ctrlbit	= S3C2412_CLKCON_RTC,
	}, {
		.name		= "watchdog",
		.id		= -1,
		.parent		= &clk_p,
		.ctrlbit	= 0,
	}, {
		.name		= "usb-bus-gadget",
		.id		= -1,
		.parent		= &clk_usb_bus,
		.enable		= s3c2412_clkcon_enable,
		.ctrlbit	= S3C2412_CLKCON_USB_DEV48,
	}, {
		.name		= "usb-bus-host",
		.id		= -1,
		.parent		= &clk_usb_bus,
		.enable		= s3c2412_clkcon_enable,
		.ctrlbit	= S3C2412_CLKCON_USB_HOST48,
	}
};

/* clocks to add where we need to check their parentage */

struct clk_init {
	struct clk	*clk;
	unsigned int	 bit;
	struct clk	*src_0;
	struct clk	*src_1;
};

static struct clk_init clks_src[] __initdata = {
	{
		.clk	= &clk_usysclk,
		.bit	= S3C2412_CLKSRC_USBCLK_HCLK,
		.src_0	= &clk_urefclk,
		.src_1	= &clk_upll,
	}, {
		.clk	= &clk_i2s,
		.bit	= S3C2412_CLKSRC_I2SCLK_MPLL,
		.src_0	= &clk_erefclk,
		.src_1	= &clk_mpll,
	}, {
		.clk	= &clk_cam,
		.bit	= S3C2412_CLKSRC_CAMCLK_HCLK,
		.src_0	= &clk_usysclk,
		.src_1	= &clk_h,
	}, {
		.clk	= &clk_msysclk,
		.bit	= S3C2412_CLKSRC_MSYSCLK_MPLL,
		.src_0	= &clk_mdivclk,
		.src_1	= &clk_mpll,
	}, {
		.clk	= &clk_uart,
		.bit	= S3C2412_CLKSRC_UARTCLK_MPLL,
		.src_0	= &clk_erefclk,
		.src_1	= &clk_mpll,
	}, {
		.clk	= &clk_usbsrc,
		.bit	= S3C2412_CLKSRC_USBCLK_HCLK,
		.src_0	= &clk_usysclk,
		.src_1	= &clk_h,
	},
};

/* s3c2412_clk_initparents
 *
 * Initialise the parents for the clocks that we get at start-time
*/

static void __init s3c2412_clk_initparents(void)
{
	unsigned long clksrc = __raw_readl(S3C2412_CLKSRC);
	struct clk_init *cip = clks_src;
	struct clk *src;
	int ptr;
	int ret;

	for (ptr = 0; ptr < ARRAY_SIZE(clks_src); ptr++, cip++) {
		ret = s3c24xx_register_clock(cip->clk);
		if (ret < 0) {
			printk(KERN_ERR "Failed to register clock %s (%d)\n",
			       cip->clk->name, ret);
		}

		src = (clksrc & cip->bit) ? cip->src_1 : cip->src_0;

		printk(KERN_INFO "%s: parent %s\n", cip->clk->name, src->name);
		clk_set_parent(cip->clk, src);
	}
}

/* clocks to add straight away */

static struct clk *clks[] __initdata = {
	&clk_ext,
	&clk_usb_bus,
	&clk_erefclk,
	&clk_urefclk,
	&clk_mrefclk,
};

int __init s3c2412_baseclk_add(void)
{
	unsigned long clkcon  = __raw_readl(S3C2410_CLKCON);
	struct clk *clkp;
	int ret;
	int ptr;

	clk_upll.enable = s3c2412_upll_enable;
	clk_usb_bus.parent = &clk_usbsrc;
	clk_usb_bus.rate = 0x0;

	s3c2412_clk_initparents();

	for (ptr = 0; ptr < ARRAY_SIZE(clks); ptr++) {
		clkp = clks[ptr];

		ret = s3c24xx_register_clock(clkp);
		if (ret < 0) {
			printk(KERN_ERR "Failed to register clock %s (%d)\n",
			       clkp->name, ret);
		}
	}

	/* ensure usb bus clock is within correct rate of 48MHz */

	if (clk_get_rate(&clk_usb_bus) != (48 * 1000 * 1000)) {
		printk(KERN_INFO "Warning: USB bus clock not at 48MHz\n");

		/* for the moment, let's use the UPLL, and see if we can
		 * get 48MHz */

		clk_set_parent(&clk_usysclk, &clk_upll);
		clk_set_parent(&clk_usbsrc, &clk_usysclk);
		clk_set_rate(&clk_usbsrc, 48*1000*1000);
	}

	printk("S3C2412: upll %s, %ld.%03ld MHz, usb-bus %ld.%03ld MHz\n",
	       (__raw_readl(S3C2410_UPLLCON) & S3C2412_PLLCON_OFF) ? "off":"on",
	       print_mhz(clk_get_rate(&clk_upll)),
	       print_mhz(clk_get_rate(&clk_usb_bus)));

	/* register clocks from clock array */

	clkp = init_clocks;
	for (ptr = 0; ptr < ARRAY_SIZE(init_clocks); ptr++, clkp++) {
		/* ensure that we note the clock state */

		clkp->usage = clkcon & clkp->ctrlbit ? 1 : 0;

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

		s3c2412_clkcon_enable(clkp, 0);
	}

	return 0;
}
