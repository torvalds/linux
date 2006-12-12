/*
 * linux/arch/sh/boards/renesas/hs7751rvoip/io.c
 *
 * Copyright (C) 2001  Ian da Silva, Jeremy Siegel
 * Based largely on io_se.c.
 *
 * I/O routine for Renesas Technology sales HS7751RVoIP
 *
 * Initial version only to support LAN access; some
 * placeholder code from io_hs7751rvoip.c left in with the
 * expectation of later SuperIO and PCMCIA access.
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <asm/hs7751rvoip.h>
#include <asm/addrspace.h>

extern void *area6_io8_base;	/* Area 6 8bit I/O Base address */
extern void *area5_io16_base;	/* Area 5 16bit I/O Base address */

/*
 * The 7751R HS7751RVoIP uses the built-in PCI controller (PCIC)
 * of the 7751R processor, and has a SuperIO accessible via the PCI.
 * The board also includes a PCMCIA controller on its memory bus,
 * like the other Solution Engine boards.
 */

#define CODEC_IO_BASE	0x1000
#define CODEC_IOMAP(a)	((unsigned long)area6_io8_base + ((a) - CODEC_IO_BASE))

static inline unsigned long port2adr(unsigned int port)
{
	if ((0x1f0 <= port && port < 0x1f8) || port == 0x3f6)
		if (port == 0x3f6)
			return ((unsigned long)area5_io16_base + 0x0c);
		else
			return ((unsigned long)area5_io16_base + 0x800 +
				((port-0x1f0) << 1));
	else
		maybebadio((unsigned long)port);
	return port;
}

/* The 7751R HS7751RVoIP seems to have everything hooked */
/* up pretty normally (nothing on high-bytes only...) so this */
/* shouldn't be needed */
static inline int shifted_port(unsigned long port)
{
	/* For IDE registers, value is not shifted */
	if ((0x1f0 <= port && port < 0x1f8) || port == 0x3f6)
		return 0;
	else
		return 1;
}

#if defined(CONFIG_HS7751RVOIP_CODEC)
#define codec_port(port)	\
	((CODEC_IO_BASE <= (port)) && ((port) < (CODEC_IO_BASE + 0x20)))
#else
#define codec_port(port)	(0)
#endif

/*
 * General outline: remap really low stuff [eventually] to SuperIO,
 * stuff in PCI IO space (at or above window at pci.h:PCIBIOS_MIN_IO)
 * is mapped through the PCI IO window.  Stuff with high bits (PXSEG)
 * should be way beyond the window, and is used  w/o translation for
 * compatibility.
 */
unsigned char hs7751rvoip_inb(unsigned long port)
{
	if (PXSEG(port))
		return ctrl_inb(port);
	else if (codec_port(port))
		return ctrl_inb(CODEC_IOMAP(port));
	else if (is_pci_ioaddr(port) || shifted_port(port))
		return ctrl_inb(pci_ioaddr(port));
	else
		return ctrl_inw(port2adr(port)) & 0xff;
}

unsigned char hs7751rvoip_inb_p(unsigned long port)
{
	unsigned char v;

        if (PXSEG(port))
		v = ctrl_inb(port);
	else if (codec_port(port))
		v = ctrl_inb(CODEC_IOMAP(port));
	else if (is_pci_ioaddr(port) || shifted_port(port))
		v = ctrl_inb(pci_ioaddr(port));
	else
		v = ctrl_inw(port2adr(port)) & 0xff;
	ctrl_delay();
	return v;
}

unsigned short hs7751rvoip_inw(unsigned long port)
{
        if (PXSEG(port))
		return ctrl_inw(port);
	else if (is_pci_ioaddr(port) || shifted_port(port))
		return ctrl_inw(pci_ioaddr(port));
	else
		maybebadio(port);
	return 0;
}

unsigned int hs7751rvoip_inl(unsigned long port)
{
        if (PXSEG(port))
		return ctrl_inl(port);
	else if (is_pci_ioaddr(port) || shifted_port(port))
		return ctrl_inl(pci_ioaddr(port));
	else
		maybebadio(port);
	return 0;
}

void hs7751rvoip_outb(unsigned char value, unsigned long port)
{

        if (PXSEG(port))
		ctrl_outb(value, port);
	else if (codec_port(port))
		ctrl_outb(value, CODEC_IOMAP(port));
	else if (is_pci_ioaddr(port) || shifted_port(port))
		ctrl_outb(value, pci_ioaddr(port));
	else
		ctrl_outb(value, port2adr(port));
}

