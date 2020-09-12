// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2009-2010, Lars-Peter Clausen <lars@metafoo.de>
 *  Copyright (C) 2011, Maarten ter Huurne <maarten@treewalker.org>
 *  JZ4740 setup code
 */

#include <linux/clocksource.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/irqchip.h>
#include <linux/kernel.h>
#include <linux/libfdt.h>
#include <linux/of_clk.h>
#include <linux/of_fdt.h>
#include <linux/pm.h>
#include <linux/sizes.h>
#include <linux/suspend.h>

#include <asm/bootinfo.h>
#include <asm/fw/fw.h>
#include <asm/prom.h>
#include <asm/reboot.h>
#include <asm/time.h>

static unsigned long __init get_board_mach_type(const void *fdt)
{
	if (!fdt_node_check_compatible(fdt, 0, "ingenic,x2000"))
		return MACH_INGENIC_X2000;
	if (!fdt_node_check_compatible(fdt, 0, "ingenic,x1830"))
		return MACH_INGENIC_X1830;
	if (!fdt_node_check_compatible(fdt, 0, "ingenic,x1000"))
		return MACH_INGENIC_X1000;
	if (!fdt_node_check_compatible(fdt, 0, "ingenic,jz4780"))
		return MACH_INGENIC_JZ4780;
	if (!fdt_node_check_compatible(fdt, 0, "ingenic,jz4770"))
		return MACH_INGENIC_JZ4770;
	if (!fdt_node_check_compatible(fdt, 0, "ingenic,jz4725b"))
		return MACH_INGENIC_JZ4725B;

	return MACH_INGENIC_JZ4740;
}

void __init plat_mem_setup(void)
{
	void *dtb = (void *)fw_passed_dtb;

	__dt_setup_arch(dtb);

	/*
	 * Old devicetree files for the qi,lb60 board did not have a /memory
	 * node. Hardcode the memory info here.
	 */
	if (!fdt_node_check_compatible(dtb, 0, "qi,lb60") &&
	    fdt_path_offset(dtb, "/memory") < 0)
		early_init_dt_add_memory_arch(0, SZ_32M);

	mips_machtype = get_board_mach_type(dtb);
}

void __init device_tree_init(void)
{
	if (!initial_boot_params)
		return;

	unflatten_and_copy_device_tree();
}

const char *get_system_type(void)
{
	switch (mips_machtype) {
	case MACH_INGENIC_X2000:
		return "X2000";
	case MACH_INGENIC_X1830:
		return "X1830";
	case MACH_INGENIC_X1000:
		return "X1000";
	case MACH_INGENIC_JZ4780:
		return "JZ4780";
	case MACH_INGENIC_JZ4770:
		return "JZ4770";
	case MACH_INGENIC_JZ4725B:
		return "JZ4725B";
	default:
		return "JZ4740";
	}
}

void __init arch_init_irq(void)
{
	irqchip_init();
}

void __init plat_time_init(void)
{
	of_clk_init(NULL);
	timer_probe();
}

void __init prom_init(void)
{
	fw_init_cmdline();
}

void __init prom_free_prom_memory(void)
{
}

static void jz4740_wait_instr(void)
{
	__asm__(".set push;\n"
		".set mips3;\n"
		"wait;\n"
		".set pop;\n"
	);
}

static void jz4740_halt(void)
{
	for (;;)
		jz4740_wait_instr();
}

static int __maybe_unused jz4740_pm_enter(suspend_state_t state)
{
	jz4740_wait_instr();

	return 0;
}

static const struct platform_suspend_ops jz4740_pm_ops __maybe_unused = {
	.valid = suspend_valid_only_mem,
	.enter = jz4740_pm_enter,
};

static int __init jz4740_pm_init(void)
{
	if (IS_ENABLED(CONFIG_PM_SLEEP))
		suspend_set_ops(&jz4740_pm_ops);
	_machine_halt = jz4740_halt;

	return 0;

}
late_initcall(jz4740_pm_init);
