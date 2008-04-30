/*
 * Copyright (c) 2005 Ammasso, Inc. All rights reserved.
 * Copyright (c) 2005 Open Grid Computing, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include "c2.h"
#include "c2_mq.h"

void *c2_mq_alloc(struct c2_mq *q)
{
	BUG_ON(q->magic != C2_MQ_MAGIC);
	BUG_ON(q->type != C2_MQ_ADAPTER_TARGET);

	if (c2_mq_full(q)) {
		return NULL;
	} else {
#ifdef DEBUG
		struct c2wr_hdr *m =
		    (struct c2wr_hdr *) (q->msg_pool.host + q->priv * q->msg_size);
#ifdef CCMSGMAGIC
		BUG_ON(m->magic != be32_to_cpu(~CCWR_MAGIC));
		m->magic = cpu_to_be32(CCWR_MAGIC);
#endif
		return m;
#else
		return q->msg_pool.host + q->priv * q->msg_size;
#endif
	}
}

void c2_mq_produce(struct c2_mq *q)
{
	BUG_ON(q->magic != C2_MQ_MAGIC);
	BUG_ON(q->type != C2_MQ_ADAPTER_TARGET);

	if (!c2_mq_full(q)) {
		q->priv = (q->priv + 1) % q->q_size;
		q->hint_count++;
		/* Update peer's offset. */
		__raw_writew((__force u16) cpu_to_be16(q->priv), &q->peer->shared);
	}
}

void *c2_mq_consume(struct c2_mq *q)
{
	BUG_ON(q->magic != C2_MQ_MAGIC);
	BUG_ON(q->type != C2_MQ_HOST_TARGET);

	if (c2_mq_empty(q)) {
		return NULL;
	} else {
#ifdef DEBUG
		struct c2wr_hdr *m = (struct c2wr_hdr *)
		    (q->msg_pool.host + q->priv * q->msg_size);
#ifdef CCMSGMAGIC
		BUG_ON(m->magic != be32_to_cpu(CCWR_MAGIC));
#endif
		return m;
#else
		return q->msg_pool.host + q->priv * q->msg_size;
#endif
	}
}

void c2_mq_free(struct c2_mq *q)
{
	BUG_ON(q->magic != C2_MQ_MAGIC);
	BUG_ON(q->type != C2_MQ_HOST_TARGET);

	if (!c2_mq_empty(q)) {

#ifdef CCMSGMAGIC
		{
			struct c2wr_hdr __iomem *m = (struct c2wr_hdr __iomem *)
			    (q->msg_pool.adapter + q->priv * q->msg_size);
			__raw_writel(cpu_to_be32(~CCWR_MAGIC), &m->magic);
		}
#endif
		q->priv = (q->priv + 1) % q->q_size;
		/* Update peer's offset. */
		__raw_writew((__force u16) cpu_to_be16(q->priv), &q->peer->shared);
	}
}


void c2_mq_lconsume(struct c2_mq *q, u32 wqe_count)
{
	BUG_ON(q->magic != C2_MQ_MAGIC);
	BUG_ON(q->type != C2_MQ_ADAPTER_TARGET);

	while (wqe_count--) {
		BUG_ON(c2_mq_empty(q));
		*q->shared = cpu_to_be16((be16_to_cpu(*q->shared)+1) % q->q_size);
	}
}

#if 0
u32 c2_mq_count(struct c2_mq *q)
{
	s32 count;

	if (q->type == C2_MQ_HOST_TARGET)
		count = be16_to_cpu(*q->shared) - q->priv;
	else
		count = q->priv - be16_to_cpu(*q->shared);

	if (count < 0)
		count += q->q_size;

	return (u32) count;
}
#endif  /*  0  */

void c2_mq_req_init(struct c2_mq *q, u32 index, u32 q_size, u32 msg_size,
		    u8 __iomem *pool_start, u16 __iomem *peer, u32 type)
{
	BUG_ON(!q->shared);

	/* This code assumes the byte swapping has already been done! */
	q->index = index;
	q->q_size = q_size;
	q->msg_size = msg_size;
	q->msg_pool.adapter = pool_start;
	q->peer = (struct c2_mq_shared __iomem *) peer;
	q->magic = C2_MQ_MAGIC;
	q->type = type;
	q->priv = 0;
	q->hint_count = 0;
	return;
}
void c2_mq_rep_init(struct c2_mq *q, u32 index, u32 q_size, u32 msg_size,
		    u8 *pool_start, u16 __iomem *peer, u32 type)
{
	BUG_ON(!q->shared);

	/* This code assumes the byte swapping has already been done! */
	q->index = index;
	q->q_size = q_size;
	q->msg_size = msg_size;
	q->msg_pool.host = pool_start;
	q->peer = (struct c2_mq_shared __iomem *) peer;
	q->magic = C2_MQ_MAGIC;
	q->type = type;
	q->priv = 0;
	q->hint_count = 0;
	return;
}
