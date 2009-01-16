/*
 *  linux/arch/arm/kernel/dma-ebsa285.c
 *
 *  Copyright (C) 1998 Phil Blundell
 *
 * DMA functions specific to EBSA-285/CATS architectures
 *
 *  Changelog:
 *   09-Nov-1998 RMK	Split out ISA DMA functions to dma-isa.c
 *   17-Mar-1999 RMK	Allow any EBSA285-like architecture to have
 *			ISA DMA controllers.
 */
#include <linux/init.h>
#include <linux/io.h>
#include <linux/spinlock.h>

#include <asm/dma.h>
#include <asm/scatterlist.h>

#include <asm/mach/dma.h>
#include <asm/hardware/dec21285.h>

#if 0
static int fb_dma_request(dmach_t channel, dma_t *dma)
{
	return -EINVAL;
}

static void fb_dma_enable(dmach_t channel, dma_t *dma)
{
}

static void fb_dma_disable(dmach_t channel, dma_t *dma)
{
}

static struct dma_ops fb_dma_ops = {
	.type		= "fb",
	.request	= fb_dma_request,
	.enable		= fb_dma_enable,
	.disable	= fb_dma_disable,
};
#endif

void __init arch_dma_init(dma_t *dma)
{
#if 0
	dma[_DC21285_DMA(0)].d_ops = &fb_dma_ops;
	dma[_DC21285_DMA(1)].d_ops = &fb_dma_ops;
#endif
#ifdef CONFIG_ISA_DMA
	if (footbridge_cfn_mode())
		isa_init_dma(dma + _ISA_DMA(0));
#endif
}
