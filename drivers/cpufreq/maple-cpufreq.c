// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (C) 2011 Dmitry Eremin-Solenikov
 *  Copyright (C) 2002 - 2005 Benjamin Herrenschmidt <benh@kernel.crashing.org>
 *  and                       Markus Demleitner <msdemlei@cl.uni-heidelberg.de>
 *
 * This driver adds basic cpufreq support for SMU & 970FX based G5 Macs,
 * that is iMac G5 and latest single CPU desktop.
 */

#undef DEBUG

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

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
#include <linux/time.h>
#include <linux/of_device.h>

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

static struct cpufreq_frequency_table maple_cpu_freqs[] = {
	{0, CPUFREQ_HIGH,		0},
	{0, CPUFREQ_LOW,		0},
	{0, 0,				CPUFREQ_TABLE_END},
};

/* Power mode data is an array of the 32 bits PCR values to use for
 * the various frequencies, retrieved from the device-tree
 */
static int maple_pmode_cur;

static const u32 *maple_pmode_data;
static int maple_pmode_max;

/*
 * SCOM based frequency switching for 970FX rev3
 */
static int maple_scom_switch_freq(int speed_mode)
{
	unsigned long flags;
	int to;

	local_irq_save(flags);

	/* Clear PCR high */
	scom970_write(SCOM_PCR, 0);
	/* Clear PCR low */
	scom970_write(SCOM_PCR, PCR_HILO_SELECT | 0);
	/* Set PCR low */
	scom970_write(SCOM_PCR, PCR_HILO_SELECT |
		      maple_pmode_data[speed_mode]);

	/* Wait for completion */
	for (to = 0; to < 10; to++) {
		unsigned long psr = scom970_read(SCOM_PSR);

		if ((psr & PSR_CMD_RECEIVED) == 0 &&
		    (((psr >> PSR_CUR_SPEED_SHIFT) ^
		      (maple_pmode_data[speed_mode] >> PCR_SPEED_SHIFT)) & 0x3)
		    == 0)
			break;
		if (psr & PSR_CMD_COMPLETED)
			break;
		udelay(100);
	}

	local_irq_restore(flags);

	maple_pmode_cur = speed_mode;
	ppc_proc_freq = maple_cpu_freqs[speed_mode].frequency * 1000ul;

	return 0;
}

static int maple_scom_query_freq(void)
{
	unsigned long psr = scom970_read(SCOM_PSR);
	int i;

	for (i = 0; i <= maple_pmode_max; i++)
		if ((((psr >> PSR_CUR_SPEED_SHIFT) ^
		      (maple_pmode_data[i] >> PCR_SPEED_SHIFT)) & 0x3) == 0)
			break;
	return i;
}

/*
 * Common interface to the cpufreq core
 */

static int maple_cpufreq_target(struct cpufreq_policy *policy,
	unsigned int index)
{
	return maple_scom_switch_freq(index);
}

static unsigned int maple_cpufreq_get_speed(unsigned int cpu)
{
	return maple_cpu_freqs[maple_pmode_cur].frequency;
}

static int maple_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	return cpufreq_generic_init(policy, maple_cpu_freqs, 12000);
}

static struct cpufreq_driver maple_cpufreq_driver = {
	.name		= "maple",
	.flags		= CPUFREQ_CONST_LOOPS,
	.init		= maple_cpufreq_cpu_init,
	.verify		= cpufreq_generic_frequency_table_verify,
	.target_index	= maple_cpufreq_target,
	.get		= maple_cpufreq_get_speed,
	.attr		= cpufreq_generic_attr,
};

static int __init maple_cpufreq_init(void)
{
	struct device_node *cpunode;
	unsigned int psize;
	unsigned long max_freq;
	const u32 *valp;
	u32 pvr_hi;
	int rc = -ENODEV;

	/*
	 * Behave here like powermac driver which checks machine compatibility
	 * to ease merging of two drivers in future.
	 */
	if (!of_machine_is_compatible("Momentum,Maple") &&
	    !of_machine_is_compatible("Momentum,Apache"))
		return 0;

	/* Get first CPU node */
	cpunode = of_cpu_device_node_get(0);
	if (cpunode == NULL) {
		pr_err("Can't find any CPU 0 node\n");
		goto bail_noprops;
	}

	/* Check 970FX for now */
	/* we actually don't care on which CPU to access PVR */
	pvr_hi = PVR_VER(mfspr(SPRN_PVR));
	if (pvr_hi != 0x3c && pvr_hi != 0x44) {
		pr_err("Unsupported CPU version (%x)\n", pvr_hi);
		goto bail_noprops;
	}

	/* Look for the powertune data in the device-tree */
	/*
	 * On Maple this property is provided by PIBS in dual-processor config,
	 * not provided by PIBS in CPU0 config and also not provided by SLOF,
	 * so YMMV
	 */
	maple_pmode_data = of_get_property(cpunode, "power-mode-data", &psize);
	if (!maple_pmode_data) {
		DBG("No power-mode-data !\n");
		goto bail_noprops;
	}
	maple_pmode_max = psize / sizeof(u32) - 1;

	/*
	 * From what I see, clock-frequency is always the maximal frequency.
	 * The current driver can not slew sysclk yet, so we really only deal
	 * with powertune steps for now. We also only implement full freq and
	 * half freq in this version. So far, I haven't yet seen a machine
	 * supporting anything else.
	 */
	valp = of_get_property(cpunode, "clock-frequency", NULL);
	if (!valp)
		goto bail_noprops;
	max_freq = (*valp)/1000;
	maple_cpu_freqs[0].frequency = max_freq;
	maple_cpu_freqs[1].frequency = max_freq/2;

	/* Force apply current frequency to make sure everything is in
	 * sync (voltage is right for example). Firmware may leave us with
	 * a strange setting ...
	 */
	msleep(10);
	maple_pmode_cur = -1;
	maple_scom_switch_freq(maple_scom_query_freq());

	pr_info("Registering Maple CPU frequency driver\n");
	pr_info("Low: %d Mhz, High: %d Mhz, Cur: %d MHz\n",
		maple_cpu_freqs[1].frequency/1000,
		maple_cpu_freqs[0].frequency/1000,
		maple_cpu_freqs[maple_pmode_cur].frequency/1000);

	rc = cpufreq_register_driver(&maple_cpufreq_driver);

bail_noprops:
	of_node_put(cpunode);

	return rc;
}

module_init(maple_cpufreq_init);


MODULE_LICENSE("GPL");
