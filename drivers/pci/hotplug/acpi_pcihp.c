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
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/pci_hotplug.h>
#include <linux/pci-acpi.h>
#include <acpi/acpi.h>
#include <acpi/acpi_bus.h>

#define MY_NAME	"acpi_pcihp"

#define dbg(fmt, arg...) do { if (debug_acpi) printk(KERN_DEBUG "%s: %s: " fmt , MY_NAME , __func__ , ## arg); } while (0)
#define err(format, arg...) printk(KERN_ERR "%s: " format , MY_NAME , ## arg)
#define info(format, arg...) printk(KERN_INFO "%s: " format , MY_NAME , ## arg)
#define warn(format, arg...) printk(KERN_WARNING "%s: " format , MY_NAME , ## arg)

#define	METHOD_NAME__SUN	"_SUN"
#define	METHOD_NAME__HPP	"_HPP"
#define	METHOD_NAME_OSHP	"OSHP"

static int debug_acpi;

static acpi_status
decode_type0_hpx_record(union acpi_object *record, struct hotplug_params *hpx)
{
	int i;
	union acpi_object *fields = record->package.elements;
	u32 revision = fields[1].integer.value;

	switch (revision) {
	case 1:
		if (record->package.count != 6)
			return AE_ERROR;
		for (i = 2; i < 6; i++)
			if (fields[i].type != ACPI_TYPE_INTEGER)
				return AE_ERROR;
		hpx->t0 = &hpx->type0_data;
		hpx->t0->revision        = revision;
		hpx->t0->cache_line_size = fields[2].integer.value;
		hpx->t0->latency_timer   = fields[3].integer.value;
		hpx->t0->enable_serr     = fields[4].integer.value;
		hpx->t0->enable_perr     = fields[5].integer.value;
		break;
	default:
		printk(KERN_WARNING
		       "%s: Type 0 Revision %d record not supported\n",
		       __func__, revision);
		return AE_ERROR;
	}
	return AE_OK;
}

static acpi_status
decode_type1_hpx_record(union acpi_object *record, struct hotplug_params *hpx)
{
	int i;
	union acpi_object *fields = record->package.elements;
	u32 revision = fields[1].integer.value;

	switch (revision) {
	case 1:
		if (record->package.count != 5)
			return AE_ERROR;
		for (i = 2; i < 5; i++)
			if (fields[i].type != ACPI_TYPE_INTEGER)
				return AE_ERROR;
		hpx->t1 = &hpx->type1_data;
		hpx->t1->revision      = revision;
		hpx->t1->max_mem_read  = fields[2].integer.value;
		hpx->t1->avg_max_split = fields[3].integer.value;
		hpx->t1->tot_max_split = fields[4].integer.value;
		break;
	default:
		printk(KERN_WARNING
		       "%s: Type 1 Revision %d record not supported\n",
		       __func__, revision);
		return AE_ERROR;
	}
	return AE_OK;
}

static acpi_status
decode_type2_hpx_record(union acpi_object *record, struct hotplug_params *hpx)
{
	int i;
	union acpi_object *fields = record->package.elements;
	u32 revision = fields[1].integer.value;

	switch (revision) {
	case 1:
		if (record->package.count != 18)
			return AE_ERROR;
		for (i = 2; i < 18; i++)
			if (fields[i].type != ACPI_TYPE_INTEGER)
				return AE_ERROR;
		hpx->t2 = &hpx->type2_data;
		hpx->t2->revision      = revision;
		hpx->t2->unc_err_mask_and      = fields[2].integer.value;
		hpx->t2->unc_err_mask_or       = fields[3].integer.value;
		hpx->t2->unc_err_sever_and     = fields[4].integer.value;
		hpx->t2->unc_err_sever_or      = fields[5].integer.value;
		hpx->t2->cor_err_mask_and      = fields[6].integer.value;
		hpx->t2->cor_err_mask_or       = fields[7].integer.value;
		hpx->t2->adv_err_cap_and       = fields[8].integer.value;
		hpx->t2->adv_err_cap_or        = fields[9].integer.value;
		hpx->t2->pci_exp_devctl_and    = fields[10].integer.value;
		hpx->t2->pci_exp_devctl_or     = fields[11].integer.value;
		hpx->t2->pci_exp_lnkctl_and    = fields[12].integer.value;
		hpx->t2->pci_exp_lnkctl_or     = fields[13].integer.value;
		hpx->t2->sec_unc_err_sever_and = fields[14].integer.value;
		hpx->t2->sec_unc_err_sever_or  = fields[15].integer.value;
		hpx->t2->sec_unc_err_mask_and  = fields[16].integer.value;
		hpx->t2->sec_unc_err_mask_or   = fields[17].integer.value;
		break;
	default:
		printk(KERN_WARNING
		       "%s: Type 2 Revision %d record not supported\n",
		       __func__, revision);
		return AE_ERROR;
	}
	return AE_OK;
}

