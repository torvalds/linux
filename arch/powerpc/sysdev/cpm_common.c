/*
 * Common CPM code
 *
 * Author: Scott Wood <scottwood@freescale.com>
 *
 * Copyright 2007-2008,2010 Freescale Semiconductor, Inc.
 *
 * Some parts derived from commproc.c/cpm2_common.c, which is:
 * Copyright (c) 1997 Dan error_act (dmalek@jlc.net)
 * Copyright (c) 1999-2001 Dan Malek <dan@embeddedalley.com>
 * Copyright (c) 2000 MontaVista Software, Inc (source@mvista.com)
 * 2006 (c) MontaVista Software, Inc.
 * Vitaly Bordug <vbordug@ru.mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/of_device.h>
#include <linux/spinlock.h>
#include <linux/export.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>

#include <asm/udbg.h>
#include <asm/io.h>
#include <asm/rheap.h>
#include <asm/cpm.h>

#include <mm/mmu_decl.h>

#if defined(CONFIG_CPM2) || defined(CONFIG_8xx_GPIO)
#include <linux/of_gpio.h>
#endif

#ifdef CONFIG_PPC_EARLY_DEBUG_CPM
static u32 __iomem *cpm_udbg_txdesc =
	(u32 __iomem __force *)CONFIG_PPC_EARLY_DEBUG_CPM_ADDR;

static void udbg_putc_cpm(char c)
{
	u8 __iomem *txbuf = (u8 __iomem __force *)in_be32(&cpm_udbg_txdesc[1]);

	if (c == '\n')
		udbg_putc_cpm('\r');

	while (in_be32(&cpm_udbg_txdesc[0]) & 0x80000000)
		;

	out_8(txbuf, c);
	out_be32(&cpm_udbg_txdesc[0], 0xa0000001);
}

void __init udbg_init_cpm(void)
{
	if (cpm_udbg_txdesc) {
#ifdef CONFIG_CPM2
		setbat(1, 0xf0000000, 0xf0000000, 1024*1024, PAGE_KERNEL_NCG);
#endif
		udbg_putc = udbg_putc_cpm;
	}
}
#endif

static spinlock_t cpm_muram_lock;
static rh_block_t cpm_boot_muram_rh_block[16];
static rh_info_t cpm_muram_info;
static u8 __iomem *muram_vbase;
static phys_addr_t muram_pbase;

/* Max address size we deal with */
#define OF_MAX_ADDR_CELLS	4

int cpm_muram_init(void)
{
	struct device_node *np;
	struct resource r;
	u32 zero[OF_MAX_ADDR_CELLS] = {};
	resource_size_t max = 0;
	int i = 0;
	int ret = 0;

	if (muram_pbase)
		return 0;

	spin_lock_init(&cpm_muram_lock);
	/* initialize the info header */
	rh_init(&cpm_muram_info, 1,
	        sizeof(cpm_boot_muram_rh_block) /
	        sizeof(cpm_boot_muram_rh_block[0]),
	        cpm_boot_muram_rh_block);

	np = of_find_compatible_node(NULL, NULL, "fsl,cpm-muram-data");
	if (!np) {
		/* try legacy bindings */
		np = of_find_node_by_name(NULL, "data-only");
		if (!np) {
			printk(KERN_ERR "Cannot find CPM muram data node");
			ret = -ENODEV;
			goto out;
		}
	}

	muram_pbase = of_translate_address(np, zero);
	if (muram_pbase == (phys_addr_t)OF_BAD_ADDR) {
		printk(KERN_ERR "Cannot translate zero through CPM muram node");
		ret = -ENODEV;
		goto out;
	}

	while (of_address_to_resource(np, i++, &r) == 0) {
		if (r.end > max)
			max = r.end;

		rh_attach_region(&cpm_muram_info, r.start - muram_pbase,
				 resource_size(&r));
	}

	muram_vbase = ioremap(muram_pbase, max - muram_pbase + 1);
	if (!muram_vbase) {
		printk(KERN_ERR "Cannot map CPM muram");
		ret = -ENOMEM;
	}

out:
	of_node_put(np);
	return ret;
}

