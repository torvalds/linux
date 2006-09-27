/* 
 * linux/arch/sh/kernel/io_7751se.c
 *
 * Copyright (C) 2002  David McCullough <davidm@snapgear.com>
 * Copyright (C) 2001  Ian da Silva, Jeremy Siegel
 * Based largely on io_se.c.
 *
 * I/O routine for Hitachi 7751 SolutionEngine.
 *
 * Initial version only to support LAN access; some
 * placeholder code from io_se.c left in with the
 * expectation of later SuperIO and PCMCIA access.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <asm/addrspace.h>

#include <asm/pci.h>
#include "../../drivers/pci/pci-sh7751.h"

#ifdef CONFIG_SH_SECUREEDGE5410
unsigned short secureedge5410_ioport;
#endif

/*
 * The SnapGear uses the built-in PCI controller (PCIC)
 * of the 7751 processor
 */

#define PCIIOBR		(volatile long *)PCI_REG(SH7751_PCIIOBR)
#define PCIMBR          (volatile long *)PCI_REG(SH7751_PCIMBR)
#define PCI_IO_AREA	SH7751_PCI_IO_BASE
#define PCI_MEM_AREA	SH7751_PCI_CONFIG_BASE

#define PCI_IOMAP(adr)	(PCI_IO_AREA + (adr & ~SH7751_PCIIOBR_MASK))

static inline void delay(void)
{
	ctrl_inw(0xa0000000);
}

static inline volatile __u16 *port2adr(unsigned int port)
{
	maybebadio((unsigned long)port);
	return (volatile __u16*)port;
}

/* In case someone configures the kernel w/o PCI support: in that */
/* scenario, don't ever bother to check for PCI-window addresses */

/* NOTE: WINDOW CHECK MAY BE A BIT OFF, HIGH PCIBIOS_MIN_IO WRAPS? */
#if defined(CONFIG_PCI)
#define CHECK_SH7751_PCIIO(port) \
  ((port >= PCIBIOS_MIN_IO) && (port < (PCIBIOS_MIN_IO + SH7751_PCI_IO_SIZE)))
#else
#define CHECK_SH7751_PCIIO(port) (0)
#endif

/*
 * General outline: remap really low stuff [eventually] to SuperIO,
 * stuff in PCI IO space (at or above window at pci.h:PCIBIOS_MIN_IO)
 * is mapped through the PCI IO window.  Stuff with high bits (PXSEG)
 * should be way beyond the window, and is used  w/o translation for
 * compatibility.
 */

unsigned char snapgear_inb(unsigned long port)
{
	if (PXSEG(port))
		return *(volatile unsigned char *)port;
	else if (CHECK_SH7751_PCIIO(port))
		return *(volatile unsigned char *)PCI_IOMAP(port);
	else
		return (*port2adr(port))&0xff; 
}


unsigned char snapgear_inb_p(unsigned long port)
{
	unsigned char v;

	if (PXSEG(port))
		v = *(volatile unsigned char *)port;
	else if (CHECK_SH7751_PCIIO(port))
		v = *(volatile unsigned char *)PCI_IOMAP(port);
	else
		v = (*port2adr(port))&0xff; 
	delay();
	return v;
}


unsigned short snapgear_inw(unsigned long port)
{
	if (PXSEG(port))
		return *(volatile unsigned short *)port;
	else if (CHECK_SH7751_PCIIO(port))
		return *(volatile unsigned short *)PCI_IOMAP(port);
	else if (port >= 0x2000)
		return *port2adr(port);
	else
		maybebadio(port);
	return 0;
}


unsigned int snapgear_inl(unsigned long port)
{
	if (PXSEG(port))
		return *(volatile unsigned long *)port;
	else if (CHECK_SH7751_PCIIO(port))
		return *(volatile unsigned int *)PCI_IOMAP(port);
	else if (port >= 0x2000)
		return *port2adr(port);
	else
		maybebadio(port);
	return 0;
}


void snapgear_outb(unsigned char value, unsigned long port)
{

	if (PXSEG(port))
		*(volatile unsigned char *)port = value;
	else if (CHECK_SH7751_PCIIO(port))
		*((unsigned char*)PCI_IOMAP(port)) = value;
	else
		*(port2adr(port)) = value;
}


void snapgear_outb_p(unsigned char value, unsigned long port)
{
	if (PXSEG(port))
		*(volatile unsigned char *)port = value;
	else if (CHECK_SH7751_PCIIO(port))
		*((unsigned char*)PCI_IOMAP(port)) = value;
	else
		*(port2adr(port)) = value;
	delay();
}


void snapgear_outw(unsigned short value, unsigned long port)
{
	if (PXSEG(port))
		*(volatile unsigned short *)port = value;
	else if (CHECK_SH7751_PCIIO(port))
		*((unsigned short *)PCI_IOMAP(port)) = value;
	else if (port >= 0x2000)
		*port2adr(port) = value;
	else
		maybebadio(port);
}


void snapgear_outl(unsigned int value, unsigned long port)
{
	if (PXSEG(port))
		*(volatile unsigned long *)port = value;
	else if (CHECK_SH7751_PCIIO(port))
		*((unsigned long*)PCI_IOMAP(port)) = value;
	else
		maybebadio(port);
}

void snapgear_insl(unsigned long port, void *addr, unsigned long count)
{
	maybebadio(port);
}

void snapgear_outsl(unsigned long port, const void *addr, unsigned long count)
{
	maybebadio(port);
}
