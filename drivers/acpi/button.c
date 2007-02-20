/*
 *  acpi_button.c - ACPI Button Driver ($Revision: 30 $)
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/input.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>

#define ACPI_BUTTON_COMPONENT		0x00080000
#define ACPI_BUTTON_CLASS		"button"
#define ACPI_BUTTON_FILE_INFO		"info"
#define ACPI_BUTTON_FILE_STATE		"state"
#define ACPI_BUTTON_TYPE_UNKNOWN	0x00
#define ACPI_BUTTON_NOTIFY_STATUS	0x80

#define ACPI_BUTTON_SUBCLASS_POWER	"power"
#define ACPI_BUTTON_HID_POWER		"PNP0C0C"
#define ACPI_BUTTON_DEVICE_NAME_POWER	"Power Button (CM)"
#define ACPI_BUTTON_DEVICE_NAME_POWERF	"Power Button (FF)"
#define ACPI_BUTTON_TYPE_POWER		0x01
#define ACPI_BUTTON_TYPE_POWERF		0x02

#define ACPI_BUTTON_SUBCLASS_SLEEP	"sleep"
#define ACPI_BUTTON_HID_SLEEP		"PNP0C0E"
#define ACPI_BUTTON_DEVICE_NAME_SLEEP	"Sleep Button (CM)"
#define ACPI_BUTTON_DEVICE_NAME_SLEEPF	"Sleep Button (FF)"
#define ACPI_BUTTON_TYPE_SLEEP		0x03
#define ACPI_BUTTON_TYPE_SLEEPF		0x04

#define ACPI_BUTTON_SUBCLASS_LID	"lid"
#define ACPI_BUTTON_HID_LID		"PNP0C0D"
#define ACPI_BUTTON_DEVICE_NAME_LID	"Lid Switch"
#define ACPI_BUTTON_TYPE_LID		0x05

#define _COMPONENT		ACPI_BUTTON_COMPONENT
ACPI_MODULE_NAME("button");

MODULE_AUTHOR("Paul Diefenbaugh");
MODULE_DESCRIPTION("ACPI Button Driver");
MODULE_LICENSE("GPL");

static int acpi_button_add(struct acpi_device *device);
static int acpi_button_remove(struct acpi_device *device, int type);
static int acpi_button_info_open_fs(struct inode *inode, struct file *file);
static int acpi_button_state_open_fs(struct inode *inode, struct file *file);

static struct acpi_driver acpi_button_driver = {
	.name = "button",
	.class = ACPI_BUTTON_CLASS,
	.ids = "button_power,button_sleep,PNP0C0D,PNP0C0C,PNP0C0E",
	.ops = {
		.add = acpi_button_add,
		.remove = acpi_button_remove,
	},
};

struct acpi_button {
	struct acpi_device *device;	/* Fixed button kludge */
	unsigned int type;
	struct input_dev *input;
	char phys[32];			/* for input device */
	unsigned long pushed;
};

static const struct file_operations acpi_button_info_fops = {
	.open = acpi_button_info_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations acpi_button_state_fops = {
	.open = acpi_button_state_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

/* --------------------------------------------------------------------------
                              FS Interface (/proc)
   -------------------------------------------------------------------------- */

static struct proc_dir_entry *acpi_button_dir;

static int acpi_button_info_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_button *button = seq->private;

	if (!button || !button->device)
		return 0;

	seq_printf(seq, "type:                    %s\n",
		   acpi_device_name(button->device));

	return 0;
}

static int acpi_button_info_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_button_info_seq_show, PDE(inode)->data);
}

static int acpi_button_state_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_button *button = seq->private;
	acpi_status status;
	unsigned long state;

	if (!button || !button->device)
		return 0;

	status = acpi_evaluate_integer(button->device->handle, "_LID", NULL, &state);
	seq_printf(seq, "state:      %s\n",
		   ACPI_FAILURE(status) ? "unsupported" :
			(state ? "open" : "closed"));
	return 0;
}

static int acpi_button_state_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_button_state_seq_show, PDE(inode)->data);
}

static struct proc_dir_entry *acpi_power_dir;
static struct proc_dir_entry *acpi_sleep_dir;
static struct proc_dir_entry *acpi_lid_dir;

