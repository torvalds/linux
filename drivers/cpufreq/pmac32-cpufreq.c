/*
 *  Copyright (C) 2002 - 2005 Benjamin Herrenschmidt <benh@kernel.crashing.org>
 *  Copyright (C) 2004        John Steele Scott <toojays@toojays.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * TODO: Need a big cleanup here. Basically, we need to have different
 * cpufreq_driver structures for the different type of HW instead of the
 * current mess. We also need to better deal with the detection of the
 * type of machine.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/adb.h>
#include <linux/pmu.h>
#include <linux/cpufreq.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/hardirq.h>
#include <linux/of_device.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/irq.h>
#include <asm/pmac_feature.h>
#include <asm/mmu_context.h>
#include <asm/sections.h>
#include <asm/cputable.h>
#include <asm/time.h>
#include <asm/mpic.h>
#include <asm/keylargo.h>
#include <asm/switch_to.h>

/* WARNING !!! This will cause calibrate_delay() to be called,
 * but this is an __init function ! So you MUST go edit
 * init/main.c to make it non-init before enabling DEBUG_FREQ
 */
#undef DEBUG_FREQ

extern void low_choose_7447a_dfs(int dfs);
extern void low_choose_750fx_pll(int pll);
extern void low_sleep_handler(void);

/*
 * Currently, PowerMac cpufreq supports only high & low frequencies
 * that are set by the firmware
 */
static unsigned int low_freq;
static unsigned int hi_freq;
static unsigned int cur_freq;
static unsigned int sleep_freq;
static unsigned long transition_latency;

/*
 * Different models uses different mechanisms to switch the frequency
 */
static int (*set_speed_proc)(int low_speed);
static unsigned int (*get_speed_proc)(void);

/*
 * Some definitions used by the various speedprocs
 */
static u32 voltage_gpio;
static u32 frequency_gpio;
static u32 slew_done_gpio;
static int no_schedule;
static int has_cpu_l2lve;
static int is_pmu_based;

/* There are only two frequency states for each processor. Values
 * are in kHz for the time being.
 */
#define CPUFREQ_HIGH                  0
#define CPUFREQ_LOW                   1

static struct cpufreq_frequency_table pmac_cpu_freqs[] = {
	{0, CPUFREQ_HIGH,	0},
	{0, CPUFREQ_LOW,	0},
	{0, 0,			CPUFREQ_TABLE_END},
};

static inline void local_delay(unsigned long ms)
{
	if (no_schedule)
		mdelay(ms);
	else
		msleep(ms);
}

#ifdef DEBUG_FREQ
static inline void debug_calc_bogomips(void)
{
	/* This will cause a recalc of bogomips and display the
	 * result. We backup/restore the value to avoid affecting the
	 * core cpufreq framework's own calculation.
	 */
	unsigned long save_lpj = loops_per_jiffy;
	calibrate_delay();
	loops_per_jiffy = save_lpj;
}
#endif /* DEBUG_FREQ */

/* Switch CPU speed under 750FX CPU control
 */
static int cpu_750fx_cpu_speed(int low_speed)
{
	u32 hid2;

	if (low_speed == 0) {
		/* ramping up, set voltage first */
		pmac_call_feature(PMAC_FTR_WRITE_GPIO, NULL, voltage_gpio, 0x05);
		/* Make sure we sleep for at least 1ms */
		local_delay(10);

		/* tweak L2 for high voltage */
		if (has_cpu_l2lve) {
			hid2 = mfspr(SPRN_HID2);
			hid2 &= ~0x2000;
			mtspr(SPRN_HID2, hid2);
		}
	}
#ifdef CONFIG_6xx
	low_choose_750fx_pll(low_speed);
#endif
	if (low_speed == 1) {
		/* tweak L2 for low voltage */
		if (has_cpu_l2lve) {
			hid2 = mfspr(SPRN_HID2);
			hid2 |= 0x2000;
			mtspr(SPRN_HID2, hid2);
		}

		/* ramping down, set voltage last */
		pmac_call_feature(PMAC_FTR_WRITE_GPIO, NULL, voltage_gpio, 0x04);
		local_delay(10);
	}

	return 0;
}

