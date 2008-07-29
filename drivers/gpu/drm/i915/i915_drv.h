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

#include "i915_reg.h"

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
 * 1.5: Add vblank pipe configuration
 * 1.6: - New ioctl for scheduling buffer swaps on vertical blank
 *      - Support vertical blank on secondary display pipe
 */
#define DRIVER_MAJOR		1
#define DRIVER_MINOR		6
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
	struct drm_file *file_priv; /* NULL: free, -1: heap, other: real files */
};

typedef struct _drm_i915_vbl_swap {
	struct list_head head;
	drm_drawable_t drw_id;
	unsigned int pipe;
	unsigned int sequence;
} drm_i915_vbl_swap_t;

typedef struct drm_i915_private {
	drm_local_map_t *sarea;
	drm_local_map_t *mmio_map;

	drm_i915_sarea_t *sarea_priv;
	drm_i915_ring_buffer_t ring;

	drm_dma_handle_t *status_page_dmah;
	void *hw_status_page;
	dma_addr_t dma_status_page;
	unsigned long counter;
	unsigned int status_gfx_addr;
	drm_local_map_t hws_map;

	unsigned int cpp;
	int back_offset;
	int front_offset;
	int current_page;
	int page_flipping;

	wait_queue_head_t irq_queue;
	atomic_t irq_received;
	atomic_t irq_emitted;
	/** Protects user_irq_refcount and irq_mask_reg */
	spinlock_t user_irq_lock;
	/** Refcount for i915_user_irq_get() versus i915_user_irq_put(). */
	int user_irq_refcount;
	/** Cached value of IMR to avoid reads in updating the bitfield */
	u32 irq_mask_reg;

	int tex_lru_log_granularity;
	int allow_batchbuffer;
	struct mem_block *agp_heap;
	unsigned int sr01, adpa, ppcr, dvob, dvoc, lvds;
	int vblank_pipe;

	spinlock_t swaps_lock;
	drm_i915_vbl_swap_t vbl_swaps;
	unsigned int swaps_pending;

	/* Register state */
	u8 saveLBB;
	u32 saveDSPACNTR;
	u32 saveDSPBCNTR;
	u32 saveDSPARB;
	u32 savePIPEACONF;
	u32 savePIPEBCONF;
	u32 savePIPEASRC;
	u32 savePIPEBSRC;
	u32 saveFPA0;
	u32 saveFPA1;
	u32 saveDPLL_A;
	u32 saveDPLL_A_MD;
	u32 saveHTOTAL_A;
	u32 saveHBLANK_A;
	u32 saveHSYNC_A;
	u32 saveVTOTAL_A;
	u32 saveVBLANK_A;
	u32 saveVSYNC_A;
	u32 saveBCLRPAT_A;
	u32 savePIPEASTAT;
	u32 saveDSPASTRIDE;
	u32 saveDSPASIZE;
	u32 saveDSPAPOS;
	u32 saveDSPAADDR;
	u32 saveDSPASURF;
	u32 saveDSPATILEOFF;
	u32 savePFIT_PGM_RATIOS;
	u32 saveBLC_PWM_CTL;
	u32 saveBLC_PWM_CTL2;
	u32 saveFPB0;
	u32 saveFPB1;
	u32 saveDPLL_B;
	u32 saveDPLL_B_MD;
	u32 saveHTOTAL_B;
	u32 saveHBLANK_B;
	u32 saveHSYNC_B;
	u32 saveVTOTAL_B;
	u32 saveVBLANK_B;
	u32 saveVSYNC_B;
	u32 saveBCLRPAT_B;
	u32 savePIPEBSTAT;
	u32 saveDSPBSTRIDE;
	u32 saveDSPBSIZE;
	u32 saveDSPBPOS;
	u32 saveDSPBADDR;
	u32 saveDSPBSURF;
	u32 saveDSPBTILEOFF;
	u32 saveVGA0;
	u32 saveVGA1;
	u32 saveVGA_PD;
	u32 saveVGACNTRL;
	u32 saveADPA;
	u32 saveLVDS;
	u32 savePP_ON_DELAYS;
	u32 savePP_OFF_DELAYS;
	u32 saveDVOA;
	u32 saveDVOB;
	u32 saveDVOC;
	u32 savePP_ON;
	u32 savePP_OFF;
	u32 savePP_CONTROL;
	u32 savePP_DIVISOR;
	u32 savePFIT_CONTROL;
	u32 save_palette_a[256];
	u32 save_palette_b[256];
	u32 saveFBC_CFB_BASE;
	u32 saveFBC_LL_BASE;
	u32 saveFBC_CONTROL;
	u32 saveFBC_CONTROL2;
	u32 saveIER;
	u32 saveIIR;
	u32 saveIMR;
	u32 saveCACHE_MODE_0;
	u32 saveD_STATE;
	u32 saveCG_2D_DIS;
	u32 saveMI_ARB_STATE;
	u32 saveSWF0[16];
	u32 saveSWF1[16];
	u32 saveSWF2[3];
	u8 saveMSR;
	u8 saveSR[8];
	u8 saveGR[25];
	u8 saveAR_INDEX;
	u8 saveAR[21];
	u8 saveDACMASK;
	u8 saveDACDATA[256*3]; /* 256 3-byte colors */
	u8 saveCR[37];
} drm_i915_private_t;

