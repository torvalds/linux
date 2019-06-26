/*******************************************************************************
*
* Copyright (c) 2015-2016 Intel Corporation.  All rights reserved.
*
* This software is available to you under a choice of one of two
* licenses.  You may choose to be licensed under the terms of the GNU
* General Public License (GPL) Version 2, available from the file
* COPYING in the main directory of this source tree, or the
* OpenFabrics.org BSD license below:
*
*   Redistribution and use in source and binary forms, with or
*   without modification, are permitted provided that the following
*   conditions are met:
*
*    - Redistributions of source code must retain the above
*	copyright notice, this list of conditions and the following
*	disclaimer.
*
*    - Redistributions in binary form must reproduce the above
*	copyright notice, this list of conditions and the following
*	disclaimer in the documentation and/or other materials
*	provided with the distribution.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
* BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
* ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
* CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*
*******************************************************************************/

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/if_vlan.h>

#include "i40iw.h"

/**
 * i40iw_initialize_hw_resources - initialize hw resource during open
 * @iwdev: iwarp device
 */
u32 i40iw_initialize_hw_resources(struct i40iw_device *iwdev)
{
	unsigned long num_pds;
	u32 resources_size;
	u32 max_mr;
	u32 max_qp;
	u32 max_cq;
	u32 arp_table_size;
	u32 mrdrvbits;
	void *resource_ptr;

	max_qp = iwdev->sc_dev.hmc_info->hmc_obj[I40IW_HMC_IW_QP].cnt;
	max_cq = iwdev->sc_dev.hmc_info->hmc_obj[I40IW_HMC_IW_CQ].cnt;
	max_mr = iwdev->sc_dev.hmc_info->hmc_obj[I40IW_HMC_IW_MR].cnt;
	arp_table_size = iwdev->sc_dev.hmc_info->hmc_obj[I40IW_HMC_IW_ARP].cnt;
	iwdev->max_cqe = 0xFFFFF;
	num_pds = I40IW_MAX_PDS;
	resources_size = sizeof(struct i40iw_arp_entry) * arp_table_size;
	resources_size += sizeof(unsigned long) * BITS_TO_LONGS(max_qp);
	resources_size += sizeof(unsigned long) * BITS_TO_LONGS(max_mr);
	resources_size += sizeof(unsigned long) * BITS_TO_LONGS(max_cq);
	resources_size += sizeof(unsigned long) * BITS_TO_LONGS(num_pds);
	resources_size += sizeof(unsigned long) * BITS_TO_LONGS(arp_table_size);
	resources_size += sizeof(struct i40iw_qp **) * max_qp;
	iwdev->mem_resources = kzalloc(resources_size, GFP_KERNEL);

	if (!iwdev->mem_resources)
		return -ENOMEM;

	iwdev->max_qp = max_qp;
	iwdev->max_mr = max_mr;
	iwdev->max_cq = max_cq;
	iwdev->max_pd = num_pds;
	iwdev->arp_table_size = arp_table_size;
	iwdev->arp_table = (struct i40iw_arp_entry *)iwdev->mem_resources;
	resource_ptr = iwdev->mem_resources + (sizeof(struct i40iw_arp_entry) * arp_table_size);

	iwdev->device_cap_flags = IB_DEVICE_LOCAL_DMA_LKEY |
	    IB_DEVICE_MEM_WINDOW | IB_DEVICE_MEM_MGT_EXTENSIONS;

	iwdev->allocated_qps = resource_ptr;
	iwdev->allocated_cqs = &iwdev->allocated_qps[BITS_TO_LONGS(max_qp)];
	iwdev->allocated_mrs = &iwdev->allocated_cqs[BITS_TO_LONGS(max_cq)];
	iwdev->allocated_pds = &iwdev->allocated_mrs[BITS_TO_LONGS(max_mr)];
	iwdev->allocated_arps = &iwdev->allocated_pds[BITS_TO_LONGS(num_pds)];
	iwdev->qp_table = (struct i40iw_qp **)(&iwdev->allocated_arps[BITS_TO_LONGS(arp_table_size)]);
	set_bit(0, iwdev->allocated_mrs);
	set_bit(0, iwdev->allocated_qps);
	set_bit(0, iwdev->allocated_cqs);
	set_bit(0, iwdev->allocated_pds);
	set_bit(0, iwdev->allocated_arps);

	/* Following for ILQ/IEQ */
	set_bit(1, iwdev->allocated_qps);
	set_bit(1, iwdev->allocated_cqs);
	set_bit(1, iwdev->allocated_pds);
	set_bit(2, iwdev->allocated_cqs);
	set_bit(2, iwdev->allocated_pds);

	spin_lock_init(&iwdev->resource_lock);
	spin_lock_init(&iwdev->qptable_lock);
	/* stag index mask has a minimum of 14 bits */
	mrdrvbits = 24 - max(get_count_order(iwdev->max_mr), 14);
	iwdev->mr_stagmask = ~(((1 << mrdrvbits) - 1) << (32 - mrdrvbits));
	return 0;
}

