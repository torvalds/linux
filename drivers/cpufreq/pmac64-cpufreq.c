/*
 *  Copyright (C) 2002 - 2005 Benjamin Herrenschmidt <benh@kernel.crashing.org>
 *  and                       Markus Demleitner <msdemlei@cl.uni-heidelberg.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This driver adds basic cpufreq support for SMU & 970FX based G5 Macs,
 * that is iMac G5 and latest single CPU desktop.
 */

#undef DEBUG

#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/cpufreq.h>
#include <linux/init.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/irq.h>
#include <asm/sections.h>
#include <asm/cputable.h>
#include <asm/time.h>
#include <asm/smu.h>
#include <asm/pmac_pfunc.h>

#define DBG(fmt...) pr_debug(fmt)

/* see 970FX user manual */

#define SCOM_PCR 0x0aa001			/* PCR scom addr */

#define PCR_HILO_SELECT		0x80000000U	/* 1 = PCR, 0 = PCRH */
#define PCR_SPEED_FULL		0x00000000U	/* 1:1 speed value */
#define PCR_SPEED_HALF		0x00020000U	/* 1:2 speed value */
#define PCR_SPEED_QUARTER	0x00040000U	/* 1:4 speed value */
#define PCR_SPEED_MASK		0x000e0000U	/* speed mask */
#define PCR_SPEED_SHIFT		17
#define PCR_FREQ_REQ_VALID	0x00010000U	/* freq request valid */
#define PCR_VOLT_REQ_VALID	0x00008000U	/* volt request valid */
#define PCR_TARGET_TIME_MASK	0x00006000U	/* target time */
#define PCR_STATLAT_MASK	0x00001f00U	/* STATLAT value */
#define PCR_SNOOPLAT_MASK	0x000000f0U	/* SNOOPLAT value */
#define PCR_SNOOPACC_MASK	0x0000000fU	/* SNOOPACC value */

#define SCOM_PSR 0x408001			/* PSR scom addr */
/* warning: PSR is a 64 bits register */
#define PSR_CMD_RECEIVED	0x2000000000000000U   /* command received */
#define PSR_CMD_COMPLETED	0x1000000000000000U   /* command completed */
#define PSR_CUR_SPEED_MASK	0x0300000000000000U   /* current speed */
#define PSR_CUR_SPEED_SHIFT	(56)

/*
 * The G5 only supports two frequencies (Quarter speed is not supported)
 */
#define CPUFREQ_HIGH                  0
#define CPUFREQ_LOW                   1

static struct cpufreq_frequency_table g5_cpu_freqs[] = {
	{CPUFREQ_HIGH, 		0},
	{CPUFREQ_LOW,		0},
	{0,			CPUFREQ_TABLE_END},
};

/* Power mode data is an array of the 32 bits PCR values to use for
 * the various frequencies, retrieved from the device-tree
 */
static int g5_pmode_cur;

static void (*g5_switch_volt)(int speed_mode);
static int (*g5_switch_freq)(int speed_mode);
static int (*g5_query_freq)(void);

static DEFINE_MUTEX(g5_switch_mutex);

static unsigned long transition_latency;

#ifdef CONFIG_PMAC_SMU

static const u32 *g5_pmode_data;
static int g5_pmode_max;

static struct smu_sdbp_fvt *g5_fvt_table;	/* table of op. points */
static int g5_fvt_count;			/* number of op. points */
static int g5_fvt_cur;				/* current op. point */

/*
 * SMU based voltage switching for Neo2 platforms
 */

static void g5_smu_switch_volt(int speed_mode)
{
	struct smu_simple_cmd	cmd;

	DECLARE_COMPLETION_ONSTACK(comp);
	smu_queue_simple(&cmd, SMU_CMD_POWER_COMMAND, 8, smu_done_complete,
			 &comp, 'V', 'S', 'L', 'E', 'W',
			 0xff, g5_fvt_cur+1, speed_mode);
	wait_for_completion(&comp);
}

/*
 * Platform function based voltage/vdnap switching for Neo2
 */

static struct pmf_function *pfunc_set_vdnap0;
static struct pmf_function *pfunc_vdnap0_complete;

