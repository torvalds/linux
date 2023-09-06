// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2015 Carlo Caione <carlo@endlessm.com>
 * Copyright (C) 2017 Martin Blumenstingl <martin.blumenstingl@googlemail.com>
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/smp.h>
#include <linux/mfd/syscon.h>

#include <asm/cacheflush.h>
#include <asm/cp15.h>
#include <asm/smp_scu.h>
#include <asm/smp_plat.h>

#define MESON_SMP_SRAM_CPU_CTRL_REG		(0x00)
#define MESON_SMP_SRAM_CPU_CTRL_ADDR_REG(c)	(0x04 + ((c - 1) << 2))

#define MESON_CPU_AO_RTI_PWR_A9_CNTL0		(0x00)
#define MESON_CPU_AO_RTI_PWR_A9_CNTL1		(0x04)
#define MESON_CPU_AO_RTI_PWR_A9_MEM_PD0		(0x14)

#define MESON_CPU_PWR_A9_CNTL0_M(c)		(0x03 << ((c * 2) + 16))
#define MESON_CPU_PWR_A9_CNTL1_M(c)		(0x03 << ((c + 1) << 1))
#define MESON_CPU_PWR_A9_MEM_PD0_M(c)		(0x0f << (32 - (c * 4)))
#define MESON_CPU_PWR_A9_CNTL1_ST(c)		(0x01 << (c + 16))

static void __iomem *sram_base;
static void __iomem *scu_base;
static struct regmap *pmu;

static struct reset_control *meson_smp_get_core_reset(int cpu)
{
	struct device_node *np = of_get_cpu_node(cpu, 0);

	return of_reset_control_get_exclusive(np, NULL);
}

static void meson_smp_set_cpu_ctrl(int cpu, bool on_off)
{
	u32 val = readl(sram_base + MESON_SMP_SRAM_CPU_CTRL_REG);

	if (on_off)
		val |= BIT(cpu);
	else
		val &= ~BIT(cpu);

	/* keep bit 0 always enabled */
	val |= BIT(0);

	writel(val, sram_base + MESON_SMP_SRAM_CPU_CTRL_REG);
}

static void __init meson_smp_prepare_cpus(const char *scu_compatible,
					  const char *pmu_compatible,
					  const char *sram_compatible)
{
	static struct device_node *node;

	/* SMP SRAM */
	node = of_find_compatible_node(NULL, NULL, sram_compatible);
	if (!node) {
		pr_err("Missing SRAM node\n");
		return;
	}

	sram_base = of_iomap(node, 0);
	of_node_put(node);
	if (!sram_base) {
		pr_err("Couldn't map SRAM registers\n");
		return;
	}

	/* PMU */
	pmu = syscon_regmap_lookup_by_compatible(pmu_compatible);
	if (IS_ERR(pmu)) {
		pr_err("Couldn't map PMU registers\n");
		return;
	}

	/* SCU */
	node = of_find_compatible_node(NULL, NULL, scu_compatible);
	if (!node) {
		pr_err("Missing SCU node\n");
		return;
	}

	scu_base = of_iomap(node, 0);
	of_node_put(node);
	if (!scu_base) {
		pr_err("Couldn't map SCU registers\n");
		return;
	}

	scu_enable(scu_base);
}

static void __init meson8b_smp_prepare_cpus(unsigned int max_cpus)
{
	meson_smp_prepare_cpus("arm,cortex-a5-scu", "amlogic,meson8b-pmu",
			       "amlogic,meson8b-smp-sram");
}

static void __init meson8_smp_prepare_cpus(unsigned int max_cpus)
{
	meson_smp_prepare_cpus("arm,cortex-a9-scu", "amlogic,meson8-pmu",
			       "amlogic,meson8-smp-sram");
}

static void meson_smp_begin_secondary_boot(unsigned int cpu)
{
	/*
	 * Set the entry point before powering on the CPU through the SCU. This
	 * is needed if the CPU is in "warm" state (= after rebooting the
	 * system without power-cycling, or when taking the CPU offline and
	 * then taking it online again.
	 */
	writel(__pa_symbol(secondary_startup),
	       sram_base + MESON_SMP_SRAM_CPU_CTRL_ADDR_REG(cpu));

	/*
	 * SCU Power on CPU (needs to be done before starting the CPU,
	 * otherwise the secondary CPU will not start).
	 */
	scu_cpu_power_enable(scu_base, cpu);
}