/**
 * i40iw_cqp_ce_handler - handle cqp completions
 * @iwdev: iwarp device
 * @arm: flag to arm after completions
 * @cq: cq for cqp completions
 */
static void i40iw_cqp_ce_handler(struct i40iw_device *iwdev, struct i40iw_sc_cq *cq, bool arm)
{
	struct i40iw_cqp_request *cqp_request;
	struct i40iw_sc_dev *dev = &iwdev->sc_dev;
	u32 cqe_count = 0;
	struct i40iw_ccq_cqe_info info;
	int ret;

	do {
		memset(&info, 0, sizeof(info));
		ret = dev->ccq_ops->ccq_get_cqe_info(cq, &info);
		if (ret)
			break;
		cqp_request = (struct i40iw_cqp_request *)(unsigned long)info.scratch;
		if (info.error)
			i40iw_pr_err("opcode = 0x%x maj_err_code = 0x%x min_err_code = 0x%x\n",
				     info.op_code, info.maj_err_code, info.min_err_code);
		if (cqp_request) {
			cqp_request->compl_info.maj_err_code = info.maj_err_code;
			cqp_request->compl_info.min_err_code = info.min_err_code;
			cqp_request->compl_info.op_ret_val = info.op_ret_val;
			cqp_request->compl_info.error = info.error;

			if (cqp_request->waiting) {
				cqp_request->request_done = true;
				wake_up(&cqp_request->waitq);
				i40iw_put_cqp_request(&iwdev->cqp, cqp_request);
			} else {
				if (cqp_request->callback_fcn)
					cqp_request->callback_fcn(cqp_request, 1);
				i40iw_put_cqp_request(&iwdev->cqp, cqp_request);
			}
		}

		cqe_count++;
	} while (1);

	if (arm && cqe_count) {
		i40iw_process_bh(dev);
		dev->ccq_ops->ccq_arm(cq);
	}
}

/**
 * i40iw_iwarp_ce_handler - handle iwarp completions
 * @iwdev: iwarp device
 * @iwcp: iwarp cq receiving event
 */
static void i40iw_iwarp_ce_handler(struct i40iw_device *iwdev,
				   struct i40iw_sc_cq *iwcq)
{
	struct i40iw_cq *i40iwcq = iwcq->back_cq;

	if (i40iwcq->ibcq.comp_handler)
		i40iwcq->ibcq.comp_handler(&i40iwcq->ibcq,
					   i40iwcq->ibcq.cq_context);
}

/**
 * i40iw_puda_ce_handler - handle puda completion events
 * @iwdev: iwarp device
 * @cq: puda completion q for event
 */
static void i40iw_puda_ce_handler(struct i40iw_device *iwdev,
				  struct i40iw_sc_cq *cq)
{
	struct i40iw_sc_dev *dev = (struct i40iw_sc_dev *)&iwdev->sc_dev;
	enum i40iw_status_code status;
	u32 compl_error;

	do {
		status = i40iw_puda_poll_completion(dev, cq, &compl_error);
		if (status == I40IW_ERR_QUEUE_EMPTY)
			break;
		if (status) {
			i40iw_pr_err("puda  status = %d\n", status);
			break;
		}
		if (compl_error) {
			i40iw_pr_err("puda compl_err  =0x%x\n", compl_error);
			break;
		}
	} while (1);

	dev->ccq_ops->ccq_arm(cq);
}

/**
 * i40iw_process_ceq - handle ceq for completions
 * @iwdev: iwarp device
 * @ceq: ceq having cq for completion
 */
void i40iw_process_ceq(struct i40iw_device *iwdev, struct i40iw_ceq *ceq)
{
	struct i40iw_sc_dev *dev = &iwdev->sc_dev;
	struct i40iw_sc_ceq *sc_ceq;
	struct i40iw_sc_cq *cq;
	bool arm = true;

	sc_ceq = &ceq->sc_ceq;
	do {
		cq = dev->ceq_ops->process_ceq(dev, sc_ceq);
		if (!cq)
			break;

		if (cq->cq_type == I40IW_CQ_TYPE_CQP)
			i40iw_cqp_ce_handler(iwdev, cq, arm);
		else if (cq->cq_type == I40IW_CQ_TYPE_IWARP)
			i40iw_iwarp_ce_handler(iwdev, cq);
		else if ((cq->cq_type == I40IW_CQ_TYPE_ILQ) ||
			 (cq->cq_type == I40IW_CQ_TYPE_IEQ))
			i40iw_puda_ce_handler(iwdev, cq);
	} while (1);
}

