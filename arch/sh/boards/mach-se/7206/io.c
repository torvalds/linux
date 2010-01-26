/* $Id: io.c,v 1.5 2004/02/22 23:08:43 kkojima Exp $
 *
 * linux/arch/sh/boards/se/7206/io.c
 *
 * Copyright (C) 2006 Yoshinori Sato
 *
 * I/O routine for Hitachi 7206 SolutionEngine.
 *
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <asm/io.h>
#include <mach-se/mach/se7206.h>


static inline void delay(void)
{
	__raw_readw(0x20000000);  /* P2 ROM Area */
}

/* MS7750 requires special versions of in*, out* routines, since
   PC-like io ports are located at upper half byte of 16-bit word which
   can be accessed only with 16-bit wide.  */

static inline volatile __u16 *
port2adr(unsigned int port)
{
	if (port >= 0x2000 && port < 0x2020)
		return (volatile __u16 *) (PA_MRSHPC + (port - 0x2000));
	else if (port >= 0x300 && port < 0x310)
		return (volatile __u16 *) (PA_SMSC + (port - 0x300));

	return (volatile __u16 *)port;
}

unsigned char se7206_inb(unsigned long port)
{
	return (*port2adr(port)) & 0xff;
}

unsigned char se7206_inb_p(unsigned long port)
{
	unsigned long v;

	v = (*port2adr(port)) & 0xff;
	delay();
	return v;
}

unsigned short se7206_inw(unsigned long port)
{
	return *port2adr(port);
}

void se7206_outb(unsigned char value, unsigned long port)
{
	*(port2adr(port)) = value;
}

void se7206_outb_p(unsigned char value, unsigned long port)
{
	*(port2adr(port)) = value;
	delay();
}

void se7206_outw(unsigned short value, unsigned long port)
{
	*port2adr(port) = value;
}

void se7206_insb(unsigned long port, void *addr, unsigned long count)
{
	volatile __u16 *p = port2adr(port);
	__u8 *ap = addr;

	while (count--)
		*ap++ = *p;
}

void se7206_insw(unsigned long port, void *addr, unsigned long count)
{
	volatile __u16 *p = port2adr(port);
	__u16 *ap = addr;
	while (count--)
		*ap++ = *p;
}

void se7206_outsb(unsigned long port, const void *addr, unsigned long count)
{
	volatile __u16 *p = port2adr(port);
	const __u8 *ap = addr;

	while (count--)
		*p = *ap++;
}

void se7206_outsw(unsigned long port, const void *addr, unsigned long count)
{
	volatile __u16 *p = port2adr(port);
	const __u16 *ap = addr;
	while (count--)
		*p = *ap++;
}
