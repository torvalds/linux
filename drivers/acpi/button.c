// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  button.c - ACPI Button Driver
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 */

#define pr_fmt(fmt) "ACPI: button: " fmt

#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/platform_device.h>
#include <acpi/button.h>

#define ACPI_BUTTON_CLASS		"button"
#define ACPI_BUTTON_FILE_STATE		"state"
#define ACPI_BUTTON_TYPE_UNKNOWN	0x00
#define ACPI_BUTTON_NOTIFY_WAKE		0x02
#define ACPI_BUTTON_NOTIFY_STATUS	0x80

#define ACPI_BUTTON_CLASS_POWER		"button/power"
#define ACPI_BUTTON_DEVICE_NAME_POWER	"Power Button"
#define ACPI_BUTTON_TYPE_POWER		0x01

#define ACPI_BUTTON_CLASS_SLEEP		"button/sleep"
#define ACPI_BUTTON_DEVICE_NAME_SLEEP	"Sleep Button"
#define ACPI_BUTTON_TYPE_SLEEP		0x03

#define ACPI_BUTTON_CLASS_LID		"button/lid"
#define ACPI_BUTTON_SUBCLASS_LID	"lid"
#define ACPI_BUTTON_DEVICE_NAME_LID	"Lid Switch"
#define ACPI_BUTTON_TYPE_LID		0x05

enum {
	ACPI_BUTTON_LID_INIT_IGNORE,
	ACPI_BUTTON_LID_INIT_OPEN,
	ACPI_BUTTON_LID_INIT_METHOD,
	ACPI_BUTTON_LID_INIT_DISABLED,
};

static const char * const lid_init_state_str[] = {
	[ACPI_BUTTON_LID_INIT_IGNORE]		= "ignore",
	[ACPI_BUTTON_LID_INIT_OPEN]		= "open",
	[ACPI_BUTTON_LID_INIT_METHOD]		= "method",
	[ACPI_BUTTON_LID_INIT_DISABLED]		= "disabled",
};

MODULE_AUTHOR("Paul Diefenbaugh");
MODULE_DESCRIPTION("ACPI Button Driver");
MODULE_LICENSE("GPL");

static const struct acpi_device_id button_device_ids[] = {
	{ACPI_BUTTON_HID_LID, ACPI_BUTTON_TYPE_LID},
	{ACPI_BUTTON_HID_SLEEP, ACPI_BUTTON_TYPE_SLEEP},
	{ACPI_BUTTON_HID_SLEEPF, ACPI_BUTTON_TYPE_SLEEP},
	{ACPI_BUTTON_HID_POWER, ACPI_BUTTON_TYPE_POWER},
	{ACPI_BUTTON_HID_POWERF, ACPI_BUTTON_TYPE_POWER},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, button_device_ids);

