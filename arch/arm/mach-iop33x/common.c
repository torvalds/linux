/*
 * arch/arm/mach-iop33x/common.c
 *
 * Common routines shared across all IOP3xx implementations
 *
 * Author: Deepak Saxena <dsaxena@mvista.com>
 *
 * Copyright 2003 (c) MontaVista, Software, Inc.
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/delay.h>
#include <asm/hardware.h>

/*
 * Shared variables
 */
unsigned long iop3xx_pcibios_min_io = 0;
unsigned long iop3xx_pcibios_min_mem = 0;
