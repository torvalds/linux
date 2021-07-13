/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2020 Intel Corporation. */

#ifndef __CXL_H__
#define __CXL_H__

#include <linux/libnvdimm.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/io.h>

/**
 * DOC: cxl objects
 *
 * The CXL core objects like ports, decoders, and regions are shared
 * between the subsystem drivers cxl_acpi, cxl_pci, and core drivers
 * (port-driver, region-driver, nvdimm object-drivers... etc).
 */

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

#define   CXL_CM_CAP_CAP_ID_HDM 0x5
#define   CXL_CM_CAP_CAP_HDM_VERSION 1

/* HDM decoders CXL 2.0 8.2.5.12 CXL HDM Decoder Capability Structure */
#define CXL_HDM_DECODER_CAP_OFFSET 0x0
#define   CXL_HDM_DECODER_COUNT_MASK GENMASK(3, 0)
#define   CXL_HDM_DECODER_TARGET_COUNT_MASK GENMASK(7, 4)
#define CXL_HDM_DECODER0_BASE_LOW_OFFSET 0x10
#define CXL_HDM_DECODER0_BASE_HIGH_OFFSET 0x14
#define CXL_HDM_DECODER0_SIZE_LOW_OFFSET 0x18
#define CXL_HDM_DECODER0_SIZE_HIGH_OFFSET 0x1c
#define CXL_HDM_DECODER0_CTRL_OFFSET 0x20

static inline int cxl_hdm_decoder_count(u32 cap_hdr)
{
	int val = FIELD_GET(CXL_HDM_DECODER_COUNT_MASK, cap_hdr);

	return val ? val * 2 : 1;
}

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

/* CXL 2.0 8.2.8.4 Mailbox Registers */
#define CXLDEV_MBOX_CAPS_OFFSET 0x00
#define   CXLDEV_MBOX_CAP_PAYLOAD_SIZE_MASK GENMASK(4, 0)
#define CXLDEV_MBOX_CTRL_OFFSET 0x04
#define   CXLDEV_MBOX_CTRL_DOORBELL BIT(0)
#define CXLDEV_MBOX_CMD_OFFSET 0x08
#define   CXLDEV_MBOX_CMD_COMMAND_OPCODE_MASK GENMASK_ULL(15, 0)
#define   CXLDEV_MBOX_CMD_PAYLOAD_LENGTH_MASK GENMASK_ULL(36, 16)
#define CXLDEV_MBOX_STATUS_OFFSET 0x10
#define   CXLDEV_MBOX_STATUS_RET_CODE_MASK GENMASK_ULL(47, 32)
#define CXLDEV_MBOX_BG_CMD_STATUS_OFFSET 0x18
#define CXLDEV_MBOX_PAYLOAD_OFFSET 0x20

#define CXL_COMPONENT_REGS() \
	void __iomem *hdm_decoder

#define CXL_DEVICE_REGS() \
	void __iomem *status; \
	void __iomem *mbox; \
	void __iomem *memdev

/* See note for 'struct cxl_regs' for the rationale of this organization */
/*
 * CXL_COMPONENT_REGS - Common set of CXL Component register block base pointers
 * @hdm_decoder: CXL 2.0 8.2.5.12 CXL HDM Decoder Capability Structure
 */
struct cxl_component_regs {
	CXL_COMPONENT_REGS();
};

/* See note for 'struct cxl_regs' for the rationale of this organization */
/*
 * CXL_DEVICE_REGS - Common set of CXL Device register block base pointers
 * @status: CXL 2.0 8.2.8.3 Device Status Registers
 * @mbox: CXL 2.0 8.2.8.4 Mailbox Registers
 * @memdev: CXL 2.0 8.2.8.5 Memory Device Registers
 */
struct cxl_device_regs {
	CXL_DEVICE_REGS();
};

/*
 * Note, the anonymous union organization allows for per
 * register-block-type helper routines, without requiring block-type
 * agnostic code to include the prefix.
 */
struct cxl_regs {
	union {
		struct {
			CXL_COMPONENT_REGS();
		};
		struct cxl_component_regs component;
	};
	union {
		struct {
			CXL_DEVICE_REGS();
		};
		struct cxl_device_regs device_regs;
	};
};

struct cxl_reg_map {
	bool valid;
	unsigned long offset;
	unsigned long size;
};

struct cxl_component_reg_map {
	struct cxl_reg_map hdm_decoder;
};

struct cxl_device_reg_map {
	struct cxl_reg_map status;
	struct cxl_reg_map mbox;
	struct cxl_reg_map memdev;
};

struct cxl_register_map {
	struct list_head list;
	u64 block_offset;
	u8 reg_type;
	u8 barno;
	union {
		struct cxl_component_reg_map component_map;
		struct cxl_device_reg_map device_map;
	};
};

void cxl_probe_component_regs(struct device *dev, void __iomem *base,
			      struct cxl_component_reg_map *map);
void cxl_probe_device_regs(struct device *dev, void __iomem *base,
			   struct cxl_device_reg_map *map);
int cxl_map_component_regs(struct pci_dev *pdev,
			   struct cxl_component_regs *regs,
			   struct cxl_register_map *map);
int cxl_map_device_regs(struct pci_dev *pdev,
			struct cxl_device_regs *regs,
			struct cxl_register_map *map);

#define CXL_RESOURCE_NONE ((resource_size_t) -1)
#define CXL_TARGET_STRLEN 20

/*
 * cxl_decoder flags that define the type of memory / devices this
 * decoder supports as well as configuration lock status See "CXL 2.0
 * 8.2.5.12.7 CXL HDM Decoder 0 Control Register" for details.
 */
