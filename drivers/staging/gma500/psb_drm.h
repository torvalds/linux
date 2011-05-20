/**************************************************************************
 * Copyright (c) 2007, Intel Corporation.
 * All Rights Reserved.
 * Copyright (c) 2008, Tungsten Graphics Inc.  Cedar Park, TX., USA.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 **************************************************************************/

#ifndef _PSB_DRM_H_
#define _PSB_DRM_H_

#if defined(__linux__) && !defined(__KERNEL__)
#include<stdint.h>
#include <linux/types.h>
#include "drm_mode.h"
#endif

#include "psb_ttm_fence_user.h"
#include "psb_ttm_placement_user.h"

#define DRM_PSB_SAREA_MAJOR 0
#define DRM_PSB_SAREA_MINOR 2
#define PSB_FIXED_SHIFT 16

#define PSB_NUM_PIPE 3

/*
 * Public memory types.
 */

#define DRM_PSB_MEM_MMU 	TTM_PL_PRIV1
#define DRM_PSB_FLAG_MEM_MMU	TTM_PL_FLAG_PRIV1

#define TTM_PL_CI               TTM_PL_PRIV0
#define TTM_PL_FLAG_CI          TTM_PL_FLAG_PRIV0

#define TTM_PL_RAR              TTM_PL_PRIV2
#define TTM_PL_FLAG_RAR         TTM_PL_FLAG_PRIV2

typedef s32 psb_fixed;
typedef u32 psb_ufixed;

static inline s32 psb_int_to_fixed(int a)
{
	return a * (1 << PSB_FIXED_SHIFT);
}

static inline u32 psb_unsigned_to_ufixed(unsigned int a)
{
	return a << PSB_FIXED_SHIFT;
}

/*Status of the command sent to the gfx device.*/
typedef enum {
	DRM_CMD_SUCCESS,
	DRM_CMD_FAILED,
	DRM_CMD_HANG
} drm_cmd_status_t;

struct drm_psb_scanout {
	u32 buffer_id;	/* DRM buffer object ID */
	u32 rotation;	/* Rotation as in RR_rotation definitions */
	u32 stride;	/* Buffer stride in bytes */
	u32 depth;		/* Buffer depth in bits (NOT) bpp */
	u32 width;		/* Buffer width in pixels */
	u32 height;	/* Buffer height in lines */
	s32 transform[3][3];	/* Buffer composite transform */
	/* (scaling, rot, reflect) */
};

#define DRM_PSB_SAREA_OWNERS 16
#define DRM_PSB_SAREA_OWNER_2D 0
#define DRM_PSB_SAREA_OWNER_3D 1

#define DRM_PSB_SAREA_SCANOUTS 3

struct drm_psb_sarea {
	/* Track changes of this data structure */

	u32 major;
	u32 minor;

	/* Last context to touch part of hw */
	u32 ctx_owners[DRM_PSB_SAREA_OWNERS];

	/* Definition of front- and rotated buffers */
	u32 num_scanouts;
	struct drm_psb_scanout scanouts[DRM_PSB_SAREA_SCANOUTS];

	int planeA_x;
	int planeA_y;
	int planeA_w;
	int planeA_h;
	int planeB_x;
	int planeB_y;
	int planeB_w;
	int planeB_h;
	/* Number of active scanouts */
	u32 num_active_scanouts;
};

#define PSB_RELOC_MAGIC         0x67676767
#define PSB_RELOC_SHIFT_MASK    0x0000FFFF
#define PSB_RELOC_SHIFT_SHIFT   0
#define PSB_RELOC_ALSHIFT_MASK  0xFFFF0000
#define PSB_RELOC_ALSHIFT_SHIFT 16

#define PSB_RELOC_OP_OFFSET     0	/* Offset of the indicated
					 * buffer
					 */

struct drm_psb_reloc {
	u32 reloc_op;
	u32 where;		/* offset in destination buffer */
	u32 buffer;	/* Buffer reloc applies to */
	u32 mask;		/* Destination format: */
	u32 shift;		/* Destination format: */
	u32 pre_add;	/* Destination format: */
	u32 background;	/* Destination add */
	u32 dst_buffer;	/* Destination buffer. Index into buffer_list */
	u32 arg0;		/* Reloc-op dependent */
	u32 arg1;
};


