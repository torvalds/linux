// SPDX-License-Identifier: GPL-2.0+

#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

#include <linux/serial.h>
#include <linux/serial_8250.h>

#include "8250.h"

#define PORT_RSA_MAX 4
static unsigned long probe_rsa[PORT_RSA_MAX];
static unsigned int probe_rsa_count;

static int rsa8250_request_resource(struct uart_8250_port *up)
{
	unsigned long start = UART_RSA_BASE << up->port.regshift;
	unsigned int size = 8 << up->port.regshift;
	struct uart_port *port = &up->port;
	int ret = -EINVAL;

	switch (port->iotype) {
	case UPIO_HUB6:
	case UPIO_PORT:
		start += port->iobase;
		if (request_region(start, size, "serial-rsa"))
			ret = 0;
		else
			ret = -EBUSY;
		break;
	}

	return ret;
}

static void rsa8250_release_resource(struct uart_8250_port *up)
{
	unsigned long offset = UART_RSA_BASE << up->port.regshift;
	unsigned int size = 8 << up->port.regshift;
	struct uart_port *port = &up->port;

	switch (port->iotype) {
	case UPIO_HUB6:
	case UPIO_PORT:
		release_region(port->iobase + offset, size);
		break;
	}
}

static void univ8250_config_port(struct uart_port *port, int flags)
{
	struct uart_8250_port *up = up_to_u8250p(port);
	unsigned int i;

	up->probe &= ~UART_PROBE_RSA;
	if (port->type == PORT_RSA) {
		if (rsa8250_request_resource(up) == 0)
			up->probe |= UART_PROBE_RSA;
	} else if (flags & UART_CONFIG_TYPE) {
		for (i = 0; i < probe_rsa_count; i++) {
			if (probe_rsa[i] == up->port.iobase) {
				if (rsa8250_request_resource(up) == 0)
					up->probe |= UART_PROBE_RSA;
				break;
			}
		}
	}

	univ8250_port_base_ops->config_port(port, flags);

	if (port->type != PORT_RSA && up->probe & UART_PROBE_RSA)
		rsa8250_release_resource(up);
}

static int univ8250_request_port(struct uart_port *port)
{
	struct uart_8250_port *up = up_to_u8250p(port);
	int ret;

	ret = univ8250_port_base_ops->request_port(port);
	if (ret == 0 && port->type == PORT_RSA) {
		ret = rsa8250_request_resource(up);
		if (ret < 0)
			univ8250_port_base_ops->release_port(port);
	}

	return ret;
}

static void univ8250_release_port(struct uart_port *port)
{
	struct uart_8250_port *up = up_to_u8250p(port);

	if (port->type == PORT_RSA)
		rsa8250_release_resource(up);
	univ8250_port_base_ops->release_port(port);
}

void univ8250_rsa_support(struct uart_ops *ops)
{
	ops->config_port  = univ8250_config_port;
	ops->request_port = univ8250_request_port;
	ops->release_port = univ8250_release_port;
}

module_param_hw_array(probe_rsa, ulong, ioport, &probe_rsa_count, 0444);
MODULE_PARM_DESC(probe_rsa, "Probe I/O ports for RSA");

#ifdef CONFIG_SERIAL_8250_DEPRECATED_OPTIONS
#ifndef MODULE
/*
 * Keep the old "8250" name working as well for the module options so we don't
 * break people. We need to keep the names identical and the convenient macros
 * will happily refuse to let us do that by failing the build with redefinition
 * errors of global variables.  So we stick them inside a dummy function to
 * avoid those conflicts.  The options still get parsed, and the redefined
 * MODULE_PARAM_PREFIX lets us keep the "8250." syntax alive.
 *
 * This is hacky. I'm sorry.
 */
static void __used rsa8250_options(void)
{
#undef MODULE_PARAM_PREFIX
#define MODULE_PARAM_PREFIX "8250_core."

	__module_param_call(MODULE_PARAM_PREFIX, probe_rsa,
		&param_array_ops, .arr = &__param_arr_probe_rsa,
		0444, -1, 0);
}
#endif
#endif
