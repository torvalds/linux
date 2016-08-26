/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012 MIPS Technologies, Inc.  All rights reserved.
 * Copyright (C) 2013 Imagination Technologies Ltd.
 */
#include <linux/init.h>
#include <linux/libfdt.h>
#include <linux/of_fdt.h>

#include <asm/prom.h>

#include <asm/mach-sead3/sead3-dtshim.h>
#include <asm/mips-boards/generic.h>

const char *get_system_type(void)
{
	return "MIPS SEAD3";
}

void __init *plat_get_fdt(void)
{
	return (void *)__dtb_start;
}

void __init plat_mem_setup(void)
{
	void *fdt = plat_get_fdt();

	fdt = sead3_dt_shim(fdt);
	__dt_setup_arch(fdt);
}

void __init device_tree_init(void)
{
	unflatten_and_copy_device_tree();
}
