/* i830_drv.h -- Private header for the I830 driver -*- linux-c -*-
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

#ifndef _I830_DRV_H_
#define _I830_DRV_H_

/* General customization:
 */

#define DRIVER_AUTHOR		"VA Linux Systems Inc."

#define DRIVER_NAME		"i830"
#define DRIVER_DESC		"Intel 830M"
#define DRIVER_DATE		"20021108"

/* Interface history:
 *
 * 1.1: Original.
 * 1.2: ?
 * 1.3: New irq emit/wait ioctls.
 *      New pageflip ioctl.
 *      New getparam ioctl.
 *      State for texunits 3&4 in sarea.
 *      New (alternative) layout for texture state.
 */
#define DRIVER_MAJOR		1
#define DRIVER_MINOR		3
#define DRIVER_PATCHLEVEL	2

/* Driver will work either way: IRQ's save cpu time when waiting for
 * the card, but are subject to subtle interactions between bios,
 * hardware and the driver.
 */
/* XXX: Add vblank support? */
#define USE_IRQS 0

typedef struct drm_i830_buf_priv {
	u32 *in_use;
	int my_use_idx;
	int currently_mapped;
	void __user *virtual;
	void *kernel_virtual;
} drm_i830_buf_priv_t;

typedef struct _drm_i830_ring_buffer {
	int tail_mask;
	unsigned long Start;
	unsigned long End;
	unsigned long Size;
	u8 *virtual_start;
	int head;
	int tail;
	int space;
} drm_i830_ring_buffer_t;

typedef struct drm_i830_private {
	drm_map_t *sarea_map;
	drm_map_t *mmio_map;

	drm_i830_sarea_t *sarea_priv;
	drm_i830_ring_buffer_t ring;

	void *hw_status_page;
	unsigned long counter;

	dma_addr_t dma_status_page;

	drm_buf_t *mmap_buffer;

	u32 front_di1, back_di1, zi1;

	int back_offset;
	int depth_offset;
	int front_offset;
	int w, h;
	int pitch;
	int back_pitch;
	int depth_pitch;
	unsigned int cpp;

	int do_boxes;
	int dma_used;

	int current_page;
	int page_flipping;

	wait_queue_head_t irq_queue;
	atomic_t irq_received;
	atomic_t irq_emitted;

	int use_mi_batchbuffer_start;

} drm_i830_private_t;

extern drm_ioctl_desc_t i830_ioctls[];
extern int i830_max_ioctl;

/* i830_irq.c */
extern int i830_irq_emit(struct inode *inode, struct file *filp,
			 unsigned int cmd, unsigned long arg);
extern int i830_irq_wait(struct inode *inode, struct file *filp,
			 unsigned int cmd, unsigned long arg);

extern irqreturn_t i830_driver_irq_handler(DRM_IRQ_ARGS);
extern void i830_driver_irq_preinstall(drm_device_t * dev);
extern void i830_driver_irq_postinstall(drm_device_t * dev);
extern void i830_driver_irq_uninstall(drm_device_t * dev);
extern int i830_driver_load(struct drm_device *, unsigned long flags);
extern void i830_driver_preclose(drm_device_t * dev, DRMFILE filp);
extern void i830_driver_lastclose(drm_device_t * dev);
extern void i830_driver_reclaim_buffers_locked(drm_device_t * dev,
					       struct file *filp);
extern int i830_driver_dma_quiescent(drm_device_t * dev);
extern int i830_driver_device_is_agp(drm_device_t * dev);

#define I830_READ(reg)          DRM_READ32(dev_priv->mmio_map, reg)
#define I830_WRITE(reg,val)     DRM_WRITE32(dev_priv->mmio_map, reg, val)
#define I830_READ16(reg)        DRM_READ16(dev_priv->mmio_map, reg)
#define I830_WRITE16(reg,val)   DRM_WRITE16(dev_priv->mmio_map, reg, val)

#define I830_VERBOSE 0

#define RING_LOCALS	unsigned int outring, ringmask, outcount; \
                        volatile char *virt;

#define BEGIN_LP_RING(n) do {				\
	if (I830_VERBOSE)				\
		printk("BEGIN_LP_RING(%d) in %s\n",	\
			  n, __FUNCTION__);		\
	if (dev_priv->ring.space < n*4)			\
		i830_wait_ring(dev, n*4, __FUNCTION__);		\
	outcount = 0;					\
	outring = dev_priv->ring.tail;			\
	ringmask = dev_priv->ring.tail_mask;		\
	virt = dev_priv->ring.virtual_start;		\
} while (0)

