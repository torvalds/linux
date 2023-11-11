/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BOOT_IO_H
#define BOOT_IO_H

#include <asm/shared/io.h>

#undef inb
#undef inw
#undef inl
#undef outb
#undef outw
#undef outl

struct port_io_ops {
	u8	(*f_inb)(u16 port);
	void	(*f_outb)(u8 v, u16 port);
	void	(*f_outw)(u16 v, u16 port);
};

extern struct port_io_ops pio_ops;

/*
 * Use the normal I/O instructions by default.
 * TDX guests override these to use hypercalls.
 */
static inline void init_default_io_ops(void)
{
	pio_ops.f_inb  = __inb;
	pio_ops.f_outb = __outb;
	pio_ops.f_outw = __outw;
}

/*
 * Redirect port I/O operations via pio_ops callbacks.
 * TDX guests override these callbacks with TDX-specific helpers.
 */
#define inb  pio_ops.f_inb
#define outb pio_ops.f_outb
#define outw pio_ops.f_outw

#endif
