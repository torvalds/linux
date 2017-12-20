/*
 * Mediatek Watchdog Driver
 *
 * Copyright (C) 2014 Matthias Brugger
 *
 * Matthias Brugger <matthias.bgg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Based on sunxi_wdt.c
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#ifdef CONFIG_FIQ_GLUE
#include <linux/irqchip/mtk-gic-extend.h>
#include <mt-plat/aee.h>
#endif
#include <linux/types.h>
#include <linux/watchdog.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/delay.h>
#include <linux/reset-controller.h>
#include <linux/reset.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/sched/signal.h>
#include <asm/system_misc.h>
#ifdef CONFIG_MT6397_MISC
#include <linux/mfd/mt6397/rtc_misc.h>
#endif

#define WDT_MAX_TIMEOUT		31
#define WDT_MIN_TIMEOUT		1
#define WDT_LENGTH_TIMEOUT(n)	((n) << 5)

#define WDT_LENGTH		0x04
#define WDT_LENGTH_KEY		0x8

#define WDT_RST			0x08
#define WDT_RST_RELOAD		0x1971

#define WDT_MODE		0x00
#define WDT_MODE_EN		(1 << 0)
#define WDT_MODE_EXT_POL_LOW	(0 << 1)
#define WDT_MODE_EXT_POL_HIGH	(1 << 1)
#define WDT_MODE_EXRST_EN	(1 << 2)
#define WDT_MODE_IRQ_EN		(1 << 3)
#define WDT_MODE_AUTO_START	(1 << 4)
#define WDT_MODE_IRQ_LVL	(1 << 5)
#define WDT_MODE_DUAL_EN	(1 << 6)
#define WDT_MODE_KEY		0x22000000

#define WDT_STATUS		0x0c
#define WDT_NONRST_REG		0x20
#define WDT_NONRST_REG2		0x24

#define WDT_SWRST		0x14
#define WDT_SWRST_KEY		0x1209

#define WDT_SWSYSRST		0x18
#define WDT_SWSYSRST_KEY	0x88000000

#define WDT_REQ_MODE 0x30
#define WDT_REQ_MODE_KEY 0x33000000
#define WDT_REQ_IRQ_EN 0x34
#define WDT_REQ_IRQ_KEY 0x44000000
#define WDT_REQ_MODE_DEBUG_EN 0x80000


#define DRV_NAME		"mtk-wdt"
#define DRV_VERSION		"2.0"

static bool nowayout = WATCHDOG_NOWAYOUT;
static unsigned int timeout = WDT_MAX_TIMEOUT;

struct toprgu_reset {
	spinlock_t lock;
	void __iomem *toprgu_swrst_base;
	int regofs;
	struct reset_controller_dev rcdev;
};

struct mtk_wdt_dev {
	struct watchdog_device wdt_dev;
	void __iomem *wdt_base;
	int wdt_irq_id;
	struct notifier_block restart_handler;
	struct toprgu_reset reset_controller;
};

static void __iomem *toprgu_base;
static struct watchdog_device *wdt_dev;

static int toprgu_reset_assert(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	unsigned int tmp;
	unsigned long flags;
	struct toprgu_reset *data = container_of(rcdev, struct toprgu_reset, rcdev);

	spin_lock_irqsave(&data->lock, flags);

	tmp = __raw_readl(data->toprgu_swrst_base + data->regofs);
	tmp |= BIT(id);
	tmp |= WDT_SWSYSRST_KEY;
	writel(tmp, data->toprgu_swrst_base + data->regofs);

	spin_unlock_irqrestore(&data->lock, flags);

	return 0;
}

static int toprgu_reset_deassert(struct reset_controller_dev *rcdev,
				unsigned long id)
{
	unsigned int tmp;
	unsigned long flags;
	struct toprgu_reset *data = container_of(rcdev, struct toprgu_reset, rcdev);

	spin_lock_irqsave(&data->lock, flags);

	tmp = __raw_readl(data->toprgu_swrst_base + data->regofs);
	tmp &= ~BIT(id);
	tmp |= WDT_SWSYSRST_KEY;
	writel(tmp, data->toprgu_swrst_base + data->regofs);

	spin_unlock_irqrestore(&data->lock, flags);

	return 0;
}

static int toprgu_reset(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	int ret;

	ret = toprgu_reset_assert(rcdev, id);
	if (ret)
		return ret;

	return toprgu_reset_deassert(rcdev, id);
}

static struct reset_control_ops toprgu_reset_ops = {
	.assert = toprgu_reset_assert,
	.deassert = toprgu_reset_deassert,
	.reset = toprgu_reset,
};

static void toprgu_register_reset_controller(struct platform_device *pdev, int regofs)
{
	int ret;
	struct mtk_wdt_dev *mtk_wdt = platform_get_drvdata(pdev);

	spin_lock_init(&mtk_wdt->reset_controller.lock);

	mtk_wdt->reset_controller.toprgu_swrst_base = mtk_wdt->wdt_base;
	mtk_wdt->reset_controller.regofs = regofs;
	mtk_wdt->reset_controller.rcdev.owner = THIS_MODULE;
	mtk_wdt->reset_controller.rcdev.nr_resets = 15;
	mtk_wdt->reset_controller.rcdev.ops = &toprgu_reset_ops;
	mtk_wdt->reset_controller.rcdev.of_node = pdev->dev.of_node;

	ret = reset_controller_register(&mtk_wdt->reset_controller.rcdev);
	if (ret)
		pr_err("could not register toprgu reset controller: %d\n", ret);
}

static int mtk_reset_handler(struct notifier_block *this, unsigned long mode,
				void *cmd)
{
	struct mtk_wdt_dev *mtk_wdt;
	void __iomem *wdt_base;
	u32 reg;

	mtk_wdt = container_of(this, struct mtk_wdt_dev, restart_handler);
	wdt_base = mtk_wdt->wdt_base;

	/* WDT_STATUS will be cleared to  zero after writing to WDT_MODE, so we backup it in WDT_NONRST_REG,
	  * and then print it out in mtk_wdt_probe() after reset
	  */
	writel(__raw_readl(wdt_base + WDT_STATUS), wdt_base + WDT_NONRST_REG);

	reg = ioread32(wdt_base + WDT_MODE);
	reg &= ~(WDT_MODE_DUAL_EN | WDT_MODE_IRQ_EN | WDT_MODE_EN);
	reg |= WDT_MODE_KEY;
	iowrite32(reg, wdt_base + WDT_MODE);

	if (cmd && !strcmp(cmd, "rpmbpk")) {
		iowrite32(ioread32(wdt_base + WDT_NONRST_REG2) | (1 << 0), wdt_base + WDT_NONRST_REG2);
	} else if (cmd && !strcmp(cmd, "recovery")) {
		iowrite32(ioread32(wdt_base + WDT_NONRST_REG2) | (1 << 1), wdt_base + WDT_NONRST_REG2);
		#ifdef CONFIG_MT6397_MISC
		mtk_misc_mark_recovery();
		#endif
	} else if (cmd && !strcmp(cmd, "bootloader")) {
		iowrite32(ioread32(wdt_base + WDT_NONRST_REG2) | (1 << 2), wdt_base + WDT_NONRST_REG2);
		#ifdef CONFIG_MT6397_MISC
		mtk_misc_mark_fast();
		#endif
	}

	if (!arm_pm_restart) {
		while (1) {
			writel(WDT_SWRST_KEY, wdt_base + WDT_SWRST);
			mdelay(5);
		}
	}
	return NOTIFY_DONE;
}

