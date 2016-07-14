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

/* ACPI 6.1 */
#define UUID_NFIT_BUS "2f10e7a4-9e91-11e4-89d3-123b93f75cba"

/* http://pmem.io/documents/NVDIMM_DSM_Interface_Example.pdf */
#define UUID_NFIT_DIMM "4309ac30-0d11-11e4-9191-0800200c9a66"

/* https://github.com/HewlettPackard/hpe-nvm/blob/master/Documentation/ */
#define UUID_NFIT_DIMM_N_HPE1 "9002c334-acf3-4c0e-9642-a235f0d53bc6"
#define UUID_NFIT_DIMM_N_HPE2 "5008664b-b758-41a0-a03c-27c2f2d04f7e"

/* https://msdn.microsoft.com/library/windows/hardware/mt604741 */
#define UUID_NFIT_DIMM_N_MSFT "1ee68b36-d4bd-4a1a-9a16-4f8e53d46e05"

#define ACPI_NFIT_MEM_FAILED_MASK (ACPI_NFIT_MEM_SAVE_FAILED \
		| ACPI_NFIT_MEM_RESTORE_FAILED | ACPI_NFIT_MEM_FLUSH_FAILED \
		| ACPI_NFIT_MEM_NOT_ARMED)

enum nfit_uuids {
	/* for simplicity alias the uuid index with the family id */
	NFIT_DEV_DIMM = NVDIMM_FAMILY_INTEL,
	NFIT_DEV_DIMM_N_HPE1 = NVDIMM_FAMILY_HPE1,
	NFIT_DEV_DIMM_N_HPE2 = NVDIMM_FAMILY_HPE2,
	NFIT_DEV_DIMM_N_MSFT = NVDIMM_FAMILY_MSFT,
	NFIT_SPA_VOLATILE,
	NFIT_SPA_PM,
	NFIT_SPA_DCR,
	NFIT_SPA_BDW,
	NFIT_SPA_VDISK,
	NFIT_SPA_VCD,
	NFIT_SPA_PDISK,
	NFIT_SPA_PCD,
	NFIT_DEV_BUS,
	NFIT_UUID_MAX,
};

/*
 * Region format interface codes are stored as an array of bytes in the
 * NFIT DIMM Control Region structure
 */
#define NFIT_FIC_BYTE cpu_to_be16(0x101) /* byte-addressable energy backed */
#define NFIT_FIC_BLK cpu_to_be16(0x201) /* block-addressable non-energy backed */
#define NFIT_FIC_BYTEN cpu_to_be16(0x301) /* byte-addressable non-energy backed */

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
	struct list_head list;
	struct nd_region *nd_region;
	unsigned int ars_done:1;
	u32 clear_err_unit;
	u32 max_ars;
	struct acpi_nfit_system_address spa[0];
};

struct nfit_dcr {
	struct list_head list;
	struct acpi_nfit_control_region dcr[0];
};

struct nfit_bdw {
	struct list_head list;
	struct acpi_nfit_data_region bdw[0];
};

struct nfit_idt {
	struct list_head list;
	struct acpi_nfit_interleave idt[0];
};

struct nfit_flush {
	struct list_head list;
	struct acpi_nfit_flush_address flush[0];
};

struct nfit_memdev {
	struct list_head list;
	struct acpi_nfit_memory_map memdev[0];
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
	struct acpi_nfit_desc *acpi_desc;
	struct resource *flush_wpq;
	unsigned long dsm_mask;
	int family;
};

struct acpi_nfit_desc {
	struct nvdimm_bus_descriptor nd_desc;
	struct acpi_table_header acpi_header;
	struct mutex init_mutex;
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
	unsigned long dimm_cmd_force_en;
	unsigned long bus_cmd_force_en;
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
		void *aperture;
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
	u32 dimm_flags;
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

const u8 *to_nfit_uuid(enum nfit_uuids id);
int acpi_nfit_init(struct acpi_nfit_desc *acpi_desc, void *nfit, acpi_size sz);
void acpi_nfit_desc_init(struct acpi_nfit_desc *acpi_desc, struct device *dev);
#endif /* __NFIT_H__ */
