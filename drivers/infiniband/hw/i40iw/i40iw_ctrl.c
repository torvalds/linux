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

#include "i40iw_osdep.h"
#include "i40iw_register.h"
#include "i40iw_status.h"
#include "i40iw_hmc.h"

#include "i40iw_d.h"
#include "i40iw_type.h"
#include "i40iw_p.h"
#include "i40iw_vf.h"
#include "i40iw_virtchnl.h"

/**
 * i40iw_insert_wqe_hdr - write wqe header
 * @wqe: cqp wqe for header
 * @header: header for the cqp wqe
 */
void i40iw_insert_wqe_hdr(u64 *wqe, u64 header)
{
	wmb();            /* make sure WQE is populated before polarity is set */
	set_64bit_val(wqe, 24, header);
}

void i40iw_check_cqp_progress(struct i40iw_cqp_timeout *cqp_timeout, struct i40iw_sc_dev *dev)
{
	if (cqp_timeout->compl_cqp_cmds != dev->cqp_cmd_stats[OP_COMPLETED_COMMANDS]) {
		cqp_timeout->compl_cqp_cmds = dev->cqp_cmd_stats[OP_COMPLETED_COMMANDS];
		cqp_timeout->count = 0;
	} else {
		if (dev->cqp_cmd_stats[OP_REQUESTED_COMMANDS] != cqp_timeout->compl_cqp_cmds)
			cqp_timeout->count++;
	}
}

/**
 * i40iw_get_cqp_reg_info - get head and tail for cqp using registers
 * @cqp: struct for cqp hw
 * @val: cqp tail register value
 * @tail:wqtail register value
 * @error: cqp processing err
 */
static inline void i40iw_get_cqp_reg_info(struct i40iw_sc_cqp *cqp,
					  u32 *val,
					  u32 *tail,
					  u32 *error)
{
	if (cqp->dev->is_pf) {
		*val = i40iw_rd32(cqp->dev->hw, I40E_PFPE_CQPTAIL);
		*tail = RS_32(*val, I40E_PFPE_CQPTAIL_WQTAIL);
		*error = RS_32(*val, I40E_PFPE_CQPTAIL_CQP_OP_ERR);
	} else {
		*val = i40iw_rd32(cqp->dev->hw, I40E_VFPE_CQPTAIL1);
		*tail = RS_32(*val, I40E_VFPE_CQPTAIL_WQTAIL);
		*error = RS_32(*val, I40E_VFPE_CQPTAIL_CQP_OP_ERR);
	}
}

/**
 * i40iw_cqp_poll_registers - poll cqp registers
 * @cqp: struct for cqp hw
 * @tail:wqtail register value
 * @count: how many times to try for completion
 */
static enum i40iw_status_code i40iw_cqp_poll_registers(
						struct i40iw_sc_cqp *cqp,
						u32 tail,
						u32 count)
{
	u32 i = 0;
	u32 newtail, error, val;

	while (i < count) {
		i++;
		i40iw_get_cqp_reg_info(cqp, &val, &newtail, &error);
		if (error) {
			error = (cqp->dev->is_pf) ?
				 i40iw_rd32(cqp->dev->hw, I40E_PFPE_CQPERRCODES) :
				 i40iw_rd32(cqp->dev->hw, I40E_VFPE_CQPERRCODES1);
			return I40IW_ERR_CQP_COMPL_ERROR;
		}
		if (newtail != tail) {
			/* SUCCESS */
			I40IW_RING_MOVE_TAIL(cqp->sq_ring);
			cqp->dev->cqp_cmd_stats[OP_COMPLETED_COMMANDS]++;
			return 0;
		}
		udelay(I40IW_SLEEP_COUNT);
	}
	return I40IW_ERR_TIMEOUT;
}

/**
 * i40iw_sc_parse_fpm_commit_buf - parse fpm commit buffer
 * @buf: ptr to fpm commit buffer
 * @info: ptr to i40iw_hmc_obj_info struct
 * @sd: number of SDs for HMC objects
 *
 * parses fpm commit info and copy base value
 * of hmc objects in hmc_info
 */
static enum i40iw_status_code i40iw_sc_parse_fpm_commit_buf(
				u64 *buf,
				struct i40iw_hmc_obj_info *info,
				u32 *sd)
{
	u64 temp;
	u64 size;
	u64 base = 0;
	u32 i, j;
	u32 k = 0;

	/* copy base values in obj_info */
	for (i = I40IW_HMC_IW_QP, j = 0; i <= I40IW_HMC_IW_PBLE; i++, j += 8) {
		if ((i == I40IW_HMC_IW_SRQ) ||
			(i == I40IW_HMC_IW_FSIMC) ||
			(i == I40IW_HMC_IW_FSIAV)) {
			info[i].base = 0;
			info[i].cnt = 0;
			continue;
		}
		get_64bit_val(buf, j, &temp);
		info[i].base = RS_64_1(temp, 32) * 512;
		if (info[i].base > base) {
			base = info[i].base;
			k = i;
		}
		if (i == I40IW_HMC_IW_APBVT_ENTRY) {
			info[i].cnt = 1;
			continue;
		}
		if (i == I40IW_HMC_IW_QP)
			info[i].cnt = (u32)RS_64(temp, I40IW_QUERY_FPM_MAX_QPS);
		else if (i == I40IW_HMC_IW_CQ)
			info[i].cnt = (u32)RS_64(temp, I40IW_QUERY_FPM_MAX_CQS);
		else
			info[i].cnt = (u32)(temp);
	}
	size = info[k].cnt * info[k].size + info[k].base;
	if (size & 0x1FFFFF)
		*sd = (u32)((size >> 21) + 1); /* add 1 for remainder */
	else
		*sd = (u32)(size >> 21);

	return 0;
}

/**
 * i40iw_sc_decode_fpm_query() - Decode a 64 bit value into max count and size
 * @buf: ptr to fpm query buffer
 * @buf_idx: index into buf
 * @info: ptr to i40iw_hmc_obj_info struct
 * @rsrc_idx: resource index into info
 *
 * Decode a 64 bit value from fpm query buffer into max count and size
 */
static u64 i40iw_sc_decode_fpm_query(u64 *buf,
					    u32 buf_idx,
					    struct i40iw_hmc_obj_info *obj_info,
					    u32 rsrc_idx)
{
	u64 temp;
	u32 size;

	get_64bit_val(buf, buf_idx, &temp);
	obj_info[rsrc_idx].max_cnt = (u32)temp;
	size = (u32)RS_64_1(temp, 32);
	obj_info[rsrc_idx].size = LS_64_1(1, size);

	return temp;
}

/**
 * i40iw_sc_parse_fpm_query_buf() - parses fpm query buffer
 * @buf: ptr to fpm query buffer
 * @info: ptr to i40iw_hmc_obj_info struct
 * @hmc_fpm_misc: ptr to fpm data
 *
 * parses fpm query buffer and copy max_cnt and
 * size value of hmc objects in hmc_info
 */
static enum i40iw_status_code i40iw_sc_parse_fpm_query_buf(
				u64 *buf,
				struct i40iw_hmc_info *hmc_info,
				struct i40iw_hmc_fpm_misc *hmc_fpm_misc)
{
	struct i40iw_hmc_obj_info *obj_info;
	u64 temp;
	u32 size;
	u16 max_pe_sds;

	obj_info = hmc_info->hmc_obj;

	get_64bit_val(buf, 0, &temp);
	hmc_info->first_sd_index = (u16)RS_64(temp, I40IW_QUERY_FPM_FIRST_PE_SD_INDEX);
	max_pe_sds = (u16)RS_64(temp, I40IW_QUERY_FPM_MAX_PE_SDS);

	/* Reduce SD count for VFs by 1 to account for PBLE backing page rounding */
	if (hmc_info->hmc_fn_id >= I40IW_FIRST_VF_FPM_ID)
		max_pe_sds--;
	hmc_fpm_misc->max_sds = max_pe_sds;
	hmc_info->sd_table.sd_cnt = max_pe_sds + hmc_info->first_sd_index;

	get_64bit_val(buf, 8, &temp);
	obj_info[I40IW_HMC_IW_QP].max_cnt = (u32)RS_64(temp, I40IW_QUERY_FPM_MAX_QPS);
	size = (u32)RS_64_1(temp, 32);
	obj_info[I40IW_HMC_IW_QP].size = LS_64_1(1, size);

	get_64bit_val(buf, 16, &temp);
	obj_info[I40IW_HMC_IW_CQ].max_cnt = (u32)RS_64(temp, I40IW_QUERY_FPM_MAX_CQS);
	size = (u32)RS_64_1(temp, 32);
	obj_info[I40IW_HMC_IW_CQ].size = LS_64_1(1, size);

	i40iw_sc_decode_fpm_query(buf, 32, obj_info, I40IW_HMC_IW_HTE);
	i40iw_sc_decode_fpm_query(buf, 40, obj_info, I40IW_HMC_IW_ARP);

	obj_info[I40IW_HMC_IW_APBVT_ENTRY].size = 8192;
	obj_info[I40IW_HMC_IW_APBVT_ENTRY].max_cnt = 1;

	i40iw_sc_decode_fpm_query(buf, 48, obj_info, I40IW_HMC_IW_MR);
	i40iw_sc_decode_fpm_query(buf, 56, obj_info, I40IW_HMC_IW_XF);

	get_64bit_val(buf, 64, &temp);
	obj_info[I40IW_HMC_IW_XFFL].max_cnt = (u32)temp;
	obj_info[I40IW_HMC_IW_XFFL].size = 4;
	hmc_fpm_misc->xf_block_size = RS_64(temp, I40IW_QUERY_FPM_XFBLOCKSIZE);
	if (!hmc_fpm_misc->xf_block_size)
		return I40IW_ERR_INVALID_SIZE;

	i40iw_sc_decode_fpm_query(buf, 72, obj_info, I40IW_HMC_IW_Q1);

	get_64bit_val(buf, 80, &temp);
	obj_info[I40IW_HMC_IW_Q1FL].max_cnt = (u32)temp;
	obj_info[I40IW_HMC_IW_Q1FL].size = 4;
	hmc_fpm_misc->q1_block_size = RS_64(temp, I40IW_QUERY_FPM_Q1BLOCKSIZE);
	if (!hmc_fpm_misc->q1_block_size)
		return I40IW_ERR_INVALID_SIZE;

	i40iw_sc_decode_fpm_query(buf, 88, obj_info, I40IW_HMC_IW_TIMER);

	get_64bit_val(buf, 112, &temp);
	obj_info[I40IW_HMC_IW_PBLE].max_cnt = (u32)temp;
	obj_info[I40IW_HMC_IW_PBLE].size = 8;

	get_64bit_val(buf, 120, &temp);
	hmc_fpm_misc->max_ceqs = (u8)RS_64(temp, I40IW_QUERY_FPM_MAX_CEQS);
	hmc_fpm_misc->ht_multiplier = RS_64(temp, I40IW_QUERY_FPM_HTMULTIPLIER);
	hmc_fpm_misc->timer_bucket = RS_64(temp, I40IW_QUERY_FPM_TIMERBUCKET);

	return 0;
}

/**
 * i40iw_fill_qos_list - Change all unknown qs handles to available ones
 * @qs_list: list of qs_handles to be fixed with valid qs_handles
 */
static void i40iw_fill_qos_list(u16 *qs_list)
{
	u16 qshandle = qs_list[0];
	int i;

	for (i = 0; i < I40IW_MAX_USER_PRIORITY; i++) {
		if (qs_list[i] == QS_HANDLE_UNKNOWN)
			qs_list[i] = qshandle;
		else
			qshandle = qs_list[i];
	}
}

/**
 * i40iw_qp_from_entry - Given entry, get to the qp structure
 * @entry: Points to list of qp structure
 */
static struct i40iw_sc_qp *i40iw_qp_from_entry(struct list_head *entry)
{
	if (!entry)
		return NULL;

	return (struct i40iw_sc_qp *)((char *)entry - offsetof(struct i40iw_sc_qp, list));
}

/**
 * i40iw_get_qp - get the next qp from the list given current qp
 * @head: Listhead of qp's
 * @qp: current qp
 */
static struct i40iw_sc_qp *i40iw_get_qp(struct list_head *head, struct i40iw_sc_qp *qp)
{
	struct list_head *entry = NULL;
	struct list_head *lastentry;

	if (list_empty(head))
		return NULL;

	if (!qp) {
		entry = head->next;
	} else {
		lastentry = &qp->list;
		entry = (lastentry != head) ? lastentry->next : NULL;
	}

	return i40iw_qp_from_entry(entry);
}

/**
 * i40iw_change_l2params - given the new l2 parameters, change all qp
 * @vsi: pointer to the vsi structure
 * @l2params: New paramaters from l2
 */
void i40iw_change_l2params(struct i40iw_sc_vsi *vsi, struct i40iw_l2params *l2params)
{
	struct i40iw_sc_dev *dev = vsi->dev;
	struct i40iw_sc_qp *qp = NULL;
	bool qs_handle_change = false;
	unsigned long flags;
	u16 qs_handle;
	int i;

	if (vsi->mtu != l2params->mtu) {
		vsi->mtu = l2params->mtu;
		i40iw_reinitialize_ieq(dev);
	}

	i40iw_fill_qos_list(l2params->qs_handle_list);
	for (i = 0; i < I40IW_MAX_USER_PRIORITY; i++) {
		qs_handle = l2params->qs_handle_list[i];
		if (vsi->qos[i].qs_handle != qs_handle)
			qs_handle_change = true;
		spin_lock_irqsave(&vsi->qos[i].lock, flags);
		qp = i40iw_get_qp(&vsi->qos[i].qplist, qp);
		while (qp) {
			if (qs_handle_change) {
				qp->qs_handle = qs_handle;
				/* issue cqp suspend command */
				i40iw_qp_suspend_resume(dev, qp, true);
			}
			qp = i40iw_get_qp(&vsi->qos[i].qplist, qp);
		}
		spin_unlock_irqrestore(&vsi->qos[i].lock, flags);
		vsi->qos[i].qs_handle = qs_handle;
	}
}

/**
 * i40iw_qp_rem_qos - remove qp from qos lists during destroy qp
 * @qp: qp to be removed from qos
 */
void i40iw_qp_rem_qos(struct i40iw_sc_qp *qp)
{
	struct i40iw_sc_vsi *vsi = qp->vsi;
	unsigned long flags;

	if (!qp->on_qoslist)
		return;
	spin_lock_irqsave(&vsi->qos[qp->user_pri].lock, flags);
	list_del(&qp->list);
	spin_unlock_irqrestore(&vsi->qos[qp->user_pri].lock, flags);
}

/**
 * i40iw_qp_add_qos - called during setctx fot qp to be added to qos
 * @qp: qp to be added to qos
 */
void i40iw_qp_add_qos(struct i40iw_sc_qp *qp)
{
	struct i40iw_sc_vsi *vsi = qp->vsi;
	unsigned long flags;

	if (qp->on_qoslist)
		return;
	spin_lock_irqsave(&vsi->qos[qp->user_pri].lock, flags);
	qp->qs_handle = vsi->qos[qp->user_pri].qs_handle;
	list_add(&qp->list, &vsi->qos[qp->user_pri].qplist);
	qp->on_qoslist = true;
	spin_unlock_irqrestore(&vsi->qos[qp->user_pri].lock, flags);
}

/**
 * i40iw_sc_pd_init - initialize sc pd struct
 * @dev: sc device struct
 * @pd: sc pd ptr
 * @pd_id: pd_id for allocated pd
 * @abi_ver: ABI version from user context, -1 if not valid
 */
static void i40iw_sc_pd_init(struct i40iw_sc_dev *dev,
			     struct i40iw_sc_pd *pd,
			     u16 pd_id,
			     int abi_ver)
{
	pd->size = sizeof(*pd);
	pd->pd_id = pd_id;
	pd->abi_ver = abi_ver;
	pd->dev = dev;
}

/**
 * i40iw_get_encoded_wqe_size - given wq size, returns hardware encoded size
 * @wqsize: size of the wq (sq, rq, srq) to encoded_size
 * @cqpsq: encoded size for sq for cqp as its encoded size is 1+ other wq's
 */
u8 i40iw_get_encoded_wqe_size(u32 wqsize, bool cqpsq)
{
	u8 encoded_size = 0;

	/* cqp sq's hw coded value starts from 1 for size of 4
	 * while it starts from 0 for qp' wq's.
	 */
	if (cqpsq)
		encoded_size = 1;
	wqsize >>= 2;
	while (wqsize >>= 1)
		encoded_size++;
	return encoded_size;
}

/**
 * i40iw_sc_cqp_init - Initialize buffers for a control Queue Pair
 * @cqp: IWARP control queue pair pointer
 * @info: IWARP control queue pair init info pointer
 *
 * Initializes the object and context buffers for a control Queue Pair.
 */
static enum i40iw_status_code i40iw_sc_cqp_init(struct i40iw_sc_cqp *cqp,
						struct i40iw_cqp_init_info *info)
{
	u8 hw_sq_size;

	if ((info->sq_size > I40IW_CQP_SW_SQSIZE_2048) ||
	    (info->sq_size < I40IW_CQP_SW_SQSIZE_4) ||
	    ((info->sq_size & (info->sq_size - 1))))
		return I40IW_ERR_INVALID_SIZE;

	hw_sq_size = i40iw_get_encoded_wqe_size(info->sq_size, true);
	cqp->size = sizeof(*cqp);
	cqp->sq_size = info->sq_size;
	cqp->hw_sq_size = hw_sq_size;
	cqp->sq_base = info->sq;
	cqp->host_ctx = info->host_ctx;
	cqp->sq_pa = info->sq_pa;
	cqp->host_ctx_pa = info->host_ctx_pa;
	cqp->dev = info->dev;
	cqp->struct_ver = info->struct_ver;
	cqp->scratch_array = info->scratch_array;
	cqp->polarity = 0;
	cqp->en_datacenter_tcp = info->en_datacenter_tcp;
	cqp->enabled_vf_count = info->enabled_vf_count;
	cqp->hmc_profile = info->hmc_profile;
	info->dev->cqp = cqp;

	I40IW_RING_INIT(cqp->sq_ring, cqp->sq_size);
	cqp->dev->cqp_cmd_stats[OP_REQUESTED_COMMANDS] = 0;
	cqp->dev->cqp_cmd_stats[OP_COMPLETED_COMMANDS] = 0;
	INIT_LIST_HEAD(&cqp->dev->cqp_cmd_head);               /* for the cqp commands backlog. */

	i40iw_wr32(cqp->dev->hw, I40E_PFPE_CQPTAIL, 0);
	i40iw_wr32(cqp->dev->hw, I40E_PFPE_CQPDB, 0);

	i40iw_debug(cqp->dev, I40IW_DEBUG_WQE,
		    "%s: sq_size[%04d] hw_sq_size[%04d] sq_base[%p] sq_pa[%llxh] cqp[%p] polarity[x%04X]\n",
		    __func__, cqp->sq_size, cqp->hw_sq_size,
		    cqp->sq_base, cqp->sq_pa, cqp, cqp->polarity);
	return 0;
}

/**
 * i40iw_sc_cqp_create - create cqp during bringup
 * @cqp: struct for cqp hw
 * @maj_err: If error, major err number
 * @min_err: If error, minor err number
 */
static enum i40iw_status_code i40iw_sc_cqp_create(struct i40iw_sc_cqp *cqp,
						  u16 *maj_err,
						  u16 *min_err)
{
	u64 temp;
	u32 cnt = 0, p1, p2, val = 0, err_code;
	enum i40iw_status_code ret_code;

	*maj_err = 0;
	*min_err = 0;

	ret_code = i40iw_allocate_dma_mem(cqp->dev->hw,
					  &cqp->sdbuf,
					  I40IW_UPDATE_SD_BUF_SIZE * cqp->sq_size,
					  I40IW_SD_BUF_ALIGNMENT);

	if (ret_code)
		goto exit;

	temp = LS_64(cqp->hw_sq_size, I40IW_CQPHC_SQSIZE) |
	       LS_64(cqp->struct_ver, I40IW_CQPHC_SVER);

	set_64bit_val(cqp->host_ctx, 0, temp);
	set_64bit_val(cqp->host_ctx, 8, cqp->sq_pa);
	temp = LS_64(cqp->enabled_vf_count, I40IW_CQPHC_ENABLED_VFS) |
	       LS_64(cqp->hmc_profile, I40IW_CQPHC_HMC_PROFILE);
	set_64bit_val(cqp->host_ctx, 16, temp);
	set_64bit_val(cqp->host_ctx, 24, (uintptr_t)cqp);
	set_64bit_val(cqp->host_ctx, 32, 0);
	set_64bit_val(cqp->host_ctx, 40, 0);
	set_64bit_val(cqp->host_ctx, 48, 0);
	set_64bit_val(cqp->host_ctx, 56, 0);

	i40iw_debug_buf(cqp->dev, I40IW_DEBUG_WQE, "CQP_HOST_CTX",
			cqp->host_ctx, I40IW_CQP_CTX_SIZE * 8);

	p1 = RS_32_1(cqp->host_ctx_pa, 32);
	p2 = (u32)cqp->host_ctx_pa;

	if (cqp->dev->is_pf) {
		i40iw_wr32(cqp->dev->hw, I40E_PFPE_CCQPHIGH, p1);
		i40iw_wr32(cqp->dev->hw, I40E_PFPE_CCQPLOW, p2);
	} else {
		i40iw_wr32(cqp->dev->hw, I40E_VFPE_CCQPHIGH1, p1);
		i40iw_wr32(cqp->dev->hw, I40E_VFPE_CCQPLOW1, p2);
	}
	do {
		if (cnt++ > I40IW_DONE_COUNT) {
			i40iw_free_dma_mem(cqp->dev->hw, &cqp->sdbuf);
			ret_code = I40IW_ERR_TIMEOUT;
			/*
			 * read PFPE_CQPERRORCODES register to get the minor
			 * and major error code
			 */
			if (cqp->dev->is_pf)
				err_code = i40iw_rd32(cqp->dev->hw, I40E_PFPE_CQPERRCODES);
			else
				err_code = i40iw_rd32(cqp->dev->hw, I40E_VFPE_CQPERRCODES1);
			*min_err = RS_32(err_code, I40E_PFPE_CQPERRCODES_CQP_MINOR_CODE);
			*maj_err = RS_32(err_code, I40E_PFPE_CQPERRCODES_CQP_MAJOR_CODE);
			goto exit;
		}
		udelay(I40IW_SLEEP_COUNT);
		if (cqp->dev->is_pf)
			val = i40iw_rd32(cqp->dev->hw, I40E_PFPE_CCQPSTATUS);
		else
			val = i40iw_rd32(cqp->dev->hw, I40E_VFPE_CCQPSTATUS1);
	} while (!val);

exit:
	if (!ret_code)
		cqp->process_cqp_sds = i40iw_update_sds_noccq;
	return ret_code;
}

/**
 * i40iw_sc_cqp_post_sq - post of cqp's sq
 * @cqp: struct for cqp hw
 */
void i40iw_sc_cqp_post_sq(struct i40iw_sc_cqp *cqp)
{
	if (cqp->dev->is_pf)
		i40iw_wr32(cqp->dev->hw, I40E_PFPE_CQPDB, I40IW_RING_GETCURRENT_HEAD(cqp->sq_ring));
	else
		i40iw_wr32(cqp->dev->hw, I40E_VFPE_CQPDB1, I40IW_RING_GETCURRENT_HEAD(cqp->sq_ring));

	i40iw_debug(cqp->dev,
		    I40IW_DEBUG_WQE,
		    "%s: HEAD_TAIL[%04d,%04d,%04d]\n",
		    __func__,
		    cqp->sq_ring.head,
		    cqp->sq_ring.tail,
		    cqp->sq_ring.size);
}

/**
 * i40iw_sc_cqp_get_next_send_wqe_idx - get next WQE on CQP SQ and pass back the index
 * @cqp: pointer to CQP structure
 * @scratch: private data for CQP WQE
 * @wqe_idx: WQE index for next WQE on CQP SQ
 */
static u64 *i40iw_sc_cqp_get_next_send_wqe_idx(struct i40iw_sc_cqp *cqp,
					       u64 scratch, u32 *wqe_idx)
{
	u64 *wqe = NULL;
	enum i40iw_status_code ret_code;

	if (I40IW_RING_FULL_ERR(cqp->sq_ring)) {
		i40iw_debug(cqp->dev,
			    I40IW_DEBUG_WQE,
			    "%s: ring is full head %x tail %x size %x\n",
			    __func__,
			    cqp->sq_ring.head,
			    cqp->sq_ring.tail,
			    cqp->sq_ring.size);
		return NULL;
	}
	I40IW_ATOMIC_RING_MOVE_HEAD(cqp->sq_ring, *wqe_idx, ret_code);
	cqp->dev->cqp_cmd_stats[OP_REQUESTED_COMMANDS]++;
	if (ret_code)
		return NULL;
	if (!*wqe_idx)
		cqp->polarity = !cqp->polarity;

	wqe = cqp->sq_base[*wqe_idx].elem;
	cqp->scratch_array[*wqe_idx] = scratch;
	I40IW_CQP_INIT_WQE(wqe);

	return wqe;
}

/**
 * i40iw_sc_cqp_get_next_send_wqe - get next wqe on cqp sq
 * @cqp: struct for cqp hw
 * @scratch: private data for CQP WQE
 */
u64 *i40iw_sc_cqp_get_next_send_wqe(struct i40iw_sc_cqp *cqp, u64 scratch)
{
	u32 wqe_idx;

	return i40iw_sc_cqp_get_next_send_wqe_idx(cqp, scratch, &wqe_idx);
}

/**
 * i40iw_sc_cqp_destroy - destroy cqp during close
 * @cqp: struct for cqp hw
 */
static enum i40iw_status_code i40iw_sc_cqp_destroy(struct i40iw_sc_cqp *cqp)
{
	u32 cnt = 0, val = 1;
	enum i40iw_status_code ret_code = 0;
	u32 cqpstat_addr;

	if (cqp->dev->is_pf) {
		i40iw_wr32(cqp->dev->hw, I40E_PFPE_CCQPHIGH, 0);
		i40iw_wr32(cqp->dev->hw, I40E_PFPE_CCQPLOW, 0);
		cqpstat_addr = I40E_PFPE_CCQPSTATUS;
	} else {
		i40iw_wr32(cqp->dev->hw, I40E_VFPE_CCQPHIGH1, 0);
		i40iw_wr32(cqp->dev->hw, I40E_VFPE_CCQPLOW1, 0);
		cqpstat_addr = I40E_VFPE_CCQPSTATUS1;
	}
	do {
		if (cnt++ > I40IW_DONE_COUNT) {
			ret_code = I40IW_ERR_TIMEOUT;
			break;
		}
		udelay(I40IW_SLEEP_COUNT);
		val = i40iw_rd32(cqp->dev->hw, cqpstat_addr);
	} while (val);

	i40iw_free_dma_mem(cqp->dev->hw, &cqp->sdbuf);
	return ret_code;
}

/**
 * i40iw_sc_ccq_arm - enable intr for control cq
 * @ccq: ccq sc struct
 */
static void i40iw_sc_ccq_arm(struct i40iw_sc_cq *ccq)
{
	u64 temp_val;
	u16 sw_cq_sel;
	u8 arm_next_se;
	u8 arm_seq_num;

	/* write to cq doorbell shadow area */
	/* arm next se should always be zero */
	get_64bit_val(ccq->cq_uk.shadow_area, 32, &temp_val);

	sw_cq_sel = (u16)RS_64(temp_val, I40IW_CQ_DBSA_SW_CQ_SELECT);
	arm_next_se = (u8)RS_64(temp_val, I40IW_CQ_DBSA_ARM_NEXT_SE);

	arm_seq_num = (u8)RS_64(temp_val, I40IW_CQ_DBSA_ARM_SEQ_NUM);
	arm_seq_num++;

	temp_val = LS_64(arm_seq_num, I40IW_CQ_DBSA_ARM_SEQ_NUM) |
		   LS_64(sw_cq_sel, I40IW_CQ_DBSA_SW_CQ_SELECT) |
		   LS_64(arm_next_se, I40IW_CQ_DBSA_ARM_NEXT_SE) |
		   LS_64(1, I40IW_CQ_DBSA_ARM_NEXT);

	set_64bit_val(ccq->cq_uk.shadow_area, 32, temp_val);

	wmb();       /* make sure shadow area is updated before arming */

	if (ccq->dev->is_pf)
		i40iw_wr32(ccq->dev->hw, I40E_PFPE_CQARM, ccq->cq_uk.cq_id);
	else
		i40iw_wr32(ccq->dev->hw, I40E_VFPE_CQARM1, ccq->cq_uk.cq_id);
}

/**
 * i40iw_sc_ccq_get_cqe_info - get ccq's cq entry
 * @ccq: ccq sc struct
 * @info: completion q entry to return
 */
static enum i40iw_status_code i40iw_sc_ccq_get_cqe_info(
					struct i40iw_sc_cq *ccq,
					struct i40iw_ccq_cqe_info *info)
{
	u64 qp_ctx, temp, temp1;
	u64 *cqe;
	struct i40iw_sc_cqp *cqp;
	u32 wqe_idx;
	u8 polarity;
	enum i40iw_status_code ret_code = 0;

