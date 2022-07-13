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

#include <linux/dma-mapping.h>
#include <linux/irqreturn.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/wait.h>

#include <drm/drm_ioctl.h>
#include <drm/drm_legacy.h>
#include <drm/drm_mm.h>
#include <drm/via_drm.h>

#define DRIVER_AUTHOR	"Various"

#define DRIVER_NAME		"via"
#define DRIVER_DESC		"VIA Unichrome / Pro"
#define DRIVER_DATE		"20070202"

#define DRIVER_MAJOR		2
#define DRIVER_MINOR		11
#define DRIVER_PATCHLEVEL	1

#include "via_verifier.h"

#define VIA_PCI_BUF_SIZE 60000
#define VIA_FIRE_BUF_SIZE  1024
#define VIA_NUM_IRQS 4


#define VIA_NUM_BLIT_ENGINES 2
#define VIA_NUM_BLIT_SLOTS 8

struct _drm_via_descriptor;

typedef struct _drm_via_sg_info {
	struct page **pages;
	unsigned long num_pages;
	struct _drm_via_descriptor **desc_pages;
	int num_desc_pages;
	int num_desc;
	enum dma_data_direction direction;
	unsigned char *bounce_buffer;
	dma_addr_t chain_start;
	uint32_t free_on_sequence;
	unsigned int descriptors_per_page;
	int aborted;
	enum {
		dr_via_device_mapped,
		dr_via_desc_pages_alloc,
		dr_via_pages_locked,
		dr_via_pages_alloc,
		dr_via_sg_init
	} state;
} drm_via_sg_info_t;

typedef struct _drm_via_blitq {
	struct drm_device *dev;
	uint32_t cur_blit_handle;
	uint32_t done_blit_handle;
	unsigned serviced;
	unsigned head;
	unsigned cur;
	unsigned num_free;
	unsigned num_outstanding;
	unsigned long end;
	int aborting;
	int is_active;
	drm_via_sg_info_t *blits[VIA_NUM_BLIT_SLOTS];
	spinlock_t blit_lock;
	wait_queue_head_t blit_queue[VIA_NUM_BLIT_SLOTS];
	wait_queue_head_t busy_queue;
	struct work_struct wq;
	struct timer_list poll_timer;
} drm_via_blitq_t;

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
	ktime_t last_vblank;
	int last_vblank_valid;
	ktime_t nsec_per_vblank;
	atomic_t vbl_received;
	drm_via_state_t hc_state;
	char pci_buf[VIA_PCI_BUF_SIZE];
	const uint32_t *fire_offsets[VIA_FIRE_BUF_SIZE];
	uint32_t num_fire_offsets;
	int chipset;
	drm_via_irq_t via_irqs[VIA_NUM_IRQS];
	unsigned num_irqs;
	maskarray_t *irq_masks;
	uint32_t irq_enable_mask;
	uint32_t irq_pending_mask;
	int *irq_map;
	unsigned int idle_fault;
	int vram_initialized;
	struct drm_mm vram_mm;
	int agp_initialized;
	struct drm_mm agp_mm;
	/** Mapping of userspace keys to mm objects */
	struct idr object_idr;
	unsigned long vram_offset;
	unsigned long agp_offset;
	drm_via_blitq_t blit_queues[VIA_NUM_BLIT_ENGINES];
	uint32_t dma_diff;
} drm_via_private_t;

struct via_file_private {
	struct list_head obj_list;
};

enum via_family {
  VIA_OTHER = 0,     /* Baseline */
  VIA_PRO_GROUP_A,   /* Another video engine and DMA commands */
  VIA_DX9_0          /* Same video as pro_group_a, but 3D is unsupported */
};

/* VIA MMIO register access */
static inline u32 via_read(struct drm_via_private *dev_priv, u32 reg)
{
	return readl((void __iomem *)(dev_priv->mmio->handle + reg));
}

static inline void via_write(struct drm_via_private *dev_priv, u32 reg,
			     u32 val)
{
	writel(val, (void __iomem *)(dev_priv->mmio->handle + reg));
}

static inline void via_write8(struct drm_via_private *dev_priv, u32 reg,
			      u32 val)
{
	writeb(val, (void __iomem *)(dev_priv->mmio->handle + reg));
}

static inline void via_write8_mask(struct drm_via_private *dev_priv,
				   u32 reg, u32 mask, u32 val)
{
	u32 tmp;

	tmp = readb((void __iomem *)(dev_priv->mmio->handle + reg));
	tmp = (tmp & ~mask) | (val & mask);
	writeb(tmp, (void __iomem *)(dev_priv->mmio->handle + reg));
}

/*
 * Poll in a loop waiting for 'contidition' to be true.
 * Note: A direct replacement with wait_event_interruptible_timeout()
 *       will not work unless driver is updated to emit wake_up()
 *       in relevant places that can impact the 'condition'
 *
 * Returns:
 *   ret keeps current value if 'condition' becomes true
 *   ret = -BUSY if timeout happens
 *   ret = -EINTR if a signal interrupted the waiting period
 */
#define VIA_WAIT_ON( ret, queue, timeout, condition )		\
do {								\
	DECLARE_WAITQUEUE(entry, current);			\
	unsigned long end = jiffies + (timeout);		\
	add_wait_queue(&(queue), &entry);			\
								\
	for (;;) {						\
		__set_current_state(TASK_INTERRUPTIBLE);	\
		if (condition)					\
			break;					\
		if (time_after_eq(jiffies, end)) {		\
			ret = -EBUSY;				\
			break;					\
		}						\
		schedule_timeout((HZ/100 > 1) ? HZ/100 : 1);	\
		if (signal_pending(current)) {			\
			ret = -EINTR;				\
			break;					\
		}						\
	}							\
	__set_current_state(TASK_RUNNING);			\
	remove_wait_queue(&(queue), &entry);			\
} while (0)

extern int via_init_context(struct drm_device *dev, int context);

extern int via_do_cleanup_map(struct drm_device *dev);

extern int via_dma_cleanup(struct drm_device *dev);
extern void via_init_command_verifier(void);
extern int via_driver_dma_quiescent(struct drm_device *dev);

#endif
