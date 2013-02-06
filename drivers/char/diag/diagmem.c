/* Copyright (c) 2008-2010, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/mempool.h>
#include <linux/mutex.h>
#include <linux/atomic.h>
#include "diagchar.h"

void *diagmem_alloc(struct diagchar_dev *driver, int size, int pool_type)
{
	void *buf = NULL;

	if (pool_type == POOL_TYPE_COPY) {
		if (driver->diagpool) {
			mutex_lock(&driver->diagmem_mutex);
			if (driver->count < driver->poolsize) {
				atomic_add(1, (atomic_t *)&driver->count);
				buf = mempool_alloc(driver->diagpool,
								 GFP_ATOMIC);
			}
			mutex_unlock(&driver->diagmem_mutex);
		}
	} else if (pool_type == POOL_TYPE_HDLC) {
		if (driver->diag_hdlc_pool) {
			if (driver->count_hdlc_pool < driver->poolsize_hdlc) {
				atomic_add(1,
					 (atomic_t *)&driver->count_hdlc_pool);
				buf = mempool_alloc(driver->diag_hdlc_pool,
								 GFP_ATOMIC);
			}
		}
	} else if (pool_type == POOL_TYPE_WRITE_STRUCT) {
		if (driver->diag_write_struct_pool) {
			if (driver->count_write_struct_pool <
					 driver->poolsize_write_struct) {
				atomic_add(1,
				 (atomic_t *)&driver->count_write_struct_pool);
				buf = mempool_alloc(
				driver->diag_write_struct_pool, GFP_ATOMIC);
			}
		}
	}
	return buf;
}

void diagmem_exit(struct diagchar_dev *driver, int pool_type)
{
	if (driver->diagpool) {
		if (driver->count == 0 && driver->ref_count == 0) {
			mempool_destroy(driver->diagpool);
			driver->diagpool = NULL;
		} else if (driver->ref_count == 0 && pool_type == POOL_TYPE_ALL)
			printk(KERN_ALERT "Unable to destroy COPY mempool");
		}

	if (driver->diag_hdlc_pool) {
		if (driver->count_hdlc_pool == 0 && driver->ref_count == 0) {
			mempool_destroy(driver->diag_hdlc_pool);
			driver->diag_hdlc_pool = NULL;
		} else if (driver->ref_count == 0 && pool_type == POOL_TYPE_ALL)
			printk(KERN_ALERT "Unable to destroy HDLC mempool");
		}

	if (driver->diag_write_struct_pool) {
		/* Free up struct pool ONLY if there are no outstanding
		transactions(aggregation buffer) with USB */
		if (driver->count_write_struct_pool == 0 &&
		 driver->count_hdlc_pool == 0 && driver->ref_count == 0) {
			mempool_destroy(driver->diag_write_struct_pool);
			driver->diag_write_struct_pool = NULL;
		} else if (driver->ref_count == 0 && pool_type == POOL_TYPE_ALL)
			printk(KERN_ALERT "Unable to destroy STRUCT mempool");
		}
}

void diagmem_free(struct diagchar_dev *driver, void *buf, int pool_type)
{
	if (pool_type == POOL_TYPE_COPY) {
		if (driver->diagpool != NULL && driver->count > 0) {
			mempool_free(buf, driver->diagpool);
			atomic_add(-1, (atomic_t *)&driver->count);
		} else
			pr_err("diag: Attempt to free up DIAG driver "
	       "mempool memory which is already free %d", driver->count);
	} else if (pool_type == POOL_TYPE_HDLC) {
		if (driver->diag_hdlc_pool != NULL &&
			 driver->count_hdlc_pool > 0) {
			mempool_free(buf, driver->diag_hdlc_pool);
			atomic_add(-1, (atomic_t *)&driver->count_hdlc_pool);
		} else
			pr_err("diag: Attempt to free up DIAG driver "
	"HDLC mempool which is already free %d ", driver->count_hdlc_pool);
	} else if (pool_type == POOL_TYPE_WRITE_STRUCT) {
		if (driver->diag_write_struct_pool != NULL &&
			 driver->count_write_struct_pool > 0) {
			mempool_free(buf, driver->diag_write_struct_pool);
			atomic_add(-1,
				 (atomic_t *)&driver->count_write_struct_pool);
		} else
			pr_err("diag: Attempt to free up DIAG driver "
			   "USB structure mempool which is already free %d ",
				    driver->count_write_struct_pool);
	}

	diagmem_exit(driver, pool_type);
}

void diagmem_init(struct diagchar_dev *driver)
{
	mutex_init(&driver->diagmem_mutex);

	if (driver->count == 0)
		driver->diagpool = mempool_create_kmalloc_pool(
					driver->poolsize, driver->itemsize);

	if (driver->count_hdlc_pool == 0)
		driver->diag_hdlc_pool = mempool_create_kmalloc_pool(
				driver->poolsize_hdlc, driver->itemsize_hdlc);

	if (driver->count_write_struct_pool == 0)
		driver->diag_write_struct_pool = mempool_create_kmalloc_pool(
		driver->poolsize_write_struct, driver->itemsize_write_struct);

	if (!driver->diagpool)
		printk(KERN_INFO "Cannot allocate diag mempool\n");

	if (!driver->diag_hdlc_pool)
		printk(KERN_INFO "Cannot allocate diag HDLC mempool\n");

	if (!driver->diag_write_struct_pool)
		printk(KERN_INFO "Cannot allocate diag USB struct mempool\n");
}

