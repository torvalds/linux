/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Rockchip Electronics Co.Ltd
 * Author: Felix Zeng <felix.zeng@rock-chips.com>
 */

#ifndef __LINUX_RKNPU_MM_H
#define __LINUX_RKNPU_MM_H

#include <linux/mutex.h>
#include <linux/seq_file.h>
#include <linux/iommu.h>
#include <linux/iova.h>

#include "rknpu_drv.h"

struct rknpu_mm {
	void *bitmap;
	struct mutex lock;
	unsigned int chunk_size;
	unsigned int total_chunks;
	unsigned int free_chunks;
};

struct rknpu_mm_obj {
	uint32_t range_start;
	uint32_t range_end;
};

int rknpu_mm_create(unsigned int mem_size, unsigned int chunk_size,
		    struct rknpu_mm **mm);

void rknpu_mm_destroy(struct rknpu_mm *mm);

int rknpu_mm_alloc(struct rknpu_mm *mm, unsigned int size,
		   struct rknpu_mm_obj **mm_obj);

int rknpu_mm_free(struct rknpu_mm *mm, struct rknpu_mm_obj *mm_obj);

int rknpu_mm_dump(struct seq_file *m, void *data);

#endif
