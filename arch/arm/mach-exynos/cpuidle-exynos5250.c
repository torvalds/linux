/*
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/clockchips.h>
#include <linux/cpu.h>
#include <linux/cpuidle.h>
#include <linux/cpu_pm.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/export.h>
#include <linux/moduleparam.h>
#include <linux/ratelimit.h>
#include <linux/time.h>
#include <linux/serial_core.h>

#include <asm/cp15.h>
#include <asm/cacheflush.h>
#include <asm/proc-fns.h>
#include <asm/smp_scu.h>
#include <asm/suspend.h>
#include <asm/unified.h>
#include <asm/hardware/gic.h>
#include <mach/regs-pmu.h>
#include <mach/regs-clock.h>
#include <mach/pmu.h>
#include <mach/smc.h>

#include <plat/pm.h>
#include <plat/cpu.h>
#include <plat/regs-serial.h>

#include <trace/events/power.h>

#ifdef CONFIG_ARM_TRUSTZONE
#define REG_DIRECTGO_ADDR	(S5P_VA_SYSRAM_NS + 0x24)
#define REG_DIRECTGO_FLAG	(S5P_VA_SYSRAM_NS + 0x20)
#define BOOT_VECTOR		(S5P_VA_SYSRAM_NS + 0x1C)
#else
#define REG_DIRECTGO_ADDR	(S5P_VA_SYSRAM + 0x24)
#define REG_DIRECTGO_FLAG	(S5P_VA_SYSRAM + 0x20)
#define BOOT_VECTOR		S5P_VA_SYSRAM
#endif

static bool allow_coupled_idle = true;
module_param(allow_coupled_idle, bool, 0644);

#define EXYNOS_CHECK_DIRECTGO	0xFCBA0D10

static atomic_t exynos_idle_barrier;
static volatile bool cpu1_abort;

static int exynos_enter_idle(struct cpuidle_device *dev,
			     struct cpuidle_driver *drv,
			     int index);
static int exynos_enter_lowpower(struct cpuidle_device *dev,
				 struct cpuidle_driver *drv,
				 int index);

static inline void cpu_enter_lowpower_a9(void)
{
	unsigned int v;

	flush_cache_all();
	asm volatile(
	"	mcr	p15, 0, %1, c7, c5, 0\n"
	"	mcr	p15, 0, %1, c7, c10, 4\n"
	/*
	 * Turn off coherency
	 */
	"	mrc	p15, 0, %0, c1, c0, 1\n"
	"	bic	%0, %0, %3\n"
	"	mcr	p15, 0, %0, c1, c0, 1\n"
	"	mrc	p15, 0, %0, c1, c0, 0\n"
	"	bic	%0, %0, %2\n"
	"	mcr	p15, 0, %0, c1, c0, 0\n"
	  : "=&r" (v)
	  : "r" (0), "Ir" (CR_C), "Ir" (0x40)
	  : "cc");
}

static inline void cpu_enter_lowpower_a15(void)
{
	unsigned int v;

	asm volatile(
	"       mrc     p15, 0, %0, c1, c0, 0\n"
	"       bic     %0, %0, %1\n"
	"       mcr     p15, 0, %0, c1, c0, 0\n"
	  : "=&r" (v)
	  : "Ir" (CR_C)
	  : "cc");

	flush_cache_all();

	asm volatile(
	/*
	* Turn off coherency
	*/
	"	dmb\n"
	"       mrc     p15, 0, %0, c1, c0, 1\n"
	"       bic     %0, %0, %1\n"
	"       mcr     p15, 0, %0, c1, c0, 1\n"
	: "=&r" (v)
	: "Ir" (0x40)
	: "cc");

	isb();
	dsb();
}

static inline void cpu_leave_lowpower(void)
{
	unsigned int v;

	asm volatile(
	"mrc	p15, 0, %0, c1, c0, 0\n"
	"	orr	%0, %0, %1\n"
	"	mcr	p15, 0, %0, c1, c0, 0\n"
	"	mrc	p15, 0, %0, c1, c0, 1\n"
	"	orr	%0, %0, %2\n"
	"	mcr	p15, 0, %0, c1, c0, 1\n"
	  : "=&r" (v)
	  : "Ir" (CR_C), "Ir" (0x40)
	  : "cc");
}

