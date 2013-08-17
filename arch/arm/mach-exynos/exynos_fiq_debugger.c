/*
 * Serial Debugger Interface for exynos
 *
 * Copyright (C) 2012 Google, Inc.
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

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/stacktrace.h>
#include <linux/uaccess.h>

#include <asm/fiq_debugger.h>

#include <plat/regs-serial.h>

#include <mach/exynos_fiq_debugger.h>
#include <mach/map.h>

struct exynos_fiq_debugger {
	struct fiq_debugger_pdata pdata;
	struct platform_device *pdev;
	void __iomem *debug_port_base;
	u32 baud;
	u32 frac_baud;
};

static inline struct exynos_fiq_debugger *get_dbg(struct platform_device *pdev)
{
	struct fiq_debugger_pdata *pdata = dev_get_platdata(&pdev->dev);
	return container_of(pdata, struct exynos_fiq_debugger, pdata);
}

static inline void exynos_write(struct exynos_fiq_debugger *dbg,
			       unsigned int val, unsigned int off)
{
	__raw_writel(val, dbg->debug_port_base + off);
}

static inline unsigned int exynos_read(struct exynos_fiq_debugger *dbg,
				      unsigned int off)
{
	return __raw_readl(dbg->debug_port_base + off);
}

static int debug_port_init(struct platform_device *pdev)
{
	struct exynos_fiq_debugger *dbg = get_dbg(pdev);
	unsigned long timeout;

	exynos_write(dbg, dbg->baud, S3C2410_UBRDIV);
	exynos_write(dbg, dbg->frac_baud, S3C2443_DIVSLOT);

	/* Mask and clear all interrupts */
	exynos_write(dbg, 0xF, S3C64XX_UINTM);
	exynos_write(dbg, 0xF, S3C64XX_UINTP);

	exynos_write(dbg, S3C2410_LCON_CS8, S3C2410_ULCON);
	exynos_write(dbg, S5PV210_UCON_DEFAULT, S3C2410_UCON);
	exynos_write(dbg, S5PV210_UFCON_DEFAULT, S3C2410_UFCON);
	exynos_write(dbg, 0, S3C2410_UMCON);

	/* Reset TX and RX fifos */
	exynos_write(dbg, S5PV210_UFCON_DEFAULT | S3C2410_UFCON_RESETBOTH,
		S3C2410_UFCON);

	timeout = jiffies + HZ;
	while (exynos_read(dbg, S3C2410_UFCON) & S3C2410_UFCON_RESETBOTH)
		if (time_after(jiffies, timeout))
			return -ETIMEDOUT;

	/* Enable all interrupts except TX */
	exynos_write(dbg, S3C64XX_UINTM_TXD_MSK, S3C64XX_UINTM);

	return 0;
}

static int debug_getc(struct platform_device *pdev)
{
	struct exynos_fiq_debugger *dbg = get_dbg(pdev);
	u32 stat;
	int ret = FIQ_DEBUGGER_NO_CHAR;

	/* Clear all pending interrupts */
	exynos_write(dbg, 0xF, S3C64XX_UINTP);

	stat = exynos_read(dbg, S3C2410_UERSTAT);
	if (stat & S3C2410_UERSTAT_BREAK)
		return FIQ_DEBUGGER_BREAK;

	stat = exynos_read(dbg, S3C2410_UTRSTAT);
	if (stat & S3C2410_UTRSTAT_RXDR)
		ret = exynos_read(dbg, S3C2410_URXH);

	return ret;
}

static void debug_putc(struct platform_device *pdev, unsigned int c)
{
	struct exynos_fiq_debugger *dbg = get_dbg(pdev);
	int count = loops_per_jiffy;

	if (exynos_read(dbg, S3C2410_ULCON) != S3C2410_LCON_CS8)
		debug_port_init(pdev);

	while (exynos_read(dbg, S3C2410_UFSTAT) & S5PV210_UFSTAT_TXFULL)
		if (--count == 0)
			return;

	exynos_write(dbg, c, S3C2410_UTXH);
}

static void debug_flush(struct platform_device *pdev)
{
	struct exynos_fiq_debugger *dbg = get_dbg(pdev);
	int count = loops_per_jiffy * HZ;

	while (!(exynos_read(dbg, S3C2410_UTRSTAT) & S3C2410_UTRSTAT_TXE))
		if (--count == 0)
			return;
}

static int debug_suspend(struct platform_device *pdev)
{
	struct exynos_fiq_debugger *dbg = get_dbg(pdev);

	exynos_write(dbg, 0xF, S3C64XX_UINTM);

	return 0;
}

static int debug_resume(struct platform_device *pdev)
{
	struct exynos_fiq_debugger *dbg = get_dbg(pdev);

	debug_port_init(pdev);

	return 0;
}

int __init exynos_serial_debug_init(int id, bool is_fiq)
{
	struct exynos_fiq_debugger *dbg = NULL;
	struct platform_device *pdev = NULL;
	struct resource *res = NULL;
	int ret = -ENOMEM;
	struct resource irq_res =
		DEFINE_RES_IRQ_NAMED(EXYNOS_IRQ_UARTx(id), "uart_irq");

	if (id >= CONFIG_SERIAL_SAMSUNG_UARTS)
		return -EINVAL;

	dbg = kzalloc(sizeof(struct exynos_fiq_debugger), GFP_KERNEL);
	if (!dbg) {
		pr_err("exynos_fiq_debugger: failed to allocate fiq debugger\n");
		goto err_free;
	}

	res = kmemdup(&irq_res, sizeof(struct resource), GFP_KERNEL);
	if (!res) {
		pr_err("exynos_fiq_debugger: failed to alloc resources\n");
		goto err_free;
	}

	pdev = kzalloc(sizeof(struct platform_device), GFP_KERNEL);
	if (!pdev) {
		pr_err("exynos_fiq_debugger: failed to alloc platform device\n");
		goto err_free;
	};

	dbg->debug_port_base = S3C_VA_UARTx(id);

	pdev->name = "fiq_debugger";
	pdev->id = id;
	pdev->dev.platform_data = &dbg->pdata;
	pdev->resource = res;
	pdev->num_resources = 1;

	dbg->pdata.uart_init = debug_port_init;
	dbg->pdata.uart_getc = debug_getc;
	dbg->pdata.uart_putc = debug_putc;
	dbg->pdata.uart_flush = debug_flush;
	dbg->pdata.uart_dev_suspend = debug_suspend;
	dbg->pdata.uart_dev_resume = debug_resume;

	dbg->pdev = pdev;

	dbg->baud = exynos_read(dbg, S3C2410_UBRDIV);
	dbg->frac_baud = exynos_read(dbg, S3C2443_DIVSLOT);

	if (platform_device_register(pdev)) {
		pr_err("exynos_fiq_debugger: failed to register fiq debugger\n");
		goto err_free;
	}

	return 0;

err_free:
	kfree(pdev);
	kfree(res);
	kfree(dbg);
	return ret;
}
