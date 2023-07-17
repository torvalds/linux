// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2022, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <asm/unistd.h>
#include <asm/ioctl.h>
#include <linux/types.h>
#include <linux/dma-buf.h>
#include <linux/interrupt.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/uaccess.h>
#include <uapi/linux/hgsl.h>
#include <linux/delay.h>
#include <trace/events/gpu_mem.h>
#include <linux/printk.h>

#include "hgsl.h"
#include "hgsl_tcsr.h"
#include "hgsl_memory.h"
#include "hgsl_hyp.h"

#define HGSL_DEVICE_NAME "hgsl"
#define HGSL_DEV_NUM 1

#define IORESOURCE_HWINF "hgsl_reg_hwinf"
#define IORESOURCE_GMUCX "hgsl_reg_gmucx"

/* Set-up profiling packets as needed by scope */
#define CMDBATCH_PROFILING 0x00000010

/* Ping the user of HFI when this command is done */
#define CMDBATCH_NOTIFY    0x00000020

#define CMDBATCH_EOF       0x00000100
#define ECP_MAX_NUM_IB1    (2000)

/* Max retry count of waiting for free space of doorbell queue. */
#define HGSL_QFREE_MAX_RETRY_COUNT     (500)
#define GLB_DB_SRC_ISSUEIB_IRQ_ID_0    TCSR_SRC_IRQ_ID_0
#define GLB_DB_DEST_TS_RETIRE_IRQ_ID   TCSR_DEST_IRQ_ID_0
#define GLB_DB_DEST_TS_RETIRE_IRQ_MASK TCSR_DEST_IRQ_MASK_0

#define HGSL_HYP_GENERAL_MAX_SIZE 4096

#define DB_STATE_Q_MASK	0xffff
#define DB_STATE_Q_UNINIT	1
#define DB_STATE_Q_INIT_DONE	2
#define DB_STATE_Q_FAULT	3

/* Doorbell Signal types */
#define DB_SIGNAL_INVALID	0
#define DB_SIGNAL_GLOBAL_0	1
#define DB_SIGNAL_GLOBAL_1	2
#define DB_SIGNAL_LOCAL	3
#define DB_SIGNAL_MAX	DB_SIGNAL_LOCAL
#define DB_SIGNAL_GLOBAL_2	3
#define DB_SIGNAL_GLOBAL_3	4
#define DBCQ_SIGNAL_MAX	DB_SIGNAL_GLOBAL_3
#define HGSL_CLEANUP_WAIT_SLICE_IN_MS  50

#define QHDR_STATUS_INACTIVE 0x00
#define QHDR_STATUS_ACTIVE 0x01

#define HGSL_CONTEXT_NUM                     (256)
#define HGSL_SEND_MSG_MAX_RETRY_COUNT        (150)

#define HGSL_FT_POLICY_FLAG_KILL             BIT(2)

#define ALIGN_ADDRESS_4DWORD(addr)         (((addr)+15) & ((long long) ~15))
#define ALIGN_DWORD_ADDRESS_4DWORD(dwaddr) (ALIGN_ADDRESS_4DWORD((dwaddr) * \
				sizeof(uint32_t)) / sizeof(uint32_t))

enum HGSL_DBQ_METADATA_COMMAND_INFO {
	HGSL_DBQ_METADATA_CONTEXT_INFO,
	HGSL_DBQ_METADATA_QUEUE_INDEX,
	HGSL_DBQ_METADATA_COOPERATIVE_RESET,
};

#define HGSL_DBQ_CONTEXT_ANY                 (0x0)
#define HGSL_DBQ_OFFSET_ZERO                 (0x0)

#define HGSL_DBQ_WRITE_INDEX_OFFSET_IN_DWORD (0x0)
#define HGSL_DBQ_READ_INDEX_OFFSET_IN_DWORD  (0x1)

enum HGSL_DBQ_METADATA_COOPERATIVE_RESET_INFO {
	HGSL_DBQ_HOST_TO_GVM_HARDRESET_REQ,
	HGSL_DBQ_GVM_TO_HOST_HARDRESET_DISPATCH_IN_BUSY,
};

enum HGSL_DBQ_METADATA_CONTEXT_OFFSET_INFO {
	HGSL_DBQ_CONTEXT_CONTEXT_ID_OFFSET_IN_DWORD,
	HGSL_DBQ_CONTEXT_TIMESTAMP_OFFSET_IN_DWORD,
	HGSL_DBQ_CONTEXT_DESTROY_OFFSET_IN_DWORD,
	HGSL_DBQ_METADATA_CTXT_TOTAL_ENTITY_NUM,
};

/* DBQ structure
 *   IBs storage | reserved | w.idx/r.idx | ctxt.info | hard reset | batch ibs |
 * 0             1K         1.5K          2K          5.5K         6K          |
 * |             |          |             |           |            |           |
 */

#define HGSL_DBQ_HFI_Q_INDEX_BASE_OFFSET_IN_DWORD            (1536 >> 2)
#define HGSL_DBQ_CONTEXT_INFO_BASE_OFFSET_IN_DWORD           (2048 >> 2)
#define HGSL_DBQ_COOPERATIVE_RESET_INFO_BASE_OFFSET_IN_DWORD (5632 >> 2)
#define HGSL_DBQ_IBDESC_BASE_OFFSET_IN_DWORD                 (6144 >> 2)

#define HGSL_CTXT_QUEUE_BODY_DWSIZE          (256)
#define HGSL_CTXT_QUEUE_BODY_SIZE            (HGSL_CTXT_QUEUE_BODY_DWSIZE * sizeof(uint32_t))
#define HGSL_CTXT_QUEUE_BODY_OFFSET          ALIGN_ADDRESS_4DWORD(sizeof(struct ctx_queue_header))
#define HGSL_CTXT_QUEUE_TOTAL_SIZE           PAGE_ALIGN(HGSL_CTXT_QUEUE_BODY_SIZE +\
							HGSL_CTXT_QUEUE_BODY_OFFSET)

struct ctx_queue_header {
	uint32_t version;             // Version of the context queue header
	uint32_t startAddr;           // GMU VA of start of queue
	uint32_t dwSize;              // Queue size in dwords
	uint32_t outFenceTs; // Timestamp of the last output hardware fence sent to TxQueue
	uint32_t syncObjTs;  // Timestamp of last SYNC object that has been signaled
	uint32_t readIdx;    // Read index of the queue
	uint32_t writeIdx;   // Write index of the queue
	uint32_t hwFenceArrayAddr;    // GMU VA of the buffer to store output hardware fences
	uint32_t hwFenceArraySize;    // Size(bytes) of the buffer to store output hardware fences
	uint32_t dbqSignal;
	uint32_t unused0;
	uint32_t unused1;
};


static inline bool _timestamp_retired(struct hgsl_context *ctxt,
				unsigned int timestamp);

static inline void set_context_retired_ts(struct hgsl_context *ctxt,
				unsigned int ts);
static void _signal_contexts(struct qcom_hgsl *hgsl);

static int db_get_busy_state(void *dbq_base);
static void db_set_busy_state(void *dbq_base, int in_busy);

static struct hgsl_context *hgsl_get_context(struct qcom_hgsl *hgsl,
				uint32_t context_id);
static void hgsl_put_context(struct hgsl_context *ctxt);

static uint32_t hgsl_dbq_get_state_info(uint32_t *va_base, uint32_t command,
				uint32_t ctxt_id, uint32_t offset)
{
	uint32_t *dest = NULL;

	switch (command) {
	case HGSL_DBQ_METADATA_QUEUE_INDEX:
		dest = (uint32_t *)(va_base +
				HGSL_DBQ_HFI_Q_INDEX_BASE_OFFSET_IN_DWORD +
				offset);
		break;
	case HGSL_DBQ_METADATA_CONTEXT_INFO:
		dest = (uint32_t *)(va_base +
				HGSL_DBQ_CONTEXT_INFO_BASE_OFFSET_IN_DWORD +
				(HGSL_DBQ_METADATA_CTXT_TOTAL_ENTITY_NUM *
				ctxt_id) + offset);
		break;
	case HGSL_DBQ_METADATA_COOPERATIVE_RESET:
		dest = (uint32_t *)(va_base +
		HGSL_DBQ_COOPERATIVE_RESET_INFO_BASE_OFFSET_IN_DWORD +
				offset);
		break;
	default:
		break;
	}

	return ((dest != NULL) ? (*dest) : (0));
}

static void hgsl_dbq_set_state_info(uint32_t *va_base, uint32_t command,
				uint32_t ctxt_id, uint32_t offset,
				uint32_t value)
{
	uint32_t *dest = NULL;

	switch (command) {
	case HGSL_DBQ_METADATA_QUEUE_INDEX:
		dest = (uint32_t *)(va_base +
				HGSL_DBQ_HFI_Q_INDEX_BASE_OFFSET_IN_DWORD +
				(HGSL_DBQ_METADATA_CTXT_TOTAL_ENTITY_NUM *
				ctxt_id) + offset);
		*dest = value;
		break;
	case HGSL_DBQ_METADATA_CONTEXT_INFO:
		dest = (uint32_t *)(va_base +
				HGSL_DBQ_CONTEXT_INFO_BASE_OFFSET_IN_DWORD +
				(HGSL_DBQ_METADATA_CTXT_TOTAL_ENTITY_NUM *
				ctxt_id) + offset);
		*dest = value;
		break;
	case HGSL_DBQ_METADATA_COOPERATIVE_RESET:
		dest = (uint32_t *)(va_base +
		HGSL_DBQ_COOPERATIVE_RESET_INFO_BASE_OFFSET_IN_DWORD +
				offset);
		*dest = value;
		break;
	default:
		break;
	}
}

#define HFI_MSG_TYPE_CMD  0
#define HFI_MSG_TYPE_RET  1

/* HFI command define. */
#define HTOF_MSG_ISSUE_CMD 130

#define MSG_ISSUE_INF_SZ()	(sizeof(struct hgsl_db_cmds) >> 2)
#define MSG_ISSUE_IBS_SZ(numIB) \
		((numIB) * (sizeof(struct hgsl_fw_ib_desc) >> 2))

#define MSG_SEQ_NO_MASK     0xFFF00000
#define MSG_SEQ_NO_SHIFT    20
#define MSG_SEQ_NO_GET(x)   (((x) & MSG_SEQ_NO_MASK) >> MSG_SEQ_NO_SHIFT)
#define MSG_TYPE_MASK       0x000F0000
#define MSG_TYPE_SHIFT      16
#define MSG_TYPE_GET(x)     (((x) & MSG_TYPE_MASK) >> MSG_TYPE_SHIFT)
#define MSG_SZ_MASK         0x0000FF00
#define MSG_SZ_SHIFT        8
#define MSG_SZ_GET(x)       (((x) & MSG_SZ_MASK) >> MSG_SZ_SHIFT)
#define MSG_ID_MASK         0x000000FF
#define MSG_ID_GET(x)       ((x) & MSG_ID_MASK)

#define MAKE_HFI_MSG_HEADER(msgID, msgType, msgSize, msgSeqnum) \
				((msgID) | ((msgSize) << MSG_SZ_SHIFT) | \
				((msgType) << MSG_TYPE_SHIFT) | \
				((msgSeqnum) << MSG_SEQ_NO_SHIFT))

#define HFI_ISSUE_IB_HEADER(numIB, sz, msgSeqnum) \
					MAKE_HFI_MSG_HEADER( \
					HTOF_MSG_ISSUE_CMD, \
					HFI_MSG_TYPE_CMD, \
					sz,\
					msgSeqnum)

/*
 * GMU HFI memory allocation options:
 * RGS_GMU_HFI_BUFFER_DTCM: Allocated from GMU CM3 DTCM.
 * RGS_GMU_HFI_BUFFER_NON_CACHEMEM: POR mode. Allocated from non cached memory.
 */
enum db_buffer_mode_t {
	RGS_GMU_HFI_BUFFER_DTCM = 0,
	RGS_GMU_HFI_BUFFER_NON_CACHEMEM = 1,
	RGS_GMU_HFI_BUFFER_DEFAULT = 1
};

struct db_msg_request {
	int msg_has_response;
	int msg_has_ret_packet;
	int ignore_ret_packet;
	void *ptr_data;
	unsigned int msg_dwords;
} __packed;

struct db_msg_response {
	void *ptr_data;
	unsigned int size_dword;
} __packed;

/*
 * IB start address
 * IB size
 */
struct hgsl_fw_ib_desc {
	uint64_t addr;
	uint32_t sz;
} __packed;

struct hfi_msg_header_fields {
	uint32_t msg_id             : 8;   ///< 0~127 power, 128~255 eCP
	uint32_t msg_size_dword     : 8;   ///< unit in dword, maximum 255
	uint32_t msg_type           : 4;   ///< refer to adreno_hfi_msg_type_t
	uint32_t msg_packet_seq_no  : 12;
};

union hfi_msg_header {
	uint32_t u32_all;
	struct hfi_msg_header_fields fields;
};

/*
 * Context ID
 * cmd_flags
 * Per-context user space gsl timestamp. It has to be
 * greater than last retired timestamp.
 * Number of IB descriptors
 * An array of IB descriptors
 */
struct hgsl_db_cmds {
	union hfi_msg_header header;
	uint32_t ctx_id;
	uint32_t cmd_flags;
	uint32_t timestamp;
	uint64_t user_profile_gpuaddr;
	uint32_t num_ibs;
	uint32_t ib_desc_gmuaddr;
	struct hgsl_fw_ib_desc ib_descs[];
} __packed;

struct hgsl_db_msg_ret {
	uint32_t header;
	uint32_t ack;
	uint32_t err;
} __packed;

struct db_msg_id {
	uint32_t seq_no;
	uint32_t msg_id;
} __packed;

struct db_wait_retpacket {
	size_t event_signal;
	int in_use;
	struct db_msg_id db_msg_id;
	struct db_msg_response response;
} __packed;

struct db_ignore_retpacket {
	int in_use;
	struct db_msg_id db_msg_id;
} __packed;


struct hgsl_active_wait {
	struct list_head head;
	struct hgsl_context *ctxt;
	unsigned int timestamp;
};

#ifdef CONFIG_TRACE_GPU_MEM
static inline void hgsl_trace_gpu_mem_total(struct hgsl_priv *priv, int64_t delta)
{
	struct qcom_hgsl *hgsl = priv->dev;
	uint64_t size = atomic64_add_return(delta, &priv->total_mem_size);
	uint64_t global_size = atomic64_add_return(delta, &hgsl->total_mem_size);

	trace_gpu_mem_total(0, priv->pid, size);
	trace_gpu_mem_total(0, 0, global_size);
}
#else
static inline void hgsl_trace_gpu_mem_total(struct hgsl_priv *priv, int64_t delta)
{
}
#endif

static int hgsl_reg_map(struct platform_device *pdev,
			char *res_name, struct reg *reg);

static void hgsl_reg_read(struct reg *reg, unsigned int off,
					unsigned int *value)
{
	if (reg == NULL)
		return;

	if (WARN(off > reg->size,
		"Invalid reg read:0x%x, reg size:0x%x\n",
						off, reg->size))
		return;
	*value = __raw_readl(reg->vaddr + off);

	/* ensure this read finishes before the next one.*/
	dma_rmb();
}

static void hgsl_reg_write(struct reg *reg, unsigned int off,
					unsigned int value)
{
	if (reg == NULL)
		return;

	if (WARN(off > reg->size,
		"Invalid reg write:0x%x, reg size:0x%x\n",
						off, reg->size))
		return;

	/*
	 * ensure previous writes post before this one,
	 * i.e. act like normal writel()
	 */
	dma_wmb();
	__raw_writel(value, (reg->vaddr + off));
}

static inline bool is_global_db(int tcsr_idx)
{
	return (tcsr_idx >= 0);
}

static void gmu_ring_local_db(struct qcom_hgsl  *hgsl, unsigned int value)
{
	hgsl_reg_write(&hgsl->reg_dbidx, 0, value);
}

static void tcsr_ring_global_db(struct qcom_hgsl *hgsl, uint32_t tcsr_idx,
				uint32_t dbq_idx)
{
	if (tcsr_idx < HGSL_TCSR_NUM)
		hgsl_tcsr_irq_trigger(hgsl->tcsr[tcsr_idx][HGSL_TCSR_ROLE_SENDER],
						GLB_DB_SRC_ISSUEIB_IRQ_ID_0 + dbq_idx);
}

