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

#include <asm-generic/kmap_types.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/io-mapping.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/cq.h>
#include <linux/mlx5/qp.h>
#include <linux/mlx5/srq.h>
#include <linux/debugfs.h>
#include <linux/kmod.h>
#include <linux/mlx5/mlx5_ifc.h>
#include "mlx5_core.h"

#define DRIVER_NAME "mlx5_core"
#define DRIVER_VERSION "2.2-1"
#define DRIVER_RELDATE	"Feb 2014"

MODULE_AUTHOR("Eli Cohen <eli@mellanox.com>");
MODULE_DESCRIPTION("Mellanox ConnectX-IB HCA core library");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(DRIVER_VERSION);

int mlx5_core_debug_mask;
module_param_named(debug_mask, mlx5_core_debug_mask, int, 0644);
MODULE_PARM_DESC(debug_mask, "debug mask: 1 = dump cmd data, 2 = dump cmd exec time, 3 = both. Default=0");

#define MLX5_DEFAULT_PROF	2
static int prof_sel = MLX5_DEFAULT_PROF;
module_param_named(prof_sel, prof_sel, int, 0444);
MODULE_PARM_DESC(prof_sel, "profile selector. Valid range 0 - 2");

struct workqueue_struct *mlx5_core_wq;
static LIST_HEAD(intf_list);
static LIST_HEAD(dev_list);
static DEFINE_MUTEX(intf_mutex);

struct mlx5_device_context {
	struct list_head	list;
	struct mlx5_interface  *intf;
	void		       *context;
};

static struct mlx5_profile profile[] = {
	[0] = {
		.mask           = 0,
	},
	[1] = {
		.mask		= MLX5_PROF_MASK_QP_SIZE,
		.log_max_qp	= 12,
	},
	[2] = {
		.mask		= MLX5_PROF_MASK_QP_SIZE |
				  MLX5_PROF_MASK_MR_CACHE,
		.log_max_qp	= 17,
		.mr_cache[0]	= {
			.size	= 500,
			.limit	= 250
		},
		.mr_cache[1]	= {
			.size	= 500,
			.limit	= 250
		},
		.mr_cache[2]	= {
			.size	= 500,
			.limit	= 250
		},
		.mr_cache[3]	= {
			.size	= 500,
			.limit	= 250
		},
		.mr_cache[4]	= {
			.size	= 500,
			.limit	= 250
		},
		.mr_cache[5]	= {
			.size	= 500,
			.limit	= 250
		},
		.mr_cache[6]	= {
			.size	= 500,
			.limit	= 250
		},
		.mr_cache[7]	= {
			.size	= 500,
			.limit	= 250
		},
		.mr_cache[8]	= {
			.size	= 500,
			.limit	= 250
		},
		.mr_cache[9]	= {
			.size	= 500,
			.limit	= 250
		},
		.mr_cache[10]	= {
			.size	= 500,
			.limit	= 250
		},
		.mr_cache[11]	= {
			.size	= 500,
			.limit	= 250
		},
		.mr_cache[12]	= {
			.size	= 64,
			.limit	= 32
		},
		.mr_cache[13]	= {
			.size	= 32,
			.limit	= 16
		},
		.mr_cache[14]	= {
			.size	= 16,
			.limit	= 8
		},
		.mr_cache[15]	= {
			.size	= 8,
			.limit	= 4
		},
	},
};

static int set_dma_caps(struct pci_dev *pdev)
{
	int err;

	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (err) {
		dev_warn(&pdev->dev, "Warning: couldn't set 64-bit PCI DMA mask\n");
		err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (err) {
			dev_err(&pdev->dev, "Can't set PCI DMA mask, aborting\n");
			return err;
		}
	}

	err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
	if (err) {
		dev_warn(&pdev->dev,
			 "Warning: couldn't set 64-bit consistent PCI DMA mask\n");
		err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
		if (err) {
			dev_err(&pdev->dev,
				"Can't set consistent PCI DMA mask, aborting\n");
			return err;
		}
	}

	dma_set_max_seg_size(&pdev->dev, 2u * 1024 * 1024 * 1024);
	return err;
}

