/*
 *  pci_root.c - ACPI PCI Root Bridge Driver ($Revision: 40 $)
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
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/pci.h>
#include <linux/pci-acpi.h>
#include <linux/pci-aspm.h>
#include <linux/dmar.h>
#include <linux/acpi.h>
#include <linux/slab.h>
#include <linux/dmi.h>
#include <linux/platform_data/x86/apple.h>
#include <acpi/apei.h>	/* for acpi_hest_init() */

#include "internal.h"

#define _COMPONENT		ACPI_PCI_COMPONENT
ACPI_MODULE_NAME("pci_root");
#define ACPI_PCI_ROOT_CLASS		"pci_bridge"
#define ACPI_PCI_ROOT_DEVICE_NAME	"PCI Root Bridge"
static int acpi_pci_root_add(struct acpi_device *device,
			     const struct acpi_device_id *not_used);
static void acpi_pci_root_remove(struct acpi_device *device);

static int acpi_pci_root_scan_dependent(struct acpi_device *adev)
{
	acpiphp_check_host_bridge(adev);
	return 0;
}

#define ACPI_PCIE_REQ_SUPPORT (OSC_PCI_EXT_CONFIG_SUPPORT \
				| OSC_PCI_ASPM_SUPPORT \
				| OSC_PCI_CLOCK_PM_SUPPORT \
				| OSC_PCI_MSI_SUPPORT)

static const struct acpi_device_id root_device_ids[] = {
	{"PNP0A03", 0},
	{"", 0},
};

static struct acpi_scan_handler pci_root_handler = {
	.ids = root_device_ids,
	.attach = acpi_pci_root_add,
	.detach = acpi_pci_root_remove,
	.hotplug = {
		.enabled = true,
		.scan_dependent = acpi_pci_root_scan_dependent,
	},
};

static DEFINE_MUTEX(osc_lock);

/**
 * acpi_is_root_bridge - determine whether an ACPI CA node is a PCI root bridge
 * @handle - the ACPI CA node in question.
 *
 * Note: we could make this API take a struct acpi_device * instead, but
 * for now, it's more convenient to operate on an acpi_handle.
 */
int acpi_is_root_bridge(acpi_handle handle)
{
	int ret;
	struct acpi_device *device;

	ret = acpi_bus_get_device(handle, &device);
	if (ret)
		return 0;

	ret = acpi_match_device_ids(device, root_device_ids);
	if (ret)
		return 0;
	else
		return 1;
}
EXPORT_SYMBOL_GPL(acpi_is_root_bridge);

static acpi_status
get_root_bridge_busnr_callback(struct acpi_resource *resource, void *data)
{
	struct resource *res = data;
	struct acpi_resource_address64 address;
	acpi_status status;

	status = acpi_resource_to_address64(resource, &address);
	if (ACPI_FAILURE(status))
		return AE_OK;

	if ((address.address.address_length > 0) &&
	    (address.resource_type == ACPI_BUS_NUMBER_RANGE)) {
		res->start = address.address.minimum;
		res->end = address.address.minimum + address.address.address_length - 1;
	}

	return AE_OK;
}

static acpi_status try_get_root_bridge_busnr(acpi_handle handle,
					     struct resource *res)
{
	acpi_status status;

	res->start = -1;
	status =
	    acpi_walk_resources(handle, METHOD_NAME__CRS,
				get_root_bridge_busnr_callback, res);
	if (ACPI_FAILURE(status))
		return status;
	if (res->start == -1)
		return AE_ERROR;
	return AE_OK;
}

struct pci_osc_bit_struct {
	u32 bit;
	char *desc;
};

static struct pci_osc_bit_struct pci_osc_support_bit[] = {
	{ OSC_PCI_EXT_CONFIG_SUPPORT, "ExtendedConfig" },
	{ OSC_PCI_ASPM_SUPPORT, "ASPM" },
	{ OSC_PCI_CLOCK_PM_SUPPORT, "ClockPM" },
	{ OSC_PCI_SEGMENT_GROUPS_SUPPORT, "Segments" },
	{ OSC_PCI_MSI_SUPPORT, "MSI" },
};

