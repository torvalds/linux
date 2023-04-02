// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "gh_vm_mgr: " fmt

#include <linux/gunyah_rsc_mgr.h>
#include <linux/mm.h>

#include <uapi/linux/gunyah.h>

#include "vm_mgr.h"

static struct gh_vm_mem *__gh_vm_mem_find_by_label(struct gh_vm *ghvm, u32 label)
	__must_hold(&ghvm->mm_lock)
{
	struct gh_vm_mem *mapping;

	list_for_each_entry(mapping, &ghvm->memory_mappings, list)
		if (mapping->parcel.label == label)
			return mapping;

	return NULL;
}

void gh_vm_mem_reclaim(struct gh_vm *ghvm, struct gh_vm_mem *mapping)
	__must_hold(&ghvm->mm_lock)
{
	int i, ret = 0;

	if (mapping->parcel.mem_handle != GH_MEM_HANDLE_INVAL) {
		ret = gh_rm_mem_reclaim(ghvm->rm, &mapping->parcel);
		if (ret)
			pr_warn("Failed to reclaim memory parcel for label %d: %d\n",
				mapping->parcel.label, ret);
	}

	if (!ret)
		for (i = 0; i < mapping->npages; i++)
			unpin_user_page(mapping->pages[i]);

	kfree(mapping->pages);
	kfree(mapping->parcel.acl_entries);
	kfree(mapping->parcel.mem_entries);

	list_del(&mapping->list);
}

struct gh_vm_mem *gh_vm_mem_find_by_addr(struct gh_vm *ghvm, u64 guest_phys_addr, u32 size)
{
	struct gh_vm_mem *mapping = NULL;
	int ret;

	ret = mutex_lock_interruptible(&ghvm->mm_lock);
	if (ret)
		return ERR_PTR(ret);

	list_for_each_entry(mapping, &ghvm->memory_mappings, list) {
		if (guest_phys_addr >= mapping->guest_phys_addr &&
			(guest_phys_addr + size <= mapping->guest_phys_addr +
			(mapping->npages << PAGE_SHIFT))) {
			goto unlock;
		}
	}

	mapping = NULL;
unlock:
	mutex_unlock(&ghvm->mm_lock);
	return mapping;
}

struct gh_vm_mem *gh_vm_mem_find_by_label(struct gh_vm *ghvm, u32 label)
{
	struct gh_vm_mem *mapping;
	int ret;

	ret = mutex_lock_interruptible(&ghvm->mm_lock);
	if (ret)
		return ERR_PTR(ret);

	mapping = __gh_vm_mem_find_by_label(ghvm, label);
	mutex_unlock(&ghvm->mm_lock);

	return mapping ? : ERR_PTR(-ENODEV);
}

