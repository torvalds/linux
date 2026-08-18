// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel PMC PWRM ACPI driver
 *
 * Copyright (C) 2025, Intel Corporation
 */

#include <linux/acpi.h>
#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/intel_vsec.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/resource.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uuid.h>

#include "core.h"

#define ENTRY_LEN		5

/* DWORD2 */
#define DVSEC_ID_MASK		GENMASK(15, 0)
#define NUM_ENTRIES_MASK	GENMASK(23, 16)
#define ENTRY_SIZE_MASK		GENMASK(31, 24)

/* DWORD3 */
#define TBIR_MASK		GENMASK(2, 0)
#define DISC_TBL_OFF_MASK	GENMASK(31, 3)

const guid_t intel_vsec_guid =
	GUID_INIT(0x294903fb, 0x634d, 0x4fc7, 0xaf, 0x1f, 0x0f, 0xb9,
		  0x56, 0xb0, 0x4f, 0xc1);

static bool is_valid_entry(union acpi_object *pkg)
{
	int i;

	if (!pkg || pkg->type != ACPI_TYPE_PACKAGE || pkg->package.count != ENTRY_LEN)
		return false;

	if (pkg->package.elements[0].type != ACPI_TYPE_STRING)
		return false;

	for (i = 1; i < ENTRY_LEN; i++)
		if (pkg->package.elements[i].type != ACPI_TYPE_INTEGER)
			return false;

	return true;
}

acpi_disc_t pmc_parse_telem_dsd(union acpi_object *obj,
			struct intel_vsec_header *header)
{
	union acpi_object *vsec_pkg;
	union acpi_object *disc_pkg;
	u64 hdr0, hdr1;
	int num_regions;
	int i;

	if (!header)
		return ERR_PTR(-EINVAL);

	if (!obj || obj->type != ACPI_TYPE_PACKAGE || obj->package.count != 2)
		return ERR_PTR(-EINVAL);

	/* First Package is DVSEC info */
	vsec_pkg = &obj->package.elements[0];
	if (!is_valid_entry(vsec_pkg))
		return ERR_PTR(-EINVAL);

	hdr0 = vsec_pkg->package.elements[3].integer.value;
	hdr1 = vsec_pkg->package.elements[4].integer.value;

	header->id          = FIELD_GET(DVSEC_ID_MASK, hdr0);
	header->num_entries = FIELD_GET(NUM_ENTRIES_MASK, hdr0);
	header->entry_size  = FIELD_GET(ENTRY_SIZE_MASK, hdr0);
	header->tbir        = FIELD_GET(TBIR_MASK, hdr1);
	header->offset      = hdr1 & DISC_TBL_OFF_MASK;

	/* Second Package contains the discovery tables */
	disc_pkg = &obj->package.elements[1];
	if (disc_pkg->type != ACPI_TYPE_PACKAGE || disc_pkg->package.count < 1)
		return ERR_PTR(-EINVAL);

	num_regions = disc_pkg->package.count;
	if (header->num_entries != num_regions)
		return ERR_PTR(-EINVAL);

	acpi_disc_t disc __free(kfree) = kmalloc_array(num_regions, sizeof(*disc),
						      GFP_KERNEL);
	if (!disc)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < num_regions; i++) {
		union acpi_object *pkg;
		u64 value;
		int j;

		pkg = &disc_pkg->package.elements[i];
		if (!is_valid_entry(pkg))
			return ERR_PTR(-EINVAL);

		/* Element 0 is a descriptive string; DWORD values start at index 1. */
		for (j = 1; j < ENTRY_LEN; j++) {
			value = pkg->package.elements[j].integer.value;
			if (value > U32_MAX)
				return ERR_PTR(-ERANGE);

			disc[i][j - 1] = value;
		}
	}

	return no_free_ptr(disc);
}
EXPORT_SYMBOL_NS_GPL(pmc_parse_telem_dsd, "INTEL_PMC_CORE");

union acpi_object *pmc_find_telem_guid(union acpi_object *dsd)
{
	int i;

	if (!dsd || dsd->type != ACPI_TYPE_PACKAGE)
		return NULL;

	for (i = 0; i + 1 < dsd->package.count; i += 2) {
		union acpi_object *uuid_obj, *data_obj;
		guid_t uuid;

		uuid_obj = &dsd->package.elements[i];
		data_obj = &dsd->package.elements[i + 1];

		if (uuid_obj->type != ACPI_TYPE_BUFFER ||
		    uuid_obj->buffer.length != 16)
			continue;

		memcpy(&uuid, uuid_obj->buffer.pointer, 16);
		if (guid_equal(&uuid, &intel_vsec_guid))
			return data_obj;
	}

	return NULL;
}
EXPORT_SYMBOL_NS_GPL(pmc_find_telem_guid, "INTEL_PMC_CORE");

static int pmc_pwrm_acpi_probe(struct platform_device *pdev)
{
	struct intel_vsec_header header;
	struct intel_vsec_header *headers[2] = { &header, NULL };
	struct acpi_buffer buf = { ACPI_ALLOCATE_BUFFER, NULL };
	struct intel_vsec_platform_info info = { };
	struct device *dev = &pdev->dev;
	union acpi_object *dsd;
	struct resource *res;
	acpi_handle handle;
	acpi_status status;

	handle = ACPI_HANDLE(&pdev->dev);
	if (!handle)
		return -ENODEV;

	status = acpi_evaluate_object(handle, "_DSD", NULL, &buf);
	if (ACPI_FAILURE(status)) {
		return dev_err_probe(dev, -ENODEV, "Could not evaluate _DSD: %s\n",
				     acpi_format_exception(status));
	}

	void *dsd_buf __free(pmc_acpi_free) = buf.pointer;

	dsd = pmc_find_telem_guid(dsd_buf);
	if (!dsd)
		return -ENODEV;

	acpi_disc_t acpi_disc __free(kfree) = pmc_parse_telem_dsd(dsd, &header);
	if (IS_ERR(acpi_disc))
		return PTR_ERR(acpi_disc);

	res = platform_get_resource(pdev, IORESOURCE_MEM, header.tbir);
	if (!res)
		return -EINVAL;

	info.headers = headers;
	info.caps = VSEC_CAP_TELEMETRY;
	info.acpi_disc = acpi_disc;
	info.src = INTEL_VSEC_DISC_ACPI;
	info.base_addr = res->start;

	return intel_vsec_register(&pdev->dev, &info);
}

static const struct acpi_device_id pmc_pwrm_acpi_ids[] = {
	{ "INTC1122", 0 }, /* Nova Lake */
	{ "INTC1129", 0 }, /* Nova Lake */
	{ }
};
MODULE_DEVICE_TABLE(acpi, pmc_pwrm_acpi_ids);

static struct platform_driver pmc_pwrm_acpi_driver = {
	.probe = pmc_pwrm_acpi_probe,
	.driver = {
		.name = "intel_pmc_pwrm_acpi",
		.acpi_match_table = ACPI_PTR(pmc_pwrm_acpi_ids),
	},
};
module_platform_driver(pmc_pwrm_acpi_driver);

MODULE_AUTHOR("David E. Box <david.e.box@linux.intel.com>");
MODULE_DESCRIPTION("Intel PMC PWRM ACPI driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("INTEL_VSEC");
