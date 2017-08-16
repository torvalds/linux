/*
 * Copyright (c) 2006 Chelsio, Inc. All rights reserved.
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
/* Crude resource management */
#include <linux/kernel.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/kfifo.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include "cxio_resource.h"
#include "cxio_hal.h"

static struct kfifo rhdl_fifo;
static spinlock_t rhdl_fifo_lock;

#define RANDOM_SIZE 16

static int __cxio_init_resource_fifo(struct kfifo *fifo,
				   spinlock_t *fifo_lock,
				   u32 nr, u32 skip_low,
				   u32 skip_high,
				   int random)
{
	u32 i, j, entry = 0, idx;
	u32 random_bytes;
	u32 rarray[16];
	spin_lock_init(fifo_lock);

	if (kfifo_alloc(fifo, nr * sizeof(u32), GFP_KERNEL))
		return -ENOMEM;

	for (i = 0; i < skip_low + skip_high; i++)
		kfifo_in(fifo, (unsigned char *) &entry, sizeof(u32));
	if (random) {
		j = 0;
		random_bytes = prandom_u32();
		for (i = 0; i < RANDOM_SIZE; i++)
			rarray[i] = i + skip_low;
		for (i = skip_low + RANDOM_SIZE; i < nr - skip_high; i++) {
			if (j >= RANDOM_SIZE) {
				j = 0;
				random_bytes = prandom_u32();
			}
			idx = (random_bytes >> (j * 2)) & 0xF;
			kfifo_in(fifo,
				(unsigned char *) &rarray[idx],
				sizeof(u32));
			rarray[idx] = i;
			j++;
		}
		for (i = 0; i < RANDOM_SIZE; i++)
			kfifo_in(fifo,
				(unsigned char *) &rarray[i],
				sizeof(u32));
	} else
		for (i = skip_low; i < nr - skip_high; i++)
			kfifo_in(fifo, (unsigned char *) &i, sizeof(u32));

	for (i = 0; i < skip_low + skip_high; i++)
		if (kfifo_out_locked(fifo, (unsigned char *) &entry,
				sizeof(u32), fifo_lock) != sizeof(u32))
					break;
	return 0;
}

static int cxio_init_resource_fifo(struct kfifo *fifo, spinlock_t * fifo_lock,
				   u32 nr, u32 skip_low, u32 skip_high)
{
	return (__cxio_init_resource_fifo(fifo, fifo_lock, nr, skip_low,
					  skip_high, 0));
}

static int cxio_init_resource_fifo_random(struct kfifo *fifo,
				   spinlock_t * fifo_lock,
				   u32 nr, u32 skip_low, u32 skip_high)
{

	return (__cxio_init_resource_fifo(fifo, fifo_lock, nr, skip_low,
					  skip_high, 1));
}

static int cxio_init_qpid_fifo(struct cxio_rdev *rdev_p)
{
	u32 i;

	spin_lock_init(&rdev_p->rscp->qpid_fifo_lock);

	if (kfifo_alloc(&rdev_p->rscp->qpid_fifo, T3_MAX_NUM_QP * sizeof(u32),
					      GFP_KERNEL))
		return -ENOMEM;

	for (i = 16; i < T3_MAX_NUM_QP; i++)
		if (!(i & rdev_p->qpmask))
			kfifo_in(&rdev_p->rscp->qpid_fifo,
				    (unsigned char *) &i, sizeof(u32));
	return 0;
}

int cxio_hal_init_rhdl_resource(u32 nr_rhdl)
{
	return cxio_init_resource_fifo(&rhdl_fifo, &rhdl_fifo_lock, nr_rhdl, 1,
				       0);
}

void cxio_hal_destroy_rhdl_resource(void)
{
	kfifo_free(&rhdl_fifo);
}