	if (ccq->cq_uk.avoid_mem_cflct)
		cqe = (u64 *)I40IW_GET_CURRENT_EXTENDED_CQ_ELEMENT(&ccq->cq_uk);
	else
		cqe = (u64 *)I40IW_GET_CURRENT_CQ_ELEMENT(&ccq->cq_uk);

	get_64bit_val(cqe, 24, &temp);
	polarity = (u8)RS_64(temp, I40IW_CQ_VALID);
	if (polarity != ccq->cq_uk.polarity)
		return I40IW_ERR_QUEUE_EMPTY;

	get_64bit_val(cqe, 8, &qp_ctx);
	cqp = (struct i40iw_sc_cqp *)(unsigned long)qp_ctx;
	info->error = (bool)RS_64(temp, I40IW_CQ_ERROR);
	info->min_err_code = (u16)RS_64(temp, I40IW_CQ_MINERR);
	if (info->error) {
		info->maj_err_code = (u16)RS_64(temp, I40IW_CQ_MAJERR);
		info->min_err_code = (u16)RS_64(temp, I40IW_CQ_MINERR);
	}
	wqe_idx = (u32)RS_64(temp, I40IW_CQ_WQEIDX);
	info->scratch = cqp->scratch_array[wqe_idx];

	get_64bit_val(cqe, 16, &temp1);
	info->op_ret_val = (u32)RS_64(temp1, I40IW_CCQ_OPRETVAL);
	get_64bit_val(cqp->sq_base[wqe_idx].elem, 24, &temp1);
	info->op_code = (u8)RS_64(temp1, I40IW_CQPSQ_OPCODE);
	info->cqp = cqp;

	/*  move the head for cq */
	I40IW_RING_MOVE_HEAD(ccq->cq_uk.cq_ring, ret_code);
	if (I40IW_RING_GETCURRENT_HEAD(ccq->cq_uk.cq_ring) == 0)
		ccq->cq_uk.polarity ^= 1;

	/* update cq tail in cq shadow memory also */
	I40IW_RING_MOVE_TAIL(ccq->cq_uk.cq_ring);
	set_64bit_val(ccq->cq_uk.shadow_area,
		      0,
		      I40IW_RING_GETCURRENT_HEAD(ccq->cq_uk.cq_ring));
	wmb(); /* write shadow area before tail */
	I40IW_RING_MOVE_TAIL(cqp->sq_ring);
	ccq->dev->cqp_cmd_stats[OP_COMPLETED_COMMANDS]++;

	return ret_code;
}

/**
 * i40iw_sc_poll_for_cqp_op_done - Waits for last write to complete in CQP SQ
 * @cqp: struct for cqp hw
 * @op_code: cqp opcode for completion
 * @info: completion q entry to return
 */
static enum i40iw_status_code i40iw_sc_poll_for_cqp_op_done(
					struct i40iw_sc_cqp *cqp,
					u8 op_code,
					struct i40iw_ccq_cqe_info *compl_info)
{
	struct i40iw_ccq_cqe_info info;
	struct i40iw_sc_cq *ccq;
	enum i40iw_status_code ret_code = 0;
	u32 cnt = 0;

	memset(&info, 0, sizeof(info));
	ccq = cqp->dev->ccq;
	while (1) {
		if (cnt++ > I40IW_DONE_COUNT)
			return I40IW_ERR_TIMEOUT;

		if (i40iw_sc_ccq_get_cqe_info(ccq, &info)) {
			udelay(I40IW_SLEEP_COUNT);
			continue;
		}

		if (info.error) {
			ret_code = I40IW_ERR_CQP_COMPL_ERROR;
			break;
		}
		/* check if opcode is cq create */
		if (op_code != info.op_code) {
			i40iw_debug(cqp->dev, I40IW_DEBUG_WQE,
				    "%s: opcode mismatch for my op code 0x%x, returned opcode %x\n",
				    __func__, op_code, info.op_code);
		}
		/* success, exit out of the loop */
		if (op_code == info.op_code)
			break;
	}

	if (compl_info)
		memcpy(compl_info, &info, sizeof(*compl_info));

	return ret_code;
}

/**
 * i40iw_sc_manage_push_page - Handle push page
 * @cqp: struct for cqp hw
 * @info: push page info
 * @scratch: u64 saved to be used during cqp completion
 * @post_sq: flag for cqp db to ring
 */
static enum i40iw_status_code i40iw_sc_manage_push_page(
				struct i40iw_sc_cqp *cqp,
				struct i40iw_cqp_manage_push_page_info *info,
				u64 scratch,
				bool post_sq)
{
	u64 *wqe;
	u64 header;

	if (info->push_idx >= I40IW_MAX_PUSH_PAGE_COUNT)
		return I40IW_ERR_INVALID_PUSH_PAGE_INDEX;

	wqe = i40iw_sc_cqp_get_next_send_wqe(cqp, scratch);
	if (!wqe)
		return I40IW_ERR_RING_FULL;

	set_64bit_val(wqe, 16, info->qs_handle);

	header = LS_64(info->push_idx, I40IW_CQPSQ_MPP_PPIDX) |
		 LS_64(I40IW_CQP_OP_MANAGE_PUSH_PAGES, I40IW_CQPSQ_OPCODE) |
		 LS_64(cqp->polarity, I40IW_CQPSQ_WQEVALID) |
		 LS_64(info->free_page, I40IW_CQPSQ_MPP_FREE_PAGE);

	i40iw_insert_wqe_hdr(wqe, header);

	i40iw_debug_buf(cqp->dev, I40IW_DEBUG_WQE, "MANAGE_PUSH_PAGES WQE",
			wqe, I40IW_CQP_WQE_SIZE * 8);

	if (post_sq)
		i40iw_sc_cqp_post_sq(cqp);
	return 0;
}

/**
 * i40iw_sc_manage_hmc_pm_func_table - manage of function table
 * @cqp: struct for cqp hw
 * @scratch: u64 saved to be used during cqp completion
 * @vf_index: vf index for cqp
 * @free_pm_fcn: function number
 * @post_sq: flag for cqp db to ring
 */
static enum i40iw_status_code i40iw_sc_manage_hmc_pm_func_table(
				struct i40iw_sc_cqp *cqp,
				u64 scratch,
				u8 vf_index,
				bool free_pm_fcn,
				bool post_sq)
{
	u64 *wqe;
	u64 header;

	if (vf_index >= I40IW_MAX_VF_PER_PF)
		return I40IW_ERR_INVALID_VF_ID;
	wqe = i40iw_sc_cqp_get_next_send_wqe(cqp, scratch);
	if (!wqe)
		return I40IW_ERR_RING_FULL;

	header = LS_64(vf_index, I40IW_CQPSQ_MHMC_VFIDX) |
		 LS_64(I40IW_CQP_OP_MANAGE_HMC_PM_FUNC_TABLE, I40IW_CQPSQ_OPCODE) |
		 LS_64(free_pm_fcn, I40IW_CQPSQ_MHMC_FREEPMFN) |
		 LS_64(cqp->polarity, I40IW_CQPSQ_WQEVALID);

	i40iw_insert_wqe_hdr(wqe, header);
	i40iw_debug_buf(cqp->dev, I40IW_DEBUG_WQE, "MANAGE_HMC_PM_FUNC_TABLE WQE",
			wqe, I40IW_CQP_WQE_SIZE * 8);
	if (post_sq)
		i40iw_sc_cqp_post_sq(cqp);
	return 0;
}

/**
 * i40iw_sc_set_hmc_resource_profile - cqp wqe for hmc profile
 * @cqp: struct for cqp hw
 * @scratch: u64 saved to be used during cqp completion
 * @hmc_profile_type: type of profile to set
 * @vf_num: vf number for profile
 * @post_sq: flag for cqp db to ring
 * @poll_registers: flag to poll register for cqp completion
 */
static enum i40iw_status_code i40iw_sc_set_hmc_resource_profile(
				struct i40iw_sc_cqp *cqp,
				u64 scratch,
				u8 hmc_profile_type,
				u8 vf_num, bool post_sq,
				bool poll_registers)
{
	u64 *wqe;
	u64 header;
	u32 val, tail, error;
	enum i40iw_status_code ret_code = 0;

	wqe = i40iw_sc_cqp_get_next_send_wqe(cqp, scratch);
	if (!wqe)
		return I40IW_ERR_RING_FULL;

	set_64bit_val(wqe, 16,
		      (LS_64(hmc_profile_type, I40IW_CQPSQ_SHMCRP_HMC_PROFILE) |
				LS_64(vf_num, I40IW_CQPSQ_SHMCRP_VFNUM)));

	header = LS_64(I40IW_CQP_OP_SET_HMC_RESOURCE_PROFILE, I40IW_CQPSQ_OPCODE) |
		       LS_64(cqp->polarity, I40IW_CQPSQ_WQEVALID);

	i40iw_insert_wqe_hdr(wqe, header);

	i40iw_debug_buf(cqp->dev, I40IW_DEBUG_WQE, "MANAGE_HMC_PM_FUNC_TABLE WQE",
			wqe, I40IW_CQP_WQE_SIZE * 8);

	i40iw_get_cqp_reg_info(cqp, &val, &tail, &error);
	if (error)
		return I40IW_ERR_CQP_COMPL_ERROR;

	if (post_sq) {
		i40iw_sc_cqp_post_sq(cqp);
		if (poll_registers)
			ret_code = i40iw_cqp_poll_registers(cqp, tail, 1000000);
		else
			ret_code = i40iw_sc_poll_for_cqp_op_done(cqp,
								 I40IW_CQP_OP_SHMC_PAGES_ALLOCATED,
								 NULL);
	}

	return ret_code;
}

/**
 * i40iw_sc_manage_hmc_pm_func_table_done - wait for cqp wqe completion for function table
 * @cqp: struct for cqp hw
 */
static enum i40iw_status_code i40iw_sc_manage_hmc_pm_func_table_done(struct i40iw_sc_cqp *cqp)
{
	return i40iw_sc_poll_for_cqp_op_done(cqp, I40IW_CQP_OP_MANAGE_HMC_PM_FUNC_TABLE, NULL);
}

/**
 * i40iw_sc_commit_fpm_values_done - wait for cqp eqe completion for fpm commit
 * @cqp: struct for cqp hw
 */
static enum i40iw_status_code i40iw_sc_commit_fpm_values_done(struct i40iw_sc_cqp *cqp)
{
	return i40iw_sc_poll_for_cqp_op_done(cqp, I40IW_CQP_OP_COMMIT_FPM_VALUES, NULL);
}

/**
 * i40iw_sc_commit_fpm_values - cqp wqe for commit fpm values
 * @cqp: struct for cqp hw
 * @scratch: u64 saved to be used during cqp completion
 * @hmc_fn_id: hmc function id
 * @commit_fpm_mem; Memory for fpm values
 * @post_sq: flag for cqp db to ring
 * @wait_type: poll ccq or cqp registers for cqp completion
 */
static enum i40iw_status_code i40iw_sc_commit_fpm_values(
					struct i40iw_sc_cqp *cqp,
					u64 scratch,
					u8 hmc_fn_id,
					struct i40iw_dma_mem *commit_fpm_mem,
					bool post_sq,
					u8 wait_type)
{
	u64 *wqe;
	u64 header;
	u32 tail, val, error;
	enum i40iw_status_code ret_code = 0;

	wqe = i40iw_sc_cqp_get_next_send_wqe(cqp, scratch);
	if (!wqe)
		return I40IW_ERR_RING_FULL;

	set_64bit_val(wqe, 16, hmc_fn_id);
	set_64bit_val(wqe, 32, commit_fpm_mem->pa);

	header = LS_64(I40IW_CQP_OP_COMMIT_FPM_VALUES, I40IW_CQPSQ_OPCODE) |
		 LS_64(cqp->polarity, I40IW_CQPSQ_WQEVALID);

	i40iw_insert_wqe_hdr(wqe, header);

	i40iw_debug_buf(cqp->dev, I40IW_DEBUG_WQE, "COMMIT_FPM_VALUES WQE",
			wqe, I40IW_CQP_WQE_SIZE * 8);

	i40iw_get_cqp_reg_info(cqp, &val, &tail, &error);
	if (error)
		return I40IW_ERR_CQP_COMPL_ERROR;

	if (post_sq) {
		i40iw_sc_cqp_post_sq(cqp);

		if (wait_type == I40IW_CQP_WAIT_POLL_REGS)
			ret_code = i40iw_cqp_poll_registers(cqp, tail, I40IW_DONE_COUNT);
		else if (wait_type == I40IW_CQP_WAIT_POLL_CQ)
			ret_code = i40iw_sc_commit_fpm_values_done(cqp);
	}

	return ret_code;
}

/**
 * i40iw_sc_query_fpm_values_done - poll for cqp wqe completion for query fpm
 * @cqp: struct for cqp hw
 */
static enum i40iw_status_code i40iw_sc_query_fpm_values_done(struct i40iw_sc_cqp *cqp)
{
	return i40iw_sc_poll_for_cqp_op_done(cqp, I40IW_CQP_OP_QUERY_FPM_VALUES, NULL);
}

/**
 * i40iw_sc_query_fpm_values - cqp wqe query fpm values
 * @cqp: struct for cqp hw
 * @scratch: u64 saved to be used during cqp completion
 * @hmc_fn_id: hmc function id
 * @query_fpm_mem: memory for return fpm values
 * @post_sq: flag for cqp db to ring
 * @wait_type: poll ccq or cqp registers for cqp completion
 */
static enum i40iw_status_code i40iw_sc_query_fpm_values(
					struct i40iw_sc_cqp *cqp,
					u64 scratch,
					u8 hmc_fn_id,
					struct i40iw_dma_mem *query_fpm_mem,
					bool post_sq,
					u8 wait_type)
{
	u64 *wqe;
	u64 header;
	u32 tail, val, error;
	enum i40iw_status_code ret_code = 0;

	wqe = i40iw_sc_cqp_get_next_send_wqe(cqp, scratch);
	if (!wqe)
		return I40IW_ERR_RING_FULL;

	set_64bit_val(wqe, 16, hmc_fn_id);
	set_64bit_val(wqe, 32, query_fpm_mem->pa);

	header = LS_64(I40IW_CQP_OP_QUERY_FPM_VALUES, I40IW_CQPSQ_OPCODE) |
		 LS_64(cqp->polarity, I40IW_CQPSQ_WQEVALID);

	i40iw_insert_wqe_hdr(wqe, header);

	i40iw_debug_buf(cqp->dev, I40IW_DEBUG_WQE, "QUERY_FPM WQE",
			wqe, I40IW_CQP_WQE_SIZE * 8);

	/* read the tail from CQP_TAIL register */
	i40iw_get_cqp_reg_info(cqp, &val, &tail, &error);

	if (error)
		return I40IW_ERR_CQP_COMPL_ERROR;

	if (post_sq) {
		i40iw_sc_cqp_post_sq(cqp);
		if (wait_type == I40IW_CQP_WAIT_POLL_REGS)
			ret_code = i40iw_cqp_poll_registers(cqp, tail, I40IW_DONE_COUNT);
		else if (wait_type == I40IW_CQP_WAIT_POLL_CQ)
			ret_code = i40iw_sc_query_fpm_values_done(cqp);
	}

	return ret_code;
}

/**
 * i40iw_sc_add_arp_cache_entry - cqp wqe add arp cache entry
 * @cqp: struct for cqp hw
 * @info: arp entry information
 * @scratch: u64 saved to be used during cqp completion
 * @post_sq: flag for cqp db to ring
 */
static enum i40iw_status_code i40iw_sc_add_arp_cache_entry(
				struct i40iw_sc_cqp *cqp,
				struct i40iw_add_arp_cache_entry_info *info,
				u64 scratch,
				bool post_sq)
{
	u64 *wqe;
	u64 temp, header;

	wqe = i40iw_sc_cqp_get_next_send_wqe(cqp, scratch);
	if (!wqe)
		return I40IW_ERR_RING_FULL;
	set_64bit_val(wqe, 8, info->reach_max);

	temp = info->mac_addr[5] |
	       LS_64_1(info->mac_addr[4], 8) |
	       LS_64_1(info->mac_addr[3], 16) |
	       LS_64_1(info->mac_addr[2], 24) |
	       LS_64_1(info->mac_addr[1], 32) |
	       LS_64_1(info->mac_addr[0], 40);

	set_64bit_val(wqe, 16, temp);

	header = info->arp_index |
		 LS_64(I40IW_CQP_OP_MANAGE_ARP, I40IW_CQPSQ_OPCODE) |
		 LS_64((info->permanent ? 1 : 0), I40IW_CQPSQ_MAT_PERMANENT) |
		 LS_64(1, I40IW_CQPSQ_MAT_ENTRYVALID) |
		 LS_64(cqp->polarity, I40IW_CQPSQ_WQEVALID);

	i40iw_insert_wqe_hdr(wqe, header);

	i40iw_debug_buf(cqp->dev, I40IW_DEBUG_WQE, "ARP_CACHE_ENTRY WQE",
			wqe, I40IW_CQP_WQE_SIZE * 8);

	if (post_sq)
		i40iw_sc_cqp_post_sq(cqp);
	return 0;
}

/**
 * i40iw_sc_del_arp_cache_entry - dele arp cache entry
 * @cqp: struct for cqp hw
 * @scratch: u64 saved to be used during cqp completion
 * @arp_index: arp index to delete arp entry
 * @post_sq: flag for cqp db to ring
 */
static enum i40iw_status_code i40iw_sc_del_arp_cache_entry(
					struct i40iw_sc_cqp *cqp,
					u64 scratch,
					u16 arp_index,
					bool post_sq)
{
	u64 *wqe;
	u64 header;

	wqe = i40iw_sc_cqp_get_next_send_wqe(cqp, scratch);
	if (!wqe)
		return I40IW_ERR_RING_FULL;

	header = arp_index |
		 LS_64(I40IW_CQP_OP_MANAGE_ARP, I40IW_CQPSQ_OPCODE) |
		 LS_64(cqp->polarity, I40IW_CQPSQ_WQEVALID);
	i40iw_insert_wqe_hdr(wqe, header);

	i40iw_debug_buf(cqp->dev, I40IW_DEBUG_WQE, "ARP_CACHE_DEL_ENTRY WQE",
			wqe, I40IW_CQP_WQE_SIZE * 8);

	if (post_sq)
		i40iw_sc_cqp_post_sq(cqp);
	return 0;
}

/**
 * i40iw_sc_query_arp_cache_entry - cqp wqe to query arp and arp index
 * @cqp: struct for cqp hw
 * @scratch: u64 saved to be used during cqp completion
 * @arp_index: arp index to delete arp entry
 * @post_sq: flag for cqp db to ring
 */
static enum i40iw_status_code i40iw_sc_query_arp_cache_entry(
				struct i40iw_sc_cqp *cqp,
				u64 scratch,
				u16 arp_index,
				bool post_sq)
{
	u64 *wqe;
	u64 header;

	wqe = i40iw_sc_cqp_get_next_send_wqe(cqp, scratch);
	if (!wqe)
		return I40IW_ERR_RING_FULL;

	header = arp_index |
		 LS_64(I40IW_CQP_OP_MANAGE_ARP, I40IW_CQPSQ_OPCODE) |
		 LS_64(1, I40IW_CQPSQ_MAT_QUERY) |
		 LS_64(cqp->polarity, I40IW_CQPSQ_WQEVALID);

	i40iw_insert_wqe_hdr(wqe, header);

	i40iw_debug_buf(cqp->dev, I40IW_DEBUG_WQE, "QUERY_ARP_CACHE_ENTRY WQE",
			wqe, I40IW_CQP_WQE_SIZE * 8);

	if (post_sq)
		i40iw_sc_cqp_post_sq(cqp);
	return 0;
}

/**
 * i40iw_sc_manage_apbvt_entry - for adding and deleting apbvt entries
 * @cqp: struct for cqp hw
 * @info: info for apbvt entry to add or delete
 * @scratch: u64 saved to be used during cqp completion
 * @post_sq: flag for cqp db to ring
 */
static enum i40iw_status_code i40iw_sc_manage_apbvt_entry(
				struct i40iw_sc_cqp *cqp,
				struct i40iw_apbvt_info *info,
				u64 scratch,
				bool post_sq)
{
	u64 *wqe;
	u64 header;

	wqe = i40iw_sc_cqp_get_next_send_wqe(cqp, scratch);
	if (!wqe)
		return I40IW_ERR_RING_FULL;

	set_64bit_val(wqe, 16, info->port);

	header = LS_64(I40IW_CQP_OP_MANAGE_APBVT, I40IW_CQPSQ_OPCODE) |
		 LS_64(info->add, I40IW_CQPSQ_MAPT_ADDPORT) |
		 LS_64(cqp->polarity, I40IW_CQPSQ_WQEVALID);

	i40iw_insert_wqe_hdr(wqe, header);

	i40iw_debug_buf(cqp->dev, I40IW_DEBUG_WQE, "MANAGE_APBVT WQE",
			wqe, I40IW_CQP_WQE_SIZE * 8);

	if (post_sq)
		i40iw_sc_cqp_post_sq(cqp);
	return 0;
}

/**
 * i40iw_sc_manage_qhash_table_entry - manage quad hash entries
 * @cqp: struct for cqp hw
 * @info: info for quad hash to manage
 * @scratch: u64 saved to be used during cqp completion
 * @post_sq: flag for cqp db to ring
 *
 * This is called before connection establishment is started. For passive connections, when
 * listener is created, it will call with entry type of  I40IW_QHASH_TYPE_TCP_SYN with local
 * ip address and tcp port. When SYN is received (passive connections) or
 * sent (active connections), this routine is called with entry type of
 * I40IW_QHASH_TYPE_TCP_ESTABLISHED and quad is passed in info.
 *
 * When iwarp connection is done and its state moves to RTS, the quad hash entry in
 * the hardware will point to iwarp's qp number and requires no calls from the driver.
 */
static enum i40iw_status_code i40iw_sc_manage_qhash_table_entry(
					struct i40iw_sc_cqp *cqp,
					struct i40iw_qhash_table_info *info,
					u64 scratch,
					bool post_sq)
{
	u64 *wqe;
	u64 qw1 = 0;
	u64 qw2 = 0;
	u64 temp;
	struct i40iw_sc_vsi *vsi = info->vsi;

	wqe = i40iw_sc_cqp_get_next_send_wqe(cqp, scratch);
	if (!wqe)
		return I40IW_ERR_RING_FULL;

	temp = info->mac_addr[5] |
		LS_64_1(info->mac_addr[4], 8) |
		LS_64_1(info->mac_addr[3], 16) |
		LS_64_1(info->mac_addr[2], 24) |
		LS_64_1(info->mac_addr[1], 32) |
		LS_64_1(info->mac_addr[0], 40);

	set_64bit_val(wqe, 0, temp);

	qw1 = LS_64(info->qp_num, I40IW_CQPSQ_QHASH_QPN) |
	      LS_64(info->dest_port, I40IW_CQPSQ_QHASH_DEST_PORT);
	if (info->ipv4_valid) {
		set_64bit_val(wqe,
			      48,
			      LS_64(info->dest_ip[0], I40IW_CQPSQ_QHASH_ADDR3));
	} else {
		set_64bit_val(wqe,
			      56,
			      LS_64(info->dest_ip[0], I40IW_CQPSQ_QHASH_ADDR0) |
			      LS_64(info->dest_ip[1], I40IW_CQPSQ_QHASH_ADDR1));

		set_64bit_val(wqe,
			      48,
			      LS_64(info->dest_ip[2], I40IW_CQPSQ_QHASH_ADDR2) |
			      LS_64(info->dest_ip[3], I40IW_CQPSQ_QHASH_ADDR3));
	}
	qw2 = LS_64(vsi->qos[info->user_pri].qs_handle, I40IW_CQPSQ_QHASH_QS_HANDLE);
	if (info->vlan_valid)
		qw2 |= LS_64(info->vlan_id, I40IW_CQPSQ_QHASH_VLANID);
	set_64bit_val(wqe, 16, qw2);
	if (info->entry_type == I40IW_QHASH_TYPE_TCP_ESTABLISHED) {
		qw1 |= LS_64(info->src_port, I40IW_CQPSQ_QHASH_SRC_PORT);
		if (!info->ipv4_valid) {
			set_64bit_val(wqe,
				      40,
				      LS_64(info->src_ip[0], I40IW_CQPSQ_QHASH_ADDR0) |
				      LS_64(info->src_ip[1], I40IW_CQPSQ_QHASH_ADDR1));
			set_64bit_val(wqe,
				      32,
				      LS_64(info->src_ip[2], I40IW_CQPSQ_QHASH_ADDR2) |
				      LS_64(info->src_ip[3], I40IW_CQPSQ_QHASH_ADDR3));
		} else {
			set_64bit_val(wqe,
				      32,
				      LS_64(info->src_ip[0], I40IW_CQPSQ_QHASH_ADDR3));
		}
	}

	set_64bit_val(wqe, 8, qw1);
	temp = LS_64(cqp->polarity, I40IW_CQPSQ_QHASH_WQEVALID) |
	       LS_64(I40IW_CQP_OP_MANAGE_QUAD_HASH_TABLE_ENTRY, I40IW_CQPSQ_QHASH_OPCODE) |
	       LS_64(info->manage, I40IW_CQPSQ_QHASH_MANAGE) |
	       LS_64(info->ipv4_valid, I40IW_CQPSQ_QHASH_IPV4VALID) |
	       LS_64(info->vlan_valid, I40IW_CQPSQ_QHASH_VLANVALID) |
	       LS_64(info->entry_type, I40IW_CQPSQ_QHASH_ENTRYTYPE);

	i40iw_insert_wqe_hdr(wqe, temp);

	i40iw_debug_buf(cqp->dev, I40IW_DEBUG_WQE, "MANAGE_QHASH WQE",
			wqe, I40IW_CQP_WQE_SIZE * 8);

	if (post_sq)
		i40iw_sc_cqp_post_sq(cqp);
	return 0;
}

/**
 * i40iw_sc_alloc_local_mac_ipaddr_entry - cqp wqe for loc mac entry
 * @cqp: struct for cqp hw
 * @scratch: u64 saved to be used during cqp completion
 * @post_sq: flag for cqp db to ring
 */
static enum i40iw_status_code i40iw_sc_alloc_local_mac_ipaddr_entry(
					struct i40iw_sc_cqp *cqp,
					u64 scratch,
					bool post_sq)
{
	u64 *wqe;
	u64 header;

	wqe = i40iw_sc_cqp_get_next_send_wqe(cqp, scratch);
	if (!wqe)
		return I40IW_ERR_RING_FULL;
	header = LS_64(I40IW_CQP_OP_ALLOCATE_LOC_MAC_IP_TABLE_ENTRY, I40IW_CQPSQ_OPCODE) |
		 LS_64(cqp->polarity, I40IW_CQPSQ_WQEVALID);

	i40iw_insert_wqe_hdr(wqe, header);
	i40iw_debug_buf(cqp->dev, I40IW_DEBUG_WQE, "ALLOCATE_LOCAL_MAC_IPADDR WQE",
			wqe, I40IW_CQP_WQE_SIZE * 8);
	if (post_sq)
		i40iw_sc_cqp_post_sq(cqp);
	return 0;
}

/**
 * i40iw_sc_add_local_mac_ipaddr_entry - add mac enry
 * @cqp: struct for cqp hw
 * @info:mac addr info
 * @scratch: u64 saved to be used during cqp completion
 * @post_sq: flag for cqp db to ring
 */
static enum i40iw_status_code i40iw_sc_add_local_mac_ipaddr_entry(
				struct i40iw_sc_cqp *cqp,
				struct i40iw_local_mac_ipaddr_entry_info *info,
				u64 scratch,
				bool post_sq)
{
	u64 *wqe;
	u64 temp, header;

	wqe = i40iw_sc_cqp_get_next_send_wqe(cqp, scratch);
	if (!wqe)
		return I40IW_ERR_RING_FULL;
	temp = info->mac_addr[5] |
		LS_64_1(info->mac_addr[4], 8) |
		LS_64_1(info->mac_addr[3], 16) |
		LS_64_1(info->mac_addr[2], 24) |
		LS_64_1(info->mac_addr[1], 32) |
		LS_64_1(info->mac_addr[0], 40);

	set_64bit_val(wqe, 32, temp);

	header = LS_64(info->entry_idx, I40IW_CQPSQ_MLIPA_IPTABLEIDX) |
		 LS_64(I40IW_CQP_OP_MANAGE_LOC_MAC_IP_TABLE, I40IW_CQPSQ_OPCODE) |
		 LS_64(cqp->polarity, I40IW_CQPSQ_WQEVALID);

	i40iw_insert_wqe_hdr(wqe, header);

	i40iw_debug_buf(cqp->dev, I40IW_DEBUG_WQE, "ADD_LOCAL_MAC_IPADDR WQE",
			wqe, I40IW_CQP_WQE_SIZE * 8);

	if (post_sq)
		i40iw_sc_cqp_post_sq(cqp);
	return 0;
}

/**
 * i40iw_sc_del_local_mac_ipaddr_entry - cqp wqe to dele local mac
 * @cqp: struct for cqp hw
 * @scratch: u64 saved to be used during cqp completion
 * @entry_idx: index of mac entry
 * @ ignore_ref_count: to force mac adde delete
 * @post_sq: flag for cqp db to ring
 */