static uint32_t db_queue_freedwords(struct doorbell_queue *dbq)
{
	uint32_t queue_size;
	uint32_t queue_used;
	uint32_t wptr;
	uint32_t rptr;

	if (dbq == NULL)
		return 0;

	wptr = hgsl_dbq_get_state_info((uint32_t *)dbq->vbase,
			HGSL_DBQ_METADATA_QUEUE_INDEX, HGSL_DBQ_CONTEXT_ANY,
				HGSL_DBQ_WRITE_INDEX_OFFSET_IN_DWORD);

	rptr = hgsl_dbq_get_state_info((uint32_t *)dbq->vbase,
			HGSL_DBQ_METADATA_QUEUE_INDEX, HGSL_DBQ_CONTEXT_ANY,
			HGSL_DBQ_READ_INDEX_OFFSET_IN_DWORD);

	queue_size = dbq->data.dwords;
	queue_used = (wptr + queue_size - rptr) % queue_size;
	return (queue_size - queue_used - 1);
}

static int db_queue_wait_freewords(struct doorbell_queue *dbq, uint32_t size)
{
	unsigned int retry_count = 0;
	unsigned int hard_reset_req = false;

	if (size == 0)
		return 0;

	if (dbq == NULL)
		return -EINVAL;

	do {
		hard_reset_req = hgsl_dbq_get_state_info((uint32_t *)dbq->vbase,
			HGSL_DBQ_METADATA_COOPERATIVE_RESET,
			HGSL_DBQ_CONTEXT_ANY,
			HGSL_DBQ_HOST_TO_GVM_HARDRESET_REQ);

		/* ensure read is done before comparison */
		dma_rmb();

		if (hard_reset_req == true) {
			if (db_get_busy_state(dbq->vbase) == true)
				db_set_busy_state(dbq->vbase, false);
		} else {
			if (db_queue_freedwords(dbq) >= size) {
				db_set_busy_state(dbq->vbase, true);
				return 0;
			}
		}

		if (msleep_interruptible(1))
			/* Let user handle this */
			return -EINTR;
	} while (retry_count++ < HGSL_QFREE_MAX_RETRY_COUNT);

	return -ETIMEDOUT;
}

static uint32_t db_context_queue_freedwords(struct doorbell_context_queue *dbcq)
{
	struct ctx_queue_header *queue_header = (struct ctx_queue_header *)dbcq->queue_header;
	uint32_t queue_size = queue_header->dwSize;
	uint32_t wptr = queue_header->writeIdx;
	uint32_t rptr = queue_header->readIdx;
	uint32_t queue_used = (wptr + queue_size - rptr) % queue_size;

	return (queue_size - queue_used - 1);
}

static int dbcq_queue_wait_freewords(struct doorbell_context_queue *dbcq, uint32_t size)
{
	unsigned int retry_count = 0;

	do {
		if (db_context_queue_freedwords(dbcq) >= size)
			return 0;

		if (msleep_interruptible(1))
			/* Let user handle this */
			return -EINTR;
	} while (retry_count++ < HGSL_QFREE_MAX_RETRY_COUNT);

	return -ETIMEDOUT;
}

static int db_get_busy_state(void *dbq_base)
{
	unsigned int busy_state = false;

	busy_state = hgsl_dbq_get_state_info((uint32_t *)dbq_base,
		HGSL_DBQ_METADATA_COOPERATIVE_RESET,
		HGSL_DBQ_CONTEXT_ANY,
		HGSL_DBQ_GVM_TO_HOST_HARDRESET_DISPATCH_IN_BUSY);

	/* ensure read is done before comparison */
	dma_rmb();

	return busy_state;
}

static void db_set_busy_state(void *dbq_base, int in_busy)
{
	hgsl_dbq_set_state_info((uint32_t *)dbq_base,
		HGSL_DBQ_METADATA_COOPERATIVE_RESET,
		HGSL_DBQ_CONTEXT_ANY,
		HGSL_DBQ_GVM_TO_HOST_HARDRESET_DISPATCH_IN_BUSY,
		in_busy);

	/* confirm write to memory done */
	dma_wmb();
}

static int dbcq_send_msg(struct hgsl_priv  *priv,
			struct db_msg_id *db_msg_id,
			struct db_msg_request *msg_req,
			struct db_msg_response *msg_resp,
			struct hgsl_context *ctxt)
{
	uint32_t msg_size_align;
	int ret;
	uint8_t *src, *dst;
	uint32_t move_dwords, resid_move_dwords;
	uint32_t queue_size_dword;
	struct qcom_hgsl *hgsl = priv->dev;
	struct doorbell_context_queue *dbcq = ctxt->dbcq;
	uint32_t wptr;
	struct ctx_queue_header *queue_header = (struct ctx_queue_header *)dbcq->queue_header;

	queue_size_dword = queue_header->dwSize;
	msg_size_align = ALIGN(msg_req->msg_dwords, 4);

	ret = dbcq_queue_wait_freewords(dbcq, msg_size_align);
	if (ret)
		goto quit;

	wptr = queue_header->writeIdx;

	move_dwords = msg_req->msg_dwords;
	if ((msg_req->msg_dwords + wptr) >= queue_size_dword) {
		move_dwords = queue_size_dword - wptr;
		resid_move_dwords = msg_req->msg_dwords - move_dwords;
		dst = (uint8_t *)dbcq->queue_body;
		src = msg_req->ptr_data + (move_dwords << 2);
		memcpy(dst, src, (resid_move_dwords << 2));
	}

	dst = (uint8_t *)dbcq->queue_body + (wptr << 2);
	src = msg_req->ptr_data;
	memcpy(dst, src, (move_dwords << 2));

	/* ensure data is committed before update wptr */
	dma_wmb();

	wptr = (wptr + msg_size_align) % queue_size_dword;
	queue_header->writeIdx = wptr;

	/* confirm write to memory done before ring door bell. */
	wmb();

	if (is_global_db(ctxt->tcsr_idx))
		/* trigger TCSR interrupt for global doorbell */
		tcsr_ring_global_db(hgsl, ctxt->tcsr_idx, dbcq->irq_idx);
	else
		/* trigger GMU interrupt */
		gmu_ring_local_db(hgsl, dbcq->irq_idx);

quit:

	/* let user try again incase we miss to submit */
	if (-ETIMEDOUT == ret) {
		LOGE("Timed out to send db msg, try again\n");
		ret = -EAGAIN;
	}
	return ret;
}

static int db_send_msg(struct hgsl_priv  *priv,
			struct db_msg_id *db_msg_id,
			struct db_msg_request *msg_req,
			struct db_msg_response *msg_resp,
			struct hgsl_context *ctxt)
{
	uint32_t msg_size_align;
	int ret;
	uint8_t *src, *dst;
	uint32_t move_dwords, resid_move_dwords;
	uint32_t queue_size_dword;
	struct qcom_hgsl *hgsl;
	struct doorbell_queue *dbq;
	uint32_t wptr;
	struct hgsl_db_cmds *cmds;
	int retry_count = 0;
	uint32_t hard_reset_req = false;

	hgsl = priv->dev;
	dbq = ctxt->dbq;

	mutex_lock(&dbq->lock);

	cmds = (struct hgsl_db_cmds *)msg_req->ptr_data;
	do {
		hard_reset_req = hgsl_dbq_get_state_info((uint32_t *)dbq->vbase,
			HGSL_DBQ_METADATA_COOPERATIVE_RESET,
			HGSL_DBQ_CONTEXT_ANY,
			HGSL_DBQ_HOST_TO_GVM_HARDRESET_REQ);

		/* ensure read is done before comparison */
		dma_rmb();

		if (hard_reset_req) {
			if (msleep_interruptible(1)) {
				/* Let user handle this */
				ret = -EINTR;
				goto quit;
			}
			if (retry_count++ > HGSL_SEND_MSG_MAX_RETRY_COUNT) {
				ret = -ETIMEDOUT;
				goto quit;
			}
		}
	} while (hard_reset_req);

	db_set_busy_state(dbq->vbase, true);

	queue_size_dword = dbq->data.dwords;
	msg_size_align = ALIGN(msg_req->msg_dwords, 4);

	ret = db_queue_wait_freewords(dbq, msg_size_align);
	if (ret < 0) {
		dev_err(hgsl->dev,
			"Timed out waiting for queue to free up\n");
		goto quit;
	}

	wptr = hgsl_dbq_get_state_info((uint32_t *)dbq->vbase,
			HGSL_DBQ_METADATA_QUEUE_INDEX, HGSL_DBQ_CONTEXT_ANY,
			HGSL_DBQ_WRITE_INDEX_OFFSET_IN_DWORD);

	move_dwords = msg_req->msg_dwords;
	if ((msg_req->msg_dwords + wptr) >= queue_size_dword) {
		move_dwords = queue_size_dword - wptr;
		resid_move_dwords = msg_req->msg_dwords - move_dwords;
		dst = (uint8_t *)dbq->data.vaddr;
		src = msg_req->ptr_data + (move_dwords << 2);
		memcpy(dst, src, (resid_move_dwords << 2));
	}

	dst = dbq->data.vaddr + (wptr << 2);
	src = msg_req->ptr_data;
	memcpy(dst, src, (move_dwords << 2));

	/* ensure data is committed before update wptr */
	dma_wmb();

	wptr = (wptr + msg_size_align) % queue_size_dword;
	hgsl_dbq_set_state_info((uint32_t *)dbq->vbase,
				HGSL_DBQ_METADATA_QUEUE_INDEX,
				HGSL_DBQ_CONTEXT_ANY,
				HGSL_DBQ_WRITE_INDEX_OFFSET_IN_DWORD,
							wptr);

	hgsl_dbq_set_state_info((uint32_t *)dbq->vbase,
				HGSL_DBQ_METADATA_CONTEXT_INFO,
				cmds->ctx_id,
				HGSL_DBQ_CONTEXT_CONTEXT_ID_OFFSET_IN_DWORD,
				cmds->ctx_id);

	hgsl_dbq_set_state_info((uint32_t *)dbq->vbase,
				HGSL_DBQ_METADATA_CONTEXT_INFO,
				((struct hgsl_db_cmds *)src)->ctx_id,
				HGSL_DBQ_CONTEXT_TIMESTAMP_OFFSET_IN_DWORD,
				((struct hgsl_db_cmds *)src)->timestamp);

	/* confirm write to memory done before ring door bell. */
	wmb();

	if (is_global_db(ctxt->tcsr_idx))
		/* trigger TCSR interrupt for global doorbell */
		tcsr_ring_global_db(hgsl, ctxt->tcsr_idx, dbq->dbq_idx);
	else
		/* trigger GMU interrupt */
		gmu_ring_local_db(hgsl, dbq->dbq_idx);

quit:
	db_set_busy_state(dbq->vbase, false);

	mutex_unlock(&dbq->lock);
	/* let user try again incase we miss to submit */
	if (-ETIMEDOUT == ret) {
		LOGE("Timed out to send db msg, try again\n");
		ret = -EAGAIN;
	}
	return ret;
}

static int hgsl_db_next_timestamp(struct hgsl_context *ctxt,
	uint32_t *timestamp)
{
	if (timestamp == NULL) {
		LOGE("invalid timestamp");
		return -EINVAL;
	} else if ((ctxt->flags & GSL_CONTEXT_FLAG_USER_GENERATED_TS) == 0) {
		return 0;
	} else if (ctxt->flags & GSL_CONTEXT_FLAG_CLIENT_GENERATED_TS) {
		if (hgsl_ts32_ge(ctxt->queued_ts, *timestamp)) {
			LOGW("ctx:%d next client ts %d isn't greater than current ts %d",
				ctxt->context_id, *timestamp, ctxt->queued_ts);
			return -ERANGE;
		}
	} else {
		/*
		 * callers use 0 and ~0 as special values, do not assign them as
		 * timestamps, instead rollover to 1.
		 */
		*timestamp = ctxt->queued_ts + 1;
		if (*timestamp == UINT_MAX)
			*timestamp = 1;
	}
	return 0;
}

static void ts_retire_worker(struct work_struct *work)
{
	struct qcom_hgsl *hgsl =
		container_of(work, struct qcom_hgsl, ts_retire_work);
	struct hgsl_active_wait *wait, *w;

	spin_lock(&hgsl->active_wait_lock);
	list_for_each_entry_safe(wait, w, &hgsl->active_wait_list, head) {
		if (_timestamp_retired(wait->ctxt, wait->timestamp))
			wake_up_all(&wait->ctxt->wait_q);
	}
	spin_unlock(&hgsl->active_wait_lock);

	_signal_contexts(hgsl);
}

static irqreturn_t hgsl_tcsr_isr(struct device *dev, uint32_t status)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct qcom_hgsl *hgsl = platform_get_drvdata(pdev);

	if ((status & GLB_DB_DEST_TS_RETIRE_IRQ_MASK) == 0)
		return IRQ_NONE;

	queue_work(hgsl->wq, &hgsl->ts_retire_work);

	return IRQ_HANDLED;
}

static int hgsl_init_global_db(struct qcom_hgsl *hgsl,
				enum hgsl_tcsr_role role, int idx)
{
	struct device *dev = hgsl->dev;
	struct device_node *np = dev->of_node;
	bool  is_sender = (role == HGSL_TCSR_ROLE_SENDER);
	const char *node_name = is_sender ? "qcom,glb-db-senders" :
			"qcom,glb-db-receivers";
	struct device_node *tcsr_np;
	struct platform_device *tcsr_pdev;
	struct hgsl_tcsr *tcsr;
	int ret;

	if (hgsl->tcsr[idx][role] != NULL)
		return 0;

	tcsr_np = of_parse_phandle(np, node_name, idx);
	if (IS_ERR_OR_NULL(tcsr_np)) {
		LOGE("failed to find %s node\n", node_name);
		ret = -ENODEV;
		goto fail;
	}

	tcsr_pdev = of_find_device_by_node(tcsr_np);
	if (IS_ERR_OR_NULL(tcsr_pdev)) {
		LOGE("failed to find %s tcsr dev from node\n",
			is_sender ? "sender" : "receiver");
		ret = -ENODEV;
		goto fail;
	}

	if (!is_sender && !hgsl->wq) {
		hgsl->wq = create_workqueue("hgsl-wq");
		if (IS_ERR_OR_NULL(hgsl->wq)) {
			LOGE("failed to create workqueue\n");
			ret = PTR_ERR(hgsl->wq);
			goto fail;
		}
		INIT_WORK(&hgsl->ts_retire_work, ts_retire_worker);

		INIT_LIST_HEAD(&hgsl->active_wait_list);
		spin_lock_init(&hgsl->active_wait_lock);
	}

	tcsr = hgsl_tcsr_request(tcsr_pdev, role, dev,
			is_sender ? NULL : hgsl_tcsr_isr);
	if (IS_ERR_OR_NULL(tcsr)) {
		LOGE("failed to request %s tcsr, ret %lx\n",
			is_sender ? "sender" : "receiver", PTR_ERR(tcsr));
		ret = tcsr ? PTR_ERR(tcsr) : -ENODEV;
		goto destroy_wq;
	}

	ret = hgsl_tcsr_enable(tcsr);
	if (ret) {
		LOGE("failed to enable %s tcsr, ret %d\n",
			is_sender ? "sender" : "receiver", ret);
		goto free_tcsr;
	}

	if (!is_sender)
		hgsl_tcsr_irq_enable(tcsr, GLB_DB_DEST_TS_RETIRE_IRQ_MASK,
					true);

	hgsl->tcsr[idx][role] = tcsr;
	return 0;

free_tcsr:
	hgsl_tcsr_free(tcsr);
destroy_wq:
	if (hgsl->wq)
		destroy_workqueue(hgsl->wq);
fail:
	return ret;
}

static int hgsl_init_local_db(struct qcom_hgsl *hgsl)
{
	struct platform_device *pdev = to_platform_device(hgsl->dev);

	if (hgsl->reg_dbidx.vaddr != NULL)
		return 0;
	else
		return hgsl_reg_map(pdev, IORESOURCE_GMUCX, &hgsl->reg_dbidx);
}

static int hgsl_init_db_signal(struct qcom_hgsl *hgsl, int tcsr_idx)
{
	int ret;

	mutex_lock(&hgsl->mutex);
	if (is_global_db(tcsr_idx)) {
		ret = hgsl_init_global_db(hgsl, HGSL_TCSR_ROLE_SENDER,
						tcsr_idx);
		ret |= hgsl_init_global_db(hgsl, HGSL_TCSR_ROLE_RECEIVER,
						tcsr_idx);
	} else {
		ret = hgsl_init_local_db(hgsl);
	}
	mutex_unlock(&hgsl->mutex);

	return ret;
}