/* Please keep this list sorted alphabetically by vendor and model */
static const struct dmi_system_id dmi_lid_quirks[] = {
	{
		/* GP-electronic T701, _LID method points to a floating GPIO */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Insyde"),
			DMI_MATCH(DMI_PRODUCT_NAME, "T701"),
			DMI_MATCH(DMI_BIOS_VERSION, "BYT70A.YNCHENG.WIN.007"),
		},
		.driver_data = (void *)(long)ACPI_BUTTON_LID_INIT_DISABLED,
	},
	{
		/* Nextbook Ares 8A tablet, _LID device always reports lid closed */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Insyde"),
			DMI_MATCH(DMI_PRODUCT_NAME, "CherryTrail"),
			DMI_MATCH(DMI_BIOS_VERSION, "M882"),
		},
		.driver_data = (void *)(long)ACPI_BUTTON_LID_INIT_DISABLED,
	},
	{
		/*
		 * Lenovo Yoga 9 14ITL5, initial notification of the LID device
		 * never happens.
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "82BG"),
		},
		.driver_data = (void *)(long)ACPI_BUTTON_LID_INIT_OPEN,
	},
	{
		/*
		 * Medion Akoya E2215T, notification of the LID device only
		 * happens on close, not on open and _LID always returns closed.
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "MEDION"),
			DMI_MATCH(DMI_PRODUCT_NAME, "E2215T"),
		},
		.driver_data = (void *)(long)ACPI_BUTTON_LID_INIT_OPEN,
	},
	{
		/*
		 * Medion Akoya E2228T, notification of the LID device only
		 * happens on close, not on open and _LID always returns closed.
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "MEDION"),
			DMI_MATCH(DMI_PRODUCT_NAME, "E2228T"),
		},
		.driver_data = (void *)(long)ACPI_BUTTON_LID_INIT_OPEN,
	},
	{
		/*
		 * Razer Blade Stealth 13 late 2019, notification of the LID device
		 * only happens on close, not on open and _LID always returns closed.
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Razer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Razer Blade Stealth 13 Late 2019"),
		},
		.driver_data = (void *)(long)ACPI_BUTTON_LID_INIT_OPEN,
	},
	{
		/*
		 * Samsung galaxybook2 ,initial _LID device notification returns
		 * lid closed.
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
			DMI_MATCH(DMI_PRODUCT_NAME, "750XED"),
		},
		.driver_data = (void *)(long)ACPI_BUTTON_LID_INIT_OPEN,
	},
	{}
};

static int acpi_button_probe(struct platform_device *pdev);
static void acpi_button_remove(struct platform_device *pdev);

#ifdef CONFIG_PM_SLEEP
static int acpi_button_suspend(struct device *dev);
static int acpi_button_resume(struct device *dev);
#else
#define acpi_button_suspend NULL
#define acpi_button_resume NULL
#endif
static SIMPLE_DEV_PM_OPS(acpi_button_pm, acpi_button_suspend, acpi_button_resume);

static struct platform_driver acpi_button_driver = {
	.probe = acpi_button_probe,
	.remove = acpi_button_remove,
	.driver = {
		.name = "acpi-button",
		.acpi_match_table = button_device_ids,
		.pm = &acpi_button_pm,
	},
};

struct acpi_button {
	struct acpi_device *adev;
	struct device *dev;		/* physical button device */
	unsigned int type;
	struct input_dev *input;
	const char *class;		/* for netlink messages */
	char phys[32];			/* for input device */
	unsigned long pushed;
	bool last_state;
	ktime_t last_time;
	bool suspended;
	bool lid_state_initialized;
	bool gpe_enabled;
};

static long lid_init_state = -1;

static unsigned long lid_report_interval __read_mostly = 500;
module_param(lid_report_interval, ulong, 0644);
MODULE_PARM_DESC(lid_report_interval, "Interval (ms) between lid key events");

/* FS Interface (/proc) */
static struct proc_dir_entry *acpi_button_dir;
static struct proc_dir_entry *acpi_lid_dir;

static int acpi_lid_evaluate_state(acpi_handle lid_handle)
{
	unsigned long long lid_state;
	acpi_status status;

	status = acpi_evaluate_integer(lid_handle, "_LID", NULL, &lid_state);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	return !!lid_state;
}

static void acpi_lid_notify_state(struct acpi_button *button, bool state)
{
	struct acpi_device *device = button->adev;
	ktime_t next_report;
	bool do_update;

	/*
	 * In lid_init_state=ignore mode, if user opens/closes lid
	 * frequently with "open" missing, and "last_time" is also updated
	 * frequently, "close" cannot be delivered to the userspace.
	 * So "last_time" is only updated after a timeout or an actual
	 * switch.
	 */
	do_update = lid_init_state != ACPI_BUTTON_LID_INIT_IGNORE ||
			button->last_state != state;
	next_report = ktime_add(button->last_time,
				ms_to_ktime(lid_report_interval));
	if (button->last_state == state &&
	    ktime_after(ktime_get(), next_report)) {
		/* Complain about the buggy firmware. */
		pr_warn_once(FW_BUG "Unexpected lid state reported by firmware\n");

		/*
		 * Send the unreliable complement switch event:
		 *
		 * On most platforms, the lid device is reliable. However
		 * there are exceptions:
		 * 1. Platforms returning initial lid state as "close" by
		 *    default after booting/resuming:
		 *     https://bugzilla.kernel.org/show_bug.cgi?id=89211
		 *     https://bugzilla.kernel.org/show_bug.cgi?id=106151
		 * 2. Platforms never reporting "open" events:
		 *     https://bugzilla.kernel.org/show_bug.cgi?id=106941
		 * On these buggy platforms, the usage model of the ACPI
		 * lid device actually is:
		 * 1. The initial returning value of _LID may not be
		 *    reliable.
		 * 2. The open event may not be reliable.
		 * 3. The close event is reliable.
		 *
		 * But SW_LID is typed as input switch event, the input
		 * layer checks if the event is redundant. Hence if the
		 * state is not switched, the userspace cannot see this
		 * platform triggered reliable event. By inserting a
		 * complement switch event, it then is guaranteed that the
		 * platform triggered reliable one can always be seen by
		 * the userspace.
		 */
		if (lid_init_state == ACPI_BUTTON_LID_INIT_IGNORE) {
			do_update = true;
			/*
			 * Do generate complement switch event for "close"
			 * as "close" is reliable and wrong "open" won't
			 * trigger unexpected behaviors.
			 * Do not generate complement switch event for
			 * "open" as "open" is not reliable and wrong
			 * "close" will trigger unexpected behaviors.
			 */
			if (!state) {
				input_report_switch(button->input,
						    SW_LID, state);
				input_sync(button->input);
			}
		}
	}
	/* Send the platform triggered reliable event */
	if (do_update) {
		acpi_handle_debug(device->handle, "ACPI LID %s\n",
				  state ? "open" : "closed");
		input_report_switch(button->input, SW_LID, !state);
		input_sync(button->input);
		button->last_state = state;
		button->last_time = ktime_get();
	}
}