static int request_bar(struct pci_dev *pdev)
{
	int err = 0;

	if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM)) {
		dev_err(&pdev->dev, "Missing registers BAR, aborting\n");
		return -ENODEV;
	}

	err = pci_request_regions(pdev, DRIVER_NAME);
	if (err)
		dev_err(&pdev->dev, "Couldn't get PCI resources, aborting\n");

	return err;
}

static void release_bar(struct pci_dev *pdev)
{
	pci_release_regions(pdev);
}

static int mlx5_enable_msix(struct mlx5_core_dev *dev)
{
	struct mlx5_eq_table *table = &dev->priv.eq_table;
	int num_eqs = 1 << dev->caps.gen.log_max_eq;
	int nvec;
	int i;

	nvec = dev->caps.gen.num_ports * num_online_cpus() + MLX5_EQ_VEC_COMP_BASE;
	nvec = min_t(int, nvec, num_eqs);
	if (nvec <= MLX5_EQ_VEC_COMP_BASE)
		return -ENOMEM;

	table->msix_arr = kzalloc(nvec * sizeof(*table->msix_arr), GFP_KERNEL);
	if (!table->msix_arr)
		return -ENOMEM;

	for (i = 0; i < nvec; i++)
		table->msix_arr[i].entry = i;

	nvec = pci_enable_msix_range(dev->pdev, table->msix_arr,
				     MLX5_EQ_VEC_COMP_BASE, nvec);
	if (nvec < 0)
		return nvec;

	table->num_comp_vectors = nvec - MLX5_EQ_VEC_COMP_BASE;

	return 0;
}

static void mlx5_disable_msix(struct mlx5_core_dev *dev)
{
	struct mlx5_eq_table *table = &dev->priv.eq_table;

	pci_disable_msix(dev->pdev);
	kfree(table->msix_arr);
}

struct mlx5_reg_host_endianess {
	u8	he;
	u8      rsvd[15];
};


#define CAP_MASK(pos, size) ((u64)((1 << (size)) - 1) << (pos))

enum {
	MLX5_CAP_BITS_RW_MASK = CAP_MASK(MLX5_CAP_OFF_CMDIF_CSUM, 2) |
				MLX5_DEV_CAP_FLAG_DCT,
};

static u16 to_fw_pkey_sz(u32 size)
{
	switch (size) {
	case 128:
		return 0;
	case 256:
		return 1;
	case 512:
		return 2;
	case 1024:
		return 3;
	case 2048:
		return 4;
	case 4096:
		return 5;
	default:
		pr_warn("invalid pkey table size %d\n", size);
		return 0;
	}
}

/* selectively copy writable fields clearing any reserved area
 */
static void copy_rw_fields(void *to, struct mlx5_caps *from)
{
	__be64 *flags_off = (__be64 *)MLX5_ADDR_OF(cmd_hca_cap, to, reserved_22);
	u64 v64;

	MLX5_SET(cmd_hca_cap, to, log_max_qp, from->gen.log_max_qp);
	MLX5_SET(cmd_hca_cap, to, log_max_ra_req_qp, from->gen.log_max_ra_req_qp);
	MLX5_SET(cmd_hca_cap, to, log_max_ra_res_qp, from->gen.log_max_ra_res_qp);
	MLX5_SET(cmd_hca_cap, to, pkey_table_size, from->gen.pkey_table_size);
	MLX5_SET(cmd_hca_cap, to, log_max_ra_req_dc, from->gen.log_max_ra_req_dc);
	MLX5_SET(cmd_hca_cap, to, log_max_ra_res_dc, from->gen.log_max_ra_res_dc);
	MLX5_SET(cmd_hca_cap, to, pkey_table_size, to_fw_pkey_sz(from->gen.pkey_table_size));
	v64 = from->gen.flags & MLX5_CAP_BITS_RW_MASK;
	*flags_off = cpu_to_be64(v64);
}

static u16 get_pkey_table_size(int pkey)
{
	if (pkey > MLX5_MAX_LOG_PKEY_TABLE)
		return 0;

	return MLX5_MIN_PKEY_TABLE_SIZE << pkey;
}