static int acpi_button_add_fs(struct acpi_device *device)
{
	struct proc_dir_entry *entry = NULL;
	struct acpi_button *button;

	if (!device || !acpi_driver_data(device))
		return -EINVAL;

	button = acpi_driver_data(device);

	switch (button->type) {
	case ACPI_BUTTON_TYPE_POWER:
	case ACPI_BUTTON_TYPE_POWERF:
		if (!acpi_power_dir)
			acpi_power_dir = proc_mkdir(ACPI_BUTTON_SUBCLASS_POWER,
						    acpi_button_dir);
		entry = acpi_power_dir;
		break;
	case ACPI_BUTTON_TYPE_SLEEP:
	case ACPI_BUTTON_TYPE_SLEEPF:
		if (!acpi_sleep_dir)
			acpi_sleep_dir = proc_mkdir(ACPI_BUTTON_SUBCLASS_SLEEP,
						    acpi_button_dir);
		entry = acpi_sleep_dir;
		break;
	case ACPI_BUTTON_TYPE_LID:
		if (!acpi_lid_dir)
			acpi_lid_dir = proc_mkdir(ACPI_BUTTON_SUBCLASS_LID,
						  acpi_button_dir);
		entry = acpi_lid_dir;
		break;
	}

	if (!entry)
		return -ENODEV;
	entry->owner = THIS_MODULE;

	acpi_device_dir(device) = proc_mkdir(acpi_device_bid(device), entry);
	if (!acpi_device_dir(device))
		return -ENODEV;
	acpi_device_dir(device)->owner = THIS_MODULE;

	/* 'info' [R] */
	entry = create_proc_entry(ACPI_BUTTON_FILE_INFO,
				  S_IRUGO, acpi_device_dir(device));
	if (!entry)
		return -ENODEV;
	else {
		entry->proc_fops = &acpi_button_info_fops;
		entry->data = acpi_driver_data(device);
		entry->owner = THIS_MODULE;
	}

	/* show lid state [R] */
	if (button->type == ACPI_BUTTON_TYPE_LID) {
		entry = create_proc_entry(ACPI_BUTTON_FILE_STATE,
					  S_IRUGO, acpi_device_dir(device));
		if (!entry)
			return -ENODEV;
		else {
			entry->proc_fops = &acpi_button_state_fops;
			entry->data = acpi_driver_data(device);
			entry->owner = THIS_MODULE;
		}
	}

	return 0;
}

static int acpi_button_remove_fs(struct acpi_device *device)
{
	struct acpi_button *button = acpi_driver_data(device);

	if (acpi_device_dir(device)) {
		if (button->type == ACPI_BUTTON_TYPE_LID)
			remove_proc_entry(ACPI_BUTTON_FILE_STATE,
					  acpi_device_dir(device));
		remove_proc_entry(ACPI_BUTTON_FILE_INFO,
				  acpi_device_dir(device));

		remove_proc_entry(acpi_device_bid(device),
				  acpi_device_dir(device)->parent);
		acpi_device_dir(device) = NULL;
	}

	return 0;
}

/* --------------------------------------------------------------------------
                                Driver Interface
   -------------------------------------------------------------------------- */

static void acpi_button_notify(acpi_handle handle, u32 event, void *data)
{
	struct acpi_button *button = data;
	struct input_dev *input;

	if (!button || !button->device)
		return;

	switch (event) {
	case ACPI_BUTTON_NOTIFY_STATUS:
		input = button->input;

		if (button->type == ACPI_BUTTON_TYPE_LID) {
			struct acpi_handle *handle = button->device->handle;
			unsigned long state;

			if (!ACPI_FAILURE(acpi_evaluate_integer(handle, "_LID",
								NULL, &state)))
				input_report_switch(input, SW_LID, !state);

		} else {
			int keycode = test_bit(KEY_SLEEP, input->keybit) ?
						KEY_SLEEP : KEY_POWER;

			input_report_key(input, keycode, 1);
			input_sync(input);
			input_report_key(input, keycode, 0);
		}
		input_sync(input);

		acpi_bus_generate_event(button->device, event,
					++button->pushed);
		break;
	default:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Unsupported event [0x%x]\n", event));
		break;
	}

	return;
}

