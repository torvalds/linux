// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  pci_root.c - ACPI PCI Root Bridge Driver ($Revision: 40 $)
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 */

#define pr_fmt(fmt) "ACPI: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/pci.h>
#include <linux/pci-acpi.h>
#include <linux/dmar.h>
#include <linux/acpi.h>
#include <linux/slab.h>
#include <linux/dmi.h>
#include <linux/platform_data/x86/apple.h>
#include "internal.h"

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

/**
 * acpi_is_root_bridge - determine whether an ACPI CA node is a PCI root bridge
 * @handle:  the ACPI CA node in question.
 *
 * Note: we could make this API take a struct acpi_device * instead, but
 * for now, it's more convenient to operate on an acpi_handle.
 */
int acpi_is_root_bridge(acpi_handle handle)
{
	struct acpi_device *device = acpi_fetch_acpi_dev(handle);
	int ret;

	if (!device)
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
	{ OSC_PCI_EDR_SUPPORT, "EDR" },
	{ OSC_PCI_HPX_TYPE_3_SUPPORT, "HPX-Type3" },
};

static struct pci_osc_bit_struct pci_osc_control_bit[] = {
	{ OSC_PCI_EXPRESS_NATIVE_HP_CONTROL, "PCIeHotplug" },
	{ OSC_PCI_SHPC_NATIVE_HP_CONTROL, "SHPCHotplug" },
	{ OSC_PCI_EXPRESS_PME_CONTROL, "PME" },
	{ OSC_PCI_EXPRESS_AER_CONTROL, "AER" },
	{ OSC_PCI_EXPRESS_CAPABILITY_CONTROL, "PCIeCapability" },
	{ OSC_PCI_EXPRESS_LTR_CONTROL, "LTR" },
	{ OSC_PCI_EXPRESS_DPC_CONTROL, "DPC" },
};

static struct pci_osc_bit_struct cxl_osc_support_bit[] = {
	{ OSC_CXL_1_1_PORT_REG_ACCESS_SUPPORT, "CXL11PortRegAccess" },
	{ OSC_CXL_2_0_PORT_DEV_REG_ACCESS_SUPPORT, "CXL20PortDevRegAccess" },
	{ OSC_CXL_PROTOCOL_ERR_REPORTING_SUPPORT, "CXLProtocolErrorReporting" },
	{ OSC_CXL_NATIVE_HP_SUPPORT, "CXLNativeHotPlug" },
};

