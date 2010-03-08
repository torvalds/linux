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
#include <linux/reboot.h>

#include <asm/io.h>

#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>

#include "internal.h"
#include "sleep.h"

u8 sleep_states[ACPI_S_STATE_COUNT];

static void acpi_sleep_tts_switch(u32 acpi_state)
{
	union acpi_object in_arg = { ACPI_TYPE_INTEGER };
	struct acpi_object_list arg_list = { 1, &in_arg };
	acpi_status status = AE_OK;

	in_arg.integer.value = acpi_state;
	status = acpi_evaluate_object(NULL, "\\_TTS", &arg_list, NULL);
	if (ACPI_FAILURE(status) && status != AE_NOT_FOUND) {
		/*
		 * OS can't evaluate the _TTS object correctly. Some warning
		 * message will be printed. But it won't break anything.
		 */
		printk(KERN_NOTICE "Failure in evaluating _TTS object\n");
	}
}

static int tts_notify_reboot(struct notifier_block *this,
			unsigned long code, void *x)
{
	acpi_sleep_tts_switch(ACPI_STATE_S5);
	return NOTIFY_DONE;
}

static struct notifier_block tts_notifier = {
	.notifier_call	= tts_notify_reboot,
	.next		= NULL,
	.priority	= 0,
};

static int acpi_sleep_prepare(u32 acpi_state)
{
#ifdef CONFIG_ACPI_SLEEP
	/* do we have a wakeup address for S2 and S3? */
	if (acpi_state == ACPI_STATE_S3) {
		if (!acpi_wakeup_address) {
			return -EFAULT;
		}
		acpi_set_firmware_waking_vector(
				(acpi_physical_address)acpi_wakeup_address);

	}
	ACPI_FLUSH_CPU_CACHE();
	acpi_enable_wakeup_device_prep(acpi_state);
#endif
	printk(KERN_INFO PREFIX "Preparing to enter system sleep state S%d\n",
		acpi_state);
	acpi_enter_sleep_state_prep(acpi_state);
	return 0;
}

#ifdef CONFIG_ACPI_SLEEP
static u32 acpi_target_sleep_state = ACPI_STATE_S0;
/*
 * According to the ACPI specification the BIOS should make sure that ACPI is
 * enabled and SCI_EN bit is set on wake-up from S1 - S3 sleep states.  Still,
 * some BIOSes don't do that and therefore we use acpi_enable() to enable ACPI
 * on such systems during resume.  Unfortunately that doesn't help in
 * particularly pathological cases in which SCI_EN has to be set directly on
 * resume, although the specification states very clearly that this flag is
 * owned by the hardware.  The set_sci_en_on_resume variable will be set in such
 * cases.
 */
static bool set_sci_en_on_resume;

void __init acpi_set_sci_en_on_resume(void)
{
	set_sci_en_on_resume = true;
}

/*
 * ACPI 1.0 wants us to execute _PTS before suspending devices, so we allow the
 * user to request that behavior by using the 'acpi_old_suspend_ordering'
 * kernel command line option that causes the following variable to be set.
 */
static bool old_suspend_ordering;

void __init acpi_old_suspend_ordering(void)
{
	old_suspend_ordering = true;
}

/**
 *	acpi_pm_disable_gpes - Disable the GPEs.
 */
static int acpi_pm_disable_gpes(void)
{
	acpi_disable_all_gpes();
	return 0;
}

/**
 *	__acpi_pm_prepare - Prepare the platform to enter the target state.
 *
 *	If necessary, set the firmware waking vector and do arch-specific
 *	nastiness to get the wakeup code to the waking vector.
 */
static int __acpi_pm_prepare(void)
{
	int error = acpi_sleep_prepare(acpi_target_sleep_state);

	if (error)
		acpi_target_sleep_state = ACPI_STATE_S0;
	return error;
}

/**
 *	acpi_pm_prepare - Prepare the platform to enter the target sleep
 *		state and disable the GPEs.
 */
