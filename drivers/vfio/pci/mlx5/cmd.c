// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */

#include "cmd.h"

enum { CQ_OK = 0, CQ_EMPTY = -1, CQ_POLL_ERR = -2 };

static int mlx5vf_cmd_get_vhca_id(struct mlx5_core_dev *mdev, u16 function_id,
				  u16 *vhca_id);
static void
_mlx5vf_free_page_tracker_resources(struct mlx5vf_pci_core_device *mvdev);

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
	init_completion(&mvdev->tracker_comp);

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
			struct mlx5_vf_migration_file *migf,
			struct mlx5_vhca_recv_buf *recv_buf,
			u32 *mkey)
{
	size_t npages = migf ? DIV_ROUND_UP(migf->total_length, PAGE_SIZE) :
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

	if (migf) {
		struct sg_dma_page_iter dma_iter;

		for_each_sgtable_dma_page(&migf->table.sgt, &dma_iter, 0)
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
	MLX5_SET64(mkc, mkc, len,
		   migf ? migf->total_length : (npages * PAGE_SIZE));
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

	err = _create_mkey(mdev, pdn, migf, NULL, &mkey);
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

	err = _create_mkey(mdev, pdn, migf, NULL, &mkey);
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

static void combine_ranges(struct rb_root_cached *root, u32 cur_nodes,
			   u32 req_nodes)
{
	struct interval_tree_node *prev, *curr, *comb_start, *comb_end;
	unsigned long min_gap;
	unsigned long curr_gap;

	/* Special shortcut when a single range is required */
	if (req_nodes == 1) {
		unsigned long last;

		curr = comb_start = interval_tree_iter_first(root, 0, ULONG_MAX);
		while (curr) {
			last = curr->last;
			prev = curr;
			curr = interval_tree_iter_next(curr, 0, ULONG_MAX);
			if (prev != comb_start)
				interval_tree_remove(prev, root);
		}
		comb_start->last = last;
		return;
	}

	/* Combine ranges which have the smallest gap */
	while (cur_nodes > req_nodes) {
		prev = NULL;
		min_gap = ULONG_MAX;
		curr = interval_tree_iter_first(root, 0, ULONG_MAX);
		while (curr) {
			if (prev) {
				curr_gap = curr->start - prev->last;
				if (curr_gap < min_gap) {
					min_gap = curr_gap;
					comb_start = prev;
					comb_end = curr;
				}
			}
			prev = curr;
			curr = interval_tree_iter_next(curr, 0, ULONG_MAX);
		}
		comb_start->last = comb_end->last;
		interval_tree_remove(comb_end, root);
		cur_nodes--;
	}
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
		combine_ranges(ranges, nnodes, max_num_range);
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
		unsigned long length = node->last - node->start;

		MLX5_SET64(page_track_range, addr_range_i_base, start_address,
			   node->start);
		MLX5_SET64(page_track_range, addr_range_i_base, length, length);
		total_ranges_len += length;
		node = interval_tree_iter_next(node, 0, ULONG_MAX);
	}

	WARN_ON(node);
	log_addr_space_size = ilog2(total_ranges_len);
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

	vector = raw_smp_processor_id() % mlx5_comp_vectors_count(mdev);
	err = mlx5_vector2eqn(mdev, vector, &eqn);
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

	qp = kzalloc(sizeof(*qp), GFP_KERNEL);
	if (!qp)
		return ERR_PTR(-ENOMEM);

	qp->rq.wqe_cnt = roundup_pow_of_two(max_recv_wr);
	log_rq_stride = ilog2(MLX5_SEND_WQE_DS);
	log_rq_sz = ilog2(qp->rq.wqe_cnt);
	err = mlx5_db_alloc_node(mdev, &qp->db, mdev->priv.numa_node);
	if (err)
		goto err_free;

	if (max_recv_wr) {
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
				       GFP_KERNEL);
	if (!recv_buf->page_list)
		return -ENOMEM;

	for (;;) {
		filled = alloc_pages_bulk_array(GFP_KERNEL, npages - done,
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
				       GFP_KERNEL);
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
	while (tracker->status == MLX5_PAGE_TRACK_STATE_REPORTING) {
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
		err = -EIO;

end:
	mlx5vf_state_mutex_unlock(mvdev);
	return err;
}