static void hgsl_dbcq_init(struct hgsl_priv *priv,
	struct hgsl_context *ctxt, uint32_t db_signal,
	uint32_t gmuaddr, uint32_t irq_idx)
{
	struct qcom_hgsl *hgsl = priv->dev;
	struct doorbell_context_queue *dbcq = NULL;
	int tcsr_idx = 0;
	int ret = 0;

	if ((db_signal <= DB_SIGNAL_INVALID) ||
		(db_signal > DBCQ_SIGNAL_MAX) ||
		(gmuaddr == 0) ||
		(irq_idx == GLB_DB_DEST_TS_RETIRE_IRQ_ID)) {
		LOGE("Invalid db signal %d or queue buffer 0x%x\n or irq_idx %d",
			db_signal, gmuaddr, irq_idx);
		goto err;
	}

	dbcq = hgsl_zalloc(sizeof(struct doorbell_context_queue));
	if (!dbcq) {
		LOGE("Failed to allocate memory for doorbell context queue\n");
		goto err;
	}

	tcsr_idx = db_signal - DB_SIGNAL_GLOBAL_0;
	ret = hgsl_init_db_signal(hgsl, tcsr_idx);
	if (ret != 0) {
		LOGE("failed to init dbcq signal %d", db_signal);
		goto err;
	}

	dbcq->db_signal = db_signal;
	dbcq->irq_idx = irq_idx;
	dbcq->queue_header_gmuaddr = gmuaddr;
	dbcq->queue_body_gmuaddr = dbcq->queue_header_gmuaddr + HGSL_CTXT_QUEUE_BODY_OFFSET;
	ctxt->tcsr_idx = tcsr_idx;
	ctxt->dbcq = dbcq;
	return;

err:
	hgsl_free(dbcq);
}

static void hgsl_dbcq_close(struct hgsl_context *ctxt)
{
	struct doorbell_context_queue *dbcq = ctxt->dbcq;

	if (!dbcq)
		return;

	if (dbcq->queue_mem != NULL) {
		if (dbcq->queue_mem->dma_buf != NULL) {
			if (dbcq->queue_header != NULL) {
				dma_buf_vunmap(dbcq->queue_mem->dma_buf, &dbcq->map);
				dbcq->queue_header = NULL;
			}
			dma_buf_end_cpu_access(dbcq->queue_mem->dma_buf,
						   DMA_BIDIRECTIONAL);
		}
		hgsl_sharedmem_free(dbcq->queue_mem);
	}

	hgsl_free(dbcq);
	ctxt->dbcq = NULL;
}

static int hgsl_dbcq_open(struct hgsl_priv *priv,
	struct hgsl_context *ctxt)
{
	struct qcom_hgsl *hgsl = priv->dev;
	struct doorbell_context_queue *dbcq = ctxt->dbcq;
	struct hgsl_hab_channel_t *hab_channel = NULL;
	int ret = 0;
	struct ctx_queue_header *queue_header = NULL;

	if (!dbcq) {
		ret = -EPERM;
		goto out;
	}

	if (dbcq->queue_header != NULL)
		goto out;

	ret = hgsl_hyp_channel_pool_get(&priv->hyp_priv, 0, &hab_channel);
	if (ret == -EINTR) {
		goto out;
	} else if (ret != 0) {
		LOGE("failed to open hab channel");
		goto err;
	}

	dbcq->queue_mem = hgsl_zalloc(sizeof(struct hgsl_mem_node));
	if (!dbcq->queue_mem) {
		LOGE("out of memory");
		ret = -ENOMEM;
		goto err;
	}

	dbcq->queue_mem->flags = GSL_MEMFLAGS_UNCACHED | GSL_MEMFLAGS_ALIGN4K;
	ret = hgsl_sharedmem_alloc(hgsl->dev, HGSL_CTXT_QUEUE_TOTAL_SIZE,
		dbcq->queue_mem->flags, dbcq->queue_mem);
	if (ret != 0) {
		LOGE("Failed to allocate memory for doorbell context queue buffer\n");
		goto err;
	}

	dma_buf_begin_cpu_access(dbcq->queue_mem->dma_buf, DMA_BIDIRECTIONAL);
	ret = dma_buf_vmap(dbcq->queue_mem->dma_buf, &dbcq->map);
	if (ret) {
		LOGE("failed to map dbq buffer");
		goto err;
	}

	dbcq->queue_header = dbcq->map.vaddr;
	dbcq->queue_body = (void *)((uint8_t *)dbcq->queue_header + HGSL_CTXT_QUEUE_BODY_OFFSET);
	dbcq->indirect_ibs = NULL;
	dbcq->queue_size = HGSL_CTXT_QUEUE_BODY_DWSIZE;

	queue_header = (struct ctx_queue_header *)dbcq->queue_header;
	queue_header->version = 0;
	queue_header->startAddr = dbcq->queue_body_gmuaddr;
	queue_header->dwSize = HGSL_CTXT_QUEUE_BODY_DWSIZE;
	queue_header->readIdx = 0;
	queue_header->writeIdx = 0;
	queue_header->dbqSignal = dbcq->db_signal;

	ret = hgsl_hyp_context_register_dbcq(hab_channel, ctxt->devhandle, ctxt->context_id,
		dbcq->queue_mem->dma_buf, dbcq->queue_mem->memdesc.size,
		HGSL_CTXT_QUEUE_BODY_OFFSET, &ctxt->dbcq_export_id);
	if (ret) {
		LOGE("Failed to register dbcq %d\n", ret);
		goto err;
	}

	goto out;
err:
	hgsl_dbcq_close(ctxt);
	ret = -EPERM;
out:
	hgsl_hyp_channel_pool_put(hab_channel);

	LOGI("%d", ret);
	return ret;
}

