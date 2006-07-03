/*
 *  linux/arch/m32r/kernel/io_mappi.c
 *
 *  Typical I/O routines for Mappi board.
 *
 *  Copyright (c) 2001-2005  Hiroyuki Kondo, Hirokazu Takata,
 *                           Hitoshi Yamamoto
 */

#include <asm/m32r.h>
#include <asm/page.h>
#include <asm/io.h>
#include <asm/byteorder.h>

#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32R_PCC)
#include <linux/types.h>

#define M32R_PCC_IOMAP_SIZE 0x1000

#define M32R_PCC_IOSTART0 0x1000
#define M32R_PCC_IOEND0   (M32R_PCC_IOSTART0 + M32R_PCC_IOMAP_SIZE - 1)
#define M32R_PCC_IOSTART1 0x2000
#define M32R_PCC_IOEND1   (M32R_PCC_IOSTART1 + M32R_PCC_IOMAP_SIZE - 1)

extern void pcc_ioread(int, unsigned long, void *, size_t, size_t, int);
extern void pcc_iowrite(int, unsigned long, void *, size_t, size_t, int);
#endif /* CONFIG_PCMCIA && CONFIG_M32R_PCC */

#define PORT2ADDR(port)  _port2addr(port)

static inline void *_port2addr(unsigned long port)
{
	return (void *)(port | NONCACHE_OFFSET);
}

static inline void *_port2addr_ne(unsigned long port)
{
	return (void *)((port<<1) + NONCACHE_OFFSET + 0x0C000000);
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
	return (unsigned char) *(volatile unsigned short *)portp;
}

static inline unsigned short _ne_inw(void *portp)
{
	unsigned short tmp;

	tmp = *(volatile unsigned short *)portp;
	return le16_to_cpu(tmp);
}

static inline void _ne_outb(unsigned char b, void *portp)
{
	*(volatile unsigned short *)portp = (unsigned short)b;
}

static inline void _ne_outw(unsigned short w, void *portp)
{
	*(volatile unsigned short *)portp = cpu_to_le16(w);
}

unsigned char _inb(unsigned long port)
{
	if (port >= 0x300 && port < 0x320)
		return _ne_inb(PORT2ADDR_NE(port));
	else
#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32R_PCC)
        if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
		unsigned char b;
		pcc_ioread(0, port, &b, sizeof(b), 1, 0);
		return b;
	} else 	if (port >= M32R_PCC_IOSTART1 && port <= M32R_PCC_IOEND1) {
		unsigned char b;
		pcc_ioread(1, port, &b, sizeof(b), 1, 0);
		return b;
	} else
#endif

	return *(volatile unsigned char *)PORT2ADDR(port);
}

unsigned short _inw(unsigned long port)
{
	if (port >= 0x300 && port < 0x320)
		return _ne_inw(PORT2ADDR_NE(port));
	else
#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32R_PCC)
	if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
		unsigned short w;
		pcc_ioread(0, port, &w, sizeof(w), 1, 0);
		return w;
	} else 	if (port >= M32R_PCC_IOSTART1 && port <= M32R_PCC_IOEND1) {
		unsigned short w;
		pcc_ioread(1, port, &w, sizeof(w), 1, 0);
		return w;
	} else
#endif
	return *(volatile unsigned short *)PORT2ADDR(port);
}

unsigned long _inl(unsigned long port)
{
#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32R_PCC)
	if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
		unsigned long l;
		pcc_ioread(0, port, &l, sizeof(l), 1, 0);
		return l;
	} else 	if (port >= M32R_PCC_IOSTART1 && port <= M32R_PCC_IOEND1) {
		unsigned short l;
		pcc_ioread(1, port, &l, sizeof(l), 1, 0);
		return l;
	} else
#endif
	return *(volatile unsigned long *)PORT2ADDR(port);
}

unsigned char _inb_p(unsigned long port)
{
	unsigned char v = _inb(port);
	delay();
	return (v);
}

unsigned short _inw_p(unsigned long port)
{
	unsigned short v = _inw(port);
	delay();
	return (v);
}

unsigned long _inl_p(unsigned long port)
{
	unsigned long v = _inl(port);
	delay();
	return (v);
}

void _outb(unsigned char b, unsigned long port)
{
	if (port >= 0x300 && port < 0x320)
		_ne_outb(b, PORT2ADDR_NE(port));
	else
#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32R_PCC)
	if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
		pcc_iowrite(0, port, &b, sizeof(b), 1, 0);
	} else 	if (port >= M32R_PCC_IOSTART1 && port <= M32R_PCC_IOEND1) {
		pcc_iowrite(1, port, &b, sizeof(b), 1, 0);
	} else
#endif
		*(volatile unsigned char *)PORT2ADDR(port) = b;
}

void _outw(unsigned short w, unsigned long port)
{
	if (port >= 0x300 && port < 0x320)
		_ne_outw(w, PORT2ADDR_NE(port));
	else
#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32R_PCC)
	if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
		pcc_iowrite(0, port, &w, sizeof(w), 1, 0);
	} else 	if (port >= M32R_PCC_IOSTART1 && port <= M32R_PCC_IOEND1) {
		pcc_iowrite(1, port, &w, sizeof(w), 1, 0);
	} else
#endif
		*(volatile unsigned short *)PORT2ADDR(port) = w;
}

void _outl(unsigned long l, unsigned long port)
{
#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32R_PCC)
	if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
		pcc_iowrite(0, port, &l, sizeof(l), 1, 0);
	} else 	if (port >= M32R_PCC_IOSTART1 && port <= M32R_PCC_IOEND1) {
		pcc_iowrite(1, port, &l, sizeof(l), 1, 0);
	} else
#endif
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

void _insb(unsigned int port, void *addr, unsigned long count)
{
	unsigned short *buf = addr;
	unsigned short *portp;

	if (port >= 0x300 && port < 0x320){
		portp = PORT2ADDR_NE(port);
		while (count--)
			*buf++ = *(volatile unsigned char *)portp;
#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32R_PCC)
	} else if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
		pcc_ioread(0, port, (void *)addr, sizeof(unsigned char),
			   count, 1);
	} else 	if (port >= M32R_PCC_IOSTART1 && port <= M32R_PCC_IOEND1) {
		pcc_ioread(1, port, (void *)addr, sizeof(unsigned char),
			   count, 1);
#endif
	} else {
		portp = PORT2ADDR(port);
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
#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32R_PCC)
	} else if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
		pcc_ioread(0, port, (void *)addr, sizeof(unsigned short),
			   count, 1);
	} else 	if (port >= M32R_PCC_IOSTART1 && port <= M32R_PCC_IOEND1) {
		pcc_ioread(1, port, (void *)addr, sizeof(unsigned short),
			   count, 1);
#endif
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
#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32R_PCC)
	} else if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
		pcc_iowrite(0, port, (void *)addr, sizeof(unsigned char),
			    count, 1);
	} else if (port >= M32R_PCC_IOSTART1 && port <= M32R_PCC_IOEND1) {
		pcc_iowrite(1, port, (void *)addr, sizeof(unsigned char),
			    count, 1);
#endif
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
#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32R_PCC)
	} else if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
		pcc_iowrite(0, port, (void *)addr, sizeof(unsigned short),
			    count, 1);
	} else 	if (port >= M32R_PCC_IOSTART1 && port <= M32R_PCC_IOEND1) {
		pcc_iowrite(1, port, (void *)addr, sizeof(unsigned short),
			    count, 1);
#endif
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
