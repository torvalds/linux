// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

/* Authors: Cheng Xu <chengyou@linux.alibaba.com> */
/*          Kai Shen <kaishen@linux.alibaba.com> */
/* Copyright (c) 2020-2021, Alibaba Group */
/* Authors: Bernard Metzler <bmt@zurich.ibm.com> */
/* Copyright (c) 2008-2019, IBM Corporation */

#include "erdma_cm.h"
#include "erdma_verbs.h"

void erdma_qp_llp_close(struct erdma_qp *qp)
{
	struct erdma_mod_qp_params_iwarp params;

	down_write(&qp->state_lock);

	switch (qp->attrs.iwarp.state) {
	case ERDMA_QPS_IWARP_RTS:
	case ERDMA_QPS_IWARP_RTR:
	case ERDMA_QPS_IWARP_IDLE:
	case ERDMA_QPS_IWARP_TERMINATE:
		params.state = ERDMA_QPS_IWARP_CLOSING;
		erdma_modify_qp_state_iwarp(qp, &params, ERDMA_QPA_IWARP_STATE);
		break;
	case ERDMA_QPS_IWARP_CLOSING:
		qp->attrs.iwarp.state = ERDMA_QPS_IWARP_IDLE;
		break;
	default:
		break;
	}

	if (qp->cep) {
		erdma_cep_put(qp->cep);
		qp->cep = NULL;
	}

	up_write(&qp->state_lock);
}

struct ib_qp *erdma_get_ibqp(struct ib_device *ibdev, int id)
{
	struct erdma_qp *qp = find_qp_by_qpn(to_edev(ibdev), id);

	if (qp)
		return &qp->ibqp;

	return NULL;
}

static int
erdma_modify_qp_state_to_rts(struct erdma_qp *qp,
			     struct erdma_mod_qp_params_iwarp *params,
			     enum erdma_qpa_mask_iwarp mask)
{
	int ret;
	struct erdma_dev *dev = qp->dev;
	struct erdma_cmdq_modify_qp_req req;
	struct tcp_sock *tp;
	struct erdma_cep *cep = qp->cep;
	struct sockaddr_storage local_addr, remote_addr;

	if (!(mask & ERDMA_QPA_IWARP_LLP_HANDLE))
		return -EINVAL;

	if (!(mask & ERDMA_QPA_IWARP_MPA))
		return -EINVAL;

	if (!(mask & ERDMA_QPA_IWARP_CC))
		params->cc = qp->attrs.cc;

	ret = getname_local(cep->sock, &local_addr);
	if (ret < 0)
		return ret;

	ret = getname_peer(cep->sock, &remote_addr);
	if (ret < 0)
		return ret;

	tp = tcp_sk(qp->cep->sock->sk);

	erdma_cmdq_build_reqhdr(&req.hdr, CMDQ_SUBMOD_RDMA,
				CMDQ_OPCODE_MODIFY_QP);

	req.cfg = FIELD_PREP(ERDMA_CMD_MODIFY_QP_STATE_MASK, params->state) |
		  FIELD_PREP(ERDMA_CMD_MODIFY_QP_CC_MASK, params->cc) |
		  FIELD_PREP(ERDMA_CMD_MODIFY_QP_QPN_MASK, QP_ID(qp));

	req.cookie = be32_to_cpu(cep->mpa.ext_data.cookie);
	req.dip = to_sockaddr_in(remote_addr).sin_addr.s_addr;
	req.sip = to_sockaddr_in(local_addr).sin_addr.s_addr;
	req.dport = to_sockaddr_in(remote_addr).sin_port;
	req.sport = to_sockaddr_in(local_addr).sin_port;

	req.send_nxt = tp->snd_nxt;
	/* rsvd tcp seq for mpa-rsp in server. */
	if (params->qp_type == ERDMA_QP_PASSIVE)
		req.send_nxt += MPA_DEFAULT_HDR_LEN + params->pd_len;
	req.recv_nxt = tp->rcv_nxt;

	ret = erdma_post_cmd_wait(&dev->cmdq, &req, sizeof(req), NULL, NULL,
				  true);
	if (ret)
		return ret;