#define PSB_GPU_ACCESS_READ         (1ULL << 32)
#define PSB_GPU_ACCESS_WRITE        (1ULL << 33)
#define PSB_GPU_ACCESS_MASK         (PSB_GPU_ACCESS_READ | PSB_GPU_ACCESS_WRITE)

#define PSB_BO_FLAG_COMMAND         (1ULL << 52)

#define PSB_ENGINE_2D 0
#define PSB_ENGINE_VIDEO 1
#define LNC_ENGINE_ENCODE 5

/*
 * For this fence class we have a couple of
 * fence types.
 */

#define _PSB_FENCE_EXE_SHIFT           0
#define _PSB_FENCE_FEEDBACK_SHIFT      4

#define _PSB_FENCE_TYPE_EXE         (1 << _PSB_FENCE_EXE_SHIFT)
#define _PSB_FENCE_TYPE_FEEDBACK    (1 << _PSB_FENCE_FEEDBACK_SHIFT)

#define PSB_NUM_ENGINES 6


#define PSB_FEEDBACK_OP_VISTEST (1 << 0)

struct drm_psb_extension_rep {
	s32 exists;
	u32 driver_ioctl_offset;
	u32 sarea_offset;
	u32 major;
	u32 minor;
	u32 pl;
};

#define DRM_PSB_EXT_NAME_LEN 128

union drm_psb_extension_arg {
	char extension[DRM_PSB_EXT_NAME_LEN];
	struct drm_psb_extension_rep rep;
};

struct psb_validate_req {
	u64 set_flags;
	u64 clear_flags;
	u64 next;
	u64 presumed_gpu_offset;
	u32 buffer_handle;
	u32 presumed_flags;
	u32 group;
	u32 pad64;
};

struct psb_validate_rep {
	u64 gpu_offset;
	u32 placement;
	u32 fence_type_mask;
};

#define PSB_USE_PRESUMED     (1 << 0)

struct psb_validate_arg {
	int handled;
	int ret;
	union {
		struct psb_validate_req req;
		struct psb_validate_rep rep;
	} d;
};


#define DRM_PSB_FENCE_NO_USER        (1 << 0)

struct psb_ttm_fence_rep {
	u32 handle;
	u32 fence_class;
	u32 fence_type;
	u32 signaled_types;
	u32 error;
};

/*
 * Feedback components:
 */

struct drm_psb_sizes_arg {
	u32 ta_mem_size;
	u32 mmu_size;
	u32 pds_size;
	u32 rastgeom_size;
	u32 tt_size;
	u32 vram_size;
};

struct drm_psb_dpst_lut_arg {
	uint8_t lut[256];
	int output_id;
};

#define PSB_DC_CRTC_SAVE 0x01
#define PSB_DC_CRTC_RESTORE 0x02
#define PSB_DC_OUTPUT_SAVE 0x04
#define PSB_DC_OUTPUT_RESTORE 0x08
#define PSB_DC_CRTC_MASK 0x03
#define PSB_DC_OUTPUT_MASK 0x0C

struct drm_psb_dc_state_arg {
	u32 flags;
	u32 obj_id;
};

struct drm_psb_mode_operation_arg {
	u32 obj_id;
	u16 operation;
	struct drm_mode_modeinfo mode;
	void *data;
};

struct drm_psb_stolen_memory_arg {
	u32 base;
	u32 size;
};

/*Display Register Bits*/
#define REGRWBITS_PFIT_CONTROLS			(1 << 0)
#define REGRWBITS_PFIT_AUTOSCALE_RATIOS		(1 << 1)
#define REGRWBITS_PFIT_PROGRAMMED_SCALE_RATIOS	(1 << 2)
#define REGRWBITS_PIPEASRC			(1 << 3)
#define REGRWBITS_PIPEBSRC			(1 << 4)
#define REGRWBITS_VTOTAL_A			(1 << 5)
#define REGRWBITS_VTOTAL_B			(1 << 6)
#define REGRWBITS_DSPACNTR	(1 << 8)
#define REGRWBITS_DSPBCNTR	(1 << 9)
#define REGRWBITS_DSPCCNTR	(1 << 10)

