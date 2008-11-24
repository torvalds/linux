/*
 * linux/arch/sh/boards/renesas/systemh/io.c
 *
 * Copyright (C) 2001  Ian da Silva, Jeremy Siegel
 * Based largely on io_se.c.
 *
 * I/O routine for Hitachi 7751 Systemh.
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <mach/systemh7751.h>
#include <asm/addrspace.h>
#include <asm/io.h>

#define ETHER_IOMAP(adr) (0xB3000000 + (adr)) /*map to 16bits access area
                                                of smc lan chip*/
static inline volatile __u16 *
port2adr(unsigned int port)
{
	if (port >= 0x2000)
		return (volatile __u16 *) (PA_MRSHPC + (port - 0x2000));
	maybebadio((unsigned long)port);
	return (volatile __u16*)port;
}

/*
 * General outline: remap really low stuff [eventually] to SuperIO,
 * stuff in PCI IO space (at or above window at pci.h:PCIBIOS_MIN_IO)
 * is mapped through the PCI IO window.  Stuff with high bits (PXSEG)
 * should be way beyond the window, and is used  w/o translation for
 * compatibility.
 */
unsigned char sh7751systemh_inb(unsigned long port)
{
	if (PXSEG(port))
		return *(volatile unsigned char *)port;
	else if (is_pci_ioaddr(port))
		return *(volatile unsigned char *)pci_ioaddr(port);
	else if (port <= 0x3F1)
		return *(volatile unsigned char *)ETHER_IOMAP(port);
	else
		return (*port2adr(port))&0xff;
}

unsigned char sh7751systemh_inb_p(unsigned long port)
{
	unsigned char v;

        if (PXSEG(port))
                v = *(volatile unsigned char *)port;
	else if (is_pci_ioaddr(port))
                v = *(volatile unsigned char *)pci_ioaddr(port);
	else if (port <= 0x3F1)
		v = *(volatile unsigned char *)ETHER_IOMAP(port);
	else
		v = (*port2adr(port))&0xff;
	ctrl_delay();
	return v;
}

unsigned short sh7751systemh_inw(unsigned long port)
{
        if (PXSEG(port))
                return *(volatile unsigned short *)port;
	else if (is_pci_ioaddr(port))
                return *(volatile unsigned short *)pci_ioaddr(port);
	else if (port >= 0x2000)
		return *port2adr(port);
	else if (port <= 0x3F1)
		return *(volatile unsigned int *)ETHER_IOMAP(port);
	else
		maybebadio(port);
	return 0;
}

unsigned int sh7751systemh_inl(unsigned long port)
{
        if (PXSEG(port))
                return *(volatile unsigned long *)port;
	else if (is_pci_ioaddr(port))
                return *(volatile unsigned int *)pci_ioaddr(port);
	else if (port >= 0x2000)
		return *port2adr(port);
	else if (port <= 0x3F1)
		return *(volatile unsigned int *)ETHER_IOMAP(port);
	else
		maybebadio(port);
	return 0;
}

void sh7751systemh_outb(unsigned char value, unsigned long port)
{

        if (PXSEG(port))
                *(volatile unsigned char *)port = value;
	else if (is_pci_ioaddr(port))
		*((unsigned char*)pci_ioaddr(port)) = value;
	else if (port <= 0x3F1)
		*(volatile unsigned char *)ETHER_IOMAP(port) = value;
	else
		*(port2adr(port)) = value;
}

void sh7751systemh_outb_p(unsigned char value, unsigned long port)
{
        if (PXSEG(port))
                *(volatile unsigned char *)port = value;
	else if (is_pci_ioaddr(port))
		*((unsigned char*)pci_ioaddr(port)) = value;
	else if (port <= 0x3F1)
		*(volatile unsigned char *)ETHER_IOMAP(port) = value;
	else
		*(port2adr(port)) = value;
	ctrl_delay();
}

void sh7751systemh_outw(unsigned short value, unsigned long port)
{
        if (PXSEG(port))
                *(volatile unsigned short *)port = value;
	else if (is_pci_ioaddr(port))
		*((unsigned short *)pci_ioaddr(port)) = value;
	else if (port >= 0x2000)
		*port2adr(port) = value;
	else if (port <= 0x3F1)
		*(volatile unsigned short *)ETHER_IOMAP(port) = value;
	else
		maybebadio(port);
}

void sh7751systemh_outl(unsigned int value, unsigned long port)
{
        if (PXSEG(port))
                *(volatile unsigned long *)port = value;
	else if (is_pci_ioaddr(port))
		*((unsigned long*)pci_ioaddr(port)) = value;
	else
		maybebadio(port);
}

void sh7751systemh_insb(unsigned long port, void *addr, unsigned long count)
{
	unsigned char *p = addr;
	while (count--) *p++ = sh7751systemh_inb(port);
}

void sh7751systemh_insw(unsigned long port, void *addr, unsigned long count)
{
	unsigned short *p = addr;
	while (count--) *p++ = sh7751systemh_inw(port);
}

void sh7751systemh_insl(unsigned long port, void *addr, unsigned long count)
{
	maybebadio(port);
}

void sh7751systemh_outsb(unsigned long port, const void *addr, unsigned long count)
{
	unsigned char *p = (unsigned char*)addr;
	while (count--) sh7751systemh_outb(*p++, port);
}

void sh7751systemh_outsw(unsigned long port, const void *addr, unsigned long count)
{
	unsigned short *p = (unsigned short*)addr;
	while (count--) sh7751systemh_outw(*p++, port);
}

void sh7751systemh_outsl(unsigned long port, const void *addr, unsigned long count)
{
	maybebadio(port);
}
