// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019 Mellanox Technologies. */

#include <linux/smp.h>
#include "dr_types.h"

#define QUEUE_SIZE 128
#define SIGNAL_PER_DIV_QUEUE 16
#define TH_NUMS_TO_DRAIN 2

enum { CQ_OK = 0, CQ_EMPTY = -1, CQ_POLL_ERR = -2 };

struct dr_data_seg {
	u64 addr;
	u32 length;
	u32 lkey;
	unsigned int send_flags;
};

struct postsend_info {
	struct dr_data_seg write;
	struct dr_data_seg read;
	u64 remote_addr;
	u32 rkey;
};

struct dr_qp_rtr_attr {
	struct mlx5dr_cmd_gid_attr dgid_attr;
	enum ib_mtu mtu;
	u32 qp_num;
	u16 port_num;
	u8 min_rnr_timer;
	u8 sgid_index;
	u16 udp_src_port;
};

struct dr_qp_rts_attr {
	u8 timeout;
	u8 retry_cnt;
	u8 rnr_retry;
};

struct dr_qp_init_attr {
	u32 cqn;
	u32 pdn;
	u32 max_send_wr;
	struct mlx5_uars_page *uar;
};

static int dr_parse_cqe(struct mlx5dr_cq *dr_cq, struct mlx5_cqe64 *cqe64)
{
	unsigned int idx;
	u8 opcode;

	opcode = get_cqe_opcode(cqe64);
	if (opcode == MLX5_CQE_REQ_ERR) {
		idx = be16_to_cpu(cqe64->wqe_counter) &
			(dr_cq->qp->sq.wqe_cnt - 1);
		dr_cq->qp->sq.cc = dr_cq->qp->sq.wqe_head[idx] + 1;
	} else if (opcode == MLX5_CQE_RESP_ERR) {
		++dr_cq->qp->sq.cc;
	} else {
		idx = be16_to_cpu(cqe64->wqe_counter) &
			(dr_cq->qp->sq.wqe_cnt - 1);
		dr_cq->qp->sq.cc = dr_cq->qp->sq.wqe_head[idx] + 1;

		return CQ_OK;
	}

	return CQ_POLL_ERR;
}

static int dr_cq_poll_one(struct mlx5dr_cq *dr_cq)
{
	struct mlx5_cqe64 *cqe64;
	int err;

	cqe64 = mlx5_cqwq_get_cqe(&dr_cq->wq);
	if (!cqe64)
		return CQ_EMPTY;

	mlx5_cqwq_pop(&dr_cq->wq);
	err = dr_parse_cqe(dr_cq, cqe64);
	mlx5_cqwq_update_db_record(&dr_cq->wq);

	return err;
}

static int dr_poll_cq(struct mlx5dr_cq *dr_cq, int ne)
{
	int npolled;
	int err = 0;

	for (npolled = 0; npolled < ne; ++npolled) {
		err = dr_cq_poll_one(dr_cq);
		if (err != CQ_OK)
			break;
	}

	return err == CQ_POLL_ERR ? err : npolled;
}

static void dr_qp_event(struct mlx5_core_qp *mqp, int event)
{
	pr_info("DR QP event %u on QP #%u\n", event, mqp->qpn);
}

static struct mlx5dr_qp *dr_create_rc_qp(struct mlx5_core_dev *mdev,
					 struct dr_qp_init_attr *attr)
{
	u32 temp_qpc[MLX5_ST_SZ_DW(qpc)] = {};
	struct mlx5_wq_param wqp;
	struct mlx5dr_qp *dr_qp;
	int inlen;
	void *qpc;
	void *in;
	int err;

	dr_qp = kzalloc(sizeof(*dr_qp), GFP_KERNEL);
	if (!dr_qp)
		return NULL;

	wqp.buf_numa_node = mdev->priv.numa_node;
	wqp.db_numa_node = mdev->priv.numa_node;

	dr_qp->rq.pc = 0;
	dr_qp->rq.cc = 0;
	dr_qp->rq.wqe_cnt = 4;
	dr_qp->sq.pc = 0;
	dr_qp->sq.cc = 0;
	dr_qp->sq.wqe_cnt = roundup_pow_of_two(attr->max_send_wr);

	MLX5_SET(qpc, temp_qpc, log_rq_stride, ilog2(MLX5_SEND_WQE_DS) - 4);
	MLX5_SET(qpc, temp_qpc, log_rq_size, ilog2(dr_qp->rq.wqe_cnt));
	MLX5_SET(qpc, temp_qpc, log_sq_size, ilog2(dr_qp->sq.wqe_cnt));
	err = mlx5_wq_qp_create(mdev, &wqp, temp_qpc, &dr_qp->wq,
				&dr_qp->wq_ctrl);
	if (err) {
		mlx5_core_warn(mdev, "Can't create QP WQ\n");
		goto err_wq;
	}

