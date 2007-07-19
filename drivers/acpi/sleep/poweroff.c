/*
 * poweroff.c - ACPI handler for powering off the system.
 *
 * AKA S5, but it is independent of whether or not the kernel supports
 * any other sleep support in the system.
 *
 * Copyright (c) 2005 Alexey Starikovskiy <alexey.y.starikovskiy@intel.com>
 *
 * This file is released under the GPLv2.
 */

#include <linux/pm.h>
#include <linux/init.h>
#include <acpi/acpi_bus.h>
#include <linux/sysdev.h>
#include <asm/io.h>
#include "sleep.h"

int acpi_sleep_prepare(u32 acpi_state)
{
#ifdef CONFIG_ACPI_SLEEP
	/* do we have a wakeup address for S2 and S3? */
	if (acpi_state == ACPI_STATE_S3) {
		if (!acpi_wakeup_address) {
			return -EFAULT;
		}
		acpi_set_firmware_waking_vector((acpi_physical_address)
						virt_to_phys((void *)
							     acpi_wakeup_address));

	}
	ACPI_FLUSH_CPU_CACHE();
	acpi_enable_wakeup_device_prep(acpi_state);
#endif
	acpi_gpe_sleep_prepare(acpi_state);
	acpi_enter_sleep_state_prep(acpi_state);
	return 0;
}

#ifdef CONFIG_PM

void acpi_power_off(void)
{
	/* acpi_sleep_prepare(ACPI_STATE_S5) should have already been called */
	printk("%s called\n", __FUNCTION__);
	local_irq_disable();
	/* Some SMP machines only can poweroff in boot CPU */
	acpi_enter_sleep_state(ACPI_STATE_S5);
}

static int acpi_shutdown(struct sys_device *x)
{
	switch (system_state) {
	case SYSTEM_POWER_OFF:
		/* Prepare to power off the system */
		return acpi_sleep_prepare(ACPI_STATE_S5);
	default:
		return 0;
	}
}

static struct sysdev_class acpi_sysclass = {
	set_kset_name("acpi"),
	.shutdown = acpi_shutdown
};

static struct sys_device device_acpi = {
	.id = 0,
	.cls = &acpi_sysclass,
};

static int acpi_poweroff_init(void)
{
	if (!acpi_disabled) {
		u8 type_a, type_b;
		acpi_status status;

		status =
		    acpi_get_sleep_type_data(ACPI_STATE_S5, &type_a, &type_b);
		if (ACPI_SUCCESS(status)) {
			int error;
			error = sysdev_class_register(&acpi_sysclass);
			if (!error)
				error = sysdev_register(&device_acpi);
			if (!error)
				pm_power_off = acpi_power_off;
			return error;
		}
	}
	return 0;
}

late_initcall(acpi_poweroff_init);

#endif				/* CONFIG_PM */
