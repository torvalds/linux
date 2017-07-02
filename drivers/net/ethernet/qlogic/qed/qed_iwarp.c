/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
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
 *        disclaimer in the documentation and /or other materials
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
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include "qed_cxt.h"
#include "qed_hw.h"
#include "qed_ll2.h"
#include "qed_rdma.h"
#include "qed_reg_addr.h"
#include "qed_sp.h"

#define QED_IWARP_ORD_DEFAULT		32
#define QED_IWARP_IRD_DEFAULT		32
#define QED_IWARP_RCV_WND_SIZE_DEF	(256 * 1024)
#define QED_IWARP_RCV_WND_SIZE_MIN	(64 * 1024)
#define QED_IWARP_TS_EN			BIT(0)
#define QED_IWARP_PARAM_CRC_NEEDED	(1)
#define QED_IWARP_PARAM_P2P		(1)

static int qed_iwarp_async_event(struct qed_hwfn *p_hwfn,
				 u8 fw_event_code, u16 echo,
				 union event_ring_data *data,
				 u8 fw_return_code);

/* Override devinfo with iWARP specific values */
void qed_iwarp_init_devinfo(struct qed_hwfn *p_hwfn)
{
	struct qed_rdma_device *dev = p_hwfn->p_rdma_info->dev;

	dev->max_inline = IWARP_REQ_MAX_INLINE_DATA_SIZE;
	dev->max_qp = min_t(u32,
			    IWARP_MAX_QPS,
			    p_hwfn->p_rdma_info->num_qps);

	dev->max_cq = dev->max_qp;

	dev->max_qp_resp_rd_atomic_resc = QED_IWARP_IRD_DEFAULT;
	dev->max_qp_req_rd_atomic_resc = QED_IWARP_ORD_DEFAULT;
}

void qed_iwarp_init_hw(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	p_hwfn->rdma_prs_search_reg = PRS_REG_SEARCH_TCP;
	qed_wr(p_hwfn, p_ptt, p_hwfn->rdma_prs_search_reg, 1);
	p_hwfn->b_rdma_enabled_in_prs = true;
}

static void qed_iwarp_cid_cleaned(struct qed_hwfn *p_hwfn, u32 cid)
{
	cid -= qed_cxt_get_proto_cid_start(p_hwfn, p_hwfn->p_rdma_info->proto);

	spin_lock_bh(&p_hwfn->p_rdma_info->lock);
	qed_bmap_release_id(p_hwfn, &p_hwfn->p_rdma_info->cid_map, cid);
	spin_unlock_bh(&p_hwfn->p_rdma_info->lock);
}

static int qed_iwarp_alloc_cid(struct qed_hwfn *p_hwfn, u32 *cid)
{
	int rc;

	spin_lock_bh(&p_hwfn->p_rdma_info->lock);
	rc = qed_rdma_bmap_alloc_id(p_hwfn, &p_hwfn->p_rdma_info->cid_map, cid);
	spin_unlock_bh(&p_hwfn->p_rdma_info->lock);
	if (rc) {
		DP_NOTICE(p_hwfn, "Failed in allocating iwarp cid\n");
		return rc;
	}
	*cid += qed_cxt_get_proto_cid_start(p_hwfn, p_hwfn->p_rdma_info->proto);

	rc = qed_cxt_dynamic_ilt_alloc(p_hwfn, QED_ELEM_CXT, *cid);
	if (rc)
		qed_iwarp_cid_cleaned(p_hwfn, *cid);

	return rc;
}

int qed_iwarp_create_qp(struct qed_hwfn *p_hwfn,
			struct qed_rdma_qp *qp,
			struct qed_rdma_create_qp_out_params *out_params)
{
	struct iwarp_create_qp_ramrod_data *p_ramrod;
	struct qed_sp_init_data init_data;
	struct qed_spq_entry *p_ent;
	u16 physical_queue;
	u32 cid;
	int rc;

