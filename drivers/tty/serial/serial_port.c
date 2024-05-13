// SPDX-License-Identifier: GPL-2.0+
/*
 * Serial core port device driver
 *
 * Copyright (C) 2023 Texas Instruments Incorporated - https://www.ti.com/
 * Author: Tony Lindgren <tony@atomide.com>
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/serial_core.h>
#include <linux/spinlock.h>

#include "serial_base.h"

#define SERIAL_PORT_AUTOSUSPEND_DELAY_MS	500

/* Only considers pending TX for now. Caller must take care of locking */
static int __serial_port_busy(struct uart_port *port)
{
	return !uart_tx_stopped(port) &&
		uart_circ_chars_pending(&port->state->xmit);
}

static int serial_port_runtime_resume(struct device *dev)
{
	struct serial_port_device *port_dev = to_serial_base_port_device(dev);
	struct uart_port *port;
	unsigned long flags;

	port = port_dev->port;

	if (port->flags & UPF_DEAD)
		goto out;

	/* Flush any pending TX for the port */
	spin_lock_irqsave(&port->lock, flags);
	if (__serial_port_busy(port))
		port->ops->start_tx(port);
	spin_unlock_irqrestore(&port->lock, flags);

out:
	pm_runtime_mark_last_busy(dev);

	return 0;
}

static DEFINE_RUNTIME_DEV_PM_OPS(serial_port_pm,
				 NULL, serial_port_runtime_resume, NULL);

static int serial_port_probe(struct device *dev)
{
	pm_runtime_enable(dev);
	pm_runtime_set_autosuspend_delay(dev, SERIAL_PORT_AUTOSUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(dev);

	return 0;
}

static int serial_port_remove(struct device *dev)
{
	pm_runtime_dont_use_autosuspend(dev);
	pm_runtime_disable(dev);

	return 0;
}

/*
 * Serial core port device init functions. Note that the physical serial
 * port device driver may not have completed probe at this point.
 */
int uart_add_one_port(struct uart_driver *drv, struct uart_port *port)
{
	return serial_ctrl_register_port(drv, port);
}
EXPORT_SYMBOL(uart_add_one_port);

void uart_remove_one_port(struct uart_driver *drv, struct uart_port *port)
{
	serial_ctrl_unregister_port(drv, port);
}
EXPORT_SYMBOL(uart_remove_one_port);

static struct device_driver serial_port_driver = {
	.name = "port",
	.suppress_bind_attrs = true,
	.probe = serial_port_probe,
	.remove = serial_port_remove,
	.pm = pm_ptr(&serial_port_pm),
};

int serial_base_port_init(void)
{
	return serial_base_driver_register(&serial_port_driver);
}

void serial_base_port_exit(void)
{
	serial_base_driver_unregister(&serial_port_driver);
}

MODULE_AUTHOR("Tony Lindgren <tony@atomide.com>");
MODULE_DESCRIPTION("Serial controller port driver");
MODULE_LICENSE("GPL");