static int hgsl_dbcq_issue_cmd(struct hgsl_priv  *priv,
			struct hgsl_context *ctxt, uint32_t num_ibs,
			uint32_t gmu_cmd_flags,
			uint32_t *timestamp,
			struct hgsl_fw_ib_desc ib_descs[],
			uint64_t user_profile_gpuaddr)
{
	int ret;
	uint32_t msg_dwords;
	uint32_t msg_buf_sz;
	struct hgsl_db_cmds *cmds = NULL;
	struct db_msg_request req;
	struct db_msg_response resp;
	struct db_msg_id db_msg_id;
	struct doorbell_context_queue *dbcq = NULL;
	struct qcom_hgsl  *hgsl = priv->dev;

	mutex_lock(&ctxt->lock);

	ret = hgsl_dbcq_open(priv, ctxt);
	if (ret)
		goto out;

	dbcq = ctxt->dbcq;
	db_msg_id.msg_id = HTOF_MSG_ISSUE_CMD;
	db_msg_id.seq_no = dbcq->seq_num++;

	msg_dwords = MSG_ISSUE_INF_SZ() + MSG_ISSUE_IBS_SZ(num_ibs);
	msg_buf_sz = ALIGN(msg_dwords, 4) << 2;

	if (num_ibs > ECP_MAX_NUM_IB1) {
		LOGE("number of ibs %d exceed max %d",
			num_ibs, ECP_MAX_NUM_IB1);
		ret = -EINVAL;
		goto out;
	}

	if ((msg_buf_sz >= dbcq->queue_size) || (msg_buf_sz > (MSG_SZ_MASK >> MSG_SZ_SHIFT))) {
		LOGE("msg size %d exceed queue size %d or max hfi cmd size %d",
			msg_buf_sz, dbcq->queue_size, (MSG_SZ_MASK >> MSG_SZ_SHIFT));
		ret = -EINVAL;
		goto out;
	}

	ret = hgsl_db_next_timestamp(ctxt, timestamp);
	if (ret)
		goto out;

	cmds = hgsl_zalloc(msg_buf_sz);
	if (cmds == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	cmds->header = (union hfi_msg_header)HFI_ISSUE_IB_HEADER(num_ibs,
					msg_dwords,
					db_msg_id.seq_no);
	cmds->ctx_id = ctxt->context_id;
	cmds->num_ibs = num_ibs;
	cmds->cmd_flags = gmu_cmd_flags;
	cmds->timestamp = *timestamp;
	cmds->user_profile_gpuaddr = user_profile_gpuaddr;
	memcpy(cmds->ib_descs, ib_descs, sizeof(ib_descs[0]) * num_ibs);

	req.msg_has_response = 0;
	req.msg_has_ret_packet = 0;
	req.ignore_ret_packet = 1;
	req.msg_dwords = msg_dwords;
	req.ptr_data = cmds;

	if (!ctxt->is_killed) {
		ret = dbcq_send_msg(priv, &db_msg_id, &req, &resp, ctxt);
	} else {
		/* Retire ts immediately*/
		set_context_retired_ts(ctxt, *timestamp);

		/* Trigger event to waitfor ts thread */
		_signal_contexts(hgsl);
		ret = 0;
	}

	if (ret == 0)
		ctxt->queued_ts = *timestamp;
out:
	hgsl_free(cmds);
	mutex_unlock(&ctxt->lock);
	return ret;
}

static int hgsl_db_issue_cmd(struct hgsl_priv  *priv,
			struct hgsl_context *ctxt, uint32_t num_ibs,
			uint32_t gmu_cmd_flags,
			uint32_t *timestamp,
			struct hgsl_fw_ib_desc ib_descs[],
			uint64_t user_profile_gpuaddr)
{
	int ret;
	uint32_t msg_dwords;
	uint32_t msg_buf_sz;
	struct hgsl_db_cmds *cmds;
	struct db_msg_request req;
	struct db_msg_response resp;
	struct db_msg_id db_msg_id;
	struct doorbell_queue *dbq = ctxt->dbq;
	struct qcom_hgsl  *hgsl = priv->dev;

	ret = hgsl_dbcq_issue_cmd(priv, ctxt, num_ibs, gmu_cmd_flags,
							timestamp, ib_descs, user_profile_gpuaddr);
	if (ret != -EPERM)
		return ret;

	if (dbq == NULL)
		return -EPERM;

	db_msg_id.msg_id = HTOF_MSG_ISSUE_CMD;
	db_msg_id.seq_no = atomic_inc_return(&dbq->seq_num);

	msg_dwords = MSG_ISSUE_INF_SZ() + MSG_ISSUE_IBS_SZ(num_ibs);
	msg_buf_sz = ALIGN(msg_dwords, 4) << 2;

	if (num_ibs > ECP_MAX_NUM_IB1) {
		LOGE("number of ibs %d exceed max %d",
			num_ibs, ECP_MAX_NUM_IB1);
		return -EINVAL;
	}

	if ((msg_buf_sz >= dbq->data.dwords) || (msg_buf_sz > (MSG_SZ_MASK >> MSG_SZ_SHIFT))) {
		LOGE("msg size %d exceed queue size %d or max hfi cmd size %d",
			msg_buf_sz, dbq->data.dwords, (MSG_SZ_MASK >> MSG_SZ_SHIFT));
		return -EINVAL;
	}

	ret = hgsl_db_next_timestamp(ctxt, timestamp);
	if (ret)
		return ret;

	cmds = hgsl_zalloc(msg_buf_sz);
	if (cmds == NULL)
		return -ENOMEM;

	cmds->header = (union hfi_msg_header)HFI_ISSUE_IB_HEADER(num_ibs,
					msg_dwords,
					db_msg_id.seq_no);
	cmds->ctx_id = ctxt->context_id;
	cmds->num_ibs = num_ibs;
	cmds->cmd_flags = gmu_cmd_flags;
	cmds->timestamp = *timestamp;
	cmds->user_profile_gpuaddr = user_profile_gpuaddr;
	memcpy(cmds->ib_descs, ib_descs, sizeof(ib_descs[0]) * num_ibs);

	req.msg_has_response = 0;
	req.msg_has_ret_packet = 0;
	req.ignore_ret_packet = 1;
	req.msg_dwords = msg_dwords;
	req.ptr_data = cmds;

	if (!ctxt->is_killed) {
		ret = db_send_msg(priv, &db_msg_id, &req, &resp, ctxt);
	} else {
		/* Retire ts immediately*/
		set_context_retired_ts(ctxt, *timestamp);

		/* Trigger event to waitfor ts thread */
		_signal_contexts(hgsl);
		ret = 0;
	}

	if (ret == 0)
		ctxt->queued_ts = *timestamp;

	hgsl_free(cmds);
	return ret;
}

#define USRPTR(a) u64_to_user_ptr((uint64_t)(a))

static void hgsl_reset_dbq(struct doorbell_queue *dbq)
{
	if (dbq->dma) {
		dma_buf_end_cpu_access(dbq->dma,
				       DMA_BIDIRECTIONAL);
		if (dbq->vbase) {
			dma_buf_vunmap(dbq->dma, &dbq->map);
			dbq->vbase = NULL;
		}
		dma_buf_put(dbq->dma);
		dbq->dma = NULL;
	}

	dbq->state = DB_STATE_Q_UNINIT;
}

static inline uint32_t get_context_retired_ts(struct hgsl_context *ctxt)
{
	unsigned int ts = ctxt->shadow_ts->eop;

	/* ensure read is done before comparison */
	dma_rmb();
	return ts;
}

static inline void set_context_retired_ts(struct hgsl_context *ctxt,
				unsigned int ts)
{
	ctxt->shadow_ts->eop = ts;

	/* ensure update is done before return */
	dma_wmb();
}

static inline bool _timestamp_retired(struct hgsl_context *ctxt,
				unsigned int timestamp)
{
	return hgsl_ts32_ge(get_context_retired_ts(ctxt), timestamp);
}

static inline void _destroy_context(struct kref *kref);
static void _signal_contexts(struct qcom_hgsl *hgsl)
{
	struct hgsl_context *ctxt;
	int i;
	uint32_t ts;

	for (i = 0; i < HGSL_CONTEXT_NUM; i++) {
		ctxt = hgsl_get_context(hgsl, i);

		if ((ctxt == NULL) || (ctxt->timeline == NULL)) {
			hgsl_put_context(ctxt);
			continue;
		}

		ts = get_context_retired_ts(ctxt);
		if (ts != ctxt->last_ts) {
			hgsl_hsync_timeline_signal(ctxt->timeline, ts);
			ctxt->last_ts = ts;
		}
		hgsl_put_context(ctxt);

	}

}

static int hgsl_init_context(struct qcom_hgsl *hgsl)
{
	int ret = 0;

	hgsl->contexts = kzalloc(sizeof(struct hgsl_context *) *
				HGSL_CONTEXT_NUM, GFP_KERNEL);
	if (!hgsl->contexts) {
		ret = -ENOMEM;
		goto out;
	}
	rwlock_init(&hgsl->ctxt_lock);

out:
	return ret;
}

static int hgsl_init_global_hyp_channel(struct qcom_hgsl *hgsl)
{
	int ret = 0;
	int rval = 0;

	ret = hgsl_hyp_init(&hgsl->global_hyp, hgsl->dev, 0, "hgsl");
	if (ret != 0)
		goto out;

	ret = hgsl_hyp_gsl_lib_open(&hgsl->global_hyp, 0, &rval);
	if (rval)
		ret = -EINVAL;
	else
		hgsl->global_hyp_inited = true;
out:
	if (ret)
		hgsl_hyp_close(&hgsl->global_hyp);

	return ret;
}

static int hgsl_dbq_init(struct qcom_hgsl *hgsl,
	uint32_t dbq_idx, uint32_t db_signal)
{
	struct doorbell_queue *dbq;
	struct dma_buf *dma_buf;
	int tcsr_idx;
	int ret;

	if ((db_signal <= DB_SIGNAL_INVALID) ||
		(db_signal > DB_SIGNAL_MAX)) {
		LOGE("Invalid db signal %d\n", db_signal);
		return -EINVAL;
	}

	if (dbq_idx >= MAX_DB_QUEUE) {
		LOGE("Invalid dbq_idx %d\n", dbq_idx);
		return -EINVAL;
	}

	if ((dbq_idx == GLB_DB_DEST_TS_RETIRE_IRQ_ID) && (db_signal != DB_SIGNAL_LOCAL)) {
		LOGE("TCSR send and receive irq bit conflict %d, %d", dbq_idx, db_signal);
		return -EINVAL;
	}

	dbq = &hgsl->dbq[dbq_idx];
	mutex_lock(&dbq->lock);
	if (dbq->state == DB_STATE_Q_INIT_DONE) {
		mutex_unlock(&dbq->lock);
		return 0;
	}

	ret = hgsl_hyp_get_dbq_info(&hgsl->global_hyp, dbq_idx,
		&hgsl->dbq_info[dbq_idx]);
	if (ret) {
		LOGE("Failed to get dbq info %d\n", ret);
		goto err;
	}

	dma_buf = hgsl->dbq_info[dbq_idx].dma_buf;
	dbq->state = DB_STATE_Q_FAULT;
	dbq->dma = dma_buf;
	dbq->dbq_idx = dbq_idx;
	atomic_set(&dbq->seq_num, 0);

	dma_buf_begin_cpu_access(dbq->dma, DMA_BIDIRECTIONAL);
	ret = dma_buf_vmap(dbq->dma, &dbq->map);
	if (ret)
		goto err;

	dbq->vbase = dbq->map.vaddr;
	dbq->data.vaddr = (uint32_t *)dbq->vbase +
		hgsl->dbq_info[dbq_idx].queue_off_dwords;
	dbq->data.dwords = hgsl->dbq_info[dbq_idx].queue_dwords;

	tcsr_idx = (db_signal != DB_SIGNAL_LOCAL) ?
				db_signal - DB_SIGNAL_GLOBAL_0 : -1;
	ret = hgsl_init_db_signal(hgsl, tcsr_idx);
	if (ret != 0) {
		LOGE("failed to init dbq signal %d, idx %d",
			db_signal, dbq_idx);
		goto err;
	}

	dbq->tcsr_idx = tcsr_idx;
	dbq->state = DB_STATE_Q_INIT_DONE;

	mutex_unlock(&dbq->lock);
	return 0;
err:
	hgsl_reset_dbq(dbq);
	mutex_unlock(&dbq->lock);

	return ret;
}

static inline void _unmap_shadow(struct hgsl_context *ctxt)
{
	if (ctxt->shadow_ts) {
		dma_buf_vunmap(ctxt->shadow_ts_node.dma_buf, &ctxt->map);
		dma_buf_end_cpu_access(ctxt->shadow_ts_node.dma_buf, DMA_FROM_DEVICE);
		ctxt->shadow_ts = NULL;
	}
}

static inline void _destroy_context(struct kref *kref)
{
	struct hgsl_context *ctxt =
			container_of(kref, struct hgsl_context, kref);
	struct doorbell_queue *dbq = ctxt->dbq;

	LOGD("%d", ctxt->context_id);
	if (ctxt->timeline) {
		hgsl_hsync_timeline_fini(ctxt);
		hgsl_hsync_timeline_put(ctxt->timeline);
	}

	if (dbq) {
		hgsl_dbq_set_state_info((uint32_t *)dbq->vbase,
					HGSL_DBQ_METADATA_CONTEXT_INFO,
					ctxt->context_id,
					HGSL_DBQ_CONTEXT_DESTROY_OFFSET_IN_DWORD,
					1);
	}
	_unmap_shadow(ctxt);
	ctxt->destroyed = true;
	/* ensure update is done before return */
	dma_wmb();
}

static struct hgsl_context *hgsl_get_context(struct qcom_hgsl *hgsl,
	uint32_t context_id)
{
	struct hgsl_context *ctxt = NULL;

	if (context_id < HGSL_CONTEXT_NUM) {
		read_lock(&hgsl->ctxt_lock);
		ctxt = hgsl->contexts[context_id];
		if (ctxt)
			kref_get(&ctxt->kref);
		read_unlock(&hgsl->ctxt_lock);
	}

	return ctxt;
}

static struct hgsl_context *hgsl_get_context_owner(struct hgsl_priv *priv,
	uint32_t context_id)
{
	struct hgsl_context *ctxt = NULL;
	struct qcom_hgsl *hgsl = priv->dev;

	ctxt = hgsl_get_context(hgsl, context_id);

	if (ctxt && ctxt->priv != priv) {
		hgsl_put_context(ctxt);
		ctxt = NULL;
	}

	return ctxt;
}

static struct hgsl_context *hgsl_remove_context(struct hgsl_priv *priv,
	uint32_t context_id)
{
	struct hgsl_context *ctxt = NULL;
	struct qcom_hgsl *hgsl = priv->dev;

	if (context_id < HGSL_CONTEXT_NUM) {
		write_lock(&hgsl->ctxt_lock);
		ctxt = hgsl->contexts[context_id];
		if ((ctxt != NULL) && (ctxt->priv == priv))
			hgsl->contexts[context_id] = NULL;
		else
			ctxt = NULL;
		write_unlock(&hgsl->ctxt_lock);
	}

	return ctxt;
}

static int hgsl_check_context_owner(struct hgsl_priv *priv,
	uint32_t context_id)
{
	struct hgsl_context *ctxt = hgsl_get_context_owner(priv, context_id);
	int ret = -EINVAL;

	if (ctxt) {
		hgsl_put_context(ctxt);
		ret = 0;
	}

	return ret;
}

static void hgsl_put_context(struct hgsl_context *ctxt)
{
	if (ctxt)
		kref_put(&ctxt->kref, _destroy_context);
}

static int hgsl_read_shadow_timestamp(struct hgsl_context *ctxt,
	enum gsl_timestamp_type_t type,
	uint32_t *timestamp)
{
	int ret = -EINVAL;

	if (ctxt && ctxt->shadow_ts) {
		switch (type) {
		case GSL_TIMESTAMP_RETIRED:
			*timestamp = ctxt->shadow_ts->eop;
			ret = 0;
			break;
		case GSL_TIMESTAMP_CONSUMED:
			*timestamp = ctxt->shadow_ts->sop;
			ret = 0;
			break;
		case GSL_TIMESTAMP_QUEUED:
			//todo
			break;
		default:
			break;
		}
		/* ensure read is done before return */
		dma_rmb();
	}
	LOGD("%d, %u, %u, %u", ret, ctxt->context_id, type, *timestamp);
	return ret;
}

static int hgsl_check_shadow_timestamp(struct hgsl_context *ctxt,
	enum gsl_timestamp_type_t type,
	uint32_t timestamp, bool *expired)
{
	uint32_t ts_read = 0;
	int ret = hgsl_read_shadow_timestamp(ctxt, type, &ts_read);

	if (!ret)
		*expired = hgsl_ts32_ge(ts_read, timestamp);

	return ret;
}

static void hgsl_get_shadowts_mem(struct hgsl_hab_channel_t *hab_channel,
	struct hgsl_context *ctxt)
{
	struct dma_buf *dma_buf = NULL;
	int ret = 0;

	ret = hgsl_hyp_get_shadowts_mem(hab_channel, ctxt->context_id,
				&ctxt->shadow_ts_flags, &ctxt->shadow_ts_node);
	if (ret)
		goto out;

	dma_buf = ctxt->shadow_ts_node.dma_buf;
	if (dma_buf) {
		dma_buf_begin_cpu_access(dma_buf, DMA_FROM_DEVICE);
		ret = dma_buf_vmap(dma_buf, &ctxt->map);
		if (ret)
			goto out;
		ctxt->shadow_ts = (struct shadow_ts *)ctxt->map.vaddr;
	}
	LOGD("0x%llx, 0x%llx", (uint64_t)ctxt, (uint64_t)ctxt->map.vaddr);

out:
	if (ret) {
		if (dma_buf)
			dma_buf_end_cpu_access(dma_buf, DMA_FROM_DEVICE);
		hgsl_hyp_put_shadowts_mem(hab_channel, &ctxt->shadow_ts_node);
		ctxt->shadow_ts_flags = 0;
		memset(&ctxt->shadow_ts_node, 0, sizeof(ctxt->shadow_ts_node));
	}
}

static int hgsl_ioctl_get_shadowts_mem(struct file *filep, unsigned long arg)
{
	struct hgsl_priv *priv = filep->private_data;
	struct hgsl_ioctl_get_shadowts_mem_params params;
	struct hgsl_context *ctxt = NULL;
	struct dma_buf *dma_buf = NULL;
	int ret = 0;

	if (copy_from_user(&params, USRPTR(arg), sizeof(params))) {
		pr_err_ratelimited("failed to copy params from user");
		ret = -EFAULT;
		goto out;
	}

	ctxt = hgsl_get_context_owner(priv, params.ctxthandle);
	if (ctxt == NULL) {
		ret = -EINVAL;
		goto out;
	}

	params.flags = ctxt->shadow_ts_flags;
	params.size = ctxt->shadow_ts_node.memdesc.size64;
	params.fd = -1;

	dma_buf = ctxt->shadow_ts_node.dma_buf;
	if (dma_buf) {
		params.fd = dma_buf_fd(dma_buf, O_CLOEXEC);
		if (params.fd < 0) {
			LOGE("dma buf to fd failed\n");
			ret = -ENOMEM;
			goto out;
		}
		get_dma_buf(dma_buf);
	}

	if (copy_to_user(USRPTR(arg), &params, sizeof(params))) {
		ret = -EFAULT;
	}

out:
	hgsl_put_context(ctxt);
	return ret;
}

static int hgsl_ioctl_put_shadowts_mem(struct file *filep, unsigned long arg)
{
	struct hgsl_priv *priv = filep->private_data;
	struct hgsl_ioctl_put_shadowts_mem_params params;
	int ret = 0;

	if (copy_from_user(&params, USRPTR(arg), sizeof(params))) {
		pr_err_ratelimited("failed to copy params from user");
		ret = -EFAULT;
		goto out;
	}

	ret = hgsl_check_context_owner(priv, params.ctxthandle);

	/* return OK and keep shadow ts until we destroy context*/
out:
	return ret;
}

static int hgsl_ctxt_create_dbq(struct hgsl_priv *priv,
	struct hgsl_hab_channel_t *hab_channel,
	struct hgsl_context *ctxt)
{
	struct qcom_hgsl *hgsl = priv->dev;
	uint32_t dbq_info;
	uint32_t dbq_idx;
	uint32_t db_signal;
	uint32_t queue_gmuaddr;
	uint32_t irq_idx;
	int ret;

	ret = hgsl_hyp_query_dbcq(hab_channel, ctxt->devhandle, ctxt->context_id,
		HGSL_CTXT_QUEUE_TOTAL_SIZE, &db_signal, &queue_gmuaddr, &irq_idx);
	if (!ret) {
		hgsl_dbcq_init(priv, ctxt, db_signal, queue_gmuaddr, irq_idx);
		return 0;
	}

	ret = hgsl_hyp_dbq_create(hab_channel,
				ctxt->context_id, &dbq_info);
	if (ret)
		return ret;

	dbq_idx = dbq_info >> 16;
	db_signal = dbq_info & 0xFFFF;
	ret = hgsl_dbq_init(hgsl, dbq_idx, db_signal);
	if (ret)
		return ret;

	ctxt->dbq = &hgsl->dbq[dbq_idx];
	ctxt->tcsr_idx = ctxt->dbq->tcsr_idx;
	hgsl_dbq_set_state_info(ctxt->dbq->vbase,
				HGSL_DBQ_METADATA_CONTEXT_INFO,
				ctxt->context_id,
				HGSL_DBQ_CONTEXT_DESTROY_OFFSET_IN_DWORD,
				0);
	return 0;
}

static int hgsl_ctxt_destroy(struct hgsl_priv *priv,
	struct hgsl_hab_channel_t *hab_channel,
	uint32_t context_id, uint32_t *rval)
{
	struct hgsl_context *ctxt = NULL;
	int ret;
	bool put_channel = false;


	if (!hab_channel) {
		ret = hgsl_hyp_channel_pool_get(&priv->hyp_priv, 0, &hab_channel);
		if (ret) {
			LOGE("Failed to get hab channel %d", ret);
			goto out;
		}
		put_channel = true;
	}

	ctxt = hgsl_remove_context(priv, context_id);
	if (!ctxt) {
		LOGE("Invalid context id %d\n", context_id);
		ret = -EINVAL;
		goto out;
	}

	/* unblock all waiting threads on this context */
	ctxt->in_destroy = true;
	wake_up_all(&ctxt->wait_q);

	hgsl_put_context(ctxt);

	while (!ctxt->destroyed)
		cpu_relax();

	hgsl_hyp_put_shadowts_mem(hab_channel, &ctxt->shadow_ts_node);

	ret = hgsl_hyp_ctxt_destroy(hab_channel,
		ctxt->devhandle, ctxt->context_id, rval, ctxt->dbcq_export_id);

	hgsl_dbcq_close(ctxt);
	hgsl_free(ctxt);

out:
	if (put_channel)
		hgsl_hyp_channel_pool_put(hab_channel);
	return ret;
}

static inline bool hgsl_ctxt_use_global_dbq(struct hgsl_context *ctxt)
{
	return ((ctxt != NULL) &&
		(ctxt->shadow_ts != NULL) &&
		((ctxt->dbq != NULL) || (ctxt->dbcq != NULL)) &&
		(is_global_db(ctxt->tcsr_idx)));
}

static inline bool hgsl_ctxt_use_dbq(struct hgsl_context *ctxt)
{
	return ((ctxt != NULL) &&
		((ctxt->dbq != NULL) || (ctxt->dbcq != NULL)) &&
		((ctxt->shadow_ts != NULL) || (!is_global_db(ctxt->tcsr_idx))));
}

static int hgsl_ioctl_ctxt_create(struct file *filep, unsigned long arg)
{
	struct hgsl_priv *priv = filep->private_data;
	struct qcom_hgsl *hgsl = priv->dev;
	struct hgsl_ioctl_ctxt_create_params params;
	struct hgsl_context *ctxt = NULL;
	int ret = 0;
	struct hgsl_hab_channel_t *hab_channel = NULL;
	bool ctxt_created = false;

	if (copy_from_user(&params, USRPTR(arg), sizeof(params))) {
		pr_err_ratelimited("failed to copy params from user");
		ret = -EFAULT;
		return ret;
	}

	ret = hgsl_hyp_channel_pool_get(&priv->hyp_priv, 0, &hab_channel);
	if (ret) {
		LOGE("Failed to get hab channel %d", ret);
		goto out;
	}

	ctxt = hgsl_zalloc(sizeof(*ctxt));
	if (ctxt == NULL) {
		ret = -ENOMEM;
		return ret;
	}

	if (params.flags & GSL_CONTEXT_FLAG_CLIENT_GENERATED_TS)
		params.flags |= GSL_CONTEXT_FLAG_USER_GENERATED_TS;

	if (params.flags & GSL_CONTEXT_FLAG_BIND) {
		params.flags &= ~GSL_CONTEXT_FLAG_CLIENT_GENERATED_TS;
		params.flags |= GSL_CONTEXT_FLAG_USER_GENERATED_TS;
	}

	ret = hgsl_hyp_ctxt_create(hab_channel, &params);
	if (ret)
		goto out;

	if (params.ctxthandle >= HGSL_CONTEXT_NUM) {
		LOGE("invalid ctxt id %d", params.ctxthandle);
		ret = -EINVAL;
		goto out;
	}

	ctxt->context_id = params.ctxthandle;
	ctxt->devhandle = params.devhandle;
	ctxt->pid = priv->pid;
	ctxt->priv = priv;
	ctxt->flags = params.flags;

	kref_init(&ctxt->kref);
	init_waitqueue_head(&ctxt->wait_q);
	mutex_init(&ctxt->lock);

	write_lock(&hgsl->ctxt_lock);
	if (hgsl->contexts[ctxt->context_id] != NULL) {
		LOGE("context id %d already created",
			ctxt->context_id);
		ret = -EBUSY;
		write_unlock(&hgsl->ctxt_lock);
		goto out;
	}

	hgsl->contexts[ctxt->context_id] = ctxt;
	write_unlock(&hgsl->ctxt_lock);
	ctxt_created = true;

	hgsl_get_shadowts_mem(hab_channel, ctxt);
	if (hgsl->global_hyp_inited && !hgsl->db_off)
		hgsl_ctxt_create_dbq(priv, hab_channel, ctxt);

	if (hgsl_ctxt_use_global_dbq(ctxt)) {
		ret = hgsl_hsync_timeline_create(ctxt);
		if (ret < 0)
			LOGE("hsync timeline failed for context %d", params.ctxthandle);
	}

	if (ctxt->timeline)
		params.sync_type = HGSL_SYNC_TYPE_HSYNC;
	else
		params.sync_type = HGSL_SYNC_TYPE_ISYNC;

	if (copy_to_user(USRPTR(arg), &params, sizeof(params))) {
		ret = -EFAULT;
		goto out;
	}

out:
	LOGD("%d", params.ctxthandle);
	if (ret) {
		if (ctxt_created)
			hgsl_ctxt_destroy(priv, hab_channel, params.ctxthandle, NULL);
		else if (ctxt && (params.ctxthandle < HGSL_CONTEXT_NUM)) {
			_unmap_shadow(ctxt);
			hgsl_hyp_put_shadowts_mem(hab_channel, &ctxt->shadow_ts_node);
			hgsl_hyp_ctxt_destroy(hab_channel, ctxt->devhandle, ctxt->context_id,
				NULL, ctxt->dbcq_export_id);
			hgsl_dbcq_close(ctxt);
			kfree(ctxt);
		}
		LOGE("failed to create context");
	}

	hgsl_hyp_channel_pool_put(hab_channel);
	return ret;
}

static int hgsl_ioctl_ctxt_destroy(struct file *filep, unsigned long arg)
{
	struct hgsl_priv *priv = filep->private_data;
	struct hgsl_ioctl_ctxt_destroy_params params;
	struct hgsl_hab_channel_t *hab_channel = NULL;
	int ret;

	ret = hgsl_hyp_channel_pool_get(&priv->hyp_priv, 0, &hab_channel);
	if (ret) {
		LOGE("Failed to get hab channel %d", ret);
		goto out;
	}

	if (copy_from_user(&params, USRPTR(arg), sizeof(params))) {
		pr_err_ratelimited("failed to copy params from user\n");
		ret = -EFAULT;
		goto out;
	}

	ret = hgsl_ctxt_destroy(priv, hab_channel, params.ctxthandle, &params.rval);

	if (ret == 0) {
		if (copy_to_user(USRPTR(arg), &params, sizeof(params)))
			ret = -EFAULT;
	}

out:
	hgsl_hyp_channel_pool_put(hab_channel);
	return ret;
}

static int hgsl_wait_timestamp(struct qcom_hgsl *hgsl,
	struct hgsl_wait_ts_info *param)
{
	struct hgsl_active_wait *wait = NULL;
	struct hgsl_context *ctxt = hgsl_get_context(hgsl, param->context_id);
	unsigned int timestamp;
	int ret;

	if (ctxt == NULL) {
		LOGE("Invalid context id %d\n", param->context_id);
		ret = -EINVAL;
		goto out;
	}

	if (!hgsl_ctxt_use_global_dbq(ctxt)) {
		ret = -EPERM;
		goto out;
	}

	timestamp = param->timestamp;

	wait = kzalloc(sizeof(*wait), GFP_KERNEL);
	if (!wait) {
		ret = -ENOMEM;
		goto out;
	}

	wait->ctxt = ctxt;
	wait->timestamp = timestamp;

	spin_lock(&hgsl->active_wait_lock);
	list_add_tail(&wait->head, &hgsl->active_wait_list);
	spin_unlock(&hgsl->active_wait_lock);

	ret = wait_event_interruptible_timeout(ctxt->wait_q,
				_timestamp_retired(ctxt, timestamp) ||
						ctxt->in_destroy,
				msecs_to_jiffies(param->timeout));
	if (ret == 0)
		ret = -ETIMEDOUT;
	else if (ret == -ERESTARTSYS)
		/* Let user handle this */
		ret = -EINTR;
	else
		ret = 0;

	spin_lock(&hgsl->active_wait_lock);
	list_del(&wait->head);
	spin_unlock(&hgsl->active_wait_lock);

out:
	hgsl_put_context(ctxt);
	kfree(wait);
	return ret;
}

static int hgsl_ioctl_hyp_generic_transaction(struct file *filep,
	unsigned long arg)
{
	struct hgsl_priv *priv = filep->private_data;
	struct hgsl_ioctl_hyp_generic_transaction_params params;
	void *pSend[HGSL_HYP_GENERAL_MAX_SEND_NUM];
	void *pReply[HGSL_HYP_GENERAL_MAX_REPLY_NUM];
	unsigned int i = 0;
	int ret = 0;
	int ret_value = 0;
	int *pRval = NULL;

	memset(pSend, 0, sizeof(pSend));
	memset(pReply, 0, sizeof(pReply));

	if (copy_from_user(&params, USRPTR(arg), sizeof(params))) {
		pr_err_ratelimited("failed to copy params from user\n");
		ret = -EFAULT;
		goto out;
	}

	if ((params.send_num > HGSL_HYP_GENERAL_MAX_SEND_NUM) ||
		(params.reply_num > HGSL_HYP_GENERAL_MAX_REPLY_NUM)) {
		ret = -EINVAL;
		LOGE("invalid Send %d or reply %d number\n",
			params.send_num, params.reply_num);
		goto out;
	}

	for (i = 0; i < params.send_num; i++) {
		if ((params.send_size[i] > HGSL_HYP_GENERAL_MAX_SIZE) ||
			(params.send_size[i] == 0)) {
			LOGE("Invalid size 0x%x for %d\n", params.send_size[i], i);
			ret = -EINVAL;
			goto out;
		} else {
			pSend[i] = hgsl_malloc(params.send_size[i]);
			if (pSend[i] == NULL) {
				ret = -ENOMEM;
				goto out;
			}
			if (copy_from_user(pSend[i],
				USRPTR(params.send_data[i]),
				params.send_size[i])) {
				pr_err_ratelimited("Failed to copy send data %d\n", i);
				ret = -EFAULT;
				goto out;
			}
		}
	}

	for (i = 0; i < params.reply_num; i++) {
		if ((params.reply_size[i] > HGSL_HYP_GENERAL_MAX_SIZE) ||
			(params.reply_size[i] == 0)) {
			ret = -EINVAL;
			goto out;
		} else {
			pReply[i] = hgsl_malloc(params.reply_size[i]);
			if (pReply[i] == NULL) {
				ret = -ENOMEM;
				goto out;
			}
			memset(pReply[i], 0, params.reply_size[i]);
		}
	}

	if (params.ret_value)
		pRval = &ret_value;

	ret = hgsl_hyp_generic_transaction(&priv->hyp_priv,
					&params, pSend, pReply, pRval);

	if (ret == 0) {
		for (i = 0; i < params.reply_num; i++) {
			if (copy_to_user(USRPTR(params.reply_data[i]),
			pReply[i], params.reply_size[i])) {
				ret = -EFAULT;
				goto out;
			}
		}
		if (params.ret_value) {
			if (copy_to_user(USRPTR(params.ret_value),
			&ret_value, sizeof(ret_value)))
				ret = -EFAULT;
		}
	}

out:
	for (i = 0; i < HGSL_HYP_GENERAL_MAX_SEND_NUM; i++)
		hgsl_free(pSend[i]);
	for (i = 0; i < HGSL_HYP_GENERAL_MAX_REPLY_NUM; i++)
		hgsl_free(pReply[i]);

	return ret;
}

static int hgsl_ioctl_mem_alloc(struct file *filep, unsigned long arg)
{
	struct hgsl_priv *priv = filep->private_data;
	struct hgsl_ioctl_mem_alloc_params params;
	struct qcom_hgsl *hgsl = priv->dev;
	int ret = 0;
	struct hgsl_mem_node *mem_node = NULL;
	struct hgsl_hab_channel_t *hab_channel = NULL;

	ret = hgsl_hyp_channel_pool_get(&priv->hyp_priv, 0, &hab_channel);
	if (ret) {
		LOGE("Failed to get hab channel %d", ret);
		goto out;
	}

	if (copy_from_user(&params, USRPTR(arg), sizeof(params))) {
		pr_err_ratelimited("failed to copy params from user");
		ret = -EFAULT;
		goto out;
	}

	if (params.sizebytes == 0) {
		LOGE("requested size is 0");
		ret = -EINVAL;
		goto out;
	}

	mem_node = hgsl_zalloc(sizeof(*mem_node));
	if (mem_node == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	mem_node->flags = params.flags;

	ret = hgsl_sharedmem_alloc(hgsl->dev, params.sizebytes, params.flags, mem_node);
	if (ret)
		goto out;

	ret = hgsl_hyp_mem_map_smmu(hab_channel, mem_node->memdesc.size, 0, mem_node);
	LOGD("%d, %d, gpuaddr 0x%llx",
		ret, mem_node->export_id, mem_node->memdesc.gpuaddr);
	if (ret)
		goto out;

	params.fd = dma_buf_fd(mem_node->dma_buf, O_CLOEXEC);

	if (params.fd < 0) {
		LOGE("dma_buf_fd failed, size 0x%x", mem_node->memdesc.size);
		ret = -EINVAL;
		goto out;
	}
	get_dma_buf(mem_node->dma_buf);

	if (copy_to_user(USRPTR(arg), &params, sizeof(params))) {
		ret = -EFAULT;
		goto out;
	}
	if (copy_to_user(USRPTR(params.memdesc),
		&mem_node->memdesc, sizeof(mem_node->memdesc))) {
		ret = -EFAULT;
		goto out;
	}
	mutex_lock(&priv->lock);
	list_add(&mem_node->node, &priv->mem_allocated);
	mutex_unlock(&priv->lock);
	hgsl_trace_gpu_mem_total(priv, mem_node->memdesc.size64);

out:
	if (ret && mem_node) {
		hgsl_hyp_mem_unmap_smmu(hab_channel, mem_node);
		hgsl_sharedmem_free(mem_node);
	}
	hgsl_hyp_channel_pool_put(hab_channel);
	return ret;
}

static int hgsl_ioctl_mem_free(struct file *filep, unsigned long arg)
{
	struct hgsl_priv *priv = filep->private_data;
	struct hgsl_ioctl_mem_free_params params;
	struct gsl_memdesc_t memdesc;
	int ret = 0;
	struct hgsl_mem_node *node_found = NULL;
	struct hgsl_mem_node *tmp = NULL;
	struct hgsl_hab_channel_t *hab_channel = NULL;

	ret = hgsl_hyp_channel_pool_get(&priv->hyp_priv, 0, &hab_channel);
	if (ret) {
		LOGE("Failed to get hab channel %d", ret);
		goto out;
	}

	if (copy_from_user(&params, USRPTR(arg), sizeof(params))) {
		pr_err_ratelimited("failed to copy params from user");
		ret = -EFAULT;
		goto out;
	}
	if (copy_from_user(&memdesc, USRPTR(params.memdesc),
		sizeof(memdesc))) {
		pr_err_ratelimited("failed to copy memdesc from user");
		ret = -EFAULT;
		goto out;
	}

	mutex_lock(&priv->lock);
	list_for_each_entry(tmp, &priv->mem_allocated, node) {
		if ((tmp->memdesc.gpuaddr == memdesc.gpuaddr)
			&& (tmp->memdesc.size == memdesc.size)) {
			node_found = tmp;
			list_del(&node_found->node);
			break;
		}
	}
	mutex_unlock(&priv->lock);

	if (node_found) {
		ret = hgsl_hyp_mem_unmap_smmu(hab_channel, node_found);
		if (!ret) {
			hgsl_trace_gpu_mem_total(priv,
					-(node_found->memdesc.size64));
			hgsl_sharedmem_free(node_found);
		} else {
			LOGE("hgsl_hyp_mem_unmap_smmu failed %d", ret);
			mutex_lock(&priv->lock);
			list_add(&node_found->node, &priv->mem_allocated);
			mutex_unlock(&priv->lock);
		}
	} else {
		LOGE("can't find the memory 0x%llx, 0x%x",
			memdesc.gpuaddr, memdesc.size);
		goto out;
	}

out:
	hgsl_hyp_channel_pool_put(hab_channel);
	return ret;
}

static int hgsl_ioctl_set_metainfo(struct file *filep, unsigned long arg)
{
	struct hgsl_priv *priv = filep->private_data;
	struct hgsl_ioctl_set_metainfo_params params;
	int ret = 0;
	struct hgsl_mem_node *mem_node = NULL;
	struct hgsl_mem_node *tmp = NULL;
	char metainfo[HGSL_MEM_META_MAX_SIZE];

	if (copy_from_user(&params, USRPTR(arg), sizeof(params))) {
		pr_err_ratelimited("failed to copy params from user");
		ret = -EFAULT;
		goto out;
	}

	if (params.metainfo_len > HGSL_MEM_META_MAX_SIZE) {
		LOGE("metainfo_len %d exceeded max", params.metainfo_len);
		ret = -EINVAL;
		goto out;
	}
	if (copy_from_user(metainfo, USRPTR(params.metainfo),
					params.metainfo_len)) {
		pr_err_ratelimited("failed to copy metainfo from user");
		ret = -EFAULT;
		goto out;
	}
	metainfo[HGSL_MEM_META_MAX_SIZE - 1] = '\0';

	mutex_lock(&priv->lock);
	list_for_each_entry(tmp, &priv->mem_allocated, node) {
		if (tmp->memdesc.priv64 == params.memdesc_priv) {
			mem_node = tmp;
			break;
		}
	}
	if (mem_node) {
		strscpy(mem_node->metainfo, metainfo,
			sizeof(mem_node->metainfo));
	}
	mutex_unlock(&priv->lock);

	if (!mem_node) {
		LOGE("Failed to find the requested memory");
		ret = -EINVAL;
		goto out;
	}

	ret = hgsl_hyp_set_metainfo(&priv->hyp_priv, &params, metainfo);

out:
	return ret;
}

static int hgsl_ioctl_mem_map_smmu(struct file *filep, unsigned long arg)
{
	struct hgsl_priv *priv = filep->private_data;
	struct hgsl_ioctl_mem_map_smmu_params params;
	int ret = 0;
	struct hgsl_mem_node *mem_node = NULL;
	struct hgsl_hab_channel_t *hab_channel = NULL;

	ret = hgsl_hyp_channel_pool_get(&priv->hyp_priv, 0, &hab_channel);
	if (ret) {
		LOGE("Failed to get hab channel %d", ret);
		goto out;
	}

	if (copy_from_user(&params, USRPTR(arg), sizeof(params))) {
		pr_err_ratelimited("failed to copy params from user");
		ret = -EFAULT;
		goto out;
	}

	mem_node = hgsl_zalloc(sizeof(*mem_node));
	if (mem_node == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	params.size = PAGE_ALIGN(params.size);
	mem_node->flags = params.flags;
	mem_node->fd = params.fd;
	mem_node->memtype = params.memtype;
	ret = hgsl_hyp_mem_map_smmu(hab_channel, params.size, params.offset, mem_node);

	if (ret == 0) {
		if (copy_to_user(USRPTR(arg), &params, sizeof(params))) {
			ret = -EFAULT;
			goto out;
		}
		if (copy_to_user(USRPTR(params.memdesc), &mem_node->memdesc,
			sizeof(mem_node->memdesc))) {
			ret = -EFAULT;
			goto out;
		}
		mutex_lock(&priv->lock);
		list_add(&mem_node->node, &priv->mem_mapped);
		mutex_unlock(&priv->lock);

		hgsl_trace_gpu_mem_total(priv, mem_node->memdesc.size64);
	}

out:
	if (ret) {
		hgsl_hyp_mem_unmap_smmu(hab_channel, mem_node);
		hgsl_free(mem_node);
	}
	hgsl_hyp_channel_pool_put(hab_channel);
	return ret;
}

static int hgsl_ioctl_mem_unmap_smmu(struct file *filep, unsigned long arg)
{
	struct hgsl_priv *priv = filep->private_data;
	struct hgsl_ioctl_mem_unmap_smmu_params params;
	int ret = 0;
	struct hgsl_mem_node *node_found = NULL;
	struct hgsl_mem_node *tmp = NULL;
	struct hgsl_hab_channel_t *hab_channel = NULL;

	ret = hgsl_hyp_channel_pool_get(&priv->hyp_priv, 0, &hab_channel);
	if (ret) {
		LOGE("Failed to get hab channel %d", ret);
		goto out;
	}

	if (copy_from_user(&params, USRPTR(arg), sizeof(params))) {
		pr_err_ratelimited("failed to copy params from user");
		ret = -EFAULT;
		goto out;
	}

	mutex_lock(&priv->lock);
	list_for_each_entry(tmp, &priv->mem_mapped, node) {
		if ((tmp->memdesc.gpuaddr == params.gpuaddr)
			&& (tmp->memdesc.size == params.size)) {
			node_found = tmp;
			list_del(&node_found->node);
			break;
		}
	}
	mutex_unlock(&priv->lock);

	if (node_found) {
		hgsl_put_sgt(node_found, false);
		ret = hgsl_hyp_mem_unmap_smmu(hab_channel, node_found);
		if (ret) {
			mutex_lock(&priv->lock);
			list_add(&node_found->node, &priv->mem_mapped);
			mutex_unlock(&priv->lock);
		} else {
			hgsl_trace_gpu_mem_total(priv,
					-(node_found->memdesc.size64));
			hgsl_free(node_found);
		}
	} else {
		ret = -EINVAL;
	}

out:
	hgsl_hyp_channel_pool_put(hab_channel);
	return ret;
}

static int hgsl_ioctl_mem_cache_operation(struct file *filep, unsigned long arg)
{
	struct hgsl_priv *priv = filep->private_data;
	struct hgsl_ioctl_mem_cache_operation_params params;
	struct qcom_hgsl *hgsl = priv->dev;
	struct hgsl_mem_node *node_found = NULL;
	int ret = 0;
	uint64_t gpuaddr = 0;
	bool internal = false;

	if (copy_from_user(&params, USRPTR(arg), sizeof(params))) {
		pr_err_ratelimited("failed to copy params from user");
		ret = -EFAULT;
		goto out;
	}

	gpuaddr = params.gpuaddr + params.offsetbytes;
	if ((gpuaddr < params.gpuaddr) || ((gpuaddr + params.sizebytes) <= gpuaddr)) {
		ret = -EINVAL;
		goto out;
	}

	mutex_lock(&priv->lock);
	node_found = hgsl_mem_find_base_locked(&priv->mem_allocated,
					gpuaddr, params.sizebytes);
	if (node_found)
		internal = true;
	else {
		node_found = hgsl_mem_find_base_locked(&priv->mem_mapped,
					gpuaddr, params.sizebytes);
		if (!node_found) {
			LOGE("failed to find node %d", ret);
			ret = -EINVAL;
			mutex_unlock(&priv->lock);
			goto out;
		}
	}

	ret = hgsl_mem_cache_op(hgsl->dev, node_found, internal,
			gpuaddr - node_found->memdesc.gpuaddr, params.sizebytes, params.operation);
	mutex_unlock(&priv->lock);

out:
	if (ret)
		LOGE("ret %d", ret);
	return ret;
}

static int hgsl_db_issueib_with_alloc_list(struct hgsl_priv *priv,
	struct hgsl_ioctl_issueib_with_alloc_list_params *param,
	struct gsl_command_buffer_object_t *ib,
	struct gsl_memory_object_t *allocations,
	struct gsl_memdesc_t *be_descs,
	uint64_t *be_offsets,
	uint32_t *timestamp)
{
	struct qcom_hgsl *hgsl = priv->dev;
	struct hgsl_context *ctxt = hgsl_get_context(hgsl, param->ctxthandle);
	int ret = 0;
	struct hgsl_fw_ib_desc *ib_descs = NULL;
	uint32_t gmu_flags = CMDBATCH_NOTIFY;
	uint32_t i;
	uint64_t user_profile_gpuaddr = 0;

	if (!hgsl_ctxt_use_dbq(ctxt)) {
		ret = -EPERM;
		goto out;
	}

	ib_descs = hgsl_malloc(sizeof(*ib_descs) * param->num_ibs);
	if (ib_descs == NULL) {
		LOGE("Out of memory");
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < param->num_ibs; i++) {
		ib_descs[i].addr = be_descs[i].gpuaddr + ib[i].offset + be_offsets[i];
		ib_descs[i].sz = ib[i].sizedwords << 2;
	}

	for (i = 0; i < param->num_allocations; i++) {
		if (allocations[i].flags & GSL_IBDESC_PROFILING_BUFFER) {
			user_profile_gpuaddr =
				be_descs[i + param->num_ibs].gpuaddr +
				allocations[i].offset +
				be_offsets[i + param->num_ibs];
				gmu_flags |= CMDBATCH_PROFILING;
			break;
		}
	}

	ret = hgsl_db_issue_cmd(priv, ctxt, param->num_ibs, gmu_flags,
			timestamp, ib_descs, user_profile_gpuaddr);
out:
	hgsl_put_context(ctxt);
	hgsl_free(ib_descs);
	return ret;
}

static int hgsl_db_issueib(struct hgsl_priv *priv,
	struct hgsl_ioctl_issueib_params *param,
	struct hgsl_ibdesc *ibs, uint32_t *timestamp)
{
	struct qcom_hgsl *hgsl = priv->dev;
	struct hgsl_context *ctxt = hgsl_get_context(hgsl, param->ctxthandle);
	int ret = 0;
	struct hgsl_fw_ib_desc *ib_descs = NULL;
	uint32_t gmu_flags = CMDBATCH_NOTIFY;
	uint32_t i;
	uint64_t user_profile_gpuaddr = 0;

	if (!hgsl_ctxt_use_dbq(ctxt)) {
		ret = -EPERM;
		goto out;
	}

	ib_descs = hgsl_malloc(sizeof(*ib_descs) * param->num_ibs);
	if (ib_descs == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < param->num_ibs; i++) {
		ib_descs[i].addr = ibs[i].gpuaddr;
		ib_descs[i].sz = ibs[i].sizedwords << 2;
	}

	ret = hgsl_db_issue_cmd(priv, ctxt, param->num_ibs, gmu_flags,
			timestamp, ib_descs, user_profile_gpuaddr);
out:
	hgsl_put_context(ctxt);
	hgsl_free(ib_descs);
	return ret;
}

static int hgsl_ioctl_issueib(struct file *filep, unsigned long arg)
{
	struct hgsl_priv *priv = filep->private_data;
	struct hgsl_ioctl_issueib_params params;
	int ret = 0;
	struct hgsl_ibdesc *ibs = NULL;
	size_t ib_size = 0;
	uint32_t ts = 0;
	bool remote_issueib = false;

	if (copy_from_user(&params, USRPTR(arg), sizeof(params))) {
		LOGE("failed to copy params from user");
		ret = -EFAULT;
		goto out;
	}

	if (params.num_ibs == 0) {
		ret = -EINVAL;
		goto out;
	}

	ret = hgsl_check_context_owner(priv, params.ctxthandle);
	if (ret) {
		LOGE("Invalid context id %d", params.ctxthandle);
		goto out;
	}

	if (params.channel_id > 0) {
		remote_issueib = true;
	} else {
		ib_size = params.num_ibs * sizeof(struct hgsl_ibdesc);
		ibs = hgsl_malloc(ib_size);
		if (ibs == NULL) {
			ret = -ENOMEM;
			goto out;
		}
		if (copy_from_user(ibs, USRPTR(params.ibs), ib_size)) {
			ret = -EFAULT;
			goto out;
		}

		ts = params.timestamp;
		ret = hgsl_db_issueib(priv, &params, ibs, &ts);
		if (!ret) {
			params.rval = GSL_SUCCESS;
			params.timestamp = ts;
		} else if (ret == -EPERM)
			remote_issueib = true;
	}
	if (remote_issueib)
		ret = hgsl_hyp_issueib(&priv->hyp_priv, &params, ibs);

	if (copy_to_user(USRPTR(arg), &params, sizeof(params))) {
		LOGE("failed to copy param to user");
		ret = -EFAULT;
		goto out;
	}

out:
	hgsl_free(ibs);
	return ret;
}

static int hgsl_ioctl_issueib_with_alloc_list(struct file *filep,
	unsigned long arg)
{
	struct hgsl_priv *priv = filep->private_data;
	struct hgsl_ioctl_issueib_with_alloc_list_params params;
	int ret = 0;
	struct gsl_command_buffer_object_t *ibs = NULL;
	struct gsl_memory_object_t *allocations = NULL;
	size_t ib_size = 0;
	size_t allocation_size = 0;
	size_t be_data_size = 0;
	struct gsl_memdesc_t *be_descs = NULL;
	uint64_t *be_offsets = NULL;
	uint32_t ts = 0;
	bool remote_issueib = false;

	if (copy_from_user(&params, USRPTR(arg), sizeof(params))) {
		pr_err_ratelimited("failed to copy params from user");
		ret = -EFAULT;
		goto out;
	}

	if (params.num_ibs == 0) {
		ret = -EINVAL;
		goto out;
	}

	ret = hgsl_check_context_owner(priv, params.ctxthandle);
	if (ret) {
		LOGE("Invalid context id %d", params.ctxthandle);
		goto out;
	}

	if (params.channel_id > 0) {
		remote_issueib = true;
	} else {
		ib_size = params.num_ibs * sizeof(struct gsl_command_buffer_object_t);
		ibs = hgsl_malloc(ib_size);
		if (ibs == NULL) {
			ret = -ENOMEM;
			goto out;
		}
		if (copy_from_user(ibs, USRPTR(params.ibs), ib_size)) {
			ret = -EFAULT;
			goto out;
		}

		if (params.num_allocations != 0) {
			allocation_size = params.num_allocations *
				sizeof(struct gsl_memory_object_t);
			allocations = hgsl_malloc(allocation_size);
			if (allocations == NULL) {
				ret = -ENOMEM;
				goto out;
			}
			if (copy_from_user(allocations, USRPTR(params.allocations),
				allocation_size)) {
				ret = -EFAULT;
				goto out;
			}
		}

		be_data_size = (params.num_ibs + params.num_allocations) *
			(sizeof(struct gsl_memdesc_t) + sizeof(uint64_t));
		be_descs = (struct gsl_memdesc_t *)hgsl_malloc(be_data_size);
		if (be_descs == NULL) {
			ret = -ENOMEM;
			goto out;
		}
		be_offsets = (uint64_t *)&be_descs[params.num_ibs +
							params.num_allocations];
		if (copy_from_user(be_descs, USRPTR(params.be_data), be_data_size)) {
			ret = -EFAULT;
			goto out;
		}

		ts = params.timestamp;
		ret = hgsl_db_issueib_with_alloc_list(priv, &params, ibs,
					allocations, be_descs, be_offsets, &ts);
		if (!ret) {
			params.rval = GSL_SUCCESS;
			params.timestamp = ts;
		} else if (ret == -EPERM)
			remote_issueib = true;
	}
	if (remote_issueib)
		ret = hgsl_hyp_issueib_with_alloc_list(&priv->hyp_priv,
			&params, ibs, allocations, be_descs, be_offsets);

	if (copy_to_user(USRPTR(arg), &params, sizeof(params))) {
		LOGE("failed to copy param to user");
		ret = -EFAULT;
		goto out;
	}

out:
	hgsl_free(ibs);
	hgsl_free(allocations);
	hgsl_free(be_descs);
	return ret;
}

static int hgsl_ioctl_wait_timestamp(struct file *filep, unsigned long arg)
{
	struct hgsl_priv *priv = filep->private_data;
	struct qcom_hgsl *hgsl = priv->dev;
	struct hgsl_wait_ts_info param;
	int ret;
	bool expired;
	bool remote_wait = false;
	struct hgsl_context *ctxt;

	if (copy_from_user(&param, USRPTR(arg), sizeof(param))) {
		LOGE("failed to copy param from user");
		return -EFAULT;
	}

	ctxt = hgsl_get_context_owner(priv, param.context_id);
	if (!ctxt) {
		LOGE("Invalid context id %d", param.context_id);
		return -EINVAL;
	}

	if (param.channel_id) {
		remote_wait = true;
	} else {
		ret = hgsl_check_shadow_timestamp(ctxt,
					GSL_TIMESTAMP_RETIRED, param.timestamp,
					&expired);

		if (ret)
			remote_wait = true;
		else if (!expired) {
			ret = hgsl_wait_timestamp(hgsl, &param);
			if (ret == -EPERM)
				remote_wait = true;
		}
	}
	hgsl_put_context(ctxt);

	if (remote_wait) {
		/* dbq or shadow timestamp is not enabled */
		ret = hgsl_hyp_wait_timestamp(&priv->hyp_priv, &param);
		if (ret == -EINTR) {
			if (copy_to_user(USRPTR(arg), &param, sizeof(param))) {
				LOGE("failed to copy param to user");
				return -EFAULT;
			}
		}
	}

	return ret;
}

static int hgsl_ioctl_read_timestamp(struct file *filep, unsigned long arg)
{
	struct hgsl_priv *priv = filep->private_data;
	struct hgsl_ioctl_read_ts_params param;
	int ret;
	struct hgsl_context *ctxt;

	if (copy_from_user(&param, USRPTR(arg), sizeof(param))) {
		LOGE("failed to copy param from user");
		return -EFAULT;
	}

	ctxt = hgsl_get_context_owner(priv, param.ctxthandle);
	if (!ctxt) {
		LOGE("Invalid context id %d", param.ctxthandle);
		return -EINVAL;
	}

	ret = hgsl_read_shadow_timestamp(ctxt,
					param.type, &param.timestamp);

	hgsl_put_context(ctxt);
	if (ret)
		ret = hgsl_hyp_read_timestamp(&priv->hyp_priv, &param);

	if (ret == 0) {
		if (copy_to_user(USRPTR(arg), &param, sizeof(param))) {
			LOGE("failed to copy param to user");
			return -EFAULT;
		}
	}

	return ret;
}

static int hgsl_ioctl_check_timestamp(struct file *filep, unsigned long arg)
{
	struct hgsl_priv *priv = filep->private_data;
	struct hgsl_ioctl_check_ts_params param;
	int ret;
	bool expired;
	struct hgsl_context *ctxt;

	if (copy_from_user(&param, USRPTR(arg), sizeof(param))) {
		LOGE("failed to copy param from user");
		return -EFAULT;
	}

	ctxt = hgsl_get_context_owner(priv, param.ctxthandle);
	if (!ctxt) {
		LOGE("Invalid context id %d", param.ctxthandle);
		return -EINVAL;
	}

	ret = hgsl_check_shadow_timestamp(ctxt, param.type,
						param.timestamp, &expired);

	if (ret)
		param.rval = -1;
	else
		param.rval = expired ? 1 : 0;

	hgsl_put_context(ctxt);

	if (ret)
		ret = hgsl_hyp_check_timestamp(&priv->hyp_priv, &param);

	if (ret == 0) {
		if (copy_to_user(USRPTR(arg), &param, sizeof(param))) {
			LOGE("failed to copy param to user");
			return -EFAULT;
		}
	}

	return ret;
}

static int hgsl_ioctl_get_system_time(struct file *filep, unsigned long arg)
{
	struct hgsl_priv *priv = filep->private_data;
	uint64_t param;
	int ret = 0;

	if (copy_from_user(&param, USRPTR(arg), sizeof(param))) {
		LOGE("failed to copy param from user");
		return -EFAULT;
	}

	ret = hgsl_hyp_get_system_time(&priv->hyp_priv, &param);
	if (!ret) {
		if (copy_to_user(USRPTR(arg), &param, sizeof(param))) {
			LOGE("failed to copy param to user");
			return -EFAULT;
		}
	}

	return ret;
}

static int hgsl_ioctl_syncobj_wait_multiple(struct file *filep,
	unsigned long arg)
{
	struct hgsl_priv *priv = filep->private_data;
	struct hgsl_ioctl_syncobj_wait_multiple_params param;
	int ret = 0;
	uint64_t *rpc_syncobj = NULL;
	int32_t *status = NULL;
	size_t rpc_syncobj_size = 0;
	size_t status_size = 0;

	if (copy_from_user(&param, USRPTR(arg), sizeof(param))) {
		pr_err_ratelimited("failed to copy param from user");
		ret = -EFAULT;
		goto out;
	}

	if ((param.num_syncobjs == 0) ||
	(param.num_syncobjs > (SIZE_MAX / sizeof(uint64_t))) ||
	(param.num_syncobjs > (SIZE_MAX / sizeof(int32_t)))) {
		LOGE("invalid num_syncobjs %zu", param.num_syncobjs);
		return -EINVAL;
		goto out;
	}

	rpc_syncobj_size = sizeof(uint64_t) * param.num_syncobjs;
	rpc_syncobj = (uint64_t *)hgsl_malloc(rpc_syncobj_size);
	if (!rpc_syncobj) {
		ret = -ENOMEM;
		goto out;
	}
	if (copy_from_user(rpc_syncobj, USRPTR(param.rpc_syncobj),
		rpc_syncobj_size)) {
		pr_err_ratelimited("failed to copy param from user");
		ret = -EFAULT;
		goto out;
	}

	status_size = sizeof(int32_t) * param.num_syncobjs;
	status = (int32_t *)hgsl_malloc(status_size);
	if (status == NULL) {
		LOGE("failed to allocate memory");
		ret = -ENOMEM;
		goto out;
	}
	memset(status, 0, status_size);

	ret = hgsl_hyp_syncobj_wait_multiple(&priv->hyp_priv, rpc_syncobj,
		param.num_syncobjs, param.timeout_ms, status, &param.result);

	if (ret == 0) {
		if (copy_to_user(USRPTR(arg), &param, sizeof(param))) {
			ret = -EFAULT;
			goto out;
		}
		if (copy_to_user(USRPTR(param.status), status, status_size)) {
			ret = -EFAULT;
			goto out;
		}
	}

out:
	hgsl_free(rpc_syncobj);
	hgsl_free(status);
	return ret;
}

static int hgsl_ioctl_perfcounter_select(struct file *filep, unsigned long arg)
{
	struct hgsl_priv *priv = filep->private_data;
	struct hgsl_ioctl_perfcounter_select_params param;
	int ret = 0;
	uint32_t *groups = NULL;
	uint32_t *counter_ids = NULL;
	uint32_t *counter_val_regs = NULL;
	uint32_t *counter_val_hi_regs = NULL;

	if (copy_from_user(&param, USRPTR(arg), sizeof(param))) {
		pr_err_ratelimited("failed to copy param from user");
		ret = -EFAULT;
		goto out;
	}

	if ((param.num_counters <= 0) ||
		(param.num_counters > (SIZE_MAX / (sizeof(int32_t) * 4)))) {
		LOGE("invalid num_counters %zu", param.num_counters);
		return -EINVAL;
		goto out;
	}

	groups = (uint32_t *)hgsl_malloc(
		sizeof(int32_t) * 4 * param.num_counters);
	if (!groups) {
		ret = -ENOMEM;
		goto out;
	}

	counter_ids = groups + param.num_counters;
	counter_val_regs = counter_ids + param.num_counters;
	counter_val_hi_regs = counter_val_regs + param.num_counters;

	if (copy_from_user(groups, USRPTR(param.groups),
		sizeof(uint32_t) * param.num_counters)) {
		pr_err_ratelimited("failed to copy groups from user");
		ret = -EFAULT;
		goto out;
	}
	if (copy_from_user(counter_ids, USRPTR(param.counter_ids),
		sizeof(uint32_t) * param.num_counters)) {
		pr_err_ratelimited("failed to copy counter_ids from user");
		ret = -EFAULT;
		goto out;
	}

	ret = hgsl_hyp_perfcounter_select(&priv->hyp_priv, &param, groups,
		counter_ids, counter_val_regs, counter_val_hi_regs);

	if (!ret) {
		if (copy_to_user(USRPTR(arg), &param, sizeof(param))) {
			ret = -EFAULT;
			goto out;
		}
		if (copy_to_user(USRPTR(param.counter_val_regs),
			counter_val_regs,
			sizeof(uint32_t) * param.num_counters)) {
			ret = -EFAULT;
			goto out;
		}
		if (param.counter_val_hi_regs) {
			if (copy_to_user(USRPTR(param.counter_val_hi_regs),
				counter_val_hi_regs,
				sizeof(uint32_t) * param.num_counters)) {
				ret = -EFAULT;
				goto out;
			}
		}
	}

out:
	hgsl_free(groups);
	return ret;
}

static int hgsl_ioctl_perfcounter_deselect(struct file *filep,
	unsigned long arg)
{
	struct hgsl_priv *priv = filep->private_data;
	struct hgsl_ioctl_perfcounter_deselect_params param;
	int ret = 0;
	uint32_t *groups = NULL;
	uint32_t *counter_ids = NULL;

	if (copy_from_user(&param, USRPTR(arg), sizeof(param))) {
		pr_err_ratelimited("failed to copy param from user");
		ret = -EFAULT;
		goto out;
	}

	if ((param.num_counters <= 0) ||
		(param.num_counters > (SIZE_MAX / (sizeof(int32_t) * 2)))) {
		LOGE("invalid num_counters %zu", param.num_counters);
		return -EINVAL;
		goto out;
	}

	groups = (uint32_t *)hgsl_malloc(
		sizeof(int32_t) * 2 * param.num_counters);
	if (!groups) {
		ret = -ENOMEM;
		goto out;
	}

	counter_ids = groups + param.num_counters;

	if (copy_from_user(groups, USRPTR(param.groups),
				sizeof(uint32_t) * param.num_counters)) {
		pr_err_ratelimited("failed to copy groups from user");
		ret = -EFAULT;
		goto out;
	}
	if (copy_from_user(counter_ids, USRPTR(param.counter_ids),
				sizeof(uint32_t) * param.num_counters)) {
		pr_err_ratelimited("failed to copy counter_ids from user");
		ret = -EFAULT;
		goto out;
	}
	ret = hgsl_hyp_perfcounter_deselect(&priv->hyp_priv,
		&param, groups, counter_ids);

out:
	hgsl_free(groups);
	return ret;
}

static int hgsl_ioctl_perfcounter_query_selection(struct file *filep,
	unsigned long arg)
{
	struct hgsl_priv *priv = filep->private_data;
	struct hgsl_ioctl_perfcounter_query_selections_params param;
	int ret = 0;
	int32_t *selections = NULL;

	if (copy_from_user(&param, USRPTR(arg), sizeof(param))) {
		pr_err_ratelimited("failed to copy param from user");
		ret = -EFAULT;
		goto out;
	}

	if ((param.num_counters <= 0) ||
		(param.num_counters > (SIZE_MAX / sizeof(int32_t)))) {
		LOGE("invalid num_counters %zu", param.num_counters);
		return -EINVAL;
		goto out;
	}

	selections = (int32_t *)hgsl_malloc(
		sizeof(int32_t) * param.num_counters);
	if (!selections) {
		ret = -ENOMEM;
		goto out;
	}
	memset(selections, 0, sizeof(int32_t)  * param.num_counters);

	ret = hgsl_hyp_perfcounter_query_selections(&priv->hyp_priv,
							&param, selections);

	if (ret)
		goto out;

	if (copy_to_user(USRPTR(arg), &param, sizeof(param))) {
		ret = -EFAULT;
		goto out;
	}
	if (param.selections != 0) {
		if (copy_to_user(USRPTR(param.selections), selections,
			sizeof(int32_t) * param.num_counters)) {
			ret = -EFAULT;
			goto out;
		}
	}

out:
	hgsl_free(selections);
	return ret;
}

static int hgsl_ioctl_perfcounter_read(struct file *filep, unsigned long arg)
{
	struct hgsl_priv *priv = filep->private_data;
	struct hgsl_ioctl_perfcounter_read_params param;
	int ret = 0;

	if (copy_from_user(&param, USRPTR(arg), sizeof(param))) {
		pr_err_ratelimited("failed to copy param from user");
		ret = -EFAULT;
		goto out;
	}

	ret = hgsl_hyp_perfcounter_read(&priv->hyp_priv, &param);

	if (ret == 0) {
		if (copy_to_user(USRPTR(arg), &param, sizeof(param))) {
			ret = -EFAULT;
			goto out;
		}
	}

out:
	return ret;
}

static int hgsl_open(struct inode *inodep, struct file *filep)
{
	struct hgsl_priv *priv = hgsl_zalloc(sizeof(*priv));
	struct qcom_hgsl  *hgsl = container_of(inodep->i_cdev,
					       struct qcom_hgsl, cdev);
	struct pid *pid = task_tgid(current);
	struct task_struct *task = pid_task(pid, PIDTYPE_PID);
	int ret = 0;

	if (!priv)
		return -ENOMEM;

	if (!task) {
		ret = -EINVAL;
		goto out;
	}

	INIT_LIST_HEAD(&priv->mem_mapped);
	INIT_LIST_HEAD(&priv->mem_allocated);
	mutex_init(&priv->lock);
	priv->pid = task_pid_nr(task);

	ret = hgsl_hyp_init(&priv->hyp_priv, hgsl->dev,
		priv->pid, task->comm);
	if (ret != 0)
		goto out;

	priv->dev = hgsl;
	filep->private_data = priv;

out:
	if (ret != 0)
		kfree(priv);
	return ret;
}

static int hgsl_cleanup(struct hgsl_priv *priv)
{
	struct hgsl_mem_node *node_found = NULL;
	struct hgsl_mem_node *tmp = NULL;
	int ret;
	bool need_notify = (!list_empty(&priv->mem_mapped) || !list_empty(&priv->mem_allocated));
	struct hgsl_hab_channel_t *hab_channel = NULL;

	if (need_notify) {
		ret = hgsl_hyp_channel_pool_get(&priv->hyp_priv, 0, &hab_channel);
		if (ret)
			LOGE("Failed to get channel %d", ret);

		ret = hgsl_hyp_notify_cleanup(hab_channel, HGSL_CLEANUP_WAIT_SLICE_IN_MS);
		if (ret == -ETIMEDOUT) {
			hgsl_hyp_channel_pool_put(hab_channel);
			return ret;
		}
	}

	mutex_lock(&priv->lock);
	if ((hab_channel == NULL) &&
	(!list_empty(&priv->mem_mapped) || !list_empty(&priv->mem_allocated))) {
		ret = hgsl_hyp_channel_pool_get(&priv->hyp_priv, 0, &hab_channel);
		if (ret)
			LOGE("Failed to get channel %d", ret);
	}

	list_for_each_entry_safe(node_found, tmp, &priv->mem_mapped, node) {
		hgsl_put_sgt(node_found, false);
		ret = hgsl_hyp_mem_unmap_smmu(hab_channel, node_found);
		if (ret)
			LOGE("Failed to clean mapped buffer %u, 0x%llx, ret %d",
				node_found->export_id,
				node_found->memdesc.gpuaddr, ret);
		else
			hgsl_trace_gpu_mem_total(priv,
				-(node_found->memdesc.size64));
		list_del(&node_found->node);
		hgsl_free(node_found);
	}
	list_for_each_entry_safe(node_found, tmp, &priv->mem_allocated, node) {
		ret = hgsl_hyp_mem_unmap_smmu(hab_channel, node_found);
		if (ret)
			LOGE("Failed to clean mapped buffer %u, 0x%llx, ret %d",
			node_found->export_id, node_found->memdesc.gpuaddr, ret);
		list_del(&node_found->node);
		hgsl_trace_gpu_mem_total(priv, -(node_found->memdesc.size64));
		hgsl_sharedmem_free(node_found);
	}
	mutex_unlock(&priv->lock);

	hgsl_hyp_channel_pool_put(hab_channel);
	return 0;
}

static int _hgsl_release(struct hgsl_priv *priv)
{
	struct qcom_hgsl *hgsl = priv->dev;
	uint32_t i;
	int ret;

	read_lock(&hgsl->ctxt_lock);
	for (i = 0; i < HGSL_CONTEXT_NUM; i++) {
		if ((hgsl->contexts != NULL) &&
			(hgsl->contexts[i] != NULL) &&
			(priv == hgsl->contexts[i]->priv)) {
			read_unlock(&hgsl->ctxt_lock);
			hgsl_ctxt_destroy(priv, NULL, i, NULL);
			read_lock(&hgsl->ctxt_lock);
		}
	}
	read_unlock(&hgsl->ctxt_lock);

	hgsl_isync_fini(priv);
	ret = hgsl_cleanup(priv);
	if (ret)
		return ret;

	hgsl_hyp_close(&priv->hyp_priv);

	hgsl_free(priv);

	return 0;
}

static void hgsl_release_worker(struct work_struct *work)
{
	struct qcom_hgsl *hgsl =
		container_of(work, struct qcom_hgsl, release_work);
	struct hgsl_priv *priv = NULL;
	int ret;

	while (true) {
		mutex_lock(&hgsl->mutex);
		if (!list_empty(&hgsl->release_list)) {
			priv = container_of(hgsl->release_list.next,
				struct hgsl_priv, node);
			list_del(&priv->node);
		} else {
			priv = NULL;
		}
		mutex_unlock(&hgsl->mutex);

		if (!priv)
			break;

		ret = _hgsl_release(priv);
		if (ret == -ETIMEDOUT) {
			mutex_lock(&hgsl->mutex);
			list_add_tail(&priv->node, &hgsl->release_list);
			mutex_unlock(&hgsl->mutex);
		}
	}
}

static int hgsl_init_release_wq(struct qcom_hgsl *hgsl)
{
	int ret = 0;

	hgsl->release_wq = create_workqueue("hgsl-release-wq");
	if (IS_ERR_OR_NULL(hgsl->release_wq)) {
		dev_err(hgsl->dev, "failed to create workqueue\n");
		ret = PTR_ERR(hgsl->release_wq);
		goto out;
	}
	INIT_WORK(&hgsl->release_work, hgsl_release_worker);

	INIT_LIST_HEAD(&hgsl->release_list);
	mutex_init(&hgsl->mutex);

out:
	return ret;
}

static int hgsl_release(struct inode *inodep, struct file *filep)
{
	struct hgsl_priv *priv = filep->private_data;
	struct qcom_hgsl *hgsl = priv->dev;

	mutex_lock(&hgsl->mutex);
	list_add(&priv->node, &hgsl->release_list);
	mutex_unlock(&hgsl->mutex);

	queue_work(hgsl->release_wq, &hgsl->release_work);
	return 0;
}

static ssize_t hgsl_read(struct file *filep, char __user *buf, size_t count,
		loff_t *pos)
{
	struct hgsl_priv *priv = filep->private_data;
	struct qcom_hgsl *hgsl = priv->dev;
	struct platform_device *pdev = to_platform_device(hgsl->dev);
	uint32_t version = 0;
	uint32_t release = 0;
	char buff[100];
	int ret = 0;

	if (!hgsl->db_off) {
		if (hgsl->reg_ver.vaddr == NULL) {
			ret = hgsl_reg_map(pdev, IORESOURCE_HWINF, &hgsl->reg_ver);
			if (ret < 0) {
				dev_err(hgsl->dev, "Unable to map resource:%s\n",
						IORESOURCE_HWINF);
			}
		}

		if (hgsl->reg_ver.vaddr != NULL) {
			hgsl_reg_read(&hgsl->reg_ver, 0, &version);
			hgsl_reg_read(&hgsl->reg_ver, 4, &release);
			snprintf(buff, 100, "gpu HW Version:%x HW Release:%x\n",
								version, release);
		} else {
			snprintf(buff, 100, "Unable to read HW version\n");
		}
	} else {
		snprintf(buff, 100, "Doorbell closed\n");
	}

	return simple_read_from_buffer(buf, count, pos,
			buff, strlen(buff) + 1);
}

static int hgsl_ioctl_hsync_fence_create(struct file *filep,
					   unsigned long arg)
{
	struct hgsl_priv *priv = filep->private_data;
	struct qcom_hgsl *hgsl = priv->dev;
	struct hgsl_hsync_fence_create param;
	struct hgsl_context *ctxt = NULL;
	int ret = 0;

	if (hgsl->db_off) {
		dev_err(hgsl->dev, "Doorbell not open\n");
		return -EPERM;
	}

	if (copy_from_user(&param, USRPTR(arg), sizeof(param))) {
		pr_err_ratelimited("failed to copy param from user");
		ret = -EFAULT;
		goto out;
	}

	ctxt = hgsl_get_context_owner(priv, param.context_id);
	if ((ctxt == NULL) || (ctxt->timeline  == NULL)) {
		ret = -EINVAL;
		goto out;
	}

	param.fence_fd = hgsl_hsync_fence_create_fd(ctxt, param.timestamp);
	if (param.fence_fd < 0) {
		ret = param.fence_fd;
		goto out;
	}
	(void)copy_to_user(USRPTR(arg), &param, sizeof(param));
out:
	hgsl_put_context(ctxt);

	return ret;
}

static int hgsl_ioctl_isync_timeline_create(struct file *filep,
					unsigned long arg)
{
	struct hgsl_priv *priv = filep->private_data;
	uint32_t param = 0;
	int ret = 0;

	ret = hgsl_isync_timeline_create(priv, &param, HGSL_ISYNC_32BITS_TIMELINE, 0);
	if (ret == 0)
		(void)copy_to_user(USRPTR(arg), &param, sizeof(param));

	return ret;
}

static int hgsl_ioctl_isync_timeline_destroy(struct file *filep,
					unsigned long arg)
{
	struct hgsl_priv *priv = filep->private_data;
	uint32_t param = 0;
	int ret = 0;

	if (copy_from_user(&param, USRPTR(arg), sizeof(param))) {
		pr_err_ratelimited("failed to copy param from user");
		ret = -EFAULT;
		goto out;
	}
	ret = hgsl_isync_timeline_destroy(priv, param);

out:
	return ret;
}

static int hgsl_ioctl_isync_fence_create(struct file *filep,
					unsigned long arg)
{
	struct hgsl_priv *priv = filep->private_data;
	struct hgsl_isync_create_fence param;
	int ret = 0;
	int fence = 0;
	bool ts_is_valid;

	if (copy_from_user(&param, USRPTR(arg), sizeof(param))) {
		pr_err_ratelimited("failed to copy param from user");
		ret = -EFAULT;
		goto out;
	}
	ts_is_valid = (param.padding == HGSL_ISYNC_FENCE_CREATE_USE_TS);

	ret = hgsl_isync_fence_create(priv, param.timeline_id, param.ts,
						ts_is_valid, &fence);

	if (!ret) {
		param.fence_id = fence;
		(void)copy_to_user(USRPTR(arg), &param, sizeof(param));
	}

out:
	return ret;
}

static int hgsl_ioctl_isync_fence_signal(struct file *filep,
					unsigned long arg)
{
	struct hgsl_priv *priv = filep->private_data;
	struct hgsl_isync_signal_fence param;
	int ret = 0;

	if (copy_from_user(&param, USRPTR(arg), sizeof(param))) {
		pr_err_ratelimited("failed to copy param from user");
		ret = -EFAULT;
		goto out;
	}

	ret = hgsl_isync_fence_signal(priv, param.timeline_id,
						  param.fence_id);

out:
	return ret;
}

static int hgsl_ioctl_isync_forward(struct file *filep,
					unsigned long arg)
{
	struct hgsl_priv *priv = filep->private_data;
	struct hgsl_isync_forward param;
	int ret = 0;

	if (copy_from_user(&param, USRPTR(arg), sizeof(param))) {
		pr_err_ratelimited("failed to copy param from user");
		ret = -EFAULT;
		goto out;
	}
	ret = hgsl_isync_forward(priv, param.timeline_id,
						  (uint64_t)param.ts, true);

out:
	return ret;
}

static int hgsl_ioctl_timeline_create(struct file *filep,
					unsigned long arg)
{
	struct hgsl_priv *priv = filep->private_data;
	struct hgsl_timeline_create param;
	int ret = 0;

	if (copy_from_user(&param, USRPTR(arg), sizeof(param)))
		return -EFAULT;

	ret = hgsl_isync_timeline_create(priv, &param.timeline_id,
					HGSL_ISYNC_64BITS_TIMELINE, param.initial_ts);
	if (ret == 0)
		(void)copy_to_user(USRPTR(arg), &param, sizeof(param));

	return ret;
}

static int hgsl_ioctl_timeline_signal(struct file *filep,
					unsigned long arg)
{
	struct hgsl_priv *priv = filep->private_data;
	struct hgsl_timeline_signal param;
	int ret = 0;
	uint64_t timelines;
	uint32_t i;

	if (copy_from_user(&param, USRPTR(arg), sizeof(param)))
		return -EFAULT;

	if (!param.timelines_size)
		param.timelines_size = sizeof(struct hgsl_timeline_val);

	timelines = param.timelines;

	for (i = 0; i < param.count; i++) {
		struct hgsl_timeline_val val;

		if (copy_struct_from_user(&val, sizeof(val),
			USRPTR(timelines), param.timelines_size))
			return -EFAULT;

		if (val.padding)
			return -EINVAL;

		ret = hgsl_isync_forward(priv, val.timeline_id, val.timepoint, false);
		if (ret)
			return ret;

		timelines += param.timelines_size;
	}

	return ret;
}

static int hgsl_ioctl_timeline_query(struct file *filep,
					unsigned long arg)
{
	struct hgsl_priv *priv = filep->private_data;
	struct hgsl_timeline_query param;
	int ret = 0;
	uint64_t timelines;
	uint32_t i;

	if (copy_from_user(&param, USRPTR(arg), sizeof(param)))
		return -EFAULT;

	if (!param.timelines_size)
		param.timelines_size = sizeof(struct hgsl_timeline_val);

	timelines = param.timelines;

	for (i = 0; i < param.count; i++) {
		struct hgsl_timeline_val val;

		if (copy_struct_from_user(&val, sizeof(val),
			USRPTR(timelines), param.timelines_size))
			return -EFAULT;

		if (val.padding)
			return -EINVAL;

		ret = hgsl_isync_query(priv, val.timeline_id, &val.timepoint);
		if (ret)
			return ret;

		(void)copy_to_user(USRPTR(timelines), &val, sizeof(val));

		timelines += param.timelines_size;
	}

	return ret;
}

static int hgsl_ioctl_timeline_wait(struct file *filep,
					unsigned long arg)
{
	struct hgsl_priv *priv = filep->private_data;
	struct hgsl_timeline_wait param;
	int ret = 0;

	if (copy_from_user(&param, USRPTR(arg), sizeof(param)))
		return -EFAULT;

	if (!param.timelines_size)
		param.timelines_size = sizeof(struct hgsl_timeline_val);

	ret = hgsl_isync_wait_multiple(priv, &param);

	return ret;
}

static long hgsl_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
	int ret;

	switch (cmd) {
	case HGSL_IOCTL_ISSUE_IB:
		ret = hgsl_ioctl_issueib(filep, arg);
		break;
	case HGSL_IOCTL_CTXT_CREATE:
		ret = hgsl_ioctl_ctxt_create(filep, arg);
		break;
	case HGSL_IOCTL_CTXT_DESTROY:
		ret = hgsl_ioctl_ctxt_destroy(filep, arg);
		break;
	case HGSL_IOCTL_WAIT_TIMESTAMP:
		ret = hgsl_ioctl_wait_timestamp(filep, arg);
		break;
	case HGSL_IOCTL_READ_TIMESTAMP:
		ret = hgsl_ioctl_read_timestamp(filep, arg);
		break;
	case HGSL_IOCTL_CHECK_TIMESTAMP:
		ret = hgsl_ioctl_check_timestamp(filep, arg);
		break;
	case HGSL_IOCTL_HYP_GENERIC_TRANSACTION:
		ret = hgsl_ioctl_hyp_generic_transaction(filep, arg);
		break;
	case HGSL_IOCTL_GET_SHADOWTS_MEM:
		ret = hgsl_ioctl_get_shadowts_mem(filep, arg);
		break;
	case HGSL_IOCTL_PUT_SHADOWTS_MEM:
		ret = hgsl_ioctl_put_shadowts_mem(filep, arg);
		break;
	case HGSL_IOCTL_MEM_ALLOC:
		ret = hgsl_ioctl_mem_alloc(filep, arg);
		break;
	case HGSL_IOCTL_MEM_FREE:
		ret = hgsl_ioctl_mem_free(filep, arg);
		break;
	case HGSL_IOCTL_MEM_MAP_SMMU:
		ret = hgsl_ioctl_mem_map_smmu(filep, arg);
		break;
	case HGSL_IOCTL_MEM_UNMAP_SMMU:
		ret = hgsl_ioctl_mem_unmap_smmu(filep, arg);
		break;
	case HGSL_IOCTL_MEM_CACHE_OPERATION:
		ret = hgsl_ioctl_mem_cache_operation(filep, arg);
		break;
	case HGSL_IOCTL_ISSUIB_WITH_ALLOC_LIST:
		ret = hgsl_ioctl_issueib_with_alloc_list(filep, arg);
		break;
	case HGSL_IOCTL_GET_SYSTEM_TIME:
		ret = hgsl_ioctl_get_system_time(filep, arg);
		break;
	case HGSL_IOCTL_SYNCOBJ_WAIT_MULTIPLE:
		ret = hgsl_ioctl_syncobj_wait_multiple(filep, arg);
		break;
	case HGSL_IOCTL_PERFCOUNTER_SELECT:
		ret = hgsl_ioctl_perfcounter_select(filep, arg);
		break;
	case HGSL_IOCTL_PERFCOUNTER_DESELECT:
		ret = hgsl_ioctl_perfcounter_deselect(filep, arg);
		break;
	case HGSL_IOCTL_PERFCOUNTER_QUERY_SELECTION:
		ret = hgsl_ioctl_perfcounter_query_selection(filep, arg);
		break;
	case HGSL_IOCTL_PERFCOUNTER_READ:
		ret = hgsl_ioctl_perfcounter_read(filep, arg);
		break;
	case HGSL_IOCTL_SET_METAINFO:
		ret = hgsl_ioctl_set_metainfo(filep, arg);
		break;
	case HGSL_IOCTL_HSYNC_FENCE_CREATE:
		ret = hgsl_ioctl_hsync_fence_create(filep, arg);
		break;
	case HGSL_IOCTL_ISYNC_TIMELINE_CREATE:
		ret = hgsl_ioctl_isync_timeline_create(filep, arg);
		break;
	case HGSL_IOCTL_ISYNC_TIMELINE_DESTROY:
		ret = hgsl_ioctl_isync_timeline_destroy(filep, arg);
		break;
	case HGSL_IOCTL_ISYNC_FENCE_CREATE:
		ret = hgsl_ioctl_isync_fence_create(filep, arg);
		break;
	case HGSL_IOCTL_ISYNC_FENCE_SIGNAL:
		ret = hgsl_ioctl_isync_fence_signal(filep, arg);
		break;
	case HGSL_IOCTL_ISYNC_FORWARD:
		ret = hgsl_ioctl_isync_forward(filep, arg);
		break;
	case HGSL_IOCTL_TIMELINE_CREATE:
		ret = hgsl_ioctl_timeline_create(filep, arg);
		break;
	case HGSL_IOCTL_TIMELINE_SIGNAL:
		ret = hgsl_ioctl_timeline_signal(filep, arg);
		break;
	case HGSL_IOCTL_TIMELINE_QUERY:
		ret = hgsl_ioctl_timeline_query(filep, arg);
		break;
	case HGSL_IOCTL_TIMELINE_WAIT:
		ret = hgsl_ioctl_timeline_wait(filep, arg);
		break;

	default:
		ret = ENOTTY;
	}

	return ret;
}

static long hgsl_compat_ioctl(struct file *filep, unsigned int cmd,
	unsigned long arg)
{
	return hgsl_ioctl(filep, cmd, arg);
}

static const struct file_operations hgsl_fops = {
	.owner = THIS_MODULE,
	.open = hgsl_open,
	.release = hgsl_release,
	.read = hgsl_read,
	.unlocked_ioctl = hgsl_ioctl,
	.compat_ioctl = hgsl_compat_ioctl
};

static int qcom_hgsl_register(struct platform_device *pdev,
				struct qcom_hgsl *hgsl_dev)
{
	int ret;

	ret = alloc_chrdev_region(&hgsl_dev->device_no, 0,
						HGSL_DEV_NUM,
						HGSL_DEVICE_NAME);
	if (ret < 0) {
		dev_err(&pdev->dev, "alloc_chrdev_region failed %d\n", ret);
		return ret;
	}

	hgsl_dev->driver_class = class_create(THIS_MODULE, HGSL_DEVICE_NAME);
	if (IS_ERR(hgsl_dev->driver_class)) {
		ret = -ENOMEM;
		dev_err(&pdev->dev, "class_create failed %d\n", ret);
		goto exit_unreg_chrdev_region;
	}

	hgsl_dev->class_dev = device_create(hgsl_dev->driver_class,
					NULL,
					hgsl_dev->device_no,
					hgsl_dev, HGSL_DEVICE_NAME);

	if (IS_ERR(hgsl_dev->class_dev)) {
		ret = PTR_ERR(hgsl_dev->class_dev);
		goto exit_destroy_class;
	}

	cdev_init(&hgsl_dev->cdev, &hgsl_fops);

	hgsl_dev->cdev.owner = THIS_MODULE;

	ret = cdev_add(&hgsl_dev->cdev,
					MKDEV(MAJOR(hgsl_dev->device_no), 0),
					1);
	if (ret < 0) {
		dev_err(&pdev->dev, "cdev_add failed %d\n", ret);
		goto exit_destroy_device;
	}

	ret = dma_coerce_mask_and_coherent(hgsl_dev->dev, DMA_BIT_MASK(64));
	if (ret)
		LOGW("Failed to set dma mask to 64 bits, ret = %d", ret);

	return 0;

exit_destroy_device:
	device_destroy(hgsl_dev->driver_class, hgsl_dev->device_no);
exit_destroy_class:
	class_destroy(hgsl_dev->driver_class);
exit_unreg_chrdev_region:
	unregister_chrdev_region(hgsl_dev->device_no, 1);
	return ret;
}

static void qcom_hgsl_deregister(struct platform_device *pdev)
{
	struct qcom_hgsl *hgsl_dev = platform_get_drvdata(pdev);

	cdev_del(&hgsl_dev->cdev);
	device_destroy(hgsl_dev->driver_class, hgsl_dev->device_no);
	class_destroy(hgsl_dev->driver_class);
	unregister_chrdev_region(hgsl_dev->device_no, HGSL_DEV_NUM);
}

static bool hgsl_is_db_off(struct platform_device *pdev)
{
	uint32_t db_off = 0;

	if (pdev == NULL)
		return true;

	db_off = of_property_read_bool(pdev->dev.of_node, "db-off");

	return db_off == 1;
}

static int hgsl_reg_map(struct platform_device *pdev,
			char *res_name, struct reg *reg)
{
	struct resource *res;
	int ret = 0;

	if (!pdev || !res_name || !reg) {
		ret = -EINVAL;
		goto exit;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						res_name);
	if (!res) {
		dev_err(&pdev->dev, "get resource :%s failed\n",
								res_name);
		ret = -EINVAL;
		goto exit;
	}

	if (!res->start || !resource_size(res)) {
		dev_err(&pdev->dev, "Register region %s is invalid\n",
								res_name);
		ret = -EINVAL;
		goto exit;
	}

	reg->paddr = res->start;
	reg->size = resource_size(res);
	if (devm_request_mem_region(&pdev->dev,
					reg->paddr, reg->size,
					res_name) == NULL) {
		dev_err(&pdev->dev, "request_mem_region  for %s failed\n",
								res_name);
		ret = -ENODEV;
		goto exit;
	}

	reg->vaddr = devm_ioremap(&pdev->dev, res->start,
						resource_size(res));
	if (!reg->vaddr) {
		dev_err(&pdev->dev, "Unable to remap %s registers\n",
								res_name);
		ret = -ENODEV;
		goto exit;
	}

exit:
	return ret;
}

static int qcom_hgsl_probe(struct platform_device *pdev)
{
	struct qcom_hgsl *hgsl_dev;
	int ret;
	int i;

	hgsl_dev = devm_kzalloc(&pdev->dev, sizeof(*hgsl_dev), GFP_KERNEL);
	if (!hgsl_dev)
		return -ENOMEM;

	hgsl_dev->dev = &pdev->dev;

	ret = qcom_hgsl_register(pdev, hgsl_dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "qcom_hgsl_register failed, ret %d\n",
									ret);
		return ret;
	}

	ret = hgsl_init_context(hgsl_dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "hgsl_init_context failed, ret %d\n",
									ret);
		goto exit_dereg;
	}

	ret = hgsl_init_release_wq(hgsl_dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "hgsl_init_release_wq failed, ret %d\n",
									ret);
		goto exit_dereg;
	}

	hgsl_dev->db_off = hgsl_is_db_off(pdev);
	idr_init(&hgsl_dev->isync_timeline_idr);
	spin_lock_init(&hgsl_dev->isync_timeline_lock);

	for (i = 0; i < MAX_DB_QUEUE; i++) {
		mutex_init(&hgsl_dev->dbq[i].lock);
		hgsl_dev->dbq[i].state = DB_STATE_Q_UNINIT;
	}

	if (!hgsl_dev->db_off)
		hgsl_init_global_hyp_channel(hgsl_dev);

	platform_set_drvdata(pdev, hgsl_dev);

	return 0;

