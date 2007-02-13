/***************************************************************************/

/*
 *	linux/arch/m68knommu/platform/5272/config.c
 *
 *	Copyright (C) 1999-2002, Greg Ungerer (gerg@snapgear.com)
 *	Copyright (C) 2001-2002, SnapGear Inc. (www.snapgear.com)
 */

/***************************************************************************/

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/param.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <asm/irq.h>
#include <asm/dma.h>
#include <asm/traps.h>
#include <asm/machdep.h>
#include <asm/coldfire.h>
#include <asm/mcftimer.h>
#include <asm/mcfsim.h>
#include <asm/mcfdma.h>

/***************************************************************************/

void coldfire_tick(void);
void coldfire_timer_init(irq_handler_t handler);
unsigned long coldfire_timer_offset(void);
void coldfire_trap_init(void);
void coldfire_reset(void);

extern unsigned int mcf_timervector;
extern unsigned int mcf_profilevector;
extern unsigned int mcf_timerlevel;

/***************************************************************************/

/*
 *	Some platforms need software versions of the GPIO data registers.
 */
unsigned short ppdata;
unsigned char ledbank = 0xff;

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
	volatile unsigned long	*icrp;

	icrp = (volatile unsigned long *) (MCF_MBAR + MCFSIM_ICR1);
	icrp[0] = 0x88888888;
	icrp[1] = 0x88888888;
	icrp[2] = 0x88888888;
	icrp[3] = 0x88888888;
}

/***************************************************************************/

void mcf_autovector(unsigned int vec)
{
	/* Everything is auto-vectored on the 5272 */
}

/***************************************************************************/

void mcf_settimericr(int timer, int level)
{
	volatile unsigned long *icrp;

	if ((timer >= 1 ) && (timer <= 4)) {
		icrp = (volatile unsigned long *) (MCF_MBAR + MCFSIM_ICR1);
		*icrp = (0x8 | level) << ((4 - timer) * 4);
	}
}

/***************************************************************************/

int mcf_timerirqpending(int timer)
{
	volatile unsigned long *icrp;

	if ((timer >= 1 ) && (timer <= 4)) {
		icrp = (volatile unsigned long *) (MCF_MBAR + MCFSIM_ICR1);
		return (*icrp & (0x8 << ((4 - timer) * 4)));
	}
	return 0;
}

/***************************************************************************/

void config_BSP(char *commandp, int size)
{
#if defined (CONFIG_MOD5272)
	volatile unsigned char	*pivrp;

	/* Set base of device vectors to be 64 */
	pivrp = (volatile unsigned char *) (MCF_MBAR + MCFSIM_PIVR);
	*pivrp = 0x40;
#endif

	mcf_disableall();

#if defined(CONFIG_BOOTPARAM)
	strncpy(commandp, CONFIG_BOOTPARAM_STRING, size);
	commandp[size-1] = 0;
#elif defined(CONFIG_NETtel) || defined(CONFIG_SCALES)
	/* Copy command line from FLASH to local buffer... */
	memcpy(commandp, (char *) 0xf0004000, size);
	commandp[size-1] = 0;
#elif defined(CONFIG_MTD_KeyTechnology)
	/* Copy command line from FLASH to local buffer... */
	memcpy(commandp, (char *) 0xffe06000, size);
	commandp[size-1] = 0;
#elif defined(CONFIG_CANCam)
	/* Copy command line from FLASH to local buffer... */
	memcpy(commandp, (char *) 0xf0010000, size);
	commandp[size-1] = 0;
#else
	memset(commandp, 0, size);
#endif

	mcf_timervector = 69;
	mcf_profilevector = 70;
	mach_sched_init = coldfire_timer_init;
	mach_tick = coldfire_tick;
	mach_gettimeoffset = coldfire_timer_offset;
	mach_trap_init = coldfire_trap_init;
	mach_reset = coldfire_reset;
}

/***************************************************************************/