	dr_qp->sq.wqe_head = kcalloc(dr_qp->sq.wqe_cnt,
				     sizeof(dr_qp->sq.wqe_head[0]),
				     GFP_KERNEL);

	if (!dr_qp->sq.wqe_head) {
		mlx5_core_warn(mdev, "Can't allocate wqe head\n");
		goto err_wqe_head;
	}

	inlen = MLX5_ST_SZ_BYTES(create_qp_in) +
		MLX5_FLD_SZ_BYTES(create_qp_in, pas[0]) *
		dr_qp->wq_ctrl.buf.npages;
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in) {
		err = -ENOMEM;
		goto err_in;
	}

	qpc = MLX5_ADDR_OF(create_qp_in, in, qpc);
	MLX5_SET(qpc, qpc, st, MLX5_QP_ST_RC);
	MLX5_SET(qpc, qpc, pm_state, MLX5_QP_PM_MIGRATED);
	MLX5_SET(qpc, qpc, pd, attr->pdn);
	MLX5_SET(qpc, qpc, uar_page, attr->uar->index);
	MLX5_SET(qpc, qpc, log_page_size,
		 dr_qp->wq_ctrl.buf.page_shift - MLX5_ADAPTER_PAGE_SHIFT);
	MLX5_SET(qpc, qpc, fre, 1);
	MLX5_SET(qpc, qpc, rlky, 1);
	MLX5_SET(qpc, qpc, cqn_snd, attr->cqn);
	MLX5_SET(qpc, qpc, cqn_rcv, attr->cqn);
	MLX5_SET(qpc, qpc, log_rq_stride, ilog2(MLX5_SEND_WQE_DS) - 4);
	MLX5_SET(qpc, qpc, log_rq_size, ilog2(dr_qp->rq.wqe_cnt));
	MLX5_SET(qpc, qpc, rq_type, MLX5_NON_ZERO_RQ);
	MLX5_SET(qpc, qpc, log_sq_size, ilog2(dr_qp->sq.wqe_cnt));
	MLX5_SET64(qpc, qpc, dbr_addr, dr_qp->wq_ctrl.db.dma);
	if (MLX5_CAP_GEN(mdev, cqe_version) == 1)
		MLX5_SET(qpc, qpc, user_index, 0xFFFFFF);
	mlx5_fill_page_frag_array(&dr_qp->wq_ctrl.buf,
				  (__be64 *)MLX5_ADDR_OF(create_qp_in,
							 in, pas));

	err = mlx5_core_create_qp(mdev, &dr_qp->mqp, in, inlen);
	kfree(in);

	if (err) {
		mlx5_core_warn(mdev, " Can't create QP\n");
		goto err_in;
	}
	dr_qp->mqp.event = dr_qp_event;
	dr_qp->uar = attr->uar;

	return dr_qp;

err_in:
	kfree(dr_qp->sq.wqe_head);
err_wqe_head:
	mlx5_wq_destroy(&dr_qp->wq_ctrl);
err_wq:
	kfree(dr_qp);
	return NULL;
}

static void dr_destroy_qp(struct mlx5_core_dev *mdev,
			  struct mlx5dr_qp *dr_qp)
{
	mlx5_core_destroy_qp(mdev, &dr_qp->mqp);
	kfree(dr_qp->sq.wqe_head);
	mlx5_wq_destroy(&dr_qp->wq_ctrl);
	kfree(dr_qp);
}

static void dr_cmd_notify_hw(struct mlx5dr_qp *dr_qp, void *ctrl)
{
	dma_wmb();
	*dr_qp->wq.sq.db = cpu_to_be32(dr_qp->sq.pc & 0xfffff);

	/* After wmb() the hw aware of new work */
	wmb();

	mlx5_write64(ctrl, dr_qp->uar->map + MLX5_BF_OFFSET);
}

