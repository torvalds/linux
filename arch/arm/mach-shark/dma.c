/*
 *  linux/arch/arm/mach-shark/dma.c
 *
 *  by Alexander Schulz
 *
 *  derived from:
 *  arch/arm/kernel/dma-ebsa285.c
 *  Copyright (C) 1998 Phil Blundell
 */

#include <linux/init.h>

#include <asm/dma.h>
#include <asm/mach/dma.h>

static int __init shark_dma_init(void)
{
#ifdef CONFIG_ISA_DMA
	isa_init_dma();
#endif
	return 0;
}
core_initcall(shark_dma_init);