static acpi_status acpi_button_notify_fixed(void *data)
{
	struct acpi_button *button = data;

	if (!button)
		return AE_BAD_PARAMETER;

	acpi_button_notify(button->device->handle, ACPI_BUTTON_NOTIFY_STATUS, button);

	return AE_OK;
}

static int acpi_button_install_notify_handlers(struct acpi_button *button)
{
	acpi_status status;

	switch (button->type) {
	case ACPI_BUTTON_TYPE_POWERF:
		status =
		    acpi_install_fixed_event_handler(ACPI_EVENT_POWER_BUTTON,
						     acpi_button_notify_fixed,
						     button);
		break;
	case ACPI_BUTTON_TYPE_SLEEPF:
		status =
		    acpi_install_fixed_event_handler(ACPI_EVENT_SLEEP_BUTTON,
						     acpi_button_notify_fixed,
						     button);
		break;
	default:
		status = acpi_install_notify_handler(button->device->handle,
						     ACPI_DEVICE_NOTIFY,
						     acpi_button_notify,
						     button);
		break;
	}

	return ACPI_FAILURE(status) ? -ENODEV : 0;
}

static void acpi_button_remove_notify_handlers(struct acpi_button *button)
{
	switch (button->type) {
	case ACPI_BUTTON_TYPE_POWERF:
		acpi_remove_fixed_event_handler(ACPI_EVENT_POWER_BUTTON,
						acpi_button_notify_fixed);
		break;
	case ACPI_BUTTON_TYPE_SLEEPF:
		acpi_remove_fixed_event_handler(ACPI_EVENT_SLEEP_BUTTON,
						acpi_button_notify_fixed);
		break;
	default:
		acpi_remove_notify_handler(button->device->handle,
					   ACPI_DEVICE_NOTIFY,
					   acpi_button_notify);
		break;
	}
}

