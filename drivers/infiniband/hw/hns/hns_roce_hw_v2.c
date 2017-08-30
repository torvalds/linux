/*
 * Copyright (c) 2016-2017 Hisilicon Limited.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/acpi.h>
#include <linux/etherdevice.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <rdma/ib_umem.h>

#include "hnae3.h"
#include "hns_roce_common.h"
#include "hns_roce_device.h"
#include "hns_roce_cmd.h"
#include "hns_roce_hem.h"

static const struct hns_roce_hw hns_roce_hw_v2;

static const struct pci_device_id hns_roce_hw_v2_pci_tbl[] = {
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_25GE_RDMA), 0},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_25GE_RDMA_MACSEC), 0},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_100G_RDMA_MACSEC), 0},
	/* required last entry */
	{0, }
};

static int hns_roce_hw_v2_get_cfg(struct hns_roce_dev *hr_dev,
				  struct hnae3_handle *handle)
{
	const struct pci_device_id *id;

	id = pci_match_id(hns_roce_hw_v2_pci_tbl, hr_dev->pci_dev);
	if (!id) {
		dev_err(hr_dev->dev, "device is not compatible!\n");
		return -ENXIO;
	}

	hr_dev->hw = &hns_roce_hw_v2;

	/* Get info from NIC driver. */
	hr_dev->reg_base = handle->rinfo.roce_io_base;
	hr_dev->caps.num_ports = 1;
	hr_dev->iboe.netdevs[0] = handle->rinfo.netdev;
	hr_dev->iboe.phy_port[0] = 0;

	/* cmd issue mode: 0 is poll, 1 is event */
	hr_dev->cmd_mod = 0;
	hr_dev->loop_idc = 0;

	return 0;
}

static int hns_roce_hw_v2_init_instance(struct hnae3_handle *handle)
{
	struct hns_roce_dev *hr_dev;
	int ret;

	hr_dev = (struct hns_roce_dev *)ib_alloc_device(sizeof(*hr_dev));
	if (!hr_dev)
		return -ENOMEM;

	hr_dev->pci_dev = handle->pdev;
	hr_dev->dev = &handle->pdev->dev;
	handle->priv = hr_dev;

	ret = hns_roce_hw_v2_get_cfg(hr_dev, handle);
	if (ret) {
		dev_err(hr_dev->dev, "Get Configuration failed!\n");
		goto error_failed_get_cfg;
	}

	ret = hns_roce_init(hr_dev);
	if (ret) {
		dev_err(hr_dev->dev, "RoCE Engine init failed!\n");
		goto error_failed_get_cfg;
	}

	return 0;

error_failed_get_cfg:
	ib_dealloc_device(&hr_dev->ib_dev);

	return ret;
}

static void hns_roce_hw_v2_uninit_instance(struct hnae3_handle *handle,
					   bool reset)
{
	struct hns_roce_dev *hr_dev = (struct hns_roce_dev *)handle->priv;

	hns_roce_exit(hr_dev);
	ib_dealloc_device(&hr_dev->ib_dev);
}

static const struct hnae3_client_ops hns_roce_hw_v2_ops = {
	.init_instance = hns_roce_hw_v2_init_instance,
	.uninit_instance = hns_roce_hw_v2_uninit_instance,
};

static struct hnae3_client hns_roce_hw_v2_client = {
	.name = "hns_roce_hw_v2",
	.type = HNAE3_CLIENT_ROCE,
	.ops = &hns_roce_hw_v2_ops,
};

static int __init hns_roce_hw_v2_init(void)
{
	return hnae3_register_client(&hns_roce_hw_v2_client);
}

static void __exit hns_roce_hw_v2_exit(void)
{
	hnae3_unregister_client(&hns_roce_hw_v2_client);
}

module_init(hns_roce_hw_v2_init);
module_exit(hns_roce_hw_v2_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Wei Hu <xavier.huwei@huawei.com>");
MODULE_AUTHOR("Lijun Ou <oulijun@huawei.com>");
MODULE_AUTHOR("Shaobo Xu <xushaobo2@huawei.com>");
MODULE_DESCRIPTION("Hisilicon Hip08 Family RoCE Driver");
