/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef MEM_BUF_IDS_H
#define MEM_BUF_IDS_H

#include <linux/cdev.h>
#include <linux/gunyah/gh_common.h>

#define MEM_BUF_API_HYP_ASSIGN BIT(0)
#define MEM_BUF_API_GUNYAH BIT(1)

/*
 * @vmid - id assigned by hypervisor to uniquely identify a VM
 * @allowed_api - Some vms may use a different hypervisor interface.
 */
struct mem_buf_vm {
	const char *name;
	u16 vmid;
	u32 allowed_api;
	struct cdev cdev;
	struct device dev;
};

extern int current_vmid;
int mem_buf_vm_init(struct device *dev);
void mem_buf_vm_exit(void);

bool mem_buf_vm_uses_hyp_assign(void);
/*
 * Returns a negative number for invalid arguments, otherwise a positive value
 * if gunyah APIs are required.
 */
int mem_buf_vm_uses_gunyah(int *vmids, unsigned int nr_acl_entries);

/* @Return: A negative number on failure, or vmid on success */
int mem_buf_fd_to_vmid(int fd);

#endif