static unsigned int cpu_750fx_get_cpu_speed(void)
{
	if (mfspr(SPRN_HID1) & HID1_PS)
		return low_freq;
	else
		return hi_freq;
}

/* Switch CPU speed using DFS */
static int dfs_set_cpu_speed(int low_speed)
{
	if (low_speed == 0) {
		/* ramping up, set voltage first */
		pmac_call_feature(PMAC_FTR_WRITE_GPIO, NULL, voltage_gpio, 0x05);
		/* Make sure we sleep for at least 1ms */
		local_delay(1);
	}

	/* set frequency */
#ifdef CONFIG_6xx
	low_choose_7447a_dfs(low_speed);
#endif
	udelay(100);

	if (low_speed == 1) {
		/* ramping down, set voltage last */
		pmac_call_feature(PMAC_FTR_WRITE_GPIO, NULL, voltage_gpio, 0x04);
		local_delay(1);
	}

	return 0;
}

static unsigned int dfs_get_cpu_speed(void)
{
	if (mfspr(SPRN_HID1) & HID1_DFS)
		return low_freq;
	else
		return hi_freq;
}


/* Switch CPU speed using slewing GPIOs
 */
static int gpios_set_cpu_speed(int low_speed)
{
	int gpio, timeout = 0;

	/* If ramping up, set voltage first */
	if (low_speed == 0) {
		pmac_call_feature(PMAC_FTR_WRITE_GPIO, NULL, voltage_gpio, 0x05);
		/* Delay is way too big but it's ok, we schedule */
		local_delay(10);
	}

	/* Set frequency */
	gpio = 	pmac_call_feature(PMAC_FTR_READ_GPIO, NULL, frequency_gpio, 0);
	if (low_speed == ((gpio & 0x01) == 0))
		goto skip;

	pmac_call_feature(PMAC_FTR_WRITE_GPIO, NULL, frequency_gpio,
			  low_speed ? 0x04 : 0x05);
	udelay(200);
	do {
		if (++timeout > 100)
			break;
		local_delay(1);
		gpio = pmac_call_feature(PMAC_FTR_READ_GPIO, NULL, slew_done_gpio, 0);
	} while((gpio & 0x02) == 0);
 skip:
	/* If ramping down, set voltage last */
	if (low_speed == 1) {
		pmac_call_feature(PMAC_FTR_WRITE_GPIO, NULL, voltage_gpio, 0x04);
		/* Delay is way too big but it's ok, we schedule */
		local_delay(10);
	}

#ifdef DEBUG_FREQ
	debug_calc_bogomips();
#endif

	return 0;
}

/* Switch CPU speed under PMU control
 */