static void dr_rdma_segments(struct mlx5dr_qp *dr_qp, u64 remote_addr,
			     u32 rkey, struct dr_data_seg *data_seg,
			     u32 opcode, int nreq)
{
	struct mlx5_wqe_raddr_seg *wq_raddr;
	struct mlx5_wqe_ctrl_seg *wq_ctrl;
	struct mlx5_wqe_data_seg *wq_dseg;
	unsigned int size;
	unsigned int idx;

	size = sizeof(*wq_ctrl) / 16 + sizeof(*wq_dseg) / 16 +
		sizeof(*wq_raddr) / 16;

	idx = dr_qp->sq.pc & (dr_qp->sq.wqe_cnt - 1);

	wq_ctrl = mlx5_wq_cyc_get_wqe(&dr_qp->wq.sq, idx);
	wq_ctrl->imm = 0;
	wq_ctrl->fm_ce_se = (data_seg->send_flags) ?
		MLX5_WQE_CTRL_CQ_UPDATE : 0;
	wq_ctrl->opmod_idx_opcode = cpu_to_be32(((dr_qp->sq.pc & 0xffff) << 8) |
						opcode);
	wq_ctrl->qpn_ds = cpu_to_be32(size | dr_qp->mqp.qpn << 8);
	wq_raddr = (void *)(wq_ctrl + 1);
	wq_raddr->raddr = cpu_to_be64(remote_addr);
	wq_raddr->rkey = cpu_to_be32(rkey);
	wq_raddr->reserved = 0;

	wq_dseg = (void *)(wq_raddr + 1);
	wq_dseg->byte_count = cpu_to_be32(data_seg->length);
	wq_dseg->lkey = cpu_to_be32(data_seg->lkey);
	wq_dseg->addr = cpu_to_be64(data_seg->addr);

	dr_qp->sq.wqe_head[idx] = dr_qp->sq.pc++;

	if (nreq)
		dr_cmd_notify_hw(dr_qp, wq_ctrl);
}

static void dr_post_send(struct mlx5dr_qp *dr_qp, struct postsend_info *send_info)
{
	dr_rdma_segments(dr_qp, send_info->remote_addr, send_info->rkey,
			 &send_info->write, MLX5_OPCODE_RDMA_WRITE, 0);
	dr_rdma_segments(dr_qp, send_info->remote_addr, send_info->rkey,
			 &send_info->read, MLX5_OPCODE_RDMA_READ, 1);
}

/**
 * mlx5dr_send_fill_and_append_ste_send_info: Add data to be sent
 * with send_list parameters:
 *
 *     @ste:       The data that attached to this specific ste
 *     @size:      of data to write
 *     @offset:    of the data from start of the hw_ste entry
 *     @data:      data
 *     @ste_info:  ste to be sent with send_list
 *     @send_list: to append into it
 *     @copy_data: if true indicates that the data should be kept because
 *                 it's not backuped any where (like in re-hash).
 *                 if false, it lets the data to be updated after
 *                 it was added to the list.
 */
void mlx5dr_send_fill_and_append_ste_send_info(struct mlx5dr_ste *ste, u16 size,
					       u16 offset, u8 *data,
					       struct mlx5dr_ste_send_info *ste_info,
					       struct list_head *send_list,
					       bool copy_data)
{
	ste_info->size = size;
	ste_info->ste = ste;
	ste_info->offset = offset;

	if (copy_data) {
		memcpy(ste_info->data_cont, data, size);
		ste_info->data = ste_info->data_cont;
	} else {
		ste_info->data = data;
	}

	list_add_tail(&ste_info->send_list, send_list);
}

/* The function tries to consume one wc each time, unless the queue is full, in
 * that case, which means that the hw is behind the sw in a full queue len
 * the function will drain the cq till it empty.
 */
static int dr_handle_pending_wc(struct mlx5dr_domain *dmn,
				struct mlx5dr_send_ring *send_ring)
{
	bool is_drain = false;
	int ne;

	if (send_ring->pending_wqe < send_ring->signal_th)
		return 0;

	/* Queue is full start drain it */
	if (send_ring->pending_wqe >=
	    dmn->send_ring->signal_th * TH_NUMS_TO_DRAIN)
		is_drain = true;

	do {
		ne = dr_poll_cq(send_ring->cq, 1);
		if (ne < 0)
			return ne;
		else if (ne == 1)
			send_ring->pending_wqe -= send_ring->signal_th;
	} while (is_drain && send_ring->pending_wqe);

	return 0;
}

static void dr_fill_data_segs(struct mlx5dr_send_ring *send_ring,
			      struct postsend_info *send_info)
{
	send_ring->pending_wqe++;

	if (send_ring->pending_wqe % send_ring->signal_th == 0)
		send_info->write.send_flags |= IB_SEND_SIGNALED;

	send_ring->pending_wqe++;
	send_info->read.length = send_info->write.length;
	/* Read into the same write area */
	send_info->read.addr = (uintptr_t)send_info->write.addr;
	send_info->read.lkey = send_ring->mr->mkey.key;

	if (send_ring->pending_wqe % send_ring->signal_th == 0)
		send_info->read.send_flags = IB_SEND_SIGNALED;
	else
		send_info->read.send_flags = 0;
}

static int dr_postsend_icm_data(struct mlx5dr_domain *dmn,
				struct postsend_info *send_info)
{
	struct mlx5dr_send_ring *send_ring = dmn->send_ring;
	u32 buff_offset;
	int ret;

	ret = dr_handle_pending_wc(dmn, send_ring);
	if (ret)
		return ret;

