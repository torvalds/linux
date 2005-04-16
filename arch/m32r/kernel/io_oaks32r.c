/*
 *  linux/arch/m32r/kernel/io_oaks32r.c
 *
 *  Typical I/O routines for OAKS32R board.
 *
 *  Copyright (c) 2001-2004  Hiroyuki Kondo, Hirokazu Takata,
 *                           Hitoshi Yamamoto, Mamoru Sakugawa
 */

#include <linux/config.h>
#include <asm/m32r.h>
#include <asm/page.h>
#include <asm/io.h>

#define PORT2ADDR(port)  _port2addr(port)

static inline void *_port2addr(unsigned long port)
{
	return (void *)(port + NONCACHE_OFFSET);
}

static inline  void *_port2addr_ne(unsigned long port)
{
	return (void *)((port<<1) + NONCACHE_OFFSET + 0x02000000);
}

static inline void delay(void)
{
	__asm__ __volatile__ ("push r0; \n\t pop r0;" : : :"memory");
}

/*
 * NIC I/O function
 */

#define PORT2ADDR_NE(port)  _port2addr_ne(port)

static inline unsigned char _ne_inb(void *portp)
{
	return *(volatile unsigned char *)(portp+1);
}

static inline unsigned short _ne_inw(void *portp)
{
	unsigned short tmp;

	tmp = *(unsigned short *)(portp) & 0xff;
	tmp |= *(unsigned short *)(portp+2) << 8;
	return tmp;
}

static inline  void _ne_insb(void *portp, void *addr, unsigned long count)
{
	unsigned char *buf = addr;
	while (count--)
		*buf++ = *(volatile unsigned char *)(portp+1);
}

static inline void _ne_outb(unsigned char b, void *portp)
{
	*(volatile unsigned char *)(portp+1) = b;
}

static inline void _ne_outw(unsigned short w, void *portp)
{
	*(volatile unsigned short *)portp =  (w >> 8);
	*(volatile unsigned short *)(portp+2) =  (w & 0xff);
}

unsigned char _inb(unsigned long port)
{
	if (port >= 0x300 && port < 0x320)
		return _ne_inb(PORT2ADDR_NE(port));

	return *(volatile unsigned char *)PORT2ADDR(port);
}

unsigned short _inw(unsigned long port)
{
	if (port >= 0x300 && port < 0x320)
		return _ne_inw(PORT2ADDR_NE(port));

	return *(volatile unsigned short *)PORT2ADDR(port);
}

unsigned long _inl(unsigned long port)
{
	return *(volatile unsigned long *)PORT2ADDR(port);
}

unsigned char _inb_p(unsigned long port)
{
	unsigned char  v;

	if (port >= 0x300 && port < 0x320)
		v = _ne_inb(PORT2ADDR_NE(port));
	else
		v = *(volatile unsigned char *)PORT2ADDR(port);

	delay();
	return (v);
}

unsigned short _inw_p(unsigned long port)
{
	unsigned short  v;

	if (port >= 0x300 && port < 0x320)
		v = _ne_inw(PORT2ADDR_NE(port));
	else
		v = *(volatile unsigned short *)PORT2ADDR(port);

	delay();
	return (v);
}

unsigned long _inl_p(unsigned long port)
{
	unsigned long  v;

	v = *(volatile unsigned long *)PORT2ADDR(port);
	delay();
	return (v);
}

void _outb(unsigned char b, unsigned long port)
{
	if (port >= 0x300 && port < 0x320)
		_ne_outb(b, PORT2ADDR_NE(port));
	else
		*(volatile unsigned char *)PORT2ADDR(port) = b;
}

void _outw(unsigned short w, unsigned long port)
{
	if (port >= 0x300 && port < 0x320)
		_ne_outw(w, PORT2ADDR_NE(port));
	else
		*(volatile unsigned short *)PORT2ADDR(port) = w;
}

void _outl(unsigned long l, unsigned long port)
{
	*(volatile unsigned long *)PORT2ADDR(port) = l;
}

void _outb_p(unsigned char b, unsigned long port)
{
	if (port >= 0x300 && port < 0x320)
		_ne_outb(b, PORT2ADDR_NE(port));
	else
		*(volatile unsigned char *)PORT2ADDR(port) = b;

	delay();
}

void _outw_p(unsigned short w, unsigned long port)
{
	if (port >= 0x300 && port < 0x320)
		_ne_outw(w, PORT2ADDR_NE(port));
	else
		*(volatile unsigned short *)PORT2ADDR(port) = w;

	delay();
}

void _outl_p(unsigned long l, unsigned long port)
{
	*(volatile unsigned long *)PORT2ADDR(port) = l;
	delay();
}

void _insb(unsigned int port, void *addr, unsigned long count)
{
	if (port >= 0x300 && port < 0x320)
		_ne_insb(PORT2ADDR_NE(port), addr, count);
	else {
		unsigned char *buf = addr;
		unsigned char *portp = PORT2ADDR(port);
		while (count--)
			*buf++ = *(volatile unsigned char *)portp;
	}
}

void _insw(unsigned int port, void *addr, unsigned long count)
{
	unsigned short *buf = addr;
	unsigned short *portp;

	if (port >= 0x300 && port < 0x320) {
		portp = PORT2ADDR_NE(port);
		while (count--)
			*buf++ = _ne_inw(portp);
	} else {
		portp = PORT2ADDR(port);
		while (count--)
			*buf++ = *(volatile unsigned short *)portp;
	}
}

void _insl(unsigned int port, void *addr, unsigned long count)
{
	unsigned long *buf = addr;
	unsigned long *portp;

	portp = PORT2ADDR(port);
	while (count--)
		*buf++ = *(volatile unsigned long *)portp;
}

void _outsb(unsigned int port, const void *addr, unsigned long count)
{
	const unsigned char *buf = addr;
	unsigned char *portp;

	if (port >= 0x300 && port < 0x320) {
		portp = PORT2ADDR_NE(port);
		while (count--)
			_ne_outb(*buf++, portp);
	} else {
		portp = PORT2ADDR(port);
		while (count--)
			*(volatile unsigned char *)portp = *buf++;
	}
}

void _outsw(unsigned int port, const void *addr, unsigned long count)
{
	const unsigned short *buf = addr;
	unsigned short *portp;

	if (port >= 0x300 && port < 0x320) {
		portp = PORT2ADDR_NE(port);
		while (count--)
			_ne_outw(*buf++, portp);
	} else {
		portp = PORT2ADDR(port);
		while (count--)
			*(volatile unsigned short *)portp = *buf++;
	}
}

void _outsl(unsigned int port, const void *addr, unsigned long count)
{
	const unsigned long *buf = addr;
	unsigned char *portp;

	portp = PORT2ADDR(port);
	while (count--)
		*(volatile unsigned long *)portp = *buf++;
}