static int pmu_set_cpu_speed(int low_speed)
{
	struct adb_request req;
	unsigned long save_l2cr;
	unsigned long save_l3cr;
	unsigned int pic_prio;
	unsigned long flags;

	preempt_disable();

#ifdef DEBUG_FREQ
	printk(KERN_DEBUG "HID1, before: %x\n", mfspr(SPRN_HID1));
#endif
	pmu_suspend();

	/* Disable all interrupt sources on openpic */
 	pic_prio = mpic_cpu_get_priority();
	mpic_cpu_set_priority(0xf);

	/* Make sure the decrementer won't interrupt us */
	asm volatile("mtdec %0" : : "r" (0x7fffffff));
	/* Make sure any pending DEC interrupt occurring while we did
	 * the above didn't re-enable the DEC */
	mb();
	asm volatile("mtdec %0" : : "r" (0x7fffffff));

	/* We can now disable MSR_EE */
	local_irq_save(flags);

	/* Giveup the FPU & vec */
	enable_kernel_fp();

#ifdef CONFIG_ALTIVEC
	if (cpu_has_feature(CPU_FTR_ALTIVEC))
		enable_kernel_altivec();
#endif /* CONFIG_ALTIVEC */

	/* Save & disable L2 and L3 caches */
	save_l3cr = _get_L3CR();	/* (returns -1 if not available) */
	save_l2cr = _get_L2CR();	/* (returns -1 if not available) */

	/* Send the new speed command. My assumption is that this command
	 * will cause PLL_CFG[0..3] to be changed next time CPU goes to sleep
	 */
	pmu_request(&req, NULL, 6, PMU_CPU_SPEED, 'W', 'O', 'O', 'F', low_speed);
	while (!req.complete)
		pmu_poll();

	/* Prepare the northbridge for the speed transition */
	pmac_call_feature(PMAC_FTR_SLEEP_STATE,NULL,1,1);

	/* Call low level code to backup CPU state and recover from
	 * hardware reset
	 */
	low_sleep_handler();

	/* Restore the northbridge */
	pmac_call_feature(PMAC_FTR_SLEEP_STATE,NULL,1,0);

	/* Restore L2 cache */
	if (save_l2cr != 0xffffffff && (save_l2cr & L2CR_L2E) != 0)
 		_set_L2CR(save_l2cr);
	/* Restore L3 cache */
	if (save_l3cr != 0xffffffff && (save_l3cr & L3CR_L3E) != 0)
 		_set_L3CR(save_l3cr);

	/* Restore userland MMU context */
	switch_mmu_context(NULL, current->active_mm, NULL);

#ifdef DEBUG_FREQ
	printk(KERN_DEBUG "HID1, after: %x\n", mfspr(SPRN_HID1));
#endif

	/* Restore low level PMU operations */
	pmu_unlock();

	/*
	 * Restore decrementer; we'll take a decrementer interrupt
	 * as soon as interrupts are re-enabled and the generic
	 * clockevents code will reprogram it with the right value.
	 */
	set_dec(1);

	/* Restore interrupts */
 	mpic_cpu_set_priority(pic_prio);

	/* Let interrupts flow again ... */
	local_irq_restore(flags);

#ifdef DEBUG_FREQ
	debug_calc_bogomips();
#endif

	pmu_resume();

	preempt_enable();

	return 0;
}

static int do_set_cpu_speed(struct cpufreq_policy *policy, int speed_mode)
{
	unsigned long l3cr;
	static unsigned long prev_l3cr;

	if (speed_mode == CPUFREQ_LOW &&
	    cpu_has_feature(CPU_FTR_L3CR)) {
		l3cr = _get_L3CR();
		if (l3cr & L3CR_L3E) {
			prev_l3cr = l3cr;
			_set_L3CR(0);
		}
	}
	set_speed_proc(speed_mode == CPUFREQ_LOW);
	if (speed_mode == CPUFREQ_HIGH &&
	    cpu_has_feature(CPU_FTR_L3CR)) {
		l3cr = _get_L3CR();
		if ((prev_l3cr & L3CR_L3E) && l3cr != prev_l3cr)
			_set_L3CR(prev_l3cr);
	}
	cur_freq = (speed_mode == CPUFREQ_HIGH) ? hi_freq : low_freq;

	return 0;
}

static unsigned int pmac_cpufreq_get_speed(unsigned int cpu)
{
	return cur_freq;
}

static int pmac_cpufreq_target(	struct cpufreq_policy *policy,
					unsigned int index)
{
	int		rc;

	rc = do_set_cpu_speed(policy, index);

	ppc_proc_freq = cur_freq * 1000ul;
	return rc;
}

static int pmac_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	return cpufreq_generic_init(policy, pmac_cpu_freqs, transition_latency);
}

