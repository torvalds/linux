// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// NVIDIA Tegra UTC (UART Trace Controller) driver.

#include <linux/bits.h>
#include <linux/console.h>
#include <linux/container_of.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/iopoll.h>
#include <linux/kfifo.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>
#include <linux/platform_device.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/types.h>

#define TEGRA_UTC_ENABLE			0x000
#define TEGRA_UTC_ENABLE_CLIENT_ENABLE		BIT(0)

#define TEGRA_UTC_FIFO_THRESHOLD		0x008

#define TEGRA_UTC_COMMAND			0x00c
#define TEGRA_UTC_COMMAND_RESET			BIT(0)
#define TEGRA_UTC_COMMAND_FLUSH			BIT(1)

#define TEGRA_UTC_DATA				0x020

#define TEGRA_UTC_FIFO_STATUS			0x100
#define TEGRA_UTC_FIFO_EMPTY			BIT(0)
#define TEGRA_UTC_FIFO_FULL			BIT(1)
#define TEGRA_UTC_FIFO_REQ			BIT(2)
#define TEGRA_UTC_FIFO_OVERFLOW			BIT(3)
#define TEGRA_UTC_FIFO_TIMEOUT			BIT(4)

#define TEGRA_UTC_FIFO_OCCUPANCY		0x104

#define TEGRA_UTC_INTR_STATUS			0x108
#define TEGRA_UTC_INTR_SET			0x10c
#define TEGRA_UTC_INTR_MASK			0x110
#define TEGRA_UTC_INTR_CLEAR			0x114
#define TEGRA_UTC_INTR_EMPTY			BIT(0)
#define TEGRA_UTC_INTR_FULL			BIT(1)
#define TEGRA_UTC_INTR_REQ			BIT(2)
#define TEGRA_UTC_INTR_OVERFLOW			BIT(3)
#define TEGRA_UTC_INTR_TIMEOUT			BIT(4)

#define TEGRA_UTC_UART_NR			16

#define TEGRA_UTC_INTR_COMMON	(TEGRA_UTC_INTR_REQ | TEGRA_UTC_INTR_FULL | TEGRA_UTC_INTR_EMPTY)

struct tegra_utc_port {
#if IS_ENABLED(CONFIG_SERIAL_TEGRA_UTC_CONSOLE)
	struct console console;
#endif
	struct uart_port port;

	void __iomem *rx_base;
	void __iomem *tx_base;

	u32 tx_irqmask;
	u32 rx_irqmask;

	unsigned int fifosize;
	u32 tx_threshold;
	u32 rx_threshold;
};

static u32 tegra_utc_rx_readl(struct tegra_utc_port *tup, unsigned int offset)
{
	void __iomem *addr = tup->rx_base + offset;

	return readl_relaxed(addr);
}

static void tegra_utc_rx_writel(struct tegra_utc_port *tup, u32 val, unsigned int offset)
{
	void __iomem *addr = tup->rx_base + offset;

	writel_relaxed(val, addr);
}

static u32 tegra_utc_tx_readl(struct tegra_utc_port *tup, unsigned int offset)
{
	void __iomem *addr = tup->tx_base + offset;

	return readl_relaxed(addr);
}

static void tegra_utc_tx_writel(struct tegra_utc_port *tup, u32 val, unsigned int offset)
{
	void __iomem *addr = tup->tx_base + offset;

	writel_relaxed(val, addr);
}

static void tegra_utc_enable_tx_irq(struct tegra_utc_port *tup)
{
	tup->tx_irqmask = TEGRA_UTC_INTR_REQ;

	tegra_utc_tx_writel(tup, tup->tx_irqmask, TEGRA_UTC_INTR_MASK);
	tegra_utc_tx_writel(tup, tup->tx_irqmask, TEGRA_UTC_INTR_SET);
}