static void g5_vdnap_switch_volt(int speed_mode)
{
	struct pmf_args args;
	u32 slew, done = 0;
	unsigned long timeout;

	slew = (speed_mode == CPUFREQ_LOW) ? 1 : 0;
	args.count = 1;
	args.u[0].p = &slew;

	pmf_call_one(pfunc_set_vdnap0, &args);

	/* It's an irq GPIO so we should be able to just block here,
	 * I'll do that later after I've properly tested the IRQ code for
	 * platform functions
	 */
	timeout = jiffies + HZ/10;
	while(!time_after(jiffies, timeout)) {
		args.count = 1;
		args.u[0].p = &done;
		pmf_call_one(pfunc_vdnap0_complete, &args);
		if (done)
			break;
		usleep_range(1000, 1000);
	}
	if (done == 0)
		printk(KERN_WARNING "cpufreq: Timeout in clock slewing !\n");
}


/*
 * SCOM based frequency switching for 970FX rev3
 */
static int g5_scom_switch_freq(int speed_mode)
{
	unsigned long flags;
	int to;

	/* If frequency is going up, first ramp up the voltage */
	if (speed_mode < g5_pmode_cur)
		g5_switch_volt(speed_mode);

	local_irq_save(flags);

	/* Clear PCR high */
	scom970_write(SCOM_PCR, 0);
	/* Clear PCR low */
       	scom970_write(SCOM_PCR, PCR_HILO_SELECT | 0);
	/* Set PCR low */
	scom970_write(SCOM_PCR, PCR_HILO_SELECT |
		      g5_pmode_data[speed_mode]);

	/* Wait for completion */
	for (to = 0; to < 10; to++) {
		unsigned long psr = scom970_read(SCOM_PSR);

		if ((psr & PSR_CMD_RECEIVED) == 0 &&
		    (((psr >> PSR_CUR_SPEED_SHIFT) ^
		      (g5_pmode_data[speed_mode] >> PCR_SPEED_SHIFT)) & 0x3)
		    == 0)
			break;
		if (psr & PSR_CMD_COMPLETED)
			break;
		udelay(100);
	}

	local_irq_restore(flags);

	/* If frequency is going down, last ramp the voltage */
	if (speed_mode > g5_pmode_cur)
		g5_switch_volt(speed_mode);

	g5_pmode_cur = speed_mode;
	ppc_proc_freq = g5_cpu_freqs[speed_mode].frequency * 1000ul;

	return 0;
}

static int g5_scom_query_freq(void)
{
	unsigned long psr = scom970_read(SCOM_PSR);
	int i;

	for (i = 0; i <= g5_pmode_max; i++)
		if ((((psr >> PSR_CUR_SPEED_SHIFT) ^
		      (g5_pmode_data[i] >> PCR_SPEED_SHIFT)) & 0x3) == 0)
			break;
	return i;
}

/*
 * Fake voltage switching for platforms with missing support
 */

static void g5_dummy_switch_volt(int speed_mode)
{
}

#endif /* CONFIG_PMAC_SMU */

/*
 * Platform function based voltage switching for PowerMac7,2 & 7,3
 */

static struct pmf_function *pfunc_cpu0_volt_high;
static struct pmf_function *pfunc_cpu0_volt_low;
static struct pmf_function *pfunc_cpu1_volt_high;
static struct pmf_function *pfunc_cpu1_volt_low;

static void g5_pfunc_switch_volt(int speed_mode)
{
	if (speed_mode == CPUFREQ_HIGH) {
		if (pfunc_cpu0_volt_high)
			pmf_call_one(pfunc_cpu0_volt_high, NULL);
		if (pfunc_cpu1_volt_high)
			pmf_call_one(pfunc_cpu1_volt_high, NULL);
	} else {
		if (pfunc_cpu0_volt_low)
			pmf_call_one(pfunc_cpu0_volt_low, NULL);
		if (pfunc_cpu1_volt_low)
			pmf_call_one(pfunc_cpu1_volt_low, NULL);
	}
	usleep_range(10000, 10000); /* should be faster , to fix */
}

/*
 * Platform function based frequency switching for PowerMac7,2 & 7,3
 */

