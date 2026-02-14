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

static const struct uart_ops *core_port_base_ops;

static int rsa8250_request_resource(struct uart_8250_port *up)
{
	struct uart_port *port = &up->port;
	unsigned long start = UART_RSA_BASE << port->regshift;
	unsigned int size = 8 << port->regshift;

	switch (port->iotype) {
	case UPIO_HUB6:
	case UPIO_PORT:
		start += port->iobase;
		if (!request_region(start, size, "serial-rsa"))
			return -EBUSY;
		return 0;
	default:
		return -EINVAL;
	}
}

static void rsa8250_release_resource(struct uart_8250_port *up)
{
	struct uart_port *port = &up->port;
	unsigned long offset = UART_RSA_BASE << port->regshift;
	unsigned int size = 8 << port->regshift;

	switch (port->iotype) {
	case UPIO_HUB6:
	case UPIO_PORT:
		release_region(port->iobase + offset, size);
		break;
	default:
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

	core_port_base_ops->config_port(port, flags);

	if (port->type != PORT_RSA && up->probe & UART_PROBE_RSA)
		rsa8250_release_resource(up);
}

static int univ8250_request_port(struct uart_port *port)
{
	struct uart_8250_port *up = up_to_u8250p(port);
	int ret;

	ret = core_port_base_ops->request_port(port);
	if (ret == 0 && port->type == PORT_RSA) {
		ret = rsa8250_request_resource(up);
		if (ret < 0)
			core_port_base_ops->release_port(port);
	}

	return ret;
}

static void univ8250_release_port(struct uart_port *port)
{
	struct uart_8250_port *up = up_to_u8250p(port);

	if (port->type == PORT_RSA)
		rsa8250_release_resource(up);
	core_port_base_ops->release_port(port);
}

/*
 * It is not allowed to directly reference any symbols from 8250.ko here as
 * that would result in a dependency loop between the 8250.ko and
 * 8250_base.ko modules. This function is called from 8250.ko and is used to
 * break the symbolic dependency cycle. Anything that is needed from 8250.ko
 * has to be passed as pointers to this function which then can adjust those
 * variables on 8250.ko side or store them locally as needed.
 */
void univ8250_rsa_support(struct uart_ops *ops, const struct uart_ops *core_ops)
{
	core_port_base_ops = core_ops;
	ops->config_port  = univ8250_config_port;
	ops->request_port = univ8250_request_port;
	ops->release_port = univ8250_release_port;
}
EXPORT_SYMBOL_FOR_MODULES(univ8250_rsa_support, "8250");

module_param_hw_array(probe_rsa, ulong, ioport, &probe_rsa_count, 0444);
MODULE_PARM_DESC(probe_rsa, "Probe I/O ports for RSA");

/*
 * Attempts to turn on the RSA FIFO.  Returns zero on failure.
 * We set the port uart clock rate if we succeed.
 */
static int __rsa_enable(struct uart_8250_port *up)
{
	unsigned char mode;
	int result;

	mode = serial_in(up, UART_RSA_MSR);
	result = mode & UART_RSA_MSR_FIFO;

	if (!result) {
		serial_out(up, UART_RSA_MSR, mode | UART_RSA_MSR_FIFO);
		mode = serial_in(up, UART_RSA_MSR);
		result = mode & UART_RSA_MSR_FIFO;
	}

	if (result)
		up->port.uartclk = SERIAL_RSA_BAUD_BASE * 16;

	return result;
}

/*
 * If this is an RSA port, see if we can kick it up to the higher speed clock.
 */
void rsa_enable(struct uart_8250_port *up)
{
	if (up->port.type != PORT_RSA)
		return;

	if (up->port.uartclk != SERIAL_RSA_BAUD_BASE * 16) {
		guard(uart_port_lock_irq)(&up->port);
		__rsa_enable(up);
	}
	if (up->port.uartclk == SERIAL_RSA_BAUD_BASE * 16)
		serial_out(up, UART_RSA_FRR, 0);
}

/*
 * Attempts to turn off the RSA FIFO and resets the RSA board back to 115kbps compat mode. It is
 * unknown why interrupts were disabled in here. However, the caller is expected to preserve this
 * behaviour by grabbing the spinlock before calling this function.
 */
void rsa_disable(struct uart_8250_port *up)
{
	unsigned char mode;
	int result;

	if (up->port.type != PORT_RSA)
		return;

	if (up->port.uartclk != SERIAL_RSA_BAUD_BASE * 16)
		return;

	guard(uart_port_lock_irq)(&up->port);

	mode = serial_in(up, UART_RSA_MSR);
	result = !(mode & UART_RSA_MSR_FIFO);

	if (!result) {
		serial_out(up, UART_RSA_MSR, mode & ~UART_RSA_MSR_FIFO);
		mode = serial_in(up, UART_RSA_MSR);
		result = !(mode & UART_RSA_MSR_FIFO);
	}

	if (result)
		up->port.uartclk = SERIAL_RSA_BAUD_BASE_LO * 16;
}

void rsa_autoconfig(struct uart_8250_port *up)
{
	/* Only probe for RSA ports if we got the region. */
	if (up->port.type != PORT_16550A)
		return;
	if (!(up->probe & UART_PROBE_RSA))
		return;

	if (__rsa_enable(up))
		up->port.type = PORT_RSA;
}

void rsa_reset(struct uart_8250_port *up)
{
	if (up->port.type != PORT_RSA)
		return;

	serial_out(up, UART_RSA_FRR, 0);
}

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
