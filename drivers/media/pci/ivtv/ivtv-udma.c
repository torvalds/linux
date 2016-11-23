/*
    User DMA

    Copyright (C) 2003-2004  Kevin Thayer <nufan_wfk at yahoo.com>
    Copyright (C) 2004  Chris Kennedy <c@groovy.org>
    Copyright (C) 2005-2007  Hans Verkuil <hverkuil@xs4all.nl>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "ivtv-driver.h"
#include "ivtv-udma.h"

void ivtv_udma_get_page_info(struct ivtv_dma_page_info *dma_page, unsigned long first, unsigned long size)
{
	dma_page->uaddr = first & PAGE_MASK;
	dma_page->offset = first & ~PAGE_MASK;
	dma_page->tail = 1 + ((first+size-1) & ~PAGE_MASK);
	dma_page->first = (first & PAGE_MASK) >> PAGE_SHIFT;
	dma_page->last = ((first+size-1) & PAGE_MASK) >> PAGE_SHIFT;
	dma_page->page_count = dma_page->last - dma_page->first + 1;
	if (dma_page->page_count == 1) dma_page->tail -= dma_page->offset;
}

int ivtv_udma_fill_sg_list (struct ivtv_user_dma *dma, struct ivtv_dma_page_info *dma_page, int map_offset)
{
	int i, offset;
	unsigned long flags;

	if (map_offset < 0)
		return map_offset;

	offset = dma_page->offset;

	/* Fill SG Array with new values */
	for (i = 0; i < dma_page->page_count; i++) {
		unsigned int len = (i == dma_page->page_count - 1) ?
			dma_page->tail : PAGE_SIZE - offset;

		if (PageHighMem(dma->map[map_offset])) {
			void *src;

			if (dma->bouncemap[map_offset] == NULL)
				dma->bouncemap[map_offset] = alloc_page(GFP_KERNEL);
			if (dma->bouncemap[map_offset] == NULL)
				return -1;
			local_irq_save(flags);
			src = kmap_atomic(dma->map[map_offset]) + offset;
			memcpy(page_address(dma->bouncemap[map_offset]) + offset, src, len);
			kunmap_atomic(src);
			local_irq_restore(flags);
			sg_set_page(&dma->SGlist[map_offset], dma->bouncemap[map_offset], len, offset);
		}
		else {
			sg_set_page(&dma->SGlist[map_offset], dma->map[map_offset], len, offset);
		}
		offset = 0;
		map_offset++;
	}
	return map_offset;
}

void ivtv_udma_fill_sg_array (struct ivtv_user_dma *dma, u32 buffer_offset, u32 buffer_offset_2, u32 split) {
	int i;
	struct scatterlist *sg;

	for (i = 0, sg = dma->SGlist; i < dma->SG_length; i++, sg = sg_next(sg)) {
		dma->SGarray[i].size = cpu_to_le32(sg_dma_len(sg));
		dma->SGarray[i].src = cpu_to_le32(sg_dma_address(sg));
		dma->SGarray[i].dst = cpu_to_le32(buffer_offset);
		buffer_offset += sg_dma_len(sg);

		split -= sg_dma_len(sg);
		if (split == 0)
			buffer_offset = buffer_offset_2;
	}
}

/* User DMA Buffers */
void ivtv_udma_alloc(struct ivtv *itv)
{
	if (itv->udma.SG_handle == 0) {
		/* Map DMA Page Array Buffer */
		itv->udma.SG_handle = pci_map_single(itv->pdev, itv->udma.SGarray,
			   sizeof(itv->udma.SGarray), PCI_DMA_TODEVICE);
		ivtv_udma_sync_for_cpu(itv);
	}
}

