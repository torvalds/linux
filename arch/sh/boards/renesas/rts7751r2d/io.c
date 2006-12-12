/*
 * Copyright (C) 2001  Ian da Silva, Jeremy Siegel
 * Based largely on io_se.c.
 *
 * I/O routine for Renesas Technology sales RTS7751R2D.
 *
 * Initial version only to support LAN access; some
 * placeholder code from io_rts7751r2d.c left in with the
 * expectation of later SuperIO and PCMCIA access.
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <asm/rts7751r2d.h>
#include <asm/addrspace.h>

/*
 * The 7751R RTS7751R2D uses the built-in PCI controller (PCIC)
 * of the 7751R processor, and has a SuperIO accessible via the PCI.
 * The board also includes a PCMCIA controller on its memory bus,
 * like the other Solution Engine boards.
 */

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

/* The 7751R RTS7751R2D seems to have everything hooked */
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
unsigned char rts7751r2d_inb(unsigned long port)
{
	if (CHECK_AX88796L_PORT(port))
		return (*(volatile unsigned short *)port88796l(port, 0)) & 0xff;
	else if (PXSEG(port))
		return *(volatile unsigned char *)port;
	else if (is_pci_ioaddr(port) || shifted_port(port))
		return *(volatile unsigned char *)pci_ioaddr(port);
	else
		return (*(volatile unsigned short *)port2adr(port) & 0xff);
}

unsigned char rts7751r2d_inb_p(unsigned long port)
{
	unsigned char v;

	if (CHECK_AX88796L_PORT(port))
		v = (*(volatile unsigned short *)port88796l(port, 0)) & 0xff;
        else if (PXSEG(port))
		v = *(volatile unsigned char *)port;
	else if (is_pci_ioaddr(port) || shifted_port(port))
		v = *(volatile unsigned char *)pci_ioaddr(port);
	else
		v = (*(volatile unsigned short *)port2adr(port) & 0xff);

	ctrl_delay();

	return v;
}

unsigned short rts7751r2d_inw(unsigned long port)
{
	if (CHECK_AX88796L_PORT(port))
		maybebadio(port);
        else if (PXSEG(port))
		return *(volatile unsigned short *)port;
	else if (is_pci_ioaddr(port) || shifted_port(port))
		return *(volatile unsigned short *)pci_ioaddr(port);
	else
		maybebadio(port);

	return 0;
}

unsigned int rts7751r2d_inl(unsigned long port)
{
	if (CHECK_AX88796L_PORT(port))
		maybebadio(port);
        else if (PXSEG(port))
		return *(volatile unsigned long *)port;
	else if (is_pci_ioaddr(port) || shifted_port(port))
		return *(volatile unsigned long *)pci_ioaddr(port);
	else
		maybebadio(port);

	return 0;
}

void rts7751r2d_outb(unsigned char value, unsigned long port)
{
	if (CHECK_AX88796L_PORT(port))
		*((volatile unsigned short *)port88796l(port, 0)) = value;
        else if (PXSEG(port))
		*(volatile unsigned char *)port = value;
	else if (is_pci_ioaddr(port) || shifted_port(port))
		*(volatile unsigned char *)pci_ioaddr(port) = value;
	else
		*(volatile unsigned short *)port2adr(port) = value;
}

void rts7751r2d_outb_p(unsigned char value, unsigned long port)
{
	if (CHECK_AX88796L_PORT(port))
		*((volatile unsigned short *)port88796l(port, 0)) = value;
        else if (PXSEG(port))
		*(volatile unsigned char *)port = value;
	else if (is_pci_ioaddr(port) || shifted_port(port))
		*(volatile unsigned char *)pci_ioaddr(port) = value;
	else
		*(volatile unsigned short *)port2adr(port) = value;

	ctrl_delay();
}

void rts7751r2d_outw(unsigned short value, unsigned long port)
{
	if (CHECK_AX88796L_PORT(port))
		maybebadio(port);
        else if (PXSEG(port))
		*(volatile unsigned short *)port = value;
	else if (is_pci_ioaddr(port) || shifted_port(port))
		*(volatile unsigned short *)pci_ioaddr(port) = value;
	else
		maybebadio(port);
}

