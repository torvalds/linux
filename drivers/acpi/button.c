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
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>

#define ACPI_BUTTON_COMPONENT		0x00080000
#define ACPI_BUTTON_DRIVER_NAME		"ACPI Button Driver"
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
ACPI_MODULE_NAME("acpi_button")

    MODULE_AUTHOR("Paul Diefenbaugh");
MODULE_DESCRIPTION(ACPI_BUTTON_DRIVER_NAME);
MODULE_LICENSE("GPL");

static int acpi_button_add(struct acpi_device *device);
static int acpi_button_remove(struct acpi_device *device, int type);
static int acpi_button_info_open_fs(struct inode *inode, struct file *file);
static int acpi_button_state_open_fs(struct inode *inode, struct file *file);

static struct acpi_driver acpi_button_driver = {
	.name = ACPI_BUTTON_DRIVER_NAME,
	.class = ACPI_BUTTON_CLASS,
	.ids = "ACPI_FPB,ACPI_FSB,PNP0C0D,PNP0C0C,PNP0C0E",
	.ops = {
		.add = acpi_button_add,
		.remove = acpi_button_remove,
		},
};

struct acpi_button {
	acpi_handle handle;
	struct acpi_device *device;	/* Fixed button kludge */
	u8 type;
	unsigned long pushed;
};

static struct file_operations acpi_button_info_fops = {
	.open = acpi_button_info_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static struct file_operations acpi_button_state_fops = {
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
	struct acpi_button *button = (struct acpi_button *)seq->private;

	ACPI_FUNCTION_TRACE("acpi_button_info_seq_show");

	if (!button || !button->device)
		return_VALUE(0);

	seq_printf(seq, "type:                    %s\n",
		   acpi_device_name(button->device));

	return_VALUE(0);
}

static int acpi_button_info_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_button_info_seq_show, PDE(inode)->data);
}

static int acpi_button_state_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_button *button = (struct acpi_button *)seq->private;
	acpi_status status;
	unsigned long state;

	ACPI_FUNCTION_TRACE("acpi_button_state_seq_show");

	if (!button || !button->device)
		return_VALUE(0);

	status = acpi_evaluate_integer(button->handle, "_LID", NULL, &state);
	if (ACPI_FAILURE(status)) {
		seq_printf(seq, "state:      unsupported\n");
	} else {
		seq_printf(seq, "state:      %s\n",
			   (state ? "open" : "closed"));
	}

	return_VALUE(0);
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
	struct acpi_button *button = NULL;

	ACPI_FUNCTION_TRACE("acpi_button_add_fs");

	if (!device || !acpi_driver_data(device))
		return_VALUE(-EINVAL);

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
		return_VALUE(-ENODEV);
	entry->owner = THIS_MODULE;

	acpi_device_dir(device) = proc_mkdir(acpi_device_bid(device), entry);
	if (!acpi_device_dir(device))
		return_VALUE(-ENODEV);
	acpi_device_dir(device)->owner = THIS_MODULE;

	/* 'info' [R] */
	entry = create_proc_entry(ACPI_BUTTON_FILE_INFO,
				  S_IRUGO, acpi_device_dir(device));
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Unable to create '%s' fs entry\n",
				  ACPI_BUTTON_FILE_INFO));
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
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					  "Unable to create '%s' fs entry\n",
					  ACPI_BUTTON_FILE_INFO));
		else {
			entry->proc_fops = &acpi_button_state_fops;
			entry->data = acpi_driver_data(device);
			entry->owner = THIS_MODULE;
		}
	}

	return_VALUE(0);
}

static int acpi_button_remove_fs(struct acpi_device *device)
{
	struct acpi_button *button = NULL;

	ACPI_FUNCTION_TRACE("acpi_button_remove_fs");

	button = acpi_driver_data(device);
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

	return_VALUE(0);
}

/* --------------------------------------------------------------------------
                                Driver Interface
   -------------------------------------------------------------------------- */

static void acpi_button_notify(acpi_handle handle, u32 event, void *data)
{
	struct acpi_button *button = (struct acpi_button *)data;

	ACPI_FUNCTION_TRACE("acpi_button_notify");

	if (!button || !button->device)
		return_VOID;

	switch (event) {
	case ACPI_BUTTON_NOTIFY_STATUS:
		acpi_bus_generate_event(button->device, event,
					++button->pushed);
		break;
	default:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Unsupported event [0x%x]\n", event));
		break;
	}

	return_VOID;
}