static void tegra_utc_disable_tx_irq(struct tegra_utc_port *tup)
{
	tup->tx_irqmask = 0x0;

	tegra_utc_tx_writel(tup, tup->tx_irqmask, TEGRA_UTC_INTR_MASK);
	tegra_utc_tx_writel(tup, tup->tx_irqmask, TEGRA_UTC_INTR_SET);
}

static void tegra_utc_stop_tx(struct uart_port *port)
{
	struct tegra_utc_port *tup = container_of(port, struct tegra_utc_port, port);

	tegra_utc_disable_tx_irq(tup);
}

static void tegra_utc_init_tx(struct tegra_utc_port *tup)
{
	/* Disable TX. */
	tegra_utc_tx_writel(tup, 0x0, TEGRA_UTC_ENABLE);

	/* Update the FIFO Threshold. */
	tegra_utc_tx_writel(tup, tup->tx_threshold, TEGRA_UTC_FIFO_THRESHOLD);

	/* Clear and mask all the interrupts. */
	tegra_utc_tx_writel(tup, TEGRA_UTC_INTR_COMMON, TEGRA_UTC_INTR_CLEAR);
	tegra_utc_disable_tx_irq(tup);

	/* Enable TX. */
	tegra_utc_tx_writel(tup, TEGRA_UTC_ENABLE_CLIENT_ENABLE, TEGRA_UTC_ENABLE);
}

static void tegra_utc_init_rx(struct tegra_utc_port *tup)
{
	tup->rx_irqmask = TEGRA_UTC_INTR_REQ | TEGRA_UTC_INTR_TIMEOUT;

	tegra_utc_rx_writel(tup, TEGRA_UTC_COMMAND_RESET, TEGRA_UTC_COMMAND);
	tegra_utc_rx_writel(tup, tup->rx_threshold, TEGRA_UTC_FIFO_THRESHOLD);

	/* Clear all the pending interrupts. */
	tegra_utc_rx_writel(tup, TEGRA_UTC_INTR_TIMEOUT | TEGRA_UTC_INTR_OVERFLOW |
			    TEGRA_UTC_INTR_COMMON, TEGRA_UTC_INTR_CLEAR);
	tegra_utc_rx_writel(tup, tup->rx_irqmask, TEGRA_UTC_INTR_MASK);
	tegra_utc_rx_writel(tup, tup->rx_irqmask, TEGRA_UTC_INTR_SET);

	/* Enable RX. */
	tegra_utc_rx_writel(tup, TEGRA_UTC_ENABLE_CLIENT_ENABLE, TEGRA_UTC_ENABLE);
}

static bool tegra_utc_tx_chars(struct tegra_utc_port *tup)
{
	struct uart_port *port = &tup->port;
	unsigned int pending;
	u8 c;

	pending = uart_port_tx(port, c,
		     !(tegra_utc_tx_readl(tup, TEGRA_UTC_FIFO_STATUS) & TEGRA_UTC_FIFO_FULL),
		     tegra_utc_tx_writel(tup, c, TEGRA_UTC_DATA));

	return pending;
}

static void tegra_utc_rx_chars(struct tegra_utc_port *tup)
{
	struct tty_port *port = &tup->port.state->port;
	unsigned int max_chars = 256;
	u32 status;
	int sysrq;
	u32 ch;

	while (max_chars--) {
		status = tegra_utc_rx_readl(tup, TEGRA_UTC_FIFO_STATUS);
		if (status & TEGRA_UTC_FIFO_EMPTY)
			break;

		ch = tegra_utc_rx_readl(tup, TEGRA_UTC_DATA);
		tup->port.icount.rx++;

		if (status & TEGRA_UTC_FIFO_OVERFLOW)
			tup->port.icount.overrun++;

		uart_port_unlock(&tup->port);
		sysrq = uart_handle_sysrq_char(&tup->port, ch);
		uart_port_lock(&tup->port);

		if (!sysrq)
			tty_insert_flip_char(port, ch, TTY_NORMAL);
	}

	tty_flip_buffer_push(port);
}

