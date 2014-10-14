/*
 * arch/arm/plat-rk/rk_fiq_debugger.c
 *
 * Serial Debugger Interface for Rockchip
 *
 * Copyright (C) 2012 ROCKCHIP, Inc.
 * Copyright (C) 2008 Google, Inc.
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
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/serial_reg.h>
#include <linux/slab.h>
#include <linux/stacktrace.h>
#include <linux/uaccess.h>
#include <linux/kfifo.h>
#include <linux/kthread.h>
#include <linux/sched/rt.h>
#include <../drivers/staging/android/fiq_debugger/fiq_debugger.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/clk.h>
#include "rk_fiq_debugger.h"

#define UART_USR	0x1f	/* In: UART Status Register */
#define UART_USR_RX_FIFO_FULL		0x10 /* Receive FIFO full */
#define UART_USR_RX_FIFO_NOT_EMPTY	0x08 /* Receive FIFO not empty */
#define UART_USR_TX_FIFO_EMPTY		0x04 /* Transmit FIFO empty */
#define UART_USR_TX_FIFO_NOT_FULL	0x02 /* Transmit FIFO not full */
#define UART_USR_BUSY			0x01 /* UART busy indicator */

struct rk_fiq_debugger {
	struct fiq_debugger_pdata pdata;
	void __iomem *debug_port_base;
	bool break_seen;
#ifdef CONFIG_RK_CONSOLE_THREAD
	struct task_struct *console_task;
#endif
};

static inline void rk_fiq_write(struct rk_fiq_debugger *t,
	unsigned int val, unsigned int off)
{
	__raw_writel(val, t->debug_port_base + off * 4);
}

static inline unsigned int rk_fiq_read(struct rk_fiq_debugger *t,
	unsigned int off)
{
	return __raw_readl(t->debug_port_base + off * 4);
}

static inline unsigned int rk_fiq_read_lsr(struct rk_fiq_debugger *t)
{
	unsigned int lsr;

	lsr = rk_fiq_read(t, UART_LSR);
	if (lsr & UART_LSR_BI)
		t->break_seen = true;

	return lsr;
}

static int debug_port_init(struct platform_device *pdev)
{
	struct rk_fiq_debugger *t;
	t = container_of(dev_get_platdata(&pdev->dev), typeof(*t), pdata);

	if (rk_fiq_read(t, UART_LSR) & UART_LSR_DR)
		(void)rk_fiq_read(t, UART_RX);
	/* enable rx and lsr interrupt */
	rk_fiq_write(t, UART_IER_RLSI | UART_IER_RDI, UART_IER);
	/* interrupt on every character when receive,but we can enable fifo for TX
	I found that if we enable the RX fifo, some problem may vanish such as when
	you continuously input characters in the command line the uart irq may be disable
	because of the uart irq is served when CPU is at IRQ exception,but it is
	found unregistered, so it is disable.
	hhb@rock-chips.com */
	rk_fiq_write(t, 0xc1, UART_FCR);

	return 0;
}

static int debug_getc(struct platform_device *pdev)
{
	unsigned int lsr;
	struct rk_fiq_debugger *t;
	t = container_of(dev_get_platdata(&pdev->dev), typeof(*t), pdata);

	lsr = rk_fiq_read_lsr(t);

	if (lsr & UART_LSR_BI || t->break_seen) {
		t->break_seen = false;
		return FIQ_DEBUGGER_BREAK;
	}

	if (lsr & UART_LSR_DR)
		return rk_fiq_read(t, UART_RX);

	return FIQ_DEBUGGER_NO_CHAR;
}

static void debug_putc(struct platform_device *pdev, unsigned int c)
{
	struct rk_fiq_debugger *t;
	t = container_of(dev_get_platdata(&pdev->dev), typeof(*t), pdata);

	while (!(rk_fiq_read(t, UART_USR) & UART_USR_TX_FIFO_NOT_FULL))
		cpu_relax();
	rk_fiq_write(t, c, UART_TX);
}

static void debug_flush(struct platform_device *pdev)
{
	struct rk_fiq_debugger *t;
	t = container_of(dev_get_platdata(&pdev->dev), typeof(*t), pdata);

	while (!(rk_fiq_read_lsr(t) & UART_LSR_TEMT))
		cpu_relax();
}

#ifdef CONFIG_RK_CONSOLE_THREAD
#define FIFO_SIZE SZ_64K
static DEFINE_KFIFO(fifo, unsigned char, FIFO_SIZE);
static bool console_thread_stop;

static int console_thread(void *data)
{
	struct platform_device *pdev = data;
	struct rk_fiq_debugger *t;
	unsigned char c;
	t = container_of(dev_get_platdata(&pdev->dev), typeof(*t), pdata);

	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
		if (kthread_should_stop())
			break;
		set_current_state(TASK_RUNNING);
		while (!console_thread_stop && kfifo_get(&fifo, &c))
			debug_putc(pdev, c);
		if (!console_thread_stop)
			debug_flush(pdev);
	}

	return 0;
}

