/* mga_drm.h -- Public header for the Matrox g200/g400 driver -*- linux-c -*-
 * Created: Tue Jan 25 01:50:01 1999 by jhartmann@precisioninsight.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Jeff Hartmann <jhartmann@valinux.com>
 *    Keith Whitwell <keith@tungstengraphics.com>
 *
 * Rewritten by:
 *    Gareth Hughes <gareth@valinux.com>
 */

#ifndef __MGA_DRM_H__
#define __MGA_DRM_H__

/* WARNING: If you change any of these defines, make sure to change the
 * defines in the Xserver file (mga_sarea.h)
 */

#ifndef __MGA_SAREA_DEFINES__
#define __MGA_SAREA_DEFINES__

/* WARP pipe flags
 */
#define MGA_F			0x1	/* fog */
#define MGA_A			0x2	/* alpha */
#define MGA_S			0x4	/* specular */
#define MGA_T2			0x8	/* multitexture */

#define MGA_WARP_TGZ		0
#define MGA_WARP_TGZF		(MGA_F)
#define MGA_WARP_TGZA		(MGA_A)
#define MGA_WARP_TGZAF		(MGA_F|MGA_A)
#define MGA_WARP_TGZS		(MGA_S)
#define MGA_WARP_TGZSF		(MGA_S|MGA_F)
#define MGA_WARP_TGZSA		(MGA_S|MGA_A)
#define MGA_WARP_TGZSAF		(MGA_S|MGA_F|MGA_A)
#define MGA_WARP_T2GZ		(MGA_T2)
#define MGA_WARP_T2GZF		(MGA_T2|MGA_F)
#define MGA_WARP_T2GZA		(MGA_T2|MGA_A)
#define MGA_WARP_T2GZAF		(MGA_T2|MGA_A|MGA_F)
#define MGA_WARP_T2GZS		(MGA_T2|MGA_S)
#define MGA_WARP_T2GZSF		(MGA_T2|MGA_S|MGA_F)
#define MGA_WARP_T2GZSA		(MGA_T2|MGA_S|MGA_A)
#define MGA_WARP_T2GZSAF	(MGA_T2|MGA_S|MGA_F|MGA_A)

#define MGA_MAX_G200_PIPES	8	/* no multitex */
#define MGA_MAX_G400_PIPES	16
#define MGA_MAX_WARP_PIPES	MGA_MAX_G400_PIPES
#define MGA_WARP_UCODE_SIZE	32768	/* in bytes */

#define MGA_CARD_TYPE_G200	1
#define MGA_CARD_TYPE_G400	2
#define MGA_CARD_TYPE_G450	3	/* not currently used */
#define MGA_CARD_TYPE_G550	4

#define MGA_FRONT		0x1
#define MGA_BACK		0x2
#define MGA_DEPTH		0x4

/* What needs to be changed for the current vertex dma buffer?
 */
#define MGA_UPLOAD_CONTEXT	0x1
#define MGA_UPLOAD_TEX0		0x2
#define MGA_UPLOAD_TEX1		0x4
#define MGA_UPLOAD_PIPE		0x8
#define MGA_UPLOAD_TEX0IMAGE	0x10	/* handled client-side */
#define MGA_UPLOAD_TEX1IMAGE	0x20	/* handled client-side */
#define MGA_UPLOAD_2D		0x40
#define MGA_WAIT_AGE		0x80	/* handled client-side */
#define MGA_UPLOAD_CLIPRECTS	0x100	/* handled client-side */
#if 0
#define MGA_DMA_FLUSH		0x200	/* set when someone gets the lock
					   quiescent */
#endif

/* 32 buffers of 64k each, total 2 meg.
 */
#define MGA_BUFFER_SIZE		(1 << 16)
#define MGA_NUM_BUFFERS		128

/* Keep these small for testing.
 */
#define MGA_NR_SAREA_CLIPRECTS	8

/* 2 heaps (1 for card, 1 for agp), each divided into upto 128
 * regions, subject to a minimum region size of (1<<16) == 64k.
 *
 * Clients may subdivide regions internally, but when sharing between
 * clients, the region size is the minimum granularity.
 */

