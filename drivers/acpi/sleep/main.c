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

#include <asm/io.h>

#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>
#include "sleep.h"

u8 sleep_states[ACPI_S_STATE_COUNT];

#ifdef CONFIG_PM_SLEEP
static u32 acpi_target_sleep_state = ACPI_STATE_S0;
#endif

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

#ifdef CONFIG_SUSPEND
static struct platform_suspend_ops acpi_pm_ops;

extern void do_suspend_lowlevel(void);

static u32 acpi_suspend_states[] = {
	[PM_SUSPEND_ON] = ACPI_STATE_S0,
	[PM_SUSPEND_STANDBY] = ACPI_STATE_S1,
	[PM_SUSPEND_MEM] = ACPI_STATE_S3,
	[PM_SUSPEND_MAX] = ACPI_STATE_S5
};

static int init_8259A_after_S1;

/**
 *	acpi_pm_set_target - Set the target system sleep state to the state
 *		associated with given @pm_state, if supported.
 */

static int acpi_pm_set_target(suspend_state_t pm_state)
{
	u32 acpi_state = acpi_suspend_states[pm_state];
	int error = 0;

	if (sleep_states[acpi_state]) {
		acpi_target_sleep_state = acpi_state;
	} else {
		printk(KERN_ERR "ACPI does not support this state: %d\n",
			pm_state);
		error = -ENOSYS;
	}
	return error;
}

/**
 *	acpi_pm_prepare - Do preliminary suspend work.
 *	@pm_state: ignored
 *
 *	If necessary, set the firmware waking vector and do arch-specific
 *	nastiness to get the wakeup code to the waking vector.
 */

static int acpi_pm_prepare(suspend_state_t pm_state)
{
	int error = acpi_sleep_prepare(acpi_target_sleep_state);

	if (error)
		acpi_target_sleep_state = ACPI_STATE_S0;

	return error;
}

/**
 *	acpi_pm_enter - Actually enter a sleep state.
 *	@pm_state: ignored
 *
 *	Flush caches and go to sleep. For STR we have to call arch-specific
 *	assembly, which in turn call acpi_enter_sleep_state().
 *	It's unfortunate, but it works. Please fix if you're feeling frisky.
 */

static int acpi_pm_enter(suspend_state_t pm_state)
{
	acpi_status status = AE_OK;
	unsigned long flags = 0;
	u32 acpi_state = acpi_target_sleep_state;

	ACPI_FLUSH_CPU_CACHE();

	/* Do arch specific saving of state. */
	if (acpi_state == ACPI_STATE_S3) {
		int error = acpi_save_state_mem();

		if (error) {
			acpi_target_sleep_state = ACPI_STATE_S0;
			return error;
		}
	}

	local_irq_save(flags);
	acpi_enable_wakeup_device(acpi_state);
	switch (acpi_state) {
	case ACPI_STATE_S1:
		barrier();
		status = acpi_enter_sleep_state(acpi_state);
		break;

	case ACPI_STATE_S3:
		do_suspend_lowlevel();
		break;
	}

	/* ACPI 3.0 specs (P62) says that it's the responsabilty
	 * of the OSPM to clear the status bit [ implying that the
	 * POWER_BUTTON event should not reach userspace ]
	 */
	if (ACPI_SUCCESS(status) && (acpi_state == ACPI_STATE_S3))
		acpi_clear_event(ACPI_EVENT_POWER_BUTTON);

	local_irq_restore(flags);
	printk(KERN_DEBUG "Back to C!\n");

	/* restore processor state */
	if (acpi_state == ACPI_STATE_S3)
		acpi_restore_state_mem();

	return ACPI_SUCCESS(status) ? 0 : -EFAULT;
}

/**
 *	acpi_pm_finish - Finish up suspend sequence.
 *	@pm_state: ignored
 *
 *	This is called after we wake back up (or if entering the sleep state
 *	failed). 
 */

