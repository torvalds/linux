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
#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>

#include <asm/io.h>

#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>

#include "internal.h"
#include "sleep.h"

u8 wake_sleep_flags = ACPI_NO_OPTIONAL_METHODS;
static unsigned int gts, bfs;
static int set_param_wake_flag(const char *val, struct kernel_param *kp)
{
	int ret = param_set_int(val, kp);

	if (ret)
		return ret;

	if (kp->arg == (const char *)&gts) {
		if (gts)
			wake_sleep_flags |= ACPI_EXECUTE_GTS;
		else
			wake_sleep_flags &= ~ACPI_EXECUTE_GTS;
	}
	if (kp->arg == (const char *)&bfs) {
		if (bfs)
			wake_sleep_flags |= ACPI_EXECUTE_BFS;
		else
			wake_sleep_flags &= ~ACPI_EXECUTE_BFS;
	}
	return ret;
}
module_param_call(gts, set_param_wake_flag, param_get_int, &gts, 0644);
module_param_call(bfs, set_param_wake_flag, param_get_int, &bfs, 0644);
MODULE_PARM_DESC(gts, "Enable evaluation of _GTS on suspend.");
MODULE_PARM_DESC(bfs, "Enable evaluation of _BFS on resume".);

static u8 sleep_states[ACPI_S_STATE_COUNT];
static bool pwr_btn_event_pending;

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
		if (!acpi_wakeup_address)
			return -EFAULT;
		acpi_set_firmware_waking_vector(acpi_wakeup_address);

	}
	ACPI_FLUSH_CPU_CACHE();
#endif
	printk(KERN_INFO PREFIX "Preparing to enter system sleep state S%d\n",
		acpi_state);
	acpi_enable_wakeup_devices(acpi_state);
	acpi_enter_sleep_state_prep(acpi_state);
	return 0;
}

#ifdef CONFIG_ACPI_SLEEP
static u32 acpi_target_sleep_state = ACPI_STATE_S0;

/*
 * The ACPI specification wants us to save NVS memory regions during hibernation
 * and to restore them during the subsequent resume.  Windows does that also for
 * suspend to RAM.  However, it is known that this mechanism does not work on
 * all machines, so we allow the user to disable it with the help of the
 * 'acpi_sleep=nonvs' kernel command line option.
 */
static bool nvs_nosave;

