#ifndef _I8042_IP22_H
#define _I8042_IP22_H

#include <asm/sgi/ioc.h>
#include <asm/sgi/ip22.h>

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

/*
 * Names.
 */

#define I8042_KBD_PHYS_DESC "hpc3ps2/serio0"
#define I8042_AUX_PHYS_DESC "hpc3ps2/serio1"
#define I8042_MUX_PHYS_DESC "hpc3ps2/serio%d"

/*
 * IRQs.
 */

#define I8042_KBD_IRQ SGI_KEYBD_IRQ
#define I8042_AUX_IRQ SGI_KEYBD_IRQ

/*
 * Register numbers.
 */

#define I8042_COMMAND_REG	((unsigned long)&sgioc->kbdmouse.command)
#define I8042_STATUS_REG	((unsigned long)&sgioc->kbdmouse.command)
#define I8042_DATA_REG		((unsigned long)&sgioc->kbdmouse.data)

static inline int i8042_read_data(void)
{
	return sgioc->kbdmouse.data;
}

static inline int i8042_read_status(void)
{
	return sgioc->kbdmouse.command;
}

static inline void i8042_write_data(int val)
{
	sgioc->kbdmouse.data = val;
}

static inline void i8042_write_command(int val)
{
	sgioc->kbdmouse.command = val;
}

static inline int i8042_platform_init(void)
{
#if 0
	/* XXX sgi_kh is a virtual address */
	if (!request_mem_region(sgi_kh, sizeof(struct hpc_keyb), "i8042"))
		return -EBUSY;
#endif

	i8042_reset = 1;

	return 0;
}

static inline void i8042_platform_exit(void)
{
#if 0
	release_mem_region(JAZZ_KEYBOARD_ADDRESS, sizeof(struct hpc_keyb));
#endif
}

#endif /* _I8042_IP22_H */
