/*
 * Power Management Service Unit(PMSU) support for Armada 370/XP platforms.
 *
 * Copyright (C) 2012 Marvell
 *
 * Yehuda Yitschak <yehuday@marvell.com>
 * Gregory Clement <gregory.clement@free-electrons.com>
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 * The Armada 370 and Armada XP SOCs have a power management service
 * unit which is responsible for powering down and waking up CPUs and
 * other SOC units
 */

#define pr_fmt(fmt) "mvebu-pmsu: " fmt

#include <linux/clk.h>
#include <linux/cpu_pm.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/smp.h>
#include <linux/resource.h>
#include <linux/slab.h>
#include <asm/cacheflush.h>
#include <asm/cp15.h>
#include <asm/smp_plat.h>
#include <asm/suspend.h>
#include <asm/tlbflush.h>
#include "common.h"
#include "armada-370-xp.h"

static void __iomem *pmsu_mp_base;

#define PMSU_BASE_OFFSET    0x100
#define PMSU_REG_SIZE	    0x1000

/* PMSU MP registers */
#define PMSU_CONTROL_AND_CONFIG(cpu)	    ((cpu * 0x100) + 0x104)
#define PMSU_CONTROL_AND_CONFIG_DFS_REQ		BIT(18)
#define PMSU_CONTROL_AND_CONFIG_PWDDN_REQ	BIT(16)
#define PMSU_CONTROL_AND_CONFIG_L2_PWDDN	BIT(20)

#define PMSU_CPU_POWER_DOWN_CONTROL(cpu)    ((cpu * 0x100) + 0x108)

#define PMSU_CPU_POWER_DOWN_DIS_SNP_Q_SKIP	BIT(0)

#define PMSU_STATUS_AND_MASK(cpu)	    ((cpu * 0x100) + 0x10c)
#define PMSU_STATUS_AND_MASK_CPU_IDLE_WAIT	BIT(16)
#define PMSU_STATUS_AND_MASK_SNP_Q_EMPTY_WAIT	BIT(17)
#define PMSU_STATUS_AND_MASK_IRQ_WAKEUP		BIT(20)
#define PMSU_STATUS_AND_MASK_FIQ_WAKEUP		BIT(21)
#define PMSU_STATUS_AND_MASK_DBG_WAKEUP		BIT(22)
#define PMSU_STATUS_AND_MASK_IRQ_MASK		BIT(24)
#define PMSU_STATUS_AND_MASK_FIQ_MASK		BIT(25)

#define PMSU_EVENT_STATUS_AND_MASK(cpu)     ((cpu * 0x100) + 0x120)
#define PMSU_EVENT_STATUS_AND_MASK_DFS_DONE        BIT(1)
#define PMSU_EVENT_STATUS_AND_MASK_DFS_DONE_MASK   BIT(17)

#define PMSU_BOOT_ADDR_REDIRECT_OFFSET(cpu) ((cpu * 0x100) + 0x124)

/* PMSU fabric registers */
#define L2C_NFABRIC_PM_CTL		    0x4
#define L2C_NFABRIC_PM_CTL_PWR_DOWN		BIT(20)

extern void ll_disable_coherency(void);
extern void ll_enable_coherency(void);

static struct platform_device armada_xp_cpuidle_device = {
	.name = "cpuidle-armada-370-xp",
};

static struct of_device_id of_pmsu_table[] = {
	{ .compatible = "marvell,armada-370-pmsu", },
	{ .compatible = "marvell,armada-370-xp-pmsu", },
	{ .compatible = "marvell,armada-380-pmsu", },
	{ /* end of list */ },
};

void mvebu_pmsu_set_cpu_boot_addr(int hw_cpu, void *boot_addr)
{
	writel(virt_to_phys(boot_addr), pmsu_mp_base +
		PMSU_BOOT_ADDR_REDIRECT_OFFSET(hw_cpu));
}

static int __init armada_370_xp_pmsu_init(void)
{
	struct device_node *np;
	struct resource res;
	int ret = 0;

	np = of_find_matching_node(NULL, of_pmsu_table);
	if (!np)
		return 0;

	pr_info("Initializing Power Management Service Unit\n");

	if (of_address_to_resource(np, 0, &res)) {
		pr_err("unable to get resource\n");
		ret = -ENOENT;
		goto out;
	}

	if (of_device_is_compatible(np, "marvell,armada-370-xp-pmsu")) {
		pr_warn(FW_WARN "deprecated pmsu binding\n");
		res.start = res.start - PMSU_BASE_OFFSET;
		res.end = res.start + PMSU_REG_SIZE - 1;
	}

	if (!request_mem_region(res.start, resource_size(&res),
				np->full_name)) {
		pr_err("unable to request region\n");
		ret = -EBUSY;
		goto out;
	}

	pmsu_mp_base = ioremap(res.start, resource_size(&res));
	if (!pmsu_mp_base) {
		pr_err("unable to map registers\n");
		release_mem_region(res.start, resource_size(&res));
		ret = -ENOMEM;
		goto out;
	}

 out:
	of_node_put(np);
	return ret;
}

