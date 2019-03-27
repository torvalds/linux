/*
 * Copyright (c) 2012 Mellanox Technologies, Inc.  All rights reserved.
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

#include <config.h>

#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "mlx5.h"
#include "bitmap.h"

static int mlx5_bitmap_init(struct mlx5_bitmap *bitmap, uint32_t num,
			    uint32_t mask)
{
	bitmap->last = 0;
	bitmap->top  = 0;
	bitmap->max  = num;
	bitmap->avail = num;
	bitmap->mask = mask;
	bitmap->avail = bitmap->max;
	bitmap->table = calloc(BITS_TO_LONGS(bitmap->max), sizeof(uint32_t));
	if (!bitmap->table)
		return -ENOMEM;

	return 0;
}

static void bitmap_free_range(struct mlx5_bitmap *bitmap, uint32_t obj,
			      int cnt)
{
	int i;

	obj &= bitmap->max - 1;

	for (i = 0; i < cnt; i++)
		mlx5_clear_bit(obj + i, bitmap->table);
	bitmap->last = min(bitmap->last, obj);
	bitmap->top = (bitmap->top + bitmap->max) & bitmap->mask;
	bitmap->avail += cnt;
}

static int bitmap_empty(struct mlx5_bitmap *bitmap)
{
	return (bitmap->avail == bitmap->max) ? 1 : 0;
}

static int bitmap_avail(struct mlx5_bitmap *bitmap)
{
	return bitmap->avail;
}

static void mlx5_bitmap_cleanup(struct mlx5_bitmap *bitmap)
{
	if (bitmap->table)
		free(bitmap->table);
}

static void free_huge_mem(struct mlx5_hugetlb_mem *hmem)
{
	mlx5_bitmap_cleanup(&hmem->bitmap);
	if (shmdt(hmem->shmaddr) == -1)
		mlx5_dbg(stderr, MLX5_DBG_CONTIG, "%s\n", strerror(errno));
	shmctl(hmem->shmid, IPC_RMID, NULL);
	free(hmem);
}

static int mlx5_bitmap_alloc(struct mlx5_bitmap *bitmap)
{
	uint32_t obj;
	int ret;

	obj = mlx5_find_first_zero_bit(bitmap->table, bitmap->max);
	if (obj < bitmap->max) {
		mlx5_set_bit(obj, bitmap->table);
		bitmap->last = (obj + 1);
		if (bitmap->last == bitmap->max)
			bitmap->last = 0;
		obj |= bitmap->top;
		ret = obj;
	} else
		ret = -1;

	if (ret != -1)
		--bitmap->avail;

	return ret;
}

static uint32_t find_aligned_range(unsigned long *bitmap,
				   uint32_t start, uint32_t nbits,
				   int len, int alignment)
{
	uint32_t end, i;

again:
	start = align(start, alignment);

	while ((start < nbits) && mlx5_test_bit(start, bitmap))
		start += alignment;

	if (start >= nbits)
		return -1;

	end = start + len;
	if (end > nbits)
		return -1;

	for (i = start + 1; i < end; i++) {
		if (mlx5_test_bit(i, bitmap)) {
			start = i + 1;
			goto again;
		}
	}

	return start;
}

static int bitmap_alloc_range(struct mlx5_bitmap *bitmap, int cnt,
			      int align)
{
	uint32_t obj;
	int ret, i;

	if (cnt == 1 && align == 1)
		return mlx5_bitmap_alloc(bitmap);

	if (cnt > bitmap->max)
		return -1;

	obj = find_aligned_range(bitmap->table, bitmap->last,
				 bitmap->max, cnt, align);
	if (obj >= bitmap->max) {
		bitmap->top = (bitmap->top + bitmap->max) & bitmap->mask;
		obj = find_aligned_range(bitmap->table, 0, bitmap->max,
					 cnt, align);
	}

	if (obj < bitmap->max) {
		for (i = 0; i < cnt; i++)
			mlx5_set_bit(obj + i, bitmap->table);
		if (obj == bitmap->last) {
			bitmap->last = (obj + cnt);
			if (bitmap->last >= bitmap->max)
				bitmap->last = 0;
		}
		obj |= bitmap->top;
		ret = obj;
	} else
		ret = -1;

	if (ret != -1)
		bitmap->avail -= cnt;

	return obj;
}

static struct mlx5_hugetlb_mem *alloc_huge_mem(size_t size)
{
	struct mlx5_hugetlb_mem *hmem;
	size_t shm_len;

	hmem = malloc(sizeof(*hmem));
	if (!hmem)
		return NULL;

	shm_len = align(size, MLX5_SHM_LENGTH);
	hmem->shmid = shmget(IPC_PRIVATE, shm_len, SHM_HUGETLB | SHM_R | SHM_W);
	if (hmem->shmid == -1) {
		mlx5_dbg(stderr, MLX5_DBG_CONTIG, "%s\n", strerror(errno));
		goto out_free;
	}

	hmem->shmaddr = shmat(hmem->shmid, MLX5_SHM_ADDR, MLX5_SHMAT_FLAGS);
	if (hmem->shmaddr == (void *)-1) {
		mlx5_dbg(stderr, MLX5_DBG_CONTIG, "%s\n", strerror(errno));
		goto out_rmid;
	}

	if (mlx5_bitmap_init(&hmem->bitmap, shm_len / MLX5_Q_CHUNK_SIZE,
			     shm_len / MLX5_Q_CHUNK_SIZE - 1)) {
		mlx5_dbg(stderr, MLX5_DBG_CONTIG, "%s\n", strerror(errno));
		goto out_shmdt;
	}

	/*
	 * Marked to be destroyed when process detaches from shmget segment
	 */
	shmctl(hmem->shmid, IPC_RMID, NULL);

	return hmem;

