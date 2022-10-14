/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _GH_PRIV_VM_MGR_H
#define _GH_PRIV_VM_MGR_H

#include <linux/gunyah_rsc_mgr.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>

#include <uapi/linux/gunyah.h>

long gh_dev_vm_mgr_ioctl(struct gh_rm *rm, unsigned int cmd, unsigned long arg);

enum gh_vm_mem_share_type {
	VM_MEM_SHARE,
	VM_MEM_LEND,
};

struct gh_vm_mem {
	struct list_head list;
	enum gh_vm_mem_share_type share_type;
	struct gh_rm_mem_parcel parcel;

	__u64 guest_phys_addr;
	struct page **pages;
	unsigned long npages;
};

struct gh_vm {
	u16 vmid;
	struct gh_rm *rm;
	struct device *parent;

	struct work_struct free_work;
	struct mutex mm_lock;
	struct list_head memory_mappings;
};

int gh_vm_mem_alloc(struct gh_vm *ghvm, struct gh_userspace_memory_region *region);
void gh_vm_mem_reclaim(struct gh_vm *ghvm, struct gh_vm_mem *mapping);
int gh_vm_mem_free(struct gh_vm *ghvm, u32 label);
struct gh_vm_mem *gh_vm_mem_find_by_label(struct gh_vm *ghvm, u32 label);

#endif