	qp->shared_queue = dma_alloc_coherent(&p_hwfn->cdev->pdev->dev,
					      IWARP_SHARED_QUEUE_PAGE_SIZE,
					      &qp->shared_queue_phys_addr,
					      GFP_KERNEL);
	if (!qp->shared_queue)
		return -ENOMEM;

	out_params->sq_pbl_virt = (u8 *)qp->shared_queue +
	    IWARP_SHARED_QUEUE_PAGE_SQ_PBL_OFFSET;
	out_params->sq_pbl_phys = qp->shared_queue_phys_addr +
	    IWARP_SHARED_QUEUE_PAGE_SQ_PBL_OFFSET;
	out_params->rq_pbl_virt = (u8 *)qp->shared_queue +
	    IWARP_SHARED_QUEUE_PAGE_RQ_PBL_OFFSET;
	out_params->rq_pbl_phys = qp->shared_queue_phys_addr +
	    IWARP_SHARED_QUEUE_PAGE_RQ_PBL_OFFSET;

	rc = qed_iwarp_alloc_cid(p_hwfn, &cid);
	if (rc)
		goto err1;

	qp->icid = (u16)cid;

	memset(&init_data, 0, sizeof(init_data));
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.cid = qp->icid;
	init_data.comp_mode = QED_SPQ_MODE_EBLOCK;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 IWARP_RAMROD_CMD_ID_CREATE_QP,
				 PROTOCOLID_IWARP, &init_data);
	if (rc)
		goto err2;

	p_ramrod = &p_ent->ramrod.iwarp_create_qp;

	SET_FIELD(p_ramrod->flags,
		  IWARP_CREATE_QP_RAMROD_DATA_FMR_AND_RESERVED_EN,
		  qp->fmr_and_reserved_lkey);

	SET_FIELD(p_ramrod->flags,
		  IWARP_CREATE_QP_RAMROD_DATA_SIGNALED_COMP, qp->signal_all);

	SET_FIELD(p_ramrod->flags,
		  IWARP_CREATE_QP_RAMROD_DATA_RDMA_RD_EN,
		  qp->incoming_rdma_read_en);

	SET_FIELD(p_ramrod->flags,
		  IWARP_CREATE_QP_RAMROD_DATA_RDMA_WR_EN,
		  qp->incoming_rdma_write_en);

	SET_FIELD(p_ramrod->flags,
		  IWARP_CREATE_QP_RAMROD_DATA_ATOMIC_EN,
		  qp->incoming_atomic_en);

	SET_FIELD(p_ramrod->flags,
		  IWARP_CREATE_QP_RAMROD_DATA_SRQ_FLG, qp->use_srq);

	p_ramrod->pd = qp->pd;
	p_ramrod->sq_num_pages = qp->sq_num_pages;
	p_ramrod->rq_num_pages = qp->rq_num_pages;

	p_ramrod->qp_handle_for_cqe.hi = cpu_to_le32(qp->qp_handle.hi);
	p_ramrod->qp_handle_for_cqe.lo = cpu_to_le32(qp->qp_handle.lo);

	p_ramrod->cq_cid_for_sq =
	    cpu_to_le32((p_hwfn->hw_info.opaque_fid << 16) | qp->sq_cq_id);
	p_ramrod->cq_cid_for_rq =
	    cpu_to_le32((p_hwfn->hw_info.opaque_fid << 16) | qp->rq_cq_id);

	p_ramrod->dpi = cpu_to_le16(qp->dpi);

	physical_queue = qed_get_cm_pq_idx(p_hwfn, PQ_FLAGS_OFLD);
	p_ramrod->physical_q0 = cpu_to_le16(physical_queue);
	physical_queue = qed_get_cm_pq_idx(p_hwfn, PQ_FLAGS_ACK);
	p_ramrod->physical_q1 = cpu_to_le16(physical_queue);

	rc = qed_spq_post(p_hwfn, p_ent, NULL);
	if (rc)
		goto err2;

	return rc;