static void armada_370_xp_pmsu_enable_l2_powerdown_onidle(void)
{
	u32 reg;

	if (pmsu_mp_base == NULL)
		return;

	/* Enable L2 & Fabric powerdown in Deep-Idle mode - Fabric */
	reg = readl(pmsu_mp_base + L2C_NFABRIC_PM_CTL);
	reg |= L2C_NFABRIC_PM_CTL_PWR_DOWN;
	writel(reg, pmsu_mp_base + L2C_NFABRIC_PM_CTL);
}

static void armada_370_xp_cpu_resume(void)
{
	asm volatile("bl    ll_add_cpu_to_smp_group\n\t"
		     "bl    ll_enable_coherency\n\t"
		     "b	    cpu_resume\n\t");
}

/* No locking is needed because we only access per-CPU registers */
void armada_370_xp_pmsu_idle_prepare(bool deepidle)
{
	unsigned int hw_cpu = cpu_logical_map(smp_processor_id());
	u32 reg;

	if (pmsu_mp_base == NULL)
		return;

	/*
	 * Adjust the PMSU configuration to wait for WFI signal, enable
	 * IRQ and FIQ as wakeup events, set wait for snoop queue empty
	 * indication and mask IRQ and FIQ from CPU
	 */
	reg = readl(pmsu_mp_base + PMSU_STATUS_AND_MASK(hw_cpu));
	reg |= PMSU_STATUS_AND_MASK_CPU_IDLE_WAIT    |
	       PMSU_STATUS_AND_MASK_IRQ_WAKEUP       |
	       PMSU_STATUS_AND_MASK_FIQ_WAKEUP       |
	       PMSU_STATUS_AND_MASK_SNP_Q_EMPTY_WAIT |
	       PMSU_STATUS_AND_MASK_IRQ_MASK         |
	       PMSU_STATUS_AND_MASK_FIQ_MASK;
	writel(reg, pmsu_mp_base + PMSU_STATUS_AND_MASK(hw_cpu));

	reg = readl(pmsu_mp_base + PMSU_CONTROL_AND_CONFIG(hw_cpu));
	/* ask HW to power down the L2 Cache if needed */
	if (deepidle)
		reg |= PMSU_CONTROL_AND_CONFIG_L2_PWDDN;

	/* request power down */
	reg |= PMSU_CONTROL_AND_CONFIG_PWDDN_REQ;
	writel(reg, pmsu_mp_base + PMSU_CONTROL_AND_CONFIG(hw_cpu));

	/* Disable snoop disable by HW - SW is taking care of it */
	reg = readl(pmsu_mp_base + PMSU_CPU_POWER_DOWN_CONTROL(hw_cpu));
	reg |= PMSU_CPU_POWER_DOWN_DIS_SNP_Q_SKIP;
	writel(reg, pmsu_mp_base + PMSU_CPU_POWER_DOWN_CONTROL(hw_cpu));
}

static noinline int do_armada_370_xp_cpu_suspend(unsigned long deepidle)
{
	armada_370_xp_pmsu_idle_prepare(deepidle);

	v7_exit_coherency_flush(all);

	ll_disable_coherency();

	dsb();

	wfi();

	/* If we are here, wfi failed. As processors run out of
	 * coherency for some time, tlbs might be stale, so flush them
	 */
	local_flush_tlb_all();

	ll_enable_coherency();

	/* Test the CR_C bit and set it if it was cleared */
	asm volatile(
	"mrc	p15, 0, %0, c1, c0, 0 \n\t"
	"tst	%0, #(1 << 2) \n\t"
	"orreq	%0, %0, #(1 << 2) \n\t"
	"mcreq	p15, 0, %0, c1, c0, 0 \n\t"
	"isb	"
	: : "r" (0));

	pr_warn("Failed to suspend the system\n");

	return 0;
}

static int armada_370_xp_cpu_suspend(unsigned long deepidle)
{
	return cpu_suspend(deepidle, do_armada_370_xp_cpu_suspend);
}

