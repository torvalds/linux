/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *              htt://www.samsung.com
 *
 * bL platform support
 *
 * Based on arch/arm/mach-vexpress/kingfisher.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/smp.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/syscore_ops.h>
#include <linux/cpu.h>
#include <linux/uaccess.h>

#include <asm/bL_switcher.h>
#include <asm/bL_entry.h>

#include <plat/cpu.h>
#include <plat/cci.h>
#include <plat/s5p-clock.h>

#include <mach/regs-pmu.h>
#include <mach/smc.h>
#include <mach/pmu.h>
#include <mach/debug.h>
#include <mach/cpufreq.h>

#define GIC_ENABLE_STATUS	0x1

static int core_count[BL_NR_CLUSTERS];
/*
 * We can't use regular spinlocks. In the switcher case, it is possible
 * for an outbound CPU to call power_down() after its inbound counterpart
 * is already live using the same logical CPU number which trips lockdep
 * debugging.
 */
static arch_spinlock_t exynos_lock = __ARCH_SPIN_LOCK_UNLOCKED;
static struct clk *apll;
static struct clk *fout_apll;

static int add_core_count(unsigned int cluster)
{
	BUG_ON(core_count[cluster] >= num_online_cpus());
	return ++core_count[cluster];
}

static unsigned int dec_core_count(unsigned int cluster)
{
	BUG_ON(core_count[cluster] == 0);
	return --core_count[cluster];
}

static void exynos_core_power_control(unsigned int cpu,
				      unsigned int cluster,
				      int enable)
{
	int value = 0;

	if (samsung_rev() >= EXYNOS5410_REV_1_0)
		cluster = !cluster;

	if (enable)
		value = EXYNOS_CORE_LOCAL_PWR_EN;

	if (cluster == 0)
		cpu += 4;

	__raw_writel(value, EXYNOS_ARM_CORE_CONFIGURATION(cpu));
}

static void bL_debug_info(void)
{
	/*
	 * If abnormal state occurs, prints the power state of 8 cores
	 * @PMU configuration register value
	 * @PMU status register value
	 */
	unsigned int core_num = 0, cores = 0;
	unsigned int config_val = 0, status_val = 0;
	void __iomem *info_addr = (S5P_VA_SYSRAM_NS + 0x28);

	for (cores = 0; cores < 8; cores++) {
		if (cores == 0) {
			pr_info("EAGLE configuration & status register\n");
		} else if (cores == 4) {
			core_num = 0;
			pr_info("KFC configuration & status register\n");
		}
		config_val = __raw_readl(EXYNOS_ARM_CORE_CONFIGURATION(cores));
		status_val = __raw_readl(EXYNOS_ARM_CORE_STATUS(cores));
		pr_info("\t\t%s\tcpu%d\tconfiguration : %#x, status %#x",
			cores > 4 ? "KFC" : "EAGLE", core_num, config_val, status_val);
		core_num++;
	}

	core_num = 0;

	/*
	 * This informations are cpu power state(C2, HOTPLUG, SWITCH) and
	 * GIC ENABLE STATE of group register 0
	 */
	for (cores = 0; cores < 8 ; cores++) {
		if (cores == 0) {
			pr_info("Information of CPU_STATE\n");
		} else if (cores == 4) {
			core_num = 0;
			pr_info("Information of GIC group 0\n");
		}
		pr_info("\t\tcore%d\tvalue : %#x\n", core_num,
			__raw_readl(info_addr + (cores * 4)));
		core_num++;
	}
}

#ifdef CONFIG_EXYNOS5_CLUSTER_POWER_CONTROL
static int exynos_change_apll_parent(int cluster_enable)
{
	int ret = 0;

	if (IS_ERR(apll))
		return -ENODEV;

	if (IS_ERR(fout_apll))
		return -ENODEV;

	if (cluster_enable) {
		clk_enable(fout_apll);
		ret = clk_set_parent(apll, &clk_fout_apll);
	} else {
		ret = clk_set_parent(apll, &clk_fin_apll);
		clk_disable(fout_apll);
	}
	return ret;
}

