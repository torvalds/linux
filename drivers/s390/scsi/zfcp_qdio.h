/*
 * zfcp device driver
 *
 * Header file for zfcp qdio interface
 *
 * Copyright IBM Corporation 2010
 */

#ifndef ZFCP_QDIO_H
#define ZFCP_QDIO_H

#include <asm/qdio.h>

#define ZFCP_QDIO_SBALE_LEN	PAGE_SIZE

/* DMQ bug workaround: don't use last SBALE */
#define ZFCP_QDIO_MAX_SBALES_PER_SBAL	(QDIO_MAX_ELEMENTS_PER_BUFFER - 1)

/* index of last SBALE (with respect to DMQ bug workaround) */
#define ZFCP_QDIO_LAST_SBALE_PER_SBAL	(ZFCP_QDIO_MAX_SBALES_PER_SBAL - 1)

/**
 * struct zfcp_qdio_queue - qdio queue buffer, zfcp index and free count
 * @sbal: qdio buffers
 * @first: index of next free buffer in queue
 * @count: number of free buffers in queue
 */
struct zfcp_qdio_queue {
	struct qdio_buffer *sbal[QDIO_MAX_BUFFERS_PER_Q];
	u8		   first;
	atomic_t           count;
};

/**
 * struct zfcp_qdio - basic qdio data structure
 * @resp_q: response queue
 * @req_q: request queue
 * @stat_lock: lock to protect req_q_util and req_q_time
 * @req_q_lock: lock to serialize access to request queue
 * @req_q_time: time of last fill level change
 * @req_q_util: used for accounting
 * @req_q_full: queue full incidents
 * @req_q_wq: used to wait for SBAL availability
 * @adapter: adapter used in conjunction with this qdio structure
 */
struct zfcp_qdio {
	struct zfcp_qdio_queue	resp_q;
	struct zfcp_qdio_queue	req_q;
	spinlock_t		stat_lock;
	spinlock_t		req_q_lock;
	unsigned long long	req_q_time;
	u64			req_q_util;
	atomic_t		req_q_full;
	wait_queue_head_t	req_q_wq;
	struct zfcp_adapter	*adapter;
};

/**
 * struct zfcp_qdio_req - qdio queue related values for a request
 * @sbtype: sbal type flags for sbale 0
 * @sbal_number: number of free sbals
 * @sbal_first: first sbal for this request
 * @sbal_last: last sbal for this request
 * @sbal_limit: last possible sbal for this request
 * @sbale_curr: current sbale at creation of this request
 * @sbal_response: sbal used in interrupt
 * @qdio_outb_usage: usage of outbound queue
 * @qdio_inb_usage: usage of inbound queue
 */
struct zfcp_qdio_req {
	u32	sbtype;
	u8	sbal_number;
	u8	sbal_first;
	u8	sbal_last;
	u8	sbal_limit;
	u8	sbale_curr;
	u8	sbal_response;
	u16	qdio_outb_usage;
	u16	qdio_inb_usage;
};

/**
 * zfcp_qdio_sbale - return pointer to sbale in qdio queue
 * @q: queue where to find sbal
 * @sbal_idx: sbal index in queue
 * @sbale_idx: sbale index in sbal
 */
static inline struct qdio_buffer_element *
zfcp_qdio_sbale(struct zfcp_qdio_queue *q, int sbal_idx, int sbale_idx)
{
	return &q->sbal[sbal_idx]->element[sbale_idx];
}

/**
 * zfcp_qdio_sbale_req - return pointer to sbale on req_q for a request
 * @qdio: pointer to struct zfcp_qdio
 * @q_rec: pointer to struct zfcp_qdio_req
 * Returns: pointer to qdio_buffer_element (sbale) structure
 */
static inline struct qdio_buffer_element *
zfcp_qdio_sbale_req(struct zfcp_qdio *qdio, struct zfcp_qdio_req *q_req)
{
	return zfcp_qdio_sbale(&qdio->req_q, q_req->sbal_last, 0);
}

