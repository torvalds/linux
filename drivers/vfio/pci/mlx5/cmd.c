// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */

#include "cmd.h"

enum { CQ_OK = 0, CQ_EMPTY = -1, CQ_POLL_ERR = -2 };

static int mlx5vf_is_migratable(struct mlx5_core_dev *mdev, u16 func_id)
{
	int query_sz = MLX5_ST_SZ_BYTES(query_hca_cap_out);
	void *query_cap = NULL, *cap;
	int ret;

	query_cap = kzalloc(query_sz, GFP_KERNEL);
	if (!query_cap)
		return -ENOMEM;

	ret = mlx5_vport_get_other_func_cap(mdev, func_id, query_cap,
					    MLX5_CAP_GENERAL_2);
	if (ret)
		goto out;

	cap = MLX5_ADDR_OF(query_hca_cap_out, query_cap, capability);
	if (!MLX5_GET(cmd_hca_cap_2, cap, migratable))
		ret = -EOPNOTSUPP;
out:
	kfree(query_cap);
	return ret;
}

static int mlx5vf_cmd_get_vhca_id(struct mlx5_core_dev *mdev, u16 function_id,
				  u16 *vhca_id);
static void
_mlx5vf_free_page_tracker_resources(struct mlx5vf_pci_core_device *mvdev);

int mlx5vf_cmd_suspend_vhca(struct mlx5vf_pci_core_device *mvdev, u16 op_mod)
{
	struct mlx5_vf_migration_file *migf = mvdev->saving_migf;
	u32 out[MLX5_ST_SZ_DW(suspend_vhca_out)] = {};
	u32 in[MLX5_ST_SZ_DW(suspend_vhca_in)] = {};
	int err;

	lockdep_assert_held(&mvdev->state_mutex);
	if (mvdev->mdev_detach)
		return -ENOTCONN;

	/*
	 * In case PRE_COPY is used, saving_migf is exposed while the device is
	 * running. Make sure to run only once there is no active save command.
	 * Running both in parallel, might end-up with a failure in the save
	 * command once it will try to turn on 'tracking' on a suspended device.
	 */
	if (migf) {
		err = wait_for_completion_interruptible(&migf->save_comp);
		if (err)
			return err;
	}

	MLX5_SET(suspend_vhca_in, in, opcode, MLX5_CMD_OP_SUSPEND_VHCA);
	MLX5_SET(suspend_vhca_in, in, vhca_id, mvdev->vhca_id);
	MLX5_SET(suspend_vhca_in, in, op_mod, op_mod);

	err = mlx5_cmd_exec_inout(mvdev->mdev, suspend_vhca, in, out);
	if (migf)
		complete(&migf->save_comp);

	return err;
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
					  size_t *state_size, u64 *total_size,
					  u8 query_flags)
{
	u32 out[MLX5_ST_SZ_DW(query_vhca_migration_state_out)] = {};
	u32 in[MLX5_ST_SZ_DW(query_vhca_migration_state_in)] = {};
	bool inc = query_flags & MLX5VF_QUERY_INC;
	int ret;

	lockdep_assert_held(&mvdev->state_mutex);
	if (mvdev->mdev_detach)
		return -ENOTCONN;

	/*
	 * In case PRE_COPY is used, saving_migf is exposed while device is
	 * running. Make sure to run only once there is no active save command.
	 * Running both in parallel, might end-up with a failure in the
	 * incremental query command on un-tracked vhca.
	 */
	if (inc) {
		ret = wait_for_completion_interruptible(&mvdev->saving_migf->save_comp);
		if (ret)
			return ret;
		if (mvdev->saving_migf->state ==
		    MLX5_MIGF_STATE_PRE_COPY_ERROR) {
			/*
			 * In case we had a PRE_COPY error, only query full
			 * image for final image
			 */
			if (!(query_flags & MLX5VF_QUERY_FINAL)) {
				*state_size = 0;
				complete(&mvdev->saving_migf->save_comp);
				return 0;
			}
			query_flags &= ~MLX5VF_QUERY_INC;
		}
	}

	MLX5_SET(query_vhca_migration_state_in, in, opcode,
		 MLX5_CMD_OP_QUERY_VHCA_MIGRATION_STATE);
	MLX5_SET(query_vhca_migration_state_in, in, vhca_id, mvdev->vhca_id);
	MLX5_SET(query_vhca_migration_state_in, in, op_mod, 0);
	MLX5_SET(query_vhca_migration_state_in, in, incremental,
		 query_flags & MLX5VF_QUERY_INC);
	MLX5_SET(query_vhca_migration_state_in, in, chunk, mvdev->chunk_mode);

	ret = mlx5_cmd_exec_inout(mvdev->mdev, query_vhca_migration_state, in,
				  out);
	if (inc)
		complete(&mvdev->saving_migf->save_comp);

	if (ret)
		return ret;

	*state_size = MLX5_GET(query_vhca_migration_state_out, out,
			       required_umem_size);
	if (total_size)
		*total_size = mvdev->chunk_mode ?
			MLX5_GET64(query_vhca_migration_state_out, out,
				   remaining_total_size) : *state_size;

	return 0;
}

static void set_tracker_error(struct mlx5vf_pci_core_device *mvdev)
{
	/* Mark the tracker under an error and wake it up if it's running */
	mvdev->tracker.is_err = true;
	complete(&mvdev->tracker_comp);
}

static int mlx5fv_vf_event(struct notifier_block *nb,
			   unsigned long event, void *data)
{
	struct mlx5vf_pci_core_device *mvdev =
		container_of(nb, struct mlx5vf_pci_core_device, nb);

	switch (event) {
	case MLX5_PF_NOTIFY_ENABLE_VF:
		mutex_lock(&mvdev->state_mutex);
		mvdev->mdev_detach = false;
		mlx5vf_state_mutex_unlock(mvdev);
		break;
	case MLX5_PF_NOTIFY_DISABLE_VF:
		mlx5vf_cmd_close_migratable(mvdev);
		mutex_lock(&mvdev->state_mutex);
		mvdev->mdev_detach = true;
		mlx5vf_state_mutex_unlock(mvdev);
		break;
	default:
		break;
	}

	return 0;
}

void mlx5vf_cmd_close_migratable(struct mlx5vf_pci_core_device *mvdev)
{
	if (!mvdev->migrate_cap)
		return;

	/* Must be done outside the lock to let it progress */
	set_tracker_error(mvdev);
	mutex_lock(&mvdev->state_mutex);
	mlx5vf_disable_fds(mvdev);
	_mlx5vf_free_page_tracker_resources(mvdev);
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
			       const struct vfio_migration_ops *mig_ops,
			       const struct vfio_log_ops *log_ops)
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

	ret = mlx5vf_is_migratable(mvdev->mdev, mvdev->vf_id + 1);
	if (ret)
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
	init_completion(&mvdev->tracker_comp);
	if (MLX5_CAP_GEN(mvdev->mdev, adv_virtualization))
		mvdev->core_device.vdev.log_ops = log_ops;

	if (MLX5_CAP_GEN_2(mvdev->mdev, migration_multi_load) &&
	    MLX5_CAP_GEN_2(mvdev->mdev, migration_tracking_state))
		mvdev->core_device.vdev.migration_flags |=
			VFIO_MIGRATION_PRE_COPY;

	if (MLX5_CAP_GEN_2(mvdev->mdev, migration_in_chunks))
		mvdev->chunk_mode = 1;

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

