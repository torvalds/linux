/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Driver Header File for FPGA Device Feature List (DFL) Support
 *
 * Copyright (C) 2017-2018 Intel Corporation, Inc.
 *
 * Authors:
 *   Kang Luwei <luwei.kang@intel.com>
 *   Zhang Yi <yi.z.zhang@intel.com>
 *   Wu Hao <hao.wu@intel.com>
 *   Xiao Guangrong <guangrong.xiao@linux.intel.com>
 */

#ifndef __FPGA_DFL_H
#define __FPGA_DFL_H

#include <linux/bitfield.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/eventfd.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uuid.h>
#include <linux/fpga/fpga-region.h>

/* maximum supported number of ports */
#define MAX_DFL_FPGA_PORT_NUM 4
/* plus one for fme device */
#define MAX_DFL_FEATURE_DEV_NUM    (MAX_DFL_FPGA_PORT_NUM + 1)

/* Reserved 0xfe for Header Group Register and 0xff for AFU */
#define FEATURE_ID_FIU_HEADER		0xfe
#define FEATURE_ID_AFU			0xff

#define FME_FEATURE_ID_HEADER		FEATURE_ID_FIU_HEADER
#define FME_FEATURE_ID_THERMAL_MGMT	0x1
#define FME_FEATURE_ID_POWER_MGMT	0x2
#define FME_FEATURE_ID_GLOBAL_IPERF	0x3
#define FME_FEATURE_ID_GLOBAL_ERR	0x4
#define FME_FEATURE_ID_PR_MGMT		0x5
#define FME_FEATURE_ID_HSSI		0x6
#define FME_FEATURE_ID_GLOBAL_DPERF	0x7

#define PORT_FEATURE_ID_HEADER		FEATURE_ID_FIU_HEADER
#define PORT_FEATURE_ID_AFU		FEATURE_ID_AFU
#define PORT_FEATURE_ID_ERROR		0x10
#define PORT_FEATURE_ID_UMSG		0x11
#define PORT_FEATURE_ID_UINT		0x12
#define PORT_FEATURE_ID_STP		0x13

/*
 * Device Feature Header Register Set
 *
 * For FIUs, they all have DFH + GUID + NEXT_AFU as common header registers.
 * For AFUs, they have DFH + GUID as common header registers.
 * For private features, they only have DFH register as common header.
 */
#define DFH			0x0
#define GUID_L			0x8
#define GUID_H			0x10
#define NEXT_AFU		0x18

#define DFH_SIZE		0x8

/* Device Feature Header Register Bitfield */
#define DFH_ID			GENMASK_ULL(11, 0)	/* Feature ID */
#define DFH_ID_FIU_FME		0
#define DFH_ID_FIU_PORT		1
#define DFH_REVISION		GENMASK_ULL(15, 12)	/* Feature revision */
#define DFH_NEXT_HDR_OFST	GENMASK_ULL(39, 16)	/* Offset to next DFH */
#define DFH_EOL			BIT_ULL(40)		/* End of list */
#define DFH_VERSION		GENMASK_ULL(59, 52)	/* DFH version */
#define DFH_TYPE		GENMASK_ULL(63, 60)	/* Feature type */
#define DFH_TYPE_AFU		1
#define DFH_TYPE_PRIVATE	3
#define DFH_TYPE_FIU		4

/*
 * DFHv1 Register Offset definitons
 * In DHFv1, DFH + GUID + CSR_START + CSR_SIZE_GROUP + PARAM_HDR + PARAM_DATA
 * as common header registers
 */
#define DFHv1_CSR_ADDR		0x18  /* CSR Register start address */
#define DFHv1_CSR_SIZE_GRP	0x20  /* Size of Reg Block and Group/tag */
#define DFHv1_PARAM_HDR		0x28  /* Optional First Param header */

/*
 * CSR Rel Bit, 1'b0 = relative (offset from feature DFH start),
 * 1'b1 = absolute (ARM or other non-PCIe use)
 */
