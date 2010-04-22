/* arch/arm/mach-rk2818/io.c
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <asm/page.h>
#include <mach/rk2818_iomap.h>
#include <asm/mach/map.h>

#include <mach/board.h>

#define RK2818_DEVICE(name) { \
		.virtual = (unsigned long) RK2818_##name##_BASE, \
		.pfn = __phys_to_pfn(RK2818_##name##_PHYS), \
		.length = RK2818_##name##_SIZE, \
		.type = MT_DEVICE_NONSHARED, \
	 }

static struct map_desc rk2818_io_desc[] __initdata = {
	/*RK2818_DEVICE(VIC),
	RK2818_DEVICE(CSR),
	RK2818_DEVICE(GPT),
	RK2818_DEVICE(DMOV),
	RK2818_DEVICE(GPIO1),
	RK2818_DEVICE(GPIO2),
	RK2818_DEVICE(CLK_CTL),
	{
		.virtual =  (unsigned long) RK2818_SHARED_RAM_BASE,
		.pfn =      __phys_to_pfn(RK2818_SHARED_RAM_PHYS),
		.length =   RK2818_SHARED_RAM_SIZE,
		.type =     MT_DEVICE,
	},*/
};

void __init rk2818_map_common_io(void)
{
	/* Make sure the peripheral register window is closed, since
	 * we will use PTE flags (TEX[1]=1,B=0,C=1) to determine which
	 * pages are peripheral interface or not.
	 */
	asm("mcr p15, 0, %0, c15, c2, 4" : : "r" (0));

	iotable_init(rk2818_io_desc, ARRAY_SIZE(rk2818_io_desc));
}

void __iomem *
__rk2818_ioremap(unsigned long phys_addr, size_t size, unsigned int mtype)
{
	if (mtype == MT_DEVICE) {
		/* The peripherals in the 88000000 - D0000000 range
		 * are only accessable by type MT_DEVICE_NONSHARED.
		 * Adjust mtype as necessary to make this "just work."
		 */
		if ((phys_addr >= 0x88000000) && (phys_addr < 0xD0000000))
			mtype = MT_DEVICE_NONSHARED;
	}

	return __arm_ioremap(phys_addr, size, mtype);
}
