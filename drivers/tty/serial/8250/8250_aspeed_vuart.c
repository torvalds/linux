// SPDX-License-Identifier: GPL-2.0+
/*
 *  Serial Port driver for Aspeed VUART device
 *
 *    Copyright (C) 2016 Jeremy Kerr <jk@ozlabs.org>, IBM Corp.
 *    Copyright (C) 2006 Arnd Bergmann <arnd@arndb.de>, IBM Corp.
 */
#if defined(CONFIG_SERIAL_8250_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/device.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/clk.h>

#include "8250.h"

#define ASPEED_VUART_GCRA		0x20
#define ASPEED_VUART_GCRA_VUART_EN		BIT(0)
#define ASPEED_VUART_GCRA_DISABLE_HOST_TX_DISCARD BIT(5)
#define ASPEED_VUART_GCRB		0x24
#define ASPEED_VUART_GCRB_HOST_SIRQ_MASK	GENMASK(7, 4)
#define ASPEED_VUART_GCRB_HOST_SIRQ_SHIFT	4
#define ASPEED_VUART_ADDRL		0x28
#define ASPEED_VUART_ADDRH		0x2c

struct aspeed_vuart {
	struct device		*dev;
	void __iomem		*regs;
	struct clk		*clk;
	int			line;
	struct timer_list	unthrottle_timer;
	struct uart_8250_port	*port;
};

/*
 * If we fill the tty flip buffers, we throttle the data ready interrupt
 * to prevent dropped characters. This timeout defines how long we wait
 * to (conditionally, depending on buffer state) unthrottle.
 */
static const int unthrottle_timeout = HZ/10;

/*
 * The VUART is basically two UART 'front ends' connected by their FIFO
 * (no actual serial line in between). One is on the BMC side (management
 * controller) and one is on the host CPU side.
 *
 * It allows the BMC to provide to the host a "UART" that pipes into
 * the BMC itself and can then be turned by the BMC into a network console
 * of some sort for example.
 *
 * This driver is for the BMC side. The sysfs files allow the BMC
 * userspace which owns the system configuration policy, to specify
 * at what IO port and interrupt number the host side will appear
 * to the host on the Host <-> BMC LPC bus. It could be different on a
 * different system (though most of them use 3f8/4).
 */

static ssize_t lpc_address_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct aspeed_vuart *vuart = dev_get_drvdata(dev);
	u16 addr;

	addr = (readb(vuart->regs + ASPEED_VUART_ADDRH) << 8) |
		(readb(vuart->regs + ASPEED_VUART_ADDRL));

	return snprintf(buf, PAGE_SIZE - 1, "0x%x\n", addr);
}

static ssize_t lpc_address_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct aspeed_vuart *vuart = dev_get_drvdata(dev);
	unsigned long val;
	int err;

	err = kstrtoul(buf, 0, &val);
	if (err)
		return err;

	writeb(val >> 8, vuart->regs + ASPEED_VUART_ADDRH);
	writeb(val >> 0, vuart->regs + ASPEED_VUART_ADDRL);

	return count;
}

static DEVICE_ATTR_RW(lpc_address);

static ssize_t sirq_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct aspeed_vuart *vuart = dev_get_drvdata(dev);
	u8 reg;

	reg = readb(vuart->regs + ASPEED_VUART_GCRB);
	reg &= ASPEED_VUART_GCRB_HOST_SIRQ_MASK;
	reg >>= ASPEED_VUART_GCRB_HOST_SIRQ_SHIFT;

	return snprintf(buf, PAGE_SIZE - 1, "%u\n", reg);
}

static ssize_t sirq_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct aspeed_vuart *vuart = dev_get_drvdata(dev);
	unsigned long val;
	int err;
	u8 reg;

	err = kstrtoul(buf, 0, &val);
	if (err)
		return err;

	val <<= ASPEED_VUART_GCRB_HOST_SIRQ_SHIFT;
	val &= ASPEED_VUART_GCRB_HOST_SIRQ_MASK;

	reg = readb(vuart->regs + ASPEED_VUART_GCRB);
	reg &= ~ASPEED_VUART_GCRB_HOST_SIRQ_MASK;
	reg |= val;
	writeb(reg, vuart->regs + ASPEED_VUART_GCRB);

	return count;
}

static DEVICE_ATTR_RW(sirq);

static struct attribute *aspeed_vuart_attrs[] = {
	&dev_attr_sirq.attr,
	&dev_attr_lpc_address.attr,
	NULL,
};

static const struct attribute_group aspeed_vuart_attr_group = {
	.attrs = aspeed_vuart_attrs,
};

static void aspeed_vuart_set_enabled(struct aspeed_vuart *vuart, bool enabled)
{
	u8 reg = readb(vuart->regs + ASPEED_VUART_GCRA);

	if (enabled)
		reg |= ASPEED_VUART_GCRA_VUART_EN;
	else
		reg &= ~ASPEED_VUART_GCRA_VUART_EN;

	writeb(reg, vuart->regs + ASPEED_VUART_GCRA);
}

