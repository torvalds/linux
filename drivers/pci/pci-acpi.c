/*
 * File:	pci-acpi.c
 * Purpose:	Provide PCI support in ACPI
 *
 * Copyright (C) 2005 David Shaohua Li <shaohua.li@intel.com>
 * Copyright (C) 2004 Tom Long Nguyen <tom.l.nguyen@intel.com>
 * Copyright (C) 2004 Intel Corp.
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <acpi/acpi.h>
#include <acpi/acnamesp.h>
#include <acpi/acresrc.h>
#include <acpi/acpi_bus.h>

#include <linux/pci-acpi.h>
#include "pci.h"

static u32 ctrlset_buf[3] = {0, 0, 0};
static u32 global_ctrlsets = 0;
static u8 OSC_UUID[16] = {0x5B, 0x4D, 0xDB, 0x33, 0xF7, 0x1F, 0x1C, 0x40, 0x96, 0x57, 0x74, 0x41, 0xC0, 0x3D, 0xD7, 0x66};

static acpi_status  
acpi_query_osc (
	acpi_handle	handle,
	u32		level,
	void		*context,
	void		**retval )
{
	acpi_status		status;
	struct acpi_object_list	input;
	union acpi_object 	in_params[4];
	struct acpi_buffer	output;
	union acpi_object 	out_obj;	
	u32			osc_dw0;

	/* Setting up output buffer */
	output.length = sizeof(out_obj) + 3*sizeof(u32);  
	output.pointer = &out_obj;
	
	/* Setting up input parameters */
	input.count = 4;
	input.pointer = in_params;
	in_params[0].type 		= ACPI_TYPE_BUFFER;
	in_params[0].buffer.length 	= 16;
	in_params[0].buffer.pointer	= OSC_UUID;
	in_params[1].type 		= ACPI_TYPE_INTEGER;
	in_params[1].integer.value 	= 1;
	in_params[2].type 		= ACPI_TYPE_INTEGER;
	in_params[2].integer.value	= 3;
	in_params[3].type		= ACPI_TYPE_BUFFER;
	in_params[3].buffer.length 	= 12;
	in_params[3].buffer.pointer 	= (u8 *)context;

	status = acpi_evaluate_object(handle, "_OSC", &input, &output);
	if (ACPI_FAILURE (status)) {
		printk(KERN_DEBUG  
			"Evaluate _OSC Set fails. Status = 0x%04x\n", status);
		return status;
	}
	if (out_obj.type != ACPI_TYPE_BUFFER) {
		printk(KERN_DEBUG  
			"Evaluate _OSC returns wrong type\n");
		return AE_TYPE;
	}
	osc_dw0 = *((u32 *) out_obj.buffer.pointer);
	if (osc_dw0) {
		if (osc_dw0 & OSC_REQUEST_ERROR)
			printk(KERN_DEBUG "_OSC request fails\n"); 
		if (osc_dw0 & OSC_INVALID_UUID_ERROR)
			printk(KERN_DEBUG "_OSC invalid UUID\n"); 
		if (osc_dw0 & OSC_INVALID_REVISION_ERROR)
			printk(KERN_DEBUG "_OSC invalid revision\n"); 
		if (osc_dw0 & OSC_CAPABILITIES_MASK_ERROR) {
			/* Update Global Control Set */
			global_ctrlsets = *((u32 *)(out_obj.buffer.pointer+8));
			return AE_OK;
		}
		return AE_ERROR;
	}

	/* Update Global Control Set */
	global_ctrlsets = *((u32 *)(out_obj.buffer.pointer + 8));
	return AE_OK;
}


static acpi_status  
acpi_run_osc (
	acpi_handle	handle,
	u32		level,
	void		*context,
	void		**retval )
{
	acpi_status		status;
	struct acpi_object_list	input;
	union acpi_object 	in_params[4];
	struct acpi_buffer	output;
	union acpi_object 	out_obj;	
	u32			osc_dw0;

	/* Setting up output buffer */
	output.length = sizeof(out_obj) + 3*sizeof(u32);  
	output.pointer = &out_obj;
	
	/* Setting up input parameters */
	input.count = 4;
	input.pointer = in_params;
	in_params[0].type 		= ACPI_TYPE_BUFFER;
	in_params[0].buffer.length 	= 16;
	in_params[0].buffer.pointer	= OSC_UUID;
	in_params[1].type 		= ACPI_TYPE_INTEGER;
	in_params[1].integer.value 	= 1;
	in_params[2].type 		= ACPI_TYPE_INTEGER;
	in_params[2].integer.value	= 3;
	in_params[3].type		= ACPI_TYPE_BUFFER;
	in_params[3].buffer.length 	= 12;
	in_params[3].buffer.pointer 	= (u8 *)context;

	status = acpi_evaluate_object(handle, "_OSC", &input, &output);
	if (ACPI_FAILURE (status)) {
		printk(KERN_DEBUG  
			"Evaluate _OSC Set fails. Status = 0x%04x\n", status);
		return status;
	}
	if (out_obj.type != ACPI_TYPE_BUFFER) {
		printk(KERN_DEBUG  
			"Evaluate _OSC returns wrong type\n");
		return AE_TYPE;
	}
	osc_dw0 = *((u32 *) out_obj.buffer.pointer);
	if (osc_dw0) {
		if (osc_dw0 & OSC_REQUEST_ERROR)
			printk(KERN_DEBUG "_OSC request fails\n"); 
		if (osc_dw0 & OSC_INVALID_UUID_ERROR)
			printk(KERN_DEBUG "_OSC invalid UUID\n"); 
		if (osc_dw0 & OSC_INVALID_REVISION_ERROR)
			printk(KERN_DEBUG "_OSC invalid revision\n"); 
		if (osc_dw0 & OSC_CAPABILITIES_MASK_ERROR) {
			printk(KERN_DEBUG "_OSC FW not grant req. control\n");
			return AE_SUPPORT;
		}
		return AE_ERROR;
	}
	return AE_OK;
}

