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
