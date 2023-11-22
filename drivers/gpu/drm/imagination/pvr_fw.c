// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#include "pvr_device.h"
#include "pvr_device_info.h"
#include "pvr_fw.h"

#include <drm/drm_drv.h>
#include <linux/firmware.h>
#include <linux/sizes.h>

#define FW_MAX_SUPPORTED_MAJOR_VERSION 1

/**
 * pvr_fw_validate() - Parse firmware header and check compatibility
 * @pvr_dev: Device pointer.
 *
 * Returns:
 *  * 0 on success, or
 *  * -EINVAL if firmware is incompatible.
 */
static int
pvr_fw_validate(struct pvr_device *pvr_dev)
{
	struct drm_device *drm_dev = from_pvr_device(pvr_dev);
	const struct firmware *firmware = pvr_dev->fw_dev.firmware;
	const struct pvr_fw_layout_entry *layout_entries;
	const struct pvr_fw_info_header *header;
	const u8 *fw = firmware->data;
	u32 fw_offset = firmware->size - SZ_4K;
	u32 layout_table_size;
	u32 entry;

	if (firmware->size < SZ_4K || (firmware->size % FW_BLOCK_SIZE))
		return -EINVAL;

	header = (const struct pvr_fw_info_header *)&fw[fw_offset];

	if (header->info_version != PVR_FW_INFO_VERSION) {
		drm_err(drm_dev, "Unsupported fw info version %u\n",
			header->info_version);
		return -EINVAL;
	}

	if (header->header_len != sizeof(struct pvr_fw_info_header) ||
	    header->layout_entry_size != sizeof(struct pvr_fw_layout_entry) ||
	    header->layout_entry_num > PVR_FW_INFO_MAX_NUM_ENTRIES) {
		drm_err(drm_dev, "FW info format mismatch\n");
		return -EINVAL;
	}

	if (!(header->flags & PVR_FW_FLAGS_OPEN_SOURCE) ||
	    header->fw_version_major > FW_MAX_SUPPORTED_MAJOR_VERSION ||
	    header->fw_version_major == 0) {
		drm_err(drm_dev, "Unsupported FW version %u.%u (build: %u%s)\n",
			header->fw_version_major, header->fw_version_minor,
			header->fw_version_build,
			(header->flags & PVR_FW_FLAGS_OPEN_SOURCE) ? " OS" : "");
		return -EINVAL;
	}

	if (pvr_gpu_id_to_packed_bvnc(&pvr_dev->gpu_id) != header->bvnc) {
		struct pvr_gpu_id fw_gpu_id;

		packed_bvnc_to_pvr_gpu_id(header->bvnc, &fw_gpu_id);
		drm_err(drm_dev, "FW built for incorrect GPU ID %i.%i.%i.%i (expected %i.%i.%i.%i)\n",
			fw_gpu_id.b, fw_gpu_id.v, fw_gpu_id.n, fw_gpu_id.c,
			pvr_dev->gpu_id.b, pvr_dev->gpu_id.v, pvr_dev->gpu_id.n, pvr_dev->gpu_id.c);
		return -EINVAL;
	}

	fw_offset += header->header_len;
	layout_table_size =
		header->layout_entry_size * header->layout_entry_num;
	if ((fw_offset + layout_table_size) > firmware->size)
		return -EINVAL;

	layout_entries = (const struct pvr_fw_layout_entry *)&fw[fw_offset];
	for (entry = 0; entry < header->layout_entry_num; entry++) {
		u32 start_addr = layout_entries[entry].base_addr;
		u32 end_addr = start_addr + layout_entries[entry].alloc_size;

		if (start_addr >= end_addr)
			return -EINVAL;
	}

	fw_offset = (firmware->size - SZ_4K) - header->device_info_size;

	drm_info(drm_dev, "FW version v%u.%u (build %u OS)\n", header->fw_version_major,
		 header->fw_version_minor, header->fw_version_build);

	pvr_dev->fw_version.major = header->fw_version_major;
	pvr_dev->fw_version.minor = header->fw_version_minor;

	pvr_dev->fw_dev.header = header;
	pvr_dev->fw_dev.layout_entries = layout_entries;

	return 0;
}

static int
pvr_fw_get_device_info(struct pvr_device *pvr_dev)
{
	const struct firmware *firmware = pvr_dev->fw_dev.firmware;
	struct pvr_fw_device_info_header *header;
	const u8 *fw = firmware->data;
	const u64 *dev_info;
	u32 fw_offset;

	fw_offset = (firmware->size - SZ_4K) - pvr_dev->fw_dev.header->device_info_size;

	header = (struct pvr_fw_device_info_header *)&fw[fw_offset];
	dev_info = (u64 *)(header + 1);

	pvr_device_info_set_quirks(pvr_dev, dev_info, header->brn_mask_size);
	dev_info += header->brn_mask_size;

	pvr_device_info_set_enhancements(pvr_dev, dev_info, header->ern_mask_size);
	dev_info += header->ern_mask_size;

	return pvr_device_info_set_features(pvr_dev, dev_info, header->feature_mask_size,
					    header->feature_param_size);
}

/**
 * pvr_fw_validate_init_device_info() - Validate firmware and initialise device information
 * @pvr_dev: Target PowerVR device.
 *
 * This function must be called before querying device information.
 *
 * Returns:
 *  * 0 on success, or
 *  * -%EINVAL if firmware validation fails.
 */
int
pvr_fw_validate_init_device_info(struct pvr_device *pvr_dev)
{
	int err;

	err = pvr_fw_validate(pvr_dev);
	if (err)
		return err;

	return pvr_fw_get_device_info(pvr_dev);
}