	if (send_info->write.length > dmn->info.max_inline_size) {
		buff_offset = (send_ring->tx_head &
			       (dmn->send_ring->signal_th - 1)) *
			send_ring->max_post_send_size;
		/* Copy to ring mr */
		memcpy(send_ring->buf + buff_offset,
		       (void *)(uintptr_t)send_info->write.addr,
		       send_info->write.length);
		send_info->write.addr = (uintptr_t)send_ring->mr->dma_addr + buff_offset;
		send_info->write.lkey = send_ring->mr->mkey.key;
	}

	send_ring->tx_head++;
	dr_fill_data_segs(send_ring, send_info);
	dr_post_send(send_ring->qp, send_info);

	return 0;
}

static int dr_get_tbl_copy_details(struct mlx5dr_domain *dmn,
				   struct mlx5dr_ste_htbl *htbl,
				   u8 **data,
				   u32 *byte_size,
				   int *iterations,
				   int *num_stes)
{
	int alloc_size;

	if (htbl->chunk->byte_size > dmn->send_ring->max_post_send_size) {
		*iterations = htbl->chunk->byte_size /
			dmn->send_ring->max_post_send_size;
		*byte_size = dmn->send_ring->max_post_send_size;
		alloc_size = *byte_size;
		*num_stes = *byte_size / DR_STE_SIZE;
	} else {
		*iterations = 1;
		*num_stes = htbl->chunk->num_of_entries;
		alloc_size = *num_stes * DR_STE_SIZE;
	}

	*data = kzalloc(alloc_size, GFP_KERNEL);
	if (!*data)
		return -ENOMEM;

	return 0;
}

/**
 * mlx5dr_send_postsend_ste: write size bytes into offset from the hw cm.
 *
 *     @dmn:    Domain
 *     @ste:    The ste struct that contains the data (at
 *              least part of it)
 *     @data:   The real data to send size data
 *     @size:   for writing.
 *     @offset: The offset from the icm mapped data to
 *              start write to this for write only part of the
 *              buffer.
 *
 * Return: 0 on success.
 */
int mlx5dr_send_postsend_ste(struct mlx5dr_domain *dmn, struct mlx5dr_ste *ste,
			     u8 *data, u16 size, u16 offset)
{
	struct postsend_info send_info = {};

	send_info.write.addr = (uintptr_t)data;
	send_info.write.length = size;
	send_info.write.lkey = 0;
	send_info.remote_addr = mlx5dr_ste_get_mr_addr(ste) + offset;
	send_info.rkey = ste->htbl->chunk->rkey;

	return dr_postsend_icm_data(dmn, &send_info);
}

int mlx5dr_send_postsend_htbl(struct mlx5dr_domain *dmn,
			      struct mlx5dr_ste_htbl *htbl,
			      u8 *formatted_ste, u8 *mask)
{
	u32 byte_size = htbl->chunk->byte_size;
	int num_stes_per_iter;
	int iterations;
	u8 *data;
	int ret;
	int i;
	int j;

	ret = dr_get_tbl_copy_details(dmn, htbl, &data, &byte_size,
				      &iterations, &num_stes_per_iter);
	if (ret)
		return ret;

	/* Send the data iteration times */
	for (i = 0; i < iterations; i++) {
		u32 ste_index = i * (byte_size / DR_STE_SIZE);
		struct postsend_info send_info = {};

		/* Copy all ste's on the data buffer
		 * need to add the bit_mask
		 */
		for (j = 0; j < num_stes_per_iter; j++) {
			u8 *hw_ste = htbl->ste_arr[ste_index + j].hw_ste;
			u32 ste_off = j * DR_STE_SIZE;

			if (mlx5dr_ste_is_not_valid_entry(hw_ste)) {
				memcpy(data + ste_off,
				       formatted_ste, DR_STE_SIZE);
			} else {
				/* Copy data */
				memcpy(data + ste_off,
				       htbl->ste_arr[ste_index + j].hw_ste,
				       DR_STE_SIZE_REDUCED);
				/* Copy bit_mask */
				memcpy(data + ste_off + DR_STE_SIZE_REDUCED,
				       mask, DR_STE_SIZE_MASK);
			}
		}

		send_info.write.addr = (uintptr_t)data;
		send_info.write.length = byte_size;
		send_info.write.lkey = 0;
		send_info.remote_addr =
			mlx5dr_ste_get_mr_addr(htbl->ste_arr + ste_index);
		send_info.rkey = htbl->chunk->rkey;

		ret = dr_postsend_icm_data(dmn, &send_info);
		if (ret)
			goto out_free;
	}

out_free:
	kfree(data);
	return ret;
}

