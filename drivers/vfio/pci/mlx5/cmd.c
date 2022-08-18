// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */

#include "cmd.h"

static int mlx5vf_cmd_get_vhca_id(struct mlx5_core_dev *mdev, u16 function_id,
				  u16 *vhca_id);

int mlx5vf_cmd_suspend_vhca(struct mlx5vf_pci_core_device *mvdev, u16 op_mod)
{
	u32 out[MLX5_ST_SZ_DW(suspend_vhca_out)] = {};
	u32 in[MLX5_ST_SZ_DW(suspend_vhca_in)] = {};

	lockdep_assert_held(&mvdev->state_mutex);
	if (mvdev->mdev_detach)
		return -ENOTCONN;

	MLX5_SET(suspend_vhca_in, in, opcode, MLX5_CMD_OP_SUSPEND_VHCA);
	MLX5_SET(suspend_vhca_in, in, vhca_id, mvdev->vhca_id);
	MLX5_SET(suspend_vhca_in, in, op_mod, op_mod);

	return mlx5_cmd_exec_inout(mvdev->mdev, suspend_vhca, in, out);
}

int mlx5vf_cmd_resume_vhca(struct mlx5vf_pci_core_device *mvdev, u16 op_mod)
{
	u32 out[MLX5_ST_SZ_DW(resume_vhca_out)] = {};
	u32 in[MLX5_ST_SZ_DW(resume_vhca_in)] = {};

	lockdep_assert_held(&mvdev->state_mutex);
	if (mvdev->mdev_detach)
		return -ENOTCONN;

	MLX5_SET(resume_vhca_in, in, opcode, MLX5_CMD_OP_RESUME_VHCA);
	MLX5_SET(resume_vhca_in, in, vhca_id, mvdev->vhca_id);
	MLX5_SET(resume_vhca_in, in, op_mod, op_mod);

	return mlx5_cmd_exec_inout(mvdev->mdev, resume_vhca, in, out);
}

int mlx5vf_cmd_query_vhca_migration_state(struct mlx5vf_pci_core_device *mvdev,
					  size_t *state_size)
{
	u32 out[MLX5_ST_SZ_DW(query_vhca_migration_state_out)] = {};
	u32 in[MLX5_ST_SZ_DW(query_vhca_migration_state_in)] = {};
	int ret;

	lockdep_assert_held(&mvdev->state_mutex);
	if (mvdev->mdev_detach)
		return -ENOTCONN;

	MLX5_SET(query_vhca_migration_state_in, in, opcode,
		 MLX5_CMD_OP_QUERY_VHCA_MIGRATION_STATE);
	MLX5_SET(query_vhca_migration_state_in, in, vhca_id, mvdev->vhca_id);
	MLX5_SET(query_vhca_migration_state_in, in, op_mod, 0);

	ret = mlx5_cmd_exec_inout(mvdev->mdev, query_vhca_migration_state, in,
				  out);
	if (ret)
		return ret;

	*state_size = MLX5_GET(query_vhca_migration_state_out, out,
			       required_umem_size);
	return 0;
}

static int mlx5fv_vf_event(struct notifier_block *nb,
			   unsigned long event, void *data)
{
	struct mlx5vf_pci_core_device *mvdev =
		container_of(nb, struct mlx5vf_pci_core_device, nb);

	mutex_lock(&mvdev->state_mutex);
	switch (event) {
	case MLX5_PF_NOTIFY_ENABLE_VF:
		mvdev->mdev_detach = false;
		break;
	case MLX5_PF_NOTIFY_DISABLE_VF:
		mlx5vf_disable_fds(mvdev);
		mvdev->mdev_detach = true;
		break;
	default:
		break;
	}
	mlx5vf_state_mutex_unlock(mvdev);
	return 0;
}