#define CXL_DECODER_F_RAM   BIT(0)
#define CXL_DECODER_F_PMEM  BIT(1)
#define CXL_DECODER_F_TYPE2 BIT(2)
#define CXL_DECODER_F_TYPE3 BIT(3)
#define CXL_DECODER_F_LOCK  BIT(4)
#define CXL_DECODER_F_MASK  GENMASK(4, 0)

enum cxl_decoder_type {
       CXL_DECODER_ACCELERATOR = 2,
       CXL_DECODER_EXPANDER = 3,
};

/**
 * struct cxl_decoder - CXL address range decode configuration
 * @dev: this decoder's device
 * @id: kernel device name id
 * @range: address range considered by this decoder
 * @interleave_ways: number of cxl_dports in this decode
 * @interleave_granularity: data stride per dport
 * @target_type: accelerator vs expander (type2 vs type3) selector
 * @flags: memory type capabilities and locking
 * @target: active ordered target list in current decoder configuration
 */
struct cxl_decoder {
	struct device dev;
	int id;
	struct range range;
	int interleave_ways;
	int interleave_granularity;
	enum cxl_decoder_type target_type;
	unsigned long flags;
	struct cxl_dport *target[];
};


enum cxl_nvdimm_brige_state {
	CXL_NVB_NEW,
	CXL_NVB_DEAD,
	CXL_NVB_ONLINE,
	CXL_NVB_OFFLINE,
};

struct cxl_nvdimm_bridge {
	struct device dev;
	struct cxl_port *port;
	struct nvdimm_bus *nvdimm_bus;
	struct nvdimm_bus_descriptor nd_desc;
	struct work_struct state_work;
	enum cxl_nvdimm_brige_state state;
};

struct cxl_nvdimm {
	struct device dev;
	struct cxl_memdev *cxlmd;
	struct nvdimm *nvdimm;
};

/**
 * struct cxl_port - logical collection of upstream port devices and
 *		     downstream port devices to construct a CXL memory
 *		     decode hierarchy.
 * @dev: this port's device
 * @uport: PCI or platform device implementing the upstream port capability
 * @id: id for port device-name
 * @dports: cxl_dport instances referenced by decoders
 * @decoder_ida: allocator for decoder ids
 * @component_reg_phys: component register capability base address (optional)
 */
struct cxl_port {
	struct device dev;
	struct device *uport;
	int id;
	struct list_head dports;
	struct ida decoder_ida;
	resource_size_t component_reg_phys;
};

/**
 * struct cxl_dport - CXL downstream port
 * @dport: PCI bridge or firmware device representing the downstream link
 * @port_id: unique hardware identifier for dport in decoder target list
 * @component_reg_phys: downstream port component registers
 * @port: reference to cxl_port that contains this downstream port
 * @list: node for a cxl_port's list of cxl_dport instances
 */
struct cxl_dport {
	struct device *dport;
	int port_id;
	resource_size_t component_reg_phys;
	struct cxl_port *port;
	struct list_head list;
};

struct cxl_port *to_cxl_port(struct device *dev);
struct cxl_port *devm_cxl_add_port(struct device *host, struct device *uport,
				   resource_size_t component_reg_phys,
				   struct cxl_port *parent_port);

int cxl_add_dport(struct cxl_port *port, struct device *dport, int port_id,
		  resource_size_t component_reg_phys);

struct cxl_decoder *to_cxl_decoder(struct device *dev);
bool is_root_decoder(struct device *dev);
struct cxl_decoder *
devm_cxl_add_decoder(struct device *host, struct cxl_port *port, int nr_targets,
		     resource_size_t base, resource_size_t len,
		     int interleave_ways, int interleave_granularity,
		     enum cxl_decoder_type type, unsigned long flags);

/*
 * Per the CXL specification (8.2.5.12 CXL HDM Decoder Capability Structure)
 * single ported host-bridges need not publish a decoder capability when a
 * passthrough decode can be assumed, i.e. all transactions that the uport sees
 * are claimed and passed to the single dport. Default the range a 0-base
 * 0-length until the first CXL region is activated.
 */
static inline struct cxl_decoder *
devm_cxl_add_passthrough_decoder(struct device *host, struct cxl_port *port)
{
	return devm_cxl_add_decoder(host, port, 1, 0, 0, 1, PAGE_SIZE,
				    CXL_DECODER_EXPANDER, 0);
}

extern struct bus_type cxl_bus_type;

struct cxl_driver {
	const char *name;
	int (*probe)(struct device *dev);
	void (*remove)(struct device *dev);
	struct device_driver drv;
	int id;
};

static inline struct cxl_driver *to_cxl_drv(struct device_driver *drv)
{
	return container_of(drv, struct cxl_driver, drv);
}

int __cxl_driver_register(struct cxl_driver *cxl_drv, struct module *owner,
			  const char *modname);
#define cxl_driver_register(x) __cxl_driver_register(x, THIS_MODULE, KBUILD_MODNAME)
void cxl_driver_unregister(struct cxl_driver *cxl_drv);

#define CXL_DEVICE_NVDIMM_BRIDGE	1
#define CXL_DEVICE_NVDIMM		2

#define MODULE_ALIAS_CXL(type) MODULE_ALIAS("cxl:t" __stringify(type) "*")
#define CXL_MODALIAS_FMT "cxl:t%d"

struct cxl_nvdimm_bridge *to_cxl_nvdimm_bridge(struct device *dev);
struct cxl_nvdimm_bridge *devm_cxl_add_nvdimm_bridge(struct device *host,
						     struct cxl_port *port);
struct cxl_nvdimm *to_cxl_nvdimm(struct device *dev);
bool is_cxl_nvdimm(struct device *dev);
int devm_cxl_add_nvdimm(struct device *host, struct cxl_memdev *cxlmd);
#endif /* __CXL_H__ */
