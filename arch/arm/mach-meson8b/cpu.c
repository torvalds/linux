/*
 * arch/arm/mach-meson8b/cpu.c
 *
 * Copyright (C) 2014 Amlogic, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <plat/io.h>
#include <plat/cpu.h>
#include <mach/io.h>
#include <mach/am_regs.h>
#include <linux/printk.h>
#include <linux/string.h>

#ifdef CONFIG_MESON_TRUSTZONE
#include <mach/meson-secure.h>
#endif

static int meson_cpu_version[MESON_CPU_VERSION_LVL_MAX+1];
int __init meson_cpu_version_init(void)
{
	unsigned int version,ver;
	unsigned int  *version_map;

	meson_cpu_version[MESON_CPU_VERSION_LVL_MAJOR] = 
		aml_read_reg32(P_ASSIST_HW_REV);

#ifndef CONFIG_MESON_TRUSTZONE
	version_map = (unsigned int *)IO_BOOTROM_BASE;
	meson_cpu_version[MESON_CPU_VERSION_LVL_MISC] = version_map[1];
#else
	meson_cpu_version[MESON_CPU_VERSION_LVL_MISC] = meson_read_socrev1();
#endif

	version = aml_read_reg32(P_METAL_REVISION);
	switch (version) {		
		case 0x11111111:
			ver = 0xA;
			break;
		default:/*changed?*/
			ver = 0xB;
			break;
	}
	meson_cpu_version[MESON_CPU_VERSION_LVL_MINOR] = ver;
	printk(KERN_INFO "Meson chip version = Rev%X (%X:%X - %X:%X)\n", ver,
		meson_cpu_version[MESON_CPU_VERSION_LVL_MAJOR],
		meson_cpu_version[MESON_CPU_VERSION_LVL_MINOR],
		meson_cpu_version[MESON_CPU_VERSION_LVL_PACK],
		meson_cpu_version[MESON_CPU_VERSION_LVL_MISC]
		);

	return 0;
}
EXPORT_SYMBOL(meson_cpu_version_init);
int get_meson_cpu_version(int level)
{
	if(level >= 0 && level <= MESON_CPU_VERSION_LVL_MAX)
		return meson_cpu_version[level];
	return 0;
}
EXPORT_SYMBOL(get_meson_cpu_version);
