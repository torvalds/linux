/*
 * Copyright (c) 2007 Mellanox Technologies. All rights reserved.
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
 *
 */

#include <linux/vmalloc.h>
#include <linux/mlx4/qp.h>

#include "mlx4_en.h"

void mlx4_en_fill_qp_context(struct mlx4_en_priv *priv, int size, int stride,
			     int is_tx, int rss, int qpn, int cqn, int srqn,
			     struct mlx4_qp_context *context)
{
	struct mlx4_en_dev *mdev = priv->mdev;

	memset(context, 0, sizeof *context);
	context->flags = cpu_to_be32(7 << 16 | rss << 13);
	context->pd = cpu_to_be32(mdev->priv_pdn);
	context->mtu_msgmax = 0xff;
	context->rq_size_stride = 0;
	if (is_tx)
		context->sq_size_stride = ilog2(size) << 3 | (ilog2(stride) - 4);
	else
		context->sq_size_stride = 1;
	context->usr_page = cpu_to_be32(mdev->priv_uar.index);
	context->local_qpn = cpu_to_be32(qpn);
	context->pri_path.ackto = 1 & 0x07;
	context->pri_path.sched_queue = 0x83 | (priv->port - 1) << 6;
	context->pri_path.counter_index = 0xff;
	context->cqn_send = cpu_to_be32(cqn);
	context->cqn_recv = cpu_to_be32(cqn);
	context->db_rec_addr = cpu_to_be64(priv->res.db.dma << 2);
	if (!rss)
		context->srqn = cpu_to_be32(MLX4_EN_USE_SRQ | srqn);
}


int mlx4_en_map_buffer(struct mlx4_buf *buf)
{
	struct page **pages;
	int i;

	if (BITS_PER_LONG == 64 || buf->nbufs == 1)
		return 0;

	pages = kmalloc(sizeof *pages * buf->nbufs, GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	for (i = 0; i < buf->nbufs; ++i)
		pages[i] = virt_to_page(buf->page_list[i].buf);

	buf->direct.buf = vmap(pages, buf->nbufs, VM_MAP, PAGE_KERNEL);
	kfree(pages);
	if (!buf->direct.buf)
		return -ENOMEM;

	return 0;
}

void mlx4_en_unmap_buffer(struct mlx4_buf *buf)
{
	if (BITS_PER_LONG == 64 || buf->nbufs == 1)
		return;

	vunmap(buf->direct.buf);
}
