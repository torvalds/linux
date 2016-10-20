/*
 * intel-mid.c: Intel MID platform setup code
 *
 * (C) Copyright 2008, 2012 Intel Corporation
 * Author: Jacob Pan (jacob.jun.pan@intel.com)
 * Author: Sathyanarayanan Kuppuswamy <sathyanarayanan.kuppuswamy@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#define pr_fmt(fmt) "intel_mid: " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/regulator/machine.h>
#include <linux/scatterlist.h>
#include <linux/sfi.h>
#include <linux/irq.h>
#include <linux/export.h>
#include <linux/notifier.h>

#include <asm/setup.h>
#include <asm/mpspec_def.h>
#include <asm/hw_irq.h>
#include <asm/apic.h>
#include <asm/io_apic.h>
#include <asm/intel-mid.h>
#include <asm/intel_mid_vrtc.h>
#include <asm/io.h>
#include <asm/i8259.h>
#include <asm/intel_scu_ipc.h>
#include <asm/apb_timer.h>
#include <asm/reboot.h>

#include "intel_mid_weak_decls.h"

/*
 * the clockevent devices on Moorestown/Medfield can be APBT or LAPIC clock,
 * cmdline option x86_intel_mid_timer can be used to override the configuration
 * to prefer one or the other.
 * at runtime, there are basically three timer configurations:
 * 1. per cpu apbt clock only
 * 2. per cpu always-on lapic clocks only, this is Penwell/Medfield only
 * 3. per cpu lapic clock (C3STOP) and one apbt clock, with broadcast.
 *
 * by default (without cmdline option), platform code first detects cpu type
 * to see if we are on lincroft or penwell, then set up both lapic or apbt
 * clocks accordingly.
 * i.e. by default, medfield uses configuration #2, moorestown uses #1.
 * config #3 is supported but not recommended on medfield.
 *
 * rating and feature summary:
 * lapic (with C3STOP) --------- 100
 * apbt (always-on) ------------ 110
 * lapic (always-on,ARAT) ------ 150
 */

enum intel_mid_timer_options intel_mid_timer_options;

/* intel_mid_ops to store sub arch ops */
static struct intel_mid_ops *intel_mid_ops;
/* getter function for sub arch ops*/
static void *(*get_intel_mid_ops[])(void) = INTEL_MID_OPS_INIT;
enum intel_mid_cpu_type __intel_mid_cpu_chip;
EXPORT_SYMBOL_GPL(__intel_mid_cpu_chip);

static void intel_mid_power_off(void)
{
	/* Shut down South Complex via PWRMU */
	intel_mid_pwr_power_off();

	/* Only for Tangier, the rest will ignore this command */
	intel_scu_ipc_simple_command(IPCMSG_COLD_OFF, 1);
};

static void intel_mid_reboot(void)
{
	intel_scu_ipc_simple_command(IPCMSG_COLD_BOOT, 0);
}

static unsigned long __init intel_mid_calibrate_tsc(void)
{
	return 0;
}

static void __init intel_mid_setup_bp_timer(void)
{
	apbt_time_init();
	setup_boot_APIC_clock();
}

static void __init intel_mid_time_init(void)
{
	sfi_table_parse(SFI_SIG_MTMR, NULL, NULL, sfi_parse_mtmr);

	switch (intel_mid_timer_options) {
	case INTEL_MID_TIMER_APBT_ONLY:
		break;
	case INTEL_MID_TIMER_LAPIC_APBT:
		/* Use apbt and local apic */
		x86_init.timers.setup_percpu_clockev = intel_mid_setup_bp_timer;
		x86_cpuinit.setup_percpu_clockev = setup_secondary_APIC_clock;
		return;
	default:
		if (!boot_cpu_has(X86_FEATURE_ARAT))
			break;
		/* Lapic only, no apbt */
		x86_init.timers.setup_percpu_clockev = setup_boot_APIC_clock;
		x86_cpuinit.setup_percpu_clockev = setup_secondary_APIC_clock;
		return;
	}

	x86_init.timers.setup_percpu_clockev = apbt_time_init;
}

static void intel_mid_arch_setup(void)
{
	if (boot_cpu_data.x86 != 6) {
		pr_err("Unknown Intel MID CPU (%d:%d), default to Penwell\n",
			boot_cpu_data.x86, boot_cpu_data.x86_model);
		__intel_mid_cpu_chip = INTEL_MID_CPU_CHIP_PENWELL;
		goto out;
	}

	switch (boot_cpu_data.x86_model) {
	case 0x35:
		__intel_mid_cpu_chip = INTEL_MID_CPU_CHIP_CLOVERVIEW;
		break;
	case 0x3C:
	case 0x4A:
		__intel_mid_cpu_chip = INTEL_MID_CPU_CHIP_TANGIER;
		break;
	case 0x27:
	default:
		__intel_mid_cpu_chip = INTEL_MID_CPU_CHIP_PENWELL;
		break;
	}

	if (__intel_mid_cpu_chip < MAX_CPU_OPS(get_intel_mid_ops))
		intel_mid_ops = get_intel_mid_ops[__intel_mid_cpu_chip]();
	else {
		intel_mid_ops = get_intel_mid_ops[INTEL_MID_CPU_CHIP_PENWELL]();
		pr_info("ARCH: Unknown SoC, assuming Penwell!\n");
	}

out:
	if (intel_mid_ops->arch_setup)
		intel_mid_ops->arch_setup();

	/*
	 * Intel MID platforms are using explicitly defined regulators.
	 *
	 * Let the regulator core know that we do not have any additional
	 * regulators left. This lets it substitute unprovided regulators with
	 * dummy ones:
	 */
	regulator_has_full_constraints();
}

/* MID systems don't have i8042 controller */
static int intel_mid_i8042_detect(void)
{
	return 0;
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

	x86_cpuinit.setup_percpu_clockev = apbt_setup_secondary_clock;

	x86_platform.calibrate_tsc = intel_mid_calibrate_tsc;
	x86_platform.i8042_detect = intel_mid_i8042_detect;
	x86_init.timers.wallclock_init = intel_mid_rtc_init;
	x86_platform.get_nmi_reason = intel_mid_get_nmi_reason;

	x86_init.pci.init = intel_mid_pci_init;
	x86_init.pci.fixup_irqs = x86_init_noop;

	legacy_pic = &null_legacy_pic;

	pm_power_off = intel_mid_power_off;
	machine_ops.emergency_restart  = intel_mid_reboot;

	/* Avoid searching for BIOS MP tables */
	x86_init.mpparse.find_smp_config = x86_init_noop;
	x86_init.mpparse.get_smp_config = x86_init_uint_noop;
	set_bit(MP_BUS_ISA, mp_bus_not_pci);
}

/*
 * if user does not want to use per CPU apb timer, just give it a lower rating
 * than local apic timer and skip the late per cpu timer init.
 */
static inline int __init setup_x86_intel_mid_timer(char *arg)
{
	if (!arg)
		return -EINVAL;

	if (strcmp("apbt_only", arg) == 0)
		intel_mid_timer_options = INTEL_MID_TIMER_APBT_ONLY;
	else if (strcmp("lapic_and_apbt", arg) == 0)
		intel_mid_timer_options = INTEL_MID_TIMER_LAPIC_APBT;
	else {
		pr_warn("X86 INTEL_MID timer option %s not recognised use x86_intel_mid_timer=apbt_only or lapic_and_apbt\n",
			arg);
		return -EINVAL;
	}
	return 0;
}
__setup("x86_intel_mid_timer=", setup_x86_intel_mid_timer);