extern struct drm_ioctl_desc i915_ioctls[];
extern int i915_max_ioctl;

				/* i915_dma.c */
extern void i915_kernel_lost_context(struct drm_device * dev);
extern int i915_driver_load(struct drm_device *, unsigned long flags);
extern int i915_driver_unload(struct drm_device *);
extern void i915_driver_lastclose(struct drm_device * dev);
extern void i915_driver_preclose(struct drm_device *dev,
				 struct drm_file *file_priv);
extern int i915_driver_device_is_agp(struct drm_device * dev);
extern long i915_compat_ioctl(struct file *filp, unsigned int cmd,
			      unsigned long arg);

/* i915_irq.c */
extern int i915_irq_emit(struct drm_device *dev, void *data,
			 struct drm_file *file_priv);
extern int i915_irq_wait(struct drm_device *dev, void *data,
			 struct drm_file *file_priv);

extern int i915_driver_vblank_wait(struct drm_device *dev, unsigned int *sequence);
extern int i915_driver_vblank_wait2(struct drm_device *dev, unsigned int *sequence);
extern irqreturn_t i915_driver_irq_handler(DRM_IRQ_ARGS);
extern void i915_driver_irq_preinstall(struct drm_device * dev);
extern void i915_driver_irq_postinstall(struct drm_device * dev);
extern void i915_driver_irq_uninstall(struct drm_device * dev);
extern int i915_vblank_pipe_set(struct drm_device *dev, void *data,
				struct drm_file *file_priv);
extern int i915_vblank_pipe_get(struct drm_device *dev, void *data,
				struct drm_file *file_priv);
extern int i915_vblank_swap(struct drm_device *dev, void *data,
			    struct drm_file *file_priv);

/* i915_mem.c */
extern int i915_mem_alloc(struct drm_device *dev, void *data,
			  struct drm_file *file_priv);
extern int i915_mem_free(struct drm_device *dev, void *data,
			 struct drm_file *file_priv);
extern int i915_mem_init_heap(struct drm_device *dev, void *data,
			      struct drm_file *file_priv);
extern int i915_mem_destroy_heap(struct drm_device *dev, void *data,
				 struct drm_file *file_priv);
extern void i915_mem_takedown(struct mem_block **heap);
extern void i915_mem_release(struct drm_device * dev,
			     struct drm_file *file_priv, struct mem_block *heap);

#define I915_READ(reg)          DRM_READ32(dev_priv->mmio_map, (reg))
#define I915_WRITE(reg,val)     DRM_WRITE32(dev_priv->mmio_map, (reg), (val))
#define I915_READ16(reg)	DRM_READ16(dev_priv->mmio_map, (reg))
#define I915_WRITE16(reg,val)	DRM_WRITE16(dev_priv->mmio_map, (reg), (val))

#define I915_VERBOSE 0

#define RING_LOCALS	unsigned int outring, ringmask, outcount; \
                        volatile char *virt;

#define BEGIN_LP_RING(n) do {				\
	if (I915_VERBOSE)				\
		DRM_DEBUG("BEGIN_LP_RING(%d)\n", (n));	\
	if (dev_priv->ring.space < (n)*4)		\
		i915_wait_ring(dev, (n)*4, __func__);		\
	outcount = 0;					\
	outring = dev_priv->ring.tail;			\
	ringmask = dev_priv->ring.tail_mask;		\
	virt = dev_priv->ring.virtual_start;		\
} while (0)