/* Initialize htble with default STEs */
int mlx5dr_send_postsend_formatted_htbl(struct mlx5dr_domain *dmn,
					struct mlx5dr_ste_htbl *htbl,
					u8 *ste_init_data,
					bool update_hw_ste)
{
	u32 byte_size = htbl->chunk->byte_size;
	int iterations;
	int num_stes;
	u8 *data;
	int ret;
	int i;

	ret = dr_get_tbl_copy_details(dmn, htbl, &data, &byte_size,
				      &iterations, &num_stes);
	if (ret)
		return ret;

	for (i = 0; i < num_stes; i++) {
		u8 *copy_dst;

		/* Copy the same ste on the data buffer */
		copy_dst = data + i * DR_STE_SIZE;
		memcpy(copy_dst, ste_init_data, DR_STE_SIZE);

		if (update_hw_ste) {
			/* Copy the reduced ste to hash table ste_arr */
			copy_dst = htbl->hw_ste_arr + i * DR_STE_SIZE_REDUCED;
			memcpy(copy_dst, ste_init_data, DR_STE_SIZE_REDUCED);
		}
	}

	/* Send the data iteration times */
	for (i = 0; i < iterations; i++) {
		u8 ste_index = i * (byte_size / DR_STE_SIZE);
		struct postsend_info send_info = {};

		send_info.write.addr = (uintptr_t)data;
		send_info.write.length = byte_size;
		send_info.write.lkey = 0;
		send_info.remote_addr =
			mlx5dr_ste_get_mr_addr(htbl->ste_arr + ste_index);
		send_info.rkey = htbl->chunk->rkey;

		ret = dr_postsend_icm_data(dmn, &send_info);
		if (ret)
			goto out_free;
	}

out_free:
	kfree(data);
	return ret;
}

int mlx5dr_send_postsend_action(struct mlx5dr_domain *dmn,
				struct mlx5dr_action *action)
{
	struct postsend_info send_info = {};
	int ret;

	send_info.write.addr = (uintptr_t)action->rewrite.data;
	send_info.write.length = action->rewrite.num_of_actions *
				 DR_MODIFY_ACTION_SIZE;
	send_info.write.lkey = 0;
	send_info.remote_addr = action->rewrite.chunk->mr_addr;
	send_info.rkey = action->rewrite.chunk->rkey;

	mutex_lock(&dmn->mutex);
	ret = dr_postsend_icm_data(dmn, &send_info);
	mutex_unlock(&dmn->mutex);

	return ret;
}

static int dr_modify_qp_rst2init(struct mlx5_core_dev *mdev,
				 struct mlx5dr_qp *dr_qp,
				 int port)
{
	u32 in[MLX5_ST_SZ_DW(rst2init_qp_in)] = {};
	void *qpc;

	qpc = MLX5_ADDR_OF(rst2init_qp_in, in, qpc);

	MLX5_SET(qpc, qpc, primary_address_path.vhca_port_num, port);
	MLX5_SET(qpc, qpc, pm_state, MLX5_QPC_PM_STATE_MIGRATED);
	MLX5_SET(qpc, qpc, rre, 1);
	MLX5_SET(qpc, qpc, rwe, 1);

	return mlx5_core_qp_modify(mdev, MLX5_CMD_OP_RST2INIT_QP, 0, qpc,
				   &dr_qp->mqp);
}

static int dr_cmd_modify_qp_rtr2rts(struct mlx5_core_dev *mdev,
				    struct mlx5dr_qp *dr_qp,
				    struct dr_qp_rts_attr *attr)
{
	u32 in[MLX5_ST_SZ_DW(rtr2rts_qp_in)] = {};
	void *qpc;

	qpc  = MLX5_ADDR_OF(rtr2rts_qp_in, in, qpc);

	MLX5_SET(rtr2rts_qp_in, in, qpn, dr_qp->mqp.qpn);

	MLX5_SET(qpc, qpc, log_ack_req_freq, 0);
	MLX5_SET(qpc, qpc, retry_count, attr->retry_cnt);
	MLX5_SET(qpc, qpc, rnr_retry, attr->rnr_retry);

	return mlx5_core_qp_modify(mdev, MLX5_CMD_OP_RTR2RTS_QP, 0, qpc,
				   &dr_qp->mqp);
}

static int dr_cmd_modify_qp_init2rtr(struct mlx5_core_dev *mdev,
				     struct mlx5dr_qp *dr_qp,
				     struct dr_qp_rtr_attr *attr)
{
	u32 in[MLX5_ST_SZ_DW(init2rtr_qp_in)] = {};
	void *qpc;

	qpc = MLX5_ADDR_OF(init2rtr_qp_in, in, qpc);

	MLX5_SET(init2rtr_qp_in, in, qpn, dr_qp->mqp.qpn);

