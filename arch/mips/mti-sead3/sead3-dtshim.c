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

#include <asm/fw/fw.h>
#include <asm/io.h>

#define SEAD_CONFIG			CKSEG1ADDR(0x1b100110)
#define SEAD_CONFIG_GIC_PRESENT		BIT(1)

static unsigned char fdt_buf[16 << 10] __initdata;

static int append_memory(void *fdt)
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

static int remove_gic(void *fdt)
{
	const unsigned int cpu_ehci_int = 2;
	const unsigned int cpu_uart_int = 4;
	const unsigned int cpu_eth_int = 6;
	int gic_off, cpu_off, uart_off, eth_off, ehci_off, err;
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

	uart_off = fdt_node_offset_by_compatible(fdt, -1, "ns16550a");
	while (uart_off >= 0) {
		err = fdt_setprop_u32(fdt, uart_off, "interrupts",
				      cpu_uart_int);
		if (err) {
			pr_err("unable to set UART interrupts property: %d\n",
			       err);
			return err;
		}

		uart_off = fdt_node_offset_by_compatible(fdt, uart_off,
							 "ns16550a");
	}
	if (uart_off != -FDT_ERR_NOTFOUND) {
		pr_err("error searching for UART DT node: %d\n", uart_off);
		return uart_off;
	}

	eth_off = fdt_node_offset_by_compatible(fdt, -1, "smsc,lan9115");
	if (eth_off < 0) {
		pr_err("unable to find ethernet DT node: %d\n", eth_off);
		return eth_off;
	}

	err = fdt_setprop_u32(fdt, eth_off, "interrupts", cpu_eth_int);
	if (err) {
		pr_err("unable to set ethernet interrupts property: %d\n", err);
		return err;
	}

	ehci_off = fdt_node_offset_by_compatible(fdt, -1, "mti,sead3-ehci");
	if (ehci_off < 0) {
		pr_err("unable to find EHCI DT node: %d\n", ehci_off);
		return ehci_off;
	}

	err = fdt_setprop_u32(fdt, ehci_off, "interrupts", cpu_ehci_int);
	if (err) {
		pr_err("unable to set EHCI interrupts property: %d\n", err);
		return err;
	}

	return 0;
}

static int serial_config(void *fdt)
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

	err = append_memory(fdt_buf);
	if (err)
		panic("Unable to patch FDT: %d", err);

	err = remove_gic(fdt_buf);
	if (err)
		panic("Unable to patch FDT: %d", err);

	err = serial_config(fdt_buf);
	if (err)
		panic("Unable to patch FDT: %d", err);

	err = fdt_pack(fdt_buf);
	if (err)
		panic("Unable to pack FDT: %d\n", err);

	return fdt_buf;
}
