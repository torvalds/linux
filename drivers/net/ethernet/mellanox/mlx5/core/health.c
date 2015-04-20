/*
 * Copyright (c) 2013-2015, Mellanox Technologies. All rights reserved.
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/vmalloc.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/cmd.h>
#include "mlx5_core.h"

enum {
	MLX5_HEALTH_POLL_INTERVAL	= 2 * HZ,
	MAX_MISSES			= 3,
};

enum {
	MLX5_HEALTH_SYNDR_FW_ERR		= 0x1,
	MLX5_HEALTH_SYNDR_IRISC_ERR		= 0x7,
	MLX5_HEALTH_SYNDR_CRC_ERR		= 0x9,
	MLX5_HEALTH_SYNDR_FETCH_PCI_ERR		= 0xa,
	MLX5_HEALTH_SYNDR_HW_FTL_ERR		= 0xb,
	MLX5_HEALTH_SYNDR_ASYNC_EQ_OVERRUN_ERR	= 0xc,
	MLX5_HEALTH_SYNDR_EQ_ERR		= 0xd,
	MLX5_HEALTH_SYNDR_FFSER_ERR		= 0xf,
};

static DEFINE_SPINLOCK(health_lock);
static LIST_HEAD(health_list);
static struct work_struct health_work;

static void health_care(struct work_struct *work)
{
	struct mlx5_core_health *health, *n;
	struct mlx5_core_dev *dev;
	struct mlx5_priv *priv;
	LIST_HEAD(tlist);

	spin_lock_irq(&health_lock);
	list_splice_init(&health_list, &tlist);

	spin_unlock_irq(&health_lock);

	list_for_each_entry_safe(health, n, &tlist, list) {
		priv = container_of(health, struct mlx5_priv, health);
		dev = container_of(priv, struct mlx5_core_dev, priv);
		mlx5_core_warn(dev, "handling bad device here\n");
		/* nothing yet */
		spin_lock_irq(&health_lock);
		list_del_init(&health->list);
		spin_unlock_irq(&health_lock);
	}
}

static const char *hsynd_str(u8 synd)
{
	switch (synd) {
	case MLX5_HEALTH_SYNDR_FW_ERR:
		return "firmware internal error";
	case MLX5_HEALTH_SYNDR_IRISC_ERR:
		return "irisc not responding";
	case MLX5_HEALTH_SYNDR_CRC_ERR:
		return "firmware CRC error";
	case MLX5_HEALTH_SYNDR_FETCH_PCI_ERR:
		return "ICM fetch PCI error";
	case MLX5_HEALTH_SYNDR_HW_FTL_ERR:
		return "HW fatal error\n";
	case MLX5_HEALTH_SYNDR_ASYNC_EQ_OVERRUN_ERR:
		return "async EQ buffer overrun";
	case MLX5_HEALTH_SYNDR_EQ_ERR:
		return "EQ error";
	case MLX5_HEALTH_SYNDR_FFSER_ERR:
		return "FFSER error";
	default:
		return "unrecognized error";
	}
}

static u16 read_be16(__be16 __iomem *p)
{
	return swab16(readl((__force u16 __iomem *) p));
}

static u32 read_be32(__be32 __iomem *p)
{
	return swab32(readl((__force u32 __iomem *) p));
}

static void print_health_info(struct mlx5_core_dev *dev)
{
	struct mlx5_core_health *health = &dev->priv.health;
	struct health_buffer __iomem *h = health->health;
	int i;

	for (i = 0; i < ARRAY_SIZE(h->assert_var); i++)
		pr_info("assert_var[%d] 0x%08x\n", i, read_be32(h->assert_var + i));

	pr_info("assert_exit_ptr 0x%08x\n", read_be32(&h->assert_exit_ptr));
	pr_info("assert_callra 0x%08x\n", read_be32(&h->assert_callra));
	pr_info("fw_ver 0x%08x\n", read_be32(&h->fw_ver));
	pr_info("hw_id 0x%08x\n", read_be32(&h->hw_id));
	pr_info("irisc_index %d\n", readb(&h->irisc_index));
	pr_info("synd 0x%x: %s\n", readb(&h->synd), hsynd_str(readb(&h->synd)));
	pr_info("ext_sync 0x%04x\n", read_be16(&h->ext_sync));
}

static void poll_health(unsigned long data)
{
	struct mlx5_core_dev *dev = (struct mlx5_core_dev *)data;
	struct mlx5_core_health *health = &dev->priv.health;
	unsigned long next;
	u32 count;

	count = ioread32be(health->health_counter);
	if (count == health->prev)
		++health->miss_counter;
	else
		health->miss_counter = 0;

	health->prev = count;
	if (health->miss_counter == MAX_MISSES) {
		mlx5_core_err(dev, "device's health compromised\n");
		print_health_info(dev);
		spin_lock_irq(&health_lock);
		list_add_tail(&health->list, &health_list);
		spin_unlock_irq(&health_lock);

		queue_work(mlx5_core_wq, &health_work);
	} else {
		get_random_bytes(&next, sizeof(next));
		next %= HZ;
		next += jiffies + MLX5_HEALTH_POLL_INTERVAL;
		mod_timer(&health->timer, next);
	}
}

void mlx5_start_health_poll(struct mlx5_core_dev *dev)
{
	struct mlx5_core_health *health = &dev->priv.health;

	INIT_LIST_HEAD(&health->list);
	init_timer(&health->timer);
	health->health = &dev->iseg->health;
	health->health_counter = &dev->iseg->health_counter;

	health->timer.data = (unsigned long)dev;
	health->timer.function = poll_health;
	health->timer.expires = round_jiffies(jiffies + MLX5_HEALTH_POLL_INTERVAL);
	add_timer(&health->timer);
}

void mlx5_stop_health_poll(struct mlx5_core_dev *dev)
{
	struct mlx5_core_health *health = &dev->priv.health;

	del_timer_sync(&health->timer);

	spin_lock_irq(&health_lock);
	if (!list_empty(&health->list))
		list_del_init(&health->list);
	spin_unlock_irq(&health_lock);
}

void mlx5_health_cleanup(void)
{
}

void  __init mlx5_health_init(void)
{
	INIT_WORK(&health_work, health_care);
}
