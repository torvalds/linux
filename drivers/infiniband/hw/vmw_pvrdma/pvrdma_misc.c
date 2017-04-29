/*
 * Copyright (c) 2012-2016 VMware, Inc.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of EITHER the GNU General Public License
 * version 2 as published by the Free Software Foundation or the BSD
 * 2-Clause License. This program is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; WITHOUT EVEN THE IMPLIED
 * WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License version 2 for more details at
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program available in the file COPYING in the main
 * directory of this source tree.
 *
 * The BSD 2-Clause License
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/bitmap.h>

#include "pvrdma.h"

int pvrdma_page_dir_init(struct pvrdma_dev *dev, struct pvrdma_page_dir *pdir,
			 u64 npages, bool alloc_pages)
{
	u64 i;

	if (npages > PVRDMA_PAGE_DIR_MAX_PAGES)
		return -EINVAL;

	memset(pdir, 0, sizeof(*pdir));

	pdir->dir = dma_alloc_coherent(&dev->pdev->dev, PAGE_SIZE,
				       &pdir->dir_dma, GFP_KERNEL);
	if (!pdir->dir)
		goto err;

	pdir->ntables = PVRDMA_PAGE_DIR_TABLE(npages - 1) + 1;
	pdir->tables = kcalloc(pdir->ntables, sizeof(*pdir->tables),
			       GFP_KERNEL);
	if (!pdir->tables)
		goto err;

	for (i = 0; i < pdir->ntables; i++) {
		pdir->tables[i] = dma_alloc_coherent(&dev->pdev->dev, PAGE_SIZE,
						(dma_addr_t *)&pdir->dir[i],
						GFP_KERNEL);
		if (!pdir->tables[i])
			goto err;
	}

	pdir->npages = npages;

	if (alloc_pages) {
		pdir->pages = kcalloc(npages, sizeof(*pdir->pages),
				      GFP_KERNEL);
		if (!pdir->pages)
			goto err;

		for (i = 0; i < pdir->npages; i++) {
			dma_addr_t page_dma;

			pdir->pages[i] = dma_alloc_coherent(&dev->pdev->dev,
							    PAGE_SIZE,
							    &page_dma,
							    GFP_KERNEL);
			if (!pdir->pages[i])
				goto err;

			pvrdma_page_dir_insert_dma(pdir, i, page_dma);
		}
	}

	return 0;

err:
	pvrdma_page_dir_cleanup(dev, pdir);

	return -ENOMEM;
}

static u64 *pvrdma_page_dir_table(struct pvrdma_page_dir *pdir, u64 idx)
{
	return pdir->tables[PVRDMA_PAGE_DIR_TABLE(idx)];
}

dma_addr_t pvrdma_page_dir_get_dma(struct pvrdma_page_dir *pdir, u64 idx)
{
	return pvrdma_page_dir_table(pdir, idx)[PVRDMA_PAGE_DIR_PAGE(idx)];
}

static void pvrdma_page_dir_cleanup_pages(struct pvrdma_dev *dev,
					  struct pvrdma_page_dir *pdir)
{
	if (pdir->pages) {
		u64 i;

		for (i = 0; i < pdir->npages && pdir->pages[i]; i++) {
			dma_addr_t page_dma = pvrdma_page_dir_get_dma(pdir, i);

			dma_free_coherent(&dev->pdev->dev, PAGE_SIZE,
					  pdir->pages[i], page_dma);
		}

		kfree(pdir->pages);
	}
}

static void pvrdma_page_dir_cleanup_tables(struct pvrdma_dev *dev,
					   struct pvrdma_page_dir *pdir)
{
	if (pdir->tables) {
		int i;

		pvrdma_page_dir_cleanup_pages(dev, pdir);

		for (i = 0; i < pdir->ntables; i++) {
			u64 *table = pdir->tables[i];

			if (table)
				dma_free_coherent(&dev->pdev->dev, PAGE_SIZE,
						  table, pdir->dir[i]);
		}

		kfree(pdir->tables);
	}
}

void pvrdma_page_dir_cleanup(struct pvrdma_dev *dev,
			     struct pvrdma_page_dir *pdir)
{
	if (pdir->dir) {
		pvrdma_page_dir_cleanup_tables(dev, pdir);
		dma_free_coherent(&dev->pdev->dev, PAGE_SIZE,
				  pdir->dir, pdir->dir_dma);
	}
}

int pvrdma_page_dir_insert_dma(struct pvrdma_page_dir *pdir, u64 idx,
			       dma_addr_t daddr)
{
	u64 *table;

	if (idx >= pdir->npages)
		return -EINVAL;

	table = pvrdma_page_dir_table(pdir, idx);
	table[PVRDMA_PAGE_DIR_PAGE(idx)] = daddr;

	return 0;
}

int pvrdma_page_dir_insert_umem(struct pvrdma_page_dir *pdir,
				struct ib_umem *umem, u64 offset)
{
	u64 i = offset;
	int j, entry;
	int ret = 0, len = 0;
	struct scatterlist *sg;

	if (offset >= pdir->npages)
		return -EINVAL;

	for_each_sg(umem->sg_head.sgl, sg, umem->nmap, entry) {
		len = sg_dma_len(sg) >> PAGE_SHIFT;
		for (j = 0; j < len; j++) {
			dma_addr_t addr = sg_dma_address(sg) +
					  (j << umem->page_shift);

			ret = pvrdma_page_dir_insert_dma(pdir, i, addr);
			if (ret)
				goto exit;

			i++;
		}
	}

exit:
	return ret;
}

int pvrdma_page_dir_insert_page_list(struct pvrdma_page_dir *pdir,
				     u64 *page_list,
				     int num_pages)
{
	int i;
	int ret;

	if (num_pages > pdir->npages)
		return -EINVAL;

	for (i = 0; i < num_pages; i++) {
		ret = pvrdma_page_dir_insert_dma(pdir, i, page_list[i]);
		if (ret)
			return ret;
	}

	return 0;
}

void pvrdma_qp_cap_to_ib(struct ib_qp_cap *dst, const struct pvrdma_qp_cap *src)
{
	dst->max_send_wr = src->max_send_wr;
	dst->max_recv_wr = src->max_recv_wr;
	dst->max_send_sge = src->max_send_sge;
	dst->max_recv_sge = src->max_recv_sge;
	dst->max_inline_data = src->max_inline_data;
}

void ib_qp_cap_to_pvrdma(struct pvrdma_qp_cap *dst, const struct ib_qp_cap *src)
{
	dst->max_send_wr = src->max_send_wr;
	dst->max_recv_wr = src->max_recv_wr;
	dst->max_send_sge = src->max_send_sge;
	dst->max_recv_sge = src->max_recv_sge;
	dst->max_inline_data = src->max_inline_data;
}

void pvrdma_gid_to_ib(union ib_gid *dst, const union pvrdma_gid *src)
{
	BUILD_BUG_ON(sizeof(union pvrdma_gid) != sizeof(union ib_gid));
	memcpy(dst, src, sizeof(*src));
}

void ib_gid_to_pvrdma(union pvrdma_gid *dst, const union ib_gid *src)
{
	BUILD_BUG_ON(sizeof(union pvrdma_gid) != sizeof(union ib_gid));
	memcpy(dst, src, sizeof(*src));
}

void pvrdma_global_route_to_ib(struct ib_global_route *dst,
			       const struct pvrdma_global_route *src)
{
	pvrdma_gid_to_ib(&dst->dgid, &src->dgid);
	dst->flow_label = src->flow_label;
	dst->sgid_index = src->sgid_index;
	dst->hop_limit = src->hop_limit;
	dst->traffic_class = src->traffic_class;
}

void ib_global_route_to_pvrdma(struct pvrdma_global_route *dst,
			       const struct ib_global_route *src)
{
	ib_gid_to_pvrdma(&dst->dgid, &src->dgid);
	dst->flow_label = src->flow_label;
	dst->sgid_index = src->sgid_index;
	dst->hop_limit = src->hop_limit;
	dst->traffic_class = src->traffic_class;
}

void pvrdma_ah_attr_to_rdma(struct rdma_ah_attr *dst,
			    const struct pvrdma_ah_attr *src)
{
	dst->type = RDMA_AH_ATTR_TYPE_ROCE;
	pvrdma_global_route_to_ib(rdma_ah_retrieve_grh(dst), &src->grh);
	rdma_ah_set_dlid(dst, src->dlid);
	rdma_ah_set_sl(dst, src->sl);
	rdma_ah_set_path_bits(dst, src->src_path_bits);
	rdma_ah_set_static_rate(dst, src->static_rate);
	rdma_ah_set_ah_flags(dst, src->ah_flags);
	rdma_ah_set_port_num(dst, src->port_num);
	memcpy(dst->roce.dmac, &src->dmac, ETH_ALEN);
}

void rdma_ah_attr_to_pvrdma(struct pvrdma_ah_attr *dst,
			    const struct rdma_ah_attr *src)
{
	ib_global_route_to_pvrdma(&dst->grh, rdma_ah_read_grh(src));
	dst->dlid = rdma_ah_get_dlid(src);
	dst->sl = rdma_ah_get_sl(src);
	dst->src_path_bits = rdma_ah_get_path_bits(src);
	dst->static_rate = rdma_ah_get_static_rate(src);
	dst->ah_flags = rdma_ah_get_ah_flags(src);
	dst->port_num = rdma_ah_get_port_num(src);
	memcpy(&dst->dmac, src->roce.dmac, sizeof(dst->dmac));
}
