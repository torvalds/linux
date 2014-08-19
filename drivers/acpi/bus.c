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
#include <linux/slab.h>
#include <linux/regulator/machine.h>
#ifdef CONFIG_X86
#include <asm/mpspec.h>
#endif
#include <linux/pci.h>
#include <acpi/apei.h>
#include <linux/dmi.h>
#include <linux/suspend.h>

#include "internal.h"

#define _COMPONENT		ACPI_BUS_COMPONENT
ACPI_MODULE_NAME("bus");

struct acpi_device *acpi_root;
struct proc_dir_entry *acpi_root_dir;
EXPORT_SYMBOL(acpi_root_dir);

#ifdef CONFIG_X86
#ifdef CONFIG_ACPI_CUSTOM_DSDT
static inline int set_copy_dsdt(const struct dmi_system_id *id)
{
	return 0;
}
#else
static int set_copy_dsdt(const struct dmi_system_id *id)
{
	printk(KERN_NOTICE "%s detected - "
		"force copy of DSDT to local memory\n", id->ident);
	acpi_gbl_copy_dsdt_locally = 1;
	return 0;
}
#endif

static struct dmi_system_id dsdt_dmi_table[] __initdata = {
	/*
	 * Invoke DSDT corruption work-around on all Toshiba Satellite.
	 * https://bugzilla.kernel.org/show_bug.cgi?id=14679
	 */
	{
	 .callback = set_copy_dsdt,
	 .ident = "TOSHIBA Satellite",
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
		DMI_MATCH(DMI_PRODUCT_NAME, "Satellite"),
		},
	},
	{}
};
#else
static struct dmi_system_id dsdt_dmi_table[] __initdata = {
	{}
};
#endif

/* --------------------------------------------------------------------------
                                Device Management
   -------------------------------------------------------------------------- */

acpi_status acpi_bus_get_status_handle(acpi_handle handle,
				       unsigned long long *sta)
{
	acpi_status status;

	status = acpi_evaluate_integer(handle, "_STA", NULL, sta);
	if (ACPI_SUCCESS(status))
		return AE_OK;

	if (status == AE_NOT_FOUND) {
		*sta = ACPI_STA_DEVICE_PRESENT | ACPI_STA_DEVICE_ENABLED |
		       ACPI_STA_DEVICE_UI      | ACPI_STA_DEVICE_FUNCTIONING;
		return AE_OK;
	}
	return status;
}

int acpi_bus_get_status(struct acpi_device *device)
{
	acpi_status status;
	unsigned long long sta;

	status = acpi_bus_get_status_handle(device->handle, &sta);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	acpi_set_device_status(device, sta);

	if (device->status.functional && !device->status.present) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Device [%s] status [%08x]: "
		       "functional but not present;\n",
			device->pnp.bus_id, (u32)sta));
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Device [%s] status [%08x]\n",
			  device->pnp.bus_id, (u32)sta));
	return 0;
}
EXPORT_SYMBOL(acpi_bus_get_status);

void acpi_bus_private_data_handler(acpi_handle handle,
				   void *context)
{
	return;
}
EXPORT_SYMBOL(acpi_bus_private_data_handler);