static int mtk_wdt_ping(struct watchdog_device *wdt_dev)
{
	struct mtk_wdt_dev *mtk_wdt = watchdog_get_drvdata(wdt_dev);
	void __iomem *wdt_base = mtk_wdt->wdt_base;

	iowrite32(WDT_RST_RELOAD, wdt_base + WDT_RST);
	printk_deferred("[WDK]: kick Ex WDT\n");

	return 0;
}

static int mtk_wdt_set_timeout(struct watchdog_device *wdt_dev,
				unsigned int timeout)
{
	struct mtk_wdt_dev *mtk_wdt = watchdog_get_drvdata(wdt_dev);
	void __iomem *wdt_base = mtk_wdt->wdt_base;
	u32 reg;

	wdt_dev->timeout = timeout;

	/*
	 * One bit is the value of 512 ticks
	 * The clock has 32 KHz
	 */
	reg = WDT_LENGTH_TIMEOUT(timeout << 6) | WDT_LENGTH_KEY;
	iowrite32(reg, wdt_base + WDT_LENGTH);

	mtk_wdt_ping(wdt_dev);

	return 0;
}

static int mtk_wdt_stop(struct watchdog_device *wdt_dev)
{
	struct mtk_wdt_dev *mtk_wdt = watchdog_get_drvdata(wdt_dev);
	void __iomem *wdt_base = mtk_wdt->wdt_base;
	u32 reg;

	reg = readl(wdt_base + WDT_MODE);
	reg &= ~WDT_MODE_EN;
	reg |= WDT_MODE_KEY;
	iowrite32(reg, wdt_base + WDT_MODE);

	return 0;
}

