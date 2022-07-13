/*
 * Copyright 1998-2003 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2003 S3 Graphics, Inc. All Rights Reserved.
 * Copyright 2002 Tungsten Graphics, Inc.
 * Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas. All Rights Reserved.
 * Copyright 2006 Tungsten Graphics Inc., Bismarck, ND., USA.
 * Copyright 2004 Digeo, Inc., Palo Alto, CA, U.S.A. All Rights Reserved.
 * Copyright 2004 The Unichrome project. All Rights Reserved.
 * Copyright 2004 BEAM Ltd.
 * Copyright 2005 Thomas Hellstrom. All Rights Reserved.
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

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>

#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_pciids.h>
#include <drm/drm_vblank.h>
#include <drm/via_drm.h>

#include "via_drv.h"
#include "via_3d_reg.h"

#define CMDBUF_ALIGNMENT_SIZE   (0x100)
#define CMDBUF_ALIGNMENT_MASK   (0x0ff)

/* defines for VIA 3D registers */
#define VIA_REG_STATUS          0x400
#define VIA_REG_TRANSET         0x43C
#define VIA_REG_TRANSPACE       0x440

/* VIA_REG_STATUS(0x400): Engine Status */
#define VIA_CMD_RGTR_BUSY       0x00000080	/* Command Regulator is busy */
#define VIA_2D_ENG_BUSY         0x00000001	/* 2D Engine is busy */
#define VIA_3D_ENG_BUSY         0x00000002	/* 3D Engine is busy */
#define VIA_VR_QUEUE_BUSY       0x00020000	/* Virtual Queue is busy */

#define SetReg2DAGP(nReg, nData) {				\
	*((uint32_t *)(vb)) = ((nReg) >> 2) | HALCYON_HEADER1;	\
	*((uint32_t *)(vb) + 1) = (nData);			\
	vb = ((uint32_t *)vb) + 2;				\
	dev_priv->dma_low += 8;					\
}

#define via_flush_write_combine() mb()

#define VIA_OUT_RING_QW(w1, w2)	do {		\
	*vb++ = (w1);				\
	*vb++ = (w2);				\
	dev_priv->dma_low += 8;			\
} while (0)

#define VIA_MM_ALIGN_SHIFT 4
#define VIA_MM_ALIGN_MASK ((1 << VIA_MM_ALIGN_SHIFT) - 1)

struct via_memblock {
	struct drm_mm_node mm_node;
	struct list_head owner_list;
};

#define VIA_REG_INTERRUPT       0x200

/* VIA_REG_INTERRUPT */
#define VIA_IRQ_GLOBAL	  (1 << 31)
#define VIA_IRQ_VBLANK_ENABLE   (1 << 19)
#define VIA_IRQ_VBLANK_PENDING  (1 << 3)
#define VIA_IRQ_HQV0_ENABLE     (1 << 11)
#define VIA_IRQ_HQV1_ENABLE     (1 << 25)
#define VIA_IRQ_HQV0_PENDING    (1 << 9)
#define VIA_IRQ_HQV1_PENDING    (1 << 10)
#define VIA_IRQ_DMA0_DD_ENABLE  (1 << 20)
#define VIA_IRQ_DMA0_TD_ENABLE  (1 << 21)
#define VIA_IRQ_DMA1_DD_ENABLE  (1 << 22)
#define VIA_IRQ_DMA1_TD_ENABLE  (1 << 23)
#define VIA_IRQ_DMA0_DD_PENDING (1 << 4)
#define VIA_IRQ_DMA0_TD_PENDING (1 << 5)
#define VIA_IRQ_DMA1_DD_PENDING (1 << 6)
#define VIA_IRQ_DMA1_TD_PENDING (1 << 7)

/*
 *  PCI DMA Registers
 *  Channels 2 & 3 don't seem to be implemented in hardware.
 */

#define VIA_PCI_DMA_MAR0            0xE40   /* Memory Address Register of Channel 0 */
#define VIA_PCI_DMA_DAR0            0xE44   /* Device Address Register of Channel 0 */
#define VIA_PCI_DMA_BCR0            0xE48   /* Byte Count Register of Channel 0 */
#define VIA_PCI_DMA_DPR0            0xE4C   /* Descriptor Pointer Register of Channel 0 */

#define VIA_PCI_DMA_MAR1            0xE50   /* Memory Address Register of Channel 1 */
#define VIA_PCI_DMA_DAR1            0xE54   /* Device Address Register of Channel 1 */
#define VIA_PCI_DMA_BCR1            0xE58   /* Byte Count Register of Channel 1 */
#define VIA_PCI_DMA_DPR1            0xE5C   /* Descriptor Pointer Register of Channel 1 */

#define VIA_PCI_DMA_MAR2            0xE60   /* Memory Address Register of Channel 2 */
#define VIA_PCI_DMA_DAR2            0xE64   /* Device Address Register of Channel 2 */
#define VIA_PCI_DMA_BCR2            0xE68   /* Byte Count Register of Channel 2 */
#define VIA_PCI_DMA_DPR2            0xE6C   /* Descriptor Pointer Register of Channel 2 */

#define VIA_PCI_DMA_MAR3            0xE70   /* Memory Address Register of Channel 3 */
#define VIA_PCI_DMA_DAR3            0xE74   /* Device Address Register of Channel 3 */
#define VIA_PCI_DMA_BCR3            0xE78   /* Byte Count Register of Channel 3 */
#define VIA_PCI_DMA_DPR3            0xE7C   /* Descriptor Pointer Register of Channel 3 */

#define VIA_PCI_DMA_MR0             0xE80   /* Mode Register of Channel 0 */
#define VIA_PCI_DMA_MR1             0xE84   /* Mode Register of Channel 1 */
#define VIA_PCI_DMA_MR2             0xE88   /* Mode Register of Channel 2 */
#define VIA_PCI_DMA_MR3             0xE8C   /* Mode Register of Channel 3 */

#define VIA_PCI_DMA_CSR0            0xE90   /* Command/Status Register of Channel 0 */
#define VIA_PCI_DMA_CSR1            0xE94   /* Command/Status Register of Channel 1 */
#define VIA_PCI_DMA_CSR2            0xE98   /* Command/Status Register of Channel 2 */
#define VIA_PCI_DMA_CSR3            0xE9C   /* Command/Status Register of Channel 3 */

#define VIA_PCI_DMA_PTR             0xEA0   /* Priority Type Register */

/* Define for DMA engine */
/* DPR */
#define VIA_DMA_DPR_EC		(1<<1)	/* end of chain */
#define VIA_DMA_DPR_DDIE	(1<<2)	/* descriptor done interrupt enable */
#define VIA_DMA_DPR_DT		(1<<3)	/* direction of transfer (RO) */

/* MR */
#define VIA_DMA_MR_CM		(1<<0)	/* chaining mode */
#define VIA_DMA_MR_TDIE		(1<<1)	/* transfer done interrupt enable */
#define VIA_DMA_MR_HENDMACMD		(1<<7) /* ? */

/* CSR */
#define VIA_DMA_CSR_DE		(1<<0)	/* DMA enable */
#define VIA_DMA_CSR_TS		(1<<1)	/* transfer start */
#define VIA_DMA_CSR_TA		(1<<2)	/* transfer abort */
#define VIA_DMA_CSR_TD		(1<<3)	/* transfer done */
#define VIA_DMA_CSR_DD		(1<<4)	/* descriptor done */
#define VIA_DMA_DPR_EC          (1<<1)  /* end of chain */

/*
 * Device-specific IRQs go here. This type might need to be extended with
 * the register if there are multiple IRQ control registers.
 * Currently we activate the HQV interrupts of  Unichrome Pro group A.
 */

static maskarray_t via_pro_group_a_irqs[] = {
	{VIA_IRQ_HQV0_ENABLE, VIA_IRQ_HQV0_PENDING, 0x000003D0, 0x00008010,
	 0x00000000 },
	{VIA_IRQ_HQV1_ENABLE, VIA_IRQ_HQV1_PENDING, 0x000013D0, 0x00008010,
	 0x00000000 },
	{VIA_IRQ_DMA0_TD_ENABLE, VIA_IRQ_DMA0_TD_PENDING, VIA_PCI_DMA_CSR0,
	 VIA_DMA_CSR_TA | VIA_DMA_CSR_TD, 0x00000008},
	{VIA_IRQ_DMA1_TD_ENABLE, VIA_IRQ_DMA1_TD_PENDING, VIA_PCI_DMA_CSR1,
	 VIA_DMA_CSR_TA | VIA_DMA_CSR_TD, 0x00000008},
};
static int via_num_pro_group_a = ARRAY_SIZE(via_pro_group_a_irqs);
static int via_irqmap_pro_group_a[] = {0, 1, -1, 2, -1, 3};

static maskarray_t via_unichrome_irqs[] = {
	{VIA_IRQ_DMA0_TD_ENABLE, VIA_IRQ_DMA0_TD_PENDING, VIA_PCI_DMA_CSR0,
	 VIA_DMA_CSR_TA | VIA_DMA_CSR_TD, 0x00000008},
	{VIA_IRQ_DMA1_TD_ENABLE, VIA_IRQ_DMA1_TD_PENDING, VIA_PCI_DMA_CSR1,
	 VIA_DMA_CSR_TA | VIA_DMA_CSR_TD, 0x00000008}
};
static int via_num_unichrome = ARRAY_SIZE(via_unichrome_irqs);
static int via_irqmap_unichrome[] = {-1, -1, -1, 0, -1, 1};


/*
 * Unmaps the DMA mappings.
 * FIXME: Is this a NoOp on x86? Also
 * FIXME: What happens if this one is called and a pending blit has previously done
 * the same DMA mappings?
 */
#define VIA_PGDN(x)	     (((unsigned long)(x)) & PAGE_MASK)
#define VIA_PGOFF(x)	    (((unsigned long)(x)) & ~PAGE_MASK)
#define VIA_PFN(x)	      ((unsigned long)(x) >> PAGE_SHIFT)

typedef struct _drm_via_descriptor {
	uint32_t mem_addr;
	uint32_t dev_addr;
	uint32_t size;
	uint32_t next;
} drm_via_descriptor_t;


/*
 * Unmap a DMA mapping.
 */
