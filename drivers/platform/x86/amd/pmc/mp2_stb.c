// SPDX-License-Identifier: GPL-2.0
/*
 * AMD MP2 STB layer
 *
 * Copyright (c) 2024, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Basavaraj Natikar <Basavaraj.Natikar@amd.com>
 */

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/iopoll.h>
#include <linux/pci.h>
#include <linux/sizes.h>
#include <linux/time.h>

#include "pmc.h"

#define VALID_MSG 0xA
#define VALID_RESPONSE 2

#define AMD_C2P_MSG0 0x10500
#define AMD_C2P_MSG1 0x10504
#define AMD_P2C_MSG0 0x10680
#define AMD_P2C_MSG1 0x10684

#define MP2_RESP_SLEEP_US 500
#define MP2_RESP_TIMEOUT_US (1600 * USEC_PER_MSEC)

#define MP2_STB_DATA_LEN_2KB 1
#define MP2_STB_DATA_LEN_16KB 4

#define MP2_MMIO_BAR 2

struct mp2_cmd_base {
	union {
		u32 ul;
		struct {
			u32 cmd_id : 4;
			u32 intr_disable : 1;
			u32 is_dma_used : 1;
			u32 rsvd : 26;
		} field;
	};
};

struct mp2_cmd_response {
	union {
		u32 resp;
		struct {
			u32 cmd_id : 4;
			u32 status : 4;
			u32 response : 4;
			u32 rsvd2 : 20;
		} field;
	};
};

struct mp2_stb_data_valid {
	union {
		u32 data_valid;
		struct {
			u32 valid : 16;
			u32 length : 16;
		} val;
	};
};

static int amd_mp2_wait_response(struct amd_mp2_dev *mp2, u8 cmd_id, u32 command_sts)
{
	struct mp2_cmd_response cmd_resp;

	if (!readl_poll_timeout(mp2->mmio + AMD_P2C_MSG0, cmd_resp.resp,
				(cmd_resp.field.response == 0x0 &&
				 cmd_resp.field.status == command_sts &&
				 cmd_resp.field.cmd_id == cmd_id), MP2_RESP_SLEEP_US,
				 MP2_RESP_TIMEOUT_US))
		return cmd_resp.field.status;

	return -ETIMEDOUT;
}

static void amd_mp2_stb_send_cmd(struct amd_mp2_dev *mp2, u8 cmd_id, bool is_dma_used)
{
	struct mp2_cmd_base cmd_base;

	cmd_base.ul = 0;
	cmd_base.field.cmd_id = cmd_id;
	cmd_base.field.intr_disable = 1;
	cmd_base.field.is_dma_used = is_dma_used;

	writeq(mp2->dma_addr, mp2->mmio + AMD_C2P_MSG1);
	writel(cmd_base.ul, mp2->mmio + AMD_C2P_MSG0);
}

static int amd_mp2_stb_region(struct amd_mp2_dev *mp2)
{
	struct device *dev = &mp2->pdev->dev;
	unsigned int len = mp2->stb_len;

	if (!mp2->stbdata) {
		mp2->vslbase = dmam_alloc_coherent(dev, len, &mp2->dma_addr, GFP_KERNEL);
		if (!mp2->vslbase)
			return -ENOMEM;

		mp2->stbdata = devm_kzalloc(dev, len, GFP_KERNEL);
		if (!mp2->stbdata)
			return -ENOMEM;
	}

	return 0;
}

static int amd_mp2_process_cmd(struct amd_mp2_dev *mp2, struct file *filp)
{
	struct device *dev = &mp2->pdev->dev;
	struct mp2_stb_data_valid stb_dv;
	int status;

	stb_dv.data_valid = readl(mp2->mmio + AMD_P2C_MSG1);

	if (stb_dv.val.valid != VALID_MSG) {
		dev_dbg(dev, "Invalid STB data\n");
		return -EBADMSG;
	}

	if (stb_dv.val.length != MP2_STB_DATA_LEN_2KB &&
	    stb_dv.val.length != MP2_STB_DATA_LEN_16KB) {
		dev_dbg(dev, "Unsupported length\n");
		return -EMSGSIZE;
	}

	mp2->stb_len = BIT(stb_dv.val.length) * SZ_1K;

	status = amd_mp2_stb_region(mp2);
	if (status) {
		dev_err(dev, "Failed to init STB region, status %d\n", status);
		return status;
	}

	amd_mp2_stb_send_cmd(mp2, VALID_MSG, true);
	status = amd_mp2_wait_response(mp2, VALID_MSG, VALID_RESPONSE);
	if (status == VALID_RESPONSE) {
		memcpy_fromio(mp2->stbdata, mp2->vslbase, mp2->stb_len);
		filp->private_data = mp2->stbdata;
		mp2->is_stb_data = true;
	} else {
		dev_err(dev, "Failed to start STB dump, status %d\n", status);
		return -EOPNOTSUPP;
	}

	return 0;
}