static int __maybe_unused acpi_button_state_seq_show(struct seq_file *seq,
						     void *offset)
{
	struct acpi_button *button = seq->private;
	int state;

	state = acpi_lid_evaluate_state(button->adev->handle);
	seq_printf(seq, "state:      %s\n",
		   state < 0 ? "unsupported" : (state ? "open" : "closed"));
	return 0;
}

static int acpi_lid_add_fs(struct acpi_button *button)
{
	struct acpi_device *device = button->adev;
	struct proc_dir_entry *entry = NULL;

	if (acpi_button_dir || acpi_lid_dir) {
		pr_info("More than one Lid device found!\n");
		return -EEXIST;
	}

	/* create /proc/acpi/button */
	acpi_button_dir = proc_mkdir(ACPI_BUTTON_CLASS, acpi_root_dir);
	if (!acpi_button_dir)
		return -ENODEV;

	/* create /proc/acpi/button/lid */
	acpi_lid_dir = proc_mkdir(ACPI_BUTTON_SUBCLASS_LID, acpi_button_dir);
	if (!acpi_lid_dir)
		goto remove_button_dir;

	/* create /proc/acpi/button/lid/LID/ */
	acpi_device_dir(device) = proc_mkdir(acpi_device_bid(device), acpi_lid_dir);
	if (!acpi_device_dir(device))
		goto remove_lid_dir;

	/* create /proc/acpi/button/lid/LID/state */
	entry = proc_create_single_data(ACPI_BUTTON_FILE_STATE, S_IRUGO,
			acpi_device_dir(device), acpi_button_state_seq_show,
			button);
	if (!entry)
		goto remove_dev_dir;

	return 0;

remove_dev_dir:
	remove_proc_entry(acpi_device_bid(device), acpi_lid_dir);
	acpi_device_dir(device) = NULL;
remove_lid_dir:
	remove_proc_entry(ACPI_BUTTON_SUBCLASS_LID, acpi_button_dir);
	acpi_lid_dir = NULL;
remove_button_dir:
	remove_proc_entry(ACPI_BUTTON_CLASS, acpi_root_dir);
	acpi_button_dir = NULL;
	return -ENODEV;
}

static void acpi_lid_remove_fs(void *data)
{
	struct acpi_button *button = data;
	struct acpi_device *device = button->adev;

	remove_proc_entry(ACPI_BUTTON_FILE_STATE,
			  acpi_device_dir(device));
	remove_proc_entry(acpi_device_bid(device),
			  acpi_lid_dir);
	acpi_device_dir(device) = NULL;
	remove_proc_entry(ACPI_BUTTON_SUBCLASS_LID, acpi_button_dir);
	acpi_lid_dir = NULL;
	remove_proc_entry(ACPI_BUTTON_CLASS, acpi_root_dir);
	acpi_button_dir = NULL;
}

static int devm_acpi_lid_add_fs(struct device *dev, struct acpi_button *button)
{
	int ret;

	ret = acpi_lid_add_fs(button);
	if (ret)
		return ret;

	return devm_add_action_or_reset(dev, acpi_lid_remove_fs, button);
}

static acpi_handle saved_lid_handle;
static DEFINE_MUTEX(acpi_lid_lock);

static void acpi_lid_save(struct acpi_device *adev)
{
	guard(mutex)(&acpi_lid_lock);

	saved_lid_handle = adev->handle;
}