	if (mask & ERDMA_QPA_IWARP_IRD)
		qp->attrs.irq_size = params->irq_size;

	if (mask & ERDMA_QPA_IWARP_ORD)
		qp->attrs.orq_size = params->orq_size;

	if (mask & ERDMA_QPA_IWARP_CC)
		qp->attrs.cc = params->cc;

	qp->attrs.iwarp.state = ERDMA_QPS_IWARP_RTS;

	return 0;
}

static int
erdma_modify_qp_state_to_stop(struct erdma_qp *qp,
			      struct erdma_mod_qp_params_iwarp *params,
			      enum erdma_qpa_mask_iwarp mask)
{
	struct erdma_dev *dev = qp->dev;
	struct erdma_cmdq_modify_qp_req req;
	int ret;

	erdma_cmdq_build_reqhdr(&req.hdr, CMDQ_SUBMOD_RDMA,
				CMDQ_OPCODE_MODIFY_QP);

	req.cfg = FIELD_PREP(ERDMA_CMD_MODIFY_QP_STATE_MASK, params->state) |
		  FIELD_PREP(ERDMA_CMD_MODIFY_QP_QPN_MASK, QP_ID(qp));

	ret = erdma_post_cmd_wait(&dev->cmdq, &req, sizeof(req), NULL, NULL,
				  true);
	if (ret)
		return ret;

	qp->attrs.iwarp.state = params->state;

	return 0;
}

int erdma_modify_qp_state_iwarp(struct erdma_qp *qp,
				struct erdma_mod_qp_params_iwarp *params,
				int mask)
{
	bool need_reflush = false;
	int drop_conn, ret = 0;

	if (!mask)
		return 0;

	if (!(mask & ERDMA_QPA_IWARP_STATE))
		return 0;

	switch (qp->attrs.iwarp.state) {
	case ERDMA_QPS_IWARP_IDLE:
	case ERDMA_QPS_IWARP_RTR:
		if (params->state == ERDMA_QPS_IWARP_RTS) {
			ret = erdma_modify_qp_state_to_rts(qp, params, mask);
		} else if (params->state == ERDMA_QPS_IWARP_ERROR) {
			qp->attrs.iwarp.state = ERDMA_QPS_IWARP_ERROR;
			need_reflush = true;
			if (qp->cep) {
				erdma_cep_put(qp->cep);
				qp->cep = NULL;
			}
			ret = erdma_modify_qp_state_to_stop(qp, params, mask);
		}
		break;
	case ERDMA_QPS_IWARP_RTS:
		drop_conn = 0;

		if (params->state == ERDMA_QPS_IWARP_CLOSING ||
		    params->state == ERDMA_QPS_IWARP_TERMINATE ||
		    params->state == ERDMA_QPS_IWARP_ERROR) {
			ret = erdma_modify_qp_state_to_stop(qp, params, mask);
			drop_conn = 1;
			need_reflush = true;
		}

		if (drop_conn)
			erdma_qp_cm_drop(qp);

		break;
	case ERDMA_QPS_IWARP_TERMINATE:
		if (params->state == ERDMA_QPS_IWARP_ERROR)
			qp->attrs.iwarp.state = ERDMA_QPS_IWARP_ERROR;
		break;
	case ERDMA_QPS_IWARP_CLOSING:
		if (params->state == ERDMA_QPS_IWARP_IDLE) {
			qp->attrs.iwarp.state = ERDMA_QPS_IWARP_IDLE;
		} else if (params->state == ERDMA_QPS_IWARP_ERROR) {
			ret = erdma_modify_qp_state_to_stop(qp, params, mask);
			qp->attrs.iwarp.state = ERDMA_QPS_IWARP_ERROR;
		} else if (params->state != ERDMA_QPS_IWARP_CLOSING) {
			return -ECONNABORTED;
		}
		break;
	default:
		break;
	}

	if (need_reflush && !ret && rdma_is_kernel_res(&qp->ibqp.res)) {
		qp->flags |= ERDMA_QP_IN_FLUSHING;
		mod_delayed_work(qp->dev->reflush_wq, &qp->reflush_dwork,
				 usecs_to_jiffies(100));
	}

	return ret;
}

