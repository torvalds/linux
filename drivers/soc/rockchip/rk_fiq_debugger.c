/*
 * drivers/soc/rockchip/rk_fiq_debugger.c
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
#include <linux/cpu.h>
#include <linux/cpu_pm.h>
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
#include <linux/delay.h>
#include <linux/soc/rockchip/rk_fiq_debugger.h>

#ifdef CONFIG_FIQ_DEBUGGER_TRUST_ZONE
#include <linux/rockchip/rockchip_sip.h>
#endif

#define UART_USR	0x1f	/* In: UART Status Register */
#define UART_USR_RX_FIFO_FULL		0x10 /* Receive FIFO full */
#define UART_USR_RX_FIFO_NOT_EMPTY	0x08 /* Receive FIFO not empty */
#define UART_USR_TX_FIFO_EMPTY		0x04 /* Transmit FIFO empty */
#define UART_USR_TX_FIFO_NOT_FULL	0x02 /* Transmit FIFO not full */
#define UART_USR_BUSY			0x01 /* UART busy indicator */
#define UART_SRR			0x22 /* software reset register */

struct rk_fiq_debugger {
	int irq;
	int baudrate;
	struct fiq_debugger_pdata pdata;
	void __iomem *debug_port_base;
	bool break_seen;
#ifdef CONFIG_RK_CONSOLE_THREAD
	struct task_struct *console_task;
#endif
};

static int rk_fiq_debugger_id;
static int serial_hwirq;

#ifdef CONFIG_FIQ_DEBUGGER_TRUST_ZONE
static bool tf_fiq_sup;
#endif

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
	int dll = 0, dlm = 0;
	struct rk_fiq_debugger *t;

	t = container_of(dev_get_platdata(&pdev->dev), typeof(*t), pdata);

	if (rk_fiq_read(t, UART_LSR) & UART_LSR_DR)
		(void)rk_fiq_read(t, UART_RX);

	switch (t->baudrate) {
	case 1500000:
		dll = 0x1;
		break;
	case 115200:
	default:
		dll = 0xd;
		break;
	}
	/* reset uart */
	rk_fiq_write(t, 0x07, UART_SRR);
	udelay(10);
	/* set uart to loop back mode */
	rk_fiq_write(t, 0x10, UART_MCR);

	rk_fiq_write(t, 0x83, UART_LCR);
	/* set baud rate */
	rk_fiq_write(t, dll, UART_DLL);
	rk_fiq_write(t, dlm, UART_DLM);
	rk_fiq_write(t, 0x03, UART_LCR);

	/* enable rx and lsr interrupt */
	rk_fiq_write(t, UART_IER_RLSI | UART_IER_RDI, UART_IER);

	/*
	 * Interrupt on every character when received, but we can enable fifo for TX
	 * I found that if we enable the RX fifo, some problem may vanish such as when
	 * you continuously input characters in the command line the uart irq may be disable
	 * because of the uart irq is served when CPU is at IRQ exception, but it is
	 * found unregistered, so it is disable.
	 */
	rk_fiq_write(t, 0x01, UART_FCR);

	/* disbale loop back mode */
	rk_fiq_write(t, 0x0, UART_MCR);

	return 0;
}

static int debug_getc(struct platform_device *pdev)
{
	unsigned int lsr;
	struct rk_fiq_debugger *t;
	unsigned int temp;
	static unsigned int n;
	static char buf[32];

	t = container_of(dev_get_platdata(&pdev->dev), typeof(*t), pdata);

	lsr = rk_fiq_read_lsr(t);

	if (lsr & UART_LSR_BI || t->break_seen) {
		t->break_seen = false;
		return FIQ_DEBUGGER_NO_CHAR;
	}

	if (lsr & UART_LSR_DR) {
		temp = rk_fiq_read(t, UART_RX);
		buf[n & 0x1f] = temp;
		n++;
		if (temp == 'q' && n > 2) {
			if ((buf[(n - 2) & 0x1f] == 'i') &&
			    (buf[(n - 3) & 0x1f] == 'f'))
				return FIQ_DEBUGGER_BREAK;
			else
				return temp;
		} else {
			return temp;
		}
	}

	return FIQ_DEBUGGER_NO_CHAR;
}

static void debug_putc(struct platform_device *pdev, unsigned int c)
{
	struct rk_fiq_debugger *t;
	unsigned int count = 10000;

	t = container_of(dev_get_platdata(&pdev->dev), typeof(*t), pdata);

	while (!(rk_fiq_read(t, UART_USR) & UART_USR_TX_FIFO_NOT_FULL) && count--)
		udelay(10);
	/* If uart is always busy, maybe it is abnormal, reinit it */
	if ((count == 0) && (rk_fiq_read(t, UART_USR) & UART_USR_BUSY))
		debug_port_init(pdev);

	rk_fiq_write(t, c, UART_TX);
}

