// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2010-2019, 2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */


/* Copyright (c) 2010-2019, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/io.h>
#include <linux/ktime.h>
#include <linux/smp.h>
#include <linux/tick.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/qcom_scm.h>
#include <linux/uaccess.h>
#include <linux/dma-mapping.h>
#include <soc/qcom/spm.h>
#include <soc/qcom/pm-legacy.h>
#include <asm/suspend.h>
#include <linux/cacheflush.h>
#include <asm/cputype.h>
#include <asm/system_misc.h>
#ifdef CONFIG_VFP
#include <asm/vfp.h>
#endif
#include "pm-boot.h"
#include "idle.h"
#include <linux/cpumask.h>

#define SCM_CMD_TERMINATE_PC	(0x2)
#define SCM_CMD_CORE_HOTPLUGGED (0x10)
#define SCM_FLUSH_FLAG_MASK	(0x3)

#define SCLK_HZ (32768)

#define MAX_BUF_SIZE  1024

static int msm_pm_debug_mask = 1;
module_param_named(
	debug_mask, msm_pm_debug_mask, int, 0664
);

enum {
	MSM_PM_DEBUG_SUSPEND = BIT(0),
	MSM_PM_DEBUG_POWER_COLLAPSE = BIT(1),
	MSM_PM_DEBUG_SUSPEND_LIMITS = BIT(2),
	MSM_PM_DEBUG_CLOCK = BIT(3),
	MSM_PM_DEBUG_RESET_VECTOR = BIT(4),
	MSM_PM_DEBUG_IDLE = BIT(5),
	MSM_PM_DEBUG_IDLE_LIMITS = BIT(6),
	MSM_PM_DEBUG_HOTPLUG = BIT(7),
};

enum msm_pc_count_offsets {
	MSM_PC_ENTRY_COUNTER,
	MSM_PC_EXIT_COUNTER,
	MSM_PC_FALLTHRU_COUNTER,
	MSM_PC_UNUSED,
	MSM_PC_NUM_COUNTERS,
};

static bool msm_pm_ldo_retention_enabled = true;
static bool msm_pm_tz_flushes_cache;
static bool msm_pm_ret_no_pll_switch;
static bool msm_no_ramp_down_pc;
static struct msm_pm_sleep_status_data *msm_pm_slp_sts;
static DEFINE_PER_CPU(struct clk *, cpu_clks);
static struct clk *l2_clk;

static long *msm_pc_debug_counters;

static cpumask_t retention_cpus;
static DEFINE_SPINLOCK(retention_lock);
static DEFINE_MUTEX(msm_pc_debug_mutex);

static bool msm_pm_is_L1_writeback(void)
{
	u32 cache_id = 0;

#if defined(CONFIG_CPU_V7)
	u32 sel = 0;

	asm volatile ("mcr p15, 2, %[ccselr], c0, c0, 0\n\t"
		      "isb\n\t"
		      "mrc p15, 1, %[ccsidr], c0, c0, 0\n\t"
		      : [ccsidr]"=r" (cache_id)
		      : [ccselr]"r" (sel)
		     );
	return cache_id & BIT(30);
#elif defined(CONFIG_ARM64)
	u32 sel = 0;

	asm volatile("msr csselr_el1, %[ccselr]\n\t"
		     "isb\n\t"
		     "mrs %[ccsidr],ccsidr_el1\n\t"
		     : [ccsidr]"=r" (cache_id)
		     : [ccselr]"r" (sel)
		    );
	return cache_id & BIT(30);
#else
#error No valid CPU arch selected
#endif
}

static bool msm_pm_swfi(bool from_idle)
{
	msm_arch_idle();
	return true;
}

static bool msm_pm_retention(bool from_idle)
{
	int ret = 0;
	unsigned int cpu = smp_processor_id();
	struct clk *cpu_clk = per_cpu(cpu_clks, cpu);

	spin_lock(&retention_lock);

	if (!msm_pm_ldo_retention_enabled)
		goto bailout;

	cpumask_set_cpu(cpu, &retention_cpus);
	spin_unlock(&retention_lock);

	if (!msm_pm_ret_no_pll_switch)
		clk_disable(cpu_clk);

	ret = msm_spm_set_low_power_mode(MSM_SPM_MODE_RETENTION, false);
	WARN_ON(ret);

	msm_arch_idle();

	ret = msm_spm_set_low_power_mode(MSM_SPM_MODE_CLOCK_GATING, false);
	WARN_ON(ret);

	if (!msm_pm_ret_no_pll_switch)
		if (clk_enable(cpu_clk))
			pr_err("%s(): Error restore cpu clk\n", __func__);

	spin_lock(&retention_lock);
	cpumask_clear_cpu(cpu, &retention_cpus);
bailout:
	spin_unlock(&retention_lock);
	return true;
}

static inline void msm_pc_inc_debug_count(uint32_t cpu,
		enum msm_pc_count_offsets offset)
{
	int cntr_offset;
	uint32_t cluster_id = MPIDR_AFFINITY_LEVEL(cpu_logical_map(cpu), 1);
	uint32_t cpu_id = MPIDR_AFFINITY_LEVEL(cpu_logical_map(cpu), 0);

	if (cluster_id >= MAX_NUM_CLUSTER || cpu_id >= MAX_CPUS_PER_CLUSTER)
		WARN_ON(cpu);

	cntr_offset = (cluster_id * MAX_CPUS_PER_CLUSTER * MSM_PC_NUM_COUNTERS)
			 + (cpu_id * MSM_PC_NUM_COUNTERS) + offset;

	if (!msm_pc_debug_counters)
		return;

	msm_pc_debug_counters[cntr_offset]++;
}

static bool msm_pm_pc_hotplug(void)
{
	uint32_t cpu = smp_processor_id();
	enum msm_pm_l2_scm_flag flag;

	flag = lpm_cpu_pre_pc_cb(cpu);

	if (!msm_pm_tz_flushes_cache) {
		if (flag == MSM_SCM_L2_OFF)
			flush_cache_all();
		else if (msm_pm_is_L1_writeback())
			flush_cache_louis();
	}

	msm_pc_inc_debug_count(cpu, MSM_PC_ENTRY_COUNTER);
	qcom_scm_cpu_power_down(SCM_CMD_CORE_HOTPLUGGED |
				 (flag & SCM_FLUSH_FLAG_MASK));

	/* Should not return here */
	msm_pc_inc_debug_count(cpu, MSM_PC_FALLTHRU_COUNTER);

	return false;
}

