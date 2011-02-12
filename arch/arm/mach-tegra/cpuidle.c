/*
 * arch/arm/mach-tegra/cpuidle.c
 *
 * CPU idle driver for Tegra CPUs
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/kernel.h>
#include <linux/cpu.h>
#include <linux/cpuidle.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/hrtimer.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/suspend.h>
#include <linux/tick.h>

#include <asm/cacheflush.h>
#include <asm/hardware/gic.h>
#include <asm/localtimer.h>

#include <mach/iomap.h>
#include <mach/irqs.h>
#include <mach/legacy_irq.h>
#include <mach/suspend.h>

#include "power.h"

#define TEGRA_CPUIDLE_BOTH_IDLE		INT_QUAD_RES_24
#define TEGRA_CPUIDLE_TEAR_DOWN		INT_QUAD_RES_25

#define EVP_CPU_RESET_VECTOR \
	(IO_ADDRESS(TEGRA_EXCEPTION_VECTORS_BASE) + 0x100)
#define CLK_RST_CONTROLLER_RST_CPU_CMPLX_SET \
	(IO_ADDRESS(TEGRA_CLK_RESET_BASE) + 0x340)
#define CLK_RST_CONTROLLER_RST_CPU_CMPLX_CLR \
	(IO_ADDRESS(TEGRA_CLK_RESET_BASE) + 0x344)
#define CLK_RST_CONTROLLER_CLK_CPU_CMPLX \
	(IO_ADDRESS(TEGRA_CLK_RESET_BASE) + 0x4c)

static bool lp2_in_idle __read_mostly = true;
static bool lp2_disabled_by_suspend;
module_param(lp2_in_idle, bool, 0644);

static s64 tegra_cpu1_idle_time = LLONG_MAX;;
static int tegra_lp2_exit_latency;
static int tegra_lp2_power_off_time;

static struct {
	unsigned int cpu_ready_count[2];
	unsigned long long cpu_wants_lp2_time[2];
	unsigned long long in_lp2_time;
	unsigned int both_idle_count;
	unsigned int tear_down_count;
	unsigned int lp2_count;
	unsigned int lp2_completed_count;
	unsigned int lp2_count_bin[32];
	unsigned int lp2_completed_count_bin[32];
	unsigned int lp2_int_count[NR_IRQS];
	unsigned int last_lp2_int_count[NR_IRQS];
} idle_stats;

struct cpuidle_driver tegra_idle = {
	.name = "tegra_idle",
	.owner = THIS_MODULE,
};

static DEFINE_PER_CPU(struct cpuidle_device *, idle_devices);

#define FLOW_CTRL_WAITEVENT   (2<<29)
#define FLOW_CTRL_JTAG_RESUME (1<<28)
#define FLOW_CTRL_HALT_CPUx_EVENTS(cpu) ((cpu)?((cpu-1)*0x8 + 0x14) : 0x0)

#define PMC_SCRATCH_38 0x134
#define PMC_SCRATCH_39 0x138

#define CLK_RESET_CLK_MASK_ARM 0x44

static inline unsigned int time_to_bin(unsigned int time)
{
	return fls(time);
}

static inline void tegra_unmask_irq(int irq)
{
	struct irq_chip *chip = get_irq_chip(irq);
	chip->unmask(irq);
}

static inline void tegra_mask_irq(int irq)
{
	struct irq_chip *chip = get_irq_chip(irq);
	chip->mask(irq);
}

static inline int tegra_pending_interrupt(void)
{
	void __iomem *gic_cpu = IO_ADDRESS(TEGRA_ARM_PERIF_BASE + 0x100);
	u32 reg = readl(gic_cpu + 0x18);
	reg &= 0x3FF;

	return reg;
}

static inline void tegra_flow_wfi(struct cpuidle_device *dev)
{
	void __iomem *flow_ctrl = IO_ADDRESS(TEGRA_FLOW_CTRL_BASE);
	u32 reg = FLOW_CTRL_WAITEVENT | FLOW_CTRL_JTAG_RESUME;

	flow_ctrl = flow_ctrl + FLOW_CTRL_HALT_CPUx_EVENTS(dev->cpu);

	stop_critical_timings();
	dsb();
	__raw_writel(reg, flow_ctrl);
	reg = __raw_readl(flow_ctrl);
	__asm__ volatile ("wfi");
	__raw_writel(0, flow_ctrl);
	reg = __raw_readl(flow_ctrl);
	start_critical_timings();
}

#ifdef CONFIG_SMP
static inline bool tegra_wait_for_both_idle(struct cpuidle_device *dev)
{
	int wake_int;

	tegra_unmask_irq(TEGRA_CPUIDLE_BOTH_IDLE);

	tegra_flow_wfi(dev);

	wake_int = tegra_pending_interrupt();

	tegra_mask_irq(TEGRA_CPUIDLE_BOTH_IDLE);

	return wake_int == TEGRA_CPUIDLE_BOTH_IDLE &&
		tegra_pending_interrupt() == 1023;
}

static inline bool tegra_wait_for_tear_down(struct cpuidle_device *dev)
{
	int wake_int;
	irq_set_affinity(TEGRA_CPUIDLE_TEAR_DOWN, cpumask_of(1));
	tegra_unmask_irq(TEGRA_CPUIDLE_TEAR_DOWN);

	tegra_flow_wfi(dev);

	wake_int = tegra_pending_interrupt();

	tegra_mask_irq(TEGRA_CPUIDLE_TEAR_DOWN);

	return wake_int == TEGRA_CPUIDLE_TEAR_DOWN &&
		tegra_pending_interrupt() == 1023;
}

static inline bool tegra_cpu_in_reset(int cpu)
{
	return !!(readl(CLK_RST_CONTROLLER_RST_CPU_CMPLX_SET) & (1 << cpu));
}

static int tegra_tear_down_cpu1(void)
{
	u32 reg;

	/* Signal to CPU1 to tear down */
	tegra_legacy_force_irq_set(TEGRA_CPUIDLE_TEAR_DOWN);

	/* At this point, CPU0 can no longer abort LP2, but CP1 can */
	/* TODO: any way not to poll here? Use the LP2 timer to wfi? */
	/* takes ~80 us */
	while (!tegra_cpu_in_reset(1) &&
		tegra_legacy_force_irq_status(TEGRA_CPUIDLE_BOTH_IDLE))
		cpu_relax();

	tegra_legacy_force_irq_clr(TEGRA_CPUIDLE_TEAR_DOWN);

	/* If CPU1 aborted LP2, restart the process */
	if (!tegra_legacy_force_irq_status(TEGRA_CPUIDLE_BOTH_IDLE))
		return -EAGAIN;

	/* CPU1 is ready for LP2, clock gate it */
	reg = readl(CLK_RST_CONTROLLER_CLK_CPU_CMPLX);
	writel(reg | (1<<9), CLK_RST_CONTROLLER_CLK_CPU_CMPLX);

	return 0;
}