/**
 * i40iw_next_iw_state - modify qp state
 * @iwqp: iwarp qp to modify
 * @state: next state for qp
 * @del_hash: del hash
 * @term: term message
 * @termlen: length of term message
 */
void i40iw_next_iw_state(struct i40iw_qp *iwqp,
			 u8 state,
			 u8 del_hash,
			 u8 term,
			 u8 termlen)
{
	struct i40iw_modify_qp_info info;

	memset(&info, 0, sizeof(info));
	info.next_iwarp_state = state;
	info.remove_hash_idx = del_hash;
	info.cq_num_valid = true;
	info.arp_cache_idx_valid = true;
	info.dont_send_term = true;
	info.dont_send_fin = true;
	info.termlen = termlen;

	if (term & I40IWQP_TERM_SEND_TERM_ONLY)
		info.dont_send_term = false;
	if (term & I40IWQP_TERM_SEND_FIN_ONLY)
		info.dont_send_fin = false;
	if (iwqp->sc_qp.term_flags && (state == I40IW_QP_STATE_ERROR))
		info.reset_tcp_conn = true;
	iwqp->hw_iwarp_state = state;
	i40iw_hw_modify_qp(iwqp->iwdev, iwqp, &info, 0);
}

/**
 * i40iw_process_aeq - handle aeq events
 * @iwdev: iwarp device
 */
