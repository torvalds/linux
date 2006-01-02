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
#ifndef _VIA_DRV_H_
#define _VIA_DRV_H_

#define DRIVER_AUTHOR	"Various"

#define DRIVER_NAME		"via"
#define DRIVER_DESC		"VIA Unichrome / Pro"
#define DRIVER_DATE		"20051116"

#define DRIVER_MAJOR		2
#define DRIVER_MINOR		7
#define DRIVER_PATCHLEVEL	4

#include "via_verifier.h"

#include "via_dmablit.h"

#define VIA_PCI_BUF_SIZE 60000
#define VIA_FIRE_BUF_SIZE  1024
#define VIA_NUM_IRQS 4

typedef struct drm_via_ring_buffer {
	drm_local_map_t map;
	char *virtual_start;
} drm_via_ring_buffer_t;

typedef uint32_t maskarray_t[5];

typedef struct drm_via_irq {
	atomic_t irq_received;
	uint32_t pending_mask;
	uint32_t enable_mask;
	wait_queue_head_t irq_queue;
} drm_via_irq_t;

typedef struct drm_via_private {
	drm_via_sarea_t *sarea_priv;
	drm_local_map_t *sarea;
	drm_local_map_t *fb;
	drm_local_map_t *mmio;
	unsigned long agpAddr;
	wait_queue_head_t decoder_queue[VIA_NR_XVMC_LOCKS];
	char *dma_ptr;
	unsigned int dma_low;
	unsigned int dma_high;
	unsigned int dma_offset;
	uint32_t dma_wrap;
	volatile uint32_t *last_pause_ptr;
	volatile uint32_t *hw_addr_ptr;
	drm_via_ring_buffer_t ring;
	struct timeval last_vblank;
	int last_vblank_valid;
	unsigned usec_per_vblank;
	drm_via_state_t hc_state;
	char pci_buf[VIA_PCI_BUF_SIZE];
	const uint32_t *fire_offsets[VIA_FIRE_BUF_SIZE];
	uint32_t num_fire_offsets;
	int pro_group_a;
	drm_via_irq_t via_irqs[VIA_NUM_IRQS];
	unsigned num_irqs;
	maskarray_t *irq_masks;
	uint32_t irq_enable_mask;
	uint32_t irq_pending_mask;
	int *irq_map;
	drm_via_blitq_t blit_queues[VIA_NUM_BLIT_ENGINES];
} drm_via_private_t;

enum via_family {
	VIA_OTHER = 0,
	VIA_PRO_GROUP_A,
};

/* VIA MMIO register access */
#define VIA_BASE ((dev_priv->mmio))

#define VIA_READ(reg)		DRM_READ32(VIA_BASE, reg)
#define VIA_WRITE(reg,val)	DRM_WRITE32(VIA_BASE, reg, val)
#define VIA_READ8(reg)		DRM_READ8(VIA_BASE, reg)
#define VIA_WRITE8(reg,val)	DRM_WRITE8(VIA_BASE, reg, val)

extern drm_ioctl_desc_t via_ioctls[];
extern int via_max_ioctl;

extern int via_fb_init(DRM_IOCTL_ARGS);
extern int via_mem_alloc(DRM_IOCTL_ARGS);
extern int via_mem_free(DRM_IOCTL_ARGS);
extern int via_agp_init(DRM_IOCTL_ARGS);
extern int via_map_init(DRM_IOCTL_ARGS);
extern int via_decoder_futex(DRM_IOCTL_ARGS);
extern int via_dma_init(DRM_IOCTL_ARGS);
extern int via_cmdbuffer(DRM_IOCTL_ARGS);
extern int via_flush_ioctl(DRM_IOCTL_ARGS);
extern int via_pci_cmdbuffer(DRM_IOCTL_ARGS);
extern int via_cmdbuf_size(DRM_IOCTL_ARGS);
extern int via_wait_irq(DRM_IOCTL_ARGS);
extern int via_dma_blit_sync( DRM_IOCTL_ARGS );
extern int via_dma_blit( DRM_IOCTL_ARGS );

extern int via_driver_load(drm_device_t *dev, unsigned long chipset);
extern int via_driver_unload(drm_device_t *dev);

extern int via_init_context(drm_device_t * dev, int context);
extern int via_final_context(drm_device_t * dev, int context);

extern int via_do_cleanup_map(drm_device_t * dev);
extern int via_driver_vblank_wait(drm_device_t * dev, unsigned int *sequence);

extern irqreturn_t via_driver_irq_handler(DRM_IRQ_ARGS);
extern void via_driver_irq_preinstall(drm_device_t * dev);
extern void via_driver_irq_postinstall(drm_device_t * dev);
extern void via_driver_irq_uninstall(drm_device_t * dev);

extern int via_dma_cleanup(drm_device_t * dev);
extern void via_init_command_verifier(void);
extern int via_driver_dma_quiescent(drm_device_t * dev);
extern void via_init_futex(drm_via_private_t * dev_priv);
extern void via_cleanup_futex(drm_via_private_t * dev_priv);
extern void via_release_futex(drm_via_private_t * dev_priv, int context);
extern int via_driver_irq_wait(drm_device_t * dev, unsigned int irq,
			       int force_sequence, unsigned int *sequence);

extern void via_dmablit_handler(drm_device_t *dev, int engine, int from_irq);
extern void via_init_dmablit(drm_device_t *dev);

#endif