static int meson_smp_finalize_secondary_boot(unsigned int cpu)
{
	unsigned long timeout;

	timeout = jiffies + (10 * HZ);
	while (readl(sram_base + MESON_SMP_SRAM_CPU_CTRL_ADDR_REG(cpu))) {
		if (!time_before(jiffies, timeout)) {
			pr_err("Timeout while waiting for CPU%d status\n",
			       cpu);
			return -ETIMEDOUT;
		}
	}

	writel(__pa_symbol(secondary_startup),
	       sram_base + MESON_SMP_SRAM_CPU_CTRL_ADDR_REG(cpu));

	meson_smp_set_cpu_ctrl(cpu, true);

	return 0;
}

static int meson8_smp_boot_secondary(unsigned int cpu,
				     struct task_struct *idle)
{
	struct reset_control *rstc;
	int ret;

	rstc = meson_smp_get_core_reset(cpu);
	if (IS_ERR(rstc)) {
		pr_err("Couldn't get the reset controller for CPU%d\n", cpu);
		return PTR_ERR(rstc);
	}

	meson_smp_begin_secondary_boot(cpu);

	/* Reset enable */
	ret = reset_control_assert(rstc);
	if (ret) {
		pr_err("Failed to assert CPU%d reset\n", cpu);
		goto out;
	}

	/* CPU power ON */
	ret = regmap_update_bits(pmu, MESON_CPU_AO_RTI_PWR_A9_CNTL1,
				 MESON_CPU_PWR_A9_CNTL1_M(cpu), 0);
	if (ret < 0) {
		pr_err("Couldn't wake up CPU%d\n", cpu);
		goto out;
	}

	udelay(10);

	/* Isolation disable */
	ret = regmap_update_bits(pmu, MESON_CPU_AO_RTI_PWR_A9_CNTL0, BIT(cpu),
				 0);
	if (ret < 0) {
		pr_err("Error when disabling isolation of CPU%d\n", cpu);
		goto out;
	}

	/* Reset disable */
	ret = reset_control_deassert(rstc);
	if (ret) {
		pr_err("Failed to de-assert CPU%d reset\n", cpu);
		goto out;
	}

	ret = meson_smp_finalize_secondary_boot(cpu);
	if (ret)
		goto out;

out:
	reset_control_put(rstc);

	return 0;
}

static int meson8b_smp_boot_secondary(unsigned int cpu,
				     struct task_struct *idle)
{
	struct reset_control *rstc;
	int ret;
	u32 val;

	rstc = meson_smp_get_core_reset(cpu);
	if (IS_ERR(rstc)) {
		pr_err("Couldn't get the reset controller for CPU%d\n", cpu);
		return PTR_ERR(rstc);
	}

	meson_smp_begin_secondary_boot(cpu);

	/* CPU power UP */
	ret = regmap_update_bits(pmu, MESON_CPU_AO_RTI_PWR_A9_CNTL0,
				 MESON_CPU_PWR_A9_CNTL0_M(cpu), 0);
	if (ret < 0) {
		pr_err("Couldn't power up CPU%d\n", cpu);
		goto out;
	}

	udelay(5);

	/* Reset enable */
	ret = reset_control_assert(rstc);
	if (ret) {
		pr_err("Failed to assert CPU%d reset\n", cpu);
		goto out;
	}

	/* Memory power UP */
	ret = regmap_update_bits(pmu, MESON_CPU_AO_RTI_PWR_A9_MEM_PD0,
				 MESON_CPU_PWR_A9_MEM_PD0_M(cpu), 0);
	if (ret < 0) {
		pr_err("Couldn't power up the memory for CPU%d\n", cpu);
		goto out;
	}

	/* Wake up CPU */
	ret = regmap_update_bits(pmu, MESON_CPU_AO_RTI_PWR_A9_CNTL1,
				 MESON_CPU_PWR_A9_CNTL1_M(cpu), 0);
	if (ret < 0) {
		pr_err("Couldn't wake up CPU%d\n", cpu);
		goto out;
	}

	udelay(10);

	ret = regmap_read_poll_timeout(pmu, MESON_CPU_AO_RTI_PWR_A9_CNTL1, val,
				       val & MESON_CPU_PWR_A9_CNTL1_ST(cpu),
				       10, 10000);
	if (ret) {
		pr_err("Timeout while polling PMU for CPU%d status\n", cpu);
		goto out;
	}

	/* Isolation disable */
	ret = regmap_update_bits(pmu, MESON_CPU_AO_RTI_PWR_A9_CNTL0, BIT(cpu),
				 0);
	if (ret < 0) {
		pr_err("Error when disabling isolation of CPU%d\n", cpu);
		goto out;
	}

	/* Reset disable */
	ret = reset_control_deassert(rstc);
	if (ret) {
		pr_err("Failed to de-assert CPU%d reset\n", cpu);
		goto out;
	}

