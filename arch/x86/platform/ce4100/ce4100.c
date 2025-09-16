// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel CE4100  platform specific setup code
 *
 * (C) Copyright 2010 Intel Corporation
 */
#include <linux/init.h>
#include <linux/reboot.h>

#include <asm/ce4100.h>
#include <asm/prom.h>
#include <asm/setup.h>
#include <asm/io.h>

/*
 * The CE4100 platform has an internal 8051 Microcontroller which is
 * responsible for signaling to the external Power Management Unit the
 * intention to reset, reboot or power off the system. This 8051 device has
 * its command register mapped at I/O port 0xcf9 and the value 0x4 is used
 * to power off the system.
 */
static void ce4100_power_off(void)
{
	outb(0x4, 0xcf9);
}

static void __init sdv_arch_setup(void)
{
	sdv_serial_fixup();
}

static void sdv_pci_init(void)
{
	x86_of_pci_init();
}

/*
 * CE4100 specific x86_init function overrides and early setup
 * calls.
 */
void __init x86_ce4100_early_setup(void)
{
	x86_init.oem.arch_setup			= sdv_arch_setup;
	x86_init.resources.probe_roms		= x86_init_noop;
	x86_init.mpparse.find_mptable		= x86_init_noop;
	x86_init.mpparse.early_parse_smp_cfg	= x86_init_noop;
	x86_init.pci.init			= ce4100_pci_init;
	x86_init.pci.init_irq			= sdv_pci_init;

	/*
	 * By default, the reboot method is ACPI which is supported by the
	 * CE4100 bootloader CEFDK using FADT.ResetReg Address and ResetValue
	 * the bootloader will however issue a system power off instead of
	 * reboot. By using BOOT_KBD we ensure proper system reboot as
	 * expected.
	 */
	reboot_type = BOOT_KBD;

	pm_power_off = ce4100_power_off;
}
