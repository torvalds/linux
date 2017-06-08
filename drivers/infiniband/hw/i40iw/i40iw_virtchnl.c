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
#include "i40iw_virtchnl.h"

/**
 * vchnl_vf_send_get_ver_req - Request Channel version
 * @dev: IWARP device pointer
 * @vchnl_req: Virtual channel message request pointer
 */
static enum i40iw_status_code vchnl_vf_send_get_ver_req(struct i40iw_sc_dev *dev,
							struct i40iw_virtchnl_req *vchnl_req)
{
	enum i40iw_status_code ret_code = I40IW_ERR_NOT_READY;
	struct i40iw_virtchnl_op_buf *vchnl_msg = vchnl_req->vchnl_msg;

	if (!dev->vchnl_up)
		return ret_code;

	memset(vchnl_msg, 0, sizeof(*vchnl_msg));
	vchnl_msg->iw_chnl_op_ctx = (uintptr_t)vchnl_req;
	vchnl_msg->iw_chnl_buf_len = sizeof(*vchnl_msg);
	vchnl_msg->iw_op_code = I40IW_VCHNL_OP_GET_VER;
	vchnl_msg->iw_op_ver = I40IW_VCHNL_OP_GET_VER_V0;
	ret_code = dev->vchnl_if.vchnl_send(dev, 0, (u8 *)vchnl_msg, vchnl_msg->iw_chnl_buf_len);
	if (ret_code)
		i40iw_debug(dev, I40IW_DEBUG_VIRT,
			    "%s: virt channel send failed 0x%x\n", __func__, ret_code);
	return ret_code;
}

/**
 * vchnl_vf_send_get_hmc_fcn_req - Request HMC Function from VF
 * @dev: IWARP device pointer
 * @vchnl_req: Virtual channel message request pointer
 */
static enum i40iw_status_code vchnl_vf_send_get_hmc_fcn_req(struct i40iw_sc_dev *dev,
							    struct i40iw_virtchnl_req *vchnl_req)
{
	enum i40iw_status_code ret_code = I40IW_ERR_NOT_READY;
	struct i40iw_virtchnl_op_buf *vchnl_msg = vchnl_req->vchnl_msg;

	if (!dev->vchnl_up)
		return ret_code;

	memset(vchnl_msg, 0, sizeof(*vchnl_msg));
	vchnl_msg->iw_chnl_op_ctx = (uintptr_t)vchnl_req;
	vchnl_msg->iw_chnl_buf_len = sizeof(*vchnl_msg);
	vchnl_msg->iw_op_code = I40IW_VCHNL_OP_GET_HMC_FCN;
	vchnl_msg->iw_op_ver = I40IW_VCHNL_OP_GET_HMC_FCN_V0;
	ret_code = dev->vchnl_if.vchnl_send(dev, 0, (u8 *)vchnl_msg, vchnl_msg->iw_chnl_buf_len);
	if (ret_code)
		i40iw_debug(dev, I40IW_DEBUG_VIRT,
			    "%s: virt channel send failed 0x%x\n", __func__, ret_code);
	return ret_code;
}

/**
 * vchnl_vf_send_get_pe_stats_req - Request PE stats from VF
 * @dev: IWARP device pointer
 * @vchnl_req: Virtual channel message request pointer
 */
static enum i40iw_status_code vchnl_vf_send_get_pe_stats_req(struct i40iw_sc_dev *dev,
							     struct i40iw_virtchnl_req  *vchnl_req)
{
	enum i40iw_status_code ret_code = I40IW_ERR_NOT_READY;
	struct i40iw_virtchnl_op_buf *vchnl_msg = vchnl_req->vchnl_msg;

	if (!dev->vchnl_up)
		return ret_code;

	memset(vchnl_msg, 0, sizeof(*vchnl_msg));
	vchnl_msg->iw_chnl_op_ctx = (uintptr_t)vchnl_req;
	vchnl_msg->iw_chnl_buf_len = sizeof(*vchnl_msg) + sizeof(struct i40iw_dev_hw_stats) - 1;
	vchnl_msg->iw_op_code = I40IW_VCHNL_OP_GET_STATS;
	vchnl_msg->iw_op_ver = I40IW_VCHNL_OP_GET_STATS_V0;
	ret_code = dev->vchnl_if.vchnl_send(dev, 0, (u8 *)vchnl_msg, vchnl_msg->iw_chnl_buf_len);
	if (ret_code)
		i40iw_debug(dev, I40IW_DEBUG_VIRT,
			    "%s: virt channel send failed 0x%x\n", __func__, ret_code);
	return ret_code;
}

/**
 * vchnl_vf_send_add_hmc_objs_req - Add HMC objects
 * @dev: IWARP device pointer
 * @vchnl_req: Virtual channel message request pointer
 */
