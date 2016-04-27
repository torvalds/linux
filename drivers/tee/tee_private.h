/*
 * Copyright (c) 2015-2016, Linaro Limited
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#ifndef TEE_PRIVATE_H
#define TEE_PRIVATE_H

#include <linux/cdev.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/kref.h>
#include <linux/mutex.h>
#include <linux/types.h>

struct tee_device;

/**
 * struct tee_shm - shared memory object
 * @teedev:	device used to allocate the object
 * @paddr:	physical address of the shared memory
 * @kaddr:	virtual address of the shared memory
 * @size:	size of shared memory
 * @dmabuf:	dmabuf used to for exporting to user space
 * @flags:	defined by TEE_SHM_* in tee_drv.h
 * @id:		unique id of a shared memory object on this device
 */
struct tee_shm {
	struct tee_device *teedev;
	phys_addr_t paddr;
	void *kaddr;
	size_t size;
	struct dma_buf *dmabuf;
	u32 flags;
	int id;
};

struct tee_shm_pool_mgr;

/**
 * struct tee_shm_pool_mgr_ops - shared memory pool manager operations
 * @alloc:	called when allocating shared memory
 * @free:	called when freeing shared memory
 */
struct tee_shm_pool_mgr_ops {
	int (*alloc)(struct tee_shm_pool_mgr *poolmgr, struct tee_shm *shm,
		     size_t size);
	void (*free)(struct tee_shm_pool_mgr *poolmgr, struct tee_shm *shm);
};

/**
 * struct tee_shm_pool_mgr - shared memory manager
 * @ops:		operations
 * @private_data:	private data for the shared memory manager
 */
struct tee_shm_pool_mgr {
	const struct tee_shm_pool_mgr_ops *ops;
	void *private_data;
};

/**
 * struct tee_shm_pool - shared memory pool
 * @private_mgr:	pool manager for shared memory only between kernel
 *			and secure world
 * @dma_buf_mgr:	pool manager for shared memory exported to user space
 * @destroy:		called when destroying the pool
 * @private_data:	private data for the pool
 */
struct tee_shm_pool {
	struct tee_shm_pool_mgr private_mgr;
	struct tee_shm_pool_mgr dma_buf_mgr;
	void (*destroy)(struct tee_shm_pool *pool);
	void *private_data;
};

#define TEE_DEVICE_FLAG_REGISTERED	0x1
#define TEE_MAX_DEV_NAME_LEN		32

/**
 * struct tee_device - TEE Device representation
 * @name:	name of device
 * @desc:	description of device
 * @id:		unique id of device
 * @flags:	represented by TEE_DEVICE_FLAG_REGISTERED above
 * @dev:	embedded basic device structure
 * @cdev:	embedded cdev
 * @num_users:	number of active users of this device
 * @c_no_user:	completion used when unregistering the device
 * @mutex:	mutex protecting @num_users and @idr
 * @idr:	register of shared memory object allocated on this device
 * @pool:	shared memory pool
 */
struct tee_device {
	char name[TEE_MAX_DEV_NAME_LEN];
	const struct tee_desc *desc;
	int id;
	unsigned int flags;

	struct device dev;
	struct cdev cdev;

	size_t num_users;
	struct completion c_no_users;
	struct mutex mutex;	/* protects num_users and idr */

	struct idr idr;
	struct tee_shm_pool *pool;
};

int tee_shm_init(void);

int tee_shm_get_fd(struct tee_shm *shm);

bool tee_device_get(struct tee_device *teedev);
void tee_device_put(struct tee_device *teedev);

#endif /*TEE_PRIVATE_H*/