void mlx5vf_cmd_close_migratable(struct mlx5vf_pci_core_device *mvdev)
{
	if (!mvdev->migrate_cap)
		return;

	mutex_lock(&mvdev->state_mutex);
	mlx5vf_disable_fds(mvdev);
	mlx5vf_state_mutex_unlock(mvdev);
}

void mlx5vf_cmd_remove_migratable(struct mlx5vf_pci_core_device *mvdev)
{
	if (!mvdev->migrate_cap)
		return;

	mlx5_sriov_blocking_notifier_unregister(mvdev->mdev, mvdev->vf_id,
						&mvdev->nb);
	destroy_workqueue(mvdev->cb_wq);
}

void mlx5vf_cmd_set_migratable(struct mlx5vf_pci_core_device *mvdev,
			       const struct vfio_migration_ops *mig_ops)
{
	struct pci_dev *pdev = mvdev->core_device.pdev;
	int ret;

	if (!pdev->is_virtfn)
		return;

	mvdev->mdev = mlx5_vf_get_core_dev(pdev);
	if (!mvdev->mdev)
		return;

	if (!MLX5_CAP_GEN(mvdev->mdev, migration))
		goto end;

	mvdev->vf_id = pci_iov_vf_id(pdev);
	if (mvdev->vf_id < 0)
		goto end;

	if (mlx5vf_cmd_get_vhca_id(mvdev->mdev, mvdev->vf_id + 1,
				   &mvdev->vhca_id))
		goto end;

	mvdev->cb_wq = alloc_ordered_workqueue("mlx5vf_wq", 0);
	if (!mvdev->cb_wq)
		goto end;

	mutex_init(&mvdev->state_mutex);
	spin_lock_init(&mvdev->reset_lock);
	mvdev->nb.notifier_call = mlx5fv_vf_event;
	ret = mlx5_sriov_blocking_notifier_register(mvdev->mdev, mvdev->vf_id,
						    &mvdev->nb);
	if (ret) {
		destroy_workqueue(mvdev->cb_wq);
		goto end;
	}

	mvdev->migrate_cap = 1;
	mvdev->core_device.vdev.migration_flags =
		VFIO_MIGRATION_STOP_COPY |
		VFIO_MIGRATION_P2P;
	mvdev->core_device.vdev.mig_ops = mig_ops;

end:
	mlx5_vf_put_core_dev(mvdev->mdev);
}

