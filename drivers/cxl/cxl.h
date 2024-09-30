/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2020 Intel Corporation. */

#ifndef __CXL_H__
#define __CXL_H__

#include <linux/libnvdimm.h>
#include <linux/bitfield.h>
#include <linux/notifier.h>
#include <linux/bitops.h>
#include <linux/log2.h>
#include <linux/node.h>
#include <linux/io.h>

extern const struct nvdimm_security_ops *cxl_security_ops;

/**
 * DOC: cxl objects
 *
 * The CXL core objects like ports, decoders, and regions are shared
 * between the subsystem drivers cxl_acpi, cxl_pci, and core drivers
 * (port-driver, region-driver, nvdimm object-drivers... etc).
 */

/* CXL 2.0 8.2.4 CXL Component Register Layout and Definition */
#define CXL_COMPONENT_REG_BLOCK_SIZE SZ_64K

/* CXL 2.0 8.2.5 CXL.cache and CXL.mem Registers*/
#define CXL_CM_OFFSET 0x1000
#define CXL_CM_CAP_HDR_OFFSET 0x0
#define   CXL_CM_CAP_HDR_ID_MASK GENMASK(15, 0)
#define     CM_CAP_HDR_CAP_ID 1
#define   CXL_CM_CAP_HDR_VERSION_MASK GENMASK(19, 16)
#define     CM_CAP_HDR_CAP_VERSION 1
#define   CXL_CM_CAP_HDR_CACHE_MEM_VERSION_MASK GENMASK(23, 20)
#define     CM_CAP_HDR_CACHE_MEM_VERSION 1
#define   CXL_CM_CAP_HDR_ARRAY_SIZE_MASK GENMASK(31, 24)
#define CXL_CM_CAP_PTR_MASK GENMASK(31, 20)

#define   CXL_CM_CAP_CAP_ID_RAS 0x2
#define   CXL_CM_CAP_CAP_ID_HDM 0x5
#define   CXL_CM_CAP_CAP_HDM_VERSION 1

/* HDM decoders CXL 2.0 8.2.5.12 CXL HDM Decoder Capability Structure */
#define CXL_HDM_DECODER_CAP_OFFSET 0x0
#define   CXL_HDM_DECODER_COUNT_MASK GENMASK(3, 0)
#define   CXL_HDM_DECODER_TARGET_COUNT_MASK GENMASK(7, 4)
#define   CXL_HDM_DECODER_INTERLEAVE_11_8 BIT(8)
#define   CXL_HDM_DECODER_INTERLEAVE_14_12 BIT(9)
#define   CXL_HDM_DECODER_INTERLEAVE_3_6_12_WAY BIT(11)
#define   CXL_HDM_DECODER_INTERLEAVE_16_WAY BIT(12)
#define CXL_HDM_DECODER_CTRL_OFFSET 0x4
#define   CXL_HDM_DECODER_ENABLE BIT(1)
#define CXL_HDM_DECODER0_BASE_LOW_OFFSET(i) (0x20 * (i) + 0x10)
#define CXL_HDM_DECODER0_BASE_HIGH_OFFSET(i) (0x20 * (i) + 0x14)
#define CXL_HDM_DECODER0_SIZE_LOW_OFFSET(i) (0x20 * (i) + 0x18)
#define CXL_HDM_DECODER0_SIZE_HIGH_OFFSET(i) (0x20 * (i) + 0x1c)
#define CXL_HDM_DECODER0_CTRL_OFFSET(i) (0x20 * (i) + 0x20)
#define   CXL_HDM_DECODER0_CTRL_IG_MASK GENMASK(3, 0)
#define   CXL_HDM_DECODER0_CTRL_IW_MASK GENMASK(7, 4)
#define   CXL_HDM_DECODER0_CTRL_LOCK BIT(8)
#define   CXL_HDM_DECODER0_CTRL_COMMIT BIT(9)
#define   CXL_HDM_DECODER0_CTRL_COMMITTED BIT(10)
#define   CXL_HDM_DECODER0_CTRL_COMMIT_ERROR BIT(11)
#define   CXL_HDM_DECODER0_CTRL_HOSTONLY BIT(12)
#define CXL_HDM_DECODER0_TL_LOW(i) (0x20 * (i) + 0x24)
#define CXL_HDM_DECODER0_TL_HIGH(i) (0x20 * (i) + 0x28)
#define CXL_HDM_DECODER0_SKIP_LOW(i) CXL_HDM_DECODER0_TL_LOW(i)
#define CXL_HDM_DECODER0_SKIP_HIGH(i) CXL_HDM_DECODER0_TL_HIGH(i)

/* HDM decoder control register constants CXL 3.0 8.2.5.19.7 */
#define CXL_DECODER_MIN_GRANULARITY 256
#define CXL_DECODER_MAX_ENCODED_IG 6

static inline int cxl_hdm_decoder_count(u32 cap_hdr)
{
	int val = FIELD_GET(CXL_HDM_DECODER_COUNT_MASK, cap_hdr);

	return val ? val * 2 : 1;
}

