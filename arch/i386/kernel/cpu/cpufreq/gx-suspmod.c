/*
 *	Cyrix MediaGX and NatSemi Geode Suspend Modulation
 *	(C) 2002 Zwane Mwaikambo <zwane@commfireservices.com>
 *	(C) 2002 Hiroshi Miura   <miura@da-cha.org>
 *	All Rights Reserved
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      version 2 as published by the Free Software Foundation
 *
 *      The author(s) of this software shall not be held liable for damages
 *      of any nature resulting due to the use of this software. This
 *      software is provided AS-IS with no warranties.
 *
 * Theoritical note:
 *
 *	(see Geode(tm) CS5530 manual (rev.4.1) page.56)
 *
 *	CPU frequency control on NatSemi Geode GX1/GXLV processor and CS55x0
 *	are based on Suspend Moduration.
 *
 *	Suspend Modulation works by asserting and de-asserting the SUSP# pin
 *	to CPU(GX1/GXLV) for configurable durations. When asserting SUSP#
 *	the CPU enters an idle state. GX1 stops its core clock when SUSP# is
 *	asserted then power consumption is reduced.
 *
 *	Suspend Modulation's OFF/ON duration are configurable
 *	with 'Suspend Modulation OFF Count Register'
 *	and 'Suspend Modulation ON Count Register'.
 *	These registers are 8bit counters that represent the number of
 *	32us intervals which the SUSP# pin is asserted(ON)/de-asserted(OFF)
 *	to the processor.
 *
 *	These counters define a ratio which is the effective frequency
 *	of operation of the system.
 *
 *			       OFF Count
 *	F_eff = Fgx * ----------------------
 *	                OFF Count + ON Count
 *
 *	0 <= On Count, Off Count <= 255
 *
 *	From these limits, we can get register values
 *
 *	off_duration + on_duration <= MAX_DURATION
 *	on_duration = off_duration * (stock_freq - freq) / freq
 *
 *      off_duration  =  (freq * DURATION) / stock_freq
 *      on_duration = DURATION - off_duration
 *
 *
 *---------------------------------------------------------------------------
 *
 * ChangeLog:
 *	Dec. 12, 2003	Hiroshi Miura <miura@da-cha.org>
 *		- fix on/off register mistake
 *		- fix cpu_khz calc when it stops cpu modulation.
 *
 *	Dec. 11, 2002	Hiroshi Miura <miura@da-cha.org>
 *		- rewrite for Cyrix MediaGX Cx5510/5520 and
 *		  NatSemi Geode Cs5530(A).
 *
 *	Jul. ??, 2002  Zwane Mwaikambo <zwane@commfireservices.com>
 *		- cs5530_mod patch for 2.4.19-rc1.
 *
 *---------------------------------------------------------------------------
 *
 * Todo
 *	Test on machines with 5510, 5530, 5530A
 */

/************************************************************************
 *			Suspend Modulation - Definitions		*
 ************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/cpufreq.h>
#include <linux/pci.h>
#include <asm/processor.h>
#include <asm/errno.h>

/* PCI config registers, all at F0 */
#define PCI_PMER1	0x80	/* power management enable register 1 */
#define PCI_PMER2	0x81	/* power management enable register 2 */
#define PCI_PMER3	0x82	/* power management enable register 3 */
#define PCI_IRQTC	0x8c	/* irq speedup timer counter register:typical 2 to 4ms */
#define PCI_VIDTC	0x8d	/* video speedup timer counter register: typical 50 to 100ms */
#define PCI_MODOFF	0x94	/* suspend modulation OFF counter register, 1 = 32us */
#define PCI_MODON	0x95	/* suspend modulation ON counter register */
#define PCI_SUSCFG	0x96	/* suspend configuration register */

/* PMER1 bits */
#define GPM		(1<<0)	/* global power management */
#define GIT		(1<<1)	/* globally enable PM device idle timers */
#define GTR		(1<<2)	/* globally enable IO traps */
#define IRQ_SPDUP	(1<<3)	/* disable clock throttle during interrupt handling */
#define VID_SPDUP	(1<<4)	/* disable clock throttle during vga video handling */

/* SUSCFG bits */
#define SUSMOD		(1<<0)	/* enable/disable suspend modulation */
/* the belows support only with cs5530 (after rev.1.2)/cs5530A */
#define SMISPDUP	(1<<1)	/* select how SMI re-enable suspend modulation: */
				/* IRQTC timer or read SMI speedup disable reg.(F1BAR[08-09h]) */
#define SUSCFG		(1<<2)	/* enable powering down a GXLV processor. "Special 3Volt Suspend" mode */
/* the belows support only with cs5530A */
#define PWRSVE_ISA	(1<<3)	/* stop ISA clock  */
#define PWRSVE		(1<<4)	/* active idle */