static void aspeed_vuart_set_host_tx_discard(struct aspeed_vuart *vuart,
					     bool discard)
{
	u8 reg;

	reg = readb(vuart->regs + ASPEED_VUART_GCRA);

	/* If the DISABLE_HOST_TX_DISCARD bit is set, discard is disabled */
	if (!discard)
		reg |= ASPEED_VUART_GCRA_DISABLE_HOST_TX_DISCARD;
	else
		reg &= ~ASPEED_VUART_GCRA_DISABLE_HOST_TX_DISCARD;

	writeb(reg, vuart->regs + ASPEED_VUART_GCRA);
}

static int aspeed_vuart_startup(struct uart_port *uart_port)
{
	struct uart_8250_port *uart_8250_port = up_to_u8250p(uart_port);
	struct aspeed_vuart *vuart = uart_8250_port->port.private_data;
	int rc;

	rc = serial8250_do_startup(uart_port);
	if (rc)
		return rc;

	aspeed_vuart_set_host_tx_discard(vuart, false);

	return 0;
}

static void aspeed_vuart_shutdown(struct uart_port *uart_port)
{
	struct uart_8250_port *uart_8250_port = up_to_u8250p(uart_port);
	struct aspeed_vuart *vuart = uart_8250_port->port.private_data;

	aspeed_vuart_set_host_tx_discard(vuart, true);

	serial8250_do_shutdown(uart_port);
}

static void __aspeed_vuart_set_throttle(struct uart_8250_port *up,
		bool throttle)
{
	unsigned char irqs = UART_IER_RLSI | UART_IER_RDI;

	up->ier &= ~irqs;
	if (!throttle)
		up->ier |= irqs;
	serial_out(up, UART_IER, up->ier);
}
static void aspeed_vuart_set_throttle(struct uart_port *port, bool throttle)
{
	struct uart_8250_port *up = up_to_u8250p(port);
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	__aspeed_vuart_set_throttle(up, throttle);
	spin_unlock_irqrestore(&port->lock, flags);
}

static void aspeed_vuart_throttle(struct uart_port *port)
{
	aspeed_vuart_set_throttle(port, true);
}

static void aspeed_vuart_unthrottle(struct uart_port *port)
{
	aspeed_vuart_set_throttle(port, false);
}

static void aspeed_vuart_unthrottle_exp(struct timer_list *timer)
{
	struct aspeed_vuart *vuart = from_timer(vuart, timer, unthrottle_timer);
	struct uart_8250_port *up = vuart->port;

	if (!tty_buffer_space_avail(&up->port.state->port)) {
		mod_timer(&vuart->unthrottle_timer,
			  jiffies + unthrottle_timeout);
		return;
	}

	aspeed_vuart_unthrottle(&up->port);
}

/*
 * Custom interrupt handler to manage finer-grained flow control. Although we
 * have throttle/unthrottle callbacks, we've seen that the VUART device can
 * deliver characters faster than the ldisc has a chance to check buffer space
 * against the throttle threshold. This results in dropped characters before
 * the throttle.
 *
 * We do this by checking for flip buffer space before RX. If we have no space,
 * throttle now and schedule an unthrottle for later, once the ldisc has had
 * a chance to drain the buffers.
 */
static int aspeed_vuart_handle_irq(struct uart_port *port)
{
	struct uart_8250_port *up = up_to_u8250p(port);
	unsigned int iir, lsr;
	unsigned long flags;
	int space, count;

	iir = serial_port_in(port, UART_IIR);

	if (iir & UART_IIR_NO_INT)
		return 0;

	spin_lock_irqsave(&port->lock, flags);

	lsr = serial_port_in(port, UART_LSR);

	if (lsr & (UART_LSR_DR | UART_LSR_BI)) {
		space = tty_buffer_space_avail(&port->state->port);

		if (!space) {
			/* throttle and schedule an unthrottle later */
			struct aspeed_vuart *vuart = port->private_data;
			__aspeed_vuart_set_throttle(up, true);

			if (!timer_pending(&vuart->unthrottle_timer)) {
				vuart->port = up;
				mod_timer(&vuart->unthrottle_timer,
					  jiffies + unthrottle_timeout);
			}

		} else {
			count = min(space, 256);

			do {
				serial8250_read_char(up, lsr);
				lsr = serial_in(up, UART_LSR);
				if (--count == 0)
					break;
			} while (lsr & (UART_LSR_DR | UART_LSR_BI));

			tty_flip_buffer_push(&port->state->port);
		}
	}

	serial8250_modem_status(up);
	if (lsr & UART_LSR_THRE)
		serial8250_tx_chars(up);

	uart_unlock_and_check_sysrq(port, flags);

	return 1;
}

