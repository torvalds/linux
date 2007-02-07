/***************************************************************************/

/*
 *	linux/arch/m68knommu/platform/523x/config.c
 *
 *	Sub-architcture dependant initialization code for the Freescale
 *	523x CPUs.
 *
 *	Copyright (C) 1999-2005, Greg Ungerer (gerg@snapgear.com)
 *	Copyright (C) 2001-2003, SnapGear Inc. (www.snapgear.com)
 */

/***************************************************************************/

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/param.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <asm/dma.h>
#include <asm/traps.h>
#include <asm/machdep.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/mcfdma.h>

/***************************************************************************/

void coldfire_pit_tick(void);
void coldfire_pit_init(irq_handler_t handler);
unsigned long coldfire_pit_offset(void);
void coldfire_trap_init(void);
void coldfire_reset(void);

/***************************************************************************/

/*
 *	DMA channel base address table.
 */
unsigned int   dma_base_addr[MAX_M68K_DMA_CHANNELS] = {
        MCF_MBAR + MCFDMA_BASE0,
};

unsigned int dma_device_address[MAX_M68K_DMA_CHANNELS];

/***************************************************************************/

void mcf_disableall(void)
{
	*((volatile unsigned long *) (MCF_IPSBAR + MCFICM_INTC0 + MCFINTC_IMRH)) = 0xffffffff;
	*((volatile unsigned long *) (MCF_IPSBAR + MCFICM_INTC0 + MCFINTC_IMRL)) = 0xffffffff;
}

/***************************************************************************/

void mcf_autovector(unsigned int vec)
{
	/* Everything is auto-vectored on the 5272 */
}

/***************************************************************************/

void config_BSP(char *commandp, int size)
{
	mcf_disableall();

#ifdef CONFIG_BOOTPARAM
	strncpy(commandp, CONFIG_BOOTPARAM_STRING, size);
	commandp[size-1] = 0;
#else
	memset(commandp, 0, size);
#endif

	mach_sched_init = coldfire_pit_init;
	mach_tick = coldfire_pit_tick;
	mach_gettimeoffset = coldfire_pit_offset;
	mach_trap_init = coldfire_trap_init;
	mach_reset = coldfire_reset;
}

/***************************************************************************/