#define DFHv1_CSR_ADDR_REL	BIT_ULL(0)

/* CSR Header Register Bit Definitions */
#define DFHv1_CSR_ADDR_MASK       GENMASK_ULL(63, 1)  /* 63:1 of CSR address */

/* CSR SIZE Goup Register Bit Definitions */
#define DFHv1_CSR_SIZE_GRP_INSTANCE_ID	GENMASK_ULL(15, 0)	/* Enumeration instantiated IP */
#define DFHv1_CSR_SIZE_GRP_GROUPING_ID	GENMASK_ULL(30, 16)	/* Group Features/interfaces */
#define DFHv1_CSR_SIZE_GRP_HAS_PARAMS	BIT_ULL(31)		/* Presence of Parameters */
#define DFHv1_CSR_SIZE_GRP_SIZE		GENMASK_ULL(63, 32)	/* Size of CSR Block in bytes */

/* PARAM Header Register Bit Definitions */
#define DFHv1_PARAM_HDR_ID		GENMASK_ULL(15, 0) /* Id of this Param  */
#define DFHv1_PARAM_HDR_VER		GENMASK_ULL(31, 16) /* Version Param */
#define DFHv1_PARAM_HDR_NEXT_OFFSET	GENMASK_ULL(63, 35) /* Offset of next Param */
#define DFHv1_PARAM_HDR_NEXT_EOP	BIT_ULL(32)
#define DFHv1_PARAM_DATA		0x08  /* Offset of Param data from Param header */

#define DFHv1_PARAM_ID_MSI_X		0x1
#define DFHv1_PARAM_MSI_X_NUMV		GENMASK_ULL(63, 32)
#define DFHv1_PARAM_MSI_X_STARTV	GENMASK_ULL(31, 0)

/* Next AFU Register Bitfield */
#define NEXT_AFU_NEXT_DFH_OFST	GENMASK_ULL(23, 0)	/* Offset to next AFU */

/* FME Header Register Set */
#define FME_HDR_DFH		DFH
#define FME_HDR_GUID_L		GUID_L
#define FME_HDR_GUID_H		GUID_H
#define FME_HDR_NEXT_AFU	NEXT_AFU
#define FME_HDR_CAP		0x30
#define FME_HDR_PORT_OFST(n)	(0x38 + ((n) * 0x8))
#define FME_PORT_OFST_BAR_SKIP	7
#define FME_HDR_BITSTREAM_ID	0x60
#define FME_HDR_BITSTREAM_MD	0x68

/* FME Fab Capability Register Bitfield */
#define FME_CAP_FABRIC_VERID	GENMASK_ULL(7, 0)	/* Fabric version ID */
#define FME_CAP_SOCKET_ID	BIT_ULL(8)		/* Socket ID */
#define FME_CAP_PCIE0_LINK_AVL	BIT_ULL(12)		/* PCIE0 Link */
#define FME_CAP_PCIE1_LINK_AVL	BIT_ULL(13)		/* PCIE1 Link */
#define FME_CAP_COHR_LINK_AVL	BIT_ULL(14)		/* Coherent Link */
#define FME_CAP_IOMMU_AVL	BIT_ULL(16)		/* IOMMU available */
#define FME_CAP_NUM_PORTS	GENMASK_ULL(19, 17)	/* Number of ports */
#define FME_CAP_ADDR_WIDTH	GENMASK_ULL(29, 24)	/* Address bus width */
#define FME_CAP_CACHE_SIZE	GENMASK_ULL(43, 32)	/* cache size in KB */
#define FME_CAP_CACHE_ASSOC	GENMASK_ULL(47, 44)	/* Associativity */

