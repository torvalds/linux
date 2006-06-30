/*
 *  linux/arch/m32r/kernel/io_mappi3.c
 *
 *  Typical I/O routines for Mappi3 board.
 *
 *  Copyright (c) 2001-2005  Hiroyuki Kondo, Hirokazu Takata,
 *                           Hitoshi Yamamoto, Mamoru Sakugawa
 */

#include <asm/m32r.h>
#include <asm/page.h>
#include <asm/io.h>
#include <asm/byteorder.h>

#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32R_CFC)
#include <linux/types.h>

#define M32R_PCC_IOMAP_SIZE 0x1000

#define M32R_PCC_IOSTART0 0x1000
#define M32R_PCC_IOEND0   (M32R_PCC_IOSTART0 + M32R_PCC_IOMAP_SIZE - 1)

extern void pcc_ioread_byte(int, unsigned long, void *, size_t, size_t, int);
extern void pcc_ioread_word(int, unsigned long, void *, size_t, size_t, int);
extern void pcc_iowrite_byte(int, unsigned long, void *, size_t, size_t, int);
extern void pcc_iowrite_word(int, unsigned long, void *, size_t, size_t, int);
#endif /* CONFIG_PCMCIA && CONFIG_M32R_CFC */

#define PORT2ADDR(port)      _port2addr(port)
#define PORT2ADDR_NE(port)   _port2addr_ne(port)
#define PORT2ADDR_USB(port)  _port2addr_usb(port)

static inline void *_port2addr(unsigned long port)
{
	return (void *)(port | NONCACHE_OFFSET);
}

#if defined(CONFIG_IDE)
static inline void *__port2addr_ata(unsigned long port)
{
	static int	dummy_reg;

	switch (port) {
	  /* IDE0 CF */
	case 0x1f0:	return (void *)(0x14002000 | NONCACHE_OFFSET);
	case 0x1f1:	return (void *)(0x14012800 | NONCACHE_OFFSET);
	case 0x1f2:	return (void *)(0x14012002 | NONCACHE_OFFSET);
	case 0x1f3:	return (void *)(0x14012802 | NONCACHE_OFFSET);
	case 0x1f4:	return (void *)(0x14012004 | NONCACHE_OFFSET);
	case 0x1f5:	return (void *)(0x14012804 | NONCACHE_OFFSET);
	case 0x1f6:	return (void *)(0x14012006 | NONCACHE_OFFSET);
	case 0x1f7:	return (void *)(0x14012806 | NONCACHE_OFFSET);
	case 0x3f6:	return (void *)(0x1401200e | NONCACHE_OFFSET);
	  /* IDE1 IDE */
	case 0x170:	/* Data 16bit */
			return (void *)(0x14810000 | NONCACHE_OFFSET);
	case 0x171:	/* Features / Error */
			return (void *)(0x14810002 | NONCACHE_OFFSET);
	case 0x172:	/* Sector count */
			return (void *)(0x14810004 | NONCACHE_OFFSET);
	case 0x173:	/* Sector number */
			return (void *)(0x14810006 | NONCACHE_OFFSET);
	case 0x174:	/* Cylinder low */
			return (void *)(0x14810008 | NONCACHE_OFFSET);
	case 0x175:	/* Cylinder high */
			return (void *)(0x1481000a | NONCACHE_OFFSET);
	case 0x176:	/* Device head */
			return (void *)(0x1481000c | NONCACHE_OFFSET);
	case 0x177:	/* Command     */
			return (void *)(0x1481000e | NONCACHE_OFFSET);
	case 0x376:	/* Device control / Alt status */
			return (void *)(0x1480800c | NONCACHE_OFFSET);

	default: 	return (void *)&dummy_reg;
	}
}
#endif

#define LAN_IOSTART	(0x300 | NONCACHE_OFFSET)
#define LAN_IOEND	(0x320 | NONCACHE_OFFSET)
static inline void *_port2addr_ne(unsigned long port)
{
	return (void *)(port + 0x10000000);
}