static void fw2drv_caps(struct mlx5_caps *caps, void *out)
{
	struct mlx5_general_caps *gen = &caps->gen;

	gen->max_srq_wqes = 1 << MLX5_GET_PR(cmd_hca_cap, out, log_max_srq_sz);
	gen->max_wqes = 1 << MLX5_GET_PR(cmd_hca_cap, out, log_max_qp_sz);
	gen->log_max_qp = MLX5_GET_PR(cmd_hca_cap, out, log_max_qp);
	gen->log_max_strq = MLX5_GET_PR(cmd_hca_cap, out, log_max_strq_sz);
	gen->log_max_srq = MLX5_GET_PR(cmd_hca_cap, out, log_max_srqs);
	gen->max_cqes = 1 << MLX5_GET_PR(cmd_hca_cap, out, log_max_cq_sz);
	gen->log_max_cq = MLX5_GET_PR(cmd_hca_cap, out, log_max_cq);
	gen->max_eqes = 1 << MLX5_GET_PR(cmd_hca_cap, out, log_max_eq_sz);
	gen->log_max_mkey = MLX5_GET_PR(cmd_hca_cap, out, log_max_mkey);
	gen->log_max_eq = MLX5_GET_PR(cmd_hca_cap, out, log_max_eq);
	gen->max_indirection = MLX5_GET_PR(cmd_hca_cap, out, max_indirection);
	gen->log_max_mrw_sz = MLX5_GET_PR(cmd_hca_cap, out, log_max_mrw_sz);
	gen->log_max_bsf_list_size = MLX5_GET_PR(cmd_hca_cap, out, log_max_bsf_list_size);
	gen->log_max_klm_list_size = MLX5_GET_PR(cmd_hca_cap, out, log_max_klm_list_size);
	gen->log_max_ra_req_dc = MLX5_GET_PR(cmd_hca_cap, out, log_max_ra_req_dc);
	gen->log_max_ra_res_dc = MLX5_GET_PR(cmd_hca_cap, out, log_max_ra_res_dc);
	gen->log_max_ra_req_qp = MLX5_GET_PR(cmd_hca_cap, out, log_max_ra_req_qp);
	gen->log_max_ra_res_qp = MLX5_GET_PR(cmd_hca_cap, out, log_max_ra_res_qp);
	gen->max_qp_counters = MLX5_GET_PR(cmd_hca_cap, out, max_qp_cnt);
	gen->pkey_table_size = get_pkey_table_size(MLX5_GET_PR(cmd_hca_cap, out, pkey_table_size));
	gen->local_ca_ack_delay = MLX5_GET_PR(cmd_hca_cap, out, local_ca_ack_delay);
	gen->num_ports = MLX5_GET_PR(cmd_hca_cap, out, num_ports);
	gen->log_max_msg = MLX5_GET_PR(cmd_hca_cap, out, log_max_msg);
	gen->stat_rate_support = MLX5_GET_PR(cmd_hca_cap, out, stat_rate_support);
	gen->flags = be64_to_cpu(*(__be64 *)MLX5_ADDR_OF(cmd_hca_cap, out, reserved_22));
	pr_debug("flags = 0x%llx\n", gen->flags);
	gen->uar_sz = MLX5_GET_PR(cmd_hca_cap, out, uar_sz);
	gen->min_log_pg_sz = MLX5_GET_PR(cmd_hca_cap, out, log_pg_sz);
	gen->bf_reg_size = MLX5_GET_PR(cmd_hca_cap, out, bf);
	gen->bf_reg_size = 1 << MLX5_GET_PR(cmd_hca_cap, out, log_bf_reg_size);
	gen->max_sq_desc_sz = MLX5_GET_PR(cmd_hca_cap, out, max_wqe_sz_sq);
	gen->max_rq_desc_sz = MLX5_GET_PR(cmd_hca_cap, out, max_wqe_sz_rq);
	gen->max_dc_sq_desc_sz = MLX5_GET_PR(cmd_hca_cap, out, max_wqe_sz_sq_dc);
	gen->max_qp_mcg = MLX5_GET_PR(cmd_hca_cap, out, max_qp_mcg);
	gen->log_max_pd = MLX5_GET_PR(cmd_hca_cap, out, log_max_pd);
	gen->log_max_xrcd = MLX5_GET_PR(cmd_hca_cap, out, log_max_xrcd);
	gen->log_uar_page_sz = MLX5_GET_PR(cmd_hca_cap, out, log_uar_page_sz);
}

