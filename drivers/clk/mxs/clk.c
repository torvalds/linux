/*
 * Copyright 2012 Freescale Semiconductor, Inc.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/spinlock.h>

DEFINE_SPINLOCK(mxs_lock);

int mxs_clk_wait(void __iomem *reg, u8 shift)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(10);

	while (readl_relaxed(reg) & (1 << shift))
		if (time_after(jiffies, timeout))
			return -ETIMEDOUT;

	return 0;
}
