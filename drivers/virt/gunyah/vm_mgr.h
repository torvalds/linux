/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _GH_PRIV_VM_MGR_H
#define _GH_PRIV_VM_MGR_H

#include <linux/gunyah_rsc_mgr.h>
#include <linux/gunyah_vm_mgr.h>
#include <linux/list.h>
#include <linux/kref.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/wait.h>

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
	enum gh_rm_vm_auth_mechanism auth;
	struct gh_vm_dtb_config dtb_config;
	struct gh_vm_firmware_config fw_config;

	struct notifier_block nb;
	enum gh_rm_vm_status vm_status;
	wait_queue_head_t vm_status_wait;
	struct rw_semaphore status_lock;
	struct gh_vm_exit_info exit_info;

	struct work_struct free_work;
	struct kref kref;
	struct mutex mm_lock;
	struct list_head memory_mappings;
	struct mutex fn_lock;
	struct list_head functions;
	struct mutex resources_lock;
	struct list_head resources;
	struct list_head resource_tickets;
	struct rb_root mmio_handler_root;
	struct rw_semaphore mmio_handler_lock;
};

int gh_vm_mem_alloc(struct gh_vm *ghvm, struct gh_userspace_memory_region *region, bool lend);
void gh_vm_mem_reclaim(struct gh_vm *ghvm, struct gh_vm_mem *mapping);
int gh_vm_mem_free(struct gh_vm *ghvm, u32 label);
struct gh_vm_mem *gh_vm_mem_find_by_label(struct gh_vm *ghvm, u32 label);
struct gh_vm_mem *gh_vm_mem_find_by_addr(struct gh_vm *ghvm, u64 guest_phys_addr, u32 size);

int gh_vm_mmio_write(struct gh_vm *ghvm, u64 addr, u32 len, u64 data);

#endif
