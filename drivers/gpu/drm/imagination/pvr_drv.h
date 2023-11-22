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

#endif /* PVR_DRV_H */
