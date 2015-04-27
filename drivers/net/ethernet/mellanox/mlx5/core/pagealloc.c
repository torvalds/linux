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

#include <asm-generic/kmap_types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/cmd.h>
#include "mlx5_core.h"

enum {
	MLX5_PAGES_CANT_GIVE	= 0,
	MLX5_PAGES_GIVE		= 1,
	MLX5_PAGES_TAKE		= 2
};

enum {
	MLX5_BOOT_PAGES		= 1,
	MLX5_INIT_PAGES		= 2,
	MLX5_POST_INIT_PAGES	= 3
};

struct mlx5_pages_req {
	struct mlx5_core_dev *dev;
	u16	func_id;
	s32	npages;
	struct work_struct work;
};

struct fw_page {
	struct rb_node		rb_node;
	u64			addr;
	struct page	       *page;
	u16			func_id;
	unsigned long		bitmask;
	struct list_head	list;
	unsigned		free_count;
};

struct mlx5_query_pages_inbox {
	struct mlx5_inbox_hdr	hdr;
	u8			rsvd[8];
};

struct mlx5_query_pages_outbox {
	struct mlx5_outbox_hdr	hdr;
	__be16			rsvd;
	__be16			func_id;
	__be32			num_pages;
};

struct mlx5_manage_pages_inbox {
	struct mlx5_inbox_hdr	hdr;
	__be16			rsvd;
	__be16			func_id;
	__be32			num_entries;
	__be64			pas[0];
};

struct mlx5_manage_pages_outbox {
	struct mlx5_outbox_hdr	hdr;
	__be32			num_entries;
	u8			rsvd[4];
	__be64			pas[0];
};

enum {
	MAX_RECLAIM_TIME_MSECS	= 5000,
};

enum {
	MLX5_MAX_RECLAIM_TIME_MILI	= 5000,
	MLX5_NUM_4K_IN_PAGE		= PAGE_SIZE / MLX5_ADAPTER_PAGE_SIZE,
};

static int insert_page(struct mlx5_core_dev *dev, u64 addr, struct page *page, u16 func_id)
{
	struct rb_root *root = &dev->priv.page_root;
	struct rb_node **new = &root->rb_node;
	struct rb_node *parent = NULL;
	struct fw_page *nfp;
	struct fw_page *tfp;
	int i;

	while (*new) {
		parent = *new;
		tfp = rb_entry(parent, struct fw_page, rb_node);
		if (tfp->addr < addr)
			new = &parent->rb_left;
		else if (tfp->addr > addr)
			new = &parent->rb_right;
		else
			return -EEXIST;
	}

	nfp = kzalloc(sizeof(*nfp), GFP_KERNEL);
	if (!nfp)
		return -ENOMEM;

	nfp->addr = addr;
	nfp->page = page;
	nfp->func_id = func_id;
	nfp->free_count = MLX5_NUM_4K_IN_PAGE;
	for (i = 0; i < MLX5_NUM_4K_IN_PAGE; i++)
		set_bit(i, &nfp->bitmask);

	rb_link_node(&nfp->rb_node, parent, new);
	rb_insert_color(&nfp->rb_node, root);
	list_add(&nfp->list, &dev->priv.free_list);

	return 0;
}

static struct fw_page *find_fw_page(struct mlx5_core_dev *dev, u64 addr)
{
	struct rb_root *root = &dev->priv.page_root;
	struct rb_node *tmp = root->rb_node;
	struct fw_page *result = NULL;
	struct fw_page *tfp;

	while (tmp) {
		tfp = rb_entry(tmp, struct fw_page, rb_node);
		if (tfp->addr < addr) {
			tmp = tmp->rb_left;
		} else if (tfp->addr > addr) {
			tmp = tmp->rb_right;
		} else {
			result = tfp;
			break;
		}
	}

	return result;
}

