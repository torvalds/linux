// SPDX-License-Identifier: GPL-2.0-only
/*
 * sleep.c - ACPI sleep support.
 *
 * Copyright (c) 2005 Alexey Starikovskiy <alexey.y.starikovskiy@intel.com>
 * Copyright (c) 2004 David Shaohua Li <shaohua.li@intel.com>
 * Copyright (c) 2000-2003 Patrick Mochel
 * Copyright (c) 2003 Open Source Development Lab
 */

#define pr_fmt(fmt) "ACPI: PM: " fmt

#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/dmi.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/suspend.h>
#include <linux/reboot.h>
#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/syscore_ops.h>
#include <asm/io.h>
#include <trace/events/power.h>

#include "internal.h"
#include "sleep.h"

/*
 * Some HW-full platforms do not have _S5, so they may need
 * to leverage efi power off for a shutdown.
 */
bool acpi_no_s5;
static u8 sleep_states[ACPI_S_STATE_COUNT];

static void acpi_sleep_tts_switch(u32 acpi_state)
{
	acpi_status status;

	status = acpi_execute_simple_method(NULL, "\\_TTS", acpi_state);
	if (ACPI_FAILURE(status) && status != AE_NOT_FOUND) {
		/*
		 * OS can't evaluate the _TTS object correctly. Some warning
		 * message will be printed. But it won't break anything.
		 */
		pr_notice("Failure in evaluating _TTS object\n");
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

#ifndef acpi_skip_set_wakeup_address
#define acpi_skip_set_wakeup_address() false
#endif

static int acpi_sleep_prepare(u32 acpi_state)
{
#ifdef CONFIG_ACPI_SLEEP
	unsigned long acpi_wakeup_address;

	/* do we have a wakeup address for S2 and S3? */
	if (acpi_state == ACPI_STATE_S3 && !acpi_skip_set_wakeup_address()) {
		acpi_wakeup_address = acpi_get_wakeup_address();
		if (!acpi_wakeup_address)
			return -EFAULT;
		acpi_set_waking_vector(acpi_wakeup_address);

	}
#endif
	pr_info("Preparing to enter system sleep state S%d\n", acpi_state);
	acpi_enable_wakeup_devices(acpi_state);
	acpi_enter_sleep_state_prep(acpi_state);
	return 0;
}

bool acpi_sleep_state_supported(u8 sleep_state)
{
	acpi_status status;
	u8 type_a, type_b;

	status = acpi_get_sleep_type_data(sleep_state, &type_a, &type_b);
	return ACPI_SUCCESS(status) && (!acpi_gbl_reduced_hardware
		|| (acpi_gbl_FADT.sleep_control.address
			&& acpi_gbl_FADT.sleep_status.address));
}

#ifdef CONFIG_ACPI_SLEEP
static u32 acpi_target_sleep_state = ACPI_STATE_S0;

u32 acpi_target_system_state(void)
{
	return acpi_target_sleep_state;
}
EXPORT_SYMBOL_GPL(acpi_target_system_state);

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

static int __init init_nvs_save_s3(const struct dmi_system_id *d)
{
	nvs_nosave_s3 = false;
	return 0;
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

bool acpi_sleep_default_s3;

static int __init init_default_s3(const struct dmi_system_id *d)
{
	acpi_sleep_default_s3 = true;
	return 0;
}

static const struct dmi_system_id acpisleep_dmi_table[] __initconst = {
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
	.ident = "Sony Vaio VGN-FW21M",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Sony Corporation"),
		DMI_MATCH(DMI_PRODUCT_NAME, "VGN-FW21M"),
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
	{
	.callback = init_nvs_save_s3,
	.ident = "Asus 1025C",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
		DMI_MATCH(DMI_PRODUCT_NAME, "1025C"),
		},
	},
	/*
	 * https://bugzilla.kernel.org/show_bug.cgi?id=189431
	 * Lenovo G50-45 is a platform later than 2012, but needs nvs memory
	 * saving during S3.
	 */
	{
	.callback = init_nvs_save_s3,
	.ident = "Lenovo G50-45",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		DMI_MATCH(DMI_PRODUCT_NAME, "80E3"),
		},
	},
	{
	.callback = init_nvs_save_s3,
	.ident = "Lenovo G40-45",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		DMI_MATCH(DMI_PRODUCT_NAME, "80E1"),
		},
	},
	/*
	 * ThinkPad X1 Tablet(2016) cannot do suspend-to-idle using
	 * the Low Power S0 Idle firmware interface (see
	 * https://bugzilla.kernel.org/show_bug.cgi?id=199057).
	 */
	{
	.callback = init_default_s3,
	.ident = "ThinkPad X1 Tablet(2016)",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		DMI_MATCH(DMI_PRODUCT_NAME, "20GGA00L00"),
		},
	},
	{},
};

