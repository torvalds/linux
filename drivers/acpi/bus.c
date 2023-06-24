// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  acpi_bus.c - ACPI Bus Driver ($Revision: 80 $)
 *
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 */

#define pr_fmt(fmt) "ACPI: " fmt

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
#include <linux/workqueue.h>
#include <linux/reboot.h>
#include <linux/delay.h>
#ifdef CONFIG_X86
#include <asm/mpspec.h>
#include <linux/dmi.h>
#endif
#include <linux/acpi_agdi.h>
#include <linux/acpi_iort.h>
#include <linux/acpi_viot.h>
#include <linux/pci.h>
#include <acpi/apei.h>
#include <linux/suspend.h>
#include <linux/prmt.h>

#include "internal.h"

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
	pr_notice("%s detected - force copy of DSDT to local memory\n", id->ident);
	acpi_gbl_copy_dsdt_locally = 1;
	return 0;
}
#endif

static const struct dmi_system_id dsdt_dmi_table[] __initconst = {
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
EXPORT_SYMBOL_GPL(acpi_bus_get_status_handle);

int acpi_bus_get_status(struct acpi_device *device)
{
	acpi_status status;
	unsigned long long sta;

	if (acpi_device_override_status(device, &sta)) {
		acpi_set_device_status(device, sta);
		return 0;
	}

	/* Battery devices must have their deps met before calling _STA */
	if (acpi_device_is_battery(device) && device->dep_unmet) {
		acpi_set_device_status(device, 0);
		return 0;
	}

	status = acpi_bus_get_status_handle(device->handle, &sta);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	acpi_set_device_status(device, sta);

	if (device->status.functional && !device->status.present) {
		pr_debug("Device [%s] status [%08x]: functional but not present\n",
			 device->pnp.bus_id, (u32)sta);
	}

	pr_debug("Device [%s] status [%08x]\n", device->pnp.bus_id, (u32)sta);
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

	if (!data)
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

static void acpi_print_osc_error(acpi_handle handle,
				 struct acpi_osc_context *context, char *error)
{
	int i;

	acpi_handle_debug(handle, "(%s): %s\n", context->uuid_str, error);

	pr_debug("_OSC request data:");
	for (i = 0; i < context->cap.length; i += sizeof(u32))
		pr_debug(" %x", *((u32 *)(context->cap.pointer + i)));

	pr_debug("\n");
}

acpi_status acpi_run_osc(acpi_handle handle, struct acpi_osc_context *context)
{
	acpi_status status;
	struct acpi_object_list input;
	union acpi_object in_params[4];
	union acpi_object *out_obj;
	guid_t guid;
	u32 errors;
	struct acpi_buffer output = {ACPI_ALLOCATE_BUFFER, NULL};

	if (!context)
		return AE_ERROR;
	if (guid_parse(context->uuid_str, &guid))
		return AE_ERROR;
	context->ret.length = ACPI_ALLOCATE_BUFFER;
	context->ret.pointer = NULL;

	/* Setting up input parameters */
	input.count = 4;
	input.pointer = in_params;
	in_params[0].type 		= ACPI_TYPE_BUFFER;
	in_params[0].buffer.length 	= 16;
	in_params[0].buffer.pointer	= (u8 *)&guid;
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
	return status;
}
EXPORT_SYMBOL(acpi_run_osc);

bool osc_sb_apei_support_acked;

/*
 * ACPI 6.0 Section 8.4.4.2 Idle State Coordination
 * OSPM supports platform coordinated low power idle(LPI) states
 */
bool osc_pc_lpi_support_confirmed;
EXPORT_SYMBOL_GPL(osc_pc_lpi_support_confirmed);

/*
 * ACPI 6.2 Section 6.2.11.2 'Platform-Wide OSPM Capabilities':
 *   Starting with ACPI Specification 6.2, all _CPC registers can be in
 *   PCC, System Memory, System IO, or Functional Fixed Hardware address
 *   spaces. OSPM support for this more flexible register space scheme is
 *   indicated by the “Flexible Address Space for CPPC Registers” _OSC bit.
 *
 * Otherwise (cf ACPI 6.1, s8.4.7.1.1.X), _CPC registers must be in:
 * - PCC or Functional Fixed Hardware address space if defined
 * - SystemMemory address space (NULL register) if not defined
 */
bool osc_cpc_flexible_adr_space_confirmed;
EXPORT_SYMBOL_GPL(osc_cpc_flexible_adr_space_confirmed);

/*
 * ACPI 6.4 Operating System Capabilities for USB.
 */
bool osc_sb_native_usb4_support_confirmed;
EXPORT_SYMBOL_GPL(osc_sb_native_usb4_support_confirmed);

bool osc_sb_cppc2_support_acked;

static u8 sb_uuid_str[] = "0811B06E-4A27-44F9-8D60-3CBBC22E7B48";
static void acpi_bus_osc_negotiate_platform_control(void)
{
	u32 capbuf[2], *capbuf_ret;
	struct acpi_osc_context context = {
		.uuid_str = sb_uuid_str,
		.rev = 1,
		.cap.length = 8,
		.cap.pointer = capbuf,
	};
	acpi_handle handle;

	capbuf[OSC_QUERY_DWORD] = OSC_QUERY_ENABLE;
	capbuf[OSC_SUPPORT_DWORD] = OSC_SB_PR3_SUPPORT; /* _PR3 is in use */
	if (IS_ENABLED(CONFIG_ACPI_PROCESSOR_AGGREGATOR))
		capbuf[OSC_SUPPORT_DWORD] |= OSC_SB_PAD_SUPPORT;
	if (IS_ENABLED(CONFIG_ACPI_PROCESSOR))
		capbuf[OSC_SUPPORT_DWORD] |= OSC_SB_PPC_OST_SUPPORT;

	capbuf[OSC_SUPPORT_DWORD] |= OSC_SB_HOTPLUG_OST_SUPPORT;
	capbuf[OSC_SUPPORT_DWORD] |= OSC_SB_PCLPI_SUPPORT;
	if (IS_ENABLED(CONFIG_ACPI_PRMT))
		capbuf[OSC_SUPPORT_DWORD] |= OSC_SB_PRM_SUPPORT;

#ifdef CONFIG_ARM64
	capbuf[OSC_SUPPORT_DWORD] |= OSC_SB_GENERIC_INITIATOR_SUPPORT;
#endif
#ifdef CONFIG_X86
	capbuf[OSC_SUPPORT_DWORD] |= OSC_SB_GENERIC_INITIATOR_SUPPORT;
#endif

#ifdef CONFIG_ACPI_CPPC_LIB
	capbuf[OSC_SUPPORT_DWORD] |= OSC_SB_CPC_SUPPORT;
	capbuf[OSC_SUPPORT_DWORD] |= OSC_SB_CPCV2_SUPPORT;
#endif

	capbuf[OSC_SUPPORT_DWORD] |= OSC_SB_CPC_FLEXIBLE_ADR_SPACE;

	if (IS_ENABLED(CONFIG_SCHED_MC_PRIO))
		capbuf[OSC_SUPPORT_DWORD] |= OSC_SB_CPC_DIVERSE_HIGH_SUPPORT;

	if (IS_ENABLED(CONFIG_USB4))
		capbuf[OSC_SUPPORT_DWORD] |= OSC_SB_NATIVE_USB4_SUPPORT;

	if (!ghes_disable)
		capbuf[OSC_SUPPORT_DWORD] |= OSC_SB_APEI_SUPPORT;
	if (ACPI_FAILURE(acpi_get_handle(NULL, "\\_SB", &handle)))
		return;

	if (ACPI_FAILURE(acpi_run_osc(handle, &context)))
		return;

	capbuf_ret = context.ret.pointer;
	if (context.ret.length <= OSC_SUPPORT_DWORD) {
		kfree(context.ret.pointer);
		return;
	}

	/*
	 * Now run _OSC again with query flag clear and with the caps
	 * supported by both the OS and the platform.
	 */
	capbuf[OSC_QUERY_DWORD] = 0;
	capbuf[OSC_SUPPORT_DWORD] = capbuf_ret[OSC_SUPPORT_DWORD];
	kfree(context.ret.pointer);

	if (ACPI_FAILURE(acpi_run_osc(handle, &context)))
		return;

	capbuf_ret = context.ret.pointer;
	if (context.ret.length > OSC_SUPPORT_DWORD) {
#ifdef CONFIG_ACPI_CPPC_LIB
		osc_sb_cppc2_support_acked = capbuf_ret[OSC_SUPPORT_DWORD] & OSC_SB_CPCV2_SUPPORT;
#endif

		osc_sb_apei_support_acked =
			capbuf_ret[OSC_SUPPORT_DWORD] & OSC_SB_APEI_SUPPORT;
		osc_pc_lpi_support_confirmed =
			capbuf_ret[OSC_SUPPORT_DWORD] & OSC_SB_PCLPI_SUPPORT;
		osc_sb_native_usb4_support_confirmed =
			capbuf_ret[OSC_SUPPORT_DWORD] & OSC_SB_NATIVE_USB4_SUPPORT;
		osc_cpc_flexible_adr_space_confirmed =
			capbuf_ret[OSC_SUPPORT_DWORD] & OSC_SB_CPC_FLEXIBLE_ADR_SPACE;
	}

	kfree(context.ret.pointer);
}

/*
 * Native control of USB4 capabilities. If any of the tunneling bits is
 * set it means OS is in control and we use software based connection
 * manager.
 */
u32 osc_sb_native_usb4_control;
EXPORT_SYMBOL_GPL(osc_sb_native_usb4_control);

static void acpi_bus_decode_usb_osc(const char *msg, u32 bits)
{
	pr_info("%s USB3%c DisplayPort%c PCIe%c XDomain%c\n", msg,
	       (bits & OSC_USB_USB3_TUNNELING) ? '+' : '-',
	       (bits & OSC_USB_DP_TUNNELING) ? '+' : '-',
	       (bits & OSC_USB_PCIE_TUNNELING) ? '+' : '-',
	       (bits & OSC_USB_XDOMAIN) ? '+' : '-');
}

static u8 sb_usb_uuid_str[] = "23A0D13A-26AB-486C-9C5F-0FFA525A575A";
static void acpi_bus_osc_negotiate_usb_control(void)
{
	u32 capbuf[3];
	struct acpi_osc_context context = {
		.uuid_str = sb_usb_uuid_str,
		.rev = 1,
		.cap.length = sizeof(capbuf),
		.cap.pointer = capbuf,
	};
	acpi_handle handle;
	acpi_status status;
	u32 control;

	if (!osc_sb_native_usb4_support_confirmed)
		return;

	if (ACPI_FAILURE(acpi_get_handle(NULL, "\\_SB", &handle)))
		return;

	control = OSC_USB_USB3_TUNNELING | OSC_USB_DP_TUNNELING |
		  OSC_USB_PCIE_TUNNELING | OSC_USB_XDOMAIN;

	capbuf[OSC_QUERY_DWORD] = 0;
	capbuf[OSC_SUPPORT_DWORD] = 0;
	capbuf[OSC_CONTROL_DWORD] = control;

	status = acpi_run_osc(handle, &context);
	if (ACPI_FAILURE(status))
		return;

	if (context.ret.length != sizeof(capbuf)) {
		pr_info("USB4 _OSC: returned invalid length buffer\n");
		goto out_free;
	}

	osc_sb_native_usb4_control =
		control &  acpi_osc_ctx_get_pci_control(&context);

	acpi_bus_decode_usb_osc("USB4 _OSC: OS supports", control);
	acpi_bus_decode_usb_osc("USB4 _OSC: OS controls",
				osc_sb_native_usb4_control);

out_free:
	kfree(context.ret.pointer);
}

/* --------------------------------------------------------------------------
                             Notification Handling
   -------------------------------------------------------------------------- */

/*
 * acpi_bus_notify
 * ---------------
 * Callback for all 'system-level' device notifications (values 0x00-0x7F).
 */
static void acpi_bus_notify(acpi_handle handle, u32 type, void *data)
{
	struct acpi_device *adev;
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

	adev = acpi_get_acpi_dev(handle);
	if (!adev)
		goto err;

	if (adev->dev.driver) {
		struct acpi_driver *driver = to_acpi_driver(adev->dev.driver);

		if (driver && driver->ops.notify &&
		    (driver->flags & ACPI_DRIVER_ALL_NOTIFY_EVENTS))
			driver->ops.notify(adev, type);
	}

	if (!hotplug_event) {
		acpi_put_acpi_dev(adev);
		return;
	}

	if (ACPI_SUCCESS(acpi_hotplug_schedule(adev, type)))
		return;

	acpi_put_acpi_dev(adev);

 err:
	acpi_evaluate_ost(handle, type, ost_code, NULL);
}

static void acpi_notify_device(acpi_handle handle, u32 event, void *data)
{
	struct acpi_device *device = data;
	struct acpi_driver *acpi_drv = to_acpi_driver(device->dev.driver);

	acpi_drv->ops.notify(device, event);
}

static void acpi_notify_device_fixed(void *data)
{
	struct acpi_device *device = data;

	/* Fixed hardware devices have no handles */
	acpi_notify_device(NULL, ACPI_FIXED_HARDWARE_EVENT, device);
}

static u32 acpi_device_fixed_event(void *data)
{
	acpi_os_execute(OSL_NOTIFY_HANDLER, acpi_notify_device_fixed, data);
	return ACPI_INTERRUPT_HANDLED;
}

static int acpi_device_install_notify_handler(struct acpi_device *device)
{
	acpi_status status;

	if (device->device_type == ACPI_BUS_TYPE_POWER_BUTTON)
		status =
		    acpi_install_fixed_event_handler(ACPI_EVENT_POWER_BUTTON,
						     acpi_device_fixed_event,
						     device);
	else if (device->device_type == ACPI_BUS_TYPE_SLEEP_BUTTON)
		status =
		    acpi_install_fixed_event_handler(ACPI_EVENT_SLEEP_BUTTON,
						     acpi_device_fixed_event,
						     device);
	else
		status = acpi_install_notify_handler(device->handle,
						     ACPI_DEVICE_NOTIFY,
						     acpi_notify_device,
						     device);

	if (ACPI_FAILURE(status))
		return -EINVAL;
	return 0;
}

static void acpi_device_remove_notify_handler(struct acpi_device *device)
{
	if (device->device_type == ACPI_BUS_TYPE_POWER_BUTTON)
		acpi_remove_fixed_event_handler(ACPI_EVENT_POWER_BUTTON,
						acpi_device_fixed_event);
	else if (device->device_type == ACPI_BUS_TYPE_SLEEP_BUTTON)
		acpi_remove_fixed_event_handler(ACPI_EVENT_SLEEP_BUTTON,
						acpi_device_fixed_event);
	else
		acpi_remove_notify_handler(device->handle, ACPI_DEVICE_NOTIFY,
					   acpi_notify_device);
}

/* Handle events targeting \_SB device (at present only graceful shutdown) */

#define ACPI_SB_NOTIFY_SHUTDOWN_REQUEST 0x81
#define ACPI_SB_INDICATE_INTERVAL	10000

static void sb_notify_work(struct work_struct *dummy)
{
	acpi_handle sb_handle;

	orderly_poweroff(true);

	/*
	 * After initiating graceful shutdown, the ACPI spec requires OSPM
	 * to evaluate _OST method once every 10seconds to indicate that
	 * the shutdown is in progress
	 */
	acpi_get_handle(NULL, "\\_SB", &sb_handle);
	while (1) {
		pr_info("Graceful shutdown in progress.\n");
		acpi_evaluate_ost(sb_handle, ACPI_OST_EC_OSPM_SHUTDOWN,
				ACPI_OST_SC_OS_SHUTDOWN_IN_PROGRESS, NULL);
		msleep(ACPI_SB_INDICATE_INTERVAL);
	}
}

static void acpi_sb_notify(acpi_handle handle, u32 event, void *data)
{
	static DECLARE_WORK(acpi_sb_work, sb_notify_work);

	if (event == ACPI_SB_NOTIFY_SHUTDOWN_REQUEST) {
		if (!work_busy(&acpi_sb_work))
			schedule_work(&acpi_sb_work);
	} else
		pr_warn("event %x is not supported by \\_SB device\n", event);
}

static int __init acpi_setup_sb_notify_handler(void)
{
	acpi_handle sb_handle;

	if (ACPI_FAILURE(acpi_get_handle(NULL, "\\_SB", &sb_handle)))
		return -ENXIO;

	if (ACPI_FAILURE(acpi_install_notify_handler(sb_handle, ACPI_DEVICE_NOTIFY,
						acpi_sb_notify, NULL)))
		return -EINVAL;

	return 0;
}

/* --------------------------------------------------------------------------
                             Device Matching
   -------------------------------------------------------------------------- */

/**
 * acpi_get_first_physical_node - Get first physical node of an ACPI device
 * @adev:	ACPI device in question
 *
 * Return: First physical node of ACPI device @adev
 */
struct device *acpi_get_first_physical_node(struct acpi_device *adev)
{
	struct mutex *physical_node_lock = &adev->physical_node_lock;
	struct device *phys_dev;

	mutex_lock(physical_node_lock);
	if (list_empty(&adev->physical_node_list)) {
		phys_dev = NULL;
	} else {
		const struct acpi_device_physical_node *node;

		node = list_first_entry(&adev->physical_node_list,
					struct acpi_device_physical_node, node);

		phys_dev = node->dev;
	}
	mutex_unlock(physical_node_lock);
	return phys_dev;
}
EXPORT_SYMBOL_GPL(acpi_get_first_physical_node);

static struct acpi_device *acpi_primary_dev_companion(struct acpi_device *adev,
						      const struct device *dev)
{
	const struct device *phys_dev = acpi_get_first_physical_node(adev);

	return phys_dev && phys_dev == dev ? adev : NULL;
}

/**
 * acpi_device_is_first_physical_node - Is given dev first physical node
 * @adev: ACPI companion device
 * @dev: Physical device to check
 *
 * Function checks if given @dev is the first physical devices attached to
 * the ACPI companion device. This distinction is needed in some cases
 * where the same companion device is shared between many physical devices.
 *
 * Note that the caller have to provide valid @adev pointer.
 */
bool acpi_device_is_first_physical_node(struct acpi_device *adev,
					const struct device *dev)
{
	return !!acpi_primary_dev_companion(adev, dev);
}

/*
 * acpi_companion_match() - Can we match via ACPI companion device
 * @dev: Device in question
 *
 * Check if the given device has an ACPI companion and if that companion has
 * a valid list of PNP IDs, and if the device is the first (primary) physical
 * device associated with it.  Return the companion pointer if that's the case
 * or NULL otherwise.
 *
 * If multiple physical devices are attached to a single ACPI companion, we need
 * to be careful.  The usage scenario for this kind of relationship is that all
 * of the physical devices in question use resources provided by the ACPI
 * companion.  A typical case is an MFD device where all the sub-devices share
 * the parent's ACPI companion.  In such cases we can only allow the primary
 * (first) physical device to be matched with the help of the companion's PNP
 * IDs.
 *
 * Additional physical devices sharing the ACPI companion can still use
 * resources available from it but they will be matched normally using functions
 * provided by their bus types (and analogously for their modalias).
 */
struct acpi_device *acpi_companion_match(const struct device *dev)
{
	struct acpi_device *adev;

	adev = ACPI_COMPANION(dev);
	if (!adev)
		return NULL;

	if (list_empty(&adev->pnp.ids))
		return NULL;

	return acpi_primary_dev_companion(adev, dev);
}

/**
 * acpi_of_match_device - Match device object using the "compatible" property.
 * @adev: ACPI device object to match.
 * @of_match_table: List of device IDs to match against.
 * @of_id: OF ID if matched
 *
 * If @dev has an ACPI companion which has ACPI_DT_NAMESPACE_HID in its list of
 * identifiers and a _DSD object with the "compatible" property, use that
 * property to match against the given list of identifiers.
 */
static bool acpi_of_match_device(struct acpi_device *adev,
				 const struct of_device_id *of_match_table,
				 const struct of_device_id **of_id)
{
	const union acpi_object *of_compatible, *obj;
	int i, nval;

	if (!adev)
		return false;

	of_compatible = adev->data.of_compatible;
	if (!of_match_table || !of_compatible)
		return false;

	if (of_compatible->type == ACPI_TYPE_PACKAGE) {
		nval = of_compatible->package.count;
		obj = of_compatible->package.elements;
	} else { /* Must be ACPI_TYPE_STRING. */
		nval = 1;
		obj = of_compatible;
	}
	/* Now we can look for the driver DT compatible strings */
	for (i = 0; i < nval; i++, obj++) {
		const struct of_device_id *id;

		for (id = of_match_table; id->compatible[0]; id++)
			if (!strcasecmp(obj->string.pointer, id->compatible)) {
				if (of_id)
					*of_id = id;
				return true;
			}
	}

	return false;
}

static bool acpi_of_modalias(struct acpi_device *adev,
			     char *modalias, size_t len)
{
	const union acpi_object *of_compatible;
	const union acpi_object *obj;
	const char *str, *chr;

	of_compatible = adev->data.of_compatible;
	if (!of_compatible)
		return false;

	if (of_compatible->type == ACPI_TYPE_PACKAGE)
		obj = of_compatible->package.elements;
	else /* Must be ACPI_TYPE_STRING. */
		obj = of_compatible;

	str = obj->string.pointer;
	chr = strchr(str, ',');
	strscpy(modalias, chr ? chr + 1 : str, len);

	return true;
}

/**
 * acpi_set_modalias - Set modalias using "compatible" property or supplied ID
 * @adev:	ACPI device object to match
 * @default_id:	ID string to use as default if no compatible string found
 * @modalias:   Pointer to buffer that modalias value will be copied into
 * @len:	Length of modalias buffer
 *
 * This is a counterpart of of_modalias_node() for struct acpi_device objects.
 * If there is a compatible string for @adev, it will be copied to @modalias
 * with the vendor prefix stripped; otherwise, @default_id will be used.
 */
void acpi_set_modalias(struct acpi_device *adev, const char *default_id,
		       char *modalias, size_t len)
{
	if (!acpi_of_modalias(adev, modalias, len))
		strscpy(modalias, default_id, len);
}
EXPORT_SYMBOL_GPL(acpi_set_modalias);

static bool __acpi_match_device_cls(const struct acpi_device_id *id,
				    struct acpi_hardware_id *hwid)
{
	int i, msk, byte_shift;
	char buf[3];

	if (!id->cls)
		return false;

	/* Apply class-code bitmask, before checking each class-code byte */
	for (i = 1; i <= 3; i++) {
		byte_shift = 8 * (3 - i);
		msk = (id->cls_msk >> byte_shift) & 0xFF;
		if (!msk)
			continue;

		sprintf(buf, "%02x", (id->cls >> byte_shift) & msk);
		if (strncmp(buf, &hwid->id[(i - 1) * 2], 2))
			return false;
	}
	return true;
}

static bool __acpi_match_device(struct acpi_device *device,
				const struct acpi_device_id *acpi_ids,
				const struct of_device_id *of_ids,
				const struct acpi_device_id **acpi_id,
				const struct of_device_id **of_id)
{
	const struct acpi_device_id *id;
	struct acpi_hardware_id *hwid;

	/*
	 * If the device is not present, it is unnecessary to load device
	 * driver for it.
	 */
	if (!device || !device->status.present)
		return false;

	list_for_each_entry(hwid, &device->pnp.ids, list) {
		/* First, check the ACPI/PNP IDs provided by the caller. */
		if (acpi_ids) {
			for (id = acpi_ids; id->id[0] || id->cls; id++) {
				if (id->id[0] && !strcmp((char *)id->id, hwid->id))
					goto out_acpi_match;
				if (id->cls && __acpi_match_device_cls(id, hwid))
					goto out_acpi_match;
			}
		}

		/*
		 * Next, check ACPI_DT_NAMESPACE_HID and try to match the
		 * "compatible" property if found.
		 */
		if (!strcmp(ACPI_DT_NAMESPACE_HID, hwid->id))
			return acpi_of_match_device(device, of_ids, of_id);
	}
	return false;

out_acpi_match:
	if (acpi_id)
		*acpi_id = id;
	return true;
}

/**
 * acpi_match_device - Match a struct device against a given list of ACPI IDs
 * @ids: Array of struct acpi_device_id object to match against.
 * @dev: The device structure to match.
 *
 * Check if @dev has a valid ACPI handle and if there is a struct acpi_device
 * object for that handle and use that object to match against a given list of
 * device IDs.
 *
 * Return a pointer to the first matching ID on success or %NULL on failure.
 */
const struct acpi_device_id *acpi_match_device(const struct acpi_device_id *ids,
					       const struct device *dev)
{
	const struct acpi_device_id *id = NULL;

	__acpi_match_device(acpi_companion_match(dev), ids, NULL, &id, NULL);
	return id;
}
EXPORT_SYMBOL_GPL(acpi_match_device);

static const void *acpi_of_device_get_match_data(const struct device *dev)
{
	struct acpi_device *adev = ACPI_COMPANION(dev);
	const struct of_device_id *match = NULL;

	if (!acpi_of_match_device(adev, dev->driver->of_match_table, &match))
		return NULL;

	return match->data;
}

const void *acpi_device_get_match_data(const struct device *dev)
{
	const struct acpi_device_id *acpi_ids = dev->driver->acpi_match_table;
	const struct acpi_device_id *match;

	if (!acpi_ids)
		return acpi_of_device_get_match_data(dev);

	match = acpi_match_device(acpi_ids, dev);
	if (!match)
		return NULL;

	return (const void *)match->driver_data;
}
EXPORT_SYMBOL_GPL(acpi_device_get_match_data);

int acpi_match_device_ids(struct acpi_device *device,
			  const struct acpi_device_id *ids)
{
	return __acpi_match_device(device, ids, NULL, NULL, NULL) ? 0 : -ENOENT;
}
EXPORT_SYMBOL(acpi_match_device_ids);

bool acpi_driver_match_device(struct device *dev,
			      const struct device_driver *drv)
{
	const struct acpi_device_id *acpi_ids = drv->acpi_match_table;
	const struct of_device_id *of_ids = drv->of_match_table;

	if (!acpi_ids)
		return acpi_of_match_device(ACPI_COMPANION(dev), of_ids, NULL);

	return __acpi_match_device(acpi_companion_match(dev), acpi_ids, of_ids, NULL, NULL);
}
EXPORT_SYMBOL_GPL(acpi_driver_match_device);

/* --------------------------------------------------------------------------
                              ACPI Driver Management
   -------------------------------------------------------------------------- */

/**
 * acpi_bus_register_driver - register a driver with the ACPI bus
 * @driver: driver being registered
 *
 * Registers a driver with the ACPI bus.  Searches the namespace for all
 * devices that match the driver's criteria and binds.  Returns zero for
 * success or a negative error status for failure.
 */
int acpi_bus_register_driver(struct acpi_driver *driver)
{
	if (acpi_disabled)
		return -ENODEV;
	driver->drv.name = driver->name;
	driver->drv.bus = &acpi_bus_type;
	driver->drv.owner = driver->owner;

	return driver_register(&driver->drv);
}

EXPORT_SYMBOL(acpi_bus_register_driver);

/**
 * acpi_bus_unregister_driver - unregisters a driver with the ACPI bus
 * @driver: driver to unregister
 *
 * Unregisters a driver with the ACPI bus.  Searches the namespace for all
 * devices that match the driver's criteria and unbinds.
 */
void acpi_bus_unregister_driver(struct acpi_driver *driver)
{
	driver_unregister(&driver->drv);
}

EXPORT_SYMBOL(acpi_bus_unregister_driver);

/* --------------------------------------------------------------------------
                              ACPI Bus operations
   -------------------------------------------------------------------------- */

static int acpi_bus_match(struct device *dev, struct device_driver *drv)
{
	struct acpi_device *acpi_dev = to_acpi_device(dev);
	struct acpi_driver *acpi_drv = to_acpi_driver(drv);

	return acpi_dev->flags.match_driver
		&& !acpi_match_device_ids(acpi_dev, acpi_drv->ids);
}

static int acpi_device_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	return __acpi_device_uevent_modalias(to_acpi_device(dev), env);
}

static int acpi_device_probe(struct device *dev)
{
	struct acpi_device *acpi_dev = to_acpi_device(dev);
	struct acpi_driver *acpi_drv = to_acpi_driver(dev->driver);
	int ret;

	if (acpi_dev->handler && !acpi_is_pnp_device(acpi_dev))
		return -EINVAL;

	if (!acpi_drv->ops.add)
		return -ENOSYS;

	ret = acpi_drv->ops.add(acpi_dev);
	if (ret)
		return ret;

	pr_debug("Driver [%s] successfully bound to device [%s]\n",
		 acpi_drv->name, acpi_dev->pnp.bus_id);

	if (acpi_drv->ops.notify) {
		ret = acpi_device_install_notify_handler(acpi_dev);
		if (ret) {
			if (acpi_drv->ops.remove)
				acpi_drv->ops.remove(acpi_dev);

			acpi_dev->driver_data = NULL;
			return ret;
		}
	}

	pr_debug("Found driver [%s] for device [%s]\n", acpi_drv->name,
		 acpi_dev->pnp.bus_id);

	get_device(dev);
	return 0;
}

static void acpi_device_remove(struct device *dev)
{
	struct acpi_device *acpi_dev = to_acpi_device(dev);
	struct acpi_driver *acpi_drv = to_acpi_driver(dev->driver);

	if (acpi_drv->ops.notify)
		acpi_device_remove_notify_handler(acpi_dev);

	if (acpi_drv->ops.remove)
		acpi_drv->ops.remove(acpi_dev);

	acpi_dev->driver_data = NULL;

	put_device(dev);
}

struct bus_type acpi_bus_type = {
	.name		= "acpi",
	.match		= acpi_bus_match,
	.probe		= acpi_device_probe,
	.remove		= acpi_device_remove,
	.uevent		= acpi_device_uevent,
};

int acpi_bus_for_each_dev(int (*fn)(struct device *, void *), void *data)
{
	return bus_for_each_dev(&acpi_bus_type, NULL, data, fn);
}
EXPORT_SYMBOL_GPL(acpi_bus_for_each_dev);

struct acpi_dev_walk_context {
	int (*fn)(struct acpi_device *, void *);
	void *data;
};

static int acpi_dev_for_one_check(struct device *dev, void *context)
{
	struct acpi_dev_walk_context *adwc = context;

	if (dev->bus != &acpi_bus_type)
		return 0;

	return adwc->fn(to_acpi_device(dev), adwc->data);
}
EXPORT_SYMBOL_GPL(acpi_dev_for_each_child);

int acpi_dev_for_each_child(struct acpi_device *adev,
			    int (*fn)(struct acpi_device *, void *), void *data)
{
	struct acpi_dev_walk_context adwc = {
		.fn = fn,
		.data = data,
	};

	return device_for_each_child(&adev->dev, &adwc, acpi_dev_for_one_check);
}

int acpi_dev_for_each_child_reverse(struct acpi_device *adev,
				    int (*fn)(struct acpi_device *, void *),
				    void *data)
{
	struct acpi_dev_walk_context adwc = {
		.fn = fn,
		.data = data,
	};

	return device_for_each_child_reverse(&adev->dev, &adwc, acpi_dev_for_one_check);
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
	case ACPI_IRQ_MODEL_GIC:
		message = "GIC";
		break;
	case ACPI_IRQ_MODEL_PLATFORM:
		message = "platform specific model";
		break;
	case ACPI_IRQ_MODEL_LPIC:
		message = "LPIC";
		break;
	default:
		pr_info("Unknown interrupt routing model\n");
		return -ENODEV;
	}

	pr_info("Using %s for interrupt routing\n", message);

	status = acpi_execute_simple_method(NULL, "\\_PIC", acpi_irq_model);
	if (ACPI_FAILURE(status) && (status != AE_NOT_FOUND)) {
		pr_info("_PIC evaluation failed: %s\n", acpi_format_exception(status));
		return -ENODEV;
	}

	return 0;
}

