/*
 * Board setup routines for the esd CPCI-405 cPCI Board.
 *
 * Copyright 2001-2006 esd electronic system design - hannover germany
 *
 * Authors: Matthias Fuchs
 *          matthias.fuchs@esd-electronics.com
 *          Stefan Roese
 *          stefan.roese@esd-electronics.com
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <linux/init.h>
#include <linux/pci.h>
#include <asm/system.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>
#include <asm/todc.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/serial_8250.h>
#include <asm/ocp.h>
#include <asm/ibm_ocp_pci.h>
#include <platforms/4xx/ibm405gp.h>

#ifdef CONFIG_GEN_RTC
void *cpci405_nvram;
#endif

extern bd_t __res;

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
		{28,	29,	30,	27},	/* IDSEL 15 - cPCI slot 8 */
		{29,	30,	27,	28},	/* IDSEL 16 - cPCI slot 7 */
		{30,	27,	28,	29},	/* IDSEL 17 - cPCI slot 6 */
		{27,	28,	29,	30},	/* IDSEL 18 - cPCI slot 5 */
		{28,	29,	30,	27},	/* IDSEL 19 - cPCI slot 4 */
		{29,	30,	27,	28},	/* IDSEL 20 - cPCI slot 3 */
		{30,	27,	28,	29},	/* IDSEL 21 - cPCI slot 2 */
        };
	const long min_idsel = 15, max_idsel = 21, irqs_per_slot = 4;
	return PCI_IRQ_TABLE_LOOKUP;
};

/* The serial clock for the chip is an internal clock determined by
 * different clock speeds/dividers.
 * Calculate the proper input baud rate and setup the serial driver.
 */
static void __init
cpci405_early_serial_map(void)
{
	u32 uart_div;
	int uart_clock;
	struct uart_port port;

         /* Calculate the serial clock input frequency
          *
          * The uart clock is the cpu frequency (provided in the board info
          * structure) divided by the external UART Divisor.
          */
	uart_div = ((mfdcr(DCRN_CHCR_BASE) & CHR0_UDIV) >> 1) + 1;
	uart_clock = __res.bi_procfreq / uart_div;

	/* Setup serial port access */
	memset(&port, 0, sizeof(port));
#if defined(CONFIG_UART0_TTYS0)
	port.membase = (void*)UART0_IO_BASE;
	port.irq = UART0_INT;
#else
	port.membase = (void*)UART1_IO_BASE;
	port.irq = UART1_INT;
#endif
	port.uartclk = uart_clock;
	port.regshift = 0;
	port.iotype = UPIO_MEM;
	port.flags = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST;
	port.line = 0;

	if (early_serial_setup(&port) != 0) {
		printk("Early serial init of port 0 failed\n");
	}
#if defined(CONFIG_UART0_TTYS0)
	port.membase = (void*)UART1_IO_BASE;
	port.irq = UART1_INT;
#else
	port.membase = (void*)UART0_IO_BASE;
	port.irq = UART0_INT;
#endif
	port.line = 1;

	if (early_serial_setup(&port) != 0) {
		printk("Early serial init of port 1 failed\n");
	}
}

void __init
cpci405_setup_arch(void)
{
	ppc4xx_setup_arch();

	ibm_ocp_set_emac(0, 0);

        cpci405_early_serial_map();

#ifdef CONFIG_GEN_RTC
	TODC_INIT(TODC_TYPE_MK48T35,
		  cpci405_nvram, cpci405_nvram, cpci405_nvram, 8);
#endif
}

void __init
bios_fixup(struct pci_controller *hose, struct pcil0_regs *pcip)
{
#ifdef CONFIG_PCI
	unsigned int bar_response, bar;

	/* Disable region first */
	out_le32((void *) &(pcip->pmm[0].ma), 0x00000000);
	/* PLB starting addr, PCI: 0x80000000 */
	out_le32((void *) &(pcip->pmm[0].la), 0x80000000);
	/* PCI start addr, 0x80000000 */
	out_le32((void *) &(pcip->pmm[0].pcila), PPC405_PCI_MEM_BASE);
	/* 512MB range of PLB to PCI */
	out_le32((void *) &(pcip->pmm[0].pciha), 0x00000000);
	/* Enable no pre-fetch, enable region */
	out_le32((void *) &(pcip->pmm[0].ma), ((0xffffffff -
						(PPC405_PCI_UPPER_MEM -
						 PPC405_PCI_MEM_BASE)) | 0x01));

	/* Disable region one */
	out_le32((void *) &(pcip->pmm[1].ma), 0x00000000);
	out_le32((void *) &(pcip->pmm[1].la), 0x00000000);
	out_le32((void *) &(pcip->pmm[1].pcila), 0x00000000);
	out_le32((void *) &(pcip->pmm[1].pciha), 0x00000000);
	out_le32((void *) &(pcip->pmm[1].ma), 0x00000000);
	out_le32((void *) &(pcip->ptm1ms), 0x00000001);

	/* Disable region two */
	out_le32((void *) &(pcip->pmm[2].ma), 0x00000000);
	out_le32((void *) &(pcip->pmm[2].la), 0x00000000);
	out_le32((void *) &(pcip->pmm[2].pcila), 0x00000000);
	out_le32((void *) &(pcip->pmm[2].pciha), 0x00000000);
	out_le32((void *) &(pcip->pmm[2].ma), 0x00000000);
	out_le32((void *) &(pcip->ptm2ms), 0x00000000);
	out_le32((void *) &(pcip->ptm2la), 0x00000000);

	/* Zero config bars */
	for (bar = PCI_BASE_ADDRESS_1; bar <= PCI_BASE_ADDRESS_2; bar += 4) {
		early_write_config_dword(hose, hose->first_busno,
					 PCI_FUNC(hose->first_busno), bar,
					 0x00000000);
		early_read_config_dword(hose, hose->first_busno,
					PCI_FUNC(hose->first_busno), bar,
					&bar_response);
	}
#endif
}

void __init
cpci405_map_io(void)
{
	ppc4xx_map_io();

#ifdef CONFIG_GEN_RTC
	cpci405_nvram = ioremap(CPCI405_NVRAM_PADDR, CPCI405_NVRAM_SIZE);
#endif
}

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	ppc4xx_init(r3, r4, r5, r6, r7);

	ppc_md.setup_arch = cpci405_setup_arch;
	ppc_md.setup_io_mappings = cpci405_map_io;

#ifdef CONFIG_GEN_RTC
	ppc_md.time_init = todc_time_init;
	ppc_md.set_rtc_time = todc_set_rtc_time;
	ppc_md.get_rtc_time = todc_get_rtc_time;
	ppc_md.nvram_read_val = todc_direct_read_val;
	ppc_md.nvram_write_val = todc_direct_write_val;
#endif
}
