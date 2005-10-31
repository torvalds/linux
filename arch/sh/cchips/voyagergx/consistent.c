/*
 * arch/sh/cchips/voyagergx/consistent.c
 *
 * Copyright (C) 2004  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/device.h>
#include <asm/io.h>
#include <asm/bus-sh.h>

struct voya_alloc_entry {
	struct list_head list;
	unsigned long ofs;
	unsigned long len;
};

static DEFINE_SPINLOCK(voya_list_lock);
static LIST_HEAD(voya_alloc_list);

#define OHCI_SRAM_START	0xb0000000
#define OHCI_HCCA_SIZE	0x100
#define OHCI_SRAM_SIZE	0x10000

void *voyagergx_consistent_alloc(struct device *dev, size_t size,
				 dma_addr_t *handle, gfp_t flag)
{
	struct list_head *list = &voya_alloc_list;
	struct voya_alloc_entry *entry;
	struct sh_dev *shdev = to_sh_dev(dev);
	unsigned long start, end;
	unsigned long flags;

	/*
	 * The SM501 contains an integrated 8051 with its own SRAM.
	 * Devices within the cchip can all hook into the 8051 SRAM.
	 * We presently use this for the OHCI.
	 *
	 * Everything else goes through consistent_alloc().
	 */
	if (!dev || dev->bus != &sh_bus_types[SH_BUS_VIRT] ||
		   (dev->bus == &sh_bus_types[SH_BUS_VIRT] &&
		    shdev->dev_id != SH_DEV_ID_USB_OHCI))
		return NULL;

	start = OHCI_SRAM_START + OHCI_HCCA_SIZE;

	entry = kmalloc(sizeof(struct voya_alloc_entry), GFP_ATOMIC);
	if (!entry)
		return ERR_PTR(-ENOMEM);

	entry->len = (size + 15) & ~15;

	/*
	 * The basis for this allocator is dwmw2's malloc.. the
	 * Matrox allocator :-)
	 */
	spin_lock_irqsave(&voya_list_lock, flags);
	list_for_each(list, &voya_alloc_list) {
		struct voya_alloc_entry *p;

		p = list_entry(list, struct voya_alloc_entry, list);

		if (p->ofs - start >= size)
			goto out;

		start = p->ofs + p->len;
	}

	end  = start + (OHCI_SRAM_SIZE  - OHCI_HCCA_SIZE);
	list = &voya_alloc_list;

	if (end - start >= size) {
out:
		entry->ofs = start;
		list_add_tail(&entry->list, list);
		spin_unlock_irqrestore(&voya_list_lock, flags);

		*handle = start;
		return (void *)start;
	}

	kfree(entry);
	spin_unlock_irqrestore(&voya_list_lock, flags);

	return ERR_PTR(-EINVAL);
}

int voyagergx_consistent_free(struct device *dev, size_t size,
			      void *vaddr, dma_addr_t handle)
{
	struct voya_alloc_entry *entry;
	struct sh_dev *shdev = to_sh_dev(dev);
	unsigned long flags;

	if (!dev || dev->bus != &sh_bus_types[SH_BUS_VIRT] ||
		   (dev->bus == &sh_bus_types[SH_BUS_VIRT] &&
		    shdev->dev_id != SH_DEV_ID_USB_OHCI))
		return -EINVAL;

	spin_lock_irqsave(&voya_list_lock, flags);
	list_for_each_entry(entry, &voya_alloc_list, list) {
		if (entry->ofs != handle)
			continue;

		list_del(&entry->list);
		kfree(entry);

		break;
	}
	spin_unlock_irqrestore(&voya_list_lock, flags);

	return 0;
}

EXPORT_SYMBOL(voyagergx_consistent_alloc);
EXPORT_SYMBOL(voyagergx_consistent_free);