static struct pmf_function *pfunc_cpu_setfreq_high;
static struct pmf_function *pfunc_cpu_setfreq_low;
static struct pmf_function *pfunc_cpu_getfreq;
static struct pmf_function *pfunc_slewing_done;

static int g5_pfunc_switch_freq(int speed_mode)
{
	struct pmf_args args;
	u32 done = 0;
	unsigned long timeout;
	int rc;

	DBG("g5_pfunc_switch_freq(%d)\n", speed_mode);

	/* If frequency is going up, first ramp up the voltage */
	if (speed_mode < g5_pmode_cur)
		g5_switch_volt(speed_mode);

	/* Do it */
	if (speed_mode == CPUFREQ_HIGH)
		rc = pmf_call_one(pfunc_cpu_setfreq_high, NULL);
	else
		rc = pmf_call_one(pfunc_cpu_setfreq_low, NULL);

	if (rc)
		printk(KERN_WARNING "cpufreq: pfunc switch error %d\n", rc);

	/* It's an irq GPIO so we should be able to just block here,
	 * I'll do that later after I've properly tested the IRQ code for
	 * platform functions
	 */
	timeout = jiffies + HZ/10;
	while(!time_after(jiffies, timeout)) {
		args.count = 1;
		args.u[0].p = &done;
		pmf_call_one(pfunc_slewing_done, &args);
		if (done)
			break;
		usleep_range(500, 500);
	}
	if (done == 0)
		printk(KERN_WARNING "cpufreq: Timeout in clock slewing !\n");

	/* If frequency is going down, last ramp the voltage */
	if (speed_mode > g5_pmode_cur)
		g5_switch_volt(speed_mode);

	g5_pmode_cur = speed_mode;
	ppc_proc_freq = g5_cpu_freqs[speed_mode].frequency * 1000ul;

	return 0;
}

static int g5_pfunc_query_freq(void)
{
	struct pmf_args args;
	u32 val = 0;

	args.count = 1;
	args.u[0].p = &val;
	pmf_call_one(pfunc_cpu_getfreq, &args);
	return val ? CPUFREQ_HIGH : CPUFREQ_LOW;
}


/*
 * Common interface to the cpufreq core
 */

static int g5_cpufreq_target(struct cpufreq_policy *policy, unsigned int index)
{
	struct cpufreq_freqs freqs;
	int rc;

	mutex_lock(&g5_switch_mutex);

	freqs.old = g5_cpu_freqs[g5_pmode_cur].frequency;
	freqs.new = g5_cpu_freqs[index].frequency;

	cpufreq_notify_transition(policy, &freqs, CPUFREQ_PRECHANGE);
	rc = g5_switch_freq(index);
	cpufreq_notify_transition(policy, &freqs, CPUFREQ_POSTCHANGE);

	mutex_unlock(&g5_switch_mutex);

	return rc;
}

static unsigned int g5_cpufreq_get_speed(unsigned int cpu)
{
	return g5_cpu_freqs[g5_pmode_cur].frequency;
}

static int g5_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	return cpufreq_generic_init(policy, g5_cpu_freqs, transition_latency);
}

static struct cpufreq_driver g5_cpufreq_driver = {
	.name		= "powermac",
	.flags		= CPUFREQ_CONST_LOOPS,
	.init		= g5_cpufreq_cpu_init,
	.verify		= cpufreq_generic_frequency_table_verify,
	.target_index	= g5_cpufreq_target,
	.get		= g5_cpufreq_get_speed,
	.attr 		= cpufreq_generic_attr,
};


#ifdef CONFIG_PMAC_SMU

