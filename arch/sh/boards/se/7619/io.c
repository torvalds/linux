/*
 *
 * linux/arch/sh/boards/se/7619/io.c
 *
 * Copyright (C) 2006  Yoshinori Sato
 *
 * I/O routine for Hitachi 7619 SolutionEngine.
 *
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <asm/io.h>
#include <asm/se7619.h>
#include <asm/irq.h>

/* FIXME: M3A-ZAB7 Compact Flash Slot support */

static inline void delay(void)
{
	ctrl_inw(0xa0000000);	/* Uncached ROM area (P2) */
}

#define badio(name,port) \
  printk("bad I/O operation (%s) for port 0x%lx at 0x%08x\n", \
	 #name, (port), (__u32) __builtin_return_address(0))

unsigned char se7619___inb(unsigned long port)
{
	badio(inb, port);
	return 0;
}

unsigned char se7619___inb_p(unsigned long port)
{
	badio(inb_p, port);
	delay();
	return 0;
}

unsigned short se7619___inw(unsigned long port)
{
	badio(inw, port);
	return 0;
}

unsigned int se7619___inl(unsigned long port)
{
	badio(inl, port);
	return 0;
}

void se7619___outb(unsigned char value, unsigned long port)
{
	badio(outb, port);
}

void se7619___outb_p(unsigned char value, unsigned long port)
{
	badio(outb_p, port);
	delay();
}

void se7619___outw(unsigned short value, unsigned long port)
{
	badio(outw, port);
}

void se7619___outl(unsigned int value, unsigned long port)
{
	badio(outl, port);
}

void se7619___insb(unsigned long port, void *addr, unsigned long count)
{
	badio(inw, port);
}

void se7619___insw(unsigned long port, void *addr, unsigned long count)
{
	badio(inw, port);
}

void se7619___insl(unsigned long port, void *addr, unsigned long count)
{
	badio(insl, port);
}

void se7619___outsb(unsigned long port, const void *addr, unsigned long count)
{
	badio(insl, port);
}

void se7619___outsw(unsigned long port, const void *addr, unsigned long count)
{
	badio(insl, port);
}

void se7619___outsl(unsigned long port, const void *addr, unsigned long count)
{
	badio(outsw, port);
}
