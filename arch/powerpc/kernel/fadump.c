/*
 * Firmware Assisted dump: A robust mechanism to get reliable kernel crash
 * dump with assistance from firmware. This approach does not use kexec,
 * instead firmware assists in booting the kdump kernel while preserving
 * memory contents. The most of the code implementation has been adapted
 * from phyp assisted dump implementation written by Linas Vepstas and
 * Manish Ahuja
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright 2011 IBM Corporation
 * Author: Mahesh Salgaonkar <mahesh@linux.vnet.ibm.com>
 */

#undef DEBUG
#define pr_fmt(fmt) "fadump: " fmt

#include <linux/string.h>
#include <linux/memblock.h>

#include <asm/page.h>
#include <asm/prom.h>
#include <asm/rtas.h>
#include <asm/fadump.h>

static struct fw_dump fw_dump;

/* Scan the Firmware Assisted dump configuration details. */
int __init early_init_dt_scan_fw_dump(unsigned long node,
			const char *uname, int depth, void *data)
{
	__be32 *sections;
	int i, num_sections;
	unsigned long size;
	const int *token;

	if (depth != 1 || strcmp(uname, "rtas") != 0)
		return 0;

	/*
	 * Check if Firmware Assisted dump is supported. if yes, check
	 * if dump has been initiated on last reboot.
	 */
	token = of_get_flat_dt_prop(node, "ibm,configure-kernel-dump", NULL);
	if (!token)
		return 0;

	fw_dump.fadump_supported = 1;
	fw_dump.ibm_configure_kernel_dump = *token;

	/*
	 * The 'ibm,kernel-dump' rtas node is present only if there is
	 * dump data waiting for us.
	 */
	if (of_get_flat_dt_prop(node, "ibm,kernel-dump", NULL))
		fw_dump.dump_active = 1;

	/* Get the sizes required to store dump data for the firmware provided
	 * dump sections.
	 * For each dump section type supported, a 32bit cell which defines
	 * the ID of a supported section followed by two 32 bit cells which
	 * gives teh size of the section in bytes.
	 */
	sections = of_get_flat_dt_prop(node, "ibm,configure-kernel-dump-sizes",
					&size);

	if (!sections)
		return 0;

	num_sections = size / (3 * sizeof(u32));

	for (i = 0; i < num_sections; i++, sections += 3) {
		u32 type = (u32)of_read_number(sections, 1);

		switch (type) {
		case FADUMP_CPU_STATE_DATA:
			fw_dump.cpu_state_data_size =
					of_read_ulong(&sections[1], 2);
			break;
		case FADUMP_HPTE_REGION:
			fw_dump.hpte_region_size =
					of_read_ulong(&sections[1], 2);
			break;
		}
	}
	return 1;
}

/**
 * fadump_calculate_reserve_size(): reserve variable boot area 5% of System RAM
 *
 * Function to find the largest memory size we need to reserve during early
 * boot process. This will be the size of the memory that is required for a
 * kernel to boot successfully.
 *
 * This function has been taken from phyp-assisted dump feature implementation.
 *
 * returns larger of 256MB or 5% rounded down to multiples of 256MB.
 *
 * TODO: Come up with better approach to find out more accurate memory size
 * that is required for a kernel to boot successfully.
 *
 */
static inline unsigned long fadump_calculate_reserve_size(void)
{
	unsigned long size;

	/*
	 * Check if the size is specified through fadump_reserve_mem= cmdline
	 * option. If yes, then use that.
	 */
	if (fw_dump.reserve_bootvar)
		return fw_dump.reserve_bootvar;

	/* divide by 20 to get 5% of value */
	size = memblock_end_of_DRAM() / 20;

	/* round it down in multiples of 256 */
	size = size & ~0x0FFFFFFFUL;

	/* Truncate to memory_limit. We don't want to over reserve the memory.*/
	if (memory_limit && size > memory_limit)
		size = memory_limit;

	return (size > MIN_BOOT_MEM ? size : MIN_BOOT_MEM);
}

/*
 * Calculate the total memory size required to be reserved for
 * firmware-assisted dump registration.
 */
static unsigned long get_fadump_area_size(void)
{
	unsigned long size = 0;

	size += fw_dump.cpu_state_data_size;
	size += fw_dump.hpte_region_size;
	size += fw_dump.boot_memory_size;

	size = PAGE_ALIGN(size);
	return size;
}

int __init fadump_reserve_mem(void)
{
	unsigned long base, size, memory_boundary;

	if (!fw_dump.fadump_enabled)
		return 0;

	if (!fw_dump.fadump_supported) {
		printk(KERN_INFO "Firmware-assisted dump is not supported on"
				" this hardware\n");
		fw_dump.fadump_enabled = 0;
		return 0;
	}
	/* Initialize boot memory size */
	fw_dump.boot_memory_size = fadump_calculate_reserve_size();

	/*
	 * Calculate the memory boundary.
	 * If memory_limit is less than actual memory boundary then reserve
	 * the memory for fadump beyond the memory_limit and adjust the
	 * memory_limit accordingly, so that the running kernel can run with
	 * specified memory_limit.
	 */
	if (memory_limit && memory_limit < memblock_end_of_DRAM()) {
		size = get_fadump_area_size();
		if ((memory_limit + size) < memblock_end_of_DRAM())
			memory_limit += size;
		else
			memory_limit = memblock_end_of_DRAM();
		printk(KERN_INFO "Adjusted memory_limit for firmware-assisted"
				" dump, now %#016llx\n",
				(unsigned long long)memory_limit);
	}
	if (memory_limit)
		memory_boundary = memory_limit;
	else
		memory_boundary = memblock_end_of_DRAM();

	if (fw_dump.dump_active) {
		printk(KERN_INFO "Firmware-assisted dump is active.\n");
		/*
		 * If last boot has crashed then reserve all the memory
		 * above boot_memory_size so that we don't touch it until
		 * dump is written to disk by userspace tool. This memory
		 * will be released for general use once the dump is saved.
		 */
		base = fw_dump.boot_memory_size;
		size = memory_boundary - base;
		memblock_reserve(base, size);
		printk(KERN_INFO "Reserved %ldMB of memory at %ldMB "
				"for saving crash dump\n",
				(unsigned long)(size >> 20),
				(unsigned long)(base >> 20));
	} else {
		/* Reserve the memory at the top of memory. */
		size = get_fadump_area_size();
		base = memory_boundary - size;
		memblock_reserve(base, size);
		printk(KERN_INFO "Reserved %ldMB of memory at %ldMB "
				"for firmware-assisted dump\n",
				(unsigned long)(size >> 20),
				(unsigned long)(base >> 20));
	}
	fw_dump.reserve_dump_area_start = base;
	fw_dump.reserve_dump_area_size = size;
	return 1;
}

/* Look for fadump= cmdline option. */
static int __init early_fadump_param(char *p)
{
	if (!p)
		return 1;

	if (strncmp(p, "on", 2) == 0)
		fw_dump.fadump_enabled = 1;
	else if (strncmp(p, "off", 3) == 0)
		fw_dump.fadump_enabled = 0;

	return 0;
}
early_param("fadump", early_fadump_param);

/* Look for fadump_reserve_mem= cmdline option */
static int __init early_fadump_reserve_mem(char *p)
{
	if (p)
		fw_dump.reserve_bootvar = memparse(p, &p);
	return 0;
}
early_param("fadump_reserve_mem", early_fadump_reserve_mem);