static irqreturn_t tegra_utc_isr(int irq, void *dev_id)
{
	struct tegra_utc_port *tup = dev_id;
	unsigned int handled = 0;
	u32 status;

	uart_port_lock(&tup->port);

	/* Process RX_REQ and RX_TIMEOUT interrupts. */
	do {
		status = tegra_utc_rx_readl(tup, TEGRA_UTC_INTR_STATUS) & tup->rx_irqmask;
		if (status) {
			tegra_utc_rx_writel(tup, tup->rx_irqmask, TEGRA_UTC_INTR_CLEAR);
			tegra_utc_rx_chars(tup);
			handled = 1;
		}
	} while (status);

	/* Process TX_REQ interrupt. */
	do {
		status = tegra_utc_tx_readl(tup, TEGRA_UTC_INTR_STATUS) & tup->tx_irqmask;
		if (status) {
			tegra_utc_tx_writel(tup, tup->tx_irqmask, TEGRA_UTC_INTR_CLEAR);
			tegra_utc_tx_chars(tup);
			handled = 1;
		}
	} while (status);

	uart_port_unlock(&tup->port);

	return IRQ_RETVAL(handled);
}

static unsigned int tegra_utc_tx_empty(struct uart_port *port)
{
	struct tegra_utc_port *tup = container_of(port, struct tegra_utc_port, port);

	return tegra_utc_tx_readl(tup, TEGRA_UTC_FIFO_OCCUPANCY) ? 0 : TIOCSER_TEMT;
}

static void tegra_utc_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
}

static unsigned int tegra_utc_get_mctrl(struct uart_port *port)
{
	return 0;
}

static void tegra_utc_start_tx(struct uart_port *port)
{
	struct tegra_utc_port *tup = container_of(port, struct tegra_utc_port, port);

	if (tegra_utc_tx_chars(tup))
		tegra_utc_enable_tx_irq(tup);
}

static void tegra_utc_stop_rx(struct uart_port *port)
{
	struct tegra_utc_port *tup = container_of(port, struct tegra_utc_port, port);

	tup->rx_irqmask = 0x0;
	tegra_utc_rx_writel(tup, tup->rx_irqmask, TEGRA_UTC_INTR_MASK);
	tegra_utc_rx_writel(tup, tup->rx_irqmask, TEGRA_UTC_INTR_SET);
}

static void tegra_utc_hw_init(struct tegra_utc_port *tup)
{
	tegra_utc_init_tx(tup);
	tegra_utc_init_rx(tup);
}

static int tegra_utc_startup(struct uart_port *port)
{
	struct tegra_utc_port *tup = container_of(port, struct tegra_utc_port, port);
	int ret;

	tegra_utc_hw_init(tup);

	/* Interrupt is dedicated to this UTC client. */
	ret = request_irq(port->irq, tegra_utc_isr, 0, dev_name(port->dev), tup);
	if (ret < 0)
		dev_err(port->dev, "failed to register interrupt handler\n");

	return ret;
}

static void tegra_utc_shutdown(struct uart_port *port)
{
	struct tegra_utc_port *tup = container_of(port, struct tegra_utc_port, port);

	tegra_utc_rx_writel(tup, 0x0, TEGRA_UTC_ENABLE);
	free_irq(port->irq, tup);
}

static void tegra_utc_set_termios(struct uart_port *port, struct ktermios *termios,
				  const struct ktermios *old)
{
	/* The Tegra UTC clients supports only 8-N-1 configuration without HW flow control */
	termios->c_cflag &= ~(CSIZE | CSTOPB | PARENB | PARODD);
	termios->c_cflag &= ~(CMSPAR | CRTSCTS);
	termios->c_cflag |= CS8 | CLOCAL;
}

#ifdef CONFIG_CONSOLE_POLL

static int tegra_utc_poll_init(struct uart_port *port)
{
	struct tegra_utc_port *tup = container_of(port, struct tegra_utc_port, port);

	tegra_utc_hw_init(tup);
	return 0;
}