static bool msm_pm_fastpc(bool from_idle)
{
	int ret = 0;
	unsigned int cpu = smp_processor_id();

	ret = msm_spm_set_low_power_mode(MSM_SPM_MODE_FASTPC, false);
	WARN_ON(ret);

	if (from_idle || cpu_online(cpu))
		msm_arch_idle();
	else
		msm_pm_pc_hotplug();

	ret = msm_spm_set_low_power_mode(MSM_SPM_MODE_CLOCK_GATING, false);
	WARN_ON(ret);

	return true;
}

int msm_pm_collapse(unsigned long unused)
{
	uint32_t cpu = smp_processor_id();
	enum msm_pm_l2_scm_flag flag;

	flag = lpm_cpu_pre_pc_cb(cpu);

	if (!msm_pm_tz_flushes_cache) {
		if (flag == MSM_SCM_L2_OFF)
			flush_cache_all();
		else if (msm_pm_is_L1_writeback())
			flush_cache_louis();
	}
	msm_pc_inc_debug_count(cpu, MSM_PC_ENTRY_COUNTER);
	qcom_scm_cpu_power_down(flag & SCM_FLUSH_FLAG_MASK);
	msm_pc_inc_debug_count(cpu, MSM_PC_FALLTHRU_COUNTER);

	return 0;
}
EXPORT_SYMBOL_GPL(msm_pm_collapse);

