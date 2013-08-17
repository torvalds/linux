/*
 * Samsung Exynos5 SoC series FIMC-IS driver
 *
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef FIMC_IS_MEM_H
#define FIMC_IS_MEM_H

struct fimc_is_minfo {
	dma_addr_t	base;		/* buffer base */
	size_t		size;		/* total length */
	dma_addr_t	vaddr_base;	/* buffer base */
	dma_addr_t	vaddr_curr;	/* current addr */
	void		*bitproc_buf;
	void		*fw_cookie;

	u32		dvaddr;
	u32		kvaddr;
	u32		dvaddr_debug;
	u32		kvaddr_debug;
	u32		dvaddr_fshared;
	u32		kvaddr_fshared;
	u32		dvaddr_region;
	u32		kvaddr_region;
	u32		dvaddr_shared; /*shared region of is region*/
	u32		kvaddr_shared;
	u32		dvaddr_odc;
	u32		kvaddr_odc;
	u32		dvaddr_dis;
	u32		kvaddr_dis;
	u32		dvaddr_3dnr;
	u32		kvaddr_3dnr;
};

struct fimc_is_vb2 {
	const struct vb2_mem_ops *ops;
	void *(*init)(struct platform_device *pdev);
	void (*cleanup)(void *alloc_ctx);

	unsigned long (*plane_addr)(struct vb2_buffer *vb, u32 plane_no);
	unsigned long (*plane_kvaddr)(struct vb2_buffer *vb, u32 plane_no);

	int (*resume)(void *alloc_ctx);
	void (*suspend)(void *alloc_ctx);

	void (*set_cacheable)(void *alloc_ctx, bool cacheable);
};

struct fimc_is_mem {
	struct platform_device		*pdev;
	struct vb2_alloc_ctx		*alloc_ctx;

	const struct fimc_is_vb2	*vb2;
};

int fimc_is_mem_probe(struct fimc_is_mem *this,
	struct platform_device *pdev);

#endif
