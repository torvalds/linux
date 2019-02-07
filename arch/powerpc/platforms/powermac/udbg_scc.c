/*
 * udbg for zilog scc ports as found on Apple PowerMacs
 *
 * Copyright (C) 2001-2005 PPC 64 Team, IBM Corp
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */
#include <linux/types.h>
#include <asm/udbg.h>
#include <asm/processor.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pmac_feature.h>

extern u8 real_readb(volatile u8 __iomem  *addr);
extern void real_writeb(u8 data, volatile u8 __iomem *addr);

#define	SCC_TXRDY	4
#define SCC_RXRDY	1

static volatile u8 __iomem *sccc;
static volatile u8 __iomem *sccd;

static void udbg_scc_putc(char c)
{
	if (sccc) {
		while ((in_8(sccc) & SCC_TXRDY) == 0)
			;
		out_8(sccd,  c);
		if (c == '\n')
			udbg_scc_putc('\r');
	}
}

static int udbg_scc_getc_poll(void)
{
	if (sccc) {
		if ((in_8(sccc) & SCC_RXRDY) != 0)
			return in_8(sccd);
		else
			return -1;
	}
	return -1;
}

static int udbg_scc_getc(void)
{
	if (sccc) {
		while ((in_8(sccc) & SCC_RXRDY) == 0)
			;
		return in_8(sccd);
	}
	return -1;
}

static unsigned char scc_inittab[] = {
    13, 0,		/* set baud rate divisor */
    12, 0,
    14, 1,		/* baud rate gen enable, src=rtxc */
    11, 0x50,		/* clocks = br gen */
    5,  0xea,		/* tx 8 bits, assert DTR & RTS */
    4,  0x46,		/* x16 clock, 1 stop */
    3,  0xc1,		/* rx enable, 8 bits */
};

void udbg_scc_init(int force_scc)
{
	const u32 *reg;
	unsigned long addr;
	struct device_node *stdout = NULL, *escc = NULL, *macio = NULL;
	struct device_node *ch, *ch_def = NULL, *ch_a = NULL;
	const char *path;
	int i;

	escc = of_find_node_by_name(NULL, "escc");
	if (escc == NULL)
		goto bail;
	macio = of_get_parent(escc);
	if (macio == NULL)
		goto bail;
	path = of_get_property(of_chosen, "linux,stdout-path", NULL);
	if (path != NULL)
		stdout = of_find_node_by_path(path);
	for (ch = NULL; (ch = of_get_next_child(escc, ch)) != NULL;) {
		if (ch == stdout)
			ch_def = of_node_get(ch);
		if (of_node_name_eq(ch, "ch-a"))
			ch_a = of_node_get(ch);
	}
	if (ch_def == NULL && !force_scc)
		goto bail;

	ch = ch_def ? ch_def : ch_a;

	/* Get address within mac-io ASIC */
	reg = of_get_property(escc, "reg", NULL);
	if (reg == NULL)
		goto bail;
	addr = reg[0];

	/* Get address of mac-io PCI itself */
	reg = of_get_property(macio, "assigned-addresses", NULL);
	if (reg == NULL)
		goto bail;
	addr += reg[2];

	/* Lock the serial port */
	pmac_call_feature(PMAC_FTR_SCC_ENABLE, ch,
			  PMAC_SCC_ASYNC | PMAC_SCC_FLAG_XMON, 1);

	if (ch == ch_a)
		addr += 0x20;
	sccc = ioremap(addr & PAGE_MASK, PAGE_SIZE) ;
	sccc += addr & ~PAGE_MASK;
	sccd = sccc + 0x10;

	mb();

	for (i = 20000; i != 0; --i)
		in_8(sccc);
	out_8(sccc, 0x09);		/* reset A or B side */
	out_8(sccc, 0xc0);

	/* If SCC was the OF output port, read the BRG value, else
	 * Setup for 38400 or 57600 8N1 depending on the machine
	 */
	if (ch_def != NULL) {
		out_8(sccc, 13);
		scc_inittab[1] = in_8(sccc);
		out_8(sccc, 12);
		scc_inittab[3] = in_8(sccc);
	} else if (of_machine_is_compatible("RackMac1,1")
		   || of_machine_is_compatible("RackMac1,2")
		   || of_machine_is_compatible("MacRISC4")) {
		/* Xserves and G5s default to 57600 */
		scc_inittab[1] = 0;
		scc_inittab[3] = 0;
	} else {
		/* Others default to 38400 */
		scc_inittab[1] = 0;
		scc_inittab[3] = 1;
	}

	for (i = 0; i < sizeof(scc_inittab); ++i)
		out_8(sccc, scc_inittab[i]);


	udbg_putc = udbg_scc_putc;
	udbg_getc = udbg_scc_getc;
	udbg_getc_poll = udbg_scc_getc_poll;

	udbg_puts("Hello World !\n");

 bail:
	of_node_put(macio);
	of_node_put(escc);
	of_node_put(stdout);
	of_node_put(ch_def);
	of_node_put(ch_a);
}

#ifdef CONFIG_PPC64
static void udbg_real_scc_putc(char c)
{
	while ((real_readb(sccc) & SCC_TXRDY) == 0)
		;
	real_writeb(c, sccd);
	if (c == '\n')
		udbg_real_scc_putc('\r');
}

void __init udbg_init_pmac_realmode(void)
{
	sccc = (volatile u8 __iomem *)0x80013020ul;
	sccd = (volatile u8 __iomem *)0x80013030ul;

	udbg_putc = udbg_real_scc_putc;
	udbg_getc = NULL;
	udbg_getc_poll = NULL;
}
#endif /* CONFIG_PPC64 */