static int tegra_utc_get_poll_char(struct uart_port *port)
{
	struct tegra_utc_port *tup = container_of(port, struct tegra_utc_port, port);

	if (tegra_utc_rx_readl(tup, TEGRA_UTC_FIFO_STATUS) & TEGRA_UTC_FIFO_EMPTY)
		return NO_POLL_CHAR;

	return tegra_utc_rx_readl(tup, TEGRA_UTC_DATA);
}

static void tegra_utc_put_poll_char(struct uart_port *port, unsigned char ch)
{
	struct tegra_utc_port *tup = container_of(port, struct tegra_utc_port, port);
	u32 val;

	read_poll_timeout_atomic(tegra_utc_tx_readl, val, !(val & TEGRA_UTC_FIFO_FULL),
				 0, USEC_PER_SEC, false, tup, TEGRA_UTC_FIFO_STATUS);

	tegra_utc_tx_writel(tup, ch, TEGRA_UTC_DATA);
}

#endif

static const struct uart_ops tegra_utc_uart_ops = {
	.tx_empty = tegra_utc_tx_empty,
	.set_mctrl = tegra_utc_set_mctrl,
	.get_mctrl = tegra_utc_get_mctrl,
	.stop_tx = tegra_utc_stop_tx,
	.start_tx = tegra_utc_start_tx,
	.stop_rx = tegra_utc_stop_rx,
	.startup = tegra_utc_startup,
	.shutdown = tegra_utc_shutdown,
	.set_termios = tegra_utc_set_termios,
#ifdef CONFIG_CONSOLE_POLL
	.poll_init = tegra_utc_poll_init,
	.poll_get_char = tegra_utc_get_poll_char,
	.poll_put_char = tegra_utc_put_poll_char,
#endif
};

#if IS_ENABLED(CONFIG_SERIAL_TEGRA_UTC_CONSOLE)
#define TEGRA_UTC_DEFAULT_FIFO_THRESHOLD	4
#define TEGRA_UTC_EARLYCON_MAX_BURST_SIZE	128

static void tegra_utc_putc(struct uart_port *port, unsigned char c)
{
	writel(c, port->membase + TEGRA_UTC_DATA);
}

static void tegra_utc_early_write(struct console *con, const char *s, unsigned int n)
{
	struct earlycon_device *dev = con->data;

	while (n) {
		u32 burst_size = TEGRA_UTC_EARLYCON_MAX_BURST_SIZE;

		burst_size -= readl(dev->port.membase + TEGRA_UTC_FIFO_OCCUPANCY);
		if (n < burst_size)
			burst_size = n;

		uart_console_write(&dev->port, s, burst_size, tegra_utc_putc);

		n -= burst_size;
		s += burst_size;
	}
}

static int __init tegra_utc_early_console_setup(struct earlycon_device *device, const char *opt)
{
	if (!device->port.membase)
		return -ENODEV;

	/* Configure TX */
	writel(TEGRA_UTC_COMMAND_FLUSH | TEGRA_UTC_COMMAND_RESET,
		device->port.membase + TEGRA_UTC_COMMAND);
	writel(TEGRA_UTC_DEFAULT_FIFO_THRESHOLD, device->port.membase + TEGRA_UTC_FIFO_THRESHOLD);

	/* Clear and mask all the interrupts. */
	writel(TEGRA_UTC_INTR_COMMON, device->port.membase + TEGRA_UTC_INTR_CLEAR);

	writel(0x0, device->port.membase + TEGRA_UTC_INTR_MASK);
	writel(0x0, device->port.membase + TEGRA_UTC_INTR_SET);

	/* Enable TX. */
	writel(TEGRA_UTC_ENABLE_CLIENT_ENABLE, device->port.membase + TEGRA_UTC_ENABLE);

	device->con->write = tegra_utc_early_write;

	return 0;
}
OF_EARLYCON_DECLARE(tegra_utc, "nvidia,tegra264-utc", tegra_utc_early_console_setup);