static enum i40iw_status_code vchnl_vf_send_add_hmc_objs_req(struct i40iw_sc_dev *dev,
							     struct i40iw_virtchnl_req *vchnl_req,
							     enum i40iw_hmc_rsrc_type rsrc_type,
							     u32 start_index,
							     u32 rsrc_count)
{
	enum i40iw_status_code ret_code = I40IW_ERR_NOT_READY;
	struct i40iw_virtchnl_op_buf *vchnl_msg = vchnl_req->vchnl_msg;
	struct i40iw_virtchnl_hmc_obj_range *add_hmc_obj;

	if (!dev->vchnl_up)
		return ret_code;

	add_hmc_obj = (struct i40iw_virtchnl_hmc_obj_range *)vchnl_msg->iw_chnl_buf;
	memset(vchnl_msg, 0, sizeof(*vchnl_msg));
	memset(add_hmc_obj, 0, sizeof(*add_hmc_obj));
	vchnl_msg->iw_chnl_op_ctx = (uintptr_t)vchnl_req;
	vchnl_msg->iw_chnl_buf_len = sizeof(*vchnl_msg) + sizeof(struct i40iw_virtchnl_hmc_obj_range) - 1;
	vchnl_msg->iw_op_code = I40IW_VCHNL_OP_ADD_HMC_OBJ_RANGE;
	vchnl_msg->iw_op_ver = I40IW_VCHNL_OP_ADD_HMC_OBJ_RANGE_V0;
	add_hmc_obj->obj_type = (u16)rsrc_type;
	add_hmc_obj->start_index = start_index;
	add_hmc_obj->obj_count = rsrc_count;
	ret_code = dev->vchnl_if.vchnl_send(dev, 0, (u8 *)vchnl_msg, vchnl_msg->iw_chnl_buf_len);
	if (ret_code)
		i40iw_debug(dev, I40IW_DEBUG_VIRT,
			    "%s: virt channel send failed 0x%x\n", __func__, ret_code);
	return ret_code;
}

/**
 * vchnl_vf_send_del_hmc_objs_req - del HMC objects
 * @dev: IWARP device pointer
 * @vchnl_req: Virtual channel message request pointer
 * @ rsrc_type - resource type to delete
 * @ start_index - starting index for resource
 * @ rsrc_count - number of resource type to delete
 */
static enum i40iw_status_code vchnl_vf_send_del_hmc_objs_req(struct i40iw_sc_dev *dev,
							     struct i40iw_virtchnl_req *vchnl_req,
							     enum i40iw_hmc_rsrc_type rsrc_type,
							     u32 start_index,
							     u32 rsrc_count)
{
	enum i40iw_status_code ret_code = I40IW_ERR_NOT_READY;
	struct i40iw_virtchnl_op_buf *vchnl_msg = vchnl_req->vchnl_msg;
	struct i40iw_virtchnl_hmc_obj_range *add_hmc_obj;

	if (!dev->vchnl_up)
		return ret_code;

	add_hmc_obj = (struct i40iw_virtchnl_hmc_obj_range *)vchnl_msg->iw_chnl_buf;
	memset(vchnl_msg, 0, sizeof(*vchnl_msg));
	memset(add_hmc_obj, 0, sizeof(*add_hmc_obj));
	vchnl_msg->iw_chnl_op_ctx = (uintptr_t)vchnl_req;
	vchnl_msg->iw_chnl_buf_len = sizeof(*vchnl_msg) + sizeof(struct i40iw_virtchnl_hmc_obj_range) - 1;
	vchnl_msg->iw_op_code = I40IW_VCHNL_OP_DEL_HMC_OBJ_RANGE;
	vchnl_msg->iw_op_ver = I40IW_VCHNL_OP_DEL_HMC_OBJ_RANGE_V0;
	add_hmc_obj->obj_type = (u16)rsrc_type;
	add_hmc_obj->start_index = start_index;
	add_hmc_obj->obj_count = rsrc_count;
	ret_code = dev->vchnl_if.vchnl_send(dev, 0, (u8 *)vchnl_msg, vchnl_msg->iw_chnl_buf_len);
	if (ret_code)
		i40iw_debug(dev, I40IW_DEBUG_VIRT,
			    "%s: virt channel send failed 0x%x\n", __func__, ret_code);
	return ret_code;
}

/**
 * vchnl_pf_send_get_ver_resp - Send channel version to VF
 * @dev: IWARP device pointer
 * @vf_id: Virtual function ID associated with the message
 * @vchnl_msg: Virtual channel message buffer pointer
 */