int ivtv_udma_setup(struct ivtv *itv, unsigned long ivtv_dest_addr,
		       void __user *userbuf, int size_in_bytes)
{
	struct ivtv_dma_page_info user_dma;
	struct ivtv_user_dma *dma = &itv->udma;
	int i, err;

	IVTV_DEBUG_DMA("ivtv_udma_setup, dst: 0x%08x\n", (unsigned int)ivtv_dest_addr);

	/* Still in USE */
	if (dma->SG_length || dma->page_count) {
		IVTV_DEBUG_WARN("ivtv_udma_setup: SG_length %d page_count %d still full?\n",
			   dma->SG_length, dma->page_count);
		return -EBUSY;
	}

	ivtv_udma_get_page_info(&user_dma, (unsigned long)userbuf, size_in_bytes);

	if (user_dma.page_count <= 0) {
		IVTV_DEBUG_WARN("ivtv_udma_setup: Error %d page_count from %d bytes %d offset\n",
			   user_dma.page_count, size_in_bytes, user_dma.offset);
		return -EINVAL;
	}

	/* Get user pages for DMA Xfer */
	err = get_user_pages_unlocked(user_dma.uaddr, user_dma.page_count,
			dma->map, FOLL_FORCE);

	if (user_dma.page_count != err) {
		IVTV_DEBUG_WARN("failed to map user pages, returned %d instead of %d\n",
			   err, user_dma.page_count);
		if (err >= 0) {
			for (i = 0; i < err; i++)
				put_page(dma->map[i]);
			return -EINVAL;
		}
		return err;
	}

	dma->page_count = user_dma.page_count;

	/* Fill SG List with new values */
	if (ivtv_udma_fill_sg_list(dma, &user_dma, 0) < 0) {
		for (i = 0; i < dma->page_count; i++) {
			put_page(dma->map[i]);
		}
		dma->page_count = 0;
		return -ENOMEM;
	}

	/* Map SG List */
	dma->SG_length = pci_map_sg(itv->pdev, dma->SGlist, dma->page_count, PCI_DMA_TODEVICE);

	/* Fill SG Array with new values */
	ivtv_udma_fill_sg_array (dma, ivtv_dest_addr, 0, -1);

	/* Tag SG Array with Interrupt Bit */
	dma->SGarray[dma->SG_length - 1].size |= cpu_to_le32(0x80000000);

	ivtv_udma_sync_for_device(itv);
	return dma->page_count;
}

void ivtv_udma_unmap(struct ivtv *itv)
{
	struct ivtv_user_dma *dma = &itv->udma;
	int i;

	IVTV_DEBUG_INFO("ivtv_unmap_user_dma\n");

	/* Nothing to free */
	if (dma->page_count == 0)
		return;

	/* Unmap Scatterlist */
	if (dma->SG_length) {
		pci_unmap_sg(itv->pdev, dma->SGlist, dma->page_count, PCI_DMA_TODEVICE);
		dma->SG_length = 0;
	}
	/* sync DMA */
	ivtv_udma_sync_for_cpu(itv);

	/* Release User Pages */
	for (i = 0; i < dma->page_count; i++) {
		put_page(dma->map[i]);
	}
	dma->page_count = 0;
}

void ivtv_udma_free(struct ivtv *itv)
{
	int i;

	/* Unmap SG Array */
	if (itv->udma.SG_handle) {
		pci_unmap_single(itv->pdev, itv->udma.SG_handle,
			 sizeof(itv->udma.SGarray), PCI_DMA_TODEVICE);
	}

	/* Unmap Scatterlist */
	if (itv->udma.SG_length) {
		pci_unmap_sg(itv->pdev, itv->udma.SGlist, itv->udma.page_count, PCI_DMA_TODEVICE);
	}

	for (i = 0; i < IVTV_DMA_SG_OSD_ENT; i++) {
		if (itv->udma.bouncemap[i])
			__free_page(itv->udma.bouncemap[i]);
	}
}

void ivtv_udma_start(struct ivtv *itv)
{
	IVTV_DEBUG_DMA("start UDMA\n");
	write_reg(itv->udma.SG_handle, IVTV_REG_DECDMAADDR);
	write_reg_sync(read_reg(IVTV_REG_DMAXFER) | 0x01, IVTV_REG_DMAXFER);
	set_bit(IVTV_F_I_DMA, &itv->i_flags);
	set_bit(IVTV_F_I_UDMA, &itv->i_flags);
	clear_bit(IVTV_F_I_UDMA_PENDING, &itv->i_flags);
}

void ivtv_udma_prepare(struct ivtv *itv)
{
	unsigned long flags;

	spin_lock_irqsave(&itv->dma_reg_lock, flags);
	if (!test_bit(IVTV_F_I_DMA, &itv->i_flags))
		ivtv_udma_start(itv);
	else
		set_bit(IVTV_F_I_UDMA_PENDING, &itv->i_flags);
	spin_unlock_irqrestore(&itv->dma_reg_lock, flags);
}