static bool __ref msm_pm_spm_power_collapse(
	unsigned int cpu, int mode, bool from_idle, bool notify_rpm)
{
	void *entry;
	bool collapsed = false;
	int ret;
	bool save_cpu_regs = (cpu_online(cpu) || from_idle);

	if (MSM_PM_DEBUG_POWER_COLLAPSE & msm_pm_debug_mask)
		pr_info("CPU%u: %s: notify_rpm %d\n",
			cpu, __func__, (int) notify_rpm);

	ret = msm_spm_set_low_power_mode(mode, notify_rpm);
	WARN_ON(ret);

	entry = save_cpu_regs ?  cpu_resume : msm_secondary_startup;

	msm_pm_boot_config_before_pc(cpu, virt_to_phys(entry));

	if (MSM_PM_DEBUG_RESET_VECTOR & msm_pm_debug_mask)
		pr_info("CPU%u: %s: program vector to %pK\n",
			cpu, __func__, entry);

	collapsed = save_cpu_regs ?
		!cpu_suspend(0, msm_pm_collapse) : msm_pm_pc_hotplug();

	if (collapsed)
		local_fiq_enable();

	msm_pm_boot_config_after_pc(cpu);

	if (MSM_PM_DEBUG_POWER_COLLAPSE & msm_pm_debug_mask)
		pr_info("CPU%u: %s: msm_pm_collapse returned, collapsed %d\n",
			cpu, __func__, collapsed);

	ret = msm_spm_set_low_power_mode(MSM_SPM_MODE_CLOCK_GATING, false);
	WARN_ON(ret);
	return collapsed;
}

static bool msm_pm_power_collapse_standalone(
		bool from_idle)
{
	unsigned int cpu = smp_processor_id();
	bool collapsed;

	collapsed = msm_pm_spm_power_collapse(cpu,
			MSM_SPM_MODE_STANDALONE_POWER_COLLAPSE,
			from_idle, false);

	return collapsed;
}

static int ramp_down_last_cpu(int cpu)
{
	struct clk *cpu_clk = per_cpu(cpu_clks, cpu);

	clk_disable(cpu_clk);
	clk_disable(l2_clk);

	return 0;
}

static int ramp_up_first_cpu(int cpu, int saved_rate)
{
	struct clk *cpu_clk = per_cpu(cpu_clks, cpu);
	int rc = 0;

	if (MSM_PM_DEBUG_CLOCK & msm_pm_debug_mask)
		pr_info("CPU%u: %s: restore clock rate\n",
				cpu, __func__);

	clk_enable(l2_clk);

	if (cpu_clk) {
		int ret = clk_enable(cpu_clk);

		if (ret) {
			pr_err("%s(): Error restoring cpu clk\n",
					__func__);
			return ret;
		}
	}

	return rc;
}

static bool msm_pm_power_collapse(bool from_idle)
{
	unsigned int cpu = smp_processor_id();
	unsigned long saved_acpuclk_rate = 0;
	bool collapsed;

	if (MSM_PM_DEBUG_POWER_COLLAPSE & msm_pm_debug_mask)
		pr_info("CPU%u: %s: idle %d\n",
			cpu, __func__, (int)from_idle);

	if (MSM_PM_DEBUG_POWER_COLLAPSE & msm_pm_debug_mask)
		pr_info("CPU%u: %s: pre power down\n", cpu, __func__);

	if (cpu_online(cpu) && !msm_no_ramp_down_pc)
		saved_acpuclk_rate = ramp_down_last_cpu(cpu);

	collapsed = msm_pm_spm_power_collapse(cpu, MSM_SPM_MODE_POWER_COLLAPSE,
			from_idle, true);

	if (cpu_online(cpu) && !msm_no_ramp_down_pc)
		ramp_up_first_cpu(cpu, saved_acpuclk_rate);

	if (MSM_PM_DEBUG_POWER_COLLAPSE & msm_pm_debug_mask)
		pr_info("CPU%u: %s: post power up\n", cpu, __func__);

	if (MSM_PM_DEBUG_POWER_COLLAPSE & msm_pm_debug_mask)
		pr_info("CPU%u: %s: return\n", cpu, __func__);
	return collapsed;
}
/******************************************************************************
 * External Idle/Suspend Functions
 *****************************************************************************/

static void arch_idle(void) {}