static int aspeed_vuart_probe(struct platform_device *pdev)
{
	struct uart_8250_port port;
	struct aspeed_vuart *vuart;
	struct device_node *np;
	struct resource *res;
	u32 clk, prop;
	int rc;

	np = pdev->dev.of_node;

	vuart = devm_kzalloc(&pdev->dev, sizeof(*vuart), GFP_KERNEL);
	if (!vuart)
		return -ENOMEM;

	vuart->dev = &pdev->dev;
	timer_setup(&vuart->unthrottle_timer, aspeed_vuart_unthrottle_exp, 0);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	vuart->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(vuart->regs))
		return PTR_ERR(vuart->regs);

	memset(&port, 0, sizeof(port));
	port.port.private_data = vuart;
	port.port.membase = vuart->regs;
	port.port.mapbase = res->start;
	port.port.mapsize = resource_size(res);
	port.port.startup = aspeed_vuart_startup;
	port.port.shutdown = aspeed_vuart_shutdown;
	port.port.throttle = aspeed_vuart_throttle;
	port.port.unthrottle = aspeed_vuart_unthrottle;
	port.port.status = UPSTAT_SYNC_FIFO;
	port.port.dev = &pdev->dev;

	rc = sysfs_create_group(&vuart->dev->kobj, &aspeed_vuart_attr_group);
	if (rc < 0)
		return rc;

	if (of_property_read_u32(np, "clock-frequency", &clk)) {
		vuart->clk = devm_clk_get(&pdev->dev, NULL);
		if (IS_ERR(vuart->clk)) {
			dev_warn(&pdev->dev,
				"clk or clock-frequency not defined\n");
			rc = PTR_ERR(vuart->clk);
			goto err_sysfs_remove;
		}

		rc = clk_prepare_enable(vuart->clk);
		if (rc < 0)
			goto err_sysfs_remove;

		clk = clk_get_rate(vuart->clk);
	}

	/* If current-speed was set, then try not to change it. */
	if (of_property_read_u32(np, "current-speed", &prop) == 0)
		port.port.custom_divisor = clk / (16 * prop);

	/* Check for shifted address mapping */
	if (of_property_read_u32(np, "reg-offset", &prop) == 0)
		port.port.mapbase += prop;

	/* Check for registers offset within the devices address range */
	if (of_property_read_u32(np, "reg-shift", &prop) == 0)
		port.port.regshift = prop;

	/* Check for fifo size */
	if (of_property_read_u32(np, "fifo-size", &prop) == 0)
		port.port.fifosize = prop;

	/* Check for a fixed line number */
	rc = of_alias_get_id(np, "serial");
	if (rc >= 0)
		port.port.line = rc;

	port.port.irq = irq_of_parse_and_map(np, 0);
	port.port.irqflags = IRQF_SHARED;
	port.port.handle_irq = aspeed_vuart_handle_irq;
	port.port.iotype = UPIO_MEM;
	port.port.type = PORT_16550A;
	port.port.uartclk = clk;
	port.port.flags = UPF_SHARE_IRQ | UPF_BOOT_AUTOCONF
		| UPF_FIXED_PORT | UPF_FIXED_TYPE | UPF_NO_THRE_TEST;

	if (of_property_read_bool(np, "no-loopback-test"))
		port.port.flags |= UPF_SKIP_TEST;

	if (port.port.fifosize)
		port.capabilities = UART_CAP_FIFO;

	if (of_property_read_bool(np, "auto-flow-control"))
		port.capabilities |= UART_CAP_AFE;

	rc = serial8250_register_8250_port(&port);
	if (rc < 0)
		goto err_clk_disable;

	vuart->line = rc;

	aspeed_vuart_set_enabled(vuart, true);
	aspeed_vuart_set_host_tx_discard(vuart, true);
	platform_set_drvdata(pdev, vuart);

	return 0;

err_clk_disable:
	clk_disable_unprepare(vuart->clk);
	irq_dispose_mapping(port.port.irq);
err_sysfs_remove:
	sysfs_remove_group(&vuart->dev->kobj, &aspeed_vuart_attr_group);
	return rc;
}

static int aspeed_vuart_remove(struct platform_device *pdev)
{
	struct aspeed_vuart *vuart = platform_get_drvdata(pdev);

	del_timer_sync(&vuart->unthrottle_timer);
	aspeed_vuart_set_enabled(vuart, false);
	serial8250_unregister_port(vuart->line);
	sysfs_remove_group(&vuart->dev->kobj, &aspeed_vuart_attr_group);
	clk_disable_unprepare(vuart->clk);

	return 0;
}

static const struct of_device_id aspeed_vuart_table[] = {
	{ .compatible = "aspeed,ast2400-vuart" },
	{ .compatible = "aspeed,ast2500-vuart" },
	{ },
};

static struct platform_driver aspeed_vuart_driver = {
	.driver = {
		.name = "aspeed-vuart",
		.of_match_table = aspeed_vuart_table,
	},
	.probe = aspeed_vuart_probe,
	.remove = aspeed_vuart_remove,
};

module_platform_driver(aspeed_vuart_driver);

MODULE_AUTHOR("Jeremy Kerr <jk@ozlabs.org>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Driver for Aspeed VUART device");