#define OUT_RING(n) do {					\
	if (I830_VERBOSE) printk("   OUT_RING %x\n", (int)(n));	\
	*(volatile unsigned int *)(virt + outring) = n;		\
        outcount++;						\
	outring += 4;						\
	outring &= ringmask;					\
} while (0)

#define ADVANCE_LP_RING() do {						\
	if (I830_VERBOSE) printk("ADVANCE_LP_RING %x\n", outring);	\
	dev_priv->ring.tail = outring;					\
	dev_priv->ring.space -= outcount * 4;				\
	I830_WRITE(LP_RING + RING_TAIL, outring);			\
} while(0)

extern int i830_wait_ring(drm_device_t * dev, int n, const char *caller);

#define GFX_OP_USER_INTERRUPT 		((0<<29)|(2<<23))
#define GFX_OP_BREAKPOINT_INTERRUPT	((0<<29)|(1<<23))
#define CMD_REPORT_HEAD			(7<<23)
#define CMD_STORE_DWORD_IDX		((0x21<<23) | 0x1)
#define CMD_OP_BATCH_BUFFER  ((0x0<<29)|(0x30<<23)|0x1)

#define STATE3D_LOAD_STATE_IMMEDIATE_2      ((0x3<<29)|(0x1d<<24)|(0x03<<16))
#define LOAD_TEXTURE_MAP0                   (1<<11)

#define INST_PARSER_CLIENT   0x00000000
#define INST_OP_FLUSH        0x02000000
#define INST_FLUSH_MAP_CACHE 0x00000001

#define BB1_START_ADDR_MASK   (~0x7)
#define BB1_PROTECTED         (1<<0)
#define BB1_UNPROTECTED       (0<<0)
#define BB2_END_ADDR_MASK     (~0x7)

#define I830REG_HWSTAM		0x02098
#define I830REG_INT_IDENTITY_R	0x020a4
#define I830REG_INT_MASK_R 	0x020a8
#define I830REG_INT_ENABLE_R	0x020a0

#define I830_IRQ_RESERVED ((1<<13)|(3<<2))

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
#define GFX_OP_PRIMITIVE         ((0x3<<29)|(0x1f<<24))

#define CMD_OP_DESTBUFFER_INFO	 ((0x3<<29)|(0x1d<<24)|(0x8e<<16)|1)

#define CMD_OP_DISPLAYBUFFER_INFO ((0x0<<29)|(0x14<<23)|2)
#define ASYNC_FLIP                (1<<22)

#define CMD_3D                          (0x3<<29)
#define STATE3D_CONST_BLEND_COLOR_CMD   (CMD_3D|(0x1d<<24)|(0x88<<16))
#define STATE3D_MAP_COORD_SETBIND_CMD   (CMD_3D|(0x1d<<24)|(0x02<<16))

#define BR00_BITBLT_CLIENT   0x40000000
#define BR00_OP_COLOR_BLT    0x10000000
#define BR00_OP_SRC_COPY_BLT 0x10C00000
#define BR13_SOLID_PATTERN   0x80000000

#define BUF_3D_ID_COLOR_BACK    (0x3<<24)
#define BUF_3D_ID_DEPTH         (0x7<<24)
#define BUF_3D_USE_FENCE        (1<<23)
#define BUF_3D_PITCH(x)         (((x)/4)<<2)

#define CMD_OP_MAP_PALETTE_LOAD	((3<<29)|(0x1d<<24)|(0x82<<16)|255)
#define MAP_PALETTE_NUM(x)	((x<<8) & (1<<8))
#define MAP_PALETTE_BOTH	(1<<11)

#define XY_COLOR_BLT_CMD		((2<<29)|(0x50<<22)|0x4)
#define XY_COLOR_BLT_WRITE_ALPHA	(1<<21)
#define XY_COLOR_BLT_WRITE_RGB		(1<<20)

#define XY_SRC_COPY_BLT_CMD             ((2<<29)|(0x53<<22)|6)
#define XY_SRC_COPY_BLT_WRITE_ALPHA     (1<<21)
#define XY_SRC_COPY_BLT_WRITE_RGB       (1<<20)

#define MI_BATCH_BUFFER 	((0x30<<23)|1)
#define MI_BATCH_BUFFER_START 	(0x31<<23)
#define MI_BATCH_BUFFER_END 	(0xA<<23)
#define MI_BATCH_NON_SECURE	(1)

#define MI_WAIT_FOR_EVENT       ((0x3<<23))
#define MI_WAIT_FOR_PLANE_A_FLIP      (1<<2)
#define MI_WAIT_FOR_PLANE_A_SCANLINES (1<<1)

#define MI_LOAD_SCAN_LINES_INCL  ((0x12<<23))

#endif