static acpi_status
acpi_run_hpx(acpi_handle handle, struct hotplug_params *hpx)
{
	acpi_status status;
	struct acpi_buffer buffer = {ACPI_ALLOCATE_BUFFER, NULL};
	union acpi_object *package, *record, *fields;
	u32 type;
	int i;

	/* Clear the return buffer with zeros */
	memset(hpx, 0, sizeof(struct hotplug_params));

	status = acpi_evaluate_object(handle, "_HPX", NULL, &buffer);
	if (ACPI_FAILURE(status))
		return status;

	package = (union acpi_object *)buffer.pointer;
	if (package->type != ACPI_TYPE_PACKAGE) {
		status = AE_ERROR;
		goto exit;
	}

	for (i = 0; i < package->package.count; i++) {
		record = &package->package.elements[i];
		if (record->type != ACPI_TYPE_PACKAGE) {
			status = AE_ERROR;
			goto exit;
		}

		fields = record->package.elements;
		if (fields[0].type != ACPI_TYPE_INTEGER ||
		    fields[1].type != ACPI_TYPE_INTEGER) {
			status = AE_ERROR;
			goto exit;
		}

		type = fields[0].integer.value;
		switch (type) {
		case 0:
			status = decode_type0_hpx_record(record, hpx);
			if (ACPI_FAILURE(status))
				goto exit;
			break;
		case 1:
			status = decode_type1_hpx_record(record, hpx);
			if (ACPI_FAILURE(status))
				goto exit;
			break;
		case 2:
			status = decode_type2_hpx_record(record, hpx);
			if (ACPI_FAILURE(status))
				goto exit;
			break;
		default:
			printk(KERN_ERR "%s: Type %d record not supported\n",
			       __func__, type);
			status = AE_ERROR;
			goto exit;
		}
	}
 exit:
	kfree(buffer.pointer);
	return status;
}

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

	/* Clear the return buffer with zeros */
	memset(hpp, 0, sizeof(struct hotplug_params));

	/* get _hpp */
	status = acpi_evaluate_object(handle, METHOD_NAME__HPP, NULL, &ret_buf);
	switch (status) {
	case AE_BUFFER_OVERFLOW:
		ret_buf.pointer = kmalloc (ret_buf.length, GFP_KERNEL);
		if (!ret_buf.pointer) {
			printk(KERN_ERR "%s:%s alloc for _HPP fail\n",
				__func__, (char *)string.pointer);
			kfree(string.pointer);
			return AE_NO_MEMORY;
		}
		status = acpi_evaluate_object(handle, METHOD_NAME__HPP,
				NULL, &ret_buf);
		if (ACPI_SUCCESS(status))
			break;
	default:
		if (ACPI_FAILURE(status)) {
			pr_debug("%s:%s _HPP fail=0x%x\n", __func__,
				(char *)string.pointer, status);
			kfree(string.pointer);
			return status;
		}
	}

	ext_obj = (union acpi_object *) ret_buf.pointer;
	if (ext_obj->type != ACPI_TYPE_PACKAGE) {
		printk(KERN_ERR "%s:%s _HPP obj not a package\n", __func__,
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
				__func__, (char *)string.pointer);
			status = AE_ERROR;
			goto free_and_return;
		}
	}

	hpp->t0 = &hpp->type0_data;
	hpp->t0->cache_line_size = nui[0];
	hpp->t0->latency_timer = nui[1];
	hpp->t0->enable_serr = nui[2];
	hpp->t0->enable_perr = nui[3];

	pr_debug("  _HPP: cache_line_size=0x%x\n", hpp->t0->cache_line_size);
	pr_debug("  _HPP: latency timer  =0x%x\n", hpp->t0->latency_timer);
	pr_debug("  _HPP: enable SERR    =0x%x\n", hpp->t0->enable_serr);
	pr_debug("  _HPP: enable PERR    =0x%x\n", hpp->t0->enable_perr);

