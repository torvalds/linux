/*
 * arch/sh/boards/landisk/io.c
 *
 * Copyright (C) 2001  Ian da Silva, Jeremy Siegel
 * Based largely on io_se.c.
 *
 * I/O routine for I-O Data Device, Inc. LANDISK.
 *
 * Initial version only to support LAN access; some
 * placeholder code from io_landisk.c left in with the
 * expectation of later SuperIO and PCMCIA access.
 */
/*
 * modifed by kogiidena
 * 2005.03.03
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <asm/landisk/iodata_landisk.h>
#include <asm/addrspace.h>
#include <asm/io.h>

#include <linux/module.h>
#include <linux/pci.h>
#include "../../drivers/pci/pci-sh7751.h"

extern void *area5_io_base;	/* Area 5 I/O Base address */
extern void *area6_io_base;	/* Area 6 I/O Base address */

/*
 * The 7751R LANDISK uses the built-in PCI controller (PCIC)
 * of the 7751R processor, and has a SuperIO accessible via the PCI.
 * The board also includes a PCMCIA controller on its memory bus,
 * like the other Solution Engine boards.
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

static inline unsigned long port2adr(unsigned int port)
{
	if ((0x1f0 <= port && port < 0x1f8) || port == 0x3f6)
		if (port == 0x3f6)
			return ((unsigned long)area5_io_base + 0x2c);
		else
			return ((unsigned long)area5_io_base + PA_PIDE_OFFSET +
				((port - 0x1f0) << 1));
	else if ((0x170 <= port && port < 0x178) || port == 0x376)
		if (port == 0x376)
			return ((unsigned long)area6_io_base + 0x2c);
		else
			return ((unsigned long)area6_io_base + PA_SIDE_OFFSET +
				((port - 0x170) << 1));
	else
		maybebadio((unsigned long)port);

	return port;
}

/* In case someone configures the kernel w/o PCI support: in that */
/* scenario, don't ever bother to check for PCI-window addresses */

/* NOTE: WINDOW CHECK MAY BE A BIT OFF, HIGH PCIBIOS_MIN_IO WRAPS? */
#if defined(CONFIG_PCI)
#define CHECK_SH7751_PCIIO(port) \
  ((port >= PCIBIOS_MIN_IO) && (port < (PCIBIOS_MIN_IO + SH7751_PCI_IO_SIZE)))
#else
#define CHECK_SH_7751_PCIIO(port) (0)
#endif

/*
 * General outline: remap really low stuff [eventually] to SuperIO,
 * stuff in PCI IO space (at or above window at pci.h:PCIBIOS_MIN_IO)
 * is mapped through the PCI IO window.  Stuff with high bits (PXSEG)
 * should be way beyond the window, and is used  w/o translation for
 * compatibility.
 */
u8 landisk_inb(unsigned long port)
{
	if (PXSEG(port))
		return ctrl_inb(port);
	else if (CHECK_SH7751_PCIIO(port))
		return ctrl_inb(PCI_IOMAP(port));

	return ctrl_inw(port2adr(port)) & 0xff;
}

u8 landisk_inb_p(unsigned long port)
{
	u8 v;

	if (PXSEG(port))
		v = ctrl_inb(port);
	else if (CHECK_SH7751_PCIIO(port))
		v = ctrl_inb(PCI_IOMAP(port));
	else
		v = ctrl_inw(port2adr(port)) & 0xff;

	delay();

	return v;
}

u16 landisk_inw(unsigned long port)
{
	if (PXSEG(port))
		return ctrl_inw(port);
	else if (CHECK_SH7751_PCIIO(port))
		return ctrl_inw(PCI_IOMAP(port));
	else
		maybebadio(port);

	return 0;
}

u32 landisk_inl(unsigned long port)
{
	if (PXSEG(port))
		return ctrl_inl(port);
	else if (CHECK_SH7751_PCIIO(port))
		return ctrl_inl(PCI_IOMAP(port));
	else
		maybebadio(port);

	return 0;
}