static int modify_qp_cmd_rocev2(struct erdma_qp *qp,
				struct erdma_mod_qp_params_rocev2 *params,
				enum erdma_qpa_mask_rocev2 attr_mask)
{
	struct erdma_cmdq_mod_qp_req_rocev2 req;

	memset(&req, 0, sizeof(req));

	erdma_cmdq_build_reqhdr(&req.hdr, CMDQ_SUBMOD_RDMA,
				CMDQ_OPCODE_MODIFY_QP);

	req.cfg0 = FIELD_PREP(ERDMA_CMD_MODIFY_QP_QPN_MASK, QP_ID(qp));

	if (attr_mask & ERDMA_QPA_ROCEV2_STATE)
		req.cfg0 |= FIELD_PREP(ERDMA_CMD_MODIFY_QP_STATE_MASK,
				       params->state);

	if (attr_mask & ERDMA_QPA_ROCEV2_DST_QPN)
		req.cfg1 = FIELD_PREP(ERDMA_CMD_MODIFY_QP_DQPN_MASK,
				      params->dst_qpn);

	if (attr_mask & ERDMA_QPA_ROCEV2_QKEY)
		req.qkey = params->qkey;

	if (attr_mask & ERDMA_QPA_ROCEV2_AV)
		erdma_set_av_cfg(&req.av_cfg, &params->av);

	if (attr_mask & ERDMA_QPA_ROCEV2_SQ_PSN)
		req.sq_psn = params->sq_psn;

	if (attr_mask & ERDMA_QPA_ROCEV2_RQ_PSN)
		req.rq_psn = params->rq_psn;

	req.attr_mask = attr_mask;

	return erdma_post_cmd_wait(&qp->dev->cmdq, &req, sizeof(req), NULL,
				   NULL, true);
}

static void erdma_reset_qp(struct erdma_qp *qp)
{
	qp->kern_qp.sq_pi = 0;
	qp->kern_qp.sq_ci = 0;
	qp->kern_qp.rq_pi = 0;
	qp->kern_qp.rq_ci = 0;
	memset(qp->kern_qp.swr_tbl, 0, qp->attrs.sq_size * sizeof(u64));
	memset(qp->kern_qp.rwr_tbl, 0, qp->attrs.rq_size * sizeof(u64));
	memset(qp->kern_qp.sq_buf, 0, qp->attrs.sq_size << SQEBB_SHIFT);
	memset(qp->kern_qp.rq_buf, 0, qp->attrs.rq_size << RQE_SHIFT);
	erdma_remove_cqes_of_qp(&qp->scq->ibcq, QP_ID(qp));
	if (qp->rcq != qp->scq)
		erdma_remove_cqes_of_qp(&qp->rcq->ibcq, QP_ID(qp));
}

int erdma_modify_qp_state_rocev2(struct erdma_qp *qp,
				 struct erdma_mod_qp_params_rocev2 *params,
				 int attr_mask)
{
	struct erdma_dev *dev = to_edev(qp->ibqp.device);
	int ret;

	ret = modify_qp_cmd_rocev2(qp, params, attr_mask);
	if (ret)
		return ret;

	if (attr_mask & ERDMA_QPA_ROCEV2_STATE)
		qp->attrs.rocev2.state = params->state;

	if (attr_mask & ERDMA_QPA_ROCEV2_QKEY)
		qp->attrs.rocev2.qkey = params->qkey;

	if (attr_mask & ERDMA_QPA_ROCEV2_DST_QPN)
		qp->attrs.rocev2.dst_qpn = params->dst_qpn;

	if (attr_mask & ERDMA_QPA_ROCEV2_AV)
		memcpy(&qp->attrs.rocev2.av, &params->av,
		       sizeof(struct erdma_av));

	if (rdma_is_kernel_res(&qp->ibqp.res) &&
	    params->state == ERDMA_QPS_ROCEV2_RESET)
		erdma_reset_qp(qp);

	if (rdma_is_kernel_res(&qp->ibqp.res) &&
	    params->state == ERDMA_QPS_ROCEV2_ERROR) {
		qp->flags |= ERDMA_QP_IN_FLUSHING;
		mod_delayed_work(dev->reflush_wq, &qp->reflush_dwork,
				 usecs_to_jiffies(100));
	}

	return 0;
}

