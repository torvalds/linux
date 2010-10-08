/*
 * drivers/video/tegra/nvmap_mru.c
 *
 * IOVMM virtualization support for nvmap
 *
 * Copyright (c) 2009-2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/list.h>
#include <linux/slab.h>

#include <asm/pgtable.h>

#include <mach/iovmm.h>

#include "nvmap.h"
#include "nvmap_mru.h"

/* if IOVMM reclamation is enabled (CONFIG_NVMAP_RECLAIM_UNPINNED_VM),
 * unpinned handles are placed onto a most-recently-used eviction list;
 * multiple lists are maintained, segmented by size (sizes were chosen to
 * roughly correspond with common sizes for graphics surfaces).
 *
 * if a handle is located on the MRU list, then the code below may
 * steal its IOVMM area at any time to satisfy a pin operation if no
 * free IOVMM space is available
 */

static const size_t mru_cutoff[] = {
	262144, 393216, 786432, 1048576, 1572864
};

static inline struct list_head *mru_list(struct nvmap_share *share, size_t size)
{
	unsigned int i;

	BUG_ON(!share->mru_lists);
	for (i = 0; i < ARRAY_SIZE(mru_cutoff); i++)
		if (size <= mru_cutoff[i])
			break;

	return &share->mru_lists[i];
}

size_t nvmap_mru_vm_size(struct tegra_iovmm_client *iovmm)
{
	size_t vm_size = tegra_iovmm_get_vm_size(iovmm);
	return (vm_size >> 2) * 3;
}

/*  nvmap_mru_vma_lock should be acquired by the caller before calling this */
void nvmap_mru_insert_locked(struct nvmap_share *share, struct nvmap_handle *h)
{
	size_t len = h->pgalloc.area->iovm_length;
	list_add(&h->pgalloc.mru_list, mru_list(share, len));
}

void nvmap_mru_remove(struct nvmap_share *s, struct nvmap_handle *h)
{
	nvmap_mru_lock(s);
	if (!list_empty(&h->pgalloc.mru_list))
		list_del(&h->pgalloc.mru_list);
	nvmap_mru_unlock(s);
	INIT_LIST_HEAD(&h->pgalloc.mru_list);
}

/* returns a tegra_iovmm_area for a handle. if the handle already has
 * an iovmm_area allocated, the handle is simply removed from its MRU list
 * and the existing iovmm_area is returned.
 *
 * if no existing allocation exists, try to allocate a new IOVMM area.
 *
 * if a new area can not be allocated, try to re-use the most-recently-unpinned
 * handle's allocation.
 *
 * and if that fails, iteratively evict handles from the MRU lists and free
 * their allocations, until the new allocation succeeds.
 */
struct tegra_iovmm_area *nvmap_handle_iovmm(struct nvmap_client *c,
					    struct nvmap_handle *h)
{
	struct list_head *mru;
	struct nvmap_handle *evict = NULL;
	struct tegra_iovmm_area *vm = NULL;
	unsigned int i, idx;
	pgprot_t prot;

	BUG_ON(!h || !c || !c->share);

	prot = nvmap_pgprot(h, pgprot_kernel);

	if (h->pgalloc.area) {
		/* since this is only called inside the pin lock, and the
		 * handle is gotten before it is pinned, there are no races
		 * where h->pgalloc.area is changed after the comparison */
		nvmap_mru_lock(c->share);
		BUG_ON(list_empty(&h->pgalloc.mru_list));
		list_del(&h->pgalloc.mru_list);
		INIT_LIST_HEAD(&h->pgalloc.mru_list);
		nvmap_mru_unlock(c->share);
		return h->pgalloc.area;
	}

	vm = tegra_iovmm_create_vm(c->share->iovmm, NULL, h->size, prot);

	if (vm) {
		INIT_LIST_HEAD(&h->pgalloc.mru_list);
		return vm;
	}
	/* attempt to re-use the most recently unpinned IOVMM area in the
	 * same size bin as the current handle. If that fails, iteratively
	 * evict handles (starting from the current bin) until an allocation
	 * succeeds or no more areas can be evicted */

	nvmap_mru_lock(c->share);
	mru = mru_list(c->share, h->size);
	if (!list_empty(mru))
		evict = list_first_entry(mru, struct nvmap_handle,
					 pgalloc.mru_list);

	if (evict && evict->pgalloc.area->iovm_length >= h->size) {
		list_del(&evict->pgalloc.mru_list);
		vm = evict->pgalloc.area;
		evict->pgalloc.area = NULL;
		INIT_LIST_HEAD(&evict->pgalloc.mru_list);
		nvmap_mru_unlock(c->share);
		return vm;
	}

	idx = mru - c->share->mru_lists;

	for (i = 0; i < c->share->nr_mru && !vm; i++, idx++) {
		if (idx >= c->share->nr_mru)
			idx = 0;
		mru = &c->share->mru_lists[idx];
		while (!list_empty(mru) && !vm) {
			evict = list_first_entry(mru, struct nvmap_handle,
						 pgalloc.mru_list);

			BUG_ON(atomic_read(&evict->pin) != 0);
			BUG_ON(!evict->pgalloc.area);
			list_del(&evict->pgalloc.mru_list);
			INIT_LIST_HEAD(&evict->pgalloc.mru_list);
			nvmap_mru_unlock(c->share);
			tegra_iovmm_free_vm(evict->pgalloc.area);
			evict->pgalloc.area = NULL;
			vm = tegra_iovmm_create_vm(c->share->iovmm,
						   NULL, h->size, prot);
			nvmap_mru_lock(c->share);
		}
	}
	nvmap_mru_unlock(c->share);
	return vm;
}

int nvmap_mru_init(struct nvmap_share *share)
{
	int i;
	spin_lock_init(&share->mru_lock);
	share->nr_mru = ARRAY_SIZE(mru_cutoff) + 1;

	share->mru_lists = kzalloc(sizeof(struct list_head) * share->nr_mru,
				   GFP_KERNEL);

	if (!share->mru_lists)
		return -ENOMEM;

	for (i = 0; i <= share->nr_mru; i++)
		INIT_LIST_HEAD(&share->mru_lists[i]);

	return 0;
}

void nvmap_mru_destroy(struct nvmap_share *share)
{
	if (share->mru_lists)
		kfree(share->mru_lists);

	share->mru_lists = NULL;
}
