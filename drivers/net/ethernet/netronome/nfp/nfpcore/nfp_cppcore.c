/*
 * Copyright (C) 2015-2017 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
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

/*
 * nfp_cppcore.c
 * Provides low-level access to the NFP's internal CPP bus
 * Authors: Jakub Kicinski <jakub.kicinski@netronome.com>
 *          Jason McMullan <jason.mcmullan@netronome.com>
 *          Rolf Neugebauer <rolf.neugebauer@netronome.com>
 */

#include <asm/unaligned.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/wait.h>

#include "nfp_arm.h"
#include "nfp_cpp.h"
#include "nfp6000/nfp6000.h"

#define NFP_ARM_GCSR_SOFTMODEL2                              0x0000014c
#define NFP_ARM_GCSR_SOFTMODEL3                              0x00000150

struct nfp_cpp_resource {
	struct list_head list;
	const char *name;
	u32 cpp_id;
	u64 start;
	u64 end;
};

struct nfp_cpp_mutex {
	struct list_head list;
	struct nfp_cpp *cpp;
	int target;
	u16 usage;
	u16 depth;
	unsigned long long address;
	u32 key;
};

struct nfp_cpp {
	struct device dev;

	void *priv; /* Private data of the low-level implementation */

	u32 model;
	u16 interface;
	u8 serial[NFP_SERIAL_LEN];

	const struct nfp_cpp_operations *op;
	struct list_head resource_list;	/* NFP CPP resource list */
	struct list_head mutex_cache;	/* Mutex cache */
	rwlock_t resource_lock;
	wait_queue_head_t waitq;

	/* NFP6000 CPP Mapping Table */
	u32 imb_cat_table[16];

	/* Cached areas for cpp/xpb readl/writel speedups */
	struct mutex area_cache_mutex;  /* Lock for the area cache */
	struct list_head area_cache_list;

	/* Cached information */
	void *hwinfo;
	void *rtsym;
};

/* Element of the area_cache_list */
struct nfp_cpp_area_cache {
	struct list_head entry;
	u32 id;
	u64 addr;
	u32 size;
	struct nfp_cpp_area *area;
};

struct nfp_cpp_area {
	struct nfp_cpp *cpp;
	struct kref kref;
	atomic_t refcount;
	struct mutex mutex;	/* Lock for the area's refcount */
	unsigned long long offset;
	unsigned long size;
	struct nfp_cpp_resource resource;
	void __iomem *iomem;
	/* Here follows the 'priv' part of nfp_cpp_area. */
};

struct nfp_cpp_explicit {
	struct nfp_cpp *cpp;
	struct nfp_cpp_explicit_command cmd;
	/* Here follows the 'priv' part of nfp_cpp_area. */
};

static void __resource_add(struct list_head *head, struct nfp_cpp_resource *res)
{
	struct nfp_cpp_resource *tmp;
	struct list_head *pos;

	list_for_each(pos, head) {
		tmp = container_of(pos, struct nfp_cpp_resource, list);

		if (tmp->cpp_id > res->cpp_id)
			break;

		if (tmp->cpp_id == res->cpp_id && tmp->start > res->start)
			break;
	}

	list_add_tail(&res->list, pos);
}

static void __resource_del(struct nfp_cpp_resource *res)
{
	list_del_init(&res->list);
}

static void __release_cpp_area(struct kref *kref)
{
	struct nfp_cpp_area *area =
		container_of(kref, struct nfp_cpp_area, kref);
	struct nfp_cpp *cpp = nfp_cpp_area_cpp(area);

	if (area->cpp->op->area_cleanup)
		area->cpp->op->area_cleanup(area);

	write_lock(&cpp->resource_lock);
	__resource_del(&area->resource);
	write_unlock(&cpp->resource_lock);
	kfree(area);
}

static void nfp_cpp_area_put(struct nfp_cpp_area *area)
{
	kref_put(&area->kref, __release_cpp_area);
}

static struct nfp_cpp_area *nfp_cpp_area_get(struct nfp_cpp_area *area)
{
	kref_get(&area->kref);

	return area;
}

/**
 * nfp_cpp_free() - free the CPP handle
 * @cpp:	CPP handle
 */
void nfp_cpp_free(struct nfp_cpp *cpp)
{
	struct nfp_cpp_area_cache *cache, *ctmp;
	struct nfp_cpp_resource *res, *rtmp;
	struct nfp_cpp_mutex *mutex, *mtmp;

	/* There should be no mutexes in the cache at this point. */
	WARN_ON(!list_empty(&cpp->mutex_cache));
	/* .. but if there are, unlock them and complain. */
	list_for_each_entry_safe(mutex, mtmp, &cpp->mutex_cache, list) {
		dev_err(cpp->dev.parent, "Dangling mutex: @%d::0x%llx, %d locks held by %d owners\n",
			mutex->target, (unsigned long long)mutex->address,
			mutex->depth, mutex->usage);

		/* Forcing an unlock */
		mutex->depth = 1;
		nfp_cpp_mutex_unlock(mutex);

		/* Forcing a free */
		mutex->usage = 1;
		nfp_cpp_mutex_free(mutex);
	}

	/* Remove all caches */
	list_for_each_entry_safe(cache, ctmp, &cpp->area_cache_list, entry) {
		list_del(&cache->entry);
		if (cache->id)
			nfp_cpp_area_release(cache->area);
		nfp_cpp_area_free(cache->area);
		kfree(cache);
	}

	/* There should be no dangling areas at this point */
	WARN_ON(!list_empty(&cpp->resource_list));

	/* .. but if they weren't, try to clean up. */
	list_for_each_entry_safe(res, rtmp, &cpp->resource_list, list) {
		struct nfp_cpp_area *area = container_of(res,
							 struct nfp_cpp_area,
							 resource);

		dev_err(cpp->dev.parent, "Dangling area: %d:%d:%d:0x%0llx-0x%0llx%s%s\n",
			NFP_CPP_ID_TARGET_of(res->cpp_id),
			NFP_CPP_ID_ACTION_of(res->cpp_id),
			NFP_CPP_ID_TOKEN_of(res->cpp_id),
			res->start, res->end,
			res->name ? " " : "",
			res->name ? res->name : "");

		if (area->cpp->op->area_release)
			area->cpp->op->area_release(area);

		__release_cpp_area(&area->kref);
	}

	if (cpp->op->free)
		cpp->op->free(cpp);

	kfree(cpp->hwinfo);
	kfree(cpp->rtsym);

	device_unregister(&cpp->dev);

	kfree(cpp);
}