static void erdma_qp_safe_free(struct kref *ref)
{
	struct erdma_qp *qp = container_of(ref, struct erdma_qp, ref);

	complete(&qp->safe_free);
}

void erdma_qp_put(struct erdma_qp *qp)
{
	WARN_ON(kref_read(&qp->ref) < 1);
	kref_put(&qp->ref, erdma_qp_safe_free);
}

void erdma_qp_get(struct erdma_qp *qp)
{
	kref_get(&qp->ref);
}

static int fill_inline_data(struct erdma_qp *qp,
			    const struct ib_send_wr *send_wr, u16 wqe_idx,
			    u32 sgl_offset, __le32 *length_field)
{
	u32 remain_size, copy_size, data_off, bytes = 0;
	char *data;
	int i = 0;

	wqe_idx += (sgl_offset >> SQEBB_SHIFT);
	sgl_offset &= (SQEBB_SIZE - 1);
	data = get_queue_entry(qp->kern_qp.sq_buf, wqe_idx, qp->attrs.sq_size,
			       SQEBB_SHIFT);

	while (i < send_wr->num_sge) {
		bytes += send_wr->sg_list[i].length;
		if (bytes > (int)ERDMA_MAX_INLINE)
			return -EINVAL;

		remain_size = send_wr->sg_list[i].length;
		data_off = 0;

		while (1) {
			copy_size = min(remain_size, SQEBB_SIZE - sgl_offset);

			memcpy(data + sgl_offset,
			       (void *)(uintptr_t)send_wr->sg_list[i].addr +
				       data_off,
			       copy_size);
			remain_size -= copy_size;
			data_off += copy_size;
			sgl_offset += copy_size;
			wqe_idx += (sgl_offset >> SQEBB_SHIFT);
			sgl_offset &= (SQEBB_SIZE - 1);

			data = get_queue_entry(qp->kern_qp.sq_buf, wqe_idx,
					       qp->attrs.sq_size, SQEBB_SHIFT);
			if (!remain_size)
				break;
		}

		i++;
	}
	*length_field = cpu_to_le32(bytes);

	return bytes;
}

static int fill_sgl(struct erdma_qp *qp, const struct ib_send_wr *send_wr,
		    u16 wqe_idx, u32 sgl_offset, __le32 *length_field)
{
	int i = 0;
	u32 bytes = 0;
	char *sgl;

	if (send_wr->num_sge > qp->dev->attrs.max_send_sge)
		return -EINVAL;

	if (sgl_offset & 0xF)
		return -EINVAL;

	while (i < send_wr->num_sge) {
		wqe_idx += (sgl_offset >> SQEBB_SHIFT);
		sgl_offset &= (SQEBB_SIZE - 1);
		sgl = get_queue_entry(qp->kern_qp.sq_buf, wqe_idx,
				      qp->attrs.sq_size, SQEBB_SHIFT);

		bytes += send_wr->sg_list[i].length;
		memcpy(sgl + sgl_offset, &send_wr->sg_list[i],
		       sizeof(struct ib_sge));

		sgl_offset += sizeof(struct ib_sge);
		i++;
	}

	*length_field = cpu_to_le32(bytes);
	return 0;
}

static void init_send_sqe_rc(struct erdma_qp *qp, struct erdma_send_sqe_rc *sqe,
			     const struct ib_send_wr *wr, u32 *hw_op)
{
	u32 op = ERDMA_OP_SEND;

	if (wr->opcode == IB_WR_SEND_WITH_IMM) {
		op = ERDMA_OP_SEND_WITH_IMM;
		sqe->imm_data = wr->ex.imm_data;
	} else if (wr->opcode == IB_WR_SEND_WITH_INV) {
		op = ERDMA_OP_SEND_WITH_INV;
		sqe->invalid_stag = cpu_to_le32(wr->ex.invalidate_rkey);
	}

	*hw_op = op;
}

