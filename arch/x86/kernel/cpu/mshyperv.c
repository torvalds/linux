/*
 * HyperV  Detection code.
 *
 * Copyright (C) 2010, Novell, Inc.
 * Author : K. Y. Srinivasan <ksrinivasan@novell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 */

#include <linux/types.h>
#include <linux/time.h>
#include <linux/clocksource.h>
#include <linux/module.h>
#include <linux/hardirq.h>
#include <linux/interrupt.h>
#include <asm/processor.h>
#include <asm/hypervisor.h>
#include <asm/hyperv.h>
#include <asm/mshyperv.h>
#include <asm/desc.h>
#include <asm/idle.h>
#include <asm/irq_regs.h>

struct ms_hyperv_info ms_hyperv;
EXPORT_SYMBOL_GPL(ms_hyperv);

static uint32_t  __init ms_hyperv_platform(void)
{
	u32 eax;
	u32 hyp_signature[3];

	if (!boot_cpu_has(X86_FEATURE_HYPERVISOR))
		return 0;

	cpuid(HYPERV_CPUID_VENDOR_AND_MAX_FUNCTIONS,
	      &eax, &hyp_signature[0], &hyp_signature[1], &hyp_signature[2]);

	if (eax >= HYPERV_CPUID_MIN &&
	    eax <= HYPERV_CPUID_MAX &&
	    !memcmp("Microsoft Hv", hyp_signature, 12))
		return HYPERV_CPUID_VENDOR_AND_MAX_FUNCTIONS;

	return 0;
}

static cycle_t read_hv_clock(struct clocksource *arg)
{
	cycle_t current_tick;
	/*
	 * Read the partition counter to get the current tick count. This count
	 * is set to 0 when the partition is created and is incremented in
	 * 100 nanosecond units.
	 */
	rdmsrl(HV_X64_MSR_TIME_REF_COUNT, current_tick);
	return current_tick;
}

static struct clocksource hyperv_cs = {
	.name		= "hyperv_clocksource",
	.rating		= 400, /* use this when running on Hyperv*/
	.read		= read_hv_clock,
	.mask		= CLOCKSOURCE_MASK(64),
};

static void __init ms_hyperv_init_platform(void)
{
	/*
	 * Extract the features and hints
	 */
	ms_hyperv.features = cpuid_eax(HYPERV_CPUID_FEATURES);
	ms_hyperv.hints    = cpuid_eax(HYPERV_CPUID_ENLIGHTMENT_INFO);

	printk(KERN_INFO "HyperV: features 0x%x, hints 0x%x\n",
	       ms_hyperv.features, ms_hyperv.hints);

	if (ms_hyperv.features & HV_X64_MSR_TIME_REF_COUNT_AVAILABLE)
		clocksource_register_hz(&hyperv_cs, NSEC_PER_SEC/100);
}

const __refconst struct hypervisor_x86 x86_hyper_ms_hyperv = {
	.name			= "Microsoft HyperV",
	.detect			= ms_hyperv_platform,
	.init_platform		= ms_hyperv_init_platform,
};
EXPORT_SYMBOL(x86_hyper_ms_hyperv);

#if IS_ENABLED(CONFIG_HYPERV)
static int vmbus_irq = -1;
static irq_handler_t vmbus_isr;

void hv_register_vmbus_handler(int irq, irq_handler_t handler)
{
	/*
	 * Setup the IDT for hypervisor callback.
	 */
	alloc_intr_gate(HYPERVISOR_CALLBACK_VECTOR, hyperv_callback_vector);

	vmbus_irq = irq;
	vmbus_isr = handler;
}

void hyperv_vector_handler(struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);
	struct irq_desc *desc;

	irq_enter();
	exit_idle();

	desc = irq_to_desc(vmbus_irq);

	if (desc)
		generic_handle_irq_desc(vmbus_irq, desc);

	irq_exit();
	set_irq_regs(old_regs);
}
#else
void hv_register_vmbus_handler(int irq, irq_handler_t handler)
{
}
#endif
EXPORT_SYMBOL_GPL(hv_register_vmbus_handler);
