/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * NVDIMM Firmware Interface Table - NFIT
 *
 * Copyright(c) 2013-2015 Intel Corporation. All rights reserved.
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

/* https://pmem.io/documents/NVDIMM_DSM_Interface-V1.6.pdf */
#define UUID_NFIT_DIMM "4309ac30-0d11-11e4-9191-0800200c9a66"
#define UUID_INTEL_BUS "c7d8acd4-2df8-4b82-9f65-a325335af149"

/* https://github.com/HewlettPackard/hpe-nvm/blob/master/Documentation/ */
#define UUID_NFIT_DIMM_N_HPE1 "9002c334-acf3-4c0e-9642-a235f0d53bc6"
#define UUID_NFIT_DIMM_N_HPE2 "5008664b-b758-41a0-a03c-27c2f2d04f7e"

/* https://msdn.microsoft.com/library/windows/hardware/mt604741 */
#define UUID_NFIT_DIMM_N_MSFT "1ee68b36-d4bd-4a1a-9a16-4f8e53d46e05"

/* http://www.uefi.org/RFIC_LIST (see "Virtual NVDIMM 0x1901") */
#define UUID_NFIT_DIMM_N_HYPERV "5746c5f2-a9a2-4264-ad0e-e4ddc9e09e80"

#define ACPI_NFIT_MEM_FAILED_MASK (ACPI_NFIT_MEM_SAVE_FAILED \
		| ACPI_NFIT_MEM_RESTORE_FAILED | ACPI_NFIT_MEM_FLUSH_FAILED \
		| ACPI_NFIT_MEM_NOT_ARMED | ACPI_NFIT_MEM_MAP_FAILED)

#define NVDIMM_CMD_MAX 31

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
	NVDIMM_INTEL_GET_SECURITY_STATE = 19,
	NVDIMM_INTEL_SET_PASSPHRASE = 20,
	NVDIMM_INTEL_DISABLE_PASSPHRASE = 21,
	NVDIMM_INTEL_UNLOCK_UNIT = 22,
	NVDIMM_INTEL_FREEZE_LOCK = 23,
	NVDIMM_INTEL_SECURE_ERASE = 24,
	NVDIMM_INTEL_OVERWRITE = 25,
	NVDIMM_INTEL_QUERY_OVERWRITE = 26,
	NVDIMM_INTEL_SET_MASTER_PASSPHRASE = 27,
	NVDIMM_INTEL_MASTER_SECURE_ERASE = 28,
	NVDIMM_INTEL_FW_ACTIVATE_DIMMINFO = 29,
	NVDIMM_INTEL_FW_ACTIVATE_ARM = 30,
};

enum nvdimm_bus_family_cmds {
	NVDIMM_BUS_INTEL_FW_ACTIVATE_BUSINFO = 1,
	NVDIMM_BUS_INTEL_FW_ACTIVATE = 2,
};

#define NVDIMM_INTEL_SECURITY_CMDMASK \
(1 << NVDIMM_INTEL_GET_SECURITY_STATE | 1 << NVDIMM_INTEL_SET_PASSPHRASE \
| 1 << NVDIMM_INTEL_DISABLE_PASSPHRASE | 1 << NVDIMM_INTEL_UNLOCK_UNIT \
| 1 << NVDIMM_INTEL_FREEZE_LOCK | 1 << NVDIMM_INTEL_SECURE_ERASE \
| 1 << NVDIMM_INTEL_OVERWRITE | 1 << NVDIMM_INTEL_QUERY_OVERWRITE \
| 1 << NVDIMM_INTEL_SET_MASTER_PASSPHRASE \
| 1 << NVDIMM_INTEL_MASTER_SECURE_ERASE)

#define NVDIMM_INTEL_FW_ACTIVATE_CMDMASK \
(1 << NVDIMM_INTEL_FW_ACTIVATE_DIMMINFO | 1 << NVDIMM_INTEL_FW_ACTIVATE_ARM)

#define NVDIMM_BUS_INTEL_FW_ACTIVATE_CMDMASK \
(1 << NVDIMM_BUS_INTEL_FW_ACTIVATE_BUSINFO | 1 << NVDIMM_BUS_INTEL_FW_ACTIVATE)

#define NVDIMM_INTEL_CMDMASK \
(NVDIMM_STANDARD_CMDMASK | 1 << NVDIMM_INTEL_GET_MODES \
 | 1 << NVDIMM_INTEL_GET_FWINFO | 1 << NVDIMM_INTEL_START_FWUPDATE \
 | 1 << NVDIMM_INTEL_SEND_FWUPDATE | 1 << NVDIMM_INTEL_FINISH_FWUPDATE \
 | 1 << NVDIMM_INTEL_QUERY_FWUPDATE | 1 << NVDIMM_INTEL_SET_THRESHOLD \
 | 1 << NVDIMM_INTEL_INJECT_ERROR | 1 << NVDIMM_INTEL_LATCH_SHUTDOWN \
 | NVDIMM_INTEL_SECURITY_CMDMASK | NVDIMM_INTEL_FW_ACTIVATE_CMDMASK)

#define NVDIMM_INTEL_DENY_CMDMASK \
(NVDIMM_INTEL_SECURITY_CMDMASK | NVDIMM_INTEL_FW_ACTIVATE_CMDMASK)

