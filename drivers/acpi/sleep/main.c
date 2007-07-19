/*
 * sleep.c - ACPI sleep support.
 *
 * Copyright (c) 2005 Alexey Starikovskiy <alexey.y.starikovskiy@intel.com>
 * Copyright (c) 2004 David Shaohua Li <shaohua.li@intel.com>
 * Copyright (c) 2000-2003 Patrick Mochel
 * Copyright (c) 2003 Open Source Development Lab
 *
 * This file is released under the GPLv2.
 *
 */

#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/dmi.h>
#include <linux/device.h>
#include <linux/suspend.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>
#include "sleep.h"

u8 sleep_states[ACPI_S_STATE_COUNT];

static struct pm_ops acpi_pm_ops;

extern void do_suspend_lowlevel(void);

static u32 acpi_suspend_states[] = {
	[PM_SUSPEND_ON] = ACPI_STATE_S0,
	[PM_SUSPEND_STANDBY] = ACPI_STATE_S1,
	[PM_SUSPEND_MEM] = ACPI_STATE_S3,
	[PM_SUSPEND_MAX] = ACPI_STATE_S5
};

static int init_8259A_after_S1;

/**
 *	acpi_pm_prepare - Do preliminary suspend work.
 *	@pm_state:		suspend state we're entering.
 *
 *	Make sure we support the state. If we do, and we need it, set the
 *	firmware waking vector and do arch-specific nastiness to get the 
 *	wakeup code to the waking vector. 
 */

extern int acpi_sleep_prepare(u32 acpi_state);
extern void acpi_power_off(void);

static int acpi_pm_prepare(suspend_state_t pm_state)
{
	u32 acpi_state = acpi_suspend_states[pm_state];

	if (!sleep_states[acpi_state]) {
		printk("acpi_pm_prepare does not support %d \n", pm_state);
		return -EPERM;
	}
	return acpi_sleep_prepare(acpi_state);
}

/**
 *	acpi_pm_enter - Actually enter a sleep state.
 *	@pm_state:		State we're entering.
 *
 *	Flush caches and go to sleep. For STR or STD, we have to call 
 *	arch-specific assembly, which in turn call acpi_enter_sleep_state().
 *	It's unfortunate, but it works. Please fix if you're feeling frisky.
 */

static int acpi_pm_enter(suspend_state_t pm_state)
{
	acpi_status status = AE_OK;
	unsigned long flags = 0;
	u32 acpi_state = acpi_suspend_states[pm_state];

	ACPI_FLUSH_CPU_CACHE();

	/* Do arch specific saving of state. */
	if (pm_state > PM_SUSPEND_STANDBY) {
		int error = acpi_save_state_mem();
		if (error)
			return error;
	}

	local_irq_save(flags);
	acpi_enable_wakeup_device(acpi_state);
	switch (pm_state) {
	case PM_SUSPEND_STANDBY:
		barrier();
		status = acpi_enter_sleep_state(acpi_state);
		break;

	case PM_SUSPEND_MEM:
		do_suspend_lowlevel();
		break;

	default:
		return -EINVAL;
	}

	/* ACPI 3.0 specs (P62) says that it's the responsabilty
	 * of the OSPM to clear the status bit [ implying that the
	 * POWER_BUTTON event should not reach userspace ]
	 */
	if (ACPI_SUCCESS(status) && (acpi_state == ACPI_STATE_S3))
		acpi_clear_event(ACPI_EVENT_POWER_BUTTON);

	local_irq_restore(flags);
	printk(KERN_DEBUG "Back to C!\n");

	/* restore processor state
	 * We should only be here if we're coming back from STR or STD.
	 * And, in the case of the latter, the memory image should have already
	 * been loaded from disk.
	 */
	if (pm_state > PM_SUSPEND_STANDBY)
		acpi_restore_state_mem();

	return ACPI_SUCCESS(status) ? 0 : -EFAULT;
}

/**
 *	acpi_pm_finish - Finish up suspend sequence.
 *	@pm_state:		State we're coming out of.
 *
 *	This is called after we wake back up (or if entering the sleep state
 *	failed). 
 */

static int acpi_pm_finish(suspend_state_t pm_state)
{
	u32 acpi_state = acpi_suspend_states[pm_state];

	acpi_leave_sleep_state(acpi_state);
	acpi_disable_wakeup_device(acpi_state);

	/* reset firmware waking vector */
	acpi_set_firmware_waking_vector((acpi_physical_address) 0);

	if (init_8259A_after_S1) {
		printk("Broken toshiba laptop -> kicking interrupts\n");
		init_8259A(0);
	}
	return 0;
}

