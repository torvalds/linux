/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2020-2021 Intel Corporation. */
#ifndef __CXL_MEM_H__
#define __CXL_MEM_H__
#include <linux/cdev.h>
#include "cxl.h"

/* CXL 2.0 8.2.8.5.1.1 Memory Device Status Register */
#define CXLMDEV_STATUS_OFFSET 0x0
#define   CXLMDEV_DEV_FATAL BIT(0)
#define   CXLMDEV_FW_HALT BIT(1)
#define   CXLMDEV_STATUS_MEDIA_STATUS_MASK GENMASK(3, 2)
#define     CXLMDEV_MS_NOT_READY 0
#define     CXLMDEV_MS_READY 1
#define     CXLMDEV_MS_ERROR 2
#define     CXLMDEV_MS_DISABLED 3
#define CXLMDEV_READY(status)                                                  \
	(FIELD_GET(CXLMDEV_STATUS_MEDIA_STATUS_MASK, status) ==                \
	 CXLMDEV_MS_READY)
#define   CXLMDEV_MBOX_IF_READY BIT(4)
#define   CXLMDEV_RESET_NEEDED_MASK GENMASK(7, 5)
#define     CXLMDEV_RESET_NEEDED_NOT 0
#define     CXLMDEV_RESET_NEEDED_COLD 1
#define     CXLMDEV_RESET_NEEDED_WARM 2
#define     CXLMDEV_RESET_NEEDED_HOT 3
#define     CXLMDEV_RESET_NEEDED_CXL 4
#define CXLMDEV_RESET_NEEDED(status)                                           \
	(FIELD_GET(CXLMDEV_RESET_NEEDED_MASK, status) !=                       \
	 CXLMDEV_RESET_NEEDED_NOT)

/**
 * struct cdevm_file_operations - devm coordinated cdev file operations
 * @fops: file operations that are synchronized against @shutdown
 * @shutdown: disconnect driver data
 *
 * @shutdown is invoked in the devres release path to disconnect any
 * driver instance data from @dev. It assumes synchronization with any
 * fops operation that requires driver data. After @shutdown an
 * operation may only reference @device data.
 */
struct cdevm_file_operations {
	struct file_operations fops;
	void (*shutdown)(struct device *dev);
};

/**
 * struct cxl_memdev - CXL bus object representing a Type-3 Memory Device
 * @dev: driver core device object
 * @cdev: char dev core object for ioctl operations
 * @cxlm: pointer to the parent device driver data
 * @id: id number of this memdev instance.
 */
struct cxl_memdev {
	struct device dev;
	struct cdev cdev;
	struct cxl_mem *cxlm;
	int id;
};

static inline struct cxl_memdev *to_cxl_memdev(struct device *dev)
{
	return container_of(dev, struct cxl_memdev, dev);
}

struct cxl_memdev *
devm_cxl_add_memdev(struct device *host, struct cxl_mem *cxlm,
		    const struct cdevm_file_operations *cdevm_fops);

/**
 * struct cxl_mem - A CXL memory device
 * @pdev: The PCI device associated with this CXL device.
 * @cxlmd: Logical memory device chardev / interface
 * @regs: Parsed register blocks
 * @payload_size: Size of space for payload
 *                (CXL 2.0 8.2.8.4.3 Mailbox Capabilities Register)
 * @lsa_size: Size of Label Storage Area
 *                (CXL 2.0 8.2.9.5.1.1 Identify Memory Device)
 * @mbox_mutex: Mutex to synchronize mailbox access.
 * @firmware_version: Firmware version for the memory device.
 * @enabled_cmds: Hardware commands found enabled in CEL.
 * @pmem_range: Persistent memory capacity information.
 * @ram_range: Volatile memory capacity information.
 */
struct cxl_mem {
	struct pci_dev *pdev;
	struct cxl_memdev *cxlmd;

	struct cxl_regs regs;

	size_t payload_size;
	size_t lsa_size;
	struct mutex mbox_mutex; /* Protects device mailbox and firmware */
	char firmware_version[0x10];
	unsigned long *enabled_cmds;

	struct range pmem_range;
	struct range ram_range;
	u64 total_bytes;
	u64 volatile_only_bytes;
	u64 persistent_only_bytes;
	u64 partition_align_bytes;

	u64 active_volatile_bytes;
	u64 active_persistent_bytes;
	u64 next_volatile_bytes;
	u64 next_persistent_bytes;
};
#endif /* __CXL_MEM_H__ */
