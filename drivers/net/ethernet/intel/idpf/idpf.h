/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2023 Intel Corporation */

#ifndef _IDPF_H_
#define _IDPF_H_

/* Forward declaration */
struct idpf_adapter;

#include <linux/aer.h>
#include <linux/etherdevice.h>
#include <linux/pci.h>

#include "virtchnl2.h"
#include "idpf_txrx.h"
#include "idpf_controlq.h"

/* Default Mailbox settings */
#define IDPF_NUM_DFLT_MBX_Q		2	/* includes both TX and RX */
#define IDPF_DFLT_MBX_Q_LEN		64
#define IDPF_DFLT_MBX_ID		-1
/* maximum number of times to try before resetting mailbox */
#define IDPF_MB_MAX_ERR			20
#define IDPF_WAIT_FOR_EVENT_TIMEO_MIN	2000
#define IDPF_WAIT_FOR_EVENT_TIMEO	60000

#define IDPF_MAX_WAIT			500

/* available message levels */
#define IDPF_AVAIL_NETIF_M (NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_LINK)

#define IDPF_VIRTCHNL_VERSION_MAJOR VIRTCHNL2_VERSION_MAJOR_2
#define IDPF_VIRTCHNL_VERSION_MINOR VIRTCHNL2_VERSION_MINOR_0

/**
 * enum idpf_state - State machine to handle bring up
 * @__IDPF_STARTUP: Start the state machine
 * @__IDPF_VER_CHECK: Negotiate virtchnl version
 * @__IDPF_GET_CAPS: Negotiate capabilities
 * @__IDPF_INIT_SW: Init based on given capabilities
 * @__IDPF_STATE_LAST: Must be last, used to determine size
 */
enum idpf_state {
	__IDPF_STARTUP,
	__IDPF_VER_CHECK,
	__IDPF_GET_CAPS,
	__IDPF_INIT_SW,
	__IDPF_STATE_LAST,
};

/**
 * enum idpf_flags - Hard reset causes.
 * @IDPF_HR_FUNC_RESET: Hard reset when TxRx timeout
 * @IDPF_HR_DRV_LOAD: Set on driver load for a clean HW
 * @IDPF_HR_RESET_IN_PROG: Reset in progress
 * @IDPF_REMOVE_IN_PROG: Driver remove in progress
 * @IDPF_MB_INTR_MODE: Mailbox in interrupt mode
 * @IDPF_FLAGS_NBITS: Must be last
 */
enum idpf_flags {
	IDPF_HR_FUNC_RESET,
	IDPF_HR_DRV_LOAD,
	IDPF_HR_RESET_IN_PROG,
	IDPF_REMOVE_IN_PROG,
	IDPF_MB_INTR_MODE,
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
 * @mb_intr_reg_init: Mailbox interrupt register initialization
 * @reset_reg_init: Reset register initialization
 * @trigger_reset: Trigger a reset to occur
 */
struct idpf_reg_ops {
	void (*ctlq_reg_init)(struct idpf_ctlq_create_info *cq);
	void (*mb_intr_reg_init)(struct idpf_adapter *adapter);
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

/* These macros allow us to generate an enum and a matching char * array of
 * stringified enums that are always in sync. Checkpatch issues a bogus warning
 * about this being a complex macro; but it's wrong, these are never used as a
 * statement and instead only used to define the enum and array.
 */
#define IDPF_FOREACH_VPORT_VC_STATE(STATE)	\
	STATE(IDPF_VC_ALLOC_VECTORS)		\
	STATE(IDPF_VC_ALLOC_VECTORS_ERR)	\
	STATE(IDPF_VC_DEALLOC_VECTORS)		\
	STATE(IDPF_VC_DEALLOC_VECTORS_ERR)	\
	STATE(IDPF_VC_NBITS)

#define IDPF_GEN_ENUM(ENUM) ENUM,
#define IDPF_GEN_STRING(STRING) #STRING,

enum idpf_vport_vc_state {
	IDPF_FOREACH_VPORT_VC_STATE(IDPF_GEN_ENUM)
};

extern const char * const idpf_vport_vc_state_str[];

/**
 * struct idpf_vport - Handle for netdevices and queue resources
 * @vport_id: Device given vport identifier
 */
struct idpf_vport {
	u32 vport_id;
};

/**
 * struct idpf_vector_lifo - Stack to maintain vector indexes used for vector
 *			     distribution algorithm
 * @top: Points to stack top i.e. next available vector index
 * @base: Always points to start of the free pool
 * @size: Total size of the vector stack
 * @vec_idx: Array to store all the vector indexes
 *
 * Vector stack maintains all the relative vector indexes at the *adapter*
 * level. This stack is divided into 2 parts, first one is called as 'default
 * pool' and other one is called 'free pool'.  Vector distribution algorithm
 * gives priority to default vports in a way that at least IDPF_MIN_Q_VEC
 * vectors are allocated per default vport and the relative vector indexes for
 * those are maintained in default pool. Free pool contains all the unallocated
 * vector indexes which can be allocated on-demand basis. Mailbox vector index
 * is maintained in the default pool of the stack.
 */
struct idpf_vector_lifo {
	u16 top;
	u16 base;
	u16 size;
	u16 *vec_idx;
};

/**
 * struct idpf_adapter - Device data struct generated on probe
 * @pdev: PCI device struct given on probe
 * @virt_ver_maj: Virtchnl version major
 * @virt_ver_min: Virtchnl version minor
 * @msg_enable: Debug message level enabled
 * @mb_wait_count: Number of times mailbox was attempted initialization
 * @state: Init state machine
 * @flags: See enum idpf_flags
 * @reset_reg: See struct idpf_reset_reg
 * @hw: Device access data
 * @num_req_msix: Requested number of MSIX vectors
 * @num_avail_msix: Available number of MSIX vectors
 * @num_msix_entries: Number of entries in MSIX table
 * @msix_entries: MSIX table
 * @req_vec_chunks: Requested vector chunk data
 * @mb_vector: Mailbox vector data
 * @vector_stack: Stack to store the msix vector indexes
 * @irq_mb_handler: Handler for hard interrupt for mailbox
 * @serv_task: Periodically recurring maintenance task
 * @serv_wq: Workqueue for service task
 * @mbx_task: Task to handle mailbox interrupts
 * @mbx_wq: Workqueue for mailbox responses
 * @vc_event_task: Task to handle out of band virtchnl event notifications
 * @vc_event_wq: Workqueue for virtchnl events
 * @caps: Negotiated capabilities with device
 * @vchnl_wq: Wait queue for virtchnl messages
 * @vc_state: Virtchnl message state
 * @vc_msg: Virtchnl message buffer
 * @dev_ops: See idpf_dev_ops
 * @vport_ctrl_lock: Lock to protect the vport control flow
 * @vector_lock: Lock to protect vector distribution
 * @vc_buf_lock: Lock to protect virtchnl buffer
 */
struct idpf_adapter {
	struct pci_dev *pdev;
	u32 virt_ver_maj;
	u32 virt_ver_min;

