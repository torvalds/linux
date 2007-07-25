/***************************************************************************/

/*
 *  linux/arch/m68knommu/platform/520x/config.c
 *
 *  Copyright (C) 2005,      Freescale (www.freescale.com)
 *  Copyright (C) 2005,      Intec Automation (mike@steroidmicros.com)
 *  Copyright (C) 1999-2003, Greg Ungerer (gerg@snapgear.com)
 *  Copyright (C) 2001-2003, SnapGear Inc. (www.snapgear.com)
 */

/***************************************************************************/

#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/interrupt.h>
#include <asm/machdep.h>
#include <asm/dma.h>

/***************************************************************************/

/*
 *	DMA channel base address table.
 */
unsigned int dma_base_addr[MAX_M68K_DMA_CHANNELS];
unsigned int dma_device_address[MAX_M68K_DMA_CHANNELS];

/***************************************************************************/

void coldfire_pit_tick(void);
void coldfire_pit_init(irq_handler_t handler);
unsigned long coldfire_pit_offset(void);
void coldfire_trap_init(void);
void coldfire_reset(void);

/***************************************************************************/

/*
 *  Program the vector to be an auto-vectored.
 */

void mcf_autovector(unsigned int vec)
{
    /* Everything is auto-vectored on the 520x devices */
}

/***************************************************************************/

void config_BSP(char *commandp, int size)
{
    mach_sched_init = coldfire_pit_init;
    mach_tick = coldfire_pit_tick;
    mach_gettimeoffset = coldfire_pit_offset;
    mach_trap_init = coldfire_trap_init;
    mach_reset = coldfire_reset;
}

/***************************************************************************/