/* Encode defined in CXL 2.0 8.2.5.12.7 HDM Decoder Control Register */
static inline int eig_to_granularity(u16 eig, unsigned int *granularity)
{
	if (eig > CXL_DECODER_MAX_ENCODED_IG)
		return -EINVAL;
	*granularity = CXL_DECODER_MIN_GRANULARITY << eig;
	return 0;
}

/* Encode defined in CXL ECN "3, 6, 12 and 16-way memory Interleaving" */
static inline int eiw_to_ways(u8 eiw, unsigned int *ways)
{
	switch (eiw) {
	case 0 ... 4:
		*ways = 1 << eiw;
		break;
	case 8 ... 10:
		*ways = 3 << (eiw - 8);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static inline int granularity_to_eig(int granularity, u16 *eig)
{
	if (granularity > SZ_16K || granularity < CXL_DECODER_MIN_GRANULARITY ||
	    !is_power_of_2(granularity))
		return -EINVAL;
	*eig = ilog2(granularity) - 8;
	return 0;
}

static inline int ways_to_eiw(unsigned int ways, u8 *eiw)
{
	if (ways > 16)
		return -EINVAL;
	if (is_power_of_2(ways)) {
		*eiw = ilog2(ways);
		return 0;
	}
	if (ways % 3)
		return -EINVAL;
	ways /= 3;
	if (!is_power_of_2(ways))
		return -EINVAL;
	*eiw = ilog2(ways) + 8;
	return 0;
}

/* RAS Registers CXL 2.0 8.2.5.9 CXL RAS Capability Structure */
#define CXL_RAS_UNCORRECTABLE_STATUS_OFFSET 0x0
#define   CXL_RAS_UNCORRECTABLE_STATUS_MASK (GENMASK(16, 14) | GENMASK(11, 0))
#define CXL_RAS_UNCORRECTABLE_MASK_OFFSET 0x4
#define   CXL_RAS_UNCORRECTABLE_MASK_MASK (GENMASK(16, 14) | GENMASK(11, 0))
#define   CXL_RAS_UNCORRECTABLE_MASK_F256B_MASK BIT(8)
#define CXL_RAS_UNCORRECTABLE_SEVERITY_OFFSET 0x8
#define   CXL_RAS_UNCORRECTABLE_SEVERITY_MASK (GENMASK(16, 14) | GENMASK(11, 0))
#define CXL_RAS_CORRECTABLE_STATUS_OFFSET 0xC
#define   CXL_RAS_CORRECTABLE_STATUS_MASK GENMASK(6, 0)
#define CXL_RAS_CORRECTABLE_MASK_OFFSET 0x10
#define   CXL_RAS_CORRECTABLE_MASK_MASK GENMASK(6, 0)
#define CXL_RAS_CAP_CONTROL_OFFSET 0x14
#define CXL_RAS_CAP_CONTROL_FE_MASK GENMASK(5, 0)
#define CXL_RAS_HEADER_LOG_OFFSET 0x18
#define CXL_RAS_CAPABILITY_LENGTH 0x58
#define CXL_HEADERLOG_SIZE SZ_512
#define CXL_HEADERLOG_SIZE_U32 SZ_512 / sizeof(u32)

/* CXL 2.0 8.2.8.1 Device Capabilities Array Register */
#define CXLDEV_CAP_ARRAY_OFFSET 0x0
#define   CXLDEV_CAP_ARRAY_CAP_ID 0
#define   CXLDEV_CAP_ARRAY_ID_MASK GENMASK_ULL(15, 0)
#define   CXLDEV_CAP_ARRAY_COUNT_MASK GENMASK_ULL(47, 32)
/* CXL 2.0 8.2.8.2 CXL Device Capability Header Register */
#define CXLDEV_CAP_HDR_CAP_ID_MASK GENMASK(15, 0)
/* CXL 2.0 8.2.8.2.1 CXL Device Capabilities */
#define CXLDEV_CAP_CAP_ID_DEVICE_STATUS 0x1
#define CXLDEV_CAP_CAP_ID_PRIMARY_MAILBOX 0x2
#define CXLDEV_CAP_CAP_ID_SECONDARY_MAILBOX 0x3
#define CXLDEV_CAP_CAP_ID_MEMDEV 0x4000

/* CXL 3.0 8.2.8.3.1 Event Status Register */
#define CXLDEV_DEV_EVENT_STATUS_OFFSET		0x00
#define CXLDEV_EVENT_STATUS_INFO		BIT(0)
#define CXLDEV_EVENT_STATUS_WARN		BIT(1)
#define CXLDEV_EVENT_STATUS_FAIL		BIT(2)
#define CXLDEV_EVENT_STATUS_FATAL		BIT(3)

#define CXLDEV_EVENT_STATUS_ALL (CXLDEV_EVENT_STATUS_INFO |	\
				 CXLDEV_EVENT_STATUS_WARN |	\
				 CXLDEV_EVENT_STATUS_FAIL |	\
				 CXLDEV_EVENT_STATUS_FATAL)

/* CXL rev 3.0 section 8.2.9.2.4; Table 8-52 */
#define CXLDEV_EVENT_INT_MODE_MASK	GENMASK(1, 0)
#define CXLDEV_EVENT_INT_MSGNUM_MASK	GENMASK(7, 4)

/* CXL 2.0 8.2.8.4 Mailbox Registers */
#define CXLDEV_MBOX_CAPS_OFFSET 0x00
#define   CXLDEV_MBOX_CAP_PAYLOAD_SIZE_MASK GENMASK(4, 0)
#define   CXLDEV_MBOX_CAP_BG_CMD_IRQ BIT(6)
#define   CXLDEV_MBOX_CAP_IRQ_MSGNUM_MASK GENMASK(10, 7)
#define CXLDEV_MBOX_CTRL_OFFSET 0x04
#define   CXLDEV_MBOX_CTRL_DOORBELL BIT(0)
#define   CXLDEV_MBOX_CTRL_BG_CMD_IRQ BIT(2)
#define CXLDEV_MBOX_CMD_OFFSET 0x08
#define   CXLDEV_MBOX_CMD_COMMAND_OPCODE_MASK GENMASK_ULL(15, 0)
#define   CXLDEV_MBOX_CMD_PAYLOAD_LENGTH_MASK GENMASK_ULL(36, 16)
#define CXLDEV_MBOX_STATUS_OFFSET 0x10
#define   CXLDEV_MBOX_STATUS_BG_CMD BIT(0)
#define   CXLDEV_MBOX_STATUS_RET_CODE_MASK GENMASK_ULL(47, 32)
#define CXLDEV_MBOX_BG_CMD_STATUS_OFFSET 0x18
#define   CXLDEV_MBOX_BG_CMD_COMMAND_OPCODE_MASK GENMASK_ULL(15, 0)
#define   CXLDEV_MBOX_BG_CMD_COMMAND_PCT_MASK GENMASK_ULL(22, 16)
#define   CXLDEV_MBOX_BG_CMD_COMMAND_RC_MASK GENMASK_ULL(47, 32)
#define   CXLDEV_MBOX_BG_CMD_COMMAND_VENDOR_MASK GENMASK_ULL(63, 48)
#define CXLDEV_MBOX_PAYLOAD_OFFSET 0x20

/*
 * Using struct_group() allows for per register-block-type helper routines,
 * without requiring block-type agnostic code to include the prefix.
 */
struct cxl_regs {
	/*
	 * Common set of CXL Component register block base pointers
	 * @hdm_decoder: CXL 2.0 8.2.5.12 CXL HDM Decoder Capability Structure
	 * @ras: CXL 2.0 8.2.5.9 CXL RAS Capability Structure
	 */
	struct_group_tagged(cxl_component_regs, component,
		void __iomem *hdm_decoder;
		void __iomem *ras;
	);
	/*
	 * Common set of CXL Device register block base pointers
	 * @status: CXL 2.0 8.2.8.3 Device Status Registers
	 * @mbox: CXL 2.0 8.2.8.4 Mailbox Registers
	 * @memdev: CXL 2.0 8.2.8.5 Memory Device Registers
	 */
	struct_group_tagged(cxl_device_regs, device_regs,
		void __iomem *status, *mbox, *memdev;
	);

	struct_group_tagged(cxl_pmu_regs, pmu_regs,
		void __iomem *pmu;
	);

	/*
	 * RCH downstream port specific RAS register
	 * @aer: CXL 3.0 8.2.1.1 RCH Downstream Port RCRB
	 */
	struct_group_tagged(cxl_rch_regs, rch_regs,
		void __iomem *dport_aer;
	);
};

struct cxl_reg_map {
	bool valid;
	int id;
	unsigned long offset;
	unsigned long size;
};

struct cxl_component_reg_map {
	struct cxl_reg_map hdm_decoder;
	struct cxl_reg_map ras;
};

struct cxl_device_reg_map {
	struct cxl_reg_map status;
	struct cxl_reg_map mbox;
	struct cxl_reg_map memdev;
};

struct cxl_pmu_reg_map {
	struct cxl_reg_map pmu;
};

/**
 * struct cxl_register_map - DVSEC harvested register block mapping parameters
 * @host: device for devm operations and logging
 * @base: virtual base of the register-block-BAR + @block_offset
 * @resource: physical resource base of the register block
 * @max_size: maximum mapping size to perform register search
 * @reg_type: see enum cxl_regloc_type
 * @component_map: cxl_reg_map for component registers
 * @device_map: cxl_reg_maps for device registers
 * @pmu_map: cxl_reg_maps for CXL Performance Monitoring Units
 */
struct cxl_register_map {
	struct device *host;
	void __iomem *base;
	resource_size_t resource;
	resource_size_t max_size;
	u8 reg_type;
	union {
		struct cxl_component_reg_map component_map;
		struct cxl_device_reg_map device_map;
		struct cxl_pmu_reg_map pmu_map;
	};
};

void cxl_probe_component_regs(struct device *dev, void __iomem *base,
			      struct cxl_component_reg_map *map);
void cxl_probe_device_regs(struct device *dev, void __iomem *base,
			   struct cxl_device_reg_map *map);
int cxl_map_component_regs(const struct cxl_register_map *map,
			   struct cxl_component_regs *regs,
			   unsigned long map_mask);
int cxl_map_device_regs(const struct cxl_register_map *map,
			struct cxl_device_regs *regs);
int cxl_map_pmu_regs(struct cxl_register_map *map, struct cxl_pmu_regs *regs);

enum cxl_regloc_type;
int cxl_count_regblock(struct pci_dev *pdev, enum cxl_regloc_type type);
int cxl_find_regblock_instance(struct pci_dev *pdev, enum cxl_regloc_type type,
			       struct cxl_register_map *map, int index);
int cxl_find_regblock(struct pci_dev *pdev, enum cxl_regloc_type type,
		      struct cxl_register_map *map);
int cxl_setup_regs(struct cxl_register_map *map);
struct cxl_dport;
resource_size_t cxl_rcd_component_reg_phys(struct device *dev,
					   struct cxl_dport *dport);

#define CXL_RESOURCE_NONE ((resource_size_t) -1)
#define CXL_TARGET_STRLEN 20

/*
 * cxl_decoder flags that define the type of memory / devices this
 * decoder supports as well as configuration lock status See "CXL 2.0
 * 8.2.5.12.7 CXL HDM Decoder 0 Control Register" for details.
 * Additionally indicate whether decoder settings were autodetected,
 * user customized.
 */
#define CXL_DECODER_F_RAM   BIT(0)
#define CXL_DECODER_F_PMEM  BIT(1)
#define CXL_DECODER_F_TYPE2 BIT(2)
#define CXL_DECODER_F_TYPE3 BIT(3)
#define CXL_DECODER_F_LOCK  BIT(4)
#define CXL_DECODER_F_ENABLE    BIT(5)
#define CXL_DECODER_F_MASK  GENMASK(5, 0)

enum cxl_decoder_type {
	CXL_DECODER_DEVMEM = 2,
	CXL_DECODER_HOSTONLYMEM = 3,
};

/*
 * Current specification goes up to 8, double that seems a reasonable
 * software max for the foreseeable future
 */
#define CXL_DECODER_MAX_INTERLEAVE 16

#define CXL_QOS_CLASS_INVALID -1

/**
 * struct cxl_decoder - Common CXL HDM Decoder Attributes
 * @dev: this decoder's device
 * @id: kernel device name id
 * @hpa_range: Host physical address range mapped by this decoder
 * @interleave_ways: number of cxl_dports in this decode
 * @interleave_granularity: data stride per dport
 * @target_type: accelerator vs expander (type2 vs type3) selector
 * @region: currently assigned region for this decoder
 * @flags: memory type capabilities and locking
 * @commit: device/decoder-type specific callback to commit settings to hw
 * @reset: device/decoder-type specific callback to reset hw settings
*/
struct cxl_decoder {
	struct device dev;
	int id;
	struct range hpa_range;
	int interleave_ways;
	int interleave_granularity;
	enum cxl_decoder_type target_type;
	struct cxl_region *region;
	unsigned long flags;
	int (*commit)(struct cxl_decoder *cxld);
	int (*reset)(struct cxl_decoder *cxld);
};

/*
 * CXL_DECODER_DEAD prevents endpoints from being reattached to regions
 * while cxld_unregister() is running
 */
enum cxl_decoder_mode {
	CXL_DECODER_NONE,
	CXL_DECODER_RAM,
	CXL_DECODER_PMEM,
	CXL_DECODER_MIXED,
	CXL_DECODER_DEAD,
};

static inline const char *cxl_decoder_mode_name(enum cxl_decoder_mode mode)
{
	static const char * const names[] = {
		[CXL_DECODER_NONE] = "none",
		[CXL_DECODER_RAM] = "ram",
		[CXL_DECODER_PMEM] = "pmem",
		[CXL_DECODER_MIXED] = "mixed",
	};

	if (mode >= CXL_DECODER_NONE && mode <= CXL_DECODER_MIXED)
		return names[mode];
	return "mixed";
}

/*
 * Track whether this decoder is reserved for region autodiscovery, or
 * free for userspace provisioning.
 */
enum cxl_decoder_state {
	CXL_DECODER_STATE_MANUAL,
	CXL_DECODER_STATE_AUTO,
};

/**
 * struct cxl_endpoint_decoder - Endpoint  / SPA to DPA decoder
 * @cxld: base cxl_decoder_object
 * @dpa_res: actively claimed DPA span of this decoder
 * @skip: offset into @dpa_res where @cxld.hpa_range maps
 * @mode: which memory type / access-mode-partition this decoder targets
 * @state: autodiscovery state
 * @pos: interleave position in @cxld.region
 */
struct cxl_endpoint_decoder {
	struct cxl_decoder cxld;
	struct resource *dpa_res;
	resource_size_t skip;
	enum cxl_decoder_mode mode;
	enum cxl_decoder_state state;
	int pos;
};

/**
 * struct cxl_switch_decoder - Switch specific CXL HDM Decoder
 * @cxld: base cxl_decoder object
 * @nr_targets: number of elements in @target
 * @target: active ordered target list in current decoder configuration
 *
 * The 'switch' decoder type represents the decoder instances of cxl_port's that
 * route from the root of a CXL memory decode topology to the endpoints. They
 * come in two flavors, root-level decoders, statically defined by platform
 * firmware, and mid-level decoders, where interleave-granularity,
 * interleave-width, and the target list are mutable.
 */
struct cxl_switch_decoder {
	struct cxl_decoder cxld;
	int nr_targets;
	struct cxl_dport *target[];
};

struct cxl_root_decoder;
typedef u64 (*cxl_hpa_to_spa_fn)(struct cxl_root_decoder *cxlrd, u64 hpa);

/**
 * struct cxl_root_decoder - Static platform CXL address decoder
 * @res: host / parent resource for region allocations
 * @region_id: region id for next region provisioning event
 * @hpa_to_spa: translate CXL host-physical-address to Platform system-physical-address
 * @platform_data: platform specific configuration data
 * @range_lock: sync region autodiscovery by address range
 * @qos_class: QoS performance class cookie
 * @cxlsd: base cxl switch decoder
 */
struct cxl_root_decoder {
	struct resource *res;
	atomic_t region_id;
	cxl_hpa_to_spa_fn hpa_to_spa;
	void *platform_data;
	struct mutex range_lock;
	int qos_class;
	struct cxl_switch_decoder cxlsd;
};

/*
 * enum cxl_config_state - State machine for region configuration
 * @CXL_CONFIG_IDLE: Any sysfs attribute can be written freely
 * @CXL_CONFIG_INTERLEAVE_ACTIVE: region size has been set, no more
 * changes to interleave_ways or interleave_granularity
 * @CXL_CONFIG_ACTIVE: All targets have been added the region is now
 * active
 * @CXL_CONFIG_RESET_PENDING: see commit_store()
 * @CXL_CONFIG_COMMIT: Soft-config has been committed to hardware
 */
enum cxl_config_state {
	CXL_CONFIG_IDLE,
	CXL_CONFIG_INTERLEAVE_ACTIVE,
	CXL_CONFIG_ACTIVE,
	CXL_CONFIG_RESET_PENDING,
	CXL_CONFIG_COMMIT,
};

/**
 * struct cxl_region_params - region settings
 * @state: allow the driver to lockdown further parameter changes
 * @uuid: unique id for persistent regions
 * @interleave_ways: number of endpoints in the region
 * @interleave_granularity: capacity each endpoint contributes to a stripe
 * @res: allocated iomem capacity for this region
 * @targets: active ordered targets in current decoder configuration
 * @nr_targets: number of targets
 *
 * State transitions are protected by the cxl_region_rwsem
 */
struct cxl_region_params {
	enum cxl_config_state state;
	uuid_t uuid;
	int interleave_ways;
	int interleave_granularity;
	struct resource *res;
	struct cxl_endpoint_decoder *targets[CXL_DECODER_MAX_INTERLEAVE];
	int nr_targets;
};

/*
 * Indicate whether this region has been assembled by autodetection or
 * userspace assembly. Prevent endpoint decoders outside of automatic
 * detection from being added to the region.
 */
#define CXL_REGION_F_AUTO 0

/*
 * Require that a committed region successfully complete a teardown once
 * any of its associated decoders have been torn down. This maintains
 * the commit state for the region since there are committed decoders,
 * but blocks cxl_region_probe().
 */
#define CXL_REGION_F_NEEDS_RESET 1

/**
 * struct cxl_region - CXL region
 * @dev: This region's device
 * @id: This region's id. Id is globally unique across all regions
 * @mode: Endpoint decoder allocation / access mode
 * @type: Endpoint decoder target type
 * @cxl_nvb: nvdimm bridge for coordinating @cxlr_pmem setup / shutdown
 * @cxlr_pmem: (for pmem regions) cached copy of the nvdimm bridge
 * @flags: Region state flags
 * @params: active + config params for the region
 * @coord: QoS access coordinates for the region
 * @memory_notifier: notifier for setting the access coordinates to node
 * @adist_notifier: notifier for calculating the abstract distance of node
 */
struct cxl_region {
	struct device dev;
	int id;
	enum cxl_decoder_mode mode;
	enum cxl_decoder_type type;
	struct cxl_nvdimm_bridge *cxl_nvb;
	struct cxl_pmem_region *cxlr_pmem;
	unsigned long flags;
	struct cxl_region_params params;
	struct access_coordinate coord[ACCESS_COORDINATE_MAX];
	struct notifier_block memory_notifier;
	struct notifier_block adist_notifier;
};

struct cxl_nvdimm_bridge {
	int id;
	struct device dev;
	struct cxl_port *port;
	struct nvdimm_bus *nvdimm_bus;
	struct nvdimm_bus_descriptor nd_desc;
};

#define CXL_DEV_ID_LEN 19

struct cxl_nvdimm {
	struct device dev;
	struct cxl_memdev *cxlmd;
	u8 dev_id[CXL_DEV_ID_LEN]; /* for nvdimm, string of 'serial' */
};

struct cxl_pmem_region_mapping {
	struct cxl_memdev *cxlmd;
	struct cxl_nvdimm *cxl_nvd;
	u64 start;
	u64 size;
	int position;
};

struct cxl_pmem_region {
	struct device dev;
	struct cxl_region *cxlr;
	struct nd_region *nd_region;
	struct range hpa_range;
	int nr_mappings;
	struct cxl_pmem_region_mapping mapping[];
};

struct cxl_dax_region {
	struct device dev;
	struct cxl_region *cxlr;
	struct range hpa_range;
};

/**
 * struct cxl_port - logical collection of upstream port devices and
 *		     downstream port devices to construct a CXL memory
 *		     decode hierarchy.
 * @dev: this port's device
 * @uport_dev: PCI or platform device implementing the upstream port capability
 * @host_bridge: Shortcut to the platform attach point for this port
 * @id: id for port device-name
 * @dports: cxl_dport instances referenced by decoders
 * @endpoints: cxl_ep instances, endpoints that are a descendant of this port
 * @regions: cxl_region_ref instances, regions mapped by this port
 * @parent_dport: dport that points to this port in the parent
 * @decoder_ida: allocator for decoder ids
 * @reg_map: component and ras register mapping parameters
 * @nr_dports: number of entries in @dports
 * @hdm_end: track last allocated HDM decoder instance for allocation ordering
 * @commit_end: cursor to track highest committed decoder for commit ordering
 * @dead: last ep has been removed, force port re-creation
 * @depth: How deep this port is relative to the root. depth 0 is the root.
 * @cdat: Cached CDAT data
 * @cdat_available: Should a CDAT attribute be available in sysfs
 * @pci_latency: Upstream latency in picoseconds
 */
struct cxl_port {
	struct device dev;
	struct device *uport_dev;
	struct device *host_bridge;
	int id;
	struct xarray dports;
	struct xarray endpoints;
	struct xarray regions;
	struct cxl_dport *parent_dport;
	struct ida decoder_ida;
	struct cxl_register_map reg_map;
	int nr_dports;
	int hdm_end;
	int commit_end;
	bool dead;
	unsigned int depth;
	struct cxl_cdat {
		void *table;
		size_t length;
	} cdat;
	bool cdat_available;
	long pci_latency;
};

/**
 * struct cxl_root - logical collection of root cxl_port items
 *
 * @port: cxl_port member
 * @ops: cxl root operations
 */
struct cxl_root {
	struct cxl_port port;
	const struct cxl_root_ops *ops;
};

static inline struct cxl_root *
to_cxl_root(const struct cxl_port *port)
{
	return container_of(port, struct cxl_root, port);
}

struct cxl_root_ops {
	int (*qos_class)(struct cxl_root *cxl_root,
			 struct access_coordinate *coord, int entries,
			 int *qos_class);
};

static inline struct cxl_dport *
cxl_find_dport_by_dev(struct cxl_port *port, const struct device *dport_dev)
{
	return xa_load(&port->dports, (unsigned long)dport_dev);
}

struct cxl_rcrb_info {
	resource_size_t base;
	u16 aer_cap;
};

/**
 * struct cxl_dport - CXL downstream port
 * @dport_dev: PCI bridge or firmware device representing the downstream link
 * @reg_map: component and ras register mapping parameters
 * @port_id: unique hardware identifier for dport in decoder target list
 * @rcrb: Data about the Root Complex Register Block layout
 * @rch: Indicate whether this dport was enumerated in RCH or VH mode
 * @port: reference to cxl_port that contains this downstream port
 * @regs: Dport parsed register blocks
 * @coord: access coordinates (bandwidth and latency performance attributes)
 * @link_latency: calculated PCIe downstream latency
 */
struct cxl_dport {
	struct device *dport_dev;
	struct cxl_register_map reg_map;
	int port_id;
	struct cxl_rcrb_info rcrb;
	bool rch;
	struct cxl_port *port;
	struct cxl_regs regs;
	struct access_coordinate coord[ACCESS_COORDINATE_MAX];
	long link_latency;
};

/**
 * struct cxl_ep - track an endpoint's interest in a port
 * @ep: device that hosts a generic CXL endpoint (expander or accelerator)
 * @dport: which dport routes to this endpoint on @port
 * @next: cxl switch port across the link attached to @dport NULL if
 *	  attached to an endpoint
 */
struct cxl_ep {
	struct device *ep;
	struct cxl_dport *dport;
	struct cxl_port *next;
};

/**
 * struct cxl_region_ref - track a region's interest in a port
 * @port: point in topology to install this reference
 * @decoder: decoder assigned for @region in @port
 * @region: region for this reference
 * @endpoints: cxl_ep references for region members beneath @port
 * @nr_targets_set: track how many targets have been programmed during setup
 * @nr_eps: number of endpoints beneath @port
 * @nr_targets: number of distinct targets needed to reach @nr_eps
 */
struct cxl_region_ref {
	struct cxl_port *port;
	struct cxl_decoder *decoder;
	struct cxl_region *region;
	struct xarray endpoints;
	int nr_targets_set;
	int nr_eps;
	int nr_targets;
};

/*
 * The platform firmware device hosting the root is also the top of the
 * CXL port topology. All other CXL ports have another CXL port as their
 * parent and their ->uport_dev / host device is out-of-line of the port
 * ancestry.
 */
static inline bool is_cxl_root(struct cxl_port *port)
{
	return port->uport_dev == port->dev.parent;
}

int cxl_num_decoders_committed(struct cxl_port *port);
bool is_cxl_port(const struct device *dev);
struct cxl_port *to_cxl_port(const struct device *dev);
struct pci_bus;
int devm_cxl_register_pci_bus(struct device *host, struct device *uport_dev,
			      struct pci_bus *bus);
struct pci_bus *cxl_port_to_pci_bus(struct cxl_port *port);
struct cxl_port *devm_cxl_add_port(struct device *host,
				   struct device *uport_dev,
				   resource_size_t component_reg_phys,
				   struct cxl_dport *parent_dport);
struct cxl_root *devm_cxl_add_root(struct device *host,
				   const struct cxl_root_ops *ops);
struct cxl_root *find_cxl_root(struct cxl_port *port);
void put_cxl_root(struct cxl_root *cxl_root);
DEFINE_FREE(put_cxl_root, struct cxl_root *, if (_T) put_cxl_root(_T))

DEFINE_FREE(put_cxl_port, struct cxl_port *, if (!IS_ERR_OR_NULL(_T)) put_device(&_T->dev))
int devm_cxl_enumerate_ports(struct cxl_memdev *cxlmd);
void cxl_bus_rescan(void);
void cxl_bus_drain(void);
struct cxl_port *cxl_pci_find_port(struct pci_dev *pdev,
				   struct cxl_dport **dport);
struct cxl_port *cxl_mem_find_port(struct cxl_memdev *cxlmd,
				   struct cxl_dport **dport);
bool schedule_cxl_memdev_detach(struct cxl_memdev *cxlmd);

struct cxl_dport *devm_cxl_add_dport(struct cxl_port *port,
				     struct device *dport, int port_id,
				     resource_size_t component_reg_phys);
struct cxl_dport *devm_cxl_add_rch_dport(struct cxl_port *port,
					 struct device *dport_dev, int port_id,
					 resource_size_t rcrb);

#ifdef CONFIG_PCIEAER_CXL
void cxl_setup_parent_dport(struct device *host, struct cxl_dport *dport);
void cxl_dport_init_ras_reporting(struct cxl_dport *dport, struct device *host);
#else
static inline void cxl_dport_init_ras_reporting(struct cxl_dport *dport,
						struct device *host) { }
#endif

struct cxl_decoder *to_cxl_decoder(struct device *dev);
struct cxl_root_decoder *to_cxl_root_decoder(struct device *dev);
struct cxl_switch_decoder *to_cxl_switch_decoder(struct device *dev);
struct cxl_endpoint_decoder *to_cxl_endpoint_decoder(struct device *dev);
bool is_root_decoder(struct device *dev);
bool is_switch_decoder(struct device *dev);
bool is_endpoint_decoder(struct device *dev);
struct cxl_root_decoder *cxl_root_decoder_alloc(struct cxl_port *port,
						unsigned int nr_targets);
struct cxl_switch_decoder *cxl_switch_decoder_alloc(struct cxl_port *port,
						    unsigned int nr_targets);
int cxl_decoder_add(struct cxl_decoder *cxld, int *target_map);
struct cxl_endpoint_decoder *cxl_endpoint_decoder_alloc(struct cxl_port *port);
int cxl_decoder_add_locked(struct cxl_decoder *cxld, int *target_map);
int cxl_decoder_autoremove(struct device *host, struct cxl_decoder *cxld);
static inline int cxl_root_decoder_autoremove(struct device *host,
					      struct cxl_root_decoder *cxlrd)
{
	return cxl_decoder_autoremove(host, &cxlrd->cxlsd.cxld);
}
int cxl_endpoint_autoremove(struct cxl_memdev *cxlmd, struct cxl_port *endpoint);

/**
 * struct cxl_endpoint_dvsec_info - Cached DVSEC info
 * @mem_enabled: cached value of mem_enabled in the DVSEC at init time
 * @ranges: Number of active HDM ranges this device uses.
 * @port: endpoint port associated with this info instance
 * @dvsec_range: cached attributes of the ranges in the DVSEC, PCIE_DEVICE
 */
struct cxl_endpoint_dvsec_info {
	bool mem_enabled;
	int ranges;
	struct cxl_port *port;
	struct range dvsec_range[2];
};

struct cxl_hdm;
struct cxl_hdm *devm_cxl_setup_hdm(struct cxl_port *port,
				   struct cxl_endpoint_dvsec_info *info);
int devm_cxl_enumerate_decoders(struct cxl_hdm *cxlhdm,
				struct cxl_endpoint_dvsec_info *info);
int devm_cxl_add_passthrough_decoder(struct cxl_port *port);
int cxl_dvsec_rr_decode(struct device *dev, struct cxl_port *port,
			struct cxl_endpoint_dvsec_info *info);

bool is_cxl_region(struct device *dev);

extern struct bus_type cxl_bus_type;

struct cxl_driver {
	const char *name;
	int (*probe)(struct device *dev);
	void (*remove)(struct device *dev);
	struct device_driver drv;
	int id;
};

#define to_cxl_drv(__drv)	container_of_const(__drv, struct cxl_driver, drv)

int __cxl_driver_register(struct cxl_driver *cxl_drv, struct module *owner,
			  const char *modname);
#define cxl_driver_register(x) __cxl_driver_register(x, THIS_MODULE, KBUILD_MODNAME)
void cxl_driver_unregister(struct cxl_driver *cxl_drv);

#define module_cxl_driver(__cxl_driver) \
	module_driver(__cxl_driver, cxl_driver_register, cxl_driver_unregister)

#define CXL_DEVICE_NVDIMM_BRIDGE	1
#define CXL_DEVICE_NVDIMM		2
#define CXL_DEVICE_PORT			3
#define CXL_DEVICE_ROOT			4
#define CXL_DEVICE_MEMORY_EXPANDER	5
#define CXL_DEVICE_REGION		6
#define CXL_DEVICE_PMEM_REGION		7
#define CXL_DEVICE_DAX_REGION		8
#define CXL_DEVICE_PMU			9

#define MODULE_ALIAS_CXL(type) MODULE_ALIAS("cxl:t" __stringify(type) "*")
#define CXL_MODALIAS_FMT "cxl:t%d"

struct cxl_nvdimm_bridge *to_cxl_nvdimm_bridge(struct device *dev);
struct cxl_nvdimm_bridge *devm_cxl_add_nvdimm_bridge(struct device *host,
						     struct cxl_port *port);
struct cxl_nvdimm *to_cxl_nvdimm(struct device *dev);
bool is_cxl_nvdimm(struct device *dev);
bool is_cxl_nvdimm_bridge(struct device *dev);
int devm_cxl_add_nvdimm(struct cxl_port *parent_port, struct cxl_memdev *cxlmd);
struct cxl_nvdimm_bridge *cxl_find_nvdimm_bridge(struct cxl_port *port);

#ifdef CONFIG_CXL_REGION
bool is_cxl_pmem_region(struct device *dev);
struct cxl_pmem_region *to_cxl_pmem_region(struct device *dev);
int cxl_add_to_region(struct cxl_port *root,
		      struct cxl_endpoint_decoder *cxled);
struct cxl_dax_region *to_cxl_dax_region(struct device *dev);
#else
static inline bool is_cxl_pmem_region(struct device *dev)
{
	return false;
}
static inline struct cxl_pmem_region *to_cxl_pmem_region(struct device *dev)
{
	return NULL;
}
static inline int cxl_add_to_region(struct cxl_port *root,
				    struct cxl_endpoint_decoder *cxled)
{
	return 0;
}
static inline struct cxl_dax_region *to_cxl_dax_region(struct device *dev)
{
	return NULL;
}
#endif

void cxl_endpoint_parse_cdat(struct cxl_port *port);
void cxl_switch_parse_cdat(struct cxl_port *port);

int cxl_endpoint_get_perf_coordinates(struct cxl_port *port,
				      struct access_coordinate *coord);
void cxl_region_perf_data_calculate(struct cxl_region *cxlr,
				    struct cxl_endpoint_decoder *cxled);
void cxl_region_shared_upstream_bandwidth_update(struct cxl_region *cxlr);

void cxl_memdev_update_perf(struct cxl_memdev *cxlmd);

void cxl_coordinates_combine(struct access_coordinate *out,
			     struct access_coordinate *c1,
			     struct access_coordinate *c2);

bool cxl_endpoint_decoder_reset_detected(struct cxl_port *port);

/*
 * Unit test builds overrides this to __weak, find the 'strong' version
 * of these symbols in tools/testing/cxl/.
 */
#ifndef __mock
#define __mock static
#endif

#endif /* __CXL_H__ */