#define MGA_CARD_HEAP			0
#define MGA_AGP_HEAP			1
#define MGA_NR_TEX_HEAPS		2
#define MGA_NR_TEX_REGIONS		16
#define MGA_LOG_MIN_TEX_REGION_SIZE	16

#define  DRM_MGA_IDLE_RETRY          2048

#endif				/* __MGA_SAREA_DEFINES__ */

/* Setup registers for 3D context
 */
typedef struct {
	unsigned int dstorg;
	unsigned int maccess;
	unsigned int plnwt;
	unsigned int dwgctl;
	unsigned int alphactrl;
	unsigned int fogcolor;
	unsigned int wflag;
	unsigned int tdualstage0;
	unsigned int tdualstage1;
	unsigned int fcol;
	unsigned int stencil;
	unsigned int stencilctl;
} drm_mga_context_regs_t;

/* Setup registers for 2D, X server
 */
typedef struct {
	unsigned int pitch;
} drm_mga_server_regs_t;

/* Setup registers for each texture unit
 */
typedef struct {
	unsigned int texctl;
	unsigned int texctl2;
	unsigned int texfilter;
	unsigned int texbordercol;
	unsigned int texorg;
	unsigned int texwidth;
	unsigned int texheight;
	unsigned int texorg1;
	unsigned int texorg2;
	unsigned int texorg3;
	unsigned int texorg4;
} drm_mga_texture_regs_t;

/* General aging mechanism
 */
typedef struct {
	unsigned int head;	/* Position of head pointer          */
	unsigned int wrap;	/* Primary DMA wrap count            */
} drm_mga_age_t;

typedef struct _drm_mga_sarea {
	/* The channel for communication of state information to the kernel
	 * on firing a vertex dma buffer.
	 */
	drm_mga_context_regs_t context_state;
	drm_mga_server_regs_t server_state;
	drm_mga_texture_regs_t tex_state[2];
	unsigned int warp_pipe;
	unsigned int dirty;
	unsigned int vertsize;

	/* The current cliprects, or a subset thereof.
	 */
	struct drm_clip_rect boxes[MGA_NR_SAREA_CLIPRECTS];
	unsigned int nbox;

	/* Information about the most recently used 3d drawable.  The
	 * client fills in the req_* fields, the server fills in the
	 * exported_ fields and puts the cliprects into boxes, above.
	 *
	 * The client clears the exported_drawable field before
	 * clobbering the boxes data.
	 */
	unsigned int req_drawable;	/* the X drawable id */
	unsigned int req_draw_buffer;	/* MGA_FRONT or MGA_BACK */

	unsigned int exported_drawable;
	unsigned int exported_index;
	unsigned int exported_stamp;
	unsigned int exported_buffers;
	unsigned int exported_nfront;
	unsigned int exported_nback;
	int exported_back_x, exported_front_x, exported_w;
	int exported_back_y, exported_front_y, exported_h;
	struct drm_clip_rect exported_boxes[MGA_NR_SAREA_CLIPRECTS];

	/* Counters for aging textures and for client-side throttling.
	 */
	unsigned int status[4];
	unsigned int last_wrap;

	drm_mga_age_t last_frame;
	unsigned int last_enqueue;	/* last time a buffer was enqueued */
	unsigned int last_dispatch;	/* age of the most recently dispatched buffer */
	unsigned int last_quiescent;	/*  */

	/* LRU lists for texture memory in agp space and on the card.
	 */
	struct drm_tex_region texList[MGA_NR_TEX_HEAPS][MGA_NR_TEX_REGIONS + 1];
	unsigned int texAge[MGA_NR_TEX_HEAPS];

	/* Mechanism to validate card state.
	 */
	int ctxOwner;
} drm_mga_sarea_t;

/* MGA specific ioctls
 * The device specific ioctl range is 0x40 to 0x79.
 */
