/*
 *  linux/arch/arm/mach-pxa/cpu-pxa.c
 *
 *  Copyright (C) 2002,2003 Intrinsyc Software
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * History:
 *   31-Jul-2002 : Initial version [FB]
 *   29-Jan-2003 : added PXA255 support [FB]
 *   20-Apr-2003 : ported to v2.5 (Dustin McIntire, Sensoria Corp.)
 *
 * Note:
 *   This driver may change the memory bus clock rate, but will not do any
 *   platform specific access timing changes... for example if you have flash
 *   memory connected to CS0, you will need to register a platform specific
 *   notifier which will adjust the memory access strobes to maintain a
 *   minimum strobe width.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/cpufreq.h>

#include <asm/hardware.h>
#include <asm/arch/pxa-regs.h>
#include <asm/arch/pxa2xx-regs.h>

#ifdef DEBUG
static unsigned int freq_debug;
MODULE_PARM(freq_debug, "i");
MODULE_PARM_DESC(freq_debug, "Set the debug messages to on=1/off=0");
#else
#define freq_debug  0
#endif

typedef struct {
	unsigned int khz;
	unsigned int membus;
	unsigned int cccr;
	unsigned int div2;
} pxa_freqs_t;

/* Define the refresh period in mSec for the SDRAM and the number of rows */
#define SDRAM_TREF          64      /* standard 64ms SDRAM */
#define SDRAM_ROWS          4096    /* 64MB=8192 32MB=4096 */
#define MDREFR_DRI(x)       (((x) * SDRAM_TREF) / (SDRAM_ROWS * 32))

#define CCLKCFG_TURBO       0x1
#define CCLKCFG_FCS         0x2
#define PXA25x_MIN_FREQ     99500
#define PXA25x_MAX_FREQ     398100
#define MDREFR_DB2_MASK     (MDREFR_K2DB2 | MDREFR_K1DB2)
#define MDREFR_DRI_MASK     0xFFF


/* Use the run mode frequencies for the CPUFREQ_POLICY_PERFORMANCE policy */
static pxa_freqs_t pxa255_run_freqs[] =
{
    /* CPU   MEMBUS  CCCR  DIV2*/
    { 99500,  99500, 0x121, 1}, /* run= 99, turbo= 99, PXbus=50,  SDRAM=50 */
    {132700, 132700, 0x123, 1}, /* run=133, turbo=133, PXbus=66,  SDRAM=66 */
    {199100,  99500, 0x141, 0}, /* run=199, turbo=199, PXbus=99,  SDRAM=99 */
    {265400, 132700, 0x143, 1}, /* run=265, turbo=265, PXbus=133, SDRAM=66 */
    {331800, 165900, 0x145, 1}, /* run=331, turbo=331, PXbus=166, SDRAM=83 */
    {398100,  99500, 0x161, 0}, /* run=398, turbo=398, PXbus=196, SDRAM=99 */
    {0,}
};
#define NUM_RUN_FREQS ARRAY_SIZE(pxa255_run_freqs)

static struct cpufreq_frequency_table pxa255_run_freq_table[NUM_RUN_FREQS+1];

/* Use the turbo mode frequencies for the CPUFREQ_POLICY_POWERSAVE policy */
static pxa_freqs_t pxa255_turbo_freqs[] =
{
    /* CPU   MEMBUS  CCCR  DIV2*/
    { 99500, 99500,  0x121, 1}, /* run=99,  turbo= 99, PXbus=50, SDRAM=50 */
    {199100, 99500,  0x221, 0}, /* run=99,  turbo=199, PXbus=50, SDRAM=99 */
    {298500, 99500,  0x321, 0}, /* run=99,  turbo=287, PXbus=50, SDRAM=99 */
    {298600, 99500,  0x1c1, 0}, /* run=199, turbo=287, PXbus=99, SDRAM=99 */
    {398100, 99500,  0x241, 0}, /* run=199, turbo=398, PXbus=99, SDRAM=99 */
    {0,}
};
#define NUM_TURBO_FREQS ARRAY_SIZE(pxa255_turbo_freqs)