	MLX5_SET(qpc, qpc, mtu, attr->mtu);
	MLX5_SET(qpc, qpc, log_msg_max, DR_CHUNK_SIZE_MAX - 1);
	MLX5_SET(qpc, qpc, remote_qpn, attr->qp_num);
	memcpy(MLX5_ADDR_OF(qpc, qpc, primary_address_path.rmac_47_32),
	       attr->dgid_attr.mac, sizeof(attr->dgid_attr.mac));
	memcpy(MLX5_ADDR_OF(qpc, qpc, primary_address_path.rgid_rip),
	       attr->dgid_attr.gid, sizeof(attr->dgid_attr.gid));
	MLX5_SET(qpc, qpc, primary_address_path.src_addr_index,
		 attr->sgid_index);

	if (attr->dgid_attr.roce_ver == MLX5_ROCE_VERSION_2)
		MLX5_SET(qpc, qpc, primary_address_path.udp_sport,
			 attr->udp_src_port);

	MLX5_SET(qpc, qpc, primary_address_path.vhca_port_num, attr->port_num);
	MLX5_SET(qpc, qpc, min_rnr_nak, 1);

	return mlx5_core_qp_modify(mdev, MLX5_CMD_OP_INIT2RTR_QP, 0, qpc,
				   &dr_qp->mqp);
}

static int dr_prepare_qp_to_rts(struct mlx5dr_domain *dmn)
{
	struct mlx5dr_qp *dr_qp = dmn->send_ring->qp;
	struct dr_qp_rts_attr rts_attr = {};
	struct dr_qp_rtr_attr rtr_attr = {};
	enum ib_mtu mtu = IB_MTU_1024;
	u16 gid_index = 0;
	int port = 1;
	int ret;

	/* Init */
	ret = dr_modify_qp_rst2init(dmn->mdev, dr_qp, port);
	if (ret) {
		mlx5dr_err(dmn, "Failed modify QP rst2init\n");
		return ret;
	}

	/* RTR */
	ret = mlx5dr_cmd_query_gid(dmn->mdev, port, gid_index, &rtr_attr.dgid_attr);
	if (ret)
		return ret;

	rtr_attr.mtu		= mtu;
	rtr_attr.qp_num		= dr_qp->mqp.qpn;
	rtr_attr.min_rnr_timer	= 12;
	rtr_attr.port_num	= port;
	rtr_attr.sgid_index	= gid_index;
	rtr_attr.udp_src_port	= dmn->info.caps.roce_min_src_udp;

	ret = dr_cmd_modify_qp_init2rtr(dmn->mdev, dr_qp, &rtr_attr);
	if (ret) {
		mlx5dr_err(dmn, "Failed modify QP init2rtr\n");
		return ret;
	}

	/* RTS */
	rts_attr.timeout	= 14;
	rts_attr.retry_cnt	= 7;
	rts_attr.rnr_retry	= 7;

	ret = dr_cmd_modify_qp_rtr2rts(dmn->mdev, dr_qp, &rts_attr);
	if (ret) {
		mlx5dr_err(dmn, "Failed modify QP rtr2rts\n");
		return ret;
	}

	return 0;
}

static void dr_cq_event(struct mlx5_core_cq *mcq,
			enum mlx5_event event)
{
	pr_info("CQ event %u on CQ #%u\n", event, mcq->cqn);
}

static void dr_cq_complete(struct mlx5_core_cq *mcq,
			   struct mlx5_eqe *eqe)
{
	pr_err("CQ completion CQ: #%u\n", mcq->cqn);
}

