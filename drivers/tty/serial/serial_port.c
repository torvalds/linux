// SPDX-License-Identifier: GPL-2.0+
/*
 * Serial core port device driver
 *
 * Copyright (C) 2023 Texas Instruments Incorporated - https://www.ti.com/
 * Author: Tony Lindgren <tony@atomide.com>
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/pnp.h>
#include <linux/property.h>
#include <linux/serial_core.h>
#include <linux/spinlock.h>

#include "serial_base.h"

#define SERIAL_PORT_AUTOSUSPEND_DELAY_MS	500

/* Only considers pending TX for now. Caller must take care of locking */
static int __serial_port_busy(struct uart_port *port)
{
	return !uart_tx_stopped(port) &&
		!kfifo_is_empty(&port->state->port.xmit_fifo);
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
	uart_port_lock_irqsave(port, &flags);
	if (!port_dev->tx_enabled)
		goto unlock;
	if (__serial_port_busy(port))
		port->ops->start_tx(port);

unlock:
	uart_port_unlock_irqrestore(port, flags);

out:
	pm_runtime_mark_last_busy(dev);

	return 0;
}

static int serial_port_runtime_suspend(struct device *dev)
{
	struct serial_port_device *port_dev = to_serial_base_port_device(dev);
	struct uart_port *port = port_dev->port;
	unsigned long flags;
	bool busy;

	if (port->flags & UPF_DEAD)
		return 0;

	/*
	 * Nothing to do on pm_runtime_force_suspend(), see
	 * DEFINE_RUNTIME_DEV_PM_OPS.
	 */
	if (!pm_runtime_enabled(dev))
		return 0;

	uart_port_lock_irqsave(port, &flags);
	if (!port_dev->tx_enabled) {
		uart_port_unlock_irqrestore(port, flags);
		return 0;
	}

	busy = __serial_port_busy(port);
	if (busy)
		port->ops->start_tx(port);
	uart_port_unlock_irqrestore(port, flags);

	if (busy)
		pm_runtime_mark_last_busy(dev);

	return busy ? -EBUSY : 0;
}

static void serial_base_port_set_tx(struct uart_port *port,
				    struct serial_port_device *port_dev,
				    bool enabled)
{
	unsigned long flags;

	uart_port_lock_irqsave(port, &flags);
	port_dev->tx_enabled = enabled;
	uart_port_unlock_irqrestore(port, flags);
}

void serial_base_port_startup(struct uart_port *port)
{
	struct serial_port_device *port_dev = port->port_dev;

	serial_base_port_set_tx(port, port_dev, true);
}

void serial_base_port_shutdown(struct uart_port *port)
{
	struct serial_port_device *port_dev = port->port_dev;

	serial_base_port_set_tx(port, port_dev, false);
}

static DEFINE_RUNTIME_DEV_PM_OPS(serial_port_pm,
				 serial_port_runtime_suspend,
				 serial_port_runtime_resume, NULL);

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

/**
 * __uart_read_properties - read firmware properties of the given UART port
 * @port: corresponding port
 * @use_defaults: apply defaults (when %true) or validate the values (when %false)
 *
 * The following device properties are supported:
 *   - clock-frequency (optional)
 *   - fifo-size (optional)
 *   - no-loopback-test (optional)
 *   - reg-shift (defaults may apply)
 *   - reg-offset (value may be validated)
 *   - reg-io-width (defaults may apply or value may be validated)
 *   - interrupts (OF only)
 *   - serial [alias ID] (OF only)
 *
 * If the port->dev is of struct platform_device type the interrupt line
 * will be retrieved via platform_get_irq() call against that device.
 * Otherwise it will be assigned by fwnode_irq_get() call. In both cases
 * the index 0 of the resource is used.
 *
 * The caller is responsible to initialize the following fields of the @port
 *   ->dev (must be valid)
 *   ->flags
 *   ->iobase
 *   ->mapbase
 *   ->mapsize
 *   ->regshift (if @use_defaults is false)
 * before calling this function. Alternatively the above mentioned fields
 * may be zeroed, in such case the only ones, that have associated properties
 * found, will be set to the respective values.
 *
 * If no error happened, the ->irq, ->mapbase, ->mapsize will be altered.
 * The ->iotype is always altered.
 *
 * When @use_defaults is true and the respective property is not found
 * the following values will be applied:
 *   ->regshift = 0
 * In this case IRQ must be provided, otherwise an error will be returned.
 *
 * When @use_defaults is false and the respective property is found
 * the following values will be validated:
 *   - reg-io-width (->iotype)
 *   - reg-offset (->mapsize against ->mapbase)
 *
 * Returns: 0 on success or negative errno on failure
 */
