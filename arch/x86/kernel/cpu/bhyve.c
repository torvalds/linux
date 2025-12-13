// SPDX-License-Identifier: GPL-2.0
/*
 * FreeBSD Bhyve guest enlightenments
 *
 * Copyright © 2025 Amazon.com, Inc. or its affiliates.
 *
 * Author: David Woodhouse <dwmw2@infradead.org>
 */

#include <linux/init.h>
#include <linux/export.h>
#include <asm/processor.h>
#include <asm/hypervisor.h>

static uint32_t bhyve_cpuid_base;
static uint32_t bhyve_cpuid_max;

#define BHYVE_SIGNATURE			"bhyve bhyve "

#define CPUID_BHYVE_FEATURES		0x40000001

/* Features advertised in CPUID_BHYVE_FEATURES %eax */

/* MSI Extended Dest ID */
#define CPUID_BHYVE_FEAT_EXT_DEST_ID	(1UL << 0)

static uint32_t __init bhyve_detect(void)
{
	if (!cpu_feature_enabled(X86_FEATURE_HYPERVISOR))
                return 0;

	bhyve_cpuid_base = cpuid_base_hypervisor(BHYVE_SIGNATURE, 0);
	if (!bhyve_cpuid_base)
		return 0;

	bhyve_cpuid_max = cpuid_eax(bhyve_cpuid_base);
	return bhyve_cpuid_max;
}

static uint32_t bhyve_features(void)
{
	unsigned int cpuid_leaf = bhyve_cpuid_base | CPUID_BHYVE_FEATURES;

	if (bhyve_cpuid_max < cpuid_leaf)
		return 0;

	return cpuid_eax(cpuid_leaf);
}

static bool __init bhyve_ext_dest_id(void)
{
	return !!(bhyve_features() & CPUID_BHYVE_FEAT_EXT_DEST_ID);
}

static bool __init bhyve_x2apic_available(void)
{
	return true;
}

const struct hypervisor_x86 x86_hyper_bhyve __refconst = {
	.name			= "Bhyve",
	.detect			= bhyve_detect,
	.init.init_platform	= x86_init_noop,
	.init.x2apic_available	= bhyve_x2apic_available,
	.init.msi_ext_dest_id	= bhyve_ext_dest_id,
};