static struct pci_osc_bit_struct pci_osc_control_bit[] = {
	{ OSC_PCI_EXPRESS_NATIVE_HP_CONTROL, "PCIeHotplug" },
	{ OSC_PCI_SHPC_NATIVE_HP_CONTROL, "SHPCHotplug" },
	{ OSC_PCI_EXPRESS_PME_CONTROL, "PME" },
	{ OSC_PCI_EXPRESS_AER_CONTROL, "AER" },
	{ OSC_PCI_EXPRESS_CAPABILITY_CONTROL, "PCIeCapability" },
	{ OSC_PCI_EXPRESS_LTR_CONTROL, "LTR" },
};

static void decode_osc_bits(struct acpi_pci_root *root, char *msg, u32 word,
			    struct pci_osc_bit_struct *table, int size)
{
	char buf[80];
	int i, len = 0;
	struct pci_osc_bit_struct *entry;

	buf[0] = '\0';
	for (i = 0, entry = table; i < size; i++, entry++)
		if (word & entry->bit)
			len += snprintf(buf + len, sizeof(buf) - len, "%s%s",
					len ? " " : "", entry->desc);

	dev_info(&root->device->dev, "_OSC: %s [%s]\n", msg, buf);
}

static void decode_osc_support(struct acpi_pci_root *root, char *msg, u32 word)
{
	decode_osc_bits(root, msg, word, pci_osc_support_bit,
			ARRAY_SIZE(pci_osc_support_bit));
}

static void decode_osc_control(struct acpi_pci_root *root, char *msg, u32 word)
{
	decode_osc_bits(root, msg, word, pci_osc_control_bit,
			ARRAY_SIZE(pci_osc_control_bit));
}

static u8 pci_osc_uuid_str[] = "33DB4D5B-1FF7-401C-9657-7441C03DD766";

static acpi_status acpi_pci_run_osc(acpi_handle handle,
				    const u32 *capbuf, u32 *retval)
{
	struct acpi_osc_context context = {
		.uuid_str = pci_osc_uuid_str,
		.rev = 1,
		.cap.length = 12,
		.cap.pointer = (void *)capbuf,
	};
	acpi_status status;

	status = acpi_run_osc(handle, &context);
	if (ACPI_SUCCESS(status)) {
		*retval = *((u32 *)(context.ret.pointer + 8));
		kfree(context.ret.pointer);
	}
	return status;
}

static acpi_status acpi_pci_query_osc(struct acpi_pci_root *root,
					u32 support,
					u32 *control)
{
	acpi_status status;
	u32 result, capbuf[3];

	support &= OSC_PCI_SUPPORT_MASKS;
	support |= root->osc_support_set;

	capbuf[OSC_QUERY_DWORD] = OSC_QUERY_ENABLE;
	capbuf[OSC_SUPPORT_DWORD] = support;
	if (control) {
		*control &= OSC_PCI_CONTROL_MASKS;
		capbuf[OSC_CONTROL_DWORD] = *control | root->osc_control_set;
	} else {
		/* Run _OSC query only with existing controls. */
		capbuf[OSC_CONTROL_DWORD] = root->osc_control_set;
	}

	status = acpi_pci_run_osc(root->device->handle, capbuf, &result);
	if (ACPI_SUCCESS(status)) {
		root->osc_support_set = support;
		if (control)
			*control = result;
	}
	return status;
}

static acpi_status acpi_pci_osc_support(struct acpi_pci_root *root, u32 flags)
{
	acpi_status status;

	mutex_lock(&osc_lock);
	status = acpi_pci_query_osc(root, flags, NULL);
	mutex_unlock(&osc_lock);
	return status;
}

struct acpi_pci_root *acpi_pci_find_root(acpi_handle handle)
{
	struct acpi_pci_root *root;
	struct acpi_device *device;

	if (acpi_bus_get_device(handle, &device) ||
	    acpi_match_device_ids(device, root_device_ids))
		return NULL;