int gh_vm_mem_alloc(struct gh_vm *ghvm, struct gh_userspace_memory_region *region, bool lend)
{
	struct gh_vm_mem *mapping, *tmp_mapping;
	struct gh_rm_mem_entry *mem_entries;
	phys_addr_t curr_page, prev_page;
	struct gh_rm_mem_parcel *parcel;
	int i, j, pinned, ret = 0;
	size_t entry_size;
	u16 vmid;

	if (!region->memory_size || !PAGE_ALIGNED(region->memory_size) ||
		!PAGE_ALIGNED(region->userspace_addr) || !PAGE_ALIGNED(region->guest_phys_addr))
		return -EINVAL;

	if (region->guest_phys_addr + region->memory_size < region->guest_phys_addr)
		return -EOVERFLOW;

	ret = mutex_lock_interruptible(&ghvm->mm_lock);
	if (ret)
		return ret;

	mapping = __gh_vm_mem_find_by_label(ghvm, region->label);
	if (mapping) {
		mutex_unlock(&ghvm->mm_lock);
		return -EEXIST;
	}

	mapping = kzalloc(sizeof(*mapping), GFP_KERNEL);
	if (!mapping) {
		mutex_unlock(&ghvm->mm_lock);
		return -ENOMEM;
	}

	mapping->parcel.label = region->label;
	mapping->guest_phys_addr = region->guest_phys_addr;
	mapping->npages = region->memory_size >> PAGE_SHIFT;
	parcel = &mapping->parcel;
	parcel->mem_handle = GH_MEM_HANDLE_INVAL; /* to be filled later by mem_share/mem_lend */
	parcel->mem_type = GH_RM_MEM_TYPE_NORMAL;

	/* Check for overlap */
	list_for_each_entry(tmp_mapping, &ghvm->memory_mappings, list) {
		if (!((mapping->guest_phys_addr + (mapping->npages << PAGE_SHIFT) <=
			tmp_mapping->guest_phys_addr) ||
			(mapping->guest_phys_addr >=
			tmp_mapping->guest_phys_addr + (tmp_mapping->npages << PAGE_SHIFT)))) {
			ret = -EEXIST;
			goto free_mapping;
		}
	}

	list_add(&mapping->list, &ghvm->memory_mappings);

	mapping->pages = kcalloc(mapping->npages, sizeof(*mapping->pages), GFP_KERNEL);
	if (!mapping->pages) {
		ret = -ENOMEM;
		mapping->npages = 0; /* update npages for reclaim */
		goto reclaim;
	}

	pinned = pin_user_pages_fast(region->userspace_addr, mapping->npages,
					FOLL_WRITE | FOLL_LONGTERM, mapping->pages);
	if (pinned < 0) {
		ret = pinned;
		mapping->npages = 0; /* update npages for reclaim */
		goto reclaim;
	} else if (pinned != mapping->npages) {
		ret = -EFAULT;
		mapping->npages = pinned; /* update npages for reclaim */
		goto reclaim;
	}

	if (lend) {
		parcel->n_acl_entries = 1;
		mapping->share_type = VM_MEM_LEND;
	} else {
		parcel->n_acl_entries = 2;
		mapping->share_type = VM_MEM_SHARE;
	}
	parcel->acl_entries = kcalloc(parcel->n_acl_entries, sizeof(*parcel->acl_entries),
					GFP_KERNEL);
	if (!parcel->acl_entries) {
		ret = -ENOMEM;
		goto reclaim;
	}

	parcel->acl_entries[0].vmid = cpu_to_le16(ghvm->vmid);

	if (region->flags & GH_MEM_ALLOW_READ)
		parcel->acl_entries[0].perms |= GH_RM_ACL_R;
	if (region->flags & GH_MEM_ALLOW_WRITE)
		parcel->acl_entries[0].perms |= GH_RM_ACL_W;
	if (region->flags & GH_MEM_ALLOW_EXEC)
		parcel->acl_entries[0].perms |= GH_RM_ACL_X;

	if (mapping->share_type == VM_MEM_SHARE) {
		ret = gh_rm_get_vmid(ghvm->rm, &vmid);
		if (ret)
			goto reclaim;

		parcel->acl_entries[1].vmid = cpu_to_le16(vmid);
		/* Host assumed to have all these permissions. Gunyah will not
		 * grant new permissions if host actually had less than RWX
		 */
		parcel->acl_entries[1].perms |= GH_RM_ACL_R | GH_RM_ACL_W | GH_RM_ACL_X;
	}

	mem_entries = kcalloc(mapping->npages, sizeof(*mem_entries), GFP_KERNEL);
	if (!mem_entries) {
		ret = -ENOMEM;
		goto reclaim;
	}

	/* reduce number of entries by combining contiguous pages into single memory entry */
	prev_page = page_to_phys(mapping->pages[0]);
	mem_entries[0].ipa_base = cpu_to_le64(prev_page);
	entry_size = PAGE_SIZE;
	for (i = 1, j = 0; i < mapping->npages; i++) {
		curr_page = page_to_phys(mapping->pages[i]);
		if (curr_page - prev_page == PAGE_SIZE) {
			entry_size += PAGE_SIZE;
		} else {
			mem_entries[j].size = cpu_to_le64(entry_size);
			j++;
			mem_entries[j].ipa_base = cpu_to_le64(curr_page);
			entry_size = PAGE_SIZE;
		}

		prev_page = curr_page;
	}
	mem_entries[j].size = cpu_to_le64(entry_size);

	parcel->n_mem_entries = j + 1;
	parcel->mem_entries = kmemdup(mem_entries, sizeof(*mem_entries) * parcel->n_mem_entries,
					GFP_KERNEL);
	kfree(mem_entries);
	if (!parcel->mem_entries) {
		ret = -ENOMEM;
		goto reclaim;
	}

	mutex_unlock(&ghvm->mm_lock);
	return 0;
reclaim:
	gh_vm_mem_reclaim(ghvm, mapping);
free_mapping:
	kfree(mapping);
	mutex_unlock(&ghvm->mm_lock);
	return ret;
}

int gh_vm_mem_free(struct gh_vm *ghvm, u32 label)
{
	struct gh_vm_mem *mapping;
	int ret;

	ret = mutex_lock_interruptible(&ghvm->mm_lock);
	if (ret)
		return ret;

	mapping = __gh_vm_mem_find_by_label(ghvm, label);
	if (!mapping)
		goto out;

	gh_vm_mem_reclaim(ghvm, mapping);
	kfree(mapping);
out:
	mutex_unlock(&ghvm->mm_lock);
	return ret;
}
