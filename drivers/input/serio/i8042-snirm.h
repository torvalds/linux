#ifndef _I8042_SNIRM_H
#define _I8042_SNIRM_H

#include <asm/sni.h>

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

/*
 * Names.
 */

#define I8042_KBD_PHYS_DESC "onboard/serio0"
#define I8042_AUX_PHYS_DESC "onboard/serio1"
#define I8042_MUX_PHYS_DESC "onboard/serio%d"

/*
 * IRQs.
 */
static int i8042_kbd_irq;
static int i8042_aux_irq;
#define I8042_KBD_IRQ i8042_kbd_irq
#define I8042_AUX_IRQ i8042_aux_irq

static void __iomem *kbd_iobase;

#define I8042_COMMAND_REG	(kbd_iobase + 0x64UL)
#define I8042_DATA_REG		(kbd_iobase + 0x60UL)

static inline int i8042_read_data(void)
{
	return readb(kbd_iobase + 0x60UL);
}

static inline int i8042_read_status(void)
{
	return readb(kbd_iobase + 0x64UL);
}

static inline void i8042_write_data(int val)
{
	writeb(val, kbd_iobase + 0x60UL);
}

static inline void i8042_write_command(int val)
{
	writeb(val, kbd_iobase + 0x64UL);
}
static inline int i8042_platform_init(void)
{
	/* RM200 is strange ... */
	if (sni_brd_type == SNI_BRD_RM200) {
		kbd_iobase = ioremap(0x16000000, 4);
		i8042_kbd_irq = 33;
		i8042_aux_irq = 44;
	} else {
		kbd_iobase = ioremap(0x14000000, 4);
		i8042_kbd_irq = 1;
		i8042_aux_irq = 12;
	}
	if (!kbd_iobase)
		return -ENOMEM;

	return 0;
}

static inline void i8042_platform_exit(void)
{

}

#endif /* _I8042_SNIRM_H */