static inline void *_port2addr_usb(unsigned long port)
{
	return (void *)(port + NONCACHE_OFFSET + 0x12000000);
}
static inline void delay(void)
{
	__asm__ __volatile__ ("push r0; \n\t pop r0;" : : :"memory");
}

/*
 * NIC I/O function
 */

static inline unsigned char _ne_inb(void *portp)
{
	return (unsigned char) *(volatile unsigned char *)portp;
}

static inline unsigned short _ne_inw(void *portp)
{
	return (unsigned short)le16_to_cpu(*(volatile unsigned short *)portp);
}

static inline void _ne_insb(void *portp, void * addr, unsigned long count)
{
	unsigned char *buf = addr;

	while (count--)
		*buf++ = *(volatile unsigned char *)portp;
}

static inline void _ne_outb(unsigned char b, void *portp)
{
	*(volatile unsigned char *)portp = (unsigned char)b;
}

static inline void _ne_outw(unsigned short w, void *portp)
{
	*(volatile unsigned short *)portp = cpu_to_le16(w);
}

unsigned char _inb(unsigned long port)
{
	if (port >= LAN_IOSTART && port < LAN_IOEND)
		return _ne_inb(PORT2ADDR_NE(port));
#if defined(CONFIG_IDE)
	else if ( ((port >= 0x170 && port <=0x177) || port == 0x376) ||
		  ((port >= 0x1f0 && port <=0x1f7) || port == 0x3f6) ){
		return *(volatile unsigned char *)__port2addr_ata(port);
	}
#endif
#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32R_CFC)
	else if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
		unsigned char b;
		pcc_ioread_byte(0, port, &b, sizeof(b), 1, 0);
		return b;
	} else
#endif
	return *(volatile unsigned char *)PORT2ADDR(port);
}

unsigned short _inw(unsigned long port)
{
	if (port >= LAN_IOSTART && port < LAN_IOEND)
		return _ne_inw(PORT2ADDR_NE(port));
#if defined(CONFIG_IDE)
	else if ( ((port >= 0x170 && port <=0x177) || port == 0x376) ||
		  ((port >= 0x1f0 && port <=0x1f7) || port == 0x3f6) ){
		return *(volatile unsigned short *)__port2addr_ata(port);
	}
#endif
#if defined(CONFIG_USB)
	else if (port >= 0x340 && port < 0x3a0)
		return *(volatile unsigned short *)PORT2ADDR_USB(port);
#endif

#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32R_CFC)
	else if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
		unsigned short w;
		pcc_ioread_word(0, port, &w, sizeof(w), 1, 0);
		return w;
	} else
#endif
	return *(volatile unsigned short *)PORT2ADDR(port);
}

unsigned long _inl(unsigned long port)
{
#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32R_CFC)
	if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
		unsigned long l;
		pcc_ioread_word(0, port, &l, sizeof(l), 1, 0);
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
	if (port >= LAN_IOSTART && port < LAN_IOEND)
		_ne_outb(b, PORT2ADDR_NE(port));
	else
#if defined(CONFIG_IDE)
	if ( ((port >= 0x170 && port <=0x177) || port == 0x376) ||
		  ((port >= 0x1f0 && port <=0x1f7) || port == 0x3f6) ){
		*(volatile unsigned char *)__port2addr_ata(port) = b;
	} else
#endif
#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32R_CFC)
	if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
		pcc_iowrite_byte(0, port, &b, sizeof(b), 1, 0);
	} else
#endif
		*(volatile unsigned char *)PORT2ADDR(port) = b;
}