static u32 read_gpio(struct device_node *np)
{
	const u32 *reg = of_get_property(np, "reg", NULL);
	u32 offset;

	if (reg == NULL)
		return 0;
	/* That works for all keylargos but shall be fixed properly
	 * some day... The problem is that it seems we can't rely
	 * on the "reg" property of the GPIO nodes, they are either
	 * relative to the base of KeyLargo or to the base of the
	 * GPIO space, and the device-tree doesn't help.
	 */
	offset = *reg;
	if (offset < KEYLARGO_GPIO_LEVELS0)
		offset += KEYLARGO_GPIO_LEVELS0;
	return offset;
}

static int pmac_cpufreq_suspend(struct cpufreq_policy *policy)
{
	/* Ok, this could be made a bit smarter, but let's be robust for now. We
	 * always force a speed change to high speed before sleep, to make sure
	 * we have appropriate voltage and/or bus speed for the wakeup process,
	 * and to make sure our loops_per_jiffies are "good enough", that is will
	 * not cause too short delays if we sleep in low speed and wake in high
	 * speed..
	 */
	no_schedule = 1;
	sleep_freq = cur_freq;
	if (cur_freq == low_freq && !is_pmu_based)
		do_set_cpu_speed(policy, CPUFREQ_HIGH);
	return 0;
}

static int pmac_cpufreq_resume(struct cpufreq_policy *policy)
{
	/* If we resume, first check if we have a get() function */
	if (get_speed_proc)
		cur_freq = get_speed_proc();
	else
		cur_freq = 0;

	/* We don't, hrm... we don't really know our speed here, best
	 * is that we force a switch to whatever it was, which is
	 * probably high speed due to our suspend() routine
	 */
	do_set_cpu_speed(policy, sleep_freq == low_freq ?
			 CPUFREQ_LOW : CPUFREQ_HIGH);

	ppc_proc_freq = cur_freq * 1000ul;

	no_schedule = 0;
	return 0;
}

static struct cpufreq_driver pmac_cpufreq_driver = {
	.verify 	= cpufreq_generic_frequency_table_verify,
	.target_index	= pmac_cpufreq_target,
	.get		= pmac_cpufreq_get_speed,
	.init		= pmac_cpufreq_cpu_init,
	.suspend	= pmac_cpufreq_suspend,
	.resume		= pmac_cpufreq_resume,
	.flags		= CPUFREQ_PM_NO_WARN |
			  CPUFREQ_NO_AUTO_DYNAMIC_SWITCHING,
	.attr		= cpufreq_generic_attr,
	.name		= "powermac",
};