static int mtk_wdt_start(struct watchdog_device *wdt_dev)
{
	u32 reg;
	struct mtk_wdt_dev *mtk_wdt = watchdog_get_drvdata(wdt_dev);
	void __iomem *wdt_base = mtk_wdt->wdt_base;
	int ret;

	ret = mtk_wdt_set_timeout(wdt_dev, wdt_dev->timeout);
	if (ret < 0)
		return ret;

	reg = ioread32(wdt_base + WDT_MODE);
	reg |= (WDT_MODE_DUAL_EN | WDT_MODE_IRQ_EN | WDT_MODE_EXRST_EN);
	reg &= ~(WDT_MODE_IRQ_LVL | WDT_MODE_EXT_POL_HIGH);
	reg |= (WDT_MODE_EN | WDT_MODE_KEY);
	iowrite32(reg, wdt_base + WDT_MODE);

	return 0;
}

static const struct watchdog_info mtk_wdt_info = {
	.identity	= DRV_NAME,
	.options	= WDIOF_SETTIMEOUT |
			  WDIOF_KEEPALIVEPING |
			  WDIOF_MAGICCLOSE,
};

static const struct watchdog_ops mtk_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= mtk_wdt_start,
	.stop		= mtk_wdt_stop,
	.ping		= mtk_wdt_ping,
	.set_timeout	= mtk_wdt_set_timeout,
};

#ifdef CONFIG_FIQ_GLUE
static void wdt_fiq(void *arg, void *regs, void *svc_sp)
{
	unsigned int wdt_mode_val;
	void __iomem *wdt_base = ((struct mtk_wdt_dev *)arg)->wdt_base;

	wdt_mode_val = __raw_readl(wdt_base + WDT_STATUS);
	writel(wdt_mode_val, wdt_base + WDT_NONRST_REG);

	aee_wdt_fiq_info(arg, regs, svc_sp);
}
#else
static void wdt_report_info(void)
{
	struct task_struct *task;

	task = &init_task;
	pr_debug("Qwdt: -- watchdog time out\n");

	for_each_process(task) {
		if (task->state == 0) {
			pr_debug("PID: %d, name: %s\n backtrace:\n", task->pid, task->comm);
			show_stack(task, NULL);
			pr_debug("\n");
		}
	}

	pr_debug("backtrace of current task:\n");
	show_stack(NULL, NULL);
	pr_debug("Qwdt: -- watchdog time out\n");
}

static irqreturn_t mtk_wdt_isr(int irq, void *dev_id)
{
	pr_err("fwq mtk_wdt_isr\n");

	wdt_report_info();
	BUG();

	return IRQ_HANDLED;
}
#endif

