/*
 * Copyright (c) 2006, 2007 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
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

#include <linux/init.h>
#include <linux/errno.h>

#include <asm/page.h>

#include "mlx4.h"
#include "icm.h"

int mlx4_pd_alloc(struct mlx4_dev *dev, u32 *pdn)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	*pdn = mlx4_bitmap_alloc(&priv->pd_bitmap);
	if (*pdn == -1)
		return -ENOMEM;

	return 0;
}
EXPORT_SYMBOL_GPL(mlx4_pd_alloc);

void mlx4_pd_free(struct mlx4_dev *dev, u32 pdn)
{
	mlx4_bitmap_free(&mlx4_priv(dev)->pd_bitmap, pdn);
}
EXPORT_SYMBOL_GPL(mlx4_pd_free);

int mlx4_init_pd_table(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	return mlx4_bitmap_init(&priv->pd_bitmap, dev->caps.num_pds,
				(1 << 24) - 1, dev->caps.reserved_pds, 0);
}

void mlx4_cleanup_pd_table(struct mlx4_dev *dev)
{
	mlx4_bitmap_cleanup(&mlx4_priv(dev)->pd_bitmap);
}


int mlx4_uar_alloc(struct mlx4_dev *dev, struct mlx4_uar *uar)
{
	uar->index = mlx4_bitmap_alloc(&mlx4_priv(dev)->uar_table.bitmap);
	if (uar->index == -1)
		return -ENOMEM;

	uar->pfn = (pci_resource_start(dev->pdev, 2) >> PAGE_SHIFT) + uar->index;

	return 0;
}
EXPORT_SYMBOL_GPL(mlx4_uar_alloc);

void mlx4_uar_free(struct mlx4_dev *dev, struct mlx4_uar *uar)
{
	mlx4_bitmap_free(&mlx4_priv(dev)->uar_table.bitmap, uar->index);
}
EXPORT_SYMBOL_GPL(mlx4_uar_free);

int mlx4_init_uar_table(struct mlx4_dev *dev)
{
	if (dev->caps.num_uars <= 128) {
		mlx4_err(dev, "Only %d UAR pages (need more than 128)\n",
			 dev->caps.num_uars);
		mlx4_err(dev, "Increase firmware log2_uar_bar_megabytes?\n");
		return -ENODEV;
	}

	return mlx4_bitmap_init(&mlx4_priv(dev)->uar_table.bitmap,
				dev->caps.num_uars, dev->caps.num_uars - 1,
				max(128, dev->caps.reserved_uars), 0);
}

void mlx4_cleanup_uar_table(struct mlx4_dev *dev)
{
	mlx4_bitmap_cleanup(&mlx4_priv(dev)->uar_table.bitmap);
}
