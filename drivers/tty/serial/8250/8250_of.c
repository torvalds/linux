// SPDX-License-Identifier: GPL-2.0+
/*
 *  Serial Port driver for Open Firmware platform devices
 *
 *    Copyright (C) 2006 Arnd Bergmann <arnd@arndb.de>, IBM Corp.
 */

#include <linux/bits.h>
#include <linux/console.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/serial_core.h>
#include <linux/serial_reg.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/notifier.h>

#include "8250.h"

struct of_serial_info {
	struct clk *clk;
	struct clk *bus_clk;
	struct reset_control *rst;
	int type;
	int line;
	struct notifier_block clk_notifier;
};

/* Nuvoton NPCM timeout register */
#define UART_NPCM_TOR          7
#define UART_NPCM_TOIE         BIT(7)  /* Timeout Interrupt Enable */

static int npcm_startup(struct uart_port *port)
{
	/*
	 * Nuvoton calls the scratch register 'UART_TOR' (timeout
	 * register). Enable it, and set TIOC (timeout interrupt
	 * comparator) to be 0x20 for correct operation.
	 */
	serial_port_out(port, UART_NPCM_TOR, UART_NPCM_TOIE | 0x20);

	return serial8250_do_startup(port);
}

/* Nuvoton NPCM UARTs have a custom divisor calculation */
static unsigned int npcm_get_divisor(struct uart_port *port, unsigned int baud,
				     unsigned int *frac)
{
	return DIV_ROUND_CLOSEST(port->uartclk, 16 * baud + 2) - 2;
}

static int npcm_setup(struct uart_port *port)
{
	port->get_divisor = npcm_get_divisor;
	port->startup = npcm_startup;
	return 0;
}

static inline struct of_serial_info *clk_nb_to_info(struct notifier_block *nb)
{
	return container_of(nb, struct of_serial_info, clk_notifier);
}

static int of_platform_serial_clk_notifier_cb(struct notifier_block *nb, unsigned long event,
					      void *data)
{
	struct of_serial_info *info = clk_nb_to_info(nb);
	struct uart_8250_port *port8250 = serial8250_get_port(info->line);
	struct clk_notifier_data *ndata = data;

	if (event == POST_RATE_CHANGE) {
		serial8250_update_uartclk(&port8250->port, ndata->new_rate);
		return NOTIFY_OK;
	}

	return NOTIFY_DONE;
}

/*
 * Fill a struct uart_port for a given device node
 */
static int of_platform_serial_setup(struct platform_device *ofdev,
			int type, struct uart_8250_port *up,
			struct of_serial_info *info)
{
	struct resource resource;
	struct device *dev = &ofdev->dev;
	struct device_node *np = dev->of_node;
	struct uart_port *port = &up->port;
	u32 spd;
	int ret;

	memset(port, 0, sizeof *port);

	pm_runtime_enable(&ofdev->dev);
	pm_runtime_get_sync(&ofdev->dev);

	ret = of_address_to_resource(np, 0, &resource);
	if (ret) {
		dev_err_probe(dev, ret, "invalid address\n");
		goto err_pmruntime;
	}

	port->dev = &ofdev->dev;
	port->flags = UPF_BOOT_AUTOCONF | UPF_FIXED_PORT | UPF_FIXED_TYPE;
	spin_lock_init(&port->lock);

	if (resource_type(&resource) == IORESOURCE_IO) {
		port->iobase = resource.start;
	} else {
		port->mapbase = resource.start;
		port->mapsize = resource_size(&resource);
		port->flags |= UPF_IOREMAP;
	}

	ret = uart_read_and_validate_port_properties(port);
	if (ret)
		goto err_pmruntime;

	/* Get clk rate through clk driver if present */
	if (!port->uartclk) {
		struct clk *bus_clk;

		bus_clk = devm_clk_get_optional_enabled(dev, "bus");
		if (IS_ERR(bus_clk)) {
			ret = dev_err_probe(dev, PTR_ERR(bus_clk), "failed to get bus clock\n");
			goto err_pmruntime;
		}

		/* If the bus clock is required, core clock must be named */
		info->clk = devm_clk_get_enabled(dev, bus_clk ? "core" : NULL);
		if (IS_ERR(info->clk)) {
			ret = dev_err_probe(dev, PTR_ERR(info->clk), "failed to get clock\n");
			goto err_pmruntime;
		}

		info->bus_clk = bus_clk;
		port->uartclk = clk_get_rate(info->clk);
	}
	/* If current-speed was set, then try not to change it. */
	if (of_property_read_u32(np, "current-speed", &spd) == 0)
		port->custom_divisor = port->uartclk / (16 * spd);

