// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2011, 2014-2016, 2018, 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024, Qualcomm Innovation Center, Inc. All rights reserved.
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
#include <trace/hooks/gic_v3.h>
#include <trace/hooks/cpuidle_psci.h>
#include <linux/notifier.h>
#include <linux/suspend.h>

#define MPIDR_RS(mpidr) (((mpidr) & 0xF0UL) >> 4)
#define GIC_LINE_NR min(GICD_TYPER_SPIS(gic_data->rdists.gicd_typer), 1020U)
#define gic_data_rdist() (this_cpu_ptr(gic_data->rdists.rdist))
#define gic_data_rdist_rd_base() (gic_data_rdist()->rd_base)
#define gic_data_rdist_sgi_base() (gic_data_rdist_rd_base() + SZ_64K)

static void __iomem *base;
static int msm_show_resume_irq_mask;
module_param_named(debug_mask, msm_show_resume_irq_mask, int, 0664);

static bool hibernation;
struct gic_chip_data_v3 *gic_data_glb;

struct gic_chip_data_ds {
	unsigned int enabled_irqs[32];
	unsigned int active_irqs[32];
	unsigned int irq_edg_lvl[64];
	unsigned int ppi_edg_lvl;
	unsigned int enabled_sgis;
	unsigned int pending_sgis;
};

static struct gic_chip_data_ds gic_data_ds __read_mostly;

static void gic_suspend_ds(void *data, struct gic_chip_data_v3 *gic_data)
{
	int i;
	void __iomem *rdist_base = gic_data_rdist_sgi_base();

	gic_data_glb = gic_data;

	if (unlikely(!hibernation) && (pm_suspend_target_state != PM_SUSPEND_MEM))
		return;
	gic_data_ds.enabled_sgis = readl_relaxed(rdist_base + GICD_ISENABLER);
	gic_data_ds.pending_sgis = readl_relaxed(rdist_base + GICD_ISPENDR);
	/* Store edge level for PPIs by reading GICR_ICFGR1 */
	gic_data_ds.ppi_edg_lvl = readl_relaxed(rdist_base + GICR_ICFGR0 + 4);

	for (i = 0; i * 32 < GIC_LINE_NR; i++) {
		gic_data_ds.enabled_irqs[i] = readl_relaxed(base + GICD_ISENABLER + i * 4);
		gic_data_ds.active_irqs[i] = readl_relaxed(base + GICD_ISPENDR + i * 4);
	}

	for (i = 2; i < GIC_LINE_NR / 16; i++)
		gic_data_ds.irq_edg_lvl[i] = readl_relaxed(base + GICD_ICFGR + i * 4);
}

static void gic_resume_ds(struct gic_chip_data_v3 *gic_data)
{
	int i;
	void __iomem *rdist_base = gic_data_rdist_sgi_base();

	pr_info("Re-initializing gic in hibernation restore\n");
	gic_v3_dist_init();
	gic_v3_cpu_init();
	writel_relaxed(gic_data_ds.enabled_sgis, rdist_base + GICD_ISENABLER);
	writel_relaxed(gic_data_ds.pending_sgis, rdist_base + GICD_ISPENDR);
	/* Restore edge and level triggers for PPIs from GICR_ICFGR1 */
	writel_relaxed(gic_data_ds.ppi_edg_lvl, rdist_base + GICR_ICFGR0 + 4);
	/* Restore edge and level triggers */
	for (i = 2; i < GIC_LINE_NR / 16; i++)
		writel_relaxed(gic_data_ds.irq_edg_lvl[i], base + GICD_ICFGR + i * 4);
	gic_v3_dist_wait_for_rwp();
	/* Activate and enable interrupts from backup */
	for (i = 0; i * 32 < GIC_LINE_NR; i++) {
		writel_relaxed(gic_data_ds.active_irqs[i], base + GICD_ISPENDR + i * 4);
		writel_relaxed(gic_data_ds.enabled_irqs[i], base + GICD_ISENABLER + i * 4);
	}
	gic_v3_dist_wait_for_rwp();
}

static void msm_show_resume_irqs(void)
{
	unsigned int i;
	u32 enabled;
	u32 pending[32];
	u32 gic_line_nr;
	u32 typer;

	if (unlikely(hibernation) || (pm_suspend_target_state == PM_SUSPEND_MEM))
		gic_resume_ds(gic_data_glb);

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

static int gic_suspend_notifier(struct notifier_block *nb, unsigned long event, void *dummy)
{
	if (event == PM_HIBERNATION_PREPARE)
		hibernation = true;
	else if (event == PM_POST_HIBERNATION)
		hibernation = false;
	return NOTIFY_OK;
}

static struct notifier_block gic_notif_block = {
	.notifier_call = gic_suspend_notifier,
};

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
	register_pm_notifier(&gic_notif_block);
	register_trace_android_vh_gic_v3_suspend(gic_suspend_ds, NULL);
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
