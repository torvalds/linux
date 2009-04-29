/*
 *  author: Sascha Hauer
 *  Created: april 20th, 2004
 *  Copyright: Synertronixx GmbH
 *
 *  Common code for i.MX machines
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
 *
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>

#include <asm/mach/map.h>

#include <mach/common.h>
#include <mach/hardware.h>

static struct map_desc imx_io_desc[] __initdata = {
	{
		.virtual	= IMX_IO_BASE,
		.pfn		= __phys_to_pfn(IMX_IO_PHYS),
		.length		= IMX_IO_SIZE,
		.type		= MT_DEVICE
	}
};

void __init mx1_map_io(void)
{
	mxc_set_cpu_type(MXC_CPU_MX1);

	iotable_init(imx_io_desc, ARRAY_SIZE(imx_io_desc));
}