static int exynos_cluster_power_control(unsigned int cluster, int enable)
{
	int value = 0;
	int ret = 0;
	unsigned long timeout = jiffies + msecs_to_jiffies(10);

#ifdef CONFIG_EXYNOS5410_DEBUG
	if (FLAG_T32_EN)
		return 0;
#endif

	if (samsung_rev() < EXYNOS5410_REV_1_0)
		cluster = !cluster;

	if (enable)
		value = EXYNOS_CORE_LOCAL_PWR_EN;

	if ((__raw_readl(EXYNOS_COMMON_STATUS(cluster)) & 0x3) == value)
		return 0;

	if (enable) {
		if (cluster == 0)
			ret = exynos_change_apll_parent(1);
	} else {
		if (cluster == 0)
			ret = exynos_change_apll_parent(0);
#ifdef CONFIG_ARM_EXYNOS_IKS_CPUFREQ
		reset_lpj_for_cluster(cluster);
#endif
	}

	if (ret)
		return ret;

	__raw_writel(value, EXYNOS_COMMON_CONFIGURATION(cluster));

	/* wait till cluster power control is applied */
	do {
		if ((__raw_readl(EXYNOS_COMMON_STATUS(cluster)) &
		__raw_readl(EXYNOS_L2_STATUS(cluster)) & 0x3) == value)
			return 0;
	} while (time_before(jiffies, timeout));

	return -EDEADLK;
}
#else
static inline int exynos_cluster_power_control(unsigned int cluster, int enable)
{
	return 0;
}
#endif

static int exynos_cluster_power_status(unsigned int cluster)
{
	int status = 0;
	int i;

	if (samsung_rev() >= EXYNOS5410_REV_1_0)
		cluster = !cluster;

	cluster = (cluster == 0) ? 4 : 0;

	for (i = 0; i < NR_CPUS; i++)
		status |= !!(__raw_readl(EXYNOS_ARM_CORE_STATUS(cluster + i)) &
			0x3) << i;

	return status;
}

static int read_mpidr(void)
{
	unsigned int id;
	asm volatile ("mrc\tp15, 0, %0, c0, c0, 5" : "=r" (id));
	return id;
}

#ifdef CONFIG_EXYNOS5_CCI

#define REG_SWITCHING_ADDR	(S5P_VA_SYSRAM_NS + 0x18)

#define MAX_CORE_COUNT 4
static int online_core_count;
static struct hrtimer cluster_power_down_timer;
static int kfs_use_count[BL_CPUS_PER_CLUSTER][BL_NR_CLUSTERS];
static unsigned int cluster_hotplug_cpu[BL_CPUS_PER_CLUSTER];

enum hrtimer_restart exynos_cluster_power_down(struct hrtimer *timer)
{
	ktime_t period;
	int cluster_to_powerdown;
	enum hrtimer_restart ret;

	arch_spin_lock(&exynos_lock);
	if (core_count[0] == 0) {
		cluster_to_powerdown = 0;
	} else if (core_count[1] == 0) {
		cluster_to_powerdown = 1;
	} else {
		ret = HRTIMER_NORESTART;
		goto end;
	}

	if (exynos_cluster_power_status(cluster_to_powerdown)) {
		period = ktime_set(0, 10000000);
		hrtimer_forward_now(timer, period);
		ret = HRTIMER_RESTART;
		goto end;
	} else {
		disable_cci_snoops(cluster_to_powerdown);
		exynos_cluster_power_control(cluster_to_powerdown, 0);
		ret = HRTIMER_NORESTART;
	}
end:
	arch_spin_unlock(&exynos_lock);
	return ret;
}

/*
 * bL_power_up - make given CPU in given cluster runable
 *
 * @cpu: CPU number within given cluster
 * @cluster: cluster number for the CPU
 *
 * The identified CPU is brought out of reset.  If the cluster was powered
 * down then it is brought up as well, taking care not to let the other CPUs
 * in the cluster run, and ensuring appropriate cluster setup.
 * Caller must ensure the appropriate entry vector is initialized prior to
 * calling this.
 */