static int mlx5_cmd_query_pages(struct mlx5_core_dev *dev, u16 *func_id,
				s32 *npages, int boot)
{
	struct mlx5_query_pages_inbox	in;
	struct mlx5_query_pages_outbox	out;
	int err;

	memset(&in, 0, sizeof(in));
	memset(&out, 0, sizeof(out));
	in.hdr.opcode = cpu_to_be16(MLX5_CMD_OP_QUERY_PAGES);
	in.hdr.opmod = boot ? cpu_to_be16(MLX5_BOOT_PAGES) : cpu_to_be16(MLX5_INIT_PAGES);

	err = mlx5_cmd_exec(dev, &in, sizeof(in), &out, sizeof(out));
	if (err)
		return err;

	if (out.hdr.status)
		return mlx5_cmd_status_to_err(&out.hdr);

	*npages = be32_to_cpu(out.num_pages);
	*func_id = be16_to_cpu(out.func_id);

	return err;
}

static int alloc_4k(struct mlx5_core_dev *dev, u64 *addr)
{
	struct fw_page *fp;
	unsigned n;

	if (list_empty(&dev->priv.free_list))
		return -ENOMEM;

	fp = list_entry(dev->priv.free_list.next, struct fw_page, list);
	n = find_first_bit(&fp->bitmask, 8 * sizeof(fp->bitmask));
	if (n >= MLX5_NUM_4K_IN_PAGE) {
		mlx5_core_warn(dev, "alloc 4k bug\n");
		return -ENOENT;
	}
	clear_bit(n, &fp->bitmask);
	fp->free_count--;
	if (!fp->free_count)
		list_del(&fp->list);

	*addr = fp->addr + n * MLX5_ADAPTER_PAGE_SIZE;

	return 0;
}

#define MLX5_U64_4K_PAGE_MASK ((~(u64)0U) << PAGE_SHIFT)

static void free_4k(struct mlx5_core_dev *dev, u64 addr)
{
	struct fw_page *fwp;
	int n;

	fwp = find_fw_page(dev, addr & MLX5_U64_4K_PAGE_MASK);
	if (!fwp) {
		mlx5_core_warn(dev, "page not found\n");
		return;
	}

	n = (addr & ~MLX5_U64_4K_PAGE_MASK) >> MLX5_ADAPTER_PAGE_SHIFT;
	fwp->free_count++;
	set_bit(n, &fwp->bitmask);
	if (fwp->free_count == MLX5_NUM_4K_IN_PAGE) {
		rb_erase(&fwp->rb_node, &dev->priv.page_root);
		if (fwp->free_count != 1)
			list_del(&fwp->list);
		dma_unmap_page(&dev->pdev->dev, addr & MLX5_U64_4K_PAGE_MASK,
			       PAGE_SIZE, DMA_BIDIRECTIONAL);
		__free_page(fwp->page);
		kfree(fwp);
	} else if (fwp->free_count == 1) {
		list_add(&fwp->list, &dev->priv.free_list);
	}
}