static void tegra_wake_cpu1(void)
{
	unsigned long boot_vector;
	unsigned long old_boot_vector;
	unsigned long timeout;
	u32 reg;

	boot_vector = virt_to_phys(tegra_hotplug_startup);
	old_boot_vector = readl(EVP_CPU_RESET_VECTOR);
	writel(boot_vector, EVP_CPU_RESET_VECTOR);

	/* enable cpu clock on cpu */
	reg = readl(CLK_RST_CONTROLLER_CLK_CPU_CMPLX);
	writel(reg & ~(1 << (8 + 1)), CLK_RST_CONTROLLER_CLK_CPU_CMPLX);

	reg = 0x1111 << 1;
	writel(reg, CLK_RST_CONTROLLER_RST_CPU_CMPLX_CLR);

	/* unhalt the cpu */
	writel(0, IO_ADDRESS(TEGRA_FLOW_CTRL_BASE) + 0x14);

	timeout = jiffies + msecs_to_jiffies(1000);
	while (time_before(jiffies, timeout)) {
		if (readl(EVP_CPU_RESET_VECTOR) != boot_vector)
			break;
		udelay(10);
	}

	/* put the old boot vector back */
	writel(old_boot_vector, EVP_CPU_RESET_VECTOR);

	/* CPU1 is now started */
}
#else
static inline bool tegra_wait_for_both_idle(struct cpuidle_device *dev)
{
	return true;
}

