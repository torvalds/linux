/***************************************************************************/

/*
 *	linux/arch/m68knommu/platform/5206/config.c
 *
 *	Copyright (C) 1999-2002, Greg Ungerer (gerg@snapgear.com)
 * 	Copyright (C) 2000-2001, Lineo Inc. (www.lineo.com) 
 */

/***************************************************************************/

#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <asm/dma.h>
#include <asm/machdep.h>
#include <asm/coldfire.h>
#include <asm/mcftimer.h>
#include <asm/mcfsim.h>
#include <asm/mcfdma.h>

/***************************************************************************/

void coldfire_reset(void);

/***************************************************************************/

/*
 *	DMA channel base address table.
 */
unsigned int   dma_base_addr[MAX_M68K_DMA_CHANNELS] = {
        MCF_MBAR + MCFDMA_BASE0,
        MCF_MBAR + MCFDMA_BASE1,
};

unsigned int dma_device_address[MAX_M68K_DMA_CHANNELS];

/***************************************************************************/

void mcf_autovector(unsigned int vec)
{
	volatile unsigned char  *mbar;
	unsigned char		icr;

	if ((vec >= 25) && (vec <= 31)) {
		vec -= 25;
		mbar = (volatile unsigned char *) MCF_MBAR;
		icr = MCFSIM_ICR_AUTOVEC | (vec << 3);
		*(mbar + MCFSIM_ICR1 + vec) = icr;
		vec = 0x1 << (vec + 1);
		mcf_setimr(mcf_getimr() & ~vec);
	}
}

/***************************************************************************/

void mcf_settimericr(unsigned int timer, unsigned int level)
{
	volatile unsigned char *icrp;
	unsigned int icr, imr;

	if (timer <= 2) {
		switch (timer) {
		case 2:  icr = MCFSIM_TIMER2ICR; imr = MCFSIM_IMR_TIMER2; break;
		default: icr = MCFSIM_TIMER1ICR; imr = MCFSIM_IMR_TIMER1; break;
		}

		icrp = (volatile unsigned char *) (MCF_MBAR + icr);
		*icrp = MCFSIM_ICR_AUTOVEC | (level << 2) | MCFSIM_ICR_PRI3;
		mcf_setimr(mcf_getimr() & ~imr);
	}
}

/***************************************************************************/

int mcf_timerirqpending(int timer)
{
	unsigned int imr = 0;

	switch (timer) {
	case 1:  imr = MCFSIM_IMR_TIMER1; break;
	case 2:  imr = MCFSIM_IMR_TIMER2; break;
	default: break;
	}
	return (mcf_getipr() & imr);
}

/***************************************************************************/

void config_BSP(char *commandp, int size)
{
	mcf_setimr(MCFSIM_IMR_MASKALL);
	mach_reset = coldfire_reset;
}

/***************************************************************************/
