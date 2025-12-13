// SPDX-License-Identifier: GPL-2.0 or Linux-OpenIB
/* Copyright (c) 2015 - 2024 Intel Corporation */

#include "osdep.h"
#include "hmc.h"
#include "defs.h"
#include "type.h"
#include "protos.h"
#include "virtchnl.h"
#include "ws.h"
#include "i40iw_hw.h"
#include "ig3rdma_hw.h"

struct vchnl_reg_map_elem {
	u16 reg_id;
	u16 reg_idx;
	bool pg_rel;
};

struct vchnl_regfld_map_elem {
	u16 regfld_id;
	u16 regfld_idx;
};

static struct vchnl_reg_map_elem vchnl_reg_map[] = {
	{IRDMA_VCHNL_REG_ID_CQPTAIL, IRDMA_CQPTAIL, false},
	{IRDMA_VCHNL_REG_ID_CQPDB, IRDMA_CQPDB, false},
	{IRDMA_VCHNL_REG_ID_CCQPSTATUS, IRDMA_CCQPSTATUS, false},
	{IRDMA_VCHNL_REG_ID_CCQPHIGH, IRDMA_CCQPHIGH, false},
	{IRDMA_VCHNL_REG_ID_CCQPLOW, IRDMA_CCQPLOW, false},
	{IRDMA_VCHNL_REG_ID_CQARM, IRDMA_CQARM, false},
	{IRDMA_VCHNL_REG_ID_CQACK, IRDMA_CQACK, false},
	{IRDMA_VCHNL_REG_ID_AEQALLOC, IRDMA_AEQALLOC, false},
	{IRDMA_VCHNL_REG_ID_CQPERRCODES, IRDMA_CQPERRCODES, false},
	{IRDMA_VCHNL_REG_ID_WQEALLOC, IRDMA_WQEALLOC, false},
	{IRDMA_VCHNL_REG_ID_DB_ADDR_OFFSET, IRDMA_DB_ADDR_OFFSET, false },
	{IRDMA_VCHNL_REG_ID_DYN_CTL, IRDMA_GLINT_DYN_CTL, false },
	{IRDMA_VCHNL_REG_INV_ID, IRDMA_VCHNL_REG_INV_ID, false }
};

static struct vchnl_regfld_map_elem vchnl_regfld_map[] = {
	{IRDMA_VCHNL_REGFLD_ID_CCQPSTATUS_CQP_OP_ERR, IRDMA_CCQPSTATUS_CCQP_ERR_M},
	{IRDMA_VCHNL_REGFLD_ID_CCQPSTATUS_CCQP_DONE, IRDMA_CCQPSTATUS_CCQP_DONE_M},
	{IRDMA_VCHNL_REGFLD_ID_CQPSQ_STAG_PDID, IRDMA_CQPSQ_STAG_PDID_M},
	{IRDMA_VCHNL_REGFLD_ID_CQPSQ_CQ_CEQID, IRDMA_CQPSQ_CQ_CEQID_M},
	{IRDMA_VCHNL_REGFLD_ID_CQPSQ_CQ_CQID, IRDMA_CQPSQ_CQ_CQID_M},
	{IRDMA_VCHNL_REGFLD_ID_COMMIT_FPM_CQCNT, IRDMA_COMMIT_FPM_CQCNT_M},
	{IRDMA_VCHNL_REGFLD_ID_UPESD_HMCN_ID, IRDMA_CQPSQ_UPESD_HMCFNID_M},
	{IRDMA_VCHNL_REGFLD_INV_ID, IRDMA_VCHNL_REGFLD_INV_ID}
};

#define IRDMA_VCHNL_REG_COUNT ARRAY_SIZE(vchnl_reg_map)
#define IRDMA_VCHNL_REGFLD_COUNT ARRAY_SIZE(vchnl_regfld_map)
#define IRDMA_VCHNL_REGFLD_BUF_SIZE \
	(IRDMA_VCHNL_REG_COUNT * sizeof(struct irdma_vchnl_reg_info) + \
	 IRDMA_VCHNL_REGFLD_COUNT * sizeof(struct irdma_vchnl_reg_field_info))
#define IRDMA_REGMAP_RESP_BUF_SIZE (IRDMA_VCHNL_RESP_MIN_SIZE + IRDMA_VCHNL_REGFLD_BUF_SIZE)

