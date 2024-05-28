/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#ifndef PVR_FW_STARTSTOP_H
#define PVR_FW_STARTSTOP_H

/* Forward declaration from pvr_device.h. */
struct pvr_device;

int pvr_fw_start(struct pvr_device *pvr_dev);
int pvr_fw_stop(struct pvr_device *pvr_dev);

#endif /* PVR_FW_STARTSTOP_H */