/**
 * zfcp_qdio_sbale_curr - return current sbale on req_q for a request
 * @qdio: pointer to struct zfcp_qdio
 * @fsf_req: pointer to struct zfcp_fsf_req
 * Returns: pointer to qdio_buffer_element (sbale) structure
 */
static inline struct qdio_buffer_element *
zfcp_qdio_sbale_curr(struct zfcp_qdio *qdio, struct zfcp_qdio_req *q_req)
{
	return zfcp_qdio_sbale(&qdio->req_q, q_req->sbal_last,
			       q_req->sbale_curr);
}

/**
 * zfcp_qdio_req_init - initialize qdio request
 * @qdio: request queue where to start putting the request
 * @q_req: the qdio request to start
 * @req_id: The request id
 * @sbtype: type flags to set for all sbals
 * @data: First data block
 * @len: Length of first data block
 *
 * This is the start of putting the request into the queue, the last
 * step is passing the request to zfcp_qdio_send. The request queue
 * lock must be held during the whole process from init to send.
 */
static inline
void zfcp_qdio_req_init(struct zfcp_qdio *qdio, struct zfcp_qdio_req *q_req,
			unsigned long req_id, u32 sbtype, void *data, u32 len)
{
	struct qdio_buffer_element *sbale;

	q_req->sbal_first = q_req->sbal_last = qdio->req_q.first;
	q_req->sbal_number = 1;
	q_req->sbtype = sbtype;

	sbale = zfcp_qdio_sbale_req(qdio, q_req);
	sbale->addr = (void *) req_id;
	sbale->flags |= SBAL_FLAGS0_COMMAND;
	sbale->flags |= sbtype;

	q_req->sbale_curr = 1;
	sbale++;
	sbale->addr = data;
	if (likely(data))
		sbale->length = len;
}

/**
 * zfcp_qdio_fill_next - Fill next sbale, only for single sbal requests
 * @qdio: pointer to struct zfcp_qdio
 * @q_req: pointer to struct zfcp_queue_req
 *
 * This is only required for single sbal requests, calling it when
 * wrapping around to the next sbal is a bug.
 */
static inline
void zfcp_qdio_fill_next(struct zfcp_qdio *qdio, struct zfcp_qdio_req *q_req,
			 void *data, u32 len)
{
	struct qdio_buffer_element *sbale;

	BUG_ON(q_req->sbale_curr == ZFCP_QDIO_LAST_SBALE_PER_SBAL);
	q_req->sbale_curr++;
	sbale = zfcp_qdio_sbale_curr(qdio, q_req);
	sbale->addr = data;
	sbale->length = len;
}

/**
 * zfcp_qdio_set_sbale_last - set last entry flag in current sbale
 * @qdio: pointer to struct zfcp_qdio
 * @q_req: pointer to struct zfcp_queue_req
 */
static inline
void zfcp_qdio_set_sbale_last(struct zfcp_qdio *qdio,
			      struct zfcp_qdio_req *q_req)
{
	struct qdio_buffer_element *sbale;

	sbale = zfcp_qdio_sbale_curr(qdio, q_req);
	sbale->flags |= SBAL_FLAGS_LAST_ENTRY;
}

/**
 * zfcp_qdio_sg_one_sbal - check if one sbale is enough for sg data
 * @sg: The scatterlist where to check the data size
 *
 * Returns: 1 when one sbale is enough for the data in the scatterlist,
 *	    0 if not.
 */
static inline
int zfcp_qdio_sg_one_sbale(struct scatterlist *sg)
{
	return sg_is_last(sg) && sg->length <= ZFCP_QDIO_SBALE_LEN;
}

/**
 * zfcp_qdio_skip_to_last_sbale - skip to last sbale in sbal
 * @q_req: The current zfcp_qdio_req
 */
static inline
void zfcp_qdio_skip_to_last_sbale(struct zfcp_qdio_req *q_req)
{
	q_req->sbale_curr = ZFCP_QDIO_LAST_SBALE_PER_SBAL;
}

#endif /* ZFCP_QDIO_H */