static void init_send_sqe_ud(struct erdma_qp *qp, struct erdma_send_sqe_ud *sqe,
			     const struct ib_send_wr *wr, u32 *hw_op)
{
	const struct ib_ud_wr *uwr = ud_wr(wr);
	struct erdma_ah *ah = to_eah(uwr->ah);
	u32 op = ERDMA_OP_SEND;

	if (wr->opcode == IB_WR_SEND_WITH_IMM) {
		op = ERDMA_OP_SEND_WITH_IMM;
		sqe->imm_data = wr->ex.imm_data;
	}

	*hw_op = op;

	sqe->ahn = cpu_to_le32(ah->ahn);
	sqe->dst_qpn = cpu_to_le32(uwr->remote_qpn);
	/* Not allowed to send control qkey */
	if (uwr->remote_qkey & 0x80000000)
		sqe->qkey = cpu_to_le32(qp->attrs.rocev2.qkey);
	else
		sqe->qkey = cpu_to_le32(uwr->remote_qkey);
}

static int erdma_push_one_sqe(struct erdma_qp *qp, u16 *pi,
			      const struct ib_send_wr *send_wr)
{
	u32 wqe_size, wqebb_cnt, hw_op, flags, sgl_offset;
	u32 idx = *pi & (qp->attrs.sq_size - 1);
	enum ib_wr_opcode op = send_wr->opcode;
	struct erdma_send_sqe_rc *rc_send_sqe;
	struct erdma_send_sqe_ud *ud_send_sqe;
	struct erdma_atomic_sqe *atomic_sqe;
	struct erdma_readreq_sqe *read_sqe;
	struct erdma_reg_mr_sqe *regmr_sge;
	struct erdma_write_sqe *write_sqe;
	struct ib_rdma_wr *rdma_wr;
	struct erdma_sge *sge;
	__le32 *length_field;
	struct erdma_mr *mr;
	u64 wqe_hdr, *entry;
	u32 attrs;
	int ret;

	if (qp->ibqp.qp_type != IB_QPT_RC && send_wr->opcode != IB_WR_SEND &&
	    send_wr->opcode != IB_WR_SEND_WITH_IMM)
		return -EINVAL;

	entry = get_queue_entry(qp->kern_qp.sq_buf, idx, qp->attrs.sq_size,
				SQEBB_SHIFT);

	/* Clear the SQE header section. */
	*entry = 0;

	qp->kern_qp.swr_tbl[idx] = send_wr->wr_id;
	flags = send_wr->send_flags;
	wqe_hdr = FIELD_PREP(
		ERDMA_SQE_HDR_CE_MASK,
		((flags & IB_SEND_SIGNALED) || qp->kern_qp.sig_all) ? 1 : 0);
	wqe_hdr |= FIELD_PREP(ERDMA_SQE_HDR_SE_MASK,
			      flags & IB_SEND_SOLICITED ? 1 : 0);
	wqe_hdr |= FIELD_PREP(ERDMA_SQE_HDR_FENCE_MASK,
			      flags & IB_SEND_FENCE ? 1 : 0);
	wqe_hdr |= FIELD_PREP(ERDMA_SQE_HDR_INLINE_MASK,
			      flags & IB_SEND_INLINE ? 1 : 0);
	wqe_hdr |= FIELD_PREP(ERDMA_SQE_HDR_QPN_MASK, QP_ID(qp));

	switch (op) {
	case IB_WR_RDMA_WRITE:
	case IB_WR_RDMA_WRITE_WITH_IMM:
		hw_op = ERDMA_OP_WRITE;
		if (op == IB_WR_RDMA_WRITE_WITH_IMM)
			hw_op = ERDMA_OP_WRITE_WITH_IMM;
		wqe_hdr |= FIELD_PREP(ERDMA_SQE_HDR_OPCODE_MASK, hw_op);
		rdma_wr = container_of(send_wr, struct ib_rdma_wr, wr);
		write_sqe = (struct erdma_write_sqe *)entry;

		write_sqe->imm_data = send_wr->ex.imm_data;
		write_sqe->sink_stag = cpu_to_le32(rdma_wr->rkey);
		write_sqe->sink_to_h =
			cpu_to_le32(upper_32_bits(rdma_wr->remote_addr));
		write_sqe->sink_to_l =
			cpu_to_le32(lower_32_bits(rdma_wr->remote_addr));

		length_field = &write_sqe->length;
		wqe_size = sizeof(struct erdma_write_sqe);
		sgl_offset = wqe_size;
		break;
	case IB_WR_RDMA_READ:
	case IB_WR_RDMA_READ_WITH_INV:
		read_sqe = (struct erdma_readreq_sqe *)entry;
		if (unlikely(send_wr->num_sge != 1))
			return -EINVAL;
		hw_op = ERDMA_OP_READ;
		if (op == IB_WR_RDMA_READ_WITH_INV) {
			hw_op = ERDMA_OP_READ_WITH_INV;
			read_sqe->invalid_stag =
				cpu_to_le32(send_wr->ex.invalidate_rkey);
		}

		wqe_hdr |= FIELD_PREP(ERDMA_SQE_HDR_OPCODE_MASK, hw_op);
		rdma_wr = container_of(send_wr, struct ib_rdma_wr, wr);
		read_sqe->length = cpu_to_le32(send_wr->sg_list[0].length);
		read_sqe->sink_stag = cpu_to_le32(send_wr->sg_list[0].lkey);
		read_sqe->sink_to_l =
			cpu_to_le32(lower_32_bits(send_wr->sg_list[0].addr));
		read_sqe->sink_to_h =
			cpu_to_le32(upper_32_bits(send_wr->sg_list[0].addr));

		sge = get_queue_entry(qp->kern_qp.sq_buf, idx + 1,
				      qp->attrs.sq_size, SQEBB_SHIFT);
		sge->addr = cpu_to_le64(rdma_wr->remote_addr);
		sge->key = cpu_to_le32(rdma_wr->rkey);
		sge->length = cpu_to_le32(send_wr->sg_list[0].length);
		wqe_size = sizeof(struct erdma_readreq_sqe) +
			   send_wr->num_sge * sizeof(struct ib_sge);

		goto out;
	case IB_WR_SEND:
	case IB_WR_SEND_WITH_IMM:
	case IB_WR_SEND_WITH_INV:
		if (qp->ibqp.qp_type == IB_QPT_RC) {
			rc_send_sqe = (struct erdma_send_sqe_rc *)entry;
			init_send_sqe_rc(qp, rc_send_sqe, send_wr, &hw_op);
			length_field = &rc_send_sqe->length;
			wqe_size = sizeof(struct erdma_send_sqe_rc);
		} else {
			ud_send_sqe = (struct erdma_send_sqe_ud *)entry;
			init_send_sqe_ud(qp, ud_send_sqe, send_wr, &hw_op);
			length_field = &ud_send_sqe->length;
			wqe_size = sizeof(struct erdma_send_sqe_ud);
		}

		sgl_offset = wqe_size;
		wqe_hdr |= FIELD_PREP(ERDMA_SQE_HDR_OPCODE_MASK, hw_op);
		break;
	case IB_WR_REG_MR:
		wqe_hdr |=
			FIELD_PREP(ERDMA_SQE_HDR_OPCODE_MASK, ERDMA_OP_REG_MR);
		regmr_sge = (struct erdma_reg_mr_sqe *)entry;
		mr = to_emr(reg_wr(send_wr)->mr);

		mr->access = ERDMA_MR_ACC_LR |
			     to_erdma_access_flags(reg_wr(send_wr)->access);
		regmr_sge->addr = cpu_to_le64(mr->ibmr.iova);
		regmr_sge->length = cpu_to_le32(mr->ibmr.length);
		regmr_sge->stag = cpu_to_le32(reg_wr(send_wr)->key);
		attrs = FIELD_PREP(ERDMA_SQE_MR_ACCESS_MASK, mr->access) |
			FIELD_PREP(ERDMA_SQE_MR_MTT_CNT_MASK,
				   mr->mem.mtt_nents);

		if (mr->mem.mtt_nents <= ERDMA_MAX_INLINE_MTT_ENTRIES) {
			attrs |= FIELD_PREP(ERDMA_SQE_MR_MTT_TYPE_MASK, 0);
			/* Copy SGLs to SQE content to accelerate */
			memcpy(get_queue_entry(qp->kern_qp.sq_buf, idx + 1,
					       qp->attrs.sq_size, SQEBB_SHIFT),
			       mr->mem.mtt->buf, MTT_SIZE(mr->mem.mtt_nents));
			wqe_size = sizeof(struct erdma_reg_mr_sqe) +
				   MTT_SIZE(mr->mem.mtt_nents);
		} else {
			attrs |= FIELD_PREP(ERDMA_SQE_MR_MTT_TYPE_MASK, 1);
			wqe_size = sizeof(struct erdma_reg_mr_sqe);
		}

		regmr_sge->attrs = cpu_to_le32(attrs);
		goto out;
	case IB_WR_LOCAL_INV:
		wqe_hdr |= FIELD_PREP(ERDMA_SQE_HDR_OPCODE_MASK,
				      ERDMA_OP_LOCAL_INV);
		regmr_sge = (struct erdma_reg_mr_sqe *)entry;
		regmr_sge->stag = cpu_to_le32(send_wr->ex.invalidate_rkey);
		wqe_size = sizeof(struct erdma_reg_mr_sqe);
		goto out;
	case IB_WR_ATOMIC_CMP_AND_SWP:
	case IB_WR_ATOMIC_FETCH_AND_ADD:
		atomic_sqe = (struct erdma_atomic_sqe *)entry;
		if (op == IB_WR_ATOMIC_CMP_AND_SWP) {
			wqe_hdr |= FIELD_PREP(ERDMA_SQE_HDR_OPCODE_MASK,
					      ERDMA_OP_ATOMIC_CAS);
			atomic_sqe->fetchadd_swap_data =
				cpu_to_le64(atomic_wr(send_wr)->swap);
			atomic_sqe->cmp_data =
				cpu_to_le64(atomic_wr(send_wr)->compare_add);
		} else {
			wqe_hdr |= FIELD_PREP(ERDMA_SQE_HDR_OPCODE_MASK,
					      ERDMA_OP_ATOMIC_FAA);
			atomic_sqe->fetchadd_swap_data =
				cpu_to_le64(atomic_wr(send_wr)->compare_add);
		}

		sge = get_queue_entry(qp->kern_qp.sq_buf, idx + 1,
				      qp->attrs.sq_size, SQEBB_SHIFT);
		sge->addr = cpu_to_le64(atomic_wr(send_wr)->remote_addr);
		sge->key = cpu_to_le32(atomic_wr(send_wr)->rkey);
		sge++;

		sge->addr = cpu_to_le64(send_wr->sg_list[0].addr);
		sge->key = cpu_to_le32(send_wr->sg_list[0].lkey);
		sge->length = cpu_to_le32(send_wr->sg_list[0].length);

		wqe_size = sizeof(*atomic_sqe);
		goto out;
	default:
		return -EOPNOTSUPP;
	}

	if (flags & IB_SEND_INLINE) {
		ret = fill_inline_data(qp, send_wr, idx, sgl_offset,
				       length_field);
		if (ret < 0)
			return -EINVAL;
		wqe_size += ret;
		wqe_hdr |= FIELD_PREP(ERDMA_SQE_HDR_SGL_LEN_MASK, ret);
	} else {
		ret = fill_sgl(qp, send_wr, idx, sgl_offset, length_field);
		if (ret)
			return -EINVAL;
		wqe_size += send_wr->num_sge * sizeof(struct ib_sge);
		wqe_hdr |= FIELD_PREP(ERDMA_SQE_HDR_SGL_LEN_MASK,
				      send_wr->num_sge);
	}

out:
	wqebb_cnt = SQEBB_COUNT(wqe_size);
	wqe_hdr |= FIELD_PREP(ERDMA_SQE_HDR_WQEBB_CNT_MASK, wqebb_cnt - 1);
	*pi += wqebb_cnt;
	wqe_hdr |= FIELD_PREP(ERDMA_SQE_HDR_WQEBB_INDEX_MASK, *pi);

	*entry = wqe_hdr;

	return 0;
}