	ret = meson_smp_finalize_secondary_boot(cpu);
	if (ret)
		goto out;

out:
	reset_control_put(rstc);

	return 0;
}

#ifdef CONFIG_HOTPLUG_CPU
static void meson8_smp_cpu_die(unsigned int cpu)
{
	meson_smp_set_cpu_ctrl(cpu, false);

	v7_exit_coherency_flush(louis);

	scu_power_mode(scu_base, SCU_PM_POWEROFF);

	dsb();
	wfi();

	/* we should never get here */
	WARN_ON(1);
}

static int meson8_smp_cpu_kill(unsigned int cpu)
{
	int ret, power_mode;
	unsigned long timeout;

	timeout = jiffies + (50 * HZ);
	do {
		power_mode = scu_get_cpu_power_mode(scu_base, cpu);

		if (power_mode == SCU_PM_POWEROFF)
			break;

		usleep_range(10000, 15000);
	} while (time_before(jiffies, timeout));

	if (power_mode != SCU_PM_POWEROFF) {
		pr_err("Error while waiting for SCU power-off on CPU%d\n",
		       cpu);
		return -ETIMEDOUT;
	}

	msleep(30);

	/* Isolation enable */
	ret = regmap_update_bits(pmu, MESON_CPU_AO_RTI_PWR_A9_CNTL0, BIT(cpu),
				 0x3);
	if (ret < 0) {
		pr_err("Error when enabling isolation for CPU%d\n", cpu);
		return ret;
	}

	udelay(10);

	/* CPU power OFF */
	ret = regmap_update_bits(pmu, MESON_CPU_AO_RTI_PWR_A9_CNTL1,
				 MESON_CPU_PWR_A9_CNTL1_M(cpu), 0x3);
	if (ret < 0) {
		pr_err("Couldn't change sleep status of CPU%d\n", cpu);
		return ret;
	}

	return 1;
}

static int meson8b_smp_cpu_kill(unsigned int cpu)
{
	int ret, power_mode, count = 5000;

	do {
		power_mode = scu_get_cpu_power_mode(scu_base, cpu);

		if (power_mode == SCU_PM_POWEROFF)
			break;

		udelay(10);
	} while (++count);

	if (power_mode != SCU_PM_POWEROFF) {
		pr_err("Error while waiting for SCU power-off on CPU%d\n",
		       cpu);
		return -ETIMEDOUT;
	}

	udelay(10);

	/* CPU power DOWN */
	ret = regmap_update_bits(pmu, MESON_CPU_AO_RTI_PWR_A9_CNTL0,
				 MESON_CPU_PWR_A9_CNTL0_M(cpu), 0x3);
	if (ret < 0) {
		pr_err("Couldn't power down CPU%d\n", cpu);
		return ret;
	}

	/* Isolation enable */
	ret = regmap_update_bits(pmu, MESON_CPU_AO_RTI_PWR_A9_CNTL0, BIT(cpu),
				 0x3);
	if (ret < 0) {
		pr_err("Error when enabling isolation for CPU%d\n", cpu);
		return ret;
	}

	udelay(10);

	/* Sleep status */
	ret = regmap_update_bits(pmu, MESON_CPU_AO_RTI_PWR_A9_CNTL1,
				 MESON_CPU_PWR_A9_CNTL1_M(cpu), 0x3);
	if (ret < 0) {
		pr_err("Couldn't change sleep status of CPU%d\n", cpu);
		return ret;
	}

	/* Memory power DOWN */
	ret = regmap_update_bits(pmu, MESON_CPU_AO_RTI_PWR_A9_MEM_PD0,
				 MESON_CPU_PWR_A9_MEM_PD0_M(cpu), 0xf);
	if (ret < 0) {
		pr_err("Couldn't power down the memory of CPU%d\n", cpu);
		return ret;
	}

	return 1;
}
#endif

static struct smp_operations meson8_smp_ops __initdata = {
	.smp_prepare_cpus	= meson8_smp_prepare_cpus,
	.smp_boot_secondary	= meson8_smp_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= meson8_smp_cpu_die,
	.cpu_kill		= meson8_smp_cpu_kill,
#endif
};

static struct smp_operations meson8b_smp_ops __initdata = {
	.smp_prepare_cpus	= meson8b_smp_prepare_cpus,
	.smp_boot_secondary	= meson8b_smp_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= meson8_smp_cpu_die,
	.cpu_kill		= meson8b_smp_cpu_kill,
#endif
};

CPU_METHOD_OF_DECLARE(meson8_smp, "amlogic,meson8-smp", &meson8_smp_ops);
CPU_METHOD_OF_DECLARE(meson8b_smp, "amlogic,meson8b-smp", &meson8b_smp_ops);
