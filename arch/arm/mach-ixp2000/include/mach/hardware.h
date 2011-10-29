/*
 * arch/arm/mach-ixp2000/include/mach/hardware.h
 *
 * Hardware definitions for IXP2400/2800 based systems
 *
 * Original Author: Naeem M Afzal <naeem.m.afzal@intel.com>
 *
 * Maintainer: Deepak Saxena <dsaxena@mvista.com>
 *
 * Copyright (C) 2001-2002 Intel Corp.
 * Copyright (C) 2003-2004 MontaVista Software, Inc.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#ifndef __ASM_ARCH_HARDWARE_H__
#define __ASM_ARCH_HARDWARE_H__

#include "ixp2000-regs.h"	/* Chipset Registers */

/*
 * Platform helper functions
 */
#include "platform.h"

/*
 * Platform-specific bits
 */
#include "enp2611.h"		/* ENP-2611 */
#include "ixdp2x00.h"		/* IXDP2400/2800 */
#include "ixdp2x01.h"		/* IXDP2401/2801 */

#endif  /* _ASM_ARCH_HARDWARE_H__ */