static int mtk_wdt_probe(struct platform_device *pdev)
{
	struct mtk_wdt_dev *mtk_wdt;
	struct resource *res;
	unsigned int tmp;
	int err;

	mtk_wdt = devm_kzalloc(&pdev->dev, sizeof(*mtk_wdt), GFP_KERNEL);
	if (!mtk_wdt)
		return -ENOMEM;

	platform_set_drvdata(pdev, mtk_wdt);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mtk_wdt->wdt_base = devm_ioremap_resource(&pdev->dev, res);

	if (IS_ERR(mtk_wdt->wdt_base))
		return PTR_ERR(mtk_wdt->wdt_base);

	pr_err("MTK_WDT_NONRST_REG(%x)\n", __raw_readl(mtk_wdt->wdt_base + WDT_NONRST_REG));

	mtk_wdt->wdt_irq_id = irq_of_parse_and_map(pdev->dev.of_node, 0);
	if (!mtk_wdt->wdt_irq_id) {
		pr_err("RGU get IRQ ID failed\n");
		return -ENODEV;
	}

#ifndef CONFIG_FIQ_GLUE
	err = request_irq(mtk_wdt->wdt_irq_id, (irq_handler_t)mtk_wdt_isr, IRQF_TRIGGER_NONE, DRV_NAME, mtk_wdt);
#else
	mtk_wdt->wdt_irq_id = get_hardware_irq(mtk_wdt->wdt_irq_id);
	err = request_fiq(mtk_wdt->wdt_irq_id, wdt_fiq, IRQF_TRIGGER_FALLING, mtk_wdt);
#endif
	if (err != 0) {
		pr_err("mtk_wdt_probe : failed to request irq (%d)\n", err);
		return err;
	}

	toprgu_base = mtk_wdt->wdt_base;
	wdt_dev = &mtk_wdt->wdt_dev;

	mtk_wdt->wdt_dev.info = &mtk_wdt_info;
	mtk_wdt->wdt_dev.ops = &mtk_wdt_ops;
	mtk_wdt->wdt_dev.timeout = WDT_MAX_TIMEOUT;
	mtk_wdt->wdt_dev.max_timeout = WDT_MAX_TIMEOUT;
	mtk_wdt->wdt_dev.min_timeout = WDT_MIN_TIMEOUT;
	mtk_wdt->wdt_dev.parent = &pdev->dev;

	watchdog_init_timeout(&mtk_wdt->wdt_dev, timeout, &pdev->dev);
	watchdog_set_nowayout(&mtk_wdt->wdt_dev, nowayout);

	watchdog_set_drvdata(&mtk_wdt->wdt_dev, mtk_wdt);

	mtk_wdt_stop(&mtk_wdt->wdt_dev);

	err = watchdog_register_device(&mtk_wdt->wdt_dev);
	if (unlikely(err))
		return err;

	mtk_wdt->restart_handler.notifier_call = mtk_reset_handler;
	mtk_wdt->restart_handler.priority = 128;

	if (arm_pm_restart) {
		dev_info(&pdev->dev, "register restart_handler on reboot_notifier_list for psci reset\n");
		err = register_reboot_notifier(&mtk_wdt->restart_handler);
		if (err != 0)
			dev_warn(&pdev->dev,
				"cannot register reboot notifier (err=%d)\n", err);
	} else {
		err = register_restart_handler(&mtk_wdt->restart_handler);
		if (err)
			dev_warn(&pdev->dev,
				"cannot register restart handler (err=%d)\n", err);
	}

	dev_info(&pdev->dev, "Watchdog enabled (timeout=%d sec, nowayout=%d)\n",
			mtk_wdt->wdt_dev.timeout, nowayout);

	writel(WDT_REQ_MODE_KEY | (__raw_readl(mtk_wdt->wdt_base + WDT_REQ_MODE) &
		(~WDT_REQ_MODE_DEBUG_EN)), mtk_wdt->wdt_base + WDT_REQ_MODE);

	toprgu_register_reset_controller(pdev, WDT_SWSYSRST);

	/* enable scpsys thermal and thermal_controller request, and set to reset directly mode */
	tmp = ioread32(mtk_wdt->wdt_base + WDT_REQ_MODE) | (1 << 18) | (1 << 0);
	tmp |= WDT_REQ_MODE_KEY;
	iowrite32(tmp, mtk_wdt->wdt_base + WDT_REQ_MODE);

	tmp = ioread32(mtk_wdt->wdt_base + WDT_REQ_IRQ_EN);
	tmp &= ~((1 << 18) | (1 << 0));
	tmp |= WDT_REQ_IRQ_KEY;
	iowrite32(tmp, mtk_wdt->wdt_base + WDT_REQ_IRQ_EN);

	return 0;
}

static void mtk_wdt_shutdown(struct platform_device *pdev)
{
	struct mtk_wdt_dev *mtk_wdt = platform_get_drvdata(pdev);

	if (watchdog_active(&mtk_wdt->wdt_dev))
		mtk_wdt_stop(&mtk_wdt->wdt_dev);
}