/**
 * nfp_cpp_model() - Retrieve the Model ID of the NFP
 * @cpp:	NFP CPP handle
 *
 * Return: NFP CPP Model ID
 */
u32 nfp_cpp_model(struct nfp_cpp *cpp)
{
	return cpp->model;
}

/**
 * nfp_cpp_interface() - Retrieve the Interface ID of the NFP
 * @cpp:	NFP CPP handle
 *
 * Return: NFP CPP Interface ID
 */
u16 nfp_cpp_interface(struct nfp_cpp *cpp)
{
	return cpp->interface;
}

/**
 * nfp_cpp_serial() - Retrieve the Serial ID of the NFP
 * @cpp:	NFP CPP handle
 * @serial:	Pointer to NFP serial number
 *
 * Return:  Length of NFP serial number
 */
int nfp_cpp_serial(struct nfp_cpp *cpp, const u8 **serial)
{
	*serial = &cpp->serial[0];
	return sizeof(cpp->serial);
}

void *nfp_hwinfo_cache(struct nfp_cpp *cpp)
{
	return cpp->hwinfo;
}

void nfp_hwinfo_cache_set(struct nfp_cpp *cpp, void *val)
{
	cpp->hwinfo = val;
}

void *nfp_rtsym_cache(struct nfp_cpp *cpp)
{
	return cpp->rtsym;
}

void nfp_rtsym_cache_set(struct nfp_cpp *cpp, void *val)
{
	cpp->rtsym = val;
}

/**
 * nfp_nffw_cache_flush() - Flush cached firmware information
 * @cpp:	NFP CPP handle
 *
 * Flush cached firmware information.  This function should be called
 * every time firmware is loaded on unloaded.
 */
void nfp_nffw_cache_flush(struct nfp_cpp *cpp)
{
	kfree(nfp_rtsym_cache(cpp));
	nfp_rtsym_cache_set(cpp, NULL);
}

/**
 * nfp_cpp_area_alloc_with_name() - allocate a new CPP area
 * @cpp:	CPP device handle
 * @dest:	NFP CPP ID
 * @name:	Name of region
 * @address:	Address of region
 * @size:	Size of region
 *
 * Allocate and initialize a CPP area structure.  The area must later
 * be locked down with an 'acquire' before it can be safely accessed.
 *
 * NOTE: @address and @size must be 32-bit aligned values.
 *
 * Return: NFP CPP area handle, or NULL
 */
struct nfp_cpp_area *
nfp_cpp_area_alloc_with_name(struct nfp_cpp *cpp, u32 dest, const char *name,
			     unsigned long long address, unsigned long size)
{
	struct nfp_cpp_area *area;
	u64 tmp64 = address;
	int err, name_len;

	/* Remap from cpp_island to cpp_target */
	err = nfp_target_cpp(dest, tmp64, &dest, &tmp64, cpp->imb_cat_table);
	if (err < 0)
		return NULL;

	address = tmp64;

	if (!name)
		name = "(reserved)";

	name_len = strlen(name) + 1;
	area = kzalloc(sizeof(*area) + cpp->op->area_priv_size + name_len,
		       GFP_KERNEL);
	if (!area)
		return NULL;

	area->cpp = cpp;
	area->resource.name = (void *)area + sizeof(*area) +
		cpp->op->area_priv_size;
	memcpy((char *)area->resource.name, name, name_len);

	area->resource.cpp_id = dest;
	area->resource.start = address;
	area->resource.end = area->resource.start + size - 1;
	INIT_LIST_HEAD(&area->resource.list);

	atomic_set(&area->refcount, 0);
	kref_init(&area->kref);
	mutex_init(&area->mutex);

	if (cpp->op->area_init) {
		int err;

		err = cpp->op->area_init(area, dest, address, size);
		if (err < 0) {
			kfree(area);
			return NULL;
		}
	}

	write_lock(&cpp->resource_lock);
	__resource_add(&cpp->resource_list, &area->resource);
	write_unlock(&cpp->resource_lock);

	area->offset = address;
	area->size = size;

	return area;
}

/**
 * nfp_cpp_area_alloc() - allocate a new CPP area
 * @cpp:	CPP handle
 * @dest:	CPP id
 * @address:	Start address on CPP target
 * @size:	Size of area in bytes
 *
 * Allocate and initialize a CPP area structure.  The area must later
 * be locked down with an 'acquire' before it can be safely accessed.
 *
 * NOTE: @address and @size must be 32-bit aligned values.
 *
 * Return: NFP CPP Area handle, or NULL
 */
struct nfp_cpp_area *
nfp_cpp_area_alloc(struct nfp_cpp *cpp, u32 dest,
		   unsigned long long address, unsigned long size)
{
	return nfp_cpp_area_alloc_with_name(cpp, dest, NULL, address, size);
}

/**
 * nfp_cpp_area_free() - free up the CPP area
 * @area:	CPP area handle
 *
 * Frees up memory resources held by the CPP area.
 */
void nfp_cpp_area_free(struct nfp_cpp_area *area)
{
	nfp_cpp_area_put(area);
}

/**
 * nfp_cpp_area_acquire() - lock down a CPP area for access
 * @area:	CPP area handle
 *
 * Locks down the CPP area for a potential long term activity.  Area
 * must always be locked down before being accessed.
 *
 * Return: 0, or -ERRNO
 */
int nfp_cpp_area_acquire(struct nfp_cpp_area *area)
{
	mutex_lock(&area->mutex);
	if (atomic_inc_return(&area->refcount) == 1) {
		int (*a_a)(struct nfp_cpp_area *);

		a_a = area->cpp->op->area_acquire;
		if (a_a) {
			int err;

			wait_event_interruptible(area->cpp->waitq,
						 (err = a_a(area)) != -EAGAIN);
			if (err < 0) {
				atomic_dec(&area->refcount);
				mutex_unlock(&area->mutex);
				return err;
			}
		}
	}
	mutex_unlock(&area->mutex);

	nfp_cpp_area_get(area);
	return 0;
}

/**
 * nfp_cpp_area_acquire_nonblocking() - lock down a CPP area for access
 * @area:	CPP area handle
 *
 * Locks down the CPP area for a potential long term activity.  Area
 * must always be locked down before being accessed.
 *
 * NOTE: Returns -EAGAIN is no area is available
 *
 * Return: 0, or -ERRNO
 */
