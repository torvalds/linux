/*
 * linux/arch/sh/boards/systemh/io.c
 *
 * Copyright (C) 2001  Ian da Silva, Jeremy Siegel
 * Based largely on io_se.c.
 *
 * I/O routine for Hitachi 7751 Systemh.
 *
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <asm/systemh/7751systemh.h>
#include <asm/addrspace.h>
#include <asm/io.h>

#include <linux/pci.h>
#include "../../drivers/pci/pci-sh7751.h"

/*
 * The 7751 SystemH Engine uses the built-in PCI controller (PCIC)
 * of the 7751 processor, and has a SuperIO accessible on its memory
 * bus.
 */

#define PCIIOBR		(volatile long *)PCI_REG(SH7751_PCIIOBR)
#define PCIMBR          (volatile long *)PCI_REG(SH7751_PCIMBR)
#define PCI_IO_AREA	SH7751_PCI_IO_BASE
#define PCI_MEM_AREA	SH7751_PCI_CONFIG_BASE

#define PCI_IOMAP(adr)	(PCI_IO_AREA + (adr & ~SH7751_PCIIOBR_MASK))
#define ETHER_IOMAP(adr) (0xB3000000 + (adr)) /*map to 16bits access area
                                                of smc lan chip*/

#define maybebadio(name,port) \
  printk("bad PC-like io %s for port 0x%lx at 0x%08x\n", \
	 #name, (port), (__u32) __builtin_return_address(0))

static inline void delay(void)
{
	ctrl_inw(0xa0000000);
}

static inline volatile __u16 *
port2adr(unsigned int port)
{
	if (port >= 0x2000)
		return (volatile __u16 *) (PA_MRSHPC + (port - 0x2000));
#if 0
	else
		return (volatile __u16 *) (PA_SUPERIO + (port << 1));
#endif
	maybebadio(name,(unsigned long)port);
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
unsigned char sh7751systemh_inb(unsigned long port)
{
	if (PXSEG(port))
		return *(volatile unsigned char *)port;
	else if (CHECK_SH7751_PCIIO(port))
		return *(volatile unsigned char *)PCI_IOMAP(port);
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
	else if (CHECK_SH7751_PCIIO(port))
                v = *(volatile unsigned char *)PCI_IOMAP(port);
	else if (port <= 0x3F1)
		v = *(volatile unsigned char *)ETHER_IOMAP(port);
	else
		v = (*port2adr(port))&0xff;
	delay();
	return v;
}

unsigned short sh7751systemh_inw(unsigned long port)
{
        if (PXSEG(port))
                return *(volatile unsigned short *)port;
	else if (CHECK_SH7751_PCIIO(port))
                return *(volatile unsigned short *)PCI_IOMAP(port);
	else if (port >= 0x2000)
		return *port2adr(port);
	else if (port <= 0x3F1)
		return *(volatile unsigned int *)ETHER_IOMAP(port);
	else
		maybebadio(inw, port);
	return 0;
}

unsigned int sh7751systemh_inl(unsigned long port)
{
        if (PXSEG(port))
                return *(volatile unsigned long *)port;
	else if (CHECK_SH7751_PCIIO(port))
                return *(volatile unsigned int *)PCI_IOMAP(port);
	else if (port >= 0x2000)
		return *port2adr(port);
	else if (port <= 0x3F1)
		return *(volatile unsigned int *)ETHER_IOMAP(port);
	else
		maybebadio(inl, port);
	return 0;
}

void sh7751systemh_outb(unsigned char value, unsigned long port)
{

        if (PXSEG(port))
                *(volatile unsigned char *)port = value;
	else if (CHECK_SH7751_PCIIO(port))
        	*((unsigned char*)PCI_IOMAP(port)) = value;
	else if (port <= 0x3F1)
		*(volatile unsigned char *)ETHER_IOMAP(port) = value;
	else
		*(port2adr(port)) = value;
}

void sh7751systemh_outb_p(unsigned char value, unsigned long port)
{
        if (PXSEG(port))
                *(volatile unsigned char *)port = value;
	else if (CHECK_SH7751_PCIIO(port))
        	*((unsigned char*)PCI_IOMAP(port)) = value;
	else if (port <= 0x3F1)
		*(volatile unsigned char *)ETHER_IOMAP(port) = value;
	else
		*(port2adr(port)) = value;
	delay();
}

void sh7751systemh_outw(unsigned short value, unsigned long port)
{
        if (PXSEG(port))
                *(volatile unsigned short *)port = value;
	else if (CHECK_SH7751_PCIIO(port))
        	*((unsigned short *)PCI_IOMAP(port)) = value;
	else if (port >= 0x2000)
		*port2adr(port) = value;
	else if (port <= 0x3F1)
		*(volatile unsigned short *)ETHER_IOMAP(port) = value;
	else
		maybebadio(outw, port);
}

void sh7751systemh_outl(unsigned int value, unsigned long port)
{
        if (PXSEG(port))
                *(volatile unsigned long *)port = value;
	else if (CHECK_SH7751_PCIIO(port))
        	*((unsigned long*)PCI_IOMAP(port)) = value;
	else
		maybebadio(outl, port);
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
	maybebadio(insl, port);
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
	maybebadio(outsw, port);
}

/* For read/write calls, just copy generic (pass-thru); PCIMBR is  */
/* already set up.  For a larger memory space, these would need to */
/* reset PCIMBR as needed on a per-call basis...                   */

unsigned char sh7751systemh_readb(unsigned long addr)
{
	return *(volatile unsigned char*)addr;
}

unsigned short sh7751systemh_readw(unsigned long addr)
{
	return *(volatile unsigned short*)addr;
}

unsigned int sh7751systemh_readl(unsigned long addr)
{
	return *(volatile unsigned long*)addr;
}

void sh7751systemh_writeb(unsigned char b, unsigned long addr)
{
	*(volatile unsigned char*)addr = b;
}

void sh7751systemh_writew(unsigned short b, unsigned long addr)
{
	*(volatile unsigned short*)addr = b;
}

void sh7751systemh_writel(unsigned int b, unsigned long addr)
{
        *(volatile unsigned long*)addr = b;
}



/* Map ISA bus address to the real address. Only for PCMCIA.  */

/* ISA page descriptor.  */
static __u32 sh_isa_memmap[256];

#if 0
static int
sh_isa_mmap(__u32 start, __u32 length, __u32 offset)
{
	int idx;

	if (start >= 0x100000 || (start & 0xfff) || (length != 0x1000))
		return -1;

	idx = start >> 12;
	sh_isa_memmap[idx] = 0xb8000000 + (offset &~ 0xfff);
	printk("sh_isa_mmap: start %x len %x offset %x (idx %x paddr %x)\n",
	       start, length, offset, idx, sh_isa_memmap[idx]);
	return 0;
}
#endif

unsigned long
sh7751systemh_isa_port2addr(unsigned long offset)
{
	int idx;

	idx = (offset >> 12) & 0xff;
	offset &= 0xfff;
	return sh_isa_memmap[idx] + offset;
}