	root = acpi_driver_data(device);

	return root;
}
EXPORT_SYMBOL_GPL(acpi_pci_find_root);

struct acpi_handle_node {
	struct list_head node;
	acpi_handle handle;
};

/**
 * acpi_get_pci_dev - convert ACPI CA handle to struct pci_dev
 * @handle: the handle in question
 *
 * Given an ACPI CA handle, the desired PCI device is located in the
 * list of PCI devices.
 *
 * If the device is found, its reference count is increased and this
 * function returns a pointer to its data structure.  The caller must
 * decrement the reference count by calling pci_dev_put().
 * If no device is found, %NULL is returned.
 */
struct pci_dev *acpi_get_pci_dev(acpi_handle handle)
{
	int dev, fn;
	unsigned long long adr;
	acpi_status status;
	acpi_handle phandle;
	struct pci_bus *pbus;
	struct pci_dev *pdev = NULL;
	struct acpi_handle_node *node, *tmp;
	struct acpi_pci_root *root;
	LIST_HEAD(device_list);

	/*
	 * Walk up the ACPI CA namespace until we reach a PCI root bridge.
	 */
	phandle = handle;
	while (!acpi_is_root_bridge(phandle)) {
		node = kzalloc(sizeof(struct acpi_handle_node), GFP_KERNEL);
		if (!node)
			goto out;

		INIT_LIST_HEAD(&node->node);
		node->handle = phandle;
		list_add(&node->node, &device_list);

		status = acpi_get_parent(phandle, &phandle);
		if (ACPI_FAILURE(status))
			goto out;
	}

	root = acpi_pci_find_root(phandle);
	if (!root)
		goto out;

	pbus = root->bus;

	/*
	 * Now, walk back down the PCI device tree until we return to our
	 * original handle. Assumes that everything between the PCI root
	 * bridge and the device we're looking for must be a P2P bridge.
	 */
	list_for_each_entry(node, &device_list, node) {
		acpi_handle hnd = node->handle;
		status = acpi_evaluate_integer(hnd, "_ADR", NULL, &adr);
		if (ACPI_FAILURE(status))
			goto out;
		dev = (adr >> 16) & 0xffff;
		fn  = adr & 0xffff;

		pdev = pci_get_slot(pbus, PCI_DEVFN(dev, fn));
		if (!pdev || hnd == handle)
			break;

		pbus = pdev->subordinate;
		pci_dev_put(pdev);

		/*
		 * This function may be called for a non-PCI device that has a
		 * PCI parent (eg. a disk under a PCI SATA controller).  In that
		 * case pdev->subordinate will be NULL for the parent.
		 */
		if (!pbus) {
			dev_dbg(&pdev->dev, "Not a PCI-to-PCI bridge\n");
			pdev = NULL;
			break;
		}
	}
out:
	list_for_each_entry_safe(node, tmp, &device_list, node)
		kfree(node);

	return pdev;
}
EXPORT_SYMBOL_GPL(acpi_get_pci_dev);

/**
 * acpi_pci_osc_control_set - Request control of PCI root _OSC features.
 * @handle: ACPI handle of a PCI root bridge (or PCIe Root Complex).
 * @mask: Mask of _OSC bits to request control of, place to store control mask.
 * @req: Mask of _OSC bits the control of is essential to the caller.
 *
 * Run _OSC query for @mask and if that is successful, compare the returned
 * mask of control bits with @req.  If all of the @req bits are set in the
 * returned mask, run _OSC request for it.
 *
 * The variable at the @mask address may be modified regardless of whether or
 * not the function returns success.  On success it will contain the mask of
 * _OSC bits the BIOS has granted control of, but its contents are meaningless
 * on failure.
 **/