static struct pci_osc_bit_struct cxl_osc_control_bit[] = {
	{ OSC_CXL_ERROR_REPORTING_CONTROL, "CXLMemErrorReporting" },
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
			len += scnprintf(buf + len, sizeof(buf) - len, "%s%s",
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

static void decode_cxl_osc_support(struct acpi_pci_root *root, char *msg, u32 word)
{
	decode_osc_bits(root, msg, word, cxl_osc_support_bit,
			ARRAY_SIZE(cxl_osc_support_bit));
}

static void decode_cxl_osc_control(struct acpi_pci_root *root, char *msg, u32 word)
{
	decode_osc_bits(root, msg, word, cxl_osc_control_bit,
			ARRAY_SIZE(cxl_osc_control_bit));
}

static inline bool is_pcie(struct acpi_pci_root *root)
{
	return root->bridge_type == ACPI_BRIDGE_TYPE_PCIE;
}

static inline bool is_cxl(struct acpi_pci_root *root)
{
	return root->bridge_type == ACPI_BRIDGE_TYPE_CXL;
}

static u8 pci_osc_uuid_str[] = "33DB4D5B-1FF7-401C-9657-7441C03DD766";
static u8 cxl_osc_uuid_str[] = "68F2D50B-C469-4d8A-BD3D-941A103FD3FC";

static char *to_uuid(struct acpi_pci_root *root)
{
	if (is_cxl(root))
		return cxl_osc_uuid_str;
	return pci_osc_uuid_str;
}

static int cap_length(struct acpi_pci_root *root)
{
	if (is_cxl(root))
		return sizeof(u32) * OSC_CXL_CAPABILITY_DWORDS;
	return sizeof(u32) * OSC_PCI_CAPABILITY_DWORDS;
}

static acpi_status acpi_pci_run_osc(struct acpi_pci_root *root,
				    const u32 *capbuf, u32 *pci_control,
				    u32 *cxl_control)
{
	struct acpi_osc_context context = {
		.uuid_str = to_uuid(root),
		.rev = 1,
		.cap.length = cap_length(root),
		.cap.pointer = (void *)capbuf,
	};
	acpi_status status;

	status = acpi_run_osc(root->device->handle, &context);
	if (ACPI_SUCCESS(status)) {
		*pci_control = acpi_osc_ctx_get_pci_control(&context);
		if (is_cxl(root))
			*cxl_control = acpi_osc_ctx_get_cxl_control(&context);
		kfree(context.ret.pointer);
	}
	return status;
}

static acpi_status acpi_pci_query_osc(struct acpi_pci_root *root, u32 support,
				      u32 *control, u32 cxl_support,
				      u32 *cxl_control)
{
	acpi_status status;
	u32 pci_result, cxl_result, capbuf[OSC_CXL_CAPABILITY_DWORDS];

	support |= root->osc_support_set;

	capbuf[OSC_QUERY_DWORD] = OSC_QUERY_ENABLE;
	capbuf[OSC_SUPPORT_DWORD] = support;
	capbuf[OSC_CONTROL_DWORD] = *control | root->osc_control_set;

	if (is_cxl(root)) {
		cxl_support |= root->osc_ext_support_set;
		capbuf[OSC_EXT_SUPPORT_DWORD] = cxl_support;
		capbuf[OSC_EXT_CONTROL_DWORD] = *cxl_control | root->osc_ext_control_set;
	}

retry:
	status = acpi_pci_run_osc(root, capbuf, &pci_result, &cxl_result);
	if (ACPI_SUCCESS(status)) {
		root->osc_support_set = support;
		*control = pci_result;
		if (is_cxl(root)) {
			root->osc_ext_support_set = cxl_support;
			*cxl_control = cxl_result;
		}
	} else if (is_cxl(root)) {
		/*
		 * CXL _OSC is optional on CXL 1.1 hosts. Fall back to PCIe _OSC
		 * upon any failure using CXL _OSC.
		 */
		root->bridge_type = ACPI_BRIDGE_TYPE_PCIE;
		goto retry;
	}
	return status;
}

struct acpi_pci_root *acpi_pci_find_root(acpi_handle handle)
{
	struct acpi_device *device = acpi_fetch_acpi_dev(handle);
	struct acpi_pci_root *root;

	if (!device || acpi_match_device_ids(device, root_device_ids))
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
	struct acpi_device *adev = acpi_fetch_acpi_dev(handle);
	struct acpi_device_physical_node *pn;
	struct pci_dev *pci_dev = NULL;

	if (!adev)
		return NULL;

	mutex_lock(&adev->physical_node_lock);

	list_for_each_entry(pn, &adev->physical_node_list, node) {
		if (dev_is_pci(pn->dev)) {
			get_device(pn->dev);
			pci_dev = to_pci_dev(pn->dev);
			break;
		}
	}

	mutex_unlock(&adev->physical_node_lock);

	return pci_dev;
}
EXPORT_SYMBOL_GPL(acpi_get_pci_dev);

/**
 * acpi_pci_osc_control_set - Request control of PCI root _OSC features.
 * @handle: ACPI handle of a PCI root bridge (or PCIe Root Complex).
 * @mask: Mask of _OSC bits to request control of, place to store control mask.
 * @support: _OSC supported capability.
 * @cxl_mask: Mask of CXL _OSC control bits, place to store control mask.
 * @cxl_support: CXL _OSC supported capability.
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
static acpi_status acpi_pci_osc_control_set(acpi_handle handle, u32 *mask,
					    u32 support, u32 *cxl_mask,
					    u32 cxl_support)
{
	u32 req = OSC_PCI_EXPRESS_CAPABILITY_CONTROL;
	struct acpi_pci_root *root;
	acpi_status status;
	u32 ctrl, cxl_ctrl = 0, capbuf[OSC_CXL_CAPABILITY_DWORDS];

	if (!mask)
		return AE_BAD_PARAMETER;

	root = acpi_pci_find_root(handle);
	if (!root)
		return AE_NOT_EXIST;

	ctrl   = *mask;
	*mask |= root->osc_control_set;

	if (is_cxl(root)) {
		cxl_ctrl = *cxl_mask;
		*cxl_mask |= root->osc_ext_control_set;
	}

	/* Need to check the available controls bits before requesting them. */
	do {
		u32 pci_missing = 0, cxl_missing = 0;

		status = acpi_pci_query_osc(root, support, mask, cxl_support,
					    cxl_mask);
		if (ACPI_FAILURE(status))
			return status;
		if (is_cxl(root)) {
			if (ctrl == *mask && cxl_ctrl == *cxl_mask)
				break;
			pci_missing = ctrl & ~(*mask);
			cxl_missing = cxl_ctrl & ~(*cxl_mask);
		} else {
			if (ctrl == *mask)
				break;
			pci_missing = ctrl & ~(*mask);
		}
		if (pci_missing)
			decode_osc_control(root, "platform does not support",
					   pci_missing);
		if (cxl_missing)
			decode_cxl_osc_control(root, "CXL platform does not support",
					   cxl_missing);
		ctrl = *mask;
		cxl_ctrl = *cxl_mask;
	} while (*mask || *cxl_mask);

	/* No need to request _OSC if the control was already granted. */
	if ((root->osc_control_set & ctrl) == ctrl &&
	    (root->osc_ext_control_set & cxl_ctrl) == cxl_ctrl)
		return AE_OK;

	if ((ctrl & req) != req) {
		decode_osc_control(root, "not requesting control; platform does not support",
				   req & ~(ctrl));
		return AE_SUPPORT;
	}

	capbuf[OSC_QUERY_DWORD] = 0;
	capbuf[OSC_SUPPORT_DWORD] = root->osc_support_set;
	capbuf[OSC_CONTROL_DWORD] = ctrl;
	if (is_cxl(root)) {
		capbuf[OSC_EXT_SUPPORT_DWORD] = root->osc_ext_support_set;
		capbuf[OSC_EXT_CONTROL_DWORD] = cxl_ctrl;
	}

	status = acpi_pci_run_osc(root, capbuf, mask, cxl_mask);
	if (ACPI_FAILURE(status))
		return status;

	root->osc_control_set = *mask;
	root->osc_ext_control_set = *cxl_mask;
	return AE_OK;
}

static u32 calculate_support(void)
{
	u32 support;

	/*
	 * All supported architectures that use ACPI have support for
	 * PCI domains, so we indicate this in _OSC support capabilities.
	 */
	support = OSC_PCI_SEGMENT_GROUPS_SUPPORT;
	support |= OSC_PCI_HPX_TYPE_3_SUPPORT;
	if (pci_ext_cfg_avail())
		support |= OSC_PCI_EXT_CONFIG_SUPPORT;
	if (pcie_aspm_support_enabled())
		support |= OSC_PCI_ASPM_SUPPORT | OSC_PCI_CLOCK_PM_SUPPORT;
	if (pci_msi_enabled())
		support |= OSC_PCI_MSI_SUPPORT;
	if (IS_ENABLED(CONFIG_PCIE_EDR))
		support |= OSC_PCI_EDR_SUPPORT;

	return support;
}

/*
 * Background on hotplug support, and making it depend on only
 * CONFIG_HOTPLUG_PCI_PCIE vs. also considering CONFIG_MEMORY_HOTPLUG:
 *
 * CONFIG_ACPI_HOTPLUG_MEMORY does depend on CONFIG_MEMORY_HOTPLUG, but
 * there is no existing _OSC for memory hotplug support. The reason is that
 * ACPI memory hotplug requires the OS to acknowledge / coordinate with
 * memory plug events via a scan handler. On the CXL side the equivalent
 * would be if Linux supported the Mechanical Retention Lock [1], or
 * otherwise had some coordination for the driver of a PCI device
 * undergoing hotplug to be consulted on whether the hotplug should
 * proceed or not.
 *
 * The concern is that if Linux says no to supporting CXL hotplug then
 * the BIOS may say no to giving the OS hotplug control of any other PCIe
 * device. So the question here is not whether hotplug is enabled, it's
 * whether it is handled natively by the at all OS, and if
 * CONFIG_HOTPLUG_PCI_PCIE is enabled then the answer is "yes".
 *
 * Otherwise, the plan for CXL coordinated remove, since the kernel does
 * not support blocking hotplug, is to require the memory device to be
 * disabled before hotplug is attempted. When CONFIG_MEMORY_HOTPLUG is
 * disabled that step will fail and the remove attempt cancelled by the
 * user. If that is not honored and the card is removed anyway then it
 * does not matter if CONFIG_MEMORY_HOTPLUG is enabled or not, it will
 * cause a crash and other badness.
 *
 * Therefore, just say yes to CXL hotplug and require removal to
 * be coordinated by userspace unless and until the kernel grows better
 * mechanisms for doing "managed" removal of devices in consultation with
 * the driver.
 *
 * [1]: https://lore.kernel.org/all/20201122014203.4706-1-ashok.raj@intel.com/
 */
static u32 calculate_cxl_support(void)
{
	u32 support;

	support = OSC_CXL_2_0_PORT_DEV_REG_ACCESS_SUPPORT;
	support |= OSC_CXL_1_1_PORT_REG_ACCESS_SUPPORT;
	if (pci_aer_available())
		support |= OSC_CXL_PROTOCOL_ERR_REPORTING_SUPPORT;
	if (IS_ENABLED(CONFIG_HOTPLUG_PCI_PCIE))
		support |= OSC_CXL_NATIVE_HP_SUPPORT;

	return support;
}

static u32 calculate_control(void)
{
	u32 control;

	control = OSC_PCI_EXPRESS_CAPABILITY_CONTROL
		| OSC_PCI_EXPRESS_PME_CONTROL;

	if (IS_ENABLED(CONFIG_PCIEASPM))
		control |= OSC_PCI_EXPRESS_LTR_CONTROL;

	if (IS_ENABLED(CONFIG_HOTPLUG_PCI_PCIE))
		control |= OSC_PCI_EXPRESS_NATIVE_HP_CONTROL;

	if (IS_ENABLED(CONFIG_HOTPLUG_PCI_SHPC))
		control |= OSC_PCI_SHPC_NATIVE_HP_CONTROL;

	if (pci_aer_available())
		control |= OSC_PCI_EXPRESS_AER_CONTROL;

	/*
	 * Per the Downstream Port Containment Related Enhancements ECN to
	 * the PCI Firmware Spec, r3.2, sec 4.5.1, table 4-5,
	 * OSC_PCI_EXPRESS_DPC_CONTROL indicates the OS supports both DPC
	 * and EDR.
	 */
	if (IS_ENABLED(CONFIG_PCIE_DPC) && IS_ENABLED(CONFIG_PCIE_EDR))
		control |= OSC_PCI_EXPRESS_DPC_CONTROL;

	return control;
}

static u32 calculate_cxl_control(void)
{
	u32 control = 0;

	if (IS_ENABLED(CONFIG_MEMORY_FAILURE))
		control |= OSC_CXL_ERROR_REPORTING_CONTROL;

	return control;
}

static bool os_control_query_checks(struct acpi_pci_root *root, u32 support)
{
	struct acpi_device *device = root->device;

	if (pcie_ports_disabled) {
		dev_info(&device->dev, "PCIe port services disabled; not requesting _OSC control\n");
		return false;
	}

	if ((support & ACPI_PCIE_REQ_SUPPORT) != ACPI_PCIE_REQ_SUPPORT) {
		decode_osc_support(root, "not requesting OS control; OS requires",
				   ACPI_PCIE_REQ_SUPPORT);
		return false;
	}

	return true;
}

static void negotiate_os_control(struct acpi_pci_root *root, int *no_aspm)
{
	u32 support, control = 0, requested = 0;
	u32 cxl_support = 0, cxl_control = 0, cxl_requested = 0;
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

	support = calculate_support();

	decode_osc_support(root, "OS supports", support);

	if (os_control_query_checks(root, support))
		requested = control = calculate_control();

	if (is_cxl(root)) {
		cxl_support = calculate_cxl_support();
		decode_cxl_osc_support(root, "OS supports", cxl_support);
		cxl_requested = cxl_control = calculate_cxl_control();
	}

	status = acpi_pci_osc_control_set(handle, &control, support,
					  &cxl_control, cxl_support);
	if (ACPI_SUCCESS(status)) {
		if (control)
			decode_osc_control(root, "OS now controls", control);
		if (cxl_control)
			decode_cxl_osc_control(root, "OS now controls",
					   cxl_control);

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
		/*
		 * We want to disable ASPM here, but aspm_disabled
		 * needs to remain in its state from boot so that we
		 * properly handle PCIe 1.1 devices.  So we set this
		 * flag here, to defer the action until after the ACPI
		 * root scan.
		 */
		*no_aspm = 1;

		/* _OSC is optional for PCI host bridges */
		if (status == AE_NOT_FOUND && !is_pcie(root))
			return;

		if (control) {
			decode_osc_control(root, "OS requested", requested);
			decode_osc_control(root, "platform willing to grant", control);
		}
		if (cxl_control) {
			decode_cxl_osc_control(root, "OS requested", cxl_requested);
			decode_cxl_osc_control(root, "platform willing to grant",
					   cxl_control);
		}

		dev_info(&device->dev, "_OSC: platform retains control of PCIe features (%s)\n",
			 acpi_format_exception(status));
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
	const char *acpi_hid;

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

	pr_info("%s [%s] (domain %04x %pR)\n",
	       acpi_device_name(device), acpi_device_bid(device),
	       root->segment, &root->secondary);

	root->mcfg_addr = acpi_pci_root_get_mcfg_addr(handle);

	acpi_hid = acpi_device_hid(root->device);
	if (strcmp(acpi_hid, "PNP0A08") == 0)
		root->bridge_type = ACPI_BRIDGE_TYPE_PCIE;
	else if (strcmp(acpi_hid, "ACPI0016") == 0)
		root->bridge_type = ACPI_BRIDGE_TYPE_CXL;
	else
		dev_dbg(&device->dev, "Assuming non-PCIe host bridge\n");

	negotiate_os_control(root, &no_aspm);

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
			if (resource_union(res1, res2, res2)) {
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
	union acpi_object *obj;

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
	if (!(root->osc_control_set & OSC_PCI_EXPRESS_DPC_CONTROL))
		host_bridge->native_dpc = 0;

	if (!(root->osc_ext_control_set & OSC_CXL_ERROR_REPORTING_CONTROL))
		host_bridge->native_cxl_error = 0;

	/*
	 * Evaluate the "PCI Boot Configuration" _DSM Function.  If it
	 * exists and returns 0, we must preserve any PCI resource
	 * assignments made by firmware for this host bridge.
	 */
	obj = acpi_evaluate_dsm(ACPI_HANDLE(bus->bridge), &pci_acpi_dsm_guid, 1,
				DSM_PCI_PRESERVE_BOOT_CONFIG, NULL);
	if (obj && obj->type == ACPI_TYPE_INTEGER && obj->integer.value == 0)
		host_bridge->preserve_config = 1;
	ACPI_FREE(obj);

	acpi_dev_power_up_children_with_adr(device);

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
	if (acpi_pci_disabled)
		return;

	pci_acpi_crs_quirks();
	acpi_scan_add_handler_with_hotplug(&pci_root_handler, "pci_root");
}
