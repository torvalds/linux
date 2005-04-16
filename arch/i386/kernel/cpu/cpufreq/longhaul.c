/*
 *  (C) 2001-2004  Dave Jones. <davej@codemonkey.org.uk>
 *  (C) 2002  Padraig Brady. <padraig@antefacto.com>
 *
 *  Licensed under the terms of the GNU GPL License version 2.
 *  Based upon datasheets & sample CPUs kindly provided by VIA.
 *
 *  VIA have currently 3 different versions of Longhaul.
 *  Version 1 (Longhaul) uses the BCR2 MSR at 0x1147.
 *   It is present only in Samuel 1 (C5A), Samuel 2 (C5B) stepping 0.
 *  Version 2 of longhaul is the same as v1, but adds voltage scaling.
 *   Present in Samuel 2 (steppings 1-7 only) (C5B), and Ezra (C5C)
 *   voltage scaling support has currently been disabled in this driver
 *   until we have code that gets it right.
 *  Version 3 of longhaul got renamed to Powersaver and redesigned
 *   to use the POWERSAVER MSR at 0x110a.
 *   It is present in Ezra-T (C5M), Nehemiah (C5X) and above.
 *   It's pretty much the same feature wise to longhaul v2, though
 *   there is provision for scaling FSB too, but this doesn't work
 *   too well in practice so we don't even try to use this.
 *
 *  BIG FAT DISCLAIMER: Work in progress code. Possibly *dangerous*
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <asm/msr.h>
#include <asm/timex.h>
#include <asm/io.h>

#include "longhaul.h"

#define PFX "longhaul: "

#define TYPE_LONGHAUL_V1	1
#define TYPE_LONGHAUL_V2	2
#define TYPE_POWERSAVER		3

#define	CPU_SAMUEL	1
#define	CPU_SAMUEL2	2
#define	CPU_EZRA	3
#define	CPU_EZRA_T	4
#define	CPU_NEHEMIAH	5

static int cpu_model;
static unsigned int numscales=16, numvscales;
static unsigned int fsb;
static int minvid, maxvid;
static unsigned int minmult, maxmult;
static int can_scale_voltage;
static int vrmrev;

/* Module parameters */
static int dont_scale_voltage;


#define dprintk(msg...) cpufreq_debug_printk(CPUFREQ_DEBUG_DRIVER, "longhaul", msg)


#define __hlt()     __asm__ __volatile__("hlt": : :"memory")

/* Clock ratios multiplied by 10 */
static int clock_ratio[32];
static int eblcr_table[32];
static int voltage_table[32];
static unsigned int highest_speed, lowest_speed; /* kHz */
static int longhaul_version;
static struct cpufreq_frequency_table *longhaul_table;

#ifdef CONFIG_CPU_FREQ_DEBUG
static char speedbuffer[8];

static char *print_speed(int speed)
{
	if (speed > 1000) {
		if (speed%1000 == 0)
			sprintf (speedbuffer, "%dGHz", speed/1000);
		else
			sprintf (speedbuffer, "%d.%dGHz", speed/1000, (speed%1000)/100);
	} else
		sprintf (speedbuffer, "%dMHz", speed);

	return speedbuffer;
}
#endif


static unsigned int calc_speed(int mult)
{
	int khz;
	khz = (mult/10)*fsb;
	if (mult%10)
		khz += fsb/2;
	khz *= 1000;
	return khz;
}


static int longhaul_get_cpu_mult(void)
{
	unsigned long invalue=0,lo, hi;

	rdmsr (MSR_IA32_EBL_CR_POWERON, lo, hi);
	invalue = (lo & (1<<22|1<<23|1<<24|1<<25)) >>22;
	if (longhaul_version==TYPE_LONGHAUL_V2 || longhaul_version==TYPE_POWERSAVER) {
		if (lo & (1<<27))
			invalue+=16;
	}
	return eblcr_table[invalue];
}


static void do_powersaver(union msr_longhaul *longhaul,
			unsigned int clock_ratio_index)
{
	int version;

	switch (cpu_model) {
	case CPU_EZRA_T:
		version = 3;
		break;
	case CPU_NEHEMIAH:
		version = 0xf;
		break;
	default:
		return;
	}

	rdmsrl(MSR_VIA_LONGHAUL, longhaul->val);
	longhaul->bits.SoftBusRatio = clock_ratio_index & 0xf;
	longhaul->bits.SoftBusRatio4 = (clock_ratio_index & 0x10) >> 4;
	longhaul->bits.EnableSoftBusRatio = 1;
	longhaul->bits.RevisionKey = 0;
	local_irq_disable();
	wrmsrl(MSR_VIA_LONGHAUL, longhaul->val);
	local_irq_enable();
	__hlt();

	rdmsrl(MSR_VIA_LONGHAUL, longhaul->val);
	longhaul->bits.EnableSoftBusRatio = 0;
	longhaul->bits.RevisionKey = version;
	local_irq_disable();
	wrmsrl(MSR_VIA_LONGHAUL, longhaul->val);
	local_irq_enable();
}

