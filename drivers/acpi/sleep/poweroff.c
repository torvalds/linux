/*
 * poweroff.c - ACPI handler for powering off the system.
 *
 * AKA S5, but it is independent of whether or not the kernel supports
 * any other sleep support in the system.
 */

#include <linux/pm.h>
#include <linux/init.h>
#include <acpi/acpi_bus.h>
#include <linux/sched.h>
#include "sleep.h"

static void
acpi_power_off (void)
{
	printk("%s called\n",__FUNCTION__);
	/* Some SMP machines only can poweroff in boot CPU */
	set_cpus_allowed(current, cpumask_of_cpu(0));
	acpi_wakeup_gpe_poweroff_prepare();
	acpi_enter_sleep_state_prep(ACPI_STATE_S5);
	ACPI_DISABLE_IRQS();
	acpi_enter_sleep_state(ACPI_STATE_S5);
}

static int acpi_poweroff_init(void)
{
	if (!acpi_disabled) {
		u8 type_a, type_b;
		acpi_status status;

		status = acpi_get_sleep_type_data(ACPI_STATE_S5, &type_a, &type_b);
		if (ACPI_SUCCESS(status))
			pm_power_off = acpi_power_off;
	}
	return 0;
}

late_initcall(acpi_poweroff_init);
