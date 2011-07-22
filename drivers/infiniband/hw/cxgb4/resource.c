/*
 * Copyright (c) 2009-2010 Chelsio, Inc. All rights reserved.
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
#include <linux/genalloc.h>
#include <linux/ratelimit.h>
#include "iw_cxgb4.h"

#define RANDOM_SIZE 16

static int __c4iw_init_resource_fifo(struct kfifo *fifo,
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
		random_bytes = random32();
		for (i = 0; i < RANDOM_SIZE; i++)
			rarray[i] = i + skip_low;
		for (i = skip_low + RANDOM_SIZE; i < nr - skip_high; i++) {
			if (j >= RANDOM_SIZE) {
				j = 0;
				random_bytes = random32();
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
				     sizeof(u32), fifo_lock))
			break;
	return 0;
}

static int c4iw_init_resource_fifo(struct kfifo *fifo, spinlock_t * fifo_lock,
				   u32 nr, u32 skip_low, u32 skip_high)
{
	return __c4iw_init_resource_fifo(fifo, fifo_lock, nr, skip_low,
					  skip_high, 0);
}

static int c4iw_init_resource_fifo_random(struct kfifo *fifo,
				   spinlock_t *fifo_lock,
				   u32 nr, u32 skip_low, u32 skip_high)
{
	return __c4iw_init_resource_fifo(fifo, fifo_lock, nr, skip_low,
					  skip_high, 1);
}

static int c4iw_init_qid_fifo(struct c4iw_rdev *rdev)
{
	u32 i;

	spin_lock_init(&rdev->resource.qid_fifo_lock);

	if (kfifo_alloc(&rdev->resource.qid_fifo, rdev->lldi.vr->qp.size *
			sizeof(u32), GFP_KERNEL))
		return -ENOMEM;

	for (i = rdev->lldi.vr->qp.start;
	     i < rdev->lldi.vr->qp.start + rdev->lldi.vr->qp.size; i++)
		if (!(i & rdev->qpmask))
			kfifo_in(&rdev->resource.qid_fifo,
				    (unsigned char *) &i, sizeof(u32));
	return 0;
}

/* nr_* must be power of 2 */
int c4iw_init_resource(struct c4iw_rdev *rdev, u32 nr_tpt, u32 nr_pdid)
{
	int err = 0;
	err = c4iw_init_resource_fifo_random(&rdev->resource.tpt_fifo,
					     &rdev->resource.tpt_fifo_lock,
					     nr_tpt, 1, 0);
	if (err)
		goto tpt_err;
	err = c4iw_init_qid_fifo(rdev);
	if (err)
		goto qid_err;
	err = c4iw_init_resource_fifo(&rdev->resource.pdid_fifo,
				      &rdev->resource.pdid_fifo_lock,
				      nr_pdid, 1, 0);
	if (err)
		goto pdid_err;
	return 0;
pdid_err:
	kfifo_free(&rdev->resource.qid_fifo);
qid_err:
	kfifo_free(&rdev->resource.tpt_fifo);
tpt_err:
	return -ENOMEM;
}

/*
 * returns 0 if no resource available
 */
u32 c4iw_get_resource(struct kfifo *fifo, spinlock_t *lock)
{
	u32 entry;
	if (kfifo_out_locked(fifo, (unsigned char *) &entry, sizeof(u32), lock))
		return entry;
	else
		return 0;
}

void c4iw_put_resource(struct kfifo *fifo, u32 entry, spinlock_t *lock)
{
	PDBG("%s entry 0x%x\n", __func__, entry);
	kfifo_in_locked(fifo, (unsigned char *) &entry, sizeof(u32), lock);
}

u32 c4iw_get_cqid(struct c4iw_rdev *rdev, struct c4iw_dev_ucontext *uctx)
{
	struct c4iw_qid_list *entry;
	u32 qid;
	int i;

	mutex_lock(&uctx->lock);
	if (!list_empty(&uctx->cqids)) {
		entry = list_entry(uctx->cqids.next, struct c4iw_qid_list,
				   entry);
		list_del(&entry->entry);
		qid = entry->qid;
		kfree(entry);
	} else {
		qid = c4iw_get_resource(&rdev->resource.qid_fifo,
					&rdev->resource.qid_fifo_lock);
		if (!qid)
			goto out;
		for (i = qid+1; i & rdev->qpmask; i++) {
			entry = kmalloc(sizeof *entry, GFP_KERNEL);
			if (!entry)
				goto out;
			entry->qid = i;
			list_add_tail(&entry->entry, &uctx->cqids);
		}

		/*
		 * now put the same ids on the qp list since they all
		 * map to the same db/gts page.
		 */
		entry = kmalloc(sizeof *entry, GFP_KERNEL);
		if (!entry)
			goto out;
		entry->qid = qid;
		list_add_tail(&entry->entry, &uctx->qpids);
		for (i = qid+1; i & rdev->qpmask; i++) {
			entry = kmalloc(sizeof *entry, GFP_KERNEL);
			if (!entry)
				goto out;
			entry->qid = i;
			list_add_tail(&entry->entry, &uctx->qpids);
		}
	}
out:
	mutex_unlock(&uctx->lock);
	PDBG("%s qid 0x%x\n", __func__, qid);
	return qid;
}

