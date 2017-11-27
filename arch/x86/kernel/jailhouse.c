// SPDX-License-Identifier: GPL2.0
/*
 * Jailhouse paravirt_ops implementation
 *
 * Copyright (c) Siemens AG, 2015-2017
 *
 * Authors:
 *  Jan Kiszka <jan.kiszka@siemens.com>
 */

#include <linux/acpi_pmtmr.h>
#include <linux/kernel.h>
#include <asm/apic.h>
#include <asm/cpu.h>
#include <asm/hypervisor.h>
#include <asm/setup.h>

static __initdata struct jailhouse_setup_data setup_data;
static unsigned int precalibrated_tsc_khz;

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

static void __init jailhouse_timer_init(void)
{
	lapic_timer_frequency = setup_data.apic_khz * (1000 / HZ);
}

static unsigned long jailhouse_get_tsc(void)
{
	return precalibrated_tsc_khz;
}

static void __init jailhouse_get_smp_config(unsigned int early)
{
	unsigned int cpu;

	if (x2apic_enabled()) {
		/*
		 * We do not have access to IR inside Jailhouse non-root cells.
		 * So we have to run in physical mode.
		 */
		x2apic_phys = 1;

		/*
		 * This will trigger the switch to apic_x2apic_phys.
		 * Empty OEM IDs ensure that only this APIC driver picks up
		 * the call.
		 */
		default_acpi_madt_oem_check("", "");
	}

	register_lapic_address(0xfee00000);

	for (cpu = 0; cpu < setup_data.num_cpus; cpu++) {
		generic_processor_info(setup_data.cpu_ids[cpu],
				       boot_cpu_apic_version);
	}

	smp_found_config = 1;
}

static void __init jailhouse_init_platform(void)
{
	u64 pa_data = boot_params.hdr.setup_data;
	struct setup_data header;
	void *mapping;

	x86_init.timers.timer_init	= jailhouse_timer_init;
	x86_init.mpparse.get_smp_config	= jailhouse_get_smp_config;

	x86_platform.calibrate_cpu	= jailhouse_get_tsc;
	x86_platform.calibrate_tsc	= jailhouse_get_tsc;

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

	pmtmr_ioport = setup_data.pm_timer_address;
	pr_debug("Jailhouse: PM-Timer IO Port: %#x\n", pmtmr_ioport);

	precalibrated_tsc_khz = setup_data.tsc_khz;
}

bool jailhouse_paravirt(void)
{
	return jailhouse_cpuid_base() != 0;
}

static bool jailhouse_x2apic_available(void)
{
	/*
	 * The x2APIC is only available if the root cell enabled it. Jailhouse
	 * does not support switching between xAPIC and x2APIC.
	 */
	return x2apic_enabled();
}

const struct hypervisor_x86 x86_hyper_jailhouse __refconst = {
	.name			= "Jailhouse",
	.detect			= jailhouse_detect,
	.init.init_platform	= jailhouse_init_platform,
	.init.x2apic_available	= jailhouse_x2apic_available,
};