static int _create_mkey(struct mlx5_core_dev *mdev, u32 pdn,
			struct mlx5_vhca_data_buffer *buf,
			struct mlx5_vhca_recv_buf *recv_buf,
			u32 *mkey)
{
	size_t npages = buf ? DIV_ROUND_UP(buf->allocated_length, PAGE_SIZE) :
				recv_buf->npages;
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

	if (buf) {
		struct sg_dma_page_iter dma_iter;

		for_each_sgtable_dma_page(&buf->table.sgt, &dma_iter, 0)
			*mtt++ = cpu_to_be64(sg_page_iter_dma_address(&dma_iter));
	} else {
		int i;

		for (i = 0; i < npages; i++)
			*mtt++ = cpu_to_be64(recv_buf->dma_addrs[i]);
	}

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
	MLX5_SET64(mkc, mkc, len, npages * PAGE_SIZE);
	err = mlx5_core_create_mkey(mdev, mkey, in, inlen);
	kvfree(in);
	return err;
}

static int mlx5vf_dma_data_buffer(struct mlx5_vhca_data_buffer *buf)
{
	struct mlx5vf_pci_core_device *mvdev = buf->migf->mvdev;
	struct mlx5_core_dev *mdev = mvdev->mdev;
	int ret;

	lockdep_assert_held(&mvdev->state_mutex);
	if (mvdev->mdev_detach)
		return -ENOTCONN;

	if (buf->dmaed || !buf->allocated_length)
		return -EINVAL;

	ret = dma_map_sgtable(mdev->device, &buf->table.sgt, buf->dma_dir, 0);
	if (ret)
		return ret;

	ret = _create_mkey(mdev, buf->migf->pdn, buf, NULL, &buf->mkey);
	if (ret)
		goto err;

	buf->dmaed = true;

	return 0;
err:
	dma_unmap_sgtable(mdev->device, &buf->table.sgt, buf->dma_dir, 0);
	return ret;
}

void mlx5vf_free_data_buffer(struct mlx5_vhca_data_buffer *buf)
{
	struct mlx5_vf_migration_file *migf = buf->migf;
	struct sg_page_iter sg_iter;

	lockdep_assert_held(&migf->mvdev->state_mutex);
	WARN_ON(migf->mvdev->mdev_detach);

	if (buf->dmaed) {
		mlx5_core_destroy_mkey(migf->mvdev->mdev, buf->mkey);
		dma_unmap_sgtable(migf->mvdev->mdev->device, &buf->table.sgt,
				  buf->dma_dir, 0);
	}

	/* Undo alloc_pages_bulk_array() */
	for_each_sgtable_page(&buf->table.sgt, &sg_iter, 0)
		__free_page(sg_page_iter_page(&sg_iter));
	sg_free_append_table(&buf->table);
	kfree(buf);
}

struct mlx5_vhca_data_buffer *
mlx5vf_alloc_data_buffer(struct mlx5_vf_migration_file *migf,
			 size_t length,
			 enum dma_data_direction dma_dir)
{
	struct mlx5_vhca_data_buffer *buf;
	int ret;

	buf = kzalloc(sizeof(*buf), GFP_KERNEL_ACCOUNT);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	buf->dma_dir = dma_dir;
	buf->migf = migf;
	if (length) {
		ret = mlx5vf_add_migration_pages(buf,
				DIV_ROUND_UP_ULL(length, PAGE_SIZE));
		if (ret)
			goto end;

		if (dma_dir != DMA_NONE) {
			ret = mlx5vf_dma_data_buffer(buf);
			if (ret)
				goto end;
		}
	}

	return buf;
end:
	mlx5vf_free_data_buffer(buf);
	return ERR_PTR(ret);
}

void mlx5vf_put_data_buffer(struct mlx5_vhca_data_buffer *buf)
{
	spin_lock_irq(&buf->migf->list_lock);
	buf->stop_copy_chunk_num = 0;
	list_add_tail(&buf->buf_elm, &buf->migf->avail_list);
	spin_unlock_irq(&buf->migf->list_lock);
}

struct mlx5_vhca_data_buffer *
mlx5vf_get_data_buffer(struct mlx5_vf_migration_file *migf,
		       size_t length, enum dma_data_direction dma_dir)
{
	struct mlx5_vhca_data_buffer *buf, *temp_buf;
	struct list_head free_list;

	lockdep_assert_held(&migf->mvdev->state_mutex);
	if (migf->mvdev->mdev_detach)
		return ERR_PTR(-ENOTCONN);

	INIT_LIST_HEAD(&free_list);

	spin_lock_irq(&migf->list_lock);
	list_for_each_entry_safe(buf, temp_buf, &migf->avail_list, buf_elm) {
		if (buf->dma_dir == dma_dir) {
			list_del_init(&buf->buf_elm);
			if (buf->allocated_length >= length) {
				spin_unlock_irq(&migf->list_lock);
				goto found;
			}
			/*
			 * Prevent holding redundant buffers. Put in a free
			 * list and call at the end not under the spin lock
			 * (&migf->list_lock) to mlx5vf_free_data_buffer which
			 * might sleep.
			 */
			list_add(&buf->buf_elm, &free_list);
		}
	}
	spin_unlock_irq(&migf->list_lock);
	buf = mlx5vf_alloc_data_buffer(migf, length, dma_dir);

found:
	while ((temp_buf = list_first_entry_or_null(&free_list,
				struct mlx5_vhca_data_buffer, buf_elm))) {
		list_del(&temp_buf->buf_elm);
		mlx5vf_free_data_buffer(temp_buf);
	}

	return buf;
}

static void
mlx5vf_save_callback_complete(struct mlx5_vf_migration_file *migf,
			      struct mlx5vf_async_data *async_data)
{
	kvfree(async_data->out);
	complete(&migf->save_comp);
	fput(migf->filp);
}

void mlx5vf_mig_file_cleanup_cb(struct work_struct *_work)
{
	struct mlx5vf_async_data *async_data = container_of(_work,
		struct mlx5vf_async_data, work);
	struct mlx5_vf_migration_file *migf = container_of(async_data,
		struct mlx5_vf_migration_file, async_data);

	mutex_lock(&migf->lock);
	if (async_data->status) {
		mlx5vf_put_data_buffer(async_data->buf);
		if (async_data->header_buf)
			mlx5vf_put_data_buffer(async_data->header_buf);
		if (!async_data->stop_copy_chunk &&
		    async_data->status == MLX5_CMD_STAT_BAD_RES_STATE_ERR)
			migf->state = MLX5_MIGF_STATE_PRE_COPY_ERROR;
		else
			migf->state = MLX5_MIGF_STATE_ERROR;
		wake_up_interruptible(&migf->poll_wait);
	}
	mutex_unlock(&migf->lock);
	mlx5vf_save_callback_complete(migf, async_data);
}

static int add_buf_header(struct mlx5_vhca_data_buffer *header_buf,
			  size_t image_size, bool initial_pre_copy)
{
	struct mlx5_vf_migration_file *migf = header_buf->migf;
	struct mlx5_vf_migration_header header = {};
	unsigned long flags;
	struct page *page;
	u8 *to_buff;

	header.record_size = cpu_to_le64(image_size);
	header.flags = cpu_to_le32(MLX5_MIGF_HEADER_FLAGS_TAG_MANDATORY);
	header.tag = cpu_to_le32(MLX5_MIGF_HEADER_TAG_FW_DATA);
	page = mlx5vf_get_migration_page(header_buf, 0);
	if (!page)
		return -EINVAL;
	to_buff = kmap_local_page(page);
	memcpy(to_buff, &header, sizeof(header));
	kunmap_local(to_buff);
	header_buf->length = sizeof(header);
	header_buf->start_pos = header_buf->migf->max_pos;
	migf->max_pos += header_buf->length;
	spin_lock_irqsave(&migf->list_lock, flags);
	list_add_tail(&header_buf->buf_elm, &migf->buf_list);
	spin_unlock_irqrestore(&migf->list_lock, flags);
	if (initial_pre_copy)
		migf->pre_copy_initial_bytes += sizeof(header);
	return 0;
}

