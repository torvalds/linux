/*
 * Copyright (c) 2013, Mellanox Technologies inc.  All rights reserved.
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


#include <linux/kref.h>
#include <linux/random.h>
#include <linux/debugfs.h>
#include <linux/export.h>
#include <linux/delay.h>
#include <rdma/ib_umem.h>
#include "mlx5_ib.h"

enum {
	MAX_PENDING_REG_MR = 8,
};

enum {
	MLX5_UMR_ALIGN	= 2048
};

static __be64 *mr_align(__be64 *ptr, int align)
{
	unsigned long mask = align - 1;

	return (__be64 *)(((unsigned long)ptr + mask) & ~mask);
}

static int order2idx(struct mlx5_ib_dev *dev, int order)
{
	struct mlx5_mr_cache *cache = &dev->cache;

	if (order < cache->ent[0].order)
		return 0;
	else
		return order - cache->ent[0].order;
}

static void reg_mr_callback(int status, void *context)
{
	struct mlx5_ib_mr *mr = context;
	struct mlx5_ib_dev *dev = mr->dev;
	struct mlx5_mr_cache *cache = &dev->cache;
	int c = order2idx(dev, mr->order);
	struct mlx5_cache_ent *ent = &cache->ent[c];
	u8 key;
	unsigned long flags;
	struct mlx5_mr_table *table = &dev->mdev->priv.mr_table;
	int err;

	spin_lock_irqsave(&ent->lock, flags);
	ent->pending--;
	spin_unlock_irqrestore(&ent->lock, flags);
	if (status) {
		mlx5_ib_warn(dev, "async reg mr failed. status %d\n", status);
		kfree(mr);
		dev->fill_delay = 1;
		mod_timer(&dev->delay_timer, jiffies + HZ);
		return;
	}

	if (mr->out.hdr.status) {
		mlx5_ib_warn(dev, "failed - status %d, syndorme 0x%x\n",
			     mr->out.hdr.status,
			     be32_to_cpu(mr->out.hdr.syndrome));
		kfree(mr);
		dev->fill_delay = 1;
		mod_timer(&dev->delay_timer, jiffies + HZ);
		return;
	}

	spin_lock_irqsave(&dev->mdev->priv.mkey_lock, flags);
	key = dev->mdev->priv.mkey_key++;
	spin_unlock_irqrestore(&dev->mdev->priv.mkey_lock, flags);
	mr->mmr.key = mlx5_idx_to_mkey(be32_to_cpu(mr->out.mkey) & 0xffffff) | key;

	cache->last_add = jiffies;

	spin_lock_irqsave(&ent->lock, flags);
	list_add_tail(&mr->list, &ent->head);
	ent->cur++;
	ent->size++;
	spin_unlock_irqrestore(&ent->lock, flags);

	write_lock_irqsave(&table->lock, flags);
	err = radix_tree_insert(&table->tree, mlx5_base_mkey(mr->mmr.key),
				&mr->mmr);
	if (err)
		pr_err("Error inserting to mr tree. 0x%x\n", -err);
	write_unlock_irqrestore(&table->lock, flags);
}

static int add_keys(struct mlx5_ib_dev *dev, int c, int num)
{
	struct mlx5_mr_cache *cache = &dev->cache;
	struct mlx5_cache_ent *ent = &cache->ent[c];
	struct mlx5_create_mkey_mbox_in *in;
	struct mlx5_ib_mr *mr;
	int npages = 1 << ent->order;
	int err = 0;
	int i;

	in = kzalloc(sizeof(*in), GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	for (i = 0; i < num; i++) {
		if (ent->pending >= MAX_PENDING_REG_MR) {
			err = -EAGAIN;
			break;
		}

		mr = kzalloc(sizeof(*mr), GFP_KERNEL);
		if (!mr) {
			err = -ENOMEM;
			break;
		}
		mr->order = ent->order;
		mr->umred = 1;
		mr->dev = dev;
		in->seg.status = 1 << 6;
		in->seg.xlt_oct_size = cpu_to_be32((npages + 1) / 2);
		in->seg.qpn_mkey7_0 = cpu_to_be32(0xffffff << 8);
		in->seg.flags = MLX5_ACCESS_MODE_MTT | MLX5_PERM_UMR_EN;
		in->seg.log2_page_size = 12;

		spin_lock_irq(&ent->lock);
		ent->pending++;
		spin_unlock_irq(&ent->lock);
		err = mlx5_core_create_mkey(dev->mdev, &mr->mmr, in,
					    sizeof(*in), reg_mr_callback,
					    mr, &mr->out);
		if (err) {
			mlx5_ib_warn(dev, "create mkey failed %d\n", err);
			kfree(mr);
			break;
		}
	}

	kfree(in);
	return err;
}

static void remove_keys(struct mlx5_ib_dev *dev, int c, int num)
{
	struct mlx5_mr_cache *cache = &dev->cache;
	struct mlx5_cache_ent *ent = &cache->ent[c];
	struct mlx5_ib_mr *mr;
	int err;
	int i;

	for (i = 0; i < num; i++) {
		spin_lock_irq(&ent->lock);
		if (list_empty(&ent->head)) {
			spin_unlock_irq(&ent->lock);
			return;
		}
		mr = list_first_entry(&ent->head, struct mlx5_ib_mr, list);
		list_del(&mr->list);
		ent->cur--;
		ent->size--;
		spin_unlock_irq(&ent->lock);
		err = mlx5_core_destroy_mkey(dev->mdev, &mr->mmr);
		if (err)
			mlx5_ib_warn(dev, "failed destroy mkey\n");
		else
			kfree(mr);
	}
}

static ssize_t size_write(struct file *filp, const char __user *buf,
			  size_t count, loff_t *pos)
{
	struct mlx5_cache_ent *ent = filp->private_data;
	struct mlx5_ib_dev *dev = ent->dev;
	char lbuf[20];
	u32 var;
	int err;
	int c;

	if (copy_from_user(lbuf, buf, sizeof(lbuf)))
		return -EFAULT;

	c = order2idx(dev, ent->order);
	lbuf[sizeof(lbuf) - 1] = 0;

	if (sscanf(lbuf, "%u", &var) != 1)
		return -EINVAL;

	if (var < ent->limit)
		return -EINVAL;

	if (var > ent->size) {
		do {
			err = add_keys(dev, c, var - ent->size);
			if (err && err != -EAGAIN)
				return err;

			usleep_range(3000, 5000);
		} while (err);
	} else if (var < ent->size) {
		remove_keys(dev, c, ent->size - var);
	}

	return count;
}

static ssize_t size_read(struct file *filp, char __user *buf, size_t count,
			 loff_t *pos)
{
	struct mlx5_cache_ent *ent = filp->private_data;
	char lbuf[20];
	int err;

	if (*pos)
		return 0;

	err = snprintf(lbuf, sizeof(lbuf), "%d\n", ent->size);
	if (err < 0)
		return err;

	if (copy_to_user(buf, lbuf, err))
		return -EFAULT;

	*pos += err;

	return err;
}

static const struct file_operations size_fops = {
	.owner	= THIS_MODULE,
	.open	= simple_open,
	.write	= size_write,
	.read	= size_read,
};

static ssize_t limit_write(struct file *filp, const char __user *buf,
			   size_t count, loff_t *pos)
{
	struct mlx5_cache_ent *ent = filp->private_data;
	struct mlx5_ib_dev *dev = ent->dev;
	char lbuf[20];
	u32 var;
	int err;
	int c;

	if (copy_from_user(lbuf, buf, sizeof(lbuf)))
		return -EFAULT;

	c = order2idx(dev, ent->order);
	lbuf[sizeof(lbuf) - 1] = 0;

	if (sscanf(lbuf, "%u", &var) != 1)
		return -EINVAL;

	if (var > ent->size)
		return -EINVAL;

	ent->limit = var;

	if (ent->cur < ent->limit) {
		err = add_keys(dev, c, 2 * ent->limit - ent->cur);
		if (err)
			return err;
	}

	return count;
}

static ssize_t limit_read(struct file *filp, char __user *buf, size_t count,
			  loff_t *pos)
{
	struct mlx5_cache_ent *ent = filp->private_data;
	char lbuf[20];
	int err;

	if (*pos)
		return 0;

	err = snprintf(lbuf, sizeof(lbuf), "%d\n", ent->limit);
	if (err < 0)
		return err;

	if (copy_to_user(buf, lbuf, err))
		return -EFAULT;

	*pos += err;

	return err;
}

static const struct file_operations limit_fops = {
	.owner	= THIS_MODULE,
	.open	= simple_open,
	.write	= limit_write,
	.read	= limit_read,
};

static int someone_adding(struct mlx5_mr_cache *cache)
{
	int i;

	for (i = 0; i < MAX_MR_CACHE_ENTRIES; i++) {
		if (cache->ent[i].cur < cache->ent[i].limit)
			return 1;
	}

	return 0;
}

static void __cache_work_func(struct mlx5_cache_ent *ent)
{
	struct mlx5_ib_dev *dev = ent->dev;
	struct mlx5_mr_cache *cache = &dev->cache;
	int i = order2idx(dev, ent->order);
	int err;

	if (cache->stopped)
		return;

	ent = &dev->cache.ent[i];
	if (ent->cur < 2 * ent->limit && !dev->fill_delay) {
		err = add_keys(dev, i, 1);
		if (ent->cur < 2 * ent->limit) {
			if (err == -EAGAIN) {
				mlx5_ib_dbg(dev, "returned eagain, order %d\n",
					    i + 2);
				queue_delayed_work(cache->wq, &ent->dwork,
						   msecs_to_jiffies(3));
			} else if (err) {
				mlx5_ib_warn(dev, "command failed order %d, err %d\n",
					     i + 2, err);
				queue_delayed_work(cache->wq, &ent->dwork,
						   msecs_to_jiffies(1000));
			} else {
				queue_work(cache->wq, &ent->work);
			}
		}
	} else if (ent->cur > 2 * ent->limit) {
		if (!someone_adding(cache) &&
		    time_after(jiffies, cache->last_add + 300 * HZ)) {
			remove_keys(dev, i, 1);
			if (ent->cur > ent->limit)
				queue_work(cache->wq, &ent->work);
		} else {
			queue_delayed_work(cache->wq, &ent->dwork, 300 * HZ);
		}
	}
}

static void delayed_cache_work_func(struct work_struct *work)
{
	struct mlx5_cache_ent *ent;

	ent = container_of(work, struct mlx5_cache_ent, dwork.work);
	__cache_work_func(ent);
}

static void cache_work_func(struct work_struct *work)
{
	struct mlx5_cache_ent *ent;

	ent = container_of(work, struct mlx5_cache_ent, work);
	__cache_work_func(ent);
}

static struct mlx5_ib_mr *alloc_cached_mr(struct mlx5_ib_dev *dev, int order)
{
	struct mlx5_mr_cache *cache = &dev->cache;
	struct mlx5_ib_mr *mr = NULL;
	struct mlx5_cache_ent *ent;
	int c;
	int i;

	c = order2idx(dev, order);
	if (c < 0 || c >= MAX_MR_CACHE_ENTRIES) {
		mlx5_ib_warn(dev, "order %d, cache index %d\n", order, c);
		return NULL;
	}

	for (i = c; i < MAX_MR_CACHE_ENTRIES; i++) {
		ent = &cache->ent[i];

		mlx5_ib_dbg(dev, "order %d, cache index %d\n", ent->order, i);

		spin_lock_irq(&ent->lock);
		if (!list_empty(&ent->head)) {
			mr = list_first_entry(&ent->head, struct mlx5_ib_mr,
					      list);
			list_del(&mr->list);
			ent->cur--;
			spin_unlock_irq(&ent->lock);
			if (ent->cur < ent->limit)
				queue_work(cache->wq, &ent->work);
			break;
		}
		spin_unlock_irq(&ent->lock);

		queue_work(cache->wq, &ent->work);

		if (mr)
			break;
	}

	if (!mr)
		cache->ent[c].miss++;

	return mr;
}

static void free_cached_mr(struct mlx5_ib_dev *dev, struct mlx5_ib_mr *mr)
{
	struct mlx5_mr_cache *cache = &dev->cache;
	struct mlx5_cache_ent *ent;
	int shrink = 0;
	int c;

	c = order2idx(dev, mr->order);
	if (c < 0 || c >= MAX_MR_CACHE_ENTRIES) {
		mlx5_ib_warn(dev, "order %d, cache index %d\n", mr->order, c);
		return;
	}
	ent = &cache->ent[c];
	spin_lock_irq(&ent->lock);
	list_add_tail(&mr->list, &ent->head);
	ent->cur++;
	if (ent->cur > 2 * ent->limit)
		shrink = 1;
	spin_unlock_irq(&ent->lock);

	if (shrink)
		queue_work(cache->wq, &ent->work);
}

static void clean_keys(struct mlx5_ib_dev *dev, int c)
{
	struct mlx5_mr_cache *cache = &dev->cache;
	struct mlx5_cache_ent *ent = &cache->ent[c];
	struct mlx5_ib_mr *mr;
	int err;

	cancel_delayed_work(&ent->dwork);
	while (1) {
		spin_lock_irq(&ent->lock);
		if (list_empty(&ent->head)) {
			spin_unlock_irq(&ent->lock);
			return;
		}
		mr = list_first_entry(&ent->head, struct mlx5_ib_mr, list);
		list_del(&mr->list);
		ent->cur--;
		ent->size--;
		spin_unlock_irq(&ent->lock);
		err = mlx5_core_destroy_mkey(dev->mdev, &mr->mmr);
		if (err)
			mlx5_ib_warn(dev, "failed destroy mkey\n");
		else
			kfree(mr);
	}
}

static int mlx5_mr_cache_debugfs_init(struct mlx5_ib_dev *dev)
{
	struct mlx5_mr_cache *cache = &dev->cache;
	struct mlx5_cache_ent *ent;
	int i;

	if (!mlx5_debugfs_root)
		return 0;

	cache->root = debugfs_create_dir("mr_cache", dev->mdev->priv.dbg_root);
	if (!cache->root)
		return -ENOMEM;

	for (i = 0; i < MAX_MR_CACHE_ENTRIES; i++) {
		ent = &cache->ent[i];
		sprintf(ent->name, "%d", ent->order);
		ent->dir = debugfs_create_dir(ent->name,  cache->root);
		if (!ent->dir)
			return -ENOMEM;

		ent->fsize = debugfs_create_file("size", 0600, ent->dir, ent,
						 &size_fops);
		if (!ent->fsize)
			return -ENOMEM;

		ent->flimit = debugfs_create_file("limit", 0600, ent->dir, ent,
						  &limit_fops);
		if (!ent->flimit)
			return -ENOMEM;

		ent->fcur = debugfs_create_u32("cur", 0400, ent->dir,
					       &ent->cur);
		if (!ent->fcur)
			return -ENOMEM;

		ent->fmiss = debugfs_create_u32("miss", 0600, ent->dir,
						&ent->miss);
		if (!ent->fmiss)
			return -ENOMEM;
	}

	return 0;
}

static void mlx5_mr_cache_debugfs_cleanup(struct mlx5_ib_dev *dev)
{
	if (!mlx5_debugfs_root)
		return;

	debugfs_remove_recursive(dev->cache.root);
}

static void delay_time_func(unsigned long ctx)
{
	struct mlx5_ib_dev *dev = (struct mlx5_ib_dev *)ctx;

	dev->fill_delay = 0;
}

int mlx5_mr_cache_init(struct mlx5_ib_dev *dev)
{
	struct mlx5_mr_cache *cache = &dev->cache;
	struct mlx5_cache_ent *ent;
	int limit;
	int err;
	int i;

	cache->wq = create_singlethread_workqueue("mkey_cache");
	if (!cache->wq) {
		mlx5_ib_warn(dev, "failed to create work queue\n");
		return -ENOMEM;
	}

	setup_timer(&dev->delay_timer, delay_time_func, (unsigned long)dev);
	for (i = 0; i < MAX_MR_CACHE_ENTRIES; i++) {
		INIT_LIST_HEAD(&cache->ent[i].head);
		spin_lock_init(&cache->ent[i].lock);

		ent = &cache->ent[i];
		INIT_LIST_HEAD(&ent->head);
		spin_lock_init(&ent->lock);
		ent->order = i + 2;
		ent->dev = dev;

		if (dev->mdev->profile->mask & MLX5_PROF_MASK_MR_CACHE)
			limit = dev->mdev->profile->mr_cache[i].limit;
		else
			limit = 0;

		INIT_WORK(&ent->work, cache_work_func);
		INIT_DELAYED_WORK(&ent->dwork, delayed_cache_work_func);
		ent->limit = limit;
		queue_work(cache->wq, &ent->work);
	}

	err = mlx5_mr_cache_debugfs_init(dev);
	if (err)
		mlx5_ib_warn(dev, "cache debugfs failure\n");

	return 0;
}

int mlx5_mr_cache_cleanup(struct mlx5_ib_dev *dev)
{
	int i;

	dev->cache.stopped = 1;
	flush_workqueue(dev->cache.wq);

	mlx5_mr_cache_debugfs_cleanup(dev);

	for (i = 0; i < MAX_MR_CACHE_ENTRIES; i++)
		clean_keys(dev, i);

	destroy_workqueue(dev->cache.wq);
	del_timer_sync(&dev->delay_timer);

	return 0;
}

struct ib_mr *mlx5_ib_get_dma_mr(struct ib_pd *pd, int acc)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	struct mlx5_core_dev *mdev = dev->mdev;
	struct mlx5_create_mkey_mbox_in *in;
	struct mlx5_mkey_seg *seg;
	struct mlx5_ib_mr *mr;
	int err;

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	in = kzalloc(sizeof(*in), GFP_KERNEL);
	if (!in) {
		err = -ENOMEM;
		goto err_free;
	}

	seg = &in->seg;
	seg->flags = convert_access(acc) | MLX5_ACCESS_MODE_PA;
	seg->flags_pd = cpu_to_be32(to_mpd(pd)->pdn | MLX5_MKEY_LEN64);
	seg->qpn_mkey7_0 = cpu_to_be32(0xffffff << 8);
	seg->start_addr = 0;

	err = mlx5_core_create_mkey(mdev, &mr->mmr, in, sizeof(*in), NULL, NULL,
				    NULL);
	if (err)
		goto err_in;

	kfree(in);
	mr->ibmr.lkey = mr->mmr.key;
	mr->ibmr.rkey = mr->mmr.key;
	mr->umem = NULL;

	return &mr->ibmr;

err_in:
	kfree(in);

err_free:
	kfree(mr);

	return ERR_PTR(err);
}

static int get_octo_len(u64 addr, u64 len, int page_size)
{
	u64 offset;
	int npages;

	offset = addr & (page_size - 1);
	npages = ALIGN(len + offset, page_size) >> ilog2(page_size);
	return (npages + 1) / 2;
}

static int use_umr(int order)
{
	return order <= 17;
}

static void prep_umr_reg_wqe(struct ib_pd *pd, struct ib_send_wr *wr,
			     struct ib_sge *sg, u64 dma, int n, u32 key,
			     int page_shift, u64 virt_addr, u64 len,
			     int access_flags)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	struct ib_mr *mr = dev->umrc.mr;

	sg->addr = dma;
	sg->length = ALIGN(sizeof(u64) * n, 64);
	sg->lkey = mr->lkey;

	wr->next = NULL;
	wr->send_flags = 0;
	wr->sg_list = sg;
	if (n)
		wr->num_sge = 1;
	else
		wr->num_sge = 0;

	wr->opcode = MLX5_IB_WR_UMR;
	wr->wr.fast_reg.page_list_len = n;
	wr->wr.fast_reg.page_shift = page_shift;
	wr->wr.fast_reg.rkey = key;
	wr->wr.fast_reg.iova_start = virt_addr;
	wr->wr.fast_reg.length = len;
	wr->wr.fast_reg.access_flags = access_flags;
	wr->wr.fast_reg.page_list = (struct ib_fast_reg_page_list *)pd;
}

static void prep_umr_unreg_wqe(struct mlx5_ib_dev *dev,
			       struct ib_send_wr *wr, u32 key)
{
	wr->send_flags = MLX5_IB_SEND_UMR_UNREG;
	wr->opcode = MLX5_IB_WR_UMR;
	wr->wr.fast_reg.rkey = key;
}

void mlx5_umr_cq_handler(struct ib_cq *cq, void *cq_context)
{
	struct mlx5_ib_umr_context *context;
	struct ib_wc wc;
	int err;

	while (1) {
		err = ib_poll_cq(cq, 1, &wc);
		if (err < 0) {
			pr_warn("poll cq error %d\n", err);
			return;
		}
		if (err == 0)
			break;

		context = (struct mlx5_ib_umr_context *) (unsigned long) wc.wr_id;
		context->status = wc.status;
		complete(&context->done);
	}
	ib_req_notify_cq(cq, IB_CQ_NEXT_COMP);
}

static struct mlx5_ib_mr *reg_umr(struct ib_pd *pd, struct ib_umem *umem,
				  u64 virt_addr, u64 len, int npages,
				  int page_shift, int order, int access_flags)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	struct device *ddev = dev->ib_dev.dma_device;
	struct umr_common *umrc = &dev->umrc;
	struct mlx5_ib_umr_context umr_context;
	struct ib_send_wr wr, *bad;
	struct mlx5_ib_mr *mr;
	struct ib_sge sg;
	int size = sizeof(u64) * npages;
	int err = 0;
	int i;

	for (i = 0; i < 1; i++) {
		mr = alloc_cached_mr(dev, order);
		if (mr)
			break;

		err = add_keys(dev, order2idx(dev, order), 1);
		if (err && err != -EAGAIN) {
			mlx5_ib_warn(dev, "add_keys failed, err %d\n", err);
			break;
		}
	}

	if (!mr)
		return ERR_PTR(-EAGAIN);

	mr->pas = kmalloc(size + MLX5_UMR_ALIGN - 1, GFP_KERNEL);
	if (!mr->pas) {
		err = -ENOMEM;
		goto free_mr;
	}

	mlx5_ib_populate_pas(dev, umem, page_shift,
			     mr_align(mr->pas, MLX5_UMR_ALIGN), 1);

	mr->dma = dma_map_single(ddev, mr_align(mr->pas, MLX5_UMR_ALIGN), size,
				 DMA_TO_DEVICE);
	if (dma_mapping_error(ddev, mr->dma)) {
		err = -ENOMEM;
		goto free_pas;
	}

	memset(&wr, 0, sizeof(wr));
	wr.wr_id = (u64)(unsigned long)&umr_context;
	prep_umr_reg_wqe(pd, &wr, &sg, mr->dma, npages, mr->mmr.key, page_shift, virt_addr, len, access_flags);

	mlx5_ib_init_umr_context(&umr_context);
	down(&umrc->sem);
	err = ib_post_send(umrc->qp, &wr, &bad);
	if (err) {
		mlx5_ib_warn(dev, "post send failed, err %d\n", err);
		goto unmap_dma;
	} else {
		wait_for_completion(&umr_context.done);
		if (umr_context.status != IB_WC_SUCCESS) {
			mlx5_ib_warn(dev, "reg umr failed\n");
			err = -EFAULT;
		}
	}

	mr->mmr.iova = virt_addr;
	mr->mmr.size = len;
	mr->mmr.pd = to_mpd(pd)->pdn;

unmap_dma:
	up(&umrc->sem);
	dma_unmap_single(ddev, mr->dma, size, DMA_TO_DEVICE);

free_pas:
	kfree(mr->pas);

free_mr:
	if (err) {
		free_cached_mr(dev, mr);
		return ERR_PTR(err);
	}

	return mr;
}

static struct mlx5_ib_mr *reg_create(struct ib_pd *pd, u64 virt_addr,
				     u64 length, struct ib_umem *umem,
				     int npages, int page_shift,
				     int access_flags)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	struct mlx5_create_mkey_mbox_in *in;
	struct mlx5_ib_mr *mr;
	int inlen;
	int err;

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	inlen = sizeof(*in) + sizeof(*in->pas) * ((npages + 1) / 2) * 2;
	in = mlx5_vzalloc(inlen);
	if (!in) {
		err = -ENOMEM;
		goto err_1;
	}
	mlx5_ib_populate_pas(dev, umem, page_shift, in->pas, 0);

	in->seg.flags = convert_access(access_flags) |
		MLX5_ACCESS_MODE_MTT;
	in->seg.flags_pd = cpu_to_be32(to_mpd(pd)->pdn);
	in->seg.start_addr = cpu_to_be64(virt_addr);
	in->seg.len = cpu_to_be64(length);
	in->seg.bsfs_octo_size = 0;
	in->seg.xlt_oct_size = cpu_to_be32(get_octo_len(virt_addr, length, 1 << page_shift));
	in->seg.log2_page_size = page_shift;
	in->seg.qpn_mkey7_0 = cpu_to_be32(0xffffff << 8);
	in->xlat_oct_act_size = cpu_to_be32(get_octo_len(virt_addr, length,
							 1 << page_shift));
	err = mlx5_core_create_mkey(dev->mdev, &mr->mmr, in, inlen, NULL,
				    NULL, NULL);
	if (err) {
		mlx5_ib_warn(dev, "create mkey failed\n");
		goto err_2;
	}
	mr->umem = umem;
	mlx5_vfree(in);

	mlx5_ib_dbg(dev, "mkey = 0x%x\n", mr->mmr.key);

	return mr;

err_2:
	mlx5_vfree(in);

err_1:
	kfree(mr);

	return ERR_PTR(err);
}

struct ib_mr *mlx5_ib_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
				  u64 virt_addr, int access_flags,
				  struct ib_udata *udata)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	struct mlx5_ib_mr *mr = NULL;
	struct ib_umem *umem;
	int page_shift;
	int npages;
	int ncont;
	int order;
	int err;

	mlx5_ib_dbg(dev, "start 0x%llx, virt_addr 0x%llx, length 0x%llx, access_flags 0x%x\n",
		    start, virt_addr, length, access_flags);
	umem = ib_umem_get(pd->uobject->context, start, length, access_flags,
			   0);
	if (IS_ERR(umem)) {
		mlx5_ib_dbg(dev, "umem get failed (%ld)\n", PTR_ERR(umem));
		return (void *)umem;
	}

	mlx5_ib_cont_pages(umem, start, &npages, &page_shift, &ncont, &order);
	if (!npages) {
		mlx5_ib_warn(dev, "avoid zero region\n");
		err = -EINVAL;
		goto error;
	}

	mlx5_ib_dbg(dev, "npages %d, ncont %d, order %d, page_shift %d\n",
		    npages, ncont, order, page_shift);

	if (use_umr(order)) {
		mr = reg_umr(pd, umem, virt_addr, length, ncont, page_shift,
			     order, access_flags);
		if (PTR_ERR(mr) == -EAGAIN) {
			mlx5_ib_dbg(dev, "cache empty for order %d", order);
			mr = NULL;
		}
	}

	if (!mr)
		mr = reg_create(pd, virt_addr, length, umem, ncont, page_shift,
				access_flags);

	if (IS_ERR(mr)) {
		err = PTR_ERR(mr);
		goto error;
	}

	mlx5_ib_dbg(dev, "mkey 0x%x\n", mr->mmr.key);

	mr->umem = umem;
	mr->npages = npages;
	spin_lock(&dev->mr_lock);
	dev->mdev->priv.reg_pages += npages;
	spin_unlock(&dev->mr_lock);
	mr->ibmr.lkey = mr->mmr.key;
	mr->ibmr.rkey = mr->mmr.key;

	return &mr->ibmr;

error:
	ib_umem_release(umem);
	return ERR_PTR(err);
}

static int unreg_umr(struct mlx5_ib_dev *dev, struct mlx5_ib_mr *mr)
{
	struct umr_common *umrc = &dev->umrc;
	struct mlx5_ib_umr_context umr_context;
	struct ib_send_wr wr, *bad;
	int err;

	memset(&wr, 0, sizeof(wr));
	wr.wr_id = (u64)(unsigned long)&umr_context;
	prep_umr_unreg_wqe(dev, &wr, mr->mmr.key);

	mlx5_ib_init_umr_context(&umr_context);
	down(&umrc->sem);
	err = ib_post_send(umrc->qp, &wr, &bad);
	if (err) {
		up(&umrc->sem);
		mlx5_ib_dbg(dev, "err %d\n", err);
		goto error;
	} else {
		wait_for_completion(&umr_context.done);
		up(&umrc->sem);
	}
	if (umr_context.status != IB_WC_SUCCESS) {
		mlx5_ib_warn(dev, "unreg umr failed\n");
		err = -EFAULT;
		goto error;
	}
	return 0;

error:
	return err;
}

int mlx5_ib_dereg_mr(struct ib_mr *ibmr)
{
	struct mlx5_ib_dev *dev = to_mdev(ibmr->device);
	struct mlx5_ib_mr *mr = to_mmr(ibmr);
	struct ib_umem *umem = mr->umem;
	int npages = mr->npages;
	int umred = mr->umred;
	int err;

	if (!umred) {
		err = mlx5_core_destroy_mkey(dev->mdev, &mr->mmr);
		if (err) {
			mlx5_ib_warn(dev, "failed to destroy mkey 0x%x (%d)\n",
				     mr->mmr.key, err);
			return err;
		}
	} else {
		err = unreg_umr(dev, mr);
		if (err) {
			mlx5_ib_warn(dev, "failed unregister\n");
			return err;
		}
		free_cached_mr(dev, mr);
	}

	if (umem) {
		ib_umem_release(umem);
		spin_lock(&dev->mr_lock);
		dev->mdev->priv.reg_pages -= npages;
		spin_unlock(&dev->mr_lock);
	}

	if (!umred)
		kfree(mr);

	return 0;
}

struct ib_mr *mlx5_ib_create_mr(struct ib_pd *pd,
				struct ib_mr_init_attr *mr_init_attr)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	struct mlx5_create_mkey_mbox_in *in;
	struct mlx5_ib_mr *mr;
	int access_mode, err;
	int ndescs = roundup(mr_init_attr->max_reg_descriptors, 4);

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	in = kzalloc(sizeof(*in), GFP_KERNEL);
	if (!in) {
		err = -ENOMEM;
		goto err_free;
	}

	in->seg.status = 1 << 6; /* free */
	in->seg.xlt_oct_size = cpu_to_be32(ndescs);
	in->seg.qpn_mkey7_0 = cpu_to_be32(0xffffff << 8);
	in->seg.flags_pd = cpu_to_be32(to_mpd(pd)->pdn);
	access_mode = MLX5_ACCESS_MODE_MTT;

	if (mr_init_attr->flags & IB_MR_SIGNATURE_EN) {
		u32 psv_index[2];

		in->seg.flags_pd = cpu_to_be32(be32_to_cpu(in->seg.flags_pd) |
							   MLX5_MKEY_BSF_EN);
		in->seg.bsfs_octo_size = cpu_to_be32(MLX5_MKEY_BSF_OCTO_SIZE);
		mr->sig = kzalloc(sizeof(*mr->sig), GFP_KERNEL);
		if (!mr->sig) {
			err = -ENOMEM;
			goto err_free_in;
		}

		/* create mem & wire PSVs */
		err = mlx5_core_create_psv(dev->mdev, to_mpd(pd)->pdn,
					   2, psv_index);
		if (err)
			goto err_free_sig;

		access_mode = MLX5_ACCESS_MODE_KLM;
		mr->sig->psv_memory.psv_idx = psv_index[0];
		mr->sig->psv_wire.psv_idx = psv_index[1];

		mr->sig->sig_status_checked = true;
		mr->sig->sig_err_exists = false;
		/* Next UMR, Arm SIGERR */
		++mr->sig->sigerr_count;
	}

	in->seg.flags = MLX5_PERM_UMR_EN | access_mode;
	err = mlx5_core_create_mkey(dev->mdev, &mr->mmr, in, sizeof(*in),
				    NULL, NULL, NULL);
	if (err)
		goto err_destroy_psv;

	mr->ibmr.lkey = mr->mmr.key;
	mr->ibmr.rkey = mr->mmr.key;
	mr->umem = NULL;
	kfree(in);

	return &mr->ibmr;