void __init acpi_nvs_nosave(void)
{
	nvs_nosave = true;
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
 * acpi_pm_freeze - Disable the GPEs and suspend EC transactions.
 */
static int acpi_pm_freeze(void)
{
	acpi_disable_all_gpes();
	acpi_os_wait_events_complete();
	acpi_ec_block_transactions();
	return 0;
}

/**
 * acpi_pre_suspend - Enable wakeup devices, "freeze" EC and save NVS.
 */
static int acpi_pm_pre_suspend(void)
{
	acpi_pm_freeze();
	return suspend_nvs_save();
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
		error = acpi_pm_pre_suspend();

	return error;
}

static int find_powerf_dev(struct device *dev, void *data)
{
	struct acpi_device *device = to_acpi_device(dev);
	const char *hid = acpi_device_hid(device);

	return !strcmp(hid, ACPI_BUTTON_HID_POWERF);
}

/**
 *	acpi_pm_finish - Instruct the platform to leave a sleep state.
 *
 *	This is called after we wake back up (or if entering the sleep state
 *	failed).
 */
static void acpi_pm_finish(void)
{
	struct device *pwr_btn_dev;
	u32 acpi_state = acpi_target_sleep_state;

	acpi_ec_unblock_transactions();
	suspend_nvs_free();

	if (acpi_state == ACPI_STATE_S0)
		return;

	printk(KERN_INFO PREFIX "Waking up from system sleep state S%d\n",
		acpi_state);
	acpi_disable_wakeup_devices(acpi_state);
	acpi_leave_sleep_state(acpi_state);

	/* reset firmware waking vector */
	acpi_set_firmware_waking_vector((acpi_physical_address) 0);

	acpi_target_sleep_state = ACPI_STATE_S0;

	/* If we were woken with the fixed power button, provide a small
	 * hint to userspace in the form of a wakeup event on the fixed power
	 * button device (if it can be found).
	 *
	 * We delay the event generation til now, as the PM layer requires
	 * timekeeping to be running before we generate events. */
	if (!pwr_btn_event_pending)
		return;

	pwr_btn_event_pending = false;
	pwr_btn_dev = bus_find_device(&acpi_bus_type, NULL, NULL,
				      find_powerf_dev);
	if (pwr_btn_dev) {
		pm_wakeup_event(pwr_btn_dev, 0);
		put_device(pwr_btn_dev);
	}
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

	error = nvs_nosave ? 0 : suspend_nvs_alloc();
	if (error)
		return error;

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
	u32 acpi_state = acpi_target_sleep_state;
	int error;

	ACPI_FLUSH_CPU_CACHE();

	switch (acpi_state) {
	case ACPI_STATE_S1:
		barrier();
		status = acpi_enter_sleep_state(acpi_state, wake_sleep_flags);
		break;

	case ACPI_STATE_S3:
		error = acpi_suspend_lowlevel();
		if (error)
			return error;
		pr_info(PREFIX "Low-level resume complete\n");
		break;
	}

	/* This violates the spec but is required for bug compatibility. */
	acpi_write_bit_register(ACPI_BITREG_SCI_ENABLE, 1);

	/* Reprogram control registers and execute _BFS */
	acpi_leave_sleep_state_prep(acpi_state, wake_sleep_flags);

	/* ACPI 3.0 specs (P62) says that it's the responsibility
	 * of the OSPM to clear the status bit [ implying that the
	 * POWER_BUTTON event should not reach userspace ]
	 *
	 * However, we do generate a small hint for userspace in the form of
	 * a wakeup event. We flag this condition for now and generate the
	 * event later, as we're currently too early in resume to be able to
	 * generate wakeup events.
	 */
	if (ACPI_SUCCESS(status) && (acpi_state == ACPI_STATE_S3)) {
		acpi_event_status pwr_btn_status;

		acpi_get_event_status(ACPI_EVENT_POWER_BUTTON, &pwr_btn_status);

		if (pwr_btn_status & ACPI_EVENT_FLAG_SET) {
			acpi_clear_event(ACPI_EVENT_POWER_BUTTON);
			/* Flag for later */
			pwr_btn_event_pending = true;
		}
	}

	/*
	 * Disable and clear GPE status before interrupt is enabled. Some GPEs
	 * (like wakeup GPE) haven't handler, this can avoid such GPE misfire.
	 * acpi_leave_sleep_state will reenable specific GPEs later
	 */
	acpi_disable_all_gpes();
	/* Allow EC transactions to happen. */
	acpi_ec_unblock_transactions_early();

	suspend_nvs_restore();

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

static const struct platform_suspend_ops acpi_suspend_ops = {
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
static const struct platform_suspend_ops acpi_suspend_ops_old = {
	.valid = acpi_suspend_state_valid,
	.begin = acpi_suspend_begin_old,
	.prepare_late = acpi_pm_pre_suspend,
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

static int __init init_nvs_nosave(const struct dmi_system_id *d)
{
	acpi_nvs_nosave();
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
	.callback = init_old_suspend_ordering,
	.ident = "Asus Pundit P1-AH2 (M2N8L motherboard)",
	.matches = {
		DMI_MATCH(DMI_BOARD_VENDOR, "ASUSTek Computer INC."),
		DMI_MATCH(DMI_BOARD_NAME, "M2N8L"),
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
	{
	.callback = init_nvs_nosave,
	.ident = "Sony Vaio VGN-FW21E",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Sony Corporation"),
		DMI_MATCH(DMI_PRODUCT_NAME, "VGN-FW21E"),
		},
	},
	{
	.callback = init_nvs_nosave,
	.ident = "Sony Vaio VPCEB17FX",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Sony Corporation"),
		DMI_MATCH(DMI_PRODUCT_NAME, "VPCEB17FX"),
		},
	},
	{
	.callback = init_nvs_nosave,
	.ident = "Sony Vaio VGN-SR11M",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Sony Corporation"),
		DMI_MATCH(DMI_PRODUCT_NAME, "VGN-SR11M"),
		},
	},
	{
	.callback = init_nvs_nosave,
	.ident = "Everex StepNote Series",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Everex Systems, Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "Everex StepNote Series"),
		},
	},
	{
	.callback = init_nvs_nosave,
	.ident = "Sony Vaio VPCEB1Z1E",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Sony Corporation"),
		DMI_MATCH(DMI_PRODUCT_NAME, "VPCEB1Z1E"),
		},
	},
	{
	.callback = init_nvs_nosave,
	.ident = "Sony Vaio VGN-NW130D",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Sony Corporation"),
		DMI_MATCH(DMI_PRODUCT_NAME, "VGN-NW130D"),
		},
	},
	{
	.callback = init_nvs_nosave,
	.ident = "Sony Vaio VPCCW29FX",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Sony Corporation"),
		DMI_MATCH(DMI_PRODUCT_NAME, "VPCCW29FX"),
		},
	},
	{
	.callback = init_nvs_nosave,
	.ident = "Averatec AV1020-ED2",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "AVERATEC"),
		DMI_MATCH(DMI_PRODUCT_NAME, "1000 Series"),
		},
	},
	{
	.callback = init_old_suspend_ordering,
	.ident = "Asus A8N-SLI DELUXE",
	.matches = {
		DMI_MATCH(DMI_BOARD_VENDOR, "ASUSTeK Computer INC."),
		DMI_MATCH(DMI_BOARD_NAME, "A8N-SLI DELUXE"),
		},
	},
	{
	.callback = init_old_suspend_ordering,
	.ident = "Asus A8N-SLI Premium",
	.matches = {
		DMI_MATCH(DMI_BOARD_VENDOR, "ASUSTeK Computer INC."),
		DMI_MATCH(DMI_BOARD_NAME, "A8N-SLI Premium"),
		},
	},
	{
	.callback = init_nvs_nosave,
	.ident = "Sony Vaio VGN-SR26GN_P",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Sony Corporation"),
		DMI_MATCH(DMI_PRODUCT_NAME, "VGN-SR26GN_P"),
		},
	},
	{
	.callback = init_nvs_nosave,
	.ident = "Sony Vaio VGN-FW520F",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Sony Corporation"),
		DMI_MATCH(DMI_PRODUCT_NAME, "VGN-FW520F"),
		},
	},
	{
	.callback = init_nvs_nosave,
	.ident = "Asus K54C",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK Computer Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "K54C"),
		},
	},
	{
	.callback = init_nvs_nosave,
	.ident = "Asus K54HR",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK Computer Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "K54HR"),
		},
	},
	{},
};
#endif /* CONFIG_SUSPEND */