static void vchnl_pf_send_get_ver_resp(struct i40iw_sc_dev *dev,
				       u32 vf_id,
				       struct i40iw_virtchnl_op_buf *vchnl_msg)
{
	enum i40iw_status_code ret_code;
	u8 resp_buffer[sizeof(struct i40iw_virtchnl_resp_buf) + sizeof(u32) - 1];
	struct i40iw_virtchnl_resp_buf *vchnl_msg_resp = (struct i40iw_virtchnl_resp_buf *)resp_buffer;

	memset(resp_buffer, 0, sizeof(*resp_buffer));
	vchnl_msg_resp->iw_chnl_op_ctx = vchnl_msg->iw_chnl_op_ctx;
	vchnl_msg_resp->iw_chnl_buf_len = sizeof(resp_buffer);
	vchnl_msg_resp->iw_op_ret_code = I40IW_SUCCESS;
	*((u32 *)vchnl_msg_resp->iw_chnl_buf) = I40IW_VCHNL_CHNL_VER_V0;
	ret_code = dev->vchnl_if.vchnl_send(dev, vf_id, resp_buffer, sizeof(resp_buffer));
	if (ret_code)
		i40iw_debug(dev, I40IW_DEBUG_VIRT,
			    "%s: virt channel send failed 0x%x\n", __func__, ret_code);
}

/**
 * vchnl_pf_send_get_hmc_fcn_resp - Send HMC Function to VF
 * @dev: IWARP device pointer
 * @vf_id: Virtual function ID associated with the message
 * @vchnl_msg: Virtual channel message buffer pointer
 */
static void vchnl_pf_send_get_hmc_fcn_resp(struct i40iw_sc_dev *dev,
					   u32 vf_id,
					   struct i40iw_virtchnl_op_buf *vchnl_msg,
					   u16 hmc_fcn)
{
	enum i40iw_status_code ret_code;
	u8 resp_buffer[sizeof(struct i40iw_virtchnl_resp_buf) + sizeof(u16) - 1];
	struct i40iw_virtchnl_resp_buf *vchnl_msg_resp = (struct i40iw_virtchnl_resp_buf *)resp_buffer;

	memset(resp_buffer, 0, sizeof(*resp_buffer));
	vchnl_msg_resp->iw_chnl_op_ctx = vchnl_msg->iw_chnl_op_ctx;
	vchnl_msg_resp->iw_chnl_buf_len = sizeof(resp_buffer);
	vchnl_msg_resp->iw_op_ret_code = I40IW_SUCCESS;
	*((u16 *)vchnl_msg_resp->iw_chnl_buf) = hmc_fcn;
	ret_code = dev->vchnl_if.vchnl_send(dev, vf_id, resp_buffer, sizeof(resp_buffer));
	if (ret_code)
		i40iw_debug(dev, I40IW_DEBUG_VIRT,
			    "%s: virt channel send failed 0x%x\n", __func__, ret_code);
}

/**
 * vchnl_pf_send_get_pe_stats_resp - Send PE Stats to VF
 * @dev: IWARP device pointer
 * @vf_id: Virtual function ID associated with the message
 * @vchnl_msg: Virtual channel message buffer pointer
 * @hw_stats: HW Stats struct
 */

static void vchnl_pf_send_get_pe_stats_resp(struct i40iw_sc_dev *dev,
					    u32 vf_id,
					    struct i40iw_virtchnl_op_buf *vchnl_msg,
					    struct i40iw_dev_hw_stats *hw_stats)
{
	enum i40iw_status_code ret_code;
	u8 resp_buffer[sizeof(struct i40iw_virtchnl_resp_buf) + sizeof(struct i40iw_dev_hw_stats) - 1];
	struct i40iw_virtchnl_resp_buf *vchnl_msg_resp = (struct i40iw_virtchnl_resp_buf *)resp_buffer;

	memset(resp_buffer, 0, sizeof(*resp_buffer));
	vchnl_msg_resp->iw_chnl_op_ctx = vchnl_msg->iw_chnl_op_ctx;
	vchnl_msg_resp->iw_chnl_buf_len = sizeof(resp_buffer);
	vchnl_msg_resp->iw_op_ret_code = I40IW_SUCCESS;
	*((struct i40iw_dev_hw_stats *)vchnl_msg_resp->iw_chnl_buf) = *hw_stats;
	ret_code = dev->vchnl_if.vchnl_send(dev, vf_id, resp_buffer, sizeof(resp_buffer));
	if (ret_code)
		i40iw_debug(dev, I40IW_DEBUG_VIRT,
			    "%s: virt channel send failed 0x%x\n", __func__, ret_code);
}

/**
 * vchnl_pf_send_error_resp - Send an error response to VF
 * @dev: IWARP device pointer
 * @vf_id: Virtual function ID associated with the message
 * @vchnl_msg: Virtual channel message buffer pointer
 */
