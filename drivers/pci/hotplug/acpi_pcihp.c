/*
 * Common ACPI functions for hot plug platforms
 *
 * Copyright (C) 2006 Intel Corporation
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Send feedback to <kristen.c.accardi@intel.com>
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <acpi/acpi.h>
#include <acpi/acpi_bus.h>
#include <acpi/actypes.h>
#include "pci_hotplug.h"

#define	METHOD_NAME__SUN	"_SUN"
#define	METHOD_NAME__HPP	"_HPP"
#define	METHOD_NAME_OSHP	"OSHP"


static acpi_status
acpi_run_hpp(acpi_handle handle, struct hotplug_params *hpp)
{
	acpi_status		status;
	u8			nui[4];
	struct acpi_buffer	ret_buf = { 0, NULL};
	struct acpi_buffer	string = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object	*ext_obj, *package;
	int			i, len = 0;

	acpi_get_name(handle, ACPI_FULL_PATHNAME, &string);

	/* get _hpp */
	status = acpi_evaluate_object(handle, METHOD_NAME__HPP, NULL, &ret_buf);
	switch (status) {
	case AE_BUFFER_OVERFLOW:
		ret_buf.pointer = kmalloc (ret_buf.length, GFP_KERNEL);
		if (!ret_buf.pointer) {
			printk(KERN_ERR "%s:%s alloc for _HPP fail\n",
				__FUNCTION__, (char *)string.pointer);
			acpi_os_free(string.pointer);
			return AE_NO_MEMORY;
		}
		status = acpi_evaluate_object(handle, METHOD_NAME__HPP,
				NULL, &ret_buf);
		if (ACPI_SUCCESS(status))
			break;
	default:
		if (ACPI_FAILURE(status)) {
			pr_debug("%s:%s _HPP fail=0x%x\n", __FUNCTION__,
				(char *)string.pointer, status);
			acpi_os_free(string.pointer);
			return status;
		}
	}

	ext_obj = (union acpi_object *) ret_buf.pointer;
	if (ext_obj->type != ACPI_TYPE_PACKAGE) {
		printk(KERN_ERR "%s:%s _HPP obj not a package\n", __FUNCTION__,
				(char *)string.pointer);
		status = AE_ERROR;
		goto free_and_return;
	}

	len = ext_obj->package.count;
	package = (union acpi_object *) ret_buf.pointer;
	for ( i = 0; (i < len) || (i < 4); i++) {
		ext_obj = (union acpi_object *) &package->package.elements[i];
		switch (ext_obj->type) {
		case ACPI_TYPE_INTEGER:
			nui[i] = (u8)ext_obj->integer.value;
			break;
		default:
			printk(KERN_ERR "%s:%s _HPP obj type incorrect\n",
				__FUNCTION__, (char *)string.pointer);
			status = AE_ERROR;
			goto free_and_return;
		}
	}

	hpp->cache_line_size = nui[0];
	hpp->latency_timer = nui[1];
	hpp->enable_serr = nui[2];
	hpp->enable_perr = nui[3];

	pr_debug("  _HPP: cache_line_size=0x%x\n", hpp->cache_line_size);
	pr_debug("  _HPP: latency timer  =0x%x\n", hpp->latency_timer);
	pr_debug("  _HPP: enable SERR    =0x%x\n", hpp->enable_serr);
	pr_debug("  _HPP: enable PERR    =0x%x\n", hpp->enable_perr);

free_and_return:
	acpi_os_free(string.pointer);
	acpi_os_free(ret_buf.pointer);
	return status;
}



/* acpi_run_oshp - get control of hotplug from the firmware
 *
 * @handle - the handle of the hotplug controller.
 */
acpi_status acpi_run_oshp(acpi_handle handle)
{
	acpi_status		status;
	struct acpi_buffer	string = { ACPI_ALLOCATE_BUFFER, NULL };

	acpi_get_name(handle, ACPI_FULL_PATHNAME, &string);

	/* run OSHP */
	status = acpi_evaluate_object(handle, METHOD_NAME_OSHP, NULL, NULL);
	if (ACPI_FAILURE(status))
		printk(KERN_ERR "%s:%s OSHP fails=0x%x\n", __FUNCTION__,
			(char *)string.pointer, status);
	else
		pr_debug("%s:%s OSHP passes\n", __FUNCTION__,
			(char *)string.pointer);

	acpi_os_free(string.pointer);
	return status;
}
EXPORT_SYMBOL_GPL(acpi_run_oshp);



/* acpi_get_hp_params_from_firmware
 *
 * @dev - the pci_dev of the newly added device
 * @hpp - allocated by the caller
 */
acpi_status acpi_get_hp_params_from_firmware(struct pci_dev *dev,
		struct hotplug_params *hpp)
{
	acpi_status status = AE_NOT_FOUND;
	struct pci_dev *pdev = dev;

	/*
	 * _HPP settings apply to all child buses, until another _HPP is
	 * encountered. If we don't find an _HPP for the input pci dev,
	 * look for it in the parent device scope since that would apply to
	 * this pci dev. If we don't find any _HPP, use hardcoded defaults
	 */
	while (pdev && (ACPI_FAILURE(status))) {
		acpi_handle handle = DEVICE_ACPI_HANDLE(&(pdev->dev));
		if (!handle)
			break;
		status = acpi_run_hpp(handle, hpp);
		if (!(pdev->bus->parent))
			break;
		/* Check if a parent object supports _HPP */
		pdev = pdev->bus->parent->self;
	}
	return status;
}
EXPORT_SYMBOL_GPL(acpi_get_hp_params_from_firmware);


/* acpi_root_bridge - check to see if this acpi object is a root bridge
 *
 * @handle - the acpi object in question.
 */
int acpi_root_bridge(acpi_handle handle)
{
	acpi_status status;
	struct acpi_device_info *info;
	struct acpi_buffer buffer = {ACPI_ALLOCATE_BUFFER, NULL};
	int i;

	status = acpi_get_object_info(handle, &buffer);
	if (ACPI_SUCCESS(status)) {
		info = buffer.pointer;
		if ((info->valid & ACPI_VALID_HID) &&
			!strcmp(PCI_ROOT_HID_STRING,
					info->hardware_id.value)) {
			acpi_os_free(buffer.pointer);
			return 1;
		}
		if (info->valid & ACPI_VALID_CID) {
			for (i=0; i < info->compatibility_id.count; i++) {
				if (!strcmp(PCI_ROOT_HID_STRING,
					info->compatibility_id.id[i].value)) {
					acpi_os_free(buffer.pointer);
					return 1;
				}
			}
		}
		acpi_os_free(buffer.pointer);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(acpi_root_bridge);
