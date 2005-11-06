/*
 * arch/ia64/kernel/acpi-ext.c
 *
 * Copyright (C) 2003 Hewlett-Packard
 * Copyright (C) Alex Williamson
 * Copyright (C) Bjorn Helgaas
 *
 * Vendor specific extensions to ACPI.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/acpi.h>
#include <linux/efi.h>

#include <asm/acpi-ext.h>

struct acpi_vendor_descriptor {
	u8 guid_id;
	efi_guid_t guid;
};

struct acpi_vendor_info {
	struct acpi_vendor_descriptor *descriptor;
	u8 *data;
	u32 length;
};

acpi_status
acpi_vendor_resource_match(struct acpi_resource *resource, void *context)
{
	struct acpi_vendor_info *info = (struct acpi_vendor_info *)context;
	struct acpi_resource_vendor *vendor;
	struct acpi_vendor_descriptor *descriptor;
	u32 length;

	if (resource->id != ACPI_RSTYPE_VENDOR)
		return AE_OK;

	vendor = (struct acpi_resource_vendor *)&resource->data;
	descriptor = (struct acpi_vendor_descriptor *)vendor->reserved;
	if (vendor->length <= sizeof(*info->descriptor) ||
	    descriptor->guid_id != info->descriptor->guid_id ||
	    efi_guidcmp(descriptor->guid, info->descriptor->guid))
		return AE_OK;

	length = vendor->length - sizeof(struct acpi_vendor_descriptor);
	info->data = acpi_os_allocate(length);
	if (!info->data)
		return AE_NO_MEMORY;

	memcpy(info->data,
	       vendor->reserved + sizeof(struct acpi_vendor_descriptor),
	       length);
	info->length = length;
	return AE_CTRL_TERMINATE;
}

acpi_status
acpi_find_vendor_resource(acpi_handle obj, struct acpi_vendor_descriptor * id,
			  u8 ** data, u32 * length)
{
	struct acpi_vendor_info info;

	info.descriptor = id;
	info.data = NULL;

	acpi_walk_resources(obj, METHOD_NAME__CRS, acpi_vendor_resource_match,
			    &info);
	if (!info.data)
		return AE_NOT_FOUND;

	*data = info.data;
	*length = info.length;
	return AE_OK;
}

struct acpi_vendor_descriptor hp_ccsr_descriptor = {
	.guid_id = 2,
	.guid =
	    EFI_GUID(0x69e9adf9, 0x924f, 0xab5f, 0xf6, 0x4a, 0x24, 0xd2, 0x01,
		     0x37, 0x0e, 0xad)
};

acpi_status hp_acpi_csr_space(acpi_handle obj, u64 * csr_base, u64 * csr_length)
{
	acpi_status status;
	u8 *data;
	u32 length;

	status =
	    acpi_find_vendor_resource(obj, &hp_ccsr_descriptor, &data, &length);

	if (ACPI_FAILURE(status) || length != 16)
		return AE_NOT_FOUND;

	memcpy(csr_base, data, sizeof(*csr_base));
	memcpy(csr_length, data + 8, sizeof(*csr_length));
	acpi_os_free(data);

	return AE_OK;
}

EXPORT_SYMBOL(hp_acpi_csr_space);