exit_dereg:
	qcom_hgsl_deregister(pdev);
	return ret;
}

static int qcom_hgsl_remove(struct platform_device *pdev)
{
	struct qcom_hgsl *hgsl = platform_get_drvdata(pdev);
	struct hgsl_tcsr *tcsr_sender, *tcsr_receiver;
	int i;

	for (i = 0; i < HGSL_TCSR_NUM; i++) {
		tcsr_sender = hgsl->tcsr[i][HGSL_TCSR_ROLE_SENDER];
		tcsr_receiver = hgsl->tcsr[i][HGSL_TCSR_ROLE_RECEIVER];

		if (tcsr_sender) {
			hgsl_tcsr_disable(tcsr_sender);
			hgsl_tcsr_free(tcsr_sender);
		}

		if (tcsr_receiver) {
			hgsl_tcsr_disable(tcsr_receiver);
			hgsl_tcsr_free(tcsr_receiver);
			flush_workqueue(hgsl->wq);
			destroy_workqueue(hgsl->wq);
		}
	}

	kfree(hgsl->contexts);
	hgsl->contexts = NULL;
	memset(hgsl->tcsr, 0, sizeof(hgsl->tcsr));

	for (i = 0; i < MAX_DB_QUEUE; i++)
		if (hgsl->dbq[i].state == DB_STATE_Q_INIT_DONE)
			hgsl_reset_dbq(&hgsl->dbq[i]);

	idr_destroy(&hgsl->isync_timeline_idr);
	qcom_hgsl_deregister(pdev);
	return 0;
}