/* nr_* must be power of 2 */
int cxio_hal_init_resource(struct cxio_rdev *rdev_p,
			   u32 nr_tpt, u32 nr_pbl,
			   u32 nr_rqt, u32 nr_qpid, u32 nr_cqid, u32 nr_pdid)
{
	int err = 0;
	struct cxio_hal_resource *rscp;

	rscp = kmalloc(sizeof(*rscp), GFP_KERNEL);
	if (!rscp)
		return -ENOMEM;
	rdev_p->rscp = rscp;
	err = cxio_init_resource_fifo_random(&rscp->tpt_fifo,
				      &rscp->tpt_fifo_lock,
				      nr_tpt, 1, 0);
	if (err)
		goto tpt_err;
	err = cxio_init_qpid_fifo(rdev_p);
	if (err)
		goto qpid_err;
	err = cxio_init_resource_fifo(&rscp->cqid_fifo, &rscp->cqid_fifo_lock,
				      nr_cqid, 1, 0);
	if (err)
		goto cqid_err;
	err = cxio_init_resource_fifo(&rscp->pdid_fifo, &rscp->pdid_fifo_lock,
				      nr_pdid, 1, 0);
	if (err)
		goto pdid_err;
	return 0;
pdid_err:
	kfifo_free(&rscp->cqid_fifo);
cqid_err:
	kfifo_free(&rscp->qpid_fifo);
qpid_err:
	kfifo_free(&rscp->tpt_fifo);
tpt_err:
	return -ENOMEM;
}

/*
 * returns 0 if no resource available
 */
static u32 cxio_hal_get_resource(struct kfifo *fifo, spinlock_t * lock)
{
	u32 entry;
	if (kfifo_out_locked(fifo, (unsigned char *) &entry, sizeof(u32), lock))
		return entry;
	else
		return 0;	/* fifo emptry */
}

static void cxio_hal_put_resource(struct kfifo *fifo, spinlock_t * lock,
		u32 entry)
{
	BUG_ON(
	kfifo_in_locked(fifo, (unsigned char *) &entry, sizeof(u32), lock)
	== 0);
}

u32 cxio_hal_get_stag(struct cxio_hal_resource *rscp)
{
	return cxio_hal_get_resource(&rscp->tpt_fifo, &rscp->tpt_fifo_lock);
}

void cxio_hal_put_stag(struct cxio_hal_resource *rscp, u32 stag)
{
	cxio_hal_put_resource(&rscp->tpt_fifo, &rscp->tpt_fifo_lock, stag);
}

u32 cxio_hal_get_qpid(struct cxio_hal_resource *rscp)
{
	u32 qpid = cxio_hal_get_resource(&rscp->qpid_fifo,
			&rscp->qpid_fifo_lock);
	pr_debug("%s qpid 0x%x\n", __func__, qpid);
	return qpid;
}

void cxio_hal_put_qpid(struct cxio_hal_resource *rscp, u32 qpid)
{
	pr_debug("%s qpid 0x%x\n", __func__, qpid);
	cxio_hal_put_resource(&rscp->qpid_fifo, &rscp->qpid_fifo_lock, qpid);
}

u32 cxio_hal_get_cqid(struct cxio_hal_resource *rscp)
{
	return cxio_hal_get_resource(&rscp->cqid_fifo, &rscp->cqid_fifo_lock);
}

void cxio_hal_put_cqid(struct cxio_hal_resource *rscp, u32 cqid)
{
	cxio_hal_put_resource(&rscp->cqid_fifo, &rscp->cqid_fifo_lock, cqid);
}

u32 cxio_hal_get_pdid(struct cxio_hal_resource *rscp)
{
	return cxio_hal_get_resource(&rscp->pdid_fifo, &rscp->pdid_fifo_lock);
}

void cxio_hal_put_pdid(struct cxio_hal_resource *rscp, u32 pdid)
{
	cxio_hal_put_resource(&rscp->pdid_fifo, &rscp->pdid_fifo_lock, pdid);
}

void cxio_hal_destroy_resource(struct cxio_hal_resource *rscp)
{
	kfifo_free(&rscp->tpt_fifo);
	kfifo_free(&rscp->cqid_fifo);
	kfifo_free(&rscp->qpid_fifo);
	kfifo_free(&rscp->pdid_fifo);
	kfree(rscp);
}