static int alloc_system_page(struct mlx5_core_dev *dev, u16 func_id)
{
	struct page *page;
	u64 addr;
	int err;
	int nid = dev_to_node(&dev->pdev->dev);

	page = alloc_pages_node(nid, GFP_HIGHUSER, 0);
	if (!page) {
		mlx5_core_warn(dev, "failed to allocate page\n");
		return -ENOMEM;
	}
	addr = dma_map_page(&dev->pdev->dev, page, 0,
			    PAGE_SIZE, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(&dev->pdev->dev, addr)) {
		mlx5_core_warn(dev, "failed dma mapping page\n");
		err = -ENOMEM;
		goto out_alloc;
	}
	err = insert_page(dev, addr, page, func_id);
	if (err) {
		mlx5_core_err(dev, "failed to track allocated page\n");
		goto out_mapping;
	}

	return 0;

out_mapping:
	dma_unmap_page(&dev->pdev->dev, addr, PAGE_SIZE, DMA_BIDIRECTIONAL);

out_alloc:
	__free_page(page);

	return err;
}
static int give_pages(struct mlx5_core_dev *dev, u16 func_id, int npages,
		      int notify_fail)
{
	struct mlx5_manage_pages_inbox *in;
	struct mlx5_manage_pages_outbox out;
	struct mlx5_manage_pages_inbox *nin;
	int inlen;
	u64 addr;
	int err;
	int i;

	inlen = sizeof(*in) + npages * sizeof(in->pas[0]);
	in = mlx5_vzalloc(inlen);
	if (!in) {
		mlx5_core_warn(dev, "vzalloc failed %d\n", inlen);
		return -ENOMEM;
	}
	memset(&out, 0, sizeof(out));

	for (i = 0; i < npages; i++) {
retry:
		err = alloc_4k(dev, &addr);
		if (err) {
			if (err == -ENOMEM)
				err = alloc_system_page(dev, func_id);
			if (err)
				goto out_4k;

			goto retry;
		}
		in->pas[i] = cpu_to_be64(addr);
	}

	in->hdr.opcode = cpu_to_be16(MLX5_CMD_OP_MANAGE_PAGES);
	in->hdr.opmod = cpu_to_be16(MLX5_PAGES_GIVE);
	in->func_id = cpu_to_be16(func_id);
	in->num_entries = cpu_to_be32(npages);
	err = mlx5_cmd_exec(dev, in, inlen, &out, sizeof(out));
	if (err) {
		mlx5_core_warn(dev, "func_id 0x%x, npages %d, err %d\n",
			       func_id, npages, err);
		goto out_alloc;
	}
	dev->priv.fw_pages += npages;

	if (out.hdr.status) {
		err = mlx5_cmd_status_to_err(&out.hdr);
		if (err) {
			mlx5_core_warn(dev, "func_id 0x%x, npages %d, status %d\n",
				       func_id, npages, out.hdr.status);
			goto out_alloc;
		}
	}

	mlx5_core_dbg(dev, "err %d\n", err);

	goto out_free;

out_alloc:
	if (notify_fail) {
		nin = kzalloc(sizeof(*nin), GFP_KERNEL);
		if (!nin) {
			mlx5_core_warn(dev, "allocation failed\n");
			goto out_4k;
		}
		memset(&out, 0, sizeof(out));
		nin->hdr.opcode = cpu_to_be16(MLX5_CMD_OP_MANAGE_PAGES);
		nin->hdr.opmod = cpu_to_be16(MLX5_PAGES_CANT_GIVE);
		if (mlx5_cmd_exec(dev, nin, sizeof(*nin), &out, sizeof(out)))
			mlx5_core_warn(dev, "page notify failed\n");
		kfree(nin);
	}

out_4k:
	for (i--; i >= 0; i--)
		free_4k(dev, be64_to_cpu(in->pas[i]));
out_free:
	kvfree(in);
	return err;
}

static int reclaim_pages(struct mlx5_core_dev *dev, u32 func_id, int npages,
			 int *nclaimed)
{
	struct mlx5_manage_pages_inbox   in;
	struct mlx5_manage_pages_outbox *out;
	int num_claimed;
	int outlen;
	u64 addr;
	int err;
	int i;

	if (nclaimed)
		*nclaimed = 0;

	memset(&in, 0, sizeof(in));
	outlen = sizeof(*out) + npages * sizeof(out->pas[0]);
	out = mlx5_vzalloc(outlen);
	if (!out)
		return -ENOMEM;

	in.hdr.opcode = cpu_to_be16(MLX5_CMD_OP_MANAGE_PAGES);
	in.hdr.opmod = cpu_to_be16(MLX5_PAGES_TAKE);
	in.func_id = cpu_to_be16(func_id);
	in.num_entries = cpu_to_be32(npages);
	mlx5_core_dbg(dev, "npages %d, outlen %d\n", npages, outlen);
	err = mlx5_cmd_exec(dev, &in, sizeof(in), out, outlen);
	if (err) {
		mlx5_core_err(dev, "failed reclaiming pages\n");
		goto out_free;
	}
	dev->priv.fw_pages -= npages;

	if (out->hdr.status) {
		err = mlx5_cmd_status_to_err(&out->hdr);
		goto out_free;
	}

	num_claimed = be32_to_cpu(out->num_entries);
	if (nclaimed)
		*nclaimed = num_claimed;

	for (i = 0; i < num_claimed; i++) {
		addr = be64_to_cpu(out->pas[i]);
		free_4k(dev, addr);
	}

out_free:
	kvfree(out);
	return err;
}