acpi_status acpi_pci_osc_control_set(acpi_handle handle, u32 *mask, u32 req)
{
	struct acpi_pci_root *root;
	acpi_status status = AE_OK;
	u32 ctrl, capbuf[3];

	if (!mask)
		return AE_BAD_PARAMETER;

	ctrl = *mask & OSC_PCI_CONTROL_MASKS;
	if ((ctrl & req) != req)
		return AE_TYPE;

	root = acpi_pci_find_root(handle);
	if (!root)
		return AE_NOT_EXIST;

	mutex_lock(&osc_lock);

	*mask = ctrl | root->osc_control_set;
	/* No need to evaluate _OSC if the control was already granted. */
	if ((root->osc_control_set & ctrl) == ctrl)
		goto out;

	/* Need to check the available controls bits before requesting them. */
	while (*mask) {
		status = acpi_pci_query_osc(root, root->osc_support_set, mask);
		if (ACPI_FAILURE(status))
			goto out;
		if (ctrl == *mask)
			break;
		decode_osc_control(root, "platform does not support",
				   ctrl & ~(*mask));
		ctrl = *mask;
	}

	if ((ctrl & req) != req) {
		decode_osc_control(root, "not requesting control; platform does not support",
				   req & ~(ctrl));
		status = AE_SUPPORT;
		goto out;
	}

	capbuf[OSC_QUERY_DWORD] = 0;
	capbuf[OSC_SUPPORT_DWORD] = root->osc_support_set;
	capbuf[OSC_CONTROL_DWORD] = ctrl;
	status = acpi_pci_run_osc(handle, capbuf, mask);
	if (ACPI_SUCCESS(status))
		root->osc_control_set = *mask;
out:
	mutex_unlock(&osc_lock);
	return status;
}
EXPORT_SYMBOL(acpi_pci_osc_control_set);

static void negotiate_os_control(struct acpi_pci_root *root, int *no_aspm,
				 bool is_pcie)
{
	u32 support, control, requested;
	acpi_status status;
	struct acpi_device *device = root->device;
	acpi_handle handle = device->handle;

	/*
	 * Apple always return failure on _OSC calls when _OSI("Darwin") has
	 * been called successfully. We know the feature set supported by the
	 * platform, so avoid calling _OSC at all
	 */
	if (x86_apple_machine) {
		root->osc_control_set = ~OSC_PCI_EXPRESS_PME_CONTROL;
		decode_osc_control(root, "OS assumes control of",
				   root->osc_control_set);
		return;
	}

	/*
	 * All supported architectures that use ACPI have support for
	 * PCI domains, so we indicate this in _OSC support capabilities.
	 */
	support = OSC_PCI_SEGMENT_GROUPS_SUPPORT;
	if (pci_ext_cfg_avail())
		support |= OSC_PCI_EXT_CONFIG_SUPPORT;
	if (pcie_aspm_support_enabled())
		support |= OSC_PCI_ASPM_SUPPORT | OSC_PCI_CLOCK_PM_SUPPORT;
	if (pci_msi_enabled())
		support |= OSC_PCI_MSI_SUPPORT;

	decode_osc_support(root, "OS supports", support);
	status = acpi_pci_osc_support(root, support);
	if (ACPI_FAILURE(status)) {
		*no_aspm = 1;

		/* _OSC is optional for PCI host bridges */
		if ((status == AE_NOT_FOUND) && !is_pcie)
			return;

		dev_info(&device->dev, "_OSC failed (%s)%s\n",
			 acpi_format_exception(status),
			 pcie_aspm_support_enabled() ? "; disabling ASPM" : "");
		return;
	}

	if (pcie_ports_disabled) {
		dev_info(&device->dev, "PCIe port services disabled; not requesting _OSC control\n");
		return;
	}

	if ((support & ACPI_PCIE_REQ_SUPPORT) != ACPI_PCIE_REQ_SUPPORT) {
		decode_osc_support(root, "not requesting OS control; OS requires",
				   ACPI_PCIE_REQ_SUPPORT);
		return;
	}

	control = OSC_PCI_EXPRESS_CAPABILITY_CONTROL
		| OSC_PCI_EXPRESS_PME_CONTROL;

	if (IS_ENABLED(CONFIG_PCIEASPM))
		control |= OSC_PCI_EXPRESS_LTR_CONTROL;

	if (IS_ENABLED(CONFIG_HOTPLUG_PCI_PCIE))
		control |= OSC_PCI_EXPRESS_NATIVE_HP_CONTROL;

	if (IS_ENABLED(CONFIG_HOTPLUG_PCI_SHPC))
		control |= OSC_PCI_SHPC_NATIVE_HP_CONTROL;

	if (pci_aer_available()) {
		if (aer_acpi_firmware_first())
			dev_info(&device->dev,
				 "PCIe AER handled by firmware\n");
		else
			control |= OSC_PCI_EXPRESS_AER_CONTROL;
	}

	requested = control;
	status = acpi_pci_osc_control_set(handle, &control,
					  OSC_PCI_EXPRESS_CAPABILITY_CONTROL);
	if (ACPI_SUCCESS(status)) {
		decode_osc_control(root, "OS now controls", control);
		if (acpi_gbl_FADT.boot_flags & ACPI_FADT_NO_ASPM) {
			/*
			 * We have ASPM control, but the FADT indicates that
			 * it's unsupported. Leave existing configuration
			 * intact and prevent the OS from touching it.
			 */
			dev_info(&device->dev, "FADT indicates ASPM is unsupported, using BIOS configuration\n");
			*no_aspm = 1;
		}
	} else {
		decode_osc_control(root, "OS requested", requested);
		decode_osc_control(root, "platform willing to grant", control);
		dev_info(&device->dev, "_OSC failed (%s); disabling ASPM\n",
			acpi_format_exception(status));

		/*
		 * We want to disable ASPM here, but aspm_disabled
		 * needs to remain in its state from boot so that we
		 * properly handle PCIe 1.1 devices.  So we set this
		 * flag here, to defer the action until after the ACPI
		 * root scan.
		 */
		*no_aspm = 1;
	}
}

