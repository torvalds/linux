/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _GH_SECURE_VM_LOADER_H
#define _GH_SECURE_VM_LOADER_H

#include "gh_private.h"
/*
 * secure vm loader APIs
 */
#if IS_ENABLED(CONFIG_GH_SECURE_VM_LOADER)
int gh_secure_vm_loader_init(void);
void gh_secure_vm_loader_exit(void);
long gh_vm_ioctl_set_fw_name(struct gh_vm *vm, unsigned long arg);
long gh_vm_ioctl_get_fw_name(struct gh_vm *vm, unsigned long arg);
int gh_secure_vm_loader_reclaim_fw(struct gh_vm *vm);
#else
static int gh_secure_vm_loader_init(void)
{
	return -EINVAL;
}
static void gh_secure_vm_loader_exit(void)
{
}
static inline long gh_vm_ioctl_set_fw_name(struct gh_vm *vm,
						unsigned long arg)
{
	return -EINVAL;
}
static inline long gh_vm_ioctl_get_fw_name(struct gh_vm *vm,
						unsigned long arg)
{
	return -EINVAL;
}
static inline int gh_secure_vm_loader_reclaim_fw(struct gh_vm *vm)
{
	return -EINVAL;
}
#endif

#endif /* _GH_SECURE_VM_LOADER_H */
