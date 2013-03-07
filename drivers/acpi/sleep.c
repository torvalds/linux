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

#include <asm/io.h>

#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>

#include "internal.h"
#include "sleep.h"

static u8 sleep_states[ACPI_S_STATE_COUNT];

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

u32 acpi_target_system_state(void)
{
	return acpi_target_sleep_state;
}

static bool pwr_btn_event_pending;

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
 * The ACPI specification wants us to save NVS memory regions during hibernation
 * but says nothing about saving NVS during S3.  Not all versions of Windows
 * save NVS on S3 suspend either, and it is clear that not all systems need
 * NVS to be saved at S3 time.  To improve suspend/resume time, allow the
 * user to disable saving NVS on S3 if their system does not require it, but
 * continue to save/restore NVS for S4 as specified.
 */
static bool nvs_nosave_s3;

void __init acpi_nvs_nosave_s3(void)
{
	nvs_nosave_s3 = true;
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

static int __init init_old_suspend_ordering(const struct dmi_system_id *d)
{
	acpi_old_suspend_ordering();
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
	.ident = "Sony Vaio VGN-FW41E_H",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Sony Corporation"),
		DMI_MATCH(DMI_PRODUCT_NAME, "VGN-FW41E_H"),
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
	.ident = "Sony Vaio VPCEB1S1E",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Sony Corporation"),
		DMI_MATCH(DMI_PRODUCT_NAME, "VPCEB1S1E"),
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

static void acpi_sleep_dmi_check(void)
{
	dmi_check_system(acpisleep_dmi_table);
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

	acpi_resume_power_resources();

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
static inline void acpi_sleep_dmi_check(void) {}
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

	error = (nvs_nosave || nvs_nosave_s3) ? 0 : suspend_nvs_alloc();
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
		status = acpi_enter_sleep_state(acpi_state);
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

	/* Reprogram control registers */
	acpi_leave_sleep_state_prep(acpi_state);

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

static void acpi_sleep_suspend_setup(void)
{
	int i;

	for (i = ACPI_STATE_S1; i < ACPI_STATE_S4; i++) {
		acpi_status status;
		u8 type_a, type_b;

		status = acpi_get_sleep_type_data(i, &type_a, &type_b);
		if (ACPI_SUCCESS(status)) {
			sleep_states[i] = 1;
		}
	}

	suspend_set_ops(old_suspend_ordering ?
		&acpi_suspend_ops_old : &acpi_suspend_ops);
}
#else /* !CONFIG_SUSPEND */
static inline void acpi_sleep_suspend_setup(void) {}
#endif /* !CONFIG_SUSPEND */

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
	status = acpi_enter_sleep_state(ACPI_STATE_S4);
	/* Reprogram control registers */
	acpi_leave_sleep_state_prep(ACPI_STATE_S4);

	return ACPI_SUCCESS(status) ? 0 : -EFAULT;
}

static void acpi_hibernation_leave(void)
{
	/*
	 * If ACPI is not enabled by the BIOS and the boot kernel, we need to
	 * enable it here.
	 */
	acpi_enable();
	/* Reprogram control registers */
	acpi_leave_sleep_state_prep(ACPI_STATE_S4);
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

static void acpi_sleep_hibernate_setup(void)
{
	acpi_status status;
	u8 type_a, type_b;

	status = acpi_get_sleep_type_data(ACPI_STATE_S4, &type_a, &type_b);
	if (ACPI_FAILURE(status))
		return;

	hibernation_set_ops(old_suspend_ordering ?
			&acpi_hibernation_ops_old : &acpi_hibernation_ops);
	sleep_states[ACPI_STATE_S4] = 1;
	if (nosigcheck)
		return;

	acpi_get_table(ACPI_SIG_FACS, 1, (struct acpi_table_header **)&facs);
	if (facs)
		s4_hardware_signature = facs->hardware_signature;
}
#else /* !CONFIG_HIBERNATION */
static inline void acpi_sleep_hibernate_setup(void) {}
#endif /* !CONFIG_HIBERNATION */

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
	acpi_enter_sleep_state(ACPI_STATE_S5);
}

int __init acpi_sleep_init(void)
{
	acpi_status status;
	u8 type_a, type_b;
	char supported[ACPI_S_STATE_COUNT * 3 + 1];
	char *pos = supported;
	int i;

	if (acpi_disabled)
		return 0;

	acpi_sleep_dmi_check();

	sleep_states[ACPI_STATE_S0] = 1;

	acpi_sleep_suspend_setup();
	acpi_sleep_hibernate_setup();

	status = acpi_get_sleep_type_data(ACPI_STATE_S5, &type_a, &type_b);
	if (ACPI_SUCCESS(status)) {
		sleep_states[ACPI_STATE_S5] = 1;
		pm_power_off_prepare = acpi_power_off_prepare;
		pm_power_off = acpi_power_off;
	}

	supported[0] = 0;
	for (i = 0; i < ACPI_S_STATE_COUNT; i++) {
		if (sleep_states[i])
			pos += sprintf(pos, " S%d", i);
	}
	pr_info(PREFIX "(supports%s)\n", supported);

	/*
	 * Register the tts_notifier to reboot notifier list so that the _TTS
	 * object can also be evaluated when the system enters S5.
	 */
	register_reboot_notifier(&tts_notifier);
	return 0;
}