#define DRM_MGA_INIT     0x00
#define DRM_MGA_FLUSH    0x01
#define DRM_MGA_RESET    0x02
#define DRM_MGA_SWAP     0x03
#define DRM_MGA_CLEAR    0x04
#define DRM_MGA_VERTEX   0x05
#define DRM_MGA_INDICES  0x06
#define DRM_MGA_ILOAD    0x07
#define DRM_MGA_BLIT     0x08
#define DRM_MGA_GETPARAM 0x09

/* 3.2:
 * ioctls for operating on fences.
 */
#define DRM_MGA_SET_FENCE      0x0a
#define DRM_MGA_WAIT_FENCE     0x0b
#define DRM_MGA_DMA_BOOTSTRAP  0x0c

#define DRM_IOCTL_MGA_INIT     DRM_IOW( DRM_COMMAND_BASE + DRM_MGA_INIT, drm_mga_init_t)
#define DRM_IOCTL_MGA_FLUSH    DRM_IOW( DRM_COMMAND_BASE + DRM_MGA_FLUSH, drm_lock_t)
#define DRM_IOCTL_MGA_RESET    DRM_IO(  DRM_COMMAND_BASE + DRM_MGA_RESET)
#define DRM_IOCTL_MGA_SWAP     DRM_IO(  DRM_COMMAND_BASE + DRM_MGA_SWAP)
#define DRM_IOCTL_MGA_CLEAR    DRM_IOW( DRM_COMMAND_BASE + DRM_MGA_CLEAR, drm_mga_clear_t)
#define DRM_IOCTL_MGA_VERTEX   DRM_IOW( DRM_COMMAND_BASE + DRM_MGA_VERTEX, drm_mga_vertex_t)
#define DRM_IOCTL_MGA_INDICES  DRM_IOW( DRM_COMMAND_BASE + DRM_MGA_INDICES, drm_mga_indices_t)
#define DRM_IOCTL_MGA_ILOAD    DRM_IOW( DRM_COMMAND_BASE + DRM_MGA_ILOAD, drm_mga_iload_t)
#define DRM_IOCTL_MGA_BLIT     DRM_IOW( DRM_COMMAND_BASE + DRM_MGA_BLIT, drm_mga_blit_t)
#define DRM_IOCTL_MGA_GETPARAM DRM_IOWR(DRM_COMMAND_BASE + DRM_MGA_GETPARAM, drm_mga_getparam_t)
#define DRM_IOCTL_MGA_SET_FENCE     DRM_IOW( DRM_COMMAND_BASE + DRM_MGA_SET_FENCE, uint32_t)
#define DRM_IOCTL_MGA_WAIT_FENCE    DRM_IOWR(DRM_COMMAND_BASE + DRM_MGA_WAIT_FENCE, uint32_t)
#define DRM_IOCTL_MGA_DMA_BOOTSTRAP DRM_IOWR(DRM_COMMAND_BASE + DRM_MGA_DMA_BOOTSTRAP, drm_mga_dma_bootstrap_t)

typedef struct _drm_mga_warp_index {
	int installed;
	unsigned long phys_addr;
	int size;
} drm_mga_warp_index_t;

typedef struct drm_mga_init {
	enum {
		MGA_INIT_DMA = 0x01,
		MGA_CLEANUP_DMA = 0x02
	} func;

	unsigned long sarea_priv_offset;

	int chipset;
	int sgram;

	unsigned int maccess;

	unsigned int fb_cpp;
	unsigned int front_offset, front_pitch;
	unsigned int back_offset, back_pitch;

	unsigned int depth_cpp;
	unsigned int depth_offset, depth_pitch;

	unsigned int texture_offset[MGA_NR_TEX_HEAPS];
	unsigned int texture_size[MGA_NR_TEX_HEAPS];

	unsigned long fb_offset;
	unsigned long mmio_offset;
	unsigned long status_offset;
	unsigned long warp_offset;
	unsigned long primary_offset;
	unsigned long buffers_offset;
} drm_mga_init_t;

