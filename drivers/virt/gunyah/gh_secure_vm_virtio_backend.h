/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _GH_SECURE_VM_VIRTIO_BACKEND_H
#define _GH_SECURE_VM_VIRTIO_BACKEND_H

#include <linux/gunyah/gh_common.h>

int gh_virtio_backend_mmap(const char *vm_name, struct vm_area_struct *vma);
long gh_virtio_backend_ioctl(const char *vm_name, unsigned int cmd,
							unsigned long arg);
int gh_parse_virtio_properties(struct device *dev, const char *vm_name);
int gh_virtio_backend_remove(const char *vm_name);
int gh_virtio_mmio_exit(gh_vmid_t vmid, const char *vm_name);
int gh_virtio_backend_init(void);
void gh_virtio_backend_exit(void);

#endif /* _GH_SECURE_VM_VIRTIO_BACKEND_H */