static acpi_status acpi_button_notify_fixed(void *data)
{
	struct acpi_button *button = (struct acpi_button *)data;

	ACPI_FUNCTION_TRACE("acpi_button_notify_fixed");

	if (!button)
		return_ACPI_STATUS(AE_BAD_PARAMETER);

	acpi_button_notify(button->handle, ACPI_BUTTON_NOTIFY_STATUS, button);

	return_ACPI_STATUS(AE_OK);
}

static int acpi_button_add(struct acpi_device *device)
{
	int result = 0;
	acpi_status status = AE_OK;
	struct acpi_button *button = NULL;

	ACPI_FUNCTION_TRACE("acpi_button_add");

	if (!device)
		return_VALUE(-EINVAL);

	button = kmalloc(sizeof(struct acpi_button), GFP_KERNEL);
	if (!button)
		return_VALUE(-ENOMEM);
	memset(button, 0, sizeof(struct acpi_button));

	button->device = device;
	button->handle = device->handle;
	acpi_driver_data(device) = button;

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
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Unsupported hid [%s]\n",
				  acpi_device_hid(device)));
		result = -ENODEV;
		goto end;
	}

	result = acpi_button_add_fs(device);
	if (result)
		goto end;

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
		status = acpi_install_notify_handler(button->handle,
						     ACPI_DEVICE_NOTIFY,
						     acpi_button_notify,
						     button);
		break;
	}

	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Error installing notify handler\n"));
		result = -ENODEV;
		goto end;
	}

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

      end:
	if (result) {
		acpi_button_remove_fs(device);
		kfree(button);
	}

	return_VALUE(result);
}

static int acpi_button_remove(struct acpi_device *device, int type)
{
	acpi_status status = 0;
	struct acpi_button *button = NULL;

	ACPI_FUNCTION_TRACE("acpi_button_remove");

	if (!device || !acpi_driver_data(device))
		return_VALUE(-EINVAL);

	button = acpi_driver_data(device);

	/* Unregister for device notifications. */
	switch (button->type) {
	case ACPI_BUTTON_TYPE_POWERF:
		status =
		    acpi_remove_fixed_event_handler(ACPI_EVENT_POWER_BUTTON,
						    acpi_button_notify_fixed);
		break;
	case ACPI_BUTTON_TYPE_SLEEPF:
		status =
		    acpi_remove_fixed_event_handler(ACPI_EVENT_SLEEP_BUTTON,
						    acpi_button_notify_fixed);
		break;
	default:
		status = acpi_remove_notify_handler(button->handle,
						    ACPI_DEVICE_NOTIFY,
						    acpi_button_notify);
		break;
	}

	if (ACPI_FAILURE(status))
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Error removing notify handler\n"));

	acpi_button_remove_fs(device);

	kfree(button);

	return_VALUE(0);
}

static int __init acpi_button_init(void)
{
	int result = 0;

	ACPI_FUNCTION_TRACE("acpi_button_init");

	acpi_button_dir = proc_mkdir(ACPI_BUTTON_CLASS, acpi_root_dir);
	if (!acpi_button_dir)
		return_VALUE(-ENODEV);
	acpi_button_dir->owner = THIS_MODULE;
	result = acpi_bus_register_driver(&acpi_button_driver);
	if (result < 0) {
		remove_proc_entry(ACPI_BUTTON_CLASS, acpi_root_dir);
		return_VALUE(-ENODEV);
	}

	return_VALUE(0);
}

static void __exit acpi_button_exit(void)
{
	ACPI_FUNCTION_TRACE("acpi_button_exit");

	acpi_bus_unregister_driver(&acpi_button_driver);

	if (acpi_power_dir)
		remove_proc_entry(ACPI_BUTTON_SUBCLASS_POWER, acpi_button_dir);
	if (acpi_sleep_dir)
		remove_proc_entry(ACPI_BUTTON_SUBCLASS_SLEEP, acpi_button_dir);
	if (acpi_lid_dir)
		remove_proc_entry(ACPI_BUTTON_SUBCLASS_LID, acpi_button_dir);
	remove_proc_entry(ACPI_BUTTON_CLASS, acpi_root_dir);

	return_VOID;
}

module_init(acpi_button_init);
module_exit(acpi_button_exit);