/**
 * longhaul_set_cpu_frequency()
 * @clock_ratio_index : bitpattern of the new multiplier.
 *
 * Sets a new clock ratio.
 */

static void longhaul_setstate(unsigned int clock_ratio_index)
{
	int speed, mult;
	struct cpufreq_freqs freqs;
	union msr_longhaul longhaul;
	union msr_bcr2 bcr2;
	static unsigned int old_ratio=-1;

	if (old_ratio == clock_ratio_index)
		return;
	old_ratio = clock_ratio_index;

	mult = clock_ratio[clock_ratio_index];
	if (mult == -1)
		return;

	speed = calc_speed(mult);
	if ((speed > highest_speed) || (speed < lowest_speed))
		return;

	freqs.old = calc_speed(longhaul_get_cpu_mult());
	freqs.new = speed;
	freqs.cpu = 0; /* longhaul.c is UP only driver */

	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	dprintk ("Setting to FSB:%dMHz Mult:%d.%dx (%s)\n",
			fsb, mult/10, mult%10, print_speed(speed/1000));

	switch (longhaul_version) {

	/*
	 * Longhaul v1. (Samuel[C5A] and Samuel2 stepping 0[C5B])
	 * Software controlled multipliers only.
	 *
	 * *NB* Until we get voltage scaling working v1 & v2 are the same code.
	 * Longhaul v2 appears in Samuel2 Steppings 1->7 [C5b] and Ezra [C5C]
	 */
	case TYPE_LONGHAUL_V1:
	case TYPE_LONGHAUL_V2:
		rdmsrl (MSR_VIA_BCR2, bcr2.val);
		/* Enable software clock multiplier */
		bcr2.bits.ESOFTBF = 1;
		bcr2.bits.CLOCKMUL = clock_ratio_index;
		local_irq_disable();
		wrmsrl (MSR_VIA_BCR2, bcr2.val);
		local_irq_enable();

		__hlt();

		/* Disable software clock multiplier */
		rdmsrl (MSR_VIA_BCR2, bcr2.val);
		bcr2.bits.ESOFTBF = 0;
		local_irq_disable();
		wrmsrl (MSR_VIA_BCR2, bcr2.val);
		local_irq_enable();
		break;

	/*
	 * Longhaul v3 (aka Powersaver). (Ezra-T [C5M] & Nehemiah [C5N])
	 * We can scale voltage with this too, but that's currently
	 * disabled until we come up with a decent 'match freq to voltage'
	 * algorithm.
	 * When we add voltage scaling, we will also need to do the
	 * voltage/freq setting in order depending on the direction
	 * of scaling (like we do in powernow-k7.c)
	 * Nehemiah can do FSB scaling too, but this has never been proven
	 * to work in practice.
	 */
	case TYPE_POWERSAVER:
		do_powersaver(&longhaul, clock_ratio_index);
		break;
	}

	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);
}

/*
 * Centaur decided to make life a little more tricky.
 * Only longhaul v1 is allowed to read EBLCR BSEL[0:1].
 * Samuel2 and above have to try and guess what the FSB is.
 * We do this by assuming we booted at maximum multiplier, and interpolate
 * between that value multiplied by possible FSBs and cpu_mhz which
 * was calculated at boot time. Really ugly, but no other way to do this.
 */

#define ROUNDING	0xf

static int _guess(int guess)
{
	int target;

	target = ((maxmult/10)*guess);
	if (maxmult%10 != 0)
		target += (guess/2);
	target += ROUNDING/2;
	target &= ~ROUNDING;
	return target;
}


static int guess_fsb(void)
{
	int speed = (cpu_khz/1000);
	int i;
	int speeds[3] = { 66, 100, 133 };

	speed += ROUNDING/2;
	speed &= ~ROUNDING;

	for (i=0; i<3; i++) {
		if (_guess(speeds[i]) == speed)
			return speeds[i];
	}
	return 0;
}


