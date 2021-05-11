/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright (c) 2021, Microsoft Corporation. */

#ifndef _HW_CHANNEL_H
#define _HW_CHANNEL_H

#define DEFAULT_LOG2_THROTTLING_FOR_ERROR_EQ  4

#define HW_CHANNEL_MAX_REQUEST_SIZE  0x1000
#define HW_CHANNEL_MAX_RESPONSE_SIZE 0x1000

#define HW_CHANNEL_VF_BOOTSTRAP_QUEUE_DEPTH 1

#define HWC_INIT_DATA_CQID		1
#define HWC_INIT_DATA_RQID		2
#define HWC_INIT_DATA_SQID		3
#define HWC_INIT_DATA_QUEUE_DEPTH	4
#define HWC_INIT_DATA_MAX_REQUEST	5
#define HWC_INIT_DATA_MAX_RESPONSE	6
#define HWC_INIT_DATA_MAX_NUM_CQS	7
#define HWC_INIT_DATA_PDID		8
#define HWC_INIT_DATA_GPA_MKEY		9

/* Structures labeled with "HW DATA" are exchanged with the hardware. All of
 * them are naturally aligned and hence don't need __packed.
 */

union hwc_init_eq_id_db {
	u32 as_uint32;

	struct {
		u32 eq_id	: 16;
		u32 doorbell	: 16;
	};
}; /* HW DATA */

union hwc_init_type_data {
	u32 as_uint32;

	struct {
		u32 value	: 24;
		u32 type	:  8;
	};
}; /* HW DATA */

struct hwc_rx_oob {
	u32 type	: 6;
	u32 eom		: 1;
	u32 som		: 1;
	u32 vendor_err	: 8;
	u32 reserved1	: 16;

	u32 src_virt_wq	: 24;
	u32 src_vfid	: 8;

	u32 reserved2;

	union {
		u32 wqe_addr_low;
		u32 wqe_offset;
	};

	u32 wqe_addr_high;

	u32 client_data_unit	: 14;
	u32 reserved3		: 18;

	u32 tx_oob_data_size;

	u32 chunk_offset	: 21;
	u32 reserved4		: 11;
}; /* HW DATA */

struct hwc_tx_oob {
	u32 reserved1;

	u32 reserved2;

	u32 vrq_id	: 24;
	u32 dest_vfid	: 8;

	u32 vrcq_id	: 24;
	u32 reserved3	: 8;

	u32 vscq_id	: 24;
	u32 loopback	: 1;
	u32 lso_override: 1;
	u32 dest_pf	: 1;
	u32 reserved4	: 5;

	u32 vsq_id	: 24;
	u32 reserved5	: 8;
}; /* HW DATA */

struct hwc_work_request {
	void *buf_va;
	void *buf_sge_addr;
	u32 buf_len;
	u32 msg_size;

	struct gdma_wqe_request wqe_req;
	struct hwc_tx_oob tx_oob;

	struct gdma_sge sge;
};

/* hwc_dma_buf represents the array of in-flight WQEs.
 * mem_info as know as the GDMA mapped memory is partitioned and used by
 * in-flight WQEs.
 * The number of WQEs is determined by the number of in-flight messages.
 */
struct hwc_dma_buf {
	struct gdma_mem_info mem_info;

	u32 gpa_mkey;

	u32 num_reqs;
	struct hwc_work_request reqs[];
};

typedef void hwc_rx_event_handler_t(void *ctx, u32 gdma_rxq_id,
				    const struct hwc_rx_oob *rx_oob);

typedef void hwc_tx_event_handler_t(void *ctx, u32 gdma_txq_id,
				    const struct hwc_rx_oob *rx_oob);

struct hwc_cq {
	struct hw_channel_context *hwc;

	struct gdma_queue *gdma_cq;
	struct gdma_queue *gdma_eq;
	struct gdma_comp *comp_buf;
	u16 queue_depth;

	hwc_rx_event_handler_t *rx_event_handler;
	void *rx_event_ctx;

	hwc_tx_event_handler_t *tx_event_handler;
	void *tx_event_ctx;
};

struct hwc_wq {
	struct hw_channel_context *hwc;

	struct gdma_queue *gdma_wq;
	struct hwc_dma_buf *msg_buf;
	u16 queue_depth;

	struct hwc_cq *hwc_cq;
};

struct hwc_caller_ctx {
	struct completion comp_event;
	void *output_buf;
	u32 output_buflen;

	u32 error; /* Linux error code */
	u32 status_code;
};

struct hw_channel_context {
	struct gdma_dev *gdma_dev;
	struct device *dev;

	u16 num_inflight_msg;
	u32 max_req_msg_size;

	u16 hwc_init_q_depth_max;
	u32 hwc_init_max_req_msg_size;
	u32 hwc_init_max_resp_msg_size;

	struct completion hwc_init_eqe_comp;

	struct hwc_wq *rxq;
	struct hwc_wq *txq;
	struct hwc_cq *cq;

	struct semaphore sema;
	struct gdma_resource inflight_msg_res;

	struct hwc_caller_ctx *caller_ctx;
};

int mana_hwc_create_channel(struct gdma_context *gc);
void mana_hwc_destroy_channel(struct gdma_context *gc);

int mana_hwc_send_request(struct hw_channel_context *hwc, u32 req_len,
			  const void *req, u32 resp_len, void *resp);

#endif /* _HW_CHANNEL_H */
