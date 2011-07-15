/**************************************************************************
 * Copyright (c) 2007-2011, Intel Corporation.
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

#define PSB_NUM_PIPE 3

#define PSB_GPU_ACCESS_READ         (1ULL << 32)
#define PSB_GPU_ACCESS_WRITE        (1ULL << 33)
#define PSB_GPU_ACCESS_MASK         (PSB_GPU_ACCESS_READ | PSB_GPU_ACCESS_WRITE)

#define PSB_BO_FLAG_COMMAND         (1ULL << 52)

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

/* Controlling the kernel modesetting buffers */

#define DRM_PSB_SIZES           0x07
#define DRM_PSB_FUSE_REG	0x08
#define DRM_PSB_DC_STATE	0x0A
#define DRM_PSB_ADB		0x0B
#define DRM_PSB_MODE_OPERATION	0x0C
#define DRM_PSB_STOLEN_MEMORY	0x0D
#define DRM_PSB_REGISTER_RW	0x0E

/*
 * NOTE: Add new commands here, but increment
 * the values below and increment their
 * corresponding defines where they're
 * defined elsewhere.
 */

#define DRM_PSB_GEM_CREATE	0x10
#define DRM_PSB_2D_OP		0x11
#define DRM_PSB_GEM_MMAP	0x12
#define DRM_PSB_DPST		0x1B
#define DRM_PSB_GAMMA		0x1C
#define DRM_PSB_DPST_BL		0x1D
#define DRM_PSB_GET_PIPE_FROM_CRTC_ID 0x1F

#define PSB_MODE_OPERATION_MODE_VALID	0x01
#define PSB_MODE_OPERATION_SET_DC_BASE  0x02

struct drm_psb_get_pipe_from_crtc_id_arg {
	/** ID of CRTC being requested **/
	u32 crtc_id;

	/** pipe of requested CRTC **/
	u32 pipe;
};

/* FIXME: move this into a medfield header once we are sure it isn't needed for an
   ioctl  */
struct psb_drm_dpu_rect {  
	int x, y;             
	int width, height;    
};  

struct drm_psb_gem_create {
	__u64 size;
	__u32 handle;
	__u32 flags;
#define PSB_GEM_CREATE_STOLEN		1	/* Stolen memory can be used */
};

#define PSB_2D_OP_BUFLEN		16

struct drm_psb_2d_op {
	__u32 src;		/* Handles, only src supported right now */
	__u32 dst;
	__u32 mask;
	__u32 pat;
	__u32 size;		/* In dwords of command */
	__u32 spare;		/* And bumps array to u64 align */
	__u32 cmd[PSB_2D_OP_BUFLEN];
};

struct drm_psb_gem_mmap {
	__u32 handle;
	__u32 pad;
	/**
	 * Fake offset to use for subsequent mmap call
	 *
	 * This is a fixed-size type for 32/64 compatibility.
	 */
	__u64 offset;
};

#endif
