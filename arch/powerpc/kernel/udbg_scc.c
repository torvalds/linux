/*
 * udbg for for zilog scc ports as found on Apple PowerMacs
 *
 * Copyright (C) 2001-2005 PPC 64 Team, IBM Corp
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */
#include <linux/config.h>
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

static void udbg_scc_putc(unsigned char c)
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

static unsigned char udbg_scc_getc(void)
{
	if (sccc) {
		while ((in_8(sccc) & SCC_RXRDY) == 0)
			;
		return in_8(sccd);
	}
	return 0;
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

void udbg_init_scc(struct device_node *np)
{
	u32 *reg;
	unsigned long addr;
	int i, x;

	if (np == NULL)
		np = of_find_node_by_name(NULL, "escc");
	if (np == NULL || np->parent == NULL)
		return;

	udbg_printf("found SCC...\n");
	/* Get address within mac-io ASIC */
	reg = (u32 *)get_property(np, "reg", NULL);
	if (reg == NULL)
		return;
	addr = reg[0];
	udbg_printf("local addr: %lx\n", addr);
	/* Get address of mac-io PCI itself */
	reg = (u32 *)get_property(np->parent, "assigned-addresses", NULL);
	if (reg == NULL)
		return;
	addr += reg[2];
	udbg_printf("final addr: %lx\n", addr);

	/* Setup for 57600 8N1 */
	addr += 0x20;
	sccc = (volatile u8 * __iomem) ioremap(addr & PAGE_MASK, PAGE_SIZE) ;
	sccc += addr & ~PAGE_MASK;
	sccd = sccc + 0x10;

	udbg_printf("ioremap result sccc: %p\n", sccc);
	mb();

	for (i = 20000; i != 0; --i)
		x = in_8(sccc);
	out_8(sccc, 0x09);		/* reset A or B side */
	out_8(sccc, 0xc0);
	for (i = 0; i < sizeof(scc_inittab); ++i)
		out_8(sccc, scc_inittab[i]);

	udbg_putc = udbg_scc_putc;
	udbg_getc = udbg_scc_getc;
	udbg_getc_poll = udbg_scc_getc_poll;

	udbg_puts("Hello World !\n");
}

static void udbg_real_scc_putc(unsigned char c)
{
	while ((real_readb(sccc) & SCC_TXRDY) == 0)
		;
	real_writeb(c, sccd);
	if (c == '\n')
		udbg_real_scc_putc('\r');
}

void udbg_init_pmac_realmode(void)
{
	sccc = (volatile u8 __iomem *)0x80013020ul;
	sccd = (volatile u8 __iomem *)0x80013030ul;

	udbg_putc = udbg_real_scc_putc;
	udbg_getc = NULL;
	udbg_getc_poll = NULL;
}
