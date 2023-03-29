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

#include <linux/highmem.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/mlx5/driver.h>
#include <linux/xarray.h>
#include "mlx5_core.h"
#include "lib/eq.h"
#include "lib/tout.h"

enum {
	MLX5_PAGES_CANT_GIVE	= 0,
	MLX5_PAGES_GIVE		= 1,
	MLX5_PAGES_TAKE		= 2
};

struct mlx5_pages_req {
	struct mlx5_core_dev *dev;
	u16	func_id;
	u8	ec_function;
	s32	npages;
	struct work_struct work;
	u8	release_all;
};

struct fw_page {
	struct rb_node		rb_node;
	u64			addr;
	struct page	       *page;
	u32			function;
	unsigned long		bitmask;
	struct list_head	list;
	unsigned int free_count;
};

enum {
	MLX5_MAX_RECLAIM_TIME_MILI	= 5000,
	MLX5_NUM_4K_IN_PAGE		= PAGE_SIZE / MLX5_ADAPTER_PAGE_SIZE,
};

static u32 get_function(u16 func_id, bool ec_function)
{
	return (u32)func_id | (ec_function << 16);
}

static u16 func_id_to_type(struct mlx5_core_dev *dev, u16 func_id, bool ec_function)
{
	if (!func_id)
		return mlx5_core_is_ecpf(dev) && !ec_function ? MLX5_HOST_PF : MLX5_PF;

	return func_id <= mlx5_core_max_vfs(dev) ?  MLX5_VF : MLX5_SF;
}

static struct rb_root *page_root_per_function(struct mlx5_core_dev *dev, u32 function)
{
	struct rb_root *root;
	int err;

	root = xa_load(&dev->priv.page_root_xa, function);
	if (root)
		return root;

	root = kzalloc(sizeof(*root), GFP_KERNEL);
	if (!root)
		return ERR_PTR(-ENOMEM);

	err = xa_insert(&dev->priv.page_root_xa, function, root, GFP_KERNEL);
	if (err) {
		kfree(root);
		return ERR_PTR(err);
	}

	*root = RB_ROOT;

	return root;
}

static int insert_page(struct mlx5_core_dev *dev, u64 addr, struct page *page, u32 function)
{
	struct rb_node *parent = NULL;
	struct rb_root *root;
	struct rb_node **new;
	struct fw_page *nfp;
	struct fw_page *tfp;
	int i;

	root = page_root_per_function(dev, function);
	if (IS_ERR(root))
		return PTR_ERR(root);

	new = &root->rb_node;

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
	nfp->function = function;
	nfp->free_count = MLX5_NUM_4K_IN_PAGE;
	for (i = 0; i < MLX5_NUM_4K_IN_PAGE; i++)
		set_bit(i, &nfp->bitmask);

	rb_link_node(&nfp->rb_node, parent, new);
	rb_insert_color(&nfp->rb_node, root);
	list_add(&nfp->list, &dev->priv.free_list);

	return 0;
}

static struct fw_page *find_fw_page(struct mlx5_core_dev *dev, u64 addr,
				    u32 function)
{
	struct fw_page *result = NULL;
	struct rb_root *root;
	struct rb_node *tmp;
	struct fw_page *tfp;

	root = xa_load(&dev->priv.page_root_xa, function);
	if (WARN_ON_ONCE(!root))
		return NULL;

	tmp = root->rb_node;

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
	u32 out[MLX5_ST_SZ_DW(query_pages_out)] = {};
	u32 in[MLX5_ST_SZ_DW(query_pages_in)] = {};
	int err;

	MLX5_SET(query_pages_in, in, opcode, MLX5_CMD_OP_QUERY_PAGES);
	MLX5_SET(query_pages_in, in, op_mod, boot ?
		 MLX5_QUERY_PAGES_IN_OP_MOD_BOOT_PAGES :
		 MLX5_QUERY_PAGES_IN_OP_MOD_INIT_PAGES);
	MLX5_SET(query_pages_in, in, embedded_cpu_function, mlx5_core_is_ecpf(dev));

	err = mlx5_cmd_exec_inout(dev, query_pages, in, out);
	if (err)
		return err;

	*npages = MLX5_GET(query_pages_out, out, num_pages);
	*func_id = MLX5_GET(query_pages_out, out, function_id);

	return err;
}

