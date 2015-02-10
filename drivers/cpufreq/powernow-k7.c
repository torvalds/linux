/*
 *  AMD K7 Powernow driver.
 *  (C) 2003 Dave Jones on behalf of SuSE Labs.
 *
 *  Licensed under the terms of the GNU GPL License version 2.
 *  Based upon datasheets & sample CPUs kindly provided by AMD.
 *
 * Errata 5:
 *  CPU may fail to execute a FID/VID change in presence of interrupt.
 *  - We cli/sti on stepping A0 CPUs around the FID/VID transition.
 * Errata 15:
 *  CPU with half frequency multipliers may hang upon wakeup from disconnect.
 *  - We disable half multipliers if ACPI is used on A0 stepping CPUs.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/dmi.h>
#include <linux/timex.h>
#include <linux/io.h>

#include <asm/timer.h>		/* Needed for recalibrate_cpu_khz() */
#include <asm/msr.h>
#include <asm/cpu_device_id.h>

#ifdef CONFIG_X86_POWERNOW_K7_ACPI
#include <linux/acpi.h>
#include <acpi/processor.h>
#endif

#include "powernow-k7.h"

#define PFX "powernow: "


struct psb_s {
	u8 signature[10];
	u8 tableversion;
	u8 flags;
	u16 settlingtime;
	u8 reserved1;
	u8 numpst;
};

struct pst_s {
	u32 cpuid;
	u8 fsbspeed;
	u8 maxfid;
	u8 startvid;
	u8 numpstates;
};

#ifdef CONFIG_X86_POWERNOW_K7_ACPI
union powernow_acpi_control_t {
	struct {
		unsigned long fid:5,
			vid:5,
			sgtc:20,
			res1:2;
	} bits;
	unsigned long val;
};
#endif

/* divide by 1000 to get VCore voltage in V. */
static const int mobile_vid_table[32] = {
    2000, 1950, 1900, 1850, 1800, 1750, 1700, 1650,
    1600, 1550, 1500, 1450, 1400, 1350, 1300, 0,
    1275, 1250, 1225, 1200, 1175, 1150, 1125, 1100,
    1075, 1050, 1025, 1000, 975, 950, 925, 0,
};

/* divide by 10 to get FID. */
static const int fid_codes[32] = {
    110, 115, 120, 125, 50, 55, 60, 65,
    70, 75, 80, 85, 90, 95, 100, 105,
    30, 190, 40, 200, 130, 135, 140, 210,
    150, 225, 160, 165, 170, 180, -1, -1,
};

/* This parameter is used in order to force ACPI instead of legacy method for
 * configuration purpose.
 */

static int acpi_force;

static struct cpufreq_frequency_table *powernow_table;

static unsigned int can_scale_bus;
static unsigned int can_scale_vid;
static unsigned int minimum_speed = -1;
static unsigned int maximum_speed;
static unsigned int number_scales;
static unsigned int fsb;
static unsigned int latency;
static char have_a0;

static int check_fsb(unsigned int fsbspeed)
{
	int delta;
	unsigned int f = fsb / 1000;

	delta = (fsbspeed > f) ? fsbspeed - f : f - fsbspeed;
	return delta < 5;
}

static const struct x86_cpu_id powernow_k7_cpuids[] = {
	{ X86_VENDOR_AMD, 6, },
	{}
};
MODULE_DEVICE_TABLE(x86cpu, powernow_k7_cpuids);

static int check_powernow(void)
{
	struct cpuinfo_x86 *c = &cpu_data(0);
	unsigned int maxei, eax, ebx, ecx, edx;

	if (!x86_match_cpu(powernow_k7_cpuids))
		return 0;

	/* Get maximum capabilities */
	maxei = cpuid_eax(0x80000000);
	if (maxei < 0x80000007) {	/* Any powernow info ? */
#ifdef MODULE
		printk(KERN_INFO PFX "No powernow capabilities detected\n");
#endif
		return 0;
	}

	if ((c->x86_model == 6) && (c->x86_mask == 0)) {
		printk(KERN_INFO PFX "K7 660[A0] core detected, "
				"enabling errata workarounds\n");
		have_a0 = 1;
	}

	cpuid(0x80000007, &eax, &ebx, &ecx, &edx);

	/* Check we can actually do something before we say anything.*/
	if (!(edx & (1 << 1 | 1 << 2)))
		return 0;

	printk(KERN_INFO PFX "PowerNOW! Technology present. Can scale: ");

	if (edx & 1 << 1) {
		printk("frequency");
		can_scale_bus = 1;
	}

	if ((edx & (1 << 1 | 1 << 2)) == 0x6)
		printk(" and ");

	if (edx & 1 << 2) {
		printk("voltage");
		can_scale_vid = 1;
	}

	printk(".\n");
	return 1;
}