/* FME Port Offset Register Bitfield */
/* Offset to port device feature header */
#define FME_PORT_OFST_DFH_OFST	GENMASK_ULL(23, 0)
/* PCI Bar ID for this port */
#define FME_PORT_OFST_BAR_ID	GENMASK_ULL(34, 32)
/* AFU MMIO access permission. 1 - VF, 0 - PF. */
#define FME_PORT_OFST_ACC_CTRL	BIT_ULL(55)
#define FME_PORT_OFST_ACC_PF	0
#define FME_PORT_OFST_ACC_VF	1
#define FME_PORT_OFST_IMP	BIT_ULL(60)

/* FME Error Capability Register */
#define FME_ERROR_CAP		0x70

/* FME Error Capability Register Bitfield */
#define FME_ERROR_CAP_SUPP_INT	BIT_ULL(0)		/* Interrupt Support */
#define FME_ERROR_CAP_INT_VECT	GENMASK_ULL(12, 1)	/* Interrupt vector */

/* PORT Header Register Set */
#define PORT_HDR_DFH		DFH
#define PORT_HDR_GUID_L		GUID_L
#define PORT_HDR_GUID_H		GUID_H
#define PORT_HDR_NEXT_AFU	NEXT_AFU
#define PORT_HDR_CAP		0x30
#define PORT_HDR_CTRL		0x38
#define PORT_HDR_STS		0x40
#define PORT_HDR_USRCLK_CMD0	0x50
#define PORT_HDR_USRCLK_CMD1	0x58
#define PORT_HDR_USRCLK_STS0	0x60
#define PORT_HDR_USRCLK_STS1	0x68

/* Port Capability Register Bitfield */
#define PORT_CAP_PORT_NUM	GENMASK_ULL(1, 0)	/* ID of this port */
#define PORT_CAP_MMIO_SIZE	GENMASK_ULL(23, 8)	/* MMIO size in KB */
#define PORT_CAP_SUPP_INT_NUM	GENMASK_ULL(35, 32)	/* Interrupts num */

/* Port Control Register Bitfield */
#define PORT_CTRL_SFTRST	BIT_ULL(0)		/* Port soft reset */
/* Latency tolerance reporting. '1' >= 40us, '0' < 40us.*/
#define PORT_CTRL_LATENCY	BIT_ULL(2)
#define PORT_CTRL_SFTRST_ACK	BIT_ULL(4)		/* HW ack for reset */

/* Port Status Register Bitfield */
#define PORT_STS_AP2_EVT	BIT_ULL(13)		/* AP2 event detected */
#define PORT_STS_AP1_EVT	BIT_ULL(12)		/* AP1 event detected */
#define PORT_STS_PWR_STATE	GENMASK_ULL(11, 8)	/* AFU power states */
#define PORT_STS_PWR_STATE_NORM 0
#define PORT_STS_PWR_STATE_AP1	1			/* 50% throttling */
#define PORT_STS_PWR_STATE_AP2	2			/* 90% throttling */
#define PORT_STS_PWR_STATE_AP6	6			/* 100% throttling */

/* Port Error Capability Register */
#define PORT_ERROR_CAP		0x38

/* Port Error Capability Register Bitfield */
#define PORT_ERROR_CAP_SUPP_INT	BIT_ULL(0)		/* Interrupt Support */
#define PORT_ERROR_CAP_INT_VECT	GENMASK_ULL(12, 1)	/* Interrupt vector */

/* Port Uint Capability Register */
#define PORT_UINT_CAP		0x8

/* Port Uint Capability Register Bitfield */
#define PORT_UINT_CAP_INT_NUM	GENMASK_ULL(11, 0)	/* Interrupts num */
#define PORT_UINT_CAP_FST_VECT	GENMASK_ULL(23, 12)	/* First Vector */

/**
 * struct dfl_fpga_port_ops - port ops
 *
 * @name: name of this port ops, to match with port platform device.
 * @owner: pointer to the module which owns this port ops.
 * @node: node to link port ops to global list.
 * @get_id: get port id from hardware.
 * @enable_set: enable/disable the port.
 */
