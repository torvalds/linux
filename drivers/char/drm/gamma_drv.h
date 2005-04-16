/* gamma_drv.h -- Private header for 3dlabs GMX 2000 driver -*- linux-c -*-
 * Created: Mon Jan  4 10:05:05 1999 by faith@precisioninsight.com
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
 * Authors:
 *    Rickard E. (Rik) Faith <faith@valinux.com>
 *
 */

#ifndef _GAMMA_DRV_H_
#define _GAMMA_DRV_H_

typedef struct drm_gamma_private {
	drm_gamma_sarea_t *sarea_priv;
	drm_map_t *sarea;
	drm_map_t *mmio0;
	drm_map_t *mmio1;
	drm_map_t *mmio2;
	drm_map_t *mmio3;
	int num_rast;
} drm_gamma_private_t;

				/* gamma_dma.c */
extern int gamma_dma_init( struct inode *inode, struct file *filp,
			 unsigned int cmd, unsigned long arg );
extern int gamma_dma_copy( struct inode *inode, struct file *filp,
			 unsigned int cmd, unsigned long arg );

extern int gamma_do_cleanup_dma( drm_device_t *dev );
extern void gamma_dma_ready(drm_device_t *dev);
extern void gamma_dma_quiescent_single(drm_device_t *dev);
extern void gamma_dma_quiescent_dual(drm_device_t *dev);

				/* gamma_dma.c */
extern int  gamma_dma_schedule(drm_device_t *dev, int locked);
extern int  gamma_dma(struct inode *inode, struct file *filp,
		      unsigned int cmd, unsigned long arg);
extern int  gamma_find_devices(void);
extern int  gamma_found(void);

/* Gamma-specific code pulled from drm_fops.h:
 */
extern int	     DRM(finish)(struct inode *inode, struct file *filp,
				 unsigned int cmd, unsigned long arg);
extern int	     DRM(flush_unblock)(drm_device_t *dev, int context,
					drm_lock_flags_t flags);
extern int	     DRM(flush_block_and_flush)(drm_device_t *dev, int context,
						drm_lock_flags_t flags);

/* Gamma-specific code pulled from drm_dma.h:
 */
extern void	     DRM(clear_next_buffer)(drm_device_t *dev);
extern int	     DRM(select_queue)(drm_device_t *dev,
				       void (*wrapper)(unsigned long));
extern int	     DRM(dma_enqueue)(struct file *filp, drm_dma_t *dma);
extern int	     DRM(dma_get_buffers)(struct file *filp, drm_dma_t *dma);


/* Gamma-specific code pulled from drm_lists.h (now renamed gamma_lists.h):
 */
extern int	     DRM(waitlist_create)(drm_waitlist_t *bl, int count);
extern int	     DRM(waitlist_destroy)(drm_waitlist_t *bl);
extern int	     DRM(waitlist_put)(drm_waitlist_t *bl, drm_buf_t *buf);
extern drm_buf_t     *DRM(waitlist_get)(drm_waitlist_t *bl);
extern int	     DRM(freelist_create)(drm_freelist_t *bl, int count);
extern int	     DRM(freelist_destroy)(drm_freelist_t *bl);
extern int	     DRM(freelist_put)(drm_device_t *dev, drm_freelist_t *bl,
				       drm_buf_t *buf);
extern drm_buf_t     *DRM(freelist_get)(drm_freelist_t *bl, int block);

/* externs for gamma changes to the ops */
extern struct file_operations DRM(fops);
extern unsigned int gamma_fops_poll(struct file *filp, struct poll_table_struct *wait);
extern ssize_t gamma_fops_read(struct file *filp, char __user *buf, size_t count, loff_t *off);


#define GLINT_DRI_BUF_COUNT 256

#define GAMMA_OFF(reg)						   \
	((reg < 0x1000)						   \
	 ? reg							   \
	 : ((reg < 0x10000)					   \
	    ? (reg - 0x1000)					   \
	    : ((reg < 0x11000)					   \
	       ? (reg - 0x10000)				   \
	       : (reg - 0x11000))))

#define GAMMA_BASE(reg)	 ((unsigned long)				     \
			  ((reg < 0x1000)    ? dev_priv->mmio0->handle :     \
			   ((reg < 0x10000)  ? dev_priv->mmio1->handle :     \
			    ((reg < 0x11000) ? dev_priv->mmio2->handle :     \
					       dev_priv->mmio3->handle))))
#define GAMMA_ADDR(reg)	 (GAMMA_BASE(reg) + GAMMA_OFF(reg))
#define GAMMA_DEREF(reg) *(__volatile__ int *)GAMMA_ADDR(reg)
#define GAMMA_READ(reg)	 GAMMA_DEREF(reg)
#define GAMMA_WRITE(reg,val) do { GAMMA_DEREF(reg) = val; } while (0)

#define GAMMA_BROADCASTMASK    0x9378
#define GAMMA_COMMANDINTENABLE 0x0c48
#define GAMMA_DMAADDRESS       0x0028
#define GAMMA_DMACOUNT	       0x0030
#define GAMMA_FILTERMODE       0x8c00
#define GAMMA_GCOMMANDINTFLAGS 0x0c50
#define GAMMA_GCOMMANDMODE     0x0c40
#define		GAMMA_QUEUED_DMA_MODE		1<<1
#define GAMMA_GCOMMANDSTATUS   0x0c60
#define GAMMA_GDELAYTIMER      0x0c38
#define GAMMA_GDMACONTROL      0x0060
#define 	GAMMA_USE_AGP			1<<1
#define GAMMA_GINTENABLE       0x0808
#define GAMMA_GINTFLAGS	       0x0810
#define GAMMA_INFIFOSPACE      0x0018
#define GAMMA_OUTFIFOWORDS     0x0020
#define GAMMA_OUTPUTFIFO       0x2000
#define GAMMA_SYNC	       0x8c40
#define GAMMA_SYNC_TAG	       0x0188
#define GAMMA_PAGETABLEADDR    0x0C00
#define GAMMA_PAGETABLELENGTH  0x0C08

#define GAMMA_PASSTHROUGH	0x1FE
#define GAMMA_DMAADDRTAG	0x530
#define GAMMA_DMACOUNTTAG	0x531
#define GAMMA_COMMANDINTTAG	0x532

#endif