static void mlx5vf_save_callback(int status, struct mlx5_async_work *context)
{
	struct mlx5vf_async_data *async_data = container_of(context,
			struct mlx5vf_async_data, cb_work);
	struct mlx5_vf_migration_file *migf = container_of(async_data,
			struct mlx5_vf_migration_file, async_data);

	if (!status) {
		size_t next_required_umem_size = 0;
		bool stop_copy_last_chunk;
		size_t image_size;
		unsigned long flags;
		bool initial_pre_copy = migf->state != MLX5_MIGF_STATE_PRE_COPY &&
				!async_data->stop_copy_chunk;

		image_size = MLX5_GET(save_vhca_state_out, async_data->out,
				      actual_image_size);
		if (async_data->buf->stop_copy_chunk_num)
			next_required_umem_size = MLX5_GET(save_vhca_state_out,
					async_data->out, next_required_umem_size);
		stop_copy_last_chunk = async_data->stop_copy_chunk &&
				!next_required_umem_size;
		if (async_data->header_buf) {
			status = add_buf_header(async_data->header_buf, image_size,
						initial_pre_copy);
			if (status)
				goto err;
		}
		async_data->buf->length = image_size;
		async_data->buf->start_pos = migf->max_pos;
		migf->max_pos += async_data->buf->length;
		spin_lock_irqsave(&migf->list_lock, flags);
		list_add_tail(&async_data->buf->buf_elm, &migf->buf_list);
		if (async_data->buf->stop_copy_chunk_num) {
			migf->num_ready_chunks++;
			if (next_required_umem_size &&
			    migf->num_ready_chunks >= MAX_NUM_CHUNKS) {
				/* Delay the next SAVE till one chunk be consumed */
				migf->next_required_umem_size = next_required_umem_size;
				next_required_umem_size = 0;
			}
		}
		spin_unlock_irqrestore(&migf->list_lock, flags);
		if (initial_pre_copy) {
			migf->pre_copy_initial_bytes += image_size;
			migf->state = MLX5_MIGF_STATE_PRE_COPY;
		}
		if (stop_copy_last_chunk)
			migf->state = MLX5_MIGF_STATE_COMPLETE;
		wake_up_interruptible(&migf->poll_wait);
		if (next_required_umem_size)
			mlx5vf_mig_file_set_save_work(migf,
				/* Picking up the next chunk num */
				(async_data->buf->stop_copy_chunk_num % MAX_NUM_CHUNKS) + 1,
				next_required_umem_size);
		mlx5vf_save_callback_complete(migf, async_data);
		return;
	}

err:
	/* The error flow can't run from an interrupt context */
	if (status == -EREMOTEIO)
		status = MLX5_GET(save_vhca_state_out, async_data->out, status);
	async_data->status = status;
	queue_work(migf->mvdev->cb_wq, &async_data->work);
}

int mlx5vf_cmd_save_vhca_state(struct mlx5vf_pci_core_device *mvdev,
			       struct mlx5_vf_migration_file *migf,
			       struct mlx5_vhca_data_buffer *buf, bool inc,
			       bool track)
{
	u32 out_size = MLX5_ST_SZ_BYTES(save_vhca_state_out);
	u32 in[MLX5_ST_SZ_DW(save_vhca_state_in)] = {};
	struct mlx5_vhca_data_buffer *header_buf = NULL;
	struct mlx5vf_async_data *async_data;
	int err;

	lockdep_assert_held(&mvdev->state_mutex);
	if (mvdev->mdev_detach)
		return -ENOTCONN;

	err = wait_for_completion_interruptible(&migf->save_comp);
	if (err)
		return err;

	if (migf->state == MLX5_MIGF_STATE_PRE_COPY_ERROR)
		/*
		 * In case we had a PRE_COPY error, SAVE is triggered only for
		 * the final image, read device full image.
		 */
		inc = false;

	MLX5_SET(save_vhca_state_in, in, opcode,
		 MLX5_CMD_OP_SAVE_VHCA_STATE);
	MLX5_SET(save_vhca_state_in, in, op_mod, 0);
	MLX5_SET(save_vhca_state_in, in, vhca_id, mvdev->vhca_id);
	MLX5_SET(save_vhca_state_in, in, mkey, buf->mkey);
	MLX5_SET(save_vhca_state_in, in, size, buf->allocated_length);
	MLX5_SET(save_vhca_state_in, in, incremental, inc);
	MLX5_SET(save_vhca_state_in, in, set_track, track);

	async_data = &migf->async_data;
	async_data->buf = buf;
	async_data->stop_copy_chunk = !track;
	async_data->out = kvzalloc(out_size, GFP_KERNEL);
	if (!async_data->out) {
		err = -ENOMEM;
		goto err_out;
	}

	if (MLX5VF_PRE_COPY_SUPP(mvdev)) {
		if (async_data->stop_copy_chunk) {
			u8 header_idx = buf->stop_copy_chunk_num ?
				buf->stop_copy_chunk_num - 1 : 0;

			header_buf = migf->buf_header[header_idx];
			migf->buf_header[header_idx] = NULL;
		}

		if (!header_buf) {
			header_buf = mlx5vf_get_data_buffer(migf,
				sizeof(struct mlx5_vf_migration_header), DMA_NONE);
			if (IS_ERR(header_buf)) {
				err = PTR_ERR(header_buf);
				goto err_free;
			}
		}
	}

	if (async_data->stop_copy_chunk)
		migf->state = MLX5_MIGF_STATE_SAVE_STOP_COPY_CHUNK;

	async_data->header_buf = header_buf;
	get_file(migf->filp);
	err = mlx5_cmd_exec_cb(&migf->async_ctx, in, sizeof(in),
			       async_data->out,
			       out_size, mlx5vf_save_callback,
			       &async_data->cb_work);
	if (err)
		goto err_exec;

	return 0;

err_exec:
	if (header_buf)
		mlx5vf_put_data_buffer(header_buf);
	fput(migf->filp);
err_free:
	kvfree(async_data->out);
err_out:
	complete(&migf->save_comp);
	return err;
}

int mlx5vf_cmd_load_vhca_state(struct mlx5vf_pci_core_device *mvdev,
			       struct mlx5_vf_migration_file *migf,
			       struct mlx5_vhca_data_buffer *buf)
{
	u32 out[MLX5_ST_SZ_DW(load_vhca_state_out)] = {};
	u32 in[MLX5_ST_SZ_DW(load_vhca_state_in)] = {};
	int err;

	lockdep_assert_held(&mvdev->state_mutex);
	if (mvdev->mdev_detach)
		return -ENOTCONN;

	if (!buf->dmaed) {
		err = mlx5vf_dma_data_buffer(buf);
		if (err)
			return err;
	}

	MLX5_SET(load_vhca_state_in, in, opcode,
		 MLX5_CMD_OP_LOAD_VHCA_STATE);
	MLX5_SET(load_vhca_state_in, in, op_mod, 0);
	MLX5_SET(load_vhca_state_in, in, vhca_id, mvdev->vhca_id);
	MLX5_SET(load_vhca_state_in, in, mkey, buf->mkey);
	MLX5_SET(load_vhca_state_in, in, size, buf->length);
	return mlx5_cmd_exec_inout(mvdev->mdev, load_vhca_state, in, out);
}

