// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * TI DaVinci serial driver
 *
 * Copyright (C) 2006 Texas Instruments.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/serial_8250.h>
#include <linux/serial_reg.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/io.h>

#include <mach/serial.h>
#include <mach/cputype.h>

static inline void serial_write_reg(struct plat_serial8250_port *p, int offset,
				    int value)
{
	offset <<= p->regshift;

	WARN_ONCE(!p->membase, "unmapped write: uart[%d]\n", offset);

	__raw_writel(value, p->membase + offset);
}

static void __init davinci_serial_reset(struct plat_serial8250_port *p)
{
	unsigned int pwremu = 0;

	serial_write_reg(p, UART_IER, 0);  /* disable all interrupts */

	/* reset both transmitter and receiver: bits 14,13 = UTRST, URRST */
	serial_write_reg(p, UART_DAVINCI_PWREMU, pwremu);
	mdelay(10);

	pwremu |= (0x3 << 13);
	pwremu |= 0x1;
	serial_write_reg(p, UART_DAVINCI_PWREMU, pwremu);

	if (cpu_is_davinci_dm646x())
		serial_write_reg(p, UART_DM646X_SCR,
				 UART_DM646X_SCR_TX_WATERMARK);
}

int __init davinci_serial_init(struct platform_device *serial_dev)
{
	int i, ret = 0;
	struct device *dev;
	struct plat_serial8250_port *p;
	struct clk *clk;

	/*
	 * Make sure the serial ports are muxed on at this point.
	 * You have to mux them off in device drivers later on if not needed.
	 */
	for (i = 0; serial_dev[i].dev.platform_data != NULL; i++) {
		dev = &serial_dev[i].dev;
		p = dev->platform_data;

		ret = platform_device_register(&serial_dev[i]);
		if (ret)
			continue;

		clk = clk_get(dev, NULL);
		if (IS_ERR(clk)) {
			pr_err("%s:%d: failed to get UART%d clock\n",
			       __func__, __LINE__, i);
			continue;
		}

		clk_prepare_enable(clk);

		p->uartclk = clk_get_rate(clk);

		if (!p->membase && p->mapbase) {
			p->membase = ioremap(p->mapbase, SZ_4K);

			if (p->membase)
				p->flags &= ~UPF_IOREMAP;
			else
				pr_err("uart regs ioremap failed\n");
		}

		if (p->membase && p->type != PORT_AR7)
			davinci_serial_reset(p);
	}
	return ret;
}