#ifdef CONFIG_HIBERNATION
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

	error = nvs_nosave ? 0 : suspend_nvs_alloc();
	if (!error) {
		acpi_target_sleep_state = ACPI_STATE_S4;
		acpi_sleep_tts_switch(acpi_target_sleep_state);
	}

	return error;
}

static int acpi_hibernation_enter(void)
{
	acpi_status status = AE_OK;

	ACPI_FLUSH_CPU_CACHE();

	/* This shouldn't return.  If it returns, we have a problem */
	status = acpi_enter_sleep_state(ACPI_STATE_S4, wake_sleep_flags);
	/* Reprogram control registers and execute _BFS */
	acpi_leave_sleep_state_prep(ACPI_STATE_S4, wake_sleep_flags);

	return ACPI_SUCCESS(status) ? 0 : -EFAULT;
}

static void acpi_hibernation_leave(void)
{
	/*
	 * If ACPI is not enabled by the BIOS and the boot kernel, we need to
	 * enable it here.
	 */
	acpi_enable();
	/* Reprogram control registers and execute _BFS */
	acpi_leave_sleep_state_prep(ACPI_STATE_S4, wake_sleep_flags);
	/* Check the hardware signature */
	if (facs && s4_hardware_signature != facs->hardware_signature) {
		printk(KERN_EMERG "ACPI: Hardware changed while hibernated, "
			"cannot resume!\n");
		panic("ACPI S4 hardware signature mismatch");
	}
	/* Restore the NVS memory area */
	suspend_nvs_restore();
	/* Allow EC transactions to happen. */
	acpi_ec_unblock_transactions_early();
}

