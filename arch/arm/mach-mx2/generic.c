/*
 * generic.c
 *
 * Copyright (C) 2008 Juergen Beisert (kernel@pengutronix.de)
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

#include <linux/mm.h>
#include <linux/init.h>
#include <mach/hardware.h>
#include <mach/common.h>
#include <asm/pgtable.h>
#include <asm/mach/map.h>

/* MX27 memory map definition */
static struct map_desc mxc_io_desc[] __initdata = {
	/*
	 * this fixed mapping covers:
	 * - AIPI1
	 * - AIPI2
	 * - AITC
	 * - ROM Patch
	 * - and some reserved space
	 */
	{
		.virtual = AIPI_BASE_ADDR_VIRT,
		.pfn = __phys_to_pfn(AIPI_BASE_ADDR),
		.length = AIPI_SIZE,
		.type = MT_DEVICE
	},
	/*
	 * this fixed mapping covers:
	 * - CSI
	 * - ATA
	 */
	{
		.virtual = SAHB1_BASE_ADDR_VIRT,
		.pfn = __phys_to_pfn(SAHB1_BASE_ADDR),
		.length = SAHB1_SIZE,
		.type = MT_DEVICE
	},
	/*
	 * this fixed mapping covers:
	 * - EMI
	 */
	{
		.virtual = X_MEMC_BASE_ADDR_VIRT,
		.pfn = __phys_to_pfn(X_MEMC_BASE_ADDR),
		.length = X_MEMC_SIZE,
		.type = MT_DEVICE
	}
};

/*
 * Initialize the memory map. It is called during the
 * system startup to create static physical to virtual
 * memory map for the IO modules.
 */
void __init mx21_map_io(void)
{
	mxc_set_cpu_type(MXC_CPU_MX21);

	iotable_init(mxc_io_desc, ARRAY_SIZE(mxc_io_desc));
}

void __init mx27_map_io(void)
{
	mxc_set_cpu_type(MXC_CPU_MX27);

	iotable_init(mxc_io_desc, ARRAY_SIZE(mxc_io_desc));
}

