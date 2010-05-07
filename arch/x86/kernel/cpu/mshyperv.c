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
#include <asm/processor.h>
#include <asm/hyperv.h>
#include <asm/mshyperv.h>


int ms_hyperv_platform(void)
{
	u32 eax, ebx, ecx, edx;
	char hyp_signature[13];

	cpuid(1, &eax, &ebx, &ecx, &edx);
	if (!(ecx & HYPERV_HYPERVISOR_PRESENT_BIT))
		return 0;

	cpuid(HYPERV_CPUID_VENDOR_AND_MAX_FUNCTIONS, &eax, &ebx, &ecx, &edx);
	*(u32 *)(hyp_signature + 0) = ebx;
	*(u32 *)(hyp_signature + 4) = ecx;
	*(u32 *)(hyp_signature + 8) = edx;

	if ((eax < HYPERV_CPUID_MIN) || (memcmp("Microsoft Hv", hyp_signature, 12)))
		return 0;
	return 1;
}

void __cpuinit ms_hyperv_set_feature_bits(struct cpuinfo_x86 *c)
{
	u32 eax, ebx, ecx, edx;

	c->x86_hyper_features = 0;
	/*
	 * Extract the features, recommendations etc.
	 * The first 9 bits will be used to track hypervisor features.
	 * The next 6 bits will be used to track the hypervisor
	 * recommendations.
	 */
	cpuid(HYPERV_CPUID_FEATURES, &eax, &ebx, &ecx, &edx);
	c->x86_hyper_features |= (eax & 0x1ff);

	cpuid(HYPERV_CPUID_ENLIGHTMENT_INFO, &eax, &ebx, &ecx, &edx);
	c->x86_hyper_features |= ((eax & 0x3f) << 9);
	printk(KERN_INFO "Detected HyperV with features: %x\n",
		c->x86_hyper_features);
}
