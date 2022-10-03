// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#include <linux/kernel.h>
#include <linux/acpi.h>
#include <linux/efi.h>
#include <linux/export.h>
#include <linux/pm.h>
#include <linux/types.h>
#include <linux/reboot.h>
#include <linux/delay.h>
#include <linux/console.h>

#include <acpi/reboot.h>
#include <asm/idle.h>
#include <asm/loongarch.h>
#include <asm/reboot.h>

static void default_halt(void)
{
	local_irq_disable();
	clear_csr_ecfg(ECFG0_IM);

	pr_notice("\n\n** You can safely turn off the power now **\n\n");
	console_flush_on_panic(CONSOLE_FLUSH_PENDING);

	while (true) {
		__arch_cpu_idle();
	}
}

static void default_poweroff(void)
{
#ifdef CONFIG_EFI
	efi.reset_system(EFI_RESET_SHUTDOWN, EFI_SUCCESS, 0, NULL);
#endif
	while (true) {
		__arch_cpu_idle();
	}
}

static void default_restart(void)
{
#ifdef CONFIG_EFI
	if (efi_capsule_pending(NULL))
		efi_reboot(REBOOT_WARM, NULL);
	else
		efi_reboot(REBOOT_COLD, NULL);
#endif
	if (!acpi_disabled)
		acpi_reboot();

	while (true) {
		__arch_cpu_idle();
	}
}

void (*pm_restart)(void);
EXPORT_SYMBOL(pm_restart);

void (*pm_power_off)(void);
EXPORT_SYMBOL(pm_power_off);

void machine_halt(void)
{
#ifdef CONFIG_SMP
	preempt_disable();
	smp_send_stop();
#endif
	default_halt();
}

void machine_power_off(void)
{
#ifdef CONFIG_SMP
	preempt_disable();
	smp_send_stop();
#endif
	pm_power_off();
}

void machine_restart(char *command)
{
#ifdef CONFIG_SMP
	preempt_disable();
	smp_send_stop();
#endif
	do_kernel_restart(command);
	pm_restart();
}

static int __init loongarch_reboot_setup(void)
{
	pm_restart = default_restart;
	pm_power_off = default_poweroff;

	return 0;
}

arch_initcall(loongarch_reboot_setup);