free_and_return:
	kfree(string.pointer);
	kfree(ret_buf.pointer);
	return status;
}



/* acpi_run_oshp - get control of hotplug from the firmware
 *
 * @handle - the handle of the hotplug controller.
 */
static acpi_status acpi_run_oshp(acpi_handle handle)
{
	acpi_status		status;
	struct acpi_buffer	string = { ACPI_ALLOCATE_BUFFER, NULL };

	acpi_get_name(handle, ACPI_FULL_PATHNAME, &string);

	/* run OSHP */
	status = acpi_evaluate_object(handle, METHOD_NAME_OSHP, NULL, NULL);
	if (ACPI_FAILURE(status))
		if (status != AE_NOT_FOUND)
			printk(KERN_ERR "%s:%s OSHP fails=0x%x\n",
			       __func__, (char *)string.pointer, status);
		else
			dbg("%s:%s OSHP not found\n",
			    __func__, (char *)string.pointer);
	else
		pr_debug("%s:%s OSHP passes\n", __func__,
			(char *)string.pointer);

	kfree(string.pointer);
	return status;
}

/* acpi_get_hp_params_from_firmware
 *
 * @bus - the pci_bus of the bus on which the device is newly added
 * @hpp - allocated by the caller
 */
acpi_status acpi_get_hp_params_from_firmware(struct pci_bus *bus,
		struct hotplug_params *hpp)
{
	acpi_status status = AE_NOT_FOUND;
	acpi_handle handle, phandle;
	struct pci_bus *pbus = bus;
	struct pci_dev *pdev;

	do {
		pdev = pbus->self;
		if (!pdev) {
			handle = acpi_get_pci_rootbridge_handle(
				pci_domain_nr(pbus), pbus->number);
			break;
		}
		handle = DEVICE_ACPI_HANDLE(&(pdev->dev));
		pbus = pbus->parent;
	} while (!handle);

	/*
	 * _HPP settings apply to all child buses, until another _HPP is
	 * encountered. If we don't find an _HPP for the input pci dev,
	 * look for it in the parent device scope since that would apply to
	 * this pci dev. If we don't find any _HPP, use hardcoded defaults
	 */
	while (handle) {
		status = acpi_run_hpx(handle, hpp);
		if (ACPI_SUCCESS(status))
			break;
		status = acpi_run_hpp(handle, hpp);
		if (ACPI_SUCCESS(status))
			break;
		if (acpi_root_bridge(handle))
			break;
		status = acpi_get_parent(handle, &phandle);
		if (ACPI_FAILURE(status))
			break;
		handle = phandle;
	}
	return status;
}
EXPORT_SYMBOL_GPL(acpi_get_hp_params_from_firmware);

/**
 * acpi_get_hp_hw_control_from_firmware
 * @dev: the pci_dev of the bridge that has a hotplug controller
 * @flags: requested control bits for _OSC
 *
 * Attempt to take hotplug control from firmware.
 */
int acpi_get_hp_hw_control_from_firmware(struct pci_dev *dev, u32 flags)
{
	acpi_status status;
	acpi_handle chandle, handle;
	struct pci_dev *pdev = dev;
	struct pci_bus *parent;
	struct acpi_buffer string = { ACPI_ALLOCATE_BUFFER, NULL };

	flags &= (OSC_PCI_EXPRESS_NATIVE_HP_CONTROL |
		  OSC_SHPC_NATIVE_HP_CONTROL |
		  OSC_PCI_EXPRESS_CAP_STRUCTURE_CONTROL);
	if (!flags) {
		err("Invalid flags %u specified!\n", flags);
		return -EINVAL;
	}

	/*
	 * Per PCI firmware specification, we should run the ACPI _OSC
	 * method to get control of hotplug hardware before using it. If
	 * an _OSC is missing, we look for an OSHP to do the same thing.
	 * To handle different BIOS behavior, we look for _OSC on a root
	 * bridge preferentially (according to PCI fw spec). Later for
	 * OSHP within the scope of the hotplug controller and its parents,
	 * upto the host bridge under which this controller exists.
	 */
	handle = acpi_find_root_bridge_handle(pdev);
	if (handle) {
		acpi_get_name(handle, ACPI_FULL_PATHNAME, &string);
		dbg("Trying to get hotplug control for %s\n",
				(char *)string.pointer);
		status = pci_osc_control_set(handle, flags);
		if (ACPI_SUCCESS(status))
			goto got_one;
		kfree(string.pointer);
		string = (struct acpi_buffer){ ACPI_ALLOCATE_BUFFER, NULL };
	}

	pdev = dev;
	handle = DEVICE_ACPI_HANDLE(&dev->dev);
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
			handle = DEVICE_ACPI_HANDLE(&(parent->self->dev));
		pdev = parent->self;
	}

	while (handle) {
		acpi_get_name(handle, ACPI_FULL_PATHNAME, &string);
		dbg("Trying to get hotplug control for %s \n",
		    (char *)string.pointer);
		status = acpi_run_oshp(handle);
		if (ACPI_SUCCESS(status))
			goto got_one;
		if (acpi_root_bridge(handle))
			break;
		chandle = handle;
		status = acpi_get_parent(chandle, &handle);
		if (ACPI_FAILURE(status))
			break;
	}

	dbg("Cannot get control of hotplug hardware for pci %s\n",
	    pci_name(dev));

	kfree(string.pointer);
	return -ENODEV;