void hs7751rvoip_outb_p(unsigned char value, unsigned long port)
{
        if (PXSEG(port))
		ctrl_outb(value, port);
	else if (codec_port(port))
		ctrl_outb(value, CODEC_IOMAP(port));
	else if (is_pci_ioaddr(port) || shifted_port(port))
		ctrl_outb(value, pci_ioaddr(port));
	else
		ctrl_outw(value, port2adr(port));

	ctrl_delay();
}

void hs7751rvoip_outw(unsigned short value, unsigned long port)
{
        if (PXSEG(port))
		ctrl_outw(value, port);
	else if (is_pci_ioaddr(port) || shifted_port(port))
		ctrl_outw(value, pci_ioaddr(port));
	else
		maybebadio(port);
}

void hs7751rvoip_outl(unsigned int value, unsigned long port)
{
        if (PXSEG(port))
		ctrl_outl(value, port);
	else if (is_pci_ioaddr(port) || shifted_port(port))
		ctrl_outl(value, pci_ioaddr(port));
	else
		maybebadio(port);
}

void hs7751rvoip_insb(unsigned long port, void *addr, unsigned long count)
{
	u8 *buf = addr;

	if (PXSEG(port))
		while (count--)
			*buf++ = ctrl_inb(port);
	else if (codec_port(port))
		while (count--)
			*buf++ = ctrl_inb(CODEC_IOMAP(port));
	else if (is_pci_ioaddr(port) || shifted_port(port)) {
		volatile u8 *bp = (volatile u8 *)pci_ioaddr(port);

		while (count--)
			*buf++ = *bp;
	} else {
		volatile u16 *p = (volatile u16 *)port2adr(port);

		while (count--)
			*buf++ = *p & 0xff;
	}
}

void hs7751rvoip_insw(unsigned long port, void *addr, unsigned long count)
{
	volatile u16 *p;
	u16 *buf = addr;

	if (PXSEG(port))
		p = (volatile u16 *)port;
	else if (is_pci_ioaddr(port) || shifted_port(port))
		p = (volatile u16 *)pci_ioaddr(port);
	else
		p = (volatile u16 *)port2adr(port);
	while (count--)
		*buf++ = *p;
}

void hs7751rvoip_insl(unsigned long port, void *addr, unsigned long count)
{

	if (is_pci_ioaddr(port) || shifted_port(port)) {
		volatile u32 *p = (volatile u32 *)pci_ioaddr(port);
		u32 *buf = addr;

		while (count--)
			*buf++ = *p;
	} else
		maybebadio(port);
}

void hs7751rvoip_outsb(unsigned long port, const void *addr, unsigned long count)
{
	const u8 *buf = addr;

	if (PXSEG(port))
		while (count--)
			ctrl_outb(*buf++, port);
	else if (codec_port(port))
		while (count--)
			ctrl_outb(*buf++, CODEC_IOMAP(port));
	else if (is_pci_ioaddr(port) || shifted_port(port)) {
		volatile u8 *bp = (volatile u8 *)pci_ioaddr(port);

		while (count--)
			*bp = *buf++;
	} else {
		volatile u16 *p = (volatile u16 *)port2adr(port);

		while (count--)
			*p = *buf++;
	}
}

void hs7751rvoip_outsw(unsigned long port, const void *addr, unsigned long count)
{
	volatile u16 *p;
	const u16 *buf = addr;

	if (PXSEG(port))
		p = (volatile u16 *)port;
	else if (is_pci_ioaddr(port) || shifted_port(port))
		p = (volatile u16 *)pci_ioaddr(port);
	else
		p = (volatile u16 *)port2adr(port);

	while (count--)
		*p = *buf++;
}

void hs7751rvoip_outsl(unsigned long port, const void *addr, unsigned long count)
{
	const u32 *buf = addr;

	if (is_pci_ioaddr(port) || shifted_port(port)) {
		volatile u32 *p = (volatile u32 *)pci_ioaddr(port);

		while (count--)
			*p = *buf++;
	} else
		maybebadio(port);
}

void __iomem *hs7751rvoip_ioport_map(unsigned long port, unsigned int size)
{
        if (PXSEG(port))
                return (void __iomem *)port;
	else if (unlikely(codec_port(port) && (size == 1)))
		return (void __iomem *)CODEC_IOMAP(port);
        else if (is_pci_ioaddr(port))
                return (void __iomem *)pci_ioaddr(port);

        return (void __iomem *)port2adr(port);
}
