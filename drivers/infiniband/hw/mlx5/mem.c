/*
 * Copyright (c) 2013, Mellanox Technologies inc.  All rights reserved.
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

#include <linux/module.h>
#include <rdma/ib_umem.h>
#include "mlx5_ib.h"

/* @umem: umem object to scan
 * @addr: ib virtual address requested by the user
 * @count: number of PAGE_SIZE pages covered by umem
 * @shift: page shift for the compound pages found in the region
 * @ncont: number of compund pages
 * @order: log2 of the number of compound pages
 */
void mlx5_ib_cont_pages(struct ib_umem *umem, u64 addr, int *count, int *shift,
			int *ncont, int *order)
{
	unsigned long tmp;
	unsigned long m;
	int i, k;
	u64 base = 0;
	int p = 0;
	int skip;
	int mask;
	u64 len;
	u64 pfn;
	struct scatterlist *sg;
	int entry;

	addr = addr >> PAGE_SHIFT;
	tmp = (unsigned long)addr;
	m = find_first_bit(&tmp, sizeof(tmp));
	skip = 1 << m;
	mask = skip - 1;
	i = 0;
	for_each_sg(umem->sg_head.sgl, sg, umem->nmap, entry) {
		len = sg_dma_len(sg) >> PAGE_SHIFT;
		pfn = sg_dma_address(sg) >> PAGE_SHIFT;
		for (k = 0; k < len; k++) {
			if (!(i & mask)) {
				tmp = (unsigned long)pfn;
				m = min(m, find_first_bit(&tmp, sizeof(tmp)));
				skip = 1 << m;
				mask = skip - 1;
				base = pfn;
				p = 0;
			} else {
				if (base + p != pfn) {
					tmp = (unsigned long)p;
					m = find_first_bit(&tmp, sizeof(tmp));
					skip = 1 << m;
					mask = skip - 1;
					base = pfn;
					p = 0;
				}
			}
			p++;
			i++;
		}
	}

	if (i) {
		m = min_t(unsigned long, ilog2(roundup_pow_of_two(i)), m);

		if (order)
			*order = ilog2(roundup_pow_of_two(i) >> m);

		*ncont = DIV_ROUND_UP(i, (1 << m));
	} else {
		m  = 0;

		if (order)
			*order = 0;

		*ncont = 0;
	}
	*shift = PAGE_SHIFT + m;
	*count = i;
}

void mlx5_ib_populate_pas(struct mlx5_ib_dev *dev, struct ib_umem *umem,
			  int page_shift, __be64 *pas, int umr)
{
	int shift = page_shift - PAGE_SHIFT;
	int mask = (1 << shift) - 1;
	int i, k;
	u64 cur = 0;
	u64 base;
	int len;
	struct scatterlist *sg;
	int entry;

	i = 0;
	for_each_sg(umem->sg_head.sgl, sg, umem->nmap, entry) {
		len = sg_dma_len(sg) >> PAGE_SHIFT;
		base = sg_dma_address(sg);
		for (k = 0; k < len; k++) {
			if (!(i & mask)) {
				cur = base + (k << PAGE_SHIFT);
				if (umr)
					cur |= 3;

				pas[i >> shift] = cpu_to_be64(cur);
				mlx5_ib_dbg(dev, "pas[%d] 0x%llx\n",
					    i >> shift, be64_to_cpu(pas[i >> shift]));
			}  else
				mlx5_ib_dbg(dev, "=====> 0x%llx\n",
					    base + (k << PAGE_SHIFT));
			i++;
		}
	}
}

int mlx5_ib_get_buf_offset(u64 addr, int page_shift, u32 *offset)
{
	u64 page_size;
	u64 page_mask;
	u64 off_size;
	u64 off_mask;
	u64 buf_off;

	page_size = 1 << page_shift;
	page_mask = page_size - 1;
	buf_off = addr & page_mask;
	off_size = page_size >> 6;
	off_mask = off_size - 1;

	if (buf_off & off_mask)
		return -EINVAL;

	*offset = buf_off >> ilog2(off_size);
	return 0;
}