static void vchnl_pf_send_error_resp(struct i40iw_sc_dev *dev, u32 vf_id,
				     struct i40iw_virtchnl_op_buf *vchnl_msg,
				     u16 op_ret_code)
{
	enum i40iw_status_code ret_code;
	u8 resp_buffer[sizeof(struct i40iw_virtchnl_resp_buf)];
	struct i40iw_virtchnl_resp_buf *vchnl_msg_resp = (struct i40iw_virtchnl_resp_buf *)resp_buffer;

	memset(resp_buffer, 0, sizeof(resp_buffer));
	vchnl_msg_resp->iw_chnl_op_ctx = vchnl_msg->iw_chnl_op_ctx;
	vchnl_msg_resp->iw_chnl_buf_len = sizeof(resp_buffer);
	vchnl_msg_resp->iw_op_ret_code = (u16)op_ret_code;
	ret_code = dev->vchnl_if.vchnl_send(dev, vf_id, resp_buffer, sizeof(resp_buffer));
	if (ret_code)
		i40iw_debug(dev, I40IW_DEBUG_VIRT,
			    "%s: virt channel send failed 0x%x\n", __func__, ret_code);
}

/**
 * pf_cqp_get_hmc_fcn_callback - Callback for Get HMC Fcn
 * @cqp_req_param: CQP Request param value
 * @not_used: unused CQP callback parameter
 */
static void pf_cqp_get_hmc_fcn_callback(struct i40iw_sc_dev *dev, void *callback_param,
					struct i40iw_ccq_cqe_info *cqe_info)
{
	struct i40iw_vfdev *vf_dev = callback_param;
	struct i40iw_virt_mem vf_dev_mem;

	if (cqe_info->error) {
		i40iw_debug(dev, I40IW_DEBUG_VIRT,
			    "CQP Completion Error on Get HMC Function.  Maj = 0x%04x, Minor = 0x%04x\n",
			    cqe_info->maj_err_code, cqe_info->min_err_code);
		dev->vf_dev[vf_dev->iw_vf_idx] = NULL;
		vchnl_pf_send_error_resp(dev, vf_dev->vf_id, &vf_dev->vf_msg_buffer.vchnl_msg,
					 (u16)I40IW_ERR_CQP_COMPL_ERROR);
		vf_dev_mem.va = vf_dev;
		vf_dev_mem.size = sizeof(*vf_dev);
		i40iw_free_virt_mem(dev->hw, &vf_dev_mem);
	} else {
		i40iw_debug(dev, I40IW_DEBUG_VIRT,
			    "CQP Completion Operation Return information = 0x%08x\n",
			    cqe_info->op_ret_val);
		vf_dev->pmf_index = (u16)cqe_info->op_ret_val;
		vf_dev->msg_count--;
		vchnl_pf_send_get_hmc_fcn_resp(dev,
					       vf_dev->vf_id,
					       &vf_dev->vf_msg_buffer.vchnl_msg,
					       vf_dev->pmf_index);
	}
}

/**
 * pf_add_hmc_obj - Callback for Add HMC Object
 * @vf_dev: pointer to the VF Device
 */
static void pf_add_hmc_obj_callback(void *work_vf_dev)
{
	struct i40iw_vfdev *vf_dev = (struct i40iw_vfdev *)work_vf_dev;
	struct i40iw_hmc_info *hmc_info = &vf_dev->hmc_info;
	struct i40iw_virtchnl_op_buf *vchnl_msg = &vf_dev->vf_msg_buffer.vchnl_msg;
	struct i40iw_hmc_create_obj_info info;
	struct i40iw_virtchnl_hmc_obj_range *add_hmc_obj;
	enum i40iw_status_code ret_code;

	if (!vf_dev->pf_hmc_initialized) {
		ret_code = i40iw_pf_init_vfhmc(vf_dev->pf_dev, (u8)vf_dev->pmf_index, NULL);
		if (ret_code)
			goto add_out;
		vf_dev->pf_hmc_initialized = true;
	}

	add_hmc_obj = (struct i40iw_virtchnl_hmc_obj_range *)vchnl_msg->iw_chnl_buf;

	memset(&info, 0, sizeof(info));
	info.hmc_info = hmc_info;
	info.is_pf = false;
	info.rsrc_type = (u32)add_hmc_obj->obj_type;
	info.entry_type = (info.rsrc_type == I40IW_HMC_IW_PBLE) ? I40IW_SD_TYPE_PAGED : I40IW_SD_TYPE_DIRECT;
	info.start_idx = add_hmc_obj->start_index;
	info.count = add_hmc_obj->obj_count;
	i40iw_debug(vf_dev->pf_dev, I40IW_DEBUG_VIRT,
		    "I40IW_VCHNL_OP_ADD_HMC_OBJ_RANGE.  Add %u type %u objects\n",
		    info.count, info.rsrc_type);
	ret_code = i40iw_sc_create_hmc_obj(vf_dev->pf_dev, &info);
	if (!ret_code)
		vf_dev->hmc_info.hmc_obj[add_hmc_obj->obj_type].cnt = add_hmc_obj->obj_count;
add_out:
	vf_dev->msg_count--;
	vchnl_pf_send_error_resp(vf_dev->pf_dev, vf_dev->vf_id, vchnl_msg, (u16)ret_code);
}