struct check_reg_lpa {
	void __iomem	*check_reg;
	unsigned int	check_bit;
};

/*
 * List of check power domain list for LPA mode
 * These register are have to power off to enter LPA mode
 */
static struct check_reg_lpa exynos5_power_domain[] = {
	{.check_reg = EXYNOS5_GSCL_STATUS,	.check_bit = 0x7},
	{.check_reg = EXYNOS5_G3D_STATUS,	.check_bit = 0x7},
};

/*
 * List of check clock gating list for LPA mode
 * If clock of list is not gated, system can not enter LPA mode.
 */
static struct check_reg_lpa exynos5_clock_gating[] = {
	{.check_reg = EXYNOS5_CLKSRC_MASK_DISP1_0,	.check_bit = 0x00000001},
	{.check_reg = EXYNOS5_CLKGATE_IP_DISP1,		.check_bit = 0x00000008},
	{.check_reg = EXYNOS5_CLKGATE_IP_MFC,		.check_bit = 0x00000001},
	{.check_reg = EXYNOS5_CLKGATE_IP_GEN,		.check_bit = 0x00004016},
	{.check_reg = EXYNOS5_CLKGATE_IP_FSYS,		.check_bit = 0x00000002},
	{.check_reg = EXYNOS5_CLKGATE_IP_PERIC,		.check_bit = 0x00377FC0},
};

static int exynos_check_reg_status(struct check_reg_lpa *reg_list,
				    unsigned int list_cnt)
{
	unsigned int i;
	unsigned int tmp;

	for (i = 0; i < list_cnt; i++) {
		tmp = __raw_readl(reg_list[i].check_reg);
		if (tmp & reg_list[i].check_bit)
			return -EBUSY;
	}

	return 0;
}

static int exynos_uart_fifo_check(void)
{
	int ret;
	unsigned int check_val;

	/* Check UART for console is empty */
	check_val = __raw_readl(S5P_VA_UART(CONFIG_S3C_LOWLEVEL_UART_PORT) +
				S3C2410_UFSTAT);

	ret = ((check_val >> 16) & 0xff);

	return ret;
}

static int __maybe_unused exynos_check_enter_mode(void)
{
	/* Check power domain */
	if (exynos_check_reg_status(exynos5_power_domain,
				    ARRAY_SIZE(exynos5_power_domain)))
		return EXYNOS_CHECK_DIDLE;

	/* Check clock gating */
	if (exynos_check_reg_status(exynos5_clock_gating,
				    ARRAY_SIZE(exynos5_clock_gating)))
		return EXYNOS_CHECK_DIDLE;

	return EXYNOS_CHECK_LPA;
}

static struct cpuidle_state exynos_cpuidle_set[] __initdata = {
	[0] = {
		.enter			= exynos_enter_idle,
		.exit_latency		= 10,
		.target_residency	= 10,
		.flags			= CPUIDLE_FLAG_TIME_VALID,
		.name			= "C0",
		.desc			= "ARM clock gating(WFI)",
	},
	[1] = {
		.enter			= exynos_enter_lowpower,
		.exit_latency		= 300,
		.target_residency	= 1000,
		.flags			= CPUIDLE_FLAG_TIME_VALID |
					  CPUIDLE_FLAG_COUPLED,
		.name			= "C1",
		.desc			= "ARM power down",
	},
};

static DEFINE_PER_CPU(struct cpuidle_device, exynos_cpuidle_device);

static struct cpuidle_driver exynos_idle_driver = {
	.name		= "exynos_idle",
	.owner		= THIS_MODULE,
	.en_core_tk_irqen = true,
};

/* Ext-GIC nIRQ/nFIQ is the only wakeup source in AFTR */
static void exynos_set_wakeupmask(void)
{
	__raw_writel(0x0000ff3e, EXYNOS_WAKEUP_MASK);
}

