// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "gh_vm_mgr: " fmt

#include <linux/gunyah_rsc_mgr.h>
#include <linux/mm.h>

#include <uapi/linux/gunyah.h>

#include "vm_mgr.h"

static bool pages_are_mergeable(struct page *a, struct page *b)
{
	return page_to_pfn(a) + 1 == page_to_pfn(b);
}

static bool gh_vm_mem_overlap(struct gh_vm_mem *a, u64 addr, u64 size)
{
	u64 a_end = a->guest_phys_addr + (a->npages << PAGE_SHIFT);
	u64 end = addr + size;

	return a->guest_phys_addr < end && addr < a_end;
}

static struct gh_vm_mem *__gh_vm_mem_find_by_label(struct gh_vm *ghvm, u32 label)
	__must_hold(&ghvm->mm_lock)
{
	struct gh_vm_mem *mapping;

	list_for_each_entry(mapping, &ghvm->memory_mappings, list)
		if (mapping->parcel.label == label)
			return mapping;

	return NULL;
}

static void gh_vm_mem_reclaim_mapping(struct gh_vm *ghvm, struct gh_vm_mem *mapping)
	__must_hold(&ghvm->mm_lock)
{
	int ret = 0;

	if (mapping->parcel.mem_handle != GH_MEM_HANDLE_INVAL) {
		ret = gh_rm_mem_reclaim(ghvm->rm, &mapping->parcel);
		if (ret)
			pr_warn("Failed to reclaim memory parcel for label %d: %d\n",
				mapping->parcel.label, ret);
	}

	if (!ret) {
		unpin_user_pages(mapping->pages, mapping->npages);
		account_locked_vm(ghvm->mm, mapping->npages, false);
	}

	kfree(mapping->pages);
	kfree(mapping->parcel.acl_entries);
	kfree(mapping->parcel.mem_entries);

	list_del(&mapping->list);
}

void gh_vm_mem_reclaim(struct gh_vm *ghvm)
{
	struct gh_vm_mem *mapping, *tmp;

	mutex_lock(&ghvm->mm_lock);

	list_for_each_entry_safe(mapping, tmp, &ghvm->memory_mappings, list) {
		gh_vm_mem_reclaim_mapping(ghvm, mapping);
		kfree(mapping);
	}

	mutex_unlock(&ghvm->mm_lock);
}

struct gh_vm_mem *gh_vm_mem_find_by_addr(struct gh_vm *ghvm, u64 guest_phys_addr, u32 size)
{
	struct gh_vm_mem *mapping;

	if (overflows_type(guest_phys_addr + size, u64))
		return NULL;

	mutex_lock(&ghvm->mm_lock);

	list_for_each_entry(mapping, &ghvm->memory_mappings, list) {
		if (gh_vm_mem_overlap(mapping, guest_phys_addr, size))
			goto unlock;
	}

	mapping = NULL;
unlock:
	mutex_unlock(&ghvm->mm_lock);
	return mapping;
}

