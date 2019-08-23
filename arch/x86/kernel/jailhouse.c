// SPDX-License-Identifier: GPL-2.0
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
#include <linux/reboot.h>
#include <asm/apic.h>
#include <asm/cpu.h>
#include <asm/hypervisor.h>
#include <asm/i8259.h>
#include <asm/irqdomain.h>
#include <asm/pci_x86.h>
#include <asm/reboot.h>
#include <asm/setup.h>
#include <asm/jailhouse_para.h>

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

static void jailhouse_get_wallclock(struct timespec64 *now)
{
	memset(now, 0, sizeof(*now));
}

static void __init jailhouse_timer_init(void)
{
	lapic_timer_period = setup_data.apic_khz * (1000 / HZ);
}

static unsigned long jailhouse_get_tsc(void)
{
	return precalibrated_tsc_khz;
}

static void __init jailhouse_x2apic_init(void)
{
#ifdef CONFIG_X86_X2APIC
	if (!x2apic_enabled())
		return;
	/*
	 * We do not have access to IR inside Jailhouse non-root cells.  So
	 * we have to run in physical mode.
	 */
	x2apic_phys = 1;
	/*
	 * This will trigger the switch to apic_x2apic_phys.  Empty OEM IDs
	 * ensure that only this APIC driver picks up the call.
	 */
	default_acpi_madt_oem_check("", "");
#endif
}

static void __init jailhouse_get_smp_config(unsigned int early)
{
	struct ioapic_domain_cfg ioapic_cfg = {
		.type = IOAPIC_DOMAIN_STRICT,
		.ops = &mp_ioapic_irqdomain_ops,
	};
	struct mpc_intsrc mp_irq = {
		.type = MP_INTSRC,
		.irqtype = mp_INT,
		.irqflag = MP_IRQPOL_ACTIVE_HIGH | MP_IRQTRIG_EDGE,
	};
	unsigned int cpu;

	jailhouse_x2apic_init();

	register_lapic_address(0xfee00000);

	for (cpu = 0; cpu < setup_data.num_cpus; cpu++) {
		generic_processor_info(setup_data.cpu_ids[cpu],
				       boot_cpu_apic_version);
	}

	smp_found_config = 1;

	if (setup_data.standard_ioapic) {
		mp_register_ioapic(0, 0xfec00000, gsi_top, &ioapic_cfg);

		/* Register 1:1 mapping for legacy UART IRQs 3 and 4 */
		mp_irq.srcbusirq = mp_irq.dstirq = 3;
		mp_save_irq(&mp_irq);

		mp_irq.srcbusirq = mp_irq.dstirq = 4;
		mp_save_irq(&mp_irq);
	}
}

static void jailhouse_no_restart(void)
{
	pr_notice("Jailhouse: Restart not supported, halting\n");
	machine_halt();
}

static int __init jailhouse_pci_arch_init(void)
{
	pci_direct_init(1);

	/*
	 * There are no bridges on the virtual PCI root bus under Jailhouse,
	 * thus no other way to discover all devices than a full scan.
	 * Respect any overrides via the command line, though.
	 */
	if (pcibios_last_bus < 0)
		pcibios_last_bus = 0xff;

#ifdef CONFIG_PCI_MMCONFIG
	if (setup_data.pci_mmconfig_base) {
		pci_mmconfig_add(0, 0, pcibios_last_bus,
				 setup_data.pci_mmconfig_base);
		pci_mmcfg_arch_init();
	}
#endif

	return 0;
}

static void __init jailhouse_init_platform(void)
{
	u64 pa_data = boot_params.hdr.setup_data;
	struct setup_data header;
	void *mapping;

	x86_init.irqs.pre_vector_init	= x86_init_noop;
	x86_init.timers.timer_init	= jailhouse_timer_init;
	x86_init.mpparse.get_smp_config	= jailhouse_get_smp_config;
	x86_init.pci.arch_init		= jailhouse_pci_arch_init;

	x86_platform.calibrate_cpu	= jailhouse_get_tsc;
	x86_platform.calibrate_tsc	= jailhouse_get_tsc;
	x86_platform.get_wallclock	= jailhouse_get_wallclock;
	x86_platform.legacy.rtc		= 0;
	x86_platform.legacy.warm_reset	= 0;
	x86_platform.legacy.i8042	= X86_LEGACY_I8042_PLATFORM_ABSENT;

	legacy_pic			= &null_legacy_pic;

	machine_ops.emergency_restart	= jailhouse_no_restart;

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
	setup_force_cpu_cap(X86_FEATURE_TSC_KNOWN_FREQ);

	pci_probe = 0;

	/*
	 * Avoid that the kernel complains about missing ACPI tables - there
	 * are none in a non-root cell.
	 */
	disable_acpi();
}

bool jailhouse_paravirt(void)
{
	return jailhouse_cpuid_base() != 0;
}

static bool __init jailhouse_x2apic_available(void)
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
	.ignore_nopv		= true,
};