static bool ignore_blacklist;

void __init acpi_sleep_no_blacklist(void)
{
	ignore_blacklist = true;
}

static void __init acpi_sleep_dmi_check(void)
{
	if (ignore_blacklist)
		return;

	if (dmi_get_bios_year() >= 2012)
		acpi_nvs_nosave_s3();

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
 * acpi_pm_pre_suspend - Enable wakeup devices, "freeze" EC and save NVS.
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

/**
 *	acpi_pm_finish - Instruct the platform to leave a sleep state.
 *
 *	This is called after we wake back up (or if entering the sleep state
 *	failed).
 */
static void acpi_pm_finish(void)
{
	struct acpi_device *pwr_btn_adev;
	u32 acpi_state = acpi_target_sleep_state;

	acpi_ec_unblock_transactions();
	suspend_nvs_free();

	if (acpi_state == ACPI_STATE_S0)
		return;

	pr_info("Waking up from system sleep state S%d\n", acpi_state);
	acpi_disable_wakeup_devices(acpi_state);
	acpi_leave_sleep_state(acpi_state);

	/* reset firmware waking vector */
	acpi_set_waking_vector(0);

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
	pwr_btn_adev = acpi_dev_get_first_match_dev(ACPI_BUTTON_HID_POWERF,
						    NULL, -1);
	if (pwr_btn_adev) {
		pm_wakeup_event(&pwr_btn_adev->dev, 0);
		acpi_dev_put(pwr_btn_adev);
	}
}

/**
 * acpi_pm_start - Start system PM transition.
 */
static void acpi_pm_start(u32 acpi_state)
{
	acpi_target_sleep_state = acpi_state;
	acpi_sleep_tts_switch(acpi_target_sleep_state);
	acpi_scan_lock_acquire();
}

/**
 * acpi_pm_end - Finish up system PM transition.
 */
static void acpi_pm_end(void)
{
	acpi_turn_off_unused_power_resources();
	acpi_scan_lock_release();
	/*
	 * This is necessary in case acpi_pm_finish() is not called during a
	 * failing transition to a sleep state.
	 */
	acpi_target_sleep_state = ACPI_STATE_S0;
	acpi_sleep_tts_switch(acpi_target_sleep_state);
}
#else /* !CONFIG_ACPI_SLEEP */
#define sleep_no_lps0	(1)
#define acpi_target_sleep_state	ACPI_STATE_S0
#define acpi_sleep_default_s3	(1)
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
	int error;

	error = (nvs_nosave || nvs_nosave_s3) ? 0 : suspend_nvs_alloc();
	if (error)
		return error;

	if (!sleep_states[acpi_state]) {
		pr_err("ACPI does not support sleep state S%u\n", acpi_state);
		return -ENOSYS;
	}
	if (acpi_state > ACPI_STATE_S1)
		pm_set_suspend_via_firmware();

	acpi_pm_start(acpi_state);
	return 0;
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

	trace_suspend_resume(TPS("acpi_suspend"), acpi_state, true);
	switch (acpi_state) {
	case ACPI_STATE_S1:
		barrier();
		status = acpi_enter_sleep_state(acpi_state);
		break;

	case ACPI_STATE_S3:
		if (!acpi_suspend_lowlevel)
			return -ENOSYS;
		error = acpi_suspend_lowlevel();
		if (error)
			return error;
		pr_info("Low-level resume complete\n");
		pm_set_resume_via_firmware();
		break;
	}
	trace_suspend_resume(TPS("acpi_suspend"), acpi_state, false);

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
		acpi_event_status pwr_btn_status = ACPI_EVENT_FLAG_DISABLED;

		acpi_get_event_status(ACPI_EVENT_POWER_BUTTON, &pwr_btn_status);

		if (pwr_btn_status & ACPI_EVENT_FLAG_STATUS_SET) {
			acpi_clear_event(ACPI_EVENT_POWER_BUTTON);
			/* Flag for later */
			pwr_btn_event_pending = true;
		}
	}

	/*
	 * Disable all GPE and clear their status bits before interrupts are
	 * enabled. Some GPEs (like wakeup GPEs) have no handlers and this can
	 * prevent them from producing spurious interrups.
	 *
	 * acpi_leave_sleep_state() will reenable specific GPEs later.
	 *
	 * Because this code runs on one CPU with disabled interrupts (all of
	 * the other CPUs are offline at this time), it need not acquire any
	 * sleeping locks which may trigger an implicit preemption point even
	 * if there is no contention, so avoid doing that by using a low-level
	 * library routine here.
	 */
	acpi_hw_disable_all_gpes();
	/* Allow EC transactions to happen. */
	acpi_ec_unblock_transactions();

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