static int acpi_pm_prepare(void)
{
	int error = __acpi_pm_prepare();

	if (!error)
		acpi_disable_all_gpes();
	return error;
}

/**
 *	acpi_pm_finish - Instruct the platform to leave a sleep state.
 *
 *	This is called after we wake back up (or if entering the sleep state
 *	failed).
 */
static void acpi_pm_finish(void)
{
	u32 acpi_state = acpi_target_sleep_state;

	if (acpi_state == ACPI_STATE_S0)
		return;

	printk(KERN_INFO PREFIX "Waking up from system sleep state S%d\n",
		acpi_state);
	acpi_disable_wakeup_device(acpi_state);
	acpi_leave_sleep_state(acpi_state);

	/* reset firmware waking vector */
	acpi_set_firmware_waking_vector((acpi_physical_address) 0);

	acpi_target_sleep_state = ACPI_STATE_S0;
}

/**
 *	acpi_pm_end - Finish up suspend sequence.
 */
static void acpi_pm_end(void)
{
	/*
	 * This is necessary in case acpi_pm_finish() is not called during a
	 * failing transition to a sleep state.
	 */
	acpi_target_sleep_state = ACPI_STATE_S0;
	acpi_sleep_tts_switch(acpi_target_sleep_state);
}
#else /* !CONFIG_ACPI_SLEEP */
#define acpi_target_sleep_state	ACPI_STATE_S0
#endif /* CONFIG_ACPI_SLEEP */

#ifdef CONFIG_SUSPEND
extern void do_suspend_lowlevel(void);

static u32 acpi_suspend_states[] = {
	[PM_SUSPEND_ON] = ACPI_STATE_S0,
	[PM_SUSPEND_STANDBY] = ACPI_STATE_S1,
	[PM_SUSPEND_MEM] = ACPI_STATE_S3,
	[PM_SUSPEND_MAX] = ACPI_STATE_S5
};

/**
 *	acpi_suspend_begin - Set the target system sleep state to the state
 *		associated with given @pm_state, if supported.
 */
static int acpi_suspend_begin(suspend_state_t pm_state)
{
	u32 acpi_state = acpi_suspend_states[pm_state];
	int error = 0;

	if (sleep_states[acpi_state]) {
		acpi_target_sleep_state = acpi_state;
		acpi_sleep_tts_switch(acpi_target_sleep_state);
	} else {
		printk(KERN_ERR "ACPI does not support this state: %d\n",
			pm_state);
		error = -ENOSYS;
	}
	return error;
}

/**
 *	acpi_suspend_enter - Actually enter a sleep state.
 *	@pm_state: ignored
 *
 *	Flush caches and go to sleep. For STR we have to call arch-specific
 *	assembly, which in turn call acpi_enter_sleep_state().
 *	It's unfortunate, but it works. Please fix if you're feeling frisky.
 */