struct dfl_fpga_port_ops {
	const char *name;
	struct module *owner;
	struct list_head node;
	int (*get_id)(struct platform_device *pdev);
	int (*enable_set)(struct platform_device *pdev, bool enable);
};

void dfl_fpga_port_ops_add(struct dfl_fpga_port_ops *ops);
void dfl_fpga_port_ops_del(struct dfl_fpga_port_ops *ops);
struct dfl_fpga_port_ops *dfl_fpga_port_ops_get(struct platform_device *pdev);
void dfl_fpga_port_ops_put(struct dfl_fpga_port_ops *ops);
int dfl_fpga_check_port_id(struct platform_device *pdev, void *pport_id);

/**
 * struct dfl_feature_id - dfl private feature id
 *
 * @id: unique dfl private feature id.
 */
struct dfl_feature_id {
	u16 id;
};

/**
 * struct dfl_feature_driver - dfl private feature driver
 *
 * @id_table: id_table for dfl private features supported by this driver.
 * @ops: ops of this dfl private feature driver.
 */
struct dfl_feature_driver {
	const struct dfl_feature_id *id_table;
	const struct dfl_feature_ops *ops;
};

/**
 * struct dfl_feature_irq_ctx - dfl private feature interrupt context
 *
 * @irq: Linux IRQ number of this interrupt.
 * @trigger: eventfd context to signal when interrupt happens.
 * @name: irq name needed when requesting irq.
 */
struct dfl_feature_irq_ctx {
	int irq;
	struct eventfd_ctx *trigger;
	char *name;
};

/**
 * struct dfl_feature - sub feature of the feature devices
 *
 * @dev: ptr to pdev of the feature device which has the sub feature.
 * @id: sub feature id.
 * @revision: revision of this sub feature.
 * @resource_index: each sub feature has one mmio resource for its registers.
 *		    this index is used to find its mmio resource from the
 *		    feature dev (platform device)'s resources.
 * @ioaddr: mapped mmio resource address.
 * @irq_ctx: interrupt context list.
 * @nr_irqs: number of interrupt contexts.
 * @ops: ops of this sub feature.
 * @ddev: ptr to the dfl device of this sub feature.
 * @priv: priv data of this feature.
 * @dfh_version: version of the DFH
 * @param_size: size of dfh parameters
 * @params: point to memory copy of dfh parameters
 */
struct dfl_feature {
	struct platform_device *dev;
	u16 id;
	u8 revision;
	int resource_index;
	void __iomem *ioaddr;
	struct dfl_feature_irq_ctx *irq_ctx;
	unsigned int nr_irqs;
	const struct dfl_feature_ops *ops;
	struct dfl_device *ddev;
	void *priv;
	u8 dfh_version;
	unsigned int param_size;
	void *params;
};

#define FEATURE_DEV_ID_UNUSED	(-1)

/**
 * struct dfl_feature_platform_data - platform data for feature devices
 *
 * @node: node to link feature devs to container device's port_dev_list.
 * @lock: mutex to protect platform data.
 * @cdev: cdev of feature dev.
 * @dev: ptr to platform device linked with this platform data.
 * @dfl_cdev: ptr to container device.
 * @id: id used for this feature device.
 * @disable_count: count for port disable.
 * @excl_open: set on feature device exclusive open.
 * @open_count: count for feature device open.
 * @num: number for sub features.
 * @private: ptr to feature dev private data.
 * @features: sub features of this feature dev.
 */
struct dfl_feature_platform_data {
	struct list_head node;
	struct mutex lock;
	struct cdev cdev;
	struct platform_device *dev;
	struct dfl_fpga_cdev *dfl_cdev;
	int id;
	unsigned int disable_count;
	bool excl_open;
	int open_count;
	void *private;
	int num;
	struct dfl_feature features[];
};