static void
via_unmap_blit_from_device(struct pci_dev *pdev, drm_via_sg_info_t *vsg)
{
	int num_desc = vsg->num_desc;
	unsigned cur_descriptor_page = num_desc / vsg->descriptors_per_page;
	unsigned descriptor_this_page = num_desc % vsg->descriptors_per_page;
	drm_via_descriptor_t *desc_ptr = vsg->desc_pages[cur_descriptor_page] +
		descriptor_this_page;
	dma_addr_t next = vsg->chain_start;

	while (num_desc--) {
		if (descriptor_this_page-- == 0) {
			cur_descriptor_page--;
			descriptor_this_page = vsg->descriptors_per_page - 1;
			desc_ptr = vsg->desc_pages[cur_descriptor_page] +
				descriptor_this_page;
		}
		dma_unmap_single(&pdev->dev, next, sizeof(*desc_ptr), DMA_TO_DEVICE);
		dma_unmap_page(&pdev->dev, desc_ptr->mem_addr, desc_ptr->size, vsg->direction);
		next = (dma_addr_t) desc_ptr->next;
		desc_ptr--;
	}
}

/*
 * If mode = 0, count how many descriptors are needed.
 * If mode = 1, Map the DMA pages for the device, put together and map also the descriptors.
 * Descriptors are run in reverse order by the hardware because we are not allowed to update the
 * 'next' field without syncing calls when the descriptor is already mapped.
 */
static void
via_map_blit_for_device(struct pci_dev *pdev,
		   const drm_via_dmablit_t *xfer,
		   drm_via_sg_info_t *vsg,
		   int mode)
{
	unsigned cur_descriptor_page = 0;
	unsigned num_descriptors_this_page = 0;
	unsigned char *mem_addr = xfer->mem_addr;
	unsigned char *cur_mem;
	unsigned char *first_addr = (unsigned char *)VIA_PGDN(mem_addr);
	uint32_t fb_addr = xfer->fb_addr;
	uint32_t cur_fb;
	unsigned long line_len;
	unsigned remaining_len;
	int num_desc = 0;
	int cur_line;
	dma_addr_t next = 0 | VIA_DMA_DPR_EC;
	drm_via_descriptor_t *desc_ptr = NULL;

	if (mode == 1)
		desc_ptr = vsg->desc_pages[cur_descriptor_page];

	for (cur_line = 0; cur_line < xfer->num_lines; ++cur_line) {

		line_len = xfer->line_length;
		cur_fb = fb_addr;
		cur_mem = mem_addr;

		while (line_len > 0) {

			remaining_len = min(PAGE_SIZE-VIA_PGOFF(cur_mem), line_len);
			line_len -= remaining_len;

			if (mode == 1) {
				desc_ptr->mem_addr =
					dma_map_page(&pdev->dev,
						     vsg->pages[VIA_PFN(cur_mem) -
								VIA_PFN(first_addr)],
						     VIA_PGOFF(cur_mem), remaining_len,
						     vsg->direction);
				desc_ptr->dev_addr = cur_fb;

				desc_ptr->size = remaining_len;
				desc_ptr->next = (uint32_t) next;
				next = dma_map_single(&pdev->dev, desc_ptr, sizeof(*desc_ptr),
						      DMA_TO_DEVICE);
				desc_ptr++;
				if (++num_descriptors_this_page >= vsg->descriptors_per_page) {
					num_descriptors_this_page = 0;
					desc_ptr = vsg->desc_pages[++cur_descriptor_page];
				}
			}

			num_desc++;
			cur_mem += remaining_len;
			cur_fb += remaining_len;
		}

		mem_addr += xfer->mem_stride;
		fb_addr += xfer->fb_stride;
	}

	if (mode == 1) {
		vsg->chain_start = next;
		vsg->state = dr_via_device_mapped;
	}
	vsg->num_desc = num_desc;
}

/*
 * Function that frees up all resources for a blit. It is usable even if the
 * blit info has only been partially built as long as the status enum is consistent
 * with the actual status of the used resources.
 */
static void
via_free_sg_info(struct pci_dev *pdev, drm_via_sg_info_t *vsg)
{
	int i;

	switch (vsg->state) {
	case dr_via_device_mapped:
		via_unmap_blit_from_device(pdev, vsg);
		fallthrough;
	case dr_via_desc_pages_alloc:
		for (i = 0; i < vsg->num_desc_pages; ++i) {
			if (vsg->desc_pages[i] != NULL)
				free_page((unsigned long)vsg->desc_pages[i]);
		}
		kfree(vsg->desc_pages);
		fallthrough;
	case dr_via_pages_locked:
		unpin_user_pages_dirty_lock(vsg->pages, vsg->num_pages,
					   (vsg->direction == DMA_FROM_DEVICE));
		fallthrough;
	case dr_via_pages_alloc:
		vfree(vsg->pages);
		fallthrough;
	default:
		vsg->state = dr_via_sg_init;
	}
	vfree(vsg->bounce_buffer);
	vsg->bounce_buffer = NULL;
	vsg->free_on_sequence = 0;
}

/*
 * Fire a blit engine.
 */
static void
via_fire_dmablit(struct drm_device *dev, drm_via_sg_info_t *vsg, int engine)
{
	drm_via_private_t *dev_priv = (drm_via_private_t *)dev->dev_private;

	via_write(dev_priv, VIA_PCI_DMA_MAR0 + engine*0x10, 0);
	via_write(dev_priv, VIA_PCI_DMA_DAR0 + engine*0x10, 0);
	via_write(dev_priv, VIA_PCI_DMA_CSR0 + engine*0x04, VIA_DMA_CSR_DD | VIA_DMA_CSR_TD |
		  VIA_DMA_CSR_DE);
	via_write(dev_priv, VIA_PCI_DMA_MR0  + engine*0x04, VIA_DMA_MR_CM | VIA_DMA_MR_TDIE);
	via_write(dev_priv, VIA_PCI_DMA_BCR0 + engine*0x10, 0);
	via_write(dev_priv, VIA_PCI_DMA_DPR0 + engine*0x10, vsg->chain_start);
	wmb();
	via_write(dev_priv, VIA_PCI_DMA_CSR0 + engine*0x04, VIA_DMA_CSR_DE | VIA_DMA_CSR_TS);
	via_read(dev_priv, VIA_PCI_DMA_CSR0 + engine*0x04);
}

/*
 * Obtain a page pointer array and lock all pages into system memory. A segmentation violation will
 * occur here if the calling user does not have access to the submitted address.
 */
static int
via_lock_all_dma_pages(drm_via_sg_info_t *vsg,  drm_via_dmablit_t *xfer)
{
	int ret;
	unsigned long first_pfn = VIA_PFN(xfer->mem_addr);
	vsg->num_pages = VIA_PFN(xfer->mem_addr + (xfer->num_lines * xfer->mem_stride - 1)) -
		first_pfn + 1;

	vsg->pages = vzalloc(array_size(sizeof(struct page *), vsg->num_pages));
	if (NULL == vsg->pages)
		return -ENOMEM;
	ret = pin_user_pages_fast((unsigned long)xfer->mem_addr,
			vsg->num_pages,
			vsg->direction == DMA_FROM_DEVICE ? FOLL_WRITE : 0,
			vsg->pages);
	if (ret != vsg->num_pages) {
		if (ret < 0)
			return ret;
		vsg->state = dr_via_pages_locked;
		return -EINVAL;
	}
	vsg->state = dr_via_pages_locked;
	DRM_DEBUG("DMA pages locked\n");
	return 0;
}

/*
 * Allocate DMA capable memory for the blit descriptor chain, and an array that keeps track of the
 * pages we allocate. We don't want to use kmalloc for the descriptor chain because it may be
 * quite large for some blits, and pages don't need to be contiguous.
 */
static int
via_alloc_desc_pages(drm_via_sg_info_t *vsg)
{
	int i;

	vsg->descriptors_per_page = PAGE_SIZE / sizeof(drm_via_descriptor_t);
	vsg->num_desc_pages = (vsg->num_desc + vsg->descriptors_per_page - 1) /
		vsg->descriptors_per_page;

	if (NULL ==  (vsg->desc_pages = kcalloc(vsg->num_desc_pages, sizeof(void *), GFP_KERNEL)))
		return -ENOMEM;

	vsg->state = dr_via_desc_pages_alloc;
	for (i = 0; i < vsg->num_desc_pages; ++i) {
		if (NULL == (vsg->desc_pages[i] =
			     (drm_via_descriptor_t *) __get_free_page(GFP_KERNEL)))
			return -ENOMEM;
	}
	DRM_DEBUG("Allocated %d pages for %d descriptors.\n", vsg->num_desc_pages,
		  vsg->num_desc);
	return 0;
}

static void
via_abort_dmablit(struct drm_device *dev, int engine)
{
	drm_via_private_t *dev_priv = (drm_via_private_t *)dev->dev_private;

	via_write(dev_priv, VIA_PCI_DMA_CSR0 + engine*0x04, VIA_DMA_CSR_TA);
}

static void
via_dmablit_engine_off(struct drm_device *dev, int engine)
{
	drm_via_private_t *dev_priv = (drm_via_private_t *)dev->dev_private;

	via_write(dev_priv, VIA_PCI_DMA_CSR0 + engine*0x04, VIA_DMA_CSR_TD | VIA_DMA_CSR_DD);
}

/*
 * The dmablit part of the IRQ handler. Trying to do only reasonably fast things here.
 * The rest, like unmapping and freeing memory for done blits is done in a separate workqueue
 * task. Basically the task of the interrupt handler is to submit a new blit to the engine, while
 * the workqueue task takes care of processing associated with the old blit.
 */
