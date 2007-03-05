/*
 * Copyright (C) 2001  Ian da Silva, Jeremy Siegel
 * Based largely on io_se.c.
 *
 * I/O routine for Renesas Solutions Highlander R7780RP-1
 *
 * Initial version only to support LAN access; some
 * placeholder code from io_r7780rp.c left in with the
 * expectation of later SuperIO and PCMCIA access.
 */
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/io.h>
#include <asm/r7780rp.h>
#include <asm/addrspace.h>

static inline unsigned long port88796l(unsigned int port, int flag)
{
	unsigned long addr;

	if (flag)
		addr = PA_AX88796L + ((port - AX88796L_IO_BASE) << 1);
	else
		addr = PA_AX88796L + ((port - AX88796L_IO_BASE) << 1) + 0x1000;

	return addr;
}

#if defined(CONFIG_NE2000) || defined(CONFIG_NE2000_MODULE)
#define CHECK_AX88796L_PORT(port) \
  ((port >= AX88796L_IO_BASE) && (port < (AX88796L_IO_BASE+0x20)))
#else
#define CHECK_AX88796L_PORT(port) (0)
#endif

/*
 * General outline: remap really low stuff [eventually] to SuperIO,
 * stuff in PCI IO space (at or above window at pci.h:PCIBIOS_MIN_IO)
 * is mapped through the PCI IO window.  Stuff with high bits (PXSEG)
 * should be way beyond the window, and is used  w/o translation for
 * compatibility.
 */
u8 r7780rp_inb(unsigned long port)
{
	if (CHECK_AX88796L_PORT(port))
		return ctrl_inw(port88796l(port, 0)) & 0xff;
	else if (is_pci_ioaddr(port))
		return ctrl_inb(pci_ioaddr(port));

	return ctrl_inw(port) & 0xff;
}

u8 r7780rp_inb_p(unsigned long port)
{
	u8 v;

	if (CHECK_AX88796L_PORT(port))
		v = ctrl_inw(port88796l(port, 0)) & 0xff;
	else if (is_pci_ioaddr(port))
		v = ctrl_inb(pci_ioaddr(port));
	else
		v = ctrl_inw(port) & 0xff;

	ctrl_delay();

	return v;
}

u16 r7780rp_inw(unsigned long port)
{
	if (is_pci_ioaddr(port))
		return ctrl_inw(pci_ioaddr(port));

	return ctrl_inw(port);
}

u32 r7780rp_inl(unsigned long port)
{
	if (is_pci_ioaddr(port))
		return ctrl_inl(pci_ioaddr(port));

	return ctrl_inl(port);
}

void r7780rp_outb(u8 value, unsigned long port)
{
	if (CHECK_AX88796L_PORT(port))
		ctrl_outw(value, port88796l(port, 0));
	else if (is_pci_ioaddr(port))
		ctrl_outb(value, pci_ioaddr(port));
	else
		ctrl_outb(value, port);
}

void r7780rp_outb_p(u8 value, unsigned long port)
{
	if (CHECK_AX88796L_PORT(port))
		ctrl_outw(value, port88796l(port, 0));
	else if (is_pci_ioaddr(port))
		ctrl_outb(value, pci_ioaddr(port));
	else
		ctrl_outb(value, port);

	ctrl_delay();
}

void r7780rp_outw(u16 value, unsigned long port)
{
	if (is_pci_ioaddr(port))
		ctrl_outw(value, pci_ioaddr(port));
	else
		ctrl_outw(value, port);
}

void r7780rp_outl(u32 value, unsigned long port)
{
	if (is_pci_ioaddr(port))
		ctrl_outl(value, pci_ioaddr(port));
	else
		ctrl_outl(value, port);
}

void r7780rp_insb(unsigned long port, void *dst, unsigned long count)
{
	volatile u16 *p;
	u8 *buf = dst;

	if (CHECK_AX88796L_PORT(port)) {
		p = (volatile u16 *)port88796l(port, 0);
		while (count--)
			*buf++ = *p & 0xff;
	} else if (is_pci_ioaddr(port)) {
		volatile u8 *bp = (volatile u8 *)pci_ioaddr(port);

		while (count--)
			*buf++ = *bp;
	} else {
		p = (volatile u16 *)port;
		while (count--)
			*buf++ = *p & 0xff;
	}
}

void r7780rp_insw(unsigned long port, void *dst, unsigned long count)
{
	volatile u16 *p;
	u16 *buf = dst;

	if (CHECK_AX88796L_PORT(port))
		p = (volatile u16 *)port88796l(port, 1);
	else if (is_pci_ioaddr(port))
		p = (volatile u16 *)pci_ioaddr(port);
	else
		p = (volatile u16 *)port;

	while (count--)
		*buf++ = *p;
}

void r7780rp_insl(unsigned long port, void *dst, unsigned long count)
{
	if (is_pci_ioaddr(port)) {
		volatile u32 *p = (volatile u32 *)pci_ioaddr(port);
		u32 *buf = dst;

		while (count--)
			*buf++ = *p;
	}
}

void r7780rp_outsb(unsigned long port, const void *src, unsigned long count)
{
	volatile u16 *p;
	const u8 *buf = src;

	if (CHECK_AX88796L_PORT(port)) {
		p = (volatile u16 *)port88796l(port, 0);
		while (count--)
			*p = *buf++;
	} else if (is_pci_ioaddr(port)) {
		volatile u8 *bp = (volatile u8 *)pci_ioaddr(port);

		while (count--)
			*bp = *buf++;
	} else
		while (count--)
			ctrl_outb(*buf++, port);
}

void r7780rp_outsw(unsigned long port, const void *src, unsigned long count)
{
	volatile u16 *p;
	const u16 *buf = src;

	if (CHECK_AX88796L_PORT(port))
		p = (volatile u16 *)port88796l(port, 1);
	else if (is_pci_ioaddr(port))
		p = (volatile u16 *)pci_ioaddr(port);
	else
		p = (volatile u16 *)port;

	while (count--)
		*p = *buf++;
}

void r7780rp_outsl(unsigned long port, const void *src, unsigned long count)
{
	const u32 *buf = src;
	u32 *p;

	if (is_pci_ioaddr(port))
		p = (u32 *)pci_ioaddr(port);
	else
		p = (u32 *)port;

	while (count--)
		ctrl_outl(*buf++, (unsigned long)p);
}

void __iomem *r7780rp_ioport_map(unsigned long port, unsigned int size)
{
	if (CHECK_AX88796L_PORT(port))
		return (void __iomem *)port88796l(port, size > 1);
	else if (is_pci_ioaddr(port))
		return (void __iomem *)pci_ioaddr(port);

	return (void __iomem *)port;
}
