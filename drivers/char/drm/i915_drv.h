/* i915_drv.h -- Private header for the I915 driver -*- linux-c -*-
 */
/*
 *
 * Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef _I915_DRV_H_
#define _I915_DRV_H_

/* General customization:
 */

#define DRIVER_AUTHOR		"Tungsten Graphics, Inc."

#define DRIVER_NAME		"i915"
#define DRIVER_DESC		"Intel Graphics"
#define DRIVER_DATE		"20060119"

/* Interface history:
 *
 * 1.1: Original.
 * 1.2: Add Power Management
 * 1.3: Add vblank support
 * 1.4: Fix cmdbuffer path, add heap destroy
 */
#define DRIVER_MAJOR		1
#define DRIVER_MINOR		4
#define DRIVER_PATCHLEVEL	0

typedef struct _drm_i915_ring_buffer {
	int tail_mask;
	unsigned long Start;
	unsigned long End;
	unsigned long Size;
	u8 *virtual_start;
	int head;
	int tail;
	int space;
	drm_local_map_t map;
} drm_i915_ring_buffer_t;

struct mem_block {
	struct mem_block *next;
	struct mem_block *prev;
	int start;
	int size;
	DRMFILE filp;		/* 0: free, -1: heap, other: real files */
};

typedef struct drm_i915_private {
	drm_local_map_t *sarea;
	drm_local_map_t *mmio_map;

	drm_i915_sarea_t *sarea_priv;
	drm_i915_ring_buffer_t ring;

	drm_dma_handle_t *status_page_dmah;
	void *hw_status_page;
	dma_addr_t dma_status_page;
	unsigned long counter;

	int back_offset;
	int front_offset;
	int current_page;
	int page_flipping;
	int use_mi_batchbuffer_start;

	wait_queue_head_t irq_queue;
	atomic_t irq_received;
	atomic_t irq_emitted;

	int tex_lru_log_granularity;
	int allow_batchbuffer;
	struct mem_block *agp_heap;
	unsigned int sr01, adpa, ppcr, dvob, dvoc, lvds;
} drm_i915_private_t;

extern drm_ioctl_desc_t i915_ioctls[];
extern int i915_max_ioctl;

				/* i915_dma.c */
extern void i915_kernel_lost_context(drm_device_t * dev);
extern int i915_driver_load(struct drm_device *, unsigned long flags);
extern void i915_driver_lastclose(drm_device_t * dev);
extern void i915_driver_preclose(drm_device_t * dev, DRMFILE filp);
extern int i915_driver_device_is_agp(drm_device_t * dev);
extern long i915_compat_ioctl(struct file *filp, unsigned int cmd,
			      unsigned long arg);

/* i915_irq.c */
extern int i915_irq_emit(DRM_IOCTL_ARGS);
extern int i915_irq_wait(DRM_IOCTL_ARGS);

extern int i915_driver_vblank_wait(drm_device_t *dev, unsigned int *sequence);
extern irqreturn_t i915_driver_irq_handler(DRM_IRQ_ARGS);
extern void i915_driver_irq_preinstall(drm_device_t * dev);
extern void i915_driver_irq_postinstall(drm_device_t * dev);
extern void i915_driver_irq_uninstall(drm_device_t * dev);

/* i915_mem.c */
extern int i915_mem_alloc(DRM_IOCTL_ARGS);
extern int i915_mem_free(DRM_IOCTL_ARGS);
extern int i915_mem_init_heap(DRM_IOCTL_ARGS);
extern int i915_mem_destroy_heap(DRM_IOCTL_ARGS);
extern void i915_mem_takedown(struct mem_block **heap);
extern void i915_mem_release(drm_device_t * dev,
			     DRMFILE filp, struct mem_block *heap);

#define I915_READ(reg)          DRM_READ32(dev_priv->mmio_map, (reg))
#define I915_WRITE(reg,val)     DRM_WRITE32(dev_priv->mmio_map, (reg), (val))
#define I915_READ16(reg) 	DRM_READ16(dev_priv->mmio_map, (reg))
#define I915_WRITE16(reg,val)	DRM_WRITE16(dev_priv->mmio_map, (reg), (val))

#define I915_VERBOSE 0

#define RING_LOCALS	unsigned int outring, ringmask, outcount; \
                        volatile char *virt;

#define BEGIN_LP_RING(n) do {				\
	if (I915_VERBOSE)				\
		DRM_DEBUG("BEGIN_LP_RING(%d) in %s\n",	\
			  n, __FUNCTION__);		\
	if (dev_priv->ring.space < n*4)			\
		i915_wait_ring(dev, n*4, __FUNCTION__);		\
	outcount = 0;					\
	outring = dev_priv->ring.tail;			\
	ringmask = dev_priv->ring.tail_mask;		\
	virt = dev_priv->ring.virtual_start;		\
} while (0)

#define OUT_RING(n) do {					\
	if (I915_VERBOSE) DRM_DEBUG("   OUT_RING %x\n", (int)(n));	\
	*(volatile unsigned int *)(virt + outring) = n;		\
        outcount++;						\
	outring += 4;						\
	outring &= ringmask;					\
} while (0)

#define ADVANCE_LP_RING() do {						\
	if (I915_VERBOSE) DRM_DEBUG("ADVANCE_LP_RING %x\n", outring);	\
	dev_priv->ring.tail = outring;					\
	dev_priv->ring.space -= outcount * 4;				\
	I915_WRITE(LP_RING + RING_TAIL, outring);			\
} while(0)

