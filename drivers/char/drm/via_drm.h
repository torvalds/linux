/*
 * Copyright 1998-2003 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2003 S3 Graphics, Inc. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * VIA, S3 GRAPHICS, AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#ifndef _VIA_DRM_H_
#define _VIA_DRM_H_

/* WARNING: These defines must be the same as what the Xserver uses.
 * if you change them, you must change the defines in the Xserver.
 */

#ifndef _VIA_DEFINES_
#define _VIA_DEFINES_

#ifndef __KERNEL__
#include "via_drmclient.h"
#endif

#define VIA_NR_SAREA_CLIPRECTS 		8
#define VIA_NR_XVMC_PORTS               10
#define VIA_NR_XVMC_LOCKS               5
#define VIA_MAX_CACHELINE_SIZE          64
#define XVMCLOCKPTR(saPriv,lockNo)					\
	((volatile drm_hw_lock_t *)(((((unsigned long) (saPriv)->XvMCLockArea) + \
				      (VIA_MAX_CACHELINE_SIZE - 1)) &	\
				     ~(VIA_MAX_CACHELINE_SIZE - 1)) +	\
				    VIA_MAX_CACHELINE_SIZE*(lockNo)))

/* Each region is a minimum of 64k, and there are at most 64 of them.
 */
#define VIA_NR_TEX_REGIONS 64
#define VIA_LOG_MIN_TEX_REGION_SIZE 16
#endif

#define VIA_UPLOAD_TEX0IMAGE  0x1	/* handled clientside */
#define VIA_UPLOAD_TEX1IMAGE  0x2	/* handled clientside */
#define VIA_UPLOAD_CTX        0x4
#define VIA_UPLOAD_BUFFERS    0x8
#define VIA_UPLOAD_TEX0       0x10
#define VIA_UPLOAD_TEX1       0x20
#define VIA_UPLOAD_CLIPRECTS  0x40
#define VIA_UPLOAD_ALL        0xff

/* VIA specific ioctls */
#define DRM_VIA_ALLOCMEM	0x00
#define DRM_VIA_FREEMEM	        0x01
#define DRM_VIA_AGP_INIT	0x02
#define DRM_VIA_FB_INIT	        0x03
#define DRM_VIA_MAP_INIT	0x04
#define DRM_VIA_DEC_FUTEX       0x05
#define NOT_USED
#define DRM_VIA_DMA_INIT	0x07
#define DRM_VIA_CMDBUFFER	0x08
#define DRM_VIA_FLUSH	        0x09
#define DRM_VIA_PCICMD	        0x0a
#define DRM_VIA_CMDBUF_SIZE	0x0b
#define NOT_USED
#define DRM_VIA_WAIT_IRQ        0x0d
#define DRM_VIA_DMA_BLIT        0x0e
#define DRM_VIA_BLIT_SYNC       0x0f

#define DRM_IOCTL_VIA_ALLOCMEM	  DRM_IOWR(DRM_COMMAND_BASE + DRM_VIA_ALLOCMEM, drm_via_mem_t)
#define DRM_IOCTL_VIA_FREEMEM	  DRM_IOW( DRM_COMMAND_BASE + DRM_VIA_FREEMEM, drm_via_mem_t)
#define DRM_IOCTL_VIA_AGP_INIT	  DRM_IOWR(DRM_COMMAND_BASE + DRM_VIA_AGP_INIT, drm_via_agp_t)
#define DRM_IOCTL_VIA_FB_INIT	  DRM_IOWR(DRM_COMMAND_BASE + DRM_VIA_FB_INIT, drm_via_fb_t)
#define DRM_IOCTL_VIA_MAP_INIT	  DRM_IOWR(DRM_COMMAND_BASE + DRM_VIA_MAP_INIT, drm_via_init_t)
#define DRM_IOCTL_VIA_DEC_FUTEX   DRM_IOW( DRM_COMMAND_BASE + DRM_VIA_DEC_FUTEX, drm_via_futex_t)
#define DRM_IOCTL_VIA_DMA_INIT	  DRM_IOWR(DRM_COMMAND_BASE + DRM_VIA_DMA_INIT, drm_via_dma_init_t)
#define DRM_IOCTL_VIA_CMDBUFFER	  DRM_IOW( DRM_COMMAND_BASE + DRM_VIA_CMDBUFFER, drm_via_cmdbuffer_t)
#define DRM_IOCTL_VIA_FLUSH	  DRM_IO(  DRM_COMMAND_BASE + DRM_VIA_FLUSH)
#define DRM_IOCTL_VIA_PCICMD	  DRM_IOW( DRM_COMMAND_BASE + DRM_VIA_PCICMD, drm_via_cmdbuffer_t)
#define DRM_IOCTL_VIA_CMDBUF_SIZE DRM_IOWR( DRM_COMMAND_BASE + DRM_VIA_CMDBUF_SIZE, \
					    drm_via_cmdbuf_size_t)
#define DRM_IOCTL_VIA_WAIT_IRQ    DRM_IOWR( DRM_COMMAND_BASE + DRM_VIA_WAIT_IRQ, drm_via_irqwait_t)
#define DRM_IOCTL_VIA_DMA_BLIT    DRM_IOW(DRM_COMMAND_BASE + DRM_VIA_DMA_BLIT, drm_via_dmablit_t)
#define DRM_IOCTL_VIA_BLIT_SYNC   DRM_IOW(DRM_COMMAND_BASE + DRM_VIA_BLIT_SYNC, drm_via_blitsync_t)