int nfp_cpp_area_acquire_nonblocking(struct nfp_cpp_area *area)
{
	mutex_lock(&area->mutex);
	if (atomic_inc_return(&area->refcount) == 1) {
		if (area->cpp->op->area_acquire) {
			int err;

			err = area->cpp->op->area_acquire(area);
			if (err < 0) {
				atomic_dec(&area->refcount);
				mutex_unlock(&area->mutex);
				return err;
			}
		}
	}
	mutex_unlock(&area->mutex);

	nfp_cpp_area_get(area);
	return 0;
}

/**
 * nfp_cpp_area_release() - release a locked down CPP area
 * @area:	CPP area handle
 *
 * Releases a previously locked down CPP area.
 */
void nfp_cpp_area_release(struct nfp_cpp_area *area)
{
	mutex_lock(&area->mutex);
	/* Only call the release on refcount == 0 */
	if (atomic_dec_and_test(&area->refcount)) {
		if (area->cpp->op->area_release) {
			area->cpp->op->area_release(area);
			/* Let anyone waiting for a BAR try to get one.. */
			wake_up_interruptible_all(&area->cpp->waitq);
		}
	}
	mutex_unlock(&area->mutex);

	nfp_cpp_area_put(area);
}

/**
 * nfp_cpp_area_release_free() - release CPP area and free it
 * @area:	CPP area handle
 *
 * Releases CPP area and frees up memory resources held by the it.
 */
void nfp_cpp_area_release_free(struct nfp_cpp_area *area)
{
	nfp_cpp_area_release(area);
	nfp_cpp_area_free(area);
}

/**
 * nfp_cpp_area_read() - read data from CPP area
 * @area:	  CPP area handle
 * @offset:	  offset into CPP area
 * @kernel_vaddr: kernel address to put data into
 * @length:	  number of bytes to read
 *
 * Read data from indicated CPP region.
 *
 * NOTE: @offset and @length must be 32-bit aligned values.
 *
 * NOTE: Area must have been locked down with an 'acquire'.
 *
 * Return: length of io, or -ERRNO
 */
int nfp_cpp_area_read(struct nfp_cpp_area *area,
		      unsigned long offset, void *kernel_vaddr,
		      size_t length)
{
	return area->cpp->op->area_read(area, kernel_vaddr, offset, length);
}

/**
 * nfp_cpp_area_write() - write data to CPP area
 * @area:	CPP area handle
 * @offset:	offset into CPP area
 * @kernel_vaddr: kernel address to read data from
 * @length:	number of bytes to write
 *
 * Write data to indicated CPP region.
 *
 * NOTE: @offset and @length must be 32-bit aligned values.
 *
 * NOTE: Area must have been locked down with an 'acquire'.
 *
 * Return: length of io, or -ERRNO
 */
int nfp_cpp_area_write(struct nfp_cpp_area *area,
		       unsigned long offset, const void *kernel_vaddr,
		       size_t length)
{
	return area->cpp->op->area_write(area, kernel_vaddr, offset, length);
}

/**
 * nfp_cpp_area_check_range() - check if address range fits in CPP area
 * @area:	CPP area handle
 * @offset:	offset into CPP target
 * @length:	size of address range in bytes
 *
 * Check if address range fits within CPP area.  Return 0 if area
 * fits or -EFAULT on error.
 *
 * Return: 0, or -ERRNO
 */
int nfp_cpp_area_check_range(struct nfp_cpp_area *area,
			     unsigned long long offset, unsigned long length)
{
	if (offset < area->offset ||
	    offset + length > area->offset + area->size)
		return -EFAULT;

	return 0;
}

/**
 * nfp_cpp_area_name() - return name of a CPP area
 * @cpp_area:	CPP area handle
 *
 * Return: Name of the area, or NULL
 */
const char *nfp_cpp_area_name(struct nfp_cpp_area *cpp_area)
{
	return cpp_area->resource.name;
}

/**
 * nfp_cpp_area_priv() - return private struct for CPP area
 * @cpp_area:	CPP area handle
 *
 * Return: Private data for the CPP area
 */
void *nfp_cpp_area_priv(struct nfp_cpp_area *cpp_area)
{
	return &cpp_area[1];
}

/**
 * nfp_cpp_area_cpp() - return CPP handle for CPP area
 * @cpp_area:	CPP area handle
 *
 * Return: NFP CPP handle
 */
struct nfp_cpp *nfp_cpp_area_cpp(struct nfp_cpp_area *cpp_area)
{
	return cpp_area->cpp;
}

/**
 * nfp_cpp_area_resource() - get resource
 * @area:	CPP area handle
 *
 * NOTE: Area must have been locked down with an 'acquire'.
 *
 * Return: struct resource pointer, or NULL
 */
struct resource *nfp_cpp_area_resource(struct nfp_cpp_area *area)
{
	struct resource *res = NULL;

	if (area->cpp->op->area_resource)
		res = area->cpp->op->area_resource(area);

	return res;
}

/**
 * nfp_cpp_area_phys() - get physical address of CPP area
 * @area:	CPP area handle
 *
 * NOTE: Area must have been locked down with an 'acquire'.
 *
 * Return: phy_addr_t of the area, or NULL
 */
phys_addr_t nfp_cpp_area_phys(struct nfp_cpp_area *area)
{
	phys_addr_t addr = ~0;

	if (area->cpp->op->area_phys)
		addr = area->cpp->op->area_phys(area);

	return addr;
}

/**
 * nfp_cpp_area_iomem() - get IOMEM region for CPP area
 * @area:	CPP area handle
 *
 * Returns an iomem pointer for use with readl()/writel() style
 * operations.
 *
 * NOTE: Area must have been locked down with an 'acquire'.
 *
 * Return: __iomem pointer to the area, or NULL
 */
void __iomem *nfp_cpp_area_iomem(struct nfp_cpp_area *area)
{
	void __iomem *iomem = NULL;

	if (area->cpp->op->area_iomem)
		iomem = area->cpp->op->area_iomem(area);

	return iomem;
}

/**
 * nfp_cpp_area_readl() - Read a u32 word from an area
 * @area:	CPP Area handle
 * @offset:	Offset into area
 * @value:	Pointer to read buffer
 *
 * Return: length of the io, or -ERRNO
 */
int nfp_cpp_area_readl(struct nfp_cpp_area *area,
		       unsigned long offset, u32 *value)
{
	u8 tmp[4];
	int err;

	err = nfp_cpp_area_read(area, offset, &tmp, sizeof(tmp));
	*value = get_unaligned_le32(tmp);

	return err;
}

/**
 * nfp_cpp_area_writel() - Write a u32 word to an area
 * @area:	CPP Area handle
 * @offset:	Offset into area
 * @value:	Value to write
 *
 * Return: length of the io, or -ERRNO
 */