/**
 * pf_del_hmc_obj_callback - Callback for delete HMC Object
 * @work_vf_dev: pointer to the VF Device
 */
static void pf_del_hmc_obj_callback(void *work_vf_dev)
{
	struct i40iw_vfdev *vf_dev = (struct i40iw_vfdev *)work_vf_dev;
	struct i40iw_hmc_info *hmc_info = &vf_dev->hmc_info;
	struct i40iw_virtchnl_op_buf *vchnl_msg = &vf_dev->vf_msg_buffer.vchnl_msg;
	struct i40iw_hmc_del_obj_info info;
	struct i40iw_virtchnl_hmc_obj_range *del_hmc_obj;
	enum i40iw_status_code ret_code = I40IW_SUCCESS;

	if (!vf_dev->pf_hmc_initialized)
		goto del_out;

	del_hmc_obj = (struct i40iw_virtchnl_hmc_obj_range *)vchnl_msg->iw_chnl_buf;

	memset(&info, 0, sizeof(info));
	info.hmc_info = hmc_info;
	info.is_pf = false;
	info.rsrc_type = (u32)del_hmc_obj->obj_type;
	info.start_idx = del_hmc_obj->start_index;
	info.count = del_hmc_obj->obj_count;
	i40iw_debug(vf_dev->pf_dev, I40IW_DEBUG_VIRT,
		    "I40IW_VCHNL_OP_DEL_HMC_OBJ_RANGE.  Delete %u type %u objects\n",
		    info.count, info.rsrc_type);
	ret_code = i40iw_sc_del_hmc_obj(vf_dev->pf_dev, &info, false);
del_out:
	vf_dev->msg_count--;
	vchnl_pf_send_error_resp(vf_dev->pf_dev, vf_dev->vf_id, vchnl_msg, (u16)ret_code);
}

/**
 * i40iw_vf_init_pestat - Initialize stats for VF
 * @devL pointer to the VF Device
 * @stats: Statistics structure pointer
 * @index: Stats index
 */
static void i40iw_vf_init_pestat(struct i40iw_sc_dev *dev, struct i40iw_vsi_pestat *stats, u16 index)
{
	stats->hw = dev->hw;
	i40iw_hw_stats_init(stats, (u8)index, false);
	spin_lock_init(&stats->lock);
}

/**
 * i40iw_vchnl_recv_pf - Receive PF virtual channel messages
 * @dev: IWARP device pointer
 * @vf_id: Virtual function ID associated with the message
 * @msg: Virtual channel message buffer pointer
 * @len: Length of the virtual channels message
 */