static void kick_sq_db(struct erdma_qp *qp, u16 pi)
{
	u64 db_data = FIELD_PREP(ERDMA_SQE_HDR_QPN_MASK, QP_ID(qp)) |
		      FIELD_PREP(ERDMA_SQE_HDR_WQEBB_INDEX_MASK, pi);

	*(u64 *)qp->kern_qp.sq_dbrec = db_data;
	writeq(db_data, qp->kern_qp.hw_sq_db);
}

int erdma_post_send(struct ib_qp *ibqp, const struct ib_send_wr *send_wr,
		    const struct ib_send_wr **bad_send_wr)
{
	struct erdma_qp *qp = to_eqp(ibqp);
	int ret = 0;
	const struct ib_send_wr *wr = send_wr;
	unsigned long flags;
	u16 sq_pi;

	if (!send_wr)
		return -EINVAL;

	spin_lock_irqsave(&qp->lock, flags);
	sq_pi = qp->kern_qp.sq_pi;

	while (wr) {
		if ((u16)(sq_pi - qp->kern_qp.sq_ci) >= qp->attrs.sq_size) {
			ret = -ENOMEM;
			*bad_send_wr = send_wr;
			break;
		}

		ret = erdma_push_one_sqe(qp, &sq_pi, wr);
		if (ret) {
			*bad_send_wr = wr;
			break;
		}
		qp->kern_qp.sq_pi = sq_pi;
		kick_sq_db(qp, sq_pi);

		wr = wr->next;
	}
	spin_unlock_irqrestore(&qp->lock, flags);

	if (unlikely(qp->flags & ERDMA_QP_IN_FLUSHING))
		mod_delayed_work(qp->dev->reflush_wq, &qp->reflush_dwork,
				 usecs_to_jiffies(100));

	return ret;
}