struct gxfreq_params {
	u8 on_duration;
	u8 off_duration;
	u8 pci_suscfg;
	u8 pci_pmer1;
	u8 pci_pmer2;
	u8 pci_rev;
	struct pci_dev *cs55x0;
};

static struct gxfreq_params *gx_params;
static int stock_freq;

/* PCI bus clock - defaults to 30.000 if cpu_khz is not available */
static int pci_busclk = 0;
module_param (pci_busclk, int, 0444);

/* maximum duration for which the cpu may be suspended
 * (32us * MAX_DURATION). If no parameter is given, this defaults
 * to 255.
 * Note that this leads to a maximum of 8 ms(!) where the CPU clock
 * is suspended -- processing power is just 0.39% of what it used to be,
 * though. 781.25 kHz(!) for a 200 MHz processor -- wow. */
static int max_duration = 255;
module_param (max_duration, int, 0444);

/* For the default policy, we want at least some processing power
 * - let's say 5%. (min = maxfreq / POLICY_MIN_DIV)
 */
#define POLICY_MIN_DIV 20


#define dprintk(msg...) cpufreq_debug_printk(CPUFREQ_DEBUG_DRIVER, "gx-suspmod", msg)

/**
 * we can detect a core multipiler from dir0_lsb
 * from GX1 datasheet p.56,
 *	MULT[3:0]:
 *	0000 = SYSCLK multiplied by 4 (test only)
 *	0001 = SYSCLK multiplied by 10
 *	0010 = SYSCLK multiplied by 4
 *	0011 = SYSCLK multiplied by 6
 *	0100 = SYSCLK multiplied by 9
 *	0101 = SYSCLK multiplied by 5
 *	0110 = SYSCLK multiplied by 7
 *	0111 = SYSCLK multiplied by 8
 *              of 33.3MHz
 **/
static int gx_freq_mult[16] = {
		4, 10, 4, 6, 9, 5, 7, 8,
		0, 0, 0, 0, 0, 0, 0, 0
};


/****************************************************************
 *	Low Level chipset interface				*
 ****************************************************************/
static struct pci_device_id gx_chipset_tbl[] __initdata = {
	{ PCI_VENDOR_ID_CYRIX, PCI_DEVICE_ID_CYRIX_5530_LEGACY, PCI_ANY_ID, PCI_ANY_ID },
	{ PCI_VENDOR_ID_CYRIX, PCI_DEVICE_ID_CYRIX_5520, PCI_ANY_ID, PCI_ANY_ID },
	{ PCI_VENDOR_ID_CYRIX, PCI_DEVICE_ID_CYRIX_5510, PCI_ANY_ID, PCI_ANY_ID },
	{ 0, },
};

/**
 * gx_detect_chipset:
 *
 **/
static __init struct pci_dev *gx_detect_chipset(void)
{
	struct pci_dev *gx_pci = NULL;

	/* check if CPU is a MediaGX or a Geode. */
	if ((current_cpu_data.x86_vendor != X86_VENDOR_NSC) &&
	    (current_cpu_data.x86_vendor != X86_VENDOR_CYRIX)) {
		dprintk("error: no MediaGX/Geode processor found!\n");
		return NULL;
	}

	/* detect which companion chip is used */
	while ((gx_pci = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, gx_pci)) != NULL) {
		if ((pci_match_id(gx_chipset_tbl, gx_pci)) != NULL)
			return gx_pci;
	}

	dprintk("error: no supported chipset found!\n");
	return NULL;
}

/**
 * gx_get_cpuspeed:
 *
 * Finds out at which efficient frequency the Cyrix MediaGX/NatSemi Geode CPU runs.
 */
static unsigned int gx_get_cpuspeed(unsigned int cpu)
{
	if ((gx_params->pci_suscfg & SUSMOD) == 0)
		return stock_freq;

	return (stock_freq * gx_params->off_duration)
		/ (gx_params->on_duration + gx_params->off_duration);
}

/**
 *      gx_validate_speed:
 *      determine current cpu speed
 *
 **/

static unsigned int gx_validate_speed(unsigned int khz, u8 *on_duration, u8 *off_duration)
{
	unsigned int i;
	u8 tmp_on, tmp_off;
	int old_tmp_freq = stock_freq;
	int tmp_freq;

	*off_duration=1;
	*on_duration=0;

	for (i=max_duration; i>0; i--) {
		tmp_off = ((khz * i) / stock_freq) & 0xff;
		tmp_on = i - tmp_off;
		tmp_freq = (stock_freq * tmp_off) / i;
		/* if this relation is closer to khz, use this. If it's equal,
		 * prefer it, too - lower latency */
		if (abs(tmp_freq - khz) <= abs(old_tmp_freq - khz)) {
			*on_duration = tmp_on;
			*off_duration = tmp_off;
			old_tmp_freq = tmp_freq;
		}
	}

	return old_tmp_freq;
}