err2:
	qed_iwarp_cid_cleaned(p_hwfn, cid);
err1:
	dma_free_coherent(&p_hwfn->cdev->pdev->dev,
			  IWARP_SHARED_QUEUE_PAGE_SIZE,
			  qp->shared_queue, qp->shared_queue_phys_addr);

	return rc;
}

static int qed_iwarp_modify_fw(struct qed_hwfn *p_hwfn, struct qed_rdma_qp *qp)
{
	struct iwarp_modify_qp_ramrod_data *p_ramrod;
	struct qed_sp_init_data init_data;
	struct qed_spq_entry *p_ent;
	int rc;

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = qp->icid;
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = QED_SPQ_MODE_EBLOCK;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 IWARP_RAMROD_CMD_ID_MODIFY_QP,
				 p_hwfn->p_rdma_info->proto, &init_data);
	if (rc)
		return rc;

	p_ramrod = &p_ent->ramrod.iwarp_modify_qp;
	SET_FIELD(p_ramrod->flags, IWARP_MODIFY_QP_RAMROD_DATA_STATE_TRANS_EN,
		  0x1);
	if (qp->iwarp_state == QED_IWARP_QP_STATE_CLOSING)
		p_ramrod->transition_to_state = IWARP_MODIFY_QP_STATE_CLOSING;
	else
		p_ramrod->transition_to_state = IWARP_MODIFY_QP_STATE_ERROR;

	rc = qed_spq_post(p_hwfn, p_ent, NULL);

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "QP(0x%x)rc=%d\n", qp->icid, rc);

	return rc;
}

enum qed_iwarp_qp_state qed_roce2iwarp_state(enum qed_roce_qp_state state)
{
	switch (state) {
	case QED_ROCE_QP_STATE_RESET:
	case QED_ROCE_QP_STATE_INIT:
	case QED_ROCE_QP_STATE_RTR:
		return QED_IWARP_QP_STATE_IDLE;
	case QED_ROCE_QP_STATE_RTS:
		return QED_IWARP_QP_STATE_RTS;
	case QED_ROCE_QP_STATE_SQD:
		return QED_IWARP_QP_STATE_CLOSING;
	case QED_ROCE_QP_STATE_ERR:
		return QED_IWARP_QP_STATE_ERROR;
	case QED_ROCE_QP_STATE_SQE:
		return QED_IWARP_QP_STATE_TERMINATE;
	default:
		return QED_IWARP_QP_STATE_ERROR;
	}
}

static enum qed_roce_qp_state
qed_iwarp2roce_state(enum qed_iwarp_qp_state state)
{
	switch (state) {
	case QED_IWARP_QP_STATE_IDLE:
		return QED_ROCE_QP_STATE_INIT;
	case QED_IWARP_QP_STATE_RTS:
		return QED_ROCE_QP_STATE_RTS;
	case QED_IWARP_QP_STATE_TERMINATE:
		return QED_ROCE_QP_STATE_SQE;
	case QED_IWARP_QP_STATE_CLOSING:
		return QED_ROCE_QP_STATE_SQD;
	case QED_IWARP_QP_STATE_ERROR:
		return QED_ROCE_QP_STATE_ERR;
	default:
		return QED_ROCE_QP_STATE_ERR;
	}
}

const char *iwarp_state_names[] = {
	"IDLE",
	"RTS",
	"TERMINATE",
	"CLOSING",
	"ERROR",
};

int
qed_iwarp_modify_qp(struct qed_hwfn *p_hwfn,
		    struct qed_rdma_qp *qp,
		    enum qed_iwarp_qp_state new_state, bool internal)
{
	enum qed_iwarp_qp_state prev_iw_state;
	bool modify_fw = false;
	int rc = 0;

	/* modify QP can be called from upper-layer or as a result of async
	 * RST/FIN... therefore need to protect
	 */
	spin_lock_bh(&p_hwfn->p_rdma_info->iwarp.qp_lock);
	prev_iw_state = qp->iwarp_state;

