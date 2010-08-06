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
#include <linux/module.h>
#include <asm/processor.h>
#include <asm/hypervisor.h>
#include <asm/hyperv.h>
#include <asm/mshyperv.h>

struct ms_hyperv_info ms_hyperv;

static bool __init ms_hyperv_platform(void)
{
	u32 eax;
	u32 hyp_signature[3];

	if (!boot_cpu_has(X86_FEATURE_HYPERVISOR))
		return false;

	cpuid(HYPERV_CPUID_VENDOR_AND_MAX_FUNCTIONS,
	      &eax, &hyp_signature[0], &hyp_signature[1], &hyp_signature[2]);

	return eax >= HYPERV_CPUID_MIN &&
		eax <= HYPERV_CPUID_MAX &&
		!memcmp("Microsoft Hv", hyp_signature, 12);
}

static void __init ms_hyperv_init_platform(void)
{
	/*
	 * Extract the features and hints
	 */
	ms_hyperv.features = cpuid_eax(HYPERV_CPUID_FEATURES);
	ms_hyperv.hints    = cpuid_eax(HYPERV_CPUID_ENLIGHTMENT_INFO);

	printk(KERN_INFO "HyperV: features 0x%x, hints 0x%x\n",
	       ms_hyperv.features, ms_hyperv.hints);
}

const __refconst struct hypervisor_x86 x86_hyper_ms_hyperv = {
	.name			= "Microsoft HyperV",
	.detect			= ms_hyperv_platform,
	.init_platform		= ms_hyperv_init_platform,
};
EXPORT_SYMBOL(x86_hyper_ms_hyperv);