static void
via_dmablit_handler(struct drm_device *dev, int engine, int from_irq)
{
	drm_via_private_t *dev_priv = (drm_via_private_t *)dev->dev_private;
	drm_via_blitq_t *blitq = dev_priv->blit_queues + engine;
	int cur;
	int done_transfer;
	unsigned long irqsave = 0;
	uint32_t status = 0;

	DRM_DEBUG("DMA blit handler called. engine = %d, from_irq = %d, blitq = 0x%lx\n",
		  engine, from_irq, (unsigned long) blitq);

	if (from_irq)
		spin_lock(&blitq->blit_lock);
	else
		spin_lock_irqsave(&blitq->blit_lock, irqsave);

	done_transfer = blitq->is_active &&
	  ((status = via_read(dev_priv, VIA_PCI_DMA_CSR0 + engine*0x04)) & VIA_DMA_CSR_TD);
	done_transfer = done_transfer || (blitq->aborting && !(status & VIA_DMA_CSR_DE));

	cur = blitq->cur;
	if (done_transfer) {

		blitq->blits[cur]->aborted = blitq->aborting;
		blitq->done_blit_handle++;
		wake_up(blitq->blit_queue + cur);

		cur++;
		if (cur >= VIA_NUM_BLIT_SLOTS)
			cur = 0;
		blitq->cur = cur;

		/*
		 * Clear transfer done flag.
		 */

		via_write(dev_priv, VIA_PCI_DMA_CSR0 + engine*0x04,  VIA_DMA_CSR_TD);

		blitq->is_active = 0;
		blitq->aborting = 0;
		schedule_work(&blitq->wq);

	} else if (blitq->is_active && time_after_eq(jiffies, blitq->end)) {

		/*
		 * Abort transfer after one second.
		 */

		via_abort_dmablit(dev, engine);
		blitq->aborting = 1;
		blitq->end = jiffies + HZ;
	}

	if (!blitq->is_active) {
		if (blitq->num_outstanding) {
			via_fire_dmablit(dev, blitq->blits[cur], engine);
			blitq->is_active = 1;
			blitq->cur = cur;
			blitq->num_outstanding--;
			blitq->end = jiffies + HZ;
			if (!timer_pending(&blitq->poll_timer))
				mod_timer(&blitq->poll_timer, jiffies + 1);
		} else {
			if (timer_pending(&blitq->poll_timer))
				del_timer(&blitq->poll_timer);
			via_dmablit_engine_off(dev, engine);
		}
	}

	if (from_irq)
		spin_unlock(&blitq->blit_lock);
	else
		spin_unlock_irqrestore(&blitq->blit_lock, irqsave);
}

/*
 * Check whether this blit is still active, performing necessary locking.
 */
static int
via_dmablit_active(drm_via_blitq_t *blitq, int engine, uint32_t handle, wait_queue_head_t **queue)
{
	unsigned long irqsave;
	uint32_t slot;
	int active;

	spin_lock_irqsave(&blitq->blit_lock, irqsave);

	/*
	 * Allow for handle wraparounds.
	 */

	active = ((blitq->done_blit_handle - handle) > (1 << 23)) &&
		((blitq->cur_blit_handle - handle) <= (1 << 23));

	if (queue && active) {
		slot = handle - blitq->done_blit_handle + blitq->cur - 1;
		if (slot >= VIA_NUM_BLIT_SLOTS)
			slot -= VIA_NUM_BLIT_SLOTS;
		*queue = blitq->blit_queue + slot;
	}

	spin_unlock_irqrestore(&blitq->blit_lock, irqsave);

	return active;
}

/*
 * Sync. Wait for at least three seconds for the blit to be performed.
 */
static int
via_dmablit_sync(struct drm_device *dev, uint32_t handle, int engine)
{

	drm_via_private_t *dev_priv = (drm_via_private_t *)dev->dev_private;
	drm_via_blitq_t *blitq = dev_priv->blit_queues + engine;
	wait_queue_head_t *queue;
	int ret = 0;

	if (via_dmablit_active(blitq, engine, handle, &queue)) {
		VIA_WAIT_ON(ret, *queue, 3 * HZ,
			    !via_dmablit_active(blitq, engine, handle, NULL));
	}
	DRM_DEBUG("DMA blit sync handle 0x%x engine %d returned %d\n",
		  handle, engine, ret);

	return ret;
}

/*
 * A timer that regularly polls the blit engine in cases where we don't have interrupts:
 * a) Broken hardware (typically those that don't have any video capture facility).
 * b) Blit abort. The hardware doesn't send an interrupt when a blit is aborted.
 * The timer and hardware IRQ's can and do work in parallel. If the hardware has
 * irqs, it will shorten the latency somewhat.
 */
static void
via_dmablit_timer(struct timer_list *t)
{
	drm_via_blitq_t *blitq = from_timer(blitq, t, poll_timer);
	struct drm_device *dev = blitq->dev;
	int engine = (int)
		(blitq - ((drm_via_private_t *)dev->dev_private)->blit_queues);

	DRM_DEBUG("Polling timer called for engine %d, jiffies %lu\n", engine,
		  (unsigned long) jiffies);

	via_dmablit_handler(dev, engine, 0);

	if (!timer_pending(&blitq->poll_timer)) {
		mod_timer(&blitq->poll_timer, jiffies + 1);

	       /*
		* Rerun handler to delete timer if engines are off, and
		* to shorten abort latency. This is a little nasty.
		*/

	       via_dmablit_handler(dev, engine, 0);

	}
}

/*
 * Workqueue task that frees data and mappings associated with a blit.
 * Also wakes up waiting processes. Each of these tasks handles one
 * blit engine only and may not be called on each interrupt.
 */
static void
via_dmablit_workqueue(struct work_struct *work)
{
	drm_via_blitq_t *blitq = container_of(work, drm_via_blitq_t, wq);
	struct drm_device *dev = blitq->dev;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	unsigned long irqsave;
	drm_via_sg_info_t *cur_sg;
	int cur_released;


	DRM_DEBUG("Workqueue task called for blit engine %ld\n", (unsigned long)
		  (blitq - ((drm_via_private_t *)dev->dev_private)->blit_queues));

	spin_lock_irqsave(&blitq->blit_lock, irqsave);

	while (blitq->serviced != blitq->cur) {

		cur_released = blitq->serviced++;

		DRM_DEBUG("Releasing blit slot %d\n", cur_released);

		if (blitq->serviced >= VIA_NUM_BLIT_SLOTS)
			blitq->serviced = 0;

		cur_sg = blitq->blits[cur_released];
		blitq->num_free++;

		spin_unlock_irqrestore(&blitq->blit_lock, irqsave);

		wake_up(&blitq->busy_queue);

		via_free_sg_info(pdev, cur_sg);
		kfree(cur_sg);

		spin_lock_irqsave(&blitq->blit_lock, irqsave);
	}

	spin_unlock_irqrestore(&blitq->blit_lock, irqsave);
}

/*
 * Init all blit engines. Currently we use two, but some hardware have 4.
 */
static void
via_init_dmablit(struct drm_device *dev)
{
	int i, j;
	drm_via_private_t *dev_priv = (drm_via_private_t *)dev->dev_private;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	drm_via_blitq_t *blitq;

	pci_set_master(pdev);

	for (i = 0; i < VIA_NUM_BLIT_ENGINES; ++i) {
		blitq = dev_priv->blit_queues + i;
		blitq->dev = dev;
		blitq->cur_blit_handle = 0;
		blitq->done_blit_handle = 0;
		blitq->head = 0;
		blitq->cur = 0;
		blitq->serviced = 0;
		blitq->num_free = VIA_NUM_BLIT_SLOTS - 1;
		blitq->num_outstanding = 0;
		blitq->is_active = 0;
		blitq->aborting = 0;
		spin_lock_init(&blitq->blit_lock);
		for (j = 0; j < VIA_NUM_BLIT_SLOTS; ++j)
			init_waitqueue_head(blitq->blit_queue + j);
		init_waitqueue_head(&blitq->busy_queue);
		INIT_WORK(&blitq->wq, via_dmablit_workqueue);
		timer_setup(&blitq->poll_timer, via_dmablit_timer, 0);
	}
}

/*
 * Build all info and do all mappings required for a blit.
 */
static int
via_build_sg_info(struct drm_device *dev, drm_via_sg_info_t *vsg, drm_via_dmablit_t *xfer)
{
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	int draw = xfer->to_fb;
	int ret = 0;

	vsg->direction = (draw) ? DMA_TO_DEVICE : DMA_FROM_DEVICE;
	vsg->bounce_buffer = NULL;

	vsg->state = dr_via_sg_init;

	if (xfer->num_lines <= 0 || xfer->line_length <= 0) {
		DRM_ERROR("Zero size bitblt.\n");
		return -EINVAL;
	}

	/*
	 * Below check is a driver limitation, not a hardware one. We
	 * don't want to lock unused pages, and don't want to incoporate the
	 * extra logic of avoiding them. Make sure there are no.
	 * (Not a big limitation anyway.)
	 */

	if ((xfer->mem_stride - xfer->line_length) > 2*PAGE_SIZE) {
		DRM_ERROR("Too large system memory stride. Stride: %d, "
			  "Length: %d\n", xfer->mem_stride, xfer->line_length);
		return -EINVAL;
	}

	if ((xfer->mem_stride == xfer->line_length) &&
	   (xfer->fb_stride == xfer->line_length)) {
		xfer->mem_stride *= xfer->num_lines;
		xfer->line_length = xfer->mem_stride;
		xfer->fb_stride = xfer->mem_stride;
		xfer->num_lines = 1;
	}

	/*
	 * Don't lock an arbitrary large number of pages, since that causes a
	 * DOS security hole.
	 */

	if (xfer->num_lines > 2048 || (xfer->num_lines*xfer->mem_stride > (2048*2048*4))) {
		DRM_ERROR("Too large PCI DMA bitblt.\n");
		return -EINVAL;
	}

	/*
	 * we allow a negative fb stride to allow flipping of images in
	 * transfer.
	 */

	if (xfer->mem_stride < xfer->line_length ||
		abs(xfer->fb_stride) < xfer->line_length) {
		DRM_ERROR("Invalid frame-buffer / memory stride.\n");
		return -EINVAL;
	}

	/*
	 * A hardware bug seems to be worked around if system memory addresses start on
	 * 16 byte boundaries. This seems a bit restrictive however. VIA is contacted
	 * about this. Meanwhile, impose the following restrictions:
	 */

#ifdef VIA_BUGFREE
	if ((((unsigned long)xfer->mem_addr & 3) != ((unsigned long)xfer->fb_addr & 3)) ||
	    ((xfer->num_lines > 1) && ((xfer->mem_stride & 3) != (xfer->fb_stride & 3)))) {
		DRM_ERROR("Invalid DRM bitblt alignment.\n");
		return -EINVAL;
	}
#else
	if ((((unsigned long)xfer->mem_addr & 15) ||
	      ((unsigned long)xfer->fb_addr & 3)) ||
	   ((xfer->num_lines > 1) &&
	   ((xfer->mem_stride & 15) || (xfer->fb_stride & 3)))) {
		DRM_ERROR("Invalid DRM bitblt alignment.\n");
		return -EINVAL;
	}
#endif

	if (0 != (ret = via_lock_all_dma_pages(vsg, xfer))) {
		DRM_ERROR("Could not lock DMA pages.\n");
		via_free_sg_info(pdev, vsg);
		return ret;
	}

	via_map_blit_for_device(pdev, xfer, vsg, 0);
	if (0 != (ret = via_alloc_desc_pages(vsg))) {
		DRM_ERROR("Could not allocate DMA descriptor pages.\n");
		via_free_sg_info(pdev, vsg);
		return ret;
	}
	via_map_blit_for_device(pdev, xfer, vsg, 1);

	return 0;
}

