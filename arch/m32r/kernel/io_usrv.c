/*
 *  linux/arch/m32r/kernel/io_usrv.c
 *
 *  Typical I/O routines for uServer board.
 *
 *  Copyright (c) 2001-2005  Hiroyuki Kondo, Hirokazu Takata,
 *                           Hitoshi Yamamoto, Takeo Takahashi
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file "COPYING" in the main directory of this
 *  archive for more details.
 *
 */

#include <asm/m32r.h>
#include <asm/page.h>
#include <asm/io.h>

#include <linux/types.h>
#include "../drivers/m32r_cfc.h"

extern void pcc_ioread_byte(int, unsigned long, void *, size_t, size_t, int);
extern void pcc_ioread_word(int, unsigned long, void *, size_t, size_t, int);
extern void pcc_iowrite_byte(int, unsigned long, void *, size_t, size_t, int);
extern void pcc_iowrite_word(int, unsigned long, void *, size_t, size_t, int);
#define CFC_IOSTART	CFC_IOPORT_BASE
#define CFC_IOEND	(CFC_IOSTART + (M32R_PCC_MAPSIZE * M32R_MAX_PCC) - 1)

#if defined(CONFIG_SERIAL_8250) || defined(CONFIG_SERIAL_8250_MODULE)
#define UART0_REGSTART		0x04c20000
#define UART1_REGSTART		0x04c20100
#define UART_IOMAP_SIZE		8
#define UART0_IOSTART		0x3f8
#define UART0_IOEND		(UART0_IOSTART + UART_IOMAP_SIZE - 1)
#define UART1_IOSTART		0x2f8
#define UART1_IOEND		(UART1_IOSTART + UART_IOMAP_SIZE - 1)
#endif	/* CONFIG_SERIAL_8250 || CONFIG_SERIAL_8250_MODULE */

#define PORT2ADDR(port)	_port2addr(port)

static inline void *_port2addr(unsigned long port)
{
#if defined(CONFIG_SERIAL_8250) || defined(CONFIG_SERIAL_8250_MODULE)
	if (port >= UART0_IOSTART && port <= UART0_IOEND)
		port = ((port - UART0_IOSTART) << 1) + UART0_REGSTART;
	else if (port >= UART1_IOSTART && port <= UART1_IOEND)
		port = ((port - UART1_IOSTART) << 1) + UART1_REGSTART;
#endif	/* CONFIG_SERIAL_8250 || CONFIG_SERIAL_8250_MODULE */
	return (void *)(port | (NONCACHE_OFFSET));
}

static inline void delay(void)
{
	__asm__ __volatile__ ("push r0; \n\t pop r0;" : : :"memory");
}

unsigned char _inb(unsigned long port)
{
	if (port >= CFC_IOSTART && port <= CFC_IOEND) {
		unsigned char b;
		pcc_ioread_byte(0, port, &b, sizeof(b), 1, 0);
		return b;
	} else
		return *(volatile unsigned char *)PORT2ADDR(port);
}

unsigned short _inw(unsigned long port)
{
	if (port >= CFC_IOSTART && port <= CFC_IOEND) {
		unsigned short w;
		pcc_ioread_word(0, port, &w, sizeof(w), 1, 0);
		return w;
	} else
		return *(volatile unsigned short *)PORT2ADDR(port);
}

unsigned long _inl(unsigned long port)
{
	if (port >= CFC_IOSTART && port <= CFC_IOEND) {
		unsigned long l;
		pcc_ioread_word(0, port, &l, sizeof(l), 1, 0);
		return l;
	} else
		return *(volatile unsigned long *)PORT2ADDR(port);
}

unsigned char _inb_p(unsigned long port)
{
	unsigned char v = _inb(port);
	delay();
	return v;
}

unsigned short _inw_p(unsigned long port)
{
	unsigned short v = _inw(port);
	delay();
	return v;
}

unsigned long _inl_p(unsigned long port)
{
	unsigned long v = _inl(port);
	delay();
	return v;
}

void _outb(unsigned char b, unsigned long port)
{
	if (port >= CFC_IOSTART && port <= CFC_IOEND)
		pcc_iowrite_byte(0, port, &b, sizeof(b), 1, 0);
	else
		*(volatile unsigned char *)PORT2ADDR(port) = b;
}

void _outw(unsigned short w, unsigned long port)
{
	if (port >= CFC_IOSTART && port <= CFC_IOEND)
		pcc_iowrite_word(0, port, &w, sizeof(w), 1, 0);
	else
		*(volatile unsigned short *)PORT2ADDR(port) = w;
}

void _outl(unsigned long l, unsigned long port)
{
	if (port >= CFC_IOSTART && port <= CFC_IOEND)
		pcc_iowrite_word(0, port, &l, sizeof(l), 1, 0);
	else
		*(volatile unsigned long *)PORT2ADDR(port) = l;
}

void _outb_p(unsigned char b, unsigned long port)
{
	_outb(b, port);
	delay();
}

void _outw_p(unsigned short w, unsigned long port)
{
	_outw(w, port);
	delay();
}

void _outl_p(unsigned long l, unsigned long port)
{
	_outl(l, port);
	delay();
}

void _insb(unsigned int port, void * addr, unsigned long count)
{
	if (port >= CFC_IOSTART && port <= CFC_IOEND)
		pcc_ioread_byte(0, port, addr, sizeof(unsigned char), count, 1);
	else {
		unsigned char *buf = addr;
		unsigned char *portp = PORT2ADDR(port);
		while (count--)
			*buf++ = *(volatile unsigned char *)portp;
	}
}

void _insw(unsigned int port, void * addr, unsigned long count)
{
	unsigned short *buf = addr;
	unsigned short *portp;

	if (port >= CFC_IOSTART && port <= CFC_IOEND)
		pcc_ioread_word(0, port, addr, sizeof(unsigned short), count,
			1);
	else {
		portp = PORT2ADDR(port);
		while (count--)
			*buf++ = *(volatile unsigned short *)portp;
	}
}

void _insl(unsigned int port, void * addr, unsigned long count)
{
	unsigned long *buf = addr;
	unsigned long *portp;

	portp = PORT2ADDR(port);
	while (count--)
		*buf++ = *(volatile unsigned long *)portp;
}

void _outsb(unsigned int port, const void * addr, unsigned long count)
{
	const unsigned char *buf = addr;
	unsigned char *portp;

	if (port >= CFC_IOSTART && port <= CFC_IOEND)
		pcc_iowrite_byte(0, port, (void *)addr, sizeof(unsigned char),
			count, 1);
	else {
		portp = PORT2ADDR(port);
		while (count--)
			*(volatile unsigned char *)portp = *buf++;
	}
}

void _outsw(unsigned int port, const void * addr, unsigned long count)
{
	const unsigned short *buf = addr;
	unsigned short *portp;

	if (port >= CFC_IOSTART && port <= CFC_IOEND)
		pcc_iowrite_word(0, port, (void *)addr, sizeof(unsigned short),
			count, 1);
	else {
		portp = PORT2ADDR(port);
		while (count--)
			*(volatile unsigned short *)portp = *buf++;
	}
}

void _outsl(unsigned int port, const void * addr, unsigned long count)
{
	const unsigned long *buf = addr;
	unsigned char *portp;

	portp = PORT2ADDR(port);
	while (count--)
		*(volatile unsigned long *)portp = *buf++;
}
