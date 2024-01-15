/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2023 Intel Corporation */

#ifndef _IDPF_CONTROLQ_API_H_
#define _IDPF_CONTROLQ_API_H_

#include "idpf_mem.h"

struct idpf_hw;

/* Used for queue init, response and events */
enum idpf_ctlq_type {
	IDPF_CTLQ_TYPE_MAILBOX_TX	= 0,
	IDPF_CTLQ_TYPE_MAILBOX_RX	= 1,
	IDPF_CTLQ_TYPE_CONFIG_TX	= 2,
	IDPF_CTLQ_TYPE_CONFIG_RX	= 3,
	IDPF_CTLQ_TYPE_EVENT_RX		= 4,
	IDPF_CTLQ_TYPE_RDMA_TX		= 5,
	IDPF_CTLQ_TYPE_RDMA_RX		= 6,
	IDPF_CTLQ_TYPE_RDMA_COMPL	= 7
};

/* Generic Control Queue Structures */
struct idpf_ctlq_reg {
	/* used for queue tracking */
	u32 head;
	u32 tail;
	/* Below applies only to default mb (if present) */
	u32 len;
	u32 bah;
	u32 bal;
	u32 len_mask;
	u32 len_ena_mask;
	u32 head_mask;
};

/* Generic queue msg structure */
struct idpf_ctlq_msg {
	u8 vmvf_type; /* represents the source of the message on recv */
#define IDPF_VMVF_TYPE_VF 0
#define IDPF_VMVF_TYPE_VM 1
#define IDPF_VMVF_TYPE_PF 2
	u8 host_id;
	/* 3b field used only when sending a message to CP - to be used in
	 * combination with target func_id to route the message
	 */
#define IDPF_HOST_ID_MASK 0x7

	u16 opcode;
	u16 data_len;	/* data_len = 0 when no payload is attached */
	union {
		u16 func_id;	/* when sending a message */
		u16 status;	/* when receiving a message */
	};
	union {
		struct {
			u32 chnl_opcode;
			u32 chnl_retval;
		} mbx;
	} cookie;
	union {
#define IDPF_DIRECT_CTX_SIZE	16
#define IDPF_INDIRECT_CTX_SIZE	8
		/* 16 bytes of context can be provided or 8 bytes of context
		 * plus the address of a DMA buffer
		 */
		u8 direct[IDPF_DIRECT_CTX_SIZE];
		struct {
			u8 context[IDPF_INDIRECT_CTX_SIZE];
			struct idpf_dma_mem *payload;
		} indirect;
	} ctx;
};

/* Generic queue info structures */
/* MB, CONFIG and EVENT q do not have extended info */
struct idpf_ctlq_create_info {
	enum idpf_ctlq_type type;
	int id; /* absolute queue offset passed as input
		 * -1 for default mailbox if present
		 */
	u16 len; /* Queue length passed as input */
	u16 buf_size; /* buffer size passed as input */
	u64 base_address; /* output, HPA of the Queue start  */
	struct idpf_ctlq_reg reg; /* registers accessed by ctlqs */

	int ext_info_size;
	void *ext_info; /* Specific to q type */
};

/* Control Queue information */
struct idpf_ctlq_info {
	struct list_head cq_list;

	enum idpf_ctlq_type cq_type;
	int q_id;
	struct mutex cq_lock;		/* control queue lock */
	/* used for interrupt processing */
	u16 next_to_use;
	u16 next_to_clean;
	u16 next_to_post;		/* starting descriptor to post buffers
					 * to after recev
					 */

	struct idpf_dma_mem desc_ring;	/* descriptor ring memory
					 * idpf_dma_mem is defined in OSdep.h
					 */
	union {
		struct idpf_dma_mem **rx_buff;
		struct idpf_ctlq_msg **tx_msg;
	} bi;

	u16 buf_size;			/* queue buffer size */
	u16 ring_size;			/* Number of descriptors */
	struct idpf_ctlq_reg reg;	/* registers accessed by ctlqs */
};

/**
 * enum idpf_mbx_opc - PF/VF mailbox commands
 * @idpf_mbq_opc_send_msg_to_cp: used by PF or VF to send a message to its CP
 */
enum idpf_mbx_opc {
	idpf_mbq_opc_send_msg_to_cp		= 0x0801,
};

/* API supported for control queue management */
/* Will init all required q including default mb.  "q_info" is an array of
 * create_info structs equal to the number of control queues to be created.
 */
int idpf_ctlq_init(struct idpf_hw *hw, u8 num_q,
		   struct idpf_ctlq_create_info *q_info);

/* Allocate and initialize a single control queue, which will be added to the
 * control queue list; returns a handle to the created control queue
 */
int idpf_ctlq_add(struct idpf_hw *hw,
		  struct idpf_ctlq_create_info *qinfo,
		  struct idpf_ctlq_info **cq);

/* Deinitialize and deallocate a single control queue */
void idpf_ctlq_remove(struct idpf_hw *hw,
		      struct idpf_ctlq_info *cq);

/* Sends messages to HW and will also free the buffer*/
int idpf_ctlq_send(struct idpf_hw *hw,
		   struct idpf_ctlq_info *cq,
		   u16 num_q_msg,
		   struct idpf_ctlq_msg q_msg[]);

/* Receives messages and called by interrupt handler/polling
 * initiated by app/process. Also caller is supposed to free the buffers
 */
int idpf_ctlq_recv(struct idpf_ctlq_info *cq, u16 *num_q_msg,
		   struct idpf_ctlq_msg *q_msg);

/* Reclaims send descriptors on HW write back */
int idpf_ctlq_clean_sq(struct idpf_ctlq_info *cq, u16 *clean_count,
		       struct idpf_ctlq_msg *msg_status[]);

/* Indicate RX buffers are done being processed */
int idpf_ctlq_post_rx_buffs(struct idpf_hw *hw,
			    struct idpf_ctlq_info *cq,
			    u16 *buff_count,
			    struct idpf_dma_mem **buffs);

/* Will destroy all q including the default mb */
void idpf_ctlq_deinit(struct idpf_hw *hw);

#endif /* _IDPF_CONTROLQ_API_H_ */