int nfp_cpp_area_writel(struct nfp_cpp_area *area,
			unsigned long offset, u32 value)
{
	u8 tmp[4];

	put_unaligned_le32(value, tmp);

	return nfp_cpp_area_write(area, offset, &tmp, sizeof(tmp));
}

/**
 * nfp_cpp_area_readq() - Read a u64 word from an area
 * @area:	CPP Area handle
 * @offset:	Offset into area
 * @value:	Pointer to read buffer
 *
 * Return: length of the io, or -ERRNO
 */
int nfp_cpp_area_readq(struct nfp_cpp_area *area,
		       unsigned long offset, u64 *value)
{
	u8 tmp[8];
	int err;

	err = nfp_cpp_area_read(area, offset, &tmp, sizeof(tmp));
	*value = get_unaligned_le64(tmp);

	return err;
}

/**
 * nfp_cpp_area_writeq() - Write a u64 word to an area
 * @area:	CPP Area handle
 * @offset:	Offset into area
 * @value:	Value to write
 *
 * Return: length of the io, or -ERRNO
 */
int nfp_cpp_area_writeq(struct nfp_cpp_area *area,
			unsigned long offset, u64 value)
{
	u8 tmp[8];

	put_unaligned_le64(value, tmp);

	return nfp_cpp_area_write(area, offset, &tmp, sizeof(tmp));
}

/**
 * nfp_cpp_area_fill() - fill a CPP area with a value
 * @area:	CPP area
 * @offset:	offset into CPP area
 * @value:	value to fill with
 * @length:	length of area to fill
 *
 * Fill indicated area with given value.
 *
 * Return: length of io, or -ERRNO
 */
int nfp_cpp_area_fill(struct nfp_cpp_area *area,
		      unsigned long offset, u32 value, size_t length)
{
	u8 tmp[4];
	size_t i;
	int k;

	put_unaligned_le32(value, tmp);

	if (offset % sizeof(tmp) || length % sizeof(tmp))
		return -EINVAL;

	for (i = 0; i < length; i += sizeof(tmp)) {
		k = nfp_cpp_area_write(area, offset + i, &tmp, sizeof(tmp));
		if (k < 0)
			return k;
	}

	return i;
}

/**
 * nfp_cpp_area_cache_add() - Permanently reserve and area for the hot cache
 * @cpp:	NFP CPP handle
 * @size:	Size of the area - MUST BE A POWER OF 2.
 */
int nfp_cpp_area_cache_add(struct nfp_cpp *cpp, size_t size)
{
	struct nfp_cpp_area_cache *cache;
	struct nfp_cpp_area *area;

	/* Allocate an area - we use the MU target's base as a placeholder,
	 * as all supported chips have a MU.
	 */
	area = nfp_cpp_area_alloc(cpp, NFP_CPP_ID(7, NFP_CPP_ACTION_RW, 0),
				  0, size);
	if (!area)
		return -ENOMEM;

	cache = kzalloc(sizeof(*cache), GFP_KERNEL);
	if (!cache)
		return -ENOMEM;

	cache->id = 0;
	cache->addr = 0;
	cache->size = size;
	cache->area = area;
	mutex_lock(&cpp->area_cache_mutex);
	list_add_tail(&cache->entry, &cpp->area_cache_list);
	mutex_unlock(&cpp->area_cache_mutex);

	return 0;
}

static struct nfp_cpp_area_cache *
area_cache_get(struct nfp_cpp *cpp, u32 id,
	       u64 addr, unsigned long *offset, size_t length)
{
	struct nfp_cpp_area_cache *cache;
	int err;

	/* Early exit when length == 0, which prevents
	 * the need for special case code below when
	 * checking against available cache size.
	 */
	if (length == 0)
		return NULL;

	if (list_empty(&cpp->area_cache_list) || id == 0)
		return NULL;

	/* Remap from cpp_island to cpp_target */
	err = nfp_target_cpp(id, addr, &id, &addr, cpp->imb_cat_table);
	if (err < 0)
		return NULL;

	addr += *offset;

	mutex_lock(&cpp->area_cache_mutex);

	/* See if we have a match */
	list_for_each_entry(cache, &cpp->area_cache_list, entry) {
		if (id == cache->id &&
		    addr >= cache->addr &&
		    addr + length <= cache->addr + cache->size)
			goto exit;
	}

	/* No matches - inspect the tail of the LRU */
	cache = list_entry(cpp->area_cache_list.prev,
			   struct nfp_cpp_area_cache, entry);

	/* Can we fit in the cache entry? */
	if (round_down(addr + length - 1, cache->size) !=
	    round_down(addr, cache->size)) {
		mutex_unlock(&cpp->area_cache_mutex);
		return NULL;
	}

	/* If id != 0, we will need to release it */
	if (cache->id) {
		nfp_cpp_area_release(cache->area);
		cache->id = 0;
		cache->addr = 0;
	}

	/* Adjust the start address to be cache size aligned */
	cache->id = id;
	cache->addr = addr & ~(u64)(cache->size - 1);

	/* Re-init to the new ID and address */
	if (cpp->op->area_init) {
		err = cpp->op->area_init(cache->area,
					 id, cache->addr, cache->size);
		if (err < 0) {
			mutex_unlock(&cpp->area_cache_mutex);
			return NULL;
		}
	}

	/* Attempt to acquire */
	err = nfp_cpp_area_acquire(cache->area);
	if (err < 0) {
		mutex_unlock(&cpp->area_cache_mutex);
		return NULL;
	}

exit:
	/* Adjust offset */
	*offset = addr - cache->addr;
	return cache;
}

static void
area_cache_put(struct nfp_cpp *cpp, struct nfp_cpp_area_cache *cache)
{
	if (!cache)
		return;

	/* Move to front of LRU */
	list_del(&cache->entry);
	list_add(&cache->entry, &cpp->area_cache_list);

	mutex_unlock(&cpp->area_cache_mutex);
}

/**
 * nfp_cpp_read() - read from CPP target
 * @cpp:		CPP handle
 * @destination:	CPP id
 * @address:		offset into CPP target
 * @kernel_vaddr:	kernel buffer for result
 * @length:		number of bytes to read
 *
 * Return: length of io, or -ERRNO
 */
