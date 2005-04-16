/*
 * arch/ppc/platforms/pq2ads.c
 *
 * PQ2ADS platform support
 *
 * Author: Kumar Gala <kumar.gala@freescale.com>
 * Derived from: est8260_setup.c by Allen Curtis
 *
 * Copyright 2004 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/init.h>

#include <asm/mpc8260.h>

void __init
m82xx_board_setup(void)
{
	/* Enable the 2nd UART port */
	*(volatile uint *)(BCSR_ADDR + 4) &= ~BCSR1_RS232_EN2;
}
