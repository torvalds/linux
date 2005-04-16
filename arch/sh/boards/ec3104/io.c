/*
 * linux/arch/sh/kernel/io_ec3104.c
 *  EC3104 companion chip support
 *
 * Copyright (C) 2000 Philipp Rumpf <prumpf@tux.org>
 *
 */
/* EC3104 note:
 * This code was written without any documentation about the EC3104 chip.  While
 * I hope I got most of the basic functionality right, the register names I use
 * are most likely completely different from those in the chip documentation.
 *
 * If you have any further information about the EC3104, please tell me
 * (prumpf@tux.org).
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/ec3104/ec3104.h>

/*
 * EC3104 has a real ISA bus which we redirect low port accesses to (the
 * actual device on mine is a ESS 1868, and I don't want to hack the driver
 * more than strictly necessary).  I am not going to duplicate the
 * hard coding of PC addresses (for the 16550s aso) here though;  it's just
 * too ugly.
 */

#define low_port(port) ((port) < 0x10000)

static inline unsigned long port2addr(unsigned long port)
{
	switch(port >> 16) {
	case 0:
		return EC3104_ISA_BASE + port * 2;

		/* XXX hack. it's unclear what to do about the serial ports */
	case 1:
		return EC3104_BASE + (port&0xffff) * 4;

	default:
		/* XXX PCMCIA */
		return 0;
	}
}

unsigned char ec3104_inb(unsigned long port)
{
	u8 ret;

	ret = *(volatile u8 *)port2addr(port);

	return ret;
}

unsigned short ec3104_inw(unsigned long port)
{
	BUG();
}

unsigned long ec3104_inl(unsigned long port)
{
	BUG();
}

void ec3104_outb(unsigned char data, unsigned long port)
{
	*(volatile u8 *)port2addr(port) = data;
}

void ec3104_outw(unsigned short data, unsigned long port)
{
	BUG();
}

void ec3104_outl(unsigned long data, unsigned long port)
{
	BUG();
}
