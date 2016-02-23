/*
 * Copyright 2008-2010 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef _VNIC_RQ_H_
#define _VNIC_RQ_H_

#include <linux/pci.h>
#include <linux/netdevice.h>

#include "vnic_dev.h"
#include "vnic_cq.h"

/* Receive queue control */
struct vnic_rq_ctrl {
	u64 ring_base;			/* 0x00 */
	u32 ring_size;			/* 0x08 */
	u32 pad0;
	u32 posted_index;		/* 0x10 */
	u32 pad1;
	u32 cq_index;			/* 0x18 */
	u32 pad2;
	u32 enable;			/* 0x20 */
	u32 pad3;
	u32 running;			/* 0x28 */
	u32 pad4;
	u32 fetch_index;		/* 0x30 */
	u32 pad5;
	u32 error_interrupt_enable;	/* 0x38 */
	u32 pad6;
	u32 error_interrupt_offset;	/* 0x40 */
	u32 pad7;
	u32 error_status;		/* 0x48 */
	u32 pad8;
	u32 dropped_packet_count;	/* 0x50 */
	u32 pad9;
	u32 dropped_packet_count_rc;	/* 0x58 */
	u32 pad10;
};

/* Break the vnic_rq_buf allocations into blocks of 32/64 entries */
#define VNIC_RQ_BUF_MIN_BLK_ENTRIES 32
#define VNIC_RQ_BUF_DFLT_BLK_ENTRIES 64
#define VNIC_RQ_BUF_BLK_ENTRIES(entries) \
	((unsigned int)((entries < VNIC_RQ_BUF_DFLT_BLK_ENTRIES) ? \
	VNIC_RQ_BUF_MIN_BLK_ENTRIES : VNIC_RQ_BUF_DFLT_BLK_ENTRIES))
#define VNIC_RQ_BUF_BLK_SZ(entries) \
	(VNIC_RQ_BUF_BLK_ENTRIES(entries) * sizeof(struct vnic_rq_buf))
#define VNIC_RQ_BUF_BLKS_NEEDED(entries) \
	DIV_ROUND_UP(entries, VNIC_RQ_BUF_BLK_ENTRIES(entries))
#define VNIC_RQ_BUF_BLKS_MAX VNIC_RQ_BUF_BLKS_NEEDED(4096)

struct vnic_rq_buf {
	struct vnic_rq_buf *next;
	dma_addr_t dma_addr;
	void *os_buf;
	unsigned int os_buf_index;
	unsigned int len;
	unsigned int index;
	void *desc;
	uint64_t wr_id;
};

enum enic_poll_state {
	ENIC_POLL_STATE_IDLE,
	ENIC_POLL_STATE_NAPI,
	ENIC_POLL_STATE_POLL
};

struct vnic_rq {
	unsigned int index;
	struct vnic_dev *vdev;
	struct vnic_rq_ctrl __iomem *ctrl;              /* memory-mapped */
	struct vnic_dev_ring ring;
	struct vnic_rq_buf *bufs[VNIC_RQ_BUF_BLKS_MAX];
	struct vnic_rq_buf *to_use;
	struct vnic_rq_buf *to_clean;
	void *os_buf_head;
	unsigned int pkts_outstanding;
#ifdef CONFIG_NET_RX_BUSY_POLL
	atomic_t bpoll_state;
#endif /* CONFIG_NET_RX_BUSY_POLL */
};

static inline unsigned int vnic_rq_desc_avail(struct vnic_rq *rq)
{
	/* how many does SW own? */
	return rq->ring.desc_avail;
}

static inline unsigned int vnic_rq_desc_used(struct vnic_rq *rq)
{
	/* how many does HW own? */
	return rq->ring.desc_count - rq->ring.desc_avail - 1;
}

static inline void *vnic_rq_next_desc(struct vnic_rq *rq)
{
	return rq->to_use->desc;
}

static inline unsigned int vnic_rq_next_index(struct vnic_rq *rq)
{
	return rq->to_use->index;
}

static inline void vnic_rq_post(struct vnic_rq *rq,
	void *os_buf, unsigned int os_buf_index,
	dma_addr_t dma_addr, unsigned int len,
	uint64_t wrid)
{
	struct vnic_rq_buf *buf = rq->to_use;

	buf->os_buf = os_buf;
	buf->os_buf_index = os_buf_index;
	buf->dma_addr = dma_addr;
	buf->len = len;
	buf->wr_id = wrid;

	buf = buf->next;
	rq->to_use = buf;
	rq->ring.desc_avail--;

	/* Move the posted_index every nth descriptor
	 */

#ifndef VNIC_RQ_RETURN_RATE
#define VNIC_RQ_RETURN_RATE		0xf	/* keep 2^n - 1 */
#endif

	if ((buf->index & VNIC_RQ_RETURN_RATE) == 0) {
		/* Adding write memory barrier prevents compiler and/or CPU
		 * reordering, thus avoiding descriptor posting before
		 * descriptor is initialized. Otherwise, hardware can read
		 * stale descriptor fields.
		 */
		wmb();
		iowrite32(buf->index, &rq->ctrl->posted_index);
	}
}

