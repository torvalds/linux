/*
 * Early serial console for 8250/16550 devices
 *
 * (c) Copyright 2004 Hewlett-Packard Development Company, L.P.
 *	Bjorn Helgaas <bjorn.helgaas@hp.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Based on the 8250.c serial driver, Copyright (C) 2001 Russell King,
 * and on early_printk.c by Andi Kleen.
 *
 * This is for use before the serial driver has initialized, in
 * particular, before the UARTs have been discovered and named.
 * Instead of specifying the console device as, e.g., "ttyS0",
 * we locate the device directly by its MMIO or I/O port address.
 *
 * The user can specify the device directly, e.g.,
 *	console=uart,io,0x3f8,9600n8
 *	console=uart,mmio,0xff5e0000,115200n8
 * or platform code can call early_uart_console_init() to set
 * the early UART device.
 *
 * After the normal serial driver starts, we try to locate the
 * matching ttyS device and start a console there.
 */

#include <linux/tty.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/serial_core.h>
#include <linux/serial_reg.h>
#include <linux/serial.h>
#include <asm/io.h>
#include <asm/serial.h>

struct early_uart_device {
	struct uart_port port;
	char options[16];		/* e.g., 115200n8 */
	unsigned int baud;
};

static struct early_uart_device early_device __initdata;
static int early_uart_registered __initdata;

static unsigned int __init serial_in(struct uart_port *port, int offset)
{
	if (port->iotype == UPIO_MEM)
		return readb(port->membase + offset);
	else
		return inb(port->iobase + offset);
}

static void __init serial_out(struct uart_port *port, int offset, int value)
{
	if (port->iotype == UPIO_MEM)
		writeb(value, port->membase + offset);
	else
		outb(value, port->iobase + offset);
}

#define BOTH_EMPTY (UART_LSR_TEMT | UART_LSR_THRE)

static void __init wait_for_xmitr(struct uart_port *port)
{
	unsigned int status;

	for (;;) {
		status = serial_in(port, UART_LSR);
		if ((status & BOTH_EMPTY) == BOTH_EMPTY)
			return;
		cpu_relax();
	}
}

static void __init putc(struct uart_port *port, int c)
{
	wait_for_xmitr(port);
	serial_out(port, UART_TX, c);
}

static void __init early_uart_write(struct console *console, const char *s, unsigned int count)
{
	struct uart_port *port = &early_device.port;
	unsigned int ier;

	/* Save the IER and disable interrupts */
	ier = serial_in(port, UART_IER);
	serial_out(port, UART_IER, 0);

	uart_console_write(port, s, count, putc);

	/* Wait for transmitter to become empty and restore the IER */
	wait_for_xmitr(port);
	serial_out(port, UART_IER, ier);
}

static unsigned int __init probe_baud(struct uart_port *port)
{
	unsigned char lcr, dll, dlm;
	unsigned int quot;

	lcr = serial_in(port, UART_LCR);
	serial_out(port, UART_LCR, lcr | UART_LCR_DLAB);
	dll = serial_in(port, UART_DLL);
	dlm = serial_in(port, UART_DLM);
	serial_out(port, UART_LCR, lcr);

	quot = (dlm << 8) | dll;
	return (port->uartclk / 16) / quot;
}

static void __init init_port(struct early_uart_device *device)
{
	struct uart_port *port = &device->port;
	unsigned int divisor;
	unsigned char c;

	serial_out(port, UART_LCR, 0x3);	/* 8n1 */
	serial_out(port, UART_IER, 0);		/* no interrupt */
	serial_out(port, UART_FCR, 0);		/* no fifo */
	serial_out(port, UART_MCR, 0x3);	/* DTR + RTS */

	divisor = port->uartclk / (16 * device->baud);
	c = serial_in(port, UART_LCR);
	serial_out(port, UART_LCR, c | UART_LCR_DLAB);
	serial_out(port, UART_DLL, divisor & 0xff);
	serial_out(port, UART_DLM, (divisor >> 8) & 0xff);
	serial_out(port, UART_LCR, c & ~UART_LCR_DLAB);
}

static int __init parse_options(struct early_uart_device *device, char *options)
{
	struct uart_port *port = &device->port;
	int mapsize = 64;
	int mmio, length;

	if (!options)
		return -ENODEV;

	port->uartclk = BASE_BAUD * 16;
	if (!strncmp(options, "mmio,", 5)) {
		port->iotype = UPIO_MEM;
		port->mapbase = simple_strtoul(options + 5, &options, 0);
		port->membase = ioremap(port->mapbase, mapsize);
		if (!port->membase) {
			printk(KERN_ERR "%s: Couldn't ioremap 0x%lx\n",
				__FUNCTION__, port->mapbase);
			return -ENOMEM;
		}
		mmio = 1;
	} else if (!strncmp(options, "io,", 3)) {
		port->iotype = UPIO_PORT;
		port->iobase = simple_strtoul(options + 3, &options, 0);
		mmio = 0;
	} else
		return -EINVAL;

	if ((options = strchr(options, ','))) {
		options++;
		device->baud = simple_strtoul(options, NULL, 0);
		length = min(strcspn(options, " "), sizeof(device->options));
		strncpy(device->options, options, length);
	} else {
		device->baud = probe_baud(port);
		snprintf(device->options, sizeof(device->options), "%u",
			device->baud);
	}

	printk(KERN_INFO "Early serial console at %s 0x%lx (options '%s')\n",
		mmio ? "MMIO" : "I/O port",
		mmio ? port->mapbase : (unsigned long) port->iobase,
		device->options);
	return 0;
}

static int __init early_uart_setup(struct console *console, char *options)
{
	struct early_uart_device *device = &early_device;
	int err;

	if (device->port.membase || device->port.iobase)
		return 0;

	if ((err = parse_options(device, options)) < 0)
		return err;

	init_port(device);
	return 0;
}

static struct console early_uart_console __initdata = {
	.name	= "uart",
	.write	= early_uart_write,
	.setup	= early_uart_setup,
	.flags	= CON_PRINTBUFFER,
	.index	= -1,
};

static int __init early_uart_console_init(void)
{
	if (!early_uart_registered) {
		register_console(&early_uart_console);
		early_uart_registered = 1;
	}
	return 0;
}
console_initcall(early_uart_console_init);

int __init early_serial_console_init(char *cmdline)
{
	char *options;
	int err;

	options = strstr(cmdline, "console=uart,");
	if (!options)
		return -ENODEV;

	options = strchr(cmdline, ',') + 1;
	if ((err = early_uart_setup(NULL, options)) < 0)
		return err;
	return early_uart_console_init();
}

static int __init early_uart_console_switch(void)
{
	struct early_uart_device *device = &early_device;
	struct uart_port *port = &device->port;
	int mmio, line;

	if (!(early_uart_console.flags & CON_ENABLED))
		return 0;

	/* Try to start the normal driver on a matching line.  */
	mmio = (port->iotype == UPIO_MEM);
	line = serial8250_start_console(port, device->options);
	if (line < 0)
		printk("No ttyS device at %s 0x%lx for console\n",
			mmio ? "MMIO" : "I/O port",
			mmio ? port->mapbase :
			    (unsigned long) port->iobase);

	unregister_console(&early_uart_console);
	if (mmio)
		iounmap(port->membase);

	return 0;
}
late_initcall(early_uart_console_switch);