static bool (*execute[MSM_PM_SLEEP_MODE_NR])(bool idle) = {
	[MSM_PM_SLEEP_MODE_WAIT_FOR_INTERRUPT] = msm_pm_swfi,
	[MSM_PM_SLEEP_MODE_POWER_COLLAPSE_STANDALONE] =
		msm_pm_power_collapse_standalone,
	[MSM_PM_SLEEP_MODE_RETENTION] = msm_pm_retention,
	[MSM_PM_SLEEP_MODE_FASTPC] = msm_pm_fastpc,
	[MSM_PM_SLEEP_MODE_POWER_COLLAPSE] = msm_pm_power_collapse,
};

/**
 * msm_cpu_pm_enter_sleep(): Enter a low power mode on current cpu
 *
 * @mode - sleep mode to enter
 * @from_idle - bool to indicate that the mode is exercised during idle/suspend
 *
 * returns none
 *
 * The code should be with interrupts disabled and on the core on which the
 * low power is to be executed.
 *
 */
bool msm_cpu_pm_enter_sleep(enum msm_pm_sleep_mode mode, bool from_idle)
{
	bool exit_stat = false;
	unsigned int cpu = smp_processor_id();

	if ((!from_idle  && cpu_online(cpu))
			|| (MSM_PM_DEBUG_IDLE & msm_pm_debug_mask))
		pr_info("CPU%u:%s mode:%d during %s\n", cpu, __func__,
				mode, from_idle ? "idle" : "suspend");

	if (execute[mode])
		exit_stat = execute[mode](from_idle);

	return exit_stat;
}

/**
 * msm_pm_wait_cpu_shutdown() - Wait for a core to be power collapsed during
 *				hotplug
 *
 * @ cpu - cpu to wait on.
 *
 * Blocking function call that waits on the core to be power collapsed. This
 * function is called from platform_cpu_die to ensure that a core is power
 * collapsed before sending the CPU_DEAD notification so the drivers could
 * remove the resource votes for this CPU(regulator and clock)
 */
int msm_pm_wait_cpu_shutdown(unsigned int cpu)
{
	int timeout = 0;

	if (!msm_pm_slp_sts)
		return 0;
	if (!msm_pm_slp_sts[cpu].base_addr)
		return 0;
	while (1) {
		/*
		 * Check for the SPM of the core being hotplugged to set
		 * its sleep state.The SPM sleep state indicates that the
		 * core has been power collapsed.
		 */
		int acc_sts = __raw_readl(msm_pm_slp_sts[cpu].base_addr);

		if (acc_sts & msm_pm_slp_sts[cpu].mask)
			return 0;

		udelay(100);
		/*
		 * Dump spm registers for debugging
		 */
		if (++timeout == 20) {
			msm_spm_dump_regs(cpu);
			//__WARN_printf(
			//"CPU%u didn't collapse in 2ms, sleep status: 0x%x\n",
			//					cpu, acc_sts);
		}
	}

	return -EBUSY;
}

static void msm_pm_ack_retention_disable(void *data)
{
	/*
	 * This is a NULL function to ensure that the core has woken up
	 * and is safe to disable retention.
	 */
}
/**
 * msm_pm_enable_retention() - Disable/Enable retention on all cores
 * @enable: Enable/Disable retention
 *
 */
void msm_pm_enable_retention(bool enable)
{
	if (enable == msm_pm_ldo_retention_enabled)
		return;

	msm_pm_ldo_retention_enabled = enable;

	/*
	 * If retention is being disabled, wakeup all online core to ensure
	 * that it isn't executing retention. Offlined cores need not be woken
	 * up as they enter the deepest sleep mode, namely RPM assited power
	 * collapse
	 */
	if (!enable) {
		preempt_disable();
		smp_call_function_many(&retention_cpus,
				msm_pm_ack_retention_disable,
				NULL, true);
		preempt_enable();
	}
}
EXPORT_SYMBOL_GPL(msm_pm_enable_retention);

/**
 * msm_pm_retention_enabled() - Check if retention is enabled
 *
 * returns true if retention is enabled
 */
