/*
 * linux/arch/sh/kernel/io_hs7751rvoip.c
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
#include <asm/io.h>
#include <asm/hs7751rvoip/hs7751rvoip.h>
#include <asm/addrspace.h>

#include <linux/module.h>
#include <linux/pci.h>
#include "../../../drivers/pci/pci-sh7751.h"

extern void *area6_io8_base;	/* Area 6 8bit I/O Base address */
extern void *area5_io16_base;	/* Area 5 16bit I/O Base address */

/*
 * The 7751R HS7751RVoIP uses the built-in PCI controller (PCIC)
 * of the 7751R processor, and has a SuperIO accessible via the PCI.
 * The board also includes a PCMCIA controller on its memory bus,
 * like the other Solution Engine boards.
 */

#define PCIIOBR		(volatile long *)PCI_REG(SH7751_PCIIOBR)
#define PCIMBR          (volatile long *)PCI_REG(SH7751_PCIMBR)
#define PCI_IO_AREA	SH7751_PCI_IO_BASE
#define PCI_MEM_AREA	SH7751_PCI_CONFIG_BASE
#define PCI_IOMAP(adr)	(PCI_IO_AREA + (adr & ~SH7751_PCIIOBR_MASK))

#define CODEC_IO_BASE	0x1000
#define CODEC_IOMAP(a)	((unsigned long)area6_io8_base + ((a) - CODEC_IO_BASE))

static inline void delay(void)
{
	ctrl_inw(0xa0000000);
}

static inline unsigned long port2adr(unsigned int port)
{
	if ((0x1f0 <= port && port < 0x1f8) || port == 0x3f6)
		if (port == 0x3f6)
			return ((unsigned long)area5_io16_base + 0x0c);
		else
			return ((unsigned long)area5_io16_base + 0x800 + ((port-0x1f0) << 1));
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
static inline int
codec_port(unsigned long port)
{
	if (CODEC_IO_BASE <= port && port < (CODEC_IO_BASE+0x20))
		return 1;
	else
		return 0;
}
#endif

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
unsigned char hs7751rvoip_inb(unsigned long port)
{
	if (PXSEG(port))
		return ctrl_inb(port);
#if defined(CONFIG_HS7751RVOIP_CODEC)
	else if (codec_port(port))
		return ctrl_inb(CODEC_IOMAP(port));
#endif
	else if (CHECK_SH7751_PCIIO(port) || shifted_port(port))
		return ctrl_inb(PCI_IOMAP(port));
	else
		return ctrl_inw(port2adr(port)) & 0xff;
}

unsigned char hs7751rvoip_inb_p(unsigned long port)
{
	unsigned char v;

        if (PXSEG(port))
		v = ctrl_inb(port);
#if defined(CONFIG_HS7751RVOIP_CODEC)
	else if (codec_port(port))
		v = ctrl_inb(CODEC_IOMAP(port));
#endif
	else if (CHECK_SH7751_PCIIO(port) || shifted_port(port))
		v = ctrl_inb(PCI_IOMAP(port));
	else
		v = ctrl_inw(port2adr(port)) & 0xff;
	delay();
	return v;
}

unsigned short hs7751rvoip_inw(unsigned long port)
{
        if (PXSEG(port))
		return ctrl_inw(port);
	else if (CHECK_SH7751_PCIIO(port) || shifted_port(port))
		return ctrl_inw(PCI_IOMAP(port));
	else
		maybebadio(port);
	return 0;
}

unsigned int hs7751rvoip_inl(unsigned long port)
{
        if (PXSEG(port))
		return ctrl_inl(port);
	else if (CHECK_SH7751_PCIIO(port) || shifted_port(port))
		return ctrl_inl(PCI_IOMAP(port));
	else
		maybebadio(port);
	return 0;
}

void hs7751rvoip_outb(unsigned char value, unsigned long port)
{

        if (PXSEG(port))
		ctrl_outb(value, port);
#if defined(CONFIG_HS7751RVOIP_CODEC)
	else if (codec_port(port))
		ctrl_outb(value, CODEC_IOMAP(port));
#endif
	else if (CHECK_SH7751_PCIIO(port) || shifted_port(port))
		ctrl_outb(value, PCI_IOMAP(port));
	else
		ctrl_outb(value, port2adr(port));
}

void hs7751rvoip_outb_p(unsigned char value, unsigned long port)
{
        if (PXSEG(port))
		ctrl_outb(value, port);
#if defined(CONFIG_HS7751RVOIP_CODEC)
	else if (codec_port(port))
		ctrl_outb(value, CODEC_IOMAP(port));
#endif
	else if (CHECK_SH7751_PCIIO(port) || shifted_port(port))
		ctrl_outb(value, PCI_IOMAP(port));
	else
		ctrl_outw(value, port2adr(port));

	delay();
}

void hs7751rvoip_outw(unsigned short value, unsigned long port)
{
        if (PXSEG(port))
		ctrl_outw(value, port);
	else if (CHECK_SH7751_PCIIO(port) || shifted_port(port))
		ctrl_outw(value, PCI_IOMAP(port));
	else
		maybebadio(port);
}

void hs7751rvoip_outl(unsigned int value, unsigned long port)
{
        if (PXSEG(port))
		ctrl_outl(value, port);
	else if (CHECK_SH7751_PCIIO(port) || shifted_port(port))
		ctrl_outl(value, PCI_IOMAP(port));
	else
		maybebadio(port);
}

void hs7751rvoip_insb(unsigned long port, void *addr, unsigned long count)
{
	u8 *buf = addr;

	if (PXSEG(port))
		while (count--)
			*buf++ = ctrl_inb(port);
#if defined(CONFIG_HS7751RVOIP_CODEC)
	else if (codec_port(port))
		while (count--)
			*buf++ = ctrl_inb(CODEC_IOMAP(port));
#endif
	else if (CHECK_SH7751_PCIIO(port) || shifted_port(port)) {
		volatile u8 *bp = (volatile u8 *)PCI_IOMAP(port);

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
	else if (CHECK_SH7751_PCIIO(port) || shifted_port(port))
		p = (volatile u16 *)PCI_IOMAP(port);
	else
		p = (volatile u16 *)port2adr(port);
	while (count--)
		*buf++ = *p;
}

void hs7751rvoip_insl(unsigned long port, void *addr, unsigned long count)
{

	if (CHECK_SH7751_PCIIO(port) || shifted_port(port)) {
		volatile u32 *p = (volatile u32 *)PCI_IOMAP(port);
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
#if defined(CONFIG_HS7751RVOIP_CODEC)
	else if (codec_port(port))
		while (count--)
			ctrl_outb(*buf++, CODEC_IOMAP(port));
#endif
	else if (CHECK_SH7751_PCIIO(port) || shifted_port(port)) {
		volatile u8 *bp = (volatile u8 *)PCI_IOMAP(port);

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
	else if (CHECK_SH7751_PCIIO(port) || shifted_port(port))
		p = (volatile u16 *)PCI_IOMAP(port);
	else
		p = (volatile u16 *)port2adr(port);

	while (count--)
		*p = *buf++;
}

void hs7751rvoip_outsl(unsigned long port, const void *addr, unsigned long count)
{
	const u32 *buf = addr;

	if (CHECK_SH7751_PCIIO(port) || shifted_port(port)) {
		volatile u32 *p = (volatile u32 *)PCI_IOMAP(port);

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
        else if (CHECK_SH7751_PCIIO(port))
                return (void __iomem *)PCI_IOMAP(port);

        return (void __iomem *)port2adr(port);
}
