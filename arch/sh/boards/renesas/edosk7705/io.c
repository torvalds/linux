/*
 * arch/sh/boards/renesas/edosk7705/io.c
 *
 * Copyright (C) 2001  Ian da Silva, Jeremy Siegel
 * Based largely on io_se.c.
 *
 * I/O routines for Hitachi EDOSK7705 board.
 *
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <asm/io.h>
#include <asm/edosk7705/io.h>
#include <asm/addrspace.h>

#define SMC_IOADDR	0xA2000000

#define maybebadio(name,port) \
  printk("bad PC-like io %s for port 0x%lx at 0x%08x\n", \
	 #name, (port), (__u32) __builtin_return_address(0))

/* Map the Ethernet addresses as if it is at 0x300 - 0x320 */
unsigned long sh_edosk7705_isa_port2addr(unsigned long port)
{
     if (port >= 0x300 && port < 0x320) {
	  /* SMC91C96 registers are 4 byte aligned rather than the
	   * usual 2 byte!
	   */
	  return SMC_IOADDR + ( (port - 0x300) * 2);
     }

     maybebadio(sh_edosk7705_isa_port2addr, port);
     return port;
}

/* Trying to read / write bytes on odd-byte boundaries to the Ethernet
 * registers causes problems. So we bit-shift the value and read / write
 * in 2 byte chunks. Setting the low byte to 0 does not cause problems
 * now as odd byte writes are only made on the bit mask / interrupt
 * register. This may not be the case in future Mar-2003 SJD
 */
unsigned char sh_edosk7705_inb(unsigned long port)
{
	if (port >= 0x300 && port < 0x320 && port & 0x01) {
		return (volatile unsigned char)(generic_inw(port -1) >> 8);
	}
	return *(volatile unsigned char *)sh_edosk7705_isa_port2addr(port);
}

unsigned int sh_edosk7705_inl(unsigned long port)
{
	return *(volatile unsigned long *)port;
}

void sh_edosk7705_outb(unsigned char value, unsigned long port)
{
	if (port >= 0x300 && port < 0x320 && port & 0x01) {
		generic_outw(((unsigned short)value << 8), port -1);
		return;
	}
	*(volatile unsigned char *)sh_edosk7705_isa_port2addr(port) = value;
}

void sh_edosk7705_outl(unsigned int value, unsigned long port)
{
	*(volatile unsigned long *)port = value;
}

void sh_edosk7705_insb(unsigned long port, void *addr, unsigned long count)
{
	unsigned char *p = addr;
	while (count--) *p++ = sh_edosk7705_inb(port);
}

void sh_edosk7705_insl(unsigned long port, void *addr, unsigned long count)
{
	unsigned long *p = (unsigned long*)addr;
	while (count--)
		*p++ = *(volatile unsigned long *)port;
}

void sh_edosk7705_outsb(unsigned long port, const void *addr, unsigned long count)
{
	unsigned char *p = (unsigned char*)addr;
	while (count--) sh_edosk7705_outb(*p++, port);
}

void sh_edosk7705_outsl(unsigned long port, const void *addr, unsigned long count)
{
	unsigned long *p = (unsigned long*)addr;
	while (count--) sh_edosk7705_outl(*p++, port);
}