static inline
int dfl_feature_dev_use_begin(struct dfl_feature_platform_data *pdata,
			      bool excl)
{
	if (pdata->excl_open)
		return -EBUSY;

	if (excl) {
		if (pdata->open_count)
			return -EBUSY;

		pdata->excl_open = true;
	}
	pdata->open_count++;

	return 0;
}

static inline
void dfl_feature_dev_use_end(struct dfl_feature_platform_data *pdata)
{
	pdata->excl_open = false;

	if (WARN_ON(pdata->open_count <= 0))
		return;

	pdata->open_count--;
}

static inline
int dfl_feature_dev_use_count(struct dfl_feature_platform_data *pdata)
{
	return pdata->open_count;
}

static inline
void dfl_fpga_pdata_set_private(struct dfl_feature_platform_data *pdata,
				void *private)
{
	pdata->private = private;
}

static inline
void *dfl_fpga_pdata_get_private(struct dfl_feature_platform_data *pdata)
{
	return pdata->private;
}

struct dfl_feature_ops {
	int (*init)(struct platform_device *pdev, struct dfl_feature *feature);
	void (*uinit)(struct platform_device *pdev,
		      struct dfl_feature *feature);
	long (*ioctl)(struct platform_device *pdev, struct dfl_feature *feature,
		      unsigned int cmd, unsigned long arg);
};

#define DFL_FPGA_FEATURE_DEV_FME		"dfl-fme"
#define DFL_FPGA_FEATURE_DEV_PORT		"dfl-port"

void dfl_fpga_dev_feature_uinit(struct platform_device *pdev);
int dfl_fpga_dev_feature_init(struct platform_device *pdev,
			      struct dfl_feature_driver *feature_drvs);

int dfl_fpga_dev_ops_register(struct platform_device *pdev,
			      const struct file_operations *fops,
			      struct module *owner);
void dfl_fpga_dev_ops_unregister(struct platform_device *pdev);

static inline
struct platform_device *dfl_fpga_inode_to_feature_dev(struct inode *inode)
{
	struct dfl_feature_platform_data *pdata;

	pdata = container_of(inode->i_cdev, struct dfl_feature_platform_data,
			     cdev);
	return pdata->dev;
}

#define dfl_fpga_dev_for_each_feature(pdata, feature)			    \
	for ((feature) = (pdata)->features;				    \
	   (feature) < (pdata)->features + (pdata)->num; (feature)++)

static inline
struct dfl_feature *dfl_get_feature_by_id(struct device *dev, u16 id)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(dev);
	struct dfl_feature *feature;

	dfl_fpga_dev_for_each_feature(pdata, feature)
		if (feature->id == id)
			return feature;

	return NULL;
}

static inline
void __iomem *dfl_get_feature_ioaddr_by_id(struct device *dev, u16 id)
{
	struct dfl_feature *feature = dfl_get_feature_by_id(dev, id);

	if (feature && feature->ioaddr)
		return feature->ioaddr;

	WARN_ON(1);
	return NULL;
}

static inline bool is_dfl_feature_present(struct device *dev, u16 id)
{
	return !!dfl_get_feature_ioaddr_by_id(dev, id);
}

static inline
struct device *dfl_fpga_pdata_to_parent(struct dfl_feature_platform_data *pdata)
{
	return pdata->dev->dev.parent->parent;
}

static inline bool dfl_feature_is_fme(void __iomem *base)
{
	u64 v = readq(base + DFH);

	return (FIELD_GET(DFH_TYPE, v) == DFH_TYPE_FIU) &&
		(FIELD_GET(DFH_ID, v) == DFH_ID_FIU_FME);
}

static inline bool dfl_feature_is_port(void __iomem *base)
{
	u64 v = readq(base + DFH);

	return (FIELD_GET(DFH_TYPE, v) == DFH_TYPE_FIU) &&
		(FIELD_GET(DFH_ID, v) == DFH_ID_FIU_PORT);
}