/**
 * gx_set_cpuspeed:
 * set cpu speed in khz.
 **/

static void gx_set_cpuspeed(unsigned int khz)
{
	u8 suscfg, pmer1;
	unsigned int new_khz;
	unsigned long flags;
	struct cpufreq_freqs freqs;

	freqs.cpu = 0;
	freqs.old = gx_get_cpuspeed(0);

	new_khz = gx_validate_speed(khz, &gx_params->on_duration, &gx_params->off_duration);

	freqs.new = new_khz;

	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);
	local_irq_save(flags);

	if (new_khz != stock_freq) {  /* if new khz == 100% of CPU speed, it is special case */
		switch (gx_params->cs55x0->device) {
		case PCI_DEVICE_ID_CYRIX_5530_LEGACY:
			pmer1 = gx_params->pci_pmer1 | IRQ_SPDUP | VID_SPDUP;
			/* FIXME: need to test other values -- Zwane,Miura */
			pci_write_config_byte(gx_params->cs55x0, PCI_IRQTC, 4); /* typical 2 to 4ms */
			pci_write_config_byte(gx_params->cs55x0, PCI_VIDTC, 100);/* typical 50 to 100ms */
			pci_write_config_byte(gx_params->cs55x0, PCI_PMER1, pmer1);

			if (gx_params->pci_rev < 0x10) {   /* CS5530(rev 1.2, 1.3) */
				suscfg = gx_params->pci_suscfg | SUSMOD;
			} else {                           /* CS5530A,B.. */
				suscfg = gx_params->pci_suscfg | SUSMOD | PWRSVE;
			}
			break;
		case PCI_DEVICE_ID_CYRIX_5520:
		case PCI_DEVICE_ID_CYRIX_5510:
			suscfg = gx_params->pci_suscfg | SUSMOD;
			break;
		default:
			local_irq_restore(flags);
			dprintk("fatal: try to set unknown chipset.\n");
			return;
		}
	} else {
		suscfg = gx_params->pci_suscfg & ~(SUSMOD);
		gx_params->off_duration = 0;
		gx_params->on_duration = 0;
		dprintk("suspend modulation disabled: cpu runs 100 percent speed.\n");
	}

	pci_write_config_byte(gx_params->cs55x0, PCI_MODOFF, gx_params->off_duration);
	pci_write_config_byte(gx_params->cs55x0, PCI_MODON, gx_params->on_duration);

	pci_write_config_byte(gx_params->cs55x0, PCI_SUSCFG, suscfg);
	pci_read_config_byte(gx_params->cs55x0, PCI_SUSCFG, &suscfg);

	local_irq_restore(flags);

	gx_params->pci_suscfg = suscfg;

	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	dprintk("suspend modulation w/ duration of ON:%d us, OFF:%d us\n",
		gx_params->on_duration * 32, gx_params->off_duration * 32);
	dprintk("suspend modulation w/ clock speed: %d kHz.\n", freqs.new);
}

/****************************************************************
 *             High level functions                             *
 ****************************************************************/

/*
 *	cpufreq_gx_verify: test if frequency range is valid
 *
 *	This function checks if a given frequency range in kHz is valid
 *      for the hardware supported by the driver.
 */

static int cpufreq_gx_verify(struct cpufreq_policy *policy)
{
	unsigned int tmp_freq = 0;
	u8 tmp1, tmp2;

	if (!stock_freq || !policy)
		return -EINVAL;

	policy->cpu = 0;
	cpufreq_verify_within_limits(policy, (stock_freq / max_duration), stock_freq);

	/* it needs to be assured that at least one supported frequency is
	 * within policy->min and policy->max. If it is not, policy->max
	 * needs to be increased until one freuqency is supported.
	 * policy->min may not be decreased, though. This way we guarantee a
	 * specific processing capacity.
	 */
	tmp_freq = gx_validate_speed(policy->min, &tmp1, &tmp2);
	if (tmp_freq < policy->min)
		tmp_freq += stock_freq / max_duration;
	policy->min = tmp_freq;
	if (policy->min > policy->max)
		policy->max = tmp_freq;
	tmp_freq = gx_validate_speed(policy->max, &tmp1, &tmp2);
	if (tmp_freq > policy->max)
		tmp_freq -= stock_freq / max_duration;
	policy->max = tmp_freq;
	if (policy->max < policy->min)
		policy->max = policy->min;
	cpufreq_verify_within_limits(policy, (stock_freq / max_duration), stock_freq);

	return 0;
}

/*
 *      cpufreq_gx_target:
 *
 */