static const char *caps_opmod_str(u16 opmod)
{
	switch (opmod) {
	case HCA_CAP_OPMOD_GET_MAX:
		return "GET_MAX";
	case HCA_CAP_OPMOD_GET_CUR:
		return "GET_CUR";
	default:
		return "Invalid";
	}
}

int mlx5_core_get_caps(struct mlx5_core_dev *dev, struct mlx5_caps *caps,
		       u16 opmod)
{
	u8 in[MLX5_ST_SZ_BYTES(query_hca_cap_in)];
	int out_sz = MLX5_ST_SZ_BYTES(query_hca_cap_out);
	void *out;
	int err;

	memset(in, 0, sizeof(in));
	out = kzalloc(out_sz, GFP_KERNEL);
	if (!out)
		return -ENOMEM;
	MLX5_SET(query_hca_cap_in, in, opcode, MLX5_CMD_OP_QUERY_HCA_CAP);
	MLX5_SET(query_hca_cap_in, in, op_mod, opmod);
	err = mlx5_cmd_exec(dev, in, sizeof(in), out, out_sz);
	if (err)
		goto query_ex;

	err = mlx5_cmd_status_to_err_v2(out);
	if (err) {
		mlx5_core_warn(dev, "query max hca cap failed, %d\n", err);
		goto query_ex;
	}
	mlx5_core_dbg(dev, "%s\n", caps_opmod_str(opmod));
	fw2drv_caps(caps, MLX5_ADDR_OF(query_hca_cap_out, out, capability_struct));

query_ex:
	kfree(out);
	return err;
}

static int set_caps(struct mlx5_core_dev *dev, void *in, int in_sz)
{
	u32 out[MLX5_ST_SZ_DW(set_hca_cap_out)];
	int err;

	memset(out, 0, sizeof(out));

	MLX5_SET(set_hca_cap_in, in, opcode, MLX5_CMD_OP_SET_HCA_CAP);
	err = mlx5_cmd_exec(dev, in, in_sz, out, sizeof(out));
	if (err)
		return err;

	err = mlx5_cmd_status_to_err_v2(out);

	return err;
}

static int handle_hca_cap(struct mlx5_core_dev *dev)
{
	void *set_ctx = NULL;
	struct mlx5_profile *prof = dev->profile;
	struct mlx5_caps *cur_caps = NULL;
	struct mlx5_caps *max_caps = NULL;
	int err = -ENOMEM;
	int set_sz = MLX5_ST_SZ_BYTES(set_hca_cap_in);

	set_ctx = kzalloc(set_sz, GFP_KERNEL);
	if (!set_ctx)
		goto query_ex;

	max_caps = kzalloc(sizeof(*max_caps), GFP_KERNEL);
	if (!max_caps)
		goto query_ex;

	cur_caps = kzalloc(sizeof(*cur_caps), GFP_KERNEL);
	if (!cur_caps)
		goto query_ex;

	err = mlx5_core_get_caps(dev, max_caps, HCA_CAP_OPMOD_GET_MAX);
	if (err)
		goto query_ex;

	err = mlx5_core_get_caps(dev, cur_caps, HCA_CAP_OPMOD_GET_CUR);
	if (err)
		goto query_ex;

	/* we limit the size of the pkey table to 128 entries for now */
	cur_caps->gen.pkey_table_size = 128;

	if (prof->mask & MLX5_PROF_MASK_QP_SIZE)
		cur_caps->gen.log_max_qp = prof->log_max_qp;

	/* disable checksum */
	cur_caps->gen.flags &= ~MLX5_DEV_CAP_FLAG_CMDIF_CSUM;

	copy_rw_fields(MLX5_ADDR_OF(set_hca_cap_in, set_ctx, hca_capability_struct),
		       cur_caps);
	err = set_caps(dev, set_ctx, set_sz);

query_ex:
	kfree(cur_caps);
	kfree(max_caps);
	kfree(set_ctx);

	return err;
}