static int __init longhaul_get_ranges(void)
{
	unsigned long invalue;
	unsigned int multipliers[32]= {
		50,30,40,100,55,35,45,95,90,70,80,60,120,75,85,65,
		-1,110,120,-1,135,115,125,105,130,150,160,140,-1,155,-1,145 };
	unsigned int j, k = 0;
	union msr_longhaul longhaul;
	unsigned long lo, hi;
	unsigned int eblcr_fsb_table_v1[] = { 66, 133, 100, -1 };
	unsigned int eblcr_fsb_table_v2[] = { 133, 100, -1, 66 };

	switch (longhaul_version) {
	case TYPE_LONGHAUL_V1:
	case TYPE_LONGHAUL_V2:
		/* Ugh, Longhaul v1 didn't have the min/max MSRs.
		   Assume min=3.0x & max = whatever we booted at. */
		minmult = 30;
		maxmult = longhaul_get_cpu_mult();
		rdmsr (MSR_IA32_EBL_CR_POWERON, lo, hi);
		invalue = (lo & (1<<18|1<<19)) >>18;
		if (cpu_model==CPU_SAMUEL || cpu_model==CPU_SAMUEL2)
			fsb = eblcr_fsb_table_v1[invalue];
		else
			fsb = guess_fsb();
		break;

	case TYPE_POWERSAVER:
		/* Ezra-T */
		if (cpu_model==CPU_EZRA_T) {
			rdmsrl (MSR_VIA_LONGHAUL, longhaul.val);
			invalue = longhaul.bits.MaxMHzBR;
			if (longhaul.bits.MaxMHzBR4)
				invalue += 16;
			maxmult=multipliers[invalue];

			invalue = longhaul.bits.MinMHzBR;
			if (longhaul.bits.MinMHzBR4 == 1)
				minmult = 30;
			else
				minmult = multipliers[invalue];
			fsb = eblcr_fsb_table_v2[longhaul.bits.MaxMHzFSB];
			break;
		}

		/* Nehemiah */
		if (cpu_model==CPU_NEHEMIAH) {
			rdmsrl (MSR_VIA_LONGHAUL, longhaul.val);

			/*
			 * TODO: This code works, but raises a lot of questions.
			 * - Some Nehemiah's seem to have broken Min/MaxMHzBR's.
			 *   We get around this by using a hardcoded multiplier of 4.0x
			 *   for the minimimum speed, and the speed we booted up at for the max.
			 *   This is done in longhaul_get_cpu_mult() by reading the EBLCR register.
			 * - According to some VIA documentation EBLCR is only
			 *   in pre-Nehemiah C3s. How this still works is a mystery.
			 *   We're possibly using something undocumented and unsupported,
			 *   But it works, so we don't grumble.
			 */
			minmult=40;
			maxmult=longhaul_get_cpu_mult();

			/* Starting with the 1.2GHz parts, theres a 200MHz bus. */
			if ((cpu_khz/1000) > 1200)
				fsb = 200;
			else
				fsb = eblcr_fsb_table_v2[longhaul.bits.MaxMHzFSB];
			break;
		}
	}

	dprintk ("MinMult:%d.%dx MaxMult:%d.%dx\n",
		 minmult/10, minmult%10, maxmult/10, maxmult%10);

	if (fsb == -1) {
		printk (KERN_INFO PFX "Invalid (reserved) FSB!\n");
		return -EINVAL;
	}

	highest_speed = calc_speed(maxmult);
	lowest_speed = calc_speed(minmult);
	dprintk ("FSB:%dMHz  Lowest speed: %s   Highest speed:%s\n", fsb,
		 print_speed(lowest_speed/1000), 
		 print_speed(highest_speed/1000));

	if (lowest_speed == highest_speed) {
		printk (KERN_INFO PFX "highestspeed == lowest, aborting.\n");
		return -EINVAL;
	}
	if (lowest_speed > highest_speed) {
		printk (KERN_INFO PFX "nonsense! lowest (%d > %d) !\n",
			lowest_speed, highest_speed);
		return -EINVAL;
	}

	longhaul_table = kmalloc((numscales + 1) * sizeof(struct cpufreq_frequency_table), GFP_KERNEL);
	if(!longhaul_table)
		return -ENOMEM;

	for (j=0; j < numscales; j++) {
		unsigned int ratio;
		ratio = clock_ratio[j];
		if (ratio == -1)
			continue;
		if (ratio > maxmult || ratio < minmult)
			continue;
		longhaul_table[k].frequency = calc_speed(ratio);
		longhaul_table[k].index	= j;
		k++;
	}

	longhaul_table[k].frequency = CPUFREQ_TABLE_END;
	if (!k) {
		kfree (longhaul_table);
		return -EINVAL;
	}

	return 0;
}