err_destroy_psv:
	if (mr->sig) {
		if (mlx5_core_destroy_psv(dev->mdev,
					  mr->sig->psv_memory.psv_idx))
			mlx5_ib_warn(dev, "failed to destroy mem psv %d\n",
				     mr->sig->psv_memory.psv_idx);
		if (mlx5_core_destroy_psv(dev->mdev,
					  mr->sig->psv_wire.psv_idx))
			mlx5_ib_warn(dev, "failed to destroy wire psv %d\n",
				     mr->sig->psv_wire.psv_idx);
	}
err_free_sig:
	kfree(mr->sig);
err_free_in:
	kfree(in);
err_free:
	kfree(mr);
	return ERR_PTR(err);
}

int mlx5_ib_destroy_mr(struct ib_mr *ibmr)
{
	struct mlx5_ib_dev *dev = to_mdev(ibmr->device);
	struct mlx5_ib_mr *mr = to_mmr(ibmr);
	int err;

	if (mr->sig) {
		if (mlx5_core_destroy_psv(dev->mdev,
					  mr->sig->psv_memory.psv_idx))
			mlx5_ib_warn(dev, "failed to destroy mem psv %d\n",
				     mr->sig->psv_memory.psv_idx);
		if (mlx5_core_destroy_psv(dev->mdev,
					  mr->sig->psv_wire.psv_idx))
			mlx5_ib_warn(dev, "failed to destroy wire psv %d\n",
				     mr->sig->psv_wire.psv_idx);
		kfree(mr->sig);
	}

	err = mlx5_core_destroy_mkey(dev->mdev, &mr->mmr);
	if (err) {
		mlx5_ib_warn(dev, "failed to destroy mkey 0x%x (%d)\n",
			     mr->mmr.key, err);
		return err;
	}

	kfree(mr);

	return err;
}

