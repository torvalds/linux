/*
 * Code specific to PKUnity SoC and UniCore ISA
 *
 *	Maintained by GUAN Xue-tao <gxt@mprc.pku.edu.cn>
 *	Copyright (C) 2001-2011 Guan Xuetao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _I8042_UNICORE32_H
#define _I8042_UNICORE32_H

#include <mach/hardware.h>

/*
 * Names.
 */
#define I8042_KBD_PHYS_DESC "isa0060/serio0"
#define I8042_AUX_PHYS_DESC "isa0060/serio1"
#define I8042_MUX_PHYS_DESC "isa0060/serio%d"

/*
 * IRQs.
 */
#define I8042_KBD_IRQ           IRQ_PS2_KBD
#define I8042_AUX_IRQ           IRQ_PS2_AUX

/*
 * Register numbers.
 */
#define I8042_COMMAND_REG	((unsigned long)&PS2_COMMAND)
#define I8042_STATUS_REG	((unsigned long)&PS2_STATUS)
#define I8042_DATA_REG		((unsigned long)&PS2_DATA)

static inline int i8042_read_data(void)
{
	return inb(I8042_DATA_REG);
}

static inline int i8042_read_status(void)
{
	return inb(I8042_STATUS_REG);
}

static inline void i8042_write_data(int val)
{
	outb(val, I8042_DATA_REG);
}

static inline void i8042_write_command(int val)
{
	outb(val, I8042_COMMAND_REG);
}

static inline int i8042_platform_init(void)
{
	if (!request_region(I8042_DATA_REG, 16, "i8042"))
		return -EBUSY;

	i8042_reset = 1;
	return 0;
}

static inline void i8042_platform_exit(void)
{
	release_region(I8042_DATA_REG, 16);
}

#endif /* _I8042_UNICORE32_H */