static void bL_power_up(unsigned int cpu, unsigned int cluster)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(1);

	pr_debug("%s: cpu %u cluster %u\n", __func__, cpu, cluster);

	/*
	 * Since this is called with IRQs enabled, and no arch_spin_lock_irq
	 * variant exists, we need to disable IRQs manually here.
	 */
	local_irq_disable();
	arch_spin_lock(&exynos_lock);

	kfs_use_count[cpu][cluster]++;
	if (kfs_use_count[cpu][cluster] == 1) {
		add_core_count(cluster);

		/*
		 * If outbound core wake up the first man of inbound cluster,
		 * outbound core must meet conditions which are power-on of
		 * COMMON BLOCK and enabled cci snoop about inbound cluster.
		 *
		 * Also non-first cores do not need to perform
		 * exynos_cluster_power_control() and enable_cci_snoops().
		 */
		if (unlikely(core_count[cluster] == 1)) {
			exynos_cluster_power_control(cluster, 1);
			enable_cci_snoops(cluster);
		}
		set_boot_flag(cpu, SWITCH);
		exynos_core_power_control(cpu, cluster, 1);
	} else if (kfs_use_count[cpu][cluster] != 2) {
		/*
		 * The only possible values are:
		 * 0 = CPU down
		 * 1 = CPU (still) up
		 * 2 = CPU requested to be up before it had a chance
		 *     to actually make itself down.
		 * Any other value is a bug.
		 */
		BUG();
	}

	arch_spin_unlock(&exynos_lock);
	local_irq_enable();

	do {
		if (read_gic_flag(cpu) == GIC_ENABLE_STATUS)
			break;
	} while (time_before(jiffies, timeout));
	clear_gic_flag(cpu);

#ifdef CONFIG_ARM_TRUSTZONE
	/*
	 * Save secure contexts of outbound and migrate secure interrupt
	 * from outbount to inbound
	 */
	exynos_smc(SMC_CMD_SAVE, OP_TYPE_CORE, SMC_POWERSTATE_SWITCH, 0);
#endif
}

/*
 * Helper to determine whether the specified cluster should still be
 * shut down.  By polling this before shutting a cluster down, we can
 * reduce the probability of wasted cache flushing etc.
 */
static bool powerdown_needed(unsigned int cluster)
{
	bool need = false;

	if (core_count[cluster] == 0)
		need = true;

	return need;
}

/*
 * bL_power_down - power down given CPU in given cluster
 *
 * @cpu: CPU number within given cluster
 * @cluster: cluster number for the CPU
 *
 * The identified CPU is powered down.  If this is the last CPU still alive
 * in the cluster then the necessary steps to power down the cluster are
 * performed as well and a non zero value is returned in that case.
 *
 * It is assumed that the reset will be effective at the next WFI instruction
 * performed by the target CPU.
 */
static void bL_power_down(unsigned int cpu, unsigned int cluster)
{
	int op_type;
	bool last_man = false, skip_wfi = false;

	pr_debug("%s: cpu %u cluster %u\n", __func__, cpu, cluster);

	__bL_cpu_going_down(cpu, cluster);

	arch_spin_lock(&exynos_lock);
	kfs_use_count[cpu][cluster]--;
	if (kfs_use_count[cpu][cluster] == 0) {
		exynos_core_power_control(cpu, cluster, 0);
		dec_core_count(cluster);
		if (core_count[cluster] == 0) {
			if (__bL_cluster_state(cluster) == CLUSTER_UP)
				last_man = true;
		}
	} else if (kfs_use_count[cpu][cluster] == 1) {
		/*
		 * A power_up request went ahead of us.
		 * Even if we do not want to shut this CPU down,
		 * the caller expects a certain state as if the WFI
		 * was aborted.  So let's continue with cache cleaning.
		 */
		skip_wfi = true;
	} else
		BUG();

	if (last_man && __bL_outbound_enter_critical(cpu, cluster)) {
		arch_spin_unlock(&exynos_lock);

		if (!powerdown_needed(cluster)) {
			__bL_outbound_leave_critical(cluster, CLUSTER_UP);
			goto non_last_man;
		}
		__bL_outbound_leave_critical(cluster, CLUSTER_DOWN);
		op_type = OP_TYPE_CLUSTER;
	} else {
		arch_spin_unlock(&exynos_lock);

non_last_man:
		op_type = OP_TYPE_CORE;
	}

	cpu_proc_fin();
	__bL_cpu_down(cpu, cluster);

#ifdef CONFIG_ARM_TRUSTZONE
	/* Now we are prepared for power-down, do it: */
	if (!skip_wfi) {
		exynos_smc(SMC_CMD_SHUTDOWN, op_type, SMC_POWERSTATE_SWITCH, 0);
		/* should never get here */
		BUG();
	}
#endif

}

static void bL_inbound_setup(unsigned int cpu, unsigned int cluster)
{
	clear_boot_flag(cpu, SWITCH);
	/*
	 * Controling cluster power is last man of inbound cluster only.
	 * In other words, non-lastman dose not need checks the count
	 * of core every swich.
	 */

	if (unlikely(core_count[cluster] == 0))
		hrtimer_start(&cluster_power_down_timer,
			ktime_set(0, 1000000), HRTIMER_MODE_REL);
}