static enum i40iw_status_code i40iw_sc_del_local_mac_ipaddr_entry(
				struct i40iw_sc_cqp *cqp,
				u64 scratch,
				u8 entry_idx,
				u8 ignore_ref_count,
				bool post_sq)
{
	u64 *wqe;
	u64 header;

	wqe = i40iw_sc_cqp_get_next_send_wqe(cqp, scratch);
	if (!wqe)
		return I40IW_ERR_RING_FULL;
	header = LS_64(entry_idx, I40IW_CQPSQ_MLIPA_IPTABLEIDX) |
		 LS_64(I40IW_CQP_OP_MANAGE_LOC_MAC_IP_TABLE, I40IW_CQPSQ_OPCODE) |
		 LS_64(1, I40IW_CQPSQ_MLIPA_FREEENTRY) |
		 LS_64(cqp->polarity, I40IW_CQPSQ_WQEVALID) |
		 LS_64(ignore_ref_count, I40IW_CQPSQ_MLIPA_IGNORE_REF_CNT);

	i40iw_insert_wqe_hdr(wqe, header);

	i40iw_debug_buf(cqp->dev, I40IW_DEBUG_WQE, "DEL_LOCAL_MAC_IPADDR WQE",
			wqe, I40IW_CQP_WQE_SIZE * 8);

	if (post_sq)
		i40iw_sc_cqp_post_sq(cqp);
	return 0;
}

/**
 * i40iw_sc_cqp_nop - send a nop wqe
 * @cqp: struct for cqp hw
 * @scratch: u64 saved to be used during cqp completion
 * @post_sq: flag for cqp db to ring
 */
static enum i40iw_status_code i40iw_sc_cqp_nop(struct i40iw_sc_cqp *cqp,
					       u64 scratch,
					       bool post_sq)
{
	u64 *wqe;
	u64 header;

	wqe = i40iw_sc_cqp_get_next_send_wqe(cqp, scratch);
	if (!wqe)
		return I40IW_ERR_RING_FULL;
	header = LS_64(I40IW_CQP_OP_NOP, I40IW_CQPSQ_OPCODE) |
		 LS_64(cqp->polarity, I40IW_CQPSQ_WQEVALID);
	i40iw_insert_wqe_hdr(wqe, header);
	i40iw_debug_buf(cqp->dev, I40IW_DEBUG_WQE, "NOP WQE",
			wqe, I40IW_CQP_WQE_SIZE * 8);

	if (post_sq)
		i40iw_sc_cqp_post_sq(cqp);
	return 0;
}

/**
 * i40iw_sc_ceq_init - initialize ceq
 * @ceq: ceq sc structure
 * @info: ceq initialization info
 */
static enum i40iw_status_code i40iw_sc_ceq_init(struct i40iw_sc_ceq *ceq,
						struct i40iw_ceq_init_info *info)
{
	u32 pble_obj_cnt;

	if ((info->elem_cnt < I40IW_MIN_CEQ_ENTRIES) ||
	    (info->elem_cnt > I40IW_MAX_CEQ_ENTRIES))
		return I40IW_ERR_INVALID_SIZE;

	if (info->ceq_id >= I40IW_MAX_CEQID)
		return I40IW_ERR_INVALID_CEQ_ID;

	pble_obj_cnt = info->dev->hmc_info->hmc_obj[I40IW_HMC_IW_PBLE].cnt;

	if (info->virtual_map && (info->first_pm_pbl_idx >= pble_obj_cnt))
		return I40IW_ERR_INVALID_PBLE_INDEX;

	ceq->size = sizeof(*ceq);
	ceq->ceqe_base = (struct i40iw_ceqe *)info->ceqe_base;
	ceq->ceq_id = info->ceq_id;
	ceq->dev = info->dev;
	ceq->elem_cnt = info->elem_cnt;
	ceq->ceq_elem_pa = info->ceqe_pa;
	ceq->virtual_map = info->virtual_map;

	ceq->pbl_chunk_size = (ceq->virtual_map ? info->pbl_chunk_size : 0);
	ceq->first_pm_pbl_idx = (ceq->virtual_map ? info->first_pm_pbl_idx : 0);
	ceq->pbl_list = (ceq->virtual_map ? info->pbl_list : NULL);

	ceq->tph_en = info->tph_en;
	ceq->tph_val = info->tph_val;
	ceq->polarity = 1;
	I40IW_RING_INIT(ceq->ceq_ring, ceq->elem_cnt);
	ceq->dev->ceq[info->ceq_id] = ceq;

	return 0;
}

/**
 * i40iw_sc_ceq_create - create ceq wqe
 * @ceq: ceq sc structure
 * @scratch: u64 saved to be used during cqp completion
 * @post_sq: flag for cqp db to ring
 */
static enum i40iw_status_code i40iw_sc_ceq_create(struct i40iw_sc_ceq *ceq,
						  u64 scratch,
						  bool post_sq)
{
	struct i40iw_sc_cqp *cqp;
	u64 *wqe;
	u64 header;

	cqp = ceq->dev->cqp;
	wqe = i40iw_sc_cqp_get_next_send_wqe(cqp, scratch);
	if (!wqe)
		return I40IW_ERR_RING_FULL;
	set_64bit_val(wqe, 16, ceq->elem_cnt);
	set_64bit_val(wqe, 32, (ceq->virtual_map ? 0 : ceq->ceq_elem_pa));
	set_64bit_val(wqe, 48, (ceq->virtual_map ? ceq->first_pm_pbl_idx : 0));
	set_64bit_val(wqe, 56, LS_64(ceq->tph_val, I40IW_CQPSQ_TPHVAL));

	header = ceq->ceq_id |
		 LS_64(I40IW_CQP_OP_CREATE_CEQ, I40IW_CQPSQ_OPCODE) |
		 LS_64(ceq->pbl_chunk_size, I40IW_CQPSQ_CEQ_LPBLSIZE) |
		 LS_64(ceq->virtual_map, I40IW_CQPSQ_CEQ_VMAP) |
		 LS_64(ceq->tph_en, I40IW_CQPSQ_TPHEN) |
		 LS_64(cqp->polarity, I40IW_CQPSQ_WQEVALID);

	i40iw_insert_wqe_hdr(wqe, header);

	i40iw_debug_buf(cqp->dev, I40IW_DEBUG_WQE, "CEQ_CREATE WQE",
			wqe, I40IW_CQP_WQE_SIZE * 8);

	if (post_sq)
		i40iw_sc_cqp_post_sq(cqp);
	return 0;
}

/**
 * i40iw_sc_cceq_create_done - poll for control ceq wqe to complete
 * @ceq: ceq sc structure
 */
static enum i40iw_status_code i40iw_sc_cceq_create_done(struct i40iw_sc_ceq *ceq)
{
	struct i40iw_sc_cqp *cqp;

	cqp = ceq->dev->cqp;
	return i40iw_sc_poll_for_cqp_op_done(cqp, I40IW_CQP_OP_CREATE_CEQ, NULL);
}

/**
 * i40iw_sc_cceq_destroy_done - poll for destroy cceq to complete
 * @ceq: ceq sc structure
 */
static enum i40iw_status_code i40iw_sc_cceq_destroy_done(struct i40iw_sc_ceq *ceq)
{
	struct i40iw_sc_cqp *cqp;

	cqp = ceq->dev->cqp;
	cqp->process_cqp_sds = i40iw_update_sds_noccq;
	return i40iw_sc_poll_for_cqp_op_done(cqp, I40IW_CQP_OP_DESTROY_CEQ, NULL);
}

/**
 * i40iw_sc_cceq_create - create cceq
 * @ceq: ceq sc structure
 * @scratch: u64 saved to be used during cqp completion
 */
static enum i40iw_status_code i40iw_sc_cceq_create(struct i40iw_sc_ceq *ceq, u64 scratch)
{
	enum i40iw_status_code ret_code;

	ret_code = i40iw_sc_ceq_create(ceq, scratch, true);
	if (!ret_code)
		ret_code = i40iw_sc_cceq_create_done(ceq);
	return ret_code;
}

/**
 * i40iw_sc_ceq_destroy - destroy ceq
 * @ceq: ceq sc structure
 * @scratch: u64 saved to be used during cqp completion
 * @post_sq: flag for cqp db to ring
 */
static enum i40iw_status_code i40iw_sc_ceq_destroy(struct i40iw_sc_ceq *ceq,
						   u64 scratch,
						   bool post_sq)
{
	struct i40iw_sc_cqp *cqp;
	u64 *wqe;
	u64 header;

	cqp = ceq->dev->cqp;
	wqe = i40iw_sc_cqp_get_next_send_wqe(cqp, scratch);
	if (!wqe)
		return I40IW_ERR_RING_FULL;
	set_64bit_val(wqe, 16, ceq->elem_cnt);
	set_64bit_val(wqe, 48, ceq->first_pm_pbl_idx);
	header = ceq->ceq_id |
		 LS_64(I40IW_CQP_OP_DESTROY_CEQ, I40IW_CQPSQ_OPCODE) |
		 LS_64(ceq->pbl_chunk_size, I40IW_CQPSQ_CEQ_LPBLSIZE) |
		 LS_64(ceq->virtual_map, I40IW_CQPSQ_CEQ_VMAP) |
		 LS_64(ceq->tph_en, I40IW_CQPSQ_TPHEN) |
		 LS_64(cqp->polarity, I40IW_CQPSQ_WQEVALID);
	i40iw_insert_wqe_hdr(wqe, header);
	i40iw_debug_buf(cqp->dev, I40IW_DEBUG_WQE, "CEQ_DESTROY WQE",
			wqe, I40IW_CQP_WQE_SIZE * 8);

	if (post_sq)
		i40iw_sc_cqp_post_sq(cqp);
	return 0;
}

/**
 * i40iw_sc_process_ceq - process ceq
 * @dev: sc device struct
 * @ceq: ceq sc structure
 */
static void *i40iw_sc_process_ceq(struct i40iw_sc_dev *dev, struct i40iw_sc_ceq *ceq)
{
	u64 temp;
	u64 *ceqe;
	struct i40iw_sc_cq *cq = NULL;
	u8 polarity;

	ceqe = (u64 *)I40IW_GET_CURRENT_CEQ_ELEMENT(ceq);
	get_64bit_val(ceqe, 0, &temp);
	polarity = (u8)RS_64(temp, I40IW_CEQE_VALID);
	if (polarity != ceq->polarity)
		return cq;

	cq = (struct i40iw_sc_cq *)(unsigned long)LS_64_1(temp, 1);

	I40IW_RING_MOVE_TAIL(ceq->ceq_ring);
	if (I40IW_RING_GETCURRENT_TAIL(ceq->ceq_ring) == 0)
		ceq->polarity ^= 1;

	if (dev->is_pf)
		i40iw_wr32(dev->hw, I40E_PFPE_CQACK, cq->cq_uk.cq_id);
	else
		i40iw_wr32(dev->hw, I40E_VFPE_CQACK1, cq->cq_uk.cq_id);

	return cq;
}

/**
 * i40iw_sc_aeq_init - initialize aeq
 * @aeq: aeq structure ptr
 * @info: aeq initialization info
 */
static enum i40iw_status_code i40iw_sc_aeq_init(struct i40iw_sc_aeq *aeq,
						struct i40iw_aeq_init_info *info)
{
	u32 pble_obj_cnt;

	if ((info->elem_cnt < I40IW_MIN_AEQ_ENTRIES) ||
	    (info->elem_cnt > I40IW_MAX_AEQ_ENTRIES))
		return I40IW_ERR_INVALID_SIZE;
	pble_obj_cnt = info->dev->hmc_info->hmc_obj[I40IW_HMC_IW_PBLE].cnt;

	if (info->virtual_map && (info->first_pm_pbl_idx >= pble_obj_cnt))
		return I40IW_ERR_INVALID_PBLE_INDEX;

	aeq->size = sizeof(*aeq);
	aeq->polarity = 1;
	aeq->aeqe_base = (struct i40iw_sc_aeqe *)info->aeqe_base;
	aeq->dev = info->dev;
	aeq->elem_cnt = info->elem_cnt;

	aeq->aeq_elem_pa = info->aeq_elem_pa;
	I40IW_RING_INIT(aeq->aeq_ring, aeq->elem_cnt);
	info->dev->aeq = aeq;

	aeq->virtual_map = info->virtual_map;
	aeq->pbl_list = (aeq->virtual_map ? info->pbl_list : NULL);
	aeq->pbl_chunk_size = (aeq->virtual_map ? info->pbl_chunk_size : 0);
	aeq->first_pm_pbl_idx = (aeq->virtual_map ? info->first_pm_pbl_idx : 0);
	info->dev->aeq = aeq;
	return 0;
}

/**
 * i40iw_sc_aeq_create - create aeq
 * @aeq: aeq structure ptr
 * @scratch: u64 saved to be used during cqp completion
 * @post_sq: flag for cqp db to ring
 */
static enum i40iw_status_code i40iw_sc_aeq_create(struct i40iw_sc_aeq *aeq,
						  u64 scratch,
						  bool post_sq)
{
	u64 *wqe;
	struct i40iw_sc_cqp *cqp;
	u64 header;

	cqp = aeq->dev->cqp;
	wqe = i40iw_sc_cqp_get_next_send_wqe(cqp, scratch);
	if (!wqe)
		return I40IW_ERR_RING_FULL;
	set_64bit_val(wqe, 16, aeq->elem_cnt);
	set_64bit_val(wqe, 32,
		      (aeq->virtual_map ? 0 : aeq->aeq_elem_pa));
	set_64bit_val(wqe, 48,
		      (aeq->virtual_map ? aeq->first_pm_pbl_idx : 0));

	header = LS_64(I40IW_CQP_OP_CREATE_AEQ, I40IW_CQPSQ_OPCODE) |
		 LS_64(aeq->pbl_chunk_size, I40IW_CQPSQ_AEQ_LPBLSIZE) |
		 LS_64(aeq->virtual_map, I40IW_CQPSQ_AEQ_VMAP) |
		 LS_64(cqp->polarity, I40IW_CQPSQ_WQEVALID);

	i40iw_insert_wqe_hdr(wqe, header);
	i40iw_debug_buf(cqp->dev, I40IW_DEBUG_WQE, "AEQ_CREATE WQE",
			wqe, I40IW_CQP_WQE_SIZE * 8);
	if (post_sq)
		i40iw_sc_cqp_post_sq(cqp);
	return 0;
}

/**
 * i40iw_sc_aeq_destroy - destroy aeq during close
 * @aeq: aeq structure ptr
 * @scratch: u64 saved to be used during cqp completion
 * @post_sq: flag for cqp db to ring
 */
static enum i40iw_status_code i40iw_sc_aeq_destroy(struct i40iw_sc_aeq *aeq,
						   u64 scratch,
						   bool post_sq)
{
	u64 *wqe;
	struct i40iw_sc_cqp *cqp;
	u64 header;

	cqp = aeq->dev->cqp;
	wqe = i40iw_sc_cqp_get_next_send_wqe(cqp, scratch);
	if (!wqe)
		return I40IW_ERR_RING_FULL;
	set_64bit_val(wqe, 16, aeq->elem_cnt);
	set_64bit_val(wqe, 48, aeq->first_pm_pbl_idx);
	header = LS_64(I40IW_CQP_OP_DESTROY_AEQ, I40IW_CQPSQ_OPCODE) |
		 LS_64(aeq->pbl_chunk_size, I40IW_CQPSQ_AEQ_LPBLSIZE) |
		 LS_64(aeq->virtual_map, I40IW_CQPSQ_AEQ_VMAP) |
		 LS_64(cqp->polarity, I40IW_CQPSQ_WQEVALID);
	i40iw_insert_wqe_hdr(wqe, header);

	i40iw_debug_buf(cqp->dev, I40IW_DEBUG_WQE, "AEQ_DESTROY WQE",
			wqe, I40IW_CQP_WQE_SIZE * 8);
	if (post_sq)
		i40iw_sc_cqp_post_sq(cqp);
	return 0;
}

/**
 * i40iw_sc_get_next_aeqe - get next aeq entry
 * @aeq: aeq structure ptr
 * @info: aeqe info to be returned
 */
static enum i40iw_status_code i40iw_sc_get_next_aeqe(struct i40iw_sc_aeq *aeq,
						     struct i40iw_aeqe_info *info)
{
	u64 temp, compl_ctx;
	u64 *aeqe;
	u16 wqe_idx;
	u8 ae_src;
	u8 polarity;

	aeqe = (u64 *)I40IW_GET_CURRENT_AEQ_ELEMENT(aeq);
	get_64bit_val(aeqe, 0, &compl_ctx);
	get_64bit_val(aeqe, 8, &temp);
	polarity = (u8)RS_64(temp, I40IW_AEQE_VALID);

	if (aeq->polarity != polarity)
		return I40IW_ERR_QUEUE_EMPTY;

	i40iw_debug_buf(aeq->dev, I40IW_DEBUG_WQE, "AEQ_ENTRY", aeqe, 16);

	ae_src = (u8)RS_64(temp, I40IW_AEQE_AESRC);
	wqe_idx = (u16)RS_64(temp, I40IW_AEQE_WQDESCIDX);
	info->qp_cq_id = (u32)RS_64(temp, I40IW_AEQE_QPCQID);
	info->ae_id = (u16)RS_64(temp, I40IW_AEQE_AECODE);
	info->tcp_state = (u8)RS_64(temp, I40IW_AEQE_TCPSTATE);
	info->iwarp_state = (u8)RS_64(temp, I40IW_AEQE_IWSTATE);
	info->q2_data_written = (u8)RS_64(temp, I40IW_AEQE_Q2DATA);
	info->aeqe_overflow = (bool)RS_64(temp, I40IW_AEQE_OVERFLOW);

	switch (info->ae_id) {
	case I40IW_AE_PRIV_OPERATION_DENIED:
	case I40IW_AE_UDA_XMIT_DGRAM_TOO_LONG:
	case I40IW_AE_UDA_XMIT_DGRAM_TOO_SHORT:
	case I40IW_AE_BAD_CLOSE:
	case I40IW_AE_RDMAP_ROE_BAD_LLP_CLOSE:
	case I40IW_AE_RDMA_READ_WHILE_ORD_ZERO:
	case I40IW_AE_STAG_ZERO_INVALID:
	case I40IW_AE_IB_RREQ_AND_Q1_FULL:
	case I40IW_AE_WQE_UNEXPECTED_OPCODE:
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
	case I40IW_AE_LLP_CLOSE_COMPLETE:
	case I40IW_AE_LLP_CONNECTION_RESET:
	case I40IW_AE_LLP_FIN_RECEIVED:
	case I40IW_AE_LLP_RECEIVED_MPA_CRC_ERROR:
	case I40IW_AE_LLP_SEGMENT_TOO_SMALL:
	case I40IW_AE_LLP_SYN_RECEIVED:
	case I40IW_AE_LLP_TERMINATE_RECEIVED:
	case I40IW_AE_LLP_TOO_MANY_RETRIES:
	case I40IW_AE_LLP_DOUBT_REACHABILITY:
	case I40IW_AE_RESET_SENT:
	case I40IW_AE_TERMINATE_SENT:
	case I40IW_AE_RESET_NOT_SENT:
	case I40IW_AE_LCE_QP_CATASTROPHIC:
	case I40IW_AE_QP_SUSPEND_COMPLETE:
		info->qp = true;
		info->compl_ctx = compl_ctx;
		ae_src = I40IW_AE_SOURCE_RSVD;
		break;
	case I40IW_AE_LCE_CQ_CATASTROPHIC:
		info->cq = true;
		info->compl_ctx = LS_64_1(compl_ctx, 1);
		ae_src = I40IW_AE_SOURCE_RSVD;
		break;
	}

	switch (ae_src) {
	case I40IW_AE_SOURCE_RQ:
	case I40IW_AE_SOURCE_RQ_0011:
		info->qp = true;
		info->wqe_idx = wqe_idx;
		info->compl_ctx = compl_ctx;
		break;
	case I40IW_AE_SOURCE_CQ:
	case I40IW_AE_SOURCE_CQ_0110:
	case I40IW_AE_SOURCE_CQ_1010:
	case I40IW_AE_SOURCE_CQ_1110:
		info->cq = true;
		info->compl_ctx = LS_64_1(compl_ctx, 1);
		break;
	case I40IW_AE_SOURCE_SQ:
	case I40IW_AE_SOURCE_SQ_0111:
		info->qp = true;
		info->sq = true;
		info->wqe_idx = wqe_idx;
		info->compl_ctx = compl_ctx;
		break;
	case I40IW_AE_SOURCE_IN_RR_WR:
	case I40IW_AE_SOURCE_IN_RR_WR_1011:
		info->qp = true;
		info->compl_ctx = compl_ctx;
		info->in_rdrsp_wr = true;
		break;
	case I40IW_AE_SOURCE_OUT_RR:
	case I40IW_AE_SOURCE_OUT_RR_1111:
		info->qp = true;
		info->compl_ctx = compl_ctx;
		info->out_rdrsp = true;
		break;
	case I40IW_AE_SOURCE_RSVD:
		/* fallthrough */
	default:
		break;
	}
	I40IW_RING_MOVE_TAIL(aeq->aeq_ring);
	if (I40IW_RING_GETCURRENT_TAIL(aeq->aeq_ring) == 0)
		aeq->polarity ^= 1;
	return 0;
}

/**
 * i40iw_sc_repost_aeq_entries - repost completed aeq entries
 * @dev: sc device struct
 * @count: allocate count
 */
static enum i40iw_status_code i40iw_sc_repost_aeq_entries(struct i40iw_sc_dev *dev,
							  u32 count)
{
	if (count > I40IW_MAX_AEQ_ALLOCATE_COUNT)
		return I40IW_ERR_INVALID_SIZE;

	if (dev->is_pf)
		i40iw_wr32(dev->hw, I40E_PFPE_AEQALLOC, count);
	else
		i40iw_wr32(dev->hw, I40E_VFPE_AEQALLOC1, count);

	return 0;
}

/**
 * i40iw_sc_aeq_create_done - create aeq
 * @aeq: aeq structure ptr
 */
static enum i40iw_status_code i40iw_sc_aeq_create_done(struct i40iw_sc_aeq *aeq)
{
	struct i40iw_sc_cqp *cqp;

	cqp = aeq->dev->cqp;
	return i40iw_sc_poll_for_cqp_op_done(cqp, I40IW_CQP_OP_CREATE_AEQ, NULL);
}

/**
 * i40iw_sc_aeq_destroy_done - destroy of aeq during close
 * @aeq: aeq structure ptr
 */
static enum i40iw_status_code i40iw_sc_aeq_destroy_done(struct i40iw_sc_aeq *aeq)
{
	struct i40iw_sc_cqp *cqp;

	cqp = aeq->dev->cqp;
	return  i40iw_sc_poll_for_cqp_op_done(cqp, I40IW_CQP_OP_DESTROY_AEQ, NULL);
}

/**
 * i40iw_sc_ccq_init - initialize control cq
 * @cq: sc's cq ctruct
 * @info: info for control cq initialization
 */
static enum i40iw_status_code i40iw_sc_ccq_init(struct i40iw_sc_cq *cq,
						struct i40iw_ccq_init_info *info)
{
	u32 pble_obj_cnt;

	if (info->num_elem < I40IW_MIN_CQ_SIZE || info->num_elem > I40IW_MAX_CQ_SIZE)
		return I40IW_ERR_INVALID_SIZE;

	if (info->ceq_id > I40IW_MAX_CEQID)
		return I40IW_ERR_INVALID_CEQ_ID;

	pble_obj_cnt = info->dev->hmc_info->hmc_obj[I40IW_HMC_IW_PBLE].cnt;

	if (info->virtual_map && (info->first_pm_pbl_idx >= pble_obj_cnt))
		return I40IW_ERR_INVALID_PBLE_INDEX;

	cq->cq_pa = info->cq_pa;
	cq->cq_uk.cq_base = info->cq_base;
	cq->shadow_area_pa = info->shadow_area_pa;
	cq->cq_uk.shadow_area = info->shadow_area;
	cq->shadow_read_threshold = info->shadow_read_threshold;
	cq->dev = info->dev;
	cq->ceq_id = info->ceq_id;
	cq->cq_uk.cq_size = info->num_elem;
	cq->cq_type = I40IW_CQ_TYPE_CQP;
	cq->ceqe_mask = info->ceqe_mask;
	I40IW_RING_INIT(cq->cq_uk.cq_ring, info->num_elem);

	cq->cq_uk.cq_id = 0;    /* control cq is id 0 always */
	cq->ceq_id_valid = info->ceq_id_valid;
	cq->tph_en = info->tph_en;
	cq->tph_val = info->tph_val;
	cq->cq_uk.avoid_mem_cflct = info->avoid_mem_cflct;

	cq->pbl_list = info->pbl_list;
	cq->virtual_map = info->virtual_map;
	cq->pbl_chunk_size = info->pbl_chunk_size;
	cq->first_pm_pbl_idx = info->first_pm_pbl_idx;
	cq->cq_uk.polarity = true;

	/* following are only for iw cqs so initialize them to zero */
	cq->cq_uk.cqe_alloc_reg = NULL;
	info->dev->ccq = cq;
	return 0;
}

/**
 * i40iw_sc_ccq_create_done - poll cqp for ccq create
 * @ccq: ccq sc struct
 */
static enum i40iw_status_code i40iw_sc_ccq_create_done(struct i40iw_sc_cq *ccq)
{
	struct i40iw_sc_cqp *cqp;

	cqp = ccq->dev->cqp;
	return	i40iw_sc_poll_for_cqp_op_done(cqp, I40IW_CQP_OP_CREATE_CQ, NULL);
}

/**
 * i40iw_sc_ccq_create - create control cq
 * @ccq: ccq sc struct
 * @scratch: u64 saved to be used during cqp completion
 * @check_overflow: overlow flag for ccq
 * @post_sq: flag for cqp db to ring
 */
static enum i40iw_status_code i40iw_sc_ccq_create(struct i40iw_sc_cq *ccq,
						  u64 scratch,
						  bool check_overflow,
						  bool post_sq)
{
	u64 *wqe;
	struct i40iw_sc_cqp *cqp;
	u64 header;
	enum i40iw_status_code ret_code;

	cqp = ccq->dev->cqp;
	wqe = i40iw_sc_cqp_get_next_send_wqe(cqp, scratch);
	if (!wqe)
		return I40IW_ERR_RING_FULL;
	set_64bit_val(wqe, 0, ccq->cq_uk.cq_size);
	set_64bit_val(wqe, 8, RS_64_1(ccq, 1));
	set_64bit_val(wqe, 16,
		      LS_64(ccq->shadow_read_threshold, I40IW_CQPSQ_CQ_SHADOW_READ_THRESHOLD));
	set_64bit_val(wqe, 32, (ccq->virtual_map ? 0 : ccq->cq_pa));
	set_64bit_val(wqe, 40, ccq->shadow_area_pa);
	set_64bit_val(wqe, 48,
		      (ccq->virtual_map ? ccq->first_pm_pbl_idx : 0));
	set_64bit_val(wqe, 56,
		      LS_64(ccq->tph_val, I40IW_CQPSQ_TPHVAL));

	header = ccq->cq_uk.cq_id |
		 LS_64((ccq->ceq_id_valid ? ccq->ceq_id : 0), I40IW_CQPSQ_CQ_CEQID) |
		 LS_64(I40IW_CQP_OP_CREATE_CQ, I40IW_CQPSQ_OPCODE) |
		 LS_64(ccq->pbl_chunk_size, I40IW_CQPSQ_CQ_LPBLSIZE) |
		 LS_64(check_overflow, I40IW_CQPSQ_CQ_CHKOVERFLOW) |
		 LS_64(ccq->virtual_map, I40IW_CQPSQ_CQ_VIRTMAP) |
		 LS_64(ccq->ceqe_mask, I40IW_CQPSQ_CQ_ENCEQEMASK) |
		 LS_64(ccq->ceq_id_valid, I40IW_CQPSQ_CQ_CEQIDVALID) |
		 LS_64(ccq->tph_en, I40IW_CQPSQ_TPHEN) |
		 LS_64(ccq->cq_uk.avoid_mem_cflct, I40IW_CQPSQ_CQ_AVOIDMEMCNFLCT) |
		 LS_64(cqp->polarity, I40IW_CQPSQ_WQEVALID);

	i40iw_insert_wqe_hdr(wqe, header);

	i40iw_debug_buf(cqp->dev, I40IW_DEBUG_WQE, "CCQ_CREATE WQE",
			wqe, I40IW_CQP_WQE_SIZE * 8);

	if (post_sq) {
		i40iw_sc_cqp_post_sq(cqp);
		ret_code = i40iw_sc_ccq_create_done(ccq);
		if (ret_code)
			return ret_code;
	}
	cqp->process_cqp_sds = i40iw_cqp_sds_cmd;

	return 0;
}

/**
 * i40iw_sc_ccq_destroy - destroy ccq during close
 * @ccq: ccq sc struct
 * @scratch: u64 saved to be used during cqp completion
 * @post_sq: flag for cqp db to ring
 */
