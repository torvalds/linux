/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#ifndef PVR_VM_H
#define PVR_VM_H

#include "pvr_rogue_mmu_defs.h"

#include <uapi/drm/pvr_drm.h>

#include <linux/types.h>

/* Forward declaration from "pvr_device.h" */
struct pvr_device;
struct pvr_file;

/* Forward declaration from "pvr_gem.h" */
struct pvr_gem_object;

/* Forward declaration from "pvr_vm.c" */
struct pvr_vm_context;

/* Forward declaration from <uapi/drm/pvr_drm.h> */
struct drm_pvr_ioctl_get_heap_info_args;

/* Forward declaration from <drm/drm_exec.h> */
struct drm_exec;

/* Functions defined in pvr_vm.c */

bool pvr_device_addr_is_valid(u64 device_addr);
bool pvr_device_addr_and_size_are_valid(struct pvr_vm_context *vm_ctx,
					u64 device_addr, u64 size);

struct pvr_vm_context *pvr_vm_create_context(struct pvr_device *pvr_dev,
					     bool is_userspace_context);

int pvr_vm_map(struct pvr_vm_context *vm_ctx,
	       struct pvr_gem_object *pvr_obj, u64 pvr_obj_offset,
	       u64 device_addr, u64 size);
int pvr_vm_unmap(struct pvr_vm_context *vm_ctx, u64 device_addr, u64 size);

dma_addr_t pvr_vm_get_page_table_root_addr(struct pvr_vm_context *vm_ctx);
struct dma_resv *pvr_vm_get_dma_resv(struct pvr_vm_context *vm_ctx);

int pvr_static_data_areas_get(const struct pvr_device *pvr_dev,
			      struct drm_pvr_ioctl_dev_query_args *args);
int pvr_heap_info_get(const struct pvr_device *pvr_dev,
		      struct drm_pvr_ioctl_dev_query_args *args);
const struct drm_pvr_heap *pvr_find_heap_containing(struct pvr_device *pvr_dev,
						    u64 addr, u64 size);

struct pvr_gem_object *pvr_vm_find_gem_object(struct pvr_vm_context *vm_ctx,
					      u64 device_addr,
					      u64 *mapped_offset_out,
					      u64 *mapped_size_out);

struct pvr_fw_object *
pvr_vm_get_fw_mem_context(struct pvr_vm_context *vm_ctx);

struct pvr_vm_context *pvr_vm_context_lookup(struct pvr_file *pvr_file, u32 handle);
struct pvr_vm_context *pvr_vm_context_get(struct pvr_vm_context *vm_ctx);
bool pvr_vm_context_put(struct pvr_vm_context *vm_ctx);
void pvr_destroy_vm_contexts_for_file(struct pvr_file *pvr_file);

#endif /* PVR_VM_H */