	if (prev_iw_state == new_state) {
		spin_unlock_bh(&p_hwfn->p_rdma_info->iwarp.qp_lock);
		return 0;
	}

	switch (prev_iw_state) {
	case QED_IWARP_QP_STATE_IDLE:
		switch (new_state) {
		case QED_IWARP_QP_STATE_RTS:
			qp->iwarp_state = QED_IWARP_QP_STATE_RTS;
			break;
		case QED_IWARP_QP_STATE_ERROR:
			qp->iwarp_state = QED_IWARP_QP_STATE_ERROR;
			if (!internal)
				modify_fw = true;
			break;
		default:
			break;
		}
		break;
	case QED_IWARP_QP_STATE_RTS:
		switch (new_state) {
		case QED_IWARP_QP_STATE_CLOSING:
			if (!internal)
				modify_fw = true;

			qp->iwarp_state = QED_IWARP_QP_STATE_CLOSING;
			break;
		case QED_IWARP_QP_STATE_ERROR:
			if (!internal)
				modify_fw = true;
			qp->iwarp_state = QED_IWARP_QP_STATE_ERROR;
			break;
		default:
			break;
		}
		break;
	case QED_IWARP_QP_STATE_ERROR:
		switch (new_state) {
		case QED_IWARP_QP_STATE_IDLE:

			qp->iwarp_state = new_state;
			break;
		case QED_IWARP_QP_STATE_CLOSING:
			/* could happen due to race... do nothing.... */
			break;
		default:
			rc = -EINVAL;
		}
		break;
	case QED_IWARP_QP_STATE_TERMINATE:
	case QED_IWARP_QP_STATE_CLOSING:
		qp->iwarp_state = new_state;
		break;
	default:
		break;
	}

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "QP(0x%x) %s --> %s%s\n",
		   qp->icid,
		   iwarp_state_names[prev_iw_state],
		   iwarp_state_names[qp->iwarp_state],
		   internal ? "internal" : "");

	spin_unlock_bh(&p_hwfn->p_rdma_info->iwarp.qp_lock);

	if (modify_fw)
		rc = qed_iwarp_modify_fw(p_hwfn, qp);

	return rc;
}

int qed_iwarp_fw_destroy(struct qed_hwfn *p_hwfn, struct qed_rdma_qp *qp)
{
	struct qed_sp_init_data init_data;
	struct qed_spq_entry *p_ent;
	int rc;

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = qp->icid;
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = QED_SPQ_MODE_EBLOCK;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 IWARP_RAMROD_CMD_ID_DESTROY_QP,
				 p_hwfn->p_rdma_info->proto, &init_data);
	if (rc)
		return rc;

	rc = qed_spq_post(p_hwfn, p_ent, NULL);

	DP_VERBOSE(p_hwfn, QED_MSG_RDMA, "QP(0x%x) rc = %d\n", qp->icid, rc);

	return rc;
}

int qed_iwarp_destroy_qp(struct qed_hwfn *p_hwfn, struct qed_rdma_qp *qp)
{
	int rc = 0;

	if (qp->iwarp_state != QED_IWARP_QP_STATE_ERROR) {
		rc = qed_iwarp_modify_qp(p_hwfn, qp,
					 QED_IWARP_QP_STATE_ERROR, false);
		if (rc)
			return rc;
	}

	rc = qed_iwarp_fw_destroy(p_hwfn, qp);

	if (qp->shared_queue)
		dma_free_coherent(&p_hwfn->cdev->pdev->dev,
				  IWARP_SHARED_QUEUE_PAGE_SIZE,
				  qp->shared_queue, qp->shared_queue_phys_addr);

	return rc;
}

#define QED_IWARP_MAX_CID_CLEAN_TIME  100
#define QED_IWARP_MAX_NO_PROGRESS_CNT 5

/* This function waits for all the bits of a bmap to be cleared, as long as
 * there is progress ( i.e. the number of bits left to be cleared decreases )
 * the function continues.
 */