int nfp_cpp_read(struct nfp_cpp *cpp, u32 destination,
		 unsigned long long address, void *kernel_vaddr, size_t length)
{
	struct nfp_cpp_area_cache *cache;
	struct nfp_cpp_area *area;
	unsigned long offset = 0;
	int err;

	cache = area_cache_get(cpp, destination, address, &offset, length);
	if (cache) {
		area = cache->area;
	} else {
		area = nfp_cpp_area_alloc(cpp, destination, address, length);
		if (!area)
			return -ENOMEM;

		err = nfp_cpp_area_acquire(area);
		if (err)
			goto out;
	}

	err = nfp_cpp_area_read(area, offset, kernel_vaddr, length);
out:
	if (cache)
		area_cache_put(cpp, cache);
	else
		nfp_cpp_area_release_free(area);

	return err;
}

/**
 * nfp_cpp_write() - write to CPP target
 * @cpp:		CPP handle
 * @destination:	CPP id
 * @address:		offset into CPP target
 * @kernel_vaddr:	kernel buffer to read from
 * @length:		number of bytes to write
 *
 * Return: length of io, or -ERRNO
 */
int nfp_cpp_write(struct nfp_cpp *cpp, u32 destination,
		  unsigned long long address,
		  const void *kernel_vaddr, size_t length)
{
	struct nfp_cpp_area_cache *cache;
	struct nfp_cpp_area *area;
	unsigned long offset = 0;
	int err;

	cache = area_cache_get(cpp, destination, address, &offset, length);
	if (cache) {
		area = cache->area;
	} else {
		area = nfp_cpp_area_alloc(cpp, destination, address, length);
		if (!area)
			return -ENOMEM;

		err = nfp_cpp_area_acquire(area);
		if (err)
			goto out;
	}

	err = nfp_cpp_area_write(area, offset, kernel_vaddr, length);

out:
	if (cache)
		area_cache_put(cpp, cache);
	else
		nfp_cpp_area_release_free(area);

	return err;
}

/* Return the correct CPP address, and fixup xpb_addr as needed. */
static u32 nfp_xpb_to_cpp(struct nfp_cpp *cpp, u32 *xpb_addr)
{
	int island;
	u32 xpb;

	xpb = NFP_CPP_ID(14, NFP_CPP_ACTION_RW, 0);
	/* Ensure that non-local XPB accesses go
	 * out through the global XPBM bus.
	 */
	island = (*xpb_addr >> 24) & 0x3f;
	if (!island)
		return xpb;

	if (island != 1) {
		*xpb_addr |= 1 << 30;
		return xpb;
	}

	/* Accesses to the ARM Island overlay uses Island 0 / Global Bit */
	*xpb_addr &= ~0x7f000000;
	if (*xpb_addr < 0x60000) {
		*xpb_addr |= 1 << 30;
	} else {
		/* And only non-ARM interfaces use the island id = 1 */
		if (NFP_CPP_INTERFACE_TYPE_of(nfp_cpp_interface(cpp))
		    != NFP_CPP_INTERFACE_TYPE_ARM)
			*xpb_addr |= 1 << 24;
	}

	return xpb;
}

/**
 * nfp_xpb_readl() - Read a u32 word from a XPB location
 * @cpp:	CPP device handle
 * @xpb_addr:	Address for operation
 * @value:	Pointer to read buffer
 *
 * Return: length of the io, or -ERRNO
 */
int nfp_xpb_readl(struct nfp_cpp *cpp, u32 xpb_addr, u32 *value)
{
	u32 cpp_dest = nfp_xpb_to_cpp(cpp, &xpb_addr);

	return nfp_cpp_readl(cpp, cpp_dest, xpb_addr, value);
}

/**
 * nfp_xpb_writel() - Write a u32 word to a XPB location
 * @cpp:	CPP device handle
 * @xpb_addr:	Address for operation
 * @value:	Value to write
 *
 * Return: length of the io, or -ERRNO
 */
int nfp_xpb_writel(struct nfp_cpp *cpp, u32 xpb_addr, u32 value)
{
	u32 cpp_dest = nfp_xpb_to_cpp(cpp, &xpb_addr);

	return nfp_cpp_writel(cpp, cpp_dest, xpb_addr, value);
}

/**
 * nfp_xpb_writelm() - Modify bits of a 32-bit value from the XPB bus
 * @cpp:	NFP CPP device handle
 * @xpb_tgt:	XPB target and address
 * @mask:	mask of bits to alter
 * @value:	value to modify
 *
 * KERNEL: This operation is safe to call in interrupt or softirq context.
 *
 * Return: length of the io, or -ERRNO
 */
int nfp_xpb_writelm(struct nfp_cpp *cpp, u32 xpb_tgt,
		    u32 mask, u32 value)
{
	int err;
	u32 tmp;

	err = nfp_xpb_readl(cpp, xpb_tgt, &tmp);
	if (err < 0)
		return err;

	tmp &= ~mask;
	tmp |= mask & value;
	return nfp_xpb_writel(cpp, xpb_tgt, tmp);
}

/* Lockdep markers */
static struct lock_class_key nfp_cpp_resource_lock_key;

static void nfp_cpp_dev_release(struct device *dev)
{
	/* Nothing to do here - it just makes the kernel happy */
}

/**
 * nfp_cpp_from_operations() - Create a NFP CPP handle
 *                             from an operations structure
 * @ops:	NFP CPP operations structure
 * @parent:	Parent device
 * @priv:	Private data of low-level implementation
 *
 * NOTE: On failure, cpp_ops->free will be called!
 *
 * Return: NFP CPP handle on success, ERR_PTR on failure
 */
struct nfp_cpp *
nfp_cpp_from_operations(const struct nfp_cpp_operations *ops,
			struct device *parent, void *priv)
{
	const u32 arm = NFP_CPP_ID(NFP_CPP_TARGET_ARM, NFP_CPP_ACTION_RW, 0);
	struct nfp_cpp *cpp;
	u32 mask[2];
	u32 xpbaddr;
	size_t tgt;
	int err;

	cpp = kzalloc(sizeof(*cpp), GFP_KERNEL);
	if (!cpp) {
		err = -ENOMEM;
		goto err_malloc;
	}

	cpp->op = ops;
	cpp->priv = priv;
	cpp->interface = ops->get_interface(parent);
	if (ops->read_serial)
		ops->read_serial(parent, cpp->serial);
	rwlock_init(&cpp->resource_lock);
	init_waitqueue_head(&cpp->waitq);
	lockdep_set_class(&cpp->resource_lock, &nfp_cpp_resource_lock_key);
	INIT_LIST_HEAD(&cpp->mutex_cache);
	INIT_LIST_HEAD(&cpp->resource_list);
	INIT_LIST_HEAD(&cpp->area_cache_list);
	mutex_init(&cpp->area_cache_mutex);
	cpp->dev.init_name = "cpp";
	cpp->dev.parent = parent;
	cpp->dev.release = nfp_cpp_dev_release;
	err = device_register(&cpp->dev);
	if (err < 0) {
		put_device(&cpp->dev);
		goto err_dev;
	}