static int acpi_pci_root_add(struct acpi_device *device,
			     const struct acpi_device_id *not_used)
{
	unsigned long long segment, bus;
	acpi_status status;
	int result;
	struct acpi_pci_root *root;
	acpi_handle handle = device->handle;
	int no_aspm = 0;
	bool hotadd = system_state == SYSTEM_RUNNING;
	bool is_pcie;

	root = kzalloc(sizeof(struct acpi_pci_root), GFP_KERNEL);
	if (!root)
		return -ENOMEM;

	segment = 0;
	status = acpi_evaluate_integer(handle, METHOD_NAME__SEG, NULL,
				       &segment);
	if (ACPI_FAILURE(status) && status != AE_NOT_FOUND) {
		dev_err(&device->dev,  "can't evaluate _SEG\n");
		result = -ENODEV;
		goto end;
	}

	/* Check _CRS first, then _BBN.  If no _BBN, default to zero. */
	root->secondary.flags = IORESOURCE_BUS;
	status = try_get_root_bridge_busnr(handle, &root->secondary);
	if (ACPI_FAILURE(status)) {
		/*
		 * We need both the start and end of the downstream bus range
		 * to interpret _CBA (MMCONFIG base address), so it really is
		 * supposed to be in _CRS.  If we don't find it there, all we
		 * can do is assume [_BBN-0xFF] or [0-0xFF].
		 */
		root->secondary.end = 0xFF;
		dev_warn(&device->dev,
			 FW_BUG "no secondary bus range in _CRS\n");
		status = acpi_evaluate_integer(handle, METHOD_NAME__BBN,
					       NULL, &bus);
		if (ACPI_SUCCESS(status))
			root->secondary.start = bus;
		else if (status == AE_NOT_FOUND)
			root->secondary.start = 0;
		else {
			dev_err(&device->dev, "can't evaluate _BBN\n");
			result = -ENODEV;
			goto end;
		}
	}

	root->device = device;
	root->segment = segment & 0xFFFF;
	strcpy(acpi_device_name(device), ACPI_PCI_ROOT_DEVICE_NAME);
	strcpy(acpi_device_class(device), ACPI_PCI_ROOT_CLASS);
	device->driver_data = root;

	if (hotadd && dmar_device_add(handle)) {
		result = -ENXIO;
		goto end;
	}

	pr_info(PREFIX "%s [%s] (domain %04x %pR)\n",
	       acpi_device_name(device), acpi_device_bid(device),
	       root->segment, &root->secondary);

	root->mcfg_addr = acpi_pci_root_get_mcfg_addr(handle);

	is_pcie = strcmp(acpi_device_hid(device), "PNP0A08") == 0;
	negotiate_os_control(root, &no_aspm, is_pcie);

	/*
	 * TBD: Need PCI interface for enumeration/configuration of roots.
	 */

	/*
	 * Scan the Root Bridge
	 * --------------------
	 * Must do this prior to any attempt to bind the root device, as the
	 * PCI namespace does not get created until this call is made (and
	 * thus the root bridge's pci_dev does not exist).
	 */
	root->bus = pci_acpi_scan_root(root);
	if (!root->bus) {
		dev_err(&device->dev,
			"Bus %04x:%02x not present in PCI namespace\n",
			root->segment, (unsigned int)root->secondary.start);
		device->driver_data = NULL;
		result = -ENODEV;
		goto remove_dmar;
	}

	if (no_aspm)
		pcie_no_aspm();

	pci_acpi_add_bus_pm_notifier(device);
	device_set_wakeup_capable(root->bus->bridge, device->wakeup.flags.valid);

	if (hotadd) {
		pcibios_resource_survey_bus(root->bus);
		pci_assign_unassigned_root_bus_resources(root->bus);
		/*
		 * This is only called for the hotadd case. For the boot-time
		 * case, we need to wait until after PCI initialization in
		 * order to deal with IOAPICs mapped in on a PCI BAR.
		 *
		 * This is currently x86-specific, because acpi_ioapic_add()
		 * is an empty function without CONFIG_ACPI_HOTPLUG_IOAPIC.
		 * And CONFIG_ACPI_HOTPLUG_IOAPIC depends on CONFIG_X86_IO_APIC
		 * (see drivers/acpi/Kconfig).
		 */
		acpi_ioapic_add(root->device->handle);
	}

	pci_lock_rescan_remove();
	pci_bus_add_devices(root->bus);
	pci_unlock_rescan_remove();
	return 1;

remove_dmar:
	if (hotadd)
		dmar_device_remove(handle);
end:
	kfree(root);
	return result;
}

