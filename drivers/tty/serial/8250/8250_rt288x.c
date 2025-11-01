// SPDX-License-Identifier: GPL-2.0
/*
 * RT288x/Au1xxx driver
 */

#include <linux/module.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/serial.h>
#include <linux/serial_8250.h>

#include "8250.h"

#define RT288X_DL	0x28

/* Au1x00/RT288x UART hardware has a weird register layout */
static const u8 au_io_in_map[7] = {
	[UART_RX]	= 0,
	[UART_IER]	= 2,
	[UART_IIR]	= 3,
	[UART_LCR]	= 5,
	[UART_MCR]	= 6,
	[UART_LSR]	= 7,
	[UART_MSR]	= 8,
};

static const u8 au_io_out_map[5] = {
	[UART_TX]	= 1,
	[UART_IER]	= 2,
	[UART_FCR]	= 4,
	[UART_LCR]	= 5,
	[UART_MCR]	= 6,
};

static u32 au_serial_in(struct uart_port *p, unsigned int offset)
{
	if (offset >= ARRAY_SIZE(au_io_in_map))
		return UINT_MAX;
	offset = au_io_in_map[offset];

	return __raw_readl(p->membase + (offset << p->regshift));
}

static void au_serial_out(struct uart_port *p, unsigned int offset, u32 value)
{
	if (offset >= ARRAY_SIZE(au_io_out_map))
		return;
	offset = au_io_out_map[offset];

	__raw_writel(value, p->membase + (offset << p->regshift));
}

/* Au1x00 haven't got a standard divisor latch */
static u32 au_serial_dl_read(struct uart_8250_port *up)
{
	return __raw_readl(up->port.membase + RT288X_DL);
}

static void au_serial_dl_write(struct uart_8250_port *up, u32 value)
{
	__raw_writel(value, up->port.membase + RT288X_DL);
}

int au_platform_setup(struct plat_serial8250_port *p)
{
	p->iotype = UPIO_AU;

	p->serial_in = au_serial_in;
	p->serial_out = au_serial_out;
	p->dl_read = au_serial_dl_read;
	p->dl_write = au_serial_dl_write;

	p->mapsize = 0x1000;

	p->bugs |= UART_BUG_NOMSR;

	return 0;
}
EXPORT_SYMBOL_GPL(au_platform_setup);

int rt288x_setup(struct uart_port *p)
{
	struct uart_8250_port *up = up_to_u8250p(p);

	p->iotype = UPIO_AU;

	p->serial_in = au_serial_in;
	p->serial_out = au_serial_out;
	up->dl_read = au_serial_dl_read;
	up->dl_write = au_serial_dl_write;

	p->mapsize = 0x100;

	up->bugs |= UART_BUG_NOMSR;

	return 0;
}
EXPORT_SYMBOL_GPL(rt288x_setup);

#ifdef CONFIG_SERIAL_8250_CONSOLE
static void au_putc(struct uart_port *port, unsigned char c)
{
	unsigned int status;

	au_serial_out(port, UART_TX, c);

	for (;;) {
		status = au_serial_in(port, UART_LSR);
		if (uart_lsr_tx_empty(status))
			break;
		cpu_relax();
	}
}

static void au_early_serial8250_write(struct console *console,
				      const char *s, unsigned int count)
{
	struct earlycon_device *device = console->data;
	struct uart_port *port = &device->port;

	uart_console_write(port, s, count, au_putc);
}

static int __init early_au_setup(struct earlycon_device *dev, const char *opt)
{
	rt288x_setup(&dev->port);
	dev->con->write = au_early_serial8250_write;

	return 0;
}
OF_EARLYCON_DECLARE(palmchip, "ralink,rt2880-uart", early_au_setup);
#endif

MODULE_DESCRIPTION("RT288x/Au1xxx UART driver");
MODULE_LICENSE("GPL");
