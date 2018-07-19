// SPDX-License-Identifier: GPL-2.0
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 *   redistributing this file, you may do so under either license.
 *
 *   GPL LICENSE SUMMARY
 *
 *   Copyright (C) 2018 Advanced Micro Devices, Inc. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of version 2 of the GNU General Public License as
 *   published by the Free Software Foundation.
 *
 *   BSD LICENSE
 *
 *   Copyright (C) 2018 Advanced Micro Devices, Inc. All Rights Reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copy
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of AMD Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * AMD PCIe MP2 Communication Driver
 * Author: Shyam Sundar S K <Shyam-sundar.S-k@amd.com>
 */

#include <linux/debugfs.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include "i2c-amd-pci-mp2.h"

#define DRIVER_NAME	"pcie_mp2_amd"
#define DRIVER_DESC	"AMD(R) PCI-E MP2 Communication Driver"
#define DRIVER_VER	"1.0"

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_VERSION(DRIVER_VER);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Shyam Sundar S K <Shyam-sundar.S-k@amd.com>");

static const struct file_operations amd_mp2_debugfs_info;
static struct dentry *debugfs_dir;

int amd_mp2_connect(struct pci_dev *dev,
		    struct i2c_connect_config connect_cfg)
{
	struct amd_mp2_dev *privdata = pci_get_drvdata(dev);
	union i2c_cmd_base i2c_cmd_base;
	unsigned  long  flags;

	raw_spin_lock_irqsave(&privdata->lock, flags);
	dev_dbg(ndev_dev(privdata), "%s addr: %x id: %d\n", __func__,
		connect_cfg.dev_addr, connect_cfg.bus_id);

	i2c_cmd_base.ul = 0;
	i2c_cmd_base.s.i2c_cmd = i2c_enable;
	i2c_cmd_base.s.bus_id = connect_cfg.bus_id;
	i2c_cmd_base.s.i2c_speed = connect_cfg.i2c_speed;

	if (i2c_cmd_base.s.bus_id == i2c_bus_1) {
		writel(i2c_cmd_base.ul, privdata->mmio + AMD_C2P_MSG1);
	} else if (i2c_cmd_base.s.bus_id == i2c_bus_0) {
		writel(i2c_cmd_base.ul, privdata->mmio + AMD_C2P_MSG0);
	} else {
		dev_err(ndev_dev(privdata), "%s Invalid bus id\n", __func__);
		return -EINVAL;
	}
	raw_spin_unlock_irqrestore(&privdata->lock, flags);
	return 0;
}
EXPORT_SYMBOL_GPL(amd_mp2_connect);

