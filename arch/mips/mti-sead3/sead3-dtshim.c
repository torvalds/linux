/*
 * Copyright (C) 2016 Imagination Technologies
 * Author: Paul Burton <paul.burton@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#define pr_fmt(fmt) "sead3-dtshim: " fmt

#include <linux/errno.h>
#include <linux/libfdt.h>
#include <linux/printk.h>

#include <asm/io.h>

#define SEAD_CONFIG			CKSEG1ADDR(0x1b100110)
#define SEAD_CONFIG_GIC_PRESENT		BIT(1)

static unsigned char fdt_buf[16 << 10] __initdata;

static int remove_gic(void *fdt)
{
	int gic_off, cpu_off, err;
	uint32_t cfg, cpu_phandle;

	/* leave the GIC node intact if a GIC is present */
	cfg = __raw_readl((uint32_t *)SEAD_CONFIG);
	if (cfg & SEAD_CONFIG_GIC_PRESENT)
		return 0;

	gic_off = fdt_node_offset_by_compatible(fdt, -1, "mti,gic");
	if (gic_off < 0) {
		pr_err("unable to find DT GIC node: %d\n", gic_off);
		return gic_off;
	}

	err = fdt_nop_node(fdt, gic_off);
	if (err) {
		pr_err("unable to nop GIC node\n");
		return err;
	}

	cpu_off = fdt_node_offset_by_compatible(fdt, -1,
			"mti,cpu-interrupt-controller");
	if (cpu_off < 0) {
		pr_err("unable to find CPU intc node: %d\n", cpu_off);
		return cpu_off;
	}

	cpu_phandle = fdt_get_phandle(fdt, cpu_off);
	if (!cpu_phandle) {
		pr_err("unable to get CPU intc phandle\n");
		return -EINVAL;
	}

	err = fdt_setprop_u32(fdt, 0, "interrupt-parent", cpu_phandle);
	if (err) {
		pr_err("unable to set root interrupt-parent: %d\n", err);
		return err;
	}

	return 0;
}

void __init *sead3_dt_shim(void *fdt)
{
	int err;

	if (fdt_check_header(fdt))
		panic("Corrupt DT");

	/* if this isn't SEAD3, leave the DT alone */
	if (fdt_node_check_compatible(fdt, 0, "mti,sead-3"))
		return fdt;

	err = fdt_open_into(fdt, fdt_buf, sizeof(fdt_buf));
	if (err)
		panic("Unable to open FDT: %d", err);

	err = remove_gic(fdt_buf);
	if (err)
		panic("Unable to patch FDT: %d", err);

	err = fdt_pack(fdt_buf);
	if (err)
		panic("Unable to pack FDT: %d\n", err);

	return fdt_buf;
}