void i40iw_process_aeq(struct i40iw_device *iwdev)
{
	struct i40iw_sc_dev *dev = &iwdev->sc_dev;
	struct i40iw_aeq *aeq = &iwdev->aeq;
	struct i40iw_sc_aeq *sc_aeq = &aeq->sc_aeq;
	struct i40iw_aeqe_info aeinfo;
	struct i40iw_aeqe_info *info = &aeinfo;
	int ret;
	struct i40iw_qp *iwqp = NULL;
	struct i40iw_sc_cq *cq = NULL;
	struct i40iw_cq *iwcq = NULL;
	struct i40iw_sc_qp *qp = NULL;
	struct i40iw_qp_host_ctx_info *ctx_info = NULL;
	unsigned long flags;

	u32 aeqcnt = 0;

	if (!sc_aeq->size)
		return;

	do {
		memset(info, 0, sizeof(*info));
		ret = dev->aeq_ops->get_next_aeqe(sc_aeq, info);
		if (ret)
			break;

		aeqcnt++;
		i40iw_debug(dev, I40IW_DEBUG_AEQ,
			    "%s ae_id = 0x%x bool qp=%d qp_id = %d\n",
			    __func__, info->ae_id, info->qp, info->qp_cq_id);
		if (info->qp) {
			spin_lock_irqsave(&iwdev->qptable_lock, flags);
			iwqp = iwdev->qp_table[info->qp_cq_id];
			if (!iwqp) {
				spin_unlock_irqrestore(&iwdev->qptable_lock, flags);
				i40iw_debug(dev, I40IW_DEBUG_AEQ,
					    "%s qp_id %d is already freed\n",
					    __func__, info->qp_cq_id);
				continue;
			}
			i40iw_add_ref(&iwqp->ibqp);
			spin_unlock_irqrestore(&iwdev->qptable_lock, flags);
			qp = &iwqp->sc_qp;
			spin_lock_irqsave(&iwqp->lock, flags);
			iwqp->hw_tcp_state = info->tcp_state;
			iwqp->hw_iwarp_state = info->iwarp_state;
			iwqp->last_aeq = info->ae_id;
			spin_unlock_irqrestore(&iwqp->lock, flags);
			ctx_info = &iwqp->ctx_info;
			ctx_info->err_rq_idx_valid = true;
		} else {
			if (info->ae_id != I40IW_AE_CQ_OPERATION_ERROR)
				continue;
		}

		switch (info->ae_id) {
		case I40IW_AE_LLP_FIN_RECEIVED:
			if (qp->term_flags)
				break;
			if (atomic_inc_return(&iwqp->close_timer_started) == 1) {
				iwqp->hw_tcp_state = I40IW_TCP_STATE_CLOSE_WAIT;
				if ((iwqp->hw_tcp_state == I40IW_TCP_STATE_CLOSE_WAIT) &&
				    (iwqp->ibqp_state == IB_QPS_RTS)) {
					i40iw_next_iw_state(iwqp,
							    I40IW_QP_STATE_CLOSING, 0, 0, 0);
					i40iw_cm_disconn(iwqp);
				}
				iwqp->cm_id->add_ref(iwqp->cm_id);
				i40iw_schedule_cm_timer(iwqp->cm_node,
							(struct i40iw_puda_buf *)iwqp,
							I40IW_TIMER_TYPE_CLOSE, 1, 0);
			}
			break;
		case I40IW_AE_LLP_CLOSE_COMPLETE:
			if (qp->term_flags)
				i40iw_terminate_done(qp, 0);
			else
				i40iw_cm_disconn(iwqp);
			break;
		case I40IW_AE_BAD_CLOSE:
			/* fall through */
		case I40IW_AE_RESET_SENT:
			i40iw_next_iw_state(iwqp, I40IW_QP_STATE_ERROR, 1, 0, 0);
			i40iw_cm_disconn(iwqp);
			break;
		case I40IW_AE_LLP_CONNECTION_RESET:
			if (atomic_read(&iwqp->close_timer_started))
				break;
			i40iw_cm_disconn(iwqp);
			break;
		case I40IW_AE_QP_SUSPEND_COMPLETE:
			i40iw_qp_suspend_resume(dev, &iwqp->sc_qp, false);
			break;
		case I40IW_AE_TERMINATE_SENT:
			i40iw_terminate_send_fin(qp);
			break;
		case I40IW_AE_LLP_TERMINATE_RECEIVED:
			i40iw_terminate_received(qp, info);
			break;
		case I40IW_AE_CQ_OPERATION_ERROR:
			i40iw_pr_err("Processing an iWARP related AE for CQ misc = 0x%04X\n",
				     info->ae_id);
			cq = (struct i40iw_sc_cq *)(unsigned long)info->compl_ctx;
			iwcq = (struct i40iw_cq *)cq->back_cq;

			if (iwcq->ibcq.event_handler) {
				struct ib_event ibevent;

				ibevent.device = iwcq->ibcq.device;
				ibevent.event = IB_EVENT_CQ_ERR;
				ibevent.element.cq = &iwcq->ibcq;
				iwcq->ibcq.event_handler(&ibevent, iwcq->ibcq.cq_context);
			}
			break;
		case I40IW_AE_LLP_DOUBT_REACHABILITY:
			break;
		case I40IW_AE_PRIV_OPERATION_DENIED:
		case I40IW_AE_STAG_ZERO_INVALID:
		case I40IW_AE_IB_RREQ_AND_Q1_FULL:
		case I40IW_AE_DDP_UBE_INVALID_DDP_VERSION:
		case I40IW_AE_DDP_UBE_INVALID_MO:
		case I40IW_AE_DDP_UBE_INVALID_QN:
		case I40IW_AE_DDP_NO_L_BIT:
		case I40IW_AE_RDMAP_ROE_INVALID_RDMAP_VERSION:
		case I40IW_AE_RDMAP_ROE_UNEXPECTED_OPCODE:
		case I40IW_AE_ROE_INVALID_RDMA_READ_REQUEST:
		case I40IW_AE_ROE_INVALID_RDMA_WRITE_OR_READ_RESP:
		case I40IW_AE_INVALID_ARP_ENTRY:
		case I40IW_AE_INVALID_TCP_OPTION_RCVD:
		case I40IW_AE_STALE_ARP_ENTRY:
		case I40IW_AE_LLP_RECEIVED_MPA_CRC_ERROR:
		case I40IW_AE_LLP_SEGMENT_TOO_SMALL:
		case I40IW_AE_LLP_SYN_RECEIVED:
		case I40IW_AE_LLP_TOO_MANY_RETRIES:
		case I40IW_AE_LCE_QP_CATASTROPHIC:
		case I40IW_AE_LCE_FUNCTION_CATASTROPHIC:
		case I40IW_AE_LCE_CQ_CATASTROPHIC:
		case I40IW_AE_UDA_XMIT_DGRAM_TOO_LONG:
		case I40IW_AE_UDA_XMIT_DGRAM_TOO_SHORT:
			ctx_info->err_rq_idx_valid = false;
			/* fall through */
		default:
			if (!info->sq && ctx_info->err_rq_idx_valid) {
				ctx_info->err_rq_idx = info->wqe_idx;
				ctx_info->tcp_info_valid = false;
				ctx_info->iwarp_info_valid = false;
				ret = dev->iw_priv_qp_ops->qp_setctx(&iwqp->sc_qp,
								     iwqp->host_ctx.va,
								     ctx_info);
			}
			i40iw_terminate_connection(qp, info);
			break;
		}
		if (info->qp)
			i40iw_rem_ref(&iwqp->ibqp);
	} while (1);

	if (aeqcnt)
		dev->aeq_ops->repost_aeq_entries(dev, aeqcnt);
}

