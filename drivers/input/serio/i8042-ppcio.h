#ifndef _I8042_PPCIO_H
#define _I8042_PPCIO_H

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#if defined(CONFIG_WALNUT)

#define I8042_KBD_IRQ 25
#define I8042_AUX_IRQ 26

#define I8042_KBD_PHYS_DESC "walnutps2/serio0"
#define I8042_AUX_PHYS_DESC "walnutps2/serio1"
#define I8042_MUX_PHYS_DESC "walnutps2/serio%d"

extern void *kb_cs;
extern void *kb_data;

#define I8042_COMMAND_REG (*(int *)kb_cs)
#define I8042_DATA_REG (*(int *)kb_data)

static inline int i8042_read_data(void)
{
	return readb(kb_data);
}

static inline int i8042_read_status(void)
{
	return readb(kb_cs);
}

static inline void i8042_write_data(int val)
{
	writeb(val, kb_data);
}

static inline void i8042_write_command(int val)
{
	writeb(val, kb_cs);
}

static inline int i8042_platform_init(void)
{
	i8042_reset = 1;
	return 0;
}

static inline void i8042_platform_exit(void)
{
}

#elif defined(CONFIG_SPRUCE)

#define I8042_KBD_IRQ 22
#define I8042_AUX_IRQ 21

#define I8042_KBD_PHYS_DESC "spruceps2/serio0"
#define I8042_AUX_PHYS_DESC "spruceps2/serio1"
#define I8042_MUX_PHYS_DESC "spruceps2/serio%d"

#define I8042_COMMAND_REG 0xff810000
#define I8042_DATA_REG 0xff810001

static inline int i8042_read_data(void)
{
	unsigned long kbd_data;

	__raw_writel(0x00000088, 0xff500008);
	eieio();

	__raw_writel(0x03000000, 0xff50000c);
	eieio();

	asm volatile("lis     7,0xff88        \n\
		      lswi    6,7,0x8         \n\
		      mr      %0,6"
	              : "=r" (kbd_data) :: "6", "7");

	__raw_writel(0x00000000, 0xff50000c);
	eieio();

	return (unsigned char)(kbd_data >> 24);
}

static inline int i8042_read_status(void)
{
	unsigned long kbd_status;

	__raw_writel(0x00000088, 0xff500008);
	eieio();

	__raw_writel(0x03000000, 0xff50000c);
	eieio();

	asm volatile("lis     7,0xff88        \n\
		      ori     7,7,0x8         \n\
		      lswi    6,7,0x8         \n\
		      mr      %0,6"
		      : "=r" (kbd_status) :: "6", "7");

	__raw_writel(0x00000000, 0xff50000c);
	eieio();

	return (unsigned char)(kbd_status >> 24);
}

static inline void i8042_write_data(int val)
{
	*((unsigned char *)0xff810000) = (char)val;
}

static inline void i8042_write_command(int val)
{
	*((unsigned char *)0xff810001) = (char)val;
}

static inline int i8042_platform_init(void)
{
	i8042_reset = 1;
	return 0;
}

static inline void i8042_platform_exit(void)
{
}

#else

#include "i8042-io.h"

#endif

#endif /* _I8042_PPCIO_H */
