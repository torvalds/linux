#ifndef __LIBSRP_H__
#define __LIBSRP_H__

#include <linux/list.h>
#include <linux/kfifo.h>
#include <scsi/srp.h>

enum srp_valid {
	INVALIDATE_CMD_RESP_EL = 0,
	VALID_CMD_RESP_EL = 0x80,
	VALID_INIT_MSG = 0xC0,
	VALID_TRANS_EVENT = 0xFF
};

enum srp_format {
	SRP_FORMAT = 1,
	MAD_FORMAT = 2,
	OS400_FORMAT = 3,
	AIX_FORMAT = 4,
	LINUX_FORMAT = 5,
	MESSAGE_IN_CRQ = 6
};

enum srp_init_msg {
	INIT_MSG = 1,
	INIT_COMPLETE_MSG = 2
};

enum srp_trans_event {
	UNUSED_FORMAT = 0,
	PARTNER_FAILED = 1,
	PARTNER_DEREGISTER = 2,
	MIGRATED = 6
};

enum srp_status {
	HEADER_DESCRIPTOR = 0xF1,
	PING = 0xF5,
	PING_RESPONSE = 0xF6
};

enum srp_mad_version {
	MAD_VERSION_1 = 1
};

enum srp_os_type {
	OS400 = 1,
	LINUX = 2,
	AIX = 3,
	OFW = 4
};

enum srp_task_attributes {
	SRP_SIMPLE_TASK = 0,
	SRP_HEAD_TASK = 1,
	SRP_ORDERED_TASK = 2,
	SRP_ACA_TASK = 4
};

enum {
	SRP_TASK_MANAGEMENT_FUNCTION_COMPLETE           = 0,
	SRP_REQUEST_FIELDS_INVALID                      = 2,
	SRP_TASK_MANAGEMENT_FUNCTION_NOT_SUPPORTED      = 4,
	SRP_TASK_MANAGEMENT_FUNCTION_FAILED             = 5
};

struct srp_buf {
	dma_addr_t dma;
	void *buf;
};

struct srp_queue {
	void *pool;
	void *items;
	struct kfifo queue;
	spinlock_t lock;
};

struct srp_target {
	struct device *dev;

	spinlock_t lock;
	struct list_head cmd_queue;

	size_t srp_iu_size;
	struct srp_queue iu_queue;
	size_t rx_ring_size;
	struct srp_buf **rx_ring;

	void *ldata;
};

struct iu_entry {
	struct srp_target *target;

	struct list_head ilist;
	dma_addr_t remote_token;
	unsigned long flags;

	struct srp_buf *sbuf;
	u16 iu_len;
};

struct ibmvscsis_cmd;

typedef int (srp_rdma_t)(struct ibmvscsis_cmd *, struct scatterlist *, int,
			 struct srp_direct_buf *, int,
			 enum dma_data_direction, unsigned int);
int srp_target_alloc(struct srp_target *, struct device *, size_t, size_t);
void srp_target_free(struct srp_target *);
struct iu_entry *srp_iu_get(struct srp_target *);
void srp_iu_put(struct iu_entry *);
int srp_transfer_data(struct ibmvscsis_cmd *, struct srp_cmd *,
		      srp_rdma_t, int, int);
u64 srp_data_length(struct srp_cmd *cmd, enum dma_data_direction dir);
int srp_get_desc_table(struct srp_cmd *srp_cmd, enum dma_data_direction *dir,
		       u64 *data_len);
static inline int srp_cmd_direction(struct srp_cmd *cmd)
{
	return (cmd->buf_fmt >> 4) ? DMA_TO_DEVICE : DMA_FROM_DEVICE;
}

#endif
