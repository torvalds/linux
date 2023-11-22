/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#ifndef PVR_FW_H
#define PVR_FW_H

#include "pvr_fw_info.h"

#include <linux/types.h>

/* Forward declarations from "pvr_device.h". */
struct pvr_device;
struct pvr_file;

struct pvr_fw_device {
	/** @firmware: Handle to the firmware loaded into the device. */
	const struct firmware *firmware;

	/** @header: Pointer to firmware header. */
	const struct pvr_fw_info_header *header;

	/** @layout_entries: Pointer to firmware layout. */
	const struct pvr_fw_layout_entry *layout_entries;

	/**
	 * @processor_type: FW processor type for this device. Must be one of
	 *                  %PVR_FW_PROCESSOR_TYPE_*.
	 */
	u16 processor_type;
};

int pvr_fw_validate_init_device_info(struct pvr_device *pvr_dev);

#endif /* PVR_FW_H */