/**
 * i40iw_cqp_manage_abvpt_cmd - send cqp command manage abpvt
 * @iwdev: iwarp device
 * @accel_local_port: port for apbvt
 * @add_port: add or delete port
 */
static enum i40iw_status_code
i40iw_cqp_manage_abvpt_cmd(struct i40iw_device *iwdev,
			   u16 accel_local_port,
			   bool add_port)
{
	struct i40iw_apbvt_info *info;
	struct i40iw_cqp_request *cqp_request;
	struct cqp_commands_info *cqp_info;
	enum i40iw_status_code status;

	cqp_request = i40iw_get_cqp_request(&iwdev->cqp, add_port);
	if (!cqp_request)
		return I40IW_ERR_NO_MEMORY;

	cqp_info = &cqp_request->info;
	info = &cqp_info->in.u.manage_apbvt_entry.info;

	memset(info, 0, sizeof(*info));
	info->add = add_port;
	info->port = cpu_to_le16(accel_local_port);

	cqp_info->cqp_cmd = OP_MANAGE_APBVT_ENTRY;
	cqp_info->post_sq = 1;
	cqp_info->in.u.manage_apbvt_entry.cqp = &iwdev->cqp.sc_cqp;
	cqp_info->in.u.manage_apbvt_entry.scratch = (uintptr_t)cqp_request;
	status = i40iw_handle_cqp_op(iwdev, cqp_request);
	if (status)
		i40iw_pr_err("CQP-OP Manage APBVT entry fail");

	return status;
}

/**
 * i40iw_manage_apbvt - add or delete tcp port
 * @iwdev: iwarp device
 * @accel_local_port: port for apbvt
 * @add_port: add or delete port
 */
enum i40iw_status_code i40iw_manage_apbvt(struct i40iw_device *iwdev,
					  u16 accel_local_port,
					  bool add_port)
{
	struct i40iw_cm_core *cm_core = &iwdev->cm_core;
	enum i40iw_status_code status;
	unsigned long flags;
	bool in_use;

	/* apbvt_lock is held across CQP delete APBVT OP (non-waiting) to
	 * protect against race where add APBVT CQP can race ahead of the delete
	 * APBVT for same port.
	 */
	if (add_port) {
		spin_lock_irqsave(&cm_core->apbvt_lock, flags);
		in_use = __test_and_set_bit(accel_local_port,
					    cm_core->ports_in_use);
		spin_unlock_irqrestore(&cm_core->apbvt_lock, flags);
		if (in_use)
			return 0;
		return i40iw_cqp_manage_abvpt_cmd(iwdev, accel_local_port,
						  true);
	} else {
		spin_lock_irqsave(&cm_core->apbvt_lock, flags);
		in_use = i40iw_port_in_use(cm_core, accel_local_port);
		if (in_use) {
			spin_unlock_irqrestore(&cm_core->apbvt_lock, flags);
			return 0;
		}
		__clear_bit(accel_local_port, cm_core->ports_in_use);
		status = i40iw_cqp_manage_abvpt_cmd(iwdev, accel_local_port,
						    false);
		spin_unlock_irqrestore(&cm_core->apbvt_lock, flags);
		return status;
	}
}

/**
 * i40iw_manage_arp_cache - manage hw arp cache
 * @iwdev: iwarp device
 * @mac_addr: mac address ptr
 * @ip_addr: ip addr for arp cache
 * @action: add, delete or modify
 */