static void acpi_lid_forget(struct acpi_device *adev)
{
	guard(mutex)(&acpi_lid_lock);

	if (saved_lid_handle == adev->handle)
		saved_lid_handle = NULL;
}

/* Driver Interface */
int acpi_lid_open(void)
{
	guard(mutex)(&acpi_lid_lock);

	if (!saved_lid_handle)
		return -ENODEV;

	return acpi_lid_evaluate_state(saved_lid_handle);
}
EXPORT_SYMBOL(acpi_lid_open);

static void acpi_lid_update_state(struct acpi_button *button, bool signal_wakeup)
{
	int state;

	state = acpi_lid_evaluate_state(button->adev->handle);
	if (state < 0)
		return;

	if (state && signal_wakeup)
		acpi_pm_wakeup_event(button->dev);

	acpi_lid_notify_state(button, state);
}

static void acpi_lid_initialize_state(struct acpi_button *button)
{
	switch (lid_init_state) {
	case ACPI_BUTTON_LID_INIT_OPEN:
		acpi_lid_notify_state(button, true);
		break;
	case ACPI_BUTTON_LID_INIT_METHOD:
		acpi_lid_update_state(button, false);
		break;
	case ACPI_BUTTON_LID_INIT_IGNORE:
	default:
		break;
	}

	button->lid_state_initialized = true;
}

static void acpi_lid_notify(acpi_handle handle, u32 event, void *data)
{
	struct acpi_button *button = data;
	struct acpi_device *device = button->adev;

	if (event != ACPI_BUTTON_NOTIFY_STATUS) {
		acpi_handle_debug(device->handle, "Unsupported event [0x%x]\n",
				  event);
		return;
	}

	if (!button->lid_state_initialized)
		return;

	acpi_lid_update_state(button, true);
}

static void acpi_button_notify(acpi_handle handle, u32 event, void *data)
{
	struct acpi_button *button = data;
	struct acpi_device *device = button->adev;
	struct input_dev *input;
	int keycode;

	switch (event) {
	case ACPI_BUTTON_NOTIFY_STATUS:
		break;
	case ACPI_BUTTON_NOTIFY_WAKE:
		break;
	default:
		acpi_handle_debug(device->handle, "Unsupported event [0x%x]\n",
				  event);
		return;
	}

	acpi_pm_wakeup_event(button->dev);

	if (button->suspended || event == ACPI_BUTTON_NOTIFY_WAKE)
		return;

	input = button->input;
	keycode = test_bit(KEY_SLEEP, input->keybit) ? KEY_SLEEP : KEY_POWER;

	input_report_key(input, keycode, 1);
	input_sync(input);
	input_report_key(input, keycode, 0);
	input_sync(input);

	acpi_bus_generate_netlink_event(button->class, dev_name(&device->dev),
					event, ++button->pushed);
}

static void acpi_button_notify_run(void *data)
{
	acpi_button_notify(NULL, ACPI_BUTTON_NOTIFY_STATUS, data);
}

static u32 acpi_button_event(void *data)
{
	acpi_os_execute(OSL_NOTIFY_HANDLER, acpi_button_notify_run, data);
	return ACPI_INTERRUPT_HANDLED;
}

#ifdef CONFIG_PM_SLEEP
static int acpi_button_suspend(struct device *dev)
{
	struct acpi_button *button = dev_get_drvdata(dev);

	button->suspended = true;
	return 0;
}

static int acpi_button_resume(struct device *dev)
{
	struct acpi_button *button = dev_get_drvdata(dev);
	struct input_dev *input;

	button->suspended = false;
	if (button->type == ACPI_BUTTON_TYPE_LID) {
		button->last_state = !!acpi_lid_evaluate_state(ACPI_HANDLE(dev));
		button->last_time = ktime_get();
		acpi_lid_initialize_state(button);
	}

	if (button->type == ACPI_BUTTON_TYPE_POWER) {
		input = button->input;
		input_report_key(input, KEY_WAKEUP, 1);
		input_sync(input);
		input_report_key(input, KEY_WAKEUP, 0);
		input_sync(input);
	}
	return 0;
}
#endif

static int acpi_lid_input_open(struct input_dev *input)
{
	struct acpi_button *button = input_get_drvdata(input);

	button->last_state = !!acpi_lid_evaluate_state(button->adev->handle);
	button->last_time = ktime_get();
	acpi_lid_initialize_state(button);

	return 0;
}