struct ib_mr *mlx5_ib_alloc_fast_reg_mr(struct ib_pd *pd,
					int max_page_list_len)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	struct mlx5_create_mkey_mbox_in *in;
	struct mlx5_ib_mr *mr;
	int err;

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	in = kzalloc(sizeof(*in), GFP_KERNEL);
	if (!in) {
		err = -ENOMEM;
		goto err_free;
	}

	in->seg.status = 1 << 6; /* free */
	in->seg.xlt_oct_size = cpu_to_be32((max_page_list_len + 1) / 2);
	in->seg.qpn_mkey7_0 = cpu_to_be32(0xffffff << 8);
	in->seg.flags = MLX5_PERM_UMR_EN | MLX5_ACCESS_MODE_MTT;
	in->seg.flags_pd = cpu_to_be32(to_mpd(pd)->pdn);
	/*
	 * TBD not needed - issue 197292 */
	in->seg.log2_page_size = PAGE_SHIFT;

	err = mlx5_core_create_mkey(dev->mdev, &mr->mmr, in, sizeof(*in), NULL,
				    NULL, NULL);
	kfree(in);
	if (err)
		goto err_free;

	mr->ibmr.lkey = mr->mmr.key;
	mr->ibmr.rkey = mr->mmr.key;
	mr->umem = NULL;

	return &mr->ibmr;