int mlx5vf_cmd_alloc_pd(struct mlx5_vf_migration_file *migf)
{
	int err;

	lockdep_assert_held(&migf->mvdev->state_mutex);
	if (migf->mvdev->mdev_detach)
		return -ENOTCONN;

	err = mlx5_core_alloc_pd(migf->mvdev->mdev, &migf->pdn);
	return err;
}

void mlx5vf_cmd_dealloc_pd(struct mlx5_vf_migration_file *migf)
{
	lockdep_assert_held(&migf->mvdev->state_mutex);
	if (migf->mvdev->mdev_detach)
		return;

	mlx5_core_dealloc_pd(migf->mvdev->mdev, migf->pdn);
}

void mlx5fv_cmd_clean_migf_resources(struct mlx5_vf_migration_file *migf)
{
	struct mlx5_vhca_data_buffer *entry;
	int i;

	lockdep_assert_held(&migf->mvdev->state_mutex);
	WARN_ON(migf->mvdev->mdev_detach);

	for (i = 0; i < MAX_NUM_CHUNKS; i++) {
		if (migf->buf[i]) {
			mlx5vf_free_data_buffer(migf->buf[i]);
			migf->buf[i] = NULL;
		}

		if (migf->buf_header[i]) {
			mlx5vf_free_data_buffer(migf->buf_header[i]);
			migf->buf_header[i] = NULL;
		}
	}

	list_splice(&migf->avail_list, &migf->buf_list);

	while ((entry = list_first_entry_or_null(&migf->buf_list,
				struct mlx5_vhca_data_buffer, buf_elm))) {
		list_del(&entry->buf_elm);
		mlx5vf_free_data_buffer(entry);
	}

	mlx5vf_cmd_dealloc_pd(migf);
}

static int mlx5vf_create_tracker(struct mlx5_core_dev *mdev,
				 struct mlx5vf_pci_core_device *mvdev,
				 struct rb_root_cached *ranges, u32 nnodes)
{
	int max_num_range =
		MLX5_CAP_ADV_VIRTUALIZATION(mdev, pg_track_max_num_range);
	struct mlx5_vhca_page_tracker *tracker = &mvdev->tracker;
	int record_size = MLX5_ST_SZ_BYTES(page_track_range);
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)] = {};
	struct interval_tree_node *node = NULL;
	u64 total_ranges_len = 0;
	u32 num_ranges = nnodes;
	u8 log_addr_space_size;
	void *range_list_ptr;
	void *obj_context;
	void *cmd_hdr;
	int inlen;
	void *in;
	int err;
	int i;

	if (num_ranges > max_num_range) {
		vfio_combine_iova_ranges(ranges, nnodes, max_num_range);
		num_ranges = max_num_range;
	}

	inlen = MLX5_ST_SZ_BYTES(create_page_track_obj_in) +
				 record_size * num_ranges;
	in = kzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	cmd_hdr = MLX5_ADDR_OF(create_page_track_obj_in, in,
			       general_obj_in_cmd_hdr);
	MLX5_SET(general_obj_in_cmd_hdr, cmd_hdr, opcode,
		 MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, cmd_hdr, obj_type,
		 MLX5_OBJ_TYPE_PAGE_TRACK);
	obj_context = MLX5_ADDR_OF(create_page_track_obj_in, in, obj_context);
	MLX5_SET(page_track, obj_context, vhca_id, mvdev->vhca_id);
	MLX5_SET(page_track, obj_context, track_type, 1);
	MLX5_SET(page_track, obj_context, log_page_size,
		 ilog2(tracker->host_qp->tracked_page_size));
	MLX5_SET(page_track, obj_context, log_msg_size,
		 ilog2(tracker->host_qp->max_msg_size));
	MLX5_SET(page_track, obj_context, reporting_qpn, tracker->fw_qp->qpn);
	MLX5_SET(page_track, obj_context, num_ranges, num_ranges);

	range_list_ptr = MLX5_ADDR_OF(page_track, obj_context, track_range);
	node = interval_tree_iter_first(ranges, 0, ULONG_MAX);
	for (i = 0; i < num_ranges; i++) {
		void *addr_range_i_base = range_list_ptr + record_size * i;
		unsigned long length = node->last - node->start + 1;

		MLX5_SET64(page_track_range, addr_range_i_base, start_address,
			   node->start);
		MLX5_SET64(page_track_range, addr_range_i_base, length, length);
		total_ranges_len += length;
		node = interval_tree_iter_next(node, 0, ULONG_MAX);
	}

	WARN_ON(node);
	log_addr_space_size = ilog2(roundup_pow_of_two(total_ranges_len));
	if (log_addr_space_size <
	    (MLX5_CAP_ADV_VIRTUALIZATION(mdev, pg_track_log_min_addr_space)) ||
	    log_addr_space_size >
	    (MLX5_CAP_ADV_VIRTUALIZATION(mdev, pg_track_log_max_addr_space))) {
		err = -EOPNOTSUPP;
		goto out;
	}

	MLX5_SET(page_track, obj_context, log_addr_space_size,
		 log_addr_space_size);
	err = mlx5_cmd_exec(mdev, in, inlen, out, sizeof(out));
	if (err)
		goto out;

	tracker->id = MLX5_GET(general_obj_out_cmd_hdr, out, obj_id);
out:
	kfree(in);
	return err;
}

static int mlx5vf_cmd_destroy_tracker(struct mlx5_core_dev *mdev,
				      u32 tracker_id)
{
	u32 in[MLX5_ST_SZ_DW(general_obj_in_cmd_hdr)] = {};
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)] = {};

	MLX5_SET(general_obj_in_cmd_hdr, in, opcode, MLX5_CMD_OP_DESTROY_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_type, MLX5_OBJ_TYPE_PAGE_TRACK);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_id, tracker_id);

	return mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
}

static int mlx5vf_cmd_modify_tracker(struct mlx5_core_dev *mdev,
				     u32 tracker_id, unsigned long iova,
				     unsigned long length, u32 tracker_state)
{
	u32 in[MLX5_ST_SZ_DW(modify_page_track_obj_in)] = {};
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)] = {};
	void *obj_context;
	void *cmd_hdr;

	cmd_hdr = MLX5_ADDR_OF(modify_page_track_obj_in, in, general_obj_in_cmd_hdr);
	MLX5_SET(general_obj_in_cmd_hdr, cmd_hdr, opcode, MLX5_CMD_OP_MODIFY_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, cmd_hdr, obj_type, MLX5_OBJ_TYPE_PAGE_TRACK);
	MLX5_SET(general_obj_in_cmd_hdr, cmd_hdr, obj_id, tracker_id);

	obj_context = MLX5_ADDR_OF(modify_page_track_obj_in, in, obj_context);
	MLX5_SET64(page_track, obj_context, modify_field_select, 0x3);
	MLX5_SET64(page_track, obj_context, range_start_address, iova);
	MLX5_SET64(page_track, obj_context, length, length);
	MLX5_SET(page_track, obj_context, state, tracker_state);

	return mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
}

static int alloc_cq_frag_buf(struct mlx5_core_dev *mdev,
			     struct mlx5_vhca_cq_buf *buf, int nent,
			     int cqe_size)
{
	struct mlx5_frag_buf *frag_buf = &buf->frag_buf;
	u8 log_wq_stride = 6 + (cqe_size == 128 ? 1 : 0);
	u8 log_wq_sz = ilog2(cqe_size);
	int err;

	err = mlx5_frag_buf_alloc_node(mdev, nent * cqe_size, frag_buf,
				       mdev->priv.numa_node);
	if (err)
		return err;

	mlx5_init_fbc(frag_buf->frags, log_wq_stride, log_wq_sz, &buf->fbc);
	buf->cqe_size = cqe_size;
	buf->nent = nent;
	return 0;
}