static int __uart_read_properties(struct uart_port *port, bool use_defaults)
{
	struct device *dev = port->dev;
	u32 value;
	int ret;

	/* Read optional UART functional clock frequency */
	device_property_read_u32(dev, "clock-frequency", &port->uartclk);

	/* Read the registers alignment (default: 8-bit) */
	ret = device_property_read_u32(dev, "reg-shift", &value);
	if (ret)
		port->regshift = use_defaults ? 0 : port->regshift;
	else
		port->regshift = value;

	/* Read the registers I/O access type (default: MMIO 8-bit) */
	ret = device_property_read_u32(dev, "reg-io-width", &value);
	if (ret) {
		port->iotype = port->iobase ? UPIO_PORT : UPIO_MEM;
	} else {
		switch (value) {
		case 1:
			port->iotype = UPIO_MEM;
			break;
		case 2:
			port->iotype = UPIO_MEM16;
			break;
		case 4:
			port->iotype = device_is_big_endian(dev) ? UPIO_MEM32BE : UPIO_MEM32;
			break;
		default:
			port->iotype = UPIO_UNKNOWN;
			if (!use_defaults) {
				dev_err(dev, "Unsupported reg-io-width (%u)\n", value);
				return -EINVAL;
			}
			break;
		}
	}

	/* Read the address mapping base offset (default: no offset) */
	ret = device_property_read_u32(dev, "reg-offset", &value);
	if (ret)
		value = 0;

	/* Check for shifted address mapping overflow */
	if (!use_defaults && port->mapsize < value) {
		dev_err(dev, "reg-offset %u exceeds region size %pa\n", value, &port->mapsize);
		return -EINVAL;
	}

	port->mapbase += value;
	port->mapsize -= value;

	/* Read optional FIFO size */
	device_property_read_u32(dev, "fifo-size", &port->fifosize);

	if (device_property_read_bool(dev, "no-loopback-test"))
		port->flags |= UPF_SKIP_TEST;

	/* Get index of serial line, if found in DT aliases */
	ret = of_alias_get_id(dev_of_node(dev), "serial");
	if (ret >= 0)
		port->line = ret;

	if (dev_is_platform(dev))
		ret = platform_get_irq(to_platform_device(dev), 0);
	else if (dev_is_pnp(dev)) {
		ret = pnp_irq(to_pnp_dev(dev), 0);
		if (ret < 0)
			ret = -ENXIO;
	} else
		ret = fwnode_irq_get(dev_fwnode(dev), 0);
	if (ret == -EPROBE_DEFER)
		return ret;
	if (ret > 0)
		port->irq = ret;
	else if (use_defaults)
		/* By default IRQ support is mandatory */
		return ret;
	else
		port->irq = 0;

	port->flags |= UPF_SHARE_IRQ;

	return 0;
}

int uart_read_port_properties(struct uart_port *port)
{
	return __uart_read_properties(port, true);
}
EXPORT_SYMBOL_GPL(uart_read_port_properties);

int uart_read_and_validate_port_properties(struct uart_port *port)
{
	return __uart_read_properties(port, false);
}
EXPORT_SYMBOL_GPL(uart_read_and_validate_port_properties);

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