/*
 * Reserve one free slot in the blit queue. Will wait for one second for one
 * to become available. Otherwise -EBUSY is returned.
 */
static int
via_dmablit_grab_slot(drm_via_blitq_t *blitq, int engine)
{
	int ret = 0;
	unsigned long irqsave;

	DRM_DEBUG("Num free is %d\n", blitq->num_free);
	spin_lock_irqsave(&blitq->blit_lock, irqsave);
	while (blitq->num_free == 0) {
		spin_unlock_irqrestore(&blitq->blit_lock, irqsave);

		VIA_WAIT_ON(ret, blitq->busy_queue, HZ, blitq->num_free > 0);
		if (ret)
			return (-EINTR == ret) ? -EAGAIN : ret;

		spin_lock_irqsave(&blitq->blit_lock, irqsave);
	}

	blitq->num_free--;
	spin_unlock_irqrestore(&blitq->blit_lock, irqsave);

	return 0;
}

/*
 * Hand back a free slot if we changed our mind.
 */
static void
via_dmablit_release_slot(drm_via_blitq_t *blitq)
{
	unsigned long irqsave;

	spin_lock_irqsave(&blitq->blit_lock, irqsave);
	blitq->num_free++;
	spin_unlock_irqrestore(&blitq->blit_lock, irqsave);
	wake_up(&blitq->busy_queue);
}

/*
 * Grab a free slot. Build blit info and queue a blit.
 */
static int
via_dmablit(struct drm_device *dev, drm_via_dmablit_t *xfer)
{
	drm_via_private_t *dev_priv = (drm_via_private_t *)dev->dev_private;
	drm_via_sg_info_t *vsg;
	drm_via_blitq_t *blitq;
	int ret;
	int engine;
	unsigned long irqsave;

	if (dev_priv == NULL) {
		DRM_ERROR("Called without initialization.\n");
		return -EINVAL;
	}

	engine = (xfer->to_fb) ? 0 : 1;
	blitq = dev_priv->blit_queues + engine;
	if (0 != (ret = via_dmablit_grab_slot(blitq, engine)))
		return ret;
	if (NULL == (vsg = kmalloc(sizeof(*vsg), GFP_KERNEL))) {
		via_dmablit_release_slot(blitq);
		return -ENOMEM;
	}
	if (0 != (ret = via_build_sg_info(dev, vsg, xfer))) {
		via_dmablit_release_slot(blitq);
		kfree(vsg);
		return ret;
	}
	spin_lock_irqsave(&blitq->blit_lock, irqsave);

	blitq->blits[blitq->head++] = vsg;
	if (blitq->head >= VIA_NUM_BLIT_SLOTS)
		blitq->head = 0;
	blitq->num_outstanding++;
	xfer->sync.sync_handle = ++blitq->cur_blit_handle;

	spin_unlock_irqrestore(&blitq->blit_lock, irqsave);
	xfer->sync.engine = engine;

	via_dmablit_handler(dev, engine, 0);

	return 0;
}

/*
 * Sync on a previously submitted blit. Note that the X server use signals extensively, and
 * that there is a very big probability that this IOCTL will be interrupted by a signal. In that
 * case it returns with -EAGAIN for the signal to be delivered.
 * The caller should then reissue the IOCTL. This is similar to what is being done for drmGetLock().
 */
static int
via_dma_blit_sync(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_via_blitsync_t *sync = data;
	int err;

	if (sync->engine >= VIA_NUM_BLIT_ENGINES)
		return -EINVAL;

	err = via_dmablit_sync(dev, sync->sync_handle, sync->engine);

	if (-EINTR == err)
		err = -EAGAIN;

	return err;
}

/*
 * Queue a blit and hand back a handle to be used for sync. This IOCTL may be interrupted by a signal
 * while waiting for a free slot in the blit queue. In that case it returns with -EAGAIN and should
 * be reissued. See the above IOCTL code.
 */
static int
via_dma_blit(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_via_dmablit_t *xfer = data;
	int err;

	err = via_dmablit(dev, xfer);

	return err;
}

static u32 via_get_vblank_counter(struct drm_device *dev, unsigned int pipe)
{
	drm_via_private_t *dev_priv = dev->dev_private;

	if (pipe != 0)
		return 0;

	return atomic_read(&dev_priv->vbl_received);
}

static irqreturn_t via_driver_irq_handler(int irq, void *arg)
{
	struct drm_device *dev = (struct drm_device *) arg;
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;
	u32 status;
	int handled = 0;
	ktime_t cur_vblank;
	drm_via_irq_t *cur_irq = dev_priv->via_irqs;
	int i;

	status = via_read(dev_priv, VIA_REG_INTERRUPT);
	if (status & VIA_IRQ_VBLANK_PENDING) {
		atomic_inc(&dev_priv->vbl_received);
		if (!(atomic_read(&dev_priv->vbl_received) & 0x0F)) {
			cur_vblank = ktime_get();
			if (dev_priv->last_vblank_valid) {
				dev_priv->nsec_per_vblank =
					ktime_sub(cur_vblank,
						dev_priv->last_vblank) >> 4;
			}
			dev_priv->last_vblank = cur_vblank;
			dev_priv->last_vblank_valid = 1;
		}
		if (!(atomic_read(&dev_priv->vbl_received) & 0xFF)) {
			DRM_DEBUG("nsec per vblank is: %llu\n",
				  ktime_to_ns(dev_priv->nsec_per_vblank));
		}
		drm_handle_vblank(dev, 0);
		handled = 1;
	}

	for (i = 0; i < dev_priv->num_irqs; ++i) {
		if (status & cur_irq->pending_mask) {
			atomic_inc(&cur_irq->irq_received);
			wake_up(&cur_irq->irq_queue);
			handled = 1;
			if (dev_priv->irq_map[drm_via_irq_dma0_td] == i)
				via_dmablit_handler(dev, 0, 1);
			else if (dev_priv->irq_map[drm_via_irq_dma1_td] == i)
				via_dmablit_handler(dev, 1, 1);
		}
		cur_irq++;
	}

	/* Acknowledge interrupts */
	via_write(dev_priv, VIA_REG_INTERRUPT, status);


	if (handled)
		return IRQ_HANDLED;
	else
		return IRQ_NONE;
}

static __inline__ void viadrv_acknowledge_irqs(drm_via_private_t *dev_priv)
{
	u32 status;

	if (dev_priv) {
		/* Acknowledge interrupts */
		status = via_read(dev_priv, VIA_REG_INTERRUPT);
		via_write(dev_priv, VIA_REG_INTERRUPT, status |
			  dev_priv->irq_pending_mask);
	}
}

static int via_enable_vblank(struct drm_device *dev, unsigned int pipe)
{
	drm_via_private_t *dev_priv = dev->dev_private;
	u32 status;

	if (pipe != 0) {
		DRM_ERROR("%s:  bad crtc %u\n", __func__, pipe);
		return -EINVAL;
	}

	status = via_read(dev_priv, VIA_REG_INTERRUPT);
	via_write(dev_priv, VIA_REG_INTERRUPT, status | VIA_IRQ_VBLANK_ENABLE);

	via_write8(dev_priv, 0x83d4, 0x11);
	via_write8_mask(dev_priv, 0x83d5, 0x30, 0x30);

	return 0;
}

static void via_disable_vblank(struct drm_device *dev, unsigned int pipe)
{
	drm_via_private_t *dev_priv = dev->dev_private;
	u32 status;

	status = via_read(dev_priv, VIA_REG_INTERRUPT);
	via_write(dev_priv, VIA_REG_INTERRUPT, status & ~VIA_IRQ_VBLANK_ENABLE);

	via_write8(dev_priv, 0x83d4, 0x11);
	via_write8_mask(dev_priv, 0x83d5, 0x30, 0);

	if (pipe != 0)
		DRM_ERROR("%s:  bad crtc %u\n", __func__, pipe);
}

static int
via_driver_irq_wait(struct drm_device *dev, unsigned int irq, int force_sequence,
		    unsigned int *sequence)
{
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;
	unsigned int cur_irq_sequence;
	drm_via_irq_t *cur_irq;
	int ret = 0;
	maskarray_t *masks;
	int real_irq;

	DRM_DEBUG("\n");

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	if (irq >= drm_via_irq_num) {
		DRM_ERROR("Trying to wait on unknown irq %d\n", irq);
		return -EINVAL;
	}

	real_irq = dev_priv->irq_map[irq];

	if (real_irq < 0) {
		DRM_ERROR("Video IRQ %d not available on this hardware.\n",
			  irq);
		return -EINVAL;
	}

	masks = dev_priv->irq_masks;
	cur_irq = dev_priv->via_irqs + real_irq;

	if (masks[real_irq][2] && !force_sequence) {
		VIA_WAIT_ON(ret, cur_irq->irq_queue, 3 * HZ,
			    ((via_read(dev_priv, masks[irq][2]) & masks[irq][3]) ==
			     masks[irq][4]));
		cur_irq_sequence = atomic_read(&cur_irq->irq_received);
	} else {
		VIA_WAIT_ON(ret, cur_irq->irq_queue, 3 * HZ,
			    (((cur_irq_sequence =
			       atomic_read(&cur_irq->irq_received)) -
			      *sequence) <= (1 << 23)));
	}
	*sequence = cur_irq_sequence;
	return ret;
}


/*
 * drm_dma.h hooks
 */