void _outw(unsigned short w, unsigned long port)
{
	if (port >= LAN_IOSTART && port < LAN_IOEND)
		_ne_outw(w, PORT2ADDR_NE(port));
	else
#if defined(CONFIG_IDE)
	if ( ((port >= 0x170 && port <=0x177) || port == 0x376) ||
		  ((port >= 0x1f0 && port <=0x1f7) || port == 0x3f6) ){
		*(volatile unsigned short *)__port2addr_ata(port) = w;
	} else
#endif
#if defined(CONFIG_USB)
	if (port >= 0x340 && port < 0x3a0)
		*(volatile unsigned short *)PORT2ADDR_USB(port) = w;
	else
#endif
#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32R_CFC)
	if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
		pcc_iowrite_word(0, port, &w, sizeof(w), 1, 0);
	} else
#endif
		*(volatile unsigned short *)PORT2ADDR(port) = w;
}

void _outl(unsigned long l, unsigned long port)
{
#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32R_CFC)
	if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
		pcc_iowrite_word(0, port, &l, sizeof(l), 1, 0);
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

void _insb(unsigned int port, void * addr, unsigned long count)
{
	if (port >= LAN_IOSTART && port < LAN_IOEND)
		_ne_insb(PORT2ADDR_NE(port), addr, count);
#if defined(CONFIG_IDE)
	else if ( ((port >= 0x170 && port <=0x177) || port == 0x376) ||
		  ((port >= 0x1f0 && port <=0x1f7) || port == 0x3f6) ){
		unsigned char *buf = addr;
		unsigned char *portp = __port2addr_ata(port);
		while (count--)
			*buf++ = *(volatile unsigned char *)portp;
	}
#endif
#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32R_CFC)
	else if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
		pcc_ioread_byte(0, port, (void *)addr, sizeof(unsigned char),
				count, 1);
	}
#endif
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

	if (port >= LAN_IOSTART && port < LAN_IOEND) {
		portp = PORT2ADDR_NE(port);
		while (count--)
			*buf++ = *(volatile unsigned short *)portp;
#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32R_CFC)
	} else if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
		pcc_ioread_word(9, port, (void *)addr, sizeof(unsigned short),
				count, 1);
#endif
#if defined(CONFIG_IDE)
	} else if ( ((port >= 0x170 && port <=0x177) || port == 0x376) ||
		  ((port >= 0x1f0 && port <=0x1f7) || port == 0x3f6) ){
		portp = __port2addr_ata(port);
		while (count--)
			*buf++ = *(volatile unsigned short *)portp;
#endif
	} else {
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

	if (port >= LAN_IOSTART && port < LAN_IOEND) {
		portp = PORT2ADDR_NE(port);
		while (count--)
			_ne_outb(*buf++, portp);
#if defined(CONFIG_IDE)
	} else if ( ((port >= 0x170 && port <=0x177) || port == 0x376) ||
		  ((port >= 0x1f0 && port <=0x1f7) || port == 0x3f6) ){
		portp = __port2addr_ata(port);
		while (count--)
			*(volatile unsigned char *)portp = *buf++;
#endif
#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32R_CFC)
	} else if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
		pcc_iowrite_byte(0, port, (void *)addr, sizeof(unsigned char),
				 count, 1);
#endif
	} else {
		portp = PORT2ADDR(port);
		while (count--)
			*(volatile unsigned char *)portp = *buf++;
	}
}

void _outsw(unsigned int port, const void * addr, unsigned long count)
{
	const unsigned short *buf = addr;
	unsigned short *portp;

	if (port >= LAN_IOSTART && port < LAN_IOEND) {
		portp = PORT2ADDR_NE(port);
		while (count--)
			*(volatile unsigned short *)portp = *buf++;
#if defined(CONFIG_IDE)
	} else if ( ((port >= 0x170 && port <=0x177) || port == 0x376) ||
		  ((port >= 0x1f0 && port <=0x1f7) || port == 0x3f6) ){
		portp = __port2addr_ata(port);
		while (count--)
			*(volatile unsigned short *)portp = *buf++;
#endif
#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32R_CFC)
	} else if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
		pcc_iowrite_word(9, port, (void *)addr, sizeof(unsigned short),
				 count, 1);
#endif
	} else {
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
