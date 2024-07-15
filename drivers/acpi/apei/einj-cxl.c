// SPDX-License-Identifier: GPL-2.0-only
/*
 * CXL Error INJection support. Used by CXL core to inject
 * protocol errors into CXL ports.
 *
 * Copyright (C) 2023 Advanced Micro Devices, Inc.
 *
 * Author: Ben Cheatham <benjamin.cheatham@amd.com>
 */
#include <linux/einj-cxl.h>
#include <linux/seq_file.h>
#include <linux/pci.h>

#include "apei-internal.h"

/* Defined in einj-core.c */
extern bool einj_initialized;

static struct { u32 mask; const char *str; } const einj_cxl_error_type_string[] = {
	{ ACPI_EINJ_CXL_CACHE_CORRECTABLE, "CXL.cache Protocol Correctable" },
	{ ACPI_EINJ_CXL_CACHE_UNCORRECTABLE, "CXL.cache Protocol Uncorrectable non-fatal" },
	{ ACPI_EINJ_CXL_CACHE_FATAL, "CXL.cache Protocol Uncorrectable fatal" },
	{ ACPI_EINJ_CXL_MEM_CORRECTABLE, "CXL.mem Protocol Correctable" },
	{ ACPI_EINJ_CXL_MEM_UNCORRECTABLE, "CXL.mem Protocol Uncorrectable non-fatal" },
	{ ACPI_EINJ_CXL_MEM_FATAL, "CXL.mem Protocol Uncorrectable fatal" },
};

int einj_cxl_available_error_type_show(struct seq_file *m, void *v)
{
	int cxl_err, rc;
	u32 available_error_type = 0;

	rc = einj_get_available_error_type(&available_error_type);
	if (rc)
		return rc;

	for (int pos = 0; pos < ARRAY_SIZE(einj_cxl_error_type_string); pos++) {
		cxl_err = ACPI_EINJ_CXL_CACHE_CORRECTABLE << pos;

		if (available_error_type & cxl_err)
			seq_printf(m, "0x%08x\t%s\n",
				   einj_cxl_error_type_string[pos].mask,
				   einj_cxl_error_type_string[pos].str);
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(einj_cxl_available_error_type_show, CXL);

static int cxl_dport_get_sbdf(struct pci_dev *dport_dev, u64 *sbdf)
{
	struct pci_bus *pbus;
	struct pci_host_bridge *bridge;
	u64 seg = 0, bus;

	pbus = dport_dev->bus;
	bridge = pci_find_host_bridge(pbus);

	if (!bridge)
		return -ENODEV;

	if (bridge->domain_nr != PCI_DOMAIN_NR_NOT_SET)
		seg = bridge->domain_nr;

	bus = pbus->number;
	*sbdf = (seg << 24) | (bus << 16) | dport_dev->devfn;

	return 0;
}

int einj_cxl_inject_rch_error(u64 rcrb, u64 type)
{
	int rc;

	/* Only CXL error types can be specified */
	if (!einj_is_cxl_error_type(type))
		return -EINVAL;

	rc = einj_validate_error_type(type);
	if (rc)
		return rc;

	return einj_cxl_rch_error_inject(type, 0x2, rcrb, GENMASK_ULL(63, 0),
					 0, 0);
}
EXPORT_SYMBOL_NS_GPL(einj_cxl_inject_rch_error, CXL);

int einj_cxl_inject_error(struct pci_dev *dport, u64 type)
{
	u64 param4 = 0;
	int rc;

	/* Only CXL error types can be specified */
	if (!einj_is_cxl_error_type(type))
		return -EINVAL;

	rc = einj_validate_error_type(type);
	if (rc)
		return rc;

	rc = cxl_dport_get_sbdf(dport, &param4);
	if (rc)
		return rc;

	return einj_error_inject(type, 0x4, 0, 0, 0, param4);
}
EXPORT_SYMBOL_NS_GPL(einj_cxl_inject_error, CXL);

bool einj_cxl_is_initialized(void)
{
	return einj_initialized;
}
EXPORT_SYMBOL_NS_GPL(einj_cxl_is_initialized, CXL);