static void __init longhaul_setup_voltagescaling(void)
{
	union msr_longhaul longhaul;

	rdmsrl (MSR_VIA_LONGHAUL, longhaul.val);

	if (!(longhaul.bits.RevisionID & 1))
		return;

	minvid = longhaul.bits.MinimumVID;
	maxvid = longhaul.bits.MaximumVID;
	vrmrev = longhaul.bits.VRMRev;

	if (minvid == 0 || maxvid == 0) {
		printk (KERN_INFO PFX "Bogus values Min:%d.%03d Max:%d.%03d. "
					"Voltage scaling disabled.\n",
					minvid/1000, minvid%1000, maxvid/1000, maxvid%1000);
		return;
	}

	if (minvid == maxvid) {
		printk (KERN_INFO PFX "Claims to support voltage scaling but min & max are "
				"both %d.%03d. Voltage scaling disabled\n",
				maxvid/1000, maxvid%1000);
		return;
	}

	if (vrmrev==0) {
		dprintk ("VRM 8.5 \n");
		memcpy (voltage_table, vrm85scales, sizeof(voltage_table));
		numvscales = (voltage_table[maxvid]-voltage_table[minvid])/25;
	} else {
		dprintk ("Mobile VRM \n");
		memcpy (voltage_table, mobilevrmscales, sizeof(voltage_table));
		numvscales = (voltage_table[maxvid]-voltage_table[minvid])/5;
	}

	/* Current voltage isn't readable at first, so we need to
	   set it to a known value. The spec says to use maxvid */
	longhaul.bits.RevisionKey = longhaul.bits.RevisionID;	/* FIXME: This is bad. */
	longhaul.bits.EnableSoftVID = 1;
	longhaul.bits.SoftVID = maxvid;
	wrmsrl (MSR_VIA_LONGHAUL, longhaul.val);

	minvid = voltage_table[minvid];
	maxvid = voltage_table[maxvid];

	dprintk ("Min VID=%d.%03d Max VID=%d.%03d, %d possible voltage scales\n",
		maxvid/1000, maxvid%1000, minvid/1000, minvid%1000, numvscales);

	can_scale_voltage = 1;
}


static int longhaul_verify(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy, longhaul_table);
}


static int longhaul_target(struct cpufreq_policy *policy,
			    unsigned int target_freq, unsigned int relation)
{
	unsigned int table_index = 0;
	unsigned int new_clock_ratio = 0;

	if (cpufreq_frequency_table_target(policy, longhaul_table, target_freq, relation, &table_index))
		return -EINVAL;

	new_clock_ratio = longhaul_table[table_index].index & 0xFF;

	longhaul_setstate(new_clock_ratio);

	return 0;
}


static unsigned int longhaul_get(unsigned int cpu)
{
	if (cpu)
		return 0;
	return calc_speed(longhaul_get_cpu_mult());
}