static int pmac_cpufreq_init_MacRISC3(struct device_node *cpunode)
{
	struct device_node *volt_gpio_np = of_find_node_by_name(NULL,
								"voltage-gpio");
	struct device_node *freq_gpio_np = of_find_node_by_name(NULL,
								"frequency-gpio");
	struct device_node *slew_done_gpio_np = of_find_node_by_name(NULL,
								     "slewing-done");
	const u32 *value;

	/*
	 * Check to see if it's GPIO driven or PMU only
	 *
	 * The way we extract the GPIO address is slightly hackish, but it
	 * works well enough for now. We need to abstract the whole GPIO
	 * stuff sooner or later anyway
	 */

	if (volt_gpio_np)
		voltage_gpio = read_gpio(volt_gpio_np);
	if (freq_gpio_np)
		frequency_gpio = read_gpio(freq_gpio_np);
	if (slew_done_gpio_np)
		slew_done_gpio = read_gpio(slew_done_gpio_np);

	/* If we use the frequency GPIOs, calculate the min/max speeds based
	 * on the bus frequencies
	 */
	if (frequency_gpio && slew_done_gpio) {
		int lenp, rc;
		const u32 *freqs, *ratio;

		freqs = of_get_property(cpunode, "bus-frequencies", &lenp);
		lenp /= sizeof(u32);
		if (freqs == NULL || lenp != 2) {
			pr_err("bus-frequencies incorrect or missing\n");
			return 1;
		}
		ratio = of_get_property(cpunode, "processor-to-bus-ratio*2",
						NULL);
		if (ratio == NULL) {
			pr_err("processor-to-bus-ratio*2 missing\n");
			return 1;
		}

		/* Get the min/max bus frequencies */
		low_freq = min(freqs[0], freqs[1]);
		hi_freq = max(freqs[0], freqs[1]);

		/* Grrrr.. It _seems_ that the device-tree is lying on the low bus
		 * frequency, it claims it to be around 84Mhz on some models while
		 * it appears to be approx. 101Mhz on all. Let's hack around here...
		 * fortunately, we don't need to be too precise
		 */
		if (low_freq < 98000000)
			low_freq = 101000000;

		/* Convert those to CPU core clocks */
		low_freq = (low_freq * (*ratio)) / 2000;
		hi_freq = (hi_freq * (*ratio)) / 2000;

		/* Now we get the frequencies, we read the GPIO to see what is out current
		 * speed
		 */
		rc = pmac_call_feature(PMAC_FTR_READ_GPIO, NULL, frequency_gpio, 0);
		cur_freq = (rc & 0x01) ? hi_freq : low_freq;

		set_speed_proc = gpios_set_cpu_speed;
		return 1;
	}

	/* If we use the PMU, look for the min & max frequencies in the
	 * device-tree
	 */
	value = of_get_property(cpunode, "min-clock-frequency", NULL);
	if (!value)
		return 1;
	low_freq = (*value) / 1000;
	/* The PowerBook G4 12" (PowerBook6,1) has an error in the device-tree
	 * here */
	if (low_freq < 100000)
		low_freq *= 10;

	value = of_get_property(cpunode, "max-clock-frequency", NULL);
	if (!value)
		return 1;
	hi_freq = (*value) / 1000;
	set_speed_proc = pmu_set_cpu_speed;
	is_pmu_based = 1;

	return 0;
}

static int pmac_cpufreq_init_7447A(struct device_node *cpunode)
{
	struct device_node *volt_gpio_np;

	if (of_get_property(cpunode, "dynamic-power-step", NULL) == NULL)
		return 1;

	volt_gpio_np = of_find_node_by_name(NULL, "cpu-vcore-select");
	if (volt_gpio_np)
		voltage_gpio = read_gpio(volt_gpio_np);
	of_node_put(volt_gpio_np);
	if (!voltage_gpio){
		pr_err("missing cpu-vcore-select gpio\n");
		return 1;
	}

	/* OF only reports the high frequency */
	hi_freq = cur_freq;
	low_freq = cur_freq/2;

	/* Read actual frequency from CPU */
	cur_freq = dfs_get_cpu_speed();
	set_speed_proc = dfs_set_cpu_speed;
	get_speed_proc = dfs_get_cpu_speed;

	return 0;
}

static int pmac_cpufreq_init_750FX(struct device_node *cpunode)
{
	struct device_node *volt_gpio_np;
	u32 pvr;
	const u32 *value;

	if (of_get_property(cpunode, "dynamic-power-step", NULL) == NULL)
		return 1;

	hi_freq = cur_freq;
	value = of_get_property(cpunode, "reduced-clock-frequency", NULL);
	if (!value)
		return 1;
	low_freq = (*value) / 1000;

	volt_gpio_np = of_find_node_by_name(NULL, "cpu-vcore-select");
	if (volt_gpio_np)
		voltage_gpio = read_gpio(volt_gpio_np);

	of_node_put(volt_gpio_np);
	pvr = mfspr(SPRN_PVR);
	has_cpu_l2lve = !((pvr & 0xf00) == 0x100);

	set_speed_proc = cpu_750fx_cpu_speed;
	get_speed_proc = cpu_750fx_get_cpu_speed;
	cur_freq = cpu_750fx_get_cpu_speed();

	return 0;
}