int gh_vm_mem_alloc(struct gh_vm *ghvm, struct gh_userspace_memory_region *region, bool lend)
{
	struct gh_vm_mem *mapping, *tmp_mapping;
	struct page *curr_page, *prev_page;
	struct gh_rm_mem_parcel *parcel;
	int i, j, pinned, ret = 0;
	unsigned int gup_flags;
	size_t entry_size;
	u16 vmid;

	if (!region->memory_size || !PAGE_ALIGNED(region->memory_size) ||
		!PAGE_ALIGNED(region->userspace_addr) ||
		!PAGE_ALIGNED(region->guest_phys_addr))
		return -EINVAL;

	if (overflows_type(region->guest_phys_addr + region->memory_size, u64))
		return -EOVERFLOW;

	ret = mutex_lock_interruptible(&ghvm->mm_lock);
	if (ret)
		return ret;

	mapping = __gh_vm_mem_find_by_label(ghvm, region->label);
	if (mapping) {
		ret = -EEXIST;
		goto unlock;
	}

	list_for_each_entry(tmp_mapping, &ghvm->memory_mappings, list) {
		if (gh_vm_mem_overlap(tmp_mapping, region->guest_phys_addr,
					region->memory_size)) {
			ret = -EEXIST;
			goto unlock;
		}
	}

	mapping = kzalloc(sizeof(*mapping), GFP_KERNEL_ACCOUNT);
	if (!mapping) {
		ret = -ENOMEM;
		goto unlock;
	}

	mapping->guest_phys_addr = region->guest_phys_addr;
	mapping->npages = region->memory_size >> PAGE_SHIFT;
	parcel = &mapping->parcel;
	parcel->label = region->label;
	parcel->mem_handle = GH_MEM_HANDLE_INVAL; /* to be filled later by mem_share/mem_lend */
	parcel->mem_type = GH_RM_MEM_TYPE_NORMAL;

	ret = account_locked_vm(ghvm->mm, mapping->npages, true);
	if (ret)
		goto free_mapping;

	mapping->pages = kcalloc(mapping->npages, sizeof(*mapping->pages), GFP_KERNEL_ACCOUNT);
	if (!mapping->pages) {
		ret = -ENOMEM;
		mapping->npages = 0; /* update npages for reclaim */
		goto unlock_pages;
	}

	gup_flags = FOLL_LONGTERM;
	if (region->flags & GH_MEM_ALLOW_WRITE)
		gup_flags |= FOLL_WRITE;

	pinned = pin_user_pages_fast(region->userspace_addr, mapping->npages,
					gup_flags, mapping->pages);
	if (pinned < 0) {
		ret = pinned;
		goto free_pages;
	} else if (pinned != mapping->npages) {
		ret = -EFAULT;
		mapping->npages = pinned; /* update npages for reclaim */
		goto unpin_pages;
	}

	if (lend) {
		parcel->n_acl_entries = 1;
		mapping->share_type = VM_MEM_LEND;
	} else {
		parcel->n_acl_entries = 2;
		mapping->share_type = VM_MEM_SHARE;
	}
	parcel->acl_entries = kcalloc(parcel->n_acl_entries,
				      sizeof(*parcel->acl_entries), GFP_KERNEL);
	if (!parcel->acl_entries) {
		ret = -ENOMEM;
		goto unpin_pages;
	}

	/* acl_entries[0].vmid will be this VM's vmid. We'll fill it when the
	 * VM is starting and we know the VM's vmid.
	 */
	if (region->flags & GH_MEM_ALLOW_READ)
		parcel->acl_entries[0].perms |= GH_RM_ACL_R;
	if (region->flags & GH_MEM_ALLOW_WRITE)
		parcel->acl_entries[0].perms |= GH_RM_ACL_W;
	if (region->flags & GH_MEM_ALLOW_EXEC)
		parcel->acl_entries[0].perms |= GH_RM_ACL_X;

	if (!lend) {
		ret = gh_rm_get_vmid(ghvm->rm, &vmid);
		if (ret)
			goto free_acl;

		parcel->acl_entries[1].vmid = cpu_to_le16(vmid);
		/* Host assumed to have all these permissions. Gunyah will not
		* grant new permissions if host actually had less than RWX
		*/
		parcel->acl_entries[1].perms = GH_RM_ACL_R | GH_RM_ACL_W | GH_RM_ACL_X;
	}

	parcel->n_mem_entries = 1;
	for (i = 1; i < mapping->npages; i++) {
		if (!pages_are_mergeable(mapping->pages[i - 1], mapping->pages[i]))
			parcel->n_mem_entries++;
	}

	parcel->mem_entries = kcalloc(parcel->n_mem_entries,
					sizeof(parcel->mem_entries[0]),
					GFP_KERNEL_ACCOUNT);
	if (!parcel->mem_entries) {
		ret = -ENOMEM;
		goto free_acl;
	}

	/* reduce number of entries by combining contiguous pages into single memory entry */
	prev_page = mapping->pages[0];
	parcel->mem_entries[0].phys_addr = cpu_to_le64(page_to_phys(prev_page));
	entry_size = PAGE_SIZE;
	for (i = 1, j = 0; i < mapping->npages; i++) {
		curr_page = mapping->pages[i];
		if (pages_are_mergeable(prev_page, curr_page)) {
			entry_size += PAGE_SIZE;
		} else {
			parcel->mem_entries[j].size = cpu_to_le64(entry_size);
			j++;
			parcel->mem_entries[j].phys_addr =
				cpu_to_le64(page_to_phys(curr_page));
			entry_size = PAGE_SIZE;
		}

		prev_page = curr_page;
	}
	parcel->mem_entries[j].size = cpu_to_le64(entry_size);

	list_add(&mapping->list, &ghvm->memory_mappings);
	mutex_unlock(&ghvm->mm_lock);
	return 0;
free_acl:
	kfree(parcel->acl_entries);
unpin_pages:
	unpin_user_pages(mapping->pages, pinned);
free_pages:
	kfree(mapping->pages);
unlock_pages:
	account_locked_vm(ghvm->mm, mapping->npages, false);
free_mapping:
	kfree(mapping);
unlock:
	mutex_unlock(&ghvm->mm_lock);
	return ret;
}