static void via_driver_irq_preinstall(struct drm_device *dev)
{
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;
	u32 status;
	drm_via_irq_t *cur_irq;
	int i;

	DRM_DEBUG("dev_priv: %p\n", dev_priv);
	if (dev_priv) {
		cur_irq = dev_priv->via_irqs;

		dev_priv->irq_enable_mask = VIA_IRQ_VBLANK_ENABLE;
		dev_priv->irq_pending_mask = VIA_IRQ_VBLANK_PENDING;

		if (dev_priv->chipset == VIA_PRO_GROUP_A ||
		    dev_priv->chipset == VIA_DX9_0) {
			dev_priv->irq_masks = via_pro_group_a_irqs;
			dev_priv->num_irqs = via_num_pro_group_a;
			dev_priv->irq_map = via_irqmap_pro_group_a;
		} else {
			dev_priv->irq_masks = via_unichrome_irqs;
			dev_priv->num_irqs = via_num_unichrome;
			dev_priv->irq_map = via_irqmap_unichrome;
		}

		for (i = 0; i < dev_priv->num_irqs; ++i) {
			atomic_set(&cur_irq->irq_received, 0);
			cur_irq->enable_mask = dev_priv->irq_masks[i][0];
			cur_irq->pending_mask = dev_priv->irq_masks[i][1];
			init_waitqueue_head(&cur_irq->irq_queue);
			dev_priv->irq_enable_mask |= cur_irq->enable_mask;
			dev_priv->irq_pending_mask |= cur_irq->pending_mask;
			cur_irq++;

			DRM_DEBUG("Initializing IRQ %d\n", i);
		}

		dev_priv->last_vblank_valid = 0;

		/* Clear VSync interrupt regs */
		status = via_read(dev_priv, VIA_REG_INTERRUPT);
		via_write(dev_priv, VIA_REG_INTERRUPT, status &
			  ~(dev_priv->irq_enable_mask));

		/* Clear bits if they're already high */
		viadrv_acknowledge_irqs(dev_priv);
	}
}

static int via_driver_irq_postinstall(struct drm_device *dev)
{
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;
	u32 status;

	DRM_DEBUG("fun: %s\n", __func__);
	if (!dev_priv)
		return -EINVAL;

	status = via_read(dev_priv, VIA_REG_INTERRUPT);
	via_write(dev_priv, VIA_REG_INTERRUPT, status | VIA_IRQ_GLOBAL
		  | dev_priv->irq_enable_mask);

	/* Some magic, oh for some data sheets ! */
	via_write8(dev_priv, 0x83d4, 0x11);
	via_write8_mask(dev_priv, 0x83d5, 0x30, 0x30);

	return 0;
}

static void via_driver_irq_uninstall(struct drm_device *dev)
{
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;
	u32 status;

	DRM_DEBUG("\n");
	if (dev_priv) {

		/* Some more magic, oh for some data sheets ! */

		via_write8(dev_priv, 0x83d4, 0x11);
		via_write8_mask(dev_priv, 0x83d5, 0x30, 0);

		status = via_read(dev_priv, VIA_REG_INTERRUPT);
		via_write(dev_priv, VIA_REG_INTERRUPT, status &
			  ~(VIA_IRQ_VBLANK_ENABLE | dev_priv->irq_enable_mask));
	}
}

static int via_wait_irq(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_via_irqwait_t *irqwait = data;
	struct timespec64 now;
	int ret = 0;
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;
	drm_via_irq_t *cur_irq = dev_priv->via_irqs;
	int force_sequence;

	if (irqwait->request.irq >= dev_priv->num_irqs) {
		DRM_ERROR("Trying to wait on unknown irq %d\n",
			  irqwait->request.irq);
		return -EINVAL;
	}

	cur_irq += irqwait->request.irq;

	switch (irqwait->request.type & ~VIA_IRQ_FLAGS_MASK) {
	case VIA_IRQ_RELATIVE:
		irqwait->request.sequence +=
			atomic_read(&cur_irq->irq_received);
		irqwait->request.type &= ~_DRM_VBLANK_RELATIVE;
		break;
	case VIA_IRQ_ABSOLUTE:
		break;
	default:
		return -EINVAL;
	}

	if (irqwait->request.type & VIA_IRQ_SIGNAL) {
		DRM_ERROR("Signals on Via IRQs not implemented yet.\n");
		return -EINVAL;
	}

	force_sequence = (irqwait->request.type & VIA_IRQ_FORCE_SEQUENCE);

	ret = via_driver_irq_wait(dev, irqwait->request.irq, force_sequence,
				  &irqwait->request.sequence);
	ktime_get_ts64(&now);
	irqwait->reply.tval_sec = now.tv_sec;
	irqwait->reply.tval_usec = now.tv_nsec / NSEC_PER_USEC;

	return ret;
}

static void via_init_futex(drm_via_private_t *dev_priv)
{
	unsigned int i;

	DRM_DEBUG("\n");

	for (i = 0; i < VIA_NR_XVMC_LOCKS; ++i) {
		init_waitqueue_head(&(dev_priv->decoder_queue[i]));
		XVMCLOCKPTR(dev_priv->sarea_priv, i)->lock = 0;
	}
}

static void via_cleanup_futex(drm_via_private_t *dev_priv)
{
}

static void via_release_futex(drm_via_private_t *dev_priv, int context)
{
	unsigned int i;
	volatile int *lock;

	if (!dev_priv->sarea_priv)
		return;

	for (i = 0; i < VIA_NR_XVMC_LOCKS; ++i) {
		lock = (volatile int *)XVMCLOCKPTR(dev_priv->sarea_priv, i);
		if ((_DRM_LOCKING_CONTEXT(*lock) == context)) {
			if (_DRM_LOCK_IS_HELD(*lock)
			    && (*lock & _DRM_LOCK_CONT)) {
				wake_up(&(dev_priv->decoder_queue[i]));
			}
			*lock = 0;
		}
	}
}

static int via_decoder_futex(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_via_futex_t *fx = data;
	volatile int *lock;
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;
	drm_via_sarea_t *sAPriv = dev_priv->sarea_priv;
	int ret = 0;

	DRM_DEBUG("\n");

	if (fx->lock >= VIA_NR_XVMC_LOCKS)
		return -EFAULT;

	lock = (volatile int *)XVMCLOCKPTR(sAPriv, fx->lock);

	switch (fx->func) {
	case VIA_FUTEX_WAIT:
		VIA_WAIT_ON(ret, dev_priv->decoder_queue[fx->lock],
			    (fx->ms / 10) * (HZ / 100), *lock != fx->val);
		return ret;
	case VIA_FUTEX_WAKE:
		wake_up(&(dev_priv->decoder_queue[fx->lock]));
		return 0;
	}
	return 0;
}

static int via_agp_init(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_via_agp_t *agp = data;
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;

	mutex_lock(&dev->struct_mutex);
	drm_mm_init(&dev_priv->agp_mm, 0, agp->size >> VIA_MM_ALIGN_SHIFT);

	dev_priv->agp_initialized = 1;
	dev_priv->agp_offset = agp->offset;
	mutex_unlock(&dev->struct_mutex);

	DRM_DEBUG("offset = %u, size = %u\n", agp->offset, agp->size);
	return 0;
}

static int via_fb_init(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_via_fb_t *fb = data;
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;

	mutex_lock(&dev->struct_mutex);
	drm_mm_init(&dev_priv->vram_mm, 0, fb->size >> VIA_MM_ALIGN_SHIFT);

	dev_priv->vram_initialized = 1;
	dev_priv->vram_offset = fb->offset;

	mutex_unlock(&dev->struct_mutex);
	DRM_DEBUG("offset = %u, size = %u\n", fb->offset, fb->size);

	return 0;

}

static int via_final_context(struct drm_device *dev, int context)
{
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;

	via_release_futex(dev_priv, context);

	/* Linux specific until context tracking code gets ported to BSD */
	/* Last context, perform cleanup */
	if (list_is_singular(&dev->ctxlist)) {
		DRM_DEBUG("Last Context\n");
		drm_legacy_irq_uninstall(dev);
		via_cleanup_futex(dev_priv);
		via_do_cleanup_map(dev);
	}
	return 1;
}

static void via_lastclose(struct drm_device *dev)
{
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;

	if (!dev_priv)
		return;

	mutex_lock(&dev->struct_mutex);
	if (dev_priv->vram_initialized) {
		drm_mm_takedown(&dev_priv->vram_mm);
		dev_priv->vram_initialized = 0;
	}
	if (dev_priv->agp_initialized) {
		drm_mm_takedown(&dev_priv->agp_mm);
		dev_priv->agp_initialized = 0;
	}
	mutex_unlock(&dev->struct_mutex);
}

static int via_mem_alloc(struct drm_device *dev, void *data,
		  struct drm_file *file)
{
	drm_via_mem_t *mem = data;
	int retval = 0, user_key;
	struct via_memblock *item;
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;
	struct via_file_private *file_priv = file->driver_priv;
	unsigned long tmpSize;

	if (mem->type > VIA_MEM_AGP) {
		DRM_ERROR("Unknown memory type allocation\n");
		return -EINVAL;
	}
	mutex_lock(&dev->struct_mutex);
	if (0 == ((mem->type == VIA_MEM_VIDEO) ? dev_priv->vram_initialized :
		      dev_priv->agp_initialized)) {
		mutex_unlock(&dev->struct_mutex);
		DRM_ERROR
		    ("Attempt to allocate from uninitialized memory manager.\n");
		return -EINVAL;
	}

	item = kzalloc(sizeof(*item), GFP_KERNEL);
	if (!item) {
		retval = -ENOMEM;
		goto fail_alloc;
	}

	tmpSize = (mem->size + VIA_MM_ALIGN_MASK) >> VIA_MM_ALIGN_SHIFT;
	if (mem->type == VIA_MEM_AGP)
		retval = drm_mm_insert_node(&dev_priv->agp_mm,
					    &item->mm_node,
					    tmpSize);
	else
		retval = drm_mm_insert_node(&dev_priv->vram_mm,
					    &item->mm_node,
					    tmpSize);
	if (retval)
		goto fail_alloc;

	retval = idr_alloc(&dev_priv->object_idr, item, 1, 0, GFP_KERNEL);
	if (retval < 0)
		goto fail_idr;
	user_key = retval;

	list_add(&item->owner_list, &file_priv->obj_list);
	mutex_unlock(&dev->struct_mutex);

	mem->offset = ((mem->type == VIA_MEM_VIDEO) ?
		      dev_priv->vram_offset : dev_priv->agp_offset) +
	    ((item->mm_node.start) << VIA_MM_ALIGN_SHIFT);
	mem->index = user_key;

	return 0;

fail_idr:
	drm_mm_remove_node(&item->mm_node);
fail_alloc:
	kfree(item);
	mutex_unlock(&dev->struct_mutex);

	mem->offset = 0;
	mem->size = 0;
	mem->index = 0;
	DRM_DEBUG("Video memory allocation failed\n");

	return retval;
}