#ifdef CONFIG_X86_POWERNOW_K7_ACPI
static void invalidate_entry(unsigned int entry)
{
	powernow_table[entry].frequency = CPUFREQ_ENTRY_INVALID;
}
#endif

static int get_ranges(unsigned char *pst)
{
	unsigned int j;
	unsigned int speed;
	u8 fid, vid;

	powernow_table = kzalloc((sizeof(*powernow_table) *
				(number_scales + 1)), GFP_KERNEL);
	if (!powernow_table)
		return -ENOMEM;

	for (j = 0 ; j < number_scales; j++) {
		fid = *pst++;

		powernow_table[j].frequency = (fsb * fid_codes[fid]) / 10;
		powernow_table[j].driver_data = fid; /* lower 8 bits */

		speed = powernow_table[j].frequency;

		if ((fid_codes[fid] % 10) == 5) {
#ifdef CONFIG_X86_POWERNOW_K7_ACPI
			if (have_a0 == 1)
				invalidate_entry(j);
#endif
		}

		if (speed < minimum_speed)
			minimum_speed = speed;
		if (speed > maximum_speed)
			maximum_speed = speed;

		vid = *pst++;
		powernow_table[j].driver_data |= (vid << 8); /* upper 8 bits */

		pr_debug("   FID: 0x%x (%d.%dx [%dMHz])  "
			 "VID: 0x%x (%d.%03dV)\n", fid, fid_codes[fid] / 10,
			 fid_codes[fid] % 10, speed/1000, vid,
			 mobile_vid_table[vid]/1000,
			 mobile_vid_table[vid]%1000);
	}
	powernow_table[number_scales].frequency = CPUFREQ_TABLE_END;
	powernow_table[number_scales].driver_data = 0;

	return 0;
}


static void change_FID(int fid)
{
	union msr_fidvidctl fidvidctl;

	rdmsrl(MSR_K7_FID_VID_CTL, fidvidctl.val);
	if (fidvidctl.bits.FID != fid) {
		fidvidctl.bits.SGTC = latency;
		fidvidctl.bits.FID = fid;
		fidvidctl.bits.VIDC = 0;
		fidvidctl.bits.FIDC = 1;
		wrmsrl(MSR_K7_FID_VID_CTL, fidvidctl.val);
	}
}


static void change_VID(int vid)
{
	union msr_fidvidctl fidvidctl;

	rdmsrl(MSR_K7_FID_VID_CTL, fidvidctl.val);
	if (fidvidctl.bits.VID != vid) {
		fidvidctl.bits.SGTC = latency;
		fidvidctl.bits.VID = vid;
		fidvidctl.bits.FIDC = 0;
		fidvidctl.bits.VIDC = 1;
		wrmsrl(MSR_K7_FID_VID_CTL, fidvidctl.val);
	}
}


static int powernow_target(struct cpufreq_policy *policy, unsigned int index)
{
	u8 fid, vid;
	struct cpufreq_freqs freqs;
	union msr_fidvidstatus fidvidstatus;
	int cfid;

	/* fid are the lower 8 bits of the index we stored into
	 * the cpufreq frequency table in powernow_decode_bios,
	 * vid are the upper 8 bits.
	 */

	fid = powernow_table[index].driver_data & 0xFF;
	vid = (powernow_table[index].driver_data & 0xFF00) >> 8;

	rdmsrl(MSR_K7_FID_VID_STATUS, fidvidstatus.val);
	cfid = fidvidstatus.bits.CFID;
	freqs.old = fsb * fid_codes[cfid] / 10;

	freqs.new = powernow_table[index].frequency;

	/* Now do the magic poking into the MSRs.  */

	if (have_a0 == 1)	/* A0 errata 5 */
		local_irq_disable();

	if (freqs.old > freqs.new) {
		/* Going down, so change FID first */
		change_FID(fid);
		change_VID(vid);
	} else {
		/* Going up, so change VID first */
		change_VID(vid);
		change_FID(fid);
	}


	if (have_a0 == 1)
		local_irq_enable();

	return 0;
}