	dev_set_drvdata(&cpp->dev, cpp);

	/* NOTE: cpp_lock is NOT locked for op->init,
	 * since it may call NFP CPP API operations
	 */
	if (cpp->op->init) {
		err = cpp->op->init(cpp);
		if (err < 0) {
			dev_err(parent,
				"NFP interface initialization failed\n");
			goto err_out;
		}
	}

	err = nfp_cpp_model_autodetect(cpp, &cpp->model);
	if (err < 0) {
		dev_err(parent, "NFP model detection failed\n");
		goto err_out;
	}

	for (tgt = 0; tgt < ARRAY_SIZE(cpp->imb_cat_table); tgt++) {
			/* Hardcoded XPB IMB Base, island 0 */
		xpbaddr = 0x000a0000 + (tgt * 4);
		err = nfp_xpb_readl(cpp, xpbaddr,
				    &cpp->imb_cat_table[tgt]);
		if (err < 0) {
			dev_err(parent,
				"Can't read CPP mapping from device\n");
			goto err_out;
		}
	}

	nfp_cpp_readl(cpp, arm, NFP_ARM_GCSR + NFP_ARM_GCSR_SOFTMODEL2,
		      &mask[0]);
	nfp_cpp_readl(cpp, arm, NFP_ARM_GCSR + NFP_ARM_GCSR_SOFTMODEL3,
		      &mask[1]);

	dev_info(cpp->dev.parent, "Model: 0x%08x, SN: %pM, Ifc: 0x%04x\n",
		 nfp_cpp_model(cpp), cpp->serial, nfp_cpp_interface(cpp));

	return cpp;

err_out:
	device_unregister(&cpp->dev);
err_dev:
	kfree(cpp);
err_malloc:
	return ERR_PTR(err);
}

/**
 * nfp_cpp_priv() - Get the operations private data of a CPP handle
 * @cpp:	CPP handle
 *
 * Return: Private data for the NFP CPP handle
 */
void *nfp_cpp_priv(struct nfp_cpp *cpp)
{
	return cpp->priv;
}

/**
 * nfp_cpp_device() - Get the Linux device handle of a CPP handle
 * @cpp:	CPP handle
 *
 * Return: Device for the NFP CPP bus
 */
struct device *nfp_cpp_device(struct nfp_cpp *cpp)
{
	return &cpp->dev;
}

