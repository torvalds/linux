/*
 * Board setup routines for the esd CPCI-405 cPCI Board.
 *
 * Author: Stefan Roese
 *         stefan.roese@esd-electronics.com
 *
 * Copyright 2001 esd electronic system design - hannover germany
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <asm/system.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>
#include <asm/todc.h>
#include <asm/ocp.h>

void *cpci405_nvram;

/*
 * Some IRQs unique to CPCI-405.
 */
int __init
ppc405_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	static char pci_irq_table[][4] =
	/*
	 *      PCI IDSEL/INTPIN->INTLINE
	 *      A       B       C       D
	 */
	{
		{28,	28,	28,	28},	/* IDSEL 15 - cPCI slot 8 */
		{29,	29,	29,	29},	/* IDSEL 16 - cPCI slot 7 */
		{30,	30,	30,	30},	/* IDSEL 17 - cPCI slot 6 */
		{27,	27,	27,	27},	/* IDSEL 18 - cPCI slot 5 */
		{28,	28,	28,	28},	/* IDSEL 19 - cPCI slot 4 */
		{29,	29,	29,	29},	/* IDSEL 20 - cPCI slot 3 */
		{30,	30,	30,	30},	/* IDSEL 21 - cPCI slot 2 */
        };
	const long min_idsel = 15, max_idsel = 21, irqs_per_slot = 4;
	return PCI_IRQ_TABLE_LOOKUP;
};

void __init
cpci405_setup_arch(void)
{
	ppc4xx_setup_arch();

	ibm_ocp_set_emac(0, 0);

	TODC_INIT(TODC_TYPE_MK48T35, cpci405_nvram, cpci405_nvram, cpci405_nvram, 8);
}

void __init
cpci405_map_io(void)
{
	ppc4xx_map_io();
	cpci405_nvram = ioremap(CPCI405_NVRAM_PADDR, CPCI405_NVRAM_SIZE);
}

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	ppc4xx_init(r3, r4, r5, r6, r7);

	ppc_md.setup_arch = cpci405_setup_arch;
	ppc_md.setup_io_mappings = cpci405_map_io;

	ppc_md.time_init = todc_time_init;
	ppc_md.set_rtc_time = todc_set_rtc_time;
	ppc_md.get_rtc_time = todc_get_rtc_time;
	ppc_md.nvram_read_val = todc_direct_read_val;
	ppc_md.nvram_write_val = todc_direct_write_val;
}