typedef struct drm_mga_dma_bootstrap {
	/**
	 * \name AGP texture region
	 *
	 * On return from the DRM_MGA_DMA_BOOTSTRAP ioctl, these fields will
	 * be filled in with the actual AGP texture settings.
	 *
	 * \warning
	 * If these fields are non-zero, but dma_mga_dma_bootstrap::agp_mode
	 * is zero, it means that PCI memory (most likely through the use of
	 * an IOMMU) is being used for "AGP" textures.
	 */
	/*@{ */
	unsigned long texture_handle; /**< Handle used to map AGP textures. */
	uint32_t texture_size;	      /**< Size of the AGP texture region. */
	/*@} */

	/**
	 * Requested size of the primary DMA region.
	 *
	 * On return from the DRM_MGA_DMA_BOOTSTRAP ioctl, this field will be
	 * filled in with the actual AGP mode.  If AGP was not available
	 */
	uint32_t primary_size;

	/**
	 * Requested number of secondary DMA buffers.
	 *
	 * On return from the DRM_MGA_DMA_BOOTSTRAP ioctl, this field will be
	 * filled in with the actual number of secondary DMA buffers
	 * allocated.  Particularly when PCI DMA is used, this may be
	 * (subtantially) less than the number requested.
	 */
	uint32_t secondary_bin_count;

	/**
	 * Requested size of each secondary DMA buffer.
	 *
	 * While the kernel \b is free to reduce
	 * dma_mga_dma_bootstrap::secondary_bin_count, it is \b not allowed
	 * to reduce dma_mga_dma_bootstrap::secondary_bin_size.
	 */
	uint32_t secondary_bin_size;

	/**
	 * Bit-wise mask of AGPSTAT2_* values.  Currently only \c AGPSTAT2_1X,
	 * \c AGPSTAT2_2X, and \c AGPSTAT2_4X are supported.  If this value is
	 * zero, it means that PCI DMA should be used, even if AGP is
	 * possible.
	 *
	 * On return from the DRM_MGA_DMA_BOOTSTRAP ioctl, this field will be
	 * filled in with the actual AGP mode.  If AGP was not available
	 * (i.e., PCI DMA was used), this value will be zero.
	 */
	uint32_t agp_mode;

	/**
	 * Desired AGP GART size, measured in megabytes.
	 */
	uint8_t agp_size;
} drm_mga_dma_bootstrap_t;

typedef struct drm_mga_clear {
	unsigned int flags;
	unsigned int clear_color;
	unsigned int clear_depth;
	unsigned int color_mask;
	unsigned int depth_mask;
} drm_mga_clear_t;

typedef struct drm_mga_vertex {
	int idx;		/* buffer to queue */
	int used;		/* bytes in use */
	int discard;		/* client finished with buffer?  */
} drm_mga_vertex_t;

typedef struct drm_mga_indices {
	int idx;		/* buffer to queue */
	unsigned int start;
	unsigned int end;
	int discard;		/* client finished with buffer?  */
} drm_mga_indices_t;

typedef struct drm_mga_iload {
	int idx;
	unsigned int dstorg;
	unsigned int length;
} drm_mga_iload_t;

typedef struct _drm_mga_blit {
	unsigned int planemask;
	unsigned int srcorg;
	unsigned int dstorg;
	int src_pitch, dst_pitch;
	int delta_sx, delta_sy;
	int delta_dx, delta_dy;
	int height, ydir;	/* flip image vertically */
	int source_pitch, dest_pitch;
} drm_mga_blit_t;

/* 3.1: An ioctl to get parameters that aren't available to the 3d
 * client any other way.
 */
#define MGA_PARAM_IRQ_NR            1

/* 3.2: Query the actual card type.  The DDX only distinguishes between
 * G200 chips and non-G200 chips, which it calls G400.  It turns out that
 * there are some very sublte differences between the G4x0 chips and the G550
 * chips.  Using this parameter query, a client-side driver can detect the
 * difference between a G4x0 and a G550.
 */
#define MGA_PARAM_CARD_TYPE         2

typedef struct drm_mga_getparam {
	int param;
	void __user *value;
} drm_mga_getparam_t;

#endif