#ifdef CONFIG_X86_POWERNOW_K7_ACPI

static struct acpi_processor_performance *acpi_processor_perf;

static int powernow_acpi_init(void)
{
	int i;
	int retval = 0;
	union powernow_acpi_control_t pc;

	if (acpi_processor_perf != NULL && powernow_table != NULL) {
		retval = -EINVAL;
		goto err0;
	}

	acpi_processor_perf = kzalloc(sizeof(*acpi_processor_perf), GFP_KERNEL);
	if (!acpi_processor_perf) {
		retval = -ENOMEM;
		goto err0;
	}

	if (!zalloc_cpumask_var(&acpi_processor_perf->shared_cpu_map,
								GFP_KERNEL)) {
		retval = -ENOMEM;
		goto err05;
	}

	if (acpi_processor_register_performance(acpi_processor_perf, 0)) {
		retval = -EIO;
		goto err1;
	}

	if (acpi_processor_perf->control_register.space_id !=
			ACPI_ADR_SPACE_FIXED_HARDWARE) {
		retval = -ENODEV;
		goto err2;
	}

	if (acpi_processor_perf->status_register.space_id !=
			ACPI_ADR_SPACE_FIXED_HARDWARE) {
		retval = -ENODEV;
		goto err2;
	}

	number_scales = acpi_processor_perf->state_count;

	if (number_scales < 2) {
		retval = -ENODEV;
		goto err2;
	}

	powernow_table = kzalloc((sizeof(*powernow_table) *
				(number_scales + 1)), GFP_KERNEL);
	if (!powernow_table) {
		retval = -ENOMEM;
		goto err2;
	}

	pc.val = (unsigned long) acpi_processor_perf->states[0].control;
	for (i = 0; i < number_scales; i++) {
		u8 fid, vid;
		struct acpi_processor_px *state =
			&acpi_processor_perf->states[i];
		unsigned int speed, speed_mhz;

		pc.val = (unsigned long) state->control;
		pr_debug("acpi:  P%d: %d MHz %d mW %d uS control %08x SGTC %d\n",
			 i,
			 (u32) state->core_frequency,
			 (u32) state->power,
			 (u32) state->transition_latency,
			 (u32) state->control,
			 pc.bits.sgtc);

		vid = pc.bits.vid;
		fid = pc.bits.fid;

		powernow_table[i].frequency = fsb * fid_codes[fid] / 10;
		powernow_table[i].driver_data = fid; /* lower 8 bits */
		powernow_table[i].driver_data |= (vid << 8); /* upper 8 bits */

		speed = powernow_table[i].frequency;
		speed_mhz = speed / 1000;

		/* processor_perflib will multiply the MHz value by 1000 to
		 * get a KHz value (e.g. 1266000). However, powernow-k7 works
		 * with true KHz values (e.g. 1266768). To ensure that all
		 * powernow frequencies are available, we must ensure that
		 * ACPI doesn't restrict them, so we round up the MHz value
		 * to ensure that perflib's computed KHz value is greater than
		 * or equal to powernow's KHz value.
		 */
		if (speed % 1000 > 0)
			speed_mhz++;

		if ((fid_codes[fid] % 10) == 5) {
			if (have_a0 == 1)
				invalidate_entry(i);
		}

		pr_debug("   FID: 0x%x (%d.%dx [%dMHz])  "
			 "VID: 0x%x (%d.%03dV)\n", fid, fid_codes[fid] / 10,
			 fid_codes[fid] % 10, speed_mhz, vid,
			 mobile_vid_table[vid]/1000,
			 mobile_vid_table[vid]%1000);

		if (state->core_frequency != speed_mhz) {
			state->core_frequency = speed_mhz;
			pr_debug("   Corrected ACPI frequency to %d\n",
				speed_mhz);
		}

		if (latency < pc.bits.sgtc)
			latency = pc.bits.sgtc;

		if (speed < minimum_speed)
			minimum_speed = speed;
		if (speed > maximum_speed)
			maximum_speed = speed;
	}

	powernow_table[i].frequency = CPUFREQ_TABLE_END;
	powernow_table[i].driver_data = 0;

	/* notify BIOS that we exist */
	acpi_processor_notify_smm(THIS_MODULE);

	return 0;

err2:
	acpi_processor_unregister_performance(acpi_processor_perf, 0);
err1:
	free_cpumask_var(acpi_processor_perf->shared_cpu_map);
err05:
	kfree(acpi_processor_perf);
err0:
	printk(KERN_WARNING PFX "ACPI perflib can not be used on "
			"this platform\n");
	acpi_processor_perf = NULL;
	return retval;
}
#else
static int powernow_acpi_init(void)
{
	printk(KERN_INFO PFX "no support for ACPI processor found."
	       "  Please recompile your kernel with ACPI processor\n");
	return -EINVAL;
}
#endif

