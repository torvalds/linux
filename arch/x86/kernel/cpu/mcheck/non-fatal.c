/*
 * Non Fatal Machine Check Exception Reporting
 *
 * (C) Copyright 2002 Dave Jones. <davej@redhat.com>
 *
 * This file contains routines to check for non-fatal MCEs every 15s
 *
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/smp.h>
#include <linux/module.h>

#include <asm/processor.h>
#include <asm/system.h>
#include <asm/msr.h>

#include "mce.h"

static int firstbank;

#define MCE_RATE	15*HZ	/* timer rate is 15s */

static void mce_checkregs(void *info)
{
	u32 low, high;
	int i;

	for (i = firstbank; i < nr_mce_banks; i++) {
		rdmsr(MSR_IA32_MC0_STATUS+i*4, low, high);

		if (high & (1<<31)) {
			printk(KERN_INFO "MCE: The hardware reports a non "
				"fatal, correctable incident occurred on "
				"CPU %d.\n",
				smp_processor_id());
			printk(KERN_INFO "Bank %d: %08x%08x\n", i, high, low);

			/*
			 * Scrub the error so we don't pick it up in MCE_RATE
			 * seconds time.
			 */
			wrmsr(MSR_IA32_MC0_STATUS+i*4, 0UL, 0UL);

			/* Serialize */
			wmb();
			add_taint(TAINT_MACHINE_CHECK);
		}
	}
}

static void mce_work_fn(struct work_struct *work);
static DECLARE_DELAYED_WORK(mce_work, mce_work_fn);

static void mce_work_fn(struct work_struct *work)
{
	on_each_cpu(mce_checkregs, NULL, 1);
	schedule_delayed_work(&mce_work, round_jiffies_relative(MCE_RATE));
}

static int __init init_nonfatal_mce_checker(void)
{
	struct cpuinfo_x86 *c = &boot_cpu_data;

	/* Check for MCE support */
	if (!cpu_has(c, X86_FEATURE_MCE))
		return -ENODEV;

	/* Check for PPro style MCA */
	if (!cpu_has(c, X86_FEATURE_MCA))
		return -ENODEV;

	/* Some Athlons misbehave when we frob bank 0 */
	if (boot_cpu_data.x86_vendor == X86_VENDOR_AMD &&
		boot_cpu_data.x86 == 6)
			firstbank = 1;
	else
			firstbank = 0;

	/*
	 * Check for non-fatal errors every MCE_RATE s
	 */
	schedule_delayed_work(&mce_work, round_jiffies_relative(MCE_RATE));
	printk(KERN_INFO "Machine check exception polling timer started.\n");
	return 0;
}
module_init(init_nonfatal_mce_checker);

MODULE_LICENSE("GPL");
