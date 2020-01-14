/*
 * Copyright (c) 2017, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#include "qmgr.h"

struct nvkm_msgqueue_seq *
msgqueue_seq_acquire(struct nvkm_msgqueue *priv)
{
	const struct nvkm_subdev *subdev = priv->falcon->owner;
	struct nvkm_msgqueue_seq *seq;
	u32 index;

	mutex_lock(&priv->seq_lock);
	index = find_first_zero_bit(priv->seq_tbl, NVKM_MSGQUEUE_NUM_SEQUENCES);
	if (index >= NVKM_MSGQUEUE_NUM_SEQUENCES) {
		nvkm_error(subdev, "no free sequence available\n");
		mutex_unlock(&priv->seq_lock);
		return ERR_PTR(-EAGAIN);
	}

	set_bit(index, priv->seq_tbl);
	mutex_unlock(&priv->seq_lock);

	seq = &priv->seq[index];
	seq->state = SEQ_STATE_PENDING;
	return seq;
}

void
msgqueue_seq_release(struct nvkm_msgqueue *priv, struct nvkm_msgqueue_seq *seq)
{
	/* no need to acquire seq_lock since clear_bit is atomic */
	seq->state = SEQ_STATE_FREE;
	seq->callback = NULL;
	seq->completion = NULL;
	clear_bit(seq->id, priv->seq_tbl);
}

void
nvkm_falcon_qmgr_del(struct nvkm_falcon_qmgr **pqmgr)
{
	struct nvkm_falcon_qmgr *qmgr = *pqmgr;
	if (qmgr) {
		kfree(*pqmgr);
		*pqmgr = NULL;
	}
}

int
nvkm_falcon_qmgr_new(struct nvkm_falcon *falcon,
		     struct nvkm_falcon_qmgr **pqmgr)
{
	struct nvkm_falcon_qmgr *qmgr;

	if (!(qmgr = *pqmgr = kzalloc(sizeof(*qmgr), GFP_KERNEL)))
		return -ENOMEM;

	qmgr->falcon = falcon;
	return 0;
}
