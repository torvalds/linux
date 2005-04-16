/*
 * CPCI-405 board specific definitions
 *
 * Copyright (c) 2001 Stefan Roese (stefan.roese@esd-electronics.com)
 */

#ifdef __KERNEL__
#ifndef __ASM_CPCI405_H__
#define __ASM_CPCI405_H__

#include <linux/config.h>

/* We have a 405GP core */
#include <platforms/4xx/ibm405gp.h>

#include <asm/ppcboot.h>

#ifndef __ASSEMBLY__
/* Some 4xx parts use a different timebase frequency from the internal clock.
*/
#define bi_tbfreq bi_intfreq

/* Map for the NVRAM space */
#define CPCI405_NVRAM_PADDR	((uint)0xf0200000)
#define CPCI405_NVRAM_SIZE	((uint)32*1024)

#ifdef CONFIG_PPC405GP_INTERNAL_CLOCK
#define BASE_BAUD		201600
#else
#define BASE_BAUD		691200
#endif

#define PPC4xx_MACHINE_NAME "esd CPCI-405"

#endif /* !__ASSEMBLY__ */
#endif	/* __ASM_CPCI405_H__ */
#endif /* __KERNEL__ */