static void init_cq_frag_buf(struct mlx5_vhca_cq_buf *buf)
{
	struct mlx5_cqe64 *cqe64;
	void *cqe;
	int i;

	for (i = 0; i < buf->nent; i++) {
		cqe = mlx5_frag_buf_get_wqe(&buf->fbc, i);
		cqe64 = buf->cqe_size == 64 ? cqe : cqe + 64;
		cqe64->op_own = MLX5_CQE_INVALID << 4;
	}
}

static void mlx5vf_destroy_cq(struct mlx5_core_dev *mdev,
			      struct mlx5_vhca_cq *cq)
{
	mlx5_core_destroy_cq(mdev, &cq->mcq);
	mlx5_frag_buf_free(mdev, &cq->buf.frag_buf);
	mlx5_db_free(mdev, &cq->db);
}

static void mlx5vf_cq_event(struct mlx5_core_cq *mcq, enum mlx5_event type)
{
	if (type != MLX5_EVENT_TYPE_CQ_ERROR)
		return;

	set_tracker_error(container_of(mcq, struct mlx5vf_pci_core_device,
				       tracker.cq.mcq));
}

static int mlx5vf_event_notifier(struct notifier_block *nb, unsigned long type,
				 void *data)
{
	struct mlx5_vhca_page_tracker *tracker =
		mlx5_nb_cof(nb, struct mlx5_vhca_page_tracker, nb);
	struct mlx5vf_pci_core_device *mvdev = container_of(
		tracker, struct mlx5vf_pci_core_device, tracker);
	struct mlx5_eqe *eqe = data;
	u8 event_type = (u8)type;
	u8 queue_type;
	int qp_num;

	switch (event_type) {
	case MLX5_EVENT_TYPE_WQ_CATAS_ERROR:
	case MLX5_EVENT_TYPE_WQ_ACCESS_ERROR:
	case MLX5_EVENT_TYPE_WQ_INVAL_REQ_ERROR:
		queue_type = eqe->data.qp_srq.type;
		if (queue_type != MLX5_EVENT_QUEUE_TYPE_QP)
			break;
		qp_num = be32_to_cpu(eqe->data.qp_srq.qp_srq_n) & 0xffffff;
		if (qp_num != tracker->host_qp->qpn &&
		    qp_num != tracker->fw_qp->qpn)
			break;
		set_tracker_error(mvdev);
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static void mlx5vf_cq_complete(struct mlx5_core_cq *mcq,
			       struct mlx5_eqe *eqe)
{
	struct mlx5vf_pci_core_device *mvdev =
		container_of(mcq, struct mlx5vf_pci_core_device,
			     tracker.cq.mcq);

	complete(&mvdev->tracker_comp);
}

static int mlx5vf_create_cq(struct mlx5_core_dev *mdev,
			    struct mlx5_vhca_page_tracker *tracker,
			    size_t ncqe)
{
	int cqe_size = cache_line_size() == 128 ? 128 : 64;
	u32 out[MLX5_ST_SZ_DW(create_cq_out)];
	struct mlx5_vhca_cq *cq;
	int inlen, err, eqn;
	void *cqc, *in;
	__be64 *pas;
	int vector;

	cq = &tracker->cq;
	ncqe = roundup_pow_of_two(ncqe);
	err = mlx5_db_alloc_node(mdev, &cq->db, mdev->priv.numa_node);
	if (err)
		return err;

	cq->ncqe = ncqe;
	cq->mcq.set_ci_db = cq->db.db;
	cq->mcq.arm_db = cq->db.db + 1;
	cq->mcq.cqe_sz = cqe_size;
	err = alloc_cq_frag_buf(mdev, &cq->buf, ncqe, cqe_size);
	if (err)
		goto err_db_free;

	init_cq_frag_buf(&cq->buf);
	inlen = MLX5_ST_SZ_BYTES(create_cq_in) +
		MLX5_FLD_SZ_BYTES(create_cq_in, pas[0]) *
		cq->buf.frag_buf.npages;
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in) {
		err = -ENOMEM;
		goto err_buff;
	}

	vector = raw_smp_processor_id() % mlx5_comp_vectors_max(mdev);
	err = mlx5_comp_eqn_get(mdev, vector, &eqn);
	if (err)
		goto err_vec;

	cqc = MLX5_ADDR_OF(create_cq_in, in, cq_context);
	MLX5_SET(cqc, cqc, log_cq_size, ilog2(ncqe));
	MLX5_SET(cqc, cqc, c_eqn_or_apu_element, eqn);
	MLX5_SET(cqc, cqc, uar_page, tracker->uar->index);
	MLX5_SET(cqc, cqc, log_page_size, cq->buf.frag_buf.page_shift -
		 MLX5_ADAPTER_PAGE_SHIFT);
	MLX5_SET64(cqc, cqc, dbr_addr, cq->db.dma);
	pas = (__be64 *)MLX5_ADDR_OF(create_cq_in, in, pas);
	mlx5_fill_page_frag_array(&cq->buf.frag_buf, pas);
	cq->mcq.comp = mlx5vf_cq_complete;
	cq->mcq.event = mlx5vf_cq_event;
	err = mlx5_core_create_cq(mdev, &cq->mcq, in, inlen, out, sizeof(out));
	if (err)
		goto err_vec;

	mlx5_cq_arm(&cq->mcq, MLX5_CQ_DB_REQ_NOT, tracker->uar->map,
		    cq->mcq.cons_index);
	kvfree(in);
	return 0;

err_vec:
	kvfree(in);
err_buff:
	mlx5_frag_buf_free(mdev, &cq->buf.frag_buf);
err_db_free:
	mlx5_db_free(mdev, &cq->db);
	return err;
}

static struct mlx5_vhca_qp *
mlx5vf_create_rc_qp(struct mlx5_core_dev *mdev,
		    struct mlx5_vhca_page_tracker *tracker, u32 max_recv_wr)
{
	u32 out[MLX5_ST_SZ_DW(create_qp_out)] = {};
	struct mlx5_vhca_qp *qp;
	u8 log_rq_stride;
	u8 log_rq_sz;
	void *qpc;
	int inlen;
	void *in;
	int err;

	qp = kzalloc(sizeof(*qp), GFP_KERNEL_ACCOUNT);
	if (!qp)
		return ERR_PTR(-ENOMEM);

	err = mlx5_db_alloc_node(mdev, &qp->db, mdev->priv.numa_node);
	if (err)
		goto err_free;

	if (max_recv_wr) {
		qp->rq.wqe_cnt = roundup_pow_of_two(max_recv_wr);
		log_rq_stride = ilog2(MLX5_SEND_WQE_DS);
		log_rq_sz = ilog2(qp->rq.wqe_cnt);
		err = mlx5_frag_buf_alloc_node(mdev,
			wq_get_byte_sz(log_rq_sz, log_rq_stride),
			&qp->buf, mdev->priv.numa_node);
		if (err)
			goto err_db_free;
		mlx5_init_fbc(qp->buf.frags, log_rq_stride, log_rq_sz, &qp->rq.fbc);
	}

	qp->rq.db = &qp->db.db[MLX5_RCV_DBR];
	inlen = MLX5_ST_SZ_BYTES(create_qp_in) +
		MLX5_FLD_SZ_BYTES(create_qp_in, pas[0]) *
		qp->buf.npages;
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in) {
		err = -ENOMEM;
		goto err_in;
	}

	qpc = MLX5_ADDR_OF(create_qp_in, in, qpc);
	MLX5_SET(qpc, qpc, st, MLX5_QP_ST_RC);
	MLX5_SET(qpc, qpc, pm_state, MLX5_QP_PM_MIGRATED);
	MLX5_SET(qpc, qpc, pd, tracker->pdn);
	MLX5_SET(qpc, qpc, uar_page, tracker->uar->index);
	MLX5_SET(qpc, qpc, log_page_size,
		 qp->buf.page_shift - MLX5_ADAPTER_PAGE_SHIFT);
	MLX5_SET(qpc, qpc, ts_format, mlx5_get_qp_default_ts(mdev));
	if (MLX5_CAP_GEN(mdev, cqe_version) == 1)
		MLX5_SET(qpc, qpc, user_index, 0xFFFFFF);
	MLX5_SET(qpc, qpc, no_sq, 1);
	if (max_recv_wr) {
		MLX5_SET(qpc, qpc, cqn_rcv, tracker->cq.mcq.cqn);
		MLX5_SET(qpc, qpc, log_rq_stride, log_rq_stride - 4);
		MLX5_SET(qpc, qpc, log_rq_size, log_rq_sz);
		MLX5_SET(qpc, qpc, rq_type, MLX5_NON_ZERO_RQ);
		MLX5_SET64(qpc, qpc, dbr_addr, qp->db.dma);
		mlx5_fill_page_frag_array(&qp->buf,
					  (__be64 *)MLX5_ADDR_OF(create_qp_in,
								 in, pas));
	} else {
		MLX5_SET(qpc, qpc, rq_type, MLX5_ZERO_LEN_RQ);
	}

	MLX5_SET(create_qp_in, in, opcode, MLX5_CMD_OP_CREATE_QP);
	err = mlx5_cmd_exec(mdev, in, inlen, out, sizeof(out));
	kvfree(in);
	if (err)
		goto err_in;

	qp->qpn = MLX5_GET(create_qp_out, out, qpn);
	return qp;

err_in:
	if (max_recv_wr)
		mlx5_frag_buf_free(mdev, &qp->buf);
err_db_free:
	mlx5_db_free(mdev, &qp->db);
err_free:
	kfree(qp);
	return ERR_PTR(err);
}

