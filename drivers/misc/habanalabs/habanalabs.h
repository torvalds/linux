/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef HABANALABSP_H_
#define HABANALABSP_H_

#define pr_fmt(fmt)			"habanalabs: " fmt

#include <linux/cdev.h>

#define HL_NAME				"habanalabs"

struct hl_device;


/*
 * ASICs
 */

/**
 * enum hl_asic_type - supported ASIC types.
 * @ASIC_AUTO_DETECT: ASIC type will be automatically set.
 * @ASIC_GOYA: Goya device.
 * @ASIC_INVALID: Invalid ASIC type.
 */
enum hl_asic_type {
	ASIC_AUTO_DETECT,
	ASIC_GOYA,
	ASIC_INVALID
};


/*
 * FILE PRIVATE STRUCTURE
 */

/**
 * struct hl_fpriv - process information stored in FD private data.
 * @hdev: habanalabs device structure.
 * @filp: pointer to the given file structure.
 * @taskpid: current process ID.
 * @refcount: number of related contexts.
 */
struct hl_fpriv {
	struct hl_device	*hdev;
	struct file		*filp;
	struct pid		*taskpid;
	struct kref		refcount;
};


/*
 * DEVICES
 */

/* Theoretical limit only. A single host can only contain up to 4 or 8 PCIe
 * x16 cards. In extereme cases, there are hosts that can accommodate 16 cards
 */
#define HL_MAX_MINORS	256

/**
 * struct hl_device - habanalabs device structure.
 * @pdev: pointer to PCI device, can be NULL in case of simulator device.
 * @cdev: related char device.
 * @dev: realted kernel basic device structure.
 * @asic_name: ASIC specific nmae.
 * @asic_type: ASIC specific type.
 * @major: habanalabs KMD major.
 * @id: device minor.
 * @disabled: is device disabled.
 */
struct hl_device {
	struct pci_dev			*pdev;
	struct cdev			cdev;
	struct device			*dev;
	char				asic_name[16];
	enum hl_asic_type		asic_type;
	u32				major;
	u16				id;
	u8				disabled;
};


/*
 * IOCTLs
 */

/**
 * typedef hl_ioctl_t - typedef for ioctl function in the driver
 * @hpriv: pointer to the FD's private data, which contains state of
 *		user process
 * @data: pointer to the input/output arguments structure of the IOCTL
 *
 * Return: 0 for success, negative value for error
 */
typedef int hl_ioctl_t(struct hl_fpriv *hpriv, void *data);

/**
 * struct hl_ioctl_desc - describes an IOCTL entry of the driver.
 * @cmd: the IOCTL code as created by the kernel macros.
 * @func: pointer to the driver's function that should be called for this IOCTL.
 */
struct hl_ioctl_desc {
	unsigned int cmd;
	hl_ioctl_t *func;
};


/*
 * Kernel module functions that can be accessed by entire module
 */

int hl_device_open(struct inode *inode, struct file *filp);
int create_hdev(struct hl_device **dev, struct pci_dev *pdev,
		enum hl_asic_type asic_type, int minor);
void destroy_hdev(struct hl_device *hdev);
int hl_poll_timeout_memory(struct hl_device *hdev, u64 addr, u32 timeout_us,
				u32 *val);
int hl_poll_timeout_device_memory(struct hl_device *hdev, void __iomem *addr,
				u32 timeout_us, u32 *val);

int hl_device_init(struct hl_device *hdev, struct class *hclass);
void hl_device_fini(struct hl_device *hdev);
int hl_device_suspend(struct hl_device *hdev);
int hl_device_resume(struct hl_device *hdev);

#endif /* HABANALABSP_H_ */
