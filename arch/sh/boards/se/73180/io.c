/*
 * arch/sh/boards/se/73180/io.c
 *
 * Copyright (C) 2003 YOSHII Takashi <yoshii-takashi@hitachi-ul.co.jp>
 * Based on arch/sh/boards/se/7300/io.c
 *
 * I/O routine for SH-Mobile3 73180 SolutionEngine.
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <asm/mach/se73180.h>
#include <asm/io.h>

#define badio(fn, a) panic("bad i/o operation %s for %08lx.", #fn, a)

struct iop {
	unsigned long start, end;
	unsigned long base;
	struct iop *(*check) (struct iop * p, unsigned long port);
	unsigned char (*inb) (struct iop * p, unsigned long port);
	unsigned short (*inw) (struct iop * p, unsigned long port);
	void (*outb) (struct iop * p, unsigned char value, unsigned long port);
	void (*outw) (struct iop * p, unsigned short value, unsigned long port);
};

struct iop *
simple_check(struct iop *p, unsigned long port)
{
	if ((p->start <= port) && (port <= p->end))
		return p;
	else
		badio(check, port);
}

struct iop *
ide_check(struct iop *p, unsigned long port)
{
	if (((0x1f0 <= port) && (port <= 0x1f7)) || (port == 0x3f7))
		return p;
	return NULL;
}

unsigned char
simple_inb(struct iop *p, unsigned long port)
{
	return *(unsigned char *) (p->base + port);
}

unsigned short
simple_inw(struct iop *p, unsigned long port)
{
	return *(unsigned short *) (p->base + port);
}

void
simple_outb(struct iop *p, unsigned char value, unsigned long port)
{
	*(unsigned char *) (p->base + port) = value;
}

void
simple_outw(struct iop *p, unsigned short value, unsigned long port)
{
	*(unsigned short *) (p->base + port) = value;
}

unsigned char
pcc_inb(struct iop *p, unsigned long port)
{
	unsigned long addr = p->base + port + 0x40000;
	unsigned long v;

	if (port & 1)
		addr += 0x00400000;
	v = *(volatile unsigned char *) addr;
	return v;
}

void
pcc_outb(struct iop *p, unsigned char value, unsigned long port)
{
	unsigned long addr = p->base + port + 0x40000;

	if (port & 1)
		addr += 0x00400000;
	*(volatile unsigned char *) addr = value;
}

unsigned char
bad_inb(struct iop *p, unsigned long port)
{
	badio(inb, port);
}

void
bad_outb(struct iop *p, unsigned char value, unsigned long port)
{
	badio(inw, port);
}

/* MSTLANEX01 LAN at 0xb400:0000 */
static struct iop laniop = {
	.start = 0x300,
	.end = 0x30f,
	.base = 0xb4000000,
	.check = simple_check,
	.inb = simple_inb,
	.inw = simple_inw,
	.outb = simple_outb,
	.outw = simple_outw,
};

/* NE2000 pc card NIC */
static struct iop neiop = {
	.start = 0x280,
	.end = 0x29f,
	.base = 0xb0600000 + 0x80,	/* soft 0x280 -> hard 0x300 */
	.check = simple_check,
	.inb = pcc_inb,
	.inw = simple_inw,
	.outb = pcc_outb,
	.outw = simple_outw,
};

/* CF in CF slot */
static struct iop cfiop = {
	.base = 0xb0600000,
	.check = ide_check,
	.inb = pcc_inb,
	.inw = simple_inw,
	.outb = pcc_outb,
	.outw = simple_outw,
};

static __inline__ struct iop *
port2iop(unsigned long port)
{
	if (0) ;
#if defined(CONFIG_SMC91111)
	else if (laniop.check(&laniop, port))
		return &laniop;
#endif
#if defined(CONFIG_NE2000)
	else if (neiop.check(&neiop, port))
		return &neiop;
#endif
#if defined(CONFIG_IDE)
	else if (cfiop.check(&cfiop, port))
		return &cfiop;
#endif
	else
		return &neiop;	/* fallback */
}

static inline void
delay(void)
{
	ctrl_inw(0xac000000);
	ctrl_inw(0xac000000);
}

unsigned char
sh73180se_inb(unsigned long port)
{
	struct iop *p = port2iop(port);
	return (p->inb) (p, port);
}

unsigned char
sh73180se_inb_p(unsigned long port)
{
	unsigned char v = sh73180se_inb(port);
	delay();
	return v;
}

unsigned short
sh73180se_inw(unsigned long port)
{
	struct iop *p = port2iop(port);
	return (p->inw) (p, port);
}

unsigned int
sh73180se_inl(unsigned long port)
{
	badio(inl, port);
}

void
sh73180se_outb(unsigned char value, unsigned long port)
{
	struct iop *p = port2iop(port);
	(p->outb) (p, value, port);
}

void
sh73180se_outb_p(unsigned char value, unsigned long port)
{
	sh73180se_outb(value, port);
	delay();
}

void
sh73180se_outw(unsigned short value, unsigned long port)
{
	struct iop *p = port2iop(port);
	(p->outw) (p, value, port);
}

void
sh73180se_outl(unsigned int value, unsigned long port)
{
	badio(outl, port);
}

void
sh73180se_insb(unsigned long port, void *addr, unsigned long count)
{
	unsigned char *a = addr;
	struct iop *p = port2iop(port);
	while (count--)
		*a++ = (p->inb) (p, port);
}

void
sh73180se_insw(unsigned long port, void *addr, unsigned long count)
{
	unsigned short *a = addr;
	struct iop *p = port2iop(port);
	while (count--)
		*a++ = (p->inw) (p, port);
}

void
sh73180se_insl(unsigned long port, void *addr, unsigned long count)
{
	badio(insl, port);
}

void
sh73180se_outsb(unsigned long port, const void *addr, unsigned long count)
{
	unsigned char *a = (unsigned char *) addr;
	struct iop *p = port2iop(port);
	while (count--)
		(p->outb) (p, *a++, port);
}

void
sh73180se_outsw(unsigned long port, const void *addr, unsigned long count)
{
	unsigned short *a = (unsigned short *) addr;
	struct iop *p = port2iop(port);
	while (count--)
		(p->outw) (p, *a++, port);
}

void
sh73180se_outsl(unsigned long port, const void *addr, unsigned long count)
{
	badio(outsw, port);
}