static void mlx5vf_post_recv(struct mlx5_vhca_qp *qp)
{
	struct mlx5_wqe_data_seg *data;
	unsigned int ix;

	WARN_ON(qp->rq.pc - qp->rq.cc >= qp->rq.wqe_cnt);
	ix = qp->rq.pc & (qp->rq.wqe_cnt - 1);
	data = mlx5_frag_buf_get_wqe(&qp->rq.fbc, ix);
	data->byte_count = cpu_to_be32(qp->max_msg_size);
	data->lkey = cpu_to_be32(qp->recv_buf.mkey);
	data->addr = cpu_to_be64(qp->recv_buf.next_rq_offset);
	qp->rq.pc++;
	/* Make sure that descriptors are written before doorbell record. */
	dma_wmb();
	*qp->rq.db = cpu_to_be32(qp->rq.pc & 0xffff);
}

static int mlx5vf_activate_qp(struct mlx5_core_dev *mdev,
			      struct mlx5_vhca_qp *qp, u32 remote_qpn,
			      bool host_qp)
{
	u32 init_in[MLX5_ST_SZ_DW(rst2init_qp_in)] = {};
	u32 rtr_in[MLX5_ST_SZ_DW(init2rtr_qp_in)] = {};
	u32 rts_in[MLX5_ST_SZ_DW(rtr2rts_qp_in)] = {};
	void *qpc;
	int ret;

	/* Init */
	qpc = MLX5_ADDR_OF(rst2init_qp_in, init_in, qpc);
	MLX5_SET(qpc, qpc, primary_address_path.vhca_port_num, 1);
	MLX5_SET(qpc, qpc, pm_state, MLX5_QPC_PM_STATE_MIGRATED);
	MLX5_SET(qpc, qpc, rre, 1);
	MLX5_SET(qpc, qpc, rwe, 1);
	MLX5_SET(rst2init_qp_in, init_in, opcode, MLX5_CMD_OP_RST2INIT_QP);
	MLX5_SET(rst2init_qp_in, init_in, qpn, qp->qpn);
	ret = mlx5_cmd_exec_in(mdev, rst2init_qp, init_in);
	if (ret)
		return ret;

	if (host_qp) {
		struct mlx5_vhca_recv_buf *recv_buf = &qp->recv_buf;
		int i;

		for (i = 0; i < qp->rq.wqe_cnt; i++) {
			mlx5vf_post_recv(qp);
			recv_buf->next_rq_offset += qp->max_msg_size;
		}
	}

	/* RTR */
	qpc = MLX5_ADDR_OF(init2rtr_qp_in, rtr_in, qpc);
	MLX5_SET(init2rtr_qp_in, rtr_in, qpn, qp->qpn);
	MLX5_SET(qpc, qpc, mtu, IB_MTU_4096);
	MLX5_SET(qpc, qpc, log_msg_max, MLX5_CAP_GEN(mdev, log_max_msg));
	MLX5_SET(qpc, qpc, remote_qpn, remote_qpn);
	MLX5_SET(qpc, qpc, primary_address_path.vhca_port_num, 1);
	MLX5_SET(qpc, qpc, primary_address_path.fl, 1);
	MLX5_SET(qpc, qpc, min_rnr_nak, 1);
	MLX5_SET(init2rtr_qp_in, rtr_in, opcode, MLX5_CMD_OP_INIT2RTR_QP);
	MLX5_SET(init2rtr_qp_in, rtr_in, qpn, qp->qpn);
	ret = mlx5_cmd_exec_in(mdev, init2rtr_qp, rtr_in);
	if (ret || host_qp)
		return ret;

	/* RTS */
	qpc = MLX5_ADDR_OF(rtr2rts_qp_in, rts_in, qpc);
	MLX5_SET(rtr2rts_qp_in, rts_in, qpn, qp->qpn);
	MLX5_SET(qpc, qpc, retry_count, 7);
	MLX5_SET(qpc, qpc, rnr_retry, 7); /* Infinite retry if RNR NACK */
	MLX5_SET(qpc, qpc, primary_address_path.ack_timeout, 0x8); /* ~1ms */
	MLX5_SET(rtr2rts_qp_in, rts_in, opcode, MLX5_CMD_OP_RTR2RTS_QP);
	MLX5_SET(rtr2rts_qp_in, rts_in, qpn, qp->qpn);

	return mlx5_cmd_exec_in(mdev, rtr2rts_qp, rts_in);
}

static void mlx5vf_destroy_qp(struct mlx5_core_dev *mdev,
			      struct mlx5_vhca_qp *qp)
{
	u32 in[MLX5_ST_SZ_DW(destroy_qp_in)] = {};

	MLX5_SET(destroy_qp_in, in, opcode, MLX5_CMD_OP_DESTROY_QP);
	MLX5_SET(destroy_qp_in, in, qpn, qp->qpn);
	mlx5_cmd_exec_in(mdev, destroy_qp, in);

	mlx5_frag_buf_free(mdev, &qp->buf);
	mlx5_db_free(mdev, &qp->db);
	kfree(qp);
}

static void free_recv_pages(struct mlx5_vhca_recv_buf *recv_buf)
{
	int i;

	/* Undo alloc_pages_bulk_array() */
	for (i = 0; i < recv_buf->npages; i++)
		__free_page(recv_buf->page_list[i]);

	kvfree(recv_buf->page_list);
}