int acpi_bus_attach_private_data(acpi_handle handle, void *data)
{
	acpi_status status;

	status = acpi_attach_data(handle,
			acpi_bus_private_data_handler, data);
	if (ACPI_FAILURE(status)) {
		acpi_handle_debug(handle, "Error attaching device data\n");
		return -ENODEV;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(acpi_bus_attach_private_data);

int acpi_bus_get_private_data(acpi_handle handle, void **data)
{
	acpi_status status;

	if (!*data)
		return -EINVAL;

	status = acpi_get_data(handle, acpi_bus_private_data_handler, data);
	if (ACPI_FAILURE(status)) {
		acpi_handle_debug(handle, "No context for object\n");
		return -ENODEV;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(acpi_bus_get_private_data);

void acpi_bus_detach_private_data(acpi_handle handle)
{
	acpi_detach_data(handle, acpi_bus_private_data_handler);
}
EXPORT_SYMBOL_GPL(acpi_bus_detach_private_data);

void acpi_bus_no_hotplug(acpi_handle handle)
{
	struct acpi_device *adev = NULL;

	acpi_bus_get_device(handle, &adev);
	if (adev)
		adev->flags.no_hotplug = true;
}
EXPORT_SYMBOL_GPL(acpi_bus_no_hotplug);

static void acpi_print_osc_error(acpi_handle handle,
	struct acpi_osc_context *context, char *error)
{
	struct acpi_buffer buffer = {ACPI_ALLOCATE_BUFFER};
	int i;

	if (ACPI_FAILURE(acpi_get_name(handle, ACPI_FULL_PATHNAME, &buffer)))
		printk(KERN_DEBUG "%s\n", error);
	else {
		printk(KERN_DEBUG "%s:%s\n", (char *)buffer.pointer, error);
		kfree(buffer.pointer);
	}
	printk(KERN_DEBUG"_OSC request data:");
	for (i = 0; i < context->cap.length; i += sizeof(u32))
		printk("%x ", *((u32 *)(context->cap.pointer + i)));
	printk("\n");
}

acpi_status acpi_str_to_uuid(char *str, u8 *uuid)
{
	int i;
	static int opc_map_to_uuid[16] = {6, 4, 2, 0, 11, 9, 16, 14, 19, 21,
		24, 26, 28, 30, 32, 34};

	if (strlen(str) != 36)
		return AE_BAD_PARAMETER;
	for (i = 0; i < 36; i++) {
		if (i == 8 || i == 13 || i == 18 || i == 23) {
			if (str[i] != '-')
				return AE_BAD_PARAMETER;
		} else if (!isxdigit(str[i]))
			return AE_BAD_PARAMETER;
	}
	for (i = 0; i < 16; i++) {
		uuid[i] = hex_to_bin(str[opc_map_to_uuid[i]]) << 4;
		uuid[i] |= hex_to_bin(str[opc_map_to_uuid[i] + 1]);
	}
	return AE_OK;
}
EXPORT_SYMBOL_GPL(acpi_str_to_uuid);

acpi_status acpi_run_osc(acpi_handle handle, struct acpi_osc_context *context)
{
	acpi_status status;
	struct acpi_object_list input;
	union acpi_object in_params[4];
	union acpi_object *out_obj;
	u8 uuid[16];
	u32 errors;
	struct acpi_buffer output = {ACPI_ALLOCATE_BUFFER, NULL};

	if (!context)
		return AE_ERROR;
	if (ACPI_FAILURE(acpi_str_to_uuid(context->uuid_str, uuid)))
		return AE_ERROR;
	context->ret.length = ACPI_ALLOCATE_BUFFER;
	context->ret.pointer = NULL;

	/* Setting up input parameters */
	input.count = 4;
	input.pointer = in_params;
	in_params[0].type 		= ACPI_TYPE_BUFFER;
	in_params[0].buffer.length 	= 16;
	in_params[0].buffer.pointer	= uuid;
	in_params[1].type 		= ACPI_TYPE_INTEGER;
	in_params[1].integer.value 	= context->rev;
	in_params[2].type 		= ACPI_TYPE_INTEGER;
	in_params[2].integer.value	= context->cap.length/sizeof(u32);
	in_params[3].type		= ACPI_TYPE_BUFFER;
	in_params[3].buffer.length 	= context->cap.length;
	in_params[3].buffer.pointer 	= context->cap.pointer;

	status = acpi_evaluate_object(handle, "_OSC", &input, &output);
	if (ACPI_FAILURE(status))
		return status;

	if (!output.length)
		return AE_NULL_OBJECT;

	out_obj = output.pointer;
	if (out_obj->type != ACPI_TYPE_BUFFER
		|| out_obj->buffer.length != context->cap.length) {
		acpi_print_osc_error(handle, context,
			"_OSC evaluation returned wrong type");
		status = AE_TYPE;
		goto out_kfree;
	}
	/* Need to ignore the bit0 in result code */
	errors = *((u32 *)out_obj->buffer.pointer) & ~(1 << 0);
	if (errors) {
		if (errors & OSC_REQUEST_ERROR)
			acpi_print_osc_error(handle, context,
				"_OSC request failed");
		if (errors & OSC_INVALID_UUID_ERROR)
			acpi_print_osc_error(handle, context,
				"_OSC invalid UUID");
		if (errors & OSC_INVALID_REVISION_ERROR)
			acpi_print_osc_error(handle, context,
				"_OSC invalid revision");
		if (errors & OSC_CAPABILITIES_MASK_ERROR) {
			if (((u32 *)context->cap.pointer)[OSC_QUERY_DWORD]
			    & OSC_QUERY_ENABLE)
				goto out_success;
			status = AE_SUPPORT;
			goto out_kfree;
		}
		status = AE_ERROR;
		goto out_kfree;
	}
out_success:
	context->ret.length = out_obj->buffer.length;
	context->ret.pointer = kmemdup(out_obj->buffer.pointer,
				       context->ret.length, GFP_KERNEL);
	if (!context->ret.pointer) {
		status =  AE_NO_MEMORY;
		goto out_kfree;
	}
	status =  AE_OK;

out_kfree:
	kfree(output.pointer);
	if (status != AE_OK)
		context->ret.pointer = NULL;
	return status;
}
EXPORT_SYMBOL(acpi_run_osc);

bool osc_sb_apei_support_acked;
static u8 sb_uuid_str[] = "0811B06E-4A27-44F9-8D60-3CBBC22E7B48";
static void acpi_bus_osc_support(void)
{
	u32 capbuf[2];
	struct acpi_osc_context context = {
		.uuid_str = sb_uuid_str,
		.rev = 1,
		.cap.length = 8,
		.cap.pointer = capbuf,
	};
	acpi_handle handle;

	capbuf[OSC_QUERY_DWORD] = OSC_QUERY_ENABLE;
	capbuf[OSC_SUPPORT_DWORD] = OSC_SB_PR3_SUPPORT; /* _PR3 is in use */
#if defined(CONFIG_ACPI_PROCESSOR_AGGREGATOR) ||\
			defined(CONFIG_ACPI_PROCESSOR_AGGREGATOR_MODULE)
	capbuf[OSC_SUPPORT_DWORD] |= OSC_SB_PAD_SUPPORT;
#endif

#if defined(CONFIG_ACPI_PROCESSOR) || defined(CONFIG_ACPI_PROCESSOR_MODULE)
	capbuf[OSC_SUPPORT_DWORD] |= OSC_SB_PPC_OST_SUPPORT;
#endif

	capbuf[OSC_SUPPORT_DWORD] |= OSC_SB_HOTPLUG_OST_SUPPORT;

	if (!ghes_disable)
		capbuf[OSC_SUPPORT_DWORD] |= OSC_SB_APEI_SUPPORT;
	if (ACPI_FAILURE(acpi_get_handle(NULL, "\\_SB", &handle)))
		return;
	if (ACPI_SUCCESS(acpi_run_osc(handle, &context))) {
		u32 *capbuf_ret = context.ret.pointer;
		if (context.ret.length > OSC_SUPPORT_DWORD)
			osc_sb_apei_support_acked =
				capbuf_ret[OSC_SUPPORT_DWORD] & OSC_SB_APEI_SUPPORT;
		kfree(context.ret.pointer);
	}
	/* do we need to check other returned cap? Sounds no */
}

/* --------------------------------------------------------------------------
                             Notification Handling
   -------------------------------------------------------------------------- */

/**
 * acpi_bus_notify
 * ---------------
 * Callback for all 'system-level' device notifications (values 0x00-0x7F).
 */
static void acpi_bus_notify(acpi_handle handle, u32 type, void *data)
{
	struct acpi_device *adev;
	struct acpi_driver *driver;
	u32 ost_code = ACPI_OST_SC_NON_SPECIFIC_FAILURE;
	bool hotplug_event = false;

	switch (type) {
	case ACPI_NOTIFY_BUS_CHECK:
		acpi_handle_debug(handle, "ACPI_NOTIFY_BUS_CHECK event\n");
		hotplug_event = true;
		break;

	case ACPI_NOTIFY_DEVICE_CHECK:
		acpi_handle_debug(handle, "ACPI_NOTIFY_DEVICE_CHECK event\n");
		hotplug_event = true;
		break;

	case ACPI_NOTIFY_DEVICE_WAKE:
		acpi_handle_debug(handle, "ACPI_NOTIFY_DEVICE_WAKE event\n");
		break;

	case ACPI_NOTIFY_EJECT_REQUEST:
		acpi_handle_debug(handle, "ACPI_NOTIFY_EJECT_REQUEST event\n");
		hotplug_event = true;
		break;

	case ACPI_NOTIFY_DEVICE_CHECK_LIGHT:
		acpi_handle_debug(handle, "ACPI_NOTIFY_DEVICE_CHECK_LIGHT event\n");
		/* TBD: Exactly what does 'light' mean? */
		break;

	case ACPI_NOTIFY_FREQUENCY_MISMATCH:
		acpi_handle_err(handle, "Device cannot be configured due "
				"to a frequency mismatch\n");
		break;

	case ACPI_NOTIFY_BUS_MODE_MISMATCH:
		acpi_handle_err(handle, "Device cannot be configured due "
				"to a bus mode mismatch\n");
		break;

	case ACPI_NOTIFY_POWER_FAULT:
		acpi_handle_err(handle, "Device has suffered a power fault\n");
		break;

	default:
		acpi_handle_debug(handle, "Unknown event type 0x%x\n", type);
		break;
	}

	adev = acpi_bus_get_acpi_device(handle);
	if (!adev)
		goto err;

	driver = adev->driver;
	if (driver && driver->ops.notify &&
	    (driver->flags & ACPI_DRIVER_ALL_NOTIFY_EVENTS))
		driver->ops.notify(adev, type);

	if (hotplug_event && ACPI_SUCCESS(acpi_hotplug_schedule(adev, type)))
		return;

	acpi_bus_put_acpi_device(adev);
	return;

 err:
	acpi_evaluate_ost(handle, type, ost_code, NULL);
}

/* --------------------------------------------------------------------------
                             Initialization/Cleanup
   -------------------------------------------------------------------------- */

static int __init acpi_bus_init_irq(void)
{
	acpi_status status;
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

	status = acpi_execute_simple_method(NULL, "\\_PIC", acpi_irq_model);
	if (ACPI_FAILURE(status) && (status != AE_NOT_FOUND)) {
		ACPI_EXCEPTION((AE_INFO, status, "Evaluating _PIC"));
		return -ENODEV;
	}

	return 0;
}

void __init acpi_early_init(void)
{
	acpi_status status;

	if (acpi_disabled)
		return;

	printk(KERN_INFO PREFIX "Core revision %08x\n", ACPI_CA_VERSION);

	/* It's safe to verify table checksums during late stage */
	acpi_gbl_verify_table_checksum = TRUE;

	/* enable workarounds, unless strict ACPI spec. compliance */
	if (!acpi_strict)
		acpi_gbl_enable_interpreter_slack = TRUE;

	acpi_gbl_permanent_mmap = 1;

	/*
	 * If the machine falls into the DMI check table,
	 * DSDT will be copied to memory
	 */
	dmi_check_system(dsdt_dmi_table);

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

	status = acpi_enable_subsystem(~ACPI_NO_ACPI_ENABLE);
	if (ACPI_FAILURE(status)) {
		printk(KERN_ERR PREFIX "Unable to enable ACPI\n");
		goto error0;
	}

	/*
	 * If the system is using ACPI then we can be reasonably
	 * confident that any regulators are managed by the firmware
	 * so tell the regulator core it has everything it needs to
	 * know.
	 */
	regulator_has_full_constraints();

	return;

      error0:
	disable_acpi();
	return;
}

static int __init acpi_bus_init(void)
{
	int result;
	acpi_status status;

	acpi_os_initialize1();

	status = acpi_enable_subsystem(ACPI_NO_ACPI_ENABLE);
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
	 * _OSC method may exist in module level code,
	 * so it must be run after ACPI_FULL_INITIALIZATION
	 */
	acpi_bus_osc_support();

	/*
	 * _PDC control method may load dynamic SSDT tables,
	 * and we need to install the table handler before that.
	 */
	acpi_sysfs_init();

	acpi_early_processor_set_pdc();

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
EXPORT_SYMBOL_GPL(acpi_kobj);

static int __init acpi_init(void)
{
	int result;

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
	if (result) {
		disable_acpi();
		return result;
	}

	pci_mmcfg_late_init();
	acpi_scan_init();
	acpi_ec_init();
	acpi_debugfs_init();
	acpi_sleep_proc_init();
	acpi_wakeup_device_init();
	return 0;
}

subsys_initcall(acpi_init);