static int set_hca_ctrl(struct mlx5_core_dev *dev)
{
	struct mlx5_reg_host_endianess he_in;
	struct mlx5_reg_host_endianess he_out;
	int err;

	memset(&he_in, 0, sizeof(he_in));
	he_in.he = MLX5_SET_HOST_ENDIANNESS;
	err = mlx5_core_access_reg(dev, &he_in,  sizeof(he_in),
					&he_out, sizeof(he_out),
					MLX5_REG_HOST_ENDIANNESS, 0, 1);
	return err;
}

static int mlx5_core_enable_hca(struct mlx5_core_dev *dev)
{
	int err;
	struct mlx5_enable_hca_mbox_in in;
	struct mlx5_enable_hca_mbox_out out;

	memset(&in, 0, sizeof(in));
	memset(&out, 0, sizeof(out));
	in.hdr.opcode = cpu_to_be16(MLX5_CMD_OP_ENABLE_HCA);
	err = mlx5_cmd_exec(dev, &in, sizeof(in), &out, sizeof(out));
	if (err)
		return err;

	if (out.hdr.status)
		return mlx5_cmd_status_to_err(&out.hdr);

	return 0;
}

static int mlx5_core_disable_hca(struct mlx5_core_dev *dev)
{
	int err;
	struct mlx5_disable_hca_mbox_in in;
	struct mlx5_disable_hca_mbox_out out;

	memset(&in, 0, sizeof(in));
	memset(&out, 0, sizeof(out));
	in.hdr.opcode = cpu_to_be16(MLX5_CMD_OP_DISABLE_HCA);
	err = mlx5_cmd_exec(dev, &in, sizeof(in), &out, sizeof(out));
	if (err)
		return err;

	if (out.hdr.status)
		return mlx5_cmd_status_to_err(&out.hdr);

	return 0;
}