/* No locking is needed because we only access per-CPU registers */
static noinline void armada_370_xp_pmsu_idle_restore(void)
{
	unsigned int hw_cpu = cpu_logical_map(smp_processor_id());
	u32 reg;

	if (pmsu_mp_base == NULL)
		return;

	/* cancel ask HW to power down the L2 Cache if possible */
	reg = readl(pmsu_mp_base + PMSU_CONTROL_AND_CONFIG(hw_cpu));
	reg &= ~PMSU_CONTROL_AND_CONFIG_L2_PWDDN;
	writel(reg, pmsu_mp_base + PMSU_CONTROL_AND_CONFIG(hw_cpu));

	/* cancel Enable wakeup events and mask interrupts */
	reg = readl(pmsu_mp_base + PMSU_STATUS_AND_MASK(hw_cpu));
	reg &= ~(PMSU_STATUS_AND_MASK_IRQ_WAKEUP | PMSU_STATUS_AND_MASK_FIQ_WAKEUP);
	reg &= ~PMSU_STATUS_AND_MASK_CPU_IDLE_WAIT;
	reg &= ~PMSU_STATUS_AND_MASK_SNP_Q_EMPTY_WAIT;
	reg &= ~(PMSU_STATUS_AND_MASK_IRQ_MASK | PMSU_STATUS_AND_MASK_FIQ_MASK);
	writel(reg, pmsu_mp_base + PMSU_STATUS_AND_MASK(hw_cpu));
}

static int armada_370_xp_cpu_pm_notify(struct notifier_block *self,
				    unsigned long action, void *hcpu)
{
	if (action == CPU_PM_ENTER) {
		unsigned int hw_cpu = cpu_logical_map(smp_processor_id());
		mvebu_pmsu_set_cpu_boot_addr(hw_cpu, armada_370_xp_cpu_resume);
	} else if (action == CPU_PM_EXIT) {
		armada_370_xp_pmsu_idle_restore();
	}

	return NOTIFY_OK;
}

static struct notifier_block armada_370_xp_cpu_pm_notifier = {
	.notifier_call = armada_370_xp_cpu_pm_notify,
};

int __init armada_370_xp_cpu_pm_init(void)
{
	struct device_node *np;

	/*
	 * Check that all the requirements are available to enable
	 * cpuidle. So far, it is only supported on Armada XP, cpuidle
	 * needs the coherency fabric and the PMSU enabled
	 */

	if (!of_machine_is_compatible("marvell,armadaxp"))
		return 0;

	np = of_find_compatible_node(NULL, NULL, "marvell,coherency-fabric");
	if (!np)
		return 0;
	of_node_put(np);

	np = of_find_matching_node(NULL, of_pmsu_table);
	if (!np)
		return 0;
	of_node_put(np);

	armada_370_xp_pmsu_enable_l2_powerdown_onidle();
	armada_xp_cpuidle_device.dev.platform_data = armada_370_xp_cpu_suspend;
	platform_device_register(&armada_xp_cpuidle_device);
	cpu_pm_register_notifier(&armada_370_xp_cpu_pm_notifier);

	return 0;
}

arch_initcall(armada_370_xp_cpu_pm_init);
early_initcall(armada_370_xp_pmsu_init);

static void mvebu_pmsu_dfs_request_local(void *data)
{
	u32 reg;
	u32 cpu = smp_processor_id();
	unsigned long flags;

	local_irq_save(flags);

	/* Prepare to enter idle */
	reg = readl(pmsu_mp_base + PMSU_STATUS_AND_MASK(cpu));
	reg |= PMSU_STATUS_AND_MASK_CPU_IDLE_WAIT |
	       PMSU_STATUS_AND_MASK_IRQ_MASK     |
	       PMSU_STATUS_AND_MASK_FIQ_MASK;
	writel(reg, pmsu_mp_base + PMSU_STATUS_AND_MASK(cpu));

	/* Request the DFS transition */
	reg = readl(pmsu_mp_base + PMSU_CONTROL_AND_CONFIG(cpu));
	reg |= PMSU_CONTROL_AND_CONFIG_DFS_REQ;
	writel(reg, pmsu_mp_base + PMSU_CONTROL_AND_CONFIG(cpu));

	/* The fact of entering idle will trigger the DFS transition */
	wfi();

	/*
	 * We're back from idle, the DFS transition has completed,
	 * clear the idle wait indication.
	 */
	reg = readl(pmsu_mp_base + PMSU_STATUS_AND_MASK(cpu));
	reg &= ~PMSU_STATUS_AND_MASK_CPU_IDLE_WAIT;
	writel(reg, pmsu_mp_base + PMSU_STATUS_AND_MASK(cpu));

	local_irq_restore(flags);
}