static int acpi_suspend_enter(suspend_state_t pm_state)
{
	acpi_status status = AE_OK;
	unsigned long flags = 0;
	u32 acpi_state = acpi_target_sleep_state;

	ACPI_FLUSH_CPU_CACHE();

	/* Do arch specific saving of state. */
	if (acpi_state == ACPI_STATE_S3) {
		int error = acpi_save_state_mem();

		if (error)
			return error;
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

	/* If ACPI is not enabled by the BIOS, we need to enable it here. */
	if (set_sci_en_on_resume)
		acpi_write_bit_register(ACPI_BITREG_SCI_ENABLE, 1);
	else
		acpi_enable();

	/* Reprogram control registers and execute _BFS */
	acpi_leave_sleep_state_prep(acpi_state);

	/* ACPI 3.0 specs (P62) says that it's the responsibility
	 * of the OSPM to clear the status bit [ implying that the
	 * POWER_BUTTON event should not reach userspace ]
	 */
	if (ACPI_SUCCESS(status) && (acpi_state == ACPI_STATE_S3))
		acpi_clear_event(ACPI_EVENT_POWER_BUTTON);

	/*
	 * Disable and clear GPE status before interrupt is enabled. Some GPEs
	 * (like wakeup GPE) haven't handler, this can avoid such GPE misfire.
	 * acpi_leave_sleep_state will reenable specific GPEs later
	 */
	acpi_disable_all_gpes();

	local_irq_restore(flags);
	printk(KERN_DEBUG "Back to C!\n");

	/* restore processor state */
	if (acpi_state == ACPI_STATE_S3)
		acpi_restore_state_mem();

	return ACPI_SUCCESS(status) ? 0 : -EFAULT;
}

static int acpi_suspend_state_valid(suspend_state_t pm_state)
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

static struct platform_suspend_ops acpi_suspend_ops = {
	.valid = acpi_suspend_state_valid,
	.begin = acpi_suspend_begin,
	.prepare_late = acpi_pm_prepare,
	.enter = acpi_suspend_enter,
	.wake = acpi_pm_finish,
	.end = acpi_pm_end,
};

/**
 *	acpi_suspend_begin_old - Set the target system sleep state to the
 *		state associated with given @pm_state, if supported, and
 *		execute the _PTS control method.  This function is used if the
 *		pre-ACPI 2.0 suspend ordering has been requested.
 */
static int acpi_suspend_begin_old(suspend_state_t pm_state)
{
	int error = acpi_suspend_begin(pm_state);

	if (!error)
		error = __acpi_pm_prepare();
	return error;
}

/*
 * The following callbacks are used if the pre-ACPI 2.0 suspend ordering has
 * been requested.
 */
static struct platform_suspend_ops acpi_suspend_ops_old = {
	.valid = acpi_suspend_state_valid,
	.begin = acpi_suspend_begin_old,
	.prepare_late = acpi_pm_disable_gpes,
	.enter = acpi_suspend_enter,
	.wake = acpi_pm_finish,
	.end = acpi_pm_end,
	.recover = acpi_pm_finish,
};

static int __init init_old_suspend_ordering(const struct dmi_system_id *d)
{
	old_suspend_ordering = true;
	return 0;
}

static int __init init_set_sci_en_on_resume(const struct dmi_system_id *d)
{
	set_sci_en_on_resume = true;
	return 0;
}

static struct dmi_system_id __initdata acpisleep_dmi_table[] = {
	{
	.callback = init_old_suspend_ordering,
	.ident = "Abit KN9 (nForce4 variant)",
	.matches = {
		DMI_MATCH(DMI_BOARD_VENDOR, "http://www.abit.com.tw/"),
		DMI_MATCH(DMI_BOARD_NAME, "KN9 Series(NF-CK804)"),
		},
	},
	{
	.callback = init_old_suspend_ordering,
	.ident = "HP xw4600 Workstation",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
		DMI_MATCH(DMI_PRODUCT_NAME, "HP xw4600 Workstation"),
		},
	},
	{
	.callback = init_set_sci_en_on_resume,
	.ident = "Apple MacBook 1,1",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Apple Computer, Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "MacBook1,1"),
		},
	},
	{
	.callback = init_set_sci_en_on_resume,
	.ident = "Apple MacMini 1,1",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Apple Computer, Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "Macmini1,1"),
		},
	},
	{
	.callback = init_old_suspend_ordering,
	.ident = "Asus Pundit P1-AH2 (M2N8L motherboard)",
	.matches = {
		DMI_MATCH(DMI_BOARD_VENDOR, "ASUSTek Computer INC."),
		DMI_MATCH(DMI_BOARD_NAME, "M2N8L"),
		},
	},
	{
	.callback = init_set_sci_en_on_resume,
	.ident = "Toshiba Satellite L300",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
		DMI_MATCH(DMI_PRODUCT_NAME, "Satellite L300"),
		},
	},
	{
	.callback = init_set_sci_en_on_resume,
	.ident = "Hewlett-Packard HP G7000 Notebook PC",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
		DMI_MATCH(DMI_PRODUCT_NAME, "HP G7000 Notebook PC"),
		},
	},
	{
	.callback = init_set_sci_en_on_resume,
	.ident = "Hewlett-Packard HP Pavilion dv3 Notebook PC",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
		DMI_MATCH(DMI_PRODUCT_NAME, "HP Pavilion dv3 Notebook PC"),
		},
	},
	{
	.callback = init_set_sci_en_on_resume,
	.ident = "Hewlett-Packard Pavilion dv4",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
		DMI_MATCH(DMI_PRODUCT_NAME, "HP Pavilion dv4"),
		},
	},
	{
	.callback = init_set_sci_en_on_resume,
	.ident = "Hewlett-Packard Pavilion dv7",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
		DMI_MATCH(DMI_PRODUCT_NAME, "HP Pavilion dv7"),
		},
	},
	{
	.callback = init_set_sci_en_on_resume,
	.ident = "Hewlett-Packard Compaq Presario C700 Notebook PC",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
		DMI_MATCH(DMI_PRODUCT_NAME, "Compaq Presario C700 Notebook PC"),
		},
	},
	{
	.callback = init_set_sci_en_on_resume,
	.ident = "Hewlett-Packard Compaq Presario CQ40 Notebook PC",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
		DMI_MATCH(DMI_PRODUCT_NAME, "Compaq Presario CQ40 Notebook PC"),
		},
	},
	{
	.callback = init_old_suspend_ordering,
	.ident = "Panasonic CF51-2L",
	.matches = {
		DMI_MATCH(DMI_BOARD_VENDOR,
				"Matsushita Electric Industrial Co.,Ltd."),
		DMI_MATCH(DMI_BOARD_NAME, "CF51-2L"),
		},
	},
	{},
};
#endif /* CONFIG_SUSPEND */