#define OUT_RING(n) do {					\
	if (I915_VERBOSE) DRM_DEBUG("   OUT_RING %x\n", (int)(n));	\
	*(volatile unsigned int *)(virt + outring) = (n);	\
        outcount++;						\
	outring += 4;						\
	outring &= ringmask;					\
} while (0)

#define ADVANCE_LP_RING() do {						\
	if (I915_VERBOSE) DRM_DEBUG("ADVANCE_LP_RING %x\n", outring);	\
	dev_priv->ring.tail = outring;					\
	dev_priv->ring.space -= outcount * 4;				\
	I915_WRITE(PRB0_TAIL, outring);			\
} while(0)

/**
 * Reads a dword out of the status page, which is written to from the command
 * queue by automatic updates, MI_REPORT_HEAD, MI_STORE_DATA_INDEX, or
 * MI_STORE_DATA_IMM.
 *
 * The following dwords have a reserved meaning:
 * 0: ISR copy, updated when an ISR bit not set in the HWSTAM changes.
 * 4: ring 0 head pointer
 * 5: ring 1 head pointer (915-class)
 * 6: ring 2 head pointer (915-class)
 *
 * The area from dword 0x10 to 0x3ff is available for driver usage.
 */
#define READ_HWSP(dev_priv, reg)  (((volatile u32*)(dev_priv->hw_status_page))[reg])
#define READ_BREADCRUMB(dev_priv) READ_HWSP(dev_priv, 5)

extern int i915_wait_ring(struct drm_device * dev, int n, const char *caller);

#define IS_I830(dev) ((dev)->pci_device == 0x3577)
#define IS_845G(dev) ((dev)->pci_device == 0x2562)
#define IS_I85X(dev) ((dev)->pci_device == 0x3582)
#define IS_I855(dev) ((dev)->pci_device == 0x3582)
#define IS_I865G(dev) ((dev)->pci_device == 0x2572)

#define IS_I915G(dev) ((dev)->pci_device == 0x2582 || (dev)->pci_device == 0x258a)
#define IS_I915GM(dev) ((dev)->pci_device == 0x2592)
#define IS_I945G(dev) ((dev)->pci_device == 0x2772)
#define IS_I945GM(dev) ((dev)->pci_device == 0x27A2 ||\
		        (dev)->pci_device == 0x27AE)
#define IS_I965G(dev) ((dev)->pci_device == 0x2972 || \
		       (dev)->pci_device == 0x2982 || \
		       (dev)->pci_device == 0x2992 || \
		       (dev)->pci_device == 0x29A2 || \
		       (dev)->pci_device == 0x2A02 || \
		       (dev)->pci_device == 0x2A12 || \
		       (dev)->pci_device == 0x2A42 || \
		       (dev)->pci_device == 0x2E02 || \
		       (dev)->pci_device == 0x2E12 || \
		       (dev)->pci_device == 0x2E22)

#define IS_I965GM(dev) ((dev)->pci_device == 0x2A02)

#define IS_IGD_GM(dev) ((dev)->pci_device == 0x2A42)

#define IS_G4X(dev) ((dev)->pci_device == 0x2E02 || \
		     (dev)->pci_device == 0x2E12 || \
		     (dev)->pci_device == 0x2E22)

#define IS_G33(dev)    ((dev)->pci_device == 0x29C2 ||	\
			(dev)->pci_device == 0x29B2 ||	\
			(dev)->pci_device == 0x29D2)

#define IS_I9XX(dev) (IS_I915G(dev) || IS_I915GM(dev) || IS_I945G(dev) || \
		      IS_I945GM(dev) || IS_I965G(dev) || IS_G33(dev))

#define IS_MOBILE(dev) (IS_I830(dev) || IS_I85X(dev) || IS_I915GM(dev) || \
			IS_I945GM(dev) || IS_I965GM(dev) || IS_IGD_GM(dev))

#define I915_NEED_GFX_HWS(dev) (IS_G33(dev) || IS_IGD_GM(dev) || IS_G4X(dev))

#define PRIMARY_RINGBUFFER_SIZE         (128*1024)

#endif