static bool s2idle_wakeup;

int acpi_s2idle_begin(void)
{
	acpi_scan_lock_acquire();
	return 0;
}

int acpi_s2idle_prepare(void)
{
	if (acpi_sci_irq_valid()) {
		int error;

		error = enable_irq_wake(acpi_sci_irq);
		if (error)
			pr_warn("Warning: Failed to enable wakeup from IRQ %d: %d\n",
				acpi_sci_irq, error);

		acpi_ec_set_gpe_wake_mask(ACPI_GPE_ENABLE);
	}

	acpi_enable_wakeup_devices(ACPI_STATE_S0);

	/* Change the configuration of GPEs to avoid spurious wakeup. */
	acpi_enable_all_wakeup_gpes();
	acpi_os_wait_events_complete();

	s2idle_wakeup = true;
	return 0;
}

bool acpi_s2idle_wake(void)
{
	if (!acpi_sci_irq_valid())
		return pm_wakeup_pending();

	while (pm_wakeup_pending()) {
		/*
		 * If IRQD_WAKEUP_ARMED is set for the SCI at this point, the
		 * SCI has not triggered while suspended, so bail out (the
		 * wakeup is pending anyway and the SCI is not the source of
		 * it).
		 */
		if (irqd_is_wakeup_armed(irq_get_irq_data(acpi_sci_irq))) {
			pm_pr_dbg("Wakeup unrelated to ACPI SCI\n");
			return true;
		}

		/*
		 * If the status bit of any enabled fixed event is set, the
		 * wakeup is regarded as valid.
		 */
		if (acpi_any_fixed_event_status_set()) {
			pm_pr_dbg("ACPI fixed event wakeup\n");
			return true;
		}

		/* Check wakeups from drivers sharing the SCI. */
		if (acpi_check_wakeup_handlers()) {
			pm_pr_dbg("ACPI custom handler wakeup\n");
			return true;
		}

		/*
		 * Check non-EC GPE wakeups and if there are none, cancel the
		 * SCI-related wakeup and dispatch the EC GPE.
		 */
		if (acpi_ec_dispatch_gpe()) {
			pm_pr_dbg("ACPI non-EC GPE wakeup\n");
			return true;
		}

		acpi_os_wait_events_complete();

		/*
		 * The SCI is in the "suspended" state now and it cannot produce
		 * new wakeup events till the rearming below, so if any of them
		 * are pending here, they must be resulting from the processing
		 * of EC events above or coming from somewhere else.
		 */
		if (pm_wakeup_pending()) {
			pm_pr_dbg("Wakeup after ACPI Notify sync\n");
			return true;
		}

		pm_pr_dbg("Rearming ACPI SCI for wakeup\n");

		pm_wakeup_clear(acpi_sci_irq);
		rearm_wake_irq(acpi_sci_irq);
	}

	return false;
}

void acpi_s2idle_restore(void)
{
	/*
	 * Drain pending events before restoring the working-state configuration
	 * of GPEs.
	 */
	acpi_os_wait_events_complete(); /* synchronize GPE processing */
	acpi_ec_flush_work(); /* flush the EC driver's workqueues */
	acpi_os_wait_events_complete(); /* synchronize Notify handling */

	s2idle_wakeup = false;

	acpi_enable_all_runtime_gpes();

	acpi_disable_wakeup_devices(ACPI_STATE_S0);

	if (acpi_sci_irq_valid()) {
		acpi_ec_set_gpe_wake_mask(ACPI_GPE_DISABLE);
		disable_irq_wake(acpi_sci_irq);
	}
}

