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
#include <linux/workqueue.h>
#include <linux/libnvdimm.h>
#include <linux/types.h>
#include <linux/uuid.h>
#include <linux/acpi.h>
#include <acpi/acuuid.h>

#define UUID_NFIT_BUS "2f10e7a4-9e91-11e4-89d3-123b93f75cba"
#define UUID_NFIT_DIMM "4309ac30-0d11-11e4-9191-0800200c9a66"
#define ACPI_NFIT_MEM_FAILED_MASK (ACPI_NFIT_MEM_SAVE_FAILED \
		| ACPI_NFIT_MEM_RESTORE_FAILED | ACPI_NFIT_MEM_FLUSH_FAILED \
		| ACPI_NFIT_MEM_NOT_ARMED)

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

enum nfit_fic {
	NFIT_FIC_BYTE = 0x101, /* byte-addressable energy backed */
	NFIT_FIC_BLK = 0x201, /* block-addressable non-energy backed */
	NFIT_FIC_BYTEN = 0x301, /* byte-addressable non-energy backed */
};

enum {
	NFIT_BLK_READ_FLUSH = 1,
	NFIT_BLK_DCR_LATCH = 2,
	NFIT_ARS_STATUS_DONE = 0,
	NFIT_ARS_STATUS_BUSY = 1 << 16,
	NFIT_ARS_STATUS_NONE = 2 << 16,
	NFIT_ARS_STATUS_INTR = 3 << 16,
	NFIT_ARS_START_BUSY = 6,
	NFIT_ARS_CAP_NONE = 1,
	NFIT_ARS_F_OVERFLOW = 1,
	NFIT_ARS_TIMEOUT = 90,
};

struct nfit_spa {
	struct acpi_nfit_system_address *spa;
	struct list_head list;
	struct nd_region *nd_region;
	unsigned int ars_done:1;
	u32 clear_err_unit;
	u32 max_ars;
};

struct nfit_dcr {
	struct acpi_nfit_control_region *dcr;
	struct list_head list;
};

struct nfit_bdw {
	struct acpi_nfit_data_region *bdw;
	struct list_head list;
};

struct nfit_idt {
	struct acpi_nfit_interleave *idt;
	struct list_head list;
};

struct nfit_flush {
	struct acpi_nfit_flush_address *flush;
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
	struct acpi_nfit_memory_map *memdev_bdw;
	struct acpi_nfit_control_region *dcr;
	struct acpi_nfit_data_region *bdw;
	struct acpi_nfit_system_address *spa_dcr;
	struct acpi_nfit_system_address *spa_bdw;
	struct acpi_nfit_interleave *idt_dcr;
	struct acpi_nfit_interleave *idt_bdw;
	struct nfit_flush *nfit_flush;
	struct list_head list;
	struct acpi_device *adev;
	unsigned long dsm_mask;
};

struct acpi_nfit_desc {
	struct nvdimm_bus_descriptor nd_desc;
	struct acpi_table_header acpi_header;
	struct acpi_nfit_header *nfit;
	struct mutex spa_map_mutex;
	struct mutex init_mutex;
	struct list_head spa_maps;
	struct list_head memdevs;
	struct list_head flushes;
	struct list_head dimms;
	struct list_head spas;
	struct list_head dcrs;
	struct list_head bdws;
	struct list_head idts;
	struct nvdimm_bus *nvdimm_bus;
	struct device *dev;
	struct nd_cmd_ars_status *ars_status;
	size_t ars_status_size;
	struct work_struct work;
	unsigned int cancel:1;
	unsigned long dimm_dsm_force_en;
	unsigned long bus_dsm_force_en;
	int (*blk_do_io)(struct nd_blk_region *ndbr, resource_size_t dpa,
			void *iobuf, u64 len, int rw);
};

enum nd_blk_mmio_selector {
	BDW,
	DCR,
};

struct nd_blk_addr {
	union {
		void __iomem *base;
		void __pmem  *aperture;
	};
};

struct nfit_blk {
	struct nfit_blk_mmio {
		struct nd_blk_addr addr;
		u64 size;
		u64 base_offset;
		u32 line_size;
		u32 num_lines;
		u32 table_size;
		struct acpi_nfit_interleave *idt;
		struct acpi_nfit_system_address *spa;
	} mmio[2];
	struct nd_region *nd_region;
	u64 bdw_offset; /* post interleave offset */
	u64 stat_offset;
	u64 cmd_offset;
	void __iomem *nvdimm_flush;
	u32 dimm_flags;
};

enum spa_map_type {
	SPA_MAP_CONTROL,
	SPA_MAP_APERTURE,
};

struct nfit_spa_mapping {
	struct acpi_nfit_desc *acpi_desc;
	struct acpi_nfit_system_address *spa;
	struct list_head list;
	struct kref kref;
	enum spa_map_type type;
	struct nd_blk_addr addr;
};

static inline struct nfit_spa_mapping *to_spa_map(struct kref *kref)
{
	return container_of(kref, struct nfit_spa_mapping, kref);
}

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

const u8 *to_nfit_uuid(enum nfit_uuids id);
int acpi_nfit_init(struct acpi_nfit_desc *nfit, acpi_size sz);
void acpi_nfit_desc_init(struct acpi_nfit_desc *acpi_desc, struct device *dev);
#endif /* __NFIT_H__ */