int amd_mp2_read(struct pci_dev *dev, struct i2c_read_config read_cfg)
{
	struct amd_mp2_dev *privdata = pci_get_drvdata(dev);
	union i2c_cmd_base i2c_cmd_base;

	dev_dbg(ndev_dev(privdata), "%s addr: %x id: %d\n", __func__,
		read_cfg.dev_addr, read_cfg.bus_id);

	privdata->requested = true;
	i2c_cmd_base.ul = 0;
	i2c_cmd_base.s.i2c_cmd = i2c_read;
	i2c_cmd_base.s.dev_addr = read_cfg.dev_addr;
	i2c_cmd_base.s.length = read_cfg.length;
	i2c_cmd_base.s.bus_id = read_cfg.bus_id;

	if (read_cfg.length <= 32) {
		i2c_cmd_base.s.mem_type = use_c2pmsg;
		privdata->eventval.buf = (u32 *)read_cfg.buf;
		dev_dbg(ndev_dev(privdata), "%s buf: %llx\n", __func__,
			(u64)privdata->eventval.buf);
	} else {
		i2c_cmd_base.s.mem_type = use_dram;
		privdata->read_cfg.phy_addr = read_cfg.phy_addr;
		privdata->read_cfg.buf = read_cfg.buf;
		write64((u64)privdata->read_cfg.phy_addr,
			privdata->mmio + AMD_C2P_MSG2);
	}

	switch (read_cfg.i2c_speed) {
	case 0:
		i2c_cmd_base.s.i2c_speed = speed100k;
		break;
	case 1:
		i2c_cmd_base.s.i2c_speed = speed400k;
		break;
	case 2:
		i2c_cmd_base.s.i2c_speed = speed1000k;
		break;
	case 3:
		i2c_cmd_base.s.i2c_speed = speed1400k;
		break;
	case 4:
		i2c_cmd_base.s.i2c_speed = speed3400k;
		break;
	default:
		dev_err(ndev_dev(privdata), "Invalid ConnectionSpeed\n");
	}

	if (i2c_cmd_base.s.bus_id == i2c_bus_1) {
		writel(i2c_cmd_base.ul, privdata->mmio + AMD_C2P_MSG1);
	} else if (i2c_cmd_base.s.bus_id == i2c_bus_0) {
		writel(i2c_cmd_base.ul, privdata->mmio + AMD_C2P_MSG0);
	} else {
		dev_err(ndev_dev(privdata), "%s Invalid bus id\n", __func__);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(amd_mp2_read);

int amd_mp2_write(struct pci_dev *dev, struct i2c_write_config write_cfg)
{
	struct amd_mp2_dev *privdata = pci_get_drvdata(dev);
	union i2c_cmd_base i2c_cmd_base;
	int i;
	int buf_len;

	privdata->requested = true;
	dev_dbg(ndev_dev(privdata), "%s addr: %x id: %d\n", __func__,
		write_cfg.dev_addr, write_cfg.bus_id);

	i2c_cmd_base.ul = 0;
	i2c_cmd_base.s.i2c_cmd = i2c_write;
	i2c_cmd_base.s.dev_addr = write_cfg.dev_addr;
	i2c_cmd_base.s.length = write_cfg.length;
	i2c_cmd_base.s.bus_id = write_cfg.bus_id;

	switch (write_cfg.i2c_speed) {
	case 0:
		i2c_cmd_base.s.i2c_speed = speed100k;
		break;
	case 1:
		i2c_cmd_base.s.i2c_speed = speed400k;
		break;
	case 2:
		i2c_cmd_base.s.i2c_speed = speed1000k;
		break;
	case 3:
		i2c_cmd_base.s.i2c_speed = speed1400k;
		break;
	case 4:
		i2c_cmd_base.s.i2c_speed = speed3400k;
		break;
	default:
		dev_err(ndev_dev(privdata), "Invalid ConnectionSpeed\n");
	}

	if (write_cfg.length <= 32) {
		i2c_cmd_base.s.mem_type = use_c2pmsg;
		buf_len = (write_cfg.length + 3) / 4;
		for (i = 0; i < buf_len; i++) {
			writel(write_cfg.buf[i],
			       privdata->mmio + (AMD_C2P_MSG2 + i * 4));
		}
	} else {
		i2c_cmd_base.s.mem_type = use_dram;
		privdata->write_cfg.phy_addr = write_cfg.phy_addr;
		write64((u64)privdata->write_cfg.phy_addr,
			privdata->mmio + AMD_C2P_MSG2);
	}

	if (i2c_cmd_base.s.bus_id == i2c_bus_1) {
		writel(i2c_cmd_base.ul, privdata->mmio + AMD_C2P_MSG1);
	} else if (i2c_cmd_base.s.bus_id == i2c_bus_0) {
		writel(i2c_cmd_base.ul, privdata->mmio + AMD_C2P_MSG0);
	} else {
		dev_err(ndev_dev(privdata), "%s Invalid bus id\n", __func__);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(amd_mp2_write);

int amd_i2c_register_cb(struct pci_dev *dev, const struct amd_i2c_pci_ops *ops,
			void *dev_ctx)
{
	struct amd_mp2_dev *privdata = pci_get_drvdata(dev);

	privdata->ops = ops;
	privdata->i2c_dev_ctx = dev_ctx;

	if (!privdata->ops || !privdata->i2c_dev_ctx)
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL_GPL(amd_i2c_register_cb);

static void amd_mp2_pci_work(struct work_struct *work)
{
	struct amd_mp2_dev *privdata = mp2_dev(work);
	u32 readdata = 0;
	int i;
	int buf_len;
	int sts = privdata->eventval.base.r.status;
	int res = privdata->eventval.base.r.response;
	int len = privdata->eventval.base.r.length;

	if (res == command_success && sts == i2c_readcomplete_event) {
		if (privdata->ops->read_complete) {
			if (len <= 32) {
				buf_len = (len + 3) / 4;
				for (i = 0; i < buf_len; i++) {
					readdata = readl(privdata->mmio +
							(AMD_C2P_MSG2 + i * 4));
					privdata->eventval.buf[i] = readdata;
				}
				privdata->ops->read_complete(privdata->eventval,
						privdata->i2c_dev_ctx);
			} else {
				privdata->ops->read_complete(privdata->eventval,
						privdata->i2c_dev_ctx);
			}
		}
	} else if (res == command_success && sts == i2c_writecomplete_event) {
		if (privdata->ops->write_complete)
			privdata->ops->write_complete(privdata->eventval,
					privdata->i2c_dev_ctx);
	} else if (res == command_success && sts == i2c_busenable_complete) {
		if (privdata->ops->connect_complete)
			privdata->ops->connect_complete(privdata->eventval,
					privdata->i2c_dev_ctx);
	} else {
		dev_err(ndev_dev(privdata), "ERROR!!nothing to be handled !\n");
	}
}

static irqreturn_t amd_mp2_irq_isr(int irq, void *dev)
{
	struct amd_mp2_dev *privdata = dev;
	u32 val = 0;
	unsigned long  flags;

	raw_spin_lock_irqsave(&privdata->lock, flags);
	val = readl(privdata->mmio + AMD_P2C_MSG1);
	if (val != 0) {
		writel(0, privdata->mmio + AMD_P2C_MSG_INTEN);
		privdata->eventval.base.ul = val;
	} else {
		val = readl(privdata->mmio + AMD_P2C_MSG2);
		if (val != 0) {
			writel(0, privdata->mmio + AMD_P2C_MSG_INTEN);
			privdata->eventval.base.ul = val;
		}
	}

	raw_spin_unlock_irqrestore(&privdata->lock, flags);
	if (!privdata->ops)
		return IRQ_NONE;

	if (!privdata->requested)
		return IRQ_HANDLED;

	privdata->requested = false;
	schedule_delayed_work(&privdata->work, 0);

	return IRQ_HANDLED;
}

static ssize_t amd_mp2_debugfs_read(struct file *filp, char __user *ubuf,
				    size_t count, loff_t *offp)
{
	struct amd_mp2_dev *privdata;
	void __iomem *mmio;
	u8 *buf;
	size_t buf_size;
	ssize_t ret, off;
	union {
		u64 v64;
		u32 v32;
		u16 v16;
	} u;

	privdata = filp->private_data;
	mmio = privdata->mmio;
	buf_size = min(count, 0x800ul);
	buf = kmalloc(buf_size, GFP_KERNEL);

	if (!buf)
		return -ENOMEM;

	off = 0;
	off += scnprintf(buf + off, buf_size - off,
			"Mp2 Device Information:\n");

	off += scnprintf(buf + off, buf_size - off,
			"========================\n");
	off += scnprintf(buf + off, buf_size - off,
			"\tMP2 C2P Message Register Dump:\n\n");
	u.v32 = readl(privdata->mmio + AMD_C2P_MSG0);
	off += scnprintf(buf + off, buf_size - off,
			"AMD_C2P_MSG0 -\t\t\t%#06x\n", u.v32);

	u.v32 = readl(privdata->mmio + AMD_C2P_MSG1);
	off += scnprintf(buf + off, buf_size - off,
			"AMD_C2P_MSG1 -\t\t\t%#06x\n", u.v32);

	u.v32 = readl(privdata->mmio + AMD_C2P_MSG2);
	off += scnprintf(buf + off, buf_size - off,
			"AMD_C2P_MSG2 -\t\t\t%#06x\n", u.v32);

	u.v32 = readl(privdata->mmio + AMD_C2P_MSG3);
	off += scnprintf(buf + off, buf_size - off,
			"AMD_C2P_MSG3 -\t\t\t%#06x\n", u.v32);

	u.v32 = readl(privdata->mmio + AMD_C2P_MSG4);
	off += scnprintf(buf + off, buf_size - off,
			"AMD_C2P_MSG4 -\t\t\t%#06x\n", u.v32);

	u.v32 = readl(privdata->mmio + AMD_C2P_MSG5);
	off += scnprintf(buf + off, buf_size - off,
			"AMD_C2P_MSG5 -\t\t\t%#06x\n", u.v32);

	u.v32 = readl(privdata->mmio + AMD_C2P_MSG6);
	off += scnprintf(buf + off, buf_size - off,
			"AMD_C2P_MSG6 -\t\t\t%#06x\n", u.v32);

	u.v32 = readl(privdata->mmio + AMD_C2P_MSG7);
	off += scnprintf(buf + off, buf_size - off,
			"AMD_C2P_MSG7 -\t\t\t%#06x\n", u.v32);

	u.v32 = readl(privdata->mmio + AMD_C2P_MSG8);
	off += scnprintf(buf + off, buf_size - off,
			"AMD_C2P_MSG8 -\t\t\t%#06x\n", u.v32);

	u.v32 = readl(privdata->mmio + AMD_C2P_MSG9);
	off += scnprintf(buf + off, buf_size - off,
			"AMD_C2P_MSG9 -\t\t\t%#06x\n", u.v32);

	off += scnprintf(buf + off, buf_size - off,
			"\n\tMP2 P2C Message Register Dump:\n\n");

	u.v32 = readl(privdata->mmio + AMD_P2C_MSG1);
	off += scnprintf(buf + off, buf_size - off,
			"AMD_P2C_MSG1 -\t\t\t%#06x\n", u.v32);

	u.v32 = readl(privdata->mmio + AMD_P2C_MSG2);
	off += scnprintf(buf + off, buf_size - off,
			"AMD_P2C_MSG2 -\t\t\t%#06x\n", u.v32);

	u.v32 = readl(privdata->mmio + AMD_P2C_MSG_INTEN);
	off += scnprintf(buf + off, buf_size - off,
			"AMD_P2C_MSG_INTEN -\t\t%#06x\n", u.v32);

	u.v32 = readl(privdata->mmio + AMD_P2C_MSG_INTSTS);
	off += scnprintf(buf + off, buf_size - off,
			"AMD_P2C_MSG_INTSTS -\t\t%#06x\n", u.v32);

	ret = simple_read_from_buffer(ubuf, count, offp, buf, off);
	kfree(buf);
	return ret;
}

static void amd_mp2_init_debugfs(struct amd_mp2_dev *privdata)
{
	if (!debugfs_dir) {
		privdata->debugfs_dir = NULL;
		privdata->debugfs_info = NULL;
	} else {
		privdata->debugfs_dir = debugfs_create_dir(ndev_name(privdata),
							   debugfs_dir);
		if (!privdata->debugfs_dir) {
			privdata->debugfs_info = NULL;
		} else {
			privdata->debugfs_info = debugfs_create_file(
					"info", 0400, privdata->debugfs_dir,
					privdata, &amd_mp2_debugfs_info);
		}
	}
}

static void amd_mp2_deinit_debugfs(struct amd_mp2_dev *privdata)
{
	debugfs_remove_recursive(privdata->debugfs_dir);
}

static void amd_mp2_clear_reg(struct amd_mp2_dev *privdata)
{
	int reg = 0;

	for (reg = AMD_C2P_MSG0; reg <= AMD_C2P_MSG9; reg += 4)
		writel(0, privdata->mmio + reg);

	for (reg = AMD_P2C_MSG0; reg <= AMD_P2C_MSG2; reg += 4)
		writel(0, privdata->mmio + reg);
}

static int amd_mp2_pci_init(struct amd_mp2_dev *privdata, struct pci_dev *pdev)
{
	int rc;
	int bar_index = 2;
	resource_size_t size, base;

	pci_set_drvdata(pdev, privdata);

	rc = pci_enable_device(pdev);
	if (rc)
		goto err_pci_enable;

	rc = pci_request_regions(pdev, DRIVER_NAME);
	if (rc)
		goto err_pci_regions;

	pci_set_master(pdev);

	rc = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (rc) {
		rc = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (rc)
			goto err_dma_mask;
		dev_warn(ndev_dev(privdata), "Cannot DMA highmem\n");
	}

	rc = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
	if (rc) {
		rc = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
		if (rc)
			goto err_dma_mask;
		dev_warn(ndev_dev(privdata), "Cannot DMA consistent highmem\n");
	}

	base = pci_resource_start(pdev, bar_index);
	size = pci_resource_len(pdev, bar_index);
	dev_dbg(ndev_dev(privdata), "Base addr:%llx size:%llx\n", base, size);

	privdata->mmio = ioremap(base, size);
	if (!privdata->mmio) {
		rc = -EIO;
		goto err_dma_mask;
	}

	/* Try to set up intx irq */
	pci_intx(pdev, 1);
	privdata->eventval.buf = NULL;
	privdata->requested = false;
	raw_spin_lock_init(&privdata->lock);
	rc = request_irq(pdev->irq, amd_mp2_irq_isr, IRQF_SHARED, "mp2_irq_isr",
			 privdata);
	if (rc)
		goto err_intx_request;

	return 0;

err_intx_request:
	return rc;
err_dma_mask:
	pci_clear_master(pdev);
	pci_release_regions(pdev);
err_pci_regions:
	pci_disable_device(pdev);
err_pci_enable:
	pci_set_drvdata(pdev, NULL);
	return rc;
}

static void amd_mp2_pci_deinit(struct amd_mp2_dev *privdata)
{
	struct pci_dev *pdev = ndev_pdev(privdata);

	pci_iounmap(pdev, privdata->mmio);

	pci_clear_master(pdev);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
}

static int amd_mp2_pci_probe(struct pci_dev *pdev,
			     const struct pci_device_id *id)
{
	struct amd_mp2_dev *privdata;
	int rc;

	dev_info(&pdev->dev, "MP2 device found [%04x:%04x] (rev %x)\n",
		 (int)pdev->vendor, (int)pdev->device, (int)pdev->revision);

	privdata = kzalloc(sizeof(*privdata), GFP_KERNEL);
	privdata->pdev = pdev;

	if (!privdata) {
		rc = -ENOMEM;
		goto err_dev;
	}

	rc = amd_mp2_pci_init(privdata, pdev);
	if (rc)
		goto err_pci_init;
	dev_dbg(&pdev->dev, "pci init done.\n");

	INIT_DELAYED_WORK(&privdata->work, amd_mp2_pci_work);

	amd_mp2_init_debugfs(privdata);
	dev_info(&pdev->dev, "MP2 device registered.\n");
	return 0;

err_pci_init:
	kfree(privdata);
err_dev:
	dev_err(&pdev->dev, "Memory Allocation Failed\n");
	return rc;
}

static void amd_mp2_pci_remove(struct pci_dev *pdev)
{
	struct amd_mp2_dev *privdata = pci_get_drvdata(pdev);

	amd_mp2_deinit_debugfs(privdata);
	amd_mp2_clear_reg(privdata);
	cancel_delayed_work_sync(&privdata->work);
	free_irq(pdev->irq, privdata);
	pci_intx(pdev, 0);
	amd_mp2_pci_deinit(privdata);
	kfree(privdata);
}

static const struct file_operations amd_mp2_debugfs_info = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = amd_mp2_debugfs_read,
};

static const struct pci_device_id amd_mp2_pci_tbl[] = {
	{PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_MP2)},
	{0}
};
MODULE_DEVICE_TABLE(pci, amd_mp2_pci_tbl);

#ifdef CONFIG_PM_SLEEP
static int amd_mp2_pci_device_suspend(struct pci_dev *pdev, pm_message_t mesg)
{
	struct amd_mp2_dev *privdata = pci_get_drvdata(pdev);

	if (!privdata)
		return -EINVAL;

	return 0;
}

static int amd_mp2_pci_device_resume(struct pci_dev *pdev)
{
	struct amd_mp2_dev *privdata = pci_get_drvdata(pdev);

	if (!privdata)
		return -EINVAL;

	return 0;
}
#endif

static struct pci_driver amd_mp2_pci_driver = {
	.name		= DRIVER_NAME,
	.id_table	= amd_mp2_pci_tbl,
	.probe		= amd_mp2_pci_probe,
	.remove		= amd_mp2_pci_remove,
#ifdef CONFIG_PM_SLEEP
	.suspend		= amd_mp2_pci_device_suspend,
	.resume			= amd_mp2_pci_device_resume,
#endif
};

static int __init amd_mp2_pci_driver_init(void)
{
	pr_info("%s: %s Version: %s\n", DRIVER_NAME, DRIVER_DESC, DRIVER_VER);

	if (debugfs_initialized())
		debugfs_dir = debugfs_create_dir(KBUILD_MODNAME, NULL);

	return pci_register_driver(&amd_mp2_pci_driver);
}
module_init(amd_mp2_pci_driver_init);

static void __exit amd_mp2_pci_driver_exit(void)
{
	pci_unregister_driver(&amd_mp2_pci_driver);
	debugfs_remove_recursive(debugfs_dir);
}
module_exit(amd_mp2_pci_driver_exit);