#ifdef CONFIG_HIBERNATION
/*
 * The ACPI specification wants us to save NVS memory regions during hibernation
 * and to restore them during the subsequent resume.  However, it is not certain
 * if this mechanism is going to work on all machines, so we allow the user to
 * disable this mechanism using the 'acpi_sleep=s4_nonvs' kernel command line
 * option.
 */
static bool s4_no_nvs;

void __init acpi_s4_no_nvs(void)
{
	s4_no_nvs = true;
}

static unsigned long s4_hardware_signature;
static struct acpi_table_facs *facs;
static bool nosigcheck;

void __init acpi_no_s4_hw_signature(void)
{
	nosigcheck = true;
}

static int acpi_hibernation_begin(void)
{
	int error;

	error = s4_no_nvs ? 0 : hibernate_nvs_alloc();
	if (!error) {
		acpi_target_sleep_state = ACPI_STATE_S4;
		acpi_sleep_tts_switch(acpi_target_sleep_state);
	}

	return error;
}

static int acpi_hibernation_pre_snapshot(void)
{
	int error = acpi_pm_prepare();

	if (!error)
		hibernate_nvs_save();

	return error;
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
	/* Reprogram control registers and execute _BFS */
	acpi_leave_sleep_state_prep(ACPI_STATE_S4);
	local_irq_restore(flags);

	return ACPI_SUCCESS(status) ? 0 : -EFAULT;
}

static void acpi_hibernation_finish(void)
{
	hibernate_nvs_free();
	acpi_pm_finish();
}

static void acpi_hibernation_leave(void)
{
	/*
	 * If ACPI is not enabled by the BIOS and the boot kernel, we need to
	 * enable it here.
	 */
	acpi_enable();
	/* Reprogram control registers and execute _BFS */
	acpi_leave_sleep_state_prep(ACPI_STATE_S4);
	/* Check the hardware signature */
	if (facs && s4_hardware_signature != facs->hardware_signature) {
		printk(KERN_EMERG "ACPI: Hardware changed while hibernated, "
			"cannot resume!\n");
		panic("ACPI S4 hardware signature mismatch");
	}
	/* Restore the NVS memory area */
	hibernate_nvs_restore();
}

