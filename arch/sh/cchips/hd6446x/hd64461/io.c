/*
 *	Copyright (C) 2000 YAEGASHI Takeshi
 *	Typical I/O routines for HD64461 system.
 */

#include <asm/io.h>
#include <asm/hd64461.h>

#define MEM_BASE (CONFIG_HD64461_IOBASE - HD64461_STBCR)

static __inline__ unsigned long PORT2ADDR(unsigned long port)
{
	/* 16550A: HD64461 internal */
	if (0x3f8<=port && port<=0x3ff)
		return CONFIG_HD64461_IOBASE + 0x8000 + ((port-0x3f8)<<1);
	if (0x2f8<=port && port<=0x2ff)
		return CONFIG_HD64461_IOBASE + 0x7000 + ((port-0x2f8)<<1);

#ifdef CONFIG_HD64461_ENABLER
	/* NE2000: HD64461 PCMCIA channel 0 (I/O) */
	if (0x300<=port && port<=0x31f)
		return 0xba000000 + port;

	/* ide0: HD64461 PCMCIA channel 1 (memory) */
	/* On HP690, CF in slot 1 is configured as a memory card
	   device.  See CF+ and CompactFlash Specification for the
	   detail of CF's memory mapped addressing. */
	if (0x1f0<=port && port<=0x1f7)	return 0xb5000000 + port;
	if (port == 0x3f6) return 0xb50001fe;
	if (port == 0x3f7) return 0xb50001ff;

	/* ide1 */
	if (0x170<=port && port<=0x177)	return 0xba000000 + port;
	if (port == 0x376) return 0xba000376;
	if (port == 0x377) return 0xba000377;
#endif

	/* ??? */
	if (port < 0xf000) return 0xa0000000 + port;
	/* PCMCIA channel 0, I/O (0xba000000) */
	if (port < 0x10000) return 0xba000000 + port - 0xf000;

	/* HD64461 internal devices (0xb0000000) */
	if (port < 0x20000) return CONFIG_HD64461_IOBASE + port - 0x10000;

	/* PCMCIA channel 0, I/O (0xba000000) */
	if (port < 0x30000) return 0xba000000 + port - 0x20000;

	/* PCMCIA channel 1, memory (0xb5000000) */
	if (port < 0x40000) return 0xb5000000 + port - 0x30000;

	/* Whole physical address space (0xa0000000) */
	return 0xa0000000 + (port & 0x1fffffff);
}

unsigned char hd64461_inb(unsigned long port)
{
	return *(volatile unsigned char*)PORT2ADDR(port);
}

unsigned char hd64461_inb_p(unsigned long port)
{
	unsigned long v = *(volatile unsigned char*)PORT2ADDR(port);
	ctrl_delay();
	return v;
}

unsigned short hd64461_inw(unsigned long port)
{
	return *(volatile unsigned short*)PORT2ADDR(port);
}

unsigned int hd64461_inl(unsigned long port)
{
	return *(volatile unsigned long*)PORT2ADDR(port);
}

void hd64461_outb(unsigned char b, unsigned long port)
{
	*(volatile unsigned char*)PORT2ADDR(port) = b;
}

void hd64461_outb_p(unsigned char b, unsigned long port)
{
	*(volatile unsigned char*)PORT2ADDR(port) = b;
	ctrl_delay();
}

void hd64461_outw(unsigned short b, unsigned long port)
{
	*(volatile unsigned short*)PORT2ADDR(port) = b;
}

void hd64461_outl(unsigned int b, unsigned long port)
{
        *(volatile unsigned long*)PORT2ADDR(port) = b;
}

void hd64461_insb(unsigned long port, void *buffer, unsigned long count)
{
	volatile unsigned char* addr=(volatile unsigned char*)PORT2ADDR(port);
	unsigned char *buf=buffer;
	while(count--) *buf++=*addr;
}

void hd64461_insw(unsigned long port, void *buffer, unsigned long count)
{
	volatile unsigned short* addr=(volatile unsigned short*)PORT2ADDR(port);
	unsigned short *buf=buffer;
	while(count--) *buf++=*addr;
}

void hd64461_insl(unsigned long port, void *buffer, unsigned long count)
{
	volatile unsigned long* addr=(volatile unsigned long*)PORT2ADDR(port);
	unsigned long *buf=buffer;
	while(count--) *buf++=*addr;
}

void hd64461_outsb(unsigned long port, const void *buffer, unsigned long count)
{
	volatile unsigned char* addr=(volatile unsigned char*)PORT2ADDR(port);
	const unsigned char *buf=buffer;
	while(count--) *addr=*buf++;
}

void hd64461_outsw(unsigned long port, const void *buffer, unsigned long count)
{
	volatile unsigned short* addr=(volatile unsigned short*)PORT2ADDR(port);
	const unsigned short *buf=buffer;
	while(count--) *addr=*buf++;
}

void hd64461_outsl(unsigned long port, const void *buffer, unsigned long count)
{
	volatile unsigned long* addr=(volatile unsigned long*)PORT2ADDR(port);
	const unsigned long *buf=buffer;
	while(count--) *addr=*buf++;
}

unsigned short hd64461_readw(void __iomem *addr)
{
	return ctrl_inw(MEM_BASE+(unsigned long __force)addr);
}

void hd64461_writew(unsigned short b, void __iomem *addr)
{
	ctrl_outw(b, MEM_BASE+(unsigned long __force)addr);
}