static void acpi_pci_root_remove(struct acpi_device *device)
{
	struct acpi_pci_root *root = acpi_driver_data(device);

	pci_lock_rescan_remove();

	pci_stop_root_bus(root->bus);

	pci_ioapic_remove(root);
	device_set_wakeup_capable(root->bus->bridge, false);
	pci_acpi_remove_bus_pm_notifier(device);

	pci_remove_root_bus(root->bus);
	WARN_ON(acpi_ioapic_remove(root));

	dmar_device_remove(device->handle);

	pci_unlock_rescan_remove();

	kfree(root);
}

/*
 * Following code to support acpi_pci_root_create() is copied from
 * arch/x86/pci/acpi.c and modified so it could be reused by x86, IA64
 * and ARM64.
 */
static void acpi_pci_root_validate_resources(struct device *dev,
					     struct list_head *resources,
					     unsigned long type)
{
	LIST_HEAD(list);
	struct resource *res1, *res2, *root = NULL;
	struct resource_entry *tmp, *entry, *entry2;

	BUG_ON((type & (IORESOURCE_MEM | IORESOURCE_IO)) == 0);
	root = (type & IORESOURCE_MEM) ? &iomem_resource : &ioport_resource;

	list_splice_init(resources, &list);
	resource_list_for_each_entry_safe(entry, tmp, &list) {
		bool free = false;
		resource_size_t end;

		res1 = entry->res;
		if (!(res1->flags & type))
			goto next;

		/* Exclude non-addressable range or non-addressable portion */
		end = min(res1->end, root->end);
		if (end <= res1->start) {
			dev_info(dev, "host bridge window %pR (ignored, not CPU addressable)\n",
				 res1);
			free = true;
			goto next;
		} else if (res1->end != end) {
			dev_info(dev, "host bridge window %pR ([%#llx-%#llx] ignored, not CPU addressable)\n",
				 res1, (unsigned long long)end + 1,
				 (unsigned long long)res1->end);
			res1->end = end;
		}

		resource_list_for_each_entry(entry2, resources) {
			res2 = entry2->res;
			if (!(res2->flags & type))
				continue;

			/*
			 * I don't like throwing away windows because then
			 * our resources no longer match the ACPI _CRS, but
			 * the kernel resource tree doesn't allow overlaps.
			 */
			if (resource_overlaps(res1, res2)) {
				res2->start = min(res1->start, res2->start);
				res2->end = max(res1->end, res2->end);
				dev_info(dev, "host bridge window expanded to %pR; %pR ignored\n",
					 res2, res1);
				free = true;
				goto next;
			}
		}

next:
		resource_list_del(entry);
		if (free)
			resource_list_free_entry(entry);
		else
			resource_list_add_tail(entry, resources);
	}
}