/**
 * pci_osc_support_set - register OS support to Firmware
 * @flags: OS support bits
 *
 * Update OS support fields and doing a _OSC Query to obtain an update
 * from Firmware on supported control bits.
 **/
acpi_status pci_osc_support_set(u32 flags)
{
	u32 temp;

	if (!(flags & OSC_SUPPORT_MASKS)) {
		return AE_TYPE;
	}
	ctrlset_buf[OSC_SUPPORT_TYPE] |= (flags & OSC_SUPPORT_MASKS);

	/* do _OSC query for all possible controls */
	temp = ctrlset_buf[OSC_CONTROL_TYPE];
	ctrlset_buf[OSC_QUERY_TYPE] = OSC_QUERY_ENABLE;
	ctrlset_buf[OSC_CONTROL_TYPE] = OSC_CONTROL_MASKS;
	acpi_get_devices ( PCI_ROOT_HID_STRING,
			acpi_query_osc,
			ctrlset_buf,
			NULL );
	ctrlset_buf[OSC_QUERY_TYPE] = !OSC_QUERY_ENABLE;
	ctrlset_buf[OSC_CONTROL_TYPE] = temp;
	return AE_OK;
}
EXPORT_SYMBOL(pci_osc_support_set);

/**
 * pci_osc_control_set - commit requested control to Firmware
 * @flags: driver's requested control bits
 *
 * Attempt to take control from Firmware on requested control bits.
 **/
acpi_status pci_osc_control_set(u32 flags)
{
	acpi_status	status;
	u32		ctrlset;

	ctrlset = (flags & OSC_CONTROL_MASKS);
	if (!ctrlset) {
		return AE_TYPE;
	}
	if (ctrlset_buf[OSC_SUPPORT_TYPE] && 
	 	((global_ctrlsets & ctrlset) != ctrlset)) {
		return AE_SUPPORT;
	}
	ctrlset_buf[OSC_CONTROL_TYPE] |= ctrlset;
	status = acpi_get_devices ( PCI_ROOT_HID_STRING,
				acpi_run_osc,
				ctrlset_buf,
				NULL );
	if (ACPI_FAILURE (status)) {
		ctrlset_buf[OSC_CONTROL_TYPE] &= ~ctrlset;
	}
	
	return status;
}
EXPORT_SYMBOL(pci_osc_control_set);

/*
 * _SxD returns the D-state with the highest power
 * (lowest D-state number) supported in the S-state "x".
 *
 * If the devices does not have a _PRW
 * (Power Resources for Wake) supporting system wakeup from "x"
 * then the OS is free to choose a lower power (higher number
 * D-state) than the return value from _SxD.
 *
 * But if _PRW is enabled at S-state "x", the OS
 * must not choose a power lower than _SxD --
 * unless the device has an _SxW method specifying
 * the lowest power (highest D-state number) the device
 * may enter while still able to wake the system.
 *
 * ie. depending on global OS policy:
 *
 * if (_PRW at S-state x)
 *	choose from highest power _SxD to lowest power _SxW
 * else // no _PRW at S-state x
 * 	choose highest power _SxD or any lower power
 *
 * currently we simply return _SxD, if present.
 */

static int acpi_pci_choose_state(struct pci_dev *pdev, pm_message_t state)
{
	/* TBD */

	return -ENODEV;
}

static int acpi_pci_set_power_state(struct pci_dev *dev, pci_power_t state)
{
	acpi_handle handle = DEVICE_ACPI_HANDLE(&dev->dev);
	static int state_conv[] = {
		[0] = 0,
		[1] = 1,
		[2] = 2,
		[3] = 3,
		[4] = 3
	};
	int acpi_state = state_conv[(int __force) state];

	if (!handle)
		return -ENODEV;
	return acpi_bus_set_power(handle, acpi_state);
}


/* ACPI bus type */
static int pci_acpi_find_device(struct device *dev, acpi_handle *handle)
{
	struct pci_dev * pci_dev;
	acpi_integer	addr;

	pci_dev = to_pci_dev(dev);
	/* Please ref to ACPI spec for the syntax of _ADR */
	addr = (PCI_SLOT(pci_dev->devfn) << 16) | PCI_FUNC(pci_dev->devfn);
	*handle = acpi_get_child(DEVICE_ACPI_HANDLE(dev->parent), addr);
	if (!*handle)
		return -ENODEV;
	return 0;
}

static int pci_acpi_find_root_bridge(struct device *dev, acpi_handle *handle)
{
	int num;
	unsigned int seg, bus;

	/*
	 * The string should be the same as root bridge's name
	 * Please look at 'pci_scan_bus_parented'
	 */
	num = sscanf(dev->bus_id, "pci%04x:%02x", &seg, &bus);
	if (num != 2)
		return -ENODEV;
	*handle = acpi_get_pci_rootbridge_handle(seg, bus);
	if (!*handle)
		return -ENODEV;
	return 0;
}

static struct acpi_bus_type pci_acpi_bus = {
	.bus = &pci_bus_type,
	.find_device = pci_acpi_find_device,
	.find_bridge = pci_acpi_find_root_bridge,
};

static int __init pci_acpi_init(void)
{
	int ret;

	ret = register_acpi_bus_type(&pci_acpi_bus);
	if (ret)
		return 0;
	platform_pci_choose_state = acpi_pci_choose_state;
	platform_pci_set_power_state = acpi_pci_set_power_state;
	return 0;
}
arch_initcall(pci_acpi_init);