static enum i40iw_status_code i40iw_sc_ccq_destroy(struct i40iw_sc_cq *ccq,
						   u64 scratch,
						   bool post_sq)
{
	struct i40iw_sc_cqp *cqp;
	u64 *wqe;
	u64 header;
	enum i40iw_status_code ret_code = 0;
	u32 tail, val, error;

	cqp = ccq->dev->cqp;
	wqe = i40iw_sc_cqp_get_next_send_wqe(cqp, scratch);
	if (!wqe)
		return I40IW_ERR_RING_FULL;
	set_64bit_val(wqe, 0, ccq->cq_uk.cq_size);
	set_64bit_val(wqe, 8, RS_64_1(ccq, 1));
	set_64bit_val(wqe, 40, ccq->shadow_area_pa);

	header = ccq->cq_uk.cq_id |
		 LS_64((ccq->ceq_id_valid ? ccq->ceq_id : 0), I40IW_CQPSQ_CQ_CEQID) |
		 LS_64(I40IW_CQP_OP_DESTROY_CQ, I40IW_CQPSQ_OPCODE) |
		 LS_64(ccq->ceqe_mask, I40IW_CQPSQ_CQ_ENCEQEMASK) |
		 LS_64(ccq->ceq_id_valid, I40IW_CQPSQ_CQ_CEQIDVALID) |
		 LS_64(ccq->tph_en, I40IW_CQPSQ_TPHEN) |
		 LS_64(ccq->cq_uk.avoid_mem_cflct, I40IW_CQPSQ_CQ_AVOIDMEMCNFLCT) |
		 LS_64(cqp->polarity, I40IW_CQPSQ_WQEVALID);

	i40iw_insert_wqe_hdr(wqe, header);

	i40iw_debug_buf(cqp->dev, I40IW_DEBUG_WQE, "CCQ_DESTROY WQE",
			wqe, I40IW_CQP_WQE_SIZE * 8);

	i40iw_get_cqp_reg_info(cqp, &val, &tail, &error);
	if (error)
		return I40IW_ERR_CQP_COMPL_ERROR;

	if (post_sq) {
		i40iw_sc_cqp_post_sq(cqp);
		ret_code = i40iw_cqp_poll_registers(cqp, tail, 1000);
	}

	cqp->process_cqp_sds = i40iw_update_sds_noccq;

	return ret_code;
}

/**
 * i40iw_sc_cq_init - initialize completion q
 * @cq: cq struct
 * @info: cq initialization info
 */
static enum i40iw_status_code i40iw_sc_cq_init(struct i40iw_sc_cq *cq,
					       struct i40iw_cq_init_info *info)
{
	u32 __iomem *cqe_alloc_reg = NULL;
	enum i40iw_status_code ret_code;
	u32 pble_obj_cnt;
	u32 arm_offset;

	pble_obj_cnt = info->dev->hmc_info->hmc_obj[I40IW_HMC_IW_PBLE].cnt;

	if (info->virtual_map && (info->first_pm_pbl_idx >= pble_obj_cnt))
		return I40IW_ERR_INVALID_PBLE_INDEX;

	cq->cq_pa = info->cq_base_pa;
	cq->dev = info->dev;
	cq->ceq_id = info->ceq_id;
	arm_offset = (info->dev->is_pf) ? I40E_PFPE_CQARM : I40E_VFPE_CQARM1;
	if (i40iw_get_hw_addr(cq->dev))
		cqe_alloc_reg = (u32 __iomem *)(i40iw_get_hw_addr(cq->dev) +
					      arm_offset);
	info->cq_uk_init_info.cqe_alloc_reg = cqe_alloc_reg;
	ret_code = i40iw_cq_uk_init(&cq->cq_uk, &info->cq_uk_init_info);
	if (ret_code)
		return ret_code;
	cq->virtual_map = info->virtual_map;
	cq->pbl_chunk_size = info->pbl_chunk_size;
	cq->ceqe_mask = info->ceqe_mask;
	cq->cq_type = (info->type) ? info->type : I40IW_CQ_TYPE_IWARP;

	cq->shadow_area_pa = info->shadow_area_pa;
	cq->shadow_read_threshold = info->shadow_read_threshold;

	cq->ceq_id_valid = info->ceq_id_valid;
	cq->tph_en = info->tph_en;
	cq->tph_val = info->tph_val;

	cq->first_pm_pbl_idx = info->first_pm_pbl_idx;

	return 0;
}

/**
 * i40iw_sc_cq_create - create completion q
 * @cq: cq struct
 * @scratch: u64 saved to be used during cqp completion
 * @check_overflow: flag for overflow check
 * @post_sq: flag for cqp db to ring
 */
static enum i40iw_status_code i40iw_sc_cq_create(struct i40iw_sc_cq *cq,
						 u64 scratch,
						 bool check_overflow,
						 bool post_sq)
{
	u64 *wqe;
	struct i40iw_sc_cqp *cqp;
	u64 header;

	if (cq->cq_uk.cq_id > I40IW_MAX_CQID)
		return I40IW_ERR_INVALID_CQ_ID;

	if (cq->ceq_id > I40IW_MAX_CEQID)
		return I40IW_ERR_INVALID_CEQ_ID;

	cqp = cq->dev->cqp;
	wqe = i40iw_sc_cqp_get_next_send_wqe(cqp, scratch);
	if (!wqe)
		return I40IW_ERR_RING_FULL;

	set_64bit_val(wqe, 0, cq->cq_uk.cq_size);
	set_64bit_val(wqe, 8, RS_64_1(cq, 1));
	set_64bit_val(wqe,
		      16,
		      LS_64(cq->shadow_read_threshold, I40IW_CQPSQ_CQ_SHADOW_READ_THRESHOLD));

	set_64bit_val(wqe, 32, (cq->virtual_map ? 0 : cq->cq_pa));

	set_64bit_val(wqe, 40, cq->shadow_area_pa);
	set_64bit_val(wqe, 48, (cq->virtual_map ? cq->first_pm_pbl_idx : 0));
	set_64bit_val(wqe, 56, LS_64(cq->tph_val, I40IW_CQPSQ_TPHVAL));

	header = cq->cq_uk.cq_id |
		 LS_64((cq->ceq_id_valid ? cq->ceq_id : 0), I40IW_CQPSQ_CQ_CEQID) |
		 LS_64(I40IW_CQP_OP_CREATE_CQ, I40IW_CQPSQ_OPCODE) |
		 LS_64(cq->pbl_chunk_size, I40IW_CQPSQ_CQ_LPBLSIZE) |
		 LS_64(check_overflow, I40IW_CQPSQ_CQ_CHKOVERFLOW) |
		 LS_64(cq->virtual_map, I40IW_CQPSQ_CQ_VIRTMAP) |
		 LS_64(cq->ceqe_mask, I40IW_CQPSQ_CQ_ENCEQEMASK) |
		 LS_64(cq->ceq_id_valid, I40IW_CQPSQ_CQ_CEQIDVALID) |
		 LS_64(cq->tph_en, I40IW_CQPSQ_TPHEN) |
		 LS_64(cq->cq_uk.avoid_mem_cflct, I40IW_CQPSQ_CQ_AVOIDMEMCNFLCT) |
		 LS_64(cqp->polarity, I40IW_CQPSQ_WQEVALID);

	i40iw_insert_wqe_hdr(wqe, header);

	i40iw_debug_buf(cqp->dev, I40IW_DEBUG_WQE, "CQ_CREATE WQE",
			wqe, I40IW_CQP_WQE_SIZE * 8);

	if (post_sq)
		i40iw_sc_cqp_post_sq(cqp);
	return 0;
}

/**
 * i40iw_sc_cq_destroy - destroy completion q
 * @cq: cq struct
 * @scratch: u64 saved to be used during cqp completion
 * @post_sq: flag for cqp db to ring
 */
static enum i40iw_status_code i40iw_sc_cq_destroy(struct i40iw_sc_cq *cq,
						  u64 scratch,
						  bool post_sq)
{
	struct i40iw_sc_cqp *cqp;
	u64 *wqe;
	u64 header;

	cqp = cq->dev->cqp;
	wqe = i40iw_sc_cqp_get_next_send_wqe(cqp, scratch);
	if (!wqe)
		return I40IW_ERR_RING_FULL;
	set_64bit_val(wqe, 0, cq->cq_uk.cq_size);
	set_64bit_val(wqe, 8, RS_64_1(cq, 1));
	set_64bit_val(wqe, 40, cq->shadow_area_pa);
	set_64bit_val(wqe, 48, (cq->virtual_map ? cq->first_pm_pbl_idx : 0));

	header = cq->cq_uk.cq_id |
		 LS_64((cq->ceq_id_valid ? cq->ceq_id : 0), I40IW_CQPSQ_CQ_CEQID) |
		 LS_64(I40IW_CQP_OP_DESTROY_CQ, I40IW_CQPSQ_OPCODE) |
		 LS_64(cq->pbl_chunk_size, I40IW_CQPSQ_CQ_LPBLSIZE) |
		 LS_64(cq->virtual_map, I40IW_CQPSQ_CQ_VIRTMAP) |
		 LS_64(cq->ceqe_mask, I40IW_CQPSQ_CQ_ENCEQEMASK) |
		 LS_64(cq->ceq_id_valid, I40IW_CQPSQ_CQ_CEQIDVALID) |
		 LS_64(cq->tph_en, I40IW_CQPSQ_TPHEN) |
		 LS_64(cq->cq_uk.avoid_mem_cflct, I40IW_CQPSQ_CQ_AVOIDMEMCNFLCT) |
		 LS_64(cqp->polarity, I40IW_CQPSQ_WQEVALID);

	i40iw_insert_wqe_hdr(wqe, header);

	i40iw_debug_buf(cqp->dev, I40IW_DEBUG_WQE, "CQ_DESTROY WQE",
			wqe, I40IW_CQP_WQE_SIZE * 8);

	if (post_sq)
		i40iw_sc_cqp_post_sq(cqp);
	return 0;
}

/**
 * i40iw_sc_cq_modify - modify a Completion Queue
 * @cq: cq struct
 * @info: modification info struct
 * @scratch:
 * @post_sq: flag to post to sq
 */
static enum i40iw_status_code i40iw_sc_cq_modify(struct i40iw_sc_cq *cq,
						 struct i40iw_modify_cq_info *info,
						 u64 scratch,
						 bool post_sq)
{
	struct i40iw_sc_cqp *cqp;
	u64 *wqe;
	u64 header;
	u32 cq_size, ceq_id, first_pm_pbl_idx;
	u8 pbl_chunk_size;
	bool virtual_map, ceq_id_valid, check_overflow;
	u32 pble_obj_cnt;

	if (info->ceq_valid && (info->ceq_id > I40IW_MAX_CEQID))
		return I40IW_ERR_INVALID_CEQ_ID;

	pble_obj_cnt = cq->dev->hmc_info->hmc_obj[I40IW_HMC_IW_PBLE].cnt;

	if (info->cq_resize && info->virtual_map &&
	    (info->first_pm_pbl_idx >= pble_obj_cnt))
		return I40IW_ERR_INVALID_PBLE_INDEX;

	cqp = cq->dev->cqp;
	wqe = i40iw_sc_cqp_get_next_send_wqe(cqp, scratch);
	if (!wqe)
		return I40IW_ERR_RING_FULL;

	cq->pbl_list = info->pbl_list;
	cq->cq_pa = info->cq_pa;
	cq->first_pm_pbl_idx = info->first_pm_pbl_idx;

	cq_size = info->cq_resize ? info->cq_size : cq->cq_uk.cq_size;
	if (info->ceq_change) {
		ceq_id_valid = true;
		ceq_id = info->ceq_id;
	} else {
		ceq_id_valid = cq->ceq_id_valid;
		ceq_id = ceq_id_valid ? cq->ceq_id : 0;
	}
	virtual_map = info->cq_resize ? info->virtual_map : cq->virtual_map;
	first_pm_pbl_idx = (info->cq_resize ?
			    (info->virtual_map ? info->first_pm_pbl_idx : 0) :
			    (cq->virtual_map ? cq->first_pm_pbl_idx : 0));
	pbl_chunk_size = (info->cq_resize ?
			  (info->virtual_map ? info->pbl_chunk_size : 0) :
			  (cq->virtual_map ? cq->pbl_chunk_size : 0));
	check_overflow = info->check_overflow_change ? info->check_overflow :
			 cq->check_overflow;
	cq->cq_uk.cq_size = cq_size;
	cq->ceq_id_valid = ceq_id_valid;
	cq->ceq_id = ceq_id;
	cq->virtual_map = virtual_map;
	cq->first_pm_pbl_idx = first_pm_pbl_idx;
	cq->pbl_chunk_size = pbl_chunk_size;
	cq->check_overflow = check_overflow;

	set_64bit_val(wqe, 0, cq_size);
	set_64bit_val(wqe, 8, RS_64_1(cq, 1));
	set_64bit_val(wqe, 16,
		      LS_64(info->shadow_read_threshold, I40IW_CQPSQ_CQ_SHADOW_READ_THRESHOLD));
	set_64bit_val(wqe, 32, (cq->virtual_map ? 0 : cq->cq_pa));
	set_64bit_val(wqe, 40, cq->shadow_area_pa);
	set_64bit_val(wqe, 48, (cq->virtual_map ? first_pm_pbl_idx : 0));
	set_64bit_val(wqe, 56, LS_64(cq->tph_val, I40IW_CQPSQ_TPHVAL));

	header = cq->cq_uk.cq_id |
		 LS_64(ceq_id, I40IW_CQPSQ_CQ_CEQID) |
		 LS_64(I40IW_CQP_OP_MODIFY_CQ, I40IW_CQPSQ_OPCODE) |
		 LS_64(info->cq_resize, I40IW_CQPSQ_CQ_CQRESIZE) |
		 LS_64(pbl_chunk_size, I40IW_CQPSQ_CQ_LPBLSIZE) |
		 LS_64(check_overflow, I40IW_CQPSQ_CQ_CHKOVERFLOW) |
		 LS_64(virtual_map, I40IW_CQPSQ_CQ_VIRTMAP) |
		 LS_64(cq->ceqe_mask, I40IW_CQPSQ_CQ_ENCEQEMASK) |
		 LS_64(ceq_id_valid, I40IW_CQPSQ_CQ_CEQIDVALID) |
		 LS_64(cq->tph_en, I40IW_CQPSQ_TPHEN) |
		 LS_64(cq->cq_uk.avoid_mem_cflct, I40IW_CQPSQ_CQ_AVOIDMEMCNFLCT) |
		 LS_64(cqp->polarity, I40IW_CQPSQ_WQEVALID);

	i40iw_insert_wqe_hdr(wqe, header);

	i40iw_debug_buf(cqp->dev, I40IW_DEBUG_WQE, "CQ_MODIFY WQE",
			wqe, I40IW_CQP_WQE_SIZE * 8);

	if (post_sq)
		i40iw_sc_cqp_post_sq(cqp);
	return 0;
}

/**
 * i40iw_sc_qp_init - initialize qp
 * @qp: sc qp
 * @info: initialization qp info
 */
static enum i40iw_status_code i40iw_sc_qp_init(struct i40iw_sc_qp *qp,
					       struct i40iw_qp_init_info *info)
{
	u32 __iomem *wqe_alloc_reg = NULL;
	enum i40iw_status_code ret_code;
	u32 pble_obj_cnt;
	u8 wqe_size;
	u32 offset;

	qp->dev = info->pd->dev;
	qp->vsi = info->vsi;
	qp->sq_pa = info->sq_pa;
	qp->rq_pa = info->rq_pa;
	qp->hw_host_ctx_pa = info->host_ctx_pa;
	qp->q2_pa = info->q2_pa;
	qp->shadow_area_pa = info->shadow_area_pa;

	qp->q2_buf = info->q2;
	qp->pd = info->pd;
	qp->hw_host_ctx = info->host_ctx;
	offset = (qp->pd->dev->is_pf) ? I40E_PFPE_WQEALLOC : I40E_VFPE_WQEALLOC1;
	if (i40iw_get_hw_addr(qp->pd->dev))
		wqe_alloc_reg = (u32 __iomem *)(i40iw_get_hw_addr(qp->pd->dev) +
					      offset);

	info->qp_uk_init_info.wqe_alloc_reg = wqe_alloc_reg;
	info->qp_uk_init_info.abi_ver = qp->pd->abi_ver;
	ret_code = i40iw_qp_uk_init(&qp->qp_uk, &info->qp_uk_init_info);
	if (ret_code)
		return ret_code;
	qp->virtual_map = info->virtual_map;

	pble_obj_cnt = info->pd->dev->hmc_info->hmc_obj[I40IW_HMC_IW_PBLE].cnt;

	if ((info->virtual_map && (info->sq_pa >= pble_obj_cnt)) ||
	    (info->virtual_map && (info->rq_pa >= pble_obj_cnt)))
		return I40IW_ERR_INVALID_PBLE_INDEX;

	qp->llp_stream_handle = (void *)(-1);
	qp->qp_type = (info->type) ? info->type : I40IW_QP_TYPE_IWARP;

	qp->hw_sq_size = i40iw_get_encoded_wqe_size(qp->qp_uk.sq_ring.size,
						    false);
	i40iw_debug(qp->dev, I40IW_DEBUG_WQE, "%s: hw_sq_size[%04d] sq_ring.size[%04d]\n",
		    __func__, qp->hw_sq_size, qp->qp_uk.sq_ring.size);

	switch (qp->pd->abi_ver) {
	case 4:
		ret_code = i40iw_fragcnt_to_wqesize_rq(qp->qp_uk.max_rq_frag_cnt,
						       &wqe_size);
		if (ret_code)
			return ret_code;
		break;
	case 5: /* fallthrough until next ABI version */
	default:
		if (qp->qp_uk.max_rq_frag_cnt > I40IW_MAX_WQ_FRAGMENT_COUNT)
			return I40IW_ERR_INVALID_FRAG_COUNT;
		wqe_size = I40IW_MAX_WQE_SIZE_RQ;
		break;
	}
	qp->hw_rq_size = i40iw_get_encoded_wqe_size(qp->qp_uk.rq_size *
				(wqe_size / I40IW_QP_WQE_MIN_SIZE), false);
	i40iw_debug(qp->dev, I40IW_DEBUG_WQE,
		    "%s: hw_rq_size[%04d] qp_uk.rq_size[%04d] wqe_size[%04d]\n",
		    __func__, qp->hw_rq_size, qp->qp_uk.rq_size, wqe_size);
	qp->sq_tph_val = info->sq_tph_val;
	qp->rq_tph_val = info->rq_tph_val;
	qp->sq_tph_en = info->sq_tph_en;
	qp->rq_tph_en = info->rq_tph_en;
	qp->rcv_tph_en = info->rcv_tph_en;
	qp->xmit_tph_en = info->xmit_tph_en;
	qp->qs_handle = qp->vsi->qos[qp->user_pri].qs_handle;

	return 0;
}

/**
 * i40iw_sc_qp_create - create qp
 * @qp: sc qp
 * @info: qp create info
 * @scratch: u64 saved to be used during cqp completion
 * @post_sq: flag for cqp db to ring
 */
static enum i40iw_status_code i40iw_sc_qp_create(
				struct i40iw_sc_qp *qp,
				struct i40iw_create_qp_info *info,
				u64 scratch,
				bool post_sq)
{
	struct i40iw_sc_cqp *cqp;
	u64 *wqe;
	u64 header;

	if ((qp->qp_uk.qp_id < I40IW_MIN_IW_QP_ID) ||
	    (qp->qp_uk.qp_id > I40IW_MAX_IW_QP_ID))
		return I40IW_ERR_INVALID_QP_ID;

	cqp = qp->pd->dev->cqp;
	wqe = i40iw_sc_cqp_get_next_send_wqe(cqp, scratch);
	if (!wqe)
		return I40IW_ERR_RING_FULL;

	set_64bit_val(wqe, 16, qp->hw_host_ctx_pa);

	set_64bit_val(wqe, 40, qp->shadow_area_pa);

	header = qp->qp_uk.qp_id |
		 LS_64(I40IW_CQP_OP_CREATE_QP, I40IW_CQPSQ_OPCODE) |
		 LS_64((info->ord_valid ? 1 : 0), I40IW_CQPSQ_QP_ORDVALID) |
		 LS_64(info->tcp_ctx_valid, I40IW_CQPSQ_QP_TOECTXVALID) |
		 LS_64(qp->qp_type, I40IW_CQPSQ_QP_QPTYPE) |
		 LS_64(qp->virtual_map, I40IW_CQPSQ_QP_VQ) |
		 LS_64(info->cq_num_valid, I40IW_CQPSQ_QP_CQNUMVALID) |
		 LS_64(info->arp_cache_idx_valid, I40IW_CQPSQ_QP_ARPTABIDXVALID) |
		 LS_64(info->next_iwarp_state, I40IW_CQPSQ_QP_NEXTIWSTATE) |
		 LS_64(cqp->polarity, I40IW_CQPSQ_WQEVALID);

	i40iw_insert_wqe_hdr(wqe, header);
	i40iw_debug_buf(cqp->dev, I40IW_DEBUG_WQE, "QP_CREATE WQE",
			wqe, I40IW_CQP_WQE_SIZE * 8);

	if (post_sq)
		i40iw_sc_cqp_post_sq(cqp);
	return 0;
}

/**
 * i40iw_sc_qp_modify - modify qp cqp wqe
 * @qp: sc qp
 * @info: modify qp info
 * @scratch: u64 saved to be used during cqp completion
 * @post_sq: flag for cqp db to ring
 */
static enum i40iw_status_code i40iw_sc_qp_modify(
				struct i40iw_sc_qp *qp,
				struct i40iw_modify_qp_info *info,
				u64 scratch,
				bool post_sq)
{
	u64 *wqe;
	struct i40iw_sc_cqp *cqp;
	u64 header;
	u8 term_actions = 0;
	u8 term_len = 0;

	cqp = qp->pd->dev->cqp;
	wqe = i40iw_sc_cqp_get_next_send_wqe(cqp, scratch);
	if (!wqe)
		return I40IW_ERR_RING_FULL;
	if (info->next_iwarp_state == I40IW_QP_STATE_TERMINATE) {
		if (info->dont_send_fin)
			term_actions += I40IWQP_TERM_SEND_TERM_ONLY;
		if (info->dont_send_term)
			term_actions += I40IWQP_TERM_SEND_FIN_ONLY;
		if ((term_actions == I40IWQP_TERM_SEND_TERM_AND_FIN) ||
		    (term_actions == I40IWQP_TERM_SEND_TERM_ONLY))
			term_len = info->termlen;
	}

	set_64bit_val(wqe,
		      8,
		      LS_64(term_len, I40IW_CQPSQ_QP_TERMLEN));

	set_64bit_val(wqe, 16, qp->hw_host_ctx_pa);
	set_64bit_val(wqe, 40, qp->shadow_area_pa);

	header = qp->qp_uk.qp_id |
		 LS_64(I40IW_CQP_OP_MODIFY_QP, I40IW_CQPSQ_OPCODE) |
		 LS_64(info->ord_valid, I40IW_CQPSQ_QP_ORDVALID) |
		 LS_64(info->tcp_ctx_valid, I40IW_CQPSQ_QP_TOECTXVALID) |
		 LS_64(info->cached_var_valid, I40IW_CQPSQ_QP_CACHEDVARVALID) |
		 LS_64(qp->virtual_map, I40IW_CQPSQ_QP_VQ) |
		 LS_64(info->cq_num_valid, I40IW_CQPSQ_QP_CQNUMVALID) |
		 LS_64(info->force_loopback, I40IW_CQPSQ_QP_FORCELOOPBACK) |
		 LS_64(qp->qp_type, I40IW_CQPSQ_QP_QPTYPE) |
		 LS_64(info->remove_hash_idx, I40IW_CQPSQ_QP_REMOVEHASHENTRY) |
		 LS_64(term_actions, I40IW_CQPSQ_QP_TERMACT) |
		 LS_64(info->reset_tcp_conn, I40IW_CQPSQ_QP_RESETCON) |
		 LS_64(info->arp_cache_idx_valid, I40IW_CQPSQ_QP_ARPTABIDXVALID) |
		 LS_64(info->next_iwarp_state, I40IW_CQPSQ_QP_NEXTIWSTATE) |
		 LS_64(cqp->polarity, I40IW_CQPSQ_WQEVALID);

	i40iw_insert_wqe_hdr(wqe, header);

	i40iw_debug_buf(cqp->dev, I40IW_DEBUG_WQE, "QP_MODIFY WQE",
			wqe, I40IW_CQP_WQE_SIZE * 8);

	if (post_sq)
		i40iw_sc_cqp_post_sq(cqp);
	return 0;
}

/**
 * i40iw_sc_qp_destroy - cqp destroy qp
 * @qp: sc qp
 * @scratch: u64 saved to be used during cqp completion
 * @remove_hash_idx: flag if to remove hash idx
 * @ignore_mw_bnd: memory window bind flag
 * @post_sq: flag for cqp db to ring
 */
static enum i40iw_status_code i40iw_sc_qp_destroy(
					struct i40iw_sc_qp *qp,
					u64 scratch,
					bool remove_hash_idx,
					bool ignore_mw_bnd,
					bool post_sq)
{
	u64 *wqe;
	struct i40iw_sc_cqp *cqp;
	u64 header;

	i40iw_qp_rem_qos(qp);
	cqp = qp->pd->dev->cqp;
	wqe = i40iw_sc_cqp_get_next_send_wqe(cqp, scratch);
	if (!wqe)
		return I40IW_ERR_RING_FULL;
	set_64bit_val(wqe, 16, qp->hw_host_ctx_pa);
	set_64bit_val(wqe, 40, qp->shadow_area_pa);

	header = qp->qp_uk.qp_id |
		 LS_64(I40IW_CQP_OP_DESTROY_QP, I40IW_CQPSQ_OPCODE) |
		 LS_64(qp->qp_type, I40IW_CQPSQ_QP_QPTYPE) |
		 LS_64(ignore_mw_bnd, I40IW_CQPSQ_QP_IGNOREMWBOUND) |
		 LS_64(remove_hash_idx, I40IW_CQPSQ_QP_REMOVEHASHENTRY) |
		 LS_64(cqp->polarity, I40IW_CQPSQ_WQEVALID);

	i40iw_insert_wqe_hdr(wqe, header);
	i40iw_debug_buf(cqp->dev, I40IW_DEBUG_WQE, "QP_DESTROY WQE",
			wqe, I40IW_CQP_WQE_SIZE * 8);

	if (post_sq)
		i40iw_sc_cqp_post_sq(cqp);
	return 0;
}

/**
 * i40iw_sc_qp_flush_wqes - flush qp's wqe
 * @qp: sc qp
 * @info: dlush information
 * @scratch: u64 saved to be used during cqp completion
 * @post_sq: flag for cqp db to ring
 */
static enum i40iw_status_code i40iw_sc_qp_flush_wqes(
				struct i40iw_sc_qp *qp,
				struct i40iw_qp_flush_info *info,
				u64 scratch,
				bool post_sq)
{
	u64 temp = 0;
	u64 *wqe;
	struct i40iw_sc_cqp *cqp;
	u64 header;
	bool flush_sq = false, flush_rq = false;

	if (info->rq && !qp->flush_rq)
		flush_rq = true;

	if (info->sq && !qp->flush_sq)
		flush_sq = true;

	qp->flush_sq |= flush_sq;
	qp->flush_rq |= flush_rq;
	if (!flush_sq && !flush_rq) {
		if (info->ae_code != I40IW_AE_LLP_RECEIVED_MPA_CRC_ERROR)
			return 0;
	}

	cqp = qp->pd->dev->cqp;
	wqe = i40iw_sc_cqp_get_next_send_wqe(cqp, scratch);
	if (!wqe)
		return I40IW_ERR_RING_FULL;
	if (info->userflushcode) {
		if (flush_rq) {
			temp |= LS_64(info->rq_minor_code, I40IW_CQPSQ_FWQE_RQMNERR) |
				LS_64(info->rq_major_code, I40IW_CQPSQ_FWQE_RQMJERR);
		}
		if (flush_sq) {
			temp |= LS_64(info->sq_minor_code, I40IW_CQPSQ_FWQE_SQMNERR) |
				LS_64(info->sq_major_code, I40IW_CQPSQ_FWQE_SQMJERR);
		}
	}
	set_64bit_val(wqe, 16, temp);

	temp = (info->generate_ae) ?
		info->ae_code | LS_64(info->ae_source, I40IW_CQPSQ_FWQE_AESOURCE) : 0;

	set_64bit_val(wqe, 8, temp);

	header = qp->qp_uk.qp_id |
		 LS_64(I40IW_CQP_OP_FLUSH_WQES, I40IW_CQPSQ_OPCODE) |
		 LS_64(info->generate_ae, I40IW_CQPSQ_FWQE_GENERATE_AE) |
		 LS_64(info->userflushcode, I40IW_CQPSQ_FWQE_USERFLCODE) |
		 LS_64(flush_sq, I40IW_CQPSQ_FWQE_FLUSHSQ) |
		 LS_64(flush_rq, I40IW_CQPSQ_FWQE_FLUSHRQ) |
		 LS_64(cqp->polarity, I40IW_CQPSQ_WQEVALID);

	i40iw_insert_wqe_hdr(wqe, header);

	i40iw_debug_buf(cqp->dev, I40IW_DEBUG_WQE, "QP_FLUSH WQE",
			wqe, I40IW_CQP_WQE_SIZE * 8);

	if (post_sq)
		i40iw_sc_cqp_post_sq(cqp);
	return 0;
}

/**
 * i40iw_sc_qp_upload_context - upload qp's context
 * @dev: sc device struct
 * @info: upload context info ptr for return
 * @scratch: u64 saved to be used during cqp completion
 * @post_sq: flag for cqp db to ring
 */