static int
qed_iwarp_wait_cid_map_cleared(struct qed_hwfn *p_hwfn, struct qed_bmap *bmap)
{
	int prev_weight = 0;
	int wait_count = 0;
	int weight = 0;

	weight = bitmap_weight(bmap->bitmap, bmap->max_count);
	prev_weight = weight;

	while (weight) {
		msleep(QED_IWARP_MAX_CID_CLEAN_TIME);

		weight = bitmap_weight(bmap->bitmap, bmap->max_count);

		if (prev_weight == weight) {
			wait_count++;
		} else {
			prev_weight = weight;
			wait_count = 0;
		}

		if (wait_count > QED_IWARP_MAX_NO_PROGRESS_CNT) {
			DP_NOTICE(p_hwfn,
				  "%s bitmap wait timed out (%d cids pending)\n",
				  bmap->name, weight);
			return -EBUSY;
		}
	}
	return 0;
}

static int qed_iwarp_wait_for_all_cids(struct qed_hwfn *p_hwfn)
{
	/* Now wait for all cids to be completed */
	return qed_iwarp_wait_cid_map_cleared(p_hwfn,
					      &p_hwfn->p_rdma_info->cid_map);
}

int qed_iwarp_alloc(struct qed_hwfn *p_hwfn)
{
	spin_lock_init(&p_hwfn->p_rdma_info->iwarp.iw_lock);

	return 0;
}

void qed_iwarp_resc_free(struct qed_hwfn *p_hwfn)
{
}

static int
qed_iwarp_ll2_post_rx(struct qed_hwfn *p_hwfn,
		      struct qed_iwarp_ll2_buff *buf, u8 handle)
{
	int rc;

	rc = qed_ll2_post_rx_buffer(p_hwfn, handle, buf->data_phys_addr,
				    (u16)buf->buff_size, buf, 1);
	if (rc) {
		DP_NOTICE(p_hwfn,
			  "Failed to repost rx buffer to ll2 rc = %d, handle=%d\n",
			  rc, handle);
		dma_free_coherent(&p_hwfn->cdev->pdev->dev, buf->buff_size,
				  buf->data, buf->data_phys_addr);
		kfree(buf);
	}

	return rc;
}

static void
qed_iwarp_ll2_comp_syn_pkt(void *cxt, struct qed_ll2_comp_rx_data *data)
{
	struct qed_iwarp_ll2_buff *buf = data->cookie;
	struct qed_hwfn *p_hwfn = cxt;

	if (GET_FIELD(data->parse_flags,
		      PARSING_AND_ERR_FLAGS_L4CHKSMWASCALCULATED) &&
	    GET_FIELD(data->parse_flags, PARSING_AND_ERR_FLAGS_L4CHKSMERROR)) {
		DP_NOTICE(p_hwfn, "Syn packet received with checksum error\n");
		goto err;
	}

	/* Process SYN packet - added later on in series */

err:
	qed_iwarp_ll2_post_rx(p_hwfn, buf,
			      p_hwfn->p_rdma_info->iwarp.ll2_syn_handle);
}

static void qed_iwarp_ll2_rel_rx_pkt(void *cxt, u8 connection_handle,
				     void *cookie, dma_addr_t rx_buf_addr,
				     bool b_last_packet)
{
	struct qed_iwarp_ll2_buff *buffer = cookie;
	struct qed_hwfn *p_hwfn = cxt;

	dma_free_coherent(&p_hwfn->cdev->pdev->dev, buffer->buff_size,
			  buffer->data, buffer->data_phys_addr);
	kfree(buffer);
}

static void qed_iwarp_ll2_comp_tx_pkt(void *cxt, u8 connection_handle,
				      void *cookie, dma_addr_t first_frag_addr,
				      bool b_last_fragment, bool b_last_packet)
{
	struct qed_iwarp_ll2_buff *buffer = cookie;
	struct qed_hwfn *p_hwfn = cxt;

	/* this was originally an rx packet, post it back */
	qed_iwarp_ll2_post_rx(p_hwfn, buffer, connection_handle);
}