enum i40iw_status_code i40iw_vchnl_recv_pf(struct i40iw_sc_dev *dev,
					   u32 vf_id,
					   u8 *msg,
					   u16 len)
{
	struct i40iw_virtchnl_op_buf *vchnl_msg = (struct i40iw_virtchnl_op_buf *)msg;
	struct i40iw_vfdev *vf_dev = NULL;
	struct i40iw_hmc_fcn_info hmc_fcn_info;
	u16 iw_vf_idx;
	u16 first_avail_iw_vf = I40IW_MAX_PE_ENABLED_VF_COUNT;
	struct i40iw_virt_mem vf_dev_mem;
	struct i40iw_virtchnl_work_info work_info;
	struct i40iw_vsi_pestat *stats;
	enum i40iw_status_code ret_code;

	if (!dev || !msg || !len)
		return I40IW_ERR_PARAM;

	if (!dev->vchnl_up)
		return I40IW_ERR_NOT_READY;
	if (vchnl_msg->iw_op_code == I40IW_VCHNL_OP_GET_VER) {
		vchnl_pf_send_get_ver_resp(dev, vf_id, vchnl_msg);
		return I40IW_SUCCESS;
	}
	for (iw_vf_idx = 0; iw_vf_idx < I40IW_MAX_PE_ENABLED_VF_COUNT; iw_vf_idx++) {
		if (!dev->vf_dev[iw_vf_idx]) {
			if (first_avail_iw_vf == I40IW_MAX_PE_ENABLED_VF_COUNT)
				first_avail_iw_vf = iw_vf_idx;
			continue;
		}
		if (dev->vf_dev[iw_vf_idx]->vf_id == vf_id) {
			vf_dev = dev->vf_dev[iw_vf_idx];
			break;
		}
	}
	if (vf_dev) {
		if (!vf_dev->msg_count) {
			vf_dev->msg_count++;
		} else {
			i40iw_debug(dev, I40IW_DEBUG_VIRT,
				    "VF%u already has a channel message in progress.\n",
				    vf_id);
			return I40IW_SUCCESS;
		}
	}
	switch (vchnl_msg->iw_op_code) {
	case I40IW_VCHNL_OP_GET_HMC_FCN:
		if (!vf_dev &&
		    (first_avail_iw_vf != I40IW_MAX_PE_ENABLED_VF_COUNT)) {
			ret_code = i40iw_allocate_virt_mem(dev->hw, &vf_dev_mem, sizeof(struct i40iw_vfdev) +
							   (sizeof(struct i40iw_hmc_obj_info) * I40IW_HMC_IW_MAX));
			if (!ret_code) {
				vf_dev = vf_dev_mem.va;
				vf_dev->stats_initialized = false;
				vf_dev->pf_dev = dev;
				vf_dev->msg_count = 1;
				vf_dev->vf_id = vf_id;
				vf_dev->iw_vf_idx = first_avail_iw_vf;
				vf_dev->pf_hmc_initialized = false;
				vf_dev->hmc_info.hmc_obj = (struct i40iw_hmc_obj_info *)(&vf_dev[1]);
				i40iw_debug(dev, I40IW_DEBUG_VIRT,
					    "vf_dev %p, hmc_info %p, hmc_obj %p\n",
					    vf_dev, &vf_dev->hmc_info, vf_dev->hmc_info.hmc_obj);
				dev->vf_dev[first_avail_iw_vf] = vf_dev;
				iw_vf_idx = first_avail_iw_vf;
			} else {
				i40iw_debug(dev, I40IW_DEBUG_VIRT,
					    "VF%u Unable to allocate a VF device structure.\n",
					    vf_id);
				vchnl_pf_send_error_resp(dev, vf_id, vchnl_msg, (u16)I40IW_ERR_NO_MEMORY);
				return I40IW_SUCCESS;
			}
			memcpy(&vf_dev->vf_msg_buffer.vchnl_msg, vchnl_msg, len);
			hmc_fcn_info.callback_fcn = pf_cqp_get_hmc_fcn_callback;
			hmc_fcn_info.vf_id = vf_id;
			hmc_fcn_info.iw_vf_idx = vf_dev->iw_vf_idx;
			hmc_fcn_info.cqp_callback_param = vf_dev;
			hmc_fcn_info.free_fcn = false;
			ret_code = i40iw_cqp_manage_hmc_fcn_cmd(dev, &hmc_fcn_info);
			if (ret_code)
				i40iw_debug(dev, I40IW_DEBUG_VIRT,
					    "VF%u error CQP HMC Function operation.\n",
					    vf_id);
			i40iw_vf_init_pestat(dev, &vf_dev->pestat, vf_dev->pmf_index);
			vf_dev->stats_initialized = true;
		} else {
			if (vf_dev) {
				vf_dev->msg_count--;
				vchnl_pf_send_get_hmc_fcn_resp(dev, vf_id, vchnl_msg, vf_dev->pmf_index);
			} else {
				vchnl_pf_send_error_resp(dev, vf_id, vchnl_msg,
							 (u16)I40IW_ERR_NO_MEMORY);
			}
		}
		break;
	case I40IW_VCHNL_OP_ADD_HMC_OBJ_RANGE:
		if (!vf_dev)
			return I40IW_ERR_BAD_PTR;
		work_info.worker_vf_dev = vf_dev;
		work_info.callback_fcn = pf_add_hmc_obj_callback;
		memcpy(&vf_dev->vf_msg_buffer.vchnl_msg, vchnl_msg, len);
		i40iw_cqp_spawn_worker(dev, &work_info, vf_dev->iw_vf_idx);
		break;
	case I40IW_VCHNL_OP_DEL_HMC_OBJ_RANGE:
		if (!vf_dev)
			return I40IW_ERR_BAD_PTR;
		work_info.worker_vf_dev = vf_dev;
		work_info.callback_fcn = pf_del_hmc_obj_callback;
		memcpy(&vf_dev->vf_msg_buffer.vchnl_msg, vchnl_msg, len);
		i40iw_cqp_spawn_worker(dev, &work_info, vf_dev->iw_vf_idx);
		break;
	case I40IW_VCHNL_OP_GET_STATS:
		if (!vf_dev)
			return I40IW_ERR_BAD_PTR;
		stats = &vf_dev->pestat;
		i40iw_hw_stats_read_all(stats, &stats->hw_stats);
		vf_dev->msg_count--;
		vchnl_pf_send_get_pe_stats_resp(dev, vf_id, vchnl_msg, &stats->hw_stats);
		break;
	default:
		i40iw_debug(dev, I40IW_DEBUG_VIRT,
			    "40iw_vchnl_recv_pf: Invalid OpCode 0x%x\n",
			    vchnl_msg->iw_op_code);
		vchnl_pf_send_error_resp(dev, vf_id,
					 vchnl_msg, (u16)I40IW_ERR_NOT_IMPLEMENTED);
	}
	return I40IW_SUCCESS;
}

