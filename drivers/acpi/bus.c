/*
 *  acpi_bus.c - ACPI Bus Driver ($Revision: 80 $)
 *
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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/device.h>
#include <linux/proc_fs.h>
#include <linux/acpi.h>
#ifdef CONFIG_X86
#include <asm/mpspec.h>
#endif
#include <linux/pci.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>

#include "internal.h"

#define _COMPONENT		ACPI_BUS_COMPONENT
ACPI_MODULE_NAME("bus");

struct acpi_device *acpi_root;
struct proc_dir_entry *acpi_root_dir;
EXPORT_SYMBOL(acpi_root_dir);

#define STRUCT_TO_INT(s)	(*((int*)&s))

static int set_power_nocheck(const struct dmi_system_id *id)
{
	printk(KERN_NOTICE PREFIX "%s detected - "
		"disable power check in power transistion\n", id->ident);
	acpi_power_nocheck = 1;
	return 0;
}
static struct dmi_system_id __cpuinitdata power_nocheck_dmi_table[] = {
	{
	set_power_nocheck, "HP Pavilion 05", {
	DMI_MATCH(DMI_BIOS_VENDOR, "Phoenix Technologies LTD"),
	DMI_MATCH(DMI_SYS_VENDOR, "HP Pavilion 05"),
	DMI_MATCH(DMI_PRODUCT_VERSION, "2001211RE101GLEND") }, NULL},
	{},
};


/* --------------------------------------------------------------------------
                                Device Management
   -------------------------------------------------------------------------- */

int acpi_bus_get_device(acpi_handle handle, struct acpi_device **device)
{
	acpi_status status = AE_OK;


	if (!device)
		return -EINVAL;

	/* TBD: Support fixed-feature devices */

	status = acpi_get_data(handle, acpi_bus_data_handler, (void **)device);
	if (ACPI_FAILURE(status) || !*device) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "No context for object [%p]\n",
				  handle));
		return -ENODEV;
	}

	return 0;
}

EXPORT_SYMBOL(acpi_bus_get_device);

int acpi_bus_get_status(struct acpi_device *device)
{
	acpi_status status = AE_OK;
	unsigned long long sta = 0;


	if (!device)
		return -EINVAL;

	/*
	 * Evaluate _STA if present.
	 */
	if (device->flags.dynamic_status) {
		status =
		    acpi_evaluate_integer(device->handle, "_STA", NULL, &sta);
		if (ACPI_FAILURE(status))
			return -ENODEV;
		STRUCT_TO_INT(device->status) = (int)sta;
	}

	/*
	 * According to ACPI spec some device can be present and functional
	 * even if the parent is not present but functional.
	 * In such conditions the child device should not inherit the status
	 * from the parent.
	 */
	else
		STRUCT_TO_INT(device->status) =
		    ACPI_STA_DEVICE_PRESENT | ACPI_STA_DEVICE_ENABLED |
		    ACPI_STA_DEVICE_UI      | ACPI_STA_DEVICE_FUNCTIONING;

	if (device->status.functional && !device->status.present) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Device [%s] status [%08x]: "
		       "functional but not present;\n",
			device->pnp.bus_id,
			(u32) STRUCT_TO_INT(device->status)));
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Device [%s] status [%08x]\n",
			  device->pnp.bus_id,
			  (u32) STRUCT_TO_INT(device->status)));

	return 0;
}

EXPORT_SYMBOL(acpi_bus_get_status);

void acpi_bus_private_data_handler(acpi_handle handle,
				   u32 function, void *context)
{
	return;
}
EXPORT_SYMBOL(acpi_bus_private_data_handler);

int acpi_bus_get_private_data(acpi_handle handle, void **data)
{
	acpi_status status = AE_OK;

	if (!*data)
		return -EINVAL;

	status = acpi_get_data(handle, acpi_bus_private_data_handler, data);
	if (ACPI_FAILURE(status) || !*data) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "No context for object [%p]\n",
				handle));
		return -ENODEV;
	}

	return 0;
}
EXPORT_SYMBOL(acpi_bus_get_private_data);