err_free:
	kfree(mr);
	return ERR_PTR(err);
}

struct ib_fast_reg_page_list *mlx5_ib_alloc_fast_reg_page_list(struct ib_device *ibdev,
							       int page_list_len)
{
	struct mlx5_ib_fast_reg_page_list *mfrpl;
	int size = page_list_len * sizeof(u64);

	mfrpl = kmalloc(sizeof(*mfrpl), GFP_KERNEL);
	if (!mfrpl)
		return ERR_PTR(-ENOMEM);

	mfrpl->ibfrpl.page_list = kmalloc(size, GFP_KERNEL);
	if (!mfrpl->ibfrpl.page_list)
		goto err_free;

	mfrpl->mapped_page_list = dma_alloc_coherent(ibdev->dma_device,
						     size, &mfrpl->map,
						     GFP_KERNEL);
	if (!mfrpl->mapped_page_list)
		goto err_free;

	WARN_ON(mfrpl->map & 0x3f);

	return &mfrpl->ibfrpl;

err_free:
	kfree(mfrpl->ibfrpl.page_list);
	kfree(mfrpl);
	return ERR_PTR(-ENOMEM);
}

void mlx5_ib_free_fast_reg_page_list(struct ib_fast_reg_page_list *page_list)
{
	struct mlx5_ib_fast_reg_page_list *mfrpl = to_mfrpl(page_list);
	struct mlx5_ib_dev *dev = to_mdev(page_list->device);
	int size = page_list->max_page_list_len * sizeof(u64);

	dma_free_coherent(&dev->mdev->pdev->dev, size, mfrpl->mapped_page_list,
			  mfrpl->map);
	kfree(mfrpl->ibfrpl.page_list);
	kfree(mfrpl);
}