/**
 * cpm_muram_alloc - allocate the requested size worth of multi-user ram
 * @size: number of bytes to allocate
 * @align: requested alignment, in bytes
 *
 * This function returns an offset into the muram area.
 * Use cpm_dpram_addr() to get the virtual address of the area.
 * Use cpm_muram_free() to free the allocation.
 */
unsigned long cpm_muram_alloc(unsigned long size, unsigned long align)
{
	unsigned long start;
	unsigned long flags;

	spin_lock_irqsave(&cpm_muram_lock, flags);
	cpm_muram_info.alignment = align;
	start = rh_alloc(&cpm_muram_info, size, "commproc");
	memset_io(cpm_muram_addr(start), 0, size);
	spin_unlock_irqrestore(&cpm_muram_lock, flags);

	return start;
}
EXPORT_SYMBOL(cpm_muram_alloc);

/**
 * cpm_muram_free - free a chunk of multi-user ram
 * @offset: The beginning of the chunk as returned by cpm_muram_alloc().
 */
int cpm_muram_free(unsigned long offset)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&cpm_muram_lock, flags);
	ret = rh_free(&cpm_muram_info, offset);
	spin_unlock_irqrestore(&cpm_muram_lock, flags);

	return ret;
}
EXPORT_SYMBOL(cpm_muram_free);

/**
 * cpm_muram_alloc_fixed - reserve a specific region of multi-user ram
 * @offset: the offset into the muram area to reserve
 * @size: the number of bytes to reserve
 *
 * This function returns "start" on success, -ENOMEM on failure.
 * Use cpm_dpram_addr() to get the virtual address of the area.
 * Use cpm_muram_free() to free the allocation.
 */
unsigned long cpm_muram_alloc_fixed(unsigned long offset, unsigned long size)
{
	unsigned long start;
	unsigned long flags;

	spin_lock_irqsave(&cpm_muram_lock, flags);
	cpm_muram_info.alignment = 1;
	start = rh_alloc_fixed(&cpm_muram_info, offset, size, "commproc");
	spin_unlock_irqrestore(&cpm_muram_lock, flags);

	return start;
}
EXPORT_SYMBOL(cpm_muram_alloc_fixed);

/**
 * cpm_muram_addr - turn a muram offset into a virtual address
 * @offset: muram offset to convert
 */
void __iomem *cpm_muram_addr(unsigned long offset)
{
	return muram_vbase + offset;
}
EXPORT_SYMBOL(cpm_muram_addr);

unsigned long cpm_muram_offset(void __iomem *addr)
{
	return addr - (void __iomem *)muram_vbase;
}
EXPORT_SYMBOL(cpm_muram_offset);

/**
 * cpm_muram_dma - turn a muram virtual address into a DMA address
 * @offset: virtual address from cpm_muram_addr() to convert
 */
dma_addr_t cpm_muram_dma(void __iomem *addr)
{
	return muram_pbase + ((u8 __iomem *)addr - muram_vbase);
}
EXPORT_SYMBOL(cpm_muram_dma);

#if defined(CONFIG_CPM2) || defined(CONFIG_8xx_GPIO)

struct cpm2_ioports {
	u32 dir, par, sor, odr, dat;
	u32 res[3];
};

struct cpm2_gpio32_chip {
	struct of_mm_gpio_chip mm_gc;
	spinlock_t lock;

	/* shadowed data register to clear/set bits safely */
	u32 cpdata;
};

static inline struct cpm2_gpio32_chip *
to_cpm2_gpio32_chip(struct of_mm_gpio_chip *mm_gc)
{
	return container_of(mm_gc, struct cpm2_gpio32_chip, mm_gc);
}

