/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2023 Intel Corporation */

#ifndef _IDPF_H_
#define _IDPF_H_

/* Forward declaration */
struct idpf_adapter;

#include <linux/aer.h>
#include <linux/etherdevice.h>
#include <linux/pci.h>

#include "idpf_controlq.h"

/* Default Mailbox settings */
#define IDPF_NUM_DFLT_MBX_Q		2	/* includes both TX and RX */
#define IDPF_DFLT_MBX_Q_LEN		64
#define IDPF_DFLT_MBX_ID		-1

/* available message levels */
#define IDPF_AVAIL_NETIF_M (NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_LINK)

/**
 * enum idpf_state - State machine to handle bring up
 * @__IDPF_STARTUP: Start the state machine
 * @__IDPF_STATE_LAST: Must be last, used to determine size
 */
enum idpf_state {
	__IDPF_STARTUP,
	__IDPF_STATE_LAST,
};

/**
 * enum idpf_flags - Hard reset causes.
 * @IDPF_HR_FUNC_RESET: Hard reset when TxRx timeout
 * @IDPF_HR_DRV_LOAD: Set on driver load for a clean HW
 * @IDPF_HR_RESET_IN_PROG: Reset in progress
 * @IDPF_REMOVE_IN_PROG: Driver remove in progress
 * @IDPF_FLAGS_NBITS: Must be last
 */
enum idpf_flags {
	IDPF_HR_FUNC_RESET,
	IDPF_HR_DRV_LOAD,
	IDPF_HR_RESET_IN_PROG,
	IDPF_REMOVE_IN_PROG,
	IDPF_FLAGS_NBITS,
};

/**
 * struct idpf_reset_reg - Reset register offsets/masks
 * @rstat: Reset status register
 * @rstat_m: Reset status mask
 */
struct idpf_reset_reg {
	void __iomem *rstat;
	u32 rstat_m;
};

/**
 * struct idpf_reg_ops - Device specific register operation function pointers
 * @ctlq_reg_init: Mailbox control queue register initialization
 * @reset_reg_init: Reset register initialization
 * @trigger_reset: Trigger a reset to occur
 */
struct idpf_reg_ops {
	void (*ctlq_reg_init)(struct idpf_ctlq_create_info *cq);
	void (*reset_reg_init)(struct idpf_adapter *adapter);
	void (*trigger_reset)(struct idpf_adapter *adapter,
			      enum idpf_flags trig_cause);
};

/**
 * struct idpf_dev_ops - Device specific operations
 * @reg_ops: Register operations
 */
struct idpf_dev_ops {
	struct idpf_reg_ops reg_ops;
};

/**
 * struct idpf_adapter - Device data struct generated on probe
 * @pdev: PCI device struct given on probe
 * @msg_enable: Debug message level enabled
 * @state: Init state machine
 * @flags: See enum idpf_flags
 * @reset_reg: See struct idpf_reset_reg
 * @hw: Device access data
 * @vc_event_task: Task to handle out of band virtchnl event notifications
 * @vc_event_wq: Workqueue for virtchnl events
 * @dev_ops: See idpf_dev_ops
 * @vport_ctrl_lock: Lock to protect the vport control flow
 */
struct idpf_adapter {
	struct pci_dev *pdev;
	u32 msg_enable;
	enum idpf_state state;
	DECLARE_BITMAP(flags, IDPF_FLAGS_NBITS);
	struct idpf_reset_reg reset_reg;
	struct idpf_hw hw;

	struct delayed_work vc_event_task;
	struct workqueue_struct *vc_event_wq;

	struct idpf_dev_ops dev_ops;

	struct mutex vport_ctrl_lock;
};

/**
 * idpf_get_reg_addr - Get BAR0 register address
 * @adapter: private data struct
 * @reg_offset: register offset value
 *
 * Based on the register offset, return the actual BAR0 register address
 */
static inline void __iomem *idpf_get_reg_addr(struct idpf_adapter *adapter,
					      resource_size_t reg_offset)
{
	return (void __iomem *)(adapter->hw.hw_addr + reg_offset);
}

/**
 * idpf_is_reset_detected - check if we were reset at some point
 * @adapter: driver specific private structure
 *
 * Returns true if we are either in reset currently or were previously reset.
 */
static inline bool idpf_is_reset_detected(struct idpf_adapter *adapter)
{
	if (!adapter->hw.arq)
		return true;

	return !(readl(idpf_get_reg_addr(adapter, adapter->hw.arq->reg.len)) &
		 adapter->hw.arq->reg.len_mask);
}

void idpf_vc_event_task(struct work_struct *work);
void idpf_dev_ops_init(struct idpf_adapter *adapter);
void idpf_vf_dev_ops_init(struct idpf_adapter *adapter);
int idpf_init_dflt_mbx(struct idpf_adapter *adapter);
void idpf_deinit_dflt_mbx(struct idpf_adapter *adapter);

#endif /* !_IDPF_H_ */