bool msm_pm_retention_enabled(void)
{
	return msm_pm_ldo_retention_enabled;
}
EXPORT_SYMBOL_GPL(msm_pm_retention_enabled);

struct msm_pc_debug_counters_buffer {
	long *reg;
	u32 len;
	char buf[MAX_BUF_SIZE];
};

static char *counter_name[MSM_PC_NUM_COUNTERS] = {
		"PC Entry Counter",
		"Warmboot Entry Counter",
		"PC Bailout Counter"
};

static int msm_pc_debug_counters_copy(
		struct msm_pc_debug_counters_buffer *data)
{
	int j;
	u32 stat;
	unsigned int cpu;
	unsigned int len;
	uint32_t cluster_id;
	uint32_t cpu_id;
	uint32_t offset;

	for_each_possible_cpu(cpu) {
		len = scnprintf(data->buf + data->len,
				sizeof(data->buf)-data->len,
				"CPU%d\n", cpu);
		cluster_id = MPIDR_AFFINITY_LEVEL(cpu_logical_map(cpu), 1);
		cpu_id = MPIDR_AFFINITY_LEVEL(cpu_logical_map(cpu), 0);
		offset = (cluster_id * MAX_CPUS_PER_CLUSTER
				 * MSM_PC_NUM_COUNTERS)
				 + (cpu_id * MSM_PC_NUM_COUNTERS);

		data->len += len;

		for (j = 0; j < MSM_PC_NUM_COUNTERS - 1; j++) {
			stat = data->reg[offset + j];
			len = scnprintf(data->buf + data->len,
					 sizeof(data->buf) - data->len,
					"\t%s: %d", counter_name[j], stat);

			data->len += len;
		}
		len = scnprintf(data->buf + data->len,
			 sizeof(data->buf) - data->len,
			"\n");

		data->len += len;
	}

	return data->len;
}

static ssize_t msm_pc_debug_counters_file_read(struct file *file,
		char __user *bufu, size_t count, loff_t *ppos)
{
	struct msm_pc_debug_counters_buffer *data;
	ssize_t ret;

	mutex_lock(&msm_pc_debug_mutex);
	data = file->private_data;

	if (!data) {
		ret = -EINVAL;
		goto exit;
	}

	if (!bufu) {
		ret = -EINVAL;
		goto exit;
	}

	if (!access_ok(bufu, count)) {
		ret = -EFAULT;
		goto exit;
	}

	if (*ppos >= data->len && data->len == 0)
		data->len = msm_pc_debug_counters_copy(data);

	ret = simple_read_from_buffer(bufu, count, ppos,
			data->buf, data->len);
exit:
	mutex_unlock(&msm_pc_debug_mutex);
	return ret;
}

static int msm_pc_debug_counters_file_open(struct inode *inode,
		struct file *file)
{
	struct msm_pc_debug_counters_buffer *buf;
	int ret = 0;

	mutex_lock(&msm_pc_debug_mutex);

	if (!inode->i_private) {
		ret = -EINVAL;
		goto exit;
	}

	file->private_data = kzalloc(
		sizeof(struct msm_pc_debug_counters_buffer), GFP_KERNEL);

	if (!file->private_data) {
		pr_err("%s: ERROR kmalloc failed to allocate %zu bytes\n",
		__func__, sizeof(struct msm_pc_debug_counters_buffer));

		ret = -ENOMEM;
		goto exit;
	}

	buf = file->private_data;
	buf->reg = (long *)inode->i_private;

exit:
	mutex_unlock(&msm_pc_debug_mutex);
	return ret;
}

static int msm_pc_debug_counters_file_close(struct inode *inode,
		struct file *file)
{
	mutex_lock(&msm_pc_debug_mutex);
	kfree(file->private_data);
	mutex_unlock(&msm_pc_debug_mutex);
	return 0;
}

static const struct file_operations msm_pc_debug_counters_fops = {
	.open = msm_pc_debug_counters_file_open,
	.read = msm_pc_debug_counters_file_read,
	.release = msm_pc_debug_counters_file_close,
	.llseek = no_llseek,
};

