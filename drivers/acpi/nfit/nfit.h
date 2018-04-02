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
#include <linux/ndctl.h>
#include <linux/types.h>
#include <linux/acpi.h>
#include <acpi/acuuid.h>

/* ACPI 6.1 */
#define UUID_NFIT_BUS "2f10e7a4-9e91-11e4-89d3-123b93f75cba"

/* http://pmem.io/documents/NVDIMM_DSM_Interface-V1.6.pdf */
#define UUID_NFIT_DIMM "4309ac30-0d11-11e4-9191-0800200c9a66"

/* https://github.com/HewlettPackard/hpe-nvm/blob/master/Documentation/ */
#define UUID_NFIT_DIMM_N_HPE1 "9002c334-acf3-4c0e-9642-a235f0d53bc6"
#define UUID_NFIT_DIMM_N_HPE2 "5008664b-b758-41a0-a03c-27c2f2d04f7e"

/* https://msdn.microsoft.com/library/windows/hardware/mt604741 */
#define UUID_NFIT_DIMM_N_MSFT "1ee68b36-d4bd-4a1a-9a16-4f8e53d46e05"

#define ACPI_NFIT_MEM_FAILED_MASK (ACPI_NFIT_MEM_SAVE_FAILED \
		| ACPI_NFIT_MEM_RESTORE_FAILED | ACPI_NFIT_MEM_FLUSH_FAILED \
		| ACPI_NFIT_MEM_NOT_ARMED | ACPI_NFIT_MEM_MAP_FAILED)

#define NVDIMM_FAMILY_MAX NVDIMM_FAMILY_MSFT

#define NVDIMM_STANDARD_CMDMASK \
(1 << ND_CMD_SMART | 1 << ND_CMD_SMART_THRESHOLD | 1 << ND_CMD_DIMM_FLAGS \
 | 1 << ND_CMD_GET_CONFIG_SIZE | 1 << ND_CMD_GET_CONFIG_DATA \
 | 1 << ND_CMD_SET_CONFIG_DATA | 1 << ND_CMD_VENDOR_EFFECT_LOG_SIZE \
 | 1 << ND_CMD_VENDOR_EFFECT_LOG | 1 << ND_CMD_VENDOR)

/*
 * Command numbers that the kernel needs to know about to handle
 * non-default DSM revision ids
 */
enum nvdimm_family_cmds {
	NVDIMM_INTEL_LATCH_SHUTDOWN = 10,
	NVDIMM_INTEL_GET_MODES = 11,
	NVDIMM_INTEL_GET_FWINFO = 12,
	NVDIMM_INTEL_START_FWUPDATE = 13,
	NVDIMM_INTEL_SEND_FWUPDATE = 14,
	NVDIMM_INTEL_FINISH_FWUPDATE = 15,
	NVDIMM_INTEL_QUERY_FWUPDATE = 16,
	NVDIMM_INTEL_SET_THRESHOLD = 17,
	NVDIMM_INTEL_INJECT_ERROR = 18,
};

#define NVDIMM_INTEL_CMDMASK \
(NVDIMM_STANDARD_CMDMASK | 1 << NVDIMM_INTEL_GET_MODES \
 | 1 << NVDIMM_INTEL_GET_FWINFO | 1 << NVDIMM_INTEL_START_FWUPDATE \
 | 1 << NVDIMM_INTEL_SEND_FWUPDATE | 1 << NVDIMM_INTEL_FINISH_FWUPDATE \
 | 1 << NVDIMM_INTEL_QUERY_FWUPDATE | 1 << NVDIMM_INTEL_SET_THRESHOLD \
 | 1 << NVDIMM_INTEL_INJECT_ERROR | 1 << NVDIMM_INTEL_LATCH_SHUTDOWN)

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
 * Region format interface codes are stored with the interface as the
 * LSB and the function as the MSB.
 */
#define NFIT_FIC_BYTE cpu_to_le16(0x101) /* byte-addressable energy backed */
#define NFIT_FIC_BLK cpu_to_le16(0x201) /* block-addressable non-energy backed */
#define NFIT_FIC_BYTEN cpu_to_le16(0x301) /* byte-addressable non-energy backed */

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

enum nfit_root_notifiers {
	NFIT_NOTIFY_UPDATE = 0x80,
	NFIT_NOTIFY_UC_MEMORY_ERROR = 0x81,
};

enum nfit_dimm_notifiers {
	NFIT_NOTIFY_DIMM_HEALTH = 0x81,
};

enum nfit_ars_state {
	ARS_REQ,
	ARS_DONE,
	ARS_SHORT,
	ARS_FAILED,
};

struct nfit_spa {
	struct list_head list;
	struct nd_region *nd_region;
	unsigned long ars_state;
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
	struct kernfs_node *flags_attr;
	struct nfit_flush *nfit_flush;
	struct list_head list;
	struct acpi_device *adev;
	struct acpi_nfit_desc *acpi_desc;
	struct resource *flush_wpq;
	unsigned long dsm_mask;
	int family;
	bool has_lsr;
	bool has_lsw;
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
	u8 ars_start_flags;
	struct nd_cmd_ars_status *ars_status;
	size_t ars_status_size;
	struct work_struct work;
	struct list_head list;
	struct kernfs_node *scrub_count_state;
	unsigned int scrub_count;
	unsigned int scrub_mode;
	unsigned int cancel:1;
	unsigned int init_complete:1;
	unsigned long dimm_cmd_force_en;
	unsigned long bus_cmd_force_en;
	unsigned long bus_nfit_cmd_force_en;
	unsigned int platform_cap;
	int (*blk_do_io)(struct nd_blk_region *ndbr, resource_size_t dpa,
			void *iobuf, u64 len, int rw);
};

enum scrub_mode {
	HW_ERROR_SCRUB_OFF,
	HW_ERROR_SCRUB_ON,
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

extern struct list_head acpi_descs;
extern struct mutex acpi_desc_lock;
int acpi_nfit_ars_rescan(struct acpi_nfit_desc *acpi_desc, unsigned long flags);

#ifdef CONFIG_X86_MCE
void nfit_mce_register(void);
void nfit_mce_unregister(void);
#else
static inline void nfit_mce_register(void)
{
}
static inline void nfit_mce_unregister(void)
{
}
#endif

int nfit_spa_type(struct acpi_nfit_system_address *spa);

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

const guid_t *to_nfit_uuid(enum nfit_uuids id);
int acpi_nfit_init(struct acpi_nfit_desc *acpi_desc, void *nfit, acpi_size sz);
void acpi_nfit_shutdown(void *data);
void __acpi_nfit_notify(struct device *dev, acpi_handle handle, u32 event);
void __acpi_nvdimm_notify(struct device *dev, u32 event);
int acpi_nfit_ctl(struct nvdimm_bus_descriptor *nd_desc, struct nvdimm *nvdimm,
		unsigned int cmd, void *buf, unsigned int buf_len, int *cmd_rc);
void acpi_nfit_desc_init(struct acpi_nfit_desc *acpi_desc, struct device *dev);
#endif /* __NFIT_H__ */