void landisk_outb(u8 value, unsigned long port)
{
	if (PXSEG(port))
		ctrl_outb(value, port);
	else if (CHECK_SH7751_PCIIO(port))
		ctrl_outb(value, PCI_IOMAP(port));
	else
		ctrl_outw(value, port2adr(port));
}

void landisk_outb_p(u8 value, unsigned long port)
{
	if (PXSEG(port))
		ctrl_outb(value, port);
	else if (CHECK_SH7751_PCIIO(port))
		ctrl_outb(value, PCI_IOMAP(port));
	else
		ctrl_outw(value, port2adr(port));
	delay();
}

void landisk_outw(u16 value, unsigned long port)
{
	if (PXSEG(port))
		ctrl_outw(value, port);
	else if (CHECK_SH7751_PCIIO(port))
		ctrl_outw(value, PCI_IOMAP(port));
	else
		maybebadio(port);
}

void landisk_outl(u32 value, unsigned long port)
{
	if (PXSEG(port))
		ctrl_outl(value, port);
	else if (CHECK_SH7751_PCIIO(port))
		ctrl_outl(value, PCI_IOMAP(port));
	else
		maybebadio(port);
}

void landisk_insb(unsigned long port, void *dst, unsigned long count)
{
        volatile u16 *p;
        u8 *buf = dst;

        if (PXSEG(port)) {
                while (count--)
                        *buf++ = *(volatile u8 *)port;
	} else if (CHECK_SH7751_PCIIO(port)) {
                volatile u8 *bp = (volatile u8 *)PCI_IOMAP(port);

                while (count--)
                        *buf++ = *bp;
	} else {
                p = (volatile u16 *)port2adr(port);
                while (count--)
                        *buf++ = *p & 0xff;
	}
}

void landisk_insw(unsigned long port, void *dst, unsigned long count)
{
        volatile u16 *p;
        u16 *buf = dst;

	if (PXSEG(port))
		p = (volatile u16 *)port;
	else if (CHECK_SH7751_PCIIO(port))
		p = (volatile u16 *)PCI_IOMAP(port);
	else
		p = (volatile u16 *)port2adr(port);
	while (count--)
		*buf++ = *p;
}

void landisk_insl(unsigned long port, void *dst, unsigned long count)
{
        u32 *buf = dst;

	if (CHECK_SH7751_PCIIO(port)) {
                volatile u32 *p = (volatile u32 *)PCI_IOMAP(port);

                while (count--)
                        *buf++ = *p;
	} else
		maybebadio(port);
}

void landisk_outsb(unsigned long port, const void *src, unsigned long count)
{
        volatile u16 *p;
        const u8 *buf = src;

	if (PXSEG(port))
                while (count--)
                        ctrl_outb(*buf++, port);
	else if (CHECK_SH7751_PCIIO(port)) {
                volatile u8 *bp = (volatile u8 *)PCI_IOMAP(port);

                while (count--)
                        *bp = *buf++;
	} else {
                p = (volatile u16 *)port2adr(port);
                while (count--)
                        *p = *buf++;
	}
}

void landisk_outsw(unsigned long port, const void *src, unsigned long count)
{
        volatile u16 *p;
        const u16 *buf = src;

	if (PXSEG(port))
                p = (volatile u16 *)port;
	else if (CHECK_SH7751_PCIIO(port))
                p = (volatile u16 *)PCI_IOMAP(port);
	else
                p = (volatile u16 *)port2adr(port);

        while (count--)
                *p = *buf++;
}

void landisk_outsl(unsigned long port, const void *src, unsigned long count)
{
        const u32 *buf = src;

	if (CHECK_SH7751_PCIIO(port)) {
                volatile u32 *p = (volatile u32 *)PCI_IOMAP(port);

                while (count--)
                        *p = *buf++;
	} else
		maybebadio(port);
}

void __iomem *landisk_ioport_map(unsigned long port, unsigned int size)
{
        if (PXSEG(port))
                return (void __iomem *)port;
        else if (CHECK_SH7751_PCIIO(port))
                return (void __iomem *)PCI_IOMAP(port);

        return (void __iomem *)port2adr(port);
}
