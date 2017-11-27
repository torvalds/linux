// SPDX-License-Identifier: GPL2.0
/*
 * Jailhouse paravirt_ops implementation
 *
 * Copyright (c) Siemens AG, 2015-2017
 *
 * Authors:
 *  Jan Kiszka <jan.kiszka@siemens.com>
 */

#include <linux/kernel.h>
#include <asm/cpu.h>
#include <asm/hypervisor.h>
#include <asm/setup.h>

static __initdata struct jailhouse_setup_data setup_data;

static uint32_t jailhouse_cpuid_base(void)
{
	if (boot_cpu_data.cpuid_level < 0 ||
	    !boot_cpu_has(X86_FEATURE_HYPERVISOR))
		return 0;

	return hypervisor_cpuid_base("Jailhouse\0\0\0", 0);
}

static uint32_t __init jailhouse_detect(void)
{
	return jailhouse_cpuid_base();
}

static void __init jailhouse_init_platform(void)
{
	u64 pa_data = boot_params.hdr.setup_data;
	struct setup_data header;
	void *mapping;

	while (pa_data) {
		mapping = early_memremap(pa_data, sizeof(header));
		memcpy(&header, mapping, sizeof(header));
		early_memunmap(mapping, sizeof(header));

		if (header.type == SETUP_JAILHOUSE &&
		    header.len >= sizeof(setup_data)) {
			pa_data += offsetof(struct setup_data, data);

			mapping = early_memremap(pa_data, sizeof(setup_data));
			memcpy(&setup_data, mapping, sizeof(setup_data));
			early_memunmap(mapping, sizeof(setup_data));

			break;
		}

		pa_data = header.next;
	}

	if (!pa_data)
		panic("Jailhouse: No valid setup data found");

	if (setup_data.compatible_version > JAILHOUSE_SETUP_REQUIRED_VERSION)
		panic("Jailhouse: Unsupported setup data structure");
}

bool jailhouse_paravirt(void)
{
	return jailhouse_cpuid_base() != 0;
}

const struct hypervisor_x86 x86_hyper_jailhouse __refconst = {
	.name			= "Jailhouse",
	.detect			= jailhouse_detect,
	.init.init_platform	= jailhouse_init_platform,
};