static int via_mem_free(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_via_private_t *dev_priv = dev->dev_private;
	drm_via_mem_t *mem = data;
	struct via_memblock *obj;

	mutex_lock(&dev->struct_mutex);
	obj = idr_find(&dev_priv->object_idr, mem->index);
	if (obj == NULL) {
		mutex_unlock(&dev->struct_mutex);
		return -EINVAL;
	}

	idr_remove(&dev_priv->object_idr, mem->index);
	list_del(&obj->owner_list);
	drm_mm_remove_node(&obj->mm_node);
	kfree(obj);
	mutex_unlock(&dev->struct_mutex);

	DRM_DEBUG("free = 0x%lx\n", mem->index);

	return 0;
}


static void via_reclaim_buffers_locked(struct drm_device *dev,
				struct drm_file *file)
{
	struct via_file_private *file_priv = file->driver_priv;
	struct via_memblock *entry, *next;

	if (!(dev->master && file->master->lock.hw_lock))
		return;

	drm_legacy_idlelock_take(&file->master->lock);

	mutex_lock(&dev->struct_mutex);
	if (list_empty(&file_priv->obj_list)) {
		mutex_unlock(&dev->struct_mutex);
		drm_legacy_idlelock_release(&file->master->lock);

		return;
	}

	via_driver_dma_quiescent(dev);

	list_for_each_entry_safe(entry, next, &file_priv->obj_list,
				 owner_list) {
		list_del(&entry->owner_list);
		drm_mm_remove_node(&entry->mm_node);
		kfree(entry);
	}
	mutex_unlock(&dev->struct_mutex);

	drm_legacy_idlelock_release(&file->master->lock);

	return;
}

static int via_do_init_map(struct drm_device *dev, drm_via_init_t *init)
{
	drm_via_private_t *dev_priv = dev->dev_private;

	DRM_DEBUG("\n");

	dev_priv->sarea = drm_legacy_getsarea(dev);
	if (!dev_priv->sarea) {
		DRM_ERROR("could not find sarea!\n");
		dev->dev_private = (void *)dev_priv;
		via_do_cleanup_map(dev);
		return -EINVAL;
	}

	dev_priv->fb = drm_legacy_findmap(dev, init->fb_offset);
	if (!dev_priv->fb) {
		DRM_ERROR("could not find framebuffer!\n");
		dev->dev_private = (void *)dev_priv;
		via_do_cleanup_map(dev);
		return -EINVAL;
	}
	dev_priv->mmio = drm_legacy_findmap(dev, init->mmio_offset);
	if (!dev_priv->mmio) {
		DRM_ERROR("could not find mmio region!\n");
		dev->dev_private = (void *)dev_priv;
		via_do_cleanup_map(dev);
		return -EINVAL;
	}

	dev_priv->sarea_priv =
	    (drm_via_sarea_t *) ((u8 *) dev_priv->sarea->handle +
				 init->sarea_priv_offset);

	dev_priv->agpAddr = init->agpAddr;

	via_init_futex(dev_priv);

	via_init_dmablit(dev);

	dev->dev_private = (void *)dev_priv;
	return 0;
}

int via_do_cleanup_map(struct drm_device *dev)
{
	via_dma_cleanup(dev);

	return 0;
}

static int via_map_init(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_via_init_t *init = data;

	DRM_DEBUG("\n");

	switch (init->func) {
	case VIA_INIT_MAP:
		return via_do_init_map(dev, init);
	case VIA_CLEANUP_MAP:
		return via_do_cleanup_map(dev);
	}

	return -EINVAL;
}

static int via_driver_load(struct drm_device *dev, unsigned long chipset)
{
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	drm_via_private_t *dev_priv;
	int ret = 0;

	dev_priv = kzalloc(sizeof(drm_via_private_t), GFP_KERNEL);
	if (dev_priv == NULL)
		return -ENOMEM;

	idr_init(&dev_priv->object_idr);
	dev->dev_private = (void *)dev_priv;

	dev_priv->chipset = chipset;

	pci_set_master(pdev);

	ret = drm_vblank_init(dev, 1);
	if (ret) {
		kfree(dev_priv);
		return ret;
	}

	return 0;
}

static void via_driver_unload(struct drm_device *dev)
{
	drm_via_private_t *dev_priv = dev->dev_private;

	idr_destroy(&dev_priv->object_idr);

	kfree(dev_priv);
}

static void via_cmdbuf_start(drm_via_private_t *dev_priv);
static void via_cmdbuf_pause(drm_via_private_t *dev_priv);
static void via_cmdbuf_reset(drm_via_private_t *dev_priv);
static void via_cmdbuf_rewind(drm_via_private_t *dev_priv);
static int via_wait_idle(drm_via_private_t *dev_priv);
static void via_pad_cache(drm_via_private_t *dev_priv, int qwords);

/*
 * Free space in command buffer.
 */

static uint32_t via_cmdbuf_space(drm_via_private_t *dev_priv)
{
	uint32_t agp_base = dev_priv->dma_offset + (uint32_t) dev_priv->agpAddr;
	uint32_t hw_addr = *(dev_priv->hw_addr_ptr) - agp_base;

	return ((hw_addr <= dev_priv->dma_low) ?
		(dev_priv->dma_high + hw_addr - dev_priv->dma_low) :
		(hw_addr - dev_priv->dma_low));
}

/*
 * How much does the command regulator lag behind?
 */

static uint32_t via_cmdbuf_lag(drm_via_private_t *dev_priv)
{
	uint32_t agp_base = dev_priv->dma_offset + (uint32_t) dev_priv->agpAddr;
	uint32_t hw_addr = *(dev_priv->hw_addr_ptr) - agp_base;

	return ((hw_addr <= dev_priv->dma_low) ?
		(dev_priv->dma_low - hw_addr) :
		(dev_priv->dma_wrap + dev_priv->dma_low - hw_addr));
}

/*
 * Check that the given size fits in the buffer, otherwise wait.
 */

static inline int
via_cmdbuf_wait(drm_via_private_t *dev_priv, unsigned int size)
{
	uint32_t agp_base = dev_priv->dma_offset + (uint32_t) dev_priv->agpAddr;
	uint32_t cur_addr, hw_addr, next_addr;
	volatile uint32_t *hw_addr_ptr;
	uint32_t count;
	hw_addr_ptr = dev_priv->hw_addr_ptr;
	cur_addr = dev_priv->dma_low;
	next_addr = cur_addr + size + 512 * 1024;
	count = 1000000;
	do {
		hw_addr = *hw_addr_ptr - agp_base;
		if (count-- == 0) {
			DRM_ERROR
			    ("via_cmdbuf_wait timed out hw %x cur_addr %x next_addr %x\n",
			     hw_addr, cur_addr, next_addr);
			return -1;
		}
		if  ((cur_addr < hw_addr) && (next_addr >= hw_addr))
			msleep(1);
	} while ((cur_addr < hw_addr) && (next_addr >= hw_addr));
	return 0;
}

/*
 * Checks whether buffer head has reach the end. Rewind the ring buffer
 * when necessary.
 *
 * Returns virtual pointer to ring buffer.
 */

static inline uint32_t *via_check_dma(drm_via_private_t * dev_priv,
				      unsigned int size)
{
	if ((dev_priv->dma_low + size + 4 * CMDBUF_ALIGNMENT_SIZE) >
	    dev_priv->dma_high) {
		via_cmdbuf_rewind(dev_priv);
	}
	if (via_cmdbuf_wait(dev_priv, size) != 0)
		return NULL;

	return (uint32_t *) (dev_priv->dma_ptr + dev_priv->dma_low);
}

int via_dma_cleanup(struct drm_device *dev)
{
	if (dev->dev_private) {
		drm_via_private_t *dev_priv =
		    (drm_via_private_t *) dev->dev_private;

		if (dev_priv->ring.virtual_start) {
			via_cmdbuf_reset(dev_priv);

			drm_legacy_ioremapfree(&dev_priv->ring.map, dev);
			dev_priv->ring.virtual_start = NULL;
		}

	}

	return 0;
}

static int via_initialize(struct drm_device *dev,
			  drm_via_private_t *dev_priv,
			  drm_via_dma_init_t *init)
{
	if (!dev_priv || !dev_priv->mmio) {
		DRM_ERROR("via_dma_init called before via_map_init\n");
		return -EFAULT;
	}

	if (dev_priv->ring.virtual_start != NULL) {
		DRM_ERROR("called again without calling cleanup\n");
		return -EFAULT;
	}

	if (!dev->agp || !dev->agp->base) {
		DRM_ERROR("called with no agp memory available\n");
		return -EFAULT;
	}

	if (dev_priv->chipset == VIA_DX9_0) {
		DRM_ERROR("AGP DMA is not supported on this chip\n");
		return -EINVAL;
	}

	dev_priv->ring.map.offset = dev->agp->base + init->offset;
	dev_priv->ring.map.size = init->size;
	dev_priv->ring.map.type = 0;
	dev_priv->ring.map.flags = 0;
	dev_priv->ring.map.mtrr = 0;

	drm_legacy_ioremap(&dev_priv->ring.map, dev);

	if (dev_priv->ring.map.handle == NULL) {
		via_dma_cleanup(dev);
		DRM_ERROR("can not ioremap virtual address for"
			  " ring buffer\n");
		return -ENOMEM;
	}

	dev_priv->ring.virtual_start = dev_priv->ring.map.handle;

	dev_priv->dma_ptr = dev_priv->ring.virtual_start;
	dev_priv->dma_low = 0;
	dev_priv->dma_high = init->size;
	dev_priv->dma_wrap = init->size;
	dev_priv->dma_offset = init->offset;
	dev_priv->last_pause_ptr = NULL;
	dev_priv->hw_addr_ptr =
		(volatile uint32_t *)((char *)dev_priv->mmio->handle +
		init->reg_pause_addr);

	via_cmdbuf_start(dev_priv);

	return 0;
}

static int via_dma_init(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_via_private_t *dev_priv = (drm_via_private_t *) dev->dev_private;
	drm_via_dma_init_t *init = data;
	int retcode = 0;

	switch (init->func) {
	case VIA_INIT_DMA:
		if (!capable(CAP_SYS_ADMIN))
			retcode = -EPERM;
		else
			retcode = via_initialize(dev, dev_priv, init);
		break;
	case VIA_CLEANUP_DMA:
		if (!capable(CAP_SYS_ADMIN))
			retcode = -EPERM;
		else
			retcode = via_dma_cleanup(dev);
		break;
	case VIA_DMA_INITIALIZED:
		retcode = (dev_priv->ring.virtual_start != NULL) ?
			0 : -EFAULT;
		break;
	default:
		retcode = -EINVAL;
		break;
	}

	return retcode;
}