static struct mlx5dr_cq *dr_create_cq(struct mlx5_core_dev *mdev,
				      struct mlx5_uars_page *uar,
				      size_t ncqe)
{
	u32 temp_cqc[MLX5_ST_SZ_DW(cqc)] = {};
	u32 out[MLX5_ST_SZ_DW(create_cq_out)];
	struct mlx5_wq_param wqp;
	struct mlx5_cqe64 *cqe;
	struct mlx5dr_cq *cq;
	int inlen, err, eqn;
	unsigned int irqn;
	void *cqc, *in;
	__be64 *pas;
	int vector;
	u32 i;

	cq = kzalloc(sizeof(*cq), GFP_KERNEL);
	if (!cq)
		return NULL;

	ncqe = roundup_pow_of_two(ncqe);
	MLX5_SET(cqc, temp_cqc, log_cq_size, ilog2(ncqe));

	wqp.buf_numa_node = mdev->priv.numa_node;
	wqp.db_numa_node = mdev->priv.numa_node;

	err = mlx5_cqwq_create(mdev, &wqp, temp_cqc, &cq->wq,
			       &cq->wq_ctrl);
	if (err)
		goto out;

	for (i = 0; i < mlx5_cqwq_get_size(&cq->wq); i++) {
		cqe = mlx5_cqwq_get_wqe(&cq->wq, i);
		cqe->op_own = MLX5_CQE_INVALID << 4 | MLX5_CQE_OWNER_MASK;
	}

	inlen = MLX5_ST_SZ_BYTES(create_cq_in) +
		sizeof(u64) * cq->wq_ctrl.buf.npages;
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		goto err_cqwq;

	vector = raw_smp_processor_id() % mlx5_comp_vectors_count(mdev);
	err = mlx5_vector2eqn(mdev, vector, &eqn, &irqn);
	if (err) {
		kvfree(in);
		goto err_cqwq;
	}

	cqc = MLX5_ADDR_OF(create_cq_in, in, cq_context);
	MLX5_SET(cqc, cqc, log_cq_size, ilog2(ncqe));
	MLX5_SET(cqc, cqc, c_eqn, eqn);
	MLX5_SET(cqc, cqc, uar_page, uar->index);
	MLX5_SET(cqc, cqc, log_page_size, cq->wq_ctrl.buf.page_shift -
		 MLX5_ADAPTER_PAGE_SHIFT);
	MLX5_SET64(cqc, cqc, dbr_addr, cq->wq_ctrl.db.dma);

	pas = (__be64 *)MLX5_ADDR_OF(create_cq_in, in, pas);
	mlx5_fill_page_frag_array(&cq->wq_ctrl.buf, pas);

	cq->mcq.event = dr_cq_event;
	cq->mcq.comp  = dr_cq_complete;

	err = mlx5_core_create_cq(mdev, &cq->mcq, in, inlen, out, sizeof(out));
	kvfree(in);

	if (err)
		goto err_cqwq;

	cq->mcq.cqe_sz = 64;
	cq->mcq.set_ci_db = cq->wq_ctrl.db.db;
	cq->mcq.arm_db = cq->wq_ctrl.db.db + 1;
	*cq->mcq.set_ci_db = 0;

	/* set no-zero value, in order to avoid the HW to run db-recovery on
	 * CQ that used in polling mode.
	 */
	*cq->mcq.arm_db = cpu_to_be32(2 << 28);

	cq->mcq.vector = 0;
	cq->mcq.irqn = irqn;
	cq->mcq.uar = uar;

	return cq;

err_cqwq:
	mlx5_wq_destroy(&cq->wq_ctrl);
out:
	kfree(cq);
	return NULL;
}

static void dr_destroy_cq(struct mlx5_core_dev *mdev, struct mlx5dr_cq *cq)
{
	mlx5_core_destroy_cq(mdev, &cq->mcq);
	mlx5_wq_destroy(&cq->wq_ctrl);
	kfree(cq);
}

static int
dr_create_mkey(struct mlx5_core_dev *mdev, u32 pdn, struct mlx5_core_mkey *mkey)
{
	u32 in[MLX5_ST_SZ_DW(create_mkey_in)] = {};
	void *mkc;

	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);
	MLX5_SET(mkc, mkc, access_mode_1_0, MLX5_MKC_ACCESS_MODE_PA);
	MLX5_SET(mkc, mkc, a, 1);
	MLX5_SET(mkc, mkc, rw, 1);
	MLX5_SET(mkc, mkc, rr, 1);
	MLX5_SET(mkc, mkc, lw, 1);
	MLX5_SET(mkc, mkc, lr, 1);

	MLX5_SET(mkc, mkc, pd, pdn);
	MLX5_SET(mkc, mkc, length64, 1);
	MLX5_SET(mkc, mkc, qpn, 0xffffff);

	return mlx5_core_create_mkey(mdev, mkey, in, sizeof(in));
}

static struct mlx5dr_mr *dr_reg_mr(struct mlx5_core_dev *mdev,
				   u32 pdn, void *buf, size_t size)
{
	struct mlx5dr_mr *mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	struct device *dma_device;
	dma_addr_t dma_addr;
	int err;

	if (!mr)
		return NULL;

	dma_device = &mdev->pdev->dev;
	dma_addr = dma_map_single(dma_device, buf, size,
				  DMA_BIDIRECTIONAL);
	err = dma_mapping_error(dma_device, dma_addr);
	if (err) {
		mlx5_core_warn(mdev, "Can't dma buf\n");
		kfree(mr);
		return NULL;
	}

	err = dr_create_mkey(mdev, pdn, &mr->mkey);
	if (err) {
		mlx5_core_warn(mdev, "Can't create mkey\n");
		dma_unmap_single(dma_device, dma_addr, size,
				 DMA_BIDIRECTIONAL);
		kfree(mr);
		return NULL;
	}

	mr->dma_addr = dma_addr;
	mr->size = size;
	mr->addr = buf;

	return mr;
}

static void dr_dereg_mr(struct mlx5_core_dev *mdev, struct mlx5dr_mr *mr)
{
	mlx5_core_destroy_mkey(mdev, &mr->mkey);
	dma_unmap_single(&mdev->pdev->dev, mr->dma_addr, mr->size,
			 DMA_BIDIRECTIONAL);
	kfree(mr);
}