/* --------------------------------------------------------------------------
                                 Power Management
   -------------------------------------------------------------------------- */

int acpi_bus_get_power(acpi_handle handle, int *state)
{
	int result = 0;
	acpi_status status = 0;
	struct acpi_device *device = NULL;
	unsigned long long psc = 0;


	result = acpi_bus_get_device(handle, &device);
	if (result)
		return result;

	*state = ACPI_STATE_UNKNOWN;

	if (!device->flags.power_manageable) {
		/* TBD: Non-recursive algorithm for walking up hierarchy */
		if (device->parent)
			*state = device->parent->power.state;
		else
			*state = ACPI_STATE_D0;
	} else {
		/*
		 * Get the device's power state either directly (via _PSC) or
		 * indirectly (via power resources).
		 */
		if (device->power.flags.explicit_get) {
			status = acpi_evaluate_integer(device->handle, "_PSC",
						       NULL, &psc);
			if (ACPI_FAILURE(status))
				return -ENODEV;
			device->power.state = (int)psc;
		} else if (device->power.flags.power_resources) {
			result = acpi_power_get_inferred_state(device);
			if (result)
				return result;
		}

		*state = device->power.state;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Device [%s] power state is D%d\n",
			  device->pnp.bus_id, device->power.state));

	return 0;
}

EXPORT_SYMBOL(acpi_bus_get_power);

