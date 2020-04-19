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
#include <acpi/button.h>

#define PREFIX "ACPI: "

#define ACPI_BUTTON_CLASS		"button"
#define ACPI_BUTTON_FILE_INFO		"info"
#define ACPI_BUTTON_FILE_STATE		"state"
#define ACPI_BUTTON_TYPE_UNKNOWN	0x00
#define ACPI_BUTTON_NOTIFY_STATUS	0x80

#define ACPI_BUTTON_SUBCLASS_POWER	"power"
#define ACPI_BUTTON_DEVICE_NAME_POWER	"Power Button"
#define ACPI_BUTTON_TYPE_POWER		0x01

#define ACPI_BUTTON_SUBCLASS_SLEEP	"sleep"
#define ACPI_BUTTON_DEVICE_NAME_SLEEP	"Sleep Button"
#define ACPI_BUTTON_TYPE_SLEEP		0x03

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

#define _COMPONENT		ACPI_BUTTON_COMPONENT
ACPI_MODULE_NAME("button");

MODULE_AUTHOR("Paul Diefenbaugh");
MODULE_DESCRIPTION("ACPI Button Driver");
MODULE_LICENSE("GPL");

static const struct acpi_device_id button_device_ids[] = {
	{ACPI_BUTTON_HID_LID,    0},
	{ACPI_BUTTON_HID_SLEEP,  0},
	{ACPI_BUTTON_HID_SLEEPF, 0},
	{ACPI_BUTTON_HID_POWER,  0},
	{ACPI_BUTTON_HID_POWERF, 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, button_device_ids);

/* Please keep this list sorted alphabetically by vendor and model */
static const struct dmi_system_id dmi_lid_quirks[] = {
	{
		/*
		 * Acer Switch 10 SW5-012. _LID method messes with home and
		 * power button GPIO IRQ settings causing an interrupt storm on
		 * both GPIOs. This is unfixable without a DSDT override, so we
		 * have to disable the lid-switch functionality altogether :|
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire SW5-012"),
		},
		.driver_data = (void *)(long)ACPI_BUTTON_LID_INIT_DISABLED,
	},
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
		/*
		 * Medion Akoya E2215T, notification of the LID device only
		 * happens on close, not on open and _LID always returns closed.
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "MEDION"),
			DMI_MATCH(DMI_PRODUCT_NAME, "E2215T MD60198"),
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
	{}
};

static int acpi_button_add(struct acpi_device *device);
static int acpi_button_remove(struct acpi_device *device);
static void acpi_button_notify(struct acpi_device *device, u32 event);

#ifdef CONFIG_PM_SLEEP
static int acpi_button_suspend(struct device *dev);
static int acpi_button_resume(struct device *dev);
#else
#define acpi_button_suspend NULL
#define acpi_button_resume NULL
#endif
static SIMPLE_DEV_PM_OPS(acpi_button_pm, acpi_button_suspend, acpi_button_resume);

static struct acpi_driver acpi_button_driver = {
	.name = "button",
	.class = ACPI_BUTTON_CLASS,
	.ids = button_device_ids,
	.ops = {
		.add = acpi_button_add,
		.remove = acpi_button_remove,
		.notify = acpi_button_notify,
	},
	.drv.pm = &acpi_button_pm,
};

struct acpi_button {
	unsigned int type;
	struct input_dev *input;
	char phys[32];			/* for input device */
	unsigned long pushed;
	int last_state;
	ktime_t last_time;
	bool suspended;
};

static struct acpi_device *lid_device;
static long lid_init_state = -1;

static unsigned long lid_report_interval __read_mostly = 500;
module_param(lid_report_interval, ulong, 0644);
MODULE_PARM_DESC(lid_report_interval, "Interval (ms) between lid key events");

/* --------------------------------------------------------------------------
                              FS Interface (/proc)
   -------------------------------------------------------------------------- */

static struct proc_dir_entry *acpi_button_dir;
static struct proc_dir_entry *acpi_lid_dir;

static int acpi_lid_evaluate_state(struct acpi_device *device)
{
	unsigned long long lid_state;
	acpi_status status;

	status = acpi_evaluate_integer(device->handle, "_LID", NULL, &lid_state);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	return lid_state ? 1 : 0;
}

static int acpi_lid_notify_state(struct acpi_device *device, int state)
{
	struct acpi_button *button = acpi_driver_data(device);
	ktime_t next_report;
	bool do_update;

	/*
	 * In lid_init_state=ignore mode, if user opens/closes lid
	 * frequently with "open" missing, and "last_time" is also updated
	 * frequently, "close" cannot be delivered to the userspace.
	 * So "last_time" is only updated after a timeout or an actual
	 * switch.
	 */
	if (lid_init_state != ACPI_BUTTON_LID_INIT_IGNORE ||
	    button->last_state != !!state)
		do_update = true;
	else
		do_update = false;

	next_report = ktime_add(button->last_time,
				ms_to_ktime(lid_report_interval));
	if (button->last_state == !!state &&
	    ktime_after(ktime_get(), next_report)) {
		/* Complain the buggy firmware */
		pr_warn_once("The lid device is not compliant to SW_LID.\n");

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
		button->last_state = !!state;
		button->last_time = ktime_get();
	}

	return 0;
}

static int __maybe_unused acpi_button_state_seq_show(struct seq_file *seq,
						     void *offset)
{
	struct acpi_device *device = seq->private;
	int state;

	state = acpi_lid_evaluate_state(device);
	seq_printf(seq, "state:      %s\n",
		   state < 0 ? "unsupported" : (state ? "open" : "closed"));
	return 0;
}

static int acpi_button_add_fs(struct acpi_device *device)
{
	struct acpi_button *button = acpi_driver_data(device);
	struct proc_dir_entry *entry = NULL;
	int ret = 0;

	/* procfs I/F for ACPI lid device only */
	if (button->type != ACPI_BUTTON_TYPE_LID)
		return 0;

	if (acpi_button_dir || acpi_lid_dir) {
		printk(KERN_ERR PREFIX "More than one Lid device found!\n");
		return -EEXIST;
	}

	/* create /proc/acpi/button */
	acpi_button_dir = proc_mkdir(ACPI_BUTTON_CLASS, acpi_root_dir);
	if (!acpi_button_dir)
		return -ENODEV;

	/* create /proc/acpi/button/lid */
	acpi_lid_dir = proc_mkdir(ACPI_BUTTON_SUBCLASS_LID, acpi_button_dir);
	if (!acpi_lid_dir) {
		ret = -ENODEV;
		goto remove_button_dir;
	}

	/* create /proc/acpi/button/lid/LID/ */
	acpi_device_dir(device) = proc_mkdir(acpi_device_bid(device), acpi_lid_dir);
	if (!acpi_device_dir(device)) {
		ret = -ENODEV;
		goto remove_lid_dir;
	}

	/* create /proc/acpi/button/lid/LID/state */
	entry = proc_create_single_data(ACPI_BUTTON_FILE_STATE, S_IRUGO,
			acpi_device_dir(device), acpi_button_state_seq_show,
			device);
	if (!entry) {
		ret = -ENODEV;
		goto remove_dev_dir;
	}

done:
	return ret;

remove_dev_dir:
	remove_proc_entry(acpi_device_bid(device),
			  acpi_lid_dir);
	acpi_device_dir(device) = NULL;
remove_lid_dir:
	remove_proc_entry(ACPI_BUTTON_SUBCLASS_LID, acpi_button_dir);
	acpi_lid_dir = NULL;
remove_button_dir:
	remove_proc_entry(ACPI_BUTTON_CLASS, acpi_root_dir);
	acpi_button_dir = NULL;
	goto done;
}

static int acpi_button_remove_fs(struct acpi_device *device)
{
	struct acpi_button *button = acpi_driver_data(device);

	if (button->type != ACPI_BUTTON_TYPE_LID)
		return 0;

	remove_proc_entry(ACPI_BUTTON_FILE_STATE,
			  acpi_device_dir(device));
	remove_proc_entry(acpi_device_bid(device),
			  acpi_lid_dir);
	acpi_device_dir(device) = NULL;
	remove_proc_entry(ACPI_BUTTON_SUBCLASS_LID, acpi_button_dir);
	acpi_lid_dir = NULL;
	remove_proc_entry(ACPI_BUTTON_CLASS, acpi_root_dir);
	acpi_button_dir = NULL;

	return 0;
}

/* --------------------------------------------------------------------------
                                Driver Interface
   -------------------------------------------------------------------------- */
int acpi_lid_open(void)
{
	if (!lid_device)
		return -ENODEV;

	return acpi_lid_evaluate_state(lid_device);
}
EXPORT_SYMBOL(acpi_lid_open);

static int acpi_lid_update_state(struct acpi_device *device,
				 bool signal_wakeup)
{
	int state;

	state = acpi_lid_evaluate_state(device);
	if (state < 0)
		return state;

	if (state && signal_wakeup)
		acpi_pm_wakeup_event(&device->dev);

	return acpi_lid_notify_state(device, state);
}

static void acpi_lid_initialize_state(struct acpi_device *device)
{
	switch (lid_init_state) {
	case ACPI_BUTTON_LID_INIT_OPEN:
		(void)acpi_lid_notify_state(device, 1);
		break;
	case ACPI_BUTTON_LID_INIT_METHOD:
		(void)acpi_lid_update_state(device, false);
		break;
	case ACPI_BUTTON_LID_INIT_IGNORE:
	default:
		break;
	}
}

static void acpi_button_notify(struct acpi_device *device, u32 event)
{
	struct acpi_button *button = acpi_driver_data(device);
	struct input_dev *input;
	int users;

	switch (event) {
	case ACPI_FIXED_HARDWARE_EVENT:
		event = ACPI_BUTTON_NOTIFY_STATUS;
		/* fall through */
	case ACPI_BUTTON_NOTIFY_STATUS:
		input = button->input;
		if (button->type == ACPI_BUTTON_TYPE_LID) {
			mutex_lock(&button->input->mutex);
			users = button->input->users;
			mutex_unlock(&button->input->mutex);
			if (users)
				acpi_lid_update_state(device, true);
		} else {
			int keycode;

			acpi_pm_wakeup_event(&device->dev);
			if (button->suspended)
				break;

			keycode = test_bit(KEY_SLEEP, input->keybit) ?
						KEY_SLEEP : KEY_POWER;
			input_report_key(input, keycode, 1);
			input_sync(input);
			input_report_key(input, keycode, 0);
			input_sync(input);

			acpi_bus_generate_netlink_event(
					device->pnp.device_class,
					dev_name(&device->dev),
					event, ++button->pushed);
		}
		break;
	default:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Unsupported event [0x%x]\n", event));
		break;
	}
}

#ifdef CONFIG_PM_SLEEP
static int acpi_button_suspend(struct device *dev)
{
	struct acpi_device *device = to_acpi_device(dev);
	struct acpi_button *button = acpi_driver_data(device);

	button->suspended = true;
	return 0;
}

static int acpi_button_resume(struct device *dev)
{
	struct acpi_device *device = to_acpi_device(dev);
	struct acpi_button *button = acpi_driver_data(device);

	button->suspended = false;
	if (button->type == ACPI_BUTTON_TYPE_LID && button->input->users) {
		button->last_state = !!acpi_lid_evaluate_state(device);
		button->last_time = ktime_get();
		acpi_lid_initialize_state(device);
	}
	return 0;
}
#endif

static int acpi_lid_input_open(struct input_dev *input)
{
	struct acpi_device *device = input_get_drvdata(input);
	struct acpi_button *button = acpi_driver_data(device);

	button->last_state = !!acpi_lid_evaluate_state(device);
	button->last_time = ktime_get();
	acpi_lid_initialize_state(device);

	return 0;
}

static int acpi_button_add(struct acpi_device *device)
{
	struct acpi_button *button;
	struct input_dev *input;
	const char *hid = acpi_device_hid(device);
	char *name, *class;
	int error;

	if (!strcmp(hid, ACPI_BUTTON_HID_LID) &&
	     lid_init_state == ACPI_BUTTON_LID_INIT_DISABLED)
		return -ENODEV;

	button = kzalloc(sizeof(struct acpi_button), GFP_KERNEL);
	if (!button)
		return -ENOMEM;

	device->driver_data = button;

	button->input = input = input_allocate_device();
	if (!input) {
		error = -ENOMEM;
		goto err_free_button;
	}

	name = acpi_device_name(device);
	class = acpi_device_class(device);

	if (!strcmp(hid, ACPI_BUTTON_HID_POWER) ||
	    !strcmp(hid, ACPI_BUTTON_HID_POWERF)) {
		button->type = ACPI_BUTTON_TYPE_POWER;
		strcpy(name, ACPI_BUTTON_DEVICE_NAME_POWER);
		sprintf(class, "%s/%s",
			ACPI_BUTTON_CLASS, ACPI_BUTTON_SUBCLASS_POWER);
	} else if (!strcmp(hid, ACPI_BUTTON_HID_SLEEP) ||
		   !strcmp(hid, ACPI_BUTTON_HID_SLEEPF)) {
		button->type = ACPI_BUTTON_TYPE_SLEEP;
		strcpy(name, ACPI_BUTTON_DEVICE_NAME_SLEEP);
		sprintf(class, "%s/%s",
			ACPI_BUTTON_CLASS, ACPI_BUTTON_SUBCLASS_SLEEP);
	} else if (!strcmp(hid, ACPI_BUTTON_HID_LID)) {
		button->type = ACPI_BUTTON_TYPE_LID;
		strcpy(name, ACPI_BUTTON_DEVICE_NAME_LID);
		sprintf(class, "%s/%s",
			ACPI_BUTTON_CLASS, ACPI_BUTTON_SUBCLASS_LID);
		input->open = acpi_lid_input_open;
	} else {
		printk(KERN_ERR PREFIX "Unsupported hid [%s]\n", hid);
		error = -ENODEV;
		goto err_free_input;
	}

	error = acpi_button_add_fs(device);
	if (error)
		goto err_free_input;

	snprintf(button->phys, sizeof(button->phys), "%s/button/input0", hid);

	input->name = name;
	input->phys = button->phys;
	input->id.bustype = BUS_HOST;
	input->id.product = button->type;
	input->dev.parent = &device->dev;

	switch (button->type) {
	case ACPI_BUTTON_TYPE_POWER:
		input_set_capability(input, EV_KEY, KEY_POWER);
		break;

	case ACPI_BUTTON_TYPE_SLEEP:
		input_set_capability(input, EV_KEY, KEY_SLEEP);
		break;

	case ACPI_BUTTON_TYPE_LID:
		input_set_capability(input, EV_SW, SW_LID);
		break;
	}

	input_set_drvdata(input, device);
	error = input_register_device(input);
	if (error)
		goto err_remove_fs;
	if (button->type == ACPI_BUTTON_TYPE_LID) {
		/*
		 * This assumes there's only one lid device, or if there are
		 * more we only care about the last one...
		 */
		lid_device = device;
	}

	device_init_wakeup(&device->dev, true);
	printk(KERN_INFO PREFIX "%s [%s]\n", name, acpi_device_bid(device));
	return 0;

 err_remove_fs:
	acpi_button_remove_fs(device);
 err_free_input:
	input_free_device(input);
 err_free_button:
	kfree(button);
	return error;
}

static int acpi_button_remove(struct acpi_device *device)
{
	struct acpi_button *button = acpi_driver_data(device);

	acpi_button_remove_fs(device);
	input_unregister_device(button->input);
	kfree(button);
	return 0;
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

static int acpi_button_register_driver(struct acpi_driver *driver)
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
	 * acpi_bus_register_driver() is returned from here with ACPI disabled
	 * when this driver is built as a module.
	 */
	if (acpi_disabled)
		return 0;

	return acpi_bus_register_driver(driver);
}

static void acpi_button_unregister_driver(struct acpi_driver *driver)
{
	if (!acpi_disabled)
		acpi_bus_unregister_driver(driver);
}

module_driver(acpi_button_driver, acpi_button_register_driver,
	       acpi_button_unregister_driver);
