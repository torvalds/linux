/*
 * Copyright (C) 2015 Imagination Technologies
 * Author: Paul Burton <paul.burton@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/libfdt.h>
#include <linux/of_fdt.h>
#include <linux/sizes.h>
#include <asm/bootinfo.h>
#include <asm/fw/fw.h>
#include <asm/page.h>

static unsigned char fdt_buf[16 << 10] __initdata;

/* determined physical memory size, not overridden by command line args	 */
extern unsigned long physical_memsize;

#define MAX_MEM_ARRAY_ENTRIES 1

static unsigned __init gen_fdt_mem_array(__be32 *mem_array, unsigned long size)
{
	unsigned long size_preio;
	unsigned entries;

	entries = 1;
	mem_array[0] = cpu_to_be32(PHYS_OFFSET);
	if (config_enabled(CONFIG_EVA)) {
		/*
		 * The current Malta EVA configuration is "special" in that it
		 * always makes use of addresses in the upper half of the 32 bit
		 * physical address map, which gives it a contiguous region of
		 * DDR but limits it to 2GB.
		 */
		mem_array[1] = cpu_to_be32(size);
	} else {
		size_preio = min_t(unsigned long, size, SZ_256M);
		mem_array[1] = cpu_to_be32(size_preio);
	}

	BUG_ON(entries > MAX_MEM_ARRAY_ENTRIES);
	return entries;
}

static void __init append_memory(void *fdt, int root_off)
{
	__be32 mem_array[2 * MAX_MEM_ARRAY_ENTRIES];
	unsigned long memsize;
	unsigned mem_entries;
	int i, err, mem_off;
	char *var, param_name[10], *var_names[] = {
		"ememsize", "memsize",
	};

	/* if a memory node already exists, leave it alone */
	mem_off = fdt_path_offset(fdt, "/memory");
	if (mem_off >= 0)
		return;

	/* find memory size from the bootloader environment */
	for (i = 0; i < ARRAY_SIZE(var_names); i++) {
		var = fw_getenv(var_names[i]);
		if (!var)
			continue;

		err = kstrtoul(var, 0, &physical_memsize);
		if (!err)
			break;

		pr_warn("Failed to read the '%s' env variable '%s'\n",
			var_names[i], var);
	}

	if (!physical_memsize) {
		pr_warn("The bootloader didn't provide memsize: defaulting to 32MB\n");
		physical_memsize = 32 << 20;
	}

	if (config_enabled(CONFIG_CPU_BIG_ENDIAN)) {
		/*
		 * SOC-it swaps, or perhaps doesn't swap, when DMA'ing
		 * the last word of physical memory.
		 */
		physical_memsize -= PAGE_SIZE;
	}

	/* default to using all available RAM */
	memsize = physical_memsize;

	/* allow the user to override the usable memory */
	for (i = 0; i < ARRAY_SIZE(var_names); i++) {
		snprintf(param_name, sizeof(param_name), "%s=", var_names[i]);
		var = strstr(arcs_cmdline, param_name);
		if (!var)
			continue;

		memsize = memparse(var + strlen(param_name), NULL);
	}

	/* if the user says there's more RAM than we thought, believe them */
	physical_memsize = max_t(unsigned long, physical_memsize, memsize);

	/* append memory to the DT */
	mem_off = fdt_add_subnode(fdt, root_off, "memory");
	if (mem_off < 0)
		panic("Unable to add memory node to DT: %d", mem_off);

	err = fdt_setprop_string(fdt, mem_off, "device_type", "memory");
	if (err)
		panic("Unable to set memory node device_type: %d", err);

	mem_entries = gen_fdt_mem_array(mem_array, physical_memsize);
	err = fdt_setprop(fdt, mem_off, "reg", mem_array,
			  mem_entries * 2 * sizeof(mem_array[0]));
	if (err)
		panic("Unable to set memory regs property: %d", err);

	mem_entries = gen_fdt_mem_array(mem_array, memsize);
	err = fdt_setprop(fdt, mem_off, "linux,usable-memory", mem_array,
			  mem_entries * 2 * sizeof(mem_array[0]));
	if (err)
		panic("Unable to set linux,usable-memory property: %d", err);
}

void __init *malta_dt_shim(void *fdt)
{
	int root_off, len, err;
	const char *compat;

	if (fdt_check_header(fdt))
		panic("Corrupt DT");

	err = fdt_open_into(fdt, fdt_buf, sizeof(fdt_buf));
	if (err)
		panic("Unable to open FDT: %d", err);

	root_off = fdt_path_offset(fdt_buf, "/");
	if (root_off < 0)
		panic("No / node in DT");

	compat = fdt_getprop(fdt_buf, root_off, "compatible", &len);
	if (!compat)
		panic("No root compatible property in DT: %d", len);

	/* if this isn't Malta, leave the DT alone */
	if (strncmp(compat, "mti,malta", len))
		return fdt;

	append_memory(fdt_buf, root_off);

	err = fdt_pack(fdt_buf);
	if (err)
		panic("Unable to pack FDT: %d\n", err);

	return fdt_buf;
}
