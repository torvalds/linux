/* $Id: io.c,v 1.7 2006/02/05 21:55:29 lethal Exp $
 *
 * linux/arch/sh/kernel/io_se.c
 *
 * Copyright (C) 2000  Kazumoto Kojima
 *
 * I/O routine for Hitachi SolutionEngine.
 *
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <asm/io.h>
#include <asm/se.h>

/* SH pcmcia io window base, start and end.  */
int sh_pcic_io_wbase = 0xb8400000;
int sh_pcic_io_start;
int sh_pcic_io_stop;
int sh_pcic_io_type;
int sh_pcic_io_dummy;

/* MS7750 requires special versions of in*, out* routines, since
   PC-like io ports are located at upper half byte of 16-bit word which
   can be accessed only with 16-bit wide.  */

static inline volatile __u16 *
port2adr(unsigned int port)
{
	if (port & 0xff000000)
		return ( volatile __u16 *) port;
	if (port >= 0x2000)
		return (volatile __u16 *) (PA_MRSHPC + (port - 0x2000));
	else if (port >= 0x1000)
		return (volatile __u16 *) (PA_83902 + (port << 1));
	else if (sh_pcic_io_start <= port && port <= sh_pcic_io_stop)
		return (volatile __u16 *) (sh_pcic_io_wbase + (port &~ 1));
	else
		return (volatile __u16 *) (PA_SUPERIO + (port << 1));
}

static inline int
shifted_port(unsigned long port)
{
	/* For IDE registers, value is not shifted */
	if ((0x1f0 <= port && port < 0x1f8) || port == 0x3f6)
		return 0;
	else
		return 1;
}

unsigned char se_inb(unsigned long port)
{
	if (sh_pcic_io_start <= port && port <= sh_pcic_io_stop)
		return *(__u8 *) (sh_pcic_io_wbase + 0x40000 + port); 
	else if (shifted_port(port))
		return (*port2adr(port) >> 8); 
	else
		return (*port2adr(port))&0xff; 
}

unsigned char se_inb_p(unsigned long port)
{
	unsigned long v;

	if (sh_pcic_io_start <= port && port <= sh_pcic_io_stop)
		v = *(__u8 *) (sh_pcic_io_wbase + 0x40000 + port); 
	else if (shifted_port(port))
		v = (*port2adr(port) >> 8); 
	else
		v = (*port2adr(port))&0xff; 
	ctrl_delay();
	return v;
}

unsigned short se_inw(unsigned long port)
{
	if (port >= 0x2000 ||
	    (sh_pcic_io_start <= port && port <= sh_pcic_io_stop))
		return *port2adr(port);
	else
		maybebadio(port);
	return 0;
}

unsigned int se_inl(unsigned long port)
{
	maybebadio(port);
	return 0;
}

void se_outb(unsigned char value, unsigned long port)
{
	if (sh_pcic_io_start <= port && port <= sh_pcic_io_stop)
		*(__u8 *)(sh_pcic_io_wbase + port) = value; 
	else if (shifted_port(port))
		*(port2adr(port)) = value << 8;
	else
		*(port2adr(port)) = value;
}

void se_outb_p(unsigned char value, unsigned long port)
{
	if (sh_pcic_io_start <= port && port <= sh_pcic_io_stop)
		*(__u8 *)(sh_pcic_io_wbase + port) = value; 
	else if (shifted_port(port))
		*(port2adr(port)) = value << 8;
	else
		*(port2adr(port)) = value;
	ctrl_delay();
}

void se_outw(unsigned short value, unsigned long port)
{
	if (port >= 0x2000 ||
	    (sh_pcic_io_start <= port && port <= sh_pcic_io_stop))
		*port2adr(port) = value;
	else
		maybebadio(port);
}

void se_outl(unsigned int value, unsigned long port)
{
	maybebadio(port);
}

void se_insb(unsigned long port, void *addr, unsigned long count)
{
	volatile __u16 *p = port2adr(port);
	__u8 *ap = addr;

	if (sh_pcic_io_start <= port && port <= sh_pcic_io_stop) {
		volatile __u8 *bp = (__u8 *) (sh_pcic_io_wbase + 0x40000 + port); 
		while (count--)
			*ap++ = *bp;
	} else if (shifted_port(port)) {
		while (count--)
			*ap++ = *p >> 8;
	} else {
		while (count--)
			*ap++ = *p;
	}
}

void se_insw(unsigned long port, void *addr, unsigned long count)
{
	volatile __u16 *p = port2adr(port);
	__u16 *ap = addr;
	while (count--)
		*ap++ = *p;
}

void se_insl(unsigned long port, void *addr, unsigned long count)
{
	maybebadio(port);
}

void se_outsb(unsigned long port, const void *addr, unsigned long count)
{
	volatile __u16 *p = port2adr(port);
	const __u8 *ap = addr;

	if (sh_pcic_io_start <= port && port <= sh_pcic_io_stop) {
		volatile __u8 *bp = (__u8 *) (sh_pcic_io_wbase + port); 
		while (count--)
			*bp = *ap++;
	} else if (shifted_port(port)) {
		while (count--)
			*p = *ap++ << 8;
	} else {
		while (count--)
			*p = *ap++;
	}
}

void se_outsw(unsigned long port, const void *addr, unsigned long count)
{
	volatile __u16 *p = port2adr(port);
	const __u16 *ap = addr;
	while (count--)
		*p = *ap++;
}

void se_outsl(unsigned long port, const void *addr, unsigned long count)
{
	maybebadio(port);
}