static void print_pst_entry(struct pst_s *pst, unsigned int j)
{
	pr_debug("PST:%d (@%p)\n", j, pst);
	pr_debug(" cpuid: 0x%x  fsb: %d  maxFID: 0x%x  startvid: 0x%x\n",
		pst->cpuid, pst->fsbspeed, pst->maxfid, pst->startvid);
}

static int powernow_decode_bios(int maxfid, int startvid)
{
	struct psb_s *psb;
	struct pst_s *pst;
	unsigned int i, j;
	unsigned char *p;
	unsigned int etuple;
	unsigned int ret;

	etuple = cpuid_eax(0x80000001);

	for (i = 0xC0000; i < 0xffff0 ; i += 16) {

		p = phys_to_virt(i);

		if (memcmp(p, "AMDK7PNOW!",  10) == 0) {
			pr_debug("Found PSB header at %p\n", p);
			psb = (struct psb_s *) p;
			pr_debug("Table version: 0x%x\n", psb->tableversion);
			if (psb->tableversion != 0x12) {
				printk(KERN_INFO PFX "Sorry, only v1.2 tables"
						" supported right now\n");
				return -ENODEV;
			}

			pr_debug("Flags: 0x%x\n", psb->flags);
			if ((psb->flags & 1) == 0)
				pr_debug("Mobile voltage regulator\n");
			else
				pr_debug("Desktop voltage regulator\n");

			latency = psb->settlingtime;
			if (latency < 100) {
				printk(KERN_INFO PFX "BIOS set settling time "
						"to %d microseconds. "
						"Should be at least 100. "
						"Correcting.\n", latency);
				latency = 100;
			}
			pr_debug("Settling Time: %d microseconds.\n",
					psb->settlingtime);
			pr_debug("Has %d PST tables. (Only dumping ones "
					"relevant to this CPU).\n",
					psb->numpst);

			p += sizeof(*psb);

			pst = (struct pst_s *) p;

			for (j = 0; j < psb->numpst; j++) {
				pst = (struct pst_s *) p;
				number_scales = pst->numpstates;

				if ((etuple == pst->cpuid) &&
				    check_fsb(pst->fsbspeed) &&
				    (maxfid == pst->maxfid) &&
				    (startvid == pst->startvid)) {
					print_pst_entry(pst, j);
					p = (char *)pst + sizeof(*pst);
					ret = get_ranges(p);
					return ret;
				} else {
					unsigned int k;
					p = (char *)pst + sizeof(*pst);
					for (k = 0; k < number_scales; k++)
						p += 2;
				}
			}
			printk(KERN_INFO PFX "No PST tables match this cpuid "
					"(0x%x)\n", etuple);
			printk(KERN_INFO PFX "This is indicative of a broken "
					"BIOS.\n");

			return -EINVAL;
		}
		p++;
	}

	return -ENODEV;
}


/*
 * We use the fact that the bus frequency is somehow
 * a multiple of 100000/3 khz, then we compute sgtc according
 * to this multiple.
 * That way, we match more how AMD thinks all of that work.
 * We will then get the same kind of behaviour already tested under
 * the "well-known" other OS.
 */
static int fixup_sgtc(void)
{
	unsigned int sgtc;
	unsigned int m;

	m = fsb / 3333;
	if ((m % 10) >= 5)
		m += 5;

	m /= 10;

	sgtc = 100 * m * latency;
	sgtc = sgtc / 3;
	if (sgtc > 0xfffff) {
		printk(KERN_WARNING PFX "SGTC too large %d\n", sgtc);
		sgtc = 0xfffff;
	}
	return sgtc;
}

static unsigned int powernow_get(unsigned int cpu)
{
	union msr_fidvidstatus fidvidstatus;
	unsigned int cfid;

	if (cpu)
		return 0;
	rdmsrl(MSR_K7_FID_VID_STATUS, fidvidstatus.val);
	cfid = fidvidstatus.bits.CFID;

	return fsb * fid_codes[cfid] / 10;
}


