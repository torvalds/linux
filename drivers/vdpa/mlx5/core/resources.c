// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2020 Mellanox Technologies Ltd. */

#include <linux/iova.h>
#include <linux/mlx5/driver.h>
#include "mlx5_vdpa.h"

static int alloc_pd(struct mlx5_vdpa_dev *dev, u32 *pdn, u16 uid)
{
	struct mlx5_core_dev *mdev = dev->mdev;

	u32 out[MLX5_ST_SZ_DW(alloc_pd_out)] = {};
	u32 in[MLX5_ST_SZ_DW(alloc_pd_in)] = {};
	int err;

	MLX5_SET(alloc_pd_in, in, opcode, MLX5_CMD_OP_ALLOC_PD);
	MLX5_SET(alloc_pd_in, in, uid, uid);

	err = mlx5_cmd_exec_inout(mdev, alloc_pd, in, out);
	if (!err)
		*pdn = MLX5_GET(alloc_pd_out, out, pd);

	return err;
}

static int dealloc_pd(struct mlx5_vdpa_dev *dev, u32 pdn, u16 uid)
{
	u32 in[MLX5_ST_SZ_DW(dealloc_pd_in)] = {};
	struct mlx5_core_dev *mdev = dev->mdev;

	MLX5_SET(dealloc_pd_in, in, opcode, MLX5_CMD_OP_DEALLOC_PD);
	MLX5_SET(dealloc_pd_in, in, pd, pdn);
	MLX5_SET(dealloc_pd_in, in, uid, uid);
	return mlx5_cmd_exec_in(mdev, dealloc_pd, in);
}

static int get_null_mkey(struct mlx5_vdpa_dev *dev, u32 *null_mkey)
{
	u32 out[MLX5_ST_SZ_DW(query_special_contexts_out)] = {};
	u32 in[MLX5_ST_SZ_DW(query_special_contexts_in)] = {};
	struct mlx5_core_dev *mdev = dev->mdev;
	int err;

	MLX5_SET(query_special_contexts_in, in, opcode, MLX5_CMD_OP_QUERY_SPECIAL_CONTEXTS);
	err = mlx5_cmd_exec_inout(mdev, query_special_contexts, in, out);
	if (!err)
		*null_mkey = MLX5_GET(query_special_contexts_out, out, null_mkey);
	return err;
}

static int create_uctx(struct mlx5_vdpa_dev *mvdev, u16 *uid)
{
	u32 out[MLX5_ST_SZ_DW(create_uctx_out)] = {};
	int inlen;
	void *in;
	int err;

	if (MLX5_CAP_GEN(mvdev->mdev, umem_uid_0))
		return 0;

	/* 0 means not supported */
	if (!MLX5_CAP_GEN(mvdev->mdev, log_max_uctx))
		return -EOPNOTSUPP;

	inlen = MLX5_ST_SZ_BYTES(create_uctx_in);
	in = kzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(create_uctx_in, in, opcode, MLX5_CMD_OP_CREATE_UCTX);
	MLX5_SET(create_uctx_in, in, uctx.cap, MLX5_UCTX_CAP_RAW_TX);

	err = mlx5_cmd_exec(mvdev->mdev, in, inlen, out, sizeof(out));
	kfree(in);
	if (!err)
		*uid = MLX5_GET(create_uctx_out, out, uid);

	return err;
}

static void destroy_uctx(struct mlx5_vdpa_dev *mvdev, u32 uid)
{
	u32 out[MLX5_ST_SZ_DW(destroy_uctx_out)] = {};
	u32 in[MLX5_ST_SZ_DW(destroy_uctx_in)] = {};

	if (!uid)
		return;

	MLX5_SET(destroy_uctx_in, in, opcode, MLX5_CMD_OP_DESTROY_UCTX);
	MLX5_SET(destroy_uctx_in, in, uid, uid);

	mlx5_cmd_exec(mvdev->mdev, in, sizeof(in), out, sizeof(out));
}

int mlx5_vdpa_create_tis(struct mlx5_vdpa_dev *mvdev, void *in, u32 *tisn)
{
	u32 out[MLX5_ST_SZ_DW(create_tis_out)] = {};
	int err;

	MLX5_SET(create_tis_in, in, opcode, MLX5_CMD_OP_CREATE_TIS);
	MLX5_SET(create_tis_in, in, uid, mvdev->res.uid);
	err = mlx5_cmd_exec_inout(mvdev->mdev, create_tis, in, out);
	if (!err)
		*tisn = MLX5_GET(create_tis_out, out, tisn);

	return err;
}