static void acpi_pm_enable_gpes(void)
{
	acpi_enable_all_runtime_gpes();
}

static struct platform_hibernation_ops acpi_hibernation_ops = {
	.begin = acpi_hibernation_begin,
	.end = acpi_pm_end,
	.pre_snapshot = acpi_hibernation_pre_snapshot,
	.finish = acpi_hibernation_finish,
	.prepare = acpi_pm_prepare,
	.enter = acpi_hibernation_enter,
	.leave = acpi_hibernation_leave,
	.pre_restore = acpi_pm_disable_gpes,
	.restore_cleanup = acpi_pm_enable_gpes,
};

/**
 *	acpi_hibernation_begin_old - Set the target system sleep state to
 *		ACPI_STATE_S4 and execute the _PTS control method.  This
 *		function is used if the pre-ACPI 2.0 suspend ordering has been
 *		requested.
 */
static int acpi_hibernation_begin_old(void)
{
	int error;
	/*
	 * The _TTS object should always be evaluated before the _PTS object.
	 * When the old_suspended_ordering is true, the _PTS object is
	 * evaluated in the acpi_sleep_prepare.
	 */
	acpi_sleep_tts_switch(ACPI_STATE_S4);

	error = acpi_sleep_prepare(ACPI_STATE_S4);

	if (!error) {
		if (!s4_no_nvs)
			error = hibernate_nvs_alloc();
		if (!error)
			acpi_target_sleep_state = ACPI_STATE_S4;
	}
	return error;
}

static int acpi_hibernation_pre_snapshot_old(void)
{
	int error = acpi_pm_disable_gpes();

	if (!error)
		hibernate_nvs_save();

	return error;
}

/*
 * The following callbacks are used if the pre-ACPI 2.0 suspend ordering has
 * been requested.
 */
static struct platform_hibernation_ops acpi_hibernation_ops_old = {
	.begin = acpi_hibernation_begin_old,
	.end = acpi_pm_end,
	.pre_snapshot = acpi_hibernation_pre_snapshot_old,
	.finish = acpi_hibernation_finish,
	.prepare = acpi_pm_disable_gpes,
	.enter = acpi_hibernation_enter,
	.leave = acpi_hibernation_leave,
	.pre_restore = acpi_pm_disable_gpes,
	.restore_cleanup = acpi_pm_enable_gpes,
	.recover = acpi_pm_finish,
};
#endif /* CONFIG_HIBERNATION */

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
 *	@dev: device to examine; its driver model wakeup flags control
 *		whether it should be able to wake up the system
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

int acpi_pm_device_sleep_state(struct device *dev, int *d_min_p)
{
	acpi_handle handle = DEVICE_ACPI_HANDLE(dev);
	struct acpi_device *adev;
	char acpi_method[] = "_SxD";
	unsigned long long d_min, d_max;

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
	    (device_may_wakeup(dev) && adev->wakeup.state.enabled &&
	     adev->wakeup.sleep_state <= acpi_target_sleep_state)) {
		acpi_status status;

		acpi_method[3] = 'W';
		status = acpi_evaluate_integer(handle, acpi_method, NULL,
						&d_max);
		if (ACPI_FAILURE(status)) {
			d_max = d_min;
		} else if (d_max < d_min) {
			/* Warn the user of the broken DSDT */
			printk(KERN_WARNING "ACPI: Wrong value from %s\n",
				acpi_method);
			/* Sanitize it */
			d_min = d_max;
		}
	}

	if (d_min_p)
		*d_min_p = d_min;
	return d_max;
}

/**
 *	acpi_pm_device_sleep_wake - enable or disable the system wake-up
 *                                  capability of given device
 *	@dev: device to handle
 *	@enable: 'true' - enable, 'false' - disable the wake-up capability
 */
