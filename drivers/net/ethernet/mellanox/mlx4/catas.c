/*
 * Copyright (c) 2007 Cisco Systems, Inc. All rights reserved.
 * Copyright (c) 2007, 2008 Mellanox Technologies. All rights reserved.
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

#include <linux/workqueue.h>
#include <linux/module.h>

#include "mlx4.h"

enum {
	MLX4_CATAS_POLL_INTERVAL	= 5 * HZ,
};



static int internal_err_reset = 1;
module_param(internal_err_reset, int, 0644);
MODULE_PARM_DESC(internal_err_reset,
		 "Reset device on internal errors if non-zero"
		 " (default 1, in SRIOV mode default is 0)");

static void dump_err_buf(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	int i;

	mlx4_err(dev, "Internal error detected:\n");
	for (i = 0; i < priv->fw.catas_size; ++i)
		mlx4_err(dev, "  buf[%02x]: %08x\n",
			 i, swab32(readl(priv->catas_err.map + i)));
}

static void poll_catas(unsigned long dev_ptr)
{
	struct mlx4_dev *dev = (struct mlx4_dev *) dev_ptr;
	struct mlx4_priv *priv = mlx4_priv(dev);

	if (readl(priv->catas_err.map)) {
		/* If the device is off-line, we cannot try to recover it */
		if (pci_channel_offline(dev->persist->pdev))
			mod_timer(&priv->catas_err.timer,
				  round_jiffies(jiffies + MLX4_CATAS_POLL_INTERVAL));
		else {
			dump_err_buf(dev);
			mlx4_dispatch_event(dev, MLX4_DEV_EVENT_CATASTROPHIC_ERROR, 0);

			if (internal_err_reset)
				queue_work(dev->persist->catas_wq,
					   &dev->persist->catas_work);
		}
	} else
		mod_timer(&priv->catas_err.timer,
			  round_jiffies(jiffies + MLX4_CATAS_POLL_INTERVAL));
}

static void catas_reset(struct work_struct *work)
{
	struct mlx4_dev_persistent *persist =
		container_of(work, struct mlx4_dev_persistent,
			     catas_work);
	struct pci_dev *pdev = persist->pdev;
	int ret;

	/* If the device is off-line, we cannot reset it */
	if (pci_channel_offline(pdev))
		return;

	ret = mlx4_restart_one(pdev);
	/* 'priv' now is not valid */
	if (ret)
		pr_err("mlx4 %s: Reset failed (%d)\n",
		       pci_name(pdev), ret);
	else
		mlx4_dbg(persist->dev, "Reset succeeded\n");
}

void mlx4_start_catas_poll(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	phys_addr_t addr;

	/*If we are in SRIOV the default of the module param must be 0*/
	if (mlx4_is_mfunc(dev))
		internal_err_reset = 0;

	INIT_LIST_HEAD(&priv->catas_err.list);
	init_timer(&priv->catas_err.timer);
	priv->catas_err.map = NULL;

	addr = pci_resource_start(dev->persist->pdev, priv->fw.catas_bar) +
		priv->fw.catas_offset;

	priv->catas_err.map = ioremap(addr, priv->fw.catas_size * 4);
	if (!priv->catas_err.map) {
		mlx4_warn(dev, "Failed to map internal error buffer at 0x%llx\n",
			  (unsigned long long) addr);
		return;
	}

	priv->catas_err.timer.data     = (unsigned long) dev;
	priv->catas_err.timer.function = poll_catas;
	priv->catas_err.timer.expires  =
		round_jiffies(jiffies + MLX4_CATAS_POLL_INTERVAL);
	add_timer(&priv->catas_err.timer);
}

void mlx4_stop_catas_poll(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	del_timer_sync(&priv->catas_err.timer);

	if (priv->catas_err.map) {
		iounmap(priv->catas_err.map);
		priv->catas_err.map = NULL;
	}
}

int  mlx4_catas_init(struct mlx4_dev *dev)
{
	INIT_WORK(&dev->persist->catas_work, catas_reset);
	dev->persist->catas_wq = create_singlethread_workqueue("mlx4_health");
	if (!dev->persist->catas_wq)
		return -ENOMEM;

	return 0;
}

void mlx4_catas_end(struct mlx4_dev *dev)
{
	if (dev->persist->catas_wq) {
		destroy_workqueue(dev->persist->catas_wq);
		dev->persist->catas_wq = NULL;
	}
}