static int mlx5_dev_init(struct mlx5_core_dev *dev, struct pci_dev *pdev)
{
	struct mlx5_priv *priv = &dev->priv;
	int err;

	dev->pdev = pdev;
	pci_set_drvdata(dev->pdev, dev);
	strncpy(priv->name, dev_name(&pdev->dev), MLX5_MAX_NAME_LEN);
	priv->name[MLX5_MAX_NAME_LEN - 1] = 0;

	mutex_init(&priv->pgdir_mutex);
	INIT_LIST_HEAD(&priv->pgdir_list);
	spin_lock_init(&priv->mkey_lock);

	priv->dbg_root = debugfs_create_dir(dev_name(&pdev->dev), mlx5_debugfs_root);
	if (!priv->dbg_root)
		return -ENOMEM;

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "Cannot enable PCI device, aborting\n");
		goto err_dbg;
	}

	err = request_bar(pdev);
	if (err) {
		dev_err(&pdev->dev, "error requesting BARs, aborting\n");
		goto err_disable;
	}

	pci_set_master(pdev);

	err = set_dma_caps(pdev);
	if (err) {
		dev_err(&pdev->dev, "Failed setting DMA capabilities mask, aborting\n");
		goto err_clr_master;
	}

	dev->iseg_base = pci_resource_start(dev->pdev, 0);
	dev->iseg = ioremap(dev->iseg_base, sizeof(*dev->iseg));
	if (!dev->iseg) {
		err = -ENOMEM;
		dev_err(&pdev->dev, "Failed mapping initialization segment, aborting\n");
		goto err_clr_master;
	}
	dev_info(&pdev->dev, "firmware version: %d.%d.%d\n", fw_rev_maj(dev),
		 fw_rev_min(dev), fw_rev_sub(dev));

	err = mlx5_cmd_init(dev);
	if (err) {
		dev_err(&pdev->dev, "Failed initializing command interface, aborting\n");
		goto err_unmap;
	}

	mlx5_pagealloc_init(dev);

	err = mlx5_core_enable_hca(dev);
	if (err) {
		dev_err(&pdev->dev, "enable hca failed\n");
		goto err_pagealloc_cleanup;
	}

	err = mlx5_satisfy_startup_pages(dev, 1);
	if (err) {
		dev_err(&pdev->dev, "failed to allocate boot pages\n");
		goto err_disable_hca;
	}

	err = set_hca_ctrl(dev);
	if (err) {
		dev_err(&pdev->dev, "set_hca_ctrl failed\n");
		goto reclaim_boot_pages;
	}

	err = handle_hca_cap(dev);
	if (err) {
		dev_err(&pdev->dev, "handle_hca_cap failed\n");
		goto reclaim_boot_pages;
	}

	err = mlx5_satisfy_startup_pages(dev, 0);
	if (err) {
		dev_err(&pdev->dev, "failed to allocate init pages\n");
		goto reclaim_boot_pages;
	}

	err = mlx5_pagealloc_start(dev);
	if (err) {
		dev_err(&pdev->dev, "mlx5_pagealloc_start failed\n");
		goto reclaim_boot_pages;
	}

	err = mlx5_cmd_init_hca(dev);
	if (err) {
		dev_err(&pdev->dev, "init hca failed\n");
		goto err_pagealloc_stop;
	}

	mlx5_start_health_poll(dev);

	err = mlx5_cmd_query_hca_cap(dev, &dev->caps);
	if (err) {
		dev_err(&pdev->dev, "query hca failed\n");
		goto err_stop_poll;
	}

	err = mlx5_cmd_query_adapter(dev);
	if (err) {
		dev_err(&pdev->dev, "query adapter failed\n");
		goto err_stop_poll;
	}

	err = mlx5_enable_msix(dev);
	if (err) {
		dev_err(&pdev->dev, "enable msix failed\n");
		goto err_stop_poll;
	}

	err = mlx5_eq_init(dev);
	if (err) {
		dev_err(&pdev->dev, "failed to initialize eq\n");
		goto disable_msix;
	}

	err = mlx5_alloc_uuars(dev, &priv->uuari);
	if (err) {
		dev_err(&pdev->dev, "Failed allocating uar, aborting\n");
		goto err_eq_cleanup;
	}

	err = mlx5_start_eqs(dev);
	if (err) {
		dev_err(&pdev->dev, "Failed to start pages and async EQs\n");
		goto err_free_uar;
	}

	MLX5_INIT_DOORBELL_LOCK(&priv->cq_uar_lock);

	mlx5_init_cq_table(dev);
	mlx5_init_qp_table(dev);
	mlx5_init_srq_table(dev);
	mlx5_init_mr_table(dev);

	return 0;

err_free_uar:
	mlx5_free_uuars(dev, &priv->uuari);

err_eq_cleanup:
	mlx5_eq_cleanup(dev);

disable_msix:
	mlx5_disable_msix(dev);

err_stop_poll:
	mlx5_stop_health_poll(dev);
	if (mlx5_cmd_teardown_hca(dev)) {
		dev_err(&dev->pdev->dev, "tear_down_hca failed, skip cleanup\n");
		return err;
	}

err_pagealloc_stop:
	mlx5_pagealloc_stop(dev);

reclaim_boot_pages:
	mlx5_reclaim_startup_pages(dev);

err_disable_hca:
	mlx5_core_disable_hca(dev);

err_pagealloc_cleanup:
	mlx5_pagealloc_cleanup(dev);
	mlx5_cmd_cleanup(dev);

err_unmap:
	iounmap(dev->iseg);

err_clr_master:
	pci_clear_master(dev->pdev);
	release_bar(dev->pdev);

err_disable:
	pci_disable_device(dev->pdev);

err_dbg:
	debugfs_remove(priv->dbg_root);
	return err;
}
EXPORT_SYMBOL(mlx5_dev_init);