static int msm_pm_clk_init(struct platform_device *pdev)
{
	bool synced_clocks;
	unsigned int cpu;
	char clk_name[] = "cpu??_clk";
	char *key;

	key = "qcom,saw-turns-off-pll";
	if (of_property_read_bool(pdev->dev.of_node, key))
		return 0;

	key = "qcom,synced-clocks";
	synced_clocks = of_property_read_bool(pdev->dev.of_node, key);

	for ((cpu) = 0; (cpu) < 1; (cpu)++) {
		struct clk *clk;

		snprintf(clk_name, sizeof(clk_name), "cpu%d_clk", cpu);
		clk = clk_get(&pdev->dev, clk_name);
		if (IS_ERR(clk)) {
			if (cpu && synced_clocks)
				return 0;
			clk = NULL;
		}
		per_cpu(cpu_clks, cpu) = clk;
	}

	if (synced_clocks)
		return 0;

	l2_clk = clk_get(&pdev->dev, "l2_clk");
	if (IS_ERR(l2_clk))
		pr_warn("%s: Could not get l2_clk (-%ld)\n", __func__,
			PTR_ERR(l2_clk));

	return 0;
}

static int msm_cpu_pm_probe(struct platform_device *pdev)
{
	struct dentry *dent = NULL;
	struct resource *res = NULL;
	int ret = 0;
	void __iomem *msm_pc_debug_counters_imem;
	char *key;
	int alloc_size = (MAX_NUM_CLUSTER * MAX_CPUS_PER_CLUSTER
					* MSM_PC_NUM_COUNTERS
					* sizeof(*msm_pc_debug_counters));

	msm_pc_debug_counters = dma_alloc_coherent(&pdev->dev, alloc_size,
				&msm_pc_debug_counters_phys, GFP_KERNEL);

	if (msm_pc_debug_counters) {
		memset(msm_pc_debug_counters, 0, alloc_size);
		dent = debugfs_create_file("pc_debug_counter", 0444, NULL,
				msm_pc_debug_counters,
				&msm_pc_debug_counters_fops);
		if (!dent)
			pr_err("%s: ERROR debugfs_create_file failed\n",
					__func__);
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (!res)
			goto skip_save_imem;
		msm_pc_debug_counters_imem = devm_ioremap(&pdev->dev,
						res->start, resource_size(res));
		if (msm_pc_debug_counters_imem) {
			writel_relaxed(msm_pc_debug_counters_phys,
					msm_pc_debug_counters_imem);
			/* memory barrier */
			mb();
		}
	} else {
		msm_pc_debug_counters = NULL;
		msm_pc_debug_counters_phys = 0;
	}
skip_save_imem:
	if (pdev->dev.of_node) {
		key = "qcom,tz-flushes-cache";
		msm_pm_tz_flushes_cache =
				of_property_read_bool(pdev->dev.of_node, key);

		key = "qcom,no-pll-switch-for-retention";
		msm_pm_ret_no_pll_switch =
				of_property_read_bool(pdev->dev.of_node, key);

		ret = msm_pm_clk_init(pdev);
		if (ret) {
			pr_info("msm_pm_clk_init returned error\n");
			return ret;
		}
	}

	if (pdev->dev.of_node)
		of_platform_populate(pdev->dev.of_node, NULL, NULL, &pdev->dev);

	return ret;
}

static const struct of_device_id msm_cpu_pm_table[] = {
	{.compatible = "qcom,pm"},
	{},
};

static struct platform_driver msm_cpu_pm_driver = {
	.probe = msm_cpu_pm_probe,
	.driver = {
		.name = "msm-pm",
		.of_match_table = msm_cpu_pm_table,
	},
};

static int __init msm_pm_debug_counters_init(void)
{
	int rc;

	rc = platform_driver_register(&msm_cpu_pm_driver);

	if (rc)
		pr_err("%s(): failed to register driver %s\n", __func__,
				msm_cpu_pm_driver.driver.name);
	return rc;
}
fs_initcall(msm_pm_debug_counters_init);

#ifdef CONFIG_ARM
static int idle_initialize(void)
{
	arm_pm_idle = arch_idle;
	return 0;
}
early_initcall(idle_initialize);
#endif