int mlx5_ib_check_mr_status(struct ib_mr *ibmr, u32 check_mask,
			    struct ib_mr_status *mr_status)
{
	struct mlx5_ib_mr *mmr = to_mmr(ibmr);
	int ret = 0;

	if (check_mask & ~IB_MR_CHECK_SIG_STATUS) {
		pr_err("Invalid status check mask\n");
		ret = -EINVAL;
		goto done;
	}

	mr_status->fail_status = 0;
	if (check_mask & IB_MR_CHECK_SIG_STATUS) {
		if (!mmr->sig) {
			ret = -EINVAL;
			pr_err("signature status check requested on a non-signature enabled MR\n");
			goto done;
		}

		mmr->sig->sig_status_checked = true;
		if (!mmr->sig->sig_err_exists)
			goto done;

		if (ibmr->lkey == mmr->sig->err_item.key)
			memcpy(&mr_status->sig_err, &mmr->sig->err_item,
			       sizeof(mr_status->sig_err));
		else {
			mr_status->sig_err.err_type = IB_SIG_BAD_GUARD;
			mr_status->sig_err.sig_err_offset = 0;
			mr_status->sig_err.key = mmr->sig->err_item.key;
		}

		mmr->sig->sig_err_exists = false;
		mr_status->fail_status |= IB_MR_CHECK_SIG_STATUS;
	}

done:
	return ret;
}
