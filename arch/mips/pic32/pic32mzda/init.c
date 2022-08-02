// SPDX-License-Identifier: GPL-2.0-only
/*
 * Joshua Henderson, joshua.henderson@microchip.com
 * Copyright (C) 2015 Microchip Technology Inc.  All rights reserved.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>
#include <linux/platform_data/sdhci-pic32.h>

#include <asm/fw/fw.h>
#include <asm/mips-boards/generic.h>
#include <asm/prom.h>

#include "pic32mzda.h"

const char *get_system_type(void)
{
	return "PIC32MZDA";
}

void __init plat_mem_setup(void)
{
	void *dtb;

	dtb = get_fdt();
	if (!dtb) {
		pr_err("pic32: no DTB found.\n");
		return;
	}

	/*
	 * Load the builtin device tree. This causes the chosen node to be
	 * parsed resulting in our memory appearing.
	 */
	__dt_setup_arch(dtb);

	pr_info("Found following command lines\n");
	pr_info(" boot_command_line: %s\n", boot_command_line);
	pr_info(" arcs_cmdline     : %s\n", arcs_cmdline);
#ifdef CONFIG_CMDLINE_BOOL
	pr_info(" builtin_cmdline  : %s\n", CONFIG_CMDLINE);
#endif
	if (dtb != __dtb_start)
		strlcpy(arcs_cmdline, boot_command_line, COMMAND_LINE_SIZE);

#ifdef CONFIG_EARLY_PRINTK
	fw_init_early_console(-1);
#endif
	pic32_config_init();
}

static __init void pic32_init_cmdline(int argc, char *argv[])
{
	unsigned int count = COMMAND_LINE_SIZE - 1;
	int i;
	char *dst = &(arcs_cmdline[0]);
	char *src;

	for (i = 1; i < argc && count; ++i) {
		src = argv[i];
		while (*src && count) {
			*dst++ = *src++;
			--count;
		}
		*dst++ = ' ';
	}
	if (i > 1)
		--dst;

	*dst = 0;
}

void __init prom_init(void)
{
	pic32_init_cmdline((int)fw_arg0, (char **)fw_arg1);
}

static struct pic32_sdhci_platform_data sdhci_data = {
	.setup_dma = pic32_set_sdhci_adma_fifo_threshold,
};

static struct of_dev_auxdata pic32_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("microchip,pic32mzda-sdhci", 0, "sdhci", &sdhci_data),
	{ /* sentinel */}
};

static int __init pic32_of_prepare_platform_data(struct of_dev_auxdata *lookup)
{
	struct device_node *root, *np;
	struct resource res;

	root = of_find_node_by_path("/");

	for (; lookup->compatible; lookup++) {
		np = of_find_compatible_node(NULL, NULL, lookup->compatible);
		if (np) {
			lookup->name = (char *)np->name;
			if (lookup->phys_addr) {
				of_node_put(np);
				continue;
			}
			if (!of_address_to_resource(np, 0, &res))
				lookup->phys_addr = res.start;
			of_node_put(np);
		}
	}

	of_node_put(root);

	return 0;
}

static int __init plat_of_setup(void)
{
	if (!of_have_populated_dt())
		panic("Device tree not present");

	pic32_of_prepare_platform_data(pic32_auxdata_lookup);
	if (of_platform_default_populate(NULL, pic32_auxdata_lookup, NULL))
		panic("Failed to populate DT");

	return 0;
}
arch_initcall(plat_of_setup);