static int acer_cpufreq_pst(const struct dmi_system_id *d)
{
	printk(KERN_WARNING PFX
		"%s laptop with broken PST tables in BIOS detected.\n",
		d->ident);
	printk(KERN_WARNING PFX
		"You need to downgrade to 3A21 (09/09/2002), or try a newer "
		"BIOS than 3A71 (01/20/2003)\n");
	printk(KERN_WARNING PFX
		"cpufreq scaling has been disabled as a result of this.\n");
	return 0;
}

/*
 * Some Athlon laptops have really fucked PST tables.
 * A BIOS update is all that can save them.
 * Mention this, and disable cpufreq.
 */
static struct dmi_system_id powernow_dmi_table[] = {
	{
		.callback = acer_cpufreq_pst,
		.ident = "Acer Aspire",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Insyde Software"),
			DMI_MATCH(DMI_BIOS_VERSION, "3A71"),
		},
	},
	{ }
};

static int powernow_cpu_init(struct cpufreq_policy *policy)
{
	union msr_fidvidstatus fidvidstatus;
	int result;

	if (policy->cpu != 0)
		return -ENODEV;

	rdmsrl(MSR_K7_FID_VID_STATUS, fidvidstatus.val);

	recalibrate_cpu_khz();

	fsb = (10 * cpu_khz) / fid_codes[fidvidstatus.bits.CFID];
	if (!fsb) {
		printk(KERN_WARNING PFX "can not determine bus frequency\n");
		return -EINVAL;
	}
	pr_debug("FSB: %3dMHz\n", fsb/1000);

	if (dmi_check_system(powernow_dmi_table) || acpi_force) {
		printk(KERN_INFO PFX "PSB/PST known to be broken.  "
				"Trying ACPI instead\n");
		result = powernow_acpi_init();
	} else {
		result = powernow_decode_bios(fidvidstatus.bits.MFID,
				fidvidstatus.bits.SVID);
		if (result) {
			printk(KERN_INFO PFX "Trying ACPI perflib\n");
			maximum_speed = 0;
			minimum_speed = -1;
			latency = 0;
			result = powernow_acpi_init();
			if (result) {
				printk(KERN_INFO PFX
					"ACPI and legacy methods failed\n");
			}
		} else {
			/* SGTC use the bus clock as timer */
			latency = fixup_sgtc();
			printk(KERN_INFO PFX "SGTC: %d\n", latency);
		}
	}

	if (result)
		return result;

	printk(KERN_INFO PFX "Minimum speed %d MHz. Maximum speed %d MHz.\n",
				minimum_speed/1000, maximum_speed/1000);

	policy->cpuinfo.transition_latency =
		cpufreq_scale(2000000UL, fsb, latency);

	return cpufreq_table_validate_and_show(policy, powernow_table);
}

static int powernow_cpu_exit(struct cpufreq_policy *policy)
{
#ifdef CONFIG_X86_POWERNOW_K7_ACPI
	if (acpi_processor_perf) {
		acpi_processor_unregister_performance(acpi_processor_perf, 0);
		free_cpumask_var(acpi_processor_perf->shared_cpu_map);
		kfree(acpi_processor_perf);
	}
#endif

	kfree(powernow_table);
	return 0;
}

static struct cpufreq_driver powernow_driver = {
	.verify		= cpufreq_generic_frequency_table_verify,
	.target_index	= powernow_target,
	.get		= powernow_get,
#ifdef CONFIG_X86_POWERNOW_K7_ACPI
	.bios_limit	= acpi_processor_get_bios_limit,
#endif
	.init		= powernow_cpu_init,
	.exit		= powernow_cpu_exit,
	.name		= "powernow-k7",
	.attr		= cpufreq_generic_attr,
};

static int __init powernow_init(void)
{
	if (check_powernow() == 0)
		return -ENODEV;
	return cpufreq_register_driver(&powernow_driver);
}


static void __exit powernow_exit(void)
{
	cpufreq_unregister_driver(&powernow_driver);
}

module_param(acpi_force,  int, 0444);
MODULE_PARM_DESC(acpi_force, "Force ACPI to be used.");

MODULE_AUTHOR("Dave Jones");
MODULE_DESCRIPTION("Powernow driver for AMD K7 processors.");
MODULE_LICENSE("GPL");

late_initcall(powernow_init);
module_exit(powernow_exit);

