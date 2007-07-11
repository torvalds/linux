/* i810_drv.h -- Private header for the Matrox g200/g400 driver -*- linux-c -*-
 * Created: Mon Dec 13 01:50:01 1999 by jhartmann@precisioninsight.com
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
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors: Rickard E. (Rik) Faith <faith@valinux.com>
 * 	    Jeff Hartmann <jhartmann@valinux.com>
 *
 */

#ifndef _I810_DRV_H_
#define _I810_DRV_H_

/* General customization:
 */

#define DRIVER_AUTHOR		"VA Linux Systems Inc."

#define DRIVER_NAME		"i810"
#define DRIVER_DESC		"Intel i810"
#define DRIVER_DATE		"20030605"

/* Interface history
 *
 * 1.1   - XFree86 4.1
 * 1.2   - XvMC interfaces
 *       - XFree86 4.2
 * 1.2.1 - Disable copying code (leave stub ioctls for backwards compatibility)
 *       - Remove requirement for interrupt (leave stubs again)
 * 1.3   - Add page flipping.
 * 1.4   - fix DRM interface
 */
#define DRIVER_MAJOR		1
#define DRIVER_MINOR		4
#define DRIVER_PATCHLEVEL	0

typedef struct drm_i810_buf_priv {
	u32 *in_use;
	int my_use_idx;
	int currently_mapped;
	void *virtual;
	void *kernel_virtual;
	drm_local_map_t map;
} drm_i810_buf_priv_t;

typedef struct _drm_i810_ring_buffer {
	int tail_mask;
	unsigned long Start;
	unsigned long End;
	unsigned long Size;
	u8 *virtual_start;
	int head;
	int tail;
	int space;
	drm_local_map_t map;
} drm_i810_ring_buffer_t;

typedef struct drm_i810_private {
	struct drm_map *sarea_map;
	struct drm_map *mmio_map;

	drm_i810_sarea_t *sarea_priv;
	drm_i810_ring_buffer_t ring;

	void *hw_status_page;
	unsigned long counter;

	dma_addr_t dma_status_page;

	struct drm_buf *mmap_buffer;

	u32 front_di1, back_di1, zi1;

	int back_offset;
	int depth_offset;
	int overlay_offset;
	int overlay_physical;
	int w, h;
	int pitch;
	int back_pitch;
	int depth_pitch;

	int do_boxes;
	int dma_used;

	int current_page;
	int page_flipping;

	wait_queue_head_t irq_queue;
	atomic_t irq_received;
	atomic_t irq_emitted;

	int front_offset;
} drm_i810_private_t;

				/* i810_dma.c */
extern int i810_driver_dma_quiescent(struct drm_device * dev);
extern void i810_driver_reclaim_buffers_locked(struct drm_device * dev,
					       struct file *filp);
extern int i810_driver_load(struct drm_device *, unsigned long flags);
extern void i810_driver_lastclose(struct drm_device * dev);
extern void i810_driver_preclose(struct drm_device * dev, DRMFILE filp);
extern void i810_driver_reclaim_buffers_locked(struct drm_device * dev,
					       struct file *filp);
extern int i810_driver_device_is_agp(struct drm_device * dev);

extern drm_ioctl_desc_t i810_ioctls[];
extern int i810_max_ioctl;

#define I810_BASE(reg)		((unsigned long) \
				dev_priv->mmio_map->handle)
#define I810_ADDR(reg)		(I810_BASE(reg) + reg)
#define I810_DEREF(reg)		*(__volatile__ int *)I810_ADDR(reg)
#define I810_READ(reg)		I810_DEREF(reg)
#define I810_WRITE(reg,val) 	do { I810_DEREF(reg) = val; } while (0)
#define I810_DEREF16(reg)	*(__volatile__ u16 *)I810_ADDR(reg)
#define I810_READ16(reg)	I810_DEREF16(reg)
#define I810_WRITE16(reg,val)	do { I810_DEREF16(reg) = val; } while (0)

#define I810_VERBOSE 0
#define RING_LOCALS	unsigned int outring, ringmask; \
                        volatile char *virt;

#define BEGIN_LP_RING(n) do {						\
	if (I810_VERBOSE)                                               \
           DRM_DEBUG("BEGIN_LP_RING(%d) in %s\n", n, __FUNCTION__);	\
	if (dev_priv->ring.space < n*4)					\
		i810_wait_ring(dev, n*4);				\
	dev_priv->ring.space -= n*4;					\
	outring = dev_priv->ring.tail;					\
	ringmask = dev_priv->ring.tail_mask;				\
	virt = dev_priv->ring.virtual_start;				\
} while (0)

#define ADVANCE_LP_RING() do {				        \
	if (I810_VERBOSE) DRM_DEBUG("ADVANCE_LP_RING\n");    	\
	dev_priv->ring.tail = outring;		        	\
	I810_WRITE(LP_RING + RING_TAIL, outring);	        \
} while(0)

#define OUT_RING(n) do {  				                \
	if (I810_VERBOSE) DRM_DEBUG("   OUT_RING %x\n", (int)(n));	\
	*(volatile unsigned int *)(virt + outring) = n;	                \
	outring += 4;					                \
	outring &= ringmask;			                        \
} while (0)

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

#define I810REG_HWSTAM		0x02098
#define I810REG_INT_IDENTITY_R	0x020a4
#define I810REG_INT_MASK_R 	0x020a8
#define I810REG_INT_ENABLE_R	0x020a0

#define LP_RING     		0x2030
#define HP_RING     		0x2040
#define RING_TAIL      		0x00
#define TAIL_ADDR		0x000FFFF8
#define RING_HEAD      		0x04
#define HEAD_WRAP_COUNT     	0xFFE00000
#define HEAD_WRAP_ONE       	0x00200000
#define HEAD_ADDR           	0x001FFFFC
#define RING_START     		0x08
#define START_ADDR          	0x00FFFFF8
#define RING_LEN       		0x0C
#define RING_NR_PAGES       	0x000FF000
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

#define GFX_OP_COLOR_FACTOR      ((0x3<<29)|(0x1d<<24)|(0x1<<16)|0x0)
#define GFX_OP_STIPPLE           ((0x3<<29)|(0x1d<<24)|(0x83<<16))
#define GFX_OP_MAP_INFO          ((0x3<<29)|(0x1d<<24)|0x2)
#define GFX_OP_DESTBUFFER_VARS   ((0x3<<29)|(0x1d<<24)|(0x85<<16)|0x0)
#define GFX_OP_DRAWRECT_INFO     ((0x3<<29)|(0x1d<<24)|(0x80<<16)|(0x3))
#define GFX_OP_PRIMITIVE         ((0x3<<29)|(0x1f<<24))

#define CMD_OP_Z_BUFFER_INFO     ((0x0<<29)|(0x16<<23))
#define CMD_OP_DESTBUFFER_INFO   ((0x0<<29)|(0x15<<23))
#define CMD_OP_FRONTBUFFER_INFO  ((0x0<<29)|(0x14<<23))
#define CMD_OP_WAIT_FOR_EVENT    ((0x0<<29)|(0x03<<23))

#define BR00_BITBLT_CLIENT   0x40000000
#define BR00_OP_COLOR_BLT    0x10000000
#define BR00_OP_SRC_COPY_BLT 0x10C00000
#define BR13_SOLID_PATTERN   0x80000000

#define WAIT_FOR_PLANE_A_SCANLINES (1<<1)
#define WAIT_FOR_PLANE_A_FLIP      (1<<2)
#define WAIT_FOR_VBLANK (1<<3)

#endif