static int erdma_post_recv_one(struct erdma_qp *qp,
			       const struct ib_recv_wr *recv_wr)
{
	struct erdma_rqe *rqe =
		get_queue_entry(qp->kern_qp.rq_buf, qp->kern_qp.rq_pi,
				qp->attrs.rq_size, RQE_SHIFT);

	rqe->qe_idx = cpu_to_le16(qp->kern_qp.rq_pi + 1);
	rqe->qpn = cpu_to_le32(QP_ID(qp));

	if (recv_wr->num_sge == 0) {
		rqe->length = 0;
	} else if (recv_wr->num_sge == 1) {
		rqe->stag = cpu_to_le32(recv_wr->sg_list[0].lkey);
		rqe->to = cpu_to_le64(recv_wr->sg_list[0].addr);
		rqe->length = cpu_to_le32(recv_wr->sg_list[0].length);
	} else {
		return -EINVAL;
	}

	*(u64 *)qp->kern_qp.rq_dbrec = *(u64 *)rqe;
	writeq(*(u64 *)rqe, qp->kern_qp.hw_rq_db);

	qp->kern_qp.rwr_tbl[qp->kern_qp.rq_pi & (qp->attrs.rq_size - 1)] =
		recv_wr->wr_id;
	qp->kern_qp.rq_pi++;

	return 0;
}

int erdma_post_recv(struct ib_qp *ibqp, const struct ib_recv_wr *recv_wr,
		    const struct ib_recv_wr **bad_recv_wr)
{
	const struct ib_recv_wr *wr = recv_wr;
	struct erdma_qp *qp = to_eqp(ibqp);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&qp->lock, flags);

	while (wr) {
		ret = erdma_post_recv_one(qp, wr);
		if (ret) {
			*bad_recv_wr = wr;
			break;
		}
		wr = wr->next;
	}

	spin_unlock_irqrestore(&qp->lock, flags);

	if (unlikely(qp->flags & ERDMA_QP_IN_FLUSHING))
		mod_delayed_work(qp->dev->reflush_wq, &qp->reflush_dwork,
				 usecs_to_jiffies(100));

	return ret;
}