static enum i40iw_status_code i40iw_sc_qp_upload_context(
					struct i40iw_sc_dev *dev,
					struct i40iw_upload_context_info *info,
					u64 scratch,
					bool post_sq)
{
	u64 *wqe;
	struct i40iw_sc_cqp *cqp;
	u64 header;

	cqp = dev->cqp;
	wqe = i40iw_sc_cqp_get_next_send_wqe(cqp, scratch);
	if (!wqe)
		return I40IW_ERR_RING_FULL;
	set_64bit_val(wqe, 16, info->buf_pa);

	header = LS_64(info->qp_id, I40IW_CQPSQ_UCTX_QPID) |
		 LS_64(I40IW_CQP_OP_UPLOAD_CONTEXT, I40IW_CQPSQ_OPCODE) |
		 LS_64(info->qp_type, I40IW_CQPSQ_UCTX_QPTYPE) |
		 LS_64(info->raw_format, I40IW_CQPSQ_UCTX_RAWFORMAT) |
		 LS_64(info->freeze_qp, I40IW_CQPSQ_UCTX_FREEZEQP) |
		 LS_64(cqp->polarity, I40IW_CQPSQ_WQEVALID);

	i40iw_insert_wqe_hdr(wqe, header);

	i40iw_debug_buf(dev, I40IW_DEBUG_WQE, "QP_UPLOAD_CTX WQE",
			wqe, I40IW_CQP_WQE_SIZE * 8);

	if (post_sq)
		i40iw_sc_cqp_post_sq(cqp);
	return 0;
}

/**
 * i40iw_sc_qp_setctx - set qp's context
 * @qp: sc qp
 * @qp_ctx: context ptr
 * @info: ctx info
 */
static enum i40iw_status_code i40iw_sc_qp_setctx(
				struct i40iw_sc_qp *qp,
				u64 *qp_ctx,
				struct i40iw_qp_host_ctx_info *info)
{
	struct i40iwarp_offload_info *iw;
	struct i40iw_tcp_offload_info *tcp;
	struct i40iw_sc_vsi *vsi;
	struct i40iw_sc_dev *dev;
	u64 qw0, qw3, qw7 = 0;

	iw = info->iwarp_info;
	tcp = info->tcp_info;
	vsi = qp->vsi;
	dev = qp->dev;
	if (info->add_to_qoslist) {
		qp->user_pri = info->user_pri;
		i40iw_qp_add_qos(qp);
		i40iw_debug(qp->dev, I40IW_DEBUG_DCB, "%s qp[%d] UP[%d] qset[%d]\n",
			    __func__, qp->qp_uk.qp_id, qp->user_pri, qp->qs_handle);
	}
	qw0 = LS_64(qp->qp_uk.rq_wqe_size, I40IWQPC_RQWQESIZE) |
	      LS_64(info->err_rq_idx_valid, I40IWQPC_ERR_RQ_IDX_VALID) |
	      LS_64(qp->rcv_tph_en, I40IWQPC_RCVTPHEN) |
	      LS_64(qp->xmit_tph_en, I40IWQPC_XMITTPHEN) |
	      LS_64(qp->rq_tph_en, I40IWQPC_RQTPHEN) |
	      LS_64(qp->sq_tph_en, I40IWQPC_SQTPHEN) |
	      LS_64(info->push_idx, I40IWQPC_PPIDX) |
	      LS_64(info->push_mode_en, I40IWQPC_PMENA);

	set_64bit_val(qp_ctx, 8, qp->sq_pa);
	set_64bit_val(qp_ctx, 16, qp->rq_pa);

	qw3 = LS_64(qp->src_mac_addr_idx, I40IWQPC_SRCMACADDRIDX) |
	      LS_64(qp->hw_rq_size, I40IWQPC_RQSIZE) |
	      LS_64(qp->hw_sq_size, I40IWQPC_SQSIZE);

	set_64bit_val(qp_ctx,
		      128,
		      LS_64(info->err_rq_idx, I40IWQPC_ERR_RQ_IDX));

	set_64bit_val(qp_ctx,
		      136,
		      LS_64(info->send_cq_num, I40IWQPC_TXCQNUM) |
		      LS_64(info->rcv_cq_num, I40IWQPC_RXCQNUM));

	set_64bit_val(qp_ctx,
		      168,
		      LS_64(info->qp_compl_ctx, I40IWQPC_QPCOMPCTX));
	set_64bit_val(qp_ctx,
		      176,
		      LS_64(qp->sq_tph_val, I40IWQPC_SQTPHVAL) |
		      LS_64(qp->rq_tph_val, I40IWQPC_RQTPHVAL) |
		      LS_64(qp->qs_handle, I40IWQPC_QSHANDLE) |
		      LS_64(vsi->exception_lan_queue, I40IWQPC_EXCEPTION_LAN_QUEUE));

	if (info->iwarp_info_valid) {
		qw0 |= LS_64(iw->ddp_ver, I40IWQPC_DDP_VER) |
		       LS_64(iw->rdmap_ver, I40IWQPC_RDMAP_VER);

		qw7 |= LS_64(iw->pd_id, I40IWQPC_PDIDX);
		set_64bit_val(qp_ctx,
			      144,
			      LS_64(qp->q2_pa, I40IWQPC_Q2ADDR) |
			      LS_64(vsi->fcn_id, I40IWQPC_STAT_INDEX));
		set_64bit_val(qp_ctx,
			      152,
			      LS_64(iw->last_byte_sent, I40IWQPC_LASTBYTESENT));

		set_64bit_val(qp_ctx,
			      160,
			      LS_64(iw->ord_size, I40IWQPC_ORDSIZE) |
			      LS_64(iw->ird_size, I40IWQPC_IRDSIZE) |
			      LS_64(iw->wr_rdresp_en, I40IWQPC_WRRDRSPOK) |
			      LS_64(iw->rd_enable, I40IWQPC_RDOK) |
			      LS_64(iw->snd_mark_en, I40IWQPC_SNDMARKERS) |
			      LS_64(iw->bind_en, I40IWQPC_BINDEN) |
			      LS_64(iw->fast_reg_en, I40IWQPC_FASTREGEN) |
			      LS_64(iw->priv_mode_en, I40IWQPC_PRIVEN) |
			      LS_64((((vsi->stats_fcn_id_alloc) &&
				      (dev->is_pf) && (vsi->fcn_id >= I40IW_FIRST_NON_PF_STAT)) ? 1 : 0),
				    I40IWQPC_USESTATSINSTANCE) |
			      LS_64(1, I40IWQPC_IWARPMODE) |
			      LS_64(iw->rcv_mark_en, I40IWQPC_RCVMARKERS) |
			      LS_64(iw->align_hdrs, I40IWQPC_ALIGNHDRS) |
			      LS_64(iw->rcv_no_mpa_crc, I40IWQPC_RCVNOMPACRC) |
			      LS_64(iw->rcv_mark_offset, I40IWQPC_RCVMARKOFFSET) |
			      LS_64(iw->snd_mark_offset, I40IWQPC_SNDMARKOFFSET));
	}
	if (info->tcp_info_valid) {
		qw0 |= LS_64(tcp->ipv4, I40IWQPC_IPV4) |
		       LS_64(tcp->no_nagle, I40IWQPC_NONAGLE) |
		       LS_64(tcp->insert_vlan_tag, I40IWQPC_INSERTVLANTAG) |
		       LS_64(tcp->time_stamp, I40IWQPC_TIMESTAMP) |
		       LS_64(tcp->cwnd_inc_limit, I40IWQPC_LIMIT) |
		       LS_64(tcp->drop_ooo_seg, I40IWQPC_DROPOOOSEG) |
		       LS_64(tcp->dup_ack_thresh, I40IWQPC_DUPACK_THRESH);

		qw3 |= LS_64(tcp->ttl, I40IWQPC_TTL) |
		       LS_64(tcp->src_mac_addr_idx, I40IWQPC_SRCMACADDRIDX) |
		       LS_64(tcp->avoid_stretch_ack, I40IWQPC_AVOIDSTRETCHACK) |
		       LS_64(tcp->tos, I40IWQPC_TOS) |
		       LS_64(tcp->src_port, I40IWQPC_SRCPORTNUM) |
		       LS_64(tcp->dst_port, I40IWQPC_DESTPORTNUM);

		qp->src_mac_addr_idx = tcp->src_mac_addr_idx;
		set_64bit_val(qp_ctx,
			      32,
			      LS_64(tcp->dest_ip_addr2, I40IWQPC_DESTIPADDR2) |
			      LS_64(tcp->dest_ip_addr3, I40IWQPC_DESTIPADDR3));

		set_64bit_val(qp_ctx,
			      40,
			      LS_64(tcp->dest_ip_addr0, I40IWQPC_DESTIPADDR0) |
			      LS_64(tcp->dest_ip_addr1, I40IWQPC_DESTIPADDR1));

		set_64bit_val(qp_ctx,
			      48,
			      LS_64(tcp->snd_mss, I40IWQPC_SNDMSS) |
				LS_64(tcp->vlan_tag, I40IWQPC_VLANTAG) |
				LS_64(tcp->arp_idx, I40IWQPC_ARPIDX));

		qw7 |= LS_64(tcp->flow_label, I40IWQPC_FLOWLABEL) |
		       LS_64(tcp->wscale, I40IWQPC_WSCALE) |
		       LS_64(tcp->ignore_tcp_opt, I40IWQPC_IGNORE_TCP_OPT) |
		       LS_64(tcp->ignore_tcp_uns_opt, I40IWQPC_IGNORE_TCP_UNS_OPT) |
		       LS_64(tcp->tcp_state, I40IWQPC_TCPSTATE) |
		       LS_64(tcp->rcv_wscale, I40IWQPC_RCVSCALE) |
		       LS_64(tcp->snd_wscale, I40IWQPC_SNDSCALE);

		set_64bit_val(qp_ctx,
			      72,
			      LS_64(tcp->time_stamp_recent, I40IWQPC_TIMESTAMP_RECENT) |
			      LS_64(tcp->time_stamp_age, I40IWQPC_TIMESTAMP_AGE));
		set_64bit_val(qp_ctx,
			      80,
			      LS_64(tcp->snd_nxt, I40IWQPC_SNDNXT) |
			      LS_64(tcp->snd_wnd, I40IWQPC_SNDWND));

		set_64bit_val(qp_ctx,
			      88,
			      LS_64(tcp->rcv_nxt, I40IWQPC_RCVNXT) |
			      LS_64(tcp->rcv_wnd, I40IWQPC_RCVWND));
		set_64bit_val(qp_ctx,
			      96,
			      LS_64(tcp->snd_max, I40IWQPC_SNDMAX) |
			      LS_64(tcp->snd_una, I40IWQPC_SNDUNA));
		set_64bit_val(qp_ctx,
			      104,
			      LS_64(tcp->srtt, I40IWQPC_SRTT) |
			      LS_64(tcp->rtt_var, I40IWQPC_RTTVAR));
		set_64bit_val(qp_ctx,
			      112,
			      LS_64(tcp->ss_thresh, I40IWQPC_SSTHRESH) |
			      LS_64(tcp->cwnd, I40IWQPC_CWND));
		set_64bit_val(qp_ctx,
			      120,
			      LS_64(tcp->snd_wl1, I40IWQPC_SNDWL1) |
			      LS_64(tcp->snd_wl2, I40IWQPC_SNDWL2));
		set_64bit_val(qp_ctx,
			      128,
			      LS_64(tcp->max_snd_window, I40IWQPC_MAXSNDWND) |
			      LS_64(tcp->rexmit_thresh, I40IWQPC_REXMIT_THRESH));
		set_64bit_val(qp_ctx,
			      184,
			      LS_64(tcp->local_ipaddr3, I40IWQPC_LOCAL_IPADDR3) |
			      LS_64(tcp->local_ipaddr2, I40IWQPC_LOCAL_IPADDR2));
		set_64bit_val(qp_ctx,
			      192,
			      LS_64(tcp->local_ipaddr1, I40IWQPC_LOCAL_IPADDR1) |
			      LS_64(tcp->local_ipaddr0, I40IWQPC_LOCAL_IPADDR0));
	}

	set_64bit_val(qp_ctx, 0, qw0);
	set_64bit_val(qp_ctx, 24, qw3);
	set_64bit_val(qp_ctx, 56, qw7);

	i40iw_debug_buf(qp->dev, I40IW_DEBUG_WQE, "QP_HOST)CTX WQE",
			qp_ctx, I40IW_QP_CTX_SIZE);
	return 0;
}

/**
 * i40iw_sc_alloc_stag - mr stag alloc
 * @dev: sc device struct
 * @info: stag info
 * @scratch: u64 saved to be used during cqp completion
 * @post_sq: flag for cqp db to ring
 */
static enum i40iw_status_code i40iw_sc_alloc_stag(
				struct i40iw_sc_dev *dev,
				struct i40iw_allocate_stag_info *info,
				u64 scratch,
				bool post_sq)
{
	u64 *wqe;
	struct i40iw_sc_cqp *cqp;
	u64 header;
	enum i40iw_page_size page_size;

	page_size = (info->page_size == 0x200000) ? I40IW_PAGE_SIZE_2M : I40IW_PAGE_SIZE_4K;
	cqp = dev->cqp;
	wqe = i40iw_sc_cqp_get_next_send_wqe(cqp, scratch);
	if (!wqe)
		return I40IW_ERR_RING_FULL;
	set_64bit_val(wqe,
		      8,
		      LS_64(info->pd_id, I40IW_CQPSQ_STAG_PDID) |
		      LS_64(info->total_len, I40IW_CQPSQ_STAG_STAGLEN));
	set_64bit_val(wqe,
		      16,
		      LS_64(info->stag_idx, I40IW_CQPSQ_STAG_IDX));
	set_64bit_val(wqe,
		      40,
		      LS_64(info->hmc_fcn_index, I40IW_CQPSQ_STAG_HMCFNIDX));

	header = LS_64(I40IW_CQP_OP_ALLOC_STAG, I40IW_CQPSQ_OPCODE) |
		 LS_64(1, I40IW_CQPSQ_STAG_MR) |
		 LS_64(info->access_rights, I40IW_CQPSQ_STAG_ARIGHTS) |
		 LS_64(info->chunk_size, I40IW_CQPSQ_STAG_LPBLSIZE) |
		 LS_64(page_size, I40IW_CQPSQ_STAG_HPAGESIZE) |
		 LS_64(info->remote_access, I40IW_CQPSQ_STAG_REMACCENABLED) |
		 LS_64(info->use_hmc_fcn_index, I40IW_CQPSQ_STAG_USEHMCFNIDX) |
		 LS_64(info->use_pf_rid, I40IW_CQPSQ_STAG_USEPFRID) |
		 LS_64(cqp->polarity, I40IW_CQPSQ_WQEVALID);

	i40iw_insert_wqe_hdr(wqe, header);

	i40iw_debug_buf(dev, I40IW_DEBUG_WQE, "ALLOC_STAG WQE",
			wqe, I40IW_CQP_WQE_SIZE * 8);

	if (post_sq)
		i40iw_sc_cqp_post_sq(cqp);
	return 0;
}

/**
 * i40iw_sc_mr_reg_non_shared - non-shared mr registration
 * @dev: sc device struct
 * @info: mr info
 * @scratch: u64 saved to be used during cqp completion
 * @post_sq: flag for cqp db to ring
 */
static enum i40iw_status_code i40iw_sc_mr_reg_non_shared(
				struct i40iw_sc_dev *dev,
				struct i40iw_reg_ns_stag_info *info,
				u64 scratch,
				bool post_sq)
{
	u64 *wqe;
	u64 temp;
	struct i40iw_sc_cqp *cqp;
	u64 header;
	u32 pble_obj_cnt;
	bool remote_access;
	u8 addr_type;
	enum i40iw_page_size page_size;

	page_size = (info->page_size == 0x200000) ? I40IW_PAGE_SIZE_2M : I40IW_PAGE_SIZE_4K;
	if (info->access_rights & (I40IW_ACCESS_FLAGS_REMOTEREAD_ONLY |
				   I40IW_ACCESS_FLAGS_REMOTEWRITE_ONLY))
		remote_access = true;
	else
		remote_access = false;

	pble_obj_cnt = dev->hmc_info->hmc_obj[I40IW_HMC_IW_PBLE].cnt;

	if (info->chunk_size && (info->first_pm_pbl_index >= pble_obj_cnt))
		return I40IW_ERR_INVALID_PBLE_INDEX;

	cqp = dev->cqp;
	wqe = i40iw_sc_cqp_get_next_send_wqe(cqp, scratch);
	if (!wqe)
		return I40IW_ERR_RING_FULL;

	temp = (info->addr_type == I40IW_ADDR_TYPE_VA_BASED) ? (uintptr_t)info->va : info->fbo;
	set_64bit_val(wqe, 0, temp);

	set_64bit_val(wqe,
		      8,
		      LS_64(info->total_len, I40IW_CQPSQ_STAG_STAGLEN) |
		      LS_64(info->pd_id, I40IW_CQPSQ_STAG_PDID));

	set_64bit_val(wqe,
		      16,
		      LS_64(info->stag_key, I40IW_CQPSQ_STAG_KEY) |
		      LS_64(info->stag_idx, I40IW_CQPSQ_STAG_IDX));
	if (!info->chunk_size) {
		set_64bit_val(wqe, 32, info->reg_addr_pa);
		set_64bit_val(wqe, 48, 0);
	} else {
		set_64bit_val(wqe, 32, 0);
		set_64bit_val(wqe, 48, info->first_pm_pbl_index);
	}
	set_64bit_val(wqe, 40, info->hmc_fcn_index);
	set_64bit_val(wqe, 56, 0);

	addr_type = (info->addr_type == I40IW_ADDR_TYPE_VA_BASED) ? 1 : 0;
	header = LS_64(I40IW_CQP_OP_REG_MR, I40IW_CQPSQ_OPCODE) |
		 LS_64(1, I40IW_CQPSQ_STAG_MR) |
		 LS_64(info->chunk_size, I40IW_CQPSQ_STAG_LPBLSIZE) |
		 LS_64(page_size, I40IW_CQPSQ_STAG_HPAGESIZE) |
		 LS_64(info->access_rights, I40IW_CQPSQ_STAG_ARIGHTS) |
		 LS_64(remote_access, I40IW_CQPSQ_STAG_REMACCENABLED) |
		 LS_64(addr_type, I40IW_CQPSQ_STAG_VABASEDTO) |
		 LS_64(info->use_hmc_fcn_index, I40IW_CQPSQ_STAG_USEHMCFNIDX) |
		 LS_64(info->use_pf_rid, I40IW_CQPSQ_STAG_USEPFRID) |
		 LS_64(cqp->polarity, I40IW_CQPSQ_WQEVALID);

	i40iw_insert_wqe_hdr(wqe, header);

	i40iw_debug_buf(dev, I40IW_DEBUG_WQE, "MR_REG_NS WQE",
			wqe, I40IW_CQP_WQE_SIZE * 8);

	if (post_sq)
		i40iw_sc_cqp_post_sq(cqp);
	return 0;
}

/**
 * i40iw_sc_mr_reg_shared - registered shared memory region
 * @dev: sc device struct
 * @info: info for shared memory registeration
 * @scratch: u64 saved to be used during cqp completion
 * @post_sq: flag for cqp db to ring
 */
static enum i40iw_status_code i40iw_sc_mr_reg_shared(
					struct i40iw_sc_dev *dev,
					struct i40iw_register_shared_stag *info,
					u64 scratch,
					bool post_sq)
{
	u64 *wqe;
	struct i40iw_sc_cqp *cqp;
	u64 temp, va64, fbo, header;
	u32 va32;
	bool remote_access;
	u8 addr_type;

	if (info->access_rights & (I40IW_ACCESS_FLAGS_REMOTEREAD_ONLY |
				   I40IW_ACCESS_FLAGS_REMOTEWRITE_ONLY))
		remote_access = true;
	else
		remote_access = false;
	cqp = dev->cqp;
	wqe = i40iw_sc_cqp_get_next_send_wqe(cqp, scratch);
	if (!wqe)
		return I40IW_ERR_RING_FULL;
	va64 = (uintptr_t)(info->va);
	va32 = (u32)(va64 & 0x00000000FFFFFFFF);
	fbo = (u64)(va32 & (4096 - 1));

	set_64bit_val(wqe,
		      0,
		      (info->addr_type == I40IW_ADDR_TYPE_VA_BASED ? (uintptr_t)info->va : fbo));

	set_64bit_val(wqe,
		      8,
		      LS_64(info->pd_id, I40IW_CQPSQ_STAG_PDID));
	temp = LS_64(info->new_stag_key, I40IW_CQPSQ_STAG_KEY) |
	       LS_64(info->new_stag_idx, I40IW_CQPSQ_STAG_IDX) |
	       LS_64(info->parent_stag_idx, I40IW_CQPSQ_STAG_PARENTSTAGIDX);
	set_64bit_val(wqe, 16, temp);

	addr_type = (info->addr_type == I40IW_ADDR_TYPE_VA_BASED) ? 1 : 0;
	header = LS_64(I40IW_CQP_OP_REG_SMR, I40IW_CQPSQ_OPCODE) |
		 LS_64(1, I40IW_CQPSQ_STAG_MR) |
		 LS_64(info->access_rights, I40IW_CQPSQ_STAG_ARIGHTS) |
		 LS_64(remote_access, I40IW_CQPSQ_STAG_REMACCENABLED) |
		 LS_64(addr_type, I40IW_CQPSQ_STAG_VABASEDTO) |
		 LS_64(cqp->polarity, I40IW_CQPSQ_WQEVALID);

	i40iw_insert_wqe_hdr(wqe, header);

	i40iw_debug_buf(dev, I40IW_DEBUG_WQE, "MR_REG_SHARED WQE",
			wqe, I40IW_CQP_WQE_SIZE * 8);

	if (post_sq)
		i40iw_sc_cqp_post_sq(cqp);
	return 0;
}

/**
 * i40iw_sc_dealloc_stag - deallocate stag
 * @dev: sc device struct
 * @info: dealloc stag info
 * @scratch: u64 saved to be used during cqp completion
 * @post_sq: flag for cqp db to ring
 */
static enum i40iw_status_code i40iw_sc_dealloc_stag(
					struct i40iw_sc_dev *dev,
					struct i40iw_dealloc_stag_info *info,
					u64 scratch,
					bool post_sq)
{
	u64 header;
	u64 *wqe;
	struct i40iw_sc_cqp *cqp;

	cqp = dev->cqp;
	wqe = i40iw_sc_cqp_get_next_send_wqe(cqp, scratch);
	if (!wqe)
		return I40IW_ERR_RING_FULL;
	set_64bit_val(wqe,
		      8,
		      LS_64(info->pd_id, I40IW_CQPSQ_STAG_PDID));
	set_64bit_val(wqe,
		      16,
		      LS_64(info->stag_idx, I40IW_CQPSQ_STAG_IDX));

	header = LS_64(I40IW_CQP_OP_DEALLOC_STAG, I40IW_CQPSQ_OPCODE) |
		 LS_64(info->mr, I40IW_CQPSQ_STAG_MR) |
		 LS_64(cqp->polarity, I40IW_CQPSQ_WQEVALID);

	i40iw_insert_wqe_hdr(wqe, header);

	i40iw_debug_buf(dev, I40IW_DEBUG_WQE, "DEALLOC_STAG WQE",
			wqe, I40IW_CQP_WQE_SIZE * 8);

	if (post_sq)
		i40iw_sc_cqp_post_sq(cqp);
	return 0;
}

/**
 * i40iw_sc_query_stag - query hardware for stag
 * @dev: sc device struct
 * @scratch: u64 saved to be used during cqp completion
 * @stag_index: stag index for query
 * @post_sq: flag for cqp db to ring
 */
static enum i40iw_status_code i40iw_sc_query_stag(struct i40iw_sc_dev *dev,
						  u64 scratch,
						  u32 stag_index,
						  bool post_sq)
{
	u64 header;
	u64 *wqe;
	struct i40iw_sc_cqp *cqp;

	cqp = dev->cqp;
	wqe = i40iw_sc_cqp_get_next_send_wqe(cqp, scratch);
	if (!wqe)
		return I40IW_ERR_RING_FULL;
	set_64bit_val(wqe,
		      16,
		      LS_64(stag_index, I40IW_CQPSQ_QUERYSTAG_IDX));

	header = LS_64(I40IW_CQP_OP_QUERY_STAG, I40IW_CQPSQ_OPCODE) |
		 LS_64(cqp->polarity, I40IW_CQPSQ_WQEVALID);

	i40iw_insert_wqe_hdr(wqe, header);

	i40iw_debug_buf(dev, I40IW_DEBUG_WQE, "QUERY_STAG WQE",
			wqe, I40IW_CQP_WQE_SIZE * 8);

	if (post_sq)
		i40iw_sc_cqp_post_sq(cqp);
	return 0;
}

/**
 * i40iw_sc_mw_alloc - mw allocate
 * @dev: sc device struct
 * @scratch: u64 saved to be used during cqp completion
 * @mw_stag_index:stag index
 * @pd_id: pd is for this mw
 * @post_sq: flag for cqp db to ring
 */
static enum i40iw_status_code i40iw_sc_mw_alloc(
					struct i40iw_sc_dev *dev,
					u64 scratch,
					u32 mw_stag_index,
					u16 pd_id,
					bool post_sq)
{
	u64 header;
	struct i40iw_sc_cqp *cqp;
	u64 *wqe;

	cqp = dev->cqp;
	wqe = i40iw_sc_cqp_get_next_send_wqe(cqp, scratch);
	if (!wqe)
		return I40IW_ERR_RING_FULL;
	set_64bit_val(wqe, 8, LS_64(pd_id, I40IW_CQPSQ_STAG_PDID));
	set_64bit_val(wqe,
		      16,
		      LS_64(mw_stag_index, I40IW_CQPSQ_STAG_IDX));

	header = LS_64(I40IW_CQP_OP_ALLOC_STAG, I40IW_CQPSQ_OPCODE) |
		 LS_64(cqp->polarity, I40IW_CQPSQ_WQEVALID);

	i40iw_insert_wqe_hdr(wqe, header);

	i40iw_debug_buf(dev, I40IW_DEBUG_WQE, "MW_ALLOC WQE",
			wqe, I40IW_CQP_WQE_SIZE * 8);

	if (post_sq)
		i40iw_sc_cqp_post_sq(cqp);
	return 0;
}

/**
 * i40iw_sc_mr_fast_register - Posts RDMA fast register mr WR to iwarp qp
 * @qp: sc qp struct
 * @info: fast mr info
 * @post_sq: flag for cqp db to ring
 */
enum i40iw_status_code i40iw_sc_mr_fast_register(
				struct i40iw_sc_qp *qp,
				struct i40iw_fast_reg_stag_info *info,
				bool post_sq)
{
	u64 temp, header;
	u64 *wqe;
	u32 wqe_idx;
	enum i40iw_page_size page_size;

	page_size = (info->page_size == 0x200000) ? I40IW_PAGE_SIZE_2M : I40IW_PAGE_SIZE_4K;
	wqe = i40iw_qp_get_next_send_wqe(&qp->qp_uk, &wqe_idx, I40IW_QP_WQE_MIN_SIZE,
					 0, info->wr_id);
	if (!wqe)
		return I40IW_ERR_QP_TOOMANY_WRS_POSTED;

	i40iw_debug(qp->dev, I40IW_DEBUG_MR, "%s: wr_id[%llxh] wqe_idx[%04d] location[%p]\n",
		    __func__, info->wr_id, wqe_idx,
		    &qp->qp_uk.sq_wrtrk_array[wqe_idx].wrid);
	temp = (info->addr_type == I40IW_ADDR_TYPE_VA_BASED) ? (uintptr_t)info->va : info->fbo;
	set_64bit_val(wqe, 0, temp);

	temp = RS_64(info->first_pm_pbl_index >> 16, I40IWQPSQ_FIRSTPMPBLIDXHI);
	set_64bit_val(wqe,
		      8,
		      LS_64(temp, I40IWQPSQ_FIRSTPMPBLIDXHI) |
		      LS_64(info->reg_addr_pa >> I40IWQPSQ_PBLADDR_SHIFT, I40IWQPSQ_PBLADDR));

	set_64bit_val(wqe,
		      16,
		      info->total_len |
		      LS_64(info->first_pm_pbl_index, I40IWQPSQ_FIRSTPMPBLIDXLO));

	header = LS_64(info->stag_key, I40IWQPSQ_STAGKEY) |
		 LS_64(info->stag_idx, I40IWQPSQ_STAGINDEX) |
		 LS_64(I40IWQP_OP_FAST_REGISTER, I40IWQPSQ_OPCODE) |
		 LS_64(info->chunk_size, I40IWQPSQ_LPBLSIZE) |
		 LS_64(page_size, I40IWQPSQ_HPAGESIZE) |
		 LS_64(info->access_rights, I40IWQPSQ_STAGRIGHTS) |
		 LS_64(info->addr_type, I40IWQPSQ_VABASEDTO) |
		 LS_64(info->read_fence, I40IWQPSQ_READFENCE) |
		 LS_64(info->local_fence, I40IWQPSQ_LOCALFENCE) |
		 LS_64(info->signaled, I40IWQPSQ_SIGCOMPL) |
		 LS_64(qp->qp_uk.swqe_polarity, I40IWQPSQ_VALID);

	i40iw_insert_wqe_hdr(wqe, header);

	i40iw_debug_buf(qp->dev, I40IW_DEBUG_WQE, "FAST_REG WQE",
			wqe, I40IW_QP_WQE_MIN_SIZE);

	if (post_sq)
		i40iw_qp_post_wr(&qp->qp_uk);
	return 0;
}

