/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#ifndef PVR_GEM_H
#define PVR_GEM_H

#include "pvr_rogue_heap_config.h"
#include "pvr_rogue_meta.h"

#include <uapi/drm/pvr_drm.h>

#include <drm/drm_gem.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_mm.h>

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/const.h>
#include <linux/compiler_attributes.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/refcount.h>
#include <linux/scatterlist.h>
#include <linux/sizes.h>
#include <linux/types.h>

/* Forward declaration from "pvr_device.h". */
struct pvr_device;
struct pvr_file;

/**
 * DOC: Flags for DRM_IOCTL_PVR_CREATE_BO (kernel-only)
 *
 * Kernel-only values allowed in &pvr_gem_object->flags. The majority of options
 * for this field are specified in the UAPI header "pvr_drm.h" with a
 * DRM_PVR_BO_ prefix. To distinguish these internal options (which must exist
 * in ranges marked as "reserved" in the UAPI header), we drop the DRM prefix.
 * The public options should be used directly, DRM prefix and all.
 *
 * To avoid potentially confusing gaps in the UAPI options, these kernel-only
 * options are specified "in reverse", starting at bit 63.
 *
 * We use "reserved" to refer to bits defined here and not exposed in the UAPI.
 * Bits not defined anywhere are "undefined".
 *
 * CPU mapping options
 *    :PVR_BO_CPU_CACHED: By default, all GEM objects are mapped write-combined on the CPU. Set this
 *       flag to override this behaviour and map the object cached.
 *
 * Firmware options
 *    :PVR_BO_FW_NO_CLEAR_ON_RESET: By default, all FW objects are cleared and reinitialised on hard
 *       reset. Set this flag to override this behaviour and preserve buffer contents on reset.
 */
#define PVR_BO_CPU_CACHED BIT_ULL(63)

#define PVR_BO_FW_NO_CLEAR_ON_RESET BIT_ULL(62)

#define PVR_BO_KERNEL_FLAGS_MASK (PVR_BO_CPU_CACHED | PVR_BO_FW_NO_CLEAR_ON_RESET)

/* Bits 61..3 are undefined. */
/* Bits 2..0 are defined in the UAPI. */

/* Other utilities. */
#define PVR_BO_UNDEFINED_MASK ~(PVR_BO_KERNEL_FLAGS_MASK | DRM_PVR_BO_FLAGS_MASK)

/*
 * All firmware-mapped memory uses (mostly) the same flags. Specifically,
 * firmware-mapped memory should be:
 *  * Read/write on the device,
 *  * Read/write on the CPU, and
 *  * Write-combined on the CPU.
 *
 * The only variation is in caching on the device.
 */
#define PVR_BO_FW_FLAGS_DEVICE_CACHED (ULL(0))
#define PVR_BO_FW_FLAGS_DEVICE_UNCACHED DRM_PVR_BO_BYPASS_DEVICE_CACHE

/**
 * struct pvr_gem_object - powervr-specific wrapper for &struct drm_gem_object
 */
struct pvr_gem_object {
	/**
	 * @base: The underlying &struct drm_gem_shmem_object.
	 *
	 * Do not access this member directly, instead call
	 * shem_gem_from_pvr_gem().
	 */
	struct drm_gem_shmem_object base;

	/**
	 * @flags: Options set at creation-time. Some of these options apply to
	 * the creation operation itself (which are stored here for reference)
	 * with the remainder used for mapping options to both the device and
	 * CPU. These are used every time this object is mapped, but may be
	 * changed after creation.
	 *
	 * Must be a combination of DRM_PVR_BO_* and/or PVR_BO_* flags.
	 *
	 * .. note::
	 *
	 *    This member is declared const to indicate that none of these
	 *    options may change or be changed throughout the object's
	 *    lifetime.
	 */
	u64 flags;

};

static_assert(offsetof(struct pvr_gem_object, base) == 0,
	      "offsetof(struct pvr_gem_object, base) not zero");

#define shmem_gem_from_pvr_gem(pvr_obj) (&(pvr_obj)->base)

#define shmem_gem_to_pvr_gem(shmem_obj) container_of_const(shmem_obj, struct pvr_gem_object, base)

#define gem_from_pvr_gem(pvr_obj) (&(pvr_obj)->base.base)

#define gem_to_pvr_gem(gem_obj) container_of_const(gem_obj, struct pvr_gem_object, base.base)

/* Functions defined in pvr_gem.c */

struct drm_gem_object *pvr_gem_create_object(struct drm_device *drm_dev, size_t size);

struct pvr_gem_object *pvr_gem_object_create(struct pvr_device *pvr_dev,
					     size_t size, u64 flags);

int pvr_gem_object_into_handle(struct pvr_gem_object *pvr_obj,
			       struct pvr_file *pvr_file, u32 *handle);
struct pvr_gem_object *pvr_gem_object_from_handle(struct pvr_file *pvr_file,
						  u32 handle);

static __always_inline struct sg_table *
pvr_gem_object_get_pages_sgt(struct pvr_gem_object *pvr_obj)
{
	return drm_gem_shmem_get_pages_sgt(shmem_gem_from_pvr_gem(pvr_obj));
}

void *pvr_gem_object_vmap(struct pvr_gem_object *pvr_obj);
void pvr_gem_object_vunmap(struct pvr_gem_object *pvr_obj);

int pvr_gem_get_dma_addr(struct pvr_gem_object *pvr_obj, u32 offset,
			 dma_addr_t *dma_addr_out);

/**
 * pvr_gem_object_get() - Acquire reference on pvr_gem_object
 * @pvr_obj: Pointer to object to acquire reference on.
 */
static __always_inline void
pvr_gem_object_get(struct pvr_gem_object *pvr_obj)
{
	drm_gem_object_get(gem_from_pvr_gem(pvr_obj));
}

/**
 * pvr_gem_object_put() - Release reference on pvr_gem_object
 * @pvr_obj: Pointer to object to release reference on.
 */
static __always_inline void
pvr_gem_object_put(struct pvr_gem_object *pvr_obj)
{
	drm_gem_object_put(gem_from_pvr_gem(pvr_obj));
}

static __always_inline size_t
pvr_gem_object_size(struct pvr_gem_object *pvr_obj)
{
	return gem_from_pvr_gem(pvr_obj)->size;
}

#endif /* PVR_GEM_H */
