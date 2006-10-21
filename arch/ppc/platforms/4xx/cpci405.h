/*
 * CPCI-405 board specific definitions
 *
 * Copyright 2001-2006 esd electronic system design - hannover germany
 *
 * Authors: Matthias Fuchs
 *          matthias.fuchs@esd-electronics.com
 *          Stefan Roese
 *          stefan.roese@esd-electronics.com
 */

#ifdef __KERNEL__
#ifndef __CPCI405_H__
#define __CPCI405_H__

#include <platforms/4xx/ibm405gp.h>
#include <asm/ppcboot.h>

/* Map for the NVRAM space */
#define CPCI405_NVRAM_PADDR	((uint)0xf0200000)
#define CPCI405_NVRAM_SIZE	((uint)32*1024)

#define BASE_BAUD		0

#define PPC4xx_MACHINE_NAME     "esd CPCI-405"

#endif	/* __CPCI405_H__ */
#endif /* __KERNEL__ */