/*Overlay Register Bits*/
#define OV_REGRWBITS_OVADD			(1 << 0)
#define OV_REGRWBITS_OGAM_ALL			(1 << 1)

#define OVC_REGRWBITS_OVADD                  (1 << 2)
#define OVC_REGRWBITS_OGAM_ALL			(1 << 3)

struct drm_psb_register_rw_arg {
	u32 b_force_hw_on;

	u32 display_read_mask;
	u32 display_write_mask;

	struct {
		u32 pfit_controls;
		u32 pfit_autoscale_ratios;
		u32 pfit_programmed_scale_ratios;
		u32 pipeasrc;
		u32 pipebsrc;
		u32 vtotal_a;
		u32 vtotal_b;
	} display;

	u32 overlay_read_mask;
	u32 overlay_write_mask;

	struct {
		u32 OVADD;
		u32 OGAMC0;
		u32 OGAMC1;
		u32 OGAMC2;
		u32 OGAMC3;
		u32 OGAMC4;
		u32 OGAMC5;
        	u32 IEP_ENABLED;
        	u32 IEP_BLE_MINMAX;
        	u32 IEP_BSSCC_CONTROL;
                u32 b_wait_vblank;
	} overlay;

	u32 sprite_enable_mask;
	u32 sprite_disable_mask;

	struct {
		u32 dspa_control;
		u32 dspa_key_value;
		u32 dspa_key_mask;
		u32 dspc_control;
		u32 dspc_stride;
		u32 dspc_position;
		u32 dspc_linear_offset;
		u32 dspc_size;
		u32 dspc_surface;
	} sprite;

	u32 subpicture_enable_mask;
	u32 subpicture_disable_mask;
};

struct psb_gtt_mapping_arg {
	void *hKernelMemInfo;
	u32 offset_pages;
};

struct drm_psb_getpageaddrs_arg {
	u32 handle;
	unsigned long *page_addrs;
	unsigned long gtt_offset;
};

/* Controlling the kernel modesetting buffers */

#define DRM_PSB_KMS_OFF		0x00
#define DRM_PSB_KMS_ON		0x01
#define DRM_PSB_VT_LEAVE        0x02
#define DRM_PSB_VT_ENTER        0x03
#define DRM_PSB_EXTENSION       0x06
#define DRM_PSB_SIZES           0x07
#define DRM_PSB_FUSE_REG	0x08
#define DRM_PSB_VBT		0x09
#define DRM_PSB_DC_STATE	0x0A
#define DRM_PSB_ADB		0x0B
#define DRM_PSB_MODE_OPERATION	0x0C
#define DRM_PSB_STOLEN_MEMORY	0x0D
#define DRM_PSB_REGISTER_RW	0x0E
#define DRM_PSB_GTT_MAP         0x0F
#define DRM_PSB_GTT_UNMAP       0x10
#define DRM_PSB_GETPAGEADDRS	0x11
/**
 * NOTE: Add new commands here, but increment
 * the values below and increment their
 * corresponding defines where they're
 * defined elsewhere.
 */
#define DRM_PVR_RESERVED1	0x12
#define DRM_PVR_RESERVED2	0x13
#define DRM_PVR_RESERVED3	0x14
#define DRM_PVR_RESERVED4	0x15
#define DRM_PVR_RESERVED5	0x16

#define DRM_PSB_HIST_ENABLE	0x17
#define DRM_PSB_HIST_STATUS	0x18
#define DRM_PSB_UPDATE_GUARD	0x19
#define DRM_PSB_INIT_COMM	0x1A
#define DRM_PSB_DPST		0x1B
#define DRM_PSB_GAMMA		0x1C
#define DRM_PSB_DPST_BL		0x1D

#define DRM_PVR_RESERVED6	0x1E

#define DRM_PSB_GET_PIPE_FROM_CRTC_ID 0x1F

#define PSB_MODE_OPERATION_MODE_VALID	0x01
#define PSB_MODE_OPERATION_SET_DC_BASE  0x02

struct drm_psb_get_pipe_from_crtc_id_arg {
	/** ID of CRTC being requested **/
	u32 crtc_id;

	/** pipe of requested CRTC **/
	u32 pipe;
};

#endif