/**
 * acpi_early_init - Initialize ACPICA and populate the ACPI namespace.
 *
 * The ACPI tables are accessible after this, but the handling of events has not
 * been initialized and the global lock is not available yet, so AML should not
 * be executed at this point.
 *
 * Doing this before switching the EFI runtime services to virtual mode allows
 * the EfiBootServices memory to be freed slightly earlier on boot.
 */
void __init acpi_early_init(void)
{
	acpi_status status;

	if (acpi_disabled)
		return;

	pr_info("Core revision %08x\n", ACPI_CA_VERSION);

	/* enable workarounds, unless strict ACPI spec. compliance */
	if (!acpi_strict)
		acpi_gbl_enable_interpreter_slack = TRUE;

	acpi_permanent_mmap = true;

#ifdef CONFIG_X86
	/*
	 * If the machine falls into the DMI check table,
	 * DSDT will be copied to memory.
	 * Note that calling dmi_check_system() here on other architectures
	 * would not be OK because only x86 initializes dmi early enough.
	 * Thankfully only x86 systems need such quirks for now.
	 */
	dmi_check_system(dsdt_dmi_table);
#endif

	status = acpi_reallocate_root_table();
	if (ACPI_FAILURE(status)) {
		pr_err("Unable to reallocate ACPI tables\n");
		goto error0;
	}

	status = acpi_initialize_subsystem();
	if (ACPI_FAILURE(status)) {
		pr_err("Unable to initialize the ACPI Interpreter\n");
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
	return;

 error0:
	disable_acpi();
}

/**
 * acpi_subsystem_init - Finalize the early initialization of ACPI.
 *
 * Switch over the platform to the ACPI mode (if possible).
 *
 * Doing this too early is generally unsafe, but at the same time it needs to be
 * done before all things that really depend on ACPI.  The right spot appears to
 * be before finalizing the EFI initialization.
 */
void __init acpi_subsystem_init(void)
{
	acpi_status status;

	if (acpi_disabled)
		return;

	status = acpi_enable_subsystem(~ACPI_NO_ACPI_ENABLE);
	if (ACPI_FAILURE(status)) {
		pr_err("Unable to enable ACPI\n");
		disable_acpi();
	} else {
		/*
		 * If the system is using ACPI then we can be reasonably
		 * confident that any regulators are managed by the firmware
		 * so tell the regulator core it has everything it needs to
		 * know.
		 */
		regulator_has_full_constraints();
	}
}

static acpi_status acpi_bus_table_handler(u32 event, void *table, void *context)
{
	if (event == ACPI_TABLE_EVENT_LOAD)
		acpi_scan_table_notify();

	return acpi_sysfs_table_handler(event, table, context);
}

static int __init acpi_bus_init(void)
{
	int result;
	acpi_status status;

	acpi_os_initialize1();

	status = acpi_load_tables();
	if (ACPI_FAILURE(status)) {
		pr_err("Unable to load the System Description Tables\n");
		goto error1;
	}

	/*
	 * ACPI 2.0 requires the EC driver to be loaded and work before the EC
	 * device is found in the namespace.
	 *
	 * This is accomplished by looking for the ECDT table and getting the EC
	 * parameters out of that.
	 *
	 * Do that before calling acpi_initialize_objects() which may trigger EC
	 * address space accesses.
	 */
	acpi_ec_ecdt_probe();

	status = acpi_enable_subsystem(ACPI_NO_ACPI_ENABLE);
	if (ACPI_FAILURE(status)) {
		pr_err("Unable to start the ACPI Interpreter\n");
		goto error1;
	}

	status = acpi_initialize_objects(ACPI_FULL_INITIALIZATION);
	if (ACPI_FAILURE(status)) {
		pr_err("Unable to initialize ACPI objects\n");
		goto error1;
	}

	/* Set capability bits for _OSC under processor scope */
	acpi_early_processor_osc();

	/*
	 * _OSC method may exist in module level code,
	 * so it must be run after ACPI_FULL_INITIALIZATION
	 */
	acpi_bus_osc_negotiate_platform_control();
	acpi_bus_osc_negotiate_usb_control();

	/*
	 * _PDC control method may load dynamic SSDT tables,
	 * and we need to install the table handler before that.
	 */
	status = acpi_install_table_handler(acpi_bus_table_handler, NULL);

	acpi_sysfs_init();

	acpi_early_processor_set_pdc();

	/*
	 * Maybe EC region is required at bus_scan/acpi_get_devices. So it
	 * is necessary to enable it as early as possible.
	 */
	acpi_ec_dsdt_probe();

	pr_info("Interpreter enabled\n");

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
		pr_err("Unable to register for system notifications\n");
		goto error1;
	}

	/*
	 * Create the top ACPI proc directory
	 */
	acpi_root_dir = proc_mkdir(ACPI_BUS_FILE_ROOT, NULL);

	result = bus_register(&acpi_bus_type);
	if (!result)
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
		pr_info("Interpreter disabled.\n");
		return -ENODEV;
	}

	acpi_kobj = kobject_create_and_add("acpi", firmware_kobj);
	if (!acpi_kobj)
		pr_debug("%s: kset create error\n", __func__);

	init_prmt();
	acpi_init_pcc();
	result = acpi_bus_init();
	if (result) {
		kobject_put(acpi_kobj);
		disable_acpi();
		return result;
	}

	pci_mmcfg_late_init();
	acpi_iort_init();
	acpi_viot_early_init();
	acpi_hest_init();
	acpi_ghes_init();
	acpi_scan_init();
	acpi_ec_init();
	acpi_debugfs_init();
	acpi_sleep_proc_init();
	acpi_wakeup_device_init();
	acpi_debugger_init();
	acpi_setup_sb_notify_handler();
	acpi_viot_init();
	acpi_agdi_init();
	return 0;
}

subsys_initcall(acpi_init);
