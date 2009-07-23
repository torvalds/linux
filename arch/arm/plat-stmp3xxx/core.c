/*
 * Freescale STMP37XX/STMP378X core routines
 *
 * Embedded Alley Solutions, Inc <source@embeddedalley.com>
 *
 * Copyright 2008 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Embedded Alley Solutions, Inc All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>

#include <mach/stmp3xxx.h>
#include <mach/platform.h>
#include <mach/dma.h>
#include <mach/regs-clkctrl.h>

static int __stmp3xxx_reset_block(void __iomem *hwreg, int just_enable)
{
	u32 c;
	int timeout;

	/* the process of software reset of IP block is done
	   in several steps:

	   - clear SFTRST and wait for block is enabled;
	   - clear clock gating (CLKGATE bit);
	   - set the SFTRST again and wait for block is in reset;
	   - clear SFTRST and wait for reset completion.
	*/
	c = __raw_readl(hwreg);
	c &= ~(1<<31);		/* clear SFTRST */
	__raw_writel(c, hwreg);
	for (timeout = 1000000; timeout > 0; timeout--)
		/* still in SFTRST state ? */
		if ((__raw_readl(hwreg) & (1<<31)) == 0)
			break;
	if (timeout <= 0) {
		printk(KERN_ERR"%s(%p): timeout when enabling\n",
				__func__, hwreg);
		return -ETIME;
	}

	c = __raw_readl(hwreg);
	c &= ~(1<<30);		/* clear CLKGATE */
	__raw_writel(c, hwreg);

	if (!just_enable) {
		c = __raw_readl(hwreg);
		c |= (1<<31);		/* now again set SFTRST */
		__raw_writel(c, hwreg);
		for (timeout = 1000000; timeout > 0; timeout--)
			/* poll until CLKGATE set */
			if (__raw_readl(hwreg) & (1<<30))
				break;
		if (timeout <= 0) {
			printk(KERN_ERR"%s(%p): timeout when resetting\n",
					__func__, hwreg);
			return -ETIME;
		}

		c = __raw_readl(hwreg);
		c &= ~(1<<31);		/* clear SFTRST */
		__raw_writel(c, hwreg);
		for (timeout = 1000000; timeout > 0; timeout--)
			/* still in SFTRST state ? */
			if ((__raw_readl(hwreg) & (1<<31)) == 0)
				break;
		if (timeout <= 0) {
			printk(KERN_ERR"%s(%p): timeout when enabling "
					"after reset\n", __func__, hwreg);
			return -ETIME;
		}

		c = __raw_readl(hwreg);
		c &= ~(1<<30);		/* clear CLKGATE */
		__raw_writel(c, hwreg);
	}
	for (timeout = 1000000; timeout > 0; timeout--)
		/* still in SFTRST state ? */
		if ((__raw_readl(hwreg) & (1<<30)) == 0)
			break;

	if (timeout <= 0) {
		printk(KERN_ERR"%s(%p): timeout when unclockgating\n",
				__func__, hwreg);
		return -ETIME;
	}

	return 0;
}

int stmp3xxx_reset_block(void __iomem *hwreg, int just_enable)
{
	int try = 10;
	int r;

	while (try--) {
		r = __stmp3xxx_reset_block(hwreg, just_enable);
		if (!r)
			break;
		pr_debug("%s: try %d failed\n", __func__, 10 - try);
	}
	return r;
}
EXPORT_SYMBOL(stmp3xxx_reset_block);

struct platform_device stmp3xxx_dbguart = {
	.name = "stmp3xxx-dbguart",
	.id = -1,
};

void __init stmp3xxx_init(void)
{
	/* Turn off auto-slow and other tricks */
	stmp3xxx_clearl(0x7f00000, REGS_CLKCTRL_BASE + HW_CLKCTRL_HBUS);

	stmp3xxx_dma_init();
}