static int via_dispatch_cmdbuffer(struct drm_device *dev, drm_via_cmdbuffer_t *cmd)
{
	drm_via_private_t *dev_priv;
	uint32_t *vb;
	int ret;

	dev_priv = (drm_via_private_t *) dev->dev_private;

	if (dev_priv->ring.virtual_start == NULL) {
		DRM_ERROR("called without initializing AGP ring buffer.\n");
		return -EFAULT;
	}

	if (cmd->size > VIA_PCI_BUF_SIZE)
		return -ENOMEM;

	if (copy_from_user(dev_priv->pci_buf, cmd->buf, cmd->size))
		return -EFAULT;

	/*
	 * Running this function on AGP memory is dead slow. Therefore
	 * we run it on a temporary cacheable system memory buffer and
	 * copy it to AGP memory when ready.
	 */

	if ((ret =
	     via_verify_command_stream((uint32_t *) dev_priv->pci_buf,
				       cmd->size, dev, 1))) {
		return ret;
	}

	vb = via_check_dma(dev_priv, (cmd->size < 0x100) ? 0x102 : cmd->size);
	if (vb == NULL)
		return -EAGAIN;

	memcpy(vb, dev_priv->pci_buf, cmd->size);

	dev_priv->dma_low += cmd->size;

	/*
	 * Small submissions somehow stalls the CPU. (AGP cache effects?)
	 * pad to greater size.
	 */

	if (cmd->size < 0x100)
		via_pad_cache(dev_priv, (0x100 - cmd->size) >> 3);
	via_cmdbuf_pause(dev_priv);

	return 0;
}

int via_driver_dma_quiescent(struct drm_device *dev)
{
	drm_via_private_t *dev_priv = dev->dev_private;

	if (!via_wait_idle(dev_priv))
		return -EBUSY;
	return 0;
}

static int via_flush_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv)
{

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	return via_driver_dma_quiescent(dev);
}

static int via_cmdbuffer(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_via_cmdbuffer_t *cmdbuf = data;
	int ret;

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	DRM_DEBUG("buf %p size %lu\n", cmdbuf->buf, cmdbuf->size);

	ret = via_dispatch_cmdbuffer(dev, cmdbuf);
	return ret;
}

static int via_dispatch_pci_cmdbuffer(struct drm_device *dev,
				      drm_via_cmdbuffer_t *cmd)
{
	drm_via_private_t *dev_priv = dev->dev_private;
	int ret;

	if (cmd->size > VIA_PCI_BUF_SIZE)
		return -ENOMEM;
	if (copy_from_user(dev_priv->pci_buf, cmd->buf, cmd->size))
		return -EFAULT;

	if ((ret =
	     via_verify_command_stream((uint32_t *) dev_priv->pci_buf,
				       cmd->size, dev, 0))) {
		return ret;
	}

	ret =
	    via_parse_command_stream(dev, (const uint32_t *)dev_priv->pci_buf,
				     cmd->size);
	return ret;
}

static int via_pci_cmdbuffer(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_via_cmdbuffer_t *cmdbuf = data;
	int ret;

	LOCK_TEST_WITH_RETURN(dev, file_priv);

	DRM_DEBUG("buf %p size %lu\n", cmdbuf->buf, cmdbuf->size);

	ret = via_dispatch_pci_cmdbuffer(dev, cmdbuf);
	return ret;
}

static inline uint32_t *via_align_buffer(drm_via_private_t *dev_priv,
					 uint32_t * vb, int qw_count)
{
	for (; qw_count > 0; --qw_count)
		VIA_OUT_RING_QW(HC_DUMMY, HC_DUMMY);
	return vb;
}

/*
 * This function is used internally by ring buffer management code.
 *
 * Returns virtual pointer to ring buffer.
 */
static inline uint32_t *via_get_dma(drm_via_private_t *dev_priv)
{
	return (uint32_t *) (dev_priv->dma_ptr + dev_priv->dma_low);
}

/*
 * Hooks a segment of data into the tail of the ring-buffer by
 * modifying the pause address stored in the buffer itself. If
 * the regulator has already paused, restart it.
 */
static int via_hook_segment(drm_via_private_t *dev_priv,
			    uint32_t pause_addr_hi, uint32_t pause_addr_lo,
			    int no_pci_fire)
{
	int paused, count;
	volatile uint32_t *paused_at = dev_priv->last_pause_ptr;
	uint32_t reader, ptr;
	uint32_t diff;

	paused = 0;
	via_flush_write_combine();
	(void) *(volatile uint32_t *)(via_get_dma(dev_priv) - 1);

	*paused_at = pause_addr_lo;
	via_flush_write_combine();
	(void) *paused_at;

	reader = *(dev_priv->hw_addr_ptr);
	ptr = ((volatile char *)paused_at - dev_priv->dma_ptr) +
		dev_priv->dma_offset + (uint32_t) dev_priv->agpAddr + 4;

	dev_priv->last_pause_ptr = via_get_dma(dev_priv) - 1;

	/*
	 * If there is a possibility that the command reader will
	 * miss the new pause address and pause on the old one,
	 * In that case we need to program the new start address
	 * using PCI.
	 */

	diff = (uint32_t) (ptr - reader) - dev_priv->dma_diff;
	count = 10000000;
	while (diff == 0 && count--) {
		paused = (via_read(dev_priv, 0x41c) & 0x80000000);
		if (paused)
			break;
		reader = *(dev_priv->hw_addr_ptr);
		diff = (uint32_t) (ptr - reader) - dev_priv->dma_diff;
	}

	paused = via_read(dev_priv, 0x41c) & 0x80000000;

	if (paused && !no_pci_fire) {
		reader = *(dev_priv->hw_addr_ptr);
		diff = (uint32_t) (ptr - reader) - dev_priv->dma_diff;
		diff &= (dev_priv->dma_high - 1);
		if (diff != 0 && diff < (dev_priv->dma_high >> 1)) {
			DRM_ERROR("Paused at incorrect address. "
				  "0x%08x, 0x%08x 0x%08x\n",
				  ptr, reader, dev_priv->dma_diff);
		} else if (diff == 0) {
			/*
			 * There is a concern that these writes may stall the PCI bus
			 * if the GPU is not idle. However, idling the GPU first
			 * doesn't make a difference.
			 */

			via_write(dev_priv, VIA_REG_TRANSET, (HC_ParaType_PreCR << 16));
			via_write(dev_priv, VIA_REG_TRANSPACE, pause_addr_hi);
			via_write(dev_priv, VIA_REG_TRANSPACE, pause_addr_lo);
			via_read(dev_priv, VIA_REG_TRANSPACE);
		}
	}
	return paused;
}

static int via_wait_idle(drm_via_private_t *dev_priv)
{
	int count = 10000000;

	while (!(via_read(dev_priv, VIA_REG_STATUS) & VIA_VR_QUEUE_BUSY) && --count)
		;

	while (count && (via_read(dev_priv, VIA_REG_STATUS) &
			   (VIA_CMD_RGTR_BUSY | VIA_2D_ENG_BUSY |
			    VIA_3D_ENG_BUSY)))
		--count;
	return count;
}

static uint32_t *via_align_cmd(drm_via_private_t *dev_priv, uint32_t cmd_type,
			       uint32_t addr, uint32_t *cmd_addr_hi,
			       uint32_t *cmd_addr_lo, int skip_wait)
{
	uint32_t agp_base;
	uint32_t cmd_addr, addr_lo, addr_hi;
	uint32_t *vb;
	uint32_t qw_pad_count;

	if (!skip_wait)
		via_cmdbuf_wait(dev_priv, 2 * CMDBUF_ALIGNMENT_SIZE);

	vb = via_get_dma(dev_priv);
	VIA_OUT_RING_QW(HC_HEADER2 | ((VIA_REG_TRANSET >> 2) << 12) |
			(VIA_REG_TRANSPACE >> 2), HC_ParaType_PreCR << 16);
	agp_base = dev_priv->dma_offset + (uint32_t) dev_priv->agpAddr;
	qw_pad_count = (CMDBUF_ALIGNMENT_SIZE >> 3) -
	    ((dev_priv->dma_low & CMDBUF_ALIGNMENT_MASK) >> 3);

	cmd_addr = (addr) ? addr :
	    agp_base + dev_priv->dma_low - 8 + (qw_pad_count << 3);
	addr_lo = ((HC_SubA_HAGPBpL << 24) | (cmd_type & HC_HAGPBpID_MASK) |
		   (cmd_addr & HC_HAGPBpL_MASK));
	addr_hi = ((HC_SubA_HAGPBpH << 24) | (cmd_addr >> 24));

	vb = via_align_buffer(dev_priv, vb, qw_pad_count - 1);
	VIA_OUT_RING_QW(*cmd_addr_hi = addr_hi, *cmd_addr_lo = addr_lo);
	return vb;
}

static void via_cmdbuf_start(drm_via_private_t *dev_priv)
{
	uint32_t pause_addr_lo, pause_addr_hi;
	uint32_t start_addr, start_addr_lo;
	uint32_t end_addr, end_addr_lo;
	uint32_t command;
	uint32_t agp_base;
	uint32_t ptr;
	uint32_t reader;
	int count;

	dev_priv->dma_low = 0;

	agp_base = dev_priv->dma_offset + (uint32_t) dev_priv->agpAddr;
	start_addr = agp_base;
	end_addr = agp_base + dev_priv->dma_high;

	start_addr_lo = ((HC_SubA_HAGPBstL << 24) | (start_addr & 0xFFFFFF));
	end_addr_lo = ((HC_SubA_HAGPBendL << 24) | (end_addr & 0xFFFFFF));
	command = ((HC_SubA_HAGPCMNT << 24) | (start_addr >> 24) |
		   ((end_addr & 0xff000000) >> 16));

	dev_priv->last_pause_ptr =
	    via_align_cmd(dev_priv, HC_HAGPBpID_PAUSE, 0,
			  &pause_addr_hi, &pause_addr_lo, 1) - 1;

	via_flush_write_combine();
	(void) *(volatile uint32_t *)dev_priv->last_pause_ptr;

	via_write(dev_priv, VIA_REG_TRANSET, (HC_ParaType_PreCR << 16));
	via_write(dev_priv, VIA_REG_TRANSPACE, command);
	via_write(dev_priv, VIA_REG_TRANSPACE, start_addr_lo);
	via_write(dev_priv, VIA_REG_TRANSPACE, end_addr_lo);

	via_write(dev_priv, VIA_REG_TRANSPACE, pause_addr_hi);
	via_write(dev_priv, VIA_REG_TRANSPACE, pause_addr_lo);
	wmb();
	via_write(dev_priv, VIA_REG_TRANSPACE, command | HC_HAGPCMNT_MASK);
	via_read(dev_priv, VIA_REG_TRANSPACE);

	dev_priv->dma_diff = 0;

	count = 10000000;
	while (!(via_read(dev_priv, 0x41c) & 0x80000000) && count--);

	reader = *(dev_priv->hw_addr_ptr);
	ptr = ((volatile char *)dev_priv->last_pause_ptr - dev_priv->dma_ptr) +
	    dev_priv->dma_offset + (uint32_t) dev_priv->agpAddr + 4;

	/*
	 * This is the difference between where we tell the
	 * command reader to pause and where it actually pauses.
	 * This differs between hw implementation so we need to
	 * detect it.
	 */

	dev_priv->dma_diff = ptr - reader;
}

