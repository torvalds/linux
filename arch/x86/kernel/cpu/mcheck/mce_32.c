/*
 * mce.c - x86 Machine Check Exception Reporting
 * (c) 2002 Alan Cox <alan@redhat.com>, Dave Jones <davej@codemonkey.org.uk>
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/thread_info.h>

#include <asm/processor.h>
#include <asm/system.h>
#include <asm/mce.h>

#include "mce.h"

int mce_disabled;
int nr_mce_banks;

EXPORT_SYMBOL_GPL(nr_mce_banks);	/* non-fatal.o */

/* Handle unconfigured int18 (should never happen) */
static void unexpected_machine_check(struct pt_regs *regs, long error_code)
{
	printk(KERN_ERR "CPU#%d: Unexpected int18 (Machine Check).\n", smp_processor_id());
}

/* Call the installed machine check handler for this CPU setup. */
void (*machine_check_vector)(struct pt_regs *, long error_code) = unexpected_machine_check;

/* This has to be run for each processor */
void mcheck_init(struct cpuinfo_x86 *c)
{
	if (mce_disabled == 1)
		return;

	switch (c->x86_vendor) {
	case X86_VENDOR_AMD:
		amd_mcheck_init(c);
		break;

	case X86_VENDOR_INTEL:
		if (c->x86 == 5)
			intel_p5_mcheck_init(c);
		if (c->x86 == 6)
			intel_p6_mcheck_init(c);
		if (c->x86 == 15)
			intel_p4_mcheck_init(c);
		break;

	case X86_VENDOR_CENTAUR:
		if (c->x86 == 5)
			winchip_mcheck_init(c);
		break;

	default:
		break;
	}
}

static unsigned long old_cr4 __initdata;

void __init stop_mce(void)
{
	old_cr4 = read_cr4();
	clear_in_cr4(X86_CR4_MCE);
}

void __init restart_mce(void)
{
	if (old_cr4 & X86_CR4_MCE)
		set_in_cr4(X86_CR4_MCE);
}

static int __init mcheck_disable(char *str)
{
	mce_disabled = 1;
	return 1;
}

static int __init mcheck_enable(char *str)
{
	mce_disabled = -1;
	return 1;
}

__setup("nomce", mcheck_disable);
__setup("mce", mcheck_enable);
