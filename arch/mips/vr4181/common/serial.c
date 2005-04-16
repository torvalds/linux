/*
 * Copyright 2001 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 *
 * arch/mips/vr4181/common/serial.c
 *     initialize serial port on vr4181.
 *
 * This program is free software; you can redistribute	it and/or modify it
 * under  the terms of	the GNU General	 Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

/*
 * [jsun, 010925]
 * You need to make sure rs_table has at least one element in
 * drivers/char/serial.c file.	There is no good way to do it right
 * now.	 A workaround is to include CONFIG_SERIAL_MANY_PORTS in your
 * configure file, which would gives you 64 ports and wastes 11K ram.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/serial.h>

#include <asm/vr4181/vr4181.h>

void __init vr4181_init_serial(void)
{
	struct serial_struct s;

	/* turn on UART clock */
	*VR4181_CMUCLKMSK |= VR4181_CMUCLKMSK_MSKSIU;

	/* clear memory */
	memset(&s, 0, sizeof(s));

	s.line = 0;			/* we set the first one */
	s.baud_base = 1152000;
	s.irq = VR4181_IRQ_SIU;
	s.flags = ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST; /* STD_COM_FLAGS */
	s.iomem_base = (u8*)VR4181_SIURB;
	s.iomem_reg_shift = 0;
	s.io_type = SERIAL_IO_MEM;
	if (early_serial_setup(&s) != 0) {
		panic("vr4181_init_serial() failed!");
	}
}