#define NFP_EXPL_OP(func, expl, args...)			  \
	({							  \
		struct nfp_cpp *cpp = nfp_cpp_explicit_cpp(expl); \
		int err = -ENODEV;				  \
								  \
		if (cpp->op->func)				  \
			err = cpp->op->func(expl, ##args);	  \
		err;						  \
	})

#define NFP_EXPL_OP_NR(func, expl, args...)			  \
	({							  \
		struct nfp_cpp *cpp = nfp_cpp_explicit_cpp(expl); \
								  \
		if (cpp->op->func)				  \
			cpp->op->func(expl, ##args);		  \
								  \
	})

/**
 * nfp_cpp_explicit_acquire() - Acquire explicit access handle
 * @cpp:	NFP CPP handle
 *
 * The 'data_ref' and 'signal_ref' values are useful when
 * constructing the NFP_EXPL_CSR1 and NFP_EXPL_POST values.
 *
 * Return: NFP CPP explicit handle
 */
struct nfp_cpp_explicit *nfp_cpp_explicit_acquire(struct nfp_cpp *cpp)
{
	struct nfp_cpp_explicit *expl;
	int err;

	expl = kzalloc(sizeof(*expl) + cpp->op->explicit_priv_size, GFP_KERNEL);
	if (!expl)
		return NULL;

	expl->cpp = cpp;
	err = NFP_EXPL_OP(explicit_acquire, expl);
	if (err < 0) {
		kfree(expl);
		return NULL;
	}

	return expl;
}

/**
 * nfp_cpp_explicit_set_target() - Set target fields for explicit
 * @expl:	Explicit handle
 * @cpp_id:	CPP ID field
 * @len:	CPP Length field
 * @mask:	CPP Mask field
 *
 * Return: 0, or -ERRNO
 */
int nfp_cpp_explicit_set_target(struct nfp_cpp_explicit *expl,
				u32 cpp_id, u8 len, u8 mask)
{
	expl->cmd.cpp_id = cpp_id;
	expl->cmd.len = len;
	expl->cmd.byte_mask = mask;

	return 0;
}

/**
 * nfp_cpp_explicit_set_data() - Set data fields for explicit
 * @expl:	Explicit handle
 * @data_master: CPP Data Master field
 * @data_ref:	CPP Data Ref field
 *
 * Return: 0, or -ERRNO
 */
int nfp_cpp_explicit_set_data(struct nfp_cpp_explicit *expl,
			      u8 data_master, u16 data_ref)
{
	expl->cmd.data_master = data_master;
	expl->cmd.data_ref = data_ref;

	return 0;
}

/**
 * nfp_cpp_explicit_set_signal() - Set signal fields for explicit
 * @expl:	Explicit handle
 * @signal_master: CPP Signal Master field
 * @signal_ref:	CPP Signal Ref field
 *
 * Return: 0, or -ERRNO
 */
int nfp_cpp_explicit_set_signal(struct nfp_cpp_explicit *expl,
				u8 signal_master, u8 signal_ref)
{
	expl->cmd.signal_master = signal_master;
	expl->cmd.signal_ref = signal_ref;

	return 0;
}

/**
 * nfp_cpp_explicit_set_posted() - Set completion fields for explicit
 * @expl:	Explicit handle
 * @posted:	True for signaled completion, false otherwise
 * @siga:	CPP Signal A field
 * @siga_mode:	CPP Signal A Mode field
 * @sigb:	CPP Signal B field
 * @sigb_mode:	CPP Signal B Mode field
 *
 * Return: 0, or -ERRNO
 */
int nfp_cpp_explicit_set_posted(struct nfp_cpp_explicit *expl, int posted,
				u8 siga,
				enum nfp_cpp_explicit_signal_mode siga_mode,
				u8 sigb,
				enum nfp_cpp_explicit_signal_mode sigb_mode)
{
	expl->cmd.posted = posted;
	expl->cmd.siga = siga;
	expl->cmd.sigb = sigb;
	expl->cmd.siga_mode = siga_mode;
	expl->cmd.sigb_mode = sigb_mode;

	return 0;
}

/**
 * nfp_cpp_explicit_put() - Set up the write (pull) data for a explicit access
 * @expl:	NFP CPP Explicit handle
 * @buff:	Data to have the target pull in the transaction
 * @len:	Length of data, in bytes
 *
 * The 'len' parameter must be less than or equal to 128 bytes.
 *
 * If this function is called before the configuration
 * registers are set, it will return -EINVAL.
 *
 * Return: 0, or -ERRNO
 */
int nfp_cpp_explicit_put(struct nfp_cpp_explicit *expl,
			 const void *buff, size_t len)
{
	return NFP_EXPL_OP(explicit_put, expl, buff, len);
}

/**
 * nfp_cpp_explicit_do() - Execute a transaction, and wait for it to complete
 * @expl:	NFP CPP Explicit handle
 * @address:	Address to send in the explicit transaction
 *
 * If this function is called before the configuration
 * registers are set, it will return -1, with an errno of EINVAL.
 *
 * Return: 0, or -ERRNO
 */
int nfp_cpp_explicit_do(struct nfp_cpp_explicit *expl, u64 address)
{
	return NFP_EXPL_OP(explicit_do, expl, &expl->cmd, address);
}

/**
 * nfp_cpp_explicit_get() - Get the 'push' (read) data from a explicit access
 * @expl:	NFP CPP Explicit handle
 * @buff:	Data that the target pushed in the transaction
 * @len:	Length of data, in bytes
 *
 * The 'len' parameter must be less than or equal to 128 bytes.
 *
 * If this function is called before all three configuration
 * registers are set, it will return -1, with an errno of EINVAL.
 *
 * If this function is called before nfp_cpp_explicit_do()
 * has completed, it will return -1, with an errno of EBUSY.
 *
 * Return: 0, or -ERRNO
 */
int nfp_cpp_explicit_get(struct nfp_cpp_explicit *expl, void *buff, size_t len)
{
	return NFP_EXPL_OP(explicit_get, expl, buff, len);
}

/**
 * nfp_cpp_explicit_release() - Release explicit access handle
 * @expl:	NFP CPP Explicit handle
 *
 */
void nfp_cpp_explicit_release(struct nfp_cpp_explicit *expl)
{
	NFP_EXPL_OP_NR(explicit_release, expl);
	kfree(expl);
}

/**
 * nfp_cpp_explicit_cpp() - return CPP handle for CPP explicit
 * @cpp_explicit:	CPP explicit handle
 *
 * Return: NFP CPP handle of the explicit
 */
struct nfp_cpp *nfp_cpp_explicit_cpp(struct nfp_cpp_explicit *cpp_explicit)
{
	return cpp_explicit->cpp;
}

/**
 * nfp_cpp_explicit_priv() - return private struct for CPP explicit
 * @cpp_explicit:	CPP explicit handle
 *
 * Return: private data of the explicit, or NULL
 */
void *nfp_cpp_explicit_priv(struct nfp_cpp_explicit *cpp_explicit)
{
	return &cpp_explicit[1];
}

/* THIS FUNCTION IS NOT EXPORTED */
static u32 nfp_mutex_locked(u16 interface)
{
	return (u32)interface << 16 | 0x000f;
}

static u32 nfp_mutex_unlocked(u16 interface)
{
	return (u32)interface << 16 | 0x0000;
}

static bool nfp_mutex_is_locked(u32 val)
{
	return (val & 0xffff) == 0x000f;
}

static bool nfp_mutex_is_unlocked(u32 val)
{
	return (val & 0xffff) == 0000;
}

/* If you need more than 65536 recursive locks, please rethink your code. */
#define MUTEX_DEPTH_MAX         0xffff

static int
nfp_cpp_mutex_validate(u16 interface, int *target, unsigned long long address)
{
	/* Not permitted on invalid interfaces */
	if (NFP_CPP_INTERFACE_TYPE_of(interface) ==
	    NFP_CPP_INTERFACE_TYPE_INVALID)
		return -EINVAL;

	/* Address must be 64-bit aligned */
	if (address & 7)
		return -EINVAL;

	if (*target != NFP_CPP_TARGET_MU)
		return -EINVAL;

	return 0;
}

/**
 * nfp_cpp_mutex_init() - Initialize a mutex location
 * @cpp:	NFP CPP handle
 * @target:	NFP CPP target ID (ie NFP_CPP_TARGET_CLS or NFP_CPP_TARGET_MU)
 * @address:	Offset into the address space of the NFP CPP target ID
 * @key:	Unique 32-bit value for this mutex
 *
 * The CPP target:address must point to a 64-bit aligned location, and
 * will initialize 64 bits of data at the location.
 *
 * This creates the initial mutex state, as locked by this
 * nfp_cpp_interface().
 *
 * This function should only be called when setting up
 * the initial lock state upon boot-up of the system.
 *
 * Return: 0 on success, or -errno on failure
 */
int nfp_cpp_mutex_init(struct nfp_cpp *cpp,
		       int target, unsigned long long address, u32 key)
{
	const u32 muw = NFP_CPP_ID(target, 4, 0);    /* atomic_write */
	u16 interface = nfp_cpp_interface(cpp);
	int err;

	err = nfp_cpp_mutex_validate(interface, &target, address);
	if (err)
		return err;

	err = nfp_cpp_writel(cpp, muw, address + 4, key);
	if (err)
		return err;

	err = nfp_cpp_writel(cpp, muw, address, nfp_mutex_locked(interface));
	if (err)
		return err;

	return 0;
}

/**
 * nfp_cpp_mutex_alloc() - Create a mutex handle
 * @cpp:	NFP CPP handle
 * @target:	NFP CPP target ID (ie NFP_CPP_TARGET_CLS or NFP_CPP_TARGET_MU)
 * @address:	Offset into the address space of the NFP CPP target ID
 * @key:	32-bit unique key (must match the key at this location)
 *
 * The CPP target:address must point to a 64-bit aligned location, and
 * reserve 64 bits of data at the location for use by the handle.
 *
 * Only target/address pairs that point to entities that support the
 * MU Atomic Engine's CmpAndSwap32 command are supported.
 *
 * Return:	A non-NULL struct nfp_cpp_mutex * on success, NULL on failure.
 */
struct nfp_cpp_mutex *nfp_cpp_mutex_alloc(struct nfp_cpp *cpp, int target,
					  unsigned long long address, u32 key)
{
	const u32 mur = NFP_CPP_ID(target, 3, 0);    /* atomic_read */
	u16 interface = nfp_cpp_interface(cpp);
	struct nfp_cpp_mutex *mutex;
	int err;
	u32 tmp;

	err = nfp_cpp_mutex_validate(interface, &target, address);
	if (err)
		return NULL;

	/* Look for mutex on cache list */
	list_for_each_entry(mutex, &cpp->mutex_cache, list) {
		if (mutex->target == target && mutex->address == address) {
			mutex->usage++;
			return mutex;
		}
	}

	err = nfp_cpp_readl(cpp, mur, address + 4, &tmp);
	if (err < 0)
		return NULL;

	if (tmp != key)
		return NULL;

	mutex = kzalloc(sizeof(*mutex), GFP_KERNEL);
	if (!mutex)
		return NULL;

	mutex->cpp = cpp;
	mutex->target = target;
	mutex->address = address;
	mutex->key = key;
	mutex->depth = 0;
	mutex->usage = 1;

	/* Add mutex to cache list */
	list_add(&mutex->list, &cpp->mutex_cache);

	return mutex;
}

/**
 * nfp_cpp_mutex_free() - Free a mutex handle - does not alter the lock state
 * @mutex:	NFP CPP Mutex handle
 */
void nfp_cpp_mutex_free(struct nfp_cpp_mutex *mutex)
{
	if (--mutex->usage)
		return;

	/* Remove mutex from cache */
	list_del(&mutex->list);
	kfree(mutex);
}

/**
 * nfp_cpp_mutex_lock() - Lock a mutex handle, using the NFP MU Atomic Engine
 * @mutex:	NFP CPP Mutex handle
 *
 * Return: 0 on success, or -errno on failure
 */
int nfp_cpp_mutex_lock(struct nfp_cpp_mutex *mutex)
{
	unsigned long warn_at = jiffies + 15 * HZ;
	unsigned int timeout_ms = 1;
	int err;

	/* We can't use a waitqueue here, because the unlocker
	 * might be on a separate CPU.
	 *
	 * So just wait for now.
	 */
	for (;;) {
		err = nfp_cpp_mutex_trylock(mutex);
		if (err != -EBUSY)
			break;

		err = msleep_interruptible(timeout_ms);
		if (err != 0)
			return -ERESTARTSYS;

		if (time_is_before_eq_jiffies(warn_at)) {
			warn_at = jiffies + 60 * HZ;
			dev_warn(mutex->cpp->dev.parent,
				 "Warning: waiting for NFP mutex [usage:%hd depth:%hd target:%d addr:%llx key:%08x]\n",
				 mutex->usage, mutex->depth,
				 mutex->target, mutex->address, mutex->key);
		}
	}

	return err;
}

/**
 * nfp_cpp_mutex_unlock() - Unlock a mutex handle, using the MU Atomic Engine
 * @mutex:	NFP CPP Mutex handle
 *
 * Return: 0 on success, or -errno on failure
 */
int nfp_cpp_mutex_unlock(struct nfp_cpp_mutex *mutex)
{
	const u32 muw = NFP_CPP_ID(mutex->target, 4, 0);    /* atomic_write */
	const u32 mur = NFP_CPP_ID(mutex->target, 3, 0);    /* atomic_read */
	struct nfp_cpp *cpp = mutex->cpp;
	u32 key, value;
	u16 interface;
	int err;

	interface = nfp_cpp_interface(cpp);

	if (mutex->depth > 1) {
		mutex->depth--;
		return 0;
	}

	err = nfp_cpp_readl(mutex->cpp, mur, mutex->address + 4, &key);
	if (err < 0)
		return err;

	if (key != mutex->key)
		return -EPERM;

	err = nfp_cpp_readl(mutex->cpp, mur, mutex->address, &value);
	if (err < 0)
		return err;

	if (value != nfp_mutex_locked(interface))
		return -EACCES;

	err = nfp_cpp_writel(cpp, muw, mutex->address,
			     nfp_mutex_unlocked(interface));
	if (err < 0)
		return err;

	mutex->depth = 0;
	return 0;
}

/**
 * nfp_cpp_mutex_trylock() - Attempt to lock a mutex handle
 * @mutex:	NFP CPP Mutex handle
 *
 * Return:      0 if the lock succeeded, -errno on failure
 */
int nfp_cpp_mutex_trylock(struct nfp_cpp_mutex *mutex)
{
	const u32 muw = NFP_CPP_ID(mutex->target, 4, 0);    /* atomic_write */
	const u32 mus = NFP_CPP_ID(mutex->target, 5, 3);    /* test_set_imm */
	const u32 mur = NFP_CPP_ID(mutex->target, 3, 0);    /* atomic_read */
	struct nfp_cpp *cpp = mutex->cpp;
	u32 key, value, tmp;
	int err;

	if (mutex->depth > 0) {
		if (mutex->depth == MUTEX_DEPTH_MAX)
			return -E2BIG;
		mutex->depth++;
		return 0;
	}

	/* Verify that the lock marker is not damaged */
	err = nfp_cpp_readl(cpp, mur, mutex->address + 4, &key);
	if (err < 0)
		return err;

	if (key != mutex->key)
		return -EPERM;

	/* Compare against the unlocked state, and if true,
	 * write the interface id into the top 16 bits, and
	 * mark as locked.
	 */
	value = nfp_mutex_locked(nfp_cpp_interface(cpp));

	/* We use test_set_imm here, as it implies a read
	 * of the current state, and sets the bits in the
	 * bytemask of the command to 1s. Since the mutex
	 * is guaranteed to be 64-bit aligned, the bytemask
	 * of this 32-bit command is ensured to be 8'b00001111,
	 * which implies that the lower 4 bits will be set to
	 * ones regardless of the initial state.
	 *
	 * Since this is a 'Readback' operation, with no Pull
	 * data, we can treat this as a normal Push (read)
	 * atomic, which returns the original value.
	 */
	err = nfp_cpp_readl(cpp, mus, mutex->address, &tmp);
	if (err < 0)
		return err;

	/* Was it unlocked? */
	if (nfp_mutex_is_unlocked(tmp)) {
		/* The read value can only be 0x....0000 in the unlocked state.
		 * If there was another contending for this lock, then
		 * the lock state would be 0x....000f
		 */

		/* Write our owner ID into the lock
		 * While not strictly necessary, this helps with
		 * debug and bookkeeping.
		 */
		err = nfp_cpp_writel(cpp, muw, mutex->address, value);
		if (err < 0)
			return err;

		mutex->depth = 1;
		return 0;
	}

	/* Already locked by us? Success! */
	if (tmp == value) {
		mutex->depth = 1;
		return 0;
	}

	return nfp_mutex_is_locked(tmp) ? -EBUSY : -EINVAL;
}
