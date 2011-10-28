/*
 * arch/arm/include/asm/fiq_debugger.h
 *
 * Copyright (C) 2010 Google, Inc.
 * Author: Colin Cross <ccross@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _ARCH_ARM_MACH_TEGRA_FIQ_DEBUGGER_H_
#define _ARCH_ARM_MACH_TEGRA_FIQ_DEBUGGER_H_

#include <linux/serial_core.h>

#define FIQ_DEBUGGER_NO_CHAR NO_POLL_CHAR
#define FIQ_DEBUGGER_BREAK 0x00ff0100

#define FIQ_DEBUGGER_FIQ_IRQ_NAME	"fiq"
#define FIQ_DEBUGGER_SIGNAL_IRQ_NAME	"signal"
#define FIQ_DEBUGGER_WAKEUP_IRQ_NAME	"wakeup"

struct fiq_debugger_pdata {
	int (*uart_init)(struct platform_device *pdev);
	void (*uart_free)(struct platform_device *pdev);
	int (*uart_resume)(struct platform_device *pdev);
	int (*uart_getc)(struct platform_device *pdev);
	void (*uart_putc)(struct platform_device *pdev, unsigned int c);
	void (*uart_flush)(struct platform_device *pdev);

	void (*fiq_enable)(struct platform_device *pdev, unsigned int fiq,
								bool enable);
	void (*fiq_ack)(struct platform_device *pdev, unsigned int fiq);

	void (*force_irq)(struct platform_device *pdev, unsigned int irq);
	void (*force_irq_ack)(struct platform_device *pdev, unsigned int irq);
};

#endif
