// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/soc/rockchip/rockchip_debug.c
 *
 * Arm debug driver
 *
 * Copyright (C) 2019 ROCKCHIP, Inc.
 */

/*	RK3399
 *	debug {
 *		compatible = "rockchip,debug";
 *		reg = <0x0 0xfe430000 0x0 0x1000>,
 *		      <0x0 0xfe432000 0x0 0x1000>,
 *		      <0x0 0xfe434000 0x0 0x1000>,
 *		      <0x0 0xfe436000 0x0 0x1000>,
 *		      <0x0 0xfe610000 0x0 0x1000>,
 *		      <0x0 0xfe710000 0x0 0x1000>;
 *	};
 */

/*	RK3326
 *	debug {
 *		compatible = "rockchip,debug";
 *		reg = <0x0 0xff690000 0x0 0x1000>,
 *		      <0x0 0xff692000 0x0 0x1000>,
 *		      <0x0 0xff694000 0x0 0x1000>,
 *		      <0x0 0xff696000 0x0 0x1000>;
 *	};
 */

/*	RK3308
 *	debug {
 *		compatible = "rockchip,debug";
 *		reg = <0x0 0xff810000 0x0 0x1000>,
 *		      <0x0 0xff812000 0x0 0x1000>,
 *		      <0x0 0xff814000 0x0 0x1000>,
 *		      <0x0 0xff816000 0x0 0x1000>;
 *	};
 */

/*	RK3288
 *	debug {
 *		compatible = "rockchip,debug";
 *		reg = <0x0 0xffbb0000 0x0 0x1000>,
 *		      <0x0 0xffbb2000 0x0 0x1000>,
 *		      <0x0 0xffbb4000 0x0 0x1000>,
 *		      <0x0 0xffbb6000 0x0 0x1000>;
 *	};
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include "../../staging/android/fiq_debugger/fiq_debugger_priv.h"
#include "rockchip_debug.h"

#define EDPCSR_LO			0x0a0
#define EDPCSR_HI			0x0ac
#define EDLAR				0xfb0
#define EDLAR_UNLOCK			0xc5acce55

#define EDPRSR				0x314
#define EDPRSR_PU			0x1

#define NUM_CPU_SAMPLES			100
#define NUM_SAMPLES_TO_PRINT		32

static void __iomem *rockchip_cpu_debug[16];

#if IS_ENABLED(CONFIG_FIQ_DEBUGGER)
int rockchip_debug_dump_pcsr(struct fiq_debugger_output *output)
{
	unsigned long dbgpcsr;
	int i = 0, j = 0;
	void *pc = NULL;
	void *prev_pc = NULL;
	int printed = 0;
	void __iomem *base;
	u32 pu = 0;

	while (rockchip_cpu_debug[i]) {
		base = rockchip_cpu_debug[i];

		pu = (u32)readl(base + EDPRSR) & EDPRSR_PU;

		if (pu != EDPRSR_PU) {
			i++;
			continue;
		}
		/* Unlock EDLSR.SLK so that EDPCSRhi gets populated */
		writel(EDLAR_UNLOCK, base + EDLAR);

		output->printf(output,
				"CPU%d online:%d\n", i, cpu_online(i));

		/* Try to read a bunch of times if CPU is actually running */
		for (j = 0; j < NUM_CPU_SAMPLES &&
			    printed < NUM_SAMPLES_TO_PRINT; j++) {
			if (sizeof(dbgpcsr) == 8)
				dbgpcsr = ((u64)readl(base + EDPCSR_LO)) |
				  ((u64)readl(base + EDPCSR_HI) << 32);
			else
				dbgpcsr = (u32)readl(base + EDPCSR_LO);

			/* NOTE: no offset on ARMv8; see DBGDEVID1.PCSROffset */
			pc = (void *)(dbgpcsr & ~1);

			if (pc != prev_pc) {
				output->printf(output,
					       "\tPC: <0x%px> %pS\n", pc, pc);
				printed++;
			}
			prev_pc = pc;
		}

		output->printf(output, "\n");
		i++;
		prev_pc = NULL;
		printed = 0;
	}
	return NOTIFY_OK;
}
EXPORT_SYMBOL_GPL(rockchip_debug_dump_pcsr);
#endif

static int rockchip_panic_notify(struct notifier_block *nb, unsigned long event,
				 void *p)
{
	unsigned long dbgpcsr;
	int i = 0, j;
	void *pc = NULL;
	void *prev_pc = NULL;
	int printed = 0;
	void __iomem *base;
	u32 pu = 0;

	/*
	 * The panic handler will try to shut down the other CPUs.
	 * If any of them are still online at this point, this loop attempts
	 * to determine the program counter value.  If there are no wedged
	 * CPUs, this loop will do nothing.
	 */

	while (rockchip_cpu_debug[i]) {
		base = rockchip_cpu_debug[i];

		pu = (u32)readl(base + EDPRSR) & EDPRSR_PU;

		if (pu != EDPRSR_PU) {
			i++;
			continue;
		}
		/* Unlock EDLSR.SLK so that EDPCSRhi gets populated */
		writel(EDLAR_UNLOCK, base + EDLAR);

		pr_err("CPU%d online:%d\n", i, cpu_online(i));

		/* Try to read a bunch of times if CPU is actually running */
		for (j = 0; j < NUM_CPU_SAMPLES &&
			    printed < NUM_SAMPLES_TO_PRINT; j++) {
			if (sizeof(dbgpcsr) == 8)
				dbgpcsr = ((u64)readl(base + EDPCSR_LO)) |
				  ((u64)readl(base + EDPCSR_HI) << 32);
			else
				dbgpcsr = (u32)readl(base + EDPCSR_LO);

			/* NOTE: no offset on ARMv8; see DBGDEVID1.PCSROffset */
			pc = (void *)(dbgpcsr & ~1);

			if (pc != prev_pc) {
				pr_err("\tPC: <0x%px> %pS\n", pc, pc);
				printed++;
			}
			prev_pc = pc;
		}

		pr_err("\n");
		i++;
		prev_pc = NULL;
		printed = 0;
	}
	return NOTIFY_OK;
}

static struct notifier_block rockchip_panic_nb = {
	.notifier_call = rockchip_panic_notify,
};

static const struct of_device_id rockchip_debug_dt_match[] __initconst = {
	{
		.compatible = "rockchip,debug",
	},
	{ /* sentinel */ },
};

static int __init rockchip_debug_init(void)
{
	int i;
	struct device_node *np = NULL;

	np = of_find_matching_node_and_match(NULL,
				rockchip_debug_dt_match, NULL);

	if (!np)
		return -ENODEV;

	i = -1;
	do {
		i++;
		rockchip_cpu_debug[i] = of_iomap(np, i);
	} while (rockchip_cpu_debug[i]);

	of_node_put(np);

	atomic_notifier_chain_register(&panic_notifier_list,
				       &rockchip_panic_nb);
	return 0;
}
arch_initcall(rockchip_debug_init);

static void __exit rockchip_debug_exit(void)
{
	int i = 0;

	atomic_notifier_chain_unregister(&panic_notifier_list,
					 &rockchip_panic_nb);

	while (rockchip_cpu_debug[i])
		iounmap(rockchip_cpu_debug[i++]);
}
module_exit(rockchip_debug_exit);

MODULE_AUTHOR("Huibin Hong <huibin.hong@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip Debugger");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:rockchip-debugger");
