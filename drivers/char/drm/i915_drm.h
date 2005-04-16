#ifndef _I915_DRM_H_
#define _I915_DRM_H_

/* Please note that modifications to all structs defined here are
 * subject to backwards-compatibility constraints.
 */

#include "drm.h"

/* Each region is a minimum of 16k, and there are at most 255 of them.
 */
#define I915_NR_TEX_REGIONS 255	/* table size 2k - maximum due to use
				 * of chars for next/prev indices */
#define I915_LOG_MIN_TEX_REGION_SIZE 14

typedef struct _drm_i915_init {
	enum {
		I915_INIT_DMA = 0x01,
		I915_CLEANUP_DMA = 0x02,
		I915_RESUME_DMA = 0x03
	} func;
	unsigned int mmio_offset;
	int sarea_priv_offset;
	unsigned int ring_start;
	unsigned int ring_end;
	unsigned int ring_size;
	unsigned int front_offset;
	unsigned int back_offset;
	unsigned int depth_offset;
	unsigned int w;
	unsigned int h;
	unsigned int pitch;
	unsigned int pitch_bits;
	unsigned int back_pitch;
	unsigned int depth_pitch;
	unsigned int cpp;
	unsigned int chipset;
} drm_i915_init_t;

typedef struct _drm_i915_sarea {
	drm_tex_region_t texList[I915_NR_TEX_REGIONS + 1];
	int last_upload;	/* last time texture was uploaded */
	int last_enqueue;	/* last time a buffer was enqueued */
	int last_dispatch;	/* age of the most recently dispatched buffer */
	int ctxOwner;		/* last context to upload state */
	int texAge;
	int pf_enabled;		/* is pageflipping allowed? */
	int pf_active;
	int pf_current_page;	/* which buffer is being displayed? */
	int perf_boxes;		/* performance boxes to be displayed */
} drm_i915_sarea_t;

/* Flags for perf_boxes
 */
#define I915_BOX_RING_EMPTY    0x1
#define I915_BOX_FLIP          0x2
#define I915_BOX_WAIT          0x4
#define I915_BOX_TEXTURE_LOAD  0x8
#define I915_BOX_LOST_CONTEXT  0x10

/* I915 specific ioctls
 * The device specific ioctl range is 0x40 to 0x79.
 */
#define DRM_I915_INIT		0x00
#define DRM_I915_FLUSH		0x01
#define DRM_I915_FLIP		0x02
#define DRM_I915_BATCHBUFFER	0x03
#define DRM_I915_IRQ_EMIT	0x04
#define DRM_I915_IRQ_WAIT	0x05
#define DRM_I915_GETPARAM	0x06
#define DRM_I915_SETPARAM	0x07
#define DRM_I915_ALLOC		0x08
#define DRM_I915_FREE		0x09
#define DRM_I915_INIT_HEAP	0x0a
#define DRM_I915_CMDBUFFER	0x0b