void i40iw_manage_arp_cache(struct i40iw_device *iwdev,
			    unsigned char *mac_addr,
			    u32 *ip_addr,
			    bool ipv4,
			    u32 action)
{
	struct i40iw_add_arp_cache_entry_info *info;
	struct i40iw_cqp_request *cqp_request;
	struct cqp_commands_info *cqp_info;
	int arp_index;

	arp_index = i40iw_arp_table(iwdev, ip_addr, ipv4, mac_addr, action);
	if (arp_index == -1)
		return;
	cqp_request = i40iw_get_cqp_request(&iwdev->cqp, false);
	if (!cqp_request)
		return;

	cqp_info = &cqp_request->info;
	if (action == I40IW_ARP_ADD) {
		cqp_info->cqp_cmd = OP_ADD_ARP_CACHE_ENTRY;
		info = &cqp_info->in.u.add_arp_cache_entry.info;
		memset(info, 0, sizeof(*info));
		info->arp_index = cpu_to_le16((u16)arp_index);
		info->permanent = true;
		ether_addr_copy(info->mac_addr, mac_addr);
		cqp_info->in.u.add_arp_cache_entry.scratch = (uintptr_t)cqp_request;
		cqp_info->in.u.add_arp_cache_entry.cqp = &iwdev->cqp.sc_cqp;
	} else {
		cqp_info->cqp_cmd = OP_DELETE_ARP_CACHE_ENTRY;
		cqp_info->in.u.del_arp_cache_entry.scratch = (uintptr_t)cqp_request;
		cqp_info->in.u.del_arp_cache_entry.cqp = &iwdev->cqp.sc_cqp;
		cqp_info->in.u.del_arp_cache_entry.arp_index = arp_index;
	}

	cqp_info->in.u.add_arp_cache_entry.cqp = &iwdev->cqp.sc_cqp;
	cqp_info->in.u.add_arp_cache_entry.scratch = (uintptr_t)cqp_request;
	cqp_info->post_sq = 1;
	if (i40iw_handle_cqp_op(iwdev, cqp_request))
		i40iw_pr_err("CQP-OP Add/Del Arp Cache entry fail");
}

/**
 * i40iw_send_syn_cqp_callback - do syn/ack after qhash
 * @cqp_request: qhash cqp completion
 * @send_ack: flag send ack
 */
static void i40iw_send_syn_cqp_callback(struct i40iw_cqp_request *cqp_request, u32 send_ack)
{
	i40iw_send_syn(cqp_request->param, send_ack);
}

/**
 * i40iw_manage_qhash - add or modify qhash
 * @iwdev: iwarp device
 * @cminfo: cm info for qhash
 * @etype: type (syn or quad)
 * @mtype: type of qhash
 * @cmnode: cmnode associated with connection
 * @wait: wait for completion
 * @user_pri:user pri of the connection
 */
enum i40iw_status_code i40iw_manage_qhash(struct i40iw_device *iwdev,
					  struct i40iw_cm_info *cminfo,
					  enum i40iw_quad_entry_type etype,
					  enum i40iw_quad_hash_manage_type mtype,
					  void *cmnode,
					  bool wait)
{
	struct i40iw_qhash_table_info *info;
	struct i40iw_sc_dev *dev = &iwdev->sc_dev;
	struct i40iw_sc_vsi *vsi = &iwdev->vsi;
	enum i40iw_status_code status;
	struct i40iw_cqp *iwcqp = &iwdev->cqp;
	struct i40iw_cqp_request *cqp_request;
	struct cqp_commands_info *cqp_info;

	cqp_request = i40iw_get_cqp_request(iwcqp, wait);
	if (!cqp_request)
		return I40IW_ERR_NO_MEMORY;
	cqp_info = &cqp_request->info;
	info = &cqp_info->in.u.manage_qhash_table_entry.info;
	memset(info, 0, sizeof(*info));

	info->vsi = &iwdev->vsi;
	info->manage = mtype;
	info->entry_type = etype;
	if (cminfo->vlan_id != 0xFFFF) {
		info->vlan_valid = true;
		info->vlan_id = cpu_to_le16(cminfo->vlan_id);
	} else {
		info->vlan_valid = false;
	}

	info->ipv4_valid = cminfo->ipv4;
	info->user_pri = cminfo->user_pri;
	ether_addr_copy(info->mac_addr, iwdev->netdev->dev_addr);
	info->qp_num = cpu_to_le32(vsi->ilq->qp_id);
	info->dest_port = cpu_to_le16(cminfo->loc_port);
	info->dest_ip[0] = cpu_to_le32(cminfo->loc_addr[0]);
	info->dest_ip[1] = cpu_to_le32(cminfo->loc_addr[1]);
	info->dest_ip[2] = cpu_to_le32(cminfo->loc_addr[2]);
	info->dest_ip[3] = cpu_to_le32(cminfo->loc_addr[3]);
	if (etype == I40IW_QHASH_TYPE_TCP_ESTABLISHED) {
		info->src_port = cpu_to_le16(cminfo->rem_port);
		info->src_ip[0] = cpu_to_le32(cminfo->rem_addr[0]);
		info->src_ip[1] = cpu_to_le32(cminfo->rem_addr[1]);
		info->src_ip[2] = cpu_to_le32(cminfo->rem_addr[2]);
		info->src_ip[3] = cpu_to_le32(cminfo->rem_addr[3]);
	}
	if (cmnode) {
		cqp_request->callback_fcn = i40iw_send_syn_cqp_callback;
		cqp_request->param = (void *)cmnode;
	}

	if (info->ipv4_valid)
		i40iw_debug(dev, I40IW_DEBUG_CM,
			    "%s:%s IP=%pI4, port=%d, mac=%pM, vlan_id=%d\n",
			    __func__, (!mtype) ? "DELETE" : "ADD",
			    info->dest_ip,
			    info->dest_port, info->mac_addr, cminfo->vlan_id);
	else
		i40iw_debug(dev, I40IW_DEBUG_CM,
			    "%s:%s IP=%pI6, port=%d, mac=%pM, vlan_id=%d\n",
			    __func__, (!mtype) ? "DELETE" : "ADD",
			    info->dest_ip,
			    info->dest_port, info->mac_addr, cminfo->vlan_id);
	cqp_info->in.u.manage_qhash_table_entry.cqp = &iwdev->cqp.sc_cqp;
	cqp_info->in.u.manage_qhash_table_entry.scratch = (uintptr_t)cqp_request;
	cqp_info->cqp_cmd = OP_MANAGE_QHASH_TABLE_ENTRY;
	cqp_info->post_sq = 1;
	status = i40iw_handle_cqp_op(iwdev, cqp_request);
	if (status)
		i40iw_pr_err("CQP-OP Manage Qhash Entry fail");
	return status;
}