static int mtk_wdt_remove(struct platform_device *pdev)
{
	struct mtk_wdt_dev *mtk_wdt = platform_get_drvdata(pdev);

	unregister_restart_handler(&mtk_wdt->restart_handler);

	watchdog_unregister_device(&mtk_wdt->wdt_dev);

	reset_controller_unregister(&mtk_wdt->reset_controller.rcdev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int mtk_wdt_suspend(struct device *dev)
{
	struct mtk_wdt_dev *mtk_wdt = dev_get_drvdata(dev);

	if (watchdog_active(&mtk_wdt->wdt_dev))
		mtk_wdt_stop(&mtk_wdt->wdt_dev);

	return 0;
}

static int mtk_wdt_resume(struct device *dev)
{
	struct mtk_wdt_dev *mtk_wdt = dev_get_drvdata(dev);

	if (watchdog_active(&mtk_wdt->wdt_dev)) {
		mtk_wdt_start(&mtk_wdt->wdt_dev);
		mtk_wdt_ping(&mtk_wdt->wdt_dev);
	}

	return 0;
}
#endif

static const struct of_device_id mtk_wdt_dt_ids[] = {
	{ .compatible = "mediatek,mt6589-wdt" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mtk_wdt_dt_ids);

static const struct dev_pm_ops mtk_wdt_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(mtk_wdt_suspend,
				mtk_wdt_resume)
};

static struct platform_driver mtk_wdt_driver = {
	.probe		= mtk_wdt_probe,
	.remove		= mtk_wdt_remove,
	.shutdown	= mtk_wdt_shutdown,
	.driver		= {
		.name		= DRV_NAME,
		.pm		= &mtk_wdt_pm_ops,
		.of_match_table	= mtk_wdt_dt_ids,
	},
};

module_platform_driver(mtk_wdt_driver);

static int wk_proc_cmd_read(struct seq_file *s, void *v)
{
	unsigned int enabled = 1;

	if (!(ioread32(toprgu_base + WDT_MODE) & WDT_MODE_EN))
		enabled = 0;

	seq_printf(s, "enabled timeout\n%-4d %-8d\n", enabled, wdt_dev->timeout);

	return 0;
}

static int wk_proc_cmd_open(struct inode *inode, struct file *file)
{
	return single_open(file, wk_proc_cmd_read, NULL);
}

static ssize_t wk_proc_cmd_write(struct file *file, const char *buf, size_t count, loff_t *data)
{
	int ret;
	int enable;
	int timeout;
	char wk_cmd_buf[256];

	if (count == 0)
		return -1;

	if (count > 255)
		count = 255;

	ret = copy_from_user(wk_cmd_buf, buf, count);
	if (ret < 0)
		return -1;

	wk_cmd_buf[count] = '\0';

	pr_debug("Write %s\n", wk_cmd_buf);

	ret = sscanf(wk_cmd_buf, "%d %d", &enable, &timeout);
	if (ret != 2)
		pr_debug("%s: expect 2 numbers\n", __func__);

	pr_debug("[WDK] enable=%d  timeout=%d\n", enable, timeout);

	if (timeout > 20 && timeout <= WDT_MAX_TIMEOUT) {
		wdt_dev->timeout = timeout;
		mtk_wdt_set_timeout(wdt_dev, wdt_dev->timeout);
	} else {
		pr_err("[WDK] The timeout(%d) should bigger than 20 and not bigger than %d\n",
				timeout, WDT_MAX_TIMEOUT);

	}

	if (enable == 1) {
		mtk_wdt_start(wdt_dev);
		set_bit(WDOG_ACTIVE, &wdt_dev->status);
		pr_err("[WDK] enable wdt\n");
	} else if (enable == 0) {
		mtk_wdt_stop(wdt_dev);
		clear_bit(WDOG_ACTIVE, &wdt_dev->status);
		pr_err("[WDK] disable wdt\n");
	}

	return count;
}

static const struct file_operations wk_proc_cmd_fops = {
	.owner = THIS_MODULE,
	.open = wk_proc_cmd_open,
	.read = seq_read,
	.write = wk_proc_cmd_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static int __init wk_proc_init(void)
{
	struct proc_dir_entry *de = proc_create("wdk", 0660, NULL, &wk_proc_cmd_fops);

	if (!de)
		pr_err("[wk_proc_init]: create /proc/wdk failed\n");

	pr_debug("[WDK] Initialize proc\n");

	return 0;
}

late_initcall(wk_proc_init);

module_param(timeout, uint, 0);
MODULE_PARM_DESC(timeout, "Watchdog heartbeat in seconds");

module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
			__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Matthias Brugger <matthias.bgg@gmail.com>");
MODULE_DESCRIPTION("Mediatek WatchDog Timer Driver");
MODULE_VERSION(DRV_VERSION);