static void acpi_pm_thaw(void)
{
	acpi_ec_unblock_transactions();
	acpi_enable_all_runtime_gpes();
}

static const struct platform_hibernation_ops acpi_hibernation_ops = {
	.begin = acpi_hibernation_begin,
	.end = acpi_pm_end,
	.pre_snapshot = acpi_pm_prepare,
	.finish = acpi_pm_finish,
	.prepare = acpi_pm_prepare,
	.enter = acpi_hibernation_enter,
	.leave = acpi_hibernation_leave,
	.pre_restore = acpi_pm_freeze,
	.restore_cleanup = acpi_pm_thaw,
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
		if (!nvs_nosave)
			error = suspend_nvs_alloc();
		if (!error)
			acpi_target_sleep_state = ACPI_STATE_S4;
	}
	return error;
}

/*
 * The following callbacks are used if the pre-ACPI 2.0 suspend ordering has
 * been requested.
 */
static const struct platform_hibernation_ops acpi_hibernation_ops_old = {
	.begin = acpi_hibernation_begin_old,
	.end = acpi_pm_end,
	.pre_snapshot = acpi_pm_pre_suspend,
	.prepare = acpi_pm_freeze,
	.finish = acpi_pm_finish,
	.enter = acpi_hibernation_enter,
	.leave = acpi_hibernation_leave,
	.pre_restore = acpi_pm_freeze,
	.restore_cleanup = acpi_pm_thaw,
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

#ifdef CONFIG_PM
/**
 *	acpi_pm_device_sleep_state - return preferred power state of ACPI device
 *		in the system sleep state given by %acpi_target_sleep_state
 *	@dev: device to examine; its driver model wakeup flags control
 *		whether it should be able to wake up the system
 *	@d_min_p: used to store the upper limit of allowed states range
 *	@d_max_in: specify the lowest allowed states
 *	Return value: preferred power state of the device on success, -ENODEV
 *	(ie. if there's no 'struct acpi_device' for @dev) or -EINVAL on failure
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

int acpi_pm_device_sleep_state(struct device *dev, int *d_min_p, int d_max_in)
{
	acpi_handle handle = DEVICE_ACPI_HANDLE(dev);
	struct acpi_device *adev;
	char acpi_method[] = "_SxD";
	unsigned long long d_min, d_max;

	if (d_max_in < ACPI_STATE_D0 || d_max_in > ACPI_STATE_D3)
		return -EINVAL;
	if (!handle || ACPI_FAILURE(acpi_bus_get_device(handle, &adev))) {
		printk(KERN_DEBUG "ACPI handle has no context!\n");
		return -ENODEV;
	}

	acpi_method[2] = '0' + acpi_target_sleep_state;
	/*
	 * If the sleep state is S0, the lowest limit from ACPI is D3,
	 * but if the device has _S0W, we will use the value from _S0W
	 * as the lowest limit from ACPI.  Finally, we will constrain
	 * the lowest limit with the specified one.
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
	    (device_may_wakeup(dev) && adev->wakeup.flags.valid &&
	     adev->wakeup.sleep_state >= acpi_target_sleep_state)) {
		acpi_status status;

		acpi_method[3] = 'W';
		status = acpi_evaluate_integer(handle, acpi_method, NULL,
						&d_max);
		if (ACPI_FAILURE(status)) {
			if (acpi_target_sleep_state != ACPI_STATE_S0 ||
			    status != AE_NOT_FOUND)
				d_max = d_min;
		} else if (d_max < d_min) {
			/* Warn the user of the broken DSDT */
			printk(KERN_WARNING "ACPI: Wrong value from %s\n",
				acpi_method);
			/* Sanitize it */
			d_min = d_max;
		}
	}

	if (d_max_in < d_min)
		return -EINVAL;
	if (d_min_p)
		*d_min_p = d_min;
	/* constrain d_max with specified lowest limit (max number) */
	if (d_max > d_max_in) {
		for (d_max = d_max_in; d_max > d_min; d_max--) {
			if (adev->power.states[d_max].flags.valid)
				break;
		}
	}
	return d_max;
}
EXPORT_SYMBOL(acpi_pm_device_sleep_state);
#endif /* CONFIG_PM */

