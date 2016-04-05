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

#ifndef I40IW_VIRTCHNL_H
#define I40IW_VIRTCHNL_H

#include "i40iw_hmc.h"

#pragma pack(push, 1)

struct i40iw_virtchnl_op_buf {
	u16 iw_op_code;
	u16 iw_op_ver;
	u16 iw_chnl_buf_len;
	u16 rsvd;
	u64 iw_chnl_op_ctx;
	/* Member alignment MUST be maintained above this location */
	u8 iw_chnl_buf[1];
};

struct i40iw_virtchnl_resp_buf {
	u64 iw_chnl_op_ctx;
	u16 iw_chnl_buf_len;
	s16 iw_op_ret_code;
	/* Member alignment MUST be maintained above this location */
	u16 rsvd[2];
	u8 iw_chnl_buf[1];
};

enum i40iw_virtchnl_ops {
	I40IW_VCHNL_OP_GET_VER = 0,
	I40IW_VCHNL_OP_GET_HMC_FCN,
	I40IW_VCHNL_OP_ADD_HMC_OBJ_RANGE,
	I40IW_VCHNL_OP_DEL_HMC_OBJ_RANGE,
	I40IW_VCHNL_OP_GET_STATS
};

#define I40IW_VCHNL_OP_GET_VER_V0 0
#define I40IW_VCHNL_OP_GET_HMC_FCN_V0 0
#define I40IW_VCHNL_OP_ADD_HMC_OBJ_RANGE_V0 0
#define I40IW_VCHNL_OP_DEL_HMC_OBJ_RANGE_V0 0
#define I40IW_VCHNL_OP_GET_STATS_V0 0
#define I40IW_VCHNL_CHNL_VER_V0 0

struct i40iw_dev_hw_stats;

struct i40iw_virtchnl_hmc_obj_range {
	u16 obj_type;
	u16 rsvd;
	u32 start_index;
	u32 obj_count;
};

enum i40iw_status_code i40iw_vchnl_recv_pf(struct i40iw_sc_dev *dev,
					   u32 vf_id,
					   u8 *msg,
					   u16 len);

enum i40iw_status_code i40iw_vchnl_recv_vf(struct i40iw_sc_dev *dev,
					   u32 vf_id,
					   u8 *msg,
					   u16 len);

struct i40iw_virtchnl_req {
	struct i40iw_sc_dev *dev;
	struct i40iw_virtchnl_op_buf *vchnl_msg;
	void *parm;
	u32 vf_id;
	u16 parm_len;
	s16 ret_code;
};

#pragma pack(pop)

enum i40iw_status_code i40iw_vchnl_vf_get_ver(struct i40iw_sc_dev *dev,
					      u32 *vchnl_ver);

enum i40iw_status_code i40iw_vchnl_vf_get_hmc_fcn(struct i40iw_sc_dev *dev,
						  u16 *hmc_fcn);

enum i40iw_status_code i40iw_vchnl_vf_add_hmc_objs(struct i40iw_sc_dev *dev,
						   enum i40iw_hmc_rsrc_type rsrc_type,
						   u32 start_index,
						   u32 rsrc_count);

enum i40iw_status_code i40iw_vchnl_vf_del_hmc_obj(struct i40iw_sc_dev *dev,
						  enum i40iw_hmc_rsrc_type rsrc_type,
						  u32 start_index,
						  u32 rsrc_count);

enum i40iw_status_code i40iw_vchnl_vf_get_pe_stats(struct i40iw_sc_dev *dev,
						   struct i40iw_dev_hw_stats *hw_stats);
#endif