static int alloc_recv_pages(struct mlx5_vhca_recv_buf *recv_buf,
			    unsigned int npages)
{
	unsigned int filled = 0, done = 0;
	int i;

	recv_buf->page_list = kvcalloc(npages, sizeof(*recv_buf->page_list),
				       GFP_KERNEL_ACCOUNT);
	if (!recv_buf->page_list)
		return -ENOMEM;

	for (;;) {
		filled = alloc_pages_bulk_array(GFP_KERNEL_ACCOUNT,
						npages - done,
						recv_buf->page_list + done);
		if (!filled)
			goto err;

		done += filled;
		if (done == npages)
			break;
	}

	recv_buf->npages = npages;
	return 0;

err:
	for (i = 0; i < npages; i++) {
		if (recv_buf->page_list[i])
			__free_page(recv_buf->page_list[i]);
	}

	kvfree(recv_buf->page_list);
	return -ENOMEM;
}

static int register_dma_recv_pages(struct mlx5_core_dev *mdev,
				   struct mlx5_vhca_recv_buf *recv_buf)
{
	int i, j;

	recv_buf->dma_addrs = kvcalloc(recv_buf->npages,
				       sizeof(*recv_buf->dma_addrs),
				       GFP_KERNEL_ACCOUNT);
	if (!recv_buf->dma_addrs)
		return -ENOMEM;

	for (i = 0; i < recv_buf->npages; i++) {
		recv_buf->dma_addrs[i] = dma_map_page(mdev->device,
						      recv_buf->page_list[i],
						      0, PAGE_SIZE,
						      DMA_FROM_DEVICE);
		if (dma_mapping_error(mdev->device, recv_buf->dma_addrs[i]))
			goto error;
	}
	return 0;

error:
	for (j = 0; j < i; j++)
		dma_unmap_single(mdev->device, recv_buf->dma_addrs[j],
				 PAGE_SIZE, DMA_FROM_DEVICE);

	kvfree(recv_buf->dma_addrs);
	return -ENOMEM;
}

static void unregister_dma_recv_pages(struct mlx5_core_dev *mdev,
				      struct mlx5_vhca_recv_buf *recv_buf)
{
	int i;

	for (i = 0; i < recv_buf->npages; i++)
		dma_unmap_single(mdev->device, recv_buf->dma_addrs[i],
				 PAGE_SIZE, DMA_FROM_DEVICE);

	kvfree(recv_buf->dma_addrs);
}

static void mlx5vf_free_qp_recv_resources(struct mlx5_core_dev *mdev,
					  struct mlx5_vhca_qp *qp)
{
	struct mlx5_vhca_recv_buf *recv_buf = &qp->recv_buf;

	mlx5_core_destroy_mkey(mdev, recv_buf->mkey);
	unregister_dma_recv_pages(mdev, recv_buf);
	free_recv_pages(&qp->recv_buf);
}

static int mlx5vf_alloc_qp_recv_resources(struct mlx5_core_dev *mdev,
					  struct mlx5_vhca_qp *qp, u32 pdn,
					  u64 rq_size)
{
	unsigned int npages = DIV_ROUND_UP_ULL(rq_size, PAGE_SIZE);
	struct mlx5_vhca_recv_buf *recv_buf = &qp->recv_buf;
	int err;

	err = alloc_recv_pages(recv_buf, npages);
	if (err < 0)
		return err;

	err = register_dma_recv_pages(mdev, recv_buf);
	if (err)
		goto end;

	err = _create_mkey(mdev, pdn, NULL, recv_buf, &recv_buf->mkey);
	if (err)
		goto err_create_mkey;

	return 0;

err_create_mkey:
	unregister_dma_recv_pages(mdev, recv_buf);
end:
	free_recv_pages(recv_buf);
	return err;
}

static void
_mlx5vf_free_page_tracker_resources(struct mlx5vf_pci_core_device *mvdev)
{
	struct mlx5_vhca_page_tracker *tracker = &mvdev->tracker;
	struct mlx5_core_dev *mdev = mvdev->mdev;

	lockdep_assert_held(&mvdev->state_mutex);

	if (!mvdev->log_active)
		return;

	WARN_ON(mvdev->mdev_detach);

	mlx5_eq_notifier_unregister(mdev, &tracker->nb);
	mlx5vf_cmd_destroy_tracker(mdev, tracker->id);
	mlx5vf_destroy_qp(mdev, tracker->fw_qp);
	mlx5vf_free_qp_recv_resources(mdev, tracker->host_qp);
	mlx5vf_destroy_qp(mdev, tracker->host_qp);
	mlx5vf_destroy_cq(mdev, &tracker->cq);
	mlx5_core_dealloc_pd(mdev, tracker->pdn);
	mlx5_put_uars_page(mdev, tracker->uar);
	mvdev->log_active = false;
}

int mlx5vf_stop_page_tracker(struct vfio_device *vdev)
{
	struct mlx5vf_pci_core_device *mvdev = container_of(
		vdev, struct mlx5vf_pci_core_device, core_device.vdev);

	mutex_lock(&mvdev->state_mutex);
	if (!mvdev->log_active)
		goto end;

	_mlx5vf_free_page_tracker_resources(mvdev);
	mvdev->log_active = false;
end:
	mlx5vf_state_mutex_unlock(mvdev);
	return 0;
}

int mlx5vf_start_page_tracker(struct vfio_device *vdev,
			      struct rb_root_cached *ranges, u32 nnodes,
			      u64 *page_size)
{
	struct mlx5vf_pci_core_device *mvdev = container_of(
		vdev, struct mlx5vf_pci_core_device, core_device.vdev);
	struct mlx5_vhca_page_tracker *tracker = &mvdev->tracker;
	u8 log_tracked_page = ilog2(*page_size);
	struct mlx5_vhca_qp *host_qp;
	struct mlx5_vhca_qp *fw_qp;
	struct mlx5_core_dev *mdev;
	u32 max_msg_size = PAGE_SIZE;
	u64 rq_size = SZ_2M;
	u32 max_recv_wr;
	int err;

	mutex_lock(&mvdev->state_mutex);
	if (mvdev->mdev_detach) {
		err = -ENOTCONN;
		goto end;
	}

	if (mvdev->log_active) {
		err = -EINVAL;
		goto end;
	}

	mdev = mvdev->mdev;
	memset(tracker, 0, sizeof(*tracker));
	tracker->uar = mlx5_get_uars_page(mdev);
	if (IS_ERR(tracker->uar)) {
		err = PTR_ERR(tracker->uar);
		goto end;
	}

	err = mlx5_core_alloc_pd(mdev, &tracker->pdn);
	if (err)
		goto err_uar;

	max_recv_wr = DIV_ROUND_UP_ULL(rq_size, max_msg_size);
	err = mlx5vf_create_cq(mdev, tracker, max_recv_wr);
	if (err)
		goto err_dealloc_pd;

	host_qp = mlx5vf_create_rc_qp(mdev, tracker, max_recv_wr);
	if (IS_ERR(host_qp)) {
		err = PTR_ERR(host_qp);
		goto err_cq;
	}

	host_qp->max_msg_size = max_msg_size;
	if (log_tracked_page < MLX5_CAP_ADV_VIRTUALIZATION(mdev,
				pg_track_log_min_page_size)) {
		log_tracked_page = MLX5_CAP_ADV_VIRTUALIZATION(mdev,
				pg_track_log_min_page_size);
	} else if (log_tracked_page > MLX5_CAP_ADV_VIRTUALIZATION(mdev,
				pg_track_log_max_page_size)) {
		log_tracked_page = MLX5_CAP_ADV_VIRTUALIZATION(mdev,
				pg_track_log_max_page_size);
	}

	host_qp->tracked_page_size = (1ULL << log_tracked_page);
	err = mlx5vf_alloc_qp_recv_resources(mdev, host_qp, tracker->pdn,
					     rq_size);
	if (err)
		goto err_host_qp;

	fw_qp = mlx5vf_create_rc_qp(mdev, tracker, 0);
	if (IS_ERR(fw_qp)) {
		err = PTR_ERR(fw_qp);
		goto err_recv_resources;
	}

	err = mlx5vf_activate_qp(mdev, host_qp, fw_qp->qpn, true);
	if (err)
		goto err_activate;

	err = mlx5vf_activate_qp(mdev, fw_qp, host_qp->qpn, false);
	if (err)
		goto err_activate;

	tracker->host_qp = host_qp;
	tracker->fw_qp = fw_qp;
	err = mlx5vf_create_tracker(mdev, mvdev, ranges, nnodes);
	if (err)
		goto err_activate;

	MLX5_NB_INIT(&tracker->nb, mlx5vf_event_notifier, NOTIFY_ANY);
	mlx5_eq_notifier_register(mdev, &tracker->nb);
	*page_size = host_qp->tracked_page_size;
	mvdev->log_active = true;
	mlx5vf_state_mutex_unlock(mvdev);
	return 0;

err_activate:
	mlx5vf_destroy_qp(mdev, fw_qp);
err_recv_resources:
	mlx5vf_free_qp_recv_resources(mdev, host_qp);
err_host_qp:
	mlx5vf_destroy_qp(mdev, host_qp);
err_cq:
	mlx5vf_destroy_cq(mdev, &tracker->cq);
err_dealloc_pd:
	mlx5_core_dealloc_pd(mdev, tracker->pdn);
err_uar:
	mlx5_put_uars_page(mdev, tracker->uar);
end:
	mlx5vf_state_mutex_unlock(mvdev);
	return err;
}