out_shmdt:
	if (shmdt(hmem->shmaddr) == -1)
		mlx5_dbg(stderr, MLX5_DBG_CONTIG, "%s\n", strerror(errno));

out_rmid:
	shmctl(hmem->shmid, IPC_RMID, NULL);

out_free:
	free(hmem);
	return NULL;
}

static int alloc_huge_buf(struct mlx5_context *mctx, struct mlx5_buf *buf,
			  size_t size, int page_size)
{
	int found = 0;
	int nchunk;
	struct mlx5_hugetlb_mem *hmem;
	int ret;

	buf->length = align(size, MLX5_Q_CHUNK_SIZE);
	nchunk = buf->length / MLX5_Q_CHUNK_SIZE;

	mlx5_spin_lock(&mctx->hugetlb_lock);
	TAILQ_FOREACH(hmem, &mctx->hugetlb_list, entry) {
		if (bitmap_avail(&hmem->bitmap)) {
			buf->base = bitmap_alloc_range(&hmem->bitmap, nchunk, 1);
			if (buf->base != -1) {
				buf->hmem = hmem;
				found = 1;
				break;
			}
		}
	}
	mlx5_spin_unlock(&mctx->hugetlb_lock);

	if (!found) {
		hmem = alloc_huge_mem(buf->length);
		if (!hmem)
			return -1;

		buf->base = bitmap_alloc_range(&hmem->bitmap, nchunk, 1);
		if (buf->base == -1) {
			free_huge_mem(hmem);
			/* TBD: remove after proven stability */
			fprintf(stderr, "BUG: huge allocation\n");
			return -1;
		}

		buf->hmem = hmem;

		mlx5_spin_lock(&mctx->hugetlb_lock);
		if (bitmap_avail(&hmem->bitmap))
			TAILQ_INSERT_HEAD(&mctx->hugetlb_list, hmem, entry);
		else
			TAILQ_INSERT_TAIL(&mctx->hugetlb_list, hmem, entry);
		mlx5_spin_unlock(&mctx->hugetlb_lock);
	}

	buf->buf = hmem->shmaddr + buf->base * MLX5_Q_CHUNK_SIZE;

	ret = ibv_dontfork_range(buf->buf, buf->length);
	if (ret) {
		mlx5_dbg(stderr, MLX5_DBG_CONTIG, "\n");
		goto out_fork;
	}
	buf->type = MLX5_ALLOC_TYPE_HUGE;

	return 0;

out_fork:
	mlx5_spin_lock(&mctx->hugetlb_lock);
	bitmap_free_range(&hmem->bitmap, buf->base, nchunk);
	if (bitmap_empty(&hmem->bitmap)) {
		TAILQ_REMOVE(&mctx->hugetlb_list, hmem, entry);
		mlx5_spin_unlock(&mctx->hugetlb_lock);
		free_huge_mem(hmem);
	} else
		mlx5_spin_unlock(&mctx->hugetlb_lock);

	return -1;
}

