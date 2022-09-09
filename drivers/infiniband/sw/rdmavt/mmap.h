/* SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause */
/*
 * Copyright(c) 2016 Intel Corporation.
 */

#ifndef DEF_RDMAVTMMAP_H
#define DEF_RDMAVTMMAP_H

#include <rdma/rdma_vt.h>

void rvt_mmap_init(struct rvt_dev_info *rdi);
void rvt_release_mmap_info(struct kref *ref);
int rvt_mmap(struct ib_ucontext *context, struct vm_area_struct *vma);
struct rvt_mmap_info *rvt_create_mmap_info(struct rvt_dev_info *rdi, u32 size,
					   struct ib_udata *udata, void *obj);
void rvt_update_mmap_info(struct rvt_dev_info *rdi, struct rvt_mmap_info *ip,
			  u32 size, void *obj);

#endif          /* DEF_RDMAVTMMAP_H */
