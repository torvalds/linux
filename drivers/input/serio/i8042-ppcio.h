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
	i8042_reset = I8042_RESET_ALWAYS;
	return 0;
}

static inline void i8042_platform_exit(void)
{
}

#else

#include "i8042-io.h"

#endif

#endif /* _I8042_PPCIO_H */