static void pages_work_handler(struct work_struct *work)
{
	struct mlx5_pages_req *req = container_of(work, struct mlx5_pages_req, work);
	struct mlx5_core_dev *dev = req->dev;
	int err = 0;

	if (req->npages < 0)
		err = reclaim_pages(dev, req->func_id, -1 * req->npages, NULL);
	else if (req->npages > 0)
		err = give_pages(dev, req->func_id, req->npages, 1);

	if (err)
		mlx5_core_warn(dev, "%s fail %d\n",
			       req->npages < 0 ? "reclaim" : "give", err);

	kfree(req);
}

void mlx5_core_req_pages_handler(struct mlx5_core_dev *dev, u16 func_id,
				 s32 npages)
{
	struct mlx5_pages_req *req;

	req = kzalloc(sizeof(*req), GFP_ATOMIC);
	if (!req) {
		mlx5_core_warn(dev, "failed to allocate pages request\n");
		return;
	}

	req->dev = dev;
	req->func_id = func_id;
	req->npages = npages;
	INIT_WORK(&req->work, pages_work_handler);
	queue_work(dev->priv.pg_wq, &req->work);
}

int mlx5_satisfy_startup_pages(struct mlx5_core_dev *dev, int boot)
{
	u16 uninitialized_var(func_id);
	s32 uninitialized_var(npages);
	int err;

	err = mlx5_cmd_query_pages(dev, &func_id, &npages, boot);
	if (err)
		return err;

	mlx5_core_dbg(dev, "requested %d %s pages for func_id 0x%x\n",
		      npages, boot ? "boot" : "init", func_id);

	return give_pages(dev, func_id, npages, 0);
}

enum {
	MLX5_BLKS_FOR_RECLAIM_PAGES = 12
};

static int optimal_reclaimed_pages(void)
{
	struct mlx5_cmd_prot_block *block;
	struct mlx5_cmd_layout *lay;
	int ret;

	ret = (sizeof(lay->out) + MLX5_BLKS_FOR_RECLAIM_PAGES * sizeof(block->data) -
	       sizeof(struct mlx5_manage_pages_outbox)) /
	       FIELD_SIZEOF(struct mlx5_manage_pages_outbox, pas[0]);

	return ret;
}

int mlx5_reclaim_startup_pages(struct mlx5_core_dev *dev)
{
	unsigned long end = jiffies + msecs_to_jiffies(MAX_RECLAIM_TIME_MSECS);
	struct fw_page *fwp;
	struct rb_node *p;
	int nclaimed = 0;
	int err;

	do {
		p = rb_first(&dev->priv.page_root);
		if (p) {
			fwp = rb_entry(p, struct fw_page, rb_node);
			err = reclaim_pages(dev, fwp->func_id,
					    optimal_reclaimed_pages(),
					    &nclaimed);
			if (err) {
				mlx5_core_warn(dev, "failed reclaiming pages (%d)\n",
					       err);
				return err;
			}
			if (nclaimed)
				end = jiffies + msecs_to_jiffies(MAX_RECLAIM_TIME_MSECS);
		}
		if (time_after(jiffies, end)) {
			mlx5_core_warn(dev, "FW did not return all pages. giving up...\n");
			break;
		}
	} while (p);

	return 0;
}

void mlx5_pagealloc_init(struct mlx5_core_dev *dev)
{
	dev->priv.page_root = RB_ROOT;
	INIT_LIST_HEAD(&dev->priv.free_list);
}

void mlx5_pagealloc_cleanup(struct mlx5_core_dev *dev)
{
	/* nothing */
}

int mlx5_pagealloc_start(struct mlx5_core_dev *dev)
{
	dev->priv.pg_wq = create_singlethread_workqueue("mlx5_page_allocator");
	if (!dev->priv.pg_wq)
		return -ENOMEM;

	return 0;
}

void mlx5_pagealloc_stop(struct mlx5_core_dev *dev)
{
	destroy_workqueue(dev->priv.pg_wq);
}