static void
set_report_output(u32 size, int index, struct mlx5_vhca_qp *qp,
		  struct iova_bitmap *dirty)
{
	u32 entry_size = MLX5_ST_SZ_BYTES(page_track_report_entry);
	u32 nent = size / entry_size;
	struct page *page;
	u64 addr;
	u64 *buf;
	int i;

	if (WARN_ON(index >= qp->recv_buf.npages ||
		    (nent > qp->max_msg_size / entry_size)))
		return;

	page = qp->recv_buf.page_list[index];
	buf = kmap_local_page(page);
	for (i = 0; i < nent; i++) {
		addr = MLX5_GET(page_track_report_entry, buf + i,
				dirty_address_low);
		addr |= (u64)MLX5_GET(page_track_report_entry, buf + i,
				      dirty_address_high) << 32;
		iova_bitmap_set(dirty, addr, qp->tracked_page_size);
	}
	kunmap_local(buf);
}

static void
mlx5vf_rq_cqe(struct mlx5_vhca_qp *qp, struct mlx5_cqe64 *cqe,
	      struct iova_bitmap *dirty, int *tracker_status)
{
	u32 size;
	int ix;

	qp->rq.cc++;
	*tracker_status = be32_to_cpu(cqe->immediate) >> 28;
	size = be32_to_cpu(cqe->byte_cnt);
	ix = be16_to_cpu(cqe->wqe_counter) & (qp->rq.wqe_cnt - 1);

	/* zero length CQE, no data */
	WARN_ON(!size && *tracker_status == MLX5_PAGE_TRACK_STATE_REPORTING);
	if (size)
		set_report_output(size, ix, qp, dirty);

	qp->recv_buf.next_rq_offset = ix * qp->max_msg_size;
	mlx5vf_post_recv(qp);
}

static void *get_cqe(struct mlx5_vhca_cq *cq, int n)
{
	return mlx5_frag_buf_get_wqe(&cq->buf.fbc, n);
}

static struct mlx5_cqe64 *get_sw_cqe(struct mlx5_vhca_cq *cq, int n)
{
	void *cqe = get_cqe(cq, n & (cq->ncqe - 1));
	struct mlx5_cqe64 *cqe64;

	cqe64 = (cq->mcq.cqe_sz == 64) ? cqe : cqe + 64;

	if (likely(get_cqe_opcode(cqe64) != MLX5_CQE_INVALID) &&
	    !((cqe64->op_own & MLX5_CQE_OWNER_MASK) ^ !!(n & (cq->ncqe)))) {
		return cqe64;
	} else {
		return NULL;
	}
}

static int
mlx5vf_cq_poll_one(struct mlx5_vhca_cq *cq, struct mlx5_vhca_qp *qp,
		   struct iova_bitmap *dirty, int *tracker_status)
{
	struct mlx5_cqe64 *cqe;
	u8 opcode;

	cqe = get_sw_cqe(cq, cq->mcq.cons_index);
	if (!cqe)
		return CQ_EMPTY;

	++cq->mcq.cons_index;
	/*
	 * Make sure we read CQ entry contents after we've checked the
	 * ownership bit.
	 */
	rmb();
	opcode = get_cqe_opcode(cqe);
	switch (opcode) {
	case MLX5_CQE_RESP_SEND_IMM:
		mlx5vf_rq_cqe(qp, cqe, dirty, tracker_status);
		return CQ_OK;
	default:
		return CQ_POLL_ERR;
	}
}

int mlx5vf_tracker_read_and_clear(struct vfio_device *vdev, unsigned long iova,
				  unsigned long length,
				  struct iova_bitmap *dirty)
{
	struct mlx5vf_pci_core_device *mvdev = container_of(
		vdev, struct mlx5vf_pci_core_device, core_device.vdev);
	struct mlx5_vhca_page_tracker *tracker = &mvdev->tracker;
	struct mlx5_vhca_cq *cq = &tracker->cq;
	struct mlx5_core_dev *mdev;
	int poll_err, err;

	mutex_lock(&mvdev->state_mutex);
	if (!mvdev->log_active) {
		err = -EINVAL;
		goto end;
	}

	if (mvdev->mdev_detach) {
		err = -ENOTCONN;
		goto end;
	}

	mdev = mvdev->mdev;
	err = mlx5vf_cmd_modify_tracker(mdev, tracker->id, iova, length,
					MLX5_PAGE_TRACK_STATE_REPORTING);
	if (err)
		goto end;

	tracker->status = MLX5_PAGE_TRACK_STATE_REPORTING;
	while (tracker->status == MLX5_PAGE_TRACK_STATE_REPORTING &&
	       !tracker->is_err) {
		poll_err = mlx5vf_cq_poll_one(cq, tracker->host_qp, dirty,
					      &tracker->status);
		if (poll_err == CQ_EMPTY) {
			mlx5_cq_arm(&cq->mcq, MLX5_CQ_DB_REQ_NOT, tracker->uar->map,
				    cq->mcq.cons_index);
			poll_err = mlx5vf_cq_poll_one(cq, tracker->host_qp,
						      dirty, &tracker->status);
			if (poll_err == CQ_EMPTY) {
				wait_for_completion(&mvdev->tracker_comp);
				continue;
			}
		}
		if (poll_err == CQ_POLL_ERR) {
			err = -EIO;
			goto end;
		}
		mlx5_cq_set_ci(&cq->mcq);
	}

	if (tracker->status == MLX5_PAGE_TRACK_STATE_ERROR)
		tracker->is_err = true;

	if (tracker->is_err)
		err = -EIO;
end:
	mlx5vf_state_mutex_unlock(mvdev);
	return err;
}