static int alloc_4k(struct mlx5_core_dev *dev, u64 *addr, u32 function)
{
	struct fw_page *fp = NULL;
	struct fw_page *iter;
	unsigned n;

	list_for_each_entry(iter, &dev->priv.free_list, list) {
		if (iter->function != function)
			continue;
		fp = iter;
	}

	if (list_empty(&dev->priv.free_list) || !fp)
		return -ENOMEM;

	n = find_first_bit(&fp->bitmask, 8 * sizeof(fp->bitmask));
	if (n >= MLX5_NUM_4K_IN_PAGE) {
		mlx5_core_warn(dev, "alloc 4k bug: fw page = 0x%llx, n = %u, bitmask: %lu, max num of 4K pages: %d\n",
			       fp->addr, n, fp->bitmask,  MLX5_NUM_4K_IN_PAGE);
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

static void free_fwp(struct mlx5_core_dev *dev, struct fw_page *fwp,
		     bool in_free_list)
{
	struct rb_root *root;

	root = xa_load(&dev->priv.page_root_xa, fwp->function);
	if (WARN_ON_ONCE(!root))
		return;

	rb_erase(&fwp->rb_node, root);
	if (in_free_list)
		list_del(&fwp->list);
	dma_unmap_page(mlx5_core_dma_dev(dev), fwp->addr & MLX5_U64_4K_PAGE_MASK,
		       PAGE_SIZE, DMA_BIDIRECTIONAL);
	__free_page(fwp->page);
	kfree(fwp);
}

static void free_4k(struct mlx5_core_dev *dev, u64 addr, u32 function)
{
	struct fw_page *fwp;
	int n;

	fwp = find_fw_page(dev, addr & MLX5_U64_4K_PAGE_MASK, function);
	if (!fwp) {
		mlx5_core_warn_rl(dev, "page not found\n");
		return;
	}
	n = (addr & ~MLX5_U64_4K_PAGE_MASK) >> MLX5_ADAPTER_PAGE_SHIFT;
	fwp->free_count++;
	set_bit(n, &fwp->bitmask);
	if (fwp->free_count == MLX5_NUM_4K_IN_PAGE)
		free_fwp(dev, fwp, fwp->free_count != 1);
	else if (fwp->free_count == 1)
		list_add(&fwp->list, &dev->priv.free_list);
}

static int alloc_system_page(struct mlx5_core_dev *dev, u32 function)
{
	struct device *device = mlx5_core_dma_dev(dev);
	int nid = dev_to_node(device);
	struct page *page;
	u64 zero_addr = 1;
	u64 addr;
	int err;

	page = alloc_pages_node(nid, GFP_HIGHUSER, 0);
	if (!page) {
		mlx5_core_warn(dev, "failed to allocate page\n");
		return -ENOMEM;
	}
map:
	addr = dma_map_page(device, page, 0, PAGE_SIZE, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(device, addr)) {
		mlx5_core_warn(dev, "failed dma mapping page\n");
		err = -ENOMEM;
		goto err_mapping;
	}

	/* Firmware doesn't support page with physical address 0 */
	if (addr == 0) {
		zero_addr = addr;
		goto map;
	}

	err = insert_page(dev, addr, page, function);
	if (err) {
		mlx5_core_err(dev, "failed to track allocated page\n");
		dma_unmap_page(device, addr, PAGE_SIZE, DMA_BIDIRECTIONAL);
	}

err_mapping:
	if (err)
		__free_page(page);

	if (zero_addr == 0)
		dma_unmap_page(device, zero_addr, PAGE_SIZE,
			       DMA_BIDIRECTIONAL);

	return err;
}

static void page_notify_fail(struct mlx5_core_dev *dev, u16 func_id,
			     bool ec_function)
{
	u32 in[MLX5_ST_SZ_DW(manage_pages_in)] = {};
	int err;

	MLX5_SET(manage_pages_in, in, opcode, MLX5_CMD_OP_MANAGE_PAGES);
	MLX5_SET(manage_pages_in, in, op_mod, MLX5_PAGES_CANT_GIVE);
	MLX5_SET(manage_pages_in, in, function_id, func_id);
	MLX5_SET(manage_pages_in, in, embedded_cpu_function, ec_function);

	err = mlx5_cmd_exec_in(dev, manage_pages, in);
	if (err)
		mlx5_core_warn(dev, "page notify failed func_id(%d) err(%d)\n",
			       func_id, err);
}

static int give_pages(struct mlx5_core_dev *dev, u16 func_id, int npages,
		      int event, bool ec_function)
{
	u32 function = get_function(func_id, ec_function);
	u32 out[MLX5_ST_SZ_DW(manage_pages_out)] = {0};
	int inlen = MLX5_ST_SZ_BYTES(manage_pages_in);
	int notify_fail = event;
	u16 func_type;
	u64 addr;
	int err;
	u32 *in;
	int i;

	inlen += npages * MLX5_FLD_SZ_BYTES(manage_pages_in, pas[0]);
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in) {
		err = -ENOMEM;
		mlx5_core_warn(dev, "vzalloc failed %d\n", inlen);
		goto out_free;
	}

	for (i = 0; i < npages; i++) {
retry:
		err = alloc_4k(dev, &addr, function);
		if (err) {
			if (err == -ENOMEM)
				err = alloc_system_page(dev, function);
			if (err) {
				dev->priv.fw_pages_alloc_failed += (npages - i);
				goto out_4k;
			}

			goto retry;
		}
		MLX5_ARRAY_SET64(manage_pages_in, in, pas, i, addr);
	}

	MLX5_SET(manage_pages_in, in, opcode, MLX5_CMD_OP_MANAGE_PAGES);
	MLX5_SET(manage_pages_in, in, op_mod, MLX5_PAGES_GIVE);
	MLX5_SET(manage_pages_in, in, function_id, func_id);
	MLX5_SET(manage_pages_in, in, input_num_entries, npages);
	MLX5_SET(manage_pages_in, in, embedded_cpu_function, ec_function);

	err = mlx5_cmd_do(dev, in, inlen, out, sizeof(out));
	if (err == -EREMOTEIO) {
		notify_fail = 0;
		/* if triggered by FW and failed by FW ignore */
		if (event) {
			err = 0;
			goto out_dropped;
		}
	}
	err = mlx5_cmd_check(dev, err, in, out);
	if (err) {
		mlx5_core_warn(dev, "func_id 0x%x, npages %d, err %d\n",
			       func_id, npages, err);
		goto out_dropped;
	}

	func_type = func_id_to_type(dev, func_id, ec_function);
	dev->priv.page_counters[func_type] += npages;
	dev->priv.fw_pages += npages;

	mlx5_core_dbg(dev, "npages %d, ec_function %d, func_id 0x%x, err %d\n",
		      npages, ec_function, func_id, err);

	kvfree(in);
	return 0;

out_dropped:
	dev->priv.give_pages_dropped += npages;
out_4k:
	for (i--; i >= 0; i--)
		free_4k(dev, MLX5_GET64(manage_pages_in, in, pas[i]), function);
out_free:
	kvfree(in);
	if (notify_fail)
		page_notify_fail(dev, func_id, ec_function);
	return err;
}

static void release_all_pages(struct mlx5_core_dev *dev, u16 func_id,
			      bool ec_function)
{
	u32 function = get_function(func_id, ec_function);
	struct rb_root *root;
	struct rb_node *p;
	int npages = 0;
	u16 func_type;

	root = xa_load(&dev->priv.page_root_xa, function);
	if (WARN_ON_ONCE(!root))
		return;

	p = rb_first(root);
	while (p) {
		struct fw_page *fwp = rb_entry(p, struct fw_page, rb_node);

		p = rb_next(p);
		npages += (MLX5_NUM_4K_IN_PAGE - fwp->free_count);
		free_fwp(dev, fwp, fwp->free_count);
	}

	func_type = func_id_to_type(dev, func_id, ec_function);
	dev->priv.page_counters[func_type] -= npages;
	dev->priv.fw_pages -= npages;

	mlx5_core_dbg(dev, "npages %d, ec_function %d, func_id 0x%x\n",
		      npages, ec_function, func_id);
}

static u32 fwp_fill_manage_pages_out(struct fw_page *fwp, u32 *out, u32 index,
				     u32 npages)
{
	u32 pages_set = 0;
	unsigned int n;

	for_each_clear_bit(n, &fwp->bitmask, MLX5_NUM_4K_IN_PAGE) {
		MLX5_ARRAY_SET64(manage_pages_out, out, pas, index + pages_set,
				 fwp->addr + (n * MLX5_ADAPTER_PAGE_SIZE));
		pages_set++;

		if (!--npages)
			break;
	}

	return pages_set;
}

static int reclaim_pages_cmd(struct mlx5_core_dev *dev,
			     u32 *in, int in_size, u32 *out, int out_size)
{
	struct rb_root *root;
	struct fw_page *fwp;
	struct rb_node *p;
	bool ec_function;
	u32 func_id;
	u32 npages;
	u32 i = 0;

	if (!mlx5_cmd_is_down(dev))
		return mlx5_cmd_do(dev, in, in_size, out, out_size);

	/* No hard feelings, we want our pages back! */
	npages = MLX5_GET(manage_pages_in, in, input_num_entries);
	func_id = MLX5_GET(manage_pages_in, in, function_id);
	ec_function = MLX5_GET(manage_pages_in, in, embedded_cpu_function);

	root = xa_load(&dev->priv.page_root_xa, get_function(func_id, ec_function));
	if (WARN_ON_ONCE(!root))
		return -EEXIST;

	p = rb_first(root);
	while (p && i < npages) {
		fwp = rb_entry(p, struct fw_page, rb_node);
		p = rb_next(p);

		i += fwp_fill_manage_pages_out(fwp, out, i, npages - i);
	}

	MLX5_SET(manage_pages_out, out, output_num_entries, i);
	return 0;
}

static int reclaim_pages(struct mlx5_core_dev *dev, u16 func_id, int npages,
			 int *nclaimed, bool event, bool ec_function)
{
	u32 function = get_function(func_id, ec_function);
	int outlen = MLX5_ST_SZ_BYTES(manage_pages_out);
	u32 in[MLX5_ST_SZ_DW(manage_pages_in)] = {};
	int num_claimed;
	u16 func_type;
	u32 *out;
	int err;
	int i;

	if (nclaimed)
		*nclaimed = 0;

	outlen += npages * MLX5_FLD_SZ_BYTES(manage_pages_out, pas[0]);
	out = kvzalloc(outlen, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	MLX5_SET(manage_pages_in, in, opcode, MLX5_CMD_OP_MANAGE_PAGES);
	MLX5_SET(manage_pages_in, in, op_mod, MLX5_PAGES_TAKE);
	MLX5_SET(manage_pages_in, in, function_id, func_id);
	MLX5_SET(manage_pages_in, in, input_num_entries, npages);
	MLX5_SET(manage_pages_in, in, embedded_cpu_function, ec_function);

	mlx5_core_dbg(dev, "func 0x%x, npages %d, outlen %d\n",
		      func_id, npages, outlen);
	err = reclaim_pages_cmd(dev, in, sizeof(in), out, outlen);
	if (err) {
		npages = MLX5_GET(manage_pages_in, in, input_num_entries);
		dev->priv.reclaim_pages_discard += npages;
	}
	/* if triggered by FW event and failed by FW then ignore */
	if (event && err == -EREMOTEIO) {
		err = 0;
		goto out_free;
	}

	err = mlx5_cmd_check(dev, err, in, out);
	if (err) {
		mlx5_core_err(dev, "failed reclaiming pages: err %d\n", err);
		goto out_free;
	}

	num_claimed = MLX5_GET(manage_pages_out, out, output_num_entries);
	if (num_claimed > npages) {
		mlx5_core_warn(dev, "fw returned %d, driver asked %d => corruption\n",
			       num_claimed, npages);
		err = -EINVAL;
		goto out_free;
	}

	for (i = 0; i < num_claimed; i++)
		free_4k(dev, MLX5_GET64(manage_pages_out, out, pas[i]), function);

	if (nclaimed)
		*nclaimed = num_claimed;

	func_type = func_id_to_type(dev, func_id, ec_function);
	dev->priv.page_counters[func_type] -= num_claimed;
	dev->priv.fw_pages -= num_claimed;

out_free:
	kvfree(out);
	return err;
}

static void pages_work_handler(struct work_struct *work)
{
	struct mlx5_pages_req *req = container_of(work, struct mlx5_pages_req, work);
	struct mlx5_core_dev *dev = req->dev;
	int err = 0;

	if (req->release_all)
		release_all_pages(dev, req->func_id, req->ec_function);
	else if (req->npages < 0)
		err = reclaim_pages(dev, req->func_id, -1 * req->npages, NULL,
				    true, req->ec_function);
	else if (req->npages > 0)
		err = give_pages(dev, req->func_id, req->npages, 1, req->ec_function);

	if (err)
		mlx5_core_warn(dev, "%s fail %d\n",
			       req->npages < 0 ? "reclaim" : "give", err);

	kfree(req);
}

enum {
	EC_FUNCTION_MASK = 0x8000,
	RELEASE_ALL_PAGES_MASK = 0x4000,
};

static int req_pages_handler(struct notifier_block *nb,
			     unsigned long type, void *data)
{
	struct mlx5_pages_req *req;
	struct mlx5_core_dev *dev;
	struct mlx5_priv *priv;
	struct mlx5_eqe *eqe;
	bool ec_function;
	bool release_all;
	u16 func_id;
	s32 npages;

	priv = mlx5_nb_cof(nb, struct mlx5_priv, pg_nb);
	dev  = container_of(priv, struct mlx5_core_dev, priv);
	eqe  = data;

	func_id = be16_to_cpu(eqe->data.req_pages.func_id);
	npages  = be32_to_cpu(eqe->data.req_pages.num_pages);
	ec_function = be16_to_cpu(eqe->data.req_pages.ec_function) & EC_FUNCTION_MASK;
	release_all = be16_to_cpu(eqe->data.req_pages.ec_function) &
		      RELEASE_ALL_PAGES_MASK;
	mlx5_core_dbg(dev, "page request for func 0x%x, npages %d, release_all %d\n",
		      func_id, npages, release_all);
	req = kzalloc(sizeof(*req), GFP_ATOMIC);
	if (!req) {
		mlx5_core_warn(dev, "failed to allocate pages request\n");
		return NOTIFY_DONE;
	}

	req->dev = dev;
	req->func_id = func_id;
	req->npages = npages;
	req->ec_function = ec_function;
	req->release_all = release_all;
	INIT_WORK(&req->work, pages_work_handler);
	queue_work(dev->priv.pg_wq, &req->work);
	return NOTIFY_OK;
}

int mlx5_satisfy_startup_pages(struct mlx5_core_dev *dev, int boot)
{
	u16 func_id;
	s32 npages;
	int err;

	err = mlx5_cmd_query_pages(dev, &func_id, &npages, boot);
	if (err)
		return err;

	mlx5_core_dbg(dev, "requested %d %s pages for func_id 0x%x\n",
		      npages, boot ? "boot" : "init", func_id);

	return give_pages(dev, func_id, npages, 0, mlx5_core_is_ecpf(dev));
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
	       MLX5_ST_SZ_BYTES(manage_pages_out)) /
	       MLX5_FLD_SZ_BYTES(manage_pages_out, pas[0]);

	return ret;
}

static int mlx5_reclaim_root_pages(struct mlx5_core_dev *dev,
				   struct rb_root *root, u16 func_id)
{
	u64 recl_pages_to_jiffies = msecs_to_jiffies(mlx5_tout_ms(dev, RECLAIM_PAGES));
	unsigned long end = jiffies + recl_pages_to_jiffies;

	while (!RB_EMPTY_ROOT(root)) {
		int nclaimed;
		int err;

		err = reclaim_pages(dev, func_id, optimal_reclaimed_pages(),
				    &nclaimed, false, mlx5_core_is_ecpf(dev));
		if (err) {
			mlx5_core_warn(dev, "failed reclaiming pages (%d) for func id 0x%x\n",
				       err, func_id);
			return err;
		}

		if (nclaimed)
			end = jiffies + recl_pages_to_jiffies;

		if (time_after(jiffies, end)) {
			mlx5_core_warn(dev, "FW did not return all pages. giving up...\n");
			break;
		}
	}

	return 0;
}

int mlx5_reclaim_startup_pages(struct mlx5_core_dev *dev)
{
	struct rb_root *root;
	unsigned long id;
	void *entry;

	xa_for_each(&dev->priv.page_root_xa, id, entry) {
		root = entry;
		mlx5_reclaim_root_pages(dev, root, id);
		xa_erase(&dev->priv.page_root_xa, id);
		kfree(root);
	}

	WARN_ON(!xa_empty(&dev->priv.page_root_xa));

	WARN(dev->priv.fw_pages,
	     "FW pages counter is %d after reclaiming all pages\n",
	     dev->priv.fw_pages);
	WARN(dev->priv.page_counters[MLX5_VF],
	     "VFs FW pages counter is %d after reclaiming all pages\n",
	     dev->priv.page_counters[MLX5_VF]);
	WARN(dev->priv.page_counters[MLX5_HOST_PF],
	     "External host PF FW pages counter is %d after reclaiming all pages\n",
	     dev->priv.page_counters[MLX5_HOST_PF]);

	return 0;
}

int mlx5_pagealloc_init(struct mlx5_core_dev *dev)
{
	INIT_LIST_HEAD(&dev->priv.free_list);
	dev->priv.pg_wq = create_singlethread_workqueue("mlx5_page_allocator");
	if (!dev->priv.pg_wq)
		return -ENOMEM;

	xa_init(&dev->priv.page_root_xa);
	mlx5_pages_debugfs_init(dev);

	return 0;
}

void mlx5_pagealloc_cleanup(struct mlx5_core_dev *dev)
{
	mlx5_pages_debugfs_cleanup(dev);
	xa_destroy(&dev->priv.page_root_xa);
	destroy_workqueue(dev->priv.pg_wq);
}

void mlx5_pagealloc_start(struct mlx5_core_dev *dev)
{
	MLX5_NB_INIT(&dev->priv.pg_nb, req_pages_handler, PAGE_REQUEST);
	mlx5_eq_notifier_register(dev, &dev->priv.pg_nb);
}

void mlx5_pagealloc_stop(struct mlx5_core_dev *dev)
{
	mlx5_eq_notifier_unregister(dev, &dev->priv.pg_nb);
	flush_workqueue(dev->priv.pg_wq);
}

int mlx5_wait_for_pages(struct mlx5_core_dev *dev, int *pages)
{
	u64 recl_vf_pages_to_jiffies = msecs_to_jiffies(mlx5_tout_ms(dev, RECLAIM_VFS_PAGES));
	unsigned long end = jiffies + recl_vf_pages_to_jiffies;
	int prev_pages = *pages;

	/* In case of internal error we will free the pages manually later */
	if (dev->state == MLX5_DEVICE_STATE_INTERNAL_ERROR) {
		mlx5_core_warn(dev, "Skipping wait for vf pages stage");
		return 0;
	}

	mlx5_core_dbg(dev, "Waiting for %d pages\n", prev_pages);
	while (*pages) {
		if (time_after(jiffies, end)) {
			mlx5_core_warn(dev, "aborting while there are %d pending pages\n", *pages);
			return -ETIMEDOUT;
		}
		if (*pages < prev_pages) {
			end = jiffies + recl_vf_pages_to_jiffies;
			prev_pages = *pages;
		}
		msleep(50);
	}

	mlx5_core_dbg(dev, "All pages received\n");
	return 0;
}
