/*
 * Actions Semi Owl family serial console
 *
 * Copyright 2013 Actions Semi Inc.
 * Author: Actions Semi, Inc.
 *
 * Copyright (c) 2016-2017 Andreas Färber
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/console.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/serial.h>
#include <linux/serial_core.h>

#define OWL_UART_CTL	0x000
#define OWL_UART_TXDAT	0x008
#define OWL_UART_STAT	0x00c

#define OWL_UART_CTL_TRFS_TX		BIT(14)
#define OWL_UART_CTL_EN			BIT(15)
#define OWL_UART_CTL_RXIE		BIT(18)
#define OWL_UART_CTL_TXIE		BIT(19)

#define OWL_UART_STAT_RIP		BIT(0)
#define OWL_UART_STAT_TIP		BIT(1)
#define OWL_UART_STAT_TFFU		BIT(6)
#define OWL_UART_STAT_TRFL_MASK		(0x1f << 11)
#define OWL_UART_STAT_UTBB		BIT(17)

static inline void owl_uart_write(struct uart_port *port, u32 val, unsigned int off)
{
	writel(val, port->membase + off);
}

static inline u32 owl_uart_read(struct uart_port *port, unsigned int off)
{
	return readl(port->membase + off);
}

#ifdef CONFIG_SERIAL_OWL_CONSOLE

static void owl_console_putchar(struct uart_port *port, int ch)
{
	if (!port->membase)
		return;

	while (owl_uart_read(port, OWL_UART_STAT) & OWL_UART_STAT_TFFU)
		cpu_relax();

	owl_uart_write(port, ch, OWL_UART_TXDAT);
}

static void owl_uart_port_write(struct uart_port *port, const char *s,
				u_int count)
{
	u32 old_ctl, val;
	unsigned long flags;
	int locked;

	local_irq_save(flags);

	if (port->sysrq)
		locked = 0;
	else if (oops_in_progress)
		locked = spin_trylock(&port->lock);
	else {
		spin_lock(&port->lock);
		locked = 1;
	}

	old_ctl = owl_uart_read(port, OWL_UART_CTL);
	val = old_ctl | OWL_UART_CTL_TRFS_TX;
	/* disable IRQ */
	val &= ~(OWL_UART_CTL_RXIE | OWL_UART_CTL_TXIE);
	owl_uart_write(port, val, OWL_UART_CTL);

	uart_console_write(port, s, count, owl_console_putchar);

	/* wait until all contents have been sent out */
	while (owl_uart_read(port, OWL_UART_STAT) & OWL_UART_STAT_TRFL_MASK)
		cpu_relax();

	/* clear IRQ pending */
	val = owl_uart_read(port, OWL_UART_STAT);
	val |= OWL_UART_STAT_TIP | OWL_UART_STAT_RIP;
	owl_uart_write(port, val, OWL_UART_STAT);

	owl_uart_write(port, old_ctl, OWL_UART_CTL);

	if (locked)
		spin_unlock(&port->lock);

	local_irq_restore(flags);
}

static void owl_uart_early_console_write(struct console *co,
					 const char *s,
					 u_int count)
{
	struct earlycon_device *dev = co->data;

	owl_uart_port_write(&dev->port, s, count);
}

static int __init
owl_uart_early_console_setup(struct earlycon_device *device, const char *opt)
{
	if (!device->port.membase)
		return -ENODEV;

	device->con->write = owl_uart_early_console_write;

	return 0;
}
OF_EARLYCON_DECLARE(owl, "actions,owl-uart",
		    owl_uart_early_console_setup);

#endif /* CONFIG_SERIAL_OWL_CONSOLE */
