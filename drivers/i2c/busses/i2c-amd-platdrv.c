// SPDX-License-Identifier: GPL-2.0
/*
 *
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

 * AMD I2C Platform Driver
 * Author: Nehal Bakulchandra Shah <Nehal-bakulchandra.shah@amd.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>
#include <linux/delay.h>
#include "i2c-amd-pci-mp2.h"
#include <linux/dma-mapping.h>
#define  DRIVER_NAME "AMD-I2C-PLATDRV"

struct amd_i2c_dev {
	struct platform_device *pdev;
	struct i2c_adapter adapter;
	struct amd_i2c_common i2c_common;
	struct completion msg_complete;
	struct i2c_msg *msg_buf;
	bool is_configured;
	u8 bus_id;
	u8 *buf;

};

static int i2c_amd_read_completion(struct i2c_event event, void *dev_ctx)
{
	struct amd_i2c_dev *i2c_dev = (struct amd_i2c_dev *)dev_ctx;
	struct amd_i2c_common *commond = &i2c_dev->i2c_common;
	int i;
	int buf_len;

	if (event.base.r.status == i2c_readcomplete_event) {
		if (event.base.r.length <= 32) {
			pr_devel(" in %s i2c_dev->msg_buf :%p\n",
				 __func__, i2c_dev->msg_buf);

			memcpy(i2c_dev->msg_buf->buf,
			       (unsigned char *)event.buf, event.base.r.length);

			buf_len = (event.base.r.length + 3) / 4;
			for (i = 0; i < buf_len; i++)
				pr_devel("%s:%s readdata:%x\n",
					 DRIVER_NAME, __func__, event.buf[i]);

		} else {
			memcpy(i2c_dev->msg_buf->buf,
			       (unsigned char *)commond->read_cfg.buf,
				event.base.r.length);
			pr_devel("%s:%s virt:%llx phy_addr:%llx\n",
				 DRIVER_NAME, __func__,
				(u64)commond->read_cfg.buf,
				(u64)commond->read_cfg.phy_addr);

			buf_len = (event.base.r.length + 3) / 4;
			for (i = 0; i < buf_len; i++)
				pr_devel("%s:%s readdata:%x\n",
					 DRIVER_NAME, __func__, ((unsigned int *)
				commond->read_cfg.buf)[i]);
		}

		complete(&i2c_dev->msg_complete);
	}

	return 0;
}

static int i2c_amd_write_completion(struct i2c_event event, void *dev_ctx)
{
	struct amd_i2c_dev *i2c_dev = (struct amd_i2c_dev *)dev_ctx;

	if (event.base.r.status == i2c_writecomplete_event)
		complete(&i2c_dev->msg_complete);

	return 0;
}

static int i2c_amd_connect_completion(struct i2c_event event, void *dev_ctx)
{
	struct amd_i2c_dev *i2c_dev = (struct amd_i2c_dev *)dev_ctx;

	if (event.base.r.status == i2c_busenable_complete)
		complete(&i2c_dev->msg_complete);

	return 0;
}

static const struct amd_i2c_pci_ops data_handler = {
		.read_complete = i2c_amd_read_completion,
		.write_complete = i2c_amd_write_completion,
		.connect_complete = i2c_amd_connect_completion,
};

static int i2c_amd_pci_configure(struct amd_i2c_dev *i2c_dev, int slaveaddr)
{
	struct amd_i2c_common *i2c_common = &i2c_dev->i2c_common;
	int ret;

	amd_i2c_register_cb(i2c_common->pdev, &data_handler, (void *)i2c_dev);
	i2c_common->connect_cfg.bus_id = i2c_dev->bus_id;
	i2c_common->connect_cfg.dev_addr = slaveaddr;
	i2c_common->connect_cfg.i2c_speed = speed400k;

	ret = amd_mp2_connect(i2c_common->pdev, i2c_common->connect_cfg);
	if (ret)
		return -1;

	mdelay(100);

	i2c_common->write_cfg.bus_id = i2c_dev->bus_id;
	i2c_common->write_cfg.dev_addr = slaveaddr;
	i2c_common->write_cfg.i2c_speed = speed400k;

	i2c_common->read_cfg.bus_id = i2c_dev->bus_id;
	i2c_common->read_cfg.dev_addr = slaveaddr;
	i2c_common->read_cfg.i2c_speed = speed400k;

	return 0;
}

static int i2c_amd_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	struct amd_i2c_dev *dev = i2c_get_adapdata(adap);
	struct amd_i2c_common *i2c_common = &dev->i2c_common;

	int i;
	unsigned long timeout;
	struct i2c_msg *pmsg;
	unsigned char *dma_buf = NULL;

	dma_addr_t phys;

	reinit_completion(&dev->msg_complete);
	if (dev->is_configured == 0) {
		i2c_amd_pci_configure(dev, msgs->addr);
		timeout = wait_for_completion_timeout(&dev->msg_complete, 50);
		dev->is_configured = 1;
	}

	for (i = 0; i < num; i++) {
		pmsg = &msgs[i];
		if (pmsg->flags & I2C_M_RD) {
			if (pmsg->len <= 32) {
				i2c_common->read_cfg.buf = dev->buf;
				i2c_common->read_cfg.length = pmsg->len;
				i2c_common->read_cfg.phy_addr =
							virt_to_phys(dev->buf);
			} else {
				dma_buf = (u8 *)dma_alloc_coherent(&i2c_common->pdev->dev,
					pmsg->len, &phys, GFP_KERNEL);

				if (!dma_buf)
					return -ENOMEM;

				i2c_common->read_cfg.buf = dma_buf;
				i2c_common->read_cfg.length = pmsg->len;
				i2c_common->read_cfg.phy_addr = phys;
			}
			dev->msg_buf = pmsg;
			amd_mp2_read(i2c_common->pdev,
				     i2c_common->read_cfg);
			timeout = wait_for_completion_timeout
					(&dev->msg_complete, 50);
			if (pmsg->len > 32 && dma_buf)
				dma_free_coherent(&i2c_common->pdev->dev,
						  pmsg->len, dma_buf, phys);

		} else {
			i2c_common->write_cfg.buf = (unsigned int *)pmsg->buf;
			i2c_common->write_cfg.length = pmsg->len;
			amd_mp2_write(i2c_common->pdev,
				      i2c_common->write_cfg);

			timeout = wait_for_completion_timeout
						(&dev->msg_complete, 50);
		}
	}
	return num;
}

static u32 i2c_amd_func(struct i2c_adapter *a)
{
	return I2C_FUNC_I2C;
}

static const struct i2c_algorithm i2c_amd_algorithm = {
	.master_xfer = i2c_amd_xfer,
	.functionality = i2c_amd_func,
};

static int i2c_amd_probe(struct platform_device *pdev)
{
	int ret;
	struct amd_i2c_dev *i2c_dev;
	struct device *dev = &pdev->dev;
	acpi_handle handle = ACPI_HANDLE(&pdev->dev);
	struct acpi_device *adev;
	const char *uid = NULL;

	i2c_dev = devm_kzalloc(dev, sizeof(*i2c_dev), GFP_KERNEL);
	if (!i2c_dev)
		return -ENOMEM;

	i2c_dev->pdev = pdev;

	if (!acpi_bus_get_device(handle, &adev)) {
		pr_err(" i2c0  pdev->id=%s\n", adev->pnp.unique_id);
		uid = adev->pnp.unique_id;
	}

	if (strcmp(uid, "0") == 0) {
		pr_err(" bus id is 0\n");
		i2c_dev->bus_id = 0;
	}

	pr_devel(" i2c1  pdev->id=%s\n", uid);
	if (strcmp(uid, "1") == 0) {
		pr_err(" bus id is 1\n");
		i2c_dev->bus_id = 1;
	}
	/* setup i2c adapter description */
	i2c_dev->adapter.owner = THIS_MODULE;
	i2c_dev->adapter.algo = &i2c_amd_algorithm;
	i2c_dev->adapter.dev.parent = dev;
	i2c_dev->adapter.algo_data = i2c_dev;
	ACPI_COMPANION_SET(&i2c_dev->adapter.dev, ACPI_COMPANION(&pdev->dev));
	i2c_dev->adapter.dev.of_node = dev->of_node;
	snprintf(i2c_dev->adapter.name, sizeof(i2c_dev->adapter.name), "%s-%s",
		 "i2c_dev-i2c", dev_name(pdev->dev.parent));

	i2c_dev->i2c_common.pdev = pci_get_device(PCI_VENDOR_ID_AMD,
						  PCI_DEVICE_ID_AMD_MP2, NULL);

	if (!i2c_dev->i2c_common.pdev) {
		pr_err("%s Could not find pdev in i2c\n", __func__);
		return -EINVAL;
	}
	i2c_dev->buf = kzalloc(32, GFP_KERNEL);

	if (!i2c_dev->buf)
		return -ENOMEM;

	platform_set_drvdata(pdev, i2c_dev);

	i2c_set_adapdata(&i2c_dev->adapter, i2c_dev);

	init_completion(&i2c_dev->msg_complete);
	/* and finally attach to i2c layer */
	ret = i2c_add_adapter(&i2c_dev->adapter);

	if (ret < 0)
		pr_err(" i2c add adpater failed =%d", ret);

	return ret;
}

static int i2c_amd_remove(struct platform_device *pdev)
{
	struct amd_i2c_dev *i2c_dev = platform_get_drvdata(pdev);

	kfree(i2c_dev->buf);
	i2c_del_adapter(&i2c_dev->adapter);

	return 0;
}

static const struct acpi_device_id i2c_amd_acpi_match[] = {
		{ "AMDI0011" },
		{ },
};

static struct platform_driver amd_i2c_plat_driver = {
		.probe = i2c_amd_probe,
		.remove = i2c_amd_remove,
		.driver = {
				.name = "i2c_amd_platdrv",
				.acpi_match_table = ACPI_PTR
						(i2c_amd_acpi_match),
		},
};

module_platform_driver(amd_i2c_plat_driver);

MODULE_AUTHOR("Nehal Shah <nehal-bakulchandra.shah@amd.com>");
MODULE_DESCRIPTION("AMD I2C Platform Driver");
MODULE_LICENSE("Dual BSD/GPL");