static size_t bL_check_status(char *info)
{
	size_t len = 0;
	int i;
	void __iomem *cci_base;

	cci_base = ioremap(EXYNOS5_PA_CCI, SZ_64K);
	if (!cci_base)
		return -ENOMEM;

	len += sprintf(&info[len], "\t0 1 2 3 L2 CCI\n");
	len += sprintf(&info[len], "[A15]   ");
	for (i = 0; i < 4; i++)
		len += sprintf(&info[len], "%d ",
			(readl(EXYNOS_PMUREG(0x2004) + i * 0x80) & 0x3)
								== 3 ? 1 : 0);

	len += sprintf(&info[len], " %d",
			(readl(EXYNOS_PMUREG(0x2504) + 0 * 0x80) & 0x3)
								== 3 ? 1 : 0);
	len += sprintf(&info[len], "  %d\n",
			(readl(cci_base + 0x4000 + 1 * 0x1000) & 0x3)
								== 3 ? 1 : 0);
	len += sprintf(&info[len], "[A7]    ");
	for (i = 4; i < 8; i++)
		len += sprintf(&info[len], "%d ",
			(readl(EXYNOS_PMUREG(0x2004) + i * 0x80) & 0x3)
								== 3 ? 1 : 0);

	len += sprintf(&info[len], " %d",
			(readl(EXYNOS_PMUREG(0x2504) + 1 * 0x80) & 0x3)
								== 3 ? 1 : 0);
	len += sprintf(&info[len], "  %d\n\n",
			(readl(cci_base + 0x4000 + 0 * 0x1000) & 0x3)
								== 3 ? 1 : 0);
	iounmap(cci_base);

	return len;
}

#else	/* !CONFIG_EXYNOS5_CCI */

/*
 * bL_power_up - make given CPU in given cluster runable
 *
 * @cpu: CPU number within given cluster
 * @cluster: cluster number for the CPU
 *
 * The identified CPU is brought out of reset.  If the cluster was powered
 * down then it is brought up as well, taking care not to let the other CPUs
 * in the cluster run, and ensuring appropriate cluster setup.
 * Caller must ensure the appropriate entry vector is initialized prior to
 * calling this.
 */
static void bL_power_up(unsigned int cpu, unsigned int cluster)
{
	/*
	 * If this code is performed when cpu state is interrupt disabled,
	 * while() statement is infinite and it doesn't excape. So Add the
	 * variable using loops_per_jiffy.
	 */
	unsigned long timeout = loops_per_jiffy * msecs_to_jiffies(10);
	unsigned long cnt = 0;

	pr_debug("%s: cpu %u cluster %u\n", __func__, cpu, cluster);

	/*
	 * Since this is called with IRQs enabled, and no arch_spin_lock_irq
	 * variant exists, we need to disable IRQs manually here.
	 */
	arch_spin_lock(&exynos_lock);

	/*
	 * If outbound core wake up the first man of inbound cluster,
	 * outbound core must meet conditions which is power-on of
	 * COMMON BLOCK.
	 *
	 * Also non-first cores do not need to perform
	 * exynos_cluster_power_control().
	 */
	if (add_core_count(cluster) == 1)
		exynos_cluster_power_control(cluster, 1);

	arch_spin_unlock(&exynos_lock);

	set_boot_flag(cpu, SWITCH);
	exynos_core_power_control(cpu, cluster, 1);

	while (1) {
		if (read_gic_flag(cpu) == GIC_ENABLE_STATUS) {
			clear_gic_flag(cpu);
			break;
		}

		if (++cnt > timeout) {
			bL_debug_info();
			panic("LINE:%d of %s\n", __LINE__, __func__);
		}
	}

#ifdef CONFIG_ARM_TRUSTZONE
	/*
	 * Save secure contexts of outbound and migrate secure interrupt
	 * from outbount to inbound
	 */
	exynos_smc(SMC_CMD_SAVE, OP_TYPE_CORE, SMC_POWERSTATE_SWITCH, 0);
#endif
}

