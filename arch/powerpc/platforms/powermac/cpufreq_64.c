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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/cpufreq.h>
#include <linux/init.h>
#include <linux/completion.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/irq.h>
#include <asm/sections.h>
#include <asm/cputable.h>
#include <asm/time.h>
#include <asm/smu.h>

#undef DEBUG

#ifdef DEBUG
#define DBG(fmt...) printk(fmt)
#else
#define DBG(fmt...)
#endif

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

static struct freq_attr* g5_cpu_freqs_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

/* Power mode data is an array of the 32 bits PCR values to use for
 * the various frequencies, retreived from the device-tree
 */
static u32 *g5_pmode_data;
static int g5_pmode_max;
static int g5_pmode_cur;

static DECLARE_MUTEX(g5_switch_mutex);


static struct smu_sdbp_fvt *g5_fvt_table;	/* table of op. points */
static int g5_fvt_count;			/* number of op. points */
static int g5_fvt_cur;				/* current op. point */

/* ----------------- real hardware interface */

static void g5_switch_volt(int speed_mode)
{
	struct smu_simple_cmd	cmd;

	DECLARE_COMPLETION(comp);
	smu_queue_simple(&cmd, SMU_CMD_POWER_COMMAND, 8, smu_done_complete,
			 &comp, 'V', 'S', 'L', 'E', 'W',
			 0xff, g5_fvt_cur+1, speed_mode);
	wait_for_completion(&comp);
}

static int g5_switch_freq(int speed_mode)
{
	struct cpufreq_freqs freqs;
	int to;

	if (g5_pmode_cur == speed_mode)
		return 0;

	down(&g5_switch_mutex);

	freqs.old = g5_cpu_freqs[g5_pmode_cur].frequency;
	freqs.new = g5_cpu_freqs[speed_mode].frequency;
	freqs.cpu = 0;

	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	/* If frequency is going up, first ramp up the voltage */
	if (speed_mode < g5_pmode_cur)
		g5_switch_volt(speed_mode);

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

	/* If frequency is going down, last ramp the voltage */
	if (speed_mode > g5_pmode_cur)
		g5_switch_volt(speed_mode);

	g5_pmode_cur = speed_mode;
	ppc_proc_freq = g5_cpu_freqs[speed_mode].frequency * 1000ul;

	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	up(&g5_switch_mutex);

	return 0;
}

static int g5_query_freq(void)
{
	unsigned long psr = scom970_read(SCOM_PSR);
	int i;

	for (i = 0; i <= g5_pmode_max; i++)
		if ((((psr >> PSR_CUR_SPEED_SHIFT) ^
		      (g5_pmode_data[i] >> PCR_SPEED_SHIFT)) & 0x3) == 0)
			break;
	return i;
}

/* ----------------- cpufreq bookkeeping */

static int g5_cpufreq_verify(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy, g5_cpu_freqs);
}

static int g5_cpufreq_target(struct cpufreq_policy *policy,
	unsigned int target_freq, unsigned int relation)
{
	unsigned int    newstate = 0;

	if (cpufreq_frequency_table_target(policy, g5_cpu_freqs,
			target_freq, relation, &newstate))
		return -EINVAL;

	return g5_switch_freq(newstate);
}

static unsigned int g5_cpufreq_get_speed(unsigned int cpu)
{
	return g5_cpu_freqs[g5_pmode_cur].frequency;
}

static int g5_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	if (policy->cpu != 0)
		return -ENODEV;

	policy->governor = CPUFREQ_DEFAULT_GOVERNOR;
	policy->cpuinfo.transition_latency = CPUFREQ_ETERNAL;
	policy->cur = g5_cpu_freqs[g5_query_freq()].frequency;
	cpufreq_frequency_table_get_attr(g5_cpu_freqs, policy->cpu);

	return cpufreq_frequency_table_cpuinfo(policy,
		g5_cpu_freqs);
}


static struct cpufreq_driver g5_cpufreq_driver = {
	.name		= "powermac",
	.owner		= THIS_MODULE,
	.flags		= CPUFREQ_CONST_LOOPS,
	.init		= g5_cpufreq_cpu_init,
	.verify		= g5_cpufreq_verify,
	.target		= g5_cpufreq_target,
	.get		= g5_cpufreq_get_speed,
	.attr 		= g5_cpu_freqs_attr,
};


static int __init g5_cpufreq_init(void)
{
	struct device_node *cpunode;
	unsigned int psize, ssize;
	struct smu_sdbp_header *shdr;
	unsigned long max_freq;
	u32 *valp;
	int rc = -ENODEV;

	/* Look for CPU and SMU nodes */
	cpunode = of_find_node_by_type(NULL, "cpu");
	if (!cpunode) {
		DBG("No CPU node !\n");
		return -ENODEV;
	}

	/* Check 970FX for now */
	valp = (u32 *)get_property(cpunode, "cpu-version", NULL);
	if (!valp) {
		DBG("No cpu-version property !\n");
		goto bail_noprops;
	}
	if (((*valp) >> 16) != 0x3c) {
		DBG("Wrong CPU version: %08x\n", *valp);
		goto bail_noprops;
	}

	/* Look for the powertune data in the device-tree */
	g5_pmode_data = (u32 *)get_property(cpunode, "power-mode-data",&psize);
	if (!g5_pmode_data) {
		DBG("No power-mode-data !\n");
		goto bail_noprops;
	}
	g5_pmode_max = psize / sizeof(u32) - 1;

	/* Look for the FVT table */
	shdr = smu_get_sdb_partition(SMU_SDB_FVT_ID, NULL);
	if (!shdr)
		goto bail_noprops;
	g5_fvt_table = (struct smu_sdbp_fvt *)&shdr[1];
	ssize = (shdr->len * sizeof(u32)) - sizeof(struct smu_sdbp_header);
	g5_fvt_count = ssize / sizeof(struct smu_sdbp_fvt);
	g5_fvt_cur = 0;

	/* Sanity checking */
	if (g5_fvt_count < 1 || g5_pmode_max < 1)
		goto bail_noprops;

	/*
	 * From what I see, clock-frequency is always the maximal frequency.
	 * The current driver can not slew sysclk yet, so we really only deal
	 * with powertune steps for now. We also only implement full freq and
	 * half freq in this version. So far, I haven't yet seen a machine
	 * supporting anything else.
	 */
	valp = (u32 *)get_property(cpunode, "clock-frequency", NULL);
	if (!valp)
		return -ENODEV;
	max_freq = (*valp)/1000;
	g5_cpu_freqs[0].frequency = max_freq;
	g5_cpu_freqs[1].frequency = max_freq/2;

	/* Check current frequency */
	g5_pmode_cur = g5_query_freq();
	if (g5_pmode_cur > 1)
		/* We don't support anything but 1:1 and 1:2, fixup ... */
		g5_pmode_cur = 1;

	/* Force apply current frequency to make sure everything is in
	 * sync (voltage is right for example). Firmware may leave us with
	 * a strange setting ...
	 */
	g5_switch_freq(g5_pmode_cur);

	printk(KERN_INFO "Registering G5 CPU frequency driver\n");
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

module_init(g5_cpufreq_init);


MODULE_LICENSE("GPL");