#ifdef CONFIG_PM_SLEEP
/**
 * acpi_pm_device_run_wake - Enable/disable wake-up for given device.
 * @phys_dev: Device to enable/disable the platform to wake-up the system for.
 * @enable: Whether enable or disable the wake-up functionality.
 *
 * Find the ACPI device object corresponding to @pci_dev and try to
 * enable/disable the GPE associated with it.
 */
int acpi_pm_device_run_wake(struct device *phys_dev, bool enable)
{
	struct acpi_device *dev;
	acpi_handle handle;

	if (!device_run_wake(phys_dev))
		return -EINVAL;

	handle = DEVICE_ACPI_HANDLE(phys_dev);
	if (!handle || ACPI_FAILURE(acpi_bus_get_device(handle, &dev))) {
		dev_dbg(phys_dev, "ACPI handle has no context in %s!\n",
			__func__);
		return -ENODEV;
	}

	if (enable) {
		acpi_enable_wakeup_device_power(dev, ACPI_STATE_S0);
		acpi_enable_gpe(dev->wakeup.gpe_device, dev->wakeup.gpe_number);
	} else {
		acpi_disable_gpe(dev->wakeup.gpe_device, dev->wakeup.gpe_number);
		acpi_disable_wakeup_device_power(dev);
	}

	return 0;
}
EXPORT_SYMBOL(acpi_pm_device_run_wake);

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

	error = enable ?
		acpi_enable_wakeup_device_power(adev, acpi_target_sleep_state) :
		acpi_disable_wakeup_device_power(adev);
	if (!error)
		dev_info(dev, "wake-up capability %s by ACPI\n",
				enable ? "enabled" : "disabled");

	return error;
}
#endif  /* CONFIG_PM_SLEEP */

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
	acpi_enter_sleep_state(ACPI_STATE_S5, wake_sleep_flags);
}

/*
 * ACPI 2.0 created the optional _GTS and _BFS,
 * but industry adoption has been neither rapid nor broad.
 *
 * Linux gets into trouble when it executes poorly validated
 * paths through the BIOS, so disable _GTS and _BFS by default,
 * but do speak up and offer the option to enable them.
 */
static void __init acpi_gts_bfs_check(void)
{
	acpi_handle dummy;

	if (ACPI_SUCCESS(acpi_get_handle(ACPI_ROOT_OBJECT, METHOD_PATHNAME__GTS, &dummy)))
	{
		printk(KERN_NOTICE PREFIX "BIOS offers _GTS\n");
		printk(KERN_NOTICE PREFIX "If \"acpi.gts=1\" improves suspend, "
			"please notify linux-acpi@vger.kernel.org\n");
	}
	if (ACPI_SUCCESS(acpi_get_handle(ACPI_ROOT_OBJECT, METHOD_PATHNAME__BFS, &dummy)))
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
			printk(KERN_CONT " S%d", i);
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
		printk(KERN_CONT " S4");
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
		printk(KERN_CONT " S5");
		pm_power_off_prepare = acpi_power_off_prepare;
		pm_power_off = acpi_power_off;
	}
	printk(KERN_CONT ")\n");
	/*
	 * Register the tts_notifier to reboot notifier list so that the _TTS
	 * object can also be evaluated when the system enters S5.
	 */
	register_reboot_notifier(&tts_notifier);
	acpi_gts_bfs_check();
	return 0;
}
