/*
 * Tegra host1x Memory Management Abstraction header
 *
 * Copyright (c) 2012-2013, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _HOST1X_BO_H
#define _HOST1X_BO_H

struct host1x_bo;

struct host1x_bo_ops {
	struct host1x_bo *(*get)(struct host1x_bo *bo);
	void (*put)(struct host1x_bo *bo);
	dma_addr_t (*pin)(struct host1x_bo *bo, struct sg_table **sgt);
	void (*unpin)(struct host1x_bo *bo, struct sg_table *sgt);
	void *(*mmap)(struct host1x_bo *bo);
	void (*munmap)(struct host1x_bo *bo, void *addr);
	void *(*kmap)(struct host1x_bo *bo, unsigned int pagenum);
	void (*kunmap)(struct host1x_bo *bo, unsigned int pagenum, void *addr);
};

struct host1x_bo {
	const struct host1x_bo_ops *ops;
};

static inline void host1x_bo_init(struct host1x_bo *bo,
				  const struct host1x_bo_ops *ops)
{
	bo->ops = ops;
}

static inline struct host1x_bo *host1x_bo_get(struct host1x_bo *bo)
{
	return bo->ops->get(bo);
}

static inline void host1x_bo_put(struct host1x_bo *bo)
{
	bo->ops->put(bo);
}

static inline dma_addr_t host1x_bo_pin(struct host1x_bo *bo,
				       struct sg_table **sgt)
{
	return bo->ops->pin(bo, sgt);
}

static inline void host1x_bo_unpin(struct host1x_bo *bo, struct sg_table *sgt)
{
	bo->ops->unpin(bo, sgt);
}

static inline void *host1x_bo_mmap(struct host1x_bo *bo)
{
	return bo->ops->mmap(bo);
}

static inline void host1x_bo_munmap(struct host1x_bo *bo, void *addr)
{
	bo->ops->munmap(bo, addr);
}

static inline void *host1x_bo_kmap(struct host1x_bo *bo, unsigned int pagenum)
{
	return bo->ops->kmap(bo, pagenum);
}

static inline void host1x_bo_kunmap(struct host1x_bo *bo,
				    unsigned int pagenum, void *addr)
{
	bo->ops->kunmap(bo, pagenum, addr);
}

#endif