static int __init g5_neo2_cpufreq_init(struct device_node *cpunode)
{
	unsigned int psize, ssize;
	unsigned long max_freq;
	char *freq_method, *volt_method;
	const u32 *valp;
	u32 pvr_hi;
	int use_volts_vdnap = 0;
	int use_volts_smu = 0;
	int rc = -ENODEV;

	/* Check supported platforms */
	if (of_machine_is_compatible("PowerMac8,1") ||
	    of_machine_is_compatible("PowerMac8,2") ||
	    of_machine_is_compatible("PowerMac9,1") ||
	    of_machine_is_compatible("PowerMac12,1"))
		use_volts_smu = 1;
	else if (of_machine_is_compatible("PowerMac11,2"))
		use_volts_vdnap = 1;
	else
		return -ENODEV;

	/* Check 970FX for now */
	valp = of_get_property(cpunode, "cpu-version", NULL);
	if (!valp) {
		DBG("No cpu-version property !\n");
		goto bail_noprops;
	}
	pvr_hi = (*valp) >> 16;
	if (pvr_hi != 0x3c && pvr_hi != 0x44) {
		printk(KERN_ERR "cpufreq: Unsupported CPU version\n");
		goto bail_noprops;
	}

	/* Look for the powertune data in the device-tree */
	g5_pmode_data = of_get_property(cpunode, "power-mode-data",&psize);
	if (!g5_pmode_data) {
		DBG("No power-mode-data !\n");
		goto bail_noprops;
	}
	g5_pmode_max = psize / sizeof(u32) - 1;

	if (use_volts_smu) {
		const struct smu_sdbp_header *shdr;

		/* Look for the FVT table */
		shdr = smu_get_sdb_partition(SMU_SDB_FVT_ID, NULL);
		if (!shdr)
			goto bail_noprops;
		g5_fvt_table = (struct smu_sdbp_fvt *)&shdr[1];
		ssize = (shdr->len * sizeof(u32)) - sizeof(*shdr);
		g5_fvt_count = ssize / sizeof(*g5_fvt_table);
		g5_fvt_cur = 0;

		/* Sanity checking */
		if (g5_fvt_count < 1 || g5_pmode_max < 1)
			goto bail_noprops;

		g5_switch_volt = g5_smu_switch_volt;
		volt_method = "SMU";
	} else if (use_volts_vdnap) {
		struct device_node *root;

		root = of_find_node_by_path("/");
		if (root == NULL) {
			printk(KERN_ERR "cpufreq: Can't find root of "
			       "device tree\n");
			goto bail_noprops;
		}
		pfunc_set_vdnap0 = pmf_find_function(root, "set-vdnap0");
		pfunc_vdnap0_complete =
			pmf_find_function(root, "slewing-done");
		if (pfunc_set_vdnap0 == NULL ||
		    pfunc_vdnap0_complete == NULL) {
			printk(KERN_ERR "cpufreq: Can't find required "
			       "platform function\n");
			goto bail_noprops;
		}

		g5_switch_volt = g5_vdnap_switch_volt;
		volt_method = "GPIO";
	} else {
		g5_switch_volt = g5_dummy_switch_volt;
		volt_method = "none";
	}

	/*
	 * From what I see, clock-frequency is always the maximal frequency.
	 * The current driver can not slew sysclk yet, so we really only deal
	 * with powertune steps for now. We also only implement full freq and
	 * half freq in this version. So far, I haven't yet seen a machine
	 * supporting anything else.
	 */
	valp = of_get_property(cpunode, "clock-frequency", NULL);
	if (!valp)
		return -ENODEV;
	max_freq = (*valp)/1000;
	g5_cpu_freqs[0].frequency = max_freq;
	g5_cpu_freqs[1].frequency = max_freq/2;

	/* Set callbacks */
	transition_latency = 12000;
	g5_switch_freq = g5_scom_switch_freq;
	g5_query_freq = g5_scom_query_freq;
	freq_method = "SCOM";

	/* Force apply current frequency to make sure everything is in
	 * sync (voltage is right for example). Firmware may leave us with
	 * a strange setting ...
	 */
	g5_switch_volt(CPUFREQ_HIGH);
	msleep(10);
	g5_pmode_cur = -1;
	g5_switch_freq(g5_query_freq());

	printk(KERN_INFO "Registering G5 CPU frequency driver\n");
	printk(KERN_INFO "Frequency method: %s, Voltage method: %s\n",
	       freq_method, volt_method);
	printk(KERN_INFO "Low: %d Mhz, High: %d Mhz, Cur: %d MHz\n",
		g5_cpu_freqs[1].frequency/1000,
		g5_cpu_freqs[0].frequency/1000,
		g5_cpu_freqs[g5_pmode_cur].frequency/1000);

	rc = cpufreq_register_driver(&g5_cpufreq_driver);

	/* We keep the CPU node on hold... hopefully, Apple G5 don't have
	 * hotplug CPU with a dynamic device-tree ...
	 */
	return rc;

 bail_noprops:
	of_node_put(cpunode);

	return rc;
}