static const struct of_device_id qcom_hgsl_of_match[] = {
	{ .compatible = "qcom,hgsl" },
	{}
};
MODULE_DEVICE_TABLE(of, qcom_hgsl_of_match);

static struct platform_driver qcom_hgsl_driver = {
	.probe = qcom_hgsl_probe,
	.remove = qcom_hgsl_remove,
	.driver  = {
		.name  = "qcom-hgsl",
		.of_match_table = qcom_hgsl_of_match,
	},
};

static int __init hgsl_init(void)
{
	int err;

	err = platform_driver_register(&qcom_hgsl_driver);
	if (err) {
		pr_err("Failed to register hgsl driver: %d\n", err);
		goto exit;
	}

#if IS_ENABLED(CONFIG_QCOM_HGSL_TCSR_SIGNAL)
	err = platform_driver_register(&hgsl_tcsr_driver);
	if (err) {
		pr_err("Failed to register hgsl tcsr driver: %d\n", err);
		platform_driver_unregister(&qcom_hgsl_driver);
	}
#endif

exit:
	return err;
}

static void __exit hgsl_exit(void)
{
	platform_driver_unregister(&qcom_hgsl_driver);
#if IS_ENABLED(CONFIG_QCOM_HGSL_TCSR_SIGNAL)
	platform_driver_unregister(&hgsl_tcsr_driver);
#endif
}

module_init(hgsl_init);
module_exit(hgsl_exit);

MODULE_IMPORT_NS(DMA_BUF);

MODULE_DESCRIPTION("QTI Hypervisor Graphics system driver");
MODULE_LICENSE("GPL");
