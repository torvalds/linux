/*
 * Device tree support
 *
 * Copyright (C) 2013, 2015 Altera Corporation
 * Copyright (C) 2010 Thomas Chou <thomas@wytron.com.tw>
 *
 * Based on MIPS support for CONFIG_OF device tree support
 *
 * Copyright (C) 2010 Cisco Systems Inc. <dediao@cisco.com>
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/bootmem.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/io.h>

#include <asm/sections.h>

void __init early_init_dt_add_memory_arch(u64 base, u64 size)
{
	u64 kernel_start = (u64)virt_to_phys(_text);

	if (!memory_size &&
	    (kernel_start >= base) && (kernel_start < (base + size)))
		memory_size = size;

}

int __init early_init_dt_reserve_memory_arch(phys_addr_t base, phys_addr_t size,
					     bool nomap)
{
	reserve_bootmem(base, size, BOOTMEM_DEFAULT);
	return 0;
}

void __init early_init_devtree(void *params)
{
	__be32 *dtb = (u32 *)__dtb_start;
#if defined(CONFIG_NIOS2_DTB_AT_PHYS_ADDR)
	if (be32_to_cpup((__be32 *)CONFIG_NIOS2_DTB_PHYS_ADDR) ==
		 OF_DT_HEADER) {
		params = (void *)CONFIG_NIOS2_DTB_PHYS_ADDR;
		early_init_dt_scan(params);
		return;
	}
#endif
	if (be32_to_cpu((__be32) *dtb) == OF_DT_HEADER)
		params = (void *)__dtb_start;

	early_init_dt_scan(params);
}