void c4iw_put_cqid(struct c4iw_rdev *rdev, u32 qid,
		   struct c4iw_dev_ucontext *uctx)
{
	struct c4iw_qid_list *entry;

	entry = kmalloc(sizeof *entry, GFP_KERNEL);
	if (!entry)
		return;
	PDBG("%s qid 0x%x\n", __func__, qid);
	entry->qid = qid;
	mutex_lock(&uctx->lock);
	list_add_tail(&entry->entry, &uctx->cqids);
	mutex_unlock(&uctx->lock);
}

u32 c4iw_get_qpid(struct c4iw_rdev *rdev, struct c4iw_dev_ucontext *uctx)
{
	struct c4iw_qid_list *entry;
	u32 qid;
	int i;

	mutex_lock(&uctx->lock);
	if (!list_empty(&uctx->qpids)) {
		entry = list_entry(uctx->qpids.next, struct c4iw_qid_list,
				   entry);
		list_del(&entry->entry);
		qid = entry->qid;
		kfree(entry);
	} else {
		qid = c4iw_get_resource(&rdev->resource.qid_fifo,
					&rdev->resource.qid_fifo_lock);
		if (!qid)
			goto out;
		for (i = qid+1; i & rdev->qpmask; i++) {
			entry = kmalloc(sizeof *entry, GFP_KERNEL);
			if (!entry)
				goto out;
			entry->qid = i;
			list_add_tail(&entry->entry, &uctx->qpids);
		}

		/*
		 * now put the same ids on the cq list since they all
		 * map to the same db/gts page.
		 */
		entry = kmalloc(sizeof *entry, GFP_KERNEL);
		if (!entry)
			goto out;
		entry->qid = qid;
		list_add_tail(&entry->entry, &uctx->cqids);
		for (i = qid; i & rdev->qpmask; i++) {
			entry = kmalloc(sizeof *entry, GFP_KERNEL);
			if (!entry)
				goto out;
			entry->qid = i;
			list_add_tail(&entry->entry, &uctx->cqids);
		}
	}
out:
	mutex_unlock(&uctx->lock);
	PDBG("%s qid 0x%x\n", __func__, qid);
	return qid;
}

void c4iw_put_qpid(struct c4iw_rdev *rdev, u32 qid,
		   struct c4iw_dev_ucontext *uctx)
{
	struct c4iw_qid_list *entry;

	entry = kmalloc(sizeof *entry, GFP_KERNEL);
	if (!entry)
		return;
	PDBG("%s qid 0x%x\n", __func__, qid);
	entry->qid = qid;
	mutex_lock(&uctx->lock);
	list_add_tail(&entry->entry, &uctx->qpids);
	mutex_unlock(&uctx->lock);
}

void c4iw_destroy_resource(struct c4iw_resource *rscp)
{
	kfifo_free(&rscp->tpt_fifo);
	kfifo_free(&rscp->qid_fifo);
	kfifo_free(&rscp->pdid_fifo);
}

/*
 * PBL Memory Manager.  Uses Linux generic allocator.
 */

#define MIN_PBL_SHIFT 8			/* 256B == min PBL size (32 entries) */

u32 c4iw_pblpool_alloc(struct c4iw_rdev *rdev, int size)
{
	unsigned long addr = gen_pool_alloc(rdev->pbl_pool, size);
	PDBG("%s addr 0x%x size %d\n", __func__, (u32)addr, size);
	if (!addr)
		printk_ratelimited(KERN_WARNING MOD "%s: Out of PBL memory\n",
		       pci_name(rdev->lldi.pdev));
	return (u32)addr;
}

void c4iw_pblpool_free(struct c4iw_rdev *rdev, u32 addr, int size)
{
	PDBG("%s addr 0x%x size %d\n", __func__, addr, size);
	gen_pool_free(rdev->pbl_pool, (unsigned long)addr, size);
}

int c4iw_pblpool_create(struct c4iw_rdev *rdev)
{
	unsigned pbl_start, pbl_chunk, pbl_top;

	rdev->pbl_pool = gen_pool_create(MIN_PBL_SHIFT, -1);
	if (!rdev->pbl_pool)
		return -ENOMEM;

	pbl_start = rdev->lldi.vr->pbl.start;
	pbl_chunk = rdev->lldi.vr->pbl.size;
	pbl_top = pbl_start + pbl_chunk;

	while (pbl_start < pbl_top) {
		pbl_chunk = min(pbl_top - pbl_start + 1, pbl_chunk);
		if (gen_pool_add(rdev->pbl_pool, pbl_start, pbl_chunk, -1)) {
			PDBG("%s failed to add PBL chunk (%x/%x)\n",
			     __func__, pbl_start, pbl_chunk);
			if (pbl_chunk <= 1024 << MIN_PBL_SHIFT) {
				printk(KERN_WARNING MOD
				       "Failed to add all PBL chunks (%x/%x)\n",
				       pbl_start,
				       pbl_top - pbl_start);
				return 0;
			}
			pbl_chunk >>= 1;
		} else {
			PDBG("%s added PBL chunk (%x/%x)\n",
			     __func__, pbl_start, pbl_chunk);
			pbl_start += pbl_chunk;
		}
	}

	return 0;
}

