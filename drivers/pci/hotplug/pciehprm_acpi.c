/*
 * PCIEHPRM ACPI: PHP Resource Manager for ACPI platform
 *
 * Copyright (C) 2003-2004 Intel Corporation
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
#include <linux/acpi.h>
#include <linux/pci-acpi.h>
#include <acpi/acpi_bus.h>
#include <acpi/actypes.h>
#include "pciehp.h"

#define	METHOD_NAME__SUN	"_SUN"
#define	METHOD_NAME__HPP	"_HPP"
#define	METHOD_NAME_OSHP	"OSHP"

static u8 * acpi_path_name( acpi_handle	handle)
{
	acpi_status		status;
	static u8		path_name[ACPI_PATHNAME_MAX];
	struct acpi_buffer	ret_buf = { ACPI_PATHNAME_MAX, path_name };

	memset(path_name, 0, sizeof (path_name));
	status = acpi_get_name(handle, ACPI_FULL_PATHNAME, &ret_buf);

	if (ACPI_FAILURE(status))
		return NULL;
	else
		return path_name;	
}

static acpi_status
acpi_run_hpp(acpi_handle handle, struct hotplug_params *hpp)
{
	acpi_status		status;
	u8			nui[4];
	struct acpi_buffer	ret_buf = { 0, NULL};
	union acpi_object	*ext_obj, *package;
	u8			*path_name = acpi_path_name(handle);
	int			i, len = 0;

	/* get _hpp */
	status = acpi_evaluate_object(handle, METHOD_NAME__HPP, NULL, &ret_buf);
	switch (status) {
	case AE_BUFFER_OVERFLOW:
		ret_buf.pointer = kmalloc (ret_buf.length, GFP_KERNEL);
		if (!ret_buf.pointer) {
			err ("%s:%s alloc for _HPP fail\n", __FUNCTION__,
					path_name);
			return AE_NO_MEMORY;
		}
		status = acpi_evaluate_object(handle, METHOD_NAME__HPP,
				NULL, &ret_buf);
		if (ACPI_SUCCESS(status))
			break;
	default:
		if (ACPI_FAILURE(status)) {
			dbg("%s:%s _HPP fail=0x%x\n", __FUNCTION__,
					path_name, status);
			return status;
		}
	}

	ext_obj = (union acpi_object *) ret_buf.pointer;
	if (ext_obj->type != ACPI_TYPE_PACKAGE) {
		err ("%s:%s _HPP obj not a package\n", __FUNCTION__,
				path_name);
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
			err ("%s:%s _HPP obj type incorrect\n", __FUNCTION__,
					path_name);
			status = AE_ERROR;
			goto free_and_return;
		}
	}

	hpp->cache_line_size = nui[0];
	hpp->latency_timer = nui[1];
	hpp->enable_serr = nui[2];
	hpp->enable_perr = nui[3];

	dbg("  _HPP: cache_line_size=0x%x\n", hpp->cache_line_size);
	dbg("  _HPP: latency timer  =0x%x\n", hpp->latency_timer);
	dbg("  _HPP: enable SERR    =0x%x\n", hpp->enable_serr);
	dbg("  _HPP: enable PERR    =0x%x\n", hpp->enable_perr);

free_and_return:
	kfree(ret_buf.pointer);
	return status;
}

static acpi_status acpi_run_oshp(acpi_handle handle)
{
	acpi_status		status;
	u8			*path_name = acpi_path_name(handle);

	/* run OSHP */
	status = acpi_evaluate_object(handle, METHOD_NAME_OSHP, NULL, NULL);
	if (ACPI_FAILURE(status)) {
		dbg("%s:%s OSHP fails=0x%x\n", __FUNCTION__, path_name,
				status);
	} else {
		dbg("%s:%s OSHP passes\n", __FUNCTION__, path_name);
	}
	return status;
}

static int is_root_bridge(acpi_handle handle)
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
	}
	return 0;
}

int pciehp_get_hp_hw_control_from_firmware(struct pci_dev *dev)
{
	acpi_status status;
	acpi_handle chandle, handle = DEVICE_ACPI_HANDLE(&(dev->dev));
	struct pci_dev *pdev = dev;
	struct pci_bus *parent;
	u8 *path_name;

	/*
	 * Per PCI firmware specification, we should run the ACPI _OSC
	 * method to get control of hotplug hardware before using it.
	 * If an _OSC is missing, we look for an OSHP to do the same thing.
	 * To handle different BIOS behavior, we look for _OSC and OSHP
	 * within the scope of the hotplug controller and its parents, upto
	 * the host bridge under which this controller exists.
	 */
	while (!handle) {
		/*
		 * This hotplug controller was not listed in the ACPI name
		 * space at all. Try to get acpi handle of parent pci bus.
		 */
		if (!pdev || !pdev->bus->parent)
			break;
		parent = pdev->bus->parent;
		dbg("Could not find %s in acpi namespace, trying parent\n",
				pci_name(pdev));
		if (!parent->self)
			/* Parent must be a host bridge */
			handle = acpi_get_pci_rootbridge_handle(
					pci_domain_nr(parent),
					parent->number);
		else
			handle = DEVICE_ACPI_HANDLE(
					&(parent->self->dev));
		pdev = parent->self;
	}

	while (handle) {
		path_name = acpi_path_name(handle);
		dbg("Trying to get hotplug control for %s \n", path_name);
		status = pci_osc_control_set(handle,
				OSC_PCI_EXPRESS_NATIVE_HP_CONTROL);
		if (status == AE_NOT_FOUND)
			status = acpi_run_oshp(handle);
		if (ACPI_SUCCESS(status)) {
			dbg("Gained control for hotplug HW for pci %s (%s)\n",
				pci_name(dev), path_name);
			return 0;
		}
		if (is_root_bridge(handle))
			break;
		chandle = handle;
		status = acpi_get_parent(chandle, &handle);
		if (ACPI_FAILURE(status))
			break;
	}

	err("Cannot get control of hotplug hardware for pci %s\n",
			pci_name(dev));
	return -1;
}

void pciehp_get_hp_params_from_firmware(struct pci_dev *dev,
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
}

