/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _I8042_JAZZ_H
#define _I8042_JAZZ_H

#include <asm/jazz.h>


/*
 * Names.
 */

#define I8042_KBD_PHYS_DESC "R4030/serio0"
#define I8042_AUX_PHYS_DESC "R4030/serio1"
#define I8042_MUX_PHYS_DESC "R4030/serio%d"

/*
 * IRQs.
 */

#define I8042_KBD_IRQ JAZZ_KEYBOARD_IRQ
#define I8042_AUX_IRQ JAZZ_MOUSE_IRQ

#define I8042_COMMAND_REG	((unsigned long)&jazz_kh->command)
#define I8042_STATUS_REG	((unsigned long)&jazz_kh->command)
#define I8042_DATA_REG		((unsigned long)&jazz_kh->data)

static inline int i8042_read_data(void)
{
	return jazz_kh->data;
}

static inline int i8042_read_status(void)
{
	return jazz_kh->command;
}

static inline void i8042_write_data(int val)
{
	jazz_kh->data = val;
}

static inline void i8042_write_command(int val)
{
	jazz_kh->command = val;
}

static inline int i8042_platform_init(void)
{
#if 0
	/* XXX JAZZ_KEYBOARD_ADDRESS is a virtual address */
	if (!request_mem_region(JAZZ_KEYBOARD_ADDRESS, 2, "i8042"))
		return -EBUSY;
#endif

	return 0;
}

static inline void i8042_platform_exit(void)
{
#if 0
	release_mem_region(JAZZ_KEYBOARD_ADDRESS, 2);
#endif
}

#endif /* _I8042_JAZZ_H */
