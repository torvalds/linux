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
#include <asm/addrspace.h>
#include <asm/bootinfo.h>
#include <asm/fw/fw.h>
#include <asm/mips-boards/generic.h>
#include <asm/mips-boards/malta.h>
#include <asm/mips-cm.h>
#include <asm/page.h>

#define ROCIT_REG_BASE			0x1f403000
#define ROCIT_CONFIG_GEN1		(ROCIT_REG_BASE + 0x04)
#define  ROCIT_CONFIG_GEN1_MEMMAP_SHIFT	8
#define  ROCIT_CONFIG_GEN1_MEMMAP_MASK	(0xf << 8)

static unsigned char fdt_buf[16 << 10] __initdata;

/* determined physical memory size, not overridden by command line args	 */
extern unsigned long physical_memsize;

enum mem_map {
	MEM_MAP_V1 = 0,
	MEM_MAP_V2,
};

#define MAX_MEM_ARRAY_ENTRIES 2

static __init int malta_scon(void)
{
	int scon = MIPS_REVISION_SCONID;

	if (scon != MIPS_REVISION_SCON_OTHER)
		return scon;

	switch (MIPS_REVISION_CORID) {
	case MIPS_REVISION_CORID_QED_RM5261:
	case MIPS_REVISION_CORID_CORE_LV:
	case MIPS_REVISION_CORID_CORE_FPGA:
	case MIPS_REVISION_CORID_CORE_FPGAR2:
		return MIPS_REVISION_SCON_GT64120;

	case MIPS_REVISION_CORID_CORE_EMUL_BON:
	case MIPS_REVISION_CORID_BONITO64:
	case MIPS_REVISION_CORID_CORE_20K:
		return MIPS_REVISION_SCON_BONITO;

	case MIPS_REVISION_CORID_CORE_MSC:
	case MIPS_REVISION_CORID_CORE_FPGA2:
	case MIPS_REVISION_CORID_CORE_24K:
		return MIPS_REVISION_SCON_SOCIT;

	case MIPS_REVISION_CORID_CORE_FPGA3:
	case MIPS_REVISION_CORID_CORE_FPGA4:
	case MIPS_REVISION_CORID_CORE_FPGA5:
	case MIPS_REVISION_CORID_CORE_EMUL_MSC:
	default:
		return MIPS_REVISION_SCON_ROCIT;
	}
}

static unsigned __init gen_fdt_mem_array(__be32 *mem_array, unsigned long size,
					 enum mem_map map)
{
	unsigned long size_preio;
	unsigned entries;

	entries = 1;
	mem_array[0] = cpu_to_be32(PHYS_OFFSET);
	if (IS_ENABLED(CONFIG_EVA)) {
		/*
		 * The current Malta EVA configuration is "special" in that it
		 * always makes use of addresses in the upper half of the 32 bit
		 * physical address map, which gives it a contiguous region of
		 * DDR but limits it to 2GB.
		 */
		mem_array[1] = cpu_to_be32(size);
		goto done;
	}

	size_preio = min_t(unsigned long, size, SZ_256M);
	mem_array[1] = cpu_to_be32(size_preio);
	size -= size_preio;
	if (!size)
		goto done;

	if (map == MEM_MAP_V2) {
		/*
		 * We have a flat 32 bit physical memory map with DDR filling
		 * all 4GB of the memory map, apart from the I/O region which
		 * obscures 256MB from 0x10000000-0x1fffffff.
		 *
		 * Therefore we discard the 256MB behind the I/O region.
		 */
		if (size <= SZ_256M)
			goto done;
		size -= SZ_256M;

		/* Make use of the memory following the I/O region */
		entries++;
		mem_array[2] = cpu_to_be32(PHYS_OFFSET + SZ_512M);
		mem_array[3] = cpu_to_be32(size);
	} else {
		/*
		 * We have a 32 bit physical memory map with a 2GB DDR region
		 * aliased in the upper & lower halves of it. The I/O region
		 * obscures 256MB from 0x10000000-0x1fffffff in the low alias
		 * but the DDR it obscures is accessible via the high alias.
		 *
		 * Simply access everything beyond the lowest 256MB of DDR using
		 * the high alias.
		 */
		entries++;
		mem_array[2] = cpu_to_be32(PHYS_OFFSET + SZ_2G + SZ_256M);
		mem_array[3] = cpu_to_be32(size);
	}

done:
	BUG_ON(entries > MAX_MEM_ARRAY_ENTRIES);
	return entries;
}

