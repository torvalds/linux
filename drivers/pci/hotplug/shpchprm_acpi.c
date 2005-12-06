/*
 * SHPCHPRM ACPI: PHP Resource Manager for ACPI platform
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
#include <acpi/acpi.h>
#include <acpi/acpi_bus.h>
#include <acpi/actypes.h>
#include "shpchp.h"

#define	METHOD_NAME__SUN	"_SUN"
#define	METHOD_NAME__HPP	"_HPP"
#define	METHOD_NAME_OSHP	"OSHP"

static u8 * acpi_path_name( acpi_handle	handle)
{
	acpi_status		status;
	static u8	path_name[ACPI_PATHNAME_MAX];
	struct acpi_buffer		ret_buf = { ACPI_PATHNAME_MAX, path_name };

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

static void acpi_run_oshp(acpi_handle handle)
{
	acpi_status		status;
	u8			*path_name = acpi_path_name(handle);

	/* run OSHP */
	status = acpi_evaluate_object(handle, METHOD_NAME_OSHP, NULL, NULL);
	if (ACPI_FAILURE(status)) {
		err("%s:%s OSHP fails=0x%x\n", __FUNCTION__, path_name,
				status);
	} else {
		dbg("%s:%s OSHP passes\n", __FUNCTION__, path_name);
	}
}

int shpchprm_get_physical_slot_number(struct controller *ctrl, u32 *sun, u8 busnum, u8 devnum)
{
	int offset = devnum - ctrl->slot_device_offset;

	dbg("%s: ctrl->slot_num_inc %d, offset %d\n", __FUNCTION__, ctrl->slot_num_inc, offset);
	*sun = (u8) (ctrl->first_slot + ctrl->slot_num_inc *offset);
	return 0;
}

void get_hp_hw_control_from_firmware(struct pci_dev *dev)
{
	/*
	 * OSHP is an optional ACPI firmware control method. If present,
	 * we need to run it to inform BIOS that we will control SHPC
	 * hardware from now on.
	 */
	acpi_handle handle = DEVICE_ACPI_HANDLE(&(dev->dev));
	if (!handle)
		return;
	acpi_run_oshp(handle);
}

void get_hp_params_from_firmware(struct pci_dev *dev,
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