static inline int tegra_tear_down_cpu1(void)
{
	return 0;
}

static inline void tegra_wake_cpu1(void)
{
}
#endif

static void tegra_idle_enter_lp2_cpu0(struct cpuidle_device *dev,
	struct cpuidle_state *state)
{
	s64 request;
	ktime_t enter;
	ktime_t exit;
	bool sleep_completed = false;
	int bin;

restart:
	if (!tegra_wait_for_both_idle(dev))
		return;

	idle_stats.both_idle_count++;

	if (need_resched())
		return;

	/* CPU1 woke CPU0 because both are idle */

	request = ktime_to_us(tick_nohz_get_sleep_length());
	if (request < state->target_residency) {
		/* Not enough time left to enter LP2 */
		tegra_flow_wfi(dev);
		return;
	}

	idle_stats.tear_down_count++;

	if (tegra_tear_down_cpu1())
		goto restart;

	/* Enter LP2 */
	request = ktime_to_us(tick_nohz_get_sleep_length());
	smp_rmb();
	request = min_t(s64, request, tegra_cpu1_idle_time);

	enter = ktime_get();
	if (request > state->target_residency) {
		s64 sleep_time = request - tegra_lp2_exit_latency;

		bin = time_to_bin((u32)request / 1000);
		idle_stats.lp2_count++;
		idle_stats.lp2_count_bin[bin]++;

		if (tegra_suspend_lp2(sleep_time) == 0)
			sleep_completed = true;
		else
			idle_stats.lp2_int_count[tegra_pending_interrupt()]++;
	}

	/* Bring CPU1 out of LP2 */
	/* TODO: polls for CPU1 to boot, wfi would be better */
	/* takes ~80 us */

	/* set the reset vector to point to the secondary_startup routine */
	smp_wmb();

	tegra_wake_cpu1();

	/*
	 * TODO: is it worth going back to wfi if no interrupt is pending
	 * and the requested sleep time has not passed?
	 */

	exit = ktime_get();
	if (sleep_completed) {
		/*
		 * Stayed in LP2 for the full time until the next tick,
		 * adjust the exit latency based on measurement
		 */
		int offset = ktime_to_us(ktime_sub(exit, enter)) - request;
		int latency = tegra_lp2_exit_latency + offset / 16;
		latency = clamp(latency, 0, 10000);
		tegra_lp2_exit_latency = latency;
		smp_wmb();

		idle_stats.lp2_completed_count++;
		idle_stats.lp2_completed_count_bin[bin]++;
		idle_stats.in_lp2_time += ktime_to_us(ktime_sub(exit, enter));

		pr_debug("%lld %lld %d %d\n", request,
			ktime_to_us(ktime_sub(exit, enter)),
			offset, bin);
	}
}

#ifdef CONFIG_SMP
static void tegra_idle_enter_lp2_cpu1(struct cpuidle_device *dev,
	struct cpuidle_state *state)
{
	u32 twd_ctrl;
	u32 twd_load;
	s64 request;

	tegra_legacy_force_irq_set(TEGRA_CPUIDLE_BOTH_IDLE);

	if (!tegra_wait_for_tear_down(dev))
		goto out;

	if (need_resched())
		goto out;

	/*
	 * CPU1 woke CPU0 because both were idle
	 * CPU0 responded by waking CPU1 to tell it to disable itself
	 */

	request = ktime_to_us(tick_nohz_get_sleep_length());
	if (request < tegra_lp2_exit_latency) {
		/*
		 * Not enough time left to enter LP2
		 * Signal to CPU0 that CPU1 rejects LP2, and stay in
		 */
		tegra_legacy_force_irq_clr(TEGRA_CPUIDLE_BOTH_IDLE);
		tegra_flow_wfi(dev);
		goto out;
	}