static void debug_flush(struct platform_device *pdev)
{
	struct rk_fiq_debugger *t;
	unsigned int count = 10000;
	t = container_of(dev_get_platdata(&pdev->dev), typeof(*t), pdata);

	while (!(rk_fiq_read_lsr(t) & UART_LSR_TEMT) && count--)
		udelay(10);
	/* If uart is always busy, maybe it is abnormal, reinit it */
	if ((count == 0) && (rk_fiq_read(t, UART_USR) & UART_USR_BUSY))
		debug_port_init(pdev);
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
				kfifo_put(&fifo, r);
			}
			kfifo_put(&fifo, *s++);
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

#ifdef CONFIG_FIQ_DEBUGGER_TRUST_ZONE
static struct pt_regs fiq_pt_regs;

static void rk_fiq_debugger_switch_cpu(struct platform_device *pdev,
				       unsigned int cpu)
{
	sip_fiq_debugger_switch_cpu(cpu);
}

static void rk_fiq_debugger_enable_debug(struct platform_device *pdev, bool val)
{
	sip_fiq_debugger_enable_debug(val);
}

static void fiq_debugger_uart_irq_tf(struct pt_regs _pt_regs, u64 cpu)
{
	fiq_pt_regs = _pt_regs;

	fiq_debugger_fiq(&fiq_pt_regs, cpu);
}

static int rk_fiq_debugger_uart_dev_resume(struct platform_device *pdev)
{
	struct rk_fiq_debugger *t;

	t = container_of(dev_get_platdata(&pdev->dev), typeof(*t), pdata);
	sip_fiq_debugger_uart_irq_tf_init(serial_hwirq,
					  fiq_debugger_uart_irq_tf);
	return 0;
}

