/*
 * Copyright (c) 2007 Cisco Systems, Inc. All rights reserved.
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

#include "mlx4_ib.h"

static u32 convert_access(int acc)
{
	return (acc & IB_ACCESS_REMOTE_ATOMIC ? MLX4_PERM_ATOMIC       : 0) |
	       (acc & IB_ACCESS_REMOTE_WRITE  ? MLX4_PERM_REMOTE_WRITE : 0) |
	       (acc & IB_ACCESS_REMOTE_READ   ? MLX4_PERM_REMOTE_READ  : 0) |
	       (acc & IB_ACCESS_LOCAL_WRITE   ? MLX4_PERM_LOCAL_WRITE  : 0) |
	       MLX4_PERM_LOCAL_READ;
}

struct ib_mr *mlx4_ib_get_dma_mr(struct ib_pd *pd, int acc)
{
	struct mlx4_ib_mr *mr;
	int err;

	mr = kmalloc(sizeof *mr, GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	err = mlx4_mr_alloc(to_mdev(pd->device)->dev, to_mpd(pd)->pdn, 0,
			    ~0ull, convert_access(acc), 0, 0, &mr->mmr);
	if (err)
		goto err_free;

	err = mlx4_mr_enable(to_mdev(pd->device)->dev, &mr->mmr);
	if (err)
		goto err_mr;

	mr->ibmr.rkey = mr->ibmr.lkey = mr->mmr.key;
	mr->umem = NULL;

	return &mr->ibmr;

err_mr:
	mlx4_mr_free(to_mdev(pd->device)->dev, &mr->mmr);

err_free:
	kfree(mr);

	return ERR_PTR(err);
}

int mlx4_ib_umem_write_mtt(struct mlx4_ib_dev *dev, struct mlx4_mtt *mtt,
			   struct ib_umem *umem)
{
	u64 *pages;
	struct ib_umem_chunk *chunk;
	int i, j, k;
	int n;
	int len;
	int err = 0;

	pages = (u64 *) __get_free_page(GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	i = n = 0;

	list_for_each_entry(chunk, &umem->chunk_list, list)
		for (j = 0; j < chunk->nmap; ++j) {
			len = sg_dma_len(&chunk->page_list[j]) >> mtt->page_shift;
			for (k = 0; k < len; ++k) {
				pages[i++] = sg_dma_address(&chunk->page_list[j]) +
					umem->page_size * k;
				/*
				 * Be friendly to WRITE_MTT firmware
				 * command, and pass it chunks of
				 * appropriate size.
				 */
				if (i == PAGE_SIZE / sizeof (u64) - 2) {
					err = mlx4_write_mtt(dev->dev, mtt, n,
							     i, pages);
					if (err)
						goto out;
					n += i;
					i = 0;
				}
			}
		}

	if (i)
		err = mlx4_write_mtt(dev->dev, mtt, n, i, pages);

out:
	free_page((unsigned long) pages);
	return err;
}

struct ib_mr *mlx4_ib_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
				  u64 virt_addr, int access_flags,
				  struct ib_udata *udata)
{
	struct mlx4_ib_dev *dev = to_mdev(pd->device);
	struct mlx4_ib_mr *mr;
	int shift;
	int err;
	int n;

	mr = kmalloc(sizeof *mr, GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	mr->umem = ib_umem_get(pd->uobject->context, start, length, access_flags);
	if (IS_ERR(mr->umem)) {
		err = PTR_ERR(mr->umem);
		goto err_free;
	}

	n = ib_umem_page_count(mr->umem);
	shift = ilog2(mr->umem->page_size);

	err = mlx4_mr_alloc(dev->dev, to_mpd(pd)->pdn, virt_addr, length,
			    convert_access(access_flags), n, shift, &mr->mmr);
	if (err)
		goto err_umem;

	err = mlx4_ib_umem_write_mtt(dev, &mr->mmr.mtt, mr->umem);
	if (err)
		goto err_mr;

	err = mlx4_mr_enable(dev->dev, &mr->mmr);
	if (err)
		goto err_mr;

	mr->ibmr.rkey = mr->ibmr.lkey = mr->mmr.key;

	return &mr->ibmr;

err_mr:
	mlx4_mr_free(to_mdev(pd->device)->dev, &mr->mmr);

err_umem:
	ib_umem_release(mr->umem);

err_free:
	kfree(mr);

	return ERR_PTR(err);
}

int mlx4_ib_dereg_mr(struct ib_mr *ibmr)
{
	struct mlx4_ib_mr *mr = to_mmr(ibmr);

	mlx4_mr_free(to_mdev(ibmr->device)->dev, &mr->mmr);
	if (mr->umem)
		ib_umem_release(mr->umem);
	kfree(mr);

	return 0;
}
