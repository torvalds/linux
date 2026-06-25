/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2025 Intel Corporation */

#ifndef _IXD_CTLQ_H_
#define _IXD_CTLQ_H_

#include <linux/net/intel/virtchnl2.h>

#define IXD_CTLQ_TIMEOUT 2000

/**
 * struct ixd_ctlq_req - Standard virtchnl request description
 * @opcode: protocol opcode, only virtchnl2 is needed for now
 * @send_size: required length of the send buffer
 * @send_buff_init: function to initialize the allocated send buffer
 * @recv_process: function to handle the CP response
 * @ctx: additional context for callbacks
 */
struct ixd_ctlq_req {
	enum virtchnl2_op opcode;
	size_t send_size;
	void (*send_buff_init)(struct ixd_adapter *adapter, void *send_buff,
			       void *ctx);
	int (*recv_process)(struct ixd_adapter *adapter, void *recv_buff,
			    size_t recv_size, void *ctx);
	void *ctx;
};

void ixd_ctlq_clean_sq(struct ixd_adapter *adapter, bool force);
int ixd_ctlq_do_req(struct ixd_adapter *adapter,
		    const struct ixd_ctlq_req *req);
void ixd_ctlq_rx_task(struct work_struct *work);

#endif /* _IXD_CTLQ_H_ */