static void cpm2_gpio32_save_regs(struct of_mm_gpio_chip *mm_gc)
{
	struct cpm2_gpio32_chip *cpm2_gc = to_cpm2_gpio32_chip(mm_gc);
	struct cpm2_ioports __iomem *iop = mm_gc->regs;

	cpm2_gc->cpdata = in_be32(&iop->dat);
}

static int cpm2_gpio32_get(struct gpio_chip *gc, unsigned int gpio)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct cpm2_ioports __iomem *iop = mm_gc->regs;
	u32 pin_mask;

	pin_mask = 1 << (31 - gpio);

	return !!(in_be32(&iop->dat) & pin_mask);
}

static void __cpm2_gpio32_set(struct of_mm_gpio_chip *mm_gc, u32 pin_mask,
	int value)
{
	struct cpm2_gpio32_chip *cpm2_gc = to_cpm2_gpio32_chip(mm_gc);
	struct cpm2_ioports __iomem *iop = mm_gc->regs;

	if (value)
		cpm2_gc->cpdata |= pin_mask;
	else
		cpm2_gc->cpdata &= ~pin_mask;

	out_be32(&iop->dat, cpm2_gc->cpdata);
}

static void cpm2_gpio32_set(struct gpio_chip *gc, unsigned int gpio, int value)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct cpm2_gpio32_chip *cpm2_gc = to_cpm2_gpio32_chip(mm_gc);
	unsigned long flags;
	u32 pin_mask = 1 << (31 - gpio);

	spin_lock_irqsave(&cpm2_gc->lock, flags);

	__cpm2_gpio32_set(mm_gc, pin_mask, value);

	spin_unlock_irqrestore(&cpm2_gc->lock, flags);
}

static int cpm2_gpio32_dir_out(struct gpio_chip *gc, unsigned int gpio, int val)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct cpm2_gpio32_chip *cpm2_gc = to_cpm2_gpio32_chip(mm_gc);
	struct cpm2_ioports __iomem *iop = mm_gc->regs;
	unsigned long flags;
	u32 pin_mask = 1 << (31 - gpio);

	spin_lock_irqsave(&cpm2_gc->lock, flags);

	setbits32(&iop->dir, pin_mask);
	__cpm2_gpio32_set(mm_gc, pin_mask, val);

	spin_unlock_irqrestore(&cpm2_gc->lock, flags);

	return 0;
}

static int cpm2_gpio32_dir_in(struct gpio_chip *gc, unsigned int gpio)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct cpm2_gpio32_chip *cpm2_gc = to_cpm2_gpio32_chip(mm_gc);
	struct cpm2_ioports __iomem *iop = mm_gc->regs;
	unsigned long flags;
	u32 pin_mask = 1 << (31 - gpio);

	spin_lock_irqsave(&cpm2_gc->lock, flags);

	clrbits32(&iop->dir, pin_mask);

	spin_unlock_irqrestore(&cpm2_gc->lock, flags);

	return 0;
}

int cpm2_gpiochip_add32(struct device_node *np)
{
	struct cpm2_gpio32_chip *cpm2_gc;
	struct of_mm_gpio_chip *mm_gc;
	struct gpio_chip *gc;

	cpm2_gc = kzalloc(sizeof(*cpm2_gc), GFP_KERNEL);
	if (!cpm2_gc)
		return -ENOMEM;

	spin_lock_init(&cpm2_gc->lock);

	mm_gc = &cpm2_gc->mm_gc;
	gc = &mm_gc->gc;

	mm_gc->save_regs = cpm2_gpio32_save_regs;
	gc->ngpio = 32;
	gc->direction_input = cpm2_gpio32_dir_in;
	gc->direction_output = cpm2_gpio32_dir_out;
	gc->get = cpm2_gpio32_get;
	gc->set = cpm2_gpio32_set;

	return of_mm_gpiochip_add(np, mm_gc);
}
#endif /* CONFIG_CPM2 || CONFIG_8xx_GPIO */
