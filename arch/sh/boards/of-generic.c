// SPDX-License-Identifier: GPL-2.0
/*
 * SH generic board support, using device tree
 *
 * Copyright (C) 2015-2016 Smart Energy Instruments, Inc.
 */

#include <linux/of.h>
#include <linux/of_clk.h>
#include <linux/of_fdt.h>
#include <linux/clocksource.h>
#include <linux/irqchip.h>

#include <asm/clock.h>
#include <asm/machvec.h>
#include <asm/rtc.h>

#ifdef CONFIG_SMP

static void dummy_smp_setup(void)
{
}

static void dummy_prepare_cpus(unsigned int max_cpus)
{
}

static void dummy_start_cpu(unsigned int cpu, unsigned long entry_point)
{
}

static unsigned int dummy_smp_processor_id(void)
{
	return 0;
}

static void dummy_send_ipi(unsigned int cpu, unsigned int message)
{
}

static struct plat_smp_ops dummy_smp_ops = {
	.smp_setup		= dummy_smp_setup,
	.prepare_cpus		= dummy_prepare_cpus,
	.start_cpu		= dummy_start_cpu,
	.smp_processor_id	= dummy_smp_processor_id,
	.send_ipi		= dummy_send_ipi,
	.cpu_die		= native_cpu_die,
	.cpu_disable		= native_cpu_disable,
	.play_dead		= native_play_dead,
};

extern const struct of_cpu_method __cpu_method_of_table[];
const struct of_cpu_method __cpu_method_of_table_sentinel
	__section("__cpu_method_of_table_end");

static void sh_of_smp_probe(void)
{
	struct device_node *np;
	const char *method = NULL;
	const struct of_cpu_method *m = __cpu_method_of_table;

	pr_info("SH generic board support: scanning for cpus\n");

	init_cpu_possible(cpumask_of(0));

	for_each_of_cpu_node(np) {
		u64 id = of_get_cpu_hwid(np, 0);

		if (id < NR_CPUS) {
			if (!method)
				of_property_read_string(np, "enable-method", &method);
			set_cpu_possible(id, true);
			set_cpu_present(id, true);
			__cpu_number_map[id] = id;
			__cpu_logical_map[id] = id;
		}
	}
	if (!method) {
		np = of_find_node_by_name(NULL, "cpus");
		of_property_read_string(np, "enable-method", &method);
		of_node_put(np);
	}

	pr_info("CPU enable method: %s\n", method);
	if (method)
		for (; m->method; m++)
			if (!strcmp(m->method, method)) {
				register_smp_ops(m->ops);
				return;
			}

	register_smp_ops(&dummy_smp_ops);
}

#else

static void sh_of_smp_probe(void)
{
}

#endif

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

static void __init sh_of_setup(char **cmdline_p)
{
	struct device_node *root;

	sh_mv.mv_name = "Unknown SH model";
	root = of_find_node_by_path("/");
	if (root) {
		of_property_read_string(root, "model", &sh_mv.mv_name);
		of_node_put(root);
	}

	sh_of_smp_probe();
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

void __init __weak arch_init_clk_ops(struct sh_clk_ops **ops, int idx)
{
}

void __init __weak plat_irq_setup(void)
{
}