/*
 * PBL Memory Manager.  Uses Linux generic allocator.
 */

#define MIN_PBL_SHIFT 8			/* 256B == min PBL size (32 entries) */

u32 cxio_hal_pblpool_alloc(struct cxio_rdev *rdev_p, int size)
{
	unsigned long addr = gen_pool_alloc(rdev_p->pbl_pool, size);
	pr_debug("%s addr 0x%x size %d\n", __func__, (u32)addr, size);
	return (u32)addr;
}

void cxio_hal_pblpool_free(struct cxio_rdev *rdev_p, u32 addr, int size)
{
	pr_debug("%s addr 0x%x size %d\n", __func__, addr, size);
	gen_pool_free(rdev_p->pbl_pool, (unsigned long)addr, size);
}

int cxio_hal_pblpool_create(struct cxio_rdev *rdev_p)
{
	unsigned pbl_start, pbl_chunk;

	rdev_p->pbl_pool = gen_pool_create(MIN_PBL_SHIFT, -1);
	if (!rdev_p->pbl_pool)
		return -ENOMEM;

	pbl_start = rdev_p->rnic_info.pbl_base;
	pbl_chunk = rdev_p->rnic_info.pbl_top - pbl_start + 1;

	while (pbl_start < rdev_p->rnic_info.pbl_top) {
		pbl_chunk = min(rdev_p->rnic_info.pbl_top - pbl_start + 1,
				pbl_chunk);
		if (gen_pool_add(rdev_p->pbl_pool, pbl_start, pbl_chunk, -1)) {
			pr_debug("%s failed to add PBL chunk (%x/%x)\n",
				 __func__, pbl_start, pbl_chunk);
			if (pbl_chunk <= 1024 << MIN_PBL_SHIFT) {
				pr_warn("%s: Failed to add all PBL chunks (%x/%x)\n",
					__func__, pbl_start,
					rdev_p->rnic_info.pbl_top - pbl_start);
				return 0;
			}
			pbl_chunk >>= 1;
		} else {
			pr_debug("%s added PBL chunk (%x/%x)\n",
				 __func__, pbl_start, pbl_chunk);
			pbl_start += pbl_chunk;
		}
	}

	return 0;
}

void cxio_hal_pblpool_destroy(struct cxio_rdev *rdev_p)
{
	gen_pool_destroy(rdev_p->pbl_pool);
}

/*
 * RQT Memory Manager.  Uses Linux generic allocator.
 */

#define MIN_RQT_SHIFT 10	/* 1KB == mini RQT size (16 entries) */
#define RQT_CHUNK 2*1024*1024

u32 cxio_hal_rqtpool_alloc(struct cxio_rdev *rdev_p, int size)
{
	unsigned long addr = gen_pool_alloc(rdev_p->rqt_pool, size << 6);
	pr_debug("%s addr 0x%x size %d\n", __func__, (u32)addr, size << 6);
	return (u32)addr;
}

void cxio_hal_rqtpool_free(struct cxio_rdev *rdev_p, u32 addr, int size)
{
	pr_debug("%s addr 0x%x size %d\n", __func__, addr, size << 6);
	gen_pool_free(rdev_p->rqt_pool, (unsigned long)addr, size << 6);
}

int cxio_hal_rqtpool_create(struct cxio_rdev *rdev_p)
{
	unsigned long i;
	rdev_p->rqt_pool = gen_pool_create(MIN_RQT_SHIFT, -1);
	if (rdev_p->rqt_pool)
		for (i = rdev_p->rnic_info.rqt_base;
		     i <= rdev_p->rnic_info.rqt_top - RQT_CHUNK + 1;
		     i += RQT_CHUNK)
			gen_pool_add(rdev_p->rqt_pool, i, RQT_CHUNK, -1);
	return rdev_p->rqt_pool ? 0 : -ENOMEM;
}

void cxio_hal_rqtpool_destroy(struct cxio_rdev *rdev_p)
{
	gen_pool_destroy(rdev_p->rqt_pool);
}
