/*
 * Support KVM gust page tracking
 *
 * This feature allows us to track page access in guest. Currently, only
 * write access is tracked.
 *
 * Copyright(C) 2015 Intel Corporation.
 *
 * Author:
 *   Xiao Guangrong <guangrong.xiao@linux.intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include <linux/kvm_host.h>
#include <asm/kvm_host.h>
#include <asm/kvm_page_track.h>

#include "mmu.h"

void kvm_page_track_free_memslot(struct kvm_memory_slot *free,
				 struct kvm_memory_slot *dont)
{
	int i;

	for (i = 0; i < KVM_PAGE_TRACK_MAX; i++)
		if (!dont || free->arch.gfn_track[i] !=
		      dont->arch.gfn_track[i]) {
			kvfree(free->arch.gfn_track[i]);
			free->arch.gfn_track[i] = NULL;
		}
}

int kvm_page_track_create_memslot(struct kvm_memory_slot *slot,
				  unsigned long npages)
{
	int  i;

	for (i = 0; i < KVM_PAGE_TRACK_MAX; i++) {
		slot->arch.gfn_track[i] = kvm_kvzalloc(npages *
					    sizeof(*slot->arch.gfn_track[i]));
		if (!slot->arch.gfn_track[i])
			goto track_free;
	}

	return 0;

track_free:
	kvm_page_track_free_memslot(slot, NULL);
	return -ENOMEM;
}
