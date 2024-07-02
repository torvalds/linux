/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#ifndef PVR_DRV_H
#define PVR_DRV_H

#include "linux/compiler_attributes.h"
#include <uapi/drm/pvr_drm.h>

#define PVR_DRIVER_NAME "powervr"
#define PVR_DRIVER_DESC "Imagination PowerVR (Series 6 and later) & IMG Graphics"
#define PVR_DRIVER_DATE "20230904"

/*
 * Driver interface version:
 *  - 1.0: Initial interface
 */
#define PVR_DRIVER_MAJOR 1
#define PVR_DRIVER_MINOR 0
#define PVR_DRIVER_PATCHLEVEL 0

int pvr_get_uobj(u64 usr_ptr, u32 usr_size, u32 min_size, u32 obj_size, void *out);
int pvr_set_uobj(u64 usr_ptr, u32 usr_size, u32 min_size, u32 obj_size, const void *in);
int pvr_get_uobj_array(const struct drm_pvr_obj_array *in, u32 min_stride, u32 obj_size,
		       void **out);
int pvr_set_uobj_array(const struct drm_pvr_obj_array *out, u32 min_stride, u32 obj_size,
		       const void *in);

#define PVR_UOBJ_MIN_SIZE_INTERNAL(_typename, _last_mandatory_field) \
	(offsetof(_typename, _last_mandatory_field) + \
	 sizeof(((_typename *)NULL)->_last_mandatory_field))

/* NOLINTBEGIN(bugprone-macro-parentheses) */
#define PVR_UOBJ_DECL(_typename, _last_mandatory_field) \
	, _typename : PVR_UOBJ_MIN_SIZE_INTERNAL(_typename, _last_mandatory_field)
/* NOLINTEND(bugprone-macro-parentheses) */

/**
 * DOC: PVR user objects.
 *
 * Macros used to aid copying structured and array data to and from
 * userspace. Objects can differ in size, provided the minimum size
 * allowed is specified (using the last mandatory field in the struct).
 * All types used with PVR_UOBJ_GET/SET macros must be listed here under
 * PVR_UOBJ_MIN_SIZE, with the last mandatory struct field specified.
 */

/**
 * PVR_UOBJ_MIN_SIZE() - Fetch the minimum copy size of a compatible type object.
 * @_obj_name: The name of the object. Cannot be a typename - this is deduced.
 *
 * This cannot fail. Using the macro with an incompatible type will result in a
 * compiler error.
 *
 * To add compatibility for a type, list it within the macro in an orderly
 * fashion. The second argument is the name of the last mandatory field of the
 * struct type, which is used to calculate the size. See also PVR_UOBJ_DECL().
 *
 * Return: The minimum copy size.
 */
#define PVR_UOBJ_MIN_SIZE(_obj_name) _Generic(_obj_name \
	PVR_UOBJ_DECL(struct drm_pvr_job, hwrt) \
	PVR_UOBJ_DECL(struct drm_pvr_sync_op, value) \
	PVR_UOBJ_DECL(struct drm_pvr_dev_query_gpu_info, num_phantoms) \
	PVR_UOBJ_DECL(struct drm_pvr_dev_query_runtime_info, cdm_max_local_mem_size_regs) \
	PVR_UOBJ_DECL(struct drm_pvr_dev_query_quirks, _padding_c) \
	PVR_UOBJ_DECL(struct drm_pvr_dev_query_enhancements, _padding_c) \
	PVR_UOBJ_DECL(struct drm_pvr_heap, page_size_log2) \
	PVR_UOBJ_DECL(struct drm_pvr_dev_query_heap_info, heaps) \
	PVR_UOBJ_DECL(struct drm_pvr_static_data_area, offset) \
	PVR_UOBJ_DECL(struct drm_pvr_dev_query_static_data_areas, static_data_areas) \
	)

/**
 * PVR_UOBJ_GET() - Copies from _src_usr_ptr to &_dest_obj.
 * @_dest_obj: The destination container object in kernel space.
 * @_usr_size: The size of the source container in user space.
 * @_src_usr_ptr: __u64 raw pointer to the source container in user space.
 *
 * Return: Error code. See pvr_get_uobj().
 */
#define PVR_UOBJ_GET(_dest_obj, _usr_size, _src_usr_ptr) \
	pvr_get_uobj(_src_usr_ptr, _usr_size, \
		     PVR_UOBJ_MIN_SIZE(_dest_obj), \
		     sizeof(_dest_obj), &(_dest_obj))

/**
 * PVR_UOBJ_SET() - Copies from &_src_obj to _dest_usr_ptr.
 * @_dest_usr_ptr: __u64 raw pointer to the destination container in user space.
 * @_usr_size: The size of the destination container in user space.
 * @_src_obj: The source container object in kernel space.
 *
 * Return: Error code. See pvr_set_uobj().
 */
#define PVR_UOBJ_SET(_dest_usr_ptr, _usr_size, _src_obj) \
	pvr_set_uobj(_dest_usr_ptr, _usr_size, \
		     PVR_UOBJ_MIN_SIZE(_src_obj), \
		     sizeof(_src_obj), &(_src_obj))

/**
 * PVR_UOBJ_GET_ARRAY() - Copies from @_src_drm_pvr_obj_array.array to
 * alloced memory and returns a pointer in _dest_array.
 * @_dest_array: The destination C array object in kernel space.
 * @_src_drm_pvr_obj_array: The &struct drm_pvr_obj_array containing a __u64 raw
 * pointer to the source C array in user space and the size of each array
 * element in user space (the 'stride').
 *
 * Return: Error code. See pvr_get_uobj_array().
 */
#define PVR_UOBJ_GET_ARRAY(_dest_array, _src_drm_pvr_obj_array) \
	pvr_get_uobj_array(_src_drm_pvr_obj_array, \
			   PVR_UOBJ_MIN_SIZE((_dest_array)[0]), \
			   sizeof((_dest_array)[0]), (void **)&(_dest_array))

/**
 * PVR_UOBJ_SET_ARRAY() - Copies from _src_array to @_dest_drm_pvr_obj_array.array.
 * @_dest_drm_pvr_obj_array: The &struct drm_pvr_obj_array containing a __u64 raw
 * pointer to the destination C array in user space and the size of each array
 * element in user space (the 'stride').
 * @_src_array: The source C array object in kernel space.
 *
 * Return: Error code. See pvr_set_uobj_array().
 */
#define PVR_UOBJ_SET_ARRAY(_dest_drm_pvr_obj_array, _src_array) \
	pvr_set_uobj_array(_dest_drm_pvr_obj_array, \
			   PVR_UOBJ_MIN_SIZE((_src_array)[0]), \
			   sizeof((_src_array)[0]), _src_array)

#endif /* PVR_DRV_H */