static acpi_notify_handler acpi_button_notify_handler(struct acpi_button *button)
{
	if (button->type == ACPI_BUTTON_TYPE_LID)
		return acpi_lid_notify;

	return acpi_button_notify;
}

static void acpi_button_wakeup_cleanup(void *data)
{
	device_init_wakeup(data, false);
}

static int devm_acpi_button_init_wakeup(struct device *dev)
{
	device_init_wakeup(dev, true);
	return devm_add_action_or_reset(dev, acpi_button_wakeup_cleanup, dev);
}

static void acpi_button_remove_event_handler(void *data)
{
	struct acpi_button *button = data;
	struct acpi_device *adev = button->adev;

	switch (adev->device_type) {
	case ACPI_BUS_TYPE_POWER_BUTTON:
		acpi_remove_fixed_event_handler(ACPI_EVENT_POWER_BUTTON,
						acpi_button_event);
		break;

	case ACPI_BUS_TYPE_SLEEP_BUTTON:
		acpi_remove_fixed_event_handler(ACPI_EVENT_SLEEP_BUTTON,
						acpi_button_event);
		break;

	default:
		if (button->gpe_enabled) {
			dev_dbg(button->dev, "Disabling ACPI GPE%02llx\n",
				adev->wakeup.gpe_number);
			acpi_disable_gpe(adev->wakeup.gpe_device,
					 adev->wakeup.gpe_number);
		}
		acpi_remove_notify_handler(adev->handle, ACPI_ALL_NOTIFY,
					   acpi_button_notify_handler(button));
		break;
	}
	acpi_os_wait_events_complete();
}

static int acpi_button_add_fixed_event_handler(u32 event,
					       struct acpi_button *button)
{
	acpi_status status;

	status = acpi_install_fixed_event_handler(event, acpi_button_event, button);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	return 0;
}

static int acpi_button_add_event_handler(struct acpi_button *button)
{
	struct acpi_device *adev = button->adev;
	acpi_status status;

	if (adev->device_type == ACPI_BUS_TYPE_POWER_BUTTON)
		return acpi_button_add_fixed_event_handler(ACPI_EVENT_POWER_BUTTON,
							   button);

	if (adev->device_type == ACPI_BUS_TYPE_SLEEP_BUTTON)
		return acpi_button_add_fixed_event_handler(ACPI_EVENT_SLEEP_BUTTON,
							   button);

	status = acpi_install_notify_handler(adev->handle, ACPI_ALL_NOTIFY,
					     acpi_button_notify_handler(button),
					     button);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	if (!adev->wakeup.flags.valid)
		return 0;

	/*
	 * If the wakeup GPE has a handler method, enable it in case it is also
	 * used for signaling runtime events.
	 */
	status = acpi_enable_gpe_cond(adev->wakeup.gpe_device,
				      adev->wakeup.gpe_number,
				      ACPI_GPE_DISPATCH_METHOD);
	button->gpe_enabled = ACPI_SUCCESS(status);
	if (button->gpe_enabled)
		dev_dbg(button->dev, "Enabled ACPI GPE%02llx\n",
			adev->wakeup.gpe_number);

	return 0;
}

static int devm_acpi_button_add_event_handler(struct device *dev,
					      struct acpi_button *button)
{
	int ret;

	ret = acpi_button_add_event_handler(button);
	if (ret)
		return ret;

	return devm_add_action_or_reset(dev, acpi_button_remove_event_handler,
					button);
}

