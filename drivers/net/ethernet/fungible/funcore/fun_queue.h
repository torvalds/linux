/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */

#ifndef _FUN_QEUEUE_H
#define _FUN_QEUEUE_H

#include <linux/interrupt.h>
#include <linux/io.h>

struct device;
struct fun_dev;
struct fun_queue;
struct fun_cqe_info;
struct fun_rsp_common;

typedef void (*cq_callback_t)(struct fun_queue *funq, void *data, void *msg,
			      const struct fun_cqe_info *info);

struct fun_rq_info {
	dma_addr_t dma;
	struct page *page;
};

/* A queue group consisting of an SQ, a CQ, and an optional RQ. */
struct fun_queue {
	struct fun_dev *fdev;
	spinlock_t sq_lock;

	dma_addr_t cq_dma_addr;
	dma_addr_t sq_dma_addr;
	dma_addr_t rq_dma_addr;

	u32 __iomem *cq_db;
	u32 __iomem *sq_db;
	u32 __iomem *rq_db;

	void *cqes;
	void *sq_cmds;
	struct fun_eprq_rqbuf *rqes;
	struct fun_rq_info *rq_info;

	u32 cqid;
	u32 sqid;
	u32 rqid;

	u32 cq_depth;
	u32 sq_depth;
	u32 rq_depth;

	u16 cq_head;
	u16 sq_tail;
	u16 rq_tail;

	u8 cqe_size_log2;
	u8 sqe_size_log2;

	u16 cqe_info_offset;

	u16 rq_buf_idx;
	int rq_buf_offset;
	u16 num_rqe_to_fill;

	u8 cq_intcoal_usec;
	u8 cq_intcoal_nentries;
	u8 sq_intcoal_usec;
	u8 sq_intcoal_nentries;

	u16 cq_flags;
	u16 sq_flags;
	u16 rq_flags;

	/* SQ head writeback */
	u16 sq_comp;

	volatile __be64 *sq_head;

	cq_callback_t cq_cb;
	void *cb_data;

	irq_handler_t irq_handler;
	void *irq_data;
	s16 cq_vector;
	u8 cq_phase;

	/* I/O q index */
	u16 qid;

	char irqname[24];
};

static inline void *fun_sqe_at(const struct fun_queue *funq, unsigned int pos)
{
	return funq->sq_cmds + (pos << funq->sqe_size_log2);
}

static inline void funq_sq_post_tail(struct fun_queue *funq, u16 tail)
{
	if (++tail == funq->sq_depth)
		tail = 0;
	funq->sq_tail = tail;
	writel(tail, funq->sq_db);
}

static inline struct fun_cqe_info *funq_cqe_info(const struct fun_queue *funq,
						 void *cqe)
{
	return cqe + funq->cqe_info_offset;
}

static inline void funq_rq_post(struct fun_queue *funq)
{
	writel(funq->rq_tail, funq->rq_db);
}

struct fun_queue_alloc_req {
	u8  cqe_size_log2;
	u8  sqe_size_log2;

	u16 cq_flags;
	u16 sq_flags;
	u16 rq_flags;

	u32 cq_depth;
	u32 sq_depth;
	u32 rq_depth;

	u8 cq_intcoal_usec;
	u8 cq_intcoal_nentries;
	u8 sq_intcoal_usec;
	u8 sq_intcoal_nentries;
};

int fun_sq_create(struct fun_dev *fdev, u16 flags, u32 sqid, u32 cqid,
		  u8 sqe_size_log2, u32 sq_depth, dma_addr_t dma_addr,
		  u8 coal_nentries, u8 coal_usec, u32 irq_num,
		  u32 scan_start_id, u32 scan_end_id,
		  u32 rq_buf_size_log2, u32 *sqidp, u32 __iomem **dbp);
int fun_cq_create(struct fun_dev *fdev, u16 flags, u32 cqid, u32 rqid,
		  u8 cqe_size_log2, u32 cq_depth, dma_addr_t dma_addr,
		  u16 headroom, u16 tailroom, u8 coal_nentries, u8 coal_usec,
		  u32 irq_num, u32 scan_start_id, u32 scan_end_id,
		  u32 *cqidp, u32 __iomem **dbp);
void *fun_alloc_ring_mem(struct device *dma_dev, size_t depth,
			 size_t hw_desc_sz, size_t sw_desc_size, bool wb,
			 int numa_node, dma_addr_t *dma_addr, void **sw_va,
			 volatile __be64 **wb_va);
void fun_free_ring_mem(struct device *dma_dev, size_t depth, size_t hw_desc_sz,
		       bool wb, void *hw_va, dma_addr_t dma_addr, void *sw_va);

#define fun_destroy_sq(fdev, sqid) \
	fun_res_destroy((fdev), FUN_ADMIN_OP_EPSQ, 0, (sqid))
#define fun_destroy_cq(fdev, cqid) \
	fun_res_destroy((fdev), FUN_ADMIN_OP_EPCQ, 0, (cqid))

struct fun_queue *fun_alloc_queue(struct fun_dev *fdev, int qid,
				  const struct fun_queue_alloc_req *req);
void fun_free_queue(struct fun_queue *funq);

static inline void fun_set_cq_callback(struct fun_queue *funq, cq_callback_t cb,
				       void *cb_data)
{
	funq->cq_cb = cb;
	funq->cb_data = cb_data;
}

int fun_create_rq(struct fun_queue *funq);
int fun_create_queue(struct fun_queue *funq);

void fun_free_irq(struct fun_queue *funq);
int fun_request_irq(struct fun_queue *funq, const char *devname,
		    irq_handler_t handler, void *data);

unsigned int __fun_process_cq(struct fun_queue *funq, unsigned int max);
unsigned int fun_process_cq(struct fun_queue *funq, unsigned int max);

#endif /* _FUN_QEUEUE_H */