	tegra_cpu1_idle_time = request;
	smp_wmb();

	/* Prepare CPU1 for LP2 by putting it in reset */

	stop_critical_timings();
	gic_cpu_exit(0);
	barrier();
	twd_ctrl = readl(twd_base + 0x8);
	twd_load = readl(twd_base + 0);

	flush_cache_all();
	barrier();
	__cortex_a9_save(0);
	/* CPU1 is in reset, waiting for CPU0 to boot it, possibly after LP2 */


	/* CPU0 booted CPU1 out of reset */
	barrier();
	writel(twd_ctrl, twd_base + 0x8);
	writel(twd_load, twd_base + 0);
	gic_cpu_init(0, IO_ADDRESS(TEGRA_ARM_PERIF_BASE) + 0x100);
	tegra_unmask_irq(IRQ_LOCALTIMER);

	tegra_legacy_force_irq_clr(TEGRA_CPUIDLE_BOTH_IDLE);

	writel(smp_processor_id(), EVP_CPU_RESET_VECTOR);
	start_critical_timings();

	/*
	 * TODO: is it worth going back to wfi if no interrupt is pending
	 * and the requested sleep time has not passed?
	 */

	return;

out:
	tegra_legacy_force_irq_clr(TEGRA_CPUIDLE_BOTH_IDLE);
}
#endif

static int tegra_idle_enter_lp3(struct cpuidle_device *dev,
	struct cpuidle_state *state)
{
	ktime_t enter, exit;
	s64 us;

	local_irq_disable();
	local_fiq_disable();

	enter = ktime_get();
	if (!need_resched())
		tegra_flow_wfi(dev);
	exit = ktime_sub(ktime_get(), enter);
	us = ktime_to_us(exit);

	local_fiq_enable();
	local_irq_enable();
	return (int)us;
}

static int tegra_idle_enter_lp2(struct cpuidle_device *dev,
	struct cpuidle_state *state)
{
	ktime_t enter, exit;
	s64 us;

	if (!lp2_in_idle || lp2_disabled_by_suspend)
		return tegra_idle_enter_lp3(dev, state);

	local_irq_disable();
	clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_ENTER, &dev->cpu);
	local_fiq_disable();
	enter = ktime_get();

	idle_stats.cpu_ready_count[dev->cpu]++;

#ifdef CONFIG_SMP
	if (dev->cpu == 0)
		tegra_idle_enter_lp2_cpu0(dev, state);
	else
		tegra_idle_enter_lp2_cpu1(dev, state);
#else
	tegra_idle_enter_lp2_cpu0(dev, state);
#endif

	exit = ktime_sub(ktime_get(), enter);
	us = ktime_to_us(exit);

	local_fiq_enable();
	clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_EXIT, &dev->cpu);
	local_irq_enable();

	smp_rmb();
	state->exit_latency = tegra_lp2_exit_latency;
	state->target_residency = tegra_lp2_exit_latency +
		tegra_lp2_power_off_time;

	idle_stats.cpu_wants_lp2_time[dev->cpu] += us;

	return (int)us;
}

static int tegra_cpuidle_register_device(unsigned int cpu)
{
	struct cpuidle_device *dev;
	struct cpuidle_state *state;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->state_count = 0;
	dev->cpu = cpu;

	tegra_lp2_power_off_time = tegra_cpu_power_off_time();

	state = &dev->states[0];
	snprintf(state->name, CPUIDLE_NAME_LEN, "LP3");
	snprintf(state->desc, CPUIDLE_DESC_LEN, "CPU flow-controlled");
	state->exit_latency = 10;
	state->target_residency = 10;
	state->power_usage = 600;
	state->flags = CPUIDLE_FLAG_SHALLOW | CPUIDLE_FLAG_TIME_VALID;
	state->enter = tegra_idle_enter_lp3;
	dev->safe_state = state;
	dev->state_count++;

	state = &dev->states[1];
	snprintf(state->name, CPUIDLE_NAME_LEN, "LP2");
	snprintf(state->desc, CPUIDLE_DESC_LEN, "CPU power-gate");
	state->exit_latency = tegra_cpu_power_good_time();

	state->target_residency = tegra_cpu_power_off_time() +
		tegra_cpu_power_good_time();
	state->power_usage = 0;
	state->flags = CPUIDLE_FLAG_BALANCED | CPUIDLE_FLAG_TIME_VALID;
	state->enter = tegra_idle_enter_lp2;

	dev->power_specified = 1;
	dev->safe_state = state;
	dev->state_count++;

	if (cpuidle_register_device(dev)) {
		pr_err("CPU%u: failed to register idle device\n", cpu);
		kfree(dev);
		return -EIO;
	}
	per_cpu(idle_devices, cpu) = dev;
	return 0;
}

