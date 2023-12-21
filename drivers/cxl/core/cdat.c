// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2023 Intel Corporation. All rights reserved. */
#include <linux/acpi.h>
#include <linux/xarray.h>
#include <linux/fw_table.h>
#include "cxlpci.h"
#include "cxl.h"

struct dsmas_entry {
	struct range dpa_range;
	u8 handle;
};

static int cdat_dsmas_handler(union acpi_subtable_headers *header, void *arg,
			      const unsigned long end)
{
	struct acpi_cdat_header *hdr = &header->cdat;
	struct acpi_cdat_dsmas *dsmas;
	int size = sizeof(*hdr) + sizeof(*dsmas);
	struct xarray *dsmas_xa = arg;
	struct dsmas_entry *dent;
	u16 len;
	int rc;

	len = le16_to_cpu((__force __le16)hdr->length);
	if (len != size || (unsigned long)hdr + len > end) {
		pr_warn("Malformed DSMAS table length: (%u:%u)\n", size, len);
		return -EINVAL;
	}

	/* Skip common header */
	dsmas = (struct acpi_cdat_dsmas *)(hdr + 1);

	dent = kzalloc(sizeof(*dent), GFP_KERNEL);
	if (!dent)
		return -ENOMEM;

	dent->handle = dsmas->dsmad_handle;
	dent->dpa_range.start = le64_to_cpu((__force __le64)dsmas->dpa_base_address);
	dent->dpa_range.end = le64_to_cpu((__force __le64)dsmas->dpa_base_address) +
			      le64_to_cpu((__force __le64)dsmas->dpa_length) - 1;

	rc = xa_insert(dsmas_xa, dent->handle, dent, GFP_KERNEL);
	if (rc) {
		kfree(dent);
		return rc;
	}

	return 0;
}

static int cxl_cdat_endpoint_process(struct cxl_port *port,
				     struct xarray *dsmas_xa)
{
	return cdat_table_parse(ACPI_CDAT_TYPE_DSMAS, cdat_dsmas_handler,
				dsmas_xa, port->cdat.table);
}

static void discard_dsmas(struct xarray *xa)
{
	unsigned long index;
	void *ent;

	xa_for_each(xa, index, ent) {
		xa_erase(xa, index);
		kfree(ent);
	}
	xa_destroy(xa);
}
DEFINE_FREE(dsmas, struct xarray *, if (_T) discard_dsmas(_T))

void cxl_endpoint_parse_cdat(struct cxl_port *port)
{
	struct xarray __dsmas_xa;
	struct xarray *dsmas_xa __free(dsmas) = &__dsmas_xa;
	int rc;

	xa_init(&__dsmas_xa);
	if (!port->cdat.table)
		return;

	rc = cxl_cdat_endpoint_process(port, dsmas_xa);
	if (rc < 0) {
		dev_dbg(&port->dev, "Failed to parse CDAT: %d\n", rc);
		return;
	}

	/* Performance data processing */
}
EXPORT_SYMBOL_NS_GPL(cxl_endpoint_parse_cdat, CXL);

MODULE_IMPORT_NS(CXL);