static void free_huge_buf(struct mlx5_context *ctx, struct mlx5_buf *buf)
{
	int nchunk;

	nchunk = buf->length / MLX5_Q_CHUNK_SIZE;
	mlx5_spin_lock(&ctx->hugetlb_lock);
	bitmap_free_range(&buf->hmem->bitmap, buf->base, nchunk);
	if (bitmap_empty(&buf->hmem->bitmap)) {
		TAILQ_REMOVE(&ctx->hugetlb_list, buf->hmem, entry);
		mlx5_spin_unlock(&ctx->hugetlb_lock);
		free_huge_mem(buf->hmem);
	} else
		mlx5_spin_unlock(&ctx->hugetlb_lock);
}

int mlx5_alloc_prefered_buf(struct mlx5_context *mctx,
			    struct mlx5_buf *buf,
			    size_t size, int page_size,
			    enum mlx5_alloc_type type,
			    const char *component)
{
	int ret;

	/*
	 * Fallback mechanism priority:
	 *	huge pages
	 *	contig pages
	 *	default
	 */
	if (type == MLX5_ALLOC_TYPE_HUGE ||
	    type == MLX5_ALLOC_TYPE_PREFER_HUGE ||
	    type == MLX5_ALLOC_TYPE_ALL) {
		ret = alloc_huge_buf(mctx, buf, size, page_size);
		if (!ret)
			return 0;

		if (type == MLX5_ALLOC_TYPE_HUGE)
			return -1;

		mlx5_dbg(stderr, MLX5_DBG_CONTIG,
			 "Huge mode allocation failed, fallback to %s mode\n",
			 MLX5_ALLOC_TYPE_ALL ? "contig" : "default");
	}

	if (type == MLX5_ALLOC_TYPE_CONTIG ||
	    type == MLX5_ALLOC_TYPE_PREFER_CONTIG ||
	    type == MLX5_ALLOC_TYPE_ALL) {
		ret = mlx5_alloc_buf_contig(mctx, buf, size, page_size, component);
		if (!ret)
			return 0;

		if (type == MLX5_ALLOC_TYPE_CONTIG)
			return -1;
		mlx5_dbg(stderr, MLX5_DBG_CONTIG,
			 "Contig allocation failed, fallback to default mode\n");
	}

	return mlx5_alloc_buf(buf, size, page_size);

}

int mlx5_free_actual_buf(struct mlx5_context *ctx, struct mlx5_buf *buf)
{
	int err = 0;

	switch (buf->type) {
	case MLX5_ALLOC_TYPE_ANON:
		mlx5_free_buf(buf);
		break;

	case MLX5_ALLOC_TYPE_HUGE:
		free_huge_buf(ctx, buf);
		break;

	case MLX5_ALLOC_TYPE_CONTIG:
		mlx5_free_buf_contig(ctx, buf);
		break;
	default:
		fprintf(stderr, "Bad allocation type\n");
	}

	return err;
}

/* This function computes log2(v) rounded up.
   We don't want to have a dependency to libm which exposes ceil & log2 APIs.
   Code was written based on public domain code:
	URL: http://graphics.stanford.edu/~seander/bithacks.html#IntegerLog.
*/
static uint32_t mlx5_get_block_order(uint32_t v)
{
	static const uint32_t bits_arr[] = {0x2, 0xC, 0xF0, 0xFF00, 0xFFFF0000};
	static const uint32_t shift_arr[] = {1, 2, 4, 8, 16};
	int i;
	uint32_t input_val = v;

	register uint32_t r = 0;/* result of log2(v) will go here */
	for (i = 4; i >= 0; i--) {
		if (v & bits_arr[i]) {
			v >>= shift_arr[i];
			r |= shift_arr[i];
		}
	}
	/* Rounding up if required */
	r += !!(input_val & ((1 << r) - 1));

	return r;
}

void mlx5_get_alloc_type(const char *component,
			 enum mlx5_alloc_type *alloc_type,
			 enum mlx5_alloc_type default_type)

{
	char *env_value;
	char name[128];

	snprintf(name, sizeof(name), "%s_ALLOC_TYPE", component);

	*alloc_type = default_type;