/**
 * i40iw_sc_send_lsmm - send last streaming mode message
 * @qp: sc qp struct
 * @lsmm_buf: buffer with lsmm message
 * @size: size of lsmm buffer
 * @stag: stag of lsmm buffer
 */
static void i40iw_sc_send_lsmm(struct i40iw_sc_qp *qp,
			       void *lsmm_buf,
			       u32 size,
			       i40iw_stag stag)
{
	u64 *wqe;
	u64 header;
	struct i40iw_qp_uk *qp_uk;

	qp_uk = &qp->qp_uk;
	wqe = qp_uk->sq_base->elem;

	set_64bit_val(wqe, 0, (uintptr_t)lsmm_buf);

	set_64bit_val(wqe, 8, (size | LS_64(stag, I40IWQPSQ_FRAG_STAG)));

	set_64bit_val(wqe, 16, 0);

	header = LS_64(I40IWQP_OP_RDMA_SEND, I40IWQPSQ_OPCODE) |
		 LS_64(1, I40IWQPSQ_STREAMMODE) |
		 LS_64(1, I40IWQPSQ_WAITFORRCVPDU) |
		 LS_64(qp->qp_uk.swqe_polarity, I40IWQPSQ_VALID);

	i40iw_insert_wqe_hdr(wqe, header);

	i40iw_debug_buf(qp->dev, I40IW_DEBUG_QP, "SEND_LSMM WQE",
			wqe, I40IW_QP_WQE_MIN_SIZE);
}

/**
 * i40iw_sc_send_lsmm_nostag - for privilege qp
 * @qp: sc qp struct
 * @lsmm_buf: buffer with lsmm message
 * @size: size of lsmm buffer
 */
static void i40iw_sc_send_lsmm_nostag(struct i40iw_sc_qp *qp,
				      void *lsmm_buf,
				      u32 size)
{
	u64 *wqe;
	u64 header;
	struct i40iw_qp_uk *qp_uk;

	qp_uk = &qp->qp_uk;
	wqe = qp_uk->sq_base->elem;

	set_64bit_val(wqe, 0, (uintptr_t)lsmm_buf);

	set_64bit_val(wqe, 8, size);

	set_64bit_val(wqe, 16, 0);

	header = LS_64(I40IWQP_OP_RDMA_SEND, I40IWQPSQ_OPCODE) |
		 LS_64(1, I40IWQPSQ_STREAMMODE) |
		 LS_64(1, I40IWQPSQ_WAITFORRCVPDU) |
		 LS_64(qp->qp_uk.swqe_polarity, I40IWQPSQ_VALID);

	i40iw_insert_wqe_hdr(wqe, header);

	i40iw_debug_buf(qp->dev, I40IW_DEBUG_WQE, "SEND_LSMM_NOSTAG WQE",
			wqe, I40IW_QP_WQE_MIN_SIZE);
}

/**
 * i40iw_sc_send_rtt - send last read0 or write0
 * @qp: sc qp struct
 * @read: Do read0 or write0
 */
static void i40iw_sc_send_rtt(struct i40iw_sc_qp *qp, bool read)
{
	u64 *wqe;
	u64 header;
	struct i40iw_qp_uk *qp_uk;

	qp_uk = &qp->qp_uk;
	wqe = qp_uk->sq_base->elem;

	set_64bit_val(wqe, 0, 0);
	set_64bit_val(wqe, 8, 0);
	set_64bit_val(wqe, 16, 0);
	if (read) {
		header = LS_64(0x1234, I40IWQPSQ_REMSTAG) |
			 LS_64(I40IWQP_OP_RDMA_READ, I40IWQPSQ_OPCODE) |
			 LS_64(qp->qp_uk.swqe_polarity, I40IWQPSQ_VALID);
		set_64bit_val(wqe, 8, ((u64)0xabcd << 32));
	} else {
		header = LS_64(I40IWQP_OP_RDMA_WRITE, I40IWQPSQ_OPCODE) |
			 LS_64(qp->qp_uk.swqe_polarity, I40IWQPSQ_VALID);
	}

	i40iw_insert_wqe_hdr(wqe, header);

	i40iw_debug_buf(qp->dev, I40IW_DEBUG_WQE, "RTR WQE",
			wqe, I40IW_QP_WQE_MIN_SIZE);
}

/**
 * i40iw_sc_post_wqe0 - send wqe with opcode
 * @qp: sc qp struct
 * @opcode: opcode to use for wqe0
 */
static enum i40iw_status_code i40iw_sc_post_wqe0(struct i40iw_sc_qp *qp, u8 opcode)
{
	u64 *wqe;
	u64 header;
	struct i40iw_qp_uk *qp_uk;

	qp_uk = &qp->qp_uk;
	wqe = qp_uk->sq_base->elem;

	if (!wqe)
		return I40IW_ERR_QP_TOOMANY_WRS_POSTED;
	switch (opcode) {
	case I40IWQP_OP_NOP:
		set_64bit_val(wqe, 0, 0);
		set_64bit_val(wqe, 8, 0);
		set_64bit_val(wqe, 16, 0);
		header = LS_64(I40IWQP_OP_NOP, I40IWQPSQ_OPCODE) |
			 LS_64(qp->qp_uk.swqe_polarity, I40IWQPSQ_VALID);

		i40iw_insert_wqe_hdr(wqe, header);
		break;
	case I40IWQP_OP_RDMA_SEND:
		set_64bit_val(wqe, 0, 0);
		set_64bit_val(wqe, 8, 0);
		set_64bit_val(wqe, 16, 0);
		header = LS_64(I40IWQP_OP_RDMA_SEND, I40IWQPSQ_OPCODE) |
			 LS_64(qp->qp_uk.swqe_polarity, I40IWQPSQ_VALID) |
			 LS_64(1, I40IWQPSQ_STREAMMODE) |
			 LS_64(1, I40IWQPSQ_WAITFORRCVPDU);

		i40iw_insert_wqe_hdr(wqe, header);
		break;
	default:
		i40iw_debug(qp->dev, I40IW_DEBUG_QP, "%s: Invalid WQE zero opcode\n",
			    __func__);
		break;
	}
	return 0;
}

/**
 * i40iw_sc_init_iw_hmc() - queries fpm values using cqp and populates hmc_info
 * @dev : ptr to i40iw_dev struct
 * @hmc_fn_id: hmc function id
 */
enum i40iw_status_code i40iw_sc_init_iw_hmc(struct i40iw_sc_dev *dev, u8 hmc_fn_id)
{
	struct i40iw_hmc_info *hmc_info;
	struct i40iw_dma_mem query_fpm_mem;
	struct i40iw_virt_mem virt_mem;
	struct i40iw_vfdev *vf_dev = NULL;
	u32 mem_size;
	enum i40iw_status_code ret_code = 0;
	bool poll_registers = true;
	u16 iw_vf_idx;
	u8 wait_type;

	if (hmc_fn_id >= I40IW_MAX_VF_FPM_ID ||
	    (dev->hmc_fn_id != hmc_fn_id && hmc_fn_id < I40IW_FIRST_VF_FPM_ID))
		return I40IW_ERR_INVALID_HMCFN_ID;

	i40iw_debug(dev, I40IW_DEBUG_HMC, "hmc_fn_id %u, dev->hmc_fn_id %u\n", hmc_fn_id,
		    dev->hmc_fn_id);
	if (hmc_fn_id == dev->hmc_fn_id) {
		hmc_info = dev->hmc_info;
		query_fpm_mem.pa = dev->fpm_query_buf_pa;
		query_fpm_mem.va = dev->fpm_query_buf;
	} else {
		vf_dev = i40iw_vfdev_from_fpm(dev, hmc_fn_id);
		if (!vf_dev)
			return I40IW_ERR_INVALID_VF_ID;

		hmc_info = &vf_dev->hmc_info;
		iw_vf_idx = vf_dev->iw_vf_idx;
		i40iw_debug(dev, I40IW_DEBUG_HMC, "vf_dev %p, hmc_info %p, hmc_obj %p\n", vf_dev,
			    hmc_info, hmc_info->hmc_obj);
		if (!vf_dev->fpm_query_buf) {
			if (!dev->vf_fpm_query_buf[iw_vf_idx].va) {
				ret_code = i40iw_alloc_query_fpm_buf(dev,
								     &dev->vf_fpm_query_buf[iw_vf_idx]);
				if (ret_code)
					return ret_code;
			}
			vf_dev->fpm_query_buf = dev->vf_fpm_query_buf[iw_vf_idx].va;
			vf_dev->fpm_query_buf_pa = dev->vf_fpm_query_buf[iw_vf_idx].pa;
		}
		query_fpm_mem.pa = vf_dev->fpm_query_buf_pa;
		query_fpm_mem.va = vf_dev->fpm_query_buf;
		/**
		 * It is HARDWARE specific:
		 * this call is done by PF for VF and
		 * i40iw_sc_query_fpm_values needs ccq poll
		 * because PF ccq is already created.
		 */
		poll_registers = false;
	}

	hmc_info->hmc_fn_id = hmc_fn_id;

	if (hmc_fn_id != dev->hmc_fn_id) {
		ret_code =
			i40iw_cqp_query_fpm_values_cmd(dev, &query_fpm_mem, hmc_fn_id);
	} else {
		wait_type = poll_registers ? (u8)I40IW_CQP_WAIT_POLL_REGS :
			    (u8)I40IW_CQP_WAIT_POLL_CQ;

		ret_code = i40iw_sc_query_fpm_values(
					dev->cqp,
					0,
					hmc_info->hmc_fn_id,
					&query_fpm_mem,
					true,
					wait_type);
	}
	if (ret_code)
		return ret_code;

	/* parse the fpm_query_buf and fill hmc obj info */
	ret_code =
		i40iw_sc_parse_fpm_query_buf((u64 *)query_fpm_mem.va,
					     hmc_info,
					     &dev->hmc_fpm_misc);
	if (ret_code)
		return ret_code;
	i40iw_debug_buf(dev, I40IW_DEBUG_HMC, "QUERY FPM BUFFER",
			query_fpm_mem.va, I40IW_QUERY_FPM_BUF_SIZE);

	if (hmc_fn_id != dev->hmc_fn_id) {
		i40iw_cqp_commit_fpm_values_cmd(dev, &query_fpm_mem, hmc_fn_id);

		/* parse the fpm_commit_buf and fill hmc obj info */
		i40iw_sc_parse_fpm_commit_buf((u64 *)query_fpm_mem.va, hmc_info->hmc_obj, &hmc_info->sd_table.sd_cnt);
		mem_size = sizeof(struct i40iw_hmc_sd_entry) *
			   (hmc_info->sd_table.sd_cnt + hmc_info->first_sd_index);
		ret_code = i40iw_allocate_virt_mem(dev->hw, &virt_mem, mem_size);
		if (ret_code)
			return ret_code;
		hmc_info->sd_table.sd_entry = virt_mem.va;
	}

	return ret_code;
}

/**
 * i40iw_sc_configure_iw_fpm() - commits hmc obj cnt values using cqp command and
 * populates fpm base address in hmc_info
 * @dev : ptr to i40iw_dev struct
 * @hmc_fn_id: hmc function id
 */
static enum i40iw_status_code i40iw_sc_configure_iw_fpm(struct i40iw_sc_dev *dev,
							u8 hmc_fn_id)
{
	struct i40iw_hmc_info *hmc_info;
	struct i40iw_hmc_obj_info *obj_info;
	u64 *buf;
	struct i40iw_dma_mem commit_fpm_mem;
	u32 i, j;
	enum i40iw_status_code ret_code = 0;
	bool poll_registers = true;
	u8 wait_type;

	if (hmc_fn_id >= I40IW_MAX_VF_FPM_ID ||
	    (dev->hmc_fn_id != hmc_fn_id && hmc_fn_id < I40IW_FIRST_VF_FPM_ID))
		return I40IW_ERR_INVALID_HMCFN_ID;

	if (hmc_fn_id == dev->hmc_fn_id) {
		hmc_info = dev->hmc_info;
	} else {
		hmc_info = i40iw_vf_hmcinfo_from_fpm(dev, hmc_fn_id);
		poll_registers = false;
	}
	if (!hmc_info)
		return I40IW_ERR_BAD_PTR;

	obj_info = hmc_info->hmc_obj;
	buf = dev->fpm_commit_buf;

	/* copy cnt values in commit buf */
	for (i = I40IW_HMC_IW_QP, j = 0; i <= I40IW_HMC_IW_PBLE;
	     i++, j += 8)
		set_64bit_val(buf, j, (u64)obj_info[i].cnt);

	set_64bit_val(buf, 40, 0);   /* APBVT rsvd */

	commit_fpm_mem.pa = dev->fpm_commit_buf_pa;
	commit_fpm_mem.va = dev->fpm_commit_buf;
	wait_type = poll_registers ? (u8)I40IW_CQP_WAIT_POLL_REGS :
			(u8)I40IW_CQP_WAIT_POLL_CQ;
	ret_code = i40iw_sc_commit_fpm_values(
					dev->cqp,
					0,
					hmc_info->hmc_fn_id,
					&commit_fpm_mem,
					true,
					wait_type);

	/* parse the fpm_commit_buf and fill hmc obj info */
	if (!ret_code)
		ret_code = i40iw_sc_parse_fpm_commit_buf(dev->fpm_commit_buf,
							 hmc_info->hmc_obj,
							 &hmc_info->sd_table.sd_cnt);

	i40iw_debug_buf(dev, I40IW_DEBUG_HMC, "COMMIT FPM BUFFER",
			commit_fpm_mem.va, I40IW_COMMIT_FPM_BUF_SIZE);

	return ret_code;
}

/**
 * cqp_sds_wqe_fill - fill cqp wqe doe sd
 * @cqp: struct for cqp hw
 * @info; sd info for wqe
 * @scratch: u64 saved to be used during cqp completion
 */
static enum i40iw_status_code cqp_sds_wqe_fill(struct i40iw_sc_cqp *cqp,
					       struct i40iw_update_sds_info *info,
					       u64 scratch)
{
	u64 data;
	u64 header;
	u64 *wqe;
	int mem_entries, wqe_entries;
	struct i40iw_dma_mem *sdbuf = &cqp->sdbuf;
	u64 offset;
	u32 wqe_idx;

	wqe = i40iw_sc_cqp_get_next_send_wqe_idx(cqp, scratch, &wqe_idx);
	if (!wqe)
		return I40IW_ERR_RING_FULL;

	I40IW_CQP_INIT_WQE(wqe);
	wqe_entries = (info->cnt > 3) ? 3 : info->cnt;
	mem_entries = info->cnt - wqe_entries;

	header = LS_64(I40IW_CQP_OP_UPDATE_PE_SDS, I40IW_CQPSQ_OPCODE) |
		 LS_64(cqp->polarity, I40IW_CQPSQ_WQEVALID) |
		 LS_64(mem_entries, I40IW_CQPSQ_UPESD_ENTRY_COUNT);

	if (mem_entries) {
		offset = wqe_idx * I40IW_UPDATE_SD_BUF_SIZE;
		memcpy((char *)sdbuf->va + offset, &info->entry[3],
		       mem_entries << 4);
		data = (u64)sdbuf->pa + offset;
	} else {
		data = 0;
	}
	data |= LS_64(info->hmc_fn_id, I40IW_CQPSQ_UPESD_HMCFNID);

	set_64bit_val(wqe, 16, data);

	switch (wqe_entries) {
	case 3:
		set_64bit_val(wqe, 48,
			      (LS_64(info->entry[2].cmd, I40IW_CQPSQ_UPESD_SDCMD) |
					LS_64(1, I40IW_CQPSQ_UPESD_ENTRY_VALID)));

		set_64bit_val(wqe, 56, info->entry[2].data);
		/* fallthrough */
	case 2:
		set_64bit_val(wqe, 32,
			      (LS_64(info->entry[1].cmd, I40IW_CQPSQ_UPESD_SDCMD) |
					LS_64(1, I40IW_CQPSQ_UPESD_ENTRY_VALID)));

		set_64bit_val(wqe, 40, info->entry[1].data);
		/* fallthrough */
	case 1:
		set_64bit_val(wqe, 0,
			      LS_64(info->entry[0].cmd, I40IW_CQPSQ_UPESD_SDCMD));

		set_64bit_val(wqe, 8, info->entry[0].data);
		break;
	default:
		break;
	}

	i40iw_insert_wqe_hdr(wqe, header);

	i40iw_debug_buf(cqp->dev, I40IW_DEBUG_WQE, "UPDATE_PE_SDS WQE",
			wqe, I40IW_CQP_WQE_SIZE * 8);
	return 0;
}

/**
 * i40iw_update_pe_sds - cqp wqe for sd
 * @dev: ptr to i40iw_dev struct
 * @info: sd info for sd's
 * @scratch: u64 saved to be used during cqp completion
 */
static enum i40iw_status_code i40iw_update_pe_sds(struct i40iw_sc_dev *dev,
						  struct i40iw_update_sds_info *info,
						  u64 scratch)
{
	struct i40iw_sc_cqp *cqp = dev->cqp;
	enum i40iw_status_code ret_code;

	ret_code = cqp_sds_wqe_fill(cqp, info, scratch);
	if (!ret_code)
		i40iw_sc_cqp_post_sq(cqp);

	return ret_code;
}

/**
 * i40iw_update_sds_noccq - update sd before ccq created
 * @dev: sc device struct
 * @info: sd info for sd's
 */
enum i40iw_status_code i40iw_update_sds_noccq(struct i40iw_sc_dev *dev,
					      struct i40iw_update_sds_info *info)
{
	u32 error, val, tail;
	struct i40iw_sc_cqp *cqp = dev->cqp;
	enum i40iw_status_code ret_code;

	ret_code = cqp_sds_wqe_fill(cqp, info, 0);
	if (ret_code)
		return ret_code;
	i40iw_get_cqp_reg_info(cqp, &val, &tail, &error);
	if (error)
		return I40IW_ERR_CQP_COMPL_ERROR;

	i40iw_sc_cqp_post_sq(cqp);
	ret_code = i40iw_cqp_poll_registers(cqp, tail, I40IW_DONE_COUNT);

	return ret_code;
}

/**
 * i40iw_sc_suspend_qp - suspend qp for param change
 * @cqp: struct for cqp hw
 * @qp: sc qp struct
 * @scratch: u64 saved to be used during cqp completion
 */
enum i40iw_status_code i40iw_sc_suspend_qp(struct i40iw_sc_cqp *cqp,
					   struct i40iw_sc_qp *qp,
					   u64 scratch)
{
	u64 header;
	u64 *wqe;

	wqe = i40iw_sc_cqp_get_next_send_wqe(cqp, scratch);
	if (!wqe)
		return I40IW_ERR_RING_FULL;
	header = LS_64(qp->qp_uk.qp_id, I40IW_CQPSQ_SUSPENDQP_QPID) |
		 LS_64(I40IW_CQP_OP_SUSPEND_QP, I40IW_CQPSQ_OPCODE) |
		 LS_64(cqp->polarity, I40IW_CQPSQ_WQEVALID);

	i40iw_insert_wqe_hdr(wqe, header);

	i40iw_debug_buf(cqp->dev, I40IW_DEBUG_WQE, "SUSPEND_QP WQE",
			wqe, I40IW_CQP_WQE_SIZE * 8);

	i40iw_sc_cqp_post_sq(cqp);
	return 0;
}

/**
 * i40iw_sc_resume_qp - resume qp after suspend
 * @cqp: struct for cqp hw
 * @qp: sc qp struct
 * @scratch: u64 saved to be used during cqp completion
 */
enum i40iw_status_code i40iw_sc_resume_qp(struct i40iw_sc_cqp *cqp,
					  struct i40iw_sc_qp *qp,
					  u64 scratch)
{
	u64 header;
	u64 *wqe;

	wqe = i40iw_sc_cqp_get_next_send_wqe(cqp, scratch);
	if (!wqe)
		return I40IW_ERR_RING_FULL;
	set_64bit_val(wqe,
		      16,
			LS_64(qp->qs_handle, I40IW_CQPSQ_RESUMEQP_QSHANDLE));

	header = LS_64(qp->qp_uk.qp_id, I40IW_CQPSQ_RESUMEQP_QPID) |
		 LS_64(I40IW_CQP_OP_RESUME_QP, I40IW_CQPSQ_OPCODE) |
		 LS_64(cqp->polarity, I40IW_CQPSQ_WQEVALID);

	i40iw_insert_wqe_hdr(wqe, header);

	i40iw_debug_buf(cqp->dev, I40IW_DEBUG_WQE, "RESUME_QP WQE",
			wqe, I40IW_CQP_WQE_SIZE * 8);

	i40iw_sc_cqp_post_sq(cqp);
	return 0;
}

/**
 * i40iw_sc_static_hmc_pages_allocated - cqp wqe to allocate hmc pages
 * @cqp: struct for cqp hw
 * @scratch: u64 saved to be used during cqp completion
 * @hmc_fn_id: hmc function id
 * @post_sq: flag for cqp db to ring
 * @poll_registers: flag to poll register for cqp completion
 */
enum i40iw_status_code i40iw_sc_static_hmc_pages_allocated(
					struct i40iw_sc_cqp *cqp,
					u64 scratch,
					u8 hmc_fn_id,
					bool post_sq,
					bool poll_registers)
{
	u64 header;
	u64 *wqe;
	u32 tail, val, error;
	enum i40iw_status_code ret_code = 0;

	wqe = i40iw_sc_cqp_get_next_send_wqe(cqp, scratch);
	if (!wqe)
		return I40IW_ERR_RING_FULL;
	set_64bit_val(wqe,
		      16,
		      LS_64(hmc_fn_id, I40IW_SHMC_PAGE_ALLOCATED_HMC_FN_ID));

	header = LS_64(I40IW_CQP_OP_SHMC_PAGES_ALLOCATED, I40IW_CQPSQ_OPCODE) |
		 LS_64(cqp->polarity, I40IW_CQPSQ_WQEVALID);

	i40iw_insert_wqe_hdr(wqe, header);

	i40iw_debug_buf(cqp->dev, I40IW_DEBUG_WQE, "SHMC_PAGES_ALLOCATED WQE",
			wqe, I40IW_CQP_WQE_SIZE * 8);
	i40iw_get_cqp_reg_info(cqp, &val, &tail, &error);
	if (error) {
		ret_code = I40IW_ERR_CQP_COMPL_ERROR;
		return ret_code;
	}
	if (post_sq) {
		i40iw_sc_cqp_post_sq(cqp);
		if (poll_registers)
			/* check for cqp sq tail update */
			ret_code = i40iw_cqp_poll_registers(cqp, tail, 1000);
		else
			ret_code = i40iw_sc_poll_for_cqp_op_done(cqp,
								 I40IW_CQP_OP_SHMC_PAGES_ALLOCATED,
								 NULL);
	}

	return ret_code;
}

/**
 * i40iw_ring_full - check if cqp ring is full
 * @cqp: struct for cqp hw
 */
static bool i40iw_ring_full(struct i40iw_sc_cqp *cqp)
{
	return I40IW_RING_FULL_ERR(cqp->sq_ring);
}

/**
 * i40iw_est_sd - returns approximate number of SDs for HMC
 * @dev: sc device struct
 * @hmc_info: hmc structure, size and count for HMC objects
 */
static u64 i40iw_est_sd(struct i40iw_sc_dev *dev, struct i40iw_hmc_info *hmc_info)
{
	int i;
	u64 size = 0;
	u64 sd;

	for (i = I40IW_HMC_IW_QP; i < I40IW_HMC_IW_PBLE; i++)
		size += hmc_info->hmc_obj[i].cnt * hmc_info->hmc_obj[i].size;

	if (dev->is_pf)
		size += hmc_info->hmc_obj[I40IW_HMC_IW_PBLE].cnt * hmc_info->hmc_obj[I40IW_HMC_IW_PBLE].size;

	if (size & 0x1FFFFF)
		sd = (size >> 21) + 1; /* add 1 for remainder */
	else
		sd = size >> 21;

	if (!dev->is_pf) {
		/* 2MB alignment for VF PBLE HMC */
		size = hmc_info->hmc_obj[I40IW_HMC_IW_PBLE].cnt * hmc_info->hmc_obj[I40IW_HMC_IW_PBLE].size;
		if (size & 0x1FFFFF)
			sd += (size >> 21) + 1; /* add 1 for remainder */
		else
			sd += size >> 21;
	}

	return sd;
}

/**
 * i40iw_config_fpm_values - configure HMC objects
 * @dev: sc device struct
 * @qp_count: desired qp count
 */
enum i40iw_status_code i40iw_config_fpm_values(struct i40iw_sc_dev *dev, u32 qp_count)
{
	struct i40iw_virt_mem virt_mem;
	u32 i, mem_size;
	u32 qpwantedoriginal, qpwanted, mrwanted, pblewanted;
	u64 sd_needed;
	u32 loop_count = 0;

	struct i40iw_hmc_info *hmc_info;
	struct i40iw_hmc_fpm_misc *hmc_fpm_misc;
	enum i40iw_status_code ret_code = 0;

	hmc_info = dev->hmc_info;
	hmc_fpm_misc = &dev->hmc_fpm_misc;

	ret_code = i40iw_sc_init_iw_hmc(dev, dev->hmc_fn_id);
	if (ret_code) {
		i40iw_debug(dev, I40IW_DEBUG_HMC,
			    "i40iw_sc_init_iw_hmc returned error_code = %d\n",
			    ret_code);
		return ret_code;
	}

	for (i = I40IW_HMC_IW_QP; i < I40IW_HMC_IW_MAX; i++)
		hmc_info->hmc_obj[i].cnt = hmc_info->hmc_obj[i].max_cnt;
	sd_needed = i40iw_est_sd(dev, hmc_info);
	i40iw_debug(dev, I40IW_DEBUG_HMC,
		    "%s: FW initial max sd_count[%08lld] first_sd_index[%04d]\n",
		    __func__, sd_needed, hmc_info->first_sd_index);
	i40iw_debug(dev, I40IW_DEBUG_HMC,
		    "%s: sd count %d where max sd is %d\n",
		    __func__, hmc_info->sd_table.sd_cnt,
		    hmc_fpm_misc->max_sds);

	qpwanted = min(qp_count, hmc_info->hmc_obj[I40IW_HMC_IW_QP].max_cnt);
	qpwantedoriginal = qpwanted;
	mrwanted = hmc_info->hmc_obj[I40IW_HMC_IW_MR].max_cnt;
	pblewanted = hmc_info->hmc_obj[I40IW_HMC_IW_PBLE].max_cnt;

	i40iw_debug(dev, I40IW_DEBUG_HMC,
		    "req_qp=%d max_sd=%d, max_qp = %d, max_cq=%d, max_mr=%d, max_pble=%d\n",
		    qp_count, hmc_fpm_misc->max_sds,
		    hmc_info->hmc_obj[I40IW_HMC_IW_QP].max_cnt,
		    hmc_info->hmc_obj[I40IW_HMC_IW_CQ].max_cnt,
		    hmc_info->hmc_obj[I40IW_HMC_IW_MR].max_cnt,
		    hmc_info->hmc_obj[I40IW_HMC_IW_PBLE].max_cnt);

	do {
		++loop_count;
		hmc_info->hmc_obj[I40IW_HMC_IW_QP].cnt = qpwanted;
		hmc_info->hmc_obj[I40IW_HMC_IW_CQ].cnt =
			min(2 * qpwanted, hmc_info->hmc_obj[I40IW_HMC_IW_CQ].cnt);
		hmc_info->hmc_obj[I40IW_HMC_IW_SRQ].cnt = 0x00; /* Reserved */
		hmc_info->hmc_obj[I40IW_HMC_IW_HTE].cnt =
					qpwanted * hmc_fpm_misc->ht_multiplier;
		hmc_info->hmc_obj[I40IW_HMC_IW_ARP].cnt =
			hmc_info->hmc_obj[I40IW_HMC_IW_ARP].max_cnt;
		hmc_info->hmc_obj[I40IW_HMC_IW_APBVT_ENTRY].cnt = 1;
		hmc_info->hmc_obj[I40IW_HMC_IW_MR].cnt = mrwanted;

		hmc_info->hmc_obj[I40IW_HMC_IW_XF].cnt =
			roundup_pow_of_two(I40IW_MAX_WQ_ENTRIES * qpwanted);
		hmc_info->hmc_obj[I40IW_HMC_IW_Q1].cnt =
			roundup_pow_of_two(2 * I40IW_MAX_IRD_SIZE * qpwanted);
		hmc_info->hmc_obj[I40IW_HMC_IW_XFFL].cnt =
			hmc_info->hmc_obj[I40IW_HMC_IW_XF].cnt / hmc_fpm_misc->xf_block_size;
		hmc_info->hmc_obj[I40IW_HMC_IW_Q1FL].cnt =
			hmc_info->hmc_obj[I40IW_HMC_IW_Q1].cnt / hmc_fpm_misc->q1_block_size;
		hmc_info->hmc_obj[I40IW_HMC_IW_TIMER].cnt =
			((qpwanted) / 512 + 1) * hmc_fpm_misc->timer_bucket;
		hmc_info->hmc_obj[I40IW_HMC_IW_FSIMC].cnt = 0x00;
		hmc_info->hmc_obj[I40IW_HMC_IW_FSIAV].cnt = 0x00;
		hmc_info->hmc_obj[I40IW_HMC_IW_PBLE].cnt = pblewanted;

		/* How much memory is needed for all the objects. */
		sd_needed = i40iw_est_sd(dev, hmc_info);
		if ((loop_count > 1000) ||
		    ((!(loop_count % 10)) &&
		    (qpwanted > qpwantedoriginal * 2 / 3))) {
			if (qpwanted > FPM_MULTIPLIER)
				qpwanted = roundup_pow_of_two(qpwanted -
							      FPM_MULTIPLIER);
			qpwanted >>= 1;
		}
		if (mrwanted > FPM_MULTIPLIER * 10)
			mrwanted -= FPM_MULTIPLIER * 10;
		if (pblewanted > FPM_MULTIPLIER * 1000)
			pblewanted -= FPM_MULTIPLIER * 1000;
	} while (sd_needed > hmc_fpm_misc->max_sds && loop_count < 2000);

