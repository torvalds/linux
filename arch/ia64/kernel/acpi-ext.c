/*
 * (c) Copyright 2003, 2006 Hewlett-Packard Development Company, L.P.
 *	Alex Williamson <alex.williamson@hp.com>
 *	Bjorn Helgaas <bjorn.helgaas@hp.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/acpi.h>

#include <asm/acpi-ext.h>

/*
 * Device CSRs that do not appear in PCI config space should be described
 * via ACPI.  This would normally be done with Address Space Descriptors
 * marked as "consumer-only," but old versions of Windows and Linux ignore
 * the producer/consumer flag, so HP invented a vendor-defined resource to
 * describe the location and size of CSR space.
 */

struct acpi_vendor_uuid hp_ccsr_uuid = {
	.subtype = 2,
	.data = { 0xf9, 0xad, 0xe9, 0x69, 0x4f, 0x92, 0x5f, 0xab, 0xf6, 0x4a,
	    0x24, 0xd2, 0x01, 0x37, 0x0e, 0xad },
};

static acpi_status hp_ccsr_locate(acpi_handle obj, u64 *base, u64 *length)
{
	acpi_status status;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_resource *resource;
	struct acpi_resource_vendor_typed *vendor;

	status = acpi_get_vendor_resource(obj, METHOD_NAME__CRS, &hp_ccsr_uuid,
		&buffer);

	resource = buffer.pointer;
	vendor = &resource->data.vendor_typed;

	if (ACPI_FAILURE(status) || vendor->byte_length < 16) {
		status = AE_NOT_FOUND;
		goto exit;
	}

	memcpy(base, vendor->byte_data, sizeof(*base));
	memcpy(length, vendor->byte_data + 8, sizeof(*length));

  exit:
	kfree(buffer.pointer);
	return status;
}

struct csr_space {
	u64	base;
	u64	length;
};

static acpi_status find_csr_space(struct acpi_resource *resource, void *data)
{
	struct csr_space *space = data;
	struct acpi_resource_address64 addr;
	acpi_status status;

	status = acpi_resource_to_address64(resource, &addr);
	if (ACPI_SUCCESS(status) &&
	    addr.resource_type == ACPI_MEMORY_RANGE &&
	    addr.address_length &&
	    addr.producer_consumer == ACPI_CONSUMER) {
		space->base = addr.minimum;
		space->length = addr.address_length;
		return AE_CTRL_TERMINATE;
	}
	return AE_OK;		/* keep looking */
}

static acpi_status hp_crs_locate(acpi_handle obj, u64 *base, u64 *length)
{
	struct csr_space space = { 0, 0 };

	acpi_walk_resources(obj, METHOD_NAME__CRS, find_csr_space, &space);
	if (!space.length)
		return AE_NOT_FOUND;

	*base = space.base;
	*length = space.length;
	return AE_OK;
}

acpi_status hp_acpi_csr_space(acpi_handle obj, u64 *csr_base, u64 *csr_length)
{
	acpi_status status;

	status = hp_ccsr_locate(obj, csr_base, csr_length);
	if (ACPI_SUCCESS(status))
		return status;

	return hp_crs_locate(obj, csr_base, csr_length);
}
EXPORT_SYMBOL(hp_acpi_csr_space);