static void mlx5_dev_cleanup(struct mlx5_core_dev *dev)
{
	struct mlx5_priv *priv = &dev->priv;

	mlx5_cleanup_srq_table(dev);
	mlx5_cleanup_qp_table(dev);
	mlx5_cleanup_cq_table(dev);
	mlx5_stop_eqs(dev);
	mlx5_free_uuars(dev, &priv->uuari);
	mlx5_eq_cleanup(dev);
	mlx5_disable_msix(dev);
	mlx5_stop_health_poll(dev);
	if (mlx5_cmd_teardown_hca(dev)) {
		dev_err(&dev->pdev->dev, "tear_down_hca failed, skip cleanup\n");
		return;
	}
	mlx5_pagealloc_stop(dev);
	mlx5_reclaim_startup_pages(dev);
	mlx5_core_disable_hca(dev);
	mlx5_pagealloc_cleanup(dev);
	mlx5_cmd_cleanup(dev);
	iounmap(dev->iseg);
	pci_clear_master(dev->pdev);
	release_bar(dev->pdev);
	pci_disable_device(dev->pdev);
	debugfs_remove(priv->dbg_root);
}

static void mlx5_add_device(struct mlx5_interface *intf, struct mlx5_priv *priv)
{
	struct mlx5_device_context *dev_ctx;
	struct mlx5_core_dev *dev = container_of(priv, struct mlx5_core_dev, priv);

	dev_ctx = kmalloc(sizeof(*dev_ctx), GFP_KERNEL);
	if (!dev_ctx) {
		pr_warn("mlx5_add_device: alloc context failed\n");
		return;
	}

	dev_ctx->intf    = intf;
	dev_ctx->context = intf->add(dev);

	if (dev_ctx->context) {
		spin_lock_irq(&priv->ctx_lock);
		list_add_tail(&dev_ctx->list, &priv->ctx_list);
		spin_unlock_irq(&priv->ctx_lock);
	} else {
		kfree(dev_ctx);
	}
}

static void mlx5_remove_device(struct mlx5_interface *intf, struct mlx5_priv *priv)
{
	struct mlx5_device_context *dev_ctx;
	struct mlx5_core_dev *dev = container_of(priv, struct mlx5_core_dev, priv);

	list_for_each_entry(dev_ctx, &priv->ctx_list, list)
		if (dev_ctx->intf == intf) {
			spin_lock_irq(&priv->ctx_lock);
			list_del(&dev_ctx->list);
			spin_unlock_irq(&priv->ctx_lock);

			intf->remove(dev, dev_ctx->context);
			kfree(dev_ctx);
			return;
		}
}
static int mlx5_register_device(struct mlx5_core_dev *dev)
{
	struct mlx5_priv *priv = &dev->priv;
	struct mlx5_interface *intf;

	mutex_lock(&intf_mutex);
	list_add_tail(&priv->dev_list, &dev_list);
	list_for_each_entry(intf, &intf_list, list)
		mlx5_add_device(intf, priv);
	mutex_unlock(&intf_mutex);

	return 0;
}
static void mlx5_unregister_device(struct mlx5_core_dev *dev)
{
	struct mlx5_priv *priv = &dev->priv;
	struct mlx5_interface *intf;

	mutex_lock(&intf_mutex);
	list_for_each_entry(intf, &intf_list, list)
		mlx5_remove_device(intf, priv);
	list_del(&priv->dev_list);
	mutex_unlock(&intf_mutex);
}

int mlx5_register_interface(struct mlx5_interface *intf)
{
	struct mlx5_priv *priv;

	if (!intf->add || !intf->remove)
		return -EINVAL;

	mutex_lock(&intf_mutex);
	list_add_tail(&intf->list, &intf_list);
	list_for_each_entry(priv, &dev_list, dev_list)
		mlx5_add_device(intf, priv);
	mutex_unlock(&intf_mutex);

	return 0;
}
EXPORT_SYMBOL(mlx5_register_interface);

void mlx5_unregister_interface(struct mlx5_interface *intf)
{
	struct mlx5_priv *priv;

	mutex_lock(&intf_mutex);
	list_for_each_entry(priv, &dev_list, dev_list)
	       mlx5_remove_device(intf, priv);
	list_del(&intf->list);
	mutex_unlock(&intf_mutex);
}
EXPORT_SYMBOL(mlx5_unregister_interface);

