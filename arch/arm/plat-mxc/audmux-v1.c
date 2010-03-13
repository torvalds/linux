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

static void __iomem *audmux_base;

static unsigned char port_mapping[] = {
	0x0, 0x4, 0x8, 0x10, 0x14, 0x1c,
};

int mxc_audmux_v1_configure_port(unsigned int port, unsigned int pcr)
{
	if (!audmux_base) {
		printk("%s: not configured\n", __func__);
		return -ENOSYS;
	}

	if (port >= ARRAY_SIZE(port_mapping))
		return -EINVAL;

	writel(pcr, audmux_base + port_mapping[port]);

	return 0;
}
EXPORT_SYMBOL_GPL(mxc_audmux_v1_configure_port);

static int mxc_audmux_v1_init(void)
{
#ifdef CONFIG_MACH_MX21
	if (cpu_is_mx21())
		audmux_base = MX21_IO_ADDRESS(MX21_AUDMUX_BASE_ADDR);
	else
#endif
#ifdef CONFIG_MACH_MX27
	if (cpu_is_mx27())
		audmux_base = MX27_IO_ADDRESS(MX27_AUDMUX_BASE_ADDR);
	else
#endif
		(void)0;
	
	return 0;
}

postcore_initcall(mxc_audmux_v1_init);