/**
 * i40iw_vchnl_recv_vf - Receive VF virtual channel messages
 * @dev: IWARP device pointer
 * @vf_id: Virtual function ID associated with the message
 * @msg: Virtual channel message buffer pointer
 * @len: Length of the virtual channels message
 */
enum i40iw_status_code i40iw_vchnl_recv_vf(struct i40iw_sc_dev *dev,
					   u32 vf_id,
					   u8 *msg,
					   u16 len)
{
	struct i40iw_virtchnl_resp_buf *vchnl_msg_resp = (struct i40iw_virtchnl_resp_buf *)msg;
	struct i40iw_virtchnl_req *vchnl_req;

	vchnl_req = (struct i40iw_virtchnl_req *)(uintptr_t)vchnl_msg_resp->iw_chnl_op_ctx;
	vchnl_req->ret_code = (enum i40iw_status_code)vchnl_msg_resp->iw_op_ret_code;
	if (len == (sizeof(*vchnl_msg_resp) + vchnl_req->parm_len - 1)) {
		if (vchnl_req->parm_len && vchnl_req->parm)
			memcpy(vchnl_req->parm, vchnl_msg_resp->iw_chnl_buf, vchnl_req->parm_len);
		i40iw_debug(dev, I40IW_DEBUG_VIRT,
			    "%s: Got response, data size %u\n", __func__,
			    vchnl_req->parm_len);
	} else {
		i40iw_debug(dev, I40IW_DEBUG_VIRT,
			    "%s: error length on response, Got %u, expected %u\n", __func__,
			    len, (u32)(sizeof(*vchnl_msg_resp) + vchnl_req->parm_len - 1));
	}

	return I40IW_SUCCESS;
}

/**
 * i40iw_vchnl_vf_get_ver - Request Channel version
 * @dev: IWARP device pointer
 * @vchnl_ver: Virtual channel message version pointer
 */
enum i40iw_status_code i40iw_vchnl_vf_get_ver(struct i40iw_sc_dev *dev,
					      u32 *vchnl_ver)
{
	struct i40iw_virtchnl_req vchnl_req;
	enum i40iw_status_code ret_code;

	if (!i40iw_vf_clear_to_send(dev))
		return I40IW_ERR_TIMEOUT;
	memset(&vchnl_req, 0, sizeof(vchnl_req));
	vchnl_req.dev = dev;
	vchnl_req.parm = vchnl_ver;
	vchnl_req.parm_len = sizeof(*vchnl_ver);
	vchnl_req.vchnl_msg = &dev->vchnl_vf_msg_buf.vchnl_msg;

	ret_code = vchnl_vf_send_get_ver_req(dev, &vchnl_req);
	if (ret_code) {
		i40iw_debug(dev, I40IW_DEBUG_VIRT,
			    "%s Send message failed 0x%0x\n", __func__, ret_code);
		return ret_code;
	}
	ret_code = i40iw_vf_wait_vchnl_resp(dev);
	if (ret_code)
		return ret_code;
	else
		return vchnl_req.ret_code;
}

/**
 * i40iw_vchnl_vf_get_hmc_fcn - Request HMC Function
 * @dev: IWARP device pointer
 * @hmc_fcn: HMC function index pointer
 */
enum i40iw_status_code i40iw_vchnl_vf_get_hmc_fcn(struct i40iw_sc_dev *dev,
						  u16 *hmc_fcn)
{
	struct i40iw_virtchnl_req vchnl_req;
	enum i40iw_status_code ret_code;

	if (!i40iw_vf_clear_to_send(dev))
		return I40IW_ERR_TIMEOUT;
	memset(&vchnl_req, 0, sizeof(vchnl_req));
	vchnl_req.dev = dev;
	vchnl_req.parm = hmc_fcn;
	vchnl_req.parm_len = sizeof(*hmc_fcn);
	vchnl_req.vchnl_msg = &dev->vchnl_vf_msg_buf.vchnl_msg;

	ret_code = vchnl_vf_send_get_hmc_fcn_req(dev, &vchnl_req);
	if (ret_code) {
		i40iw_debug(dev, I40IW_DEBUG_VIRT,
			    "%s Send message failed 0x%0x\n", __func__, ret_code);
		return ret_code;
	}
	ret_code = i40iw_vf_wait_vchnl_resp(dev);
	if (ret_code)
		return ret_code;
	else
		return vchnl_req.ret_code;
}