static void save_cpu_arch_register(void)
{
}

static void restore_cpu_arch_register(void)
{
}

static int idle_finisher(unsigned long flags)
{
#if defined(CONFIG_ARM_TRUSTZONE)
	exynos_smc(SMC_CMD_CPU0AFTR, 0, 0, 0);
#else
	cpu_do_idle();
#endif
	return 1;
}

static int exynos_enter_core0(enum sys_powerdown powerdown)
{
	unsigned long tmp;

	exynos_sys_powerdown_conf(powerdown);

	__raw_writel(virt_to_phys(s3c_cpu_resume), REG_DIRECTGO_ADDR);
	__raw_writel(EXYNOS_CHECK_DIRECTGO, REG_DIRECTGO_FLAG);

	save_cpu_arch_register();

	/* Setting Central Sequence Register for power down mode */
	tmp = __raw_readl(EXYNOS_CENTRAL_SEQ_CONFIGURATION);
	tmp &= ~EXYNOS_CENTRAL_LOWPWR_CFG;
	__raw_writel(tmp, EXYNOS_CENTRAL_SEQ_CONFIGURATION);

	if (cpu_pm_enter())
		goto abort_cpu;

	cpu_suspend(0, idle_finisher);

#ifdef CONFIG_SMP
#if !defined(CONFIG_ARM_TRUSTZONE)
	if (!soc_is_exynos5250())
		scu_enable(S5P_VA_SCU);
#endif
#endif

	cpu_pm_exit();
abort_cpu:

	restore_cpu_arch_register();

	/*
	 * If PMU failed while entering sleep mode, WFI will be
	 * ignored by PMU and then exiting cpu_do_idle().
	 * S5P_CENTRAL_LOWPWR_CFG bit will not be set automatically
	 * in this situation.
	 */
	tmp = __raw_readl(EXYNOS_CENTRAL_SEQ_CONFIGURATION);
	if (!(tmp & EXYNOS_CENTRAL_LOWPWR_CFG)) {
		tmp |= EXYNOS_CENTRAL_LOWPWR_CFG;
		__raw_writel(tmp, EXYNOS_CENTRAL_SEQ_CONFIGURATION);
	}

	return 0;
}

static int exynos_enter_core0_aftr(struct cpuidle_device *dev,
				   struct cpuidle_driver *drv,
				   int index)
{
	exynos_set_wakeupmask();

	exynos_enter_core0(SYS_AFTR);

	/* Clear wakeup state register */
	__raw_writel(0x0, EXYNOS_WAKEUP_STAT);

	return index;
}

static int exynos_enter_core0_lpa(struct cpuidle_device *dev,
				  struct cpuidle_driver *drv,
				  int index)
{
	/*
	 * Unmasking all wakeup source.
	 */
	__raw_writel(0x0, EXYNOS_WAKEUP_MASK);

	do {
		/* Waiting for flushing UART fifo */
	} while (exynos_uart_fifo_check());

	exynos_enter_core0(SYS_LPA);

	/* For release retention */
	__raw_writel((1 << 28), EXYNOS_PAD_RET_GPIO_OPTION);
	__raw_writel((1 << 28), EXYNOS_PAD_RET_UART_OPTION);
	__raw_writel((1 << 28), EXYNOS_PAD_RET_MMCA_OPTION);
	__raw_writel((1 << 28), EXYNOS_PAD_RET_MMCB_OPTION);
	__raw_writel((1 << 28), EXYNOS_PAD_RET_EBIA_OPTION);
	__raw_writel((1 << 28), EXYNOS_PAD_RET_EBIB_OPTION);

	/* Clear wakeup state register */
	__raw_writel(0x0, EXYNOS_WAKEUP_STAT);

	return index;
}

static int exynos_enter_idle(struct cpuidle_device *dev,
				struct cpuidle_driver *drv,
				int index)
{
	struct timeval before, after;
	int idle_time;

	local_irq_disable();
	do_gettimeofday(&before);

	cpu_do_idle();

	do_gettimeofday(&after);
	local_irq_enable();
	idle_time = (after.tv_sec - before.tv_sec) * USEC_PER_SEC +
		    (after.tv_usec - before.tv_usec);

	dev->last_residency = idle_time;
	return index;
}

