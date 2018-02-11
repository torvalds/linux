/* SPDX-License-Identifier: GPL-2.0 */
/*
 * zfcp device driver
 *
 * Header file for zfcp qdio interface
 *
 * Copyright IBM Corp. 2010
 */

#ifndef ZFCP_QDIO_H
#define ZFCP_QDIO_H

#include <asm/qdio.h>

#define ZFCP_QDIO_SBALE_LEN	PAGE_SIZE

/* Max SBALS for chaining */
#define ZFCP_QDIO_MAX_SBALS_PER_REQ	36

/**
 * struct zfcp_qdio - basic qdio data structure
 * @res_q: response queue
 * @req_q: request queue
 * @req_q_idx: index of next free buffer
 * @req_q_free: number of free buffers in queue
 * @stat_lock: lock to protect req_q_util and req_q_time
 * @req_q_lock: lock to serialize access to request queue
 * @req_q_time: time of last fill level change
 * @req_q_util: used for accounting
 * @req_q_full: queue full incidents
 * @req_q_wq: used to wait for SBAL availability
 * @adapter: adapter used in conjunction with this qdio structure
 */
struct zfcp_qdio {
	struct qdio_buffer	*res_q[QDIO_MAX_BUFFERS_PER_Q];
	struct qdio_buffer	*req_q[QDIO_MAX_BUFFERS_PER_Q];
	u8			req_q_idx;
	atomic_t		req_q_free;
	spinlock_t		stat_lock;
	spinlock_t		req_q_lock;
	unsigned long long	req_q_time;
	u64			req_q_util;
	atomic_t		req_q_full;
	wait_queue_head_t	req_q_wq;
	struct zfcp_adapter	*adapter;
	u16			max_sbale_per_sbal;
	u16			max_sbale_per_req;
};

/**
 * struct zfcp_qdio_req - qdio queue related values for a request
 * @sbtype: sbal type flags for sbale 0
 * @sbal_number: number of free sbals
 * @sbal_first: first sbal for this request
 * @sbal_last: last sbal for this request
 * @sbal_limit: last possible sbal for this request
 * @sbale_curr: current sbale at creation of this request
 * @qdio_outb_usage: usage of outbound queue
 */
struct zfcp_qdio_req {
	u8	sbtype;
	u8	sbal_number;
	u8	sbal_first;
	u8	sbal_last;
	u8	sbal_limit;
	u8	sbale_curr;
	u16	qdio_outb_usage;
};

/**
 * zfcp_qdio_sbale_req - return pointer to sbale on req_q for a request
 * @qdio: pointer to struct zfcp_qdio
 * @q_rec: pointer to struct zfcp_qdio_req
 * Returns: pointer to qdio_buffer_element (sbale) structure
 */
static inline struct qdio_buffer_element *
zfcp_qdio_sbale_req(struct zfcp_qdio *qdio, struct zfcp_qdio_req *q_req)
{
	return &qdio->req_q[q_req->sbal_last]->element[0];
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
	return &qdio->req_q[q_req->sbal_last]->element[q_req->sbale_curr];
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
			unsigned long req_id, u8 sbtype, void *data, u32 len)
{
	struct qdio_buffer_element *sbale;
	int count = min(atomic_read(&qdio->req_q_free),
			ZFCP_QDIO_MAX_SBALS_PER_REQ);

	q_req->sbal_first = q_req->sbal_last = qdio->req_q_idx;
	q_req->sbal_number = 1;
	q_req->sbtype = sbtype;
	q_req->sbale_curr = 1;
	q_req->sbal_limit = (q_req->sbal_first + count - 1)
					% QDIO_MAX_BUFFERS_PER_Q;

	sbale = zfcp_qdio_sbale_req(qdio, q_req);
	sbale->addr = (void *) req_id;
	sbale->eflags = 0;
	sbale->sflags = SBAL_SFLAGS0_COMMAND | sbtype;

	if (unlikely(!data))
		return;
	sbale++;
	sbale->addr = data;
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

	BUG_ON(q_req->sbale_curr == qdio->max_sbale_per_sbal - 1);
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
	sbale->eflags |= SBAL_EFLAGS_LAST_ENTRY;
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
void zfcp_qdio_skip_to_last_sbale(struct zfcp_qdio *qdio,
				  struct zfcp_qdio_req *q_req)
{
	q_req->sbale_curr = qdio->max_sbale_per_sbal - 1;
}

/**
 * zfcp_qdio_sbal_limit - set the sbal limit for a request in q_req
 * @qdio: pointer to struct zfcp_qdio
 * @q_req: The current zfcp_qdio_req
 * @max_sbals: maximum number of SBALs allowed
 */
static inline
void zfcp_qdio_sbal_limit(struct zfcp_qdio *qdio,
			  struct zfcp_qdio_req *q_req, int max_sbals)
{
	int count = min(atomic_read(&qdio->req_q_free), max_sbals);

	q_req->sbal_limit = (q_req->sbal_first + count - 1) %
				QDIO_MAX_BUFFERS_PER_Q;
}

/**
 * zfcp_qdio_set_data_div - set data division count
 * @qdio: pointer to struct zfcp_qdio
 * @q_req: The current zfcp_qdio_req
 * @count: The data division count
 */
static inline
void zfcp_qdio_set_data_div(struct zfcp_qdio *qdio,
			    struct zfcp_qdio_req *q_req, u32 count)
{
	struct qdio_buffer_element *sbale;

	sbale = qdio->req_q[q_req->sbal_first]->element;
	sbale->length = count;
}

/**
 * zfcp_qdio_real_bytes - count bytes used
 * @sg: pointer to struct scatterlist
 */
static inline
unsigned int zfcp_qdio_real_bytes(struct scatterlist *sg)
{
	unsigned int real_bytes = 0;

	for (; sg; sg = sg_next(sg))
		real_bytes += sg->length;

	return real_bytes;
}

/**
 * zfcp_qdio_set_scount - set SBAL count value
 * @qdio: pointer to struct zfcp_qdio
 * @q_req: The current zfcp_qdio_req
 */
static inline
void zfcp_qdio_set_scount(struct zfcp_qdio *qdio, struct zfcp_qdio_req *q_req)
{
	struct qdio_buffer_element *sbale;

	sbale = qdio->req_q[q_req->sbal_first]->element;
	sbale->scount = q_req->sbal_number - 1;
}

#endif /* ZFCP_QDIO_H */