/**
 * i40iw_hw_flush_wqes - flush qp's wqe
 * @iwdev: iwarp device
 * @qp: hardware control qp
 * @info: info for flush
 * @wait: flag wait for completion
 */
enum i40iw_status_code i40iw_hw_flush_wqes(struct i40iw_device *iwdev,
					   struct i40iw_sc_qp *qp,
					   struct i40iw_qp_flush_info *info,
					   bool wait)
{
	enum i40iw_status_code status;
	struct i40iw_qp_flush_info *hw_info;
	struct i40iw_cqp_request *cqp_request;
	struct cqp_commands_info *cqp_info;
	struct i40iw_qp *iwqp = (struct i40iw_qp *)qp->back_qp;

	cqp_request = i40iw_get_cqp_request(&iwdev->cqp, wait);
	if (!cqp_request)
		return I40IW_ERR_NO_MEMORY;

	cqp_info = &cqp_request->info;
	hw_info = &cqp_request->info.in.u.qp_flush_wqes.info;
	memcpy(hw_info, info, sizeof(*hw_info));

	cqp_info->cqp_cmd = OP_QP_FLUSH_WQES;
	cqp_info->post_sq = 1;
	cqp_info->in.u.qp_flush_wqes.qp = qp;
	cqp_info->in.u.qp_flush_wqes.scratch = (uintptr_t)cqp_request;
	status = i40iw_handle_cqp_op(iwdev, cqp_request);
	if (status) {
		i40iw_pr_err("CQP-OP Flush WQE's fail");
		complete(&iwqp->sq_drained);
		complete(&iwqp->rq_drained);
		return status;
	}
	if (!cqp_request->compl_info.maj_err_code) {
		switch (cqp_request->compl_info.min_err_code) {
		case I40IW_CQP_COMPL_RQ_WQE_FLUSHED:
			complete(&iwqp->sq_drained);
			break;
		case I40IW_CQP_COMPL_SQ_WQE_FLUSHED:
			complete(&iwqp->rq_drained);
			break;
		case I40IW_CQP_COMPL_RQ_SQ_WQE_FLUSHED:
			break;
		default:
			complete(&iwqp->sq_drained);
			complete(&iwqp->rq_drained);
			break;
		}
	}

	return 0;
}

/**
 * i40iw_gen_ae - generate AE
 * @iwdev: iwarp device
 * @qp: qp associated with AE
 * @info: info for ae
 * @wait: wait for completion
 */
void i40iw_gen_ae(struct i40iw_device *iwdev,
		  struct i40iw_sc_qp *qp,
		  struct i40iw_gen_ae_info *info,
		  bool wait)
{
	struct i40iw_gen_ae_info *ae_info;
	struct i40iw_cqp_request *cqp_request;
	struct cqp_commands_info *cqp_info;

	cqp_request = i40iw_get_cqp_request(&iwdev->cqp, wait);
	if (!cqp_request)
		return;

	cqp_info = &cqp_request->info;
	ae_info = &cqp_request->info.in.u.gen_ae.info;
	memcpy(ae_info, info, sizeof(*ae_info));

	cqp_info->cqp_cmd = OP_GEN_AE;
	cqp_info->post_sq = 1;
	cqp_info->in.u.gen_ae.qp = qp;
	cqp_info->in.u.gen_ae.scratch = (uintptr_t)cqp_request;
	if (i40iw_handle_cqp_op(iwdev, cqp_request))
		i40iw_pr_err("CQP OP failed attempting to generate ae_code=0x%x\n",
			     info->ae_code);
}