enum nfit_uuids {
	/* for simplicity alias the uuid index with the family id */
	NFIT_DEV_DIMM = NVDIMM_FAMILY_INTEL,
	NFIT_DEV_DIMM_N_HPE1 = NVDIMM_FAMILY_HPE1,
	NFIT_DEV_DIMM_N_HPE2 = NVDIMM_FAMILY_HPE2,
	NFIT_DEV_DIMM_N_MSFT = NVDIMM_FAMILY_MSFT,
	NFIT_DEV_DIMM_N_HYPERV = NVDIMM_FAMILY_HYPERV,
	/*
	 * to_nfit_bus_uuid() expects to translate bus uuid family ids
	 * to a UUID index using NVDIMM_FAMILY_MAX as an offset
	 */
	NFIT_BUS_INTEL = NVDIMM_FAMILY_MAX + NVDIMM_BUS_FAMILY_INTEL,
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
	ARS_REQ_SHORT,
	ARS_REQ_LONG,
	ARS_FAILED,
};

struct nfit_spa {
	struct list_head list;
	struct nd_region *nd_region;
	unsigned long ars_state;
	u32 clear_err_unit;
	u32 max_ars;
	struct acpi_nfit_system_address spa[];
};

struct nfit_dcr {
	struct list_head list;
	struct acpi_nfit_control_region dcr[];
};

struct nfit_bdw {
	struct list_head list;
	struct acpi_nfit_data_region bdw[];
};

struct nfit_idt {
	struct list_head list;
	struct acpi_nfit_interleave idt[];
};

struct nfit_flush {
	struct list_head list;
	struct acpi_nfit_flush_address flush[];
};

struct nfit_memdev {
	struct list_head list;
	struct acpi_nfit_memory_map memdev[];
};

enum nfit_mem_flags {
	NFIT_MEM_LSR,
	NFIT_MEM_LSW,
	NFIT_MEM_DIRTY,
	NFIT_MEM_DIRTY_COUNT,
};

#define NFIT_DIMM_ID_LEN	22

/* assembled tables for a given dimm/memory-device */
struct nfit_mem {
	struct nvdimm *nvdimm;
	struct acpi_nfit_memory_map *memdev_dcr;
	struct acpi_nfit_memory_map *memdev_pmem;
	struct acpi_nfit_control_region *dcr;
	struct acpi_nfit_system_address *spa_dcr;
	struct acpi_nfit_interleave *idt_dcr;
	struct kernfs_node *flags_attr;
	struct nfit_flush *nfit_flush;
	struct list_head list;
	struct acpi_device *adev;
	struct acpi_nfit_desc *acpi_desc;
	enum nvdimm_fwa_state fwa_state;
	enum nvdimm_fwa_result fwa_result;
	int fwa_count;
	char id[NFIT_DIMM_ID_LEN+1];
	struct resource *flush_wpq;
	unsigned long dsm_mask;
	unsigned long flags;
	u32 dirty_shutdown;
	int family;
};

enum scrub_flags {
	ARS_BUSY,
	ARS_CANCEL,
	ARS_VALID,
	ARS_POLL,
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
	struct nfit_spa *scrub_spa;
	struct delayed_work dwork;
	struct list_head list;
	struct kernfs_node *scrub_count_state;
	unsigned int max_ars;
	unsigned int scrub_count;
	unsigned int scrub_mode;
	unsigned long scrub_flags;
	unsigned long dimm_cmd_force_en;
	unsigned long bus_cmd_force_en;
	unsigned long bus_dsm_mask;
	unsigned long family_dsm_mask[NVDIMM_BUS_FAMILY_MAX + 1];
	unsigned int platform_cap;
	unsigned int scrub_tmo;
	enum nvdimm_fwa_state fwa_state;
	enum nvdimm_fwa_capability fwa_cap;
	int fwa_count;
	bool fwa_noidle;
	bool fwa_nosuspend;
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
int acpi_nfit_ars_rescan(struct acpi_nfit_desc *acpi_desc,
		enum nfit_ars_state req_type);

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

#ifdef CONFIG_PROVE_LOCKING
static inline void nfit_device_lock(struct device *dev)
{
	device_lock(dev);
	mutex_lock(&dev->lockdep_mutex);
}

static inline void nfit_device_unlock(struct device *dev)
{
	mutex_unlock(&dev->lockdep_mutex);
	device_unlock(dev);
}
#else
static inline void nfit_device_lock(struct device *dev)
{
	device_lock(dev);
}

static inline void nfit_device_unlock(struct device *dev)
{
	device_unlock(dev);
}
#endif

const guid_t *to_nfit_uuid(enum nfit_uuids id);
int acpi_nfit_init(struct acpi_nfit_desc *acpi_desc, void *nfit, acpi_size sz);
void acpi_nfit_shutdown(void *data);
void __acpi_nfit_notify(struct device *dev, acpi_handle handle, u32 event);
void __acpi_nvdimm_notify(struct device *dev, u32 event);
int acpi_nfit_ctl(struct nvdimm_bus_descriptor *nd_desc, struct nvdimm *nvdimm,
		unsigned int cmd, void *buf, unsigned int buf_len, int *cmd_rc);
void acpi_nfit_desc_init(struct acpi_nfit_desc *acpi_desc, struct device *dev);
bool intel_fwa_supported(struct nvdimm_bus *nvdimm_bus);
extern struct device_attribute dev_attr_firmware_activate_noidle;
#endif /* __NFIT_H__ */