#endif /* CONFIG_PMAC_SMU */


static int __init g5_pm72_cpufreq_init(struct device_node *cpunode)
{
	struct device_node *cpuid = NULL, *hwclock = NULL;
	const u8 *eeprom = NULL;
	const u32 *valp;
	u64 max_freq, min_freq, ih, il;
	int has_volt = 1, rc = 0;

	DBG("cpufreq: Initializing for PowerMac7,2, PowerMac7,3 and"
	    " RackMac3,1...\n");

	/* Lookup the cpuid eeprom node */
        cpuid = of_find_node_by_path("/u3@0,f8000000/i2c@f8001000/cpuid@a0");
	if (cpuid != NULL)
		eeprom = of_get_property(cpuid, "cpuid", NULL);
	if (eeprom == NULL) {
		printk(KERN_ERR "cpufreq: Can't find cpuid EEPROM !\n");
		rc = -ENODEV;
		goto bail;
	}

	/* Lookup the i2c hwclock */
	for (hwclock = NULL;
	     (hwclock = of_find_node_by_name(hwclock, "i2c-hwclock")) != NULL;){
		const char *loc = of_get_property(hwclock,
				"hwctrl-location", NULL);
		if (loc == NULL)
			continue;
		if (strcmp(loc, "CPU CLOCK"))
			continue;
		if (!of_get_property(hwclock, "platform-get-frequency", NULL))
			continue;
		break;
	}
	if (hwclock == NULL) {
		printk(KERN_ERR "cpufreq: Can't find i2c clock chip !\n");
		rc = -ENODEV;
		goto bail;
	}

	DBG("cpufreq: i2c clock chip found: %s\n", hwclock->full_name);

	/* Now get all the platform functions */
	pfunc_cpu_getfreq =
		pmf_find_function(hwclock, "get-frequency");
	pfunc_cpu_setfreq_high =
		pmf_find_function(hwclock, "set-frequency-high");
	pfunc_cpu_setfreq_low =
		pmf_find_function(hwclock, "set-frequency-low");
	pfunc_slewing_done =
		pmf_find_function(hwclock, "slewing-done");
	pfunc_cpu0_volt_high =
		pmf_find_function(hwclock, "set-voltage-high-0");
	pfunc_cpu0_volt_low =
		pmf_find_function(hwclock, "set-voltage-low-0");
	pfunc_cpu1_volt_high =
		pmf_find_function(hwclock, "set-voltage-high-1");
	pfunc_cpu1_volt_low =
		pmf_find_function(hwclock, "set-voltage-low-1");

	/* Check we have minimum requirements */
	if (pfunc_cpu_getfreq == NULL || pfunc_cpu_setfreq_high == NULL ||
	    pfunc_cpu_setfreq_low == NULL || pfunc_slewing_done == NULL) {
		printk(KERN_ERR "cpufreq: Can't find platform functions !\n");
		rc = -ENODEV;
		goto bail;
	}

	/* Check that we have complete sets */
	if (pfunc_cpu0_volt_high == NULL || pfunc_cpu0_volt_low == NULL) {
		pmf_put_function(pfunc_cpu0_volt_high);
		pmf_put_function(pfunc_cpu0_volt_low);
		pfunc_cpu0_volt_high = pfunc_cpu0_volt_low = NULL;
		has_volt = 0;
	}
	if (!has_volt ||
	    pfunc_cpu1_volt_high == NULL || pfunc_cpu1_volt_low == NULL) {
		pmf_put_function(pfunc_cpu1_volt_high);
		pmf_put_function(pfunc_cpu1_volt_low);
		pfunc_cpu1_volt_high = pfunc_cpu1_volt_low = NULL;
	}

	/* Note: The device tree also contains a "platform-set-values"
	 * function for which I haven't quite figured out the usage. It
	 * might have to be called on init and/or wakeup, I'm not too sure
	 * but things seem to work fine without it so far ...
	 */

	/* Get max frequency from device-tree */
	valp = of_get_property(cpunode, "clock-frequency", NULL);
	if (!valp) {
		printk(KERN_ERR "cpufreq: Can't find CPU frequency !\n");
		rc = -ENODEV;
		goto bail;
	}

	max_freq = (*valp)/1000;

	/* Now calculate reduced frequency by using the cpuid input freq
	 * ratio. This requires 64 bits math unless we are willing to lose
	 * some precision
	 */
	ih = *((u32 *)(eeprom + 0x10));
	il = *((u32 *)(eeprom + 0x20));

	/* Check for machines with no useful settings */
	if (il == ih) {
		printk(KERN_WARNING "cpufreq: No low frequency mode available"
		       " on this model !\n");
		rc = -ENODEV;
		goto bail;
	}

	min_freq = 0;
	if (ih != 0 && il != 0)
		min_freq = (max_freq * il) / ih;

	/* Sanity check */
	if (min_freq >= max_freq || min_freq < 1000) {
		printk(KERN_ERR "cpufreq: Can't calculate low frequency !\n");
		rc = -ENXIO;
		goto bail;
	}
	g5_cpu_freqs[0].frequency = max_freq;
	g5_cpu_freqs[1].frequency = min_freq;

	/* Based on a measurement on Xserve G5, rounded up. */
	transition_latency = 10 * NSEC_PER_MSEC;

	/* Set callbacks */
	g5_switch_volt = g5_pfunc_switch_volt;
	g5_switch_freq = g5_pfunc_switch_freq;
	g5_query_freq = g5_pfunc_query_freq;

	/* Force apply current frequency to make sure everything is in
	 * sync (voltage is right for example). Firmware may leave us with
	 * a strange setting ...
	 */
	g5_switch_volt(CPUFREQ_HIGH);
	msleep(10);
	g5_pmode_cur = -1;
	g5_switch_freq(g5_query_freq());

	printk(KERN_INFO "Registering G5 CPU frequency driver\n");
	printk(KERN_INFO "Frequency method: i2c/pfunc, "
	       "Voltage method: %s\n", has_volt ? "i2c/pfunc" : "none");
	printk(KERN_INFO "Low: %d Mhz, High: %d Mhz, Cur: %d MHz\n",
		g5_cpu_freqs[1].frequency/1000,
		g5_cpu_freqs[0].frequency/1000,
		g5_cpu_freqs[g5_pmode_cur].frequency/1000);

	rc = cpufreq_register_driver(&g5_cpufreq_driver);
 bail:
	if (rc != 0) {
		pmf_put_function(pfunc_cpu_getfreq);
		pmf_put_function(pfunc_cpu_setfreq_high);
		pmf_put_function(pfunc_cpu_setfreq_low);
		pmf_put_function(pfunc_slewing_done);
		pmf_put_function(pfunc_cpu0_volt_high);
		pmf_put_function(pfunc_cpu0_volt_low);
		pmf_put_function(pfunc_cpu1_volt_high);
		pmf_put_function(pfunc_cpu1_volt_low);
	}
	of_node_put(hwclock);
	of_node_put(cpuid);
	of_node_put(cpunode);

	return rc;
}

static int __init g5_cpufreq_init(void)
{
	struct device_node *cpunode;
	int rc = 0;

	/* Get first CPU node */
	cpunode = of_cpu_device_node_get(0);
	if (cpunode == NULL) {
		pr_err("cpufreq: Can't find any CPU node\n");
		return -ENODEV;
	}

	if (of_machine_is_compatible("PowerMac7,2") ||
	    of_machine_is_compatible("PowerMac7,3") ||
	    of_machine_is_compatible("RackMac3,1"))
		rc = g5_pm72_cpufreq_init(cpunode);
#ifdef CONFIG_PMAC_SMU
	else
		rc = g5_neo2_cpufreq_init(cpunode);
#endif /* CONFIG_PMAC_SMU */

	return rc;
}

module_init(g5_cpufreq_init);


MODULE_LICENSE("GPL");