int mlx5dr_send_ring_alloc(struct mlx5dr_domain *dmn)
{
	struct dr_qp_init_attr init_attr = {};
	int cq_size;
	int size;
	int ret;

	dmn->send_ring = kzalloc(sizeof(*dmn->send_ring), GFP_KERNEL);
	if (!dmn->send_ring)
		return -ENOMEM;

	cq_size = QUEUE_SIZE + 1;
	dmn->send_ring->cq = dr_create_cq(dmn->mdev, dmn->uar, cq_size);
	if (!dmn->send_ring->cq) {
		mlx5dr_err(dmn, "Failed creating CQ\n");
		ret = -ENOMEM;
		goto free_send_ring;
	}

	init_attr.cqn = dmn->send_ring->cq->mcq.cqn;
	init_attr.pdn = dmn->pdn;
	init_attr.uar = dmn->uar;
	init_attr.max_send_wr = QUEUE_SIZE;

	dmn->send_ring->qp = dr_create_rc_qp(dmn->mdev, &init_attr);
	if (!dmn->send_ring->qp)  {
		mlx5dr_err(dmn, "Failed creating QP\n");
		ret = -ENOMEM;
		goto clean_cq;
	}

	dmn->send_ring->cq->qp = dmn->send_ring->qp;

	dmn->info.max_send_wr = QUEUE_SIZE;
	dmn->info.max_inline_size = min(dmn->send_ring->qp->max_inline_data,
					DR_STE_SIZE);

	dmn->send_ring->signal_th = dmn->info.max_send_wr /
		SIGNAL_PER_DIV_QUEUE;

	/* Prepare qp to be used */
	ret = dr_prepare_qp_to_rts(dmn);
	if (ret)
		goto clean_qp;

	dmn->send_ring->max_post_send_size =
		mlx5dr_icm_pool_chunk_size_to_byte(DR_CHUNK_SIZE_1K,
						   DR_ICM_TYPE_STE);

	/* Allocating the max size as a buffer for writing */
	size = dmn->send_ring->signal_th * dmn->send_ring->max_post_send_size;
	dmn->send_ring->buf = kzalloc(size, GFP_KERNEL);
	if (!dmn->send_ring->buf) {
		ret = -ENOMEM;
		goto clean_qp;
	}

	dmn->send_ring->buf_size = size;

	dmn->send_ring->mr = dr_reg_mr(dmn->mdev,
				       dmn->pdn, dmn->send_ring->buf, size);
	if (!dmn->send_ring->mr) {
		ret = -ENOMEM;
		goto free_mem;
	}

	dmn->send_ring->sync_mr = dr_reg_mr(dmn->mdev,
					    dmn->pdn, dmn->send_ring->sync_buff,
					    MIN_READ_SYNC);
	if (!dmn->send_ring->sync_mr) {
		ret = -ENOMEM;
		goto clean_mr;
	}

	return 0;

clean_mr:
	dr_dereg_mr(dmn->mdev, dmn->send_ring->mr);
free_mem:
	kfree(dmn->send_ring->buf);
clean_qp:
	dr_destroy_qp(dmn->mdev, dmn->send_ring->qp);
clean_cq:
	dr_destroy_cq(dmn->mdev, dmn->send_ring->cq);
free_send_ring:
	kfree(dmn->send_ring);

	return ret;
}

void mlx5dr_send_ring_free(struct mlx5dr_domain *dmn,
			   struct mlx5dr_send_ring *send_ring)
{
	dr_destroy_qp(dmn->mdev, send_ring->qp);
	dr_destroy_cq(dmn->mdev, send_ring->cq);
	dr_dereg_mr(dmn->mdev, send_ring->sync_mr);
	dr_dereg_mr(dmn->mdev, send_ring->mr);
	kfree(send_ring->buf);
	kfree(send_ring);
}

int mlx5dr_send_ring_force_drain(struct mlx5dr_domain *dmn)
{
	struct mlx5dr_send_ring *send_ring = dmn->send_ring;
	struct postsend_info send_info = {};
	u8 data[DR_STE_SIZE];
	int num_of_sends_req;
	int ret;
	int i;

	/* Sending this amount of requests makes sure we will get drain */
	num_of_sends_req = send_ring->signal_th * TH_NUMS_TO_DRAIN / 2;

	/* Send fake requests forcing the last to be signaled */
	send_info.write.addr = (uintptr_t)data;
	send_info.write.length = DR_STE_SIZE;
	send_info.write.lkey = 0;
	/* Using the sync_mr in order to write/read */
	send_info.remote_addr = (uintptr_t)send_ring->sync_mr->addr;
	send_info.rkey = send_ring->sync_mr->mkey.key;

	for (i = 0; i < num_of_sends_req; i++) {
		ret = dr_postsend_icm_data(dmn, &send_info);
		if (ret)
			return ret;
	}

	ret = dr_handle_pending_wc(dmn, send_ring);

	return ret;
}
