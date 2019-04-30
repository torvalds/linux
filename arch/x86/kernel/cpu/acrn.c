// SPDX-License-Identifier: GPL-2.0
/*
 * ACRN detection support
 *
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 *
 * Jason Chen CJ <jason.cj.chen@intel.com>
 * Zhao Yakui <yakui.zhao@intel.com>
 *
 */

#include <asm/hypervisor.h>

static uint32_t __init acrn_detect(void)
{
	return hypervisor_cpuid_base("ACRNACRNACRN\0\0", 0);
}

static void __init acrn_init_platform(void)
{
}

static bool acrn_x2apic_available(void)
{
	/*
	 * x2apic is not supported for now. Future enablement will have to check
	 * X86_FEATURE_X2APIC to determine whether x2apic is supported in the
	 * guest.
	 */
	return false;
}

const __initconst struct hypervisor_x86 x86_hyper_acrn = {
	.name                   = "ACRN",
	.detect                 = acrn_detect,
	.type			= X86_HYPER_ACRN,
	.init.init_platform     = acrn_init_platform,
	.init.x2apic_available  = acrn_x2apic_available,
};
