/***************************************************************************/

/*
 *	dma.c -- Freescale ColdFire DMA support
 *
 *	Copyright (C) 2007, Greg Ungerer (gerg@snapgear.com)
 */

/***************************************************************************/

#include <linux/kernel.h>
#include <asm/dma.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/mcfdma.h>

/***************************************************************************/

/*
 *      DMA channel base address table.
 */
unsigned int dma_base_addr[MAX_M68K_DMA_CHANNELS] = {
#ifdef MCFDMA_BASE0
	MCF_MBAR + MCFDMA_BASE0,
#endif
#ifdef MCFDMA_BASE1
	MCF_MBAR + MCFDMA_BASE1,
#endif
#ifdef MCFDMA_BASE2
	MCF_MBAR + MCFDMA_BASE2,
#endif
#ifdef MCFDMA_BASE3
	MCF_MBAR + MCFDMA_BASE3,
#endif
};

unsigned int dma_device_address[MAX_M68K_DMA_CHANNELS];

/***************************************************************************/
