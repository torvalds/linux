/*
 * Copyright 2009 Pengutronix, Sascha Hauer <s.hauer@pengutronix.de>
 *
 * Initial development of this code was funded by
 * Phytec Messtechnik GmbH, http://www.phytec.de
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <mach/audmux.h>
#include <mach/hardware.h>

static struct clk *audmux_clk;
static void __iomem *audmux_base;

#define MXC_AUDMUX_V2_PTCR(x)		((x) * 8)
#define MXC_AUDMUX_V2_PDCR(x)		((x) * 8 + 4)

int mxc_audmux_v2_configure_port(unsigned int port, unsigned int ptcr,
		unsigned int pdcr)
{
	if (!audmux_base)
		return -ENOSYS;

	if (audmux_clk)
		clk_enable(audmux_clk);

	writel(ptcr, audmux_base + MXC_AUDMUX_V2_PTCR(port));
	writel(pdcr, audmux_base + MXC_AUDMUX_V2_PDCR(port));

	if (audmux_clk)
		clk_disable(audmux_clk);

	return 0;
}
EXPORT_SYMBOL_GPL(mxc_audmux_v2_configure_port);

static int mxc_audmux_v2_init(void)
{
	int ret;

	if (cpu_is_mx35()) {
		audmux_clk = clk_get(NULL, "audmux");
		if (IS_ERR(audmux_clk)) {
			ret = PTR_ERR(audmux_clk);
			printk(KERN_ERR "%s: cannot get clock: %d\n", __func__,
					ret);
			return ret;
		}
	}

	if (cpu_is_mx31() || cpu_is_mx35())
		audmux_base = IO_ADDRESS(AUDMUX_BASE_ADDR);

	return 0;
}

postcore_initcall(mxc_audmux_v2_init);