/**
 * irdma_sc_vchnl_init - Initialize dev virtchannel and get hw_rev
 * @dev: dev structure to update
 * @info: virtchannel info parameters to fill into the dev structure
 */
int irdma_sc_vchnl_init(struct irdma_sc_dev *dev,
			struct irdma_vchnl_init_info *info)
{
	dev->vchnl_up = true;
	dev->privileged = info->privileged;
	dev->is_pf = info->is_pf;
	dev->hw_attrs.uk_attrs.hw_rev = info->hw_rev;

	if (!dev->privileged) {
		int ret = irdma_vchnl_req_get_ver(dev, IRDMA_VCHNL_CHNL_VER_MAX,
						  &dev->vchnl_ver);

		ibdev_dbg(to_ibdev(dev),
			  "DEV: Get Channel version ret = %d, version is %u\n",
			  ret, dev->vchnl_ver);

		if (ret)
			return ret;

		ret = irdma_vchnl_req_get_caps(dev);
		if (ret)
			return ret;

		dev->hw_attrs.uk_attrs.hw_rev = dev->vc_caps.hw_rev;
	}

	return 0;
}

/**
 * irdma_vchnl_req_verify_resp - Verify requested response size
 * @vchnl_req: vchnl message requested
 * @resp_len: response length sent from vchnl peer
 */
