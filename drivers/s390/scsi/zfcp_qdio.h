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

#endif /* ZFCP_QDIO_H */