	/* Compatibility with the deprecated pxa driver and 8250_pxa drivers. */
	if (of_device_is_compatible(np, "mrvl,mmp-uart"))
		port->regshift = 2;

	info->rst = devm_reset_control_get_optional_shared(&ofdev->dev, NULL);
	if (IS_ERR(info->rst)) {
		ret = PTR_ERR(info->rst);
		goto err_pmruntime;
	}

	ret = reset_control_deassert(info->rst);
	if (ret)
		goto err_pmruntime;

	port->type = type;
	port->rs485_config = serial8250_em485_config;
	port->rs485_supported = serial8250_em485_supported;
	up->rs485_start_tx = serial8250_em485_start_tx;
	up->rs485_stop_tx = serial8250_em485_stop_tx;

	switch (type) {
	case PORT_RT2880:
		ret = rt288x_setup(port);
		break;
	case PORT_NPCM:
		ret = npcm_setup(port);
		break;
	default:
		/* Nothing to do */
		ret = 0;
		break;
	}
	if (ret)
		goto err_pmruntime;

	if (IS_REACHABLE(CONFIG_SERIAL_8250_FSL) &&
	    (of_device_is_compatible(np, "fsl,ns16550") ||
	     of_device_is_compatible(np, "fsl,16550-FIFO64"))) {
		port->handle_irq = fsl8250_handle_irq;
		port->has_sysrq = IS_ENABLED(CONFIG_SERIAL_8250_CONSOLE);
	}

	return 0;
err_pmruntime:
	pm_runtime_put_sync(&ofdev->dev);
	pm_runtime_disable(&ofdev->dev);
	return ret;
}

/*
 * Try to register a serial port
 */
static int of_platform_serial_probe(struct platform_device *ofdev)
{
	struct of_serial_info *info;
	struct uart_8250_port port8250;
	unsigned int port_type;
	u32 tx_threshold;
	int ret;

	if (IS_ENABLED(CONFIG_SERIAL_8250_BCM7271) &&
	    of_device_is_compatible(ofdev->dev.of_node, "brcm,bcm7271-uart"))
		return -ENODEV;

	port_type = (unsigned long)of_device_get_match_data(&ofdev->dev);
	if (port_type == PORT_UNKNOWN)
		return -EINVAL;

	if (of_property_read_bool(ofdev->dev.of_node, "used-by-rtas"))
		return -EBUSY;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (info == NULL)
		return -ENOMEM;

	memset(&port8250, 0, sizeof(port8250));
	ret = of_platform_serial_setup(ofdev, port_type, &port8250, info);
	if (ret)
		goto err_free;

	if (port8250.port.fifosize)
		port8250.capabilities = UART_CAP_FIFO;

	/* Check for TX FIFO threshold & set tx_loadsz */
	if ((of_property_read_u32(ofdev->dev.of_node, "tx-threshold",
				  &tx_threshold) == 0) &&
	    (tx_threshold < port8250.port.fifosize))
		port8250.tx_loadsz = port8250.port.fifosize - tx_threshold;

	if (of_property_read_bool(ofdev->dev.of_node, "auto-flow-control"))
		port8250.capabilities |= UART_CAP_AFE;

	if (of_property_read_u32(ofdev->dev.of_node,
			"overrun-throttle-ms",
			&port8250.overrun_backoff_time_ms) != 0)
		port8250.overrun_backoff_time_ms = 0;

	ret = serial8250_register_8250_port(&port8250);
	if (ret < 0)
		goto err_dispose;

	info->type = port_type;
	info->line = ret;
	platform_set_drvdata(ofdev, info);

	if (info->clk) {
		info->clk_notifier.notifier_call = of_platform_serial_clk_notifier_cb;
		ret = clk_notifier_register(info->clk, &info->clk_notifier);
		if (ret) {
			dev_err_probe(port8250.port.dev, ret, "Failed to set the clock notifier\n");
			goto err_unregister;
		}
	}

	return 0;
err_unregister:
	serial8250_unregister_port(info->line);
err_dispose:
	pm_runtime_put_sync(&ofdev->dev);
	pm_runtime_disable(&ofdev->dev);
err_free:
	kfree(info);
	return ret;
}