static int irdma_vchnl_req_verify_resp(struct irdma_vchnl_req *vchnl_req,
				       u16 resp_len)
{
	switch (vchnl_req->vchnl_msg->op_code) {
	case IRDMA_VCHNL_OP_GET_VER:
	case IRDMA_VCHNL_OP_GET_HMC_FCN:
	case IRDMA_VCHNL_OP_PUT_HMC_FCN:
		if (resp_len != vchnl_req->parm_len)
			return -EBADMSG;
		break;
	case IRDMA_VCHNL_OP_GET_RDMA_CAPS:
		if (resp_len < IRDMA_VCHNL_OP_GET_RDMA_CAPS_MIN_SIZE)
			return -EBADMSG;
		break;
	case IRDMA_VCHNL_OP_GET_REG_LAYOUT:
	case IRDMA_VCHNL_OP_QUEUE_VECTOR_MAP:
	case IRDMA_VCHNL_OP_QUEUE_VECTOR_UNMAP:
	case IRDMA_VCHNL_OP_ADD_VPORT:
	case IRDMA_VCHNL_OP_DEL_VPORT:
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static void irdma_free_vchnl_req_msg(struct irdma_vchnl_req *vchnl_req)
{
	kfree(vchnl_req->vchnl_msg);
}

static int irdma_alloc_vchnl_req_msg(struct irdma_vchnl_req *vchnl_req,
				     struct irdma_vchnl_req_init_info *info)
{
	struct irdma_vchnl_op_buf *vchnl_msg;

	vchnl_msg = kzalloc(IRDMA_VCHNL_MAX_MSG_SIZE, GFP_KERNEL);

	if (!vchnl_msg)
		return -ENOMEM;

	vchnl_msg->op_ctx = (uintptr_t)vchnl_req;
	vchnl_msg->buf_len = sizeof(*vchnl_msg) + info->req_parm_len;
	if (info->req_parm_len)
		memcpy(vchnl_msg->buf, info->req_parm, info->req_parm_len);
	vchnl_msg->op_code = info->op_code;
	vchnl_msg->op_ver = info->op_ver;

	vchnl_req->vchnl_msg = vchnl_msg;
	vchnl_req->parm = info->resp_parm;
	vchnl_req->parm_len = info->resp_parm_len;

	return 0;
}

static int irdma_vchnl_req_send_sync(struct irdma_sc_dev *dev,
				     struct irdma_vchnl_req_init_info *info)
{
	u16 resp_len = sizeof(dev->vc_recv_buf);
	struct irdma_vchnl_req vchnl_req = {};
	u16 msg_len;
	u8 *msg;
	int ret;

	ret = irdma_alloc_vchnl_req_msg(&vchnl_req, info);
	if (ret)
		return ret;

	msg_len = vchnl_req.vchnl_msg->buf_len;
	msg = (u8 *)vchnl_req.vchnl_msg;

	mutex_lock(&dev->vchnl_mutex);
	ret = ig3rdma_vchnl_send_sync(dev, msg, msg_len, dev->vc_recv_buf,
				      &resp_len);
	dev->vc_recv_len = resp_len;
	if (ret)
		goto exit;

	ret = irdma_vchnl_req_get_resp(dev, &vchnl_req);
exit:
	mutex_unlock(&dev->vchnl_mutex);
	ibdev_dbg(to_ibdev(dev),
		  "VIRT: virtual channel send %s caller: %pS ret=%d op=%u op_ver=%u req_len=%u parm_len=%u resp_len=%u\n",
		  !ret ? "SUCCEEDS" : "FAILS", __builtin_return_address(0),
		  ret, vchnl_req.vchnl_msg->op_code,
		  vchnl_req.vchnl_msg->op_ver, vchnl_req.vchnl_msg->buf_len,
		  vchnl_req.parm_len, vchnl_req.resp_len);
	irdma_free_vchnl_req_msg(&vchnl_req);

	return ret;
}

/**
 * irdma_vchnl_req_get_reg_layout - Get Register Layout
 * @dev: RDMA device pointer
 */
int irdma_vchnl_req_get_reg_layout(struct irdma_sc_dev *dev)
{
	u16 reg_idx, reg_id, tmp_reg_id, regfld_idx, regfld_id, tmp_regfld_id;
	struct irdma_vchnl_reg_field_info *regfld_array = NULL;
	u8 resp_buffer[IRDMA_REGMAP_RESP_BUF_SIZE] = {};
	struct vchnl_regfld_map_elem *regfld_map_array;
	struct irdma_vchnl_req_init_info info = {};
	struct vchnl_reg_map_elem *reg_map_array;
	struct irdma_vchnl_reg_info *reg_array;
	u8 num_bits, shift_cnt;
	u16 buf_len = 0;
	u64 bitmask;
	u32 rindex;
	int ret;

	if (!dev->vchnl_up)
		return -EBUSY;

	info.op_code = IRDMA_VCHNL_OP_GET_REG_LAYOUT;
	info.op_ver = IRDMA_VCHNL_OP_GET_REG_LAYOUT_V0;
	info.resp_parm = resp_buffer;
	info.resp_parm_len = sizeof(resp_buffer);

	ret = irdma_vchnl_req_send_sync(dev, &info);

	if (ret)
		return ret;

	/* parse the response buffer and update reg info*/
	/* Parse registers till invalid */
	/* Parse register fields till invalid */
	reg_array = (struct irdma_vchnl_reg_info *)resp_buffer;
	for (rindex = 0; rindex < IRDMA_VCHNL_REG_COUNT; rindex++) {
		buf_len += sizeof(struct irdma_vchnl_reg_info);
		if (buf_len >= sizeof(resp_buffer))
			return -ENOMEM;

		regfld_array =
			(struct irdma_vchnl_reg_field_info *)&reg_array[rindex + 1];
		reg_id = reg_array[rindex].reg_id;
		if (reg_id == IRDMA_VCHNL_REG_INV_ID)
			break;

		reg_id &= ~IRDMA_VCHNL_REG_PAGE_REL;
		if (reg_id >= IRDMA_VCHNL_REG_COUNT)
			return -EINVAL;

		/* search regmap for register index in hw_regs.*/
		reg_map_array = vchnl_reg_map;
		do {
			tmp_reg_id = reg_map_array->reg_id;
			if (tmp_reg_id == reg_id)
				break;

			reg_map_array++;
		} while (tmp_reg_id != IRDMA_VCHNL_REG_INV_ID);
		if (tmp_reg_id != reg_id)
			continue;

		reg_idx = reg_map_array->reg_idx;

		/* Page relative, DB Offset do not need bar offset */
		if (reg_idx == IRDMA_DB_ADDR_OFFSET ||
		    (reg_array[rindex].reg_id & IRDMA_VCHNL_REG_PAGE_REL)) {
			dev->hw_regs[reg_idx] =
				(u32 __iomem *)(uintptr_t)reg_array[rindex].reg_offset;
			continue;
		}

		/* Update the local HW struct */
		dev->hw_regs[reg_idx] = ig3rdma_get_reg_addr(dev->hw,
						reg_array[rindex].reg_offset);
		if (!dev->hw_regs[reg_idx])
			return -EINVAL;
	}

	if (!regfld_array)
		return -ENOMEM;

	/* set up doorbell variables using mapped DB page */
	dev->wqe_alloc_db = dev->hw_regs[IRDMA_WQEALLOC];
	dev->cq_arm_db = dev->hw_regs[IRDMA_CQARM];
	dev->aeq_alloc_db = dev->hw_regs[IRDMA_AEQALLOC];
	dev->cqp_db = dev->hw_regs[IRDMA_CQPDB];
	dev->cq_ack_db = dev->hw_regs[IRDMA_CQACK];

	for (rindex = 0; rindex < IRDMA_VCHNL_REGFLD_COUNT; rindex++) {
		buf_len += sizeof(struct irdma_vchnl_reg_field_info);
		if ((buf_len - 1) > sizeof(resp_buffer))
			break;

		if (regfld_array[rindex].fld_id == IRDMA_VCHNL_REGFLD_INV_ID)
			break;

		regfld_id = regfld_array[rindex].fld_id;
		regfld_map_array = vchnl_regfld_map;
		do {
			tmp_regfld_id = regfld_map_array->regfld_id;
			if (tmp_regfld_id == regfld_id)
				break;

			regfld_map_array++;
		} while (tmp_regfld_id != IRDMA_VCHNL_REGFLD_INV_ID);

		if (tmp_regfld_id != regfld_id)
			continue;

		regfld_idx = regfld_map_array->regfld_idx;

		num_bits = regfld_array[rindex].fld_bits;
		shift_cnt = regfld_array[rindex].fld_shift;
		if ((num_bits + shift_cnt > 64) || !num_bits) {
			ibdev_dbg(to_ibdev(dev),
				  "ERR: Invalid field mask id %d bits %d shift %d",
				  regfld_id, num_bits, shift_cnt);

			continue;
		}

		bitmask = (1ULL << num_bits) - 1;
		dev->hw_masks[regfld_idx] = bitmask << shift_cnt;
		dev->hw_shifts[regfld_idx] = shift_cnt;
	}

	return 0;
}

int irdma_vchnl_req_add_vport(struct irdma_sc_dev *dev, u16 vport_id,
			      u32 qp1_id, struct irdma_qos *qos)
{
	struct irdma_vchnl_resp_vport_info resp_vport = { 0 };
	struct irdma_vchnl_req_vport_info req_vport = { 0 };
	struct irdma_vchnl_req_init_info info = { 0 };
	int ret, i;

	if (!dev->vchnl_up)
		return -EBUSY;

	info.op_code = IRDMA_VCHNL_OP_ADD_VPORT;
	info.op_ver = IRDMA_VCHNL_OP_ADD_VPORT_V0;
	req_vport.vport_id = vport_id;
	req_vport.qp1_id = qp1_id;
	info.req_parm_len = sizeof(req_vport);
	info.req_parm = &req_vport;
	info.resp_parm = &resp_vport;
	info.resp_parm_len = sizeof(resp_vport);

	ret = irdma_vchnl_req_send_sync(dev, &info);
	if (ret)
		return ret;

	for (i = 0;  i < IRDMA_MAX_USER_PRIORITY; i++) {
		qos[i].qs_handle = resp_vport.qs_handle[i];
		qos[i].valid = true;
	}

	return 0;
}

int irdma_vchnl_req_del_vport(struct irdma_sc_dev *dev, u16 vport_id, u32 qp1_id)
{
	struct irdma_vchnl_req_init_info info = { 0 };
	struct irdma_vchnl_req_vport_info req_vport = { 0 };

	if (!dev->vchnl_up)
		return -EBUSY;

	info.op_code = IRDMA_VCHNL_OP_DEL_VPORT;
	info.op_ver = IRDMA_VCHNL_OP_DEL_VPORT_V0;
	req_vport.vport_id = vport_id;
	req_vport.qp1_id = qp1_id;
	info.req_parm_len = sizeof(req_vport);
	info.req_parm = &req_vport;

	return irdma_vchnl_req_send_sync(dev, &info);
}

/**
 * irdma_vchnl_req_aeq_vec_map - Map AEQ to vector on this function
 * @dev: RDMA device pointer
 * @v_idx: vector index
 */
int irdma_vchnl_req_aeq_vec_map(struct irdma_sc_dev *dev, u32 v_idx)
{
	struct irdma_vchnl_req_init_info info = {};
	struct irdma_vchnl_qvlist_info *qvl;
	struct irdma_vchnl_qv_info *qv;
	u16 qvl_size, num_vectors = 1;
	int ret;

	if (!dev->vchnl_up)
		return -EBUSY;

	qvl_size = struct_size(qvl, qv_info, num_vectors);

	qvl = kzalloc(qvl_size, GFP_KERNEL);
	if (!qvl)
		return -ENOMEM;

	qvl->num_vectors = 1;
	qv = qvl->qv_info;

	qv->ceq_idx = IRDMA_Q_INVALID_IDX;
	qv->v_idx = v_idx;
	qv->itr_idx = IRDMA_IDX_ITR0;

	info.op_code = IRDMA_VCHNL_OP_QUEUE_VECTOR_MAP;
	info.op_ver = IRDMA_VCHNL_OP_QUEUE_VECTOR_MAP_V0;
	info.req_parm = qvl;
	info.req_parm_len = qvl_size;

	ret = irdma_vchnl_req_send_sync(dev, &info);
	kfree(qvl);

	return ret;
}

/**
 * irdma_vchnl_req_ceq_vec_map - Map CEQ to vector on this function
 * @dev: RDMA device pointer
 * @ceq_id: CEQ index
 * @v_idx: vector index
 */
int irdma_vchnl_req_ceq_vec_map(struct irdma_sc_dev *dev, u16 ceq_id, u32 v_idx)
{
	struct irdma_vchnl_req_init_info info = {};
	struct irdma_vchnl_qvlist_info *qvl;
	struct irdma_vchnl_qv_info *qv;
	u16 qvl_size, num_vectors = 1;
	int ret;

	if (!dev->vchnl_up)
		return -EBUSY;

	qvl_size = struct_size(qvl, qv_info, num_vectors);

	qvl = kzalloc(qvl_size, GFP_KERNEL);
	if (!qvl)
		return -ENOMEM;

	qvl->num_vectors = num_vectors;
	qv = qvl->qv_info;

	qv->aeq_idx = IRDMA_Q_INVALID_IDX;
	qv->ceq_idx = ceq_id;
	qv->v_idx = v_idx;
	qv->itr_idx = IRDMA_IDX_ITR0;

	info.op_code = IRDMA_VCHNL_OP_QUEUE_VECTOR_MAP;
	info.op_ver = IRDMA_VCHNL_OP_QUEUE_VECTOR_MAP_V0;
	info.req_parm = qvl;
	info.req_parm_len = qvl_size;

	ret = irdma_vchnl_req_send_sync(dev, &info);
	kfree(qvl);

	return ret;
}

/**
 * irdma_vchnl_req_get_ver - Request Channel version
 * @dev: RDMA device pointer
 * @ver_req: Virtual channel version requested
 * @ver_res: Virtual channel version response
 */
int irdma_vchnl_req_get_ver(struct irdma_sc_dev *dev, u16 ver_req, u32 *ver_res)
{
	struct irdma_vchnl_req_init_info info = {};
	int ret;

	if (!dev->vchnl_up)
		return -EBUSY;

	info.op_code = IRDMA_VCHNL_OP_GET_VER;
	info.op_ver = ver_req;
	info.resp_parm = ver_res;
	info.resp_parm_len = sizeof(*ver_res);

	ret = irdma_vchnl_req_send_sync(dev, &info);
	if (ret)
		return ret;

	if (*ver_res < IRDMA_VCHNL_CHNL_VER_MIN) {
		ibdev_dbg(to_ibdev(dev),
			  "VIRT: %s unsupported vchnl version 0x%0x\n",
			  __func__, *ver_res);
		return -EOPNOTSUPP;
	}

	return 0;
}

/**
 * irdma_vchnl_req_get_hmc_fcn - Request VF HMC Function
 * @dev: RDMA device pointer
 */
int irdma_vchnl_req_get_hmc_fcn(struct irdma_sc_dev *dev)
{
	struct irdma_vchnl_req_hmc_info req_hmc = {};
	struct irdma_vchnl_resp_hmc_info resp_hmc = {};
	struct irdma_vchnl_req_init_info info = {};
	int ret;

	if (!dev->vchnl_up)
		return -EBUSY;

	info.op_code = IRDMA_VCHNL_OP_GET_HMC_FCN;
	if (dev->hw_attrs.uk_attrs.hw_rev >= IRDMA_GEN_3) {
		info.op_ver = IRDMA_VCHNL_OP_GET_HMC_FCN_V2;
		req_hmc.protocol_used = dev->protocol_used;
		info.req_parm_len = sizeof(req_hmc);
		info.req_parm = &req_hmc;
		info.resp_parm = &resp_hmc;
		info.resp_parm_len = sizeof(resp_hmc);
	}

	ret = irdma_vchnl_req_send_sync(dev, &info);

	if (ret)
		return ret;

	if (dev->hw_attrs.uk_attrs.hw_rev >= IRDMA_GEN_3) {
		int i;

		dev->hmc_fn_id = resp_hmc.hmc_func;

		for (i = 0;  i < IRDMA_MAX_USER_PRIORITY; i++) {
			dev->qos[i].qs_handle = resp_hmc.qs_handle[i];
			dev->qos[i].valid = true;
		}
	}
	return 0;
}

/**
 * irdma_vchnl_req_put_hmc_fcn - Free VF HMC Function
 * @dev: RDMA device pointer
 */
int irdma_vchnl_req_put_hmc_fcn(struct irdma_sc_dev *dev)
{
	struct irdma_vchnl_req_init_info info = {};

	if (!dev->vchnl_up)
		return -EBUSY;

	info.op_code = IRDMA_VCHNL_OP_PUT_HMC_FCN;
	info.op_ver = IRDMA_VCHNL_OP_PUT_HMC_FCN_V0;

	return irdma_vchnl_req_send_sync(dev, &info);
}

/**
 * irdma_vchnl_req_get_caps - Request RDMA capabilities
 * @dev: RDMA device pointer
 */
int irdma_vchnl_req_get_caps(struct irdma_sc_dev *dev)
{
	struct irdma_vchnl_req_init_info info = {};
	int ret;

	if (!dev->vchnl_up)
		return -EBUSY;

	info.op_code = IRDMA_VCHNL_OP_GET_RDMA_CAPS;
	info.op_ver = IRDMA_VCHNL_OP_GET_RDMA_CAPS_V0;
	info.resp_parm = &dev->vc_caps;
	info.resp_parm_len = sizeof(dev->vc_caps);

	ret = irdma_vchnl_req_send_sync(dev, &info);

	if (ret)
		return ret;

	if (dev->vc_caps.hw_rev > IRDMA_GEN_MAX ||
	    dev->vc_caps.hw_rev < IRDMA_GEN_2) {
		ibdev_dbg(to_ibdev(dev),
			  "ERR: %s unsupported hw_rev version 0x%0x\n",
			  __func__, dev->vc_caps.hw_rev);
		return -EOPNOTSUPP;
	}

	return 0;
}

/**
 * irdma_vchnl_req_get_resp - Receive the inbound vchnl response.
 * @dev: Dev pointer
 * @vchnl_req: Vchannel request
 */
int irdma_vchnl_req_get_resp(struct irdma_sc_dev *dev,
			     struct irdma_vchnl_req *vchnl_req)
{
	struct irdma_vchnl_resp_buf *vchnl_msg_resp =
		(struct irdma_vchnl_resp_buf *)dev->vc_recv_buf;
	u16 resp_len;
	int ret;

	if ((uintptr_t)vchnl_req != (uintptr_t)vchnl_msg_resp->op_ctx) {
		ibdev_dbg(to_ibdev(dev),
			  "VIRT: error vchnl context value does not match\n");
		return -EBADMSG;
	}

	resp_len = dev->vc_recv_len - sizeof(*vchnl_msg_resp);
	resp_len = min(resp_len, vchnl_req->parm_len);

	ret = irdma_vchnl_req_verify_resp(vchnl_req, resp_len);
	if (ret)
		return ret;

	ret = (int)vchnl_msg_resp->op_ret;
	if (ret)
		return ret;

	vchnl_req->resp_len = 0;
	if (vchnl_req->parm_len && vchnl_req->parm && resp_len) {
		memcpy(vchnl_req->parm, vchnl_msg_resp->buf, resp_len);
		vchnl_req->resp_len = resp_len;
		ibdev_dbg(to_ibdev(dev), "VIRT: Got response, data size %u\n",
			  resp_len);
	}

	return 0;
}
