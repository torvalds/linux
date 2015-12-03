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
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/io-mapping.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/cq.h>
#include <linux/mlx5/qp.h>
#include <linux/mlx5/srq.h>
#include <linux/debugfs.h>
#include <linux/kmod.h>
#include <linux/delay.h>
#include <linux/mlx5/mlx5_ifc.h>
#include "mlx5_core.h"
#ifdef CONFIG_MLX5_CORE_EN
#include "eswitch.h"
#endif

MODULE_AUTHOR("Eli Cohen <eli@mellanox.com>");
MODULE_DESCRIPTION("Mellanox Connect-IB, ConnectX-4 core driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(DRIVER_VERSION);

int mlx5_core_debug_mask;
module_param_named(debug_mask, mlx5_core_debug_mask, int, 0644);
MODULE_PARM_DESC(debug_mask, "debug mask: 1 = dump cmd data, 2 = dump cmd exec time, 3 = both. Default=0");

#define MLX5_DEFAULT_PROF	2
static int prof_sel = MLX5_DEFAULT_PROF;
module_param_named(prof_sel, prof_sel, int, 0444);
MODULE_PARM_DESC(prof_sel, "profile selector. Valid range 0 - 2");

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

#define FW_INIT_TIMEOUT_MILI	2000
#define FW_INIT_WAIT_MS		2

static int wait_fw_init(struct mlx5_core_dev *dev, u32 max_wait_mili)
{
	unsigned long end = jiffies + msecs_to_jiffies(max_wait_mili);
	int err = 0;

	while (fw_initializing(dev)) {
		if (time_after(jiffies, end)) {
			err = -EBUSY;
			break;
		}
		msleep(FW_INIT_WAIT_MS);
	}

	return err;
}

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

static int mlx5_pci_enable_device(struct mlx5_core_dev *dev)
{
	struct pci_dev *pdev = dev->pdev;
	int err = 0;

	mutex_lock(&dev->pci_status_mutex);
	if (dev->pci_status == MLX5_PCI_STATUS_DISABLED) {
		err = pci_enable_device(pdev);
		if (!err)
			dev->pci_status = MLX5_PCI_STATUS_ENABLED;
	}
	mutex_unlock(&dev->pci_status_mutex);

	return err;
}

static void mlx5_pci_disable_device(struct mlx5_core_dev *dev)
{
	struct pci_dev *pdev = dev->pdev;

	mutex_lock(&dev->pci_status_mutex);
	if (dev->pci_status == MLX5_PCI_STATUS_ENABLED) {
		pci_disable_device(pdev);
		dev->pci_status = MLX5_PCI_STATUS_DISABLED;
	}
	mutex_unlock(&dev->pci_status_mutex);
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
	struct mlx5_priv *priv = &dev->priv;
	struct mlx5_eq_table *table = &priv->eq_table;
	int num_eqs = 1 << MLX5_CAP_GEN(dev, log_max_eq);
	int nvec;
	int i;

	nvec = MLX5_CAP_GEN(dev, num_ports) * num_online_cpus() +
	       MLX5_EQ_VEC_COMP_BASE;
	nvec = min_t(int, nvec, num_eqs);
	if (nvec <= MLX5_EQ_VEC_COMP_BASE)
		return -ENOMEM;

	priv->msix_arr = kcalloc(nvec, sizeof(*priv->msix_arr), GFP_KERNEL);

	priv->irq_info = kcalloc(nvec, sizeof(*priv->irq_info), GFP_KERNEL);
	if (!priv->msix_arr || !priv->irq_info)
		goto err_free_msix;

	for (i = 0; i < nvec; i++)
		priv->msix_arr[i].entry = i;

	nvec = pci_enable_msix_range(dev->pdev, priv->msix_arr,
				     MLX5_EQ_VEC_COMP_BASE + 1, nvec);
	if (nvec < 0)
		return nvec;

	table->num_comp_vectors = nvec - MLX5_EQ_VEC_COMP_BASE;

	return 0;

err_free_msix:
	kfree(priv->irq_info);
	kfree(priv->msix_arr);
	return -ENOMEM;
}

static void mlx5_disable_msix(struct mlx5_core_dev *dev)
{
	struct mlx5_priv *priv = &dev->priv;

	pci_disable_msix(dev->pdev);
	kfree(priv->irq_info);
	kfree(priv->msix_arr);
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

int mlx5_core_get_caps(struct mlx5_core_dev *dev, enum mlx5_cap_type cap_type,
		       enum mlx5_cap_mode cap_mode)
{
	u8 in[MLX5_ST_SZ_BYTES(query_hca_cap_in)];
	int out_sz = MLX5_ST_SZ_BYTES(query_hca_cap_out);
	void *out, *hca_caps;
	u16 opmod = (cap_type << 1) | (cap_mode & 0x01);
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
		mlx5_core_warn(dev,
			       "QUERY_HCA_CAP : type(%x) opmode(%x) Failed(%d)\n",
			       cap_type, cap_mode, err);
		goto query_ex;
	}

	hca_caps =  MLX5_ADDR_OF(query_hca_cap_out, out, capability);

	switch (cap_mode) {
	case HCA_CAP_OPMOD_GET_MAX:
		memcpy(dev->hca_caps_max[cap_type], hca_caps,
		       MLX5_UN_SZ_BYTES(hca_cap_union));
		break;
	case HCA_CAP_OPMOD_GET_CUR:
		memcpy(dev->hca_caps_cur[cap_type], hca_caps,
		       MLX5_UN_SZ_BYTES(hca_cap_union));
		break;
	default:
		mlx5_core_warn(dev,
			       "Tried to query dev cap type(%x) with wrong opmode(%x)\n",
			       cap_type, cap_mode);
		err = -EINVAL;
		break;
	}
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
	int err = -ENOMEM;
	int set_sz = MLX5_ST_SZ_BYTES(set_hca_cap_in);
	void *set_hca_cap;

	set_ctx = kzalloc(set_sz, GFP_KERNEL);
	if (!set_ctx)
		goto query_ex;

	err = mlx5_core_get_caps(dev, MLX5_CAP_GENERAL, HCA_CAP_OPMOD_GET_MAX);
	if (err)
		goto query_ex;

	err = mlx5_core_get_caps(dev, MLX5_CAP_GENERAL, HCA_CAP_OPMOD_GET_CUR);
	if (err)
		goto query_ex;

	set_hca_cap = MLX5_ADDR_OF(set_hca_cap_in, set_ctx,
				   capability);
	memcpy(set_hca_cap, dev->hca_caps_cur[MLX5_CAP_GENERAL],
	       MLX5_ST_SZ_BYTES(cmd_hca_cap));

	mlx5_core_dbg(dev, "Current Pkey table size %d Setting new size %d\n",
		      mlx5_to_sw_pkey_sz(MLX5_CAP_GEN(dev, pkey_table_size)),
		      128);
	/* we limit the size of the pkey table to 128 entries for now */
	MLX5_SET(cmd_hca_cap, set_hca_cap, pkey_table_size,
		 to_fw_pkey_sz(128));

	if (prof->mask & MLX5_PROF_MASK_QP_SIZE)
		MLX5_SET(cmd_hca_cap, set_hca_cap, log_max_qp,
			 prof->log_max_qp);

	/* disable cmdif checksum */
	MLX5_SET(cmd_hca_cap, set_hca_cap, cmdif_checksum, 0);

	MLX5_SET(cmd_hca_cap, set_hca_cap, log_uar_page_sz, PAGE_SHIFT - 12);

	err = set_caps(dev, set_ctx, set_sz);

query_ex:
	kfree(set_ctx);
	return err;
}

static int set_hca_ctrl(struct mlx5_core_dev *dev)
{
	struct mlx5_reg_host_endianess he_in;
	struct mlx5_reg_host_endianess he_out;
	int err;

	if (!mlx5_core_is_pf(dev))
		return 0;

	memset(&he_in, 0, sizeof(he_in));
	he_in.he = MLX5_SET_HOST_ENDIANNESS;
	err = mlx5_core_access_reg(dev, &he_in,  sizeof(he_in),
					&he_out, sizeof(he_out),
					MLX5_REG_HOST_ENDIANNESS, 0, 1);
	return err;
}

int mlx5_core_enable_hca(struct mlx5_core_dev *dev, u16 func_id)
{
	u32 out[MLX5_ST_SZ_DW(enable_hca_out)];
	u32 in[MLX5_ST_SZ_DW(enable_hca_in)];
	int err;

	memset(in, 0, sizeof(in));
	MLX5_SET(enable_hca_in, in, opcode, MLX5_CMD_OP_ENABLE_HCA);
	MLX5_SET(enable_hca_in, in, function_id, func_id);
	memset(out, 0, sizeof(out));

	err = mlx5_cmd_exec(dev, &in, sizeof(in), &out, sizeof(out));
	if (err)
		return err;

	return mlx5_cmd_status_to_err_v2(out);
}

int mlx5_core_disable_hca(struct mlx5_core_dev *dev, u16 func_id)
{
	u32 out[MLX5_ST_SZ_DW(disable_hca_out)];
	u32 in[MLX5_ST_SZ_DW(disable_hca_in)];
	int err;

	memset(in, 0, sizeof(in));
	MLX5_SET(disable_hca_in, in, opcode, MLX5_CMD_OP_DISABLE_HCA);
	MLX5_SET(disable_hca_in, in, function_id, func_id);
	memset(out, 0, sizeof(out));
	err = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (err)
		return err;

	return mlx5_cmd_status_to_err_v2(out);
}

static int mlx5_irq_set_affinity_hint(struct mlx5_core_dev *mdev, int i)
{
	struct mlx5_priv *priv  = &mdev->priv;
	struct msix_entry *msix = priv->msix_arr;
	int irq                 = msix[i + MLX5_EQ_VEC_COMP_BASE].vector;
	int numa_node           = priv->numa_node;
	int err;

	if (!zalloc_cpumask_var(&priv->irq_info[i].mask, GFP_KERNEL)) {
		mlx5_core_warn(mdev, "zalloc_cpumask_var failed");
		return -ENOMEM;
	}

	cpumask_set_cpu(cpumask_local_spread(i, numa_node),
			priv->irq_info[i].mask);

	err = irq_set_affinity_hint(irq, priv->irq_info[i].mask);
	if (err) {
		mlx5_core_warn(mdev, "irq_set_affinity_hint failed,irq 0x%.4x",
			       irq);
		goto err_clear_mask;
	}

	return 0;

err_clear_mask:
	free_cpumask_var(priv->irq_info[i].mask);
	return err;
}

static void mlx5_irq_clear_affinity_hint(struct mlx5_core_dev *mdev, int i)
{
	struct mlx5_priv *priv  = &mdev->priv;
	struct msix_entry *msix = priv->msix_arr;
	int irq                 = msix[i + MLX5_EQ_VEC_COMP_BASE].vector;

	irq_set_affinity_hint(irq, NULL);
	free_cpumask_var(priv->irq_info[i].mask);
}

static int mlx5_irq_set_affinity_hints(struct mlx5_core_dev *mdev)
{
	int err;
	int i;

	for (i = 0; i < mdev->priv.eq_table.num_comp_vectors; i++) {
		err = mlx5_irq_set_affinity_hint(mdev, i);
		if (err)
			goto err_out;
	}

	return 0;

err_out:
	for (i--; i >= 0; i--)
		mlx5_irq_clear_affinity_hint(mdev, i);

	return err;
}

static void mlx5_irq_clear_affinity_hints(struct mlx5_core_dev *mdev)
{
	int i;

	for (i = 0; i < mdev->priv.eq_table.num_comp_vectors; i++)
		mlx5_irq_clear_affinity_hint(mdev, i);
}

int mlx5_vector2eqn(struct mlx5_core_dev *dev, int vector, int *eqn, int *irqn)
{
	struct mlx5_eq_table *table = &dev->priv.eq_table;
	struct mlx5_eq *eq, *n;
	int err = -ENOENT;

	spin_lock(&table->lock);
	list_for_each_entry_safe(eq, n, &table->comp_eqs_list, list) {
		if (eq->index == vector) {
			*eqn = eq->eqn;
			*irqn = eq->irqn;
			err = 0;
			break;
		}
	}
	spin_unlock(&table->lock);

	return err;
}
EXPORT_SYMBOL(mlx5_vector2eqn);

static void free_comp_eqs(struct mlx5_core_dev *dev)
{
	struct mlx5_eq_table *table = &dev->priv.eq_table;
	struct mlx5_eq *eq, *n;

	spin_lock(&table->lock);
	list_for_each_entry_safe(eq, n, &table->comp_eqs_list, list) {
		list_del(&eq->list);
		spin_unlock(&table->lock);
		if (mlx5_destroy_unmap_eq(dev, eq))
			mlx5_core_warn(dev, "failed to destroy EQ 0x%x\n",
				       eq->eqn);
		kfree(eq);
		spin_lock(&table->lock);
	}
	spin_unlock(&table->lock);
}

static int alloc_comp_eqs(struct mlx5_core_dev *dev)
{
	struct mlx5_eq_table *table = &dev->priv.eq_table;
	char name[MLX5_MAX_IRQ_NAME];
	struct mlx5_eq *eq;
	int ncomp_vec;
	int nent;
	int err;
	int i;

	INIT_LIST_HEAD(&table->comp_eqs_list);
	ncomp_vec = table->num_comp_vectors;
	nent = MLX5_COMP_EQ_SIZE;
	for (i = 0; i < ncomp_vec; i++) {
		eq = kzalloc(sizeof(*eq), GFP_KERNEL);
		if (!eq) {
			err = -ENOMEM;
			goto clean;
		}

		snprintf(name, MLX5_MAX_IRQ_NAME, "mlx5_comp%d", i);
		err = mlx5_create_map_eq(dev, eq,
					 i + MLX5_EQ_VEC_COMP_BASE, nent, 0,
					 name, &dev->priv.uuari.uars[0]);
		if (err) {
			kfree(eq);
			goto clean;
		}
		mlx5_core_dbg(dev, "allocated completion EQN %d\n", eq->eqn);
		eq->index = i;
		spin_lock(&table->lock);
		list_add_tail(&eq->list, &table->comp_eqs_list);
		spin_unlock(&table->lock);
	}

	return 0;

clean:
	free_comp_eqs(dev);
	return err;
}

#ifdef CONFIG_MLX5_CORE_EN
static int mlx5_core_set_issi(struct mlx5_core_dev *dev)
{
	u32 query_in[MLX5_ST_SZ_DW(query_issi_in)];
	u32 query_out[MLX5_ST_SZ_DW(query_issi_out)];
	u32 set_in[MLX5_ST_SZ_DW(set_issi_in)];
	u32 set_out[MLX5_ST_SZ_DW(set_issi_out)];
	int err;
	u32 sup_issi;

	memset(query_in, 0, sizeof(query_in));
	memset(query_out, 0, sizeof(query_out));

	MLX5_SET(query_issi_in, query_in, opcode, MLX5_CMD_OP_QUERY_ISSI);

	err = mlx5_cmd_exec_check_status(dev, query_in, sizeof(query_in),
					 query_out, sizeof(query_out));
	if (err) {
		if (((struct mlx5_outbox_hdr *)query_out)->status ==
		    MLX5_CMD_STAT_BAD_OP_ERR) {
			pr_debug("Only ISSI 0 is supported\n");
			return 0;
		}

		pr_err("failed to query ISSI\n");
		return err;
	}

	sup_issi = MLX5_GET(query_issi_out, query_out, supported_issi_dw0);

	if (sup_issi & (1 << 1)) {
		memset(set_in, 0, sizeof(set_in));
		memset(set_out, 0, sizeof(set_out));

		MLX5_SET(set_issi_in, set_in, opcode, MLX5_CMD_OP_SET_ISSI);
		MLX5_SET(set_issi_in, set_in, current_issi, 1);

		err = mlx5_cmd_exec_check_status(dev, set_in, sizeof(set_in),
						 set_out, sizeof(set_out));
		if (err) {
			pr_err("failed to set ISSI=1\n");
			return err;
		}

		dev->issi = 1;

		return 0;
	} else if (sup_issi & (1 << 0) || !sup_issi) {
		return 0;
	}

	return -ENOTSUPP;
}
#endif

static int map_bf_area(struct mlx5_core_dev *dev)
{
	resource_size_t bf_start = pci_resource_start(dev->pdev, 0);
	resource_size_t bf_len = pci_resource_len(dev->pdev, 0);

	dev->priv.bf_mapping = io_mapping_create_wc(bf_start, bf_len);

	return dev->priv.bf_mapping ? 0 : -ENOMEM;
}

static void unmap_bf_area(struct mlx5_core_dev *dev)
{
	if (dev->priv.bf_mapping)
		io_mapping_free(dev->priv.bf_mapping);
}

static void mlx5_add_device(struct mlx5_interface *intf, struct mlx5_priv *priv)
{
	struct mlx5_device_context *dev_ctx;
	struct mlx5_core_dev *dev = container_of(priv, struct mlx5_core_dev, priv);

	dev_ctx = kmalloc(sizeof(*dev_ctx), GFP_KERNEL);
	if (!dev_ctx)
		return;

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

void *mlx5_get_protocol_dev(struct mlx5_core_dev *mdev, int protocol)
{
	struct mlx5_priv *priv = &mdev->priv;
	struct mlx5_device_context *dev_ctx;
	unsigned long flags;
	void *result = NULL;

	spin_lock_irqsave(&priv->ctx_lock, flags);

	list_for_each_entry(dev_ctx, &mdev->priv.ctx_list, list)
		if ((dev_ctx->intf->protocol == protocol) &&
		    dev_ctx->intf->get_dev) {
			result = dev_ctx->intf->get_dev(dev_ctx->context);
			break;
		}

	spin_unlock_irqrestore(&priv->ctx_lock, flags);

	return result;
}
EXPORT_SYMBOL(mlx5_get_protocol_dev);

static int mlx5_pci_init(struct mlx5_core_dev *dev, struct mlx5_priv *priv)
{
	struct pci_dev *pdev = dev->pdev;
	int err = 0;

	pci_set_drvdata(dev->pdev, dev);
	strncpy(priv->name, dev_name(&pdev->dev), MLX5_MAX_NAME_LEN);
	priv->name[MLX5_MAX_NAME_LEN - 1] = 0;

	mutex_init(&priv->pgdir_mutex);
	INIT_LIST_HEAD(&priv->pgdir_list);
	spin_lock_init(&priv->mkey_lock);

	mutex_init(&priv->alloc_mutex);

	priv->numa_node = dev_to_node(&dev->pdev->dev);

	priv->dbg_root = debugfs_create_dir(dev_name(&pdev->dev), mlx5_debugfs_root);
	if (!priv->dbg_root)
		return -ENOMEM;

	err = mlx5_pci_enable_device(dev);
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

	return 0;

err_clr_master:
	pci_clear_master(dev->pdev);
	release_bar(dev->pdev);
err_disable:
	mlx5_pci_disable_device(dev);

err_dbg:
	debugfs_remove(priv->dbg_root);
	return err;
}

static void mlx5_pci_close(struct mlx5_core_dev *dev, struct mlx5_priv *priv)
{
	iounmap(dev->iseg);
	pci_clear_master(dev->pdev);
	release_bar(dev->pdev);
	mlx5_pci_disable_device(dev);
	debugfs_remove(priv->dbg_root);
}

#define MLX5_IB_MOD "mlx5_ib"
static int mlx5_load_one(struct mlx5_core_dev *dev, struct mlx5_priv *priv)
{
	struct pci_dev *pdev = dev->pdev;
	int err;

	mutex_lock(&dev->intf_state_mutex);
	if (dev->interface_state == MLX5_INTERFACE_STATE_UP) {
		dev_warn(&dev->pdev->dev, "%s: interface is up, NOP\n",
			 __func__);
		goto out;
	}

	dev_info(&pdev->dev, "firmware version: %d.%d.%d\n", fw_rev_maj(dev),
		 fw_rev_min(dev), fw_rev_sub(dev));

	/* on load removing any previous indication of internal error, device is
	 * up
	 */
	dev->state = MLX5_DEVICE_STATE_UP;

	err = mlx5_cmd_init(dev);
	if (err) {
		dev_err(&pdev->dev, "Failed initializing command interface, aborting\n");
		goto out_err;
	}

	err = wait_fw_init(dev, FW_INIT_TIMEOUT_MILI);
	if (err) {
		dev_err(&dev->pdev->dev, "Firmware over %d MS in initializing state, aborting\n",
			FW_INIT_TIMEOUT_MILI);
		goto out_err;
	}

	mlx5_pagealloc_init(dev);

	err = mlx5_core_enable_hca(dev, 0);
	if (err) {
		dev_err(&pdev->dev, "enable hca failed\n");
		goto err_pagealloc_cleanup;
	}

#ifdef CONFIG_MLX5_CORE_EN
	err = mlx5_core_set_issi(dev);
	if (err) {
		dev_err(&pdev->dev, "failed to set issi\n");
		goto err_disable_hca;
	}
#endif

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

	err = mlx5_query_hca_caps(dev);
	if (err) {
		dev_err(&pdev->dev, "query hca failed\n");
		goto err_stop_poll;
	}

	err = mlx5_query_board_id(dev);
	if (err) {
		dev_err(&pdev->dev, "query board id failed\n");
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

	err = alloc_comp_eqs(dev);
	if (err) {
		dev_err(&pdev->dev, "Failed to alloc completion EQs\n");
		goto err_stop_eqs;
	}

	if (map_bf_area(dev))
		dev_err(&pdev->dev, "Failed to map blue flame area\n");

	err = mlx5_irq_set_affinity_hints(dev);
	if (err) {
		dev_err(&pdev->dev, "Failed to alloc affinity hint cpumask\n");
		goto err_unmap_bf_area;
	}

	MLX5_INIT_DOORBELL_LOCK(&priv->cq_uar_lock);

	mlx5_init_cq_table(dev);
	mlx5_init_qp_table(dev);
	mlx5_init_srq_table(dev);
	mlx5_init_mr_table(dev);

#ifdef CONFIG_MLX5_CORE_EN
	err = mlx5_eswitch_init(dev);
	if (err) {
		dev_err(&pdev->dev, "eswitch init failed %d\n", err);
		goto err_reg_dev;
	}
#endif

	err = mlx5_sriov_init(dev);
	if (err) {
		dev_err(&pdev->dev, "sriov init failed %d\n", err);
		goto err_sriov;
	}

	err = mlx5_register_device(dev);
	if (err) {
		dev_err(&pdev->dev, "mlx5_register_device failed %d\n", err);
		goto err_reg_dev;
	}

	err = request_module_nowait(MLX5_IB_MOD);
	if (err)
		pr_info("failed request module on %s\n", MLX5_IB_MOD);

	dev->interface_state = MLX5_INTERFACE_STATE_UP;
out:
	mutex_unlock(&dev->intf_state_mutex);

	return 0;

err_sriov:
	if (mlx5_sriov_cleanup(dev))
		dev_err(&dev->pdev->dev, "sriov cleanup failed\n");

#ifdef CONFIG_MLX5_CORE_EN
	mlx5_eswitch_cleanup(dev->priv.eswitch);
#endif
err_reg_dev:
	mlx5_cleanup_mr_table(dev);
	mlx5_cleanup_srq_table(dev);
	mlx5_cleanup_qp_table(dev);
	mlx5_cleanup_cq_table(dev);
	mlx5_irq_clear_affinity_hints(dev);

err_unmap_bf_area:
	unmap_bf_area(dev);

	free_comp_eqs(dev);

err_stop_eqs:
	mlx5_stop_eqs(dev);

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
		goto out_err;
	}

err_pagealloc_stop:
	mlx5_pagealloc_stop(dev);

reclaim_boot_pages:
	mlx5_reclaim_startup_pages(dev);

err_disable_hca:
	mlx5_core_disable_hca(dev, 0);

err_pagealloc_cleanup:
	mlx5_pagealloc_cleanup(dev);
	mlx5_cmd_cleanup(dev);

out_err:
	dev->state = MLX5_DEVICE_STATE_INTERNAL_ERROR;
	mutex_unlock(&dev->intf_state_mutex);

	return err;
}

static int mlx5_unload_one(struct mlx5_core_dev *dev, struct mlx5_priv *priv)
{
	int err = 0;

	err = mlx5_sriov_cleanup(dev);
	if (err) {
		dev_warn(&dev->pdev->dev, "%s: sriov cleanup failed - abort\n",
			 __func__);
		return err;
	}

	mutex_lock(&dev->intf_state_mutex);
	if (dev->interface_state == MLX5_INTERFACE_STATE_DOWN) {
		dev_warn(&dev->pdev->dev, "%s: interface is down, NOP\n",
			 __func__);
		goto out;
	}
	mlx5_unregister_device(dev);
#ifdef CONFIG_MLX5_CORE_EN
	mlx5_eswitch_cleanup(dev->priv.eswitch);
#endif

	mlx5_cleanup_mr_table(dev);
	mlx5_cleanup_srq_table(dev);
	mlx5_cleanup_qp_table(dev);
	mlx5_cleanup_cq_table(dev);
	mlx5_irq_clear_affinity_hints(dev);
	unmap_bf_area(dev);
	free_comp_eqs(dev);
	mlx5_stop_eqs(dev);
	mlx5_free_uuars(dev, &priv->uuari);
	mlx5_eq_cleanup(dev);
	mlx5_disable_msix(dev);
	mlx5_stop_health_poll(dev);
	err = mlx5_cmd_teardown_hca(dev);
	if (err) {
		dev_err(&dev->pdev->dev, "tear_down_hca failed, skip cleanup\n");
		goto out;
	}
	mlx5_pagealloc_stop(dev);
	mlx5_reclaim_startup_pages(dev);
	mlx5_core_disable_hca(dev, 0);
	mlx5_pagealloc_cleanup(dev);
	mlx5_cmd_cleanup(dev);

out:
	dev->interface_state = MLX5_INTERFACE_STATE_DOWN;
	mutex_unlock(&dev->intf_state_mutex);
	return err;
}

void mlx5_core_event(struct mlx5_core_dev *dev, enum mlx5_dev_event event,
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
	priv->pci_dev_data = id->driver_data;

	pci_set_drvdata(pdev, dev);

	if (prof_sel < 0 || prof_sel >= ARRAY_SIZE(profile)) {
		pr_warn("selected profile out of range, selecting default (%d)\n",
			MLX5_DEFAULT_PROF);
		prof_sel = MLX5_DEFAULT_PROF;
	}
	dev->profile = &profile[prof_sel];
	dev->pdev = pdev;
	dev->event = mlx5_core_event;

	INIT_LIST_HEAD(&priv->ctx_list);
	spin_lock_init(&priv->ctx_lock);
	mutex_init(&dev->pci_status_mutex);
	mutex_init(&dev->intf_state_mutex);
	err = mlx5_pci_init(dev, priv);
	if (err) {
		dev_err(&pdev->dev, "mlx5_pci_init failed with error code %d\n", err);
		goto clean_dev;
	}

	err = mlx5_health_init(dev);
	if (err) {
		dev_err(&pdev->dev, "mlx5_health_init failed with error code %d\n", err);
		goto close_pci;
	}

	err = mlx5_load_one(dev, priv);
	if (err) {
		dev_err(&pdev->dev, "mlx5_load_one failed with error code %d\n", err);
		goto clean_health;
	}

	return 0;

clean_health:
	mlx5_health_cleanup(dev);
close_pci:
	mlx5_pci_close(dev, priv);
clean_dev:
	pci_set_drvdata(pdev, NULL);
	kfree(dev);

	return err;
}

static void remove_one(struct pci_dev *pdev)
{
	struct mlx5_core_dev *dev  = pci_get_drvdata(pdev);
	struct mlx5_priv *priv = &dev->priv;

	if (mlx5_unload_one(dev, priv)) {
		dev_err(&dev->pdev->dev, "mlx5_unload_one failed\n");
		mlx5_health_cleanup(dev);
		return;
	}
	mlx5_health_cleanup(dev);
	mlx5_pci_close(dev, priv);
	pci_set_drvdata(pdev, NULL);
	kfree(dev);
}

static pci_ers_result_t mlx5_pci_err_detected(struct pci_dev *pdev,
					      pci_channel_state_t state)
{
	struct mlx5_core_dev *dev = pci_get_drvdata(pdev);
	struct mlx5_priv *priv = &dev->priv;

	dev_info(&pdev->dev, "%s was called\n", __func__);
	mlx5_enter_error_state(dev);
	mlx5_unload_one(dev, priv);
	mlx5_pci_disable_device(dev);
	return state == pci_channel_io_perm_failure ?
		PCI_ERS_RESULT_DISCONNECT : PCI_ERS_RESULT_NEED_RESET;
}

static pci_ers_result_t mlx5_pci_slot_reset(struct pci_dev *pdev)
{
	struct mlx5_core_dev *dev = pci_get_drvdata(pdev);
	int err = 0;

	dev_info(&pdev->dev, "%s was called\n", __func__);

	err = mlx5_pci_enable_device(dev);
	if (err) {
		dev_err(&pdev->dev, "%s: mlx5_pci_enable_device failed with error code: %d\n"
			, __func__, err);
		return PCI_ERS_RESULT_DISCONNECT;
	}
	pci_set_master(pdev);
	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);

	return err ? PCI_ERS_RESULT_DISCONNECT : PCI_ERS_RESULT_RECOVERED;
}

void mlx5_disable_device(struct mlx5_core_dev *dev)
{
	mlx5_pci_err_detected(dev->pdev, 0);
}

/* wait for the device to show vital signs. For now we check
 * that we can read the device ID and that the health buffer
 * shows a non zero value which is different than 0xffffffff
 */
static void wait_vital(struct pci_dev *pdev)
{
	struct mlx5_core_dev *dev = pci_get_drvdata(pdev);
	struct mlx5_core_health *health = &dev->priv.health;
	const int niter = 100;
	u32 count;
	u16 did;
	int i;

	/* Wait for firmware to be ready after reset */
	msleep(1000);
	for (i = 0; i < niter; i++) {
		if (pci_read_config_word(pdev, 2, &did)) {
			dev_warn(&pdev->dev, "failed reading config word\n");
			break;
		}
		if (did == pdev->device) {
			dev_info(&pdev->dev, "device ID correctly read after %d iterations\n", i);
			break;
		}
		msleep(50);
	}
	if (i == niter)
		dev_warn(&pdev->dev, "%s-%d: could not read device ID\n", __func__, __LINE__);

	for (i = 0; i < niter; i++) {
		count = ioread32be(health->health_counter);
		if (count && count != 0xffffffff) {
			dev_info(&pdev->dev, "Counter value 0x%x after %d iterations\n", count, i);
			break;
		}
		msleep(50);
	}

	if (i == niter)
		dev_warn(&pdev->dev, "%s-%d: could not read device ID\n", __func__, __LINE__);
}

static void mlx5_pci_resume(struct pci_dev *pdev)
{
	struct mlx5_core_dev *dev = pci_get_drvdata(pdev);
	struct mlx5_priv *priv = &dev->priv;
	int err;

	dev_info(&pdev->dev, "%s was called\n", __func__);

	pci_save_state(pdev);
	wait_vital(pdev);

	err = mlx5_load_one(dev, priv);
	if (err)
		dev_err(&pdev->dev, "%s: mlx5_load_one failed with error code: %d\n"
			, __func__, err);
	else
		dev_info(&pdev->dev, "%s: device recovered\n", __func__);
}

static const struct pci_error_handlers mlx5_err_handler = {
	.error_detected = mlx5_pci_err_detected,
	.slot_reset	= mlx5_pci_slot_reset,
	.resume		= mlx5_pci_resume
};

static const struct pci_device_id mlx5_core_pci_table[] = {
	{ PCI_VDEVICE(MELLANOX, 0x1011) },			/* Connect-IB */
	{ PCI_VDEVICE(MELLANOX, 0x1012), MLX5_PCI_DEV_IS_VF},	/* Connect-IB VF */
	{ PCI_VDEVICE(MELLANOX, 0x1013) },			/* ConnectX-4 */
	{ PCI_VDEVICE(MELLANOX, 0x1014), MLX5_PCI_DEV_IS_VF},	/* ConnectX-4 VF */
	{ PCI_VDEVICE(MELLANOX, 0x1015) },			/* ConnectX-4LX */
	{ PCI_VDEVICE(MELLANOX, 0x1016), MLX5_PCI_DEV_IS_VF},	/* ConnectX-4LX VF */
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, mlx5_core_pci_table);

static struct pci_driver mlx5_core_driver = {
	.name           = DRIVER_NAME,
	.id_table       = mlx5_core_pci_table,
	.probe          = init_one,
	.remove         = remove_one,
	.err_handler	= &mlx5_err_handler,
	.sriov_configure   = mlx5_core_sriov_configure,
};

static int __init init(void)
{
	int err;

	mlx5_register_debugfs();

	err = pci_register_driver(&mlx5_core_driver);
	if (err)
		goto err_debug;

#ifdef CONFIG_MLX5_CORE_EN
	mlx5e_init();
#endif

	return 0;

err_debug:
	mlx5_unregister_debugfs();
	return err;
}

static void __exit cleanup(void)
{
#ifdef CONFIG_MLX5_CORE_EN
	mlx5e_cleanup();
#endif
	pci_unregister_driver(&mlx5_core_driver);
	mlx5_unregister_debugfs();
}

module_init(init);
module_exit(cleanup);
