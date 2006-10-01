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
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <asm/msr.h>
#include <asm/timex.h>
#include <asm/io.h>
#include <asm/acpi.h>
#include <linux/acpi.h>
#include <acpi/processor.h>

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
static unsigned int numscales=16;
static unsigned int fsb;

static struct mV_pos *vrm_mV_table;
static unsigned char *mV_vrm_table;
struct f_msr {
	unsigned char vrm;
};
static struct f_msr f_msr_table[32];

static unsigned int highest_speed, lowest_speed; /* kHz */
static unsigned int minmult, maxmult;
static int can_scale_voltage;
static struct acpi_processor *pr = NULL;
static struct acpi_processor_cx *cx = NULL;
static int port22_en;

/* Module parameters */
static int scale_voltage;
static int ignore_latency;

#define dprintk(msg...) cpufreq_debug_printk(CPUFREQ_DEBUG_DRIVER, "longhaul", msg)


/* Clock ratios multiplied by 10 */
static int clock_ratio[32];
static int eblcr_table[32];
static unsigned int highest_speed, lowest_speed; /* kHz */
static int longhaul_version;
static struct cpufreq_frequency_table *longhaul_table;

#ifdef CONFIG_CPU_FREQ_DEBUG
static char speedbuffer[8];

static char *print_speed(int speed)
{
	if (speed < 1000) {
		snprintf(speedbuffer, sizeof(speedbuffer),"%dMHz", speed);
		return speedbuffer;
	}

	if (speed%1000 == 0)
		snprintf(speedbuffer, sizeof(speedbuffer),
			"%dGHz", speed/1000);
	else
		snprintf(speedbuffer, sizeof(speedbuffer),
			"%d.%dGHz", speed/1000, (speed%1000)/100);

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

/* For processor with BCR2 MSR */

static void do_longhaul1(unsigned int clock_ratio_index)
{
	union msr_bcr2 bcr2;

	rdmsrl(MSR_VIA_BCR2, bcr2.val);
	/* Enable software clock multiplier */
	bcr2.bits.ESOFTBF = 1;
	bcr2.bits.CLOCKMUL = clock_ratio_index;

	/* Sync to timer tick */
	safe_halt();
	/* Change frequency on next halt or sleep */
	wrmsrl(MSR_VIA_BCR2, bcr2.val);
	/* Invoke transition */
	ACPI_FLUSH_CPU_CACHE();
	halt();

	/* Disable software clock multiplier */
	local_irq_disable();
	rdmsrl(MSR_VIA_BCR2, bcr2.val);
	bcr2.bits.ESOFTBF = 0;
	wrmsrl(MSR_VIA_BCR2, bcr2.val);
}

/* For processor with Longhaul MSR */

static void do_powersaver(int cx_address, unsigned int clock_ratio_index)
{
	union msr_longhaul longhaul;
	u32 t;

	rdmsrl(MSR_VIA_LONGHAUL, longhaul.val);
	longhaul.bits.RevisionKey = longhaul.bits.RevisionID;
	longhaul.bits.SoftBusRatio = clock_ratio_index & 0xf;
	longhaul.bits.SoftBusRatio4 = (clock_ratio_index & 0x10) >> 4;
	longhaul.bits.EnableSoftBusRatio = 1;

	if (can_scale_voltage) {
		longhaul.bits.SoftVID = f_msr_table[clock_ratio_index].vrm;
		longhaul.bits.EnableSoftVID = 1;
	}

	/* Sync to timer tick */
	safe_halt();
	/* Change frequency on next halt or sleep */
	wrmsrl(MSR_VIA_LONGHAUL, longhaul.val);
	if (port22_en) {
		ACPI_FLUSH_CPU_CACHE();
		/* Invoke C1 */
		halt();
	} else {
		ACPI_FLUSH_CPU_CACHE();
		/* Invoke C3 */
		inb(cx_address);
		/* Dummy op - must do something useless after P_LVL3 read */
		t = inl(acpi_fadt.xpm_tmr_blk.address);
	}

	/* Disable bus ratio bit */
	local_irq_disable();
	longhaul.bits.RevisionKey = longhaul.bits.RevisionID;
	longhaul.bits.EnableSoftBusRatio = 0;
	longhaul.bits.EnableSoftBSEL = 0;
	longhaul.bits.EnableSoftVID = 0;
	wrmsrl(MSR_VIA_LONGHAUL, longhaul.val);
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
	static unsigned int old_ratio=-1;
	unsigned long flags;
	unsigned int pic1_mask, pic2_mask;

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

	preempt_disable();
	local_irq_save(flags);

	pic2_mask = inb(0xA1);
	pic1_mask = inb(0x21);	/* works on C3. save mask. */
	outb(0xFF,0xA1);	/* Overkill */
	outb(0xFE,0x21);	/* TMR0 only */

	if (pr->flags.bm_control) {
 		/* Disable bus master arbitration */
		acpi_set_register(ACPI_BITREG_ARB_DISABLE, 1,
				  ACPI_MTX_DO_NOT_LOCK);
	} else if (port22_en) {
		/* Disable AGP and PCI arbiters */
		outb(3, 0x22);
	}

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
		do_longhaul1(clock_ratio_index);
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
		/* Don't allow wakeup */
		acpi_set_register(ACPI_BITREG_BUS_MASTER_RLD, 0,
				  ACPI_MTX_DO_NOT_LOCK);
		do_powersaver(cx->address, clock_ratio_index);
		break;
	}

	if (pr->flags.bm_control) {
		/* Enable bus master arbitration */
		acpi_set_register(ACPI_BITREG_ARB_DISABLE, 0,
				  ACPI_MTX_DO_NOT_LOCK);
	} else if (port22_en) {
		/* Enable arbiters */
		outb(0, 0x22);
	}

	outb(pic2_mask,0xA1);	/* restore mask */
	outb(pic1_mask,0x21);

	local_irq_restore(flags);
	preempt_enable();

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
	unsigned int ezra_t_multipliers[32]= {
			90,  30,  40, 100,  55,  35,  45,  95,
			50,  70,  80,  60, 120,  75,  85,  65,
			-1, 110, 120,  -1, 135, 115, 125, 105,
			130, 150, 160, 140,  -1, 155,  -1, 145 };
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
			maxmult=ezra_t_multipliers[invalue];

			invalue = longhaul.bits.MinMHzBR;
			if (longhaul.bits.MinMHzBR4 == 1)
				minmult = 30;
			else
				minmult = ezra_t_multipliers[invalue];
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
	struct mV_pos minvid, maxvid;
	unsigned int j, speed, pos, kHz_step, numvscales;

	rdmsrl(MSR_VIA_LONGHAUL, longhaul.val);
	if (!(longhaul.bits.RevisionID & 1)) {
		printk(KERN_INFO PFX "Voltage scaling not supported by CPU.\n");
		return;
	}

	if (!longhaul.bits.VRMRev) {
		printk (KERN_INFO PFX "VRM 8.5\n");
		vrm_mV_table = &vrm85_mV[0];
		mV_vrm_table = &mV_vrm85[0];
	} else {
		printk (KERN_INFO PFX "Mobile VRM\n");
		vrm_mV_table = &mobilevrm_mV[0];
		mV_vrm_table = &mV_mobilevrm[0];
	}

	minvid = vrm_mV_table[longhaul.bits.MinimumVID];
	maxvid = vrm_mV_table[longhaul.bits.MaximumVID];
	numvscales = maxvid.pos - minvid.pos + 1;
	kHz_step = (highest_speed - lowest_speed) / numvscales;

	if (minvid.mV == 0 || maxvid.mV == 0 || minvid.mV > maxvid.mV) {
		printk (KERN_INFO PFX "Bogus values Min:%d.%03d Max:%d.%03d. "
					"Voltage scaling disabled.\n",
					minvid.mV/1000, minvid.mV%1000, maxvid.mV/1000, maxvid.mV%1000);
		return;
	}

	if (minvid.mV == maxvid.mV) {
		printk (KERN_INFO PFX "Claims to support voltage scaling but min & max are "
				"both %d.%03d. Voltage scaling disabled\n",
				maxvid.mV/1000, maxvid.mV%1000);
		return;
	}

	printk(KERN_INFO PFX "Max VID=%d.%03d  Min VID=%d.%03d, %d possible voltage scales\n",
		maxvid.mV/1000, maxvid.mV%1000,
		minvid.mV/1000, minvid.mV%1000,
		numvscales);
	
	j = 0;
	while (longhaul_table[j].frequency != CPUFREQ_TABLE_END) {
		speed = longhaul_table[j].frequency;
		pos = (speed - lowest_speed) / kHz_step + minvid.pos;
		f_msr_table[longhaul_table[j].index].vrm = mV_vrm_table[pos];
		j++;
	}

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

static acpi_status longhaul_walk_callback(acpi_handle obj_handle,
					  u32 nesting_level,
					  void *context, void **return_value)
{
	struct acpi_device *d;

	if ( acpi_bus_get_device(obj_handle, &d) ) {
		return 0;
	}
	*return_value = (void *)acpi_driver_data(d);
	return 1;
}

/* VIA don't support PM2 reg, but have something similar */
static int enable_arbiter_disable(void)
{
	struct pci_dev *dev;
	int reg;
	u8 pci_cmd;

	/* Find PLE133 host bridge */
	reg = 0x78;
	dev = pci_find_device(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_8601_0, NULL);
	/* Find CLE266 host bridge */
	if (dev == NULL) {
		reg = 0x76;
		dev = pci_find_device(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_862X_0, NULL);
	}
	if (dev != NULL) {
		/* Enable access to port 0x22 */
		pci_read_config_byte(dev, reg, &pci_cmd);
		if ( !(pci_cmd & 1<<7) ) {
			pci_cmd |= 1<<7;
			pci_write_config_byte(dev, reg, pci_cmd);
		}
		return 1;
	}
	return 0;
}

static int __init longhaul_cpu_init(struct cpufreq_policy *policy)
{
	struct cpuinfo_x86 *c = cpu_data;
	char *cpuname=NULL;
	int ret;

	/* Check what we have on this motherboard */
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

	/* Find ACPI data for processor */
	acpi_walk_namespace(ACPI_TYPE_PROCESSOR, ACPI_ROOT_OBJECT, ACPI_UINT32_MAX,
			    &longhaul_walk_callback, NULL, (void *)&pr);
	if (pr == NULL)
		goto err_acpi;

	if (longhaul_version == TYPE_POWERSAVER) {
		/* Check ACPI support for C3 state */
		cx = &pr->power.states[ACPI_STATE_C3];
		if (cx->address > 0 &&
		   (cx->latency <= 1000 || ignore_latency != 0) ) {
			goto print_support_type;
		}
	}
	/* Check ACPI support for bus master arbiter disable */
	if (!pr->flags.bm_control) {
		if (enable_arbiter_disable()) {
			port22_en = 1;
		} else {
			goto err_acpi;
		}
	}
print_support_type:
	if (!port22_en) {
		printk (KERN_INFO PFX "Using ACPI support.\n");
	} else {
		printk (KERN_INFO PFX "Using northbridge support.\n");
	}

	ret = longhaul_get_ranges();
	if (ret != 0)
		return ret;

	if ((longhaul_version==TYPE_LONGHAUL_V2 || longhaul_version==TYPE_POWERSAVER) &&
		 (scale_voltage != 0))
		longhaul_setup_voltagescaling();

	policy->governor = CPUFREQ_DEFAULT_GOVERNOR;
	policy->cpuinfo.transition_latency = 200000;	/* nsec */
	policy->cur = calc_speed(longhaul_get_cpu_mult());

	ret = cpufreq_frequency_table_cpuinfo(policy, longhaul_table);
	if (ret)
		return ret;

	cpufreq_frequency_table_get_attr(longhaul_table, policy->cpu);

	return 0;

err_acpi:
	printk(KERN_ERR PFX "No ACPI support. No VT8601 or VT8623 northbridge. Aborting.\n");
	return -ENODEV;
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

#ifdef CONFIG_SMP
	if (num_online_cpus() > 1) {
		return -ENODEV;
		printk(KERN_ERR PFX "More than 1 CPU detected, longhaul disabled.\n");
	}
#endif
#ifdef CONFIG_X86_IO_APIC
	if (cpu_has_apic) {
		printk(KERN_ERR PFX "APIC detected. Longhaul is currently broken in this configuration.\n");
		return -ENODEV;
	}
#endif
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
	int i;

	for (i=0; i < numscales; i++) {
		if (clock_ratio[i] == maxmult) {
			longhaul_setstate(i);
			break;
		}
	}

	cpufreq_unregister_driver(&longhaul_driver);
	kfree(longhaul_table);
}

module_param (scale_voltage, int, 0644);
MODULE_PARM_DESC(scale_voltage, "Scale voltage of processor");
module_param(ignore_latency, int, 0644);
MODULE_PARM_DESC(ignore_latency, "Skip ACPI C3 latency test");

MODULE_AUTHOR ("Dave Jones <davej@codemonkey.org.uk>");
MODULE_DESCRIPTION ("Longhaul driver for VIA Cyrix processors.");
MODULE_LICENSE ("GPL");

late_initcall(longhaul_init);
module_exit(longhaul_exit);