void exynos_nop(void *info)
{
}

static int exynos_enter_lowpower(struct cpuidle_device *dev,
				struct cpuidle_driver *drv,
				int index)
{
	int ret = index;

	__raw_writel(virt_to_phys(s3c_cpu_resume), BOOT_VECTOR);
	cpuidle_coupled_parallel_barrier(dev, &exynos_idle_barrier);

	if (!allow_coupled_idle) {
		exynos_enter_idle(dev, drv, dev->safe_state_index);
		smp_call_function_single(!dev->cpu, exynos_nop, NULL, 0);
		return index;
	}

	/* Both cpus will reach this point at the same time */
	if (dev->cpu == 0) {
		/* Idle sequence for cpu 0 */
		if (cpu_online(1)) {
			/* Wait for cpu1 to turn itself off */
			while (__raw_readl(EXYNOS_ARM_CORE1_STATUS) & 3) {
				/* cpu1 may skip idle and boot back up again */
				if (cpu1_abort)
					goto abort;

				/*
				 * cpu1 may bounce through idle and boot back up
				 * again, getting stuck in the boot rom code
				 */
				if (__raw_readl(BOOT_VECTOR) == 0)
					goto abort;

				cpu_relax();
			}
		}

		/* Enter the final low power state */
		if (exynos_check_enter_mode() == EXYNOS_CHECK_DIDLE)
			ret = exynos_enter_core0_aftr(dev, drv, index);
		else
			ret = exynos_enter_core0_lpa(dev, drv, index);

abort:
		if (cpu_online(1)) {
			/* Set the boot vector to something non-zero */
			__raw_writel(virt_to_phys(s3c_cpu_resume),
				BOOT_VECTOR);
			dsb();

			/* Turn on cpu1 and wait for it to be on */
			__raw_writel(0x3, EXYNOS_ARM_CORE1_CONFIGURATION);
			while ((__raw_readl(EXYNOS_ARM_CORE1_STATUS) & 3) != 3)
				cpu_relax();

#ifdef CONFIG_ARM_TRUSTZONE
			exynos_smc(SMC_CMD_CPU1BOOT, 0, 0, 0);
#endif

			/* Wait for cpu1 to get stuck in the boot rom */
			while ((__raw_readl(BOOT_VECTOR) != 0) && !cpu1_abort)
				cpu_relax();

			if (!cpu1_abort) {
				/* Poke cpu1 out of the boot rom */
				__raw_writel(virt_to_phys(s3c_cpu_resume),
					BOOT_VECTOR);
				dsb_sev();
			}

			/* Wait for cpu1 to finish booting */
			while (!cpu1_abort)
				cpu_relax();
		}
	} else {
		/* Idle sequence for cpu 1 */

		/*
		 * Turn off localtimer to prevent ticks from waking up cpu 1
		 * before cpu 0.
		 */
		clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_ENTER, &dev->cpu);

		if (cpu_pm_enter())
			goto cpu1_aborted;

		cpu_enter_lowpower_a15();

		/* Turn off cpu 1 */
		__raw_writel(0, EXYNOS_ARM_CORE1_CONFIGURATION);
		cpu_suspend(0, idle_finisher);

		cpu_leave_lowpower();

		cpu_pm_exit();
		clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_EXIT, &dev->cpu);

cpu1_aborted:
		/* Notify cpu 0 that cpu 1 is awake */
		dsb();
		cpu1_abort = true;
	}

	cpuidle_coupled_parallel_barrier(dev, &exynos_idle_barrier);

	cpu1_abort = false;

	return ret;
}


static void __setup_broadcast_timer(void *arg)
{
	unsigned long reason = (unsigned long)arg;
	int cpu = smp_processor_id();

	reason = reason ?
		CLOCK_EVT_NOTIFY_BROADCAST_ON : CLOCK_EVT_NOTIFY_BROADCAST_OFF;

	clockevents_notify(reason, &cpu);
}