static struct cpufreq_frequency_table pxa255_turbo_freq_table[NUM_TURBO_FREQS+1];

extern unsigned get_clk_frequency_khz(int info);

/* find a valid frequency point */
static int pxa_verify_policy(struct cpufreq_policy *policy)
{
	struct cpufreq_frequency_table *pxa_freqs_table;
	int ret;

	if (policy->policy == CPUFREQ_POLICY_PERFORMANCE) {
		pxa_freqs_table = pxa255_run_freq_table;
	} else if (policy->policy == CPUFREQ_POLICY_POWERSAVE) {
		pxa_freqs_table = pxa255_turbo_freq_table;
	} else {
		printk("CPU PXA: Unknown policy found. "
		       "Using CPUFREQ_POLICY_PERFORMANCE\n");
		pxa_freqs_table = pxa255_run_freq_table;
	}

	ret = cpufreq_frequency_table_verify(policy, pxa_freqs_table);

	if (freq_debug)
		pr_debug("Verified CPU policy: %dKhz min to %dKhz max\n",
		       policy->min, policy->max);

	return ret;
}

static int pxa_set_target(struct cpufreq_policy *policy,
			   unsigned int target_freq,
			   unsigned int relation)
{
	struct cpufreq_frequency_table *pxa_freqs_table;
	pxa_freqs_t *pxa_freq_settings;
	struct cpufreq_freqs freqs;
	int idx;
	unsigned long flags;
	unsigned int unused, preset_mdrefr, postset_mdrefr;
	void *ramstart = phys_to_virt(0xa0000000);

	/* Get the current policy */
	if (policy->policy == CPUFREQ_POLICY_PERFORMANCE) {
		pxa_freq_settings = pxa255_run_freqs;
		pxa_freqs_table   = pxa255_run_freq_table;
	} else if (policy->policy == CPUFREQ_POLICY_POWERSAVE) {
		pxa_freq_settings = pxa255_turbo_freqs;
		pxa_freqs_table   = pxa255_turbo_freq_table;
	} else {
		printk("CPU PXA: Unknown policy found. "
		       "Using CPUFREQ_POLICY_PERFORMANCE\n");
		pxa_freq_settings = pxa255_run_freqs;
		pxa_freqs_table   = pxa255_run_freq_table;
	}

	/* Lookup the next frequency */
	if (cpufreq_frequency_table_target(policy, pxa_freqs_table,
	                                   target_freq, relation, &idx)) {
		return -EINVAL;
	}

	freqs.old = policy->cur;
	freqs.new = pxa_freq_settings[idx].khz;
	freqs.cpu = policy->cpu;

	if (freq_debug)
		pr_debug(KERN_INFO "Changing CPU frequency to %d Mhz, (SDRAM %d Mhz)\n",
		       freqs.new / 1000, (pxa_freq_settings[idx].div2) ?
		       (pxa_freq_settings[idx].membus / 2000) :
		       (pxa_freq_settings[idx].membus / 1000));

	/*
	 * Tell everyone what we're about to do...
	 * you should add a notify client with any platform specific
	 * Vcc changing capability
	 */
	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	/* Calculate the next MDREFR.  If we're slowing down the SDRAM clock
	 * we need to preset the smaller DRI before the change.  If we're speeding
	 * up we need to set the larger DRI value after the change.
	 */
	preset_mdrefr = postset_mdrefr = MDREFR;
	if ((MDREFR & MDREFR_DRI_MASK) > MDREFR_DRI(pxa_freq_settings[idx].membus)) {
		preset_mdrefr = (preset_mdrefr & ~MDREFR_DRI_MASK) |
		                MDREFR_DRI(pxa_freq_settings[idx].membus);
	}
	postset_mdrefr = (postset_mdrefr & ~MDREFR_DRI_MASK) |
		            MDREFR_DRI(pxa_freq_settings[idx].membus);

	/* If we're dividing the memory clock by two for the SDRAM clock, this
	 * must be set prior to the change.  Clearing the divide must be done
	 * after the change.
	 */
	if (pxa_freq_settings[idx].div2) {
		preset_mdrefr  |= MDREFR_DB2_MASK;
		postset_mdrefr |= MDREFR_DB2_MASK;
	} else {
		postset_mdrefr &= ~MDREFR_DB2_MASK;
	}

	local_irq_save(flags);

	/* Set new the CCCR */
	CCCR = pxa_freq_settings[idx].cccr;

	asm volatile("							\n\
		ldr	r4, [%1]		/* load MDREFR */	\n\
		b	2f						\n\
		.align	5 						\n\
1:									\n\
		str	%4, [%1]		/* preset the MDREFR */	\n\
		mcr	p14, 0, %2, c6, c0, 0	/* set CCLKCFG[FCS] */	\n\
		str	%5, [%1]		/* postset the MDREFR */ \n\
									\n\
		b	3f						\n\
2:		b	1b						\n\
3:		nop							\n\
	  "
	  : "=&r" (unused)
	  : "r" (&MDREFR), "r" (CCLKCFG_TURBO|CCLKCFG_FCS), "r" (ramstart),
	    "r" (preset_mdrefr), "r" (postset_mdrefr)
	  : "r4", "r5");
	local_irq_restore(flags);

	/*
	 * Tell everyone what we've just done...
	 * you should add a notify client with any platform specific
	 * SDRAM refresh timer adjustments
	 */
	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	return 0;
}

