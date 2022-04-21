// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */

#include "cmd.h"

int mlx5vf_cmd_suspend_vhca(struct pci_dev *pdev, u16 vhca_id, u16 op_mod)
{
	struct mlx5_core_dev *mdev = mlx5_vf_get_core_dev(pdev);
	u32 out[MLX5_ST_SZ_DW(suspend_vhca_out)] = {};
	u32 in[MLX5_ST_SZ_DW(suspend_vhca_in)] = {};
	int ret;

	if (!mdev)
		return -ENOTCONN;

	MLX5_SET(suspend_vhca_in, in, opcode, MLX5_CMD_OP_SUSPEND_VHCA);
	MLX5_SET(suspend_vhca_in, in, vhca_id, vhca_id);
	MLX5_SET(suspend_vhca_in, in, op_mod, op_mod);

	ret = mlx5_cmd_exec_inout(mdev, suspend_vhca, in, out);
	mlx5_vf_put_core_dev(mdev);
	return ret;
}

int mlx5vf_cmd_resume_vhca(struct pci_dev *pdev, u16 vhca_id, u16 op_mod)
{
	struct mlx5_core_dev *mdev = mlx5_vf_get_core_dev(pdev);
	u32 out[MLX5_ST_SZ_DW(resume_vhca_out)] = {};
	u32 in[MLX5_ST_SZ_DW(resume_vhca_in)] = {};
	int ret;

	if (!mdev)
		return -ENOTCONN;

	MLX5_SET(resume_vhca_in, in, opcode, MLX5_CMD_OP_RESUME_VHCA);
	MLX5_SET(resume_vhca_in, in, vhca_id, vhca_id);
	MLX5_SET(resume_vhca_in, in, op_mod, op_mod);

	ret = mlx5_cmd_exec_inout(mdev, resume_vhca, in, out);
	mlx5_vf_put_core_dev(mdev);
	return ret;
}

int mlx5vf_cmd_query_vhca_migration_state(struct pci_dev *pdev, u16 vhca_id,
					  size_t *state_size)
{
	struct mlx5_core_dev *mdev = mlx5_vf_get_core_dev(pdev);
	u32 out[MLX5_ST_SZ_DW(query_vhca_migration_state_out)] = {};
	u32 in[MLX5_ST_SZ_DW(query_vhca_migration_state_in)] = {};
	int ret;

	if (!mdev)
		return -ENOTCONN;

	MLX5_SET(query_vhca_migration_state_in, in, opcode,
		 MLX5_CMD_OP_QUERY_VHCA_MIGRATION_STATE);
	MLX5_SET(query_vhca_migration_state_in, in, vhca_id, vhca_id);
	MLX5_SET(query_vhca_migration_state_in, in, op_mod, 0);

	ret = mlx5_cmd_exec_inout(mdev, query_vhca_migration_state, in, out);
	if (ret)
		goto end;

	*state_size = MLX5_GET(query_vhca_migration_state_out, out,
			       required_umem_size);

end:
	mlx5_vf_put_core_dev(mdev);
	return ret;
}

int mlx5vf_cmd_get_vhca_id(struct pci_dev *pdev, u16 function_id, u16 *vhca_id)
{
	struct mlx5_core_dev *mdev = mlx5_vf_get_core_dev(pdev);
	u32 in[MLX5_ST_SZ_DW(query_hca_cap_in)] = {};
	int out_size;
	void *out;
	int ret;

	if (!mdev)
		return -ENOTCONN;

	out_size = MLX5_ST_SZ_BYTES(query_hca_cap_out);
	out = kzalloc(out_size, GFP_KERNEL);
	if (!out) {
		ret = -ENOMEM;
		goto end;
	}

	MLX5_SET(query_hca_cap_in, in, opcode, MLX5_CMD_OP_QUERY_HCA_CAP);
	MLX5_SET(query_hca_cap_in, in, other_function, 1);
	MLX5_SET(query_hca_cap_in, in, function_id, function_id);
	MLX5_SET(query_hca_cap_in, in, op_mod,
		 MLX5_SET_HCA_CAP_OP_MOD_GENERAL_DEVICE << 1 |
		 HCA_CAP_OPMOD_GET_CUR);

	ret = mlx5_cmd_exec_inout(mdev, query_hca_cap, in, out);
	if (ret)
		goto err_exec;

	*vhca_id = MLX5_GET(query_hca_cap_out, out,
			    capability.cmd_hca_cap.vhca_id);

err_exec:
	kfree(out);
end:
	mlx5_vf_put_core_dev(mdev);
	return ret;
}

static int _create_state_mkey(struct mlx5_core_dev *mdev, u32 pdn,
			      struct mlx5_vf_migration_file *migf, u32 *mkey)
{
	size_t npages = DIV_ROUND_UP(migf->total_length, PAGE_SIZE);
	struct sg_dma_page_iter dma_iter;
	int err = 0, inlen;
	__be64 *mtt;
	void *mkc;
	u32 *in;

	inlen = MLX5_ST_SZ_BYTES(create_mkey_in) +
		sizeof(*mtt) * round_up(npages, 2);

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(create_mkey_in, in, translations_octword_actual_size,
		 DIV_ROUND_UP(npages, 2));
	mtt = (__be64 *)MLX5_ADDR_OF(create_mkey_in, in, klm_pas_mtt);