/**
 * i40iw_hw_manage_vf_pble_bp - manage vf pbles
 * @iwdev: iwarp device
 * @info: info for managing pble
 * @wait: flag wait for completion
 */
enum i40iw_status_code i40iw_hw_manage_vf_pble_bp(struct i40iw_device *iwdev,
						  struct i40iw_manage_vf_pble_info *info,
						  bool wait)
{
	enum i40iw_status_code status;
	struct i40iw_manage_vf_pble_info *hw_info;
	struct i40iw_cqp_request *cqp_request;
	struct cqp_commands_info *cqp_info;

	if ((iwdev->init_state < CCQ_CREATED) && wait)
		wait = false;

	cqp_request = i40iw_get_cqp_request(&iwdev->cqp, wait);
	if (!cqp_request)
		return I40IW_ERR_NO_MEMORY;

	cqp_info = &cqp_request->info;
	hw_info = &cqp_request->info.in.u.manage_vf_pble_bp.info;
	memcpy(hw_info, info, sizeof(*hw_info));

	cqp_info->cqp_cmd = OP_MANAGE_VF_PBLE_BP;
	cqp_info->post_sq = 1;
	cqp_info->in.u.manage_vf_pble_bp.cqp = &iwdev->cqp.sc_cqp;
	cqp_info->in.u.manage_vf_pble_bp.scratch = (uintptr_t)cqp_request;
	status = i40iw_handle_cqp_op(iwdev, cqp_request);
	if (status)
		i40iw_pr_err("CQP-OP Manage VF pble_bp fail");
	return status;
}

/**
 * i40iw_get_ib_wc - return change flush code to IB's
 * @opcode: iwarp flush code
 */
static enum ib_wc_status i40iw_get_ib_wc(enum i40iw_flush_opcode opcode)
{
	switch (opcode) {
	case FLUSH_PROT_ERR:
		return IB_WC_LOC_PROT_ERR;
	case FLUSH_REM_ACCESS_ERR:
		return IB_WC_REM_ACCESS_ERR;
	case FLUSH_LOC_QP_OP_ERR:
		return IB_WC_LOC_QP_OP_ERR;
	case FLUSH_REM_OP_ERR:
		return IB_WC_REM_OP_ERR;
	case FLUSH_LOC_LEN_ERR:
		return IB_WC_LOC_LEN_ERR;
	case FLUSH_GENERAL_ERR:
		return IB_WC_GENERAL_ERR;
	case FLUSH_FATAL_ERR:
	default:
		return IB_WC_FATAL_ERR;
	}
}

/**
 * i40iw_set_flush_info - set flush info
 * @pinfo: set flush info
 * @min: minor err
 * @maj: major err
 * @opcode: flush error code
 */
static void i40iw_set_flush_info(struct i40iw_qp_flush_info *pinfo,
				 u16 *min,
				 u16 *maj,
				 enum i40iw_flush_opcode opcode)
{
	*min = (u16)i40iw_get_ib_wc(opcode);
	*maj = CQE_MAJOR_DRV;
	pinfo->userflushcode = true;
}

/**
 * i40iw_flush_wqes - flush wqe for qp
 * @iwdev: iwarp device
 * @iwqp: qp to flush wqes
 */
void i40iw_flush_wqes(struct i40iw_device *iwdev, struct i40iw_qp *iwqp)
{
	struct i40iw_qp_flush_info info;
	struct i40iw_qp_flush_info *pinfo = &info;

	struct i40iw_sc_qp *qp = &iwqp->sc_qp;

	memset(pinfo, 0, sizeof(*pinfo));
	info.sq = true;
	info.rq = true;
	if (qp->term_flags) {
		i40iw_set_flush_info(pinfo, &pinfo->sq_minor_code,
				     &pinfo->sq_major_code, qp->flush_code);
		i40iw_set_flush_info(pinfo, &pinfo->rq_minor_code,
				     &pinfo->rq_major_code, qp->flush_code);
	}
	(void)i40iw_hw_flush_wqes(iwdev, &iwqp->sc_qp, &info, true);
}