static void acpi_pci_root_remap_iospace(struct fwnode_handle *fwnode,
			struct resource_entry *entry)
{
#ifdef PCI_IOBASE
	struct resource *res = entry->res;
	resource_size_t cpu_addr = res->start;
	resource_size_t pci_addr = cpu_addr - entry->offset;
	resource_size_t length = resource_size(res);
	unsigned long port;

	if (pci_register_io_range(fwnode, cpu_addr, length))
		goto err;

	port = pci_address_to_pio(cpu_addr);
	if (port == (unsigned long)-1)
		goto err;

	res->start = port;
	res->end = port + length - 1;
	entry->offset = port - pci_addr;

	if (pci_remap_iospace(res, cpu_addr) < 0)
		goto err;

	pr_info("Remapped I/O %pa to %pR\n", &cpu_addr, res);
	return;
err:
	res->flags |= IORESOURCE_DISABLED;
#endif
}

int acpi_pci_probe_root_resources(struct acpi_pci_root_info *info)
{
	int ret;
	struct list_head *list = &info->resources;
	struct acpi_device *device = info->bridge;
	struct resource_entry *entry, *tmp;
	unsigned long flags;

	flags = IORESOURCE_IO | IORESOURCE_MEM | IORESOURCE_MEM_8AND16BIT;
	ret = acpi_dev_get_resources(device, list,
				     acpi_dev_filter_resource_type_cb,
				     (void *)flags);
	if (ret < 0)
		dev_warn(&device->dev,
			 "failed to parse _CRS method, error code %d\n", ret);
	else if (ret == 0)
		dev_dbg(&device->dev,
			"no IO and memory resources present in _CRS\n");
	else {
		resource_list_for_each_entry_safe(entry, tmp, list) {
			if (entry->res->flags & IORESOURCE_IO)
				acpi_pci_root_remap_iospace(&device->fwnode,
						entry);

			if (entry->res->flags & IORESOURCE_DISABLED)
				resource_list_destroy_entry(entry);
			else
				entry->res->name = info->name;
		}
		acpi_pci_root_validate_resources(&device->dev, list,
						 IORESOURCE_MEM);
		acpi_pci_root_validate_resources(&device->dev, list,
						 IORESOURCE_IO);
	}

	return ret;
}

static void pci_acpi_root_add_resources(struct acpi_pci_root_info *info)
{
	struct resource_entry *entry, *tmp;
	struct resource *res, *conflict, *root = NULL;

	resource_list_for_each_entry_safe(entry, tmp, &info->resources) {
		res = entry->res;
		if (res->flags & IORESOURCE_MEM)
			root = &iomem_resource;
		else if (res->flags & IORESOURCE_IO)
			root = &ioport_resource;
		else
			continue;

		/*
		 * Some legacy x86 host bridge drivers use iomem_resource and
		 * ioport_resource as default resource pool, skip it.
		 */
		if (res == root)
			continue;

		conflict = insert_resource_conflict(root, res);
		if (conflict) {
			dev_info(&info->bridge->dev,
				 "ignoring host bridge window %pR (conflicts with %s %pR)\n",
				 res, conflict->name, conflict);
			resource_list_destroy_entry(entry);
		}
	}
}