got_one:
	dbg("Gained control for hotplug HW for pci %s (%s)\n", pci_name(dev),
			(char *)string.pointer);
	kfree(string.pointer);
	return 0;
}
EXPORT_SYMBOL(acpi_get_hp_hw_control_from_firmware);

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
			kfree(buffer.pointer);
			return 1;
		}
		if (info->valid & ACPI_VALID_CID) {
			for (i=0; i < info->compatibility_id.count; i++) {
				if (!strcmp(PCI_ROOT_HID_STRING,
					info->compatibility_id.id[i].value)) {
					kfree(buffer.pointer);
					return 1;
				}
			}
		}
		kfree(buffer.pointer);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(acpi_root_bridge);


static int is_ejectable(acpi_handle handle)
{
	acpi_status status;
	acpi_handle tmp;
	unsigned long long removable;
	status = acpi_get_handle(handle, "_ADR", &tmp);
	if (ACPI_FAILURE(status))
		return 0;
	status = acpi_get_handle(handle, "_EJ0", &tmp);
	if (ACPI_SUCCESS(status))
		return 1;
	status = acpi_evaluate_integer(handle, "_RMV", NULL, &removable);
	if (ACPI_SUCCESS(status) && removable)
		return 1;
	return 0;
}

/**
 * acpi_pcihp_check_ejectable - check if handle is ejectable ACPI PCI slot
 * @pbus: the PCI bus of the PCI slot corresponding to 'handle'
 * @handle: ACPI handle to check
 *
 * Return 1 if handle is ejectable PCI slot, 0 otherwise.
 */
int acpi_pci_check_ejectable(struct pci_bus *pbus, acpi_handle handle)
{
	acpi_handle bridge_handle, parent_handle;

	if (!(bridge_handle = acpi_pci_get_bridge_handle(pbus)))
		return 0;
	if ((ACPI_FAILURE(acpi_get_parent(handle, &parent_handle))))
		return 0;
	if (bridge_handle != parent_handle)
		return 0;
	return is_ejectable(handle);
}
EXPORT_SYMBOL_GPL(acpi_pci_check_ejectable);

static acpi_status
check_hotplug(acpi_handle handle, u32 lvl, void *context, void **rv)
{
	int *found = (int *)context;
	if (is_ejectable(handle)) {
		*found = 1;
		return AE_CTRL_TERMINATE;
	}
	return AE_OK;
}

/**
 * acpi_pci_detect_ejectable - check if the PCI bus has ejectable slots
 * @pbus - PCI bus to scan
 *
 * Returns 1 if the PCI bus has ACPI based ejectable slots, 0 otherwise.
 */
int acpi_pci_detect_ejectable(struct pci_bus *pbus)
{
	acpi_handle handle;
	int found = 0;

	if (!(handle = acpi_pci_get_bridge_handle(pbus)))
		return 0;
	acpi_walk_namespace(ACPI_TYPE_DEVICE, handle, (u32)1,
			    check_hotplug, (void *)&found, NULL);
	return found;
}
EXPORT_SYMBOL_GPL(acpi_pci_detect_ejectable);

module_param(debug_acpi, bool, 0644);
MODULE_PARM_DESC(debug_acpi, "Debugging mode for ACPI enabled or not");