static void __init append_memory(void *fdt, int root_off)
{
	__be32 mem_array[2 * MAX_MEM_ARRAY_ENTRIES];
	unsigned long memsize;
	unsigned mem_entries;
	int i, err, mem_off;
	enum mem_map mem_map;
	u32 config;
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

	if (IS_ENABLED(CONFIG_CPU_BIG_ENDIAN)) {
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

	/* detect the memory map in use */
	if (malta_scon() == MIPS_REVISION_SCON_ROCIT) {
		/* ROCit has a register indicating the memory map in use */
		config = readl((void __iomem *)CKSEG1ADDR(ROCIT_CONFIG_GEN1));
		mem_map = config & ROCIT_CONFIG_GEN1_MEMMAP_MASK;
		mem_map >>= ROCIT_CONFIG_GEN1_MEMMAP_SHIFT;
	} else {
		/* if not using ROCit, presume the v1 memory map */
		mem_map = MEM_MAP_V1;
	}
	if (mem_map > MEM_MAP_V2)
		panic("Unsupported physical memory map v%u detected",
		      (unsigned int)mem_map);

	/* append memory to the DT */
	mem_off = fdt_add_subnode(fdt, root_off, "memory");
	if (mem_off < 0)
		panic("Unable to add memory node to DT: %d", mem_off);

	err = fdt_setprop_string(fdt, mem_off, "device_type", "memory");
	if (err)
		panic("Unable to set memory node device_type: %d", err);

	mem_entries = gen_fdt_mem_array(mem_array, physical_memsize, mem_map);
	err = fdt_setprop(fdt, mem_off, "reg", mem_array,
			  mem_entries * 2 * sizeof(mem_array[0]));
	if (err)
		panic("Unable to set memory regs property: %d", err);

	mem_entries = gen_fdt_mem_array(mem_array, memsize, mem_map);
	err = fdt_setprop(fdt, mem_off, "linux,usable-memory", mem_array,
			  mem_entries * 2 * sizeof(mem_array[0]));
	if (err)
		panic("Unable to set linux,usable-memory property: %d", err);
}

static void __init remove_gic(void *fdt)
{
	int err, gic_off, i8259_off, cpu_off;
	void __iomem *biu_base;
	uint32_t cpu_phandle, sc_cfg;

	/* if we have a CM which reports a GIC is present, leave the DT alone */
	err = mips_cm_probe();
	if (!err && (read_gcr_gic_status() & CM_GCR_GIC_STATUS_GICEX_MSK))
		return;

	if (malta_scon() == MIPS_REVISION_SCON_ROCIT) {
		/*
		 * On systems using the RocIT system controller a GIC may be
		 * present without a CM. Detect whether that is the case.
		 */
		biu_base = ioremap_nocache(MSC01_BIU_REG_BASE,
				MSC01_BIU_ADDRSPACE_SZ);
		sc_cfg = __raw_readl(biu_base + MSC01_SC_CFG_OFS);
		if (sc_cfg & MSC01_SC_CFG_GICPRES_MSK) {
			/* enable the GIC at the system controller level */
			sc_cfg |= BIT(MSC01_SC_CFG_GICENA_SHF);
			__raw_writel(sc_cfg, biu_base + MSC01_SC_CFG_OFS);
			return;
		}
	}

	gic_off = fdt_node_offset_by_compatible(fdt, -1, "mti,gic");
	if (gic_off < 0) {
		pr_warn("malta-dtshim: unable to find DT GIC node: %d\n",
			gic_off);
		return;
	}

	err = fdt_nop_node(fdt, gic_off);
	if (err)
		pr_warn("malta-dtshim: unable to nop GIC node\n");

	i8259_off = fdt_node_offset_by_compatible(fdt, -1, "intel,i8259");
	if (i8259_off < 0) {
		pr_warn("malta-dtshim: unable to find DT i8259 node: %d\n",
			i8259_off);
		return;
	}

	cpu_off = fdt_node_offset_by_compatible(fdt, -1,
			"mti,cpu-interrupt-controller");
	if (cpu_off < 0) {
		pr_warn("malta-dtshim: unable to find CPU intc node: %d\n",
			cpu_off);
		return;
	}

	cpu_phandle = fdt_get_phandle(fdt, cpu_off);
	if (!cpu_phandle) {
		pr_warn("malta-dtshim: unable to get CPU intc phandle\n");
		return;
	}

	err = fdt_setprop_u32(fdt, i8259_off, "interrupt-parent", cpu_phandle);
	if (err) {
		pr_warn("malta-dtshim: unable to set i8259 interrupt-parent: %d\n",
			err);
		return;
	}

	err = fdt_setprop_u32(fdt, i8259_off, "interrupts", 2);
	if (err) {
		pr_warn("malta-dtshim: unable to set i8259 interrupts: %d\n",
			err);
		return;
	}
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
	remove_gic(fdt_buf);

	err = fdt_pack(fdt_buf);
	if (err)
		panic("Unable to pack FDT: %d\n", err);

	return fdt_buf;
}
