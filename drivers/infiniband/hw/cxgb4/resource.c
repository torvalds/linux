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
#include <linux/spinlock.h>
#include <linux/genalloc.h>
#include <linux/ratelimit.h>
#include "iw_cxgb4.h"

static int c4iw_init_qid_table(struct c4iw_rdev *rdev)
{
	u32 i;

	if (c4iw_id_table_alloc(&rdev->resource.qid_table,
				rdev->lldi.vr->qp.start,
				rdev->lldi.vr->qp.size,
				rdev->lldi.vr->qp.size, 0))
		return -ENOMEM;

	for (i = rdev->lldi.vr->qp.start;
		i < rdev->lldi.vr->qp.start + rdev->lldi.vr->qp.size; i++)
		if (!(i & rdev->qpmask))
			c4iw_id_free(&rdev->resource.qid_table, i);
	return 0;
}

/* nr_* must be power of 2 */
int c4iw_init_resource(struct c4iw_rdev *rdev, u32 nr_tpt, u32 nr_pdid)
{
	int err = 0;
	err = c4iw_id_table_alloc(&rdev->resource.tpt_table, 0, nr_tpt, 1,
					C4IW_ID_TABLE_F_RANDOM);
	if (err)
		goto tpt_err;
	err = c4iw_init_qid_table(rdev);
	if (err)
		goto qid_err;
	err = c4iw_id_table_alloc(&rdev->resource.pdid_table, 0,
					nr_pdid, 1, 0);
	if (err)
		goto pdid_err;
	return 0;
 pdid_err:
	c4iw_id_table_free(&rdev->resource.qid_table);
 qid_err:
	c4iw_id_table_free(&rdev->resource.tpt_table);
 tpt_err:
	return -ENOMEM;
}

/*
 * returns 0 if no resource available
 */
u32 c4iw_get_resource(struct c4iw_id_table *id_table)
{
	u32 entry;
	entry = c4iw_id_alloc(id_table);
	if (entry == (u32)(-1))
		return 0;
	return entry;
}

void c4iw_put_resource(struct c4iw_id_table *id_table, u32 entry)
{
	pr_debug("entry 0x%x\n", entry);
	c4iw_id_free(id_table, entry);
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
		qid = c4iw_get_resource(&rdev->resource.qid_table);
		if (!qid)
			goto out;
		mutex_lock(&rdev->stats.lock);
		rdev->stats.qid.cur += rdev->qpmask + 1;
		mutex_unlock(&rdev->stats.lock);
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
	pr_debug("qid 0x%x\n", qid);
	mutex_lock(&rdev->stats.lock);
	if (rdev->stats.qid.cur > rdev->stats.qid.max)
		rdev->stats.qid.max = rdev->stats.qid.cur;
	mutex_unlock(&rdev->stats.lock);
	return qid;
}

void c4iw_put_cqid(struct c4iw_rdev *rdev, u32 qid,
		   struct c4iw_dev_ucontext *uctx)
{
	struct c4iw_qid_list *entry;

	entry = kmalloc(sizeof *entry, GFP_KERNEL);
	if (!entry)
		return;
	pr_debug("qid 0x%x\n", qid);
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
		qid = c4iw_get_resource(&rdev->resource.qid_table);
		if (!qid) {
			mutex_lock(&rdev->stats.lock);
			rdev->stats.qid.fail++;
			mutex_unlock(&rdev->stats.lock);
			goto out;
		}
		mutex_lock(&rdev->stats.lock);
		rdev->stats.qid.cur += rdev->qpmask + 1;
		mutex_unlock(&rdev->stats.lock);
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
	pr_debug("qid 0x%x\n", qid);
	mutex_lock(&rdev->stats.lock);
	if (rdev->stats.qid.cur > rdev->stats.qid.max)
		rdev->stats.qid.max = rdev->stats.qid.cur;
	mutex_unlock(&rdev->stats.lock);
	return qid;
}

void c4iw_put_qpid(struct c4iw_rdev *rdev, u32 qid,
		   struct c4iw_dev_ucontext *uctx)
{
	struct c4iw_qid_list *entry;

	entry = kmalloc(sizeof *entry, GFP_KERNEL);
	if (!entry)
		return;
	pr_debug("qid 0x%x\n", qid);
	entry->qid = qid;
	mutex_lock(&uctx->lock);
	list_add_tail(&entry->entry, &uctx->qpids);
	mutex_unlock(&uctx->lock);
}

void c4iw_destroy_resource(struct c4iw_resource *rscp)
{
	c4iw_id_table_free(&rscp->tpt_table);
	c4iw_id_table_free(&rscp->qid_table);
	c4iw_id_table_free(&rscp->pdid_table);
}

/*
 * PBL Memory Manager.  Uses Linux generic allocator.
 */