static int acpi_button_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct acpi_device *device = ACPI_COMPANION(dev);
	const struct acpi_device_id *id;
	struct acpi_button *button;
	struct input_dev *input;
	u8 button_type;
	int error = 0;

	id = acpi_match_acpi_device(button_device_ids, device);
	if (!id || strcmp(acpi_device_hid(device), id->id))
		return dev_err_probe(dev, -ENODEV, "Unsupported device\n");

	button_type = id->driver_data;
	if (button_type == ACPI_BUTTON_TYPE_LID &&
	    lid_init_state == ACPI_BUTTON_LID_INIT_DISABLED)
		return -ENODEV;

	button = devm_kzalloc(dev, sizeof(*button), GFP_KERNEL);
	if (!button)
		return -ENOMEM;

	platform_set_drvdata(pdev, button);

	button->dev = dev;
	button->adev = device;
	input = devm_input_allocate_device(dev);
	if (!input)
		return -ENOMEM;

	button->input = input;
	button->type = button_type;

	switch (button_type) {
	case ACPI_BUTTON_TYPE_LID:
		button->class = ACPI_BUTTON_CLASS_LID;

		input->name = ACPI_BUTTON_DEVICE_NAME_LID;
		input_set_capability(input, EV_SW, SW_LID);
		input->open = acpi_lid_input_open;

		error = devm_acpi_lid_add_fs(dev, button);
		if (error)
			return error;

		break;

	case ACPI_BUTTON_TYPE_POWER:
		button->class = ACPI_BUTTON_CLASS_POWER;

		input->name = ACPI_BUTTON_DEVICE_NAME_POWER;
		input_set_capability(input, EV_KEY, KEY_POWER);
		input_set_capability(input, EV_KEY, KEY_WAKEUP);
		break;

	case ACPI_BUTTON_TYPE_SLEEP:
		button->class = ACPI_BUTTON_CLASS_SLEEP;

		input->name = ACPI_BUTTON_DEVICE_NAME_SLEEP;
		input_set_capability(input, EV_KEY, KEY_SLEEP);
		break;

	default:
		return dev_err_probe(dev, -ENODEV, "Unrecognized button type\n");
	}

	snprintf(button->phys, sizeof(button->phys), "%s/button/input0",
		 acpi_device_hid(device));

	input->phys = button->phys;
	input->id.bustype = BUS_HOST;
	input->id.product = button_type;

	input_set_drvdata(input, button);
	error = input_register_device(input);
	if (error)
		return error;

	error = devm_acpi_button_init_wakeup(dev);
	if (error)
		return error;

	error = devm_acpi_button_add_event_handler(dev, button);
	if (error)
		return error;

	if (button_type == ACPI_BUTTON_TYPE_LID) {
		/*
		 * This assumes there's only one lid device, or if there are
		 * more we only care about the last one...
		 */
		acpi_lid_save(device);
	}

	pr_info("%s [%s]\n", input->name, acpi_device_bid(device));

	return 0;
}

static void acpi_button_remove(struct platform_device *pdev)
{
	struct acpi_button *button = platform_get_drvdata(pdev);

	if (button->type == ACPI_BUTTON_TYPE_LID)
		acpi_lid_forget(button->adev);
}

static int param_set_lid_init_state(const char *val,
				    const struct kernel_param *kp)
{
	int i;

	i = sysfs_match_string(lid_init_state_str, val);
	if (i < 0)
		return i;

	lid_init_state = i;
	pr_info("Initial lid state set to '%s'\n", lid_init_state_str[i]);
	return 0;
}

static int param_get_lid_init_state(char *buf, const struct kernel_param *kp)
{
	int i, c = 0;

	for (i = 0; i < ARRAY_SIZE(lid_init_state_str); i++)
		if (i == lid_init_state)
			c += sprintf(buf + c, "[%s] ", lid_init_state_str[i]);
		else
			c += sprintf(buf + c, "%s ", lid_init_state_str[i]);

	buf[c - 1] = '\n'; /* Replace the final space with a newline */

	return c;
}

module_param_call(lid_init_state,
		  param_set_lid_init_state, param_get_lid_init_state,
		  NULL, 0644);
MODULE_PARM_DESC(lid_init_state, "Behavior for reporting LID initial state");

static int __init acpi_button_init(void)
{
	const struct dmi_system_id *dmi_id;

	if (lid_init_state == -1) {
		dmi_id = dmi_first_match(dmi_lid_quirks);
		if (dmi_id)
			lid_init_state = (long)dmi_id->driver_data;
		else
			lid_init_state = ACPI_BUTTON_LID_INIT_METHOD;
	}

	/*
	 * Modules such as nouveau.ko and i915.ko have a link time dependency
	 * on acpi_lid_open(), and would therefore not be loadable on ACPI
	 * capable kernels booted in non-ACPI mode if the return value of
	 * platform_driver_register() is returned from here with ACPI disabled
	 * when this driver is built as a module.
	 */
	if (acpi_disabled)
		return 0;

	return platform_driver_register(&acpi_button_driver);
}

static void __exit acpi_button_exit(void)
{
	if (!acpi_disabled)
		platform_driver_unregister(&acpi_button_driver);
}

module_init(acpi_button_init);
module_exit(acpi_button_exit);
