// SPDX-License-Identifier: GPL-2.0-only
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
#include <asm/cpm.h>
#include <asm/fixmap.h>
#include <soc/fsl/qe/qe.h>

#include <mm/mmu_decl.h>

#if defined(CONFIG_CPM2) || defined(CONFIG_8xx_GPIO)
#include <linux/of_gpio.h>
#endif

static int __init cpm_init(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "fsl,cpm1");
	if (!np)
		np = of_find_compatible_node(NULL, NULL, "fsl,cpm2");
	if (!np)
		return -ENODEV;
	cpm_muram_init();
	of_node_put(np);
	return 0;
}
subsys_initcall(cpm_init);

#ifdef CONFIG_PPC_EARLY_DEBUG_CPM
static u32 __iomem *cpm_udbg_txdesc;
static u8 __iomem *cpm_udbg_txbuf;

static void udbg_putc_cpm(char c)
{
	if (c == '\n')
		udbg_putc_cpm('\r');

	while (in_be32(&cpm_udbg_txdesc[0]) & 0x80000000)
		;

	out_8(cpm_udbg_txbuf, c);
	out_be32(&cpm_udbg_txdesc[0], 0xa0000001);
}

void __init udbg_init_cpm(void)
{
#ifdef CONFIG_PPC_8xx
	mmu_mapin_immr();

	cpm_udbg_txdesc = (u32 __iomem __force *)
			  (CONFIG_PPC_EARLY_DEBUG_CPM_ADDR - PHYS_IMMR_BASE +
			   VIRT_IMMR_BASE);
	cpm_udbg_txbuf = (u8 __iomem __force *)
			 (in_be32(&cpm_udbg_txdesc[1]) - PHYS_IMMR_BASE +
			  VIRT_IMMR_BASE);
#else
	cpm_udbg_txdesc = (u32 __iomem __force *)
			  CONFIG_PPC_EARLY_DEBUG_CPM_ADDR;
	cpm_udbg_txbuf = (u8 __iomem __force *)in_be32(&cpm_udbg_txdesc[1]);
#endif

	if (cpm_udbg_txdesc) {
#ifdef CONFIG_CPM2
		setbat(1, 0xf0000000, 0xf0000000, 1024*1024, PAGE_KERNEL_NCG);
#endif
		udbg_putc = udbg_putc_cpm;
	}
}
#endif

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

static void cpm2_gpio32_save_regs(struct of_mm_gpio_chip *mm_gc)
{
	struct cpm2_gpio32_chip *cpm2_gc =
		container_of(mm_gc, struct cpm2_gpio32_chip, mm_gc);
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
	struct cpm2_gpio32_chip *cpm2_gc = gpiochip_get_data(&mm_gc->gc);
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
	struct cpm2_gpio32_chip *cpm2_gc = gpiochip_get_data(gc);
	unsigned long flags;
	u32 pin_mask = 1 << (31 - gpio);

	spin_lock_irqsave(&cpm2_gc->lock, flags);

	__cpm2_gpio32_set(mm_gc, pin_mask, value);

	spin_unlock_irqrestore(&cpm2_gc->lock, flags);
}

static int cpm2_gpio32_dir_out(struct gpio_chip *gc, unsigned int gpio, int val)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct cpm2_gpio32_chip *cpm2_gc = gpiochip_get_data(gc);
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
	struct cpm2_gpio32_chip *cpm2_gc = gpiochip_get_data(gc);
	struct cpm2_ioports __iomem *iop = mm_gc->regs;
	unsigned long flags;
	u32 pin_mask = 1 << (31 - gpio);

	spin_lock_irqsave(&cpm2_gc->lock, flags);

	clrbits32(&iop->dir, pin_mask);

	spin_unlock_irqrestore(&cpm2_gc->lock, flags);

	return 0;
}

int cpm2_gpiochip_add32(struct device *dev)
{
	struct device_node *np = dev->of_node;
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
	gc->parent = dev;
	gc->owner = THIS_MODULE;

	return of_mm_gpiochip_add_data(np, mm_gc, cpm2_gc);
}
#endif /* CONFIG_CPM2 || CONFIG_8xx_GPIO */
