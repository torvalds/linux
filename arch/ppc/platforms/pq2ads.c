/*
 * PQ2ADS platform support
 *
 * Author: Kumar Gala <galak@kernel.crashing.org>
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

#include <asm/io.h>
#include <asm/mpc8260.h>
#include <asm/cpm2.h>
#include <asm/immap_cpm2.h>

void __init
m82xx_board_setup(void)
{
	cpm2_map_t* immap = ioremap(CPM_MAP_ADDR, sizeof(cpm2_map_t));
	u32 *bcsr = ioremap(BCSR_ADDR+4, sizeof(u32));

	/* Enable the 2nd UART port */
	clrbits32(bcsr, BCSR1_RS232_EN2);

#ifdef CONFIG_SERIAL_CPM_SCC1
	clrbits32((u32*)&immap->im_scc[0].scc_sccm, UART_SCCM_TX | UART_SCCM_RX);
	clrbits32((u32*)&immap->im_scc[0].scc_gsmrl, SCC_GSMRL_ENR | SCC_GSMRL_ENT);
#endif

#ifdef CONFIG_SERIAL_CPM_SCC2
	clrbits32((u32*)&immap->im_scc[1].scc_sccm, UART_SCCM_TX | UART_SCCM_RX);
	clrbits32((u32*)&immap->im_scc[1].scc_gsmrl, SCC_GSMRL_ENR | SCC_GSMRL_ENT);
#endif

#ifdef CONFIG_SERIAL_CPM_SCC3
	clrbits32((u32*)&immap->im_scc[2].scc_sccm, UART_SCCM_TX | UART_SCCM_RX);
	clrbits32((u32*)&immap->im_scc[2].scc_gsmrl, SCC_GSMRL_ENR | SCC_GSMRL_ENT);
#endif

#ifdef CONFIG_SERIAL_CPM_SCC4
	clrbits32((u32*)&immap->im_scc[3].scc_sccm, UART_SCCM_TX | UART_SCCM_RX);
	clrbits32((u32*)&immap->im_scc[3].scc_gsmrl, SCC_GSMRL_ENR | SCC_GSMRL_ENT);
#endif

	iounmap(bcsr);
	iounmap(immap);
}