	sd_needed = i40iw_est_sd(dev, hmc_info);

	i40iw_debug(dev, I40IW_DEBUG_HMC,
		    "loop_cnt=%d, sd_needed=%lld, qpcnt = %d, cqcnt=%d, mrcnt=%d, pblecnt=%d\n",
		    loop_count, sd_needed,
		    hmc_info->hmc_obj[I40IW_HMC_IW_QP].cnt,
		    hmc_info->hmc_obj[I40IW_HMC_IW_CQ].cnt,
		    hmc_info->hmc_obj[I40IW_HMC_IW_MR].cnt,
		    hmc_info->hmc_obj[I40IW_HMC_IW_PBLE].cnt);

	ret_code = i40iw_sc_configure_iw_fpm(dev, dev->hmc_fn_id);
	if (ret_code) {
		i40iw_debug(dev, I40IW_DEBUG_HMC,
			    "configure_iw_fpm returned error_code[x%08X]\n",
			    i40iw_rd32(dev->hw, dev->is_pf ? I40E_PFPE_CQPERRCODES : I40E_VFPE_CQPERRCODES1));
		return ret_code;
	}

	mem_size = sizeof(struct i40iw_hmc_sd_entry) *
		   (hmc_info->sd_table.sd_cnt + hmc_info->first_sd_index + 1);
	ret_code = i40iw_allocate_virt_mem(dev->hw, &virt_mem, mem_size);
	if (ret_code) {
		i40iw_debug(dev, I40IW_DEBUG_HMC,
			    "%s: failed to allocate memory for sd_entry buffer\n",
			    __func__);
		return ret_code;
	}
	hmc_info->sd_table.sd_entry = virt_mem.va;

	return ret_code;
}

/**
 * i40iw_exec_cqp_cmd - execute cqp cmd when wqe are available
 * @dev: rdma device
 * @pcmdinfo: cqp command info
 */
static enum i40iw_status_code i40iw_exec_cqp_cmd(struct i40iw_sc_dev *dev,
						 struct cqp_commands_info *pcmdinfo)
{
	enum i40iw_status_code status;
	struct i40iw_dma_mem values_mem;

	dev->cqp_cmd_stats[pcmdinfo->cqp_cmd]++;
	switch (pcmdinfo->cqp_cmd) {
	case OP_DELETE_LOCAL_MAC_IPADDR_ENTRY:
		status = i40iw_sc_del_local_mac_ipaddr_entry(
				pcmdinfo->in.u.del_local_mac_ipaddr_entry.cqp,
				pcmdinfo->in.u.del_local_mac_ipaddr_entry.scratch,
				pcmdinfo->in.u.del_local_mac_ipaddr_entry.entry_idx,
				pcmdinfo->in.u.del_local_mac_ipaddr_entry.ignore_ref_count,
				pcmdinfo->post_sq);
		break;
	case OP_CEQ_DESTROY:
		status = i40iw_sc_ceq_destroy(pcmdinfo->in.u.ceq_destroy.ceq,
					      pcmdinfo->in.u.ceq_destroy.scratch,
					      pcmdinfo->post_sq);
		break;
	case OP_AEQ_DESTROY:
		status = i40iw_sc_aeq_destroy(pcmdinfo->in.u.aeq_destroy.aeq,
					      pcmdinfo->in.u.aeq_destroy.scratch,
					      pcmdinfo->post_sq);

		break;
	case OP_DELETE_ARP_CACHE_ENTRY:
		status = i40iw_sc_del_arp_cache_entry(
				pcmdinfo->in.u.del_arp_cache_entry.cqp,
				pcmdinfo->in.u.del_arp_cache_entry.scratch,
				pcmdinfo->in.u.del_arp_cache_entry.arp_index,
				pcmdinfo->post_sq);
		break;
	case OP_MANAGE_APBVT_ENTRY:
		status = i40iw_sc_manage_apbvt_entry(
				pcmdinfo->in.u.manage_apbvt_entry.cqp,
				&pcmdinfo->in.u.manage_apbvt_entry.info,
				pcmdinfo->in.u.manage_apbvt_entry.scratch,
				pcmdinfo->post_sq);
		break;
	case OP_CEQ_CREATE:
		status = i40iw_sc_ceq_create(pcmdinfo->in.u.ceq_create.ceq,
					     pcmdinfo->in.u.ceq_create.scratch,
					     pcmdinfo->post_sq);
		break;
	case OP_AEQ_CREATE:
		status = i40iw_sc_aeq_create(pcmdinfo->in.u.aeq_create.aeq,
					     pcmdinfo->in.u.aeq_create.scratch,
					     pcmdinfo->post_sq);
		break;
	case OP_ALLOC_LOCAL_MAC_IPADDR_ENTRY:
		status = i40iw_sc_alloc_local_mac_ipaddr_entry(
				pcmdinfo->in.u.alloc_local_mac_ipaddr_entry.cqp,
				pcmdinfo->in.u.alloc_local_mac_ipaddr_entry.scratch,
				pcmdinfo->post_sq);
		break;
	case OP_ADD_LOCAL_MAC_IPADDR_ENTRY:
		status = i40iw_sc_add_local_mac_ipaddr_entry(
				pcmdinfo->in.u.add_local_mac_ipaddr_entry.cqp,
				&pcmdinfo->in.u.add_local_mac_ipaddr_entry.info,
				pcmdinfo->in.u.add_local_mac_ipaddr_entry.scratch,
				pcmdinfo->post_sq);
		break;
	case OP_MANAGE_QHASH_TABLE_ENTRY:
		status = i40iw_sc_manage_qhash_table_entry(
				pcmdinfo->in.u.manage_qhash_table_entry.cqp,
				&pcmdinfo->in.u.manage_qhash_table_entry.info,
				pcmdinfo->in.u.manage_qhash_table_entry.scratch,
				pcmdinfo->post_sq);

		break;
	case OP_QP_MODIFY:
		status = i40iw_sc_qp_modify(
				pcmdinfo->in.u.qp_modify.qp,
				&pcmdinfo->in.u.qp_modify.info,
				pcmdinfo->in.u.qp_modify.scratch,
				pcmdinfo->post_sq);

		break;
	case OP_QP_UPLOAD_CONTEXT:
		status = i40iw_sc_qp_upload_context(
				pcmdinfo->in.u.qp_upload_context.dev,
				&pcmdinfo->in.u.qp_upload_context.info,
				pcmdinfo->in.u.qp_upload_context.scratch,
				pcmdinfo->post_sq);

		break;
	case OP_CQ_CREATE:
		status = i40iw_sc_cq_create(
				pcmdinfo->in.u.cq_create.cq,
				pcmdinfo->in.u.cq_create.scratch,
				pcmdinfo->in.u.cq_create.check_overflow,
				pcmdinfo->post_sq);
		break;
	case OP_CQ_DESTROY:
		status = i40iw_sc_cq_destroy(
				pcmdinfo->in.u.cq_destroy.cq,
				pcmdinfo->in.u.cq_destroy.scratch,
				pcmdinfo->post_sq);

		break;
	case OP_QP_CREATE:
		status = i40iw_sc_qp_create(
				pcmdinfo->in.u.qp_create.qp,
				&pcmdinfo->in.u.qp_create.info,
				pcmdinfo->in.u.qp_create.scratch,
				pcmdinfo->post_sq);
		break;
	case OP_QP_DESTROY:
		status = i40iw_sc_qp_destroy(
				pcmdinfo->in.u.qp_destroy.qp,
				pcmdinfo->in.u.qp_destroy.scratch,
				pcmdinfo->in.u.qp_destroy.remove_hash_idx,
				pcmdinfo->in.u.qp_destroy.
				ignore_mw_bnd,
				pcmdinfo->post_sq);

		break;
	case OP_ALLOC_STAG:
		status = i40iw_sc_alloc_stag(
				pcmdinfo->in.u.alloc_stag.dev,
				&pcmdinfo->in.u.alloc_stag.info,
				pcmdinfo->in.u.alloc_stag.scratch,
				pcmdinfo->post_sq);
		break;
	case OP_MR_REG_NON_SHARED:
		status = i40iw_sc_mr_reg_non_shared(
				pcmdinfo->in.u.mr_reg_non_shared.dev,
				&pcmdinfo->in.u.mr_reg_non_shared.info,
				pcmdinfo->in.u.mr_reg_non_shared.scratch,
				pcmdinfo->post_sq);

		break;
	case OP_DEALLOC_STAG:
		status = i40iw_sc_dealloc_stag(
				pcmdinfo->in.u.dealloc_stag.dev,
				&pcmdinfo->in.u.dealloc_stag.info,
				pcmdinfo->in.u.dealloc_stag.scratch,
				pcmdinfo->post_sq);

		break;
	case OP_MW_ALLOC:
		status = i40iw_sc_mw_alloc(
				pcmdinfo->in.u.mw_alloc.dev,
				pcmdinfo->in.u.mw_alloc.scratch,
				pcmdinfo->in.u.mw_alloc.mw_stag_index,
				pcmdinfo->in.u.mw_alloc.pd_id,
				pcmdinfo->post_sq);

		break;
	case OP_QP_FLUSH_WQES:
		status = i40iw_sc_qp_flush_wqes(
				pcmdinfo->in.u.qp_flush_wqes.qp,
				&pcmdinfo->in.u.qp_flush_wqes.info,
				pcmdinfo->in.u.qp_flush_wqes.
				scratch, pcmdinfo->post_sq);
		break;
	case OP_ADD_ARP_CACHE_ENTRY:
		status = i40iw_sc_add_arp_cache_entry(
				pcmdinfo->in.u.add_arp_cache_entry.cqp,
				&pcmdinfo->in.u.add_arp_cache_entry.info,
				pcmdinfo->in.u.add_arp_cache_entry.scratch,
				pcmdinfo->post_sq);
		break;
	case OP_MANAGE_PUSH_PAGE:
		status = i40iw_sc_manage_push_page(
				pcmdinfo->in.u.manage_push_page.cqp,
				&pcmdinfo->in.u.manage_push_page.info,
				pcmdinfo->in.u.manage_push_page.scratch,
				pcmdinfo->post_sq);
		break;
	case OP_UPDATE_PE_SDS:
		/* case I40IW_CQP_OP_UPDATE_PE_SDS */
		status = i40iw_update_pe_sds(
				pcmdinfo->in.u.update_pe_sds.dev,
				&pcmdinfo->in.u.update_pe_sds.info,
				pcmdinfo->in.u.update_pe_sds.
				scratch);

		break;
	case OP_MANAGE_HMC_PM_FUNC_TABLE:
		status = i40iw_sc_manage_hmc_pm_func_table(
				pcmdinfo->in.u.manage_hmc_pm.dev->cqp,
				pcmdinfo->in.u.manage_hmc_pm.scratch,
				(u8)pcmdinfo->in.u.manage_hmc_pm.info.vf_id,
				pcmdinfo->in.u.manage_hmc_pm.info.free_fcn,
				true);
		break;
	case OP_SUSPEND:
		status = i40iw_sc_suspend_qp(
				pcmdinfo->in.u.suspend_resume.cqp,
				pcmdinfo->in.u.suspend_resume.qp,
				pcmdinfo->in.u.suspend_resume.scratch);
		break;
	case OP_RESUME:
		status = i40iw_sc_resume_qp(
				pcmdinfo->in.u.suspend_resume.cqp,
				pcmdinfo->in.u.suspend_resume.qp,
				pcmdinfo->in.u.suspend_resume.scratch);
		break;
	case OP_MANAGE_VF_PBLE_BP:
		status = i40iw_manage_vf_pble_bp(
				pcmdinfo->in.u.manage_vf_pble_bp.cqp,
				&pcmdinfo->in.u.manage_vf_pble_bp.info,
				pcmdinfo->in.u.manage_vf_pble_bp.scratch, true);
		break;
	case OP_QUERY_FPM_VALUES:
		values_mem.pa = pcmdinfo->in.u.query_fpm_values.fpm_values_pa;
		values_mem.va = pcmdinfo->in.u.query_fpm_values.fpm_values_va;
		status = i40iw_sc_query_fpm_values(
				pcmdinfo->in.u.query_fpm_values.cqp,
				pcmdinfo->in.u.query_fpm_values.scratch,
				pcmdinfo->in.u.query_fpm_values.hmc_fn_id,
				&values_mem, true, I40IW_CQP_WAIT_EVENT);
		break;
	case OP_COMMIT_FPM_VALUES:
		values_mem.pa = pcmdinfo->in.u.commit_fpm_values.fpm_values_pa;
		values_mem.va = pcmdinfo->in.u.commit_fpm_values.fpm_values_va;
		status = i40iw_sc_commit_fpm_values(
				pcmdinfo->in.u.commit_fpm_values.cqp,
				pcmdinfo->in.u.commit_fpm_values.scratch,
				pcmdinfo->in.u.commit_fpm_values.hmc_fn_id,
				&values_mem,
				true,
				I40IW_CQP_WAIT_EVENT);
		break;
	default:
		status = I40IW_NOT_SUPPORTED;
		break;
	}

	return status;
}

/**
 * i40iw_process_cqp_cmd - process all cqp commands
 * @dev: sc device struct
 * @pcmdinfo: cqp command info
 */
enum i40iw_status_code i40iw_process_cqp_cmd(struct i40iw_sc_dev *dev,
					     struct cqp_commands_info *pcmdinfo)
{
	enum i40iw_status_code status = 0;
	unsigned long flags;

	spin_lock_irqsave(&dev->cqp_lock, flags);
	if (list_empty(&dev->cqp_cmd_head) && !i40iw_ring_full(dev->cqp))
		status = i40iw_exec_cqp_cmd(dev, pcmdinfo);
	else
		list_add_tail(&pcmdinfo->cqp_cmd_entry, &dev->cqp_cmd_head);
	spin_unlock_irqrestore(&dev->cqp_lock, flags);
	return status;
}

/**
 * i40iw_process_bh - called from tasklet for cqp list
 * @dev: sc device struct
 */
enum i40iw_status_code i40iw_process_bh(struct i40iw_sc_dev *dev)
{
	enum i40iw_status_code status = 0;
	struct cqp_commands_info *pcmdinfo;
	unsigned long flags;

	spin_lock_irqsave(&dev->cqp_lock, flags);
	while (!list_empty(&dev->cqp_cmd_head) && !i40iw_ring_full(dev->cqp)) {
		pcmdinfo = (struct cqp_commands_info *)i40iw_remove_head(&dev->cqp_cmd_head);

		status = i40iw_exec_cqp_cmd(dev, pcmdinfo);
		if (status)
			break;
	}
	spin_unlock_irqrestore(&dev->cqp_lock, flags);
	return status;
}

/**
 * i40iw_iwarp_opcode - determine if incoming is rdma layer
 * @info: aeq info for the packet
 * @pkt: packet for error
 */
static u32 i40iw_iwarp_opcode(struct i40iw_aeqe_info *info, u8 *pkt)
{
	__be16 *mpa;
	u32 opcode = 0xffffffff;

	if (info->q2_data_written) {
		mpa = (__be16 *)pkt;
		opcode = ntohs(mpa[1]) & 0xf;
	}
	return opcode;
}

/**
 * i40iw_locate_mpa - return pointer to mpa in the pkt
 * @pkt: packet with data
 */
static u8 *i40iw_locate_mpa(u8 *pkt)
{
	/* skip over ethernet header */
	pkt += I40IW_MAC_HLEN;

	/* Skip over IP and TCP headers */
	pkt += 4 * (pkt[0] & 0x0f);
	pkt += 4 * ((pkt[12] >> 4) & 0x0f);
	return pkt;
}

/**
 * i40iw_setup_termhdr - termhdr for terminate pkt
 * @qp: sc qp ptr for pkt
 * @hdr: term hdr
 * @opcode: flush opcode for termhdr
 * @layer_etype: error layer + error type
 * @err: error cod ein the header
 */
static void i40iw_setup_termhdr(struct i40iw_sc_qp *qp,
				struct i40iw_terminate_hdr *hdr,
				enum i40iw_flush_opcode opcode,
				u8 layer_etype,
				u8 err)
{
	qp->flush_code = opcode;
	hdr->layer_etype = layer_etype;
	hdr->error_code = err;
}

/**
 * i40iw_bld_terminate_hdr - build terminate message header
 * @qp: qp associated with received terminate AE
 * @info: the struct contiaing AE information
 */
static int i40iw_bld_terminate_hdr(struct i40iw_sc_qp *qp,
				   struct i40iw_aeqe_info *info)
{
	u8 *pkt = qp->q2_buf + Q2_BAD_FRAME_OFFSET;
	u16 ddp_seg_len;
	int copy_len = 0;
	u8 is_tagged = 0;
	u32 opcode;
	struct i40iw_terminate_hdr *termhdr;

	termhdr = (struct i40iw_terminate_hdr *)qp->q2_buf;
	memset(termhdr, 0, Q2_BAD_FRAME_OFFSET);

	if (info->q2_data_written) {
		/* Use data from offending packet to fill in ddp & rdma hdrs */
		pkt = i40iw_locate_mpa(pkt);
		ddp_seg_len = ntohs(*(__be16 *)pkt);
		if (ddp_seg_len) {
			copy_len = 2;
			termhdr->hdrct = DDP_LEN_FLAG;
			if (pkt[2] & 0x80) {
				is_tagged = 1;
				if (ddp_seg_len >= TERM_DDP_LEN_TAGGED) {
					copy_len += TERM_DDP_LEN_TAGGED;
					termhdr->hdrct |= DDP_HDR_FLAG;
				}
			} else {
				if (ddp_seg_len >= TERM_DDP_LEN_UNTAGGED) {
					copy_len += TERM_DDP_LEN_UNTAGGED;
					termhdr->hdrct |= DDP_HDR_FLAG;
				}

				if (ddp_seg_len >= (TERM_DDP_LEN_UNTAGGED + TERM_RDMA_LEN)) {
					if ((pkt[3] & RDMA_OPCODE_MASK) == RDMA_READ_REQ_OPCODE) {
						copy_len += TERM_RDMA_LEN;
						termhdr->hdrct |= RDMA_HDR_FLAG;
					}
				}
			}
		}
	}

	opcode = i40iw_iwarp_opcode(info, pkt);

	switch (info->ae_id) {
	case I40IW_AE_AMP_UNALLOCATED_STAG:
		qp->eventtype = TERM_EVENT_QP_ACCESS_ERR;
		if (opcode == I40IW_OP_TYPE_RDMA_WRITE)
			i40iw_setup_termhdr(qp, termhdr, FLUSH_PROT_ERR,
					    (LAYER_DDP << 4) | DDP_TAGGED_BUFFER, DDP_TAGGED_INV_STAG);
		else
			i40iw_setup_termhdr(qp, termhdr, FLUSH_REM_ACCESS_ERR,
					    (LAYER_RDMA << 4) | RDMAP_REMOTE_PROT, RDMAP_INV_STAG);
		break;
	case I40IW_AE_AMP_BOUNDS_VIOLATION:
		qp->eventtype = TERM_EVENT_QP_ACCESS_ERR;
		if (info->q2_data_written)
			i40iw_setup_termhdr(qp, termhdr, FLUSH_PROT_ERR,
					    (LAYER_DDP << 4) | DDP_TAGGED_BUFFER, DDP_TAGGED_BOUNDS);
		else
			i40iw_setup_termhdr(qp, termhdr, FLUSH_REM_ACCESS_ERR,
					    (LAYER_RDMA << 4) | RDMAP_REMOTE_PROT, RDMAP_INV_BOUNDS);
		break;
	case I40IW_AE_AMP_BAD_PD:
		switch (opcode) {
		case I40IW_OP_TYPE_RDMA_WRITE:
			i40iw_setup_termhdr(qp, termhdr, FLUSH_PROT_ERR,
					    (LAYER_DDP << 4) | DDP_TAGGED_BUFFER, DDP_TAGGED_UNASSOC_STAG);
			break;
		case I40IW_OP_TYPE_SEND_INV:
		case I40IW_OP_TYPE_SEND_SOL_INV:
			i40iw_setup_termhdr(qp, termhdr, FLUSH_REM_ACCESS_ERR,
					    (LAYER_RDMA << 4) | RDMAP_REMOTE_PROT, RDMAP_CANT_INV_STAG);
			break;
		default:
			i40iw_setup_termhdr(qp, termhdr, FLUSH_REM_ACCESS_ERR,
					    (LAYER_RDMA << 4) | RDMAP_REMOTE_PROT, RDMAP_UNASSOC_STAG);
		}
		break;
	case I40IW_AE_AMP_INVALID_STAG:
		qp->eventtype = TERM_EVENT_QP_ACCESS_ERR;
		i40iw_setup_termhdr(qp, termhdr, FLUSH_REM_ACCESS_ERR,
				    (LAYER_RDMA << 4) | RDMAP_REMOTE_PROT, RDMAP_INV_STAG);
		break;
	case I40IW_AE_AMP_BAD_QP:
		i40iw_setup_termhdr(qp, termhdr, FLUSH_LOC_QP_OP_ERR,
				    (LAYER_DDP << 4) | DDP_UNTAGGED_BUFFER, DDP_UNTAGGED_INV_QN);
		break;
	case I40IW_AE_AMP_BAD_STAG_KEY:
	case I40IW_AE_AMP_BAD_STAG_INDEX:
		qp->eventtype = TERM_EVENT_QP_ACCESS_ERR;
		switch (opcode) {
		case I40IW_OP_TYPE_SEND_INV:
		case I40IW_OP_TYPE_SEND_SOL_INV:
			i40iw_setup_termhdr(qp, termhdr, FLUSH_REM_OP_ERR,
					    (LAYER_RDMA << 4) | RDMAP_REMOTE_OP, RDMAP_CANT_INV_STAG);
			break;
		default:
			i40iw_setup_termhdr(qp, termhdr, FLUSH_REM_ACCESS_ERR,
					    (LAYER_RDMA << 4) | RDMAP_REMOTE_OP, RDMAP_INV_STAG);
		}
		break;
	case I40IW_AE_AMP_RIGHTS_VIOLATION:
	case I40IW_AE_AMP_INVALIDATE_NO_REMOTE_ACCESS_RIGHTS:
	case I40IW_AE_PRIV_OPERATION_DENIED:
		qp->eventtype = TERM_EVENT_QP_ACCESS_ERR;
		i40iw_setup_termhdr(qp, termhdr, FLUSH_REM_ACCESS_ERR,
				    (LAYER_RDMA << 4) | RDMAP_REMOTE_PROT, RDMAP_ACCESS);
		break;
	case I40IW_AE_AMP_TO_WRAP:
		qp->eventtype = TERM_EVENT_QP_ACCESS_ERR;
		i40iw_setup_termhdr(qp, termhdr, FLUSH_REM_ACCESS_ERR,
				    (LAYER_RDMA << 4) | RDMAP_REMOTE_PROT, RDMAP_TO_WRAP);
		break;
	case I40IW_AE_LLP_RECEIVED_MPA_CRC_ERROR:
		i40iw_setup_termhdr(qp, termhdr, FLUSH_GENERAL_ERR,
				    (LAYER_MPA << 4) | DDP_LLP, MPA_CRC);
		break;
	case I40IW_AE_LLP_SEGMENT_TOO_LARGE:
	case I40IW_AE_LLP_SEGMENT_TOO_SMALL:
		i40iw_setup_termhdr(qp, termhdr, FLUSH_LOC_LEN_ERR,
				    (LAYER_DDP << 4) | DDP_CATASTROPHIC, DDP_CATASTROPHIC_LOCAL);
		break;
	case I40IW_AE_LCE_QP_CATASTROPHIC:
	case I40IW_AE_DDP_NO_L_BIT:
		i40iw_setup_termhdr(qp, termhdr, FLUSH_FATAL_ERR,
				    (LAYER_DDP << 4) | DDP_CATASTROPHIC, DDP_CATASTROPHIC_LOCAL);
		break;
	case I40IW_AE_DDP_INVALID_MSN_GAP_IN_MSN:
		i40iw_setup_termhdr(qp, termhdr, FLUSH_GENERAL_ERR,
				    (LAYER_DDP << 4) | DDP_UNTAGGED_BUFFER, DDP_UNTAGGED_INV_MSN_RANGE);
		break;
	case I40IW_AE_DDP_UBE_DDP_MESSAGE_TOO_LONG_FOR_AVAILABLE_BUFFER:
		qp->eventtype = TERM_EVENT_QP_ACCESS_ERR;
		i40iw_setup_termhdr(qp, termhdr, FLUSH_LOC_LEN_ERR,
				    (LAYER_DDP << 4) | DDP_UNTAGGED_BUFFER, DDP_UNTAGGED_INV_TOO_LONG);
		break;
	case I40IW_AE_DDP_UBE_INVALID_DDP_VERSION:
		if (is_tagged)
			i40iw_setup_termhdr(qp, termhdr, FLUSH_GENERAL_ERR,
					    (LAYER_DDP << 4) | DDP_TAGGED_BUFFER, DDP_TAGGED_INV_DDP_VER);
		else
			i40iw_setup_termhdr(qp, termhdr, FLUSH_GENERAL_ERR,
					    (LAYER_DDP << 4) | DDP_UNTAGGED_BUFFER, DDP_UNTAGGED_INV_DDP_VER);
		break;
	case I40IW_AE_DDP_UBE_INVALID_MO:
		i40iw_setup_termhdr(qp, termhdr, FLUSH_GENERAL_ERR,
				    (LAYER_DDP << 4) | DDP_UNTAGGED_BUFFER, DDP_UNTAGGED_INV_MO);
		break;
	case I40IW_AE_DDP_UBE_INVALID_MSN_NO_BUFFER_AVAILABLE:
		i40iw_setup_termhdr(qp, termhdr, FLUSH_REM_OP_ERR,
				    (LAYER_DDP << 4) | DDP_UNTAGGED_BUFFER, DDP_UNTAGGED_INV_MSN_NO_BUF);
		break;
	case I40IW_AE_DDP_UBE_INVALID_QN:
		i40iw_setup_termhdr(qp, termhdr, FLUSH_GENERAL_ERR,
				    (LAYER_DDP << 4) | DDP_UNTAGGED_BUFFER, DDP_UNTAGGED_INV_QN);
		break;
	case I40IW_AE_RDMAP_ROE_INVALID_RDMAP_VERSION:
		i40iw_setup_termhdr(qp, termhdr, FLUSH_GENERAL_ERR,
				    (LAYER_RDMA << 4) | RDMAP_REMOTE_OP, RDMAP_INV_RDMAP_VER);
		break;
	case I40IW_AE_RDMAP_ROE_UNEXPECTED_OPCODE:
		i40iw_setup_termhdr(qp, termhdr, FLUSH_LOC_QP_OP_ERR,
				    (LAYER_RDMA << 4) | RDMAP_REMOTE_OP, RDMAP_UNEXPECTED_OP);
		break;
	default:
		i40iw_setup_termhdr(qp, termhdr, FLUSH_FATAL_ERR,
				    (LAYER_RDMA << 4) | RDMAP_REMOTE_OP, RDMAP_UNSPECIFIED);
		break;
	}

	if (copy_len)
		memcpy(termhdr + 1, pkt, copy_len);

	return sizeof(struct i40iw_terminate_hdr) + copy_len;
}

/**
 * i40iw_terminate_send_fin() - Send fin for terminate message
 * @qp: qp associated with received terminate AE
 */
void i40iw_terminate_send_fin(struct i40iw_sc_qp *qp)
{
	/* Send the fin only */
	i40iw_term_modify_qp(qp,
			     I40IW_QP_STATE_TERMINATE,
			     I40IWQP_TERM_SEND_FIN_ONLY,
			     0);
}

/**
 * i40iw_terminate_connection() - Bad AE and send terminate to remote QP
 * @qp: qp associated with received terminate AE
 * @info: the struct contiaing AE information
 */
void i40iw_terminate_connection(struct i40iw_sc_qp *qp, struct i40iw_aeqe_info *info)
{
	u8 termlen = 0;

	if (qp->term_flags & I40IW_TERM_SENT)
		return;         /* Sanity check */

	/* Eventtype can change from bld_terminate_hdr */
	qp->eventtype = TERM_EVENT_QP_FATAL;
	termlen = i40iw_bld_terminate_hdr(qp, info);
	i40iw_terminate_start_timer(qp);
	qp->term_flags |= I40IW_TERM_SENT;
	i40iw_term_modify_qp(qp, I40IW_QP_STATE_TERMINATE,
			     I40IWQP_TERM_SEND_TERM_ONLY, termlen);
}