	for_each_sgtable_dma_page(&migf->table.sgt, &dma_iter, 0)
		*mtt++ = cpu_to_be64(sg_page_iter_dma_address(&dma_iter));

	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);
	MLX5_SET(mkc, mkc, access_mode_1_0, MLX5_MKC_ACCESS_MODE_MTT);
	MLX5_SET(mkc, mkc, lr, 1);
	MLX5_SET(mkc, mkc, lw, 1);
	MLX5_SET(mkc, mkc, rr, 1);
	MLX5_SET(mkc, mkc, rw, 1);
	MLX5_SET(mkc, mkc, pd, pdn);
	MLX5_SET(mkc, mkc, bsf_octword_size, 0);
	MLX5_SET(mkc, mkc, qpn, 0xffffff);
	MLX5_SET(mkc, mkc, log_page_size, PAGE_SHIFT);
	MLX5_SET(mkc, mkc, translations_octword_size, DIV_ROUND_UP(npages, 2));
	MLX5_SET64(mkc, mkc, len, migf->total_length);
	err = mlx5_core_create_mkey(mdev, mkey, in, inlen);
	kvfree(in);
	return err;
}

int mlx5vf_cmd_save_vhca_state(struct pci_dev *pdev, u16 vhca_id,
			       struct mlx5_vf_migration_file *migf)
{
	struct mlx5_core_dev *mdev = mlx5_vf_get_core_dev(pdev);
	u32 out[MLX5_ST_SZ_DW(save_vhca_state_out)] = {};
	u32 in[MLX5_ST_SZ_DW(save_vhca_state_in)] = {};
	u32 pdn, mkey;
	int err;

	if (!mdev)
		return -ENOTCONN;

	err = mlx5_core_alloc_pd(mdev, &pdn);
	if (err)
		goto end;

	err = dma_map_sgtable(mdev->device, &migf->table.sgt, DMA_FROM_DEVICE,
			      0);
	if (err)
		goto err_dma_map;

	err = _create_state_mkey(mdev, pdn, migf, &mkey);
	if (err)
		goto err_create_mkey;

	MLX5_SET(save_vhca_state_in, in, opcode,
		 MLX5_CMD_OP_SAVE_VHCA_STATE);
	MLX5_SET(save_vhca_state_in, in, op_mod, 0);
	MLX5_SET(save_vhca_state_in, in, vhca_id, vhca_id);
	MLX5_SET(save_vhca_state_in, in, mkey, mkey);
	MLX5_SET(save_vhca_state_in, in, size, migf->total_length);

	err = mlx5_cmd_exec_inout(mdev, save_vhca_state, in, out);
	if (err)
		goto err_exec;

	migf->total_length =
		MLX5_GET(save_vhca_state_out, out, actual_image_size);

	mlx5_core_destroy_mkey(mdev, mkey);
	mlx5_core_dealloc_pd(mdev, pdn);
	dma_unmap_sgtable(mdev->device, &migf->table.sgt, DMA_FROM_DEVICE, 0);
	mlx5_vf_put_core_dev(mdev);

	return 0;

err_exec:
	mlx5_core_destroy_mkey(mdev, mkey);
err_create_mkey:
	dma_unmap_sgtable(mdev->device, &migf->table.sgt, DMA_FROM_DEVICE, 0);
err_dma_map:
	mlx5_core_dealloc_pd(mdev, pdn);
end:
	mlx5_vf_put_core_dev(mdev);
	return err;
}

int mlx5vf_cmd_load_vhca_state(struct pci_dev *pdev, u16 vhca_id,
			       struct mlx5_vf_migration_file *migf)
{
	struct mlx5_core_dev *mdev = mlx5_vf_get_core_dev(pdev);
	u32 out[MLX5_ST_SZ_DW(save_vhca_state_out)] = {};
	u32 in[MLX5_ST_SZ_DW(save_vhca_state_in)] = {};
	u32 pdn, mkey;
	int err;

	if (!mdev)
		return -ENOTCONN;

	mutex_lock(&migf->lock);
	if (!migf->total_length) {
		err = -EINVAL;
		goto end;
	}

	err = mlx5_core_alloc_pd(mdev, &pdn);
	if (err)
		goto end;

	err = dma_map_sgtable(mdev->device, &migf->table.sgt, DMA_TO_DEVICE, 0);
	if (err)
		goto err_reg;

	err = _create_state_mkey(mdev, pdn, migf, &mkey);
	if (err)
		goto err_mkey;

	MLX5_SET(load_vhca_state_in, in, opcode,
		 MLX5_CMD_OP_LOAD_VHCA_STATE);
	MLX5_SET(load_vhca_state_in, in, op_mod, 0);
	MLX5_SET(load_vhca_state_in, in, vhca_id, vhca_id);
	MLX5_SET(load_vhca_state_in, in, mkey, mkey);
	MLX5_SET(load_vhca_state_in, in, size, migf->total_length);

	err = mlx5_cmd_exec_inout(mdev, load_vhca_state, in, out);

	mlx5_core_destroy_mkey(mdev, mkey);
err_mkey:
	dma_unmap_sgtable(mdev->device, &migf->table.sgt, DMA_TO_DEVICE, 0);
err_reg:
	mlx5_core_dealloc_pd(mdev, pdn);
end:
	mlx5_vf_put_core_dev(mdev);
	mutex_unlock(&migf->lock);
	return err;
}