static int mlx5vf_cmd_get_vhca_id(struct mlx5_core_dev *mdev, u16 function_id,
				  u16 *vhca_id)
{
	u32 in[MLX5_ST_SZ_DW(query_hca_cap_in)] = {};
	int out_size;
	void *out;
	int ret;

	out_size = MLX5_ST_SZ_BYTES(query_hca_cap_out);
	out = kzalloc(out_size, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

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

void mlx5vf_mig_file_cleanup_cb(struct work_struct *_work)
{
	struct mlx5vf_async_data *async_data = container_of(_work,
		struct mlx5vf_async_data, work);
	struct mlx5_vf_migration_file *migf = container_of(async_data,
		struct mlx5_vf_migration_file, async_data);
	struct mlx5_core_dev *mdev = migf->mvdev->mdev;

	mutex_lock(&migf->lock);
	if (async_data->status) {
		migf->is_err = true;
		wake_up_interruptible(&migf->poll_wait);
	}
	mutex_unlock(&migf->lock);

	mlx5_core_destroy_mkey(mdev, async_data->mkey);
	dma_unmap_sgtable(mdev->device, &migf->table.sgt, DMA_FROM_DEVICE, 0);
	mlx5_core_dealloc_pd(mdev, async_data->pdn);
	kvfree(async_data->out);
	fput(migf->filp);
}

static void mlx5vf_save_callback(int status, struct mlx5_async_work *context)
{
	struct mlx5vf_async_data *async_data = container_of(context,
			struct mlx5vf_async_data, cb_work);
	struct mlx5_vf_migration_file *migf = container_of(async_data,
			struct mlx5_vf_migration_file, async_data);

	if (!status) {
		WRITE_ONCE(migf->total_length,
			   MLX5_GET(save_vhca_state_out, async_data->out,
				    actual_image_size));
		wake_up_interruptible(&migf->poll_wait);
	}

	/*
	 * The error and the cleanup flows can't run from an
	 * interrupt context
	 */
	async_data->status = status;
	queue_work(migf->mvdev->cb_wq, &async_data->work);
}

int mlx5vf_cmd_save_vhca_state(struct mlx5vf_pci_core_device *mvdev,
			       struct mlx5_vf_migration_file *migf)
{
	u32 out_size = MLX5_ST_SZ_BYTES(save_vhca_state_out);
	u32 in[MLX5_ST_SZ_DW(save_vhca_state_in)] = {};
	struct mlx5vf_async_data *async_data;
	struct mlx5_core_dev *mdev;
	u32 pdn, mkey;
	int err;

	lockdep_assert_held(&mvdev->state_mutex);
	if (mvdev->mdev_detach)
		return -ENOTCONN;

	mdev = mvdev->mdev;
	err = mlx5_core_alloc_pd(mdev, &pdn);
	if (err)
		return err;

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
	MLX5_SET(save_vhca_state_in, in, vhca_id, mvdev->vhca_id);
	MLX5_SET(save_vhca_state_in, in, mkey, mkey);
	MLX5_SET(save_vhca_state_in, in, size, migf->total_length);

	async_data = &migf->async_data;
	async_data->out = kvzalloc(out_size, GFP_KERNEL);
	if (!async_data->out) {
		err = -ENOMEM;
		goto err_out;
	}

	/* no data exists till the callback comes back */
	migf->total_length = 0;
	get_file(migf->filp);
	async_data->mkey = mkey;
	async_data->pdn = pdn;
	err = mlx5_cmd_exec_cb(&migf->async_ctx, in, sizeof(in),
			       async_data->out,
			       out_size, mlx5vf_save_callback,
			       &async_data->cb_work);
	if (err)
		goto err_exec;

	return 0;

err_exec:
	fput(migf->filp);
	kvfree(async_data->out);
err_out:
	mlx5_core_destroy_mkey(mdev, mkey);
err_create_mkey:
	dma_unmap_sgtable(mdev->device, &migf->table.sgt, DMA_FROM_DEVICE, 0);
err_dma_map:
	mlx5_core_dealloc_pd(mdev, pdn);
	return err;
}

int mlx5vf_cmd_load_vhca_state(struct mlx5vf_pci_core_device *mvdev,
			       struct mlx5_vf_migration_file *migf)
{
	struct mlx5_core_dev *mdev;
	u32 out[MLX5_ST_SZ_DW(save_vhca_state_out)] = {};
	u32 in[MLX5_ST_SZ_DW(save_vhca_state_in)] = {};
	u32 pdn, mkey;
	int err;

	lockdep_assert_held(&mvdev->state_mutex);
	if (mvdev->mdev_detach)
		return -ENOTCONN;

	mutex_lock(&migf->lock);
	if (!migf->total_length) {
		err = -EINVAL;
		goto end;
	}

	mdev = mvdev->mdev;
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
	MLX5_SET(load_vhca_state_in, in, vhca_id, mvdev->vhca_id);
	MLX5_SET(load_vhca_state_in, in, mkey, mkey);
	MLX5_SET(load_vhca_state_in, in, size, migf->total_length);

	err = mlx5_cmd_exec_inout(mdev, load_vhca_state, in, out);

	mlx5_core_destroy_mkey(mdev, mkey);
err_mkey:
	dma_unmap_sgtable(mdev->device, &migf->table.sgt, DMA_TO_DEVICE, 0);
err_reg:
	mlx5_core_dealloc_pd(mdev, pdn);
end:
	mutex_unlock(&migf->lock);
	return err;
}