void acpi_s2idle_end(void)
{
	acpi_scan_lock_release();
}

static const struct platform_s2idle_ops acpi_s2idle_ops = {
	.begin = acpi_s2idle_begin,
	.prepare = acpi_s2idle_prepare,
	.wake = acpi_s2idle_wake,
	.restore = acpi_s2idle_restore,
	.end = acpi_s2idle_end,
};

void __weak acpi_s2idle_setup(void)
{
	if (acpi_gbl_FADT.flags & ACPI_FADT_LOW_POWER_S0)
		pr_info("Efficient low-power S0 idle declared\n");

	s2idle_set_ops(&acpi_s2idle_ops);
}

static void __init acpi_sleep_suspend_setup(void)
{
	bool suspend_ops_needed = false;
	int i;

	for (i = ACPI_STATE_S1; i < ACPI_STATE_S4; i++)
		if (acpi_sleep_state_supported(i)) {
			sleep_states[i] = 1;
			suspend_ops_needed = true;
		}

	if (suspend_ops_needed)
		suspend_set_ops(old_suspend_ordering ?
				&acpi_suspend_ops_old : &acpi_suspend_ops);

	acpi_s2idle_setup();
}

#else /* !CONFIG_SUSPEND */
#define s2idle_wakeup		(false)
static inline void acpi_sleep_suspend_setup(void) {}
#endif /* !CONFIG_SUSPEND */

bool acpi_s2idle_wakeup(void)
{
	return s2idle_wakeup;
}

#ifdef CONFIG_PM_SLEEP
static u32 saved_bm_rld;

static int  acpi_save_bm_rld(void)
{
	acpi_read_bit_register(ACPI_BITREG_BUS_MASTER_RLD, &saved_bm_rld);
	return 0;
}

static void  acpi_restore_bm_rld(void)
{
	u32 resumed_bm_rld = 0;

	acpi_read_bit_register(ACPI_BITREG_BUS_MASTER_RLD, &resumed_bm_rld);
	if (resumed_bm_rld == saved_bm_rld)
		return;

	acpi_write_bit_register(ACPI_BITREG_BUS_MASTER_RLD, saved_bm_rld);
}

static struct syscore_ops acpi_sleep_syscore_ops = {
	.suspend = acpi_save_bm_rld,
	.resume = acpi_restore_bm_rld,
};

static void acpi_sleep_syscore_init(void)
{
	register_syscore_ops(&acpi_sleep_syscore_ops);
}
#else
static inline void acpi_sleep_syscore_init(void) {}
#endif /* CONFIG_PM_SLEEP */

#ifdef CONFIG_HIBERNATION
static unsigned long s4_hardware_signature;
static struct acpi_table_facs *facs;
int acpi_check_s4_hw_signature = -1; /* Default behaviour is just to warn */

static int acpi_hibernation_begin(pm_message_t stage)
{
	if (!nvs_nosave) {
		int error = suspend_nvs_alloc();
		if (error)
			return error;
	}

	if (stage.event == PM_EVENT_HIBERNATE)
		pm_set_suspend_via_firmware();

	acpi_pm_start(ACPI_STATE_S4);
	return 0;
}

static int acpi_hibernation_enter(void)
{
	acpi_status status = AE_OK;

	/* This shouldn't return.  If it returns, we have a problem */
	status = acpi_enter_sleep_state(ACPI_STATE_S4);
	/* Reprogram control registers */
	acpi_leave_sleep_state_prep(ACPI_STATE_S4);

	return ACPI_SUCCESS(status) ? 0 : -EFAULT;
}