/*
 * bL_power_down - power down given CPU in given cluster
 *
 * @cpu: CPU number within given cluster
 * @cluster: cluster number for the CPU
 *
 * The identified CPU is powered down.  If this is the last CPU still alive
 * in the cluster then the necessary steps to power down the cluster are
 * performed as well.
 *
 * It is assumed that the reset will be effective at the next WFI instruction
 * performed by the target CPU.
 */
static void bL_power_down(unsigned int cpu, unsigned int cluster)
{
	int op_type;
	bool last_man = false;

	/*
	 * If this code is performed when cpu state is interrupt disabled,
	 * while() statement is infinite and it doesn't excape. So Add the
	 * variable using loops_per_jiffy.
	 */
	unsigned long timeout = loops_per_jiffy * msecs_to_jiffies(10);
	unsigned long cnt = 0;

	pr_debug("%s: cpu %u cluster %u\n", __func__, cpu, cluster);

	exynos_core_power_control(cpu, cluster, 0);

	arch_spin_lock(&exynos_lock);

	if (dec_core_count(cluster) == 0)
		last_man = true;
	arch_spin_unlock(&exynos_lock);

	if (last_man) {
		op_type = OP_TYPE_CLUSTER;

		while (1) {
			/* Check for L1 flush of non last cores */
			if (!(exynos_cluster_power_status(cluster) ^ (1 << cpu)))
				break;
			if (++cnt > timeout) {
				bL_debug_info();
				panic("LINE:%d of %s\n", __LINE__, __func__);
			}
		}
	} else {
		op_type = OP_TYPE_CORE;
	}

	cpu_proc_fin();

#ifdef CONFIG_ARM_TRUSTZONE
	/* Now we are prepared for power-down, do it: */
	exynos_smc(SMC_CMD_SHUTDOWN, op_type, SMC_POWERSTATE_SWITCH, 0);
#endif

	/* should never get here */
	BUG();
}

extern void change_all_power_base_to(unsigned int cluster);

static void bL_inbound_setup(unsigned int cpu, unsigned int cluster)
{
	unsigned long timeout;
	unsigned long cnt = 0;

	clear_boot_flag(cpu, SWITCH);

	/*
	 * Controling cluster power is last man of inbound cluster only.
	 * In other words, non-lastman dose not need checks the count
	 * of core every swich.
	 */

	timeout = loops_per_jiffy * msecs_to_jiffies(10);
	if (cpu == 0) {
		change_all_power_base_to(!cluster);

		/* Wait for power off of OB cores */
		while (1) {
			if (!exynos_cluster_power_status(cluster))
				break;
			if (++cnt > timeout) {
				bL_debug_info();
				panic("LINE:%d of %s\n", __LINE__, __func__);
			}
		}

		exynos_cluster_power_control(cluster, 0);
	}
}

static size_t bL_check_status(char *info)
{
	size_t len = 0;
	int i;

	len += sprintf(&info[len], "\t0 1 2 3 L2 CCI\n");
	len += sprintf(&info[len], "[A15]   ");
	for (i = 0; i < 4; i++)
		len += sprintf(&info[len], "%d ",
			(readl(EXYNOS_PMUREG(0x2004) + i * 0x80) & 0x3)
								== 3 ? 1 : 0);

	len += sprintf(&info[len], " %d",
			(readl(EXYNOS_PMUREG(0x2504) + 0 * 0x80) & 0x3)
								== 3 ? 1 : 0);
	len += sprintf(&info[len], "\n");

	len += sprintf(&info[len], "[A7]    ");
	for (i = 4; i < 8; i++)
		len += sprintf(&info[len], "%d ",
			(readl(EXYNOS_PMUREG(0x2004) + i * 0x80) & 0x3)
								== 3 ? 1 : 0);

	len += sprintf(&info[len], " %d",
			(readl(EXYNOS_PMUREG(0x2504) + 1 * 0x80) & 0x3)
								== 3 ? 1 : 0);
	len += sprintf(&info[len], "\n\n");

	return len;
}
#endif	/* CONFIG_EXYNOS5_CCI */

static ssize_t bL_status_write(struct file *file, const char __user *buf,
			size_t len, loff_t *pos)
{
	unsigned char val[1];

	pr_debug("%s\n", __func__);

	if (len < 1)
		return -EINVAL;

	if (copy_from_user(val, buf, 1))
		return -EFAULT;

	if (val[0] == '1')
		exynos_l2_common_pwr_ctrl();
	else
		return -EINVAL;


	return len;
}