int mvebu_pmsu_dfs_request(int cpu)
{
	unsigned long timeout;
	int hwcpu = cpu_logical_map(cpu);
	u32 reg;

	/* Clear any previous DFS DONE event */
	reg = readl(pmsu_mp_base + PMSU_EVENT_STATUS_AND_MASK(hwcpu));
	reg &= ~PMSU_EVENT_STATUS_AND_MASK_DFS_DONE;
	writel(reg, pmsu_mp_base + PMSU_EVENT_STATUS_AND_MASK(hwcpu));

	/* Mask the DFS done interrupt, since we are going to poll */
	reg = readl(pmsu_mp_base + PMSU_EVENT_STATUS_AND_MASK(hwcpu));
	reg |= PMSU_EVENT_STATUS_AND_MASK_DFS_DONE_MASK;
	writel(reg, pmsu_mp_base + PMSU_EVENT_STATUS_AND_MASK(hwcpu));

	/* Trigger the DFS on the appropriate CPU */
	smp_call_function_single(cpu, mvebu_pmsu_dfs_request_local,
				 NULL, false);

	/* Poll until the DFS done event is generated */
	timeout = jiffies + HZ;
	while (time_before(jiffies, timeout)) {
		reg = readl(pmsu_mp_base + PMSU_EVENT_STATUS_AND_MASK(hwcpu));
		if (reg & PMSU_EVENT_STATUS_AND_MASK_DFS_DONE)
			break;
		udelay(10);
	}

	if (time_after(jiffies, timeout))
		return -ETIME;

	/* Restore the DFS mask to its original state */
	reg = readl(pmsu_mp_base + PMSU_EVENT_STATUS_AND_MASK(hwcpu));
	reg &= ~PMSU_EVENT_STATUS_AND_MASK_DFS_DONE_MASK;
	writel(reg, pmsu_mp_base + PMSU_EVENT_STATUS_AND_MASK(hwcpu));

	return 0;
}

static int __init armada_xp_pmsu_cpufreq_init(void)
{
	struct device_node *np;
	struct resource res;
	int ret, cpu;

	if (!of_machine_is_compatible("marvell,armadaxp"))
		return 0;

	/*
	 * In order to have proper cpufreq handling, we need to ensure
	 * that the Device Tree description of the CPU clock includes
	 * the definition of the PMU DFS registers. If not, we do not
	 * register the clock notifier and the cpufreq driver. This
	 * piece of code is only for compatibility with old Device
	 * Trees.
	 */
	np = of_find_compatible_node(NULL, NULL, "marvell,armada-xp-cpu-clock");
	if (!np)
		return 0;

	ret = of_address_to_resource(np, 1, &res);
	if (ret) {
		pr_warn(FW_WARN "not enabling cpufreq, deprecated armada-xp-cpu-clock binding\n");
		of_node_put(np);
		return 0;
	}

	of_node_put(np);

	/*
	 * For each CPU, this loop registers the operating points
	 * supported (which are the nominal CPU frequency and half of
	 * it), and registers the clock notifier that will take care
	 * of doing the PMSU part of a frequency transition.
	 */
	for_each_possible_cpu(cpu) {
		struct device *cpu_dev;
		struct clk *clk;
		int ret;

		cpu_dev = get_cpu_device(cpu);
		if (!cpu_dev) {
			pr_err("Cannot get CPU %d\n", cpu);
			continue;
		}

		clk = clk_get(cpu_dev, 0);
		if (!clk) {
			pr_err("Cannot get clock for CPU %d\n", cpu);
			return -ENODEV;
		}

		/*
		 * In case of a failure of dev_pm_opp_add(), we don't
		 * bother with cleaning up the registered OPP (there's
		 * no function to do so), and simply cancel the
		 * registration of the cpufreq device.
		 */
		ret = dev_pm_opp_add(cpu_dev, clk_get_rate(clk), 0);
		if (ret) {
			clk_put(clk);
			return ret;
		}

		ret = dev_pm_opp_add(cpu_dev, clk_get_rate(clk) / 2, 0);
		if (ret) {
			clk_put(clk);
			return ret;
		}
	}

	platform_device_register_simple("cpufreq-generic", -1, NULL, 0);
	return 0;
}

device_initcall(armada_xp_pmsu_cpufreq_init);
