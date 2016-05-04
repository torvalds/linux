/*
 * platform/hisilicon/hi6220_fiq_debugger.c
 *
 * Serial Debugger Interface for Hi6220
 *
 * Copyright (C) 2016 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <stdarg.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/stacktrace.h>
#include <linux/uaccess.h>
#include <linux/amba/serial.h>

#include "../../staging/android/fiq_debugger/fiq_debugger.h"

struct hi6220_fiq_debugger {
	struct fiq_debugger_pdata pdata;
	void __iomem *debug_port_base;
};

static inline void uart_write(struct hi6220_fiq_debugger *t,
	unsigned int val, unsigned int off)
{
	__raw_writew(val, t->debug_port_base + off);
}

static inline unsigned int uart_read(struct hi6220_fiq_debugger *t,
	unsigned int off)
{
	return __raw_readw(t->debug_port_base + off);
}

static int debug_port_init(struct platform_device *pdev)
{
	struct hi6220_fiq_debugger *t =
		container_of(dev_get_platdata(&pdev->dev), typeof(*t), pdata);

	uart_write(t, UART011_OEIS | UART011_BEIS | UART011_PEIS |
		   UART011_FEIS | UART011_RTIS | UART011_RXIS, UART011_ICR);
	uart_write(t, UART011_RTIM | UART011_RXIM, UART011_IMSC);

	/* uart_write(t, 0x1b, UART011_FBRD); */

	return 0;
}

static int debug_getc(struct platform_device *pdev)
{
	int ch;
	struct hi6220_fiq_debugger *t =
		container_of(dev_get_platdata(&pdev->dev), typeof(*t), pdata);

	if (uart_read(t, UART01x_FR) & UART01x_FR_RXFE)
		return FIQ_DEBUGGER_NO_CHAR;
	ch = uart_read(t, UART01x_DR);
	if (ch & UART011_DR_BE)
		return FIQ_DEBUGGER_BREAK;
	return ch & 0x00FF;
}

static void debug_putc(struct platform_device *pdev, unsigned int c)
{
	struct hi6220_fiq_debugger *t =
		container_of(dev_get_platdata(&pdev->dev), typeof(*t), pdata);

	while (uart_read(t, UART01x_FR) & UART01x_FR_TXFF)
		cpu_relax();
	uart_write(t, c, UART01x_DR);
	while (uart_read(t, UART01x_FR) & UART01x_FR_BUSY)
		cpu_relax();
}

static void debug_flush(struct platform_device *pdev)
{
	struct hi6220_fiq_debugger *t =
		container_of(dev_get_platdata(&pdev->dev), typeof(*t), pdata);

	while (uart_read(t, UART01x_FR) & UART01x_FR_BUSY)
		cpu_relax();
}

static int debug_suspend(struct platform_device *pdev)
{
	return 0;
}

static int debug_resume(struct platform_device *pdev)
{
	return debug_port_init(pdev);
}

static int hi6220_fiq_debugger_id;

static int __hi6220_serial_debug_init(unsigned int base, int fiq, int irq,
			   struct clk *clk, int signal_irq, int wakeup_irq)
{
	struct hi6220_fiq_debugger *t;
	struct platform_device *pdev;
	struct resource *res;
	int res_count = 1;
	int ret = -ENOMEM;

	t = kzalloc(sizeof(struct hi6220_fiq_debugger), GFP_KERNEL);
	if (!t) {
		pr_err("Failed to allocate for fiq debugger\n");
		return ret;
	}

	t->pdata.uart_init = debug_port_init;
	t->pdata.uart_getc = debug_getc;
	t->pdata.uart_putc = debug_putc;
	t->pdata.uart_flush = debug_flush;
	t->pdata.uart_dev_suspend = debug_suspend;
	t->pdata.uart_dev_resume = debug_resume;

	t->debug_port_base = ioremap(base, PAGE_SIZE);
	if (!t->debug_port_base) {
		pr_err("Failed to ioremap for fiq debugger\n");
		goto out1;
	}

	res = kzalloc(sizeof(struct resource) * 3, GFP_KERNEL);
	if (!res) {
		pr_err("Failed to alloc fiq debugger resources\n");
		goto out2;
	}

	pdev = kzalloc(sizeof(struct platform_device), GFP_KERNEL);
	if (!pdev) {
		pr_err("Failed to alloc fiq debugger platform device\n");
		goto out3;
	};

	if (fiq >= 0) {
		res[0].flags = IORESOURCE_IRQ;
		res[0].start = fiq;
		res[0].end = fiq;
		res[0].name = "fiq";
	} else {
		res[0].flags = IORESOURCE_IRQ;
		res[0].start = irq;
		res[0].end = irq;
		res[0].name = "uart_irq";
	}

	if (signal_irq >= 0) {
		res[1].flags = IORESOURCE_IRQ;
		res[1].start = signal_irq;
		res[1].end = signal_irq;
		res[1].name = "signal";
		res_count++;
	}

	if (wakeup_irq >= 0) {
		res[2].flags = IORESOURCE_IRQ;
		res[2].start = wakeup_irq;
		res[2].end = wakeup_irq;
		res[2].name = "wakeup";
		res_count++;
	}

	pdev->name = "fiq_debugger";
	pdev->id = hi6220_fiq_debugger_id++;
	pdev->dev.platform_data = &t->pdata;
	pdev->resource = res;
	pdev->num_resources = res_count;

	ret = platform_device_register(pdev);
	if (ret) {
		pr_err("Failed to register fiq debugger\n");
		goto out4;
	}

	return 0;

out4:
	kfree(pdev);
out3:
	kfree(res);
out2:
	iounmap(t->debug_port_base);
out1:
	kfree(t);
	return ret;
}

int hi6220_serial_debug_init_irq_mode(unsigned int base, int irq,
			struct clk *clk, int signal_irq, int wakeup_irq)
{
	return __hi6220_serial_debug_init(base, -1, irq, clk, signal_irq,
					  wakeup_irq);
}

static int hi6220_serial_debug_probe(struct platform_device *pdev)
{
	struct resource *res;
	int fiq;
	int signal_irq;
	int wakeup_irq;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "No mem resource\n");
		return -EINVAL;
	}

	fiq = platform_get_irq_byname(pdev, "fiq");
	if (fiq < 0) {
		dev_err(&pdev->dev, "No IRQ for fiq, error=%d\n", fiq);
		return fiq;
	}

	signal_irq = platform_get_irq_byname(pdev, "signal");
	if (signal_irq < 0) {
		dev_err(&pdev->dev, "No signal IRQ, error=%d\n", signal_irq);
		signal_irq = -1;
	}

	wakeup_irq = platform_get_irq_byname(pdev, "wakeup");
	if (wakeup_irq < 0) {
		dev_err(&pdev->dev, "No wakeup IRQ, error=%d\n", wakeup_irq);
		wakeup_irq = -1;
	}

	return hi6220_serial_debug_init_irq_mode(res->start, fiq, NULL,
						 signal_irq, wakeup_irq);
}

static const struct of_device_id hi6220_serial_debug_match[] = {
	{ .compatible = "android,irq-hi6220-uart", },
	{}
};

static struct platform_driver hi6220_serial_debug_driver = {
	.probe		= hi6220_serial_debug_probe,
	.driver		= {
		.name	= "hi6220-irq-debug",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(hi6220_serial_debug_match),
	},
};

module_platform_driver(hi6220_serial_debug_driver);
