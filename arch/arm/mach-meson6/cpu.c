/*
 * arch/arm/mach-meson6/cpu.c
 *
 * Copyright (C) 2011-2012 Amlogic, Inc.
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
#include <mach/io.h>
#include <mach/am_regs.h>
#include <linux/printk.h>
#include <linux/string.h>

static int meson_cpu_version[MESON_CPU_VERSION_LVL_MAX+1];
int __init meson_cpu_version_init(void)
{
	unsigned int ver=0xa;
	unsigned int  *version_map;

	meson_cpu_version[MESON_CPU_VERSION_LVL_MAJOR] =
		aml_read_reg32(P_ASSIST_HW_REV);

	version_map = (unsigned int *)IO_BOOTROM_BASE;
	meson_cpu_version[MESON_CPU_VERSION_LVL_MISC] = version_map[1];

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
int mali_revb_flag = -1;
//int mali_version(void)

EXPORT_SYMBOL_GPL(mali_revb_flag);
static int __init maliversion(char *str)
{
    mali_revb_flag=-1;
    if(strncasecmp(str,"a",1)==0)
        mali_revb_flag = 0;
    else if(strncasecmp(str,"b",1)==0)
       mali_revb_flag = 1;


    return 1;
}
__setup("mali_version=",maliversion);