static int amd_mp2_stb_debugfs_open(struct inode *inode, struct file *filp)
{
	struct amd_pmc_dev *dev = filp->f_inode->i_private;
	struct amd_mp2_dev *mp2 = dev->mp2;

	if (mp2) {
		if (!mp2->is_stb_data)
			return amd_mp2_process_cmd(mp2, filp);

		filp->private_data = mp2->stbdata;

		return 0;
	}

	return -ENODEV;
}

static ssize_t amd_mp2_stb_debugfs_read(struct file *filp, char __user *buf, size_t size,
					loff_t *pos)
{
	struct amd_pmc_dev *dev = filp->f_inode->i_private;
	struct amd_mp2_dev *mp2 = dev->mp2;

	if (!mp2)
		return -ENODEV;

	if (!filp->private_data)
		return -EINVAL;

	return simple_read_from_buffer(buf, size, pos, filp->private_data, mp2->stb_len);
}

static const struct file_operations amd_mp2_stb_debugfs_fops = {
	.owner = THIS_MODULE,
	.open = amd_mp2_stb_debugfs_open,
	.read = amd_mp2_stb_debugfs_read,
};

static void amd_mp2_dbgfs_register(struct amd_pmc_dev *dev)
{
	if (!dev->dbgfs_dir)
		return;

	debugfs_create_file("stb_read_previous_boot", 0644, dev->dbgfs_dir, dev,
			    &amd_mp2_stb_debugfs_fops);
}

void amd_mp2_stb_deinit(struct amd_pmc_dev *dev)
{
	struct amd_mp2_dev *mp2 = dev->mp2;
	struct pci_dev *pdev;

	if (mp2 && mp2->pdev) {
		pdev = mp2->pdev;

		if (mp2->mmio)
			pci_clear_master(pdev);

		pci_dev_put(pdev);

		if (mp2->devres_gid)
			devres_release_group(&pdev->dev, mp2->devres_gid);

		dev->mp2 = NULL;
	}
}

void amd_mp2_stb_init(struct amd_pmc_dev *dev)
{
	struct amd_mp2_dev *mp2 = NULL;
	struct pci_dev *pdev;
	int rc;

	mp2 = devm_kzalloc(dev->dev, sizeof(*mp2), GFP_KERNEL);
	if (!mp2)
		return;

	pdev = pci_get_device(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_MP2_STB, NULL);
	if (!pdev)
		return;

	dev->mp2 = mp2;
	mp2->pdev = pdev;

	mp2->devres_gid = devres_open_group(&pdev->dev, NULL, GFP_KERNEL);
	if (!mp2->devres_gid) {
		dev_err(&pdev->dev, "devres_open_group failed\n");
		goto mp2_error;
	}

	rc = pcim_enable_device(pdev);
	if (rc) {
		dev_err(&pdev->dev, "pcim_enable_device failed\n");
		goto mp2_error;
	}

	rc = pcim_iomap_regions(pdev, BIT(MP2_MMIO_BAR), "mp2 stb");
	if (rc) {
		dev_err(&pdev->dev, "pcim_iomap_regions failed\n");
		goto mp2_error;
	}

	mp2->mmio = pcim_iomap_table(pdev)[MP2_MMIO_BAR];
	if (!mp2->mmio) {
		dev_err(&pdev->dev, "pcim_iomap_table failed\n");
		goto mp2_error;
	}

	pci_set_master(pdev);

	rc = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (rc) {
		dev_err(&pdev->dev, "failed to set DMA mask\n");
		goto mp2_error;
	}

	amd_mp2_dbgfs_register(dev);

	return;

mp2_error:
	amd_mp2_stb_deinit(dev);
}