/* The IRQs that are used for communication between the cpus to agree on the
 * cpuidle state should never get handled
 */
static irqreturn_t tegra_cpuidle_irq(int irq, void *dev)
{
	pr_err("%s: unexpected interrupt %d on cpu %d\n", __func__, irq,
		smp_processor_id());
	BUG();
}

static int tegra_cpuidle_pm_notify(struct notifier_block *nb,
	unsigned long event, void *dummy)
{
	if (event == PM_SUSPEND_PREPARE)
		lp2_disabled_by_suspend = true;
	else if (event == PM_POST_SUSPEND)
		lp2_disabled_by_suspend = false;

	return NOTIFY_OK;
}

static struct notifier_block tegra_cpuidle_pm_notifier = {
	.notifier_call = tegra_cpuidle_pm_notify,
};

static int __init tegra_cpuidle_init(void)
{
	unsigned int cpu;
	void __iomem *mask_arm;
	unsigned int reg;
	int ret;

	irq_set_affinity(TEGRA_CPUIDLE_BOTH_IDLE, cpumask_of(0));
	irq_set_affinity(TEGRA_CPUIDLE_TEAR_DOWN, cpumask_of(1));

	ret = request_irq(TEGRA_CPUIDLE_BOTH_IDLE, tegra_cpuidle_irq,
		IRQF_NOAUTOEN, "tegra_cpuidle_both_idle", NULL);
	if (ret) {
		pr_err("%s: Failed to request cpuidle irq\n", __func__);
		return ret;
	}

	ret = request_irq(TEGRA_CPUIDLE_TEAR_DOWN, tegra_cpuidle_irq,
		IRQF_NOAUTOEN, "tegra_cpuidle_tear_down_cpu1", NULL);
	if (ret) {
		pr_err("%s: Failed to request cpuidle irq\n", __func__);
		return ret;
	}


	disable_irq(TEGRA_CPUIDLE_BOTH_IDLE);
	disable_irq(TEGRA_CPUIDLE_TEAR_DOWN);
	tegra_mask_irq(TEGRA_CPUIDLE_BOTH_IDLE);
	tegra_mask_irq(TEGRA_CPUIDLE_TEAR_DOWN);

	mask_arm = IO_ADDRESS(TEGRA_CLK_RESET_BASE) + CLK_RESET_CLK_MASK_ARM;

	reg = readl(mask_arm);
	writel(reg | (1<<31), mask_arm);

	ret = cpuidle_register_driver(&tegra_idle);

	if (ret)
		return ret;

	for_each_possible_cpu(cpu) {
		if (tegra_cpuidle_register_device(cpu))
			pr_err("CPU%u: error initializing idle loop\n", cpu);
	}

	tegra_lp2_exit_latency = tegra_cpu_power_good_time();

	register_pm_notifier(&tegra_cpuidle_pm_notifier);

	return 0;
}

static void __exit tegra_cpuidle_exit(void)
{
	cpuidle_unregister_driver(&tegra_idle);
}

module_init(tegra_cpuidle_init);
module_exit(tegra_cpuidle_exit);