extern int i915_wait_ring(drm_device_t * dev, int n, const char *caller);

#define GFX_OP_USER_INTERRUPT 		((0<<29)|(2<<23))
#define GFX_OP_BREAKPOINT_INTERRUPT	((0<<29)|(1<<23))
#define CMD_REPORT_HEAD			(7<<23)
#define CMD_STORE_DWORD_IDX		((0x21<<23) | 0x1)
#define CMD_OP_BATCH_BUFFER  ((0x0<<29)|(0x30<<23)|0x1)

#define INST_PARSER_CLIENT   0x00000000
#define INST_OP_FLUSH        0x02000000
#define INST_FLUSH_MAP_CACHE 0x00000001

#define BB1_START_ADDR_MASK   (~0x7)
#define BB1_PROTECTED         (1<<0)
#define BB1_UNPROTECTED       (0<<0)
#define BB2_END_ADDR_MASK     (~0x7)

#define I915REG_HWSTAM		0x02098
#define I915REG_INT_IDENTITY_R	0x020a4
#define I915REG_INT_MASK_R 	0x020a8
#define I915REG_INT_ENABLE_R	0x020a0

#define SRX_INDEX		0x3c4
#define SRX_DATA		0x3c5
#define SR01			1
#define SR01_SCREEN_OFF 	(1<<5)

#define PPCR			0x61204
#define PPCR_ON			(1<<0)

#define DVOB			0x61140
#define DVOB_ON			(1<<31)
#define DVOC			0x61160
#define DVOC_ON			(1<<31)
#define LVDS			0x61180
#define LVDS_ON			(1<<31)

#define ADPA			0x61100
#define ADPA_DPMS_MASK		(~(3<<10))
#define ADPA_DPMS_ON		(0<<10)
#define ADPA_DPMS_SUSPEND	(1<<10)
#define ADPA_DPMS_STANDBY	(2<<10)
#define ADPA_DPMS_OFF		(3<<10)

#define NOPID                   0x2094
#define LP_RING     		0x2030
#define HP_RING     		0x2040
#define RING_TAIL      		0x00
#define TAIL_ADDR		0x001FFFF8
#define RING_HEAD      		0x04
#define HEAD_WRAP_COUNT     	0xFFE00000
#define HEAD_WRAP_ONE       	0x00200000
#define HEAD_ADDR           	0x001FFFFC
#define RING_START     		0x08
#define START_ADDR          	0x0xFFFFF000
#define RING_LEN       		0x0C
#define RING_NR_PAGES       	0x001FF000
#define RING_REPORT_MASK    	0x00000006
#define RING_REPORT_64K     	0x00000002
#define RING_REPORT_128K    	0x00000004
#define RING_NO_REPORT      	0x00000000
#define RING_VALID_MASK     	0x00000001
#define RING_VALID          	0x00000001
#define RING_INVALID        	0x00000000

#define GFX_OP_SCISSOR         ((0x3<<29)|(0x1c<<24)|(0x10<<19))
#define SC_UPDATE_SCISSOR       (0x1<<1)
#define SC_ENABLE_MASK          (0x1<<0)
#define SC_ENABLE               (0x1<<0)

#define GFX_OP_SCISSOR_INFO    ((0x3<<29)|(0x1d<<24)|(0x81<<16)|(0x1))
#define SCI_YMIN_MASK      (0xffff<<16)
#define SCI_XMIN_MASK      (0xffff<<0)
#define SCI_YMAX_MASK      (0xffff<<16)
#define SCI_XMAX_MASK      (0xffff<<0)

#define GFX_OP_SCISSOR_ENABLE	 ((0x3<<29)|(0x1c<<24)|(0x10<<19))
#define GFX_OP_SCISSOR_RECT	 ((0x3<<29)|(0x1d<<24)|(0x81<<16)|1)
#define GFX_OP_COLOR_FACTOR      ((0x3<<29)|(0x1d<<24)|(0x1<<16)|0x0)
#define GFX_OP_STIPPLE           ((0x3<<29)|(0x1d<<24)|(0x83<<16))
#define GFX_OP_MAP_INFO          ((0x3<<29)|(0x1d<<24)|0x4)
#define GFX_OP_DESTBUFFER_VARS   ((0x3<<29)|(0x1d<<24)|(0x85<<16)|0x0)
#define GFX_OP_DRAWRECT_INFO     ((0x3<<29)|(0x1d<<24)|(0x80<<16)|(0x3))

#define MI_BATCH_BUFFER 	((0x30<<23)|1)
#define MI_BATCH_BUFFER_START 	(0x31<<23)
#define MI_BATCH_BUFFER_END 	(0xA<<23)
#define MI_BATCH_NON_SECURE	(1)

#define MI_WAIT_FOR_EVENT       ((0x3<<23))
#define MI_WAIT_FOR_PLANE_A_FLIP      (1<<2)
#define MI_WAIT_FOR_PLANE_A_SCANLINES (1<<1)

#define MI_LOAD_SCAN_LINES_INCL  ((0x12<<23))

#define CMD_OP_DISPLAYBUFFER_INFO ((0x0<<29)|(0x14<<23)|2)
#define ASYNC_FLIP                (1<<22)

#define CMD_OP_DESTBUFFER_INFO	 ((0x3<<29)|(0x1d<<24)|(0x8e<<16)|1)

#define READ_BREADCRUMB(dev_priv) (((u32 *)(dev_priv->hw_status_page))[5])

#endif