int acpi_bus_set_power(acpi_handle handle, int state)
{
	int result = 0;
	acpi_status status = AE_OK;
	struct acpi_device *device = NULL;
	char object_name[5] = { '_', 'P', 'S', '0' + state, '\0' };


	result = acpi_bus_get_device(handle, &device);
	if (result)
		return result;

	if ((state < ACPI_STATE_D0) || (state > ACPI_STATE_D3))
		return -EINVAL;

	/* Make sure this is a valid target state */

	if (!device->flags.power_manageable) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Device `[%s]' is not power manageable\n",
				kobject_name(&device->dev.kobj)));
		return -ENODEV;
	}
	/*
	 * Get device's current power state
	 */
	if (!acpi_power_nocheck) {
		/*
		 * Maybe the incorrect power state is returned on the bogus
		 * bios, which is different with the real power state.
		 * For example: the bios returns D0 state and the real power
		 * state is D3. OS expects to set the device to D0 state. In
		 * such case if OS uses the power state returned by the BIOS,
		 * the device can't be transisted to the correct power state.
		 * So if the acpi_power_nocheck is set, it is unnecessary to
		 * get the power state by calling acpi_bus_get_power.
		 */
		acpi_bus_get_power(device->handle, &device->power.state);
	}
	if ((state == device->power.state) && !device->flags.force_power_state) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Device is already at D%d\n",
				  state));
		return 0;
	}

	if (!device->power.states[state].flags.valid) {
		printk(KERN_WARNING PREFIX "Device does not support D%d\n", state);
		return -ENODEV;
	}
	if (device->parent && (state < device->parent->power.state)) {
		printk(KERN_WARNING PREFIX
			      "Cannot set device to a higher-powered"
			      " state than parent\n");
		return -ENODEV;
	}

	/*
	 * Transition Power
	 * ----------------
	 * On transitions to a high-powered state we first apply power (via
	 * power resources) then evalute _PSx.  Conversly for transitions to
	 * a lower-powered state.
	 */
	if (state < device->power.state) {
		if (device->power.flags.power_resources) {
			result = acpi_power_transition(device, state);
			if (result)
				goto end;
		}
		if (device->power.states[state].flags.explicit_set) {
			status = acpi_evaluate_object(device->handle,
						      object_name, NULL, NULL);
			if (ACPI_FAILURE(status)) {
				result = -ENODEV;
				goto end;
			}
		}
	} else {
		if (device->power.states[state].flags.explicit_set) {
			status = acpi_evaluate_object(device->handle,
						      object_name, NULL, NULL);
			if (ACPI_FAILURE(status)) {
				result = -ENODEV;
				goto end;
			}
		}
		if (device->power.flags.power_resources) {
			result = acpi_power_transition(device, state);
			if (result)
				goto end;
		}
	}

      end:
	if (result)
		printk(KERN_WARNING PREFIX
			      "Device [%s] failed to transition to D%d\n",
			      device->pnp.bus_id, state);
	else {
		device->power.state = state;
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Device [%s] transitioned to D%d\n",
				  device->pnp.bus_id, state));
	}

	return result;
}

EXPORT_SYMBOL(acpi_bus_set_power);

bool acpi_bus_power_manageable(acpi_handle handle)
{
	struct acpi_device *device;
	int result;

	result = acpi_bus_get_device(handle, &device);
	return result ? false : device->flags.power_manageable;
}

EXPORT_SYMBOL(acpi_bus_power_manageable);

bool acpi_bus_can_wakeup(acpi_handle handle)
{
	struct acpi_device *device;
	int result;

	result = acpi_bus_get_device(handle, &device);
	return result ? false : device->wakeup.flags.valid;
}

EXPORT_SYMBOL(acpi_bus_can_wakeup);

/* --------------------------------------------------------------------------
                                Event Management
   -------------------------------------------------------------------------- */

#ifdef CONFIG_ACPI_PROC_EVENT
static DEFINE_SPINLOCK(acpi_bus_event_lock);

LIST_HEAD(acpi_bus_event_list);
DECLARE_WAIT_QUEUE_HEAD(acpi_bus_event_queue);

extern int event_is_open;

int acpi_bus_generate_proc_event4(const char *device_class, const char *bus_id, u8 type, int data)
{
	struct acpi_bus_event *event;
	unsigned long flags = 0;

	/* drop event on the floor if no one's listening */
	if (!event_is_open)
		return 0;

	event = kmalloc(sizeof(struct acpi_bus_event), GFP_ATOMIC);
	if (!event)
		return -ENOMEM;

	strcpy(event->device_class, device_class);
	strcpy(event->bus_id, bus_id);
	event->type = type;
	event->data = data;

	spin_lock_irqsave(&acpi_bus_event_lock, flags);
	list_add_tail(&event->node, &acpi_bus_event_list);
	spin_unlock_irqrestore(&acpi_bus_event_lock, flags);

	wake_up_interruptible(&acpi_bus_event_queue);

	return 0;

}

EXPORT_SYMBOL_GPL(acpi_bus_generate_proc_event4);

int acpi_bus_generate_proc_event(struct acpi_device *device, u8 type, int data)
{
	if (!device)
		return -EINVAL;
	return acpi_bus_generate_proc_event4(device->pnp.device_class,
					     device->pnp.bus_id, type, data);
}

EXPORT_SYMBOL(acpi_bus_generate_proc_event);

int acpi_bus_receive_event(struct acpi_bus_event *event)
{
	unsigned long flags = 0;
	struct acpi_bus_event *entry = NULL;

	DECLARE_WAITQUEUE(wait, current);


	if (!event)
		return -EINVAL;

	if (list_empty(&acpi_bus_event_list)) {

		set_current_state(TASK_INTERRUPTIBLE);
		add_wait_queue(&acpi_bus_event_queue, &wait);

		if (list_empty(&acpi_bus_event_list))
			schedule();

		remove_wait_queue(&acpi_bus_event_queue, &wait);
		set_current_state(TASK_RUNNING);

		if (signal_pending(current))
			return -ERESTARTSYS;
	}

	spin_lock_irqsave(&acpi_bus_event_lock, flags);
	if (!list_empty(&acpi_bus_event_list)) {
		entry = list_entry(acpi_bus_event_list.next,
				   struct acpi_bus_event, node);
		list_del(&entry->node);
	}
	spin_unlock_irqrestore(&acpi_bus_event_lock, flags);

	if (!entry)
		return -ENODEV;

	memcpy(event, entry, sizeof(struct acpi_bus_event));

	kfree(entry);

	return 0;
}

#endif	/* CONFIG_ACPI_PROC_EVENT */

/* --------------------------------------------------------------------------
                             Notification Handling
   -------------------------------------------------------------------------- */

static int
acpi_bus_check_device(struct acpi_device *device, int *status_changed)
{
	acpi_status status = 0;
	struct acpi_device_status old_status;


	if (!device)
		return -EINVAL;

	if (status_changed)
		*status_changed = 0;

	old_status = device->status;

	/*
	 * Make sure this device's parent is present before we go about
	 * messing with the device.
	 */
	if (device->parent && !device->parent->status.present) {
		device->status = device->parent->status;
		if (STRUCT_TO_INT(old_status) != STRUCT_TO_INT(device->status)) {
			if (status_changed)
				*status_changed = 1;
		}
		return 0;
	}

	status = acpi_bus_get_status(device);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	if (STRUCT_TO_INT(old_status) == STRUCT_TO_INT(device->status))
		return 0;

	if (status_changed)
		*status_changed = 1;

	/*
	 * Device Insertion/Removal
	 */
	if ((device->status.present) && !(old_status.present)) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Device insertion detected\n"));
		/* TBD: Handle device insertion */
	} else if (!(device->status.present) && (old_status.present)) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Device removal detected\n"));
		/* TBD: Handle device removal */
	}

	return 0;
}

static int acpi_bus_check_scope(struct acpi_device *device)
{
	int result = 0;
	int status_changed = 0;


	if (!device)
		return -EINVAL;

	/* Status Change? */
	result = acpi_bus_check_device(device, &status_changed);
	if (result)
		return result;

	if (!status_changed)
		return 0;

	/*
	 * TBD: Enumerate child devices within this device's scope and
	 *       run acpi_bus_check_device()'s on them.
	 */

	return 0;
}

static BLOCKING_NOTIFIER_HEAD(acpi_bus_notify_list);
int register_acpi_bus_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&acpi_bus_notify_list, nb);
}
EXPORT_SYMBOL_GPL(register_acpi_bus_notifier);

void unregister_acpi_bus_notifier(struct notifier_block *nb)
{
	blocking_notifier_chain_unregister(&acpi_bus_notify_list, nb);
}
EXPORT_SYMBOL_GPL(unregister_acpi_bus_notifier);

/**
 * acpi_bus_notify
 * ---------------
 * Callback for all 'system-level' device notifications (values 0x00-0x7F).
 */
static void acpi_bus_notify(acpi_handle handle, u32 type, void *data)
{
	int result = 0;
	struct acpi_device *device = NULL;

	blocking_notifier_call_chain(&acpi_bus_notify_list,
		type, (void *)handle);

	if (acpi_bus_get_device(handle, &device))
		return;

	switch (type) {

	case ACPI_NOTIFY_BUS_CHECK:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Received BUS CHECK notification for device [%s]\n",
				  device->pnp.bus_id));
		result = acpi_bus_check_scope(device);
		/*
		 * TBD: We'll need to outsource certain events to non-ACPI
		 *      drivers via the device manager (device.c).
		 */
		break;

	case ACPI_NOTIFY_DEVICE_CHECK:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Received DEVICE CHECK notification for device [%s]\n",
				  device->pnp.bus_id));
		result = acpi_bus_check_device(device, NULL);
		/*
		 * TBD: We'll need to outsource certain events to non-ACPI
		 *      drivers via the device manager (device.c).
		 */
		break;

	case ACPI_NOTIFY_DEVICE_WAKE:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Received DEVICE WAKE notification for device [%s]\n",
				  device->pnp.bus_id));
		/* TBD */
		break;

	case ACPI_NOTIFY_EJECT_REQUEST:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Received EJECT REQUEST notification for device [%s]\n",
				  device->pnp.bus_id));
		/* TBD */
		break;

	case ACPI_NOTIFY_DEVICE_CHECK_LIGHT:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Received DEVICE CHECK LIGHT notification for device [%s]\n",
				  device->pnp.bus_id));
		/* TBD: Exactly what does 'light' mean? */
		break;

	case ACPI_NOTIFY_FREQUENCY_MISMATCH:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Received FREQUENCY MISMATCH notification for device [%s]\n",
				  device->pnp.bus_id));
		/* TBD */
		break;

	case ACPI_NOTIFY_BUS_MODE_MISMATCH:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Received BUS MODE MISMATCH notification for device [%s]\n",
				  device->pnp.bus_id));
		/* TBD */
		break;

	case ACPI_NOTIFY_POWER_FAULT:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Received POWER FAULT notification for device [%s]\n",
				  device->pnp.bus_id));
		/* TBD */
		break;

	default:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Received unknown/unsupported notification [%08x]\n",
				  type));
		break;
	}

	return;
}

/* --------------------------------------------------------------------------
                             Initialization/Cleanup
   -------------------------------------------------------------------------- */

static int __init acpi_bus_init_irq(void)
{
	acpi_status status = AE_OK;
	union acpi_object arg = { ACPI_TYPE_INTEGER };
	struct acpi_object_list arg_list = { 1, &arg };
	char *message = NULL;


	/*
	 * Let the system know what interrupt model we are using by
	 * evaluating the \_PIC object, if exists.
	 */

	switch (acpi_irq_model) {
	case ACPI_IRQ_MODEL_PIC:
		message = "PIC";
		break;
	case ACPI_IRQ_MODEL_IOAPIC:
		message = "IOAPIC";
		break;
	case ACPI_IRQ_MODEL_IOSAPIC:
		message = "IOSAPIC";
		break;
	case ACPI_IRQ_MODEL_PLATFORM:
		message = "platform specific model";
		break;
	default:
		printk(KERN_WARNING PREFIX "Unknown interrupt routing model\n");
		return -ENODEV;
	}

	printk(KERN_INFO PREFIX "Using %s for interrupt routing\n", message);

	arg.integer.value = acpi_irq_model;

	status = acpi_evaluate_object(NULL, "\\_PIC", &arg_list, NULL);
	if (ACPI_FAILURE(status) && (status != AE_NOT_FOUND)) {
		ACPI_EXCEPTION((AE_INFO, status, "Evaluating _PIC"));
		return -ENODEV;
	}

	return 0;
}

u8 acpi_gbl_permanent_mmap;


void __init acpi_early_init(void)
{
	acpi_status status = AE_OK;

	if (acpi_disabled)
		return;

	printk(KERN_INFO PREFIX "Core revision %08x\n", ACPI_CA_VERSION);

	/* enable workarounds, unless strict ACPI spec. compliance */
	if (!acpi_strict)
		acpi_gbl_enable_interpreter_slack = TRUE;

	acpi_gbl_permanent_mmap = 1;

	status = acpi_reallocate_root_table();
	if (ACPI_FAILURE(status)) {
		printk(KERN_ERR PREFIX
		       "Unable to reallocate ACPI tables\n");
		goto error0;
	}

	status = acpi_initialize_subsystem();
	if (ACPI_FAILURE(status)) {
		printk(KERN_ERR PREFIX
		       "Unable to initialize the ACPI Interpreter\n");
		goto error0;
	}

	status = acpi_load_tables();
	if (ACPI_FAILURE(status)) {
		printk(KERN_ERR PREFIX
		       "Unable to load the System Description Tables\n");
		goto error0;
	}

#ifdef CONFIG_X86
	if (!acpi_ioapic) {
		/* compatible (0) means level (3) */
		if (!(acpi_sci_flags & ACPI_MADT_TRIGGER_MASK)) {
			acpi_sci_flags &= ~ACPI_MADT_TRIGGER_MASK;
			acpi_sci_flags |= ACPI_MADT_TRIGGER_LEVEL;
		}
		/* Set PIC-mode SCI trigger type */
		acpi_pic_sci_set_trigger(acpi_gbl_FADT.sci_interrupt,
					 (acpi_sci_flags & ACPI_MADT_TRIGGER_MASK) >> 2);
	} else {
		/*
		 * now that acpi_gbl_FADT is initialized,
		 * update it with result from INT_SRC_OVR parsing
		 */
		acpi_gbl_FADT.sci_interrupt = acpi_sci_override_gsi;
	}
#endif

	status =
	    acpi_enable_subsystem(~
				  (ACPI_NO_HARDWARE_INIT |
				   ACPI_NO_ACPI_ENABLE));
	if (ACPI_FAILURE(status)) {
		printk(KERN_ERR PREFIX "Unable to enable ACPI\n");
		goto error0;
	}

	return;

      error0:
	disable_acpi();
	return;
}

static int __init acpi_bus_init(void)
{
	int result = 0;
	acpi_status status = AE_OK;
	extern acpi_status acpi_os_initialize1(void);

	acpi_os_initialize1();

	status =
	    acpi_enable_subsystem(ACPI_NO_HARDWARE_INIT | ACPI_NO_ACPI_ENABLE);
	if (ACPI_FAILURE(status)) {
		printk(KERN_ERR PREFIX
		       "Unable to start the ACPI Interpreter\n");
		goto error1;
	}

	/*
	 * ACPI 2.0 requires the EC driver to be loaded and work before
	 * the EC device is found in the namespace (i.e. before acpi_initialize_objects()
	 * is called).
	 *
	 * This is accomplished by looking for the ECDT table, and getting
	 * the EC parameters out of that.
	 */
	status = acpi_ec_ecdt_probe();
	/* Ignore result. Not having an ECDT is not fatal. */

	status = acpi_initialize_objects(ACPI_FULL_INITIALIZATION);
	if (ACPI_FAILURE(status)) {
		printk(KERN_ERR PREFIX "Unable to initialize ACPI objects\n");
		goto error1;
	}

	/*
	 * Maybe EC region is required at bus_scan/acpi_get_devices. So it
	 * is necessary to enable it as early as possible.
	 */
	acpi_boot_ec_enable();

	printk(KERN_INFO PREFIX "Interpreter enabled\n");

	/* Initialize sleep structures */
	acpi_sleep_init();

	/*
	 * Get the system interrupt model and evaluate \_PIC.
	 */
	result = acpi_bus_init_irq();
	if (result)
		goto error1;

	/*
	 * Register the for all standard device notifications.
	 */
	status =
	    acpi_install_notify_handler(ACPI_ROOT_OBJECT, ACPI_SYSTEM_NOTIFY,
					&acpi_bus_notify, NULL);
	if (ACPI_FAILURE(status)) {
		printk(KERN_ERR PREFIX
		       "Unable to register for device notifications\n");
		goto error1;
	}

	/*
	 * Create the top ACPI proc directory
	 */
	acpi_root_dir = proc_mkdir(ACPI_BUS_FILE_ROOT, NULL);

	return 0;

	/* Mimic structured exception handling */
      error1:
	acpi_terminate();
	return -ENODEV;
}

struct kobject *acpi_kobj;

static int __init acpi_init(void)
{
	int result = 0;


	if (acpi_disabled) {
		printk(KERN_INFO PREFIX "Interpreter disabled.\n");
		return -ENODEV;
	}

	acpi_kobj = kobject_create_and_add("acpi", firmware_kobj);
	if (!acpi_kobj) {
		printk(KERN_WARNING "%s: kset create error\n", __func__);
		acpi_kobj = NULL;
	}

	init_acpi_device_notify();
	result = acpi_bus_init();

	if (!result) {
		pci_mmcfg_late_init();
		if (!(pm_flags & PM_APM))
			pm_flags |= PM_ACPI;
		else {
			printk(KERN_INFO PREFIX
			       "APM is already active, exiting\n");
			disable_acpi();
			result = -ENODEV;
		}
	} else
		disable_acpi();

	if (acpi_disabled)
		return result;

	/*
	 * If the laptop falls into the DMI check table, the power state check
	 * will be disabled in the course of device power transistion.
	 */
	dmi_check_system(power_nocheck_dmi_table);

	acpi_scan_init();
	acpi_ec_init();
	acpi_power_init();
	acpi_system_init();
	acpi_debug_init();
	acpi_sleep_proc_init();
	acpi_wakeup_device_init();
	return result;
}

subsys_initcall(acpi_init);