static int acpi_pm_finish(suspend_state_t pm_state)
{
	u32 acpi_state = acpi_target_sleep_state;

	acpi_leave_sleep_state(acpi_state);
	acpi_disable_wakeup_device(acpi_state);

	/* reset firmware waking vector */
	acpi_set_firmware_waking_vector((acpi_physical_address) 0);

	acpi_target_sleep_state = ACPI_STATE_S0;

#ifdef CONFIG_X86
	if (init_8259A_after_S1) {
		printk("Broken toshiba laptop -> kicking interrupts\n");
		init_8259A(0);
	}
#endif
	return 0;
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

static struct platform_suspend_ops acpi_pm_ops = {
	.valid = acpi_pm_state_valid,
	.set_target = acpi_pm_set_target,
	.prepare = acpi_pm_prepare,
	.enter = acpi_pm_enter,
	.finish = acpi_pm_finish,
};

/*
 * Toshiba fails to preserve interrupts over S1, reinitialization
 * of 8259 is needed after S1 resume.
 */
static int __init init_ints_after_s1(const struct dmi_system_id *d)
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
#endif /* CONFIG_SUSPEND */

#ifdef CONFIG_HIBERNATION
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
#endif				/* CONFIG_HIBERNATION */

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

#ifdef CONFIG_PM_SLEEP
/**
 *	acpi_pm_device_sleep_state - return preferred power state of ACPI device
 *		in the system sleep state given by %acpi_target_sleep_state
 *	@dev: device to examine
 *	@wake: if set, the device should be able to wake up the system
 *	@d_min_p: used to store the upper limit of allowed states range
 *	Return value: preferred power state of the device on success, -ENODEV on
 *		failure (ie. if there's no 'struct acpi_device' for @dev)
 *
 *	Find the lowest power (highest number) ACPI device power state that
 *	device @dev can be in while the system is in the sleep state represented
 *	by %acpi_target_sleep_state.  If @wake is nonzero, the device should be
 *	able to wake up the system from this sleep state.  If @d_min_p is set,
 *	the highest power (lowest number) device power state of @dev allowed
 *	in this system sleep state is stored at the location pointed to by it.
 *
 *	The caller must ensure that @dev is valid before using this function.
 *	The caller is also responsible for figuring out if the device is
 *	supposed to be able to wake up the system and passing this information
 *	via @wake.
 */

int acpi_pm_device_sleep_state(struct device *dev, int wake, int *d_min_p)
{
	acpi_handle handle = DEVICE_ACPI_HANDLE(dev);
	struct acpi_device *adev;
	char acpi_method[] = "_SxD";
	unsigned long d_min, d_max;

	if (!handle || ACPI_FAILURE(acpi_bus_get_device(handle, &adev))) {
		printk(KERN_DEBUG "ACPI handle has no context!\n");
		return -ENODEV;
	}

	acpi_method[2] = '0' + acpi_target_sleep_state;
	/*
	 * If the sleep state is S0, we will return D3, but if the device has
	 * _S0W, we will use the value from _S0W
	 */
	d_min = ACPI_STATE_D0;
	d_max = ACPI_STATE_D3;

	/*
	 * If present, _SxD methods return the minimum D-state (highest power
	 * state) we can use for the corresponding S-states.  Otherwise, the
	 * minimum D-state is D0 (ACPI 3.x).
	 *
	 * NOTE: We rely on acpi_evaluate_integer() not clobbering the integer
	 * provided -- that's our fault recovery, we ignore retval.
	 */
	if (acpi_target_sleep_state > ACPI_STATE_S0)
		acpi_evaluate_integer(handle, acpi_method, NULL, &d_min);

	/*
	 * If _PRW says we can wake up the system from the target sleep state,
	 * the D-state returned by _SxD is sufficient for that (we assume a
	 * wakeup-aware driver if wake is set).  Still, if _SxW exists
	 * (ACPI 3.x), it should return the maximum (lowest power) D-state that
	 * can wake the system.  _S0W may be valid, too.
	 */
	if (acpi_target_sleep_state == ACPI_STATE_S0 ||
	    (wake && adev->wakeup.state.enabled &&
	     adev->wakeup.sleep_state <= acpi_target_sleep_state)) {
		acpi_method[3] = 'W';
		acpi_evaluate_integer(handle, acpi_method, NULL, &d_max);
		/* Sanity check */
		if (d_max < d_min)
			d_min = d_max;
	}

	if (d_min_p)
		*d_min_p = d_min;
	return d_max;
}
#endif

static void acpi_power_off_prepare(void)
{
	/* Prepare to power off the system */
	acpi_sleep_prepare(ACPI_STATE_S5);
}

static void acpi_power_off(void)
{
	/* acpi_sleep_prepare(ACPI_STATE_S5) should have already been called */
	printk("%s called\n", __FUNCTION__);
	local_irq_disable();
	acpi_enter_sleep_state(ACPI_STATE_S5);
}

int __init acpi_sleep_init(void)
{
	acpi_status status;
	u8 type_a, type_b;
#ifdef CONFIG_SUSPEND
	int i = 0;

	dmi_check_system(acpisleep_dmi_table);
#endif

	if (acpi_disabled)
		return 0;

	sleep_states[ACPI_STATE_S0] = 1;
	printk(KERN_INFO PREFIX "(supports S0");

#ifdef CONFIG_SUSPEND
	for (i = ACPI_STATE_S1; i < ACPI_STATE_S4; i++) {
		status = acpi_get_sleep_type_data(i, &type_a, &type_b);
		if (ACPI_SUCCESS(status)) {
			sleep_states[i] = 1;
			printk(" S%d", i);
		}
	}

	suspend_set_ops(&acpi_pm_ops);
#endif

#ifdef CONFIG_HIBERNATION
	status = acpi_get_sleep_type_data(ACPI_STATE_S4, &type_a, &type_b);
	if (ACPI_SUCCESS(status)) {
		hibernation_set_ops(&acpi_hibernation_ops);
		sleep_states[ACPI_STATE_S4] = 1;
		printk(" S4");
	}
#endif
	status = acpi_get_sleep_type_data(ACPI_STATE_S5, &type_a, &type_b);
	if (ACPI_SUCCESS(status)) {
		sleep_states[ACPI_STATE_S5] = 1;
		printk(" S5");
		pm_power_off_prepare = acpi_power_off_prepare;
		pm_power_off = acpi_power_off;
	}
	printk(")\n");
	return 0;
}