int acpi_pm_device_sleep_wake(struct device *dev, bool enable)
{
	acpi_handle handle;
	struct acpi_device *adev;
	int error;

	if (!device_can_wakeup(dev))
		return -EINVAL;

	handle = DEVICE_ACPI_HANDLE(dev);
	if (!handle || ACPI_FAILURE(acpi_bus_get_device(handle, &adev))) {
		dev_dbg(dev, "ACPI handle has no context in %s!\n", __func__);
		return -ENODEV;
	}

	if (enable) {
		error = acpi_enable_wakeup_device_power(adev,
						acpi_target_sleep_state);
		if (!error)
			acpi_enable_gpe(adev->wakeup.gpe_device,
					adev->wakeup.gpe_number,
					ACPI_GPE_TYPE_WAKE);
	} else {
		acpi_disable_gpe(adev->wakeup.gpe_device, adev->wakeup.gpe_number,
				ACPI_GPE_TYPE_WAKE);
		error = acpi_disable_wakeup_device_power(adev);
	}
	if (!error)
		dev_info(dev, "wake-up capability %s by ACPI\n",
				enable ? "enabled" : "disabled");

	return error;
}
#endif

static void acpi_power_off_prepare(void)
{
	/* Prepare to power off the system */
	acpi_sleep_prepare(ACPI_STATE_S5);
	acpi_disable_all_gpes();
}

static void acpi_power_off(void)
{
	/* acpi_sleep_prepare(ACPI_STATE_S5) should have already been called */
	printk(KERN_DEBUG "%s called\n", __func__);
	local_irq_disable();
	acpi_enable_wakeup_device(ACPI_STATE_S5);
	acpi_enter_sleep_state(ACPI_STATE_S5);
}

/*
 * ACPI 2.0 created the optional _GTS and _BFS,
 * but industry adoption has been neither rapid nor broad.
 *
 * Linux gets into trouble when it executes poorly validated
 * paths through the BIOS, so disable _GTS and _BFS by default,
 * but do speak up and offer the option to enable them.
 */
void __init acpi_gts_bfs_check(void)
{
	acpi_handle dummy;

	if (ACPI_SUCCESS(acpi_get_handle(ACPI_ROOT_OBJECT, METHOD_NAME__GTS, &dummy)))
	{
		printk(KERN_NOTICE PREFIX "BIOS offers _GTS\n");
		printk(KERN_NOTICE PREFIX "If \"acpi.gts=1\" improves suspend, "
			"please notify linux-acpi@vger.kernel.org\n");
	}
	if (ACPI_SUCCESS(acpi_get_handle(ACPI_ROOT_OBJECT, METHOD_NAME__BFS, &dummy)))
	{
		printk(KERN_NOTICE PREFIX "BIOS offers _BFS\n");
		printk(KERN_NOTICE PREFIX "If \"acpi.bfs=1\" improves resume, "
			"please notify linux-acpi@vger.kernel.org\n");
	}
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

	suspend_set_ops(old_suspend_ordering ?
		&acpi_suspend_ops_old : &acpi_suspend_ops);
#endif

#ifdef CONFIG_HIBERNATION
	status = acpi_get_sleep_type_data(ACPI_STATE_S4, &type_a, &type_b);
	if (ACPI_SUCCESS(status)) {
		hibernation_set_ops(old_suspend_ordering ?
			&acpi_hibernation_ops_old : &acpi_hibernation_ops);
		sleep_states[ACPI_STATE_S4] = 1;
		printk(" S4");
		if (!nosigcheck) {
			acpi_get_table(ACPI_SIG_FACS, 1,
				(struct acpi_table_header **)&facs);
			if (facs)
				s4_hardware_signature =
					facs->hardware_signature;
		}
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
	/*
	 * Register the tts_notifier to reboot notifier list so that the _TTS
	 * object can also be evaluated when the system enters S5.
	 */
	register_reboot_notifier(&tts_notifier);
	acpi_gts_bfs_check();
	return 0;
}