static int __init longhaul_cpu_init(struct cpufreq_policy *policy)
{
	struct cpuinfo_x86 *c = cpu_data;
	char *cpuname=NULL;
	int ret;

	switch (c->x86_model) {
	case 6:
		cpu_model = CPU_SAMUEL;
		cpuname = "C3 'Samuel' [C5A]";
		longhaul_version = TYPE_LONGHAUL_V1;
		memcpy (clock_ratio, samuel1_clock_ratio, sizeof(samuel1_clock_ratio));
		memcpy (eblcr_table, samuel1_eblcr, sizeof(samuel1_eblcr));
		break;

	case 7:
		longhaul_version = TYPE_LONGHAUL_V1;
		switch (c->x86_mask) {
		case 0:
			cpu_model = CPU_SAMUEL2;
			cpuname = "C3 'Samuel 2' [C5B]";
			/* Note, this is not a typo, early Samuel2's had Samuel1 ratios. */
			memcpy (clock_ratio, samuel1_clock_ratio, sizeof(samuel1_clock_ratio));
			memcpy (eblcr_table, samuel2_eblcr, sizeof(samuel2_eblcr));
			break;
		case 1 ... 15:
			if (c->x86_mask < 8) {
				cpu_model = CPU_SAMUEL2;
				cpuname = "C3 'Samuel 2' [C5B]";
			} else {
				cpu_model = CPU_EZRA;
				cpuname = "C3 'Ezra' [C5C]";
			}
			memcpy (clock_ratio, ezra_clock_ratio, sizeof(ezra_clock_ratio));
			memcpy (eblcr_table, ezra_eblcr, sizeof(ezra_eblcr));
			break;
		}
		break;

	case 8:
		cpu_model = CPU_EZRA_T;
		cpuname = "C3 'Ezra-T' [C5M]";
		longhaul_version = TYPE_POWERSAVER;
		numscales=32;
		memcpy (clock_ratio, ezrat_clock_ratio, sizeof(ezrat_clock_ratio));
		memcpy (eblcr_table, ezrat_eblcr, sizeof(ezrat_eblcr));
		break;

	case 9:
		cpu_model = CPU_NEHEMIAH;
		longhaul_version = TYPE_POWERSAVER;
		numscales=32;
		switch (c->x86_mask) {
		case 0 ... 1:
			cpuname = "C3 'Nehemiah A' [C5N]";
			memcpy (clock_ratio, nehemiah_a_clock_ratio, sizeof(nehemiah_a_clock_ratio));
			memcpy (eblcr_table, nehemiah_a_eblcr, sizeof(nehemiah_a_eblcr));
			break;
		case 2 ... 4:
			cpuname = "C3 'Nehemiah B' [C5N]";
			memcpy (clock_ratio, nehemiah_b_clock_ratio, sizeof(nehemiah_b_clock_ratio));
			memcpy (eblcr_table, nehemiah_b_eblcr, sizeof(nehemiah_b_eblcr));
			break;
		case 5 ... 15:
			cpuname = "C3 'Nehemiah C' [C5N]";
			memcpy (clock_ratio, nehemiah_c_clock_ratio, sizeof(nehemiah_c_clock_ratio));
			memcpy (eblcr_table, nehemiah_c_eblcr, sizeof(nehemiah_c_eblcr));
			break;
		}
		break;

	default:
		cpuname = "Unknown";
		break;
	}

	printk (KERN_INFO PFX "VIA %s CPU detected.  ", cpuname);
	switch (longhaul_version) {
	case TYPE_LONGHAUL_V1:
	case TYPE_LONGHAUL_V2:
		printk ("Longhaul v%d supported.\n", longhaul_version);
		break;
	case TYPE_POWERSAVER:
		printk ("Powersaver supported.\n");
		break;
	};

	ret = longhaul_get_ranges();
	if (ret != 0)
		return ret;

	if ((longhaul_version==TYPE_LONGHAUL_V2 || longhaul_version==TYPE_POWERSAVER) &&
		 (dont_scale_voltage==0))
		longhaul_setup_voltagescaling();

	policy->governor = CPUFREQ_DEFAULT_GOVERNOR;
	policy->cpuinfo.transition_latency = CPUFREQ_ETERNAL;
	policy->cur = calc_speed(longhaul_get_cpu_mult());

	ret = cpufreq_frequency_table_cpuinfo(policy, longhaul_table);
	if (ret)
		return ret;

	cpufreq_frequency_table_get_attr(longhaul_table, policy->cpu);

	return 0;
}

static int __devexit longhaul_cpu_exit(struct cpufreq_policy *policy)
{
	cpufreq_frequency_table_put_attr(policy->cpu);
	return 0;
}

static struct freq_attr* longhaul_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

static struct cpufreq_driver longhaul_driver = {
	.verify	= longhaul_verify,
	.target	= longhaul_target,
	.get	= longhaul_get,
	.init	= longhaul_cpu_init,
	.exit	= __devexit_p(longhaul_cpu_exit),
	.name	= "longhaul",
	.owner	= THIS_MODULE,
	.attr	= longhaul_attr,
};


static int __init longhaul_init(void)
{
	struct cpuinfo_x86 *c = cpu_data;

	if (c->x86_vendor != X86_VENDOR_CENTAUR || c->x86 != 6)
		return -ENODEV;

	switch (c->x86_model) {
	case 6 ... 9:
		return cpufreq_register_driver(&longhaul_driver);
	default:
		printk (KERN_INFO PFX "Unknown VIA CPU. Contact davej@codemonkey.org.uk\n");
	}

	return -ENODEV;
}


static void __exit longhaul_exit(void)
{
	int i=0;

	for (i=0; i < numscales; i++) {
		if (clock_ratio[i] == maxmult) {
			longhaul_setstate(i);
			break;
		}
	}

	cpufreq_unregister_driver(&longhaul_driver);
	kfree(longhaul_table);
}

module_param (dont_scale_voltage, int, 0644);
MODULE_PARM_DESC(dont_scale_voltage, "Don't scale voltage of processor");

MODULE_AUTHOR ("Dave Jones <davej@codemonkey.org.uk>");
MODULE_DESCRIPTION ("Longhaul driver for VIA Cyrix processors.");
MODULE_LICENSE ("GPL");

module_init(longhaul_init);
module_exit(longhaul_exit);