static void tegra_utc_console_putchar(struct uart_port *port, unsigned char ch)
{
	struct tegra_utc_port *tup = container_of(port, struct tegra_utc_port, port);

	tegra_utc_tx_writel(tup, ch, TEGRA_UTC_DATA);
}

static void tegra_utc_console_write_atomic(struct console *cons, struct nbcon_write_context *wctxt)
{
	struct tegra_utc_port *tup = container_of(cons, struct tegra_utc_port, console);
	unsigned int len;
	char *outbuf;

	if (!nbcon_enter_unsafe(wctxt))
		return;

	outbuf = wctxt->outbuf;
	len = wctxt->len;

	while (len) {
		u32 burst_size = tup->fifosize;

		burst_size -= tegra_utc_tx_readl(tup, TEGRA_UTC_FIFO_OCCUPANCY);
		if (len < burst_size)
			burst_size = len;

		uart_console_write(&tup->port, outbuf, burst_size, tegra_utc_console_putchar);

		outbuf += burst_size;
		len -= burst_size;
	};

	nbcon_exit_unsafe(wctxt);
}

static void tegra_utc_console_write_thread(struct console *cons, struct nbcon_write_context *wctxt)
{
	struct tegra_utc_port *tup = container_of(cons, struct tegra_utc_port, console);
	unsigned int len = READ_ONCE(wctxt->len);
	unsigned int i;
	u32 val;

	for (i = 0; i < len; i++) {
		if (!nbcon_enter_unsafe(wctxt))
			break;

		read_poll_timeout_atomic(tegra_utc_tx_readl, val, !(val & TEGRA_UTC_FIFO_FULL),
					 0, USEC_PER_SEC, false, tup, TEGRA_UTC_FIFO_STATUS);
		uart_console_write(&tup->port, wctxt->outbuf + i, 1, tegra_utc_console_putchar);

		if (!nbcon_exit_unsafe(wctxt))
			break;
	}
}

static void tegra_utc_console_device_lock(struct console *cons, unsigned long *flags)
{
	struct tegra_utc_port *tup = container_of(cons, struct tegra_utc_port, console);
	struct uart_port *port = &tup->port;

	__uart_port_lock_irqsave(port, flags);
}

static void tegra_utc_console_device_unlock(struct console *cons, unsigned long flags)
{
	struct tegra_utc_port *tup = container_of(cons, struct tegra_utc_port, console);
	struct uart_port *port = &tup->port;

	__uart_port_unlock_irqrestore(port, flags);
}

static int tegra_utc_console_setup(struct console *cons, char *options)
{
	struct tegra_utc_port *tup = container_of(cons, struct tegra_utc_port, console);

	tegra_utc_init_tx(tup);

	return 0;
}
#endif

static struct uart_driver tegra_utc_driver = {
	.driver_name	= "tegra-utc",
	.dev_name	= "ttyUTC",
	.nr		= TEGRA_UTC_UART_NR,
};

static int tegra_utc_setup_port(struct device *dev, struct tegra_utc_port *tup)
{
	tup->port.dev			= dev;
	tup->port.fifosize		= tup->fifosize;
	tup->port.flags			= UPF_BOOT_AUTOCONF;
	tup->port.iotype		= UPIO_MEM;
	tup->port.ops			= &tegra_utc_uart_ops;
	tup->port.type			= PORT_TEGRA_TCU;
	tup->port.private_data		= tup;

#if IS_ENABLED(CONFIG_SERIAL_TEGRA_UTC_CONSOLE)
	strscpy(tup->console.name, "ttyUTC", sizeof(tup->console.name));
	tup->console.write_atomic	= tegra_utc_console_write_atomic;
	tup->console.write_thread	= tegra_utc_console_write_thread;
	tup->console.device_lock	= tegra_utc_console_device_lock;
	tup->console.device_unlock	= tegra_utc_console_device_unlock;
	tup->console.device		= uart_console_device;
	tup->console.setup		= tegra_utc_console_setup;
	tup->console.flags		= CON_PRINTBUFFER | CON_NBCON;
	tup->console.data		= &tegra_utc_driver;
#endif

	return uart_read_port_properties(&tup->port);
}