static void qed_iwarp_ll2_rel_tx_pkt(void *cxt, u8 connection_handle,
				     void *cookie, dma_addr_t first_frag_addr,
				     bool b_last_fragment, bool b_last_packet)
{
	struct qed_iwarp_ll2_buff *buffer = cookie;
	struct qed_hwfn *p_hwfn = cxt;

	if (!buffer)
		return;

	dma_free_coherent(&p_hwfn->cdev->pdev->dev, buffer->buff_size,
			  buffer->data, buffer->data_phys_addr);

	kfree(buffer);
}

static int qed_iwarp_ll2_stop(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	struct qed_iwarp_info *iwarp_info = &p_hwfn->p_rdma_info->iwarp;
	int rc = 0;

	if (iwarp_info->ll2_syn_handle != QED_IWARP_HANDLE_INVAL) {
		rc = qed_ll2_terminate_connection(p_hwfn,
						  iwarp_info->ll2_syn_handle);
		if (rc)
			DP_INFO(p_hwfn, "Failed to terminate syn connection\n");

		qed_ll2_release_connection(p_hwfn, iwarp_info->ll2_syn_handle);
		iwarp_info->ll2_syn_handle = QED_IWARP_HANDLE_INVAL;
	}

	qed_llh_remove_mac_filter(p_hwfn,
				  p_ptt, p_hwfn->p_rdma_info->iwarp.mac_addr);
	return rc;
}

static int
qed_iwarp_ll2_alloc_buffers(struct qed_hwfn *p_hwfn,
			    int num_rx_bufs, int buff_size, u8 ll2_handle)
{
	struct qed_iwarp_ll2_buff *buffer;
	int rc = 0;
	int i;

	for (i = 0; i < num_rx_bufs; i++) {
		buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
		if (!buffer) {
			rc = -ENOMEM;
			break;
		}

		buffer->data = dma_alloc_coherent(&p_hwfn->cdev->pdev->dev,
						  buff_size,
						  &buffer->data_phys_addr,
						  GFP_KERNEL);
		if (!buffer->data) {
			kfree(buffer);
			rc = -ENOMEM;
			break;
		}

		buffer->buff_size = buff_size;
		rc = qed_iwarp_ll2_post_rx(p_hwfn, buffer, ll2_handle);
		if (rc)
			/* buffers will be deallocated by qed_ll2 */
			break;
	}
	return rc;
}

#define QED_IWARP_MAX_BUF_SIZE(mtu)				     \
	ALIGN((mtu) + ETH_HLEN + 2 * VLAN_HLEN + 2 + ETH_CACHE_LINE_SIZE, \
		ETH_CACHE_LINE_SIZE)

static int
qed_iwarp_ll2_start(struct qed_hwfn *p_hwfn,
		    struct qed_rdma_start_in_params *params,
		    struct qed_ptt *p_ptt)
{
	struct qed_iwarp_info *iwarp_info;
	struct qed_ll2_acquire_data data;
	struct qed_ll2_cbs cbs;
	int rc = 0;

	iwarp_info = &p_hwfn->p_rdma_info->iwarp;
	iwarp_info->ll2_syn_handle = QED_IWARP_HANDLE_INVAL;

	iwarp_info->max_mtu = params->max_mtu;

	ether_addr_copy(p_hwfn->p_rdma_info->iwarp.mac_addr, params->mac_addr);

	rc = qed_llh_add_mac_filter(p_hwfn, p_ptt, params->mac_addr);
	if (rc)
		return rc;

	/* Start SYN connection */
	cbs.rx_comp_cb = qed_iwarp_ll2_comp_syn_pkt;
	cbs.rx_release_cb = qed_iwarp_ll2_rel_rx_pkt;
	cbs.tx_comp_cb = qed_iwarp_ll2_comp_tx_pkt;
	cbs.tx_release_cb = qed_iwarp_ll2_rel_tx_pkt;
	cbs.cookie = p_hwfn;