static ssize_t bL_status_read(struct file *file, char __user *buf,
			size_t len, loff_t *pos)
{
	size_t count = 0;
	char info[100];
	count = bL_check_status(info);
	if (count < 0)
		return -EINVAL;

	return simple_read_from_buffer(buf, len, pos, info, count);
}

extern void bL_power_up_setup(void);

static const struct bL_power_ops bL_control_power_ops = {
	.power_up		= bL_power_up,
	.power_down		= bL_power_down,
	.power_up_setup		= bL_power_up_setup,
	.inbound_setup		= bL_inbound_setup,
};

static const struct file_operations bL_status_fops = {
	.read		= bL_status_read,
	.write		= bL_status_write,
};

static struct miscdevice bL_status_device = {
	MISC_DYNAMIC_MINOR,
	"bL_status",
	&bL_status_fops
};

#if defined(CONFIG_PM)
#define REG_SWITCHING_ADDR	(S5P_VA_SYSRAM_NS + 0x18)

static void bL_resume(void)
{
	/*
	 * REG_SWITCHING_ADDR is in iRAM and this area is reset after
	 * suspend/resume. Thus without setting bl_entry_point,
	 * core switching is not working properly.
	 */
	__raw_writel(virt_to_phys(bl_entry_point), REG_SWITCHING_ADDR);
}
#else
#define bL_resume NULL
#endif

static struct syscore_ops exynos_bL_syscore_ops = {
	.resume		= bL_resume,
};

static int __cpuinit bL_hotplug_cpu_callback(struct notifier_block *nfb,
					unsigned long action, void *hcpu);

static struct notifier_block __cpuinitdata bL_hotplug_cpu_notifier = {
	.notifier_call = bL_hotplug_cpu_callback,
};

#ifdef CONFIG_EXYNOS5_CCI
static int __cpuinit bL_hotplug_cpu_callback(struct notifier_block *nfb,
					unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;

	switch (action) {
	case CPU_UP_PREPARE:
		/*
		 * When carry out hotplug-in, if cluster power state is down
		 * must turn COMMON BLOCK on and enable cci snoop before cpu
		 * brings up.
		 */
		arch_spin_lock(&exynos_lock);
		if (core_count[cluster_hotplug_cpu[cpu]] == 0) {
			exynos_cluster_power_control(
				cluster_hotplug_cpu[cpu], 1);
			enable_cci_snoops(cluster_hotplug_cpu[cpu]);
		}
		arch_spin_unlock(&exynos_lock);
		break;
	case CPU_ONLINE:
		arch_spin_lock(&exynos_lock);
		BUG_ON(online_core_count >= MAX_CORE_COUNT);
		online_core_count++;
		/*
		 * When some cpu hotplug in, the instance for synchroniztion
		 * must know the information about cpu & cluster power state
		 */
		if (core_count[cluster_hotplug_cpu[cpu]] == 0)
			bL_update_cluster_state(CLUSTER_UP,
						cluster_hotplug_cpu[cpu]);
		/* Increase the count of cpu which can perform a switch */
		add_core_count(cluster_hotplug_cpu[cpu]);
		kfs_use_count[cpu][cluster_hotplug_cpu[cpu]] = 1;
		 /* The state of hotpluged in cpu changes to CPU_UP */
		bL_update_cpu_state(CPU_UP, cpu, cluster_hotplug_cpu[cpu]);
		arch_spin_unlock(&exynos_lock);
		break;
	case CPU_DEAD:
		arch_spin_lock(&exynos_lock);
		BUG_ON(online_core_count == 0);
		online_core_count--;

		/* Save the cluster number of cpu that carry out hotplug-out */
		cluster_hotplug_cpu[cpu] = bL_running_cluster_num_cpus(cpu);

		/* Decrease the count of cpu which can perform a switch */
		dec_core_count(cluster_hotplug_cpu[cpu]);
		kfs_use_count[cpu][cluster_hotplug_cpu[cpu]] = 0;
		/* The state of hotpluged out cpu changes to CPU_DOWN */
		__bL_cpu_down(cpu, cluster_hotplug_cpu[cpu]);
		arch_spin_unlock(&exynos_lock);
		break;
	case CPU_POST_DEAD:
		/*
		 * If core count is 0, the cluster power state changes
		 * to CLUSTER_DOWN.
		 */
		arch_spin_lock(&exynos_lock);
		if (core_count[cluster_hotplug_cpu[cpu]] == 0) {
			bL_update_cluster_state(CLUSTER_DOWN,
						cluster_hotplug_cpu[cpu]);
		/*
		 * I don't know why perform the cluster power down here
		 * So if it need the feature, we must think to interwork
		 * with bL_switcher core driver.
		 */
		}
		arch_spin_unlock(&exynos_lock);
		break;
	}

	return NOTIFY_OK;
}