void mlx5_vdpa_destroy_tis(struct mlx5_vdpa_dev *mvdev, u32 tisn)
{
	u32 in[MLX5_ST_SZ_DW(destroy_tis_in)] = {};

	MLX5_SET(destroy_tis_in, in, opcode, MLX5_CMD_OP_DESTROY_TIS);
	MLX5_SET(destroy_tis_in, in, uid, mvdev->res.uid);
	MLX5_SET(destroy_tis_in, in, tisn, tisn);
	mlx5_cmd_exec_in(mvdev->mdev, destroy_tis, in);
}

int mlx5_vdpa_create_rqt(struct mlx5_vdpa_dev *mvdev, void *in, int inlen, u32 *rqtn)
{
	u32 out[MLX5_ST_SZ_DW(create_rqt_out)] = {};
	int err;

	MLX5_SET(create_rqt_in, in, opcode, MLX5_CMD_OP_CREATE_RQT);
	err = mlx5_cmd_exec(mvdev->mdev, in, inlen, out, sizeof(out));
	if (!err)
		*rqtn = MLX5_GET(create_rqt_out, out, rqtn);

	return err;
}

int mlx5_vdpa_modify_rqt(struct mlx5_vdpa_dev *mvdev, void *in, int inlen, u32 rqtn)
{
	u32 out[MLX5_ST_SZ_DW(create_rqt_out)] = {};

	MLX5_SET(modify_rqt_in, in, uid, mvdev->res.uid);
	MLX5_SET(modify_rqt_in, in, rqtn, rqtn);
	MLX5_SET(modify_rqt_in, in, opcode, MLX5_CMD_OP_MODIFY_RQT);
	return mlx5_cmd_exec(mvdev->mdev, in, inlen, out, sizeof(out));
}

void mlx5_vdpa_destroy_rqt(struct mlx5_vdpa_dev *mvdev, u32 rqtn)
{
	u32 in[MLX5_ST_SZ_DW(destroy_rqt_in)] = {};

	MLX5_SET(destroy_rqt_in, in, opcode, MLX5_CMD_OP_DESTROY_RQT);
	MLX5_SET(destroy_rqt_in, in, uid, mvdev->res.uid);
	MLX5_SET(destroy_rqt_in, in, rqtn, rqtn);
	mlx5_cmd_exec_in(mvdev->mdev, destroy_rqt, in);
}

int mlx5_vdpa_create_tir(struct mlx5_vdpa_dev *mvdev, void *in, u32 *tirn)
{
	u32 out[MLX5_ST_SZ_DW(create_tir_out)] = {};
	int err;

	MLX5_SET(create_tir_in, in, opcode, MLX5_CMD_OP_CREATE_TIR);
	err = mlx5_cmd_exec_inout(mvdev->mdev, create_tir, in, out);
	if (!err)
		*tirn = MLX5_GET(create_tir_out, out, tirn);

	return err;
}

void mlx5_vdpa_destroy_tir(struct mlx5_vdpa_dev *mvdev, u32 tirn)
{
	u32 in[MLX5_ST_SZ_DW(destroy_tir_in)] = {};

	MLX5_SET(destroy_tir_in, in, opcode, MLX5_CMD_OP_DESTROY_TIR);
	MLX5_SET(destroy_tir_in, in, uid, mvdev->res.uid);
	MLX5_SET(destroy_tir_in, in, tirn, tirn);
	mlx5_cmd_exec_in(mvdev->mdev, destroy_tir, in);
}

int mlx5_vdpa_alloc_transport_domain(struct mlx5_vdpa_dev *mvdev, u32 *tdn)
{
	u32 out[MLX5_ST_SZ_DW(alloc_transport_domain_out)] = {};
	u32 in[MLX5_ST_SZ_DW(alloc_transport_domain_in)] = {};
	int err;

	MLX5_SET(alloc_transport_domain_in, in, opcode, MLX5_CMD_OP_ALLOC_TRANSPORT_DOMAIN);
	MLX5_SET(alloc_transport_domain_in, in, uid, mvdev->res.uid);

	err = mlx5_cmd_exec_inout(mvdev->mdev, alloc_transport_domain, in, out);
	if (!err)
		*tdn = MLX5_GET(alloc_transport_domain_out, out, transport_domain);

	return err;
}

void mlx5_vdpa_dealloc_transport_domain(struct mlx5_vdpa_dev *mvdev, u32 tdn)
{
	u32 in[MLX5_ST_SZ_DW(dealloc_transport_domain_in)] = {};

	MLX5_SET(dealloc_transport_domain_in, in, opcode, MLX5_CMD_OP_DEALLOC_TRANSPORT_DOMAIN);
	MLX5_SET(dealloc_transport_domain_in, in, uid, mvdev->res.uid);
	MLX5_SET(dealloc_transport_domain_in, in, transport_domain, tdn);
	mlx5_cmd_exec_in(mvdev->mdev, dealloc_transport_domain, in);
}