	memset(&data, 0, sizeof(data));
	data.input.conn_type = QED_LL2_TYPE_IWARP;
	data.input.mtu = QED_IWARP_MAX_SYN_PKT_SIZE;
	data.input.rx_num_desc = QED_IWARP_LL2_SYN_RX_SIZE;
	data.input.tx_num_desc = QED_IWARP_LL2_SYN_TX_SIZE;
	data.input.tx_max_bds_per_packet = 1;	/* will never be fragmented */
	data.input.tx_tc = PKT_LB_TC;
	data.input.tx_dest = QED_LL2_TX_DEST_LB;
	data.p_connection_handle = &iwarp_info->ll2_syn_handle;
	data.cbs = &cbs;

	rc = qed_ll2_acquire_connection(p_hwfn, &data);
	if (rc) {
		DP_NOTICE(p_hwfn, "Failed to acquire LL2 connection\n");
		qed_llh_remove_mac_filter(p_hwfn, p_ptt, params->mac_addr);
		return rc;
	}

	rc = qed_ll2_establish_connection(p_hwfn, iwarp_info->ll2_syn_handle);
	if (rc) {
		DP_NOTICE(p_hwfn, "Failed to establish LL2 connection\n");
		goto err;
	}

	rc = qed_iwarp_ll2_alloc_buffers(p_hwfn,
					 QED_IWARP_LL2_SYN_RX_SIZE,
					 QED_IWARP_MAX_SYN_PKT_SIZE,
					 iwarp_info->ll2_syn_handle);
	if (rc)
		goto err;

	return rc;
err:
	qed_iwarp_ll2_stop(p_hwfn, p_ptt);

	return rc;
}

int qed_iwarp_setup(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt,
		    struct qed_rdma_start_in_params *params)
{
	struct qed_iwarp_info *iwarp_info;
	u32 rcv_wnd_size;

	iwarp_info = &p_hwfn->p_rdma_info->iwarp;

	iwarp_info->tcp_flags = QED_IWARP_TS_EN;
	rcv_wnd_size = QED_IWARP_RCV_WND_SIZE_DEF;

	/* value 0 is used for ilog2(QED_IWARP_RCV_WND_SIZE_MIN) */
	iwarp_info->rcv_wnd_scale = ilog2(rcv_wnd_size) -
	    ilog2(QED_IWARP_RCV_WND_SIZE_MIN);
	iwarp_info->crc_needed = QED_IWARP_PARAM_CRC_NEEDED;
	iwarp_info->mpa_rev = MPA_NEGOTIATION_TYPE_ENHANCED;

	iwarp_info->peer2peer = QED_IWARP_PARAM_P2P;

	spin_lock_init(&p_hwfn->p_rdma_info->iwarp.qp_lock);

	qed_spq_register_async_cb(p_hwfn, PROTOCOLID_IWARP,
				  qed_iwarp_async_event);

	return qed_iwarp_ll2_start(p_hwfn, params, p_ptt);
}

int qed_iwarp_stop(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	int rc;

	rc = qed_iwarp_wait_for_all_cids(p_hwfn);
	if (rc)
		return rc;

	qed_spq_unregister_async_cb(p_hwfn, PROTOCOLID_IWARP);

	return qed_iwarp_ll2_stop(p_hwfn, p_ptt);
}

static int qed_iwarp_async_event(struct qed_hwfn *p_hwfn,
				 u8 fw_event_code, u16 echo,
				 union event_ring_data *data,
				 u8 fw_return_code)
{
	return 0;
}

void
qed_iwarp_query_qp(struct qed_rdma_qp *qp,
		   struct qed_rdma_query_qp_out_params *out_params)
{
	out_params->state = qed_iwarp2roce_state(qp->iwarp_state);
}