static inline void vnic_rq_return_descs(struct vnic_rq *rq, unsigned int count)
{
	rq->ring.desc_avail += count;
}

enum desc_return_options {
	VNIC_RQ_RETURN_DESC,
	VNIC_RQ_DEFER_RETURN_DESC,
};

static inline void vnic_rq_service(struct vnic_rq *rq,
	struct cq_desc *cq_desc, u16 completed_index,
	int desc_return, void (*buf_service)(struct vnic_rq *rq,
	struct cq_desc *cq_desc, struct vnic_rq_buf *buf,
	int skipped, void *opaque), void *opaque)
{
	struct vnic_rq_buf *buf;
	int skipped;

	buf = rq->to_clean;
	while (1) {

		skipped = (buf->index != completed_index);

		(*buf_service)(rq, cq_desc, buf, skipped, opaque);

		if (desc_return == VNIC_RQ_RETURN_DESC)
			rq->ring.desc_avail++;

		rq->to_clean = buf->next;

		if (!skipped)
			break;

		buf = rq->to_clean;
	}
}

static inline int vnic_rq_fill(struct vnic_rq *rq,
	int (*buf_fill)(struct vnic_rq *rq))
{
	int err;

	while (vnic_rq_desc_avail(rq) > 0) {

		err = (*buf_fill)(rq);
		if (err)
			return err;
	}

	return 0;
}

#ifdef CONFIG_NET_RX_BUSY_POLL
static inline void enic_busy_poll_init_lock(struct vnic_rq *rq)
{
	atomic_set(&rq->bpoll_state, ENIC_POLL_STATE_IDLE);
}

static inline bool enic_poll_lock_napi(struct vnic_rq *rq)
{
	int rc = atomic_cmpxchg(&rq->bpoll_state, ENIC_POLL_STATE_IDLE,
				ENIC_POLL_STATE_NAPI);

	return (rc == ENIC_POLL_STATE_IDLE);
}

static inline void enic_poll_unlock_napi(struct vnic_rq *rq,
					 struct napi_struct *napi)
{
	WARN_ON(atomic_read(&rq->bpoll_state) != ENIC_POLL_STATE_NAPI);
	napi_gro_flush(napi, false);
	atomic_set(&rq->bpoll_state, ENIC_POLL_STATE_IDLE);
}

static inline bool enic_poll_lock_poll(struct vnic_rq *rq)
{
	int rc = atomic_cmpxchg(&rq->bpoll_state, ENIC_POLL_STATE_IDLE,
				ENIC_POLL_STATE_POLL);

	return (rc == ENIC_POLL_STATE_IDLE);
}


static inline void enic_poll_unlock_poll(struct vnic_rq *rq)
{
	WARN_ON(atomic_read(&rq->bpoll_state) != ENIC_POLL_STATE_POLL);
	atomic_set(&rq->bpoll_state, ENIC_POLL_STATE_IDLE);
}

static inline bool enic_poll_busy_polling(struct vnic_rq *rq)
{
	return atomic_read(&rq->bpoll_state) & ENIC_POLL_STATE_POLL;
}

#else

static inline void enic_busy_poll_init_lock(struct vnic_rq *rq)
{
}

static inline bool enic_poll_lock_napi(struct vnic_rq *rq)
{
	return true;
}

static inline bool enic_poll_unlock_napi(struct vnic_rq *rq,
					 struct napi_struct *napi)
{
	return false;
}

static inline bool enic_poll_lock_poll(struct vnic_rq *rq)
{
	return false;
}

static inline bool enic_poll_unlock_poll(struct vnic_rq *rq)
{
	return false;
}

static inline bool enic_poll_ll_polling(struct vnic_rq *rq)
{
	return false;
}
#endif /* CONFIG_NET_RX_BUSY_POLL */

void vnic_rq_free(struct vnic_rq *rq);
int vnic_rq_alloc(struct vnic_dev *vdev, struct vnic_rq *rq, unsigned int index,
	unsigned int desc_count, unsigned int desc_size);
void vnic_rq_init(struct vnic_rq *rq, unsigned int cq_index,
	unsigned int error_interrupt_enable,
	unsigned int error_interrupt_offset);
unsigned int vnic_rq_error_status(struct vnic_rq *rq);
void vnic_rq_enable(struct vnic_rq *rq);
int vnic_rq_disable(struct vnic_rq *rq);
void vnic_rq_clean(struct vnic_rq *rq,
	void (*buf_clean)(struct vnic_rq *rq, struct vnic_rq_buf *buf));

#endif /* _VNIC_RQ_H_ */
