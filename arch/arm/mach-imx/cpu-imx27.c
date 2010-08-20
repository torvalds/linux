/*
 * Copyright 2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Juergen Beisert, kernel@pengutronix.de
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

/*
 * i.MX27 specific CPU detection code
 */

#include <linux/io.h>
#include <linux/module.h>

#include <mach/hardware.h>

static int cpu_silicon_rev = -1;
static int cpu_partnumber;

#define SYS_CHIP_ID             0x00    /* The offset of CHIP ID register */

static void query_silicon_parameter(void)
{
	u32 val;
	/*
	 * now we have access to the IO registers. As we need
	 * the silicon revision very early we read it here to
	 * avoid any further hooks
	*/
	val = __raw_readl(MX27_IO_ADDRESS(MX27_SYSCTRL_BASE_ADDR
				+ SYS_CHIP_ID));

	cpu_silicon_rev = (int)(val >> 28);
	cpu_partnumber = (int)((val >> 12) & 0xFFFF);
}

/*
 * Returns:
 *	the silicon revision of the cpu
 *	-EINVAL - not a mx27
 */
int mx27_revision(void)
{
	if (cpu_silicon_rev == -1)
		query_silicon_parameter();

	if (cpu_partnumber != 0x8821)
		return -EINVAL;

	return cpu_silicon_rev;
}
EXPORT_SYMBOL(mx27_revision);
