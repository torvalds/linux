// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2011, 2014-2016, 2018, 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/cpuidle.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/syscore_ops.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip/arm-gic-v3.h>
#include <trace/hooks/cpuidle_psci.h>

static void __iomem *base;
static int msm_show_resume_irq_mask;
module_param_named(debug_mask, msm_show_resume_irq_mask, int, 0664);

static void msm_show_resume_irqs(void)
{
	unsigned int i;
	u32 enabled;
	u32 pending[32];
	u32 gic_line_nr;
	u32 typer;

	if (!msm_show_resume_irq_mask)
		return;

	typer = readl_relaxed(base + GICD_TYPER);
	gic_line_nr = min(GICD_TYPER_SPIS(typer), 1023u);

	for (i = 0; i * 32 < gic_line_nr; i++) {
		enabled = readl_relaxed(base + GICD_ICENABLER + i * 4);
		pending[i] = readl_relaxed(base + GICD_ISPENDR + i * 4);
		pending[i] &= enabled;
	}

	for (i = find_first_bit((unsigned long *)pending, gic_line_nr);
	     i < gic_line_nr;
	     i = find_next_bit((unsigned long *)pending, gic_line_nr, i + 1)) {

		if (i < 32)
			continue;

		pr_warn("%s: HWIRQ %u\n", __func__, i);
	}
}

static atomic_t cpus_in_s2idle;

static void gic_s2idle_enter(void *unused, struct cpuidle_device *dev, bool s2idle)
{
	if (!s2idle)
		return;

	atomic_inc(&cpus_in_s2idle);
}

static void gic_s2idle_exit(void *unused, struct cpuidle_device *dev, bool s2idle)
{
	if (!s2idle)
		return;

	if (atomic_read(&cpus_in_s2idle) == num_online_cpus())
		msm_show_resume_irqs();

	atomic_dec(&cpus_in_s2idle);
}

static struct syscore_ops gic_syscore_ops = {
	.resume = msm_show_resume_irqs,
};

static int msm_show_resume_probe(struct platform_device *pdev)
{
	base = of_iomap(pdev->dev.of_node, 0);
	if (IS_ERR(base)) {
		pr_err("%pOF: error %d: unable to map GICD registers\n",
				pdev->dev.of_node, PTR_ERR(base));
		return -ENXIO;
	}

	register_trace_prio_android_vh_cpuidle_psci_enter(gic_s2idle_enter, NULL, INT_MAX);
	register_trace_prio_android_vh_cpuidle_psci_exit(gic_s2idle_exit, NULL, INT_MAX);
	register_syscore_ops(&gic_syscore_ops);
	return 0;
}

static int msm_show_resume_remove(struct platform_device *pdev)
{
	unregister_trace_android_vh_cpuidle_psci_enter(gic_s2idle_enter, NULL);
	unregister_trace_android_vh_cpuidle_psci_exit(gic_s2idle_exit, NULL);
	unregister_syscore_ops(&gic_syscore_ops);
	iounmap(base);
	return 0;
}

static const struct of_device_id msm_show_resume_match_table[] = {
	{ .compatible = "qcom,show-resume-irqs" },
	{ }
};
MODULE_DEVICE_TABLE(of, msm_show_resume_match_table);

static struct platform_driver msm_show_resume_dev_driver = {
	.probe  = msm_show_resume_probe,
	.remove = msm_show_resume_remove,
	.driver = {
		.name = "show-resume-irqs",
		.of_match_table = msm_show_resume_match_table,
	},
};
module_platform_driver(msm_show_resume_dev_driver);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. MSM Show resume IRQ");
MODULE_LICENSE("GPL");
