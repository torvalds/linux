/*
 * SH generic board support, using device tree
 *
 * Copyright (C) 2015-2016 Smart Energy Instruments, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_fdt.h>
#include <linux/of_iommu.h>
#include <linux/clocksource.h>
#include <linux/irqchip.h>
#include <linux/clk-provider.h>
#include <asm/machvec.h>
#include <asm/rtc.h>

static void noop(void)
{
}

static int noopi(void)
{
	return 0;
}

static void __init sh_of_mem_reserve(void)
{
	early_init_fdt_reserve_self();
	early_init_fdt_scan_reserved_mem();
}

static void __init sh_of_time_init(void)
{
	pr_info("SH generic board support: scanning for clocksource devices\n");
	clocksource_probe();
}

static void __init sh_of_setup(char **cmdline_p)
{
	unflatten_device_tree();

	board_time_init = sh_of_time_init;

	sh_mv.mv_name = of_flat_dt_get_machine_name();
	if (!sh_mv.mv_name)
		sh_mv.mv_name = "Unknown SH model";

	/* FIXME: register smp ops to use dt to find cpus, use
	 * cpu enable-method, and use irq controller's ipi
	 * functions. */
}

static int sh_of_irq_demux(int irq)
{
	/* FIXME: eventually this should not be used at all;
	 * the interrupt controller should set_handle_irq(). */
	return irq;
}

static void __init sh_of_init_irq(void)
{
	pr_info("SH generic board support: scanning for interrupt controllers\n");
	irqchip_init();
}

static int __init sh_of_clk_init(void)
{
#ifdef CONFIG_COMMON_CLK
	/* Disabled pending move to COMMON_CLK framework. */
	pr_info("SH generic board support: scanning for clk providers\n");
	of_clk_init(NULL);
#endif
	return 0;
}

static struct sh_machine_vector __initmv sh_of_generic_mv = {
	.mv_setup	= sh_of_setup,
	.mv_name	= "devicetree", /* replaced by DT root's model */
	.mv_irq_demux	= sh_of_irq_demux,
	.mv_init_irq	= sh_of_init_irq,
	.mv_clk_init	= sh_of_clk_init,
	.mv_mode_pins	= noopi,
	.mv_mem_init	= noop,
	.mv_mem_reserve	= sh_of_mem_reserve,
};

struct sh_clk_ops;

void __init arch_init_clk_ops(struct sh_clk_ops **ops, int idx)
{
}

void __init plat_irq_setup(void)
{
}

static int __init sh_of_device_init(void)
{
	pr_info("SH generic board support: populating platform devices\n");
	if (of_have_populated_dt()) {
		of_iommu_init();
		of_platform_populate(NULL, of_default_bus_match_table,
				     NULL, NULL);
	} else {
		pr_crit("Device tree not populated\n");
	}
	return 0;
}
arch_initcall_sync(sh_of_device_init);