static int fiq_debugger_cpu_resume_fiq(struct notifier_block *nb,
				       unsigned long action, void *hcpu)
{
	switch (action) {
	case CPU_PM_EXIT:
		if ((sip_fiq_debugger_is_enabled()) &&
		    (sip_fiq_debugger_get_target_cpu() == smp_processor_id()))
			sip_fiq_debugger_enable_fiq(true, smp_processor_id());
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static int fiq_debugger_cpu_migrate_fiq(struct notifier_block *nb,
					unsigned long action, void *hcpu)
{
	int target_cpu, cpu = (long)hcpu;

	switch (action) {
	case CPU_DEAD:
		if ((sip_fiq_debugger_is_enabled()) &&
		    (sip_fiq_debugger_get_target_cpu() == cpu)) {
			target_cpu = cpumask_first(cpu_online_mask);
			sip_fiq_debugger_switch_cpu(target_cpu);
		}
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block fiq_debugger_pm_notifier = {
	.notifier_call = fiq_debugger_cpu_resume_fiq,
	.priority = 100,
};

static struct notifier_block fiq_debugger_cpu_notifier = {
	.notifier_call = fiq_debugger_cpu_migrate_fiq,
	.priority = 100,
};

static int rk_fiq_debugger_register_cpu_pm_notify(void)
{
	int err;

	err = register_cpu_notifier(&fiq_debugger_cpu_notifier);
	if (err) {
		pr_err("fiq debugger register cpu notifier failed!\n");
		return err;
	}

	err = cpu_pm_register_notifier(&fiq_debugger_pm_notifier);
	if (err) {
		pr_err("fiq debugger register pm notifier failed!\n");
		return err;
	}

	return 0;
}

static int fiq_debugger_bind_sip_smc(struct rk_fiq_debugger *t,
				     phys_addr_t phy_base,
				     int hwirq,
				     int signal_irq,
				     unsigned int baudrate)
{
	int err;

	err = sip_fiq_debugger_request_share_memory();
	if (err) {
		pr_err("fiq debugger request share memory failed: %d\n", err);
		goto exit;
	}

	err = rk_fiq_debugger_register_cpu_pm_notify();
	if (err) {
		pr_err("fiq debugger register cpu pm notify failed: %d\n", err);
		goto exit;
	}

	err = sip_fiq_debugger_uart_irq_tf_init(hwirq,
				fiq_debugger_uart_irq_tf);
	if (err) {
		pr_err("fiq debugger bind fiq to trustzone failed: %d\n", err);
		goto exit;
	}

	t->pdata.uart_dev_resume = rk_fiq_debugger_uart_dev_resume;
	t->pdata.switch_cpu = rk_fiq_debugger_switch_cpu;
	t->pdata.enable_debug = rk_fiq_debugger_enable_debug;
	sip_fiq_debugger_set_print_port(phy_base, baudrate);

	pr_info("fiq debugger fiq mode enabled\n");

	return 0;

exit:
	t->pdata.switch_cpu = NULL;
	t->pdata.enable_debug = NULL;

	return err;
}
#endif

void rk_serial_debug_init(void __iomem *base, phys_addr_t phy_base,
			  int irq, int signal_irq,
			  int wakeup_irq, unsigned int baudrate)
{
	struct rk_fiq_debugger *t = NULL;
	struct platform_device *pdev = NULL;
	struct resource *res = NULL;
	int res_count = 0;
#ifdef CONFIG_FIQ_DEBUGGER_TRUST_ZONE
	int ret = 0;
#endif

	if (!base) {
		pr_err("Invalid fiq debugger uart base\n");
		return;
	}

	t = kzalloc(sizeof(struct rk_fiq_debugger), GFP_KERNEL);
	if (!t) {
		pr_err("Failed to allocate for fiq debugger\n");
		return;
	}

	t->irq = irq;
	t->baudrate = baudrate;
	t->pdata.uart_init = debug_port_init;
	t->pdata.uart_getc = debug_getc;
	t->pdata.uart_putc = debug_putc;
#ifndef CONFIG_RK_CONSOLE_THREAD
	t->pdata.uart_flush = debug_flush;
#endif
	t->pdata.fiq_enable = fiq_enable;
	t->pdata.force_irq = NULL;
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

#ifdef CONFIG_FIQ_DEBUGGER_TRUST_ZONE
	if ((signal_irq > 0) && (serial_hwirq > 0)) {
		ret = fiq_debugger_bind_sip_smc(t, phy_base, serial_hwirq,
						signal_irq, baudrate);
		if (ret)
			tf_fiq_sup = false;
		else
			tf_fiq_sup = true;
	}
#endif

	if (irq > 0) {
		res[0].flags = IORESOURCE_IRQ;
		res[0].start = irq;
		res[0].end = irq;
#if defined(CONFIG_FIQ_GLUE)
		if (signal_irq > 0)
			res[0].name = "fiq";
		else
			res[0].name = "uart_irq";
#elif defined(CONFIG_FIQ_DEBUGGER_TRUST_ZONE)
		if (tf_fiq_sup && (signal_irq > 0))
			res[0].name = "fiq";
		else
			res[0].name = "uart_irq";
#else
		res[0].name = "uart_irq";
#endif
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
	t->console_task = kthread_run(console_thread, pdev, "kconsole");
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
	unsigned int id, ok = 0;
	int irq, signal_irq = -1, wake_irq = -1;
	unsigned int baudrate = 0, irq_mode = 0;
	phys_addr_t phy_base = 0;
	int serial_id;
	struct clk *clk;
	struct clk *pclk;
	struct of_phandle_args oirq;
	struct resource res;

	np = of_find_matching_node(NULL, ids);

	if (!np) {
		pr_err("fiq-debugger is missing in device tree!\n");
		return -ENODEV;
	}

	if (!of_device_is_available(np)) {
		pr_err("fiq-debugger is disabled in device tree\n");
		return -ENODEV;
	}

	if (of_property_read_u32(np, "rockchip,serial-id", &serial_id))
		return -EINVAL;

	if (of_property_read_u32(np, "rockchip,irq-mode-enable", &irq_mode))
		irq_mode = -1;

	if (irq_mode == 1) {
		signal_irq = -1;
	} else {
		signal_irq = irq_of_parse_and_map(np, 0);
		if (!signal_irq)
			return -EINVAL;
	}

	if (of_property_read_u32(np, "rockchip,wake-irq", &wake_irq))
		wake_irq = -1;

	if (of_property_read_u32(np, "rockchip,baudrate", &baudrate))
		baudrate = -1;

	np = NULL;

	do {
		np = of_find_node_by_name(np, "serial");
		if (np) {
			id = of_alias_get_id(np, "serial");
			if (id == serial_id) {
				ok = 1;
				break;
			}
		}
	} while(np);

	if (!ok)
		return -EINVAL;

	/* parse serial hw irq */
	if (!of_irq_parse_one(np, 0, &oirq))
		serial_hwirq = oirq.args[1] + 32;

	/* parse serial phy base address */
	if (!of_address_to_resource(np, 0, &res))
		phy_base = res.start;

	pclk = of_clk_get_by_name(np, "apb_pclk");
	clk = of_clk_get_by_name(np, "baudclk");
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
		rk_serial_debug_init(base, phy_base,
				     irq, signal_irq, wake_irq, baudrate);
	return 0;
}
#ifdef CONFIG_FIQ_GLUE
postcore_initcall_sync(rk_fiq_debugger_init);
#else
arch_initcall_sync(rk_fiq_debugger_init);
#endif
