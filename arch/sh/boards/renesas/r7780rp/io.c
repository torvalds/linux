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
#include <asm/r7780rp.h>
#include <asm/addrspace.h>
#include <asm/io.h>

static inline unsigned long port2adr(unsigned int port)
{
	if ((0x1f0 <= port && port < 0x1f8) || port == 0x3f6)
		if (port == 0x3f6)
			return (PA_AREA5_IO + 0x80c);
		else
			return (PA_AREA5_IO + 0x1000 + ((port-0x1f0) << 1));
	else
		maybebadio((unsigned long)port);

	return port;
}

static inline unsigned long port88796l(unsigned int port, int flag)
{
	unsigned long addr;

	if (flag)
		addr = PA_AX88796L + ((port - AX88796L_IO_BASE) << 1);
	else
		addr = PA_AX88796L + ((port - AX88796L_IO_BASE) << 1) + 0x1000;

	return addr;
}

/* The 7780 R7780RP-1 seems to have everything hooked */
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
	else if (PXSEG(port))
		return ctrl_inb(port);
	else if (is_pci_ioaddr(port) || shifted_port(port))
		return ctrl_inb(pci_ioaddr(port));

	return ctrl_inw(port2adr(port)) & 0xff;
}

u8 r7780rp_inb_p(unsigned long port)
{
	u8 v;

	if (CHECK_AX88796L_PORT(port))
		v = ctrl_inw(port88796l(port, 0)) & 0xff;
	else if (PXSEG(port))
		v = ctrl_inb(port);
	else if (is_pci_ioaddr(port) || shifted_port(port))
		v = ctrl_inb(pci_ioaddr(port));
	else
		v = ctrl_inw(port2adr(port)) & 0xff;

	ctrl_delay();

	return v;
}

u16 r7780rp_inw(unsigned long port)
{
	if (CHECK_AX88796L_PORT(port))
		maybebadio(port);
	else if (PXSEG(port))
		return ctrl_inw(port);
	else if (is_pci_ioaddr(port) || shifted_port(port))
		return ctrl_inw(pci_ioaddr(port));
	else
		maybebadio(port);

	return 0;
}

u32 r7780rp_inl(unsigned long port)
{
	if (CHECK_AX88796L_PORT(port))
		maybebadio(port);
	else if (PXSEG(port))
		return ctrl_inl(port);
	else if (is_pci_ioaddr(port) || shifted_port(port))
		return ctrl_inl(pci_ioaddr(port));
	else
		maybebadio(port);

	return 0;
}

void r7780rp_outb(u8 value, unsigned long port)
{
	if (CHECK_AX88796L_PORT(port))
		ctrl_outw(value, port88796l(port, 0));
	else if (PXSEG(port))
		ctrl_outb(value, port);
	else if (is_pci_ioaddr(port) || shifted_port(port))
		ctrl_outb(value, pci_ioaddr(port));
	else
		ctrl_outw(value, port2adr(port));
}

void r7780rp_outb_p(u8 value, unsigned long port)
{
	if (CHECK_AX88796L_PORT(port))
		ctrl_outw(value, port88796l(port, 0));
	else if (PXSEG(port))
		ctrl_outb(value, port);
	else if (is_pci_ioaddr(port) || shifted_port(port))
		ctrl_outb(value, pci_ioaddr(port));
	else
		ctrl_outw(value, port2adr(port));

	ctrl_delay();
}

void r7780rp_outw(u16 value, unsigned long port)
{
	if (CHECK_AX88796L_PORT(port))
		maybebadio(port);
	else if (PXSEG(port))
		ctrl_outw(value, port);
	else if (is_pci_ioaddr(port) || shifted_port(port))
		ctrl_outw(value, pci_ioaddr(port));
	else
		maybebadio(port);
}

void r7780rp_outl(u32 value, unsigned long port)
{
	if (CHECK_AX88796L_PORT(port))
		maybebadio(port);
	else if (PXSEG(port))
		ctrl_outl(value, port);
	else if (is_pci_ioaddr(port) || shifted_port(port))
		ctrl_outl(value, pci_ioaddr(port));
	else
		maybebadio(port);
}

void r7780rp_insb(unsigned long port, void *dst, unsigned long count)
{
	volatile u16 *p;
	u8 *buf = dst;

	if (CHECK_AX88796L_PORT(port)) {
		p = (volatile u16 *)port88796l(port, 0);
		while (count--)
			*buf++ = *p & 0xff;
	} else if (PXSEG(port)) {
		while (count--)
			*buf++ = *(volatile u8 *)port;
	} else if (is_pci_ioaddr(port) || shifted_port(port)) {
		volatile u8 *bp = (volatile u8 *)pci_ioaddr(port);

		while (count--)
			*buf++ = *bp;
	} else {
		p = (volatile u16 *)port2adr(port);
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
	else if (PXSEG(port))
		p = (volatile u16 *)port;
	else if (is_pci_ioaddr(port) || shifted_port(port))
		p = (volatile u16 *)pci_ioaddr(port);
	else
		p = (volatile u16 *)port2adr(port);

	while (count--)
		*buf++ = *p;
}

void r7780rp_insl(unsigned long port, void *dst, unsigned long count)
{
	u32 *buf = dst;

	if (CHECK_AX88796L_PORT(port))
		maybebadio(port);
	else if (is_pci_ioaddr(port) || shifted_port(port)) {
		volatile u32 *p = (volatile u32 *)pci_ioaddr(port);

		while (count--)
			*buf++ = *p;
	} else
		maybebadio(port);
}

void r7780rp_outsb(unsigned long port, const void *src, unsigned long count)
{
	volatile u16 *p;
	const u8 *buf = src;

	if (CHECK_AX88796L_PORT(port)) {
		p = (volatile u16 *)port88796l(port, 0);
		while (count--)
			*p = *buf++;
	} else if (PXSEG(port))
		while (count--)
			ctrl_outb(*buf++, port);
	else if (is_pci_ioaddr(port) || shifted_port(port)) {
		volatile u8 *bp = (volatile u8 *)pci_ioaddr(port);

		while (count--)
			*bp = *buf++;
	} else {
		p = (volatile u16 *)port2adr(port);
		while (count--)
			*p = *buf++;
	}
}

void r7780rp_outsw(unsigned long port, const void *src, unsigned long count)
{
	volatile u16 *p;
	const u16 *buf = src;

	if (CHECK_AX88796L_PORT(port))
		p = (volatile u16 *)port88796l(port, 1);
	else if (PXSEG(port))
		p = (volatile u16 *)port;
	else if (is_pci_ioaddr(port) || shifted_port(port))
		p = (volatile u16 *)pci_ioaddr(port);
	else
		p = (volatile u16 *)port2adr(port);

	while (count--)
		*p = *buf++;
}

void r7780rp_outsl(unsigned long port, const void *src, unsigned long count)
{
	const u32 *buf = src;

	if (CHECK_AX88796L_PORT(port))
		maybebadio(port);
	else if (is_pci_ioaddr(port) || shifted_port(port)) {
		volatile u32 *p = (volatile u32 *)pci_ioaddr(port);

		while (count--)
			*p = *buf++;
	} else
		maybebadio(port);
}

void __iomem *r7780rp_ioport_map(unsigned long port, unsigned int size)
{
	if (CHECK_AX88796L_PORT(port))
		return (void __iomem *)port88796l(port, size > 1);
	else if (PXSEG(port))
		return (void __iomem *)port;
	else if (is_pci_ioaddr(port) || shifted_port(port))
		return (void __iomem *)pci_ioaddr(port);

	return (void __iomem *)port2adr(port);
}