static void acpi_hibernation_leave(void)
{
	pm_set_resume_via_firmware();
	/*
	 * If ACPI is not enabled by the BIOS and the boot kernel, we need to
	 * enable it here.
	 */
	acpi_enable();
	/* Reprogram control registers */
	acpi_leave_sleep_state_prep(ACPI_STATE_S4);
	/* Check the hardware signature */
	if (facs && s4_hardware_signature != facs->hardware_signature)
		pr_crit("Hardware changed while hibernated, success doubtful!\n");
	/* Restore the NVS memory area */
	suspend_nvs_restore();
	/* Allow EC transactions to happen. */
	acpi_ec_unblock_transactions();
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
static int acpi_hibernation_begin_old(pm_message_t stage)
{
	int error;
	/*
	 * The _TTS object should always be evaluated before the _PTS object.
	 * When the old_suspended_ordering is true, the _PTS object is
	 * evaluated in the acpi_sleep_prepare.
	 */
	acpi_sleep_tts_switch(ACPI_STATE_S4);

	error = acpi_sleep_prepare(ACPI_STATE_S4);
	if (error)
		return error;

	if (!nvs_nosave) {
		error = suspend_nvs_alloc();
		if (error)
			return error;
	}

	if (stage.event == PM_EVENT_HIBERNATE)
		pm_set_suspend_via_firmware();

	acpi_target_sleep_state = ACPI_STATE_S4;
	acpi_scan_lock_acquire();
	return 0;
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
	if (!acpi_sleep_state_supported(ACPI_STATE_S4))
		return;

	hibernation_set_ops(old_suspend_ordering ?
			&acpi_hibernation_ops_old : &acpi_hibernation_ops);
	sleep_states[ACPI_STATE_S4] = 1;
	if (!acpi_check_s4_hw_signature)
		return;

	acpi_get_table(ACPI_SIG_FACS, 1, (struct acpi_table_header **)&facs);
	if (facs) {
		/*
		 * s4_hardware_signature is the local variable which is just
		 * used to warn about mismatch after we're attempting to
		 * resume (in violation of the ACPI specification.)
		 */
		s4_hardware_signature = facs->hardware_signature;

		if (acpi_check_s4_hw_signature > 0) {
			/*
			 * If we're actually obeying the ACPI specification
			 * then the signature is written out as part of the
			 * swsusp header, in order to allow the boot kernel
			 * to gracefully decline to resume.
			 */
			swsusp_hardware_signature = facs->hardware_signature;
		}
	}
}
#else /* !CONFIG_HIBERNATION */
static inline void acpi_sleep_hibernate_setup(void) {}
#endif /* !CONFIG_HIBERNATION */

static int acpi_power_off_prepare(struct sys_off_data *data)
{
	/* Prepare to power off the system */
	acpi_sleep_prepare(ACPI_STATE_S5);
	acpi_disable_all_gpes();
	acpi_os_wait_events_complete();
	return NOTIFY_DONE;
}

static int acpi_power_off(struct sys_off_data *data)
{
	/* acpi_sleep_prepare(ACPI_STATE_S5) should have already been called */
	pr_debug("%s called\n", __func__);
	local_irq_disable();
	acpi_enter_sleep_state(ACPI_STATE_S5);
	return NOTIFY_DONE;
}

int __init acpi_sleep_init(void)
{
	char supported[ACPI_S_STATE_COUNT * 3 + 1];
	char *pos = supported;
	int i;

	acpi_sleep_dmi_check();

	sleep_states[ACPI_STATE_S0] = 1;

	acpi_sleep_syscore_init();
	acpi_sleep_suspend_setup();
	acpi_sleep_hibernate_setup();

	if (acpi_sleep_state_supported(ACPI_STATE_S5)) {
		sleep_states[ACPI_STATE_S5] = 1;

		register_sys_off_handler(SYS_OFF_MODE_POWER_OFF_PREPARE,
					 SYS_OFF_PRIO_FIRMWARE,
					 acpi_power_off_prepare, NULL);

		register_sys_off_handler(SYS_OFF_MODE_POWER_OFF,
					 SYS_OFF_PRIO_FIRMWARE,
					 acpi_power_off, NULL);

		/*
		 * Windows uses S5 for reboot, so some BIOSes depend on it to
		 * perform proper reboot.
		 */
		register_sys_off_handler(SYS_OFF_MODE_RESTART_PREPARE,
					 SYS_OFF_PRIO_FIRMWARE,
					 acpi_power_off_prepare, NULL);
	} else {
		acpi_no_s5 = true;
	}

	supported[0] = 0;
	for (i = 0; i < ACPI_S_STATE_COUNT; i++) {
		if (sleep_states[i])
			pos += sprintf(pos, " S%d", i);
	}
	pr_info("(supports%s)\n", supported);

	/*
	 * Register the tts_notifier to reboot notifier list so that the _TTS
	 * object can also be evaluated when the system enters S5.
	 */
	register_reboot_notifier(&tts_notifier);
	return 0;
}