#define DRM_IOCTL_I915_INIT		DRM_IOW( DRM_COMMAND_BASE + DRM_I915_INIT, drm_i915_init_t)
#define DRM_IOCTL_I915_FLUSH		DRM_IO ( DRM_COMMAND_BASE + DRM_I915_FLUSH)
#define DRM_IOCTL_I915_FLIP		DRM_IO ( DRM_COMMAND_BASE + DRM_I915_FLIP)
#define DRM_IOCTL_I915_BATCHBUFFER	DRM_IOW( DRM_COMMAND_BASE + DRM_I915_BATCHBUFFER, drm_i915_batchbuffer_t)
#define DRM_IOCTL_I915_IRQ_EMIT         DRM_IOWR(DRM_COMMAND_BASE + DRM_I915_IRQ_EMIT, drm_i915_irq_emit_t)
#define DRM_IOCTL_I915_IRQ_WAIT         DRM_IOW( DRM_COMMAND_BASE + DRM_I915_IRQ_WAIT, drm_i915_irq_wait_t)
#define DRM_IOCTL_I915_GETPARAM         DRM_IOWR(DRM_COMMAND_BASE + DRM_I915_GETPARAM, drm_i915_getparam_t)
#define DRM_IOCTL_I915_SETPARAM         DRM_IOW( DRM_COMMAND_BASE + DRM_I915_SETPARAM, drm_i915_setparam_t)
#define DRM_IOCTL_I915_ALLOC            DRM_IOWR(DRM_COMMAND_BASE + DRM_I915_ALLOC, drm_i915_mem_alloc_t)
#define DRM_IOCTL_I915_FREE             DRM_IOW( DRM_COMMAND_BASE + DRM_I915_FREE, drm_i915_mem_free_t)
#define DRM_IOCTL_I915_INIT_HEAP        DRM_IOW( DRM_COMMAND_BASE + DRM_I915_INIT_HEAP, drm_i915_mem_init_heap_t)
#define DRM_IOCTL_I915_CMDBUFFER	DRM_IOW( DRM_COMMAND_BASE + DRM_I915_CMDBUFFER, drm_i915_cmdbuffer_t)

/* Allow drivers to submit batchbuffers directly to hardware, relying
 * on the security mechanisms provided by hardware.
 */
typedef struct _drm_i915_batchbuffer {
	int start;		/* agp offset */
	int used;		/* nr bytes in use */
	int DR1;		/* hw flags for GFX_OP_DRAWRECT_INFO */
	int DR4;		/* window origin for GFX_OP_DRAWRECT_INFO */
	int num_cliprects;	/* mulitpass with multiple cliprects? */
	drm_clip_rect_t __user *cliprects;	/* pointer to userspace cliprects */
} drm_i915_batchbuffer_t;

/* As above, but pass a pointer to userspace buffer which can be
 * validated by the kernel prior to sending to hardware.
 */
typedef struct _drm_i915_cmdbuffer {
	char __user *buf;	/* pointer to userspace command buffer */
	int sz;			/* nr bytes in buf */
	int DR1;		/* hw flags for GFX_OP_DRAWRECT_INFO */
	int DR4;		/* window origin for GFX_OP_DRAWRECT_INFO */
	int num_cliprects;	/* mulitpass with multiple cliprects? */
	drm_clip_rect_t __user *cliprects;	/* pointer to userspace cliprects */
} drm_i915_cmdbuffer_t;

/* Userspace can request & wait on irq's:
 */
typedef struct drm_i915_irq_emit {
	int __user *irq_seq;
} drm_i915_irq_emit_t;

typedef struct drm_i915_irq_wait {
	int irq_seq;
} drm_i915_irq_wait_t;

/* Ioctl to query kernel params:
 */
#define I915_PARAM_IRQ_ACTIVE            1
#define I915_PARAM_ALLOW_BATCHBUFFER     2

typedef struct drm_i915_getparam {
	int param;
	int __user *value;
} drm_i915_getparam_t;

/* Ioctl to set kernel params:
 */
#define I915_SETPARAM_USE_MI_BATCHBUFFER_START            1
#define I915_SETPARAM_TEX_LRU_LOG_GRANULARITY             2
#define I915_SETPARAM_ALLOW_BATCHBUFFER                   3

typedef struct drm_i915_setparam {
	int param;
	int value;
} drm_i915_setparam_t;

/* A memory manager for regions of shared memory:
 */
#define I915_MEM_REGION_AGP 1

typedef struct drm_i915_mem_alloc {
	int region;
	int alignment;
	int size;
	int __user *region_offset;	/* offset from start of fb or agp */
} drm_i915_mem_alloc_t;

typedef struct drm_i915_mem_free {
	int region;
	int region_offset;
} drm_i915_mem_free_t;

typedef struct drm_i915_mem_init_heap {
	int region;
	int size;
	int start;
} drm_i915_mem_init_heap_t;

#endif				/* _I915_DRM_H_ */
