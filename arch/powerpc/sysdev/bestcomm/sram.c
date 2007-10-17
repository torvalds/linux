/*
 * Simple memory allocator for on-board SRAM
 *
 *
 * Maintainer : Sylvain Munaut <tnt@246tNt.com>
 *
 * Copyright (C) 2005 Sylvain Munaut <tnt@246tNt.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/of.h>

#include <asm/io.h>
#include <asm/mmu.h>

#include "sram.h"


/* Struct keeping our 'state' */
struct bcom_sram *bcom_sram = NULL;
EXPORT_SYMBOL_GPL(bcom_sram);	/* needed for inline functions */


/* ======================================================================== */
/* Public API                                                               */
/* ======================================================================== */
/* DO NOT USE in interrupts, if needed in irq handler, we should use the
   _irqsave version of the spin_locks */

int bcom_sram_init(struct device_node *sram_node, char *owner)
{
	int rv;
	const u32 *regaddr_p;
	u64 regaddr64, size64;
	unsigned int psize;

	/* Create our state struct */
	if (bcom_sram) {
		printk(KERN_ERR "%s: bcom_sram_init: "
			"Already initialized !\n", owner);
		return -EBUSY;
	}

	bcom_sram = kmalloc(sizeof(struct bcom_sram), GFP_KERNEL);
	if (!bcom_sram) {
		printk(KERN_ERR "%s: bcom_sram_init: "
			"Couldn't allocate internal state !\n", owner);
		return -ENOMEM;
	}

	/* Get address and size of the sram */
	regaddr_p = of_get_address(sram_node, 0, &size64, NULL);
	if (!regaddr_p) {
		printk(KERN_ERR "%s: bcom_sram_init: "
			"Invalid device node !\n", owner);
		rv = -EINVAL;
		goto error_free;
	}

	regaddr64 = of_translate_address(sram_node, regaddr_p);

	bcom_sram->base_phys = (phys_addr_t) regaddr64;
	bcom_sram->size = (unsigned int) size64;

	/* Request region */
	if (!request_mem_region(bcom_sram->base_phys, bcom_sram->size, owner)) {
		printk(KERN_ERR "%s: bcom_sram_init: "
			"Couldn't request region !\n", owner);
		rv = -EBUSY;
		goto error_free;
	}

	/* Map SRAM */
		/* sram is not really __iomem */
	bcom_sram->base_virt = (void*) ioremap(bcom_sram->base_phys, bcom_sram->size);

	if (!bcom_sram->base_virt) {
		printk(KERN_ERR "%s: bcom_sram_init: "
			"Map error SRAM zone 0x%08lx (0x%0x)!\n",
			owner, bcom_sram->base_phys, bcom_sram->size );
		rv = -ENOMEM;
		goto error_release;
	}

	/* Create an rheap (defaults to 32 bits word alignment) */
	bcom_sram->rh = rh_create(4);

	/* Attach the free zones */
#if 0
	/* Currently disabled ... for future use only */
	reg_addr_p = of_get_property(sram_node, "available", &psize);
#else
	regaddr_p = NULL;
	psize = 0;
#endif

	if (!regaddr_p || !psize) {
		/* Attach the whole zone */
		rh_attach_region(bcom_sram->rh, 0, bcom_sram->size);
	} else {
		/* Attach each zone independently */
		while (psize >= 2 * sizeof(u32)) {
			phys_addr_t zbase = of_translate_address(sram_node, regaddr_p);
			rh_attach_region(bcom_sram->rh, zbase - bcom_sram->base_phys, regaddr_p[1]);
			regaddr_p += 2;
			psize -= 2 * sizeof(u32);
		}
	}

	/* Init our spinlock */
	spin_lock_init(&bcom_sram->lock);

	return 0;

error_release:
	release_mem_region(bcom_sram->base_phys, bcom_sram->size);
error_free:
	kfree(bcom_sram);
	bcom_sram = NULL;

	return rv;
}
EXPORT_SYMBOL_GPL(bcom_sram_init);

void bcom_sram_cleanup(void)
{
	/* Free resources */
	if (bcom_sram) {
		rh_destroy(bcom_sram->rh);
		iounmap((void __iomem *)bcom_sram->base_virt);
		release_mem_region(bcom_sram->base_phys, bcom_sram->size);
		kfree(bcom_sram);
		bcom_sram = NULL;
	}
}
EXPORT_SYMBOL_GPL(bcom_sram_cleanup);

void* bcom_sram_alloc(int size, int align, phys_addr_t *phys)
{
	unsigned long offset;

	spin_lock(&bcom_sram->lock);
	offset = rh_alloc_align(bcom_sram->rh, size, align, NULL);
	spin_unlock(&bcom_sram->lock);

	if (IS_ERR_VALUE(offset))
		return NULL;

	*phys = bcom_sram->base_phys + offset;
	return bcom_sram->base_virt + offset;
}
EXPORT_SYMBOL_GPL(bcom_sram_alloc);

void bcom_sram_free(void *ptr)
{
	unsigned long offset;

	if (!ptr)
		return;

	offset = ptr - bcom_sram->base_virt;

	spin_lock(&bcom_sram->lock);
	rh_free(bcom_sram->rh, offset);
	spin_unlock(&bcom_sram->lock);
}
EXPORT_SYMBOL_GPL(bcom_sram_free);