static void __acpi_pci_root_release_info(struct acpi_pci_root_info *info)
{
	struct resource *res;
	struct resource_entry *entry, *tmp;

	if (!info)
		return;

	resource_list_for_each_entry_safe(entry, tmp, &info->resources) {
		res = entry->res;
		if (res->parent &&
		    (res->flags & (IORESOURCE_MEM | IORESOURCE_IO)))
			release_resource(res);
		resource_list_destroy_entry(entry);
	}

	info->ops->release_info(info);
}

static void acpi_pci_root_release_info(struct pci_host_bridge *bridge)
{
	struct resource *res;
	struct resource_entry *entry;

	resource_list_for_each_entry(entry, &bridge->windows) {
		res = entry->res;
		if (res->flags & IORESOURCE_IO)
			pci_unmap_iospace(res);
		if (res->parent &&
		    (res->flags & (IORESOURCE_MEM | IORESOURCE_IO)))
			release_resource(res);
	}
	__acpi_pci_root_release_info(bridge->release_data);
}

struct pci_bus *acpi_pci_root_create(struct acpi_pci_root *root,
				     struct acpi_pci_root_ops *ops,
				     struct acpi_pci_root_info *info,
				     void *sysdata)
{
	int ret, busnum = root->secondary.start;
	struct acpi_device *device = root->device;
	int node = acpi_get_node(device->handle);
	struct pci_bus *bus;
	struct pci_host_bridge *host_bridge;

	info->root = root;
	info->bridge = device;
	info->ops = ops;
	INIT_LIST_HEAD(&info->resources);
	snprintf(info->name, sizeof(info->name), "PCI Bus %04x:%02x",
		 root->segment, busnum);

	if (ops->init_info && ops->init_info(info))
		goto out_release_info;
	if (ops->prepare_resources)
		ret = ops->prepare_resources(info);
	else
		ret = acpi_pci_probe_root_resources(info);
	if (ret < 0)
		goto out_release_info;

	pci_acpi_root_add_resources(info);
	pci_add_resource(&info->resources, &root->secondary);
	bus = pci_create_root_bus(NULL, busnum, ops->pci_ops,
				  sysdata, &info->resources);
	if (!bus)
		goto out_release_info;

	host_bridge = to_pci_host_bridge(bus->bridge);
	if (!(root->osc_control_set & OSC_PCI_EXPRESS_NATIVE_HP_CONTROL))
		host_bridge->native_pcie_hotplug = 0;
	if (!(root->osc_control_set & OSC_PCI_SHPC_NATIVE_HP_CONTROL))
		host_bridge->native_shpc_hotplug = 0;
	if (!(root->osc_control_set & OSC_PCI_EXPRESS_AER_CONTROL))
		host_bridge->native_aer = 0;
	if (!(root->osc_control_set & OSC_PCI_EXPRESS_PME_CONTROL))
		host_bridge->native_pme = 0;
	if (!(root->osc_control_set & OSC_PCI_EXPRESS_LTR_CONTROL))
		host_bridge->native_ltr = 0;

	pci_scan_child_bus(bus);
	pci_set_host_bridge_release(host_bridge, acpi_pci_root_release_info,
				    info);
	if (node != NUMA_NO_NODE)
		dev_printk(KERN_DEBUG, &bus->dev, "on NUMA node %d\n", node);
	return bus;

out_release_info:
	__acpi_pci_root_release_info(info);
	return NULL;
}

void __init acpi_pci_root_init(void)
{
	acpi_hest_init();
	if (acpi_pci_disabled)
		return;

	pci_acpi_crs_quirks();
	acpi_scan_add_handler_with_hotplug(&pci_root_handler, "pci_root");
}