static int cpufreq_gx_target(struct cpufreq_policy *policy,
			     unsigned int target_freq,
			     unsigned int relation)
{
	u8 tmp1, tmp2;
	unsigned int tmp_freq;

	if (!stock_freq || !policy)
		return -EINVAL;

	policy->cpu = 0;

	tmp_freq = gx_validate_speed(target_freq, &tmp1, &tmp2);
	while (tmp_freq < policy->min) {
		tmp_freq += stock_freq / max_duration;
		tmp_freq = gx_validate_speed(tmp_freq, &tmp1, &tmp2);
	}
	while (tmp_freq > policy->max) {
		tmp_freq -= stock_freq / max_duration;
		tmp_freq = gx_validate_speed(tmp_freq, &tmp1, &tmp2);
	}

	gx_set_cpuspeed(tmp_freq);

	return 0;
}

static int cpufreq_gx_cpu_init(struct cpufreq_policy *policy)
{
	unsigned int maxfreq, curfreq;

	if (!policy || policy->cpu != 0)
		return -ENODEV;

	/* determine maximum frequency */
	if (pci_busclk) {
		maxfreq = pci_busclk * gx_freq_mult[getCx86(CX86_DIR1) & 0x0f];
	} else if (cpu_khz) {
		maxfreq = cpu_khz;
	} else {
		maxfreq = 30000 * gx_freq_mult[getCx86(CX86_DIR1) & 0x0f];
	}
	stock_freq = maxfreq;
	curfreq = gx_get_cpuspeed(0);

	dprintk("cpu max frequency is %d.\n", maxfreq);
	dprintk("cpu current frequency is %dkHz.\n",curfreq);

	/* setup basic struct for cpufreq API */
	policy->cpu = 0;

	if (max_duration < POLICY_MIN_DIV)
		policy->min = maxfreq / max_duration;
	else
		policy->min = maxfreq / POLICY_MIN_DIV;
	policy->max = maxfreq;
	policy->cur = curfreq;
	policy->governor = CPUFREQ_DEFAULT_GOVERNOR;
	policy->cpuinfo.min_freq = maxfreq / max_duration;
	policy->cpuinfo.max_freq = maxfreq;
	policy->cpuinfo.transition_latency = CPUFREQ_ETERNAL;

	return 0;
}

/*
 * cpufreq_gx_init:
 *   MediaGX/Geode GX initialize cpufreq driver
 */
static struct cpufreq_driver gx_suspmod_driver = {
	.get		= gx_get_cpuspeed,
	.verify		= cpufreq_gx_verify,
	.target		= cpufreq_gx_target,
	.init		= cpufreq_gx_cpu_init,
	.name		= "gx-suspmod",
	.owner		= THIS_MODULE,
};

static int __init cpufreq_gx_init(void)
{
	int ret;
	struct gxfreq_params *params;
	struct pci_dev *gx_pci;

	/* Test if we have the right hardware */
	if ((gx_pci = gx_detect_chipset()) == NULL)
		return -ENODEV;

	/* check whether module parameters are sane */
	if (max_duration > 0xff)
		max_duration = 0xff;

	dprintk("geode suspend modulation available.\n");

	params = kzalloc(sizeof(struct gxfreq_params), GFP_KERNEL);
	if (params == NULL)
		return -ENOMEM;

	params->cs55x0 = gx_pci;
	gx_params = params;

	/* keep cs55x0 configurations */
	pci_read_config_byte(params->cs55x0, PCI_SUSCFG, &(params->pci_suscfg));
	pci_read_config_byte(params->cs55x0, PCI_PMER1, &(params->pci_pmer1));
	pci_read_config_byte(params->cs55x0, PCI_PMER2, &(params->pci_pmer2));
	pci_read_config_byte(params->cs55x0, PCI_MODON, &(params->on_duration));
	pci_read_config_byte(params->cs55x0, PCI_MODOFF, &(params->off_duration));
	pci_read_config_byte(params->cs55x0, PCI_REVISION_ID, &params->pci_rev);

	if ((ret = cpufreq_register_driver(&gx_suspmod_driver))) {
		kfree(params);
		return ret;                   /* register error! */
	}

	return 0;
}

static void __exit cpufreq_gx_exit(void)
{
	cpufreq_unregister_driver(&gx_suspmod_driver);
	pci_dev_put(gx_params->cs55x0);
	kfree(gx_params);
}

MODULE_AUTHOR ("Hiroshi Miura <miura@da-cha.org>");
MODULE_DESCRIPTION ("Cpufreq driver for Cyrix MediaGX and NatSemi Geode");
MODULE_LICENSE ("GPL");

module_init(cpufreq_gx_init);
module_exit(cpufreq_gx_exit);