/**
 * i40iw_terminate_received - handle terminate received AE
 * @qp: qp associated with received terminate AE
 * @info: the struct contiaing AE information
 */
void i40iw_terminate_received(struct i40iw_sc_qp *qp, struct i40iw_aeqe_info *info)
{
	u8 *pkt = qp->q2_buf + Q2_BAD_FRAME_OFFSET;
	__be32 *mpa;
	u8 ddp_ctl;
	u8 rdma_ctl;
	u16 aeq_id = 0;
	struct i40iw_terminate_hdr *termhdr;

	mpa = (__be32 *)i40iw_locate_mpa(pkt);
	if (info->q2_data_written) {
		/* did not validate the frame - do it now */
		ddp_ctl = (ntohl(mpa[0]) >> 8) & 0xff;
		rdma_ctl = ntohl(mpa[0]) & 0xff;
		if ((ddp_ctl & 0xc0) != 0x40)
			aeq_id = I40IW_AE_LCE_QP_CATASTROPHIC;
		else if ((ddp_ctl & 0x03) != 1)
			aeq_id = I40IW_AE_DDP_UBE_INVALID_DDP_VERSION;
		else if (ntohl(mpa[2]) != 2)
			aeq_id = I40IW_AE_DDP_UBE_INVALID_QN;
		else if (ntohl(mpa[3]) != 1)
			aeq_id = I40IW_AE_DDP_INVALID_MSN_GAP_IN_MSN;
		else if (ntohl(mpa[4]) != 0)
			aeq_id = I40IW_AE_DDP_UBE_INVALID_MO;
		else if ((rdma_ctl & 0xc0) != 0x40)
			aeq_id = I40IW_AE_RDMAP_ROE_INVALID_RDMAP_VERSION;

		info->ae_id = aeq_id;
		if (info->ae_id) {
			/* Bad terminate recvd - send back a terminate */
			i40iw_terminate_connection(qp, info);
			return;
		}
	}

	qp->term_flags |= I40IW_TERM_RCVD;
	qp->eventtype = TERM_EVENT_QP_FATAL;
	termhdr = (struct i40iw_terminate_hdr *)&mpa[5];
	if (termhdr->layer_etype == RDMAP_REMOTE_PROT ||
	    termhdr->layer_etype == RDMAP_REMOTE_OP) {
		i40iw_terminate_done(qp, 0);
	} else {
		i40iw_terminate_start_timer(qp);
		i40iw_terminate_send_fin(qp);
	}
}

/**
 * i40iw_sc_vsi_init - Initialize virtual device
 * @vsi: pointer to the vsi structure
 * @info: parameters to initialize vsi
 **/
void i40iw_sc_vsi_init(struct i40iw_sc_vsi *vsi, struct i40iw_vsi_init_info *info)
{
	int i;

	vsi->dev = info->dev;
	vsi->back_vsi = info->back_vsi;
	vsi->mtu = info->params->mtu;
	vsi->exception_lan_queue = info->exception_lan_queue;
	i40iw_fill_qos_list(info->params->qs_handle_list);

	for (i = 0; i < I40IW_MAX_USER_PRIORITY; i++) {
		vsi->qos[i].qs_handle = info->params->qs_handle_list[i];
		i40iw_debug(vsi->dev, I40IW_DEBUG_DCB, "qset[%d]: %d\n", i,
			    vsi->qos[i].qs_handle);
		spin_lock_init(&vsi->qos[i].lock);
		INIT_LIST_HEAD(&vsi->qos[i].qplist);
	}
}

/**
 * i40iw_hw_stats_init - Initiliaze HW stats table
 * @stats: pestat struct
 * @fcn_idx: PCI fn id
 * @is_pf: Is it a PF?
 *
 * Populate the HW stats table with register offset addr for each
 * stats. And start the perioidic stats timer.
 */
void i40iw_hw_stats_init(struct i40iw_vsi_pestat *stats, u8 fcn_idx, bool is_pf)
{
	u32 stats_reg_offset;
	u32 stats_index;
	struct i40iw_dev_hw_stats_offsets *stats_table =
		&stats->hw_stats_offsets;
	struct i40iw_dev_hw_stats *last_rd_stats = &stats->last_read_hw_stats;

	if (is_pf) {
		stats_table->stats_offset_32[I40IW_HW_STAT_INDEX_IP4RXDISCARD] =
				I40E_GLPES_PFIP4RXDISCARD(fcn_idx);
		stats_table->stats_offset_32[I40IW_HW_STAT_INDEX_IP4RXTRUNC] =
				I40E_GLPES_PFIP4RXTRUNC(fcn_idx);
		stats_table->stats_offset_32[I40IW_HW_STAT_INDEX_IP4TXNOROUTE] =
				I40E_GLPES_PFIP4TXNOROUTE(fcn_idx);
		stats_table->stats_offset_32[I40IW_HW_STAT_INDEX_IP6RXDISCARD] =
				I40E_GLPES_PFIP6RXDISCARD(fcn_idx);
		stats_table->stats_offset_32[I40IW_HW_STAT_INDEX_IP6RXTRUNC] =
				I40E_GLPES_PFIP6RXTRUNC(fcn_idx);
		stats_table->stats_offset_32[I40IW_HW_STAT_INDEX_IP6TXNOROUTE] =
				I40E_GLPES_PFIP6TXNOROUTE(fcn_idx);
		stats_table->stats_offset_32[I40IW_HW_STAT_INDEX_TCPRTXSEG] =
				I40E_GLPES_PFTCPRTXSEG(fcn_idx);
		stats_table->stats_offset_32[I40IW_HW_STAT_INDEX_TCPRXOPTERR] =
				I40E_GLPES_PFTCPRXOPTERR(fcn_idx);
		stats_table->stats_offset_32[I40IW_HW_STAT_INDEX_TCPRXPROTOERR] =
				I40E_GLPES_PFTCPRXPROTOERR(fcn_idx);

		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_IP4RXOCTS] =
				I40E_GLPES_PFIP4RXOCTSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_IP4RXPKTS] =
				I40E_GLPES_PFIP4RXPKTSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_IP4RXFRAGS] =
				I40E_GLPES_PFIP4RXFRAGSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_IP4RXMCPKTS] =
				I40E_GLPES_PFIP4RXMCPKTSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_IP4TXOCTS] =
				I40E_GLPES_PFIP4TXOCTSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_IP4TXPKTS] =
				I40E_GLPES_PFIP4TXPKTSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_IP4TXFRAGS] =
				I40E_GLPES_PFIP4TXFRAGSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_IP4TXMCPKTS] =
				I40E_GLPES_PFIP4TXMCPKTSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_IP6RXOCTS] =
				I40E_GLPES_PFIP6RXOCTSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_IP6RXPKTS] =
				I40E_GLPES_PFIP6RXPKTSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_IP6RXFRAGS] =
				I40E_GLPES_PFIP6RXFRAGSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_IP6RXMCPKTS] =
				I40E_GLPES_PFIP6RXMCPKTSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_IP6TXOCTS] =
				I40E_GLPES_PFIP6TXOCTSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_IP6TXPKTS] =
				I40E_GLPES_PFIP6TXPKTSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_IP6TXPKTS] =
				I40E_GLPES_PFIP6TXPKTSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_IP6TXFRAGS] =
				I40E_GLPES_PFIP6TXFRAGSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_TCPRXSEGS] =
				I40E_GLPES_PFTCPRXSEGSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_TCPTXSEG] =
				I40E_GLPES_PFTCPTXSEGLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_RDMARXRDS] =
				I40E_GLPES_PFRDMARXRDSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_RDMARXSNDS] =
				I40E_GLPES_PFRDMARXSNDSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_RDMARXWRS] =
				I40E_GLPES_PFRDMARXWRSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_RDMATXRDS] =
				I40E_GLPES_PFRDMATXRDSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_RDMATXSNDS] =
				I40E_GLPES_PFRDMATXSNDSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_RDMATXWRS] =
				I40E_GLPES_PFRDMATXWRSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_RDMAVBND] =
				I40E_GLPES_PFRDMAVBNDLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_RDMAVINV] =
				I40E_GLPES_PFRDMAVINVLO(fcn_idx);
	} else {
		stats_table->stats_offset_32[I40IW_HW_STAT_INDEX_IP4RXDISCARD] =
				I40E_GLPES_VFIP4RXDISCARD(fcn_idx);
		stats_table->stats_offset_32[I40IW_HW_STAT_INDEX_IP4RXTRUNC] =
				I40E_GLPES_VFIP4RXTRUNC(fcn_idx);
		stats_table->stats_offset_32[I40IW_HW_STAT_INDEX_IP4TXNOROUTE] =
				I40E_GLPES_VFIP4TXNOROUTE(fcn_idx);
		stats_table->stats_offset_32[I40IW_HW_STAT_INDEX_IP6RXDISCARD] =
				I40E_GLPES_VFIP6RXDISCARD(fcn_idx);
		stats_table->stats_offset_32[I40IW_HW_STAT_INDEX_IP6RXTRUNC] =
				I40E_GLPES_VFIP6RXTRUNC(fcn_idx);
		stats_table->stats_offset_32[I40IW_HW_STAT_INDEX_IP6TXNOROUTE] =
				I40E_GLPES_VFIP6TXNOROUTE(fcn_idx);
		stats_table->stats_offset_32[I40IW_HW_STAT_INDEX_TCPRTXSEG] =
				I40E_GLPES_VFTCPRTXSEG(fcn_idx);
		stats_table->stats_offset_32[I40IW_HW_STAT_INDEX_TCPRXOPTERR] =
				I40E_GLPES_VFTCPRXOPTERR(fcn_idx);
		stats_table->stats_offset_32[I40IW_HW_STAT_INDEX_TCPRXPROTOERR] =
				I40E_GLPES_VFTCPRXPROTOERR(fcn_idx);

		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_IP4RXOCTS] =
				I40E_GLPES_VFIP4RXOCTSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_IP4RXPKTS] =
				I40E_GLPES_VFIP4RXPKTSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_IP4RXFRAGS] =
				I40E_GLPES_VFIP4RXFRAGSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_IP4RXMCPKTS] =
				I40E_GLPES_VFIP4RXMCPKTSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_IP4TXOCTS] =
				I40E_GLPES_VFIP4TXOCTSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_IP4TXPKTS] =
				I40E_GLPES_VFIP4TXPKTSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_IP4TXFRAGS] =
				I40E_GLPES_VFIP4TXFRAGSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_IP4TXMCPKTS] =
				I40E_GLPES_VFIP4TXMCPKTSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_IP6RXOCTS] =
				I40E_GLPES_VFIP6RXOCTSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_IP6RXPKTS] =
				I40E_GLPES_VFIP6RXPKTSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_IP6RXFRAGS] =
				I40E_GLPES_VFIP6RXFRAGSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_IP6RXMCPKTS] =
				I40E_GLPES_VFIP6RXMCPKTSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_IP6TXOCTS] =
				I40E_GLPES_VFIP6TXOCTSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_IP6TXPKTS] =
				I40E_GLPES_VFIP6TXPKTSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_IP6TXPKTS] =
				I40E_GLPES_VFIP6TXPKTSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_IP6TXFRAGS] =
				I40E_GLPES_VFIP6TXFRAGSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_TCPRXSEGS] =
				I40E_GLPES_VFTCPRXSEGSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_TCPTXSEG] =
				I40E_GLPES_VFTCPTXSEGLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_RDMARXRDS] =
				I40E_GLPES_VFRDMARXRDSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_RDMARXSNDS] =
				I40E_GLPES_VFRDMARXSNDSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_RDMARXWRS] =
				I40E_GLPES_VFRDMARXWRSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_RDMATXRDS] =
				I40E_GLPES_VFRDMATXRDSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_RDMATXSNDS] =
				I40E_GLPES_VFRDMATXSNDSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_RDMATXWRS] =
				I40E_GLPES_VFRDMATXWRSLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_RDMAVBND] =
				I40E_GLPES_VFRDMAVBNDLO(fcn_idx);
		stats_table->stats_offset_64[I40IW_HW_STAT_INDEX_RDMAVINV] =
				I40E_GLPES_VFRDMAVINVLO(fcn_idx);
	}

	for (stats_index = 0; stats_index < I40IW_HW_STAT_INDEX_MAX_64;
	     stats_index++) {
		stats_reg_offset = stats_table->stats_offset_64[stats_index];
		last_rd_stats->stats_value_64[stats_index] =
			readq(stats->hw->hw_addr + stats_reg_offset);
	}

	for (stats_index = 0; stats_index < I40IW_HW_STAT_INDEX_MAX_32;
	     stats_index++) {
		stats_reg_offset = stats_table->stats_offset_32[stats_index];
		last_rd_stats->stats_value_32[stats_index] =
			i40iw_rd32(stats->hw, stats_reg_offset);
	}
}

/**
 * i40iw_hw_stats_read_32 - Read 32-bit HW stats counters and accommodates for roll-overs.
 * @stat: pestat struct
 * @index: index in HW stats table which contains offset reg-addr
 * @value: hw stats value
 */
void i40iw_hw_stats_read_32(struct i40iw_vsi_pestat *stats,
			    enum i40iw_hw_stats_index_32b index,
			    u64 *value)
{
	struct i40iw_dev_hw_stats_offsets *stats_table =
		&stats->hw_stats_offsets;
	struct i40iw_dev_hw_stats *last_rd_stats = &stats->last_read_hw_stats;
	struct i40iw_dev_hw_stats *hw_stats = &stats->hw_stats;
	u64 new_stats_value = 0;
	u32 stats_reg_offset = stats_table->stats_offset_32[index];

	new_stats_value = i40iw_rd32(stats->hw, stats_reg_offset);
	/*roll-over case */
	if (new_stats_value < last_rd_stats->stats_value_32[index])
		hw_stats->stats_value_32[index] += new_stats_value;
	else
		hw_stats->stats_value_32[index] +=
			new_stats_value - last_rd_stats->stats_value_32[index];
	last_rd_stats->stats_value_32[index] = new_stats_value;
	*value = hw_stats->stats_value_32[index];
}

/**
 * i40iw_hw_stats_read_64 - Read HW stats counters (greater than 32-bit) and accommodates for roll-overs.
 * @stats: pestat struct
 * @index: index in HW stats table which contains offset reg-addr
 * @value: hw stats value
 */
void i40iw_hw_stats_read_64(struct i40iw_vsi_pestat *stats,
			    enum i40iw_hw_stats_index_64b index,
			    u64 *value)
{
	struct i40iw_dev_hw_stats_offsets *stats_table =
		&stats->hw_stats_offsets;
	struct i40iw_dev_hw_stats *last_rd_stats = &stats->last_read_hw_stats;
	struct i40iw_dev_hw_stats *hw_stats = &stats->hw_stats;
	u64 new_stats_value = 0;
	u32 stats_reg_offset = stats_table->stats_offset_64[index];

	new_stats_value = readq(stats->hw->hw_addr + stats_reg_offset);
	/*roll-over case */
	if (new_stats_value < last_rd_stats->stats_value_64[index])
		hw_stats->stats_value_64[index] += new_stats_value;
	else
		hw_stats->stats_value_64[index] +=
			new_stats_value - last_rd_stats->stats_value_64[index];
	last_rd_stats->stats_value_64[index] = new_stats_value;
	*value = hw_stats->stats_value_64[index];
}

/**
 * i40iw_hw_stats_read_all - read all HW stat counters
 * @stats: pestat struct
 * @stats_values: hw stats structure
 *
 * Read all the HW stat counters and populates hw_stats structure
 * of passed-in vsi's pestat as well as copy created in stat_values.
 */
void i40iw_hw_stats_read_all(struct i40iw_vsi_pestat *stats,
			     struct i40iw_dev_hw_stats *stats_values)
{
	u32 stats_index;
	unsigned long flags;

	spin_lock_irqsave(&stats->lock, flags);

	for (stats_index = 0; stats_index < I40IW_HW_STAT_INDEX_MAX_32;
	     stats_index++)
		i40iw_hw_stats_read_32(stats, stats_index,
				       &stats_values->stats_value_32[stats_index]);
	for (stats_index = 0; stats_index < I40IW_HW_STAT_INDEX_MAX_64;
	     stats_index++)
		i40iw_hw_stats_read_64(stats, stats_index,
				       &stats_values->stats_value_64[stats_index]);
	spin_unlock_irqrestore(&stats->lock, flags);
}

/**
 * i40iw_hw_stats_refresh_all - Update all HW stats structs
 * @stats: pestat struct
 *
 * Read all the HW stats counters to refresh values in hw_stats structure
 * of passed-in dev's pestat
 */
void i40iw_hw_stats_refresh_all(struct i40iw_vsi_pestat *stats)
{
	u64 stats_value;
	u32 stats_index;
	unsigned long flags;

	spin_lock_irqsave(&stats->lock, flags);

	for (stats_index = 0; stats_index < I40IW_HW_STAT_INDEX_MAX_32;
	     stats_index++)
		i40iw_hw_stats_read_32(stats, stats_index, &stats_value);
	for (stats_index = 0; stats_index < I40IW_HW_STAT_INDEX_MAX_64;
	     stats_index++)
		i40iw_hw_stats_read_64(stats, stats_index, &stats_value);
	spin_unlock_irqrestore(&stats->lock, flags);
}

/**
 * i40iw_get_fcn_id - Return the function id
 * @dev: pointer to the device
 */
static u8 i40iw_get_fcn_id(struct i40iw_sc_dev *dev)
{
	u8 fcn_id = I40IW_INVALID_FCN_ID;
	u8 i;

	for (i = I40IW_FIRST_NON_PF_STAT; i < I40IW_MAX_STATS_COUNT; i++)
		if (!dev->fcn_id_array[i]) {
			fcn_id = i;
			dev->fcn_id_array[i] = true;
			break;
		}
	return fcn_id;
}

/**
 * i40iw_vsi_stats_init - Initialize the vsi statistics
 * @vsi: pointer to the vsi structure
 * @info: The info structure used for initialization
 */
enum i40iw_status_code i40iw_vsi_stats_init(struct i40iw_sc_vsi *vsi, struct i40iw_vsi_stats_info *info)
{
	u8 fcn_id = info->fcn_id;

	if (info->alloc_fcn_id)
		fcn_id = i40iw_get_fcn_id(vsi->dev);

	if (fcn_id == I40IW_INVALID_FCN_ID)
		return I40IW_ERR_NOT_READY;

	vsi->pestat = info->pestat;
	vsi->pestat->hw = vsi->dev->hw;
	vsi->pestat->vsi = vsi;

	if (info->stats_initialize) {
		i40iw_hw_stats_init(vsi->pestat, fcn_id, true);
		spin_lock_init(&vsi->pestat->lock);
		i40iw_hw_stats_start_timer(vsi);
	}
	vsi->stats_fcn_id_alloc = info->alloc_fcn_id;
	vsi->fcn_id = fcn_id;
	return I40IW_SUCCESS;
}

/**
 * i40iw_vsi_stats_free - Free the vsi stats
 * @vsi: pointer to the vsi structure
 */
void i40iw_vsi_stats_free(struct i40iw_sc_vsi *vsi)
{
	u8 fcn_id = vsi->fcn_id;

	if (vsi->stats_fcn_id_alloc && fcn_id < I40IW_MAX_STATS_COUNT)
		vsi->dev->fcn_id_array[fcn_id] = false;
	i40iw_hw_stats_stop_timer(vsi);
}

static struct i40iw_cqp_ops iw_cqp_ops = {
	.cqp_init = i40iw_sc_cqp_init,
	.cqp_create = i40iw_sc_cqp_create,
	.cqp_post_sq = i40iw_sc_cqp_post_sq,
	.cqp_get_next_send_wqe = i40iw_sc_cqp_get_next_send_wqe,
	.cqp_destroy = i40iw_sc_cqp_destroy,
	.poll_for_cqp_op_done = i40iw_sc_poll_for_cqp_op_done
};

static struct i40iw_ccq_ops iw_ccq_ops = {
	.ccq_init = i40iw_sc_ccq_init,
	.ccq_create = i40iw_sc_ccq_create,
	.ccq_destroy = i40iw_sc_ccq_destroy,
	.ccq_create_done = i40iw_sc_ccq_create_done,
	.ccq_get_cqe_info = i40iw_sc_ccq_get_cqe_info,
	.ccq_arm = i40iw_sc_ccq_arm
};

static struct i40iw_ceq_ops iw_ceq_ops = {
	.ceq_init = i40iw_sc_ceq_init,
	.ceq_create = i40iw_sc_ceq_create,
	.cceq_create_done = i40iw_sc_cceq_create_done,
	.cceq_destroy_done = i40iw_sc_cceq_destroy_done,
	.cceq_create = i40iw_sc_cceq_create,
	.ceq_destroy = i40iw_sc_ceq_destroy,
	.process_ceq = i40iw_sc_process_ceq
};

static struct i40iw_aeq_ops iw_aeq_ops = {
	.aeq_init = i40iw_sc_aeq_init,
	.aeq_create = i40iw_sc_aeq_create,
	.aeq_destroy = i40iw_sc_aeq_destroy,
	.get_next_aeqe = i40iw_sc_get_next_aeqe,
	.repost_aeq_entries = i40iw_sc_repost_aeq_entries,
	.aeq_create_done = i40iw_sc_aeq_create_done,
	.aeq_destroy_done = i40iw_sc_aeq_destroy_done
};

/* iwarp pd ops */
static struct i40iw_pd_ops iw_pd_ops = {
	.pd_init = i40iw_sc_pd_init,
};

static struct i40iw_priv_qp_ops iw_priv_qp_ops = {
	.qp_init = i40iw_sc_qp_init,
	.qp_create = i40iw_sc_qp_create,
	.qp_modify = i40iw_sc_qp_modify,
	.qp_destroy = i40iw_sc_qp_destroy,
	.qp_flush_wqes = i40iw_sc_qp_flush_wqes,
	.qp_upload_context = i40iw_sc_qp_upload_context,
	.qp_setctx = i40iw_sc_qp_setctx,
	.qp_send_lsmm = i40iw_sc_send_lsmm,
	.qp_send_lsmm_nostag = i40iw_sc_send_lsmm_nostag,
	.qp_send_rtt = i40iw_sc_send_rtt,
	.qp_post_wqe0 = i40iw_sc_post_wqe0,
	.iw_mr_fast_register = i40iw_sc_mr_fast_register
};

static struct i40iw_priv_cq_ops iw_priv_cq_ops = {
	.cq_init = i40iw_sc_cq_init,
	.cq_create = i40iw_sc_cq_create,
	.cq_destroy = i40iw_sc_cq_destroy,
	.cq_modify = i40iw_sc_cq_modify,
};

static struct i40iw_mr_ops iw_mr_ops = {
	.alloc_stag = i40iw_sc_alloc_stag,
	.mr_reg_non_shared = i40iw_sc_mr_reg_non_shared,
	.mr_reg_shared = i40iw_sc_mr_reg_shared,
	.dealloc_stag = i40iw_sc_dealloc_stag,
	.query_stag = i40iw_sc_query_stag,
	.mw_alloc = i40iw_sc_mw_alloc
};

static struct i40iw_cqp_misc_ops iw_cqp_misc_ops = {
	.manage_push_page = i40iw_sc_manage_push_page,
	.manage_hmc_pm_func_table = i40iw_sc_manage_hmc_pm_func_table,
	.set_hmc_resource_profile = i40iw_sc_set_hmc_resource_profile,
	.commit_fpm_values = i40iw_sc_commit_fpm_values,
	.query_fpm_values = i40iw_sc_query_fpm_values,
	.static_hmc_pages_allocated = i40iw_sc_static_hmc_pages_allocated,
	.add_arp_cache_entry = i40iw_sc_add_arp_cache_entry,
	.del_arp_cache_entry = i40iw_sc_del_arp_cache_entry,
	.query_arp_cache_entry = i40iw_sc_query_arp_cache_entry,
	.manage_apbvt_entry = i40iw_sc_manage_apbvt_entry,
	.manage_qhash_table_entry = i40iw_sc_manage_qhash_table_entry,
	.alloc_local_mac_ipaddr_table_entry = i40iw_sc_alloc_local_mac_ipaddr_entry,
	.add_local_mac_ipaddr_entry = i40iw_sc_add_local_mac_ipaddr_entry,
	.del_local_mac_ipaddr_entry = i40iw_sc_del_local_mac_ipaddr_entry,
	.cqp_nop = i40iw_sc_cqp_nop,
	.commit_fpm_values_done = i40iw_sc_commit_fpm_values_done,
	.query_fpm_values_done = i40iw_sc_query_fpm_values_done,
	.manage_hmc_pm_func_table_done = i40iw_sc_manage_hmc_pm_func_table_done,
	.update_suspend_qp = i40iw_sc_suspend_qp,
	.update_resume_qp = i40iw_sc_resume_qp
};

static struct i40iw_hmc_ops iw_hmc_ops = {
	.init_iw_hmc = i40iw_sc_init_iw_hmc,
	.parse_fpm_query_buf = i40iw_sc_parse_fpm_query_buf,
	.configure_iw_fpm = i40iw_sc_configure_iw_fpm,
	.parse_fpm_commit_buf = i40iw_sc_parse_fpm_commit_buf,
	.create_hmc_object = i40iw_sc_create_hmc_obj,
	.del_hmc_object = i40iw_sc_del_hmc_obj
};

/**
 * i40iw_device_init - Initialize IWARP device
 * @dev: IWARP device pointer
 * @info: IWARP init info
 */
enum i40iw_status_code i40iw_device_init(struct i40iw_sc_dev *dev,
					 struct i40iw_device_init_info *info)
{
	u32 val;
	u32 vchnl_ver = 0;
	u16 hmc_fcn = 0;
	enum i40iw_status_code ret_code = 0;
	u8 db_size;

	spin_lock_init(&dev->cqp_lock);

	i40iw_device_init_uk(&dev->dev_uk);

	dev->debug_mask = info->debug_mask;

	dev->hmc_fn_id = info->hmc_fn_id;
	dev->is_pf = info->is_pf;

	dev->fpm_query_buf_pa = info->fpm_query_buf_pa;
	dev->fpm_query_buf = info->fpm_query_buf;

	dev->fpm_commit_buf_pa = info->fpm_commit_buf_pa;
	dev->fpm_commit_buf = info->fpm_commit_buf;

	dev->hw = info->hw;
	dev->hw->hw_addr = info->bar0;

	if (dev->is_pf) {
		val = i40iw_rd32(dev->hw, I40E_GLPCI_DREVID);
		dev->hw_rev = (u8)RS_32(val, I40E_GLPCI_DREVID_DEFAULT_REVID);

		val = i40iw_rd32(dev->hw, I40E_GLPCI_LBARCTRL);
		db_size = (u8)RS_32(val, I40E_GLPCI_LBARCTRL_PE_DB_SIZE);
		if ((db_size != I40IW_PE_DB_SIZE_4M) &&
		    (db_size != I40IW_PE_DB_SIZE_8M)) {
			i40iw_debug(dev, I40IW_DEBUG_DEV,
				    "%s: PE doorbell is not enabled in CSR val 0x%x\n",
				    __func__, val);
			ret_code = I40IW_ERR_PE_DOORBELL_NOT_ENABLED;
			return ret_code;
		}
		dev->db_addr = dev->hw->hw_addr + I40IW_DB_ADDR_OFFSET;
		dev->vchnl_if.vchnl_recv = i40iw_vchnl_recv_pf;
	} else {
		dev->db_addr = dev->hw->hw_addr + I40IW_VF_DB_ADDR_OFFSET;
	}

	dev->cqp_ops = &iw_cqp_ops;
	dev->ccq_ops = &iw_ccq_ops;
	dev->ceq_ops = &iw_ceq_ops;
	dev->aeq_ops = &iw_aeq_ops;
	dev->cqp_misc_ops = &iw_cqp_misc_ops;
	dev->iw_pd_ops = &iw_pd_ops;
	dev->iw_priv_qp_ops = &iw_priv_qp_ops;
	dev->iw_priv_cq_ops = &iw_priv_cq_ops;
	dev->mr_ops = &iw_mr_ops;
	dev->hmc_ops = &iw_hmc_ops;
	dev->vchnl_if.vchnl_send = info->vchnl_send;
	if (dev->vchnl_if.vchnl_send)
		dev->vchnl_up = true;
	else
		dev->vchnl_up = false;
	if (!dev->is_pf) {
		dev->vchnl_if.vchnl_recv = i40iw_vchnl_recv_vf;
		ret_code = i40iw_vchnl_vf_get_ver(dev, &vchnl_ver);
		if (!ret_code) {
			i40iw_debug(dev, I40IW_DEBUG_DEV,
				    "%s: Get Channel version rc = 0x%0x, version is %u\n",
				__func__, ret_code, vchnl_ver);
			ret_code = i40iw_vchnl_vf_get_hmc_fcn(dev, &hmc_fcn);
			if (!ret_code) {
				i40iw_debug(dev, I40IW_DEBUG_DEV,
					    "%s Get HMC function rc = 0x%0x, hmc fcn is %u\n",
					    __func__, ret_code, hmc_fcn);
				dev->hmc_fn_id = (u8)hmc_fcn;
			}
		}
	}
	dev->iw_vf_cqp_ops = &iw_vf_cqp_ops;

	return ret_code;
}