static int __init bL_control_init(void)
{
	unsigned int cpu, cluster_id = (read_mpidr() >> 8) & 0xf;
	int err;

	/* All future entries into the kernel goes through our entry vectors. */
	__raw_writel(virt_to_phys(bl_entry_point), REG_SWITCHING_ADDR);

	core_count[cluster_id] = MAX_CORE_COUNT;
	online_core_count = MAX_CORE_COUNT;

	hrtimer_init(&cluster_power_down_timer, CLOCK_MONOTONIC,
			HRTIMER_MODE_REL);
	cluster_power_down_timer.function = exynos_cluster_power_down;

	hrtimer_start(&cluster_power_down_timer,
			ktime_set(0, 10000000), HRTIMER_MODE_REL);

	register_syscore_ops(&exynos_bL_syscore_ops);

	/*
	 * Initialize CPU usage counts, assuming that only one cluster is
	 * activated at this point.
	 */
	for_each_online_cpu(cpu)
		kfs_use_count[cpu][cluster_id] = 1;

	/*
	 * Reaction of cpu hotplug in & out
	 */
	register_cpu_notifier(&bL_hotplug_cpu_notifier);

	/* Getting APLL */
	apll = clk_get(NULL, "mout_apll");
	if (IS_ERR(apll)) {
		pr_err("Fail to get APLL, so this problem is not resolved,");
		pr_err("function of cluster power down is nothing.\n");
	}

	/* Getting FOUT APLL */
	fout_apll = clk_get(NULL, "fout_apll");
	if (IS_ERR(fout_apll))
		pr_err("Fail to get FOUT APLL, so this problem is not resolved,"
			"function of cluster power down is nothing.\n");

	clk_enable(fout_apll);

	err = misc_register(&bL_status_device);
	if (err) {
		pr_err("Error to register misc device(%s)\n", __func__);
		return err;
	}

	return bL_switcher_init(&bL_control_power_ops);
}
#else /* !CONFIG_EXYNOS5_CCI */
static int __cpuinit bL_hotplug_cpu_callback(struct notifier_block *nfb,
					unsigned long action, void *hcpu)
{
	unsigned int cluster = (read_mpidr() >> 8) & 0xf;

	switch (action) {
	case CPU_ONLINE:
		arch_spin_lock(&exynos_lock);
		add_core_count(cluster);
		arch_spin_unlock(&exynos_lock);
		break;
	case CPU_DEAD:
		arch_spin_lock(&exynos_lock);
		dec_core_count(cluster);
		arch_spin_unlock(&exynos_lock);
		break;
	}
	return NOTIFY_OK;
}

static int __init bL_control_init(void)
{
	unsigned int cluster_id = (read_mpidr() >> 8) & 0xf;
	int err;

	/* All future entries into the kernel goes through our entry vectors. */
	__raw_writel(virt_to_phys(bl_entry_point), REG_SWITCHING_ADDR);

	core_count[cluster_id] = NR_CPUS;

	register_syscore_ops(&exynos_bL_syscore_ops);

	/*
	 * Reaction of cpu hotplug in & out
	 */
	register_cpu_notifier(&bL_hotplug_cpu_notifier);

	/* Getting APLL */
	apll = clk_get(NULL, "mout_apll");
	if (IS_ERR(apll)) {
		pr_err("Fail to get APLL, so this problem is not resolved,");
		pr_err("function of cluster power down is nothing.\n");
	}

	/* Getting FOUT APLL */
	fout_apll = clk_get(NULL, "fout_apll");
	if (IS_ERR(fout_apll))
		pr_err("Fail to get FOUT APLL, so this problem is not resolved,"
			"function of cluster power down is nothing.\n");

	clk_enable(fout_apll);

	err = misc_register(&bL_status_device);
	if (err) {
		pr_err("Error to register misc device(%s)\n", __func__);
		return err;
	}

	return bL_switcher_init(&bL_control_power_ops);
}
#endif /* !CONFIG_EXYNOS5_CCI */

device_initcall(bL_control_init);
