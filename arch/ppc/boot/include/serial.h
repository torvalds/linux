/*
 * A really private header file for the (dumb) serial driver in arch/ppc/boot
 *
 * Shamelessly taken from include/linux/serialP.h:
 *
 * Copyright (C) 1997 by Theodore Ts'o.
 *
 * Redistribution of this file is permitted under the terms of the GNU
 * Public License (GPL)
 */

#ifndef _PPC_BOOT_SERIALP_H
#define _PPC_BOOT_SERIALP_H

/*
 * This is our internal structure for each serial port's state.
 *
 * Many fields are paralleled by the structure used by the serial_struct
 * structure.
 *
 * Given that this is how SERIAL_PORT_DFNS are done, and that we need
 * to use a few of their fields, we need to have our own copy of it.
 */
struct serial_state {
	int	magic;
	int	baud_base;
	unsigned long	port;
	int	irq;
	int	flags;
	int	hub6;
	int	type;
	int	line;
	int	revision;	/* Chip revision (950) */
	int	xmit_fifo_size;
	int	custom_divisor;
	int	count;
	u8	*iomem_base;
	u16	iomem_reg_shift;
	unsigned short	close_delay;
	unsigned short	closing_wait; /* time to wait before closing */
	unsigned long	icount;
	int	io_type;
	void    *info;
	void    *dev;
};
#endif /* _PPC_BOOT_SERIAL_H */
