/******************************************************************************
 * arch/xen/drivers/blkif/backend/interface.c
 *
 * Block-device interface management.
 *
 * Copyright (c) 2004, Keir Fraser
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "common.h"
#include <xen/events.h>
#include <xen/grant_table.h>
#include <linux/kthread.h>

static struct kmem_cache *blkif_cachep;

blkif_t *blkif_alloc(domid_t domid)
{
	blkif_t *blkif;

	blkif = kmem_cache_alloc(blkif_cachep, GFP_KERNEL);
	if (!blkif)
		return ERR_PTR(-ENOMEM);

	memset(blkif, 0, sizeof(*blkif));
	blkif->domid = domid;
	spin_lock_init(&blkif->blk_ring_lock);
	atomic_set(&blkif->refcnt, 1);
	init_waitqueue_head(&blkif->wq);
	blkif->st_print = jiffies;
	init_waitqueue_head(&blkif->waiting_to_free);

	return blkif;
}

static int map_frontend_page(blkif_t *blkif, unsigned long shared_page)
{
	struct gnttab_map_grant_ref op;

	gnttab_set_map_op(&op, (unsigned long)blkif->blk_ring_area->addr,
			  GNTMAP_host_map, shared_page, blkif->domid);

	if (HYPERVISOR_grant_table_op(GNTTABOP_map_grant_ref, &op, 1))
		BUG();

	if (op.status) {
		DPRINTK(" Grant table operation failure !\n");
		return op.status;
	}

	blkif->shmem_ref = shared_page;
	blkif->shmem_handle = op.handle;

	return 0;
}

static void unmap_frontend_page(blkif_t *blkif)
{
	struct gnttab_unmap_grant_ref op;

	gnttab_set_unmap_op(&op, (unsigned long)blkif->blk_ring_area->addr,
			    GNTMAP_host_map, blkif->shmem_handle);

	if (HYPERVISOR_grant_table_op(GNTTABOP_unmap_grant_ref, &op, 1))
		BUG();
}

int blkif_map(blkif_t *blkif, unsigned long shared_page, unsigned int evtchn)
{
	int err;

	/* Already connected through? */
	if (blkif->irq)
		return 0;

	if ( (blkif->blk_ring_area = alloc_vm_area(PAGE_SIZE)) == NULL )
		return -ENOMEM;

	err = map_frontend_page(blkif, shared_page);
	if (err) {
		free_vm_area(blkif->blk_ring_area);
		return err;
	}

	switch (blkif->blk_protocol) {
	case BLKIF_PROTOCOL_NATIVE:
	{
		struct blkif_sring *sring;
		sring = (struct blkif_sring *)blkif->blk_ring_area->addr;
		BACK_RING_INIT(&blkif->blk_rings.native, sring, PAGE_SIZE);
		break;
	}
	case BLKIF_PROTOCOL_X86_32:
	{
		struct blkif_x86_32_sring *sring_x86_32;
		sring_x86_32 = (struct blkif_x86_32_sring *)blkif->blk_ring_area->addr;
		BACK_RING_INIT(&blkif->blk_rings.x86_32, sring_x86_32, PAGE_SIZE);
		break;
	}
	case BLKIF_PROTOCOL_X86_64:
	{
		struct blkif_x86_64_sring *sring_x86_64;
		sring_x86_64 = (struct blkif_x86_64_sring *)blkif->blk_ring_area->addr;
		BACK_RING_INIT(&blkif->blk_rings.x86_64, sring_x86_64, PAGE_SIZE);
		break;
	}
	default:
		BUG();
	}

	err = bind_interdomain_evtchn_to_irqhandler(
		blkif->domid, evtchn, blkif_be_int, 0, "blkif-backend", blkif);
	if (err < 0)
	{
		unmap_frontend_page(blkif);
		free_vm_area(blkif->blk_ring_area);
		blkif->blk_rings.common.sring = NULL;
		return err;
	}
	blkif->irq = err;

	return 0;
}

void blkif_disconnect(blkif_t *blkif)
{
	if (blkif->xenblkd) {
		kthread_stop(blkif->xenblkd);
		blkif->xenblkd = NULL;
	}

	atomic_dec(&blkif->refcnt);
	wait_event(blkif->waiting_to_free, atomic_read(&blkif->refcnt) == 0);
	atomic_inc(&blkif->refcnt);

	if (blkif->irq) {
		unbind_from_irqhandler(blkif->irq, blkif);
		blkif->irq = 0;
	}

	if (blkif->blk_rings.common.sring) {
		unmap_frontend_page(blkif);
		free_vm_area(blkif->blk_ring_area);
		blkif->blk_rings.common.sring = NULL;
	}
}

void blkif_free(blkif_t *blkif)
{
	if (!atomic_dec_and_test(&blkif->refcnt))
		BUG();
	kmem_cache_free(blkif_cachep, blkif);
}

void __init blkif_interface_init(void)
{
	blkif_cachep = kmem_cache_create("blkif_cache", sizeof(blkif_t),
					 0, 0, NULL);
}
