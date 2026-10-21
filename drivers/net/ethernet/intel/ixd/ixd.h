/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2025 Intel Corporation */

#ifndef _IXD_H_
#define _IXD_H_

#include <linux/net/intel/libie/controlq.h>

#define IXD_INIT_TASK_DELAY_JIFFIES	msecs_to_jiffies(500)

/**
 * struct ixd_adapter - Data structure representing a CPF
 * @cp_ctx: Control plane communication context
 * @init_task: Delayed initialization after reset
 * @init_task.init_work: Delayed initialization work
 * @init_task.reset_retries: How many times to check, whether reset is completed
 * @init_task.vc_retries: Number of retries to establish mailbox communication
 * @init_task.success: init_work completion status
 * @mbx_task: Control queue Rx handling
 * @xnm: virtchnl transaction manager
 * @asq: Send control queue info
 * @arq: Receive control queue info
 * @vc_ver: Negotiated virtchnl version
 * @vc_ver.major: Negotiated major virtchnl version
 * @vc_ver.minor: Negotiated minor virtchnl version
 * @caps: Negotiated virtchnl capabilities
 */
struct ixd_adapter {
	struct libie_ctlq_ctx cp_ctx;
	struct {
		struct delayed_work init_work;
		u8 reset_retries;
		u8 vc_retries;
		bool success;
	} init_task;
	struct delayed_work mbx_task;
	struct libie_ctlq_xn_manager *xnm;
	struct libie_ctlq_info *asq;
	struct libie_ctlq_info *arq;
	struct {
		u32 major;
		u32 minor;
	} vc_ver;
	struct virtchnl2_get_capabilities caps;
};

/**
 * ixd_to_dev - Get the corresponding device struct from an adapter
 * @adapter: PCI device driver-specific private data
 *
 * Return: struct device corresponding to the given adapter
 */
static inline struct device *ixd_to_dev(struct ixd_adapter *adapter)
{
	return &adapter->cp_ctx.mmio_info.pdev->dev;
}

void ixd_ctlq_reg_init(struct ixd_adapter *adapter,
		       struct libie_ctlq_reg *ctlq_reg_tx,
		       struct libie_ctlq_reg *ctlq_reg_rx);
void ixd_trigger_reset(struct ixd_adapter *adapter);
bool ixd_check_reset_complete(struct ixd_adapter *adapter);
void ixd_init_task(struct work_struct *work);
int ixd_init_dflt_mbx(struct ixd_adapter *adapter);
void ixd_deinit_dflt_mbx(struct ixd_adapter *adapter);

#endif /* _IXD_H_ */