int mlx5_vdpa_create_mkey(struct mlx5_vdpa_dev *mvdev, struct mlx5_core_mkey *mkey, u32 *in,
			  int inlen)
{
	u32 lout[MLX5_ST_SZ_DW(create_mkey_out)] = {};
	u32 mkey_index;
	void *mkc;
	int err;

	MLX5_SET(create_mkey_in, in, opcode, MLX5_CMD_OP_CREATE_MKEY);
	MLX5_SET(create_mkey_in, in, uid, mvdev->res.uid);

	err = mlx5_cmd_exec(mvdev->mdev, in, inlen, lout, sizeof(lout));
	if (err)
		return err;

	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);
	mkey_index = MLX5_GET(create_mkey_out, lout, mkey_index);
	mkey->key |= mlx5_idx_to_mkey(mkey_index);
	mkey->pd = MLX5_GET(mkc, mkc, pd);
	return 0;
}

int mlx5_vdpa_destroy_mkey(struct mlx5_vdpa_dev *mvdev, struct mlx5_core_mkey *mkey)
{
	u32 in[MLX5_ST_SZ_DW(destroy_mkey_in)] = {};

	MLX5_SET(destroy_mkey_in, in, uid, mvdev->res.uid);
	MLX5_SET(destroy_mkey_in, in, opcode, MLX5_CMD_OP_DESTROY_MKEY);
	MLX5_SET(destroy_mkey_in, in, mkey_index, mlx5_mkey_to_idx(mkey->key));
	return mlx5_cmd_exec_in(mvdev->mdev, destroy_mkey, in);
}

static int init_ctrl_vq(struct mlx5_vdpa_dev *mvdev)
{
	mvdev->cvq.iotlb = vhost_iotlb_alloc(0, 0);
	if (!mvdev->cvq.iotlb)
		return -ENOMEM;

	vringh_set_iotlb(&mvdev->cvq.vring, mvdev->cvq.iotlb, &mvdev->cvq.iommu_lock);

	return 0;
}

static void cleanup_ctrl_vq(struct mlx5_vdpa_dev *mvdev)
{
	vhost_iotlb_free(mvdev->cvq.iotlb);
}

int mlx5_vdpa_alloc_resources(struct mlx5_vdpa_dev *mvdev)
{
	u64 offset = MLX5_CAP64_DEV_VDPA_EMULATION(mvdev->mdev, doorbell_bar_offset);
	struct mlx5_vdpa_resources *res = &mvdev->res;
	struct mlx5_core_dev *mdev = mvdev->mdev;
	u64 kick_addr;
	int err;

	if (res->valid) {
		mlx5_vdpa_warn(mvdev, "resources already allocated\n");
		return -EINVAL;
	}
	mutex_init(&mvdev->mr.mkey_mtx);
	res->uar = mlx5_get_uars_page(mdev);
	if (IS_ERR(res->uar)) {
		err = PTR_ERR(res->uar);
		goto err_uars;
	}

	err = create_uctx(mvdev, &res->uid);
	if (err)
		goto err_uctx;

	err = alloc_pd(mvdev, &res->pdn, res->uid);
	if (err)
		goto err_pd;

	err = get_null_mkey(mvdev, &res->null_mkey);
	if (err)
		goto err_key;

	kick_addr = mdev->bar_addr + offset;
	res->phys_kick_addr = kick_addr;

	res->kick_addr = ioremap(kick_addr, PAGE_SIZE);
	if (!res->kick_addr) {
		err = -ENOMEM;
		goto err_key;
	}

	err = init_ctrl_vq(mvdev);
	if (err)
		goto err_ctrl;

	res->valid = true;

	return 0;

err_ctrl:
	iounmap(res->kick_addr);
err_key:
	dealloc_pd(mvdev, res->pdn, res->uid);
err_pd:
	destroy_uctx(mvdev, res->uid);
err_uctx:
	mlx5_put_uars_page(mdev, res->uar);
err_uars:
	mutex_destroy(&mvdev->mr.mkey_mtx);
	return err;
}

void mlx5_vdpa_free_resources(struct mlx5_vdpa_dev *mvdev)
{
	struct mlx5_vdpa_resources *res = &mvdev->res;

	if (!res->valid)
		return;

	cleanup_ctrl_vq(mvdev);
	iounmap(res->kick_addr);
	res->kick_addr = NULL;
	dealloc_pd(mvdev, res->pdn, res->uid);
	destroy_uctx(mvdev, res->uid);
	mlx5_put_uars_page(mvdev->mdev, res->uar);
	mutex_destroy(&mvdev->mr.mkey_mtx);
	res->valid = false;
}