/*
 * Release a line
 */
static void of_platform_serial_remove(struct platform_device *ofdev)
{
	struct of_serial_info *info = platform_get_drvdata(ofdev);

	if (info->clk)
		clk_notifier_unregister(info->clk, &info->clk_notifier);

	serial8250_unregister_port(info->line);

	reset_control_assert(info->rst);
	pm_runtime_put_sync(&ofdev->dev);
	pm_runtime_disable(&ofdev->dev);
	kfree(info);
}

#ifdef CONFIG_PM_SLEEP
static int of_serial_suspend(struct device *dev)
{
	struct of_serial_info *info = dev_get_drvdata(dev);
	struct uart_8250_port *port8250 = serial8250_get_port(info->line);
	struct uart_port *port = &port8250->port;

	serial8250_suspend_port(info->line);

	if (!uart_console(port) || console_suspend_enabled) {
		pm_runtime_put_sync(dev);
		clk_disable_unprepare(info->clk);
		clk_disable_unprepare(info->bus_clk);
	}
	return 0;
}

static int of_serial_resume(struct device *dev)
{
	struct of_serial_info *info = dev_get_drvdata(dev);
	struct uart_8250_port *port8250 = serial8250_get_port(info->line);
	struct uart_port *port = &port8250->port;

	if (!uart_console(port) || console_suspend_enabled) {
		pm_runtime_get_sync(dev);
		clk_prepare_enable(info->bus_clk);
		clk_prepare_enable(info->clk);
	}

	serial8250_resume_port(info->line);

	return 0;
}
#endif
static SIMPLE_DEV_PM_OPS(of_serial_pm_ops, of_serial_suspend, of_serial_resume);

/*
 * A few common types, add more as needed.
 */
static const struct of_device_id of_platform_serial_table[] = {
	{ .compatible = "ns8250",   .data = (void *)PORT_8250, },
	{ .compatible = "ns16450",  .data = (void *)PORT_16450, },
	{ .compatible = "ns16550a", .data = (void *)PORT_16550A, },
	{ .compatible = "ns16550",  .data = (void *)PORT_16550, },
	{ .compatible = "ns16750",  .data = (void *)PORT_16750, },
	{ .compatible = "ns16850",  .data = (void *)PORT_16850, },
	{ .compatible = "nxp,lpc3220-uart", .data = (void *)PORT_LPC3220, },
	{ .compatible = "ralink,rt2880-uart", .data = (void *)PORT_RT2880, },
	{ .compatible = "intel,xscale-uart", .data = (void *)PORT_XSCALE, },
	{ .compatible = "altr,16550-FIFO32",
		.data = (void *)PORT_ALTR_16550_F32, },
	{ .compatible = "altr,16550-FIFO64",
		.data = (void *)PORT_ALTR_16550_F64, },
	{ .compatible = "altr,16550-FIFO128",
		.data = (void *)PORT_ALTR_16550_F128, },
	{ .compatible = "fsl,16550-FIFO64",
		.data = (void *)PORT_16550A_FSL64, },
	{ .compatible = "mediatek,mtk-btif",
		.data = (void *)PORT_MTK_BTIF, },
	{ .compatible = "mrvl,mmp-uart",
		.data = (void *)PORT_XSCALE, },
	{ .compatible = "ti,da830-uart", .data = (void *)PORT_DA830, },
	{ .compatible = "nuvoton,wpcm450-uart", .data = (void *)PORT_NPCM, },
	{ .compatible = "nuvoton,npcm750-uart", .data = (void *)PORT_NPCM, },
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, of_platform_serial_table);

static struct platform_driver of_platform_serial_driver = {
	.driver = {
		.name = "of_serial",
		.of_match_table = of_platform_serial_table,
		.pm = &of_serial_pm_ops,
	},
	.probe = of_platform_serial_probe,
	.remove = of_platform_serial_remove,
};

module_platform_driver(of_platform_serial_driver);

MODULE_AUTHOR("Arnd Bergmann <arnd@arndb.de>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Serial Port driver for Open Firmware platform devices");