static void via_pad_cache(drm_via_private_t *dev_priv, int qwords)
{
	uint32_t *vb;

	via_cmdbuf_wait(dev_priv, qwords + 2);
	vb = via_get_dma(dev_priv);
	VIA_OUT_RING_QW(HC_HEADER2, HC_ParaType_NotTex << 16);
	via_align_buffer(dev_priv, vb, qwords);
}

static inline void via_dummy_bitblt(drm_via_private_t *dev_priv)
{
	uint32_t *vb = via_get_dma(dev_priv);
	SetReg2DAGP(0x0C, (0 | (0 << 16)));
	SetReg2DAGP(0x10, 0 | (0 << 16));
	SetReg2DAGP(0x0, 0x1 | 0x2000 | 0xAA000000);
}

static void via_cmdbuf_jump(drm_via_private_t *dev_priv)
{
	uint32_t pause_addr_lo, pause_addr_hi;
	uint32_t jump_addr_lo, jump_addr_hi;
	volatile uint32_t *last_pause_ptr;
	uint32_t dma_low_save1, dma_low_save2;

	via_align_cmd(dev_priv, HC_HAGPBpID_JUMP, 0, &jump_addr_hi,
		      &jump_addr_lo, 0);

	dev_priv->dma_wrap = dev_priv->dma_low;

	/*
	 * Wrap command buffer to the beginning.
	 */

	dev_priv->dma_low = 0;
	if (via_cmdbuf_wait(dev_priv, CMDBUF_ALIGNMENT_SIZE) != 0)
		DRM_ERROR("via_cmdbuf_jump failed\n");

	via_dummy_bitblt(dev_priv);
	via_dummy_bitblt(dev_priv);

	last_pause_ptr =
	    via_align_cmd(dev_priv, HC_HAGPBpID_PAUSE, 0, &pause_addr_hi,
			  &pause_addr_lo, 0) - 1;
	via_align_cmd(dev_priv, HC_HAGPBpID_PAUSE, 0, &pause_addr_hi,
		      &pause_addr_lo, 0);

	*last_pause_ptr = pause_addr_lo;
	dma_low_save1 = dev_priv->dma_low;

	/*
	 * Now, set a trap that will pause the regulator if it tries to rerun the old
	 * command buffer. (Which may happen if via_hook_segment detecs a command regulator pause
	 * and reissues the jump command over PCI, while the regulator has already taken the jump
	 * and actually paused at the current buffer end).
	 * There appears to be no other way to detect this condition, since the hw_addr_pointer
	 * does not seem to get updated immediately when a jump occurs.
	 */

	last_pause_ptr =
		via_align_cmd(dev_priv, HC_HAGPBpID_PAUSE, 0, &pause_addr_hi,
			      &pause_addr_lo, 0) - 1;
	via_align_cmd(dev_priv, HC_HAGPBpID_PAUSE, 0, &pause_addr_hi,
		      &pause_addr_lo, 0);
	*last_pause_ptr = pause_addr_lo;

	dma_low_save2 = dev_priv->dma_low;
	dev_priv->dma_low = dma_low_save1;
	via_hook_segment(dev_priv, jump_addr_hi, jump_addr_lo, 0);
	dev_priv->dma_low = dma_low_save2;
	via_hook_segment(dev_priv, pause_addr_hi, pause_addr_lo, 0);
}


static void via_cmdbuf_rewind(drm_via_private_t *dev_priv)
{
	via_cmdbuf_jump(dev_priv);
}

static void via_cmdbuf_flush(drm_via_private_t *dev_priv, uint32_t cmd_type)
{
	uint32_t pause_addr_lo, pause_addr_hi;

	via_align_cmd(dev_priv, cmd_type, 0, &pause_addr_hi, &pause_addr_lo, 0);
	via_hook_segment(dev_priv, pause_addr_hi, pause_addr_lo, 0);
}

static void via_cmdbuf_pause(drm_via_private_t *dev_priv)
{
	via_cmdbuf_flush(dev_priv, HC_HAGPBpID_PAUSE);
}

static void via_cmdbuf_reset(drm_via_private_t *dev_priv)
{
	via_cmdbuf_flush(dev_priv, HC_HAGPBpID_STOP);
	via_wait_idle(dev_priv);
}

/*
 * User interface to the space and lag functions.
 */

static int via_cmdbuf_size(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	drm_via_cmdbuf_size_t *d_siz = data;
	int ret = 0;
	uint32_t tmp_size, count;
	drm_via_private_t *dev_priv;

	DRM_DEBUG("\n");
	LOCK_TEST_WITH_RETURN(dev, file_priv);

	dev_priv = (drm_via_private_t *) dev->dev_private;

	if (dev_priv->ring.virtual_start == NULL) {
		DRM_ERROR("called without initializing AGP ring buffer.\n");
		return -EFAULT;
	}

	count = 1000000;
	tmp_size = d_siz->size;
	switch (d_siz->func) {
	case VIA_CMDBUF_SPACE:
		while (((tmp_size = via_cmdbuf_space(dev_priv)) < d_siz->size)
		       && --count) {
			if (!d_siz->wait)
				break;
		}
		if (!count) {
			DRM_ERROR("VIA_CMDBUF_SPACE timed out.\n");
			ret = -EAGAIN;
		}
		break;
	case VIA_CMDBUF_LAG:
		while (((tmp_size = via_cmdbuf_lag(dev_priv)) > d_siz->size)
		       && --count) {
			if (!d_siz->wait)
				break;
		}
		if (!count) {
			DRM_ERROR("VIA_CMDBUF_LAG timed out.\n");
			ret = -EAGAIN;
		}
		break;
	default:
		ret = -EFAULT;
	}
	d_siz->size = tmp_size;

	return ret;
}

static const struct drm_ioctl_desc via_ioctls[] = {
	DRM_IOCTL_DEF_DRV(VIA_ALLOCMEM, via_mem_alloc, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_FREEMEM, via_mem_free, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_AGP_INIT, via_agp_init, DRM_AUTH|DRM_MASTER),
	DRM_IOCTL_DEF_DRV(VIA_FB_INIT, via_fb_init, DRM_AUTH|DRM_MASTER),
	DRM_IOCTL_DEF_DRV(VIA_MAP_INIT, via_map_init, DRM_AUTH|DRM_MASTER),
	DRM_IOCTL_DEF_DRV(VIA_DEC_FUTEX, via_decoder_futex, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_DMA_INIT, via_dma_init, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_CMDBUFFER, via_cmdbuffer, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_FLUSH, via_flush_ioctl, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_PCICMD, via_pci_cmdbuffer, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_CMDBUF_SIZE, via_cmdbuf_size, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_WAIT_IRQ, via_wait_irq, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_DMA_BLIT, via_dma_blit, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_BLIT_SYNC, via_dma_blit_sync, DRM_AUTH)
};

static int via_max_ioctl = ARRAY_SIZE(via_ioctls);
static int via_driver_open(struct drm_device *dev, struct drm_file *file)
{
	struct via_file_private *file_priv;

	DRM_DEBUG_DRIVER("\n");
	file_priv = kmalloc(sizeof(*file_priv), GFP_KERNEL);
	if (!file_priv)
		return -ENOMEM;

	file->driver_priv = file_priv;

	INIT_LIST_HEAD(&file_priv->obj_list);

	return 0;
}

static void via_driver_postclose(struct drm_device *dev, struct drm_file *file)
{
	struct via_file_private *file_priv = file->driver_priv;

	kfree(file_priv);
}

static struct pci_device_id pciidlist[] = {
	viadrv_PCI_IDS
};

static const struct file_operations via_driver_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = drm_ioctl,
	.mmap = drm_legacy_mmap,
	.poll = drm_poll,
	.compat_ioctl = drm_compat_ioctl,
	.llseek = noop_llseek,
};

static struct drm_driver driver = {
	.driver_features =
	    DRIVER_USE_AGP | DRIVER_HAVE_IRQ | DRIVER_LEGACY,
	.load = via_driver_load,
	.unload = via_driver_unload,
	.open = via_driver_open,
	.preclose = via_reclaim_buffers_locked,
	.postclose = via_driver_postclose,
	.context_dtor = via_final_context,
	.get_vblank_counter = via_get_vblank_counter,
	.enable_vblank = via_enable_vblank,
	.disable_vblank = via_disable_vblank,
	.irq_preinstall = via_driver_irq_preinstall,
	.irq_postinstall = via_driver_irq_postinstall,
	.irq_uninstall = via_driver_irq_uninstall,
	.irq_handler = via_driver_irq_handler,
	.dma_quiescent = via_driver_dma_quiescent,
	.lastclose = via_lastclose,
	.ioctls = via_ioctls,
	.fops = &via_driver_fops,
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};

static struct pci_driver via_pci_driver = {
	.name = DRIVER_NAME,
	.id_table = pciidlist,
};

static int __init via_init(void)
{
	driver.num_ioctls = via_max_ioctl;
	via_init_command_verifier();
	return drm_legacy_pci_init(&driver, &via_pci_driver);
}

static void __exit via_exit(void)
{
	drm_legacy_pci_exit(&driver, &via_pci_driver);
}

module_init(via_init);
module_exit(via_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