static void console_write(struct platform_device *pdev, const char *s, unsigned int count)
{
	unsigned int fifo_count = FIFO_SIZE;
	unsigned char c, r = '\r';
	struct rk_fiq_debugger *t;
	t = container_of(dev_get_platdata(&pdev->dev), typeof(*t), pdata);

	if (console_thread_stop ||
	    oops_in_progress ||
	    system_state == SYSTEM_HALT ||
	    system_state == SYSTEM_POWER_OFF ||
	    system_state == SYSTEM_RESTART) {
		if (!console_thread_stop) {
			console_thread_stop = true;
			smp_wmb();
			debug_flush(pdev);
			while (fifo_count-- && kfifo_get(&fifo, &c))
				debug_putc(pdev, c);
		}
		while (count--) {
			if (*s == '\n') {
				debug_putc(pdev, r);
			}
			debug_putc(pdev, *s++);
		}
		debug_flush(pdev);
	} else {
		while (count--) {
			if (*s == '\n') {
				kfifo_put(&fifo, &r);
			}
			kfifo_put(&fifo, s++);
		}
		wake_up_process(t->console_task);
	}
}
#endif


static void fiq_enable(struct platform_device *pdev, unsigned int irq, bool on)
{
	if (on)
		enable_irq(irq);
	else
		disable_irq(irq);
}

static int rk_fiq_debugger_id;

void rk_serial_debug_init(void __iomem *base, int irq, int signal_irq, int wakeup_irq)
{
	struct rk_fiq_debugger *t = NULL;
	struct platform_device *pdev = NULL;
	struct resource *res = NULL;
	int res_count = 0;

	if (!base) {
		pr_err("Invalid fiq debugger uart base\n");
		return;
	}

	t = kzalloc(sizeof(struct rk_fiq_debugger), GFP_KERNEL);
	if (!t) {
		pr_err("Failed to allocate for fiq debugger\n");
		return;
	}

	t->pdata.uart_init = debug_port_init;
	t->pdata.uart_getc = debug_getc;
	t->pdata.uart_putc = debug_putc;
#ifndef CONFIG_RK_CONSOLE_THREAD
	t->pdata.uart_flush = debug_flush;
#endif
	t->pdata.fiq_enable = fiq_enable;
	t->pdata.force_irq = NULL;  //force_irq;
	t->debug_port_base = base;

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

	if (irq > 0) {
		res[0].flags = IORESOURCE_IRQ;
		res[0].start = irq;
		res[0].end = irq;
		res[0].name = "fiq";
		res_count++;
	}

	if (signal_irq > 0) {
		res[1].flags = IORESOURCE_IRQ;
		res[1].start = signal_irq;
		res[1].end = signal_irq;
		res[1].name = "signal";
		res_count++;
	}

	if (wakeup_irq > 0) {
		res[2].flags = IORESOURCE_IRQ;
		res[2].start = wakeup_irq;
		res[2].end = wakeup_irq;
		res[2].name = "wakeup";
		res_count++;
	}

#ifdef CONFIG_RK_CONSOLE_THREAD
	t->console_task = kthread_create(console_thread, pdev, "kconsole");
	if (!IS_ERR(t->console_task))
		t->pdata.console_write = console_write;
#endif

	pdev->name = "fiq_debugger";
	pdev->id = rk_fiq_debugger_id++;
	pdev->dev.platform_data = &t->pdata;
	pdev->resource = res;
	pdev->num_resources = res_count;
	if (platform_device_register(pdev)) {
		pr_err("Failed to register fiq debugger\n");
		goto out4;
	}
	return;

out4:
	kfree(pdev);
out3:
	kfree(res);
out2:
	kfree(t);
}

static const struct of_device_id ids[] __initconst = {
	{ .compatible = "rockchip,fiq-debugger" },
	{}
};

static int __init rk_fiq_debugger_init(void) {

	void __iomem *base;
	struct device_node *np;
	unsigned int i, id, serial_id, ok = 0;
	u32 irq, signal_irq = 0, wake_irq = 0;
	struct clk *clk;
	struct clk *pclk;

	np = of_find_matching_node(NULL, ids);

	if (!np) {
		pr_err("fiq-debugger is missing in device tree!\n");
		return -ENODEV;
	}

	if (!of_device_is_available(np)) {
		pr_err("fiq-debugger is disabled in device tree\n");
		return -ENODEV;
	}

	if (of_property_read_u32(np, "rockchip,serial-id", &serial_id)) {
		return -EINVAL;	
	}

	if (of_property_read_u32(np, "rockchip,signal-irq", &signal_irq)) {
		signal_irq = -1;
	}

	if (of_property_read_u32(np, "rockchip,wake-irq", &wake_irq)) {
		wake_irq = -1;
	}

	np = NULL;
	for (i = 0; i < 5; i++) {
		np = of_find_node_by_name(np, "serial");
		if (np) {
			id = of_alias_get_id(np, "serial");
			if (id == serial_id) {
				ok = 1;
				break;
			}
		}
	}
	if (!ok)
		return -EINVAL;

	pclk = of_clk_get_by_name(np, "pclk_uart");
	clk = of_clk_get_by_name(np, "sclk_uart");
	if (unlikely(IS_ERR(clk)) || unlikely(IS_ERR(pclk))) {
		pr_err("fiq-debugger get clock fail\n");
		return -EINVAL;
	}

	clk_prepare_enable(clk);
	clk_prepare_enable(pclk);

	irq = irq_of_parse_and_map(np, 0);
	if (!irq)
		return -EINVAL;

	base = of_iomap(np, 0);
	if (base)
		rk_serial_debug_init(base, irq, signal_irq, wake_irq);

	return 0;
}

postcore_initcall_sync(rk_fiq_debugger_init);