static inline u8 dfl_feature_revision(void __iomem *base)
{
	return (u8)FIELD_GET(DFH_REVISION, readq(base + DFH));
}

/**
 * struct dfl_fpga_enum_info - DFL FPGA enumeration information
 *
 * @dev: parent device.
 * @dfls: list of device feature lists.
 * @nr_irqs: number of irqs for all feature devices.
 * @irq_table: Linux IRQ numbers for all irqs, indexed by hw irq numbers.
 */
struct dfl_fpga_enum_info {
	struct device *dev;
	struct list_head dfls;
	unsigned int nr_irqs;
	int *irq_table;
};

/**
 * struct dfl_fpga_enum_dfl - DFL FPGA enumeration device feature list info
 *
 * @start: base address of this device feature list.
 * @len: size of this device feature list.
 * @node: node in list of device feature lists.
 */
struct dfl_fpga_enum_dfl {
	resource_size_t start;
	resource_size_t len;
	struct list_head node;
};

struct dfl_fpga_enum_info *dfl_fpga_enum_info_alloc(struct device *dev);
int dfl_fpga_enum_info_add_dfl(struct dfl_fpga_enum_info *info,
			       resource_size_t start, resource_size_t len);
int dfl_fpga_enum_info_add_irq(struct dfl_fpga_enum_info *info,
			       unsigned int nr_irqs, int *irq_table);
void dfl_fpga_enum_info_free(struct dfl_fpga_enum_info *info);

/**
 * struct dfl_fpga_cdev - container device of DFL based FPGA
 *
 * @parent: parent device of this container device.
 * @region: base fpga region.
 * @fme_dev: FME feature device under this container device.
 * @lock: mutex lock to protect the port device list.
 * @port_dev_list: list of all port feature devices under this container device.
 * @released_port_num: released port number under this container device.
 */
struct dfl_fpga_cdev {
	struct device *parent;
	struct fpga_region *region;
	struct device *fme_dev;
	struct mutex lock;
	struct list_head port_dev_list;
	int released_port_num;
};

struct dfl_fpga_cdev *
dfl_fpga_feature_devs_enumerate(struct dfl_fpga_enum_info *info);
void dfl_fpga_feature_devs_remove(struct dfl_fpga_cdev *cdev);

/*
 * need to drop the device reference with put_device() after use port platform
 * device returned by __dfl_fpga_cdev_find_port and dfl_fpga_cdev_find_port
 * functions.
 */
struct platform_device *
__dfl_fpga_cdev_find_port(struct dfl_fpga_cdev *cdev, void *data,
			  int (*match)(struct platform_device *, void *));

static inline struct platform_device *
dfl_fpga_cdev_find_port(struct dfl_fpga_cdev *cdev, void *data,
			int (*match)(struct platform_device *, void *))
{
	struct platform_device *pdev;

	mutex_lock(&cdev->lock);
	pdev = __dfl_fpga_cdev_find_port(cdev, data, match);
	mutex_unlock(&cdev->lock);

	return pdev;
}

int dfl_fpga_cdev_release_port(struct dfl_fpga_cdev *cdev, int port_id);
int dfl_fpga_cdev_assign_port(struct dfl_fpga_cdev *cdev, int port_id);
void dfl_fpga_cdev_config_ports_pf(struct dfl_fpga_cdev *cdev);
int dfl_fpga_cdev_config_ports_vf(struct dfl_fpga_cdev *cdev, int num_vf);
int dfl_fpga_set_irq_triggers(struct dfl_feature *feature, unsigned int start,
			      unsigned int count, int32_t *fds);
long dfl_feature_ioctl_get_num_irqs(struct platform_device *pdev,
				    struct dfl_feature *feature,
				    unsigned long arg);
long dfl_feature_ioctl_set_irq(struct platform_device *pdev,
			       struct dfl_feature *feature,
			       unsigned long arg);

#endif /* __FPGA_DFL_H */
