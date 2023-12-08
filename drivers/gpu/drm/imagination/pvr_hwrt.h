/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#ifndef PVR_HWRT_H
#define PVR_HWRT_H

#include <linux/compiler_attributes.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/xarray.h>
#include <uapi/drm/pvr_drm.h>

#include "pvr_device.h"
#include "pvr_rogue_fwif_shared.h"

/* Forward declaration from pvr_free_list.h. */
struct pvr_free_list;

/* Forward declaration from pvr_gem.h. */
struct pvr_fw_object;

/**
 * struct pvr_hwrt_data - structure representing HWRT data
 */
struct pvr_hwrt_data {
	/** @fw_obj: FW object representing the FW-side structure. */
	struct pvr_fw_object *fw_obj;

	/** @data: Local copy of FW-side structure. */
	struct rogue_fwif_hwrtdata data;

	/** @freelist_node: List node connecting this HWRT to the local freelist. */
	struct list_head freelist_node;

	/**
	 * @srtc_obj: FW object representing shadow render target cache.
	 *
	 * Only valid if @max_rts > 1.
	 */
	struct pvr_fw_object *srtc_obj;

	/**
	 * @raa_obj: FW object representing renders accumulation array.
	 *
	 * Only valid if @max_rts > 1.
	 */
	struct pvr_fw_object *raa_obj;

	/** @hwrt_dataset: Back pointer to owning HWRT dataset. */
	struct pvr_hwrt_dataset *hwrt_dataset;
};

/**
 * struct pvr_hwrt_dataset - structure representing a HWRT data set.
 */
struct pvr_hwrt_dataset {
	/** @ref_count: Reference count of object. */
	struct kref ref_count;

	/** @pvr_dev: Pointer to device that owns this object. */
	struct pvr_device *pvr_dev;

	/** @common_fw_obj: FW object representing common FW-side structure. */
	struct pvr_fw_object *common_fw_obj;

	/** @common: Common HWRT data. */
	struct rogue_fwif_hwrtdata_common common;

	/** @data: HWRT data structures belonging to this set. */
	struct pvr_hwrt_data data[ROGUE_FWIF_NUM_RTDATAS];

	/** @free_lists: Free lists used by HWRT data set. */
	struct pvr_free_list *free_lists[ROGUE_FWIF_NUM_RTDATA_FREELISTS];

	/** @max_rts: Maximum render targets for this HWRT data set. */
	u16 max_rts;
};

struct pvr_hwrt_dataset *
pvr_hwrt_dataset_create(struct pvr_file *pvr_file,
			struct drm_pvr_ioctl_create_hwrt_dataset_args *args);

void
pvr_destroy_hwrt_datasets_for_file(struct pvr_file *pvr_file);

/**
 * pvr_hwrt_dataset_lookup() - Lookup HWRT dataset pointer from handle
 * @pvr_file: Pointer to pvr_file structure.
 * @handle: Object handle.
 *
 * Takes reference on dataset object. Call pvr_hwrt_dataset_put() to release.
 *
 * Returns:
 *  * The requested object on success, or
 *  * %NULL on failure (object does not exist in list, or is not a HWRT
 *    dataset)
 */
static __always_inline struct pvr_hwrt_dataset *
pvr_hwrt_dataset_lookup(struct pvr_file *pvr_file, u32 handle)
{
	struct pvr_hwrt_dataset *hwrt;

	xa_lock(&pvr_file->hwrt_handles);
	hwrt = xa_load(&pvr_file->hwrt_handles, handle);

	if (hwrt)
		kref_get(&hwrt->ref_count);

	xa_unlock(&pvr_file->hwrt_handles);

	return hwrt;
}

void
pvr_hwrt_dataset_put(struct pvr_hwrt_dataset *hwrt);

/**
 * pvr_hwrt_data_lookup() - Lookup HWRT data pointer from handle and index
 * @pvr_file: Pointer to pvr_file structure.
 * @handle: Object handle.
 * @index: Index of RT data within dataset.
 *
 * Takes reference on dataset object. Call pvr_hwrt_data_put() to release.
 *
 * Returns:
 *  * The requested object on success, or
 *  * %NULL on failure (object does not exist in list, or is not a HWRT
 *    dataset, or index is out of range)
 */
static __always_inline struct pvr_hwrt_data *
pvr_hwrt_data_lookup(struct pvr_file *pvr_file, u32 handle, u32 index)
{
	struct pvr_hwrt_dataset *hwrt_dataset = pvr_hwrt_dataset_lookup(pvr_file, handle);

	if (hwrt_dataset) {
		if (index < ARRAY_SIZE(hwrt_dataset->data))
			return &hwrt_dataset->data[index];

		pvr_hwrt_dataset_put(hwrt_dataset);
	}

	return NULL;
}

/**
 * pvr_hwrt_data_put() - Release reference on HWRT data
 * @hwrt: Pointer to HWRT data to release reference on
 */
static __always_inline void
pvr_hwrt_data_put(struct pvr_hwrt_data *hwrt)
{
	if (hwrt)
		pvr_hwrt_dataset_put(hwrt->hwrt_dataset);
}

static __always_inline struct pvr_hwrt_data *
pvr_hwrt_data_get(struct pvr_hwrt_data *hwrt)
{
	if (hwrt)
		kref_get(&hwrt->hwrt_dataset->ref_count);

	return hwrt;
}

#endif /* PVR_HWRT_H */