static int acpi_button_add(struct acpi_device *device)
{
	int error;
	struct acpi_button *button;
	struct input_dev *input;

	if (!device)
		return -EINVAL;

	button = kzalloc(sizeof(struct acpi_button), GFP_KERNEL);
	if (!button)
		return -ENOMEM;

	button->device = device;
	acpi_driver_data(device) = button;

	button->input = input = input_allocate_device();
	if (!input) {
		error = -ENOMEM;
		goto err_free_button;
	}

	/*
	 * Determine the button type (via hid), as fixed-feature buttons
	 * need to be handled a bit differently than generic-space.
	 */
	if (!strcmp(acpi_device_hid(device), ACPI_BUTTON_HID_POWER)) {
		button->type = ACPI_BUTTON_TYPE_POWER;
		strcpy(acpi_device_name(device), ACPI_BUTTON_DEVICE_NAME_POWER);
		sprintf(acpi_device_class(device), "%s/%s",
			ACPI_BUTTON_CLASS, ACPI_BUTTON_SUBCLASS_POWER);
	} else if (!strcmp(acpi_device_hid(device), ACPI_BUTTON_HID_POWERF)) {
		button->type = ACPI_BUTTON_TYPE_POWERF;
		strcpy(acpi_device_name(device),
		       ACPI_BUTTON_DEVICE_NAME_POWERF);
		sprintf(acpi_device_class(device), "%s/%s",
			ACPI_BUTTON_CLASS, ACPI_BUTTON_SUBCLASS_POWER);
	} else if (!strcmp(acpi_device_hid(device), ACPI_BUTTON_HID_SLEEP)) {
		button->type = ACPI_BUTTON_TYPE_SLEEP;
		strcpy(acpi_device_name(device), ACPI_BUTTON_DEVICE_NAME_SLEEP);
		sprintf(acpi_device_class(device), "%s/%s",
			ACPI_BUTTON_CLASS, ACPI_BUTTON_SUBCLASS_SLEEP);
	} else if (!strcmp(acpi_device_hid(device), ACPI_BUTTON_HID_SLEEPF)) {
		button->type = ACPI_BUTTON_TYPE_SLEEPF;
		strcpy(acpi_device_name(device),
		       ACPI_BUTTON_DEVICE_NAME_SLEEPF);
		sprintf(acpi_device_class(device), "%s/%s",
			ACPI_BUTTON_CLASS, ACPI_BUTTON_SUBCLASS_SLEEP);
	} else if (!strcmp(acpi_device_hid(device), ACPI_BUTTON_HID_LID)) {
		button->type = ACPI_BUTTON_TYPE_LID;
		strcpy(acpi_device_name(device), ACPI_BUTTON_DEVICE_NAME_LID);
		sprintf(acpi_device_class(device), "%s/%s",
			ACPI_BUTTON_CLASS, ACPI_BUTTON_SUBCLASS_LID);
	} else {
		printk(KERN_ERR PREFIX "Unsupported hid [%s]\n",
			    acpi_device_hid(device));
		error = -ENODEV;
		goto err_free_input;
	}

	error = acpi_button_add_fs(device);
	if (error)
		goto err_free_input;

	error = acpi_button_install_notify_handlers(button);
	if (error)
		goto err_remove_fs;

	snprintf(button->phys, sizeof(button->phys),
		 "%s/button/input0", acpi_device_hid(device));

	input->name = acpi_device_name(device);
	input->phys = button->phys;
	input->id.bustype = BUS_HOST;
	input->id.product = button->type;

	switch (button->type) {
	case ACPI_BUTTON_TYPE_POWER:
	case ACPI_BUTTON_TYPE_POWERF:
		input->evbit[0] = BIT(EV_KEY);
		set_bit(KEY_POWER, input->keybit);
		break;

	case ACPI_BUTTON_TYPE_SLEEP:
	case ACPI_BUTTON_TYPE_SLEEPF:
		input->evbit[0] = BIT(EV_KEY);
		set_bit(KEY_SLEEP, input->keybit);
		break;

	case ACPI_BUTTON_TYPE_LID:
		input->evbit[0] = BIT(EV_SW);
		set_bit(SW_LID, input->swbit);
		break;
	}

	error = input_register_device(input);
	if (error)
		goto err_remove_handlers;

	if (device->wakeup.flags.valid) {
		/* Button's GPE is run-wake GPE */
		acpi_set_gpe_type(device->wakeup.gpe_device,
				  device->wakeup.gpe_number,
				  ACPI_GPE_TYPE_WAKE_RUN);
		acpi_enable_gpe(device->wakeup.gpe_device,
				device->wakeup.gpe_number, ACPI_NOT_ISR);
		device->wakeup.state.enabled = 1;
	}

	printk(KERN_INFO PREFIX "%s [%s]\n",
	       acpi_device_name(device), acpi_device_bid(device));

	return 0;

 err_remove_handlers:
	acpi_button_remove_notify_handlers(button);
 err_remove_fs:
	acpi_button_remove_fs(device);
 err_free_input:
	input_free_device(input);
 err_free_button:
	kfree(button);
	return error;
}

static int acpi_button_remove(struct acpi_device *device, int type)
{
	struct acpi_button *button;

	if (!device || !acpi_driver_data(device))
		return -EINVAL;

	button = acpi_driver_data(device);

	acpi_button_remove_notify_handlers(button);
	acpi_button_remove_fs(device);
	input_unregister_device(button->input);
	kfree(button);

	return 0;
}

static int __init acpi_button_init(void)
{
	int result;

	acpi_button_dir = proc_mkdir(ACPI_BUTTON_CLASS, acpi_root_dir);
	if (!acpi_button_dir)
		return -ENODEV;
	acpi_button_dir->owner = THIS_MODULE;
	result = acpi_bus_register_driver(&acpi_button_driver);
	if (result < 0) {
		remove_proc_entry(ACPI_BUTTON_CLASS, acpi_root_dir);
		return -ENODEV;
	}

	return 0;
}

static void __exit acpi_button_exit(void)
{
	acpi_bus_unregister_driver(&acpi_button_driver);

	if (acpi_power_dir)
		remove_proc_entry(ACPI_BUTTON_SUBCLASS_POWER, acpi_button_dir);
	if (acpi_sleep_dir)
		remove_proc_entry(ACPI_BUTTON_SUBCLASS_SLEEP, acpi_button_dir);
	if (acpi_lid_dir)
		remove_proc_entry(ACPI_BUTTON_SUBCLASS_LID, acpi_button_dir);
	remove_proc_entry(ACPI_BUTTON_CLASS, acpi_root_dir);
}

module_init(acpi_button_init);
module_exit(acpi_button_exit);