#define MIN_PBL_SHIFT 8			/* 256B == min PBL size (32 entries) */

u32 c4iw_pblpool_alloc(struct c4iw_rdev *rdev, int size)
{
	unsigned long addr = gen_pool_alloc(rdev->pbl_pool, size);
	pr_debug("addr 0x%x size %d\n", (u32)addr, size);
	mutex_lock(&rdev->stats.lock);
	if (addr) {
		rdev->stats.pbl.cur += roundup(size, 1 << MIN_PBL_SHIFT);
		if (rdev->stats.pbl.cur > rdev->stats.pbl.max)
			rdev->stats.pbl.max = rdev->stats.pbl.cur;
	} else
		rdev->stats.pbl.fail++;
	mutex_unlock(&rdev->stats.lock);
	return (u32)addr;
}

void c4iw_pblpool_free(struct c4iw_rdev *rdev, u32 addr, int size)
{
	pr_debug("addr 0x%x size %d\n", addr, size);
	mutex_lock(&rdev->stats.lock);
	rdev->stats.pbl.cur -= roundup(size, 1 << MIN_PBL_SHIFT);
	mutex_unlock(&rdev->stats.lock);
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
			pr_debug("failed to add PBL chunk (%x/%x)\n",
				 pbl_start, pbl_chunk);
			if (pbl_chunk <= 1024 << MIN_PBL_SHIFT) {
				pr_warn("Failed to add all PBL chunks (%x/%x)\n",
					pbl_start, pbl_top - pbl_start);
				return 0;
			}
			pbl_chunk >>= 1;
		} else {
			pr_debug("added PBL chunk (%x/%x)\n",
				 pbl_start, pbl_chunk);
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
	pr_debug("addr 0x%x size %d\n", (u32)addr, size << 6);
	if (!addr)
		pr_warn_ratelimited("%s: Out of RQT memory\n",
				    pci_name(rdev->lldi.pdev));
	mutex_lock(&rdev->stats.lock);
	if (addr) {
		rdev->stats.rqt.cur += roundup(size << 6, 1 << MIN_RQT_SHIFT);
		if (rdev->stats.rqt.cur > rdev->stats.rqt.max)
			rdev->stats.rqt.max = rdev->stats.rqt.cur;
	} else
		rdev->stats.rqt.fail++;
	mutex_unlock(&rdev->stats.lock);
	return (u32)addr;
}

void c4iw_rqtpool_free(struct c4iw_rdev *rdev, u32 addr, int size)
{
	pr_debug("addr 0x%x size %d\n", addr, size << 6);
	mutex_lock(&rdev->stats.lock);
	rdev->stats.rqt.cur -= roundup(size << 6, 1 << MIN_RQT_SHIFT);
	mutex_unlock(&rdev->stats.lock);
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
			pr_debug("failed to add RQT chunk (%x/%x)\n",
				 rqt_start, rqt_chunk);
			if (rqt_chunk <= 1024 << MIN_RQT_SHIFT) {
				pr_warn("Failed to add all RQT chunks (%x/%x)\n",
					rqt_start, rqt_top - rqt_start);
				return 0;
			}
			rqt_chunk >>= 1;
		} else {
			pr_debug("added RQT chunk (%x/%x)\n",
				 rqt_start, rqt_chunk);
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
	pr_debug("addr 0x%x size %d\n", (u32)addr, size);
	if (addr) {
		mutex_lock(&rdev->stats.lock);
		rdev->stats.ocqp.cur += roundup(size, 1 << MIN_OCQP_SHIFT);
		if (rdev->stats.ocqp.cur > rdev->stats.ocqp.max)
			rdev->stats.ocqp.max = rdev->stats.ocqp.cur;
		mutex_unlock(&rdev->stats.lock);
	}
	return (u32)addr;
}

void c4iw_ocqp_pool_free(struct c4iw_rdev *rdev, u32 addr, int size)
{
	pr_debug("addr 0x%x size %d\n", addr, size);
	mutex_lock(&rdev->stats.lock);
	rdev->stats.ocqp.cur -= roundup(size, 1 << MIN_OCQP_SHIFT);
	mutex_unlock(&rdev->stats.lock);
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
			pr_debug("failed to add OCQP chunk (%x/%x)\n",
				 start, chunk);
			if (chunk <= 1024 << MIN_OCQP_SHIFT) {
				pr_warn("Failed to add all OCQP chunks (%x/%x)\n",
					start, top - start);
				return 0;
			}
			chunk >>= 1;
		} else {
			pr_debug("added OCQP chunk (%x/%x)\n",
				 start, chunk);
			start += chunk;
		}
	}
	return 0;
}

void c4iw_ocqp_pool_destroy(struct c4iw_rdev *rdev)
{
	gen_pool_destroy(rdev->ocqp_pool);
}
