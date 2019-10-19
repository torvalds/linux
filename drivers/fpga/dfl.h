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
#include <linux/fs.h>
#include <linux/iopoll.h>
#include <linux/io-64-nonatomic-lo-hi.h>
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
#define DFH_TYPE		GENMASK_ULL(63, 60)	/* Feature type */
#define DFH_TYPE_AFU		1
#define DFH_TYPE_PRIVATE	3
#define DFH_TYPE_FIU		4

/* Next AFU Register Bitfield */
#define NEXT_AFU_NEXT_DFH_OFST	GENMASK_ULL(23, 0)	/* Offset to next AFU */

/* FME Header Register Set */
#define FME_HDR_DFH		DFH
#define FME_HDR_GUID_L		GUID_L
#define FME_HDR_GUID_H		GUID_H
#define FME_HDR_NEXT_AFU	NEXT_AFU
#define FME_HDR_CAP		0x30
#define FME_HDR_PORT_OFST(n)	(0x38 + ((n) * 0x8))
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
	u64 id;
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
 * struct dfl_feature - sub feature of the feature devices
 *
 * @id: sub feature id.
 * @resource_index: each sub feature has one mmio resource for its registers.
 *		    this index is used to find its mmio resource from the
 *		    feature dev (platform device)'s reources.
 * @ioaddr: mapped mmio resource address.
 * @ops: ops of this sub feature.
 */
struct dfl_feature {
	u64 id;
	int resource_index;
	void __iomem *ioaddr;
	const struct dfl_feature_ops *ops;
};

#define DEV_STATUS_IN_USE	0

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
 * @num: number for sub features.
 * @dev_status: dev status (e.g. DEV_STATUS_IN_USE).
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
	unsigned long dev_status;
	void *private;
	int num;
	struct dfl_feature features[0];
};

static inline
int dfl_feature_dev_use_begin(struct dfl_feature_platform_data *pdata)
{
	/* Test and set IN_USE flags to ensure file is exclusively used */
	if (test_and_set_bit_lock(DEV_STATUS_IN_USE, &pdata->dev_status))
		return -EBUSY;

	return 0;
}

static inline
void dfl_feature_dev_use_end(struct dfl_feature_platform_data *pdata)
{
	clear_bit_unlock(DEV_STATUS_IN_USE, &pdata->dev_status);
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

static inline int dfl_feature_platform_data_size(const int num)
{
	return sizeof(struct dfl_feature_platform_data) +
		num * sizeof(struct dfl_feature);
}

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
struct dfl_feature *dfl_get_feature_by_id(struct device *dev, u64 id)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(dev);
	struct dfl_feature *feature;

	dfl_fpga_dev_for_each_feature(pdata, feature)
		if (feature->id == id)
			return feature;

	return NULL;
}

static inline
void __iomem *dfl_get_feature_ioaddr_by_id(struct device *dev, u64 id)
{
	struct dfl_feature *feature = dfl_get_feature_by_id(dev, id);

	if (feature && feature->ioaddr)
		return feature->ioaddr;

	WARN_ON(1);
	return NULL;
}

static inline bool is_dfl_feature_present(struct device *dev, u64 id)
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
 */
struct dfl_fpga_enum_info {
	struct device *dev;
	struct list_head dfls;
};

/**
 * struct dfl_fpga_enum_dfl - DFL FPGA enumeration device feature list info
 *
 * @start: base address of this device feature list.
 * @len: size of this device feature list.
 * @ioaddr: mapped base address of this device feature list.
 * @node: node in list of device feature lists.
 */
struct dfl_fpga_enum_dfl {
	resource_size_t start;
	resource_size_t len;

	void __iomem *ioaddr;

	struct list_head node;
};

struct dfl_fpga_enum_info *dfl_fpga_enum_info_alloc(struct device *dev);
int dfl_fpga_enum_info_add_dfl(struct dfl_fpga_enum_info *info,
			       resource_size_t start, resource_size_t len,
			       void __iomem *ioaddr);
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
#endif /* __FPGA_DFL_H */
