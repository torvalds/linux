// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel MID platform setup code
 *
 * (C) Copyright 2008, 2012, 2021 Intel Corporation
 * Author: Jacob Pan (jacob.jun.pan@intel.com)
 * Author: Sathyanarayanan Kuppuswamy <sathyanarayanan.kuppuswamy@intel.com>
 */

#define pr_fmt(fmt) "intel_mid: " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/regulator/machine.h>
#include <linux/scatterlist.h>
#include <linux/irq.h>
#include <linux/export.h>
#include <linux/notifier.h>

#include <asm/setup.h>
#include <asm/mpspec_def.h>
#include <asm/hw_irq.h>
#include <asm/apic.h>
#include <asm/cpu_device_id.h>
#include <asm/io_apic.h>
#include <asm/intel-mid.h>
#include <asm/io.h>
#include <asm/i8259.h>
#include <asm/reboot.h>

#include <linux/platform_data/x86/intel_scu_ipc.h>

#define IPCMSG_COLD_OFF		0x80	/* Only for Tangier */
#define IPCMSG_COLD_RESET	0xF1

static void intel_mid_power_off(void)
{
	/* Shut down South Complex via PWRMU */
	intel_mid_pwr_power_off();

	/* Only for Tangier, the rest will ignore this command */
	intel_scu_ipc_dev_simple_command(NULL, IPCMSG_COLD_OFF, 1);
};

static void intel_mid_reboot(void)
{
	intel_scu_ipc_dev_simple_command(NULL, IPCMSG_COLD_RESET, 0);
}

static void __init intel_mid_time_init(void)
{
	/* Lapic only, no apbt */
	x86_init.timers.setup_percpu_clockev = setup_boot_APIC_clock;
	x86_cpuinit.setup_percpu_clockev = setup_secondary_APIC_clock;
}

static void intel_mid_arch_setup(void)
{
	switch (boot_cpu_data.x86_vfm) {
	case INTEL_ATOM_SILVERMONT_MID:
		x86_platform.legacy.rtc = 1;
		break;
	default:
		break;
	}

	/*
	 * Intel MID platforms are using explicitly defined regulators.
	 *
	 * Let the regulator core know that we do not have any additional
	 * regulators left. This lets it substitute unprovided regulators with
	 * dummy ones:
	 */
	regulator_has_full_constraints();
}

/*
 * Moorestown does not have external NMI source nor port 0x61 to report
 * NMI status. The possible NMI sources are from pmu as a result of NMI
 * watchdog or lock debug. Reading io port 0x61 results in 0xff which
 * misled NMI handler.
 */
static unsigned char intel_mid_get_nmi_reason(void)
{
	return 0;
}

/*
 * Moorestown specific x86_init function overrides and early setup
 * calls.
 */
void __init x86_intel_mid_early_setup(void)
{
	x86_init.resources.probe_roms = x86_init_noop;
	x86_init.resources.reserve_resources = x86_init_noop;

	x86_init.timers.timer_init = intel_mid_time_init;
	x86_init.timers.setup_percpu_clockev = x86_init_noop;

	x86_init.irqs.pre_vector_init = x86_init_noop;

	x86_init.oem.arch_setup = intel_mid_arch_setup;

	x86_platform.get_nmi_reason = intel_mid_get_nmi_reason;

	x86_init.pci.arch_init = intel_mid_pci_init;
	x86_init.pci.fixup_irqs = x86_init_noop;

	legacy_pic = &null_legacy_pic;

	/*
	 * Do nothing for now as everything needed done in
	 * x86_intel_mid_early_setup() below.
	 */
	x86_init.acpi.reduced_hw_early_init = x86_init_noop;

	pm_power_off = intel_mid_power_off;
	machine_ops.emergency_restart  = intel_mid_reboot;

	/* Avoid searching for BIOS MP tables */
	x86_init.mpparse.find_mptable		= x86_init_noop;
	x86_init.mpparse.early_parse_smp_cfg	= x86_init_noop;
	x86_init.mpparse.parse_smp_cfg		= x86_init_noop;
	set_bit(MP_BUS_ISA, mp_bus_not_pci);
}
