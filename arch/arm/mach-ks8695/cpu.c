/*
 * arch/arm/mach-ks8695/cpu.c
 *
 * Copyright (C) 2006 Ben Dooks <ben@simtec.co.uk>
 * Copyright (C) 2006 Simtec Electronics
 *
 * KS8695 CPU support
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/regs-sys.h>
#include <mach/regs-misc.h>


static struct __initdata map_desc ks8695_io_desc[] = {
	{
		.virtual	= (unsigned long)KS8695_IO_VA,
		.pfn		= __phys_to_pfn(KS8695_IO_PA),
		.length		= KS8695_IO_SIZE,
		.type		= MT_DEVICE,
	}
};

static void __init ks8695_processor_info(void)
{
	unsigned long id, rev;

	id = __raw_readl(KS8695_MISC_VA + KS8695_DID);
	rev = __raw_readl(KS8695_MISC_VA + KS8695_RID);

	printk("KS8695 ID=%04lx  SubID=%02lx  Revision=%02lx\n", (id & DID_ID), (rev & RID_SUBID), (rev & RID_REVISION));
}

static unsigned int sysclk[8] = { 125000000, 100000000, 62500000, 50000000, 41700000, 33300000, 31300000, 25000000 };
static unsigned int cpuclk[8] = { 166000000, 166000000, 83000000, 83000000, 55300000, 55300000, 41500000, 41500000 };

static void __init ks8695_clock_info(void)
{
	unsigned int scdc = __raw_readl(KS8695_SYS_VA + KS8695_CLKCON) & CLKCON_SCDC;

	printk("Clocks: System %u MHz, CPU %u MHz\n",
			sysclk[scdc] / 1000000, cpuclk[scdc] / 1000000);
}

void __init ks8695_map_io(void)
{
	iotable_init(ks8695_io_desc, ARRAY_SIZE(ks8695_io_desc));

	ks8695_processor_info();
	ks8695_clock_info();
}
