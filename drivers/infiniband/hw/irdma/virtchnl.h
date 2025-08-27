/* SPDX-License-Identifier: GPL-2.0 or Linux-OpenIB */
/* Copyright (c) 2015 - 2024 Intel Corporation */
#ifndef IRDMA_VIRTCHNL_H
#define IRDMA_VIRTCHNL_H

#include "hmc.h"
#include "irdma.h"

/* IRDMA_VCHNL_CHNL_VER_V0 is for legacy hw, no longer supported. */
#define IRDMA_VCHNL_CHNL_VER_V2 2
#define IRDMA_VCHNL_CHNL_VER_MIN IRDMA_VCHNL_CHNL_VER_V2
#define IRDMA_VCHNL_CHNL_VER_MAX IRDMA_VCHNL_CHNL_VER_V2
#define IRDMA_VCHNL_OP_GET_HMC_FCN_V0 0
#define IRDMA_VCHNL_OP_GET_HMC_FCN_V1 1
#define IRDMA_VCHNL_OP_GET_HMC_FCN_V2 2
#define IRDMA_VCHNL_OP_PUT_HMC_FCN_V0 0
#define IRDMA_VCHNL_OP_GET_RDMA_CAPS_V0 0
#define IRDMA_VCHNL_OP_GET_RDMA_CAPS_MIN_SIZE 1

enum irdma_vchnl_ops {
	IRDMA_VCHNL_OP_GET_VER = 0,
	IRDMA_VCHNL_OP_GET_HMC_FCN = 1,
	IRDMA_VCHNL_OP_PUT_HMC_FCN = 2,
	IRDMA_VCHNL_OP_GET_RDMA_CAPS = 13,
};

struct irdma_vchnl_req_hmc_info {
	u8 protocol_used;
	u8 disable_qos;
} __packed;

struct irdma_vchnl_resp_hmc_info {
	u16 hmc_func;
	u16 qs_handle[IRDMA_MAX_USER_PRIORITY];
} __packed;

struct irdma_vchnl_op_buf {
	u16 op_code;
	u16 op_ver;
	u16 buf_len;
	u16 rsvd;
	u64 op_ctx;
	u8 buf[];
} __packed;

struct irdma_vchnl_resp_buf {
	u64 op_ctx;
	u16 buf_len;
	s16 op_ret;
	u16 rsvd[2];
	u8 buf[];
} __packed;

struct irdma_vchnl_rdma_caps {
	u8 hw_rev;
	u16 cqp_timeout_s;
	u16 cqp_def_timeout_s;
	u16 max_hw_push_len;
} __packed;

struct irdma_vchnl_init_info {
	struct workqueue_struct *vchnl_wq;
	enum irdma_vers hw_rev;
	bool privileged;
	bool is_pf;
};

struct irdma_vchnl_req {
	struct irdma_vchnl_op_buf *vchnl_msg;
	void *parm;
	u32 vf_id;
	u16 parm_len;
	u16 resp_len;
};

struct irdma_vchnl_req_init_info {
	void *req_parm;
	void *resp_parm;
	u16 req_parm_len;
	u16 resp_parm_len;
	u16 op_code;
	u16 op_ver;
} __packed;

int irdma_sc_vchnl_init(struct irdma_sc_dev *dev,
			struct irdma_vchnl_init_info *info);
int irdma_vchnl_req_get_ver(struct irdma_sc_dev *dev, u16 ver_req,
			    u32 *ver_res);
int irdma_vchnl_req_get_hmc_fcn(struct irdma_sc_dev *dev);
int irdma_vchnl_req_put_hmc_fcn(struct irdma_sc_dev *dev);
int irdma_vchnl_req_get_caps(struct irdma_sc_dev *dev);
int irdma_vchnl_req_get_resp(struct irdma_sc_dev *dev,
			     struct irdma_vchnl_req *vc_req);
#endif /* IRDMA_VIRTCHNL_H */