static void mlx5_core_event(struct mlx5_core_dev *dev, enum mlx5_dev_event event,
			    unsigned long param)
{
	struct mlx5_priv *priv = &dev->priv;
	struct mlx5_device_context *dev_ctx;
	unsigned long flags;

	spin_lock_irqsave(&priv->ctx_lock, flags);

	list_for_each_entry(dev_ctx, &priv->ctx_list, list)
		if (dev_ctx->intf->event)
			dev_ctx->intf->event(dev, dev_ctx->context, event, param);

	spin_unlock_irqrestore(&priv->ctx_lock, flags);
}

struct mlx5_core_event_handler {
	void (*event)(struct mlx5_core_dev *dev,
		      enum mlx5_dev_event event,
		      void *data);
};

#define MLX5_IB_MOD "mlx5_ib"

static int init_one(struct pci_dev *pdev,
		    const struct pci_device_id *id)
{
	struct mlx5_core_dev *dev;
	struct mlx5_priv *priv;
	int err;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		dev_err(&pdev->dev, "kzalloc failed\n");
		return -ENOMEM;
	}
	priv = &dev->priv;

	pci_set_drvdata(pdev, dev);

	if (prof_sel < 0 || prof_sel >= ARRAY_SIZE(profile)) {
		pr_warn("selected profile out of range, selecting default (%d)\n",
			MLX5_DEFAULT_PROF);
		prof_sel = MLX5_DEFAULT_PROF;
	}
	dev->profile = &profile[prof_sel];
	dev->event = mlx5_core_event;

	INIT_LIST_HEAD(&priv->ctx_list);
	spin_lock_init(&priv->ctx_lock);
	err = mlx5_dev_init(dev, pdev);
	if (err) {
		dev_err(&pdev->dev, "mlx5_dev_init failed %d\n", err);
		goto out;
	}

	err = mlx5_register_device(dev);
	if (err) {
		dev_err(&pdev->dev, "mlx5_register_device failed %d\n", err);
		goto out_init;
	}

	err = request_module_nowait(MLX5_IB_MOD);
	if (err)
		pr_info("failed request module on %s\n", MLX5_IB_MOD);

	return 0;

out_init:
	mlx5_dev_cleanup(dev);
out:
	kfree(dev);
	return err;
}
static void remove_one(struct pci_dev *pdev)
{
	struct mlx5_core_dev *dev  = pci_get_drvdata(pdev);

	mlx5_unregister_device(dev);
	mlx5_dev_cleanup(dev);
	kfree(dev);
}

static const struct pci_device_id mlx5_core_pci_table[] = {
	{ PCI_VDEVICE(MELLANOX, 4113) }, /* MT4113 Connect-IB */
	{ PCI_VDEVICE(MELLANOX, 4115) }, /* ConnectX-4 */
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, mlx5_core_pci_table);

static struct pci_driver mlx5_core_driver = {
	.name           = DRIVER_NAME,
	.id_table       = mlx5_core_pci_table,
	.probe          = init_one,
	.remove         = remove_one
};

static int __init init(void)
{
	int err;

	mlx5_register_debugfs();
	mlx5_core_wq = create_singlethread_workqueue("mlx5_core_wq");
	if (!mlx5_core_wq) {
		err = -ENOMEM;
		goto err_debug;
	}
	mlx5_health_init();

	err = pci_register_driver(&mlx5_core_driver);
	if (err)
		goto err_health;

	return 0;

err_health:
	mlx5_health_cleanup();
	destroy_workqueue(mlx5_core_wq);
err_debug:
	mlx5_unregister_debugfs();
	return err;
}

static void __exit cleanup(void)
{
	pci_unregister_driver(&mlx5_core_driver);
	mlx5_health_cleanup();
	destroy_workqueue(mlx5_core_wq);
	mlx5_unregister_debugfs();
}

module_init(init);
module_exit(cleanup);