void rts7751r2d_outl(unsigned int value, unsigned long port)
{
	if (CHECK_AX88796L_PORT(port))
		maybebadio(port);
        else if (PXSEG(port))
		*(volatile unsigned long *)port = value;
	else if (is_pci_ioaddr(port) || shifted_port(port))
		*(volatile unsigned long *)pci_ioaddr(port) = value;
	else
		maybebadio(port);
}

void rts7751r2d_insb(unsigned long port, void *addr, unsigned long count)
{
	unsigned long a = (unsigned long)addr;
	volatile __u8 *bp;
	volatile __u16 *p;

	if (CHECK_AX88796L_PORT(port)) {
		p = (volatile unsigned short *)port88796l(port, 0);
		while (count--)
			ctrl_outb(*p & 0xff, a++);
	} else if (PXSEG(port))
		while (count--)
			ctrl_outb(ctrl_inb(port), a++);
	else if (is_pci_ioaddr(port) || shifted_port(port)) {
		bp = (__u8 *)pci_ioaddr(port);
		while (count--)
			ctrl_outb(*bp, a++);
	} else {
		p = (volatile unsigned short *)port2adr(port);
		while (count--)
			ctrl_outb(*p & 0xff, a++);
	}
}

void rts7751r2d_insw(unsigned long port, void *addr, unsigned long count)
{
	unsigned long a = (unsigned long)addr;
	volatile __u16 *p;

	if (CHECK_AX88796L_PORT(port))
		p = (volatile unsigned short *)port88796l(port, 1);
	else if (PXSEG(port))
		p = (volatile unsigned short *)port;
	else if (is_pci_ioaddr(port) || shifted_port(port))
		p = (volatile unsigned short *)pci_ioaddr(port);
	else
		p = (volatile unsigned short *)port2adr(port);
	while (count--)
		ctrl_outw(*p, a++);
}

void rts7751r2d_insl(unsigned long port, void *addr, unsigned long count)
{
	if (CHECK_AX88796L_PORT(port))
		maybebadio(port);
	else if (is_pci_ioaddr(port) || shifted_port(port)) {
		unsigned long a = (unsigned long)addr;

		while (count--) {
			ctrl_outl(ctrl_inl(pci_ioaddr(port)), a);
			a += 4;
		}
	} else
		maybebadio(port);
}

void rts7751r2d_outsb(unsigned long port, const void *addr, unsigned long count)
{
	unsigned long a = (unsigned long)addr;
	volatile __u8 *bp;
	volatile __u16 *p;

	if (CHECK_AX88796L_PORT(port)) {
		p = (volatile unsigned short *)port88796l(port, 0);
		while (count--)
			*p = ctrl_inb(a++);
	} else if (PXSEG(port))
		while (count--)
			ctrl_outb(a++, port);
	else if (is_pci_ioaddr(port) || shifted_port(port)) {
		bp = (__u8 *)pci_ioaddr(port);
		while (count--)
			*bp = ctrl_inb(a++);
	} else {
		p = (volatile unsigned short *)port2adr(port);
		while (count--)
			*p = ctrl_inb(a++);
	}
}

void rts7751r2d_outsw(unsigned long port, const void *addr, unsigned long count)
{
	unsigned long a = (unsigned long)addr;
	volatile __u16 *p;

	if (CHECK_AX88796L_PORT(port))
		p = (volatile unsigned short *)port88796l(port, 1);
	else if (PXSEG(port))
		p = (volatile unsigned short *)port;
	else if (is_pci_ioaddr(port) || shifted_port(port))
		p = (volatile unsigned short *)pci_ioaddr(port);
	else
		p = (volatile unsigned short *)port2adr(port);

	while (count--) {
		ctrl_outw(*p, a);
		a += 2;
	}
}

void rts7751r2d_outsl(unsigned long port, const void *addr, unsigned long count)
{
	if (CHECK_AX88796L_PORT(port))
		maybebadio(port);
	else if (is_pci_ioaddr(port) || shifted_port(port)) {
		unsigned long a = (unsigned long)addr;

		while (count--) {
			ctrl_outl(ctrl_inl(a), pci_ioaddr(port));
			a += 4;
		}
	} else
		maybebadio(port);
}

unsigned long rts7751r2d_isa_port2addr(unsigned long offset)
{
	return port2adr(offset);
}