void c4iw_pblpool_destroy(struct c4iw_rdev *rdev)
{
	gen_pool_destroy(rdev->pbl_pool);
}

/*
 * RQT Memory Manager.  Uses Linux generic allocator.
 */

#define MIN_RQT_SHIFT 10	/* 1KB == min RQT size (16 entries) */

u32 c4iw_rqtpool_alloc(struct c4iw_rdev *rdev, int size)
{
	unsigned long addr = gen_pool_alloc(rdev->rqt_pool, size << 6);
	PDBG("%s addr 0x%x size %d\n", __func__, (u32)addr, size << 6);
	if (!addr)
		printk_ratelimited(KERN_WARNING MOD "%s: Out of RQT memory\n",
		       pci_name(rdev->lldi.pdev));
	return (u32)addr;
}

void c4iw_rqtpool_free(struct c4iw_rdev *rdev, u32 addr, int size)
{
	PDBG("%s addr 0x%x size %d\n", __func__, addr, size << 6);
	gen_pool_free(rdev->rqt_pool, (unsigned long)addr, size << 6);
}

int c4iw_rqtpool_create(struct c4iw_rdev *rdev)
{
	unsigned rqt_start, rqt_chunk, rqt_top;

	rdev->rqt_pool = gen_pool_create(MIN_RQT_SHIFT, -1);
	if (!rdev->rqt_pool)
		return -ENOMEM;

	rqt_start = rdev->lldi.vr->rq.start;
	rqt_chunk = rdev->lldi.vr->rq.size;
	rqt_top = rqt_start + rqt_chunk;

	while (rqt_start < rqt_top) {
		rqt_chunk = min(rqt_top - rqt_start + 1, rqt_chunk);
		if (gen_pool_add(rdev->rqt_pool, rqt_start, rqt_chunk, -1)) {
			PDBG("%s failed to add RQT chunk (%x/%x)\n",
			     __func__, rqt_start, rqt_chunk);
			if (rqt_chunk <= 1024 << MIN_RQT_SHIFT) {
				printk(KERN_WARNING MOD
				       "Failed to add all RQT chunks (%x/%x)\n",
				       rqt_start, rqt_top - rqt_start);
				return 0;
			}
			rqt_chunk >>= 1;
		} else {
			PDBG("%s added RQT chunk (%x/%x)\n",
			     __func__, rqt_start, rqt_chunk);
			rqt_start += rqt_chunk;
		}
	}
	return 0;
}

void c4iw_rqtpool_destroy(struct c4iw_rdev *rdev)
{
	gen_pool_destroy(rdev->rqt_pool);
}

/*
 * On-Chip QP Memory.
 */
#define MIN_OCQP_SHIFT 12	/* 4KB == min ocqp size */

u32 c4iw_ocqp_pool_alloc(struct c4iw_rdev *rdev, int size)
{
	unsigned long addr = gen_pool_alloc(rdev->ocqp_pool, size);
	PDBG("%s addr 0x%x size %d\n", __func__, (u32)addr, size);
	return (u32)addr;
}

void c4iw_ocqp_pool_free(struct c4iw_rdev *rdev, u32 addr, int size)
{
	PDBG("%s addr 0x%x size %d\n", __func__, addr, size);
	gen_pool_free(rdev->ocqp_pool, (unsigned long)addr, size);
}

int c4iw_ocqp_pool_create(struct c4iw_rdev *rdev)
{
	unsigned start, chunk, top;

	rdev->ocqp_pool = gen_pool_create(MIN_OCQP_SHIFT, -1);
	if (!rdev->ocqp_pool)
		return -ENOMEM;

	start = rdev->lldi.vr->ocq.start;
	chunk = rdev->lldi.vr->ocq.size;
	top = start + chunk;

	while (start < top) {
		chunk = min(top - start + 1, chunk);
		if (gen_pool_add(rdev->ocqp_pool, start, chunk, -1)) {
			PDBG("%s failed to add OCQP chunk (%x/%x)\n",
			     __func__, start, chunk);
			if (chunk <= 1024 << MIN_OCQP_SHIFT) {
				printk(KERN_WARNING MOD
				       "Failed to add all OCQP chunks (%x/%x)\n",
				       start, top - start);
				return 0;
			}
			chunk >>= 1;
		} else {
			PDBG("%s added OCQP chunk (%x/%x)\n",
			     __func__, start, chunk);
			start += chunk;
		}
	}
	return 0;
}

void c4iw_ocqp_pool_destroy(struct c4iw_rdev *rdev)
{
	gen_pool_destroy(rdev->ocqp_pool);
}