/* Indices into buf.Setup where various bits of state are mirrored per
 * context and per buffer.  These can be fired at the card as a unit,
 * or in a piecewise fashion as required.
 */

#define VIA_TEX_SETUP_SIZE 8

/* Flags for clear ioctl
 */
#define VIA_FRONT   0x1
#define VIA_BACK    0x2
#define VIA_DEPTH   0x4
#define VIA_STENCIL 0x8
#define VIA_MEM_VIDEO   0	/* matches drm constant */
#define VIA_MEM_AGP     1	/* matches drm constant */
#define VIA_MEM_SYSTEM  2
#define VIA_MEM_MIXED   3
#define VIA_MEM_UNKNOWN 4

typedef struct {
	uint32_t offset;
	uint32_t size;
} drm_via_agp_t;

typedef struct {
	uint32_t offset;
	uint32_t size;
} drm_via_fb_t;

typedef struct {
	uint32_t context;
	uint32_t type;
	uint32_t size;
	unsigned long index;
	unsigned long offset;
} drm_via_mem_t;

typedef struct _drm_via_init {
	enum {
		VIA_INIT_MAP = 0x01,
		VIA_CLEANUP_MAP = 0x02
	} func;

	unsigned long sarea_priv_offset;
	unsigned long fb_offset;
	unsigned long mmio_offset;
	unsigned long agpAddr;
} drm_via_init_t;

typedef struct _drm_via_futex {
	enum {
		VIA_FUTEX_WAIT = 0x00,
		VIA_FUTEX_WAKE = 0X01
	} func;
	uint32_t ms;
	uint32_t lock;
	uint32_t val;
} drm_via_futex_t;

typedef struct _drm_via_dma_init {
	enum {
		VIA_INIT_DMA = 0x01,
		VIA_CLEANUP_DMA = 0x02,
		VIA_DMA_INITIALIZED = 0x03
	} func;

	unsigned long offset;
	unsigned long size;
	unsigned long reg_pause_addr;
} drm_via_dma_init_t;

typedef struct _drm_via_cmdbuffer {
	char __user *buf;
	unsigned long size;
} drm_via_cmdbuffer_t;

/* Warning: If you change the SAREA structure you must change the Xserver
 * structure as well */

typedef struct _drm_via_tex_region {
	unsigned char next, prev;	/* indices to form a circular LRU  */
	unsigned char inUse;	/* owned by a client, or free? */
	int age;		/* tracked by clients to update local LRU's */
} drm_via_tex_region_t;

typedef struct _drm_via_sarea {
	unsigned int dirty;
	unsigned int nbox;
	drm_clip_rect_t boxes[VIA_NR_SAREA_CLIPRECTS];
	drm_via_tex_region_t texList[VIA_NR_TEX_REGIONS + 1];
	int texAge;		/* last time texture was uploaded */
	int ctxOwner;		/* last context to upload state */
	int vertexPrim;

	/*
	 * Below is for XvMC.
	 * We want the lock integers alone on, and aligned to, a cache line.
	 * Therefore this somewhat strange construct.
	 */

	char XvMCLockArea[VIA_MAX_CACHELINE_SIZE * (VIA_NR_XVMC_LOCKS + 1)];

	unsigned int XvMCDisplaying[VIA_NR_XVMC_PORTS];
	unsigned int XvMCSubPicOn[VIA_NR_XVMC_PORTS];
	unsigned int XvMCCtxNoGrabbed;	/* Last context to hold decoder */

	/* Used by the 3d driver only at this point, for pageflipping:
	 */
	unsigned int pfCurrentOffset;
} drm_via_sarea_t;

typedef struct _drm_via_cmdbuf_size {
	enum {
		VIA_CMDBUF_SPACE = 0x01,
		VIA_CMDBUF_LAG = 0x02
	} func;
	int wait;
	uint32_t size;
} drm_via_cmdbuf_size_t;

typedef enum {
	VIA_IRQ_ABSOLUTE = 0x0,
	VIA_IRQ_RELATIVE = 0x1,
	VIA_IRQ_SIGNAL = 0x10000000,
	VIA_IRQ_FORCE_SEQUENCE = 0x20000000
} via_irq_seq_type_t;

#define VIA_IRQ_FLAGS_MASK 0xF0000000

enum drm_via_irqs {
	drm_via_irq_hqv0 = 0,
	drm_via_irq_hqv1,
	drm_via_irq_dma0_dd,
	drm_via_irq_dma0_td,
	drm_via_irq_dma1_dd,
	drm_via_irq_dma1_td,
	drm_via_irq_num
};

struct drm_via_wait_irq_request {
	unsigned irq;
	via_irq_seq_type_t type;
	uint32_t sequence;
	uint32_t signal;
};

typedef union drm_via_irqwait {
	struct drm_via_wait_irq_request request;
	struct drm_wait_vblank_reply reply;
} drm_via_irqwait_t;

typedef struct drm_via_blitsync {
	uint32_t sync_handle;
	unsigned engine;
} drm_via_blitsync_t;

typedef struct drm_via_dmablit {
	uint32_t num_lines;
	uint32_t line_length;
	
	uint32_t fb_addr;
	uint32_t fb_stride;

	unsigned char *mem_addr;
	uint32_t mem_stride;

	int bounce_buffer;
	int to_fb;

	drm_via_blitsync_t sync;
} drm_via_dmablit_t;

#endif				/* _VIA_DRM_H_ */