#ifdef CONFIG_DEBUG_FS
static int tegra_lp2_debug_show(struct seq_file *s, void *data)
{
	int bin;
	int i;
	seq_printf(s, "                                    cpu0     cpu1\n");
	seq_printf(s, "-------------------------------------------------\n");
	seq_printf(s, "cpu ready:                      %8u %8u\n",
		idle_stats.cpu_ready_count[0],
		idle_stats.cpu_ready_count[1]);
	seq_printf(s, "both idle:      %8u        %7u%% %7u%%\n",
		idle_stats.both_idle_count,
		idle_stats.both_idle_count * 100 /
			(idle_stats.cpu_ready_count[0] ?: 1),
		idle_stats.both_idle_count * 100 /
			(idle_stats.cpu_ready_count[1] ?: 1));
	seq_printf(s, "tear down:      %8u %7u%%\n", idle_stats.tear_down_count,
		idle_stats.tear_down_count * 100 /
			(idle_stats.both_idle_count ?: 1));
	seq_printf(s, "lp2:            %8u %7u%%\n", idle_stats.lp2_count,
		idle_stats.lp2_count * 100 /
			(idle_stats.both_idle_count ?: 1));
	seq_printf(s, "lp2 completed:  %8u %7u%%\n",
		idle_stats.lp2_completed_count,
		idle_stats.lp2_completed_count * 100 /
			(idle_stats.lp2_count ?: 1));

	seq_printf(s, "\n");
	seq_printf(s, "cpu ready time:                 %8llu %8llu ms\n",
		div64_u64(idle_stats.cpu_wants_lp2_time[0], 1000),
		div64_u64(idle_stats.cpu_wants_lp2_time[1], 1000));
	seq_printf(s, "lp2 time:       %8llu ms     %7d%% %7d%%\n",
		div64_u64(idle_stats.in_lp2_time, 1000),
		(int)div64_u64(idle_stats.in_lp2_time * 100,
			idle_stats.cpu_wants_lp2_time[0] ?: 1),
		(int)div64_u64(idle_stats.in_lp2_time * 100,
			idle_stats.cpu_wants_lp2_time[1] ?: 1));

	seq_printf(s, "\n");
	seq_printf(s, "%19s %8s %8s %8s\n", "", "lp2", "comp", "%");
	seq_printf(s, "-------------------------------------------------\n");
	for (bin = 0; bin < 32; bin++) {
		if (idle_stats.lp2_count_bin[bin] == 0)
			continue;
		seq_printf(s, "%6u - %6u ms: %8u %8u %7u%%\n",
			1 << (bin - 1), 1 << bin,
			idle_stats.lp2_count_bin[bin],
			idle_stats.lp2_completed_count_bin[bin],
			idle_stats.lp2_completed_count_bin[bin] * 100 /
				idle_stats.lp2_count_bin[bin]);
	}

	seq_printf(s, "\n");
	seq_printf(s, "%3s %20s %6s %10s\n",
		"int", "name", "count", "last count");
	seq_printf(s, "--------------------------------------------\n");
	for (i = 0; i < NR_IRQS; i++) {
		if (idle_stats.lp2_int_count[i] == 0)
			continue;
		seq_printf(s, "%3d %20s %6d %10d\n",
			i, irq_to_desc(i)->action ?
				irq_to_desc(i)->action->name ?: "???" : "???",
			idle_stats.lp2_int_count[i],
			idle_stats.lp2_int_count[i] -
				idle_stats.last_lp2_int_count[i]);
		idle_stats.last_lp2_int_count[i] = idle_stats.lp2_int_count[i];
	};
	return 0;
}

static int tegra_lp2_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, tegra_lp2_debug_show, inode->i_private);
}

static const struct file_operations tegra_lp2_debug_ops = {
	.open		= tegra_lp2_debug_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init tegra_cpuidle_debug_init(void)
{
	struct dentry *dir;
	struct dentry *d;

	dir = debugfs_create_dir("cpuidle", NULL);
	if (!dir)
		return -ENOMEM;

	d = debugfs_create_file("lp2", S_IRUGO, dir, NULL,
		&tegra_lp2_debug_ops);
	if (!d)
		return -ENOMEM;

	return 0;
}
#endif

late_initcall(tegra_cpuidle_debug_init);
