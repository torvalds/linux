/*
 * Copyright (C) 2016 Imagination Technologies
 * Author: Paul Burton <paul.burton@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#define pr_fmt(fmt) "yamon-dt: " fmt

#include <linux/bug.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/libfdt.h>
#include <linux/printk.h>

#include <asm/fw/fw.h>

__init int yamon_dt_append_cmdline(void *fdt)
{
	int err, chosen_off;

	/* find or add chosen node */
	chosen_off = fdt_path_offset(fdt, "/chosen");
	if (chosen_off == -FDT_ERR_NOTFOUND)
		chosen_off = fdt_path_offset(fdt, "/chosen@0");
	if (chosen_off == -FDT_ERR_NOTFOUND)
		chosen_off = fdt_add_subnode(fdt, 0, "chosen");
	if (chosen_off < 0) {
		pr_err("Unable to find or add DT chosen node: %d\n",
		       chosen_off);
		return chosen_off;
	}

	err = fdt_setprop_string(fdt, chosen_off, "bootargs", fw_getcmdline());
	if (err) {
		pr_err("Unable to set bootargs property: %d\n", err);
		return err;
	}

	return 0;
}

__init int yamon_dt_append_memory(void *fdt)
{
	unsigned long phys_memsize, memsize;
	__be32 mem_array[2];
	int err, mem_off;
	char *var;

	/* find memory size from the bootloader environment */
	var = fw_getenv("memsize");
	if (var) {
		err = kstrtoul(var, 0, &phys_memsize);
		if (err) {
			pr_err("Failed to read memsize env variable '%s'\n",
			       var);
			return -EINVAL;
		}
	} else {
		pr_warn("The bootloader didn't provide memsize: defaulting to 32MB\n");
		phys_memsize = 32 << 20;
	}

	/* default to using all available RAM */
	memsize = phys_memsize;

	/* allow the user to override the usable memory */
	var = strstr(arcs_cmdline, "memsize=");
	if (var)
		memsize = memparse(var + strlen("memsize="), NULL);

	/* if the user says there's more RAM than we thought, believe them */
	phys_memsize = max_t(unsigned long, phys_memsize, memsize);

	/* find or add a memory node */
	mem_off = fdt_path_offset(fdt, "/memory");
	if (mem_off == -FDT_ERR_NOTFOUND)
		mem_off = fdt_add_subnode(fdt, 0, "memory");
	if (mem_off < 0) {
		pr_err("Unable to find or add memory DT node: %d\n", mem_off);
		return mem_off;
	}

	err = fdt_setprop_string(fdt, mem_off, "device_type", "memory");
	if (err) {
		pr_err("Unable to set memory node device_type: %d\n", err);
		return err;
	}

	mem_array[0] = 0;
	mem_array[1] = cpu_to_be32(phys_memsize);
	err = fdt_setprop(fdt, mem_off, "reg", mem_array, sizeof(mem_array));
	if (err) {
		pr_err("Unable to set memory regs property: %d\n", err);
		return err;
	}

	mem_array[0] = 0;
	mem_array[1] = cpu_to_be32(memsize);
	err = fdt_setprop(fdt, mem_off, "linux,usable-memory",
			  mem_array, sizeof(mem_array));
	if (err) {
		pr_err("Unable to set linux,usable-memory property: %d\n", err);
		return err;
	}

	return 0;
}

__init int yamon_dt_serial_config(void *fdt)
{
	const char *yamontty, *mode_var;
	char mode_var_name[9], path[18], parity;
	unsigned int uart, baud, stop_bits;
	bool hw_flow;
	int chosen_off, err;

	yamontty = fw_getenv("yamontty");
	if (!yamontty || !strcmp(yamontty, "tty0")) {
		uart = 0;
	} else if (!strcmp(yamontty, "tty1")) {
		uart = 1;
	} else {
		pr_warn("yamontty environment variable '%s' invalid\n",
			yamontty);
		uart = 0;
	}

	baud = stop_bits = 0;
	parity = 0;
	hw_flow = false;

	snprintf(mode_var_name, sizeof(mode_var_name), "modetty%u", uart);
	mode_var = fw_getenv(mode_var_name);
	if (mode_var) {
		while (mode_var[0] >= '0' && mode_var[0] <= '9') {
			baud *= 10;
			baud += mode_var[0] - '0';
			mode_var++;
		}
		if (mode_var[0] == ',')
			mode_var++;
		if (mode_var[0])
			parity = mode_var[0];
		if (mode_var[0] == ',')
			mode_var++;
		if (mode_var[0])
			stop_bits = mode_var[0] - '0';
		if (mode_var[0] == ',')
			mode_var++;
		if (!strcmp(mode_var, "hw"))
			hw_flow = true;
	}

	if (!baud)
		baud = 38400;

	if (parity != 'e' && parity != 'n' && parity != 'o')
		parity = 'n';

	if (stop_bits != 7 && stop_bits != 8)
		stop_bits = 8;

	WARN_ON(snprintf(path, sizeof(path), "uart%u:%u%c%u%s",
			 uart, baud, parity, stop_bits,
			 hw_flow ? "r" : "") >= sizeof(path));

	/* find or add chosen node */
	chosen_off = fdt_path_offset(fdt, "/chosen");
	if (chosen_off == -FDT_ERR_NOTFOUND)
		chosen_off = fdt_path_offset(fdt, "/chosen@0");
	if (chosen_off == -FDT_ERR_NOTFOUND)
		chosen_off = fdt_add_subnode(fdt, 0, "chosen");
	if (chosen_off < 0) {
		pr_err("Unable to find or add DT chosen node: %d\n",
		       chosen_off);
		return chosen_off;
	}

	err = fdt_setprop_string(fdt, chosen_off, "stdout-path", path);
	if (err) {
		pr_err("Unable to set stdout-path property: %d\n", err);
		return err;
	}

	return 0;
}