static int setup_broadcast_cpuhp_notify(struct notifier_block *n,
		unsigned long action, void *hcpu)
{
	int hotcpu = (unsigned long)hcpu;

	switch (action & 0xf) {
	case CPU_ONLINE:
		smp_call_function_single(hotcpu, __setup_broadcast_timer,
			(void *)true, 1);
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block setup_broadcast_notifier = {
	.notifier_call = setup_broadcast_cpuhp_notify,
};

static void __init exynos5_core_down_clk(void)
{
	unsigned int tmp;

	tmp = __raw_readl(EXYNOS5_PWR_CTRL1);

	tmp &= ~(PWR_CTRL1_CORE2_DOWN_MASK | PWR_CTRL1_CORE1_DOWN_MASK);

	/* set arm clock divider value on idle state */
	tmp |= ((0x7 << PWR_CTRL1_CORE2_DOWN_RATIO) |
		(0x7 << PWR_CTRL1_CORE1_DOWN_RATIO));

	tmp |= (PWR_CTRL1_DIV2_DOWN_EN |
		PWR_CTRL1_DIV1_DOWN_EN |
		PWR_CTRL1_USE_CORE1_WFE |
		PWR_CTRL1_USE_CORE0_WFE |
		PWR_CTRL1_USE_CORE1_WFI |
		PWR_CTRL1_USE_CORE0_WFI);

	__raw_writel(tmp, EXYNOS5_PWR_CTRL1);

	tmp = __raw_readl(EXYNOS5_PWR_CTRL2);

	tmp &= ~(PWR_CTRL2_DUR_STANDBY2_MASK | PWR_CTRL2_DUR_STANDBY1_MASK |
		PWR_CTRL2_CORE2_UP_MASK | PWR_CTRL2_CORE1_UP_MASK);

	/* set duration value on middle wakeup step */
	tmp |=  ((0x1 << PWR_CTRL2_DUR_STANDBY2) |
		 (0x1 << PWR_CTRL2_DUR_STANDBY1));

	/* set arm clock divier value on middle wakeup step */
	tmp |= ((0x1 << PWR_CTRL2_CORE2_UP_RATIO) |
		(0x1 << PWR_CTRL2_CORE1_UP_RATIO));

	/* Set PWR_CTRL2 register to use step up for arm clock */
	tmp |= (PWR_CTRL2_DIV2_UP_EN | PWR_CTRL2_DIV1_UP_EN);

	__raw_writel(tmp, EXYNOS5_PWR_CTRL2);
	pr_info("Exynos5 : ARM Clock down on idle mode is enabled\n");
}

static int __init exynos_init_cpuidle(void)
{
	int ret;
	int i, max_cpuidle_state, cpu_id;
	struct cpuidle_device *device;
	struct cpuidle_driver *drv = &exynos_idle_driver;

	exynos5_core_down_clk();

	/* Setup cpuidle driver */
	drv->state_count = ARRAY_SIZE(exynos_cpuidle_set);

	max_cpuidle_state = drv->state_count;
	for (i = 0; i < max_cpuidle_state; i++) {
		memcpy(&drv->states[i], &exynos_cpuidle_set[i],
				sizeof(struct cpuidle_state));
	}
	drv->safe_state_index = 0;
	cpuidle_register_driver(&exynos_idle_driver);

	on_each_cpu(__setup_broadcast_timer, (void *)true, 1);
	ret = register_cpu_notifier(&setup_broadcast_notifier);
	if (ret)
		pr_err("%s: failed to register cpu notifier\n", __func__);

	for_each_cpu(cpu_id, cpu_online_mask) {
		device = &per_cpu(exynos_cpuidle_device, cpu_id);
		device->cpu = cpu_id;

		device->state_count = ARRAY_SIZE(exynos_cpuidle_set);
		device->coupled_cpus = *cpu_possible_mask;

		if (cpuidle_register_device(device)) {
			printk(KERN_ERR "CPUidle register device failed\n,");
			return -EIO;
		}
	}

	return 0;
}
device_initcall(exynos_init_cpuidle);
