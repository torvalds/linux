/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015-2016, Linaro Limited
 */
#ifndef TEE_PRIVATE_H
#define TEE_PRIVATE_H

#include <linux/cdev.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/kref.h>
#include <linux/mutex.h>
#include <linux/types.h>

/* extra references appended to shm object for registered shared memory */
struct tee_shm_dmabuf_ref {
	struct tee_shm shm;
	size_t offset;
	struct dma_buf *dmabuf;
	struct tee_shm *parent_shm;
};

int tee_shm_get_fd(struct tee_shm *shm);

struct tee_shm *tee_shm_alloc_user_buf(struct tee_context *ctx, size_t size);
struct tee_shm *tee_shm_register_user_buf(struct tee_context *ctx,
					  unsigned long addr, size_t length);

int tee_heap_update_from_dma_buf(struct tee_device *teedev,
				 struct dma_buf *dmabuf, size_t *offset,
				 struct tee_shm *shm,
				 struct tee_shm **parent_shm);

#endif /*TEE_PRIVATE_H*/