static int tegra_utc_register_port(struct tegra_utc_port *tup)
{
	int ret;

	ret = uart_add_one_port(&tegra_utc_driver, &tup->port);
	if (ret)
		return ret;

#if IS_ENABLED(CONFIG_SERIAL_TEGRA_UTC_CONSOLE)
	register_console(&tup->console);
#endif

	return 0;
}

static int tegra_utc_probe(struct platform_device *pdev)
{
	const unsigned int *soc_fifosize;
	struct device *dev = &pdev->dev;
	struct tegra_utc_port *tup;
	int ret;

	tup = devm_kzalloc(dev, sizeof(*tup), GFP_KERNEL);
	if (!tup)
		return -ENOMEM;

	ret = device_property_read_u32(dev, "tx-threshold", &tup->tx_threshold);
	if (ret)
		return dev_err_probe(dev, ret, "missing %s property\n", "tx-threshold");

	ret = device_property_read_u32(dev, "rx-threshold", &tup->rx_threshold);
	if (ret)
		return dev_err_probe(dev, ret, "missing %s property\n", "rx-threshold");

	soc_fifosize = device_get_match_data(dev);
	tup->fifosize = *soc_fifosize;

	tup->tx_base = devm_platform_ioremap_resource_byname(pdev, "tx");
	if (IS_ERR(tup->tx_base))
		return PTR_ERR(tup->tx_base);

	tup->rx_base = devm_platform_ioremap_resource_byname(pdev, "rx");
	if (IS_ERR(tup->rx_base))
		return PTR_ERR(tup->rx_base);

	ret = tegra_utc_setup_port(dev, tup);
	if (ret)
		dev_err_probe(dev, ret, "failed to setup uart port\n");

	platform_set_drvdata(pdev, tup);

	return tegra_utc_register_port(tup);
}

static void tegra_utc_remove(struct platform_device *pdev)
{
	struct tegra_utc_port *tup = platform_get_drvdata(pdev);

#if IS_ENABLED(CONFIG_SERIAL_TEGRA_UTC_CONSOLE)
	unregister_console(&tup->console);
#endif
	uart_remove_one_port(&tegra_utc_driver, &tup->port);
}

static const unsigned int tegra264_utc_soc = 128;

static const struct of_device_id tegra_utc_of_match[] = {
	{ .compatible = "nvidia,tegra264-utc", .data = &tegra264_utc_soc },
	{}
};
MODULE_DEVICE_TABLE(of, tegra_utc_of_match);

static struct platform_driver tegra_utc_platform_driver = {
	.probe = tegra_utc_probe,
	.remove = tegra_utc_remove,
	.driver = {
		.name = "tegra-utc",
		.of_match_table = tegra_utc_of_match,
	},
};

static int __init tegra_utc_init(void)
{
	int ret;

	ret = uart_register_driver(&tegra_utc_driver);
	if (ret)
		return ret;

	ret = platform_driver_register(&tegra_utc_platform_driver);
	if (ret)
		uart_unregister_driver(&tegra_utc_driver);

	return ret;
}
module_init(tegra_utc_init);

static void __exit tegra_utc_exit(void)
{
	platform_driver_unregister(&tegra_utc_platform_driver);
	uart_unregister_driver(&tegra_utc_driver);
}
module_exit(tegra_utc_exit);

MODULE_AUTHOR("Kartik Rajput <kkartik@nvidia.com>");
MODULE_DESCRIPTION("Tegra UART Trace Controller");
MODULE_LICENSE("GPL");
