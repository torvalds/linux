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
#include <asm/fw/fw.h>

#include <asm/mach-sead3/sead3-dtshim.h>
#include <asm/mips-boards/generic.h>

const char *get_system_type(void)
{
	return "MIPS SEAD3";
}

static uint32_t get_memsize_from_cmdline(void)
{
	int memsize = 0;
	char *p = arcs_cmdline;
	char *s = "memsize=";

	p = strstr(p, s);
	if (p) {
		p += strlen(s);
		memsize = memparse(p, NULL);
	}

	return memsize;
}

static uint32_t get_memsize_from_env(void)
{
	int memsize = 0;
	char *p;

	p = fw_getenv("memsize");
	if (p)
		memsize = memparse(p, NULL);

	return memsize;
}

static uint32_t get_memsize(void)
{
	uint32_t memsize;

	memsize = get_memsize_from_cmdline();
	if (memsize)
		return memsize;

	return get_memsize_from_env();
}

static void __init parse_memsize_param(void)
{
	int offset;
	const uint64_t *prop_value;
	int prop_len;
	uint32_t memsize = get_memsize();

	if (!memsize)
		return;

	offset = fdt_path_offset(__dtb_start, "/memory");
	if (offset > 0) {
		uint64_t new_value;
		/*
		 * reg contains 2 32-bits BE values, offset and size. We just
		 * want to replace the size value without affecting the offset
		 */
		prop_value = fdt_getprop(__dtb_start, offset, "reg", &prop_len);
		new_value = be64_to_cpu(*prop_value);
		new_value =  (new_value & ~0xffffffffllu) | memsize;
		fdt_setprop_inplace_u64(__dtb_start, offset, "reg", new_value);
	}
}

void __init *plat_get_fdt(void)
{
	return (void *)__dtb_start;
}

void __init plat_mem_setup(void)
{
	void *fdt = plat_get_fdt();

	/* allow command line/bootloader env to override memory size in DT */
	parse_memsize_param();

	fdt = sead3_dt_shim(fdt);
	__dt_setup_arch(fdt);
}

void __init device_tree_init(void)
{
	unflatten_and_copy_device_tree();
}