static int pxa_cpufreq_init(struct cpufreq_policy *policy)
{
	int i;

	/* set default policy and cpuinfo */
	policy->governor = CPUFREQ_DEFAULT_GOVERNOR;
	policy->policy = CPUFREQ_POLICY_PERFORMANCE;
	policy->cpuinfo.max_freq = PXA25x_MAX_FREQ;
	policy->cpuinfo.min_freq = PXA25x_MIN_FREQ;
	policy->cpuinfo.transition_latency = 1000; /* FIXME: 1 ms, assumed */
	policy->cur = get_clk_frequency_khz(0);    /* current freq */
	policy->min = policy->max = policy->cur;

	/* Generate the run cpufreq_frequency_table struct */
	for (i = 0; i < NUM_RUN_FREQS; i++) {
		pxa255_run_freq_table[i].frequency = pxa255_run_freqs[i].khz;
		pxa255_run_freq_table[i].index = i;
	}

	pxa255_run_freq_table[i].frequency = CPUFREQ_TABLE_END;
	/* Generate the turbo cpufreq_frequency_table struct */
	for (i = 0; i < NUM_TURBO_FREQS; i++) {
		pxa255_turbo_freq_table[i].frequency = pxa255_turbo_freqs[i].khz;
		pxa255_turbo_freq_table[i].index = i;
	}
	pxa255_turbo_freq_table[i].frequency = CPUFREQ_TABLE_END;

	printk(KERN_INFO "PXA CPU frequency change support initialized\n");

	return 0;
}

static struct cpufreq_driver pxa_cpufreq_driver = {
	.verify	= pxa_verify_policy,
	.target	= pxa_set_target,
	.init	= pxa_cpufreq_init,
	.name	= "PXA25x",
};

static int __init pxa_cpu_init(void)
{
	int ret = -ENODEV;
	if (cpu_is_pxa25x())
		ret = cpufreq_register_driver(&pxa_cpufreq_driver);
	return ret;
}

static void __exit pxa_cpu_exit(void)
{
	if (cpu_is_pxa25x())
		cpufreq_unregister_driver(&pxa_cpufreq_driver);
}


MODULE_AUTHOR ("Intrinsyc Software Inc.");
MODULE_DESCRIPTION ("CPU frequency changing driver for the PXA architecture");
MODULE_LICENSE("GPL");
module_init(pxa_cpu_init);
module_exit(pxa_cpu_exit);
