/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#ifndef PVR_ROGUE_FWIF_COMMON_H
#define PVR_ROGUE_FWIF_COMMON_H

#include <linux/build_bug.h>

/*
 * This macro represents a mask of LSBs that must be zero on data structure
 * sizes and offsets to ensure they are 8-byte granular on types shared between
 * the FW and host driver.
 */
#define PVR_FW_ALIGNMENT_LSB 7U

/* Macro to test structure size alignment. */
#define PVR_FW_STRUCT_SIZE_ASSERT(_a)                            \
	static_assert((sizeof(_a) & PVR_FW_ALIGNMENT_LSB) == 0U, \
		      "Size of " #_a " is not properly aligned")

/* The master definition for data masters known to the firmware. */

#define PVR_FWIF_DM_GP (0)
/* Either TDM or 2D DM is present. */
/* When the 'tla' feature is present in the hw (as per @pvr_device_features). */
#define PVR_FWIF_DM_2D (1)
/*
 * When the 'fastrender_dm' feature is present in the hw (as per
 * @pvr_device_features).
 */
#define PVR_FWIF_DM_TDM (1)

#define PVR_FWIF_DM_GEOM (2)
#define PVR_FWIF_DM_FRAG (3)
#define PVR_FWIF_DM_CDM (4)
#define PVR_FWIF_DM_RAY (5)
#define PVR_FWIF_DM_GEOM2 (6)
#define PVR_FWIF_DM_GEOM3 (7)
#define PVR_FWIF_DM_GEOM4 (8)

#define PVR_FWIF_DM_LAST PVR_FWIF_DM_GEOM4

/* Maximum number of DM in use: GP, 2D/TDM, GEOM, 3D, CDM, RAY, GEOM2, GEOM3, GEOM4 */
#define PVR_FWIF_DM_MAX (PVR_FWIF_DM_LAST + 1U)

/* GPU Utilisation states */
#define PVR_FWIF_GPU_UTIL_STATE_IDLE 0U
#define PVR_FWIF_GPU_UTIL_STATE_ACTIVE 1U
#define PVR_FWIF_GPU_UTIL_STATE_BLOCKED 2U
#define PVR_FWIF_GPU_UTIL_STATE_NUM 3U
#define PVR_FWIF_GPU_UTIL_STATE_MASK 0x3ULL

/*
 * Maximum amount of register writes that can be done by the register
 * programmer (FW or META DMA). This is not a HW limitation, it is only
 * a protection against malformed inputs to the register programmer.
 */
#define PVR_MAX_NUM_REGISTER_PROGRAMMER_WRITES 128U

#endif /* PVR_ROGUE_FWIF_COMMON_H */
