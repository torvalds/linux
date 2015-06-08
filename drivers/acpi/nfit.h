/*
 * NVDIMM Firmware Interface Table - NFIT
 *
 * Copyright(c) 2013-2015 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
#ifndef __NFIT_H__
#define __NFIT_H__
#include <linux/libnvdimm.h>
#include <linux/types.h>
#include <linux/uuid.h>
#include <linux/acpi.h>
#include <acpi/acuuid.h>

#define UUID_NFIT_BUS "2f10e7a4-9e91-11e4-89d3-123b93f75cba"
#define UUID_NFIT_DIMM "4309ac30-0d11-11e4-9191-0800200c9a66"

enum nfit_uuids {
	NFIT_SPA_VOLATILE,
	NFIT_SPA_PM,
	NFIT_SPA_DCR,
	NFIT_SPA_BDW,
	NFIT_SPA_VDISK,
	NFIT_SPA_VCD,
	NFIT_SPA_PDISK,
	NFIT_SPA_PCD,
	NFIT_DEV_BUS,
	NFIT_DEV_DIMM,
	NFIT_UUID_MAX,
};

struct nfit_spa {
	struct acpi_nfit_system_address *spa;
	struct list_head list;
};

struct nfit_dcr {
	struct acpi_nfit_control_region *dcr;
	struct list_head list;
};

struct nfit_bdw {
	struct acpi_nfit_data_region *bdw;
	struct list_head list;
};

struct nfit_memdev {
	struct acpi_nfit_memory_map *memdev;
	struct list_head list;
};

/* assembled tables for a given dimm/memory-device */
struct nfit_mem {
	struct nvdimm *nvdimm;
	struct acpi_nfit_memory_map *memdev_dcr;
	struct acpi_nfit_memory_map *memdev_pmem;
	struct acpi_nfit_control_region *dcr;
	struct acpi_nfit_data_region *bdw;
	struct acpi_nfit_system_address *spa_dcr;
	struct acpi_nfit_system_address *spa_bdw;
	struct list_head list;
	struct acpi_device *adev;
	unsigned long dsm_mask;
};

struct acpi_nfit_desc {
	struct nvdimm_bus_descriptor nd_desc;
	struct acpi_table_nfit *nfit;
	struct list_head memdevs;
	struct list_head dimms;
	struct list_head spas;
	struct list_head dcrs;
	struct list_head bdws;
	struct nvdimm_bus *nvdimm_bus;
	struct device *dev;
	unsigned long dimm_dsm_force_en;
};

static inline struct acpi_nfit_memory_map *__to_nfit_memdev(
		struct nfit_mem *nfit_mem)
{
	if (nfit_mem->memdev_dcr)
		return nfit_mem->memdev_dcr;
	return nfit_mem->memdev_pmem;
}

static inline struct acpi_nfit_desc *to_acpi_desc(
		struct nvdimm_bus_descriptor *nd_desc)
{
	return container_of(nd_desc, struct acpi_nfit_desc, nd_desc);
}
#endif /* __NFIT_H__ */