	u32 msg_enable;
	u32 mb_wait_count;
	enum idpf_state state;
	DECLARE_BITMAP(flags, IDPF_FLAGS_NBITS);
	struct idpf_reset_reg reset_reg;
	struct idpf_hw hw;
	u16 num_req_msix;
	u16 num_avail_msix;
	u16 num_msix_entries;
	struct msix_entry *msix_entries;
	struct virtchnl2_alloc_vectors *req_vec_chunks;
	struct idpf_q_vector mb_vector;
	struct idpf_vector_lifo vector_stack;
	irqreturn_t (*irq_mb_handler)(int irq, void *data);

	struct delayed_work serv_task;
	struct workqueue_struct *serv_wq;
	struct delayed_work mbx_task;
	struct workqueue_struct *mbx_wq;
	struct delayed_work vc_event_task;
	struct workqueue_struct *vc_event_wq;
	struct virtchnl2_get_capabilities caps;

	wait_queue_head_t vchnl_wq;
	DECLARE_BITMAP(vc_state, IDPF_VC_NBITS);
	char vc_msg[IDPF_CTLQ_MAX_BUF_LEN];
	struct idpf_dev_ops dev_ops;

	struct mutex vport_ctrl_lock;
	struct mutex vector_lock;
	struct mutex vc_buf_lock;
};

/**
 * idpf_get_reserved_vecs - Get reserved vectors
 * @adapter: private data struct
 */
static inline u16 idpf_get_reserved_vecs(struct idpf_adapter *adapter)
{
	return le16_to_cpu(adapter->caps.num_allocated_vectors);
}

/**
 * idpf_get_default_vports - Get default number of vports
 * @adapter: private data struct
 */
static inline u16 idpf_get_default_vports(struct idpf_adapter *adapter)
{
	return le16_to_cpu(adapter->caps.default_num_vports);
}

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

/**
 * idpf_is_reset_in_prog - check if reset is in progress
 * @adapter: driver specific private structure
 *
 * Returns true if hard reset is in progress, false otherwise
 */
static inline bool idpf_is_reset_in_prog(struct idpf_adapter *adapter)
{
	return (test_bit(IDPF_HR_RESET_IN_PROG, adapter->flags) ||
		test_bit(IDPF_HR_FUNC_RESET, adapter->flags) ||
		test_bit(IDPF_HR_DRV_LOAD, adapter->flags));
}

void idpf_service_task(struct work_struct *work);
void idpf_mbx_task(struct work_struct *work);
void idpf_vc_event_task(struct work_struct *work);
void idpf_dev_ops_init(struct idpf_adapter *adapter);
void idpf_vf_dev_ops_init(struct idpf_adapter *adapter);
int idpf_init_dflt_mbx(struct idpf_adapter *adapter);
void idpf_deinit_dflt_mbx(struct idpf_adapter *adapter);
int idpf_vc_core_init(struct idpf_adapter *adapter);
void idpf_vc_core_deinit(struct idpf_adapter *adapter);
int idpf_intr_req(struct idpf_adapter *adapter);
void idpf_intr_rel(struct idpf_adapter *adapter);
int idpf_send_dealloc_vectors_msg(struct idpf_adapter *adapter);
int idpf_send_alloc_vectors_msg(struct idpf_adapter *adapter, u16 num_vectors);
int idpf_get_vec_ids(struct idpf_adapter *adapter,
		     u16 *vecids, int num_vecids,
		     struct virtchnl2_vector_chunks *chunks);
int idpf_recv_mb_msg(struct idpf_adapter *adapter, u32 op,
		     void *msg, int msg_size);
int idpf_send_mb_msg(struct idpf_adapter *adapter, u32 op,
		     u16 msg_size, u8 *msg);

#endif /* !_IDPF_H_ */