/* Currently, we support the following machines:
 *
 *  - Titanium PowerBook 1Ghz (PMU based, 667Mhz & 1Ghz)
 *  - Titanium PowerBook 800 (PMU based, 667Mhz & 800Mhz)
 *  - Titanium PowerBook 400 (PMU based, 300Mhz & 400Mhz)
 *  - Titanium PowerBook 500 (PMU based, 300Mhz & 500Mhz)
 *  - iBook2 500/600 (PMU based, 400Mhz & 500/600Mhz)
 *  - iBook2 700 (CPU based, 400Mhz & 700Mhz, support low voltage)
 *  - Recent MacRISC3 laptops
 *  - All new machines with 7447A CPUs
 */
static int __init pmac_cpufreq_setup(void)
{
	struct device_node	*cpunode;
	const u32		*value;

	if (strstr(boot_command_line, "nocpufreq"))
		return 0;

	/* Get first CPU node */
	cpunode = of_cpu_device_node_get(0);
	if (!cpunode)
		goto out;

	/* Get current cpu clock freq */
	value = of_get_property(cpunode, "clock-frequency", NULL);
	if (!value)
		goto out;
	cur_freq = (*value) / 1000;

	/*  Check for 7447A based MacRISC3 */
	if (of_machine_is_compatible("MacRISC3") &&
	    of_get_property(cpunode, "dynamic-power-step", NULL) &&
	    PVR_VER(mfspr(SPRN_PVR)) == 0x8003) {
		pmac_cpufreq_init_7447A(cpunode);

		/* Allow dynamic switching */
		transition_latency = 8000000;
		pmac_cpufreq_driver.flags &= ~CPUFREQ_NO_AUTO_DYNAMIC_SWITCHING;
	/* Check for other MacRISC3 machines */
	} else if (of_machine_is_compatible("PowerBook3,4") ||
		   of_machine_is_compatible("PowerBook3,5") ||
		   of_machine_is_compatible("MacRISC3")) {
		pmac_cpufreq_init_MacRISC3(cpunode);
	/* Else check for iBook2 500/600 */
	} else if (of_machine_is_compatible("PowerBook4,1")) {
		hi_freq = cur_freq;
		low_freq = 400000;
		set_speed_proc = pmu_set_cpu_speed;
		is_pmu_based = 1;
	}
	/* Else check for TiPb 550 */
	else if (of_machine_is_compatible("PowerBook3,3") && cur_freq == 550000) {
		hi_freq = cur_freq;
		low_freq = 500000;
		set_speed_proc = pmu_set_cpu_speed;
		is_pmu_based = 1;
	}
	/* Else check for TiPb 400 & 500 */
	else if (of_machine_is_compatible("PowerBook3,2")) {
		/* We only know about the 400 MHz and the 500Mhz model
		 * they both have 300 MHz as low frequency
		 */
		if (cur_freq < 350000 || cur_freq > 550000)
			goto out;
		hi_freq = cur_freq;
		low_freq = 300000;
		set_speed_proc = pmu_set_cpu_speed;
		is_pmu_based = 1;
	}
	/* Else check for 750FX */
	else if (PVR_VER(mfspr(SPRN_PVR)) == 0x7000)
		pmac_cpufreq_init_750FX(cpunode);
out:
	of_node_put(cpunode);
	if (set_speed_proc == NULL)
		return -ENODEV;

	pmac_cpu_freqs[CPUFREQ_LOW].frequency = low_freq;
	pmac_cpu_freqs[CPUFREQ_HIGH].frequency = hi_freq;
	ppc_proc_freq = cur_freq * 1000ul;

	pr_info("Registering PowerMac CPU frequency driver\n");
	pr_info("Low: %d Mhz, High: %d Mhz, Boot: %d Mhz\n",
		low_freq/1000, hi_freq/1000, cur_freq/1000);

	return cpufreq_register_driver(&pmac_cpufreq_driver);
}

module_init(pmac_cpufreq_setup);