int acpi_suspend(u32 acpi_state)
{
	suspend_state_t states[] = {
		[1] = PM_SUSPEND_STANDBY,
		[3] = PM_SUSPEND_MEM,
		[5] = PM_SUSPEND_MAX
	};

	if (acpi_state < 6 && states[acpi_state])
		return pm_suspend(states[acpi_state]);
	if (acpi_state == 4)
		return hibernate();
	return -EINVAL;
}

static int acpi_pm_state_valid(suspend_state_t pm_state)
{
	u32 acpi_state;

	switch (pm_state) {
	case PM_SUSPEND_ON:
	case PM_SUSPEND_STANDBY:
	case PM_SUSPEND_MEM:
		acpi_state = acpi_suspend_states[pm_state];

		return sleep_states[acpi_state];
	default:
		return 0;
	}
}

static struct pm_ops acpi_pm_ops = {
	.valid = acpi_pm_state_valid,
	.prepare = acpi_pm_prepare,
	.enter = acpi_pm_enter,
	.finish = acpi_pm_finish,
};

#ifdef CONFIG_SOFTWARE_SUSPEND
static int acpi_hibernation_prepare(void)
{
	return acpi_sleep_prepare(ACPI_STATE_S4);
}

static int acpi_hibernation_enter(void)
{
	acpi_status status = AE_OK;
	unsigned long flags = 0;

	ACPI_FLUSH_CPU_CACHE();

	local_irq_save(flags);
	acpi_enable_wakeup_device(ACPI_STATE_S4);
	/* This shouldn't return.  If it returns, we have a problem */
	status = acpi_enter_sleep_state(ACPI_STATE_S4);
	local_irq_restore(flags);

	return ACPI_SUCCESS(status) ? 0 : -EFAULT;
}

static void acpi_hibernation_finish(void)
{
	acpi_leave_sleep_state(ACPI_STATE_S4);
	acpi_disable_wakeup_device(ACPI_STATE_S4);

	/* reset firmware waking vector */
	acpi_set_firmware_waking_vector((acpi_physical_address) 0);

	if (init_8259A_after_S1) {
		printk("Broken toshiba laptop -> kicking interrupts\n");
		init_8259A(0);
	}
}

static int acpi_hibernation_pre_restore(void)
{
	acpi_status status;

	status = acpi_hw_disable_all_gpes();

	return ACPI_SUCCESS(status) ? 0 : -EFAULT;
}

static void acpi_hibernation_restore_cleanup(void)
{
	acpi_hw_enable_all_runtime_gpes();
}

static struct hibernation_ops acpi_hibernation_ops = {
	.prepare = acpi_hibernation_prepare,
	.enter = acpi_hibernation_enter,
	.finish = acpi_hibernation_finish,
	.pre_restore = acpi_hibernation_pre_restore,
	.restore_cleanup = acpi_hibernation_restore_cleanup,
};
#endif				/* CONFIG_SOFTWARE_SUSPEND */

/*
 * Toshiba fails to preserve interrupts over S1, reinitialization
 * of 8259 is needed after S1 resume.
 */
static int __init init_ints_after_s1(struct dmi_system_id *d)
{
	printk(KERN_WARNING "%s with broken S1 detected.\n", d->ident);
	init_8259A_after_S1 = 1;
	return 0;
}

static struct dmi_system_id __initdata acpisleep_dmi_table[] = {
	{
	 .callback = init_ints_after_s1,
	 .ident = "Toshiba Satellite 4030cdt",
	 .matches = {DMI_MATCH(DMI_PRODUCT_NAME, "S4030CDT/4.3"),},
	 },
	{},
};

int __init acpi_sleep_init(void)
{
	int i = 0;

	dmi_check_system(acpisleep_dmi_table);

	if (acpi_disabled)
		return 0;

	printk(KERN_INFO PREFIX "(supports");
	for (i = 0; i < ACPI_S_STATE_COUNT; i++) {
		acpi_status status;
		u8 type_a, type_b;
		status = acpi_get_sleep_type_data(i, &type_a, &type_b);
		if (ACPI_SUCCESS(status)) {
			sleep_states[i] = 1;
			printk(" S%d", i);
		}
	}
	printk(")\n");

	pm_set_ops(&acpi_pm_ops);

#ifdef CONFIG_SOFTWARE_SUSPEND
	if (sleep_states[ACPI_STATE_S4])
		hibernation_set_ops(&acpi_hibernation_ops);
#else
	sleep_states[ACPI_STATE_S4] = 0;
#endif

	return 0;
}