	env_value = getenv(name);
	if (env_value) {
		if (!strcasecmp(env_value, "ANON"))
			*alloc_type = MLX5_ALLOC_TYPE_ANON;
		else if (!strcasecmp(env_value, "HUGE"))
			*alloc_type = MLX5_ALLOC_TYPE_HUGE;
		else if (!strcasecmp(env_value, "CONTIG"))
			*alloc_type = MLX5_ALLOC_TYPE_CONTIG;
		else if (!strcasecmp(env_value, "PREFER_CONTIG"))
			*alloc_type = MLX5_ALLOC_TYPE_PREFER_CONTIG;
		else if (!strcasecmp(env_value, "PREFER_HUGE"))
			*alloc_type = MLX5_ALLOC_TYPE_PREFER_HUGE;
		else if (!strcasecmp(env_value, "ALL"))
			*alloc_type = MLX5_ALLOC_TYPE_ALL;
	}
}

static void mlx5_alloc_get_env_info(int *max_block_log,
				    int *min_block_log,
				    const char *component)

{
	char *env;
	int value;
	char name[128];

	/* First set defaults */
	*max_block_log = MLX5_MAX_LOG2_CONTIG_BLOCK_SIZE;
	*min_block_log = MLX5_MIN_LOG2_CONTIG_BLOCK_SIZE;

	snprintf(name, sizeof(name), "%s_MAX_LOG2_CONTIG_BSIZE", component);
	env = getenv(name);
	if (env) {
		value = atoi(env);
		if (value <= MLX5_MAX_LOG2_CONTIG_BLOCK_SIZE &&
		    value >= MLX5_MIN_LOG2_CONTIG_BLOCK_SIZE)
			*max_block_log = value;
		else
			fprintf(stderr, "Invalid value %d for %s\n",
				value, name);
	}
	sprintf(name, "%s_MIN_LOG2_CONTIG_BSIZE", component);
	env = getenv(name);
	if (env) {
		value = atoi(env);
		if (value >= MLX5_MIN_LOG2_CONTIG_BLOCK_SIZE &&
		    value  <=  *max_block_log)
			*min_block_log = value;
		else
			fprintf(stderr, "Invalid value %d for %s\n",
				value, name);
	}
}

int mlx5_alloc_buf_contig(struct mlx5_context *mctx,
			  struct mlx5_buf *buf, size_t size,
			  int page_size,
			  const char *component)
{
	void *addr = MAP_FAILED;
	int block_size_exp;
	int max_block_log;
	int min_block_log;
	struct ibv_context *context = &mctx->ibv_ctx;
	off_t offset;

	mlx5_alloc_get_env_info(&max_block_log,
				&min_block_log,
				component);

	block_size_exp = mlx5_get_block_order(size);

	if (block_size_exp > max_block_log)
		block_size_exp = max_block_log;

	do {
		offset = 0;
		set_command(MLX5_MMAP_GET_CONTIGUOUS_PAGES_CMD, &offset);
		set_order(block_size_exp, &offset);
		addr = mmap(NULL , size, PROT_WRITE | PROT_READ, MAP_SHARED,
			    context->cmd_fd, page_size * offset);
		if (addr != MAP_FAILED)
			break;

		/*
		 *  The kernel returns EINVAL if not supported
		 */
		if (errno == EINVAL)
			return -1;

		block_size_exp -= 1;
	} while (block_size_exp >= min_block_log);
	mlx5_dbg(mctx->dbg_fp, MLX5_DBG_CONTIG, "block order %d, addr %p\n",
		 block_size_exp, addr);

	if (addr == MAP_FAILED)
		return -1;

	if (ibv_dontfork_range(addr, size)) {
		munmap(addr, size);
		return -1;
	}

	buf->buf = addr;
	buf->length = size;
	buf->type = MLX5_ALLOC_TYPE_CONTIG;

	return 0;
}

void mlx5_free_buf_contig(struct mlx5_context *mctx, struct mlx5_buf *buf)
{
	ibv_dofork_range(buf->buf, buf->length);
	munmap(buf->buf, buf->length);
}

int mlx5_alloc_buf(struct mlx5_buf *buf, size_t size, int page_size)
{
	int ret;
	int al_size;

	al_size = align(size, page_size);
	ret = posix_memalign(&buf->buf, page_size, al_size);
	if (ret)
		return ret;

	ret = ibv_dontfork_range(buf->buf, al_size);
	if (ret)
		free(buf->buf);

	if (!ret) {
		buf->length = al_size;
		buf->type = MLX5_ALLOC_TYPE_ANON;
	}

	return ret;
}

void mlx5_free_buf(struct mlx5_buf *buf)
{
	ibv_dofork_range(buf->buf, buf->length);
	free(buf->buf);
}