/**
 * i40iw_vchnl_vf_add_hmc_objs - Add HMC Object
 * @dev: IWARP device pointer
 * @rsrc_type: HMC Resource type
 * @start_index: Starting index of the objects to be added
 * @rsrc_count: Number of resources to be added
 */
enum i40iw_status_code i40iw_vchnl_vf_add_hmc_objs(struct i40iw_sc_dev *dev,
						   enum i40iw_hmc_rsrc_type rsrc_type,
						   u32 start_index,
						   u32 rsrc_count)
{
	struct i40iw_virtchnl_req vchnl_req;
	enum i40iw_status_code ret_code;

	if (!i40iw_vf_clear_to_send(dev))
		return I40IW_ERR_TIMEOUT;
	memset(&vchnl_req, 0, sizeof(vchnl_req));
	vchnl_req.dev = dev;
	vchnl_req.vchnl_msg = &dev->vchnl_vf_msg_buf.vchnl_msg;

	ret_code = vchnl_vf_send_add_hmc_objs_req(dev,
						  &vchnl_req,
						  rsrc_type,
						  start_index,
						  rsrc_count);
	if (ret_code) {
		i40iw_debug(dev, I40IW_DEBUG_VIRT,
			    "%s Send message failed 0x%0x\n", __func__, ret_code);
		return ret_code;
	}
	ret_code = i40iw_vf_wait_vchnl_resp(dev);
	if (ret_code)
		return ret_code;
	else
		return vchnl_req.ret_code;
}

/**
 * i40iw_vchnl_vf_del_hmc_obj - del HMC obj
 * @dev: IWARP device pointer
 * @rsrc_type: HMC Resource type
 * @start_index: Starting index of the object to delete
 * @rsrc_count: Number of resources to be delete
 */
enum i40iw_status_code i40iw_vchnl_vf_del_hmc_obj(struct i40iw_sc_dev *dev,
						  enum i40iw_hmc_rsrc_type rsrc_type,
						  u32 start_index,
						  u32 rsrc_count)
{
	struct i40iw_virtchnl_req vchnl_req;
	enum i40iw_status_code ret_code;

	if (!i40iw_vf_clear_to_send(dev))
		return I40IW_ERR_TIMEOUT;
	memset(&vchnl_req, 0, sizeof(vchnl_req));
	vchnl_req.dev = dev;
	vchnl_req.vchnl_msg = &dev->vchnl_vf_msg_buf.vchnl_msg;

	ret_code = vchnl_vf_send_del_hmc_objs_req(dev,
						  &vchnl_req,
						  rsrc_type,
						  start_index,
						  rsrc_count);
	if (ret_code) {
		i40iw_debug(dev, I40IW_DEBUG_VIRT,
			    "%s Send message failed 0x%0x\n", __func__, ret_code);
		return ret_code;
	}
	ret_code = i40iw_vf_wait_vchnl_resp(dev);
	if (ret_code)
		return ret_code;
	else
		return vchnl_req.ret_code;
}

/**
 * i40iw_vchnl_vf_get_pe_stats - Get PE stats
 * @dev: IWARP device pointer
 * @hw_stats: HW stats struct
 */
enum i40iw_status_code i40iw_vchnl_vf_get_pe_stats(struct i40iw_sc_dev *dev,
						   struct i40iw_dev_hw_stats *hw_stats)
{
	struct i40iw_virtchnl_req  vchnl_req;
	enum i40iw_status_code ret_code;

	if (!i40iw_vf_clear_to_send(dev))
		return I40IW_ERR_TIMEOUT;
	memset(&vchnl_req, 0, sizeof(vchnl_req));
	vchnl_req.dev = dev;
	vchnl_req.parm = hw_stats;
	vchnl_req.parm_len = sizeof(*hw_stats);
	vchnl_req.vchnl_msg = &dev->vchnl_vf_msg_buf.vchnl_msg;

	ret_code = vchnl_vf_send_get_pe_stats_req(dev, &vchnl_req);
	if (ret_code) {
		i40iw_debug(dev, I40IW_DEBUG_VIRT,
			    "%s Send message failed 0x%0x\n", __func__, ret_code);
		return ret_code;
	}
	ret_code = i40iw_vf_wait_vchnl_resp(dev);
	if (ret_code)
		return ret_code;
	else
		return vchnl_req.ret_code;
}
