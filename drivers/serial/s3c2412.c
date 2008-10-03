/* linux/drivers/serial/s3c2412.c
 *
 * Driver for Samsung S3C2412 and S3C2413 SoC onboard UARTs.
 *
 * Ben Dooks, Copyright (c) 2003-2005,2008 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/serial_core.h>
#include <linux/serial.h>

#include <asm/irq.h>
#include <mach/hardware.h>

#include <asm/plat-s3c/regs-serial.h>
#include <mach/regs-gpio.h>

#include "samsung.h"

static int s3c2412_serial_setsource(struct uart_port *port,
				     struct s3c24xx_uart_clksrc *clk)
{
	unsigned long ucon = rd_regl(port, S3C2410_UCON);

	ucon &= ~S3C2412_UCON_CLKMASK;

	if (strcmp(clk->name, "uclk") == 0)
		ucon |= S3C2440_UCON_UCLK;
	else if (strcmp(clk->name, "pclk") == 0)
		ucon |= S3C2440_UCON_PCLK;
	else if (strcmp(clk->name, "usysclk") == 0)
		ucon |= S3C2412_UCON_USYSCLK;
	else {
		printk(KERN_ERR "unknown clock source %s\n", clk->name);
		return -EINVAL;
	}

	wr_regl(port, S3C2410_UCON, ucon);
	return 0;
}


static int s3c2412_serial_getsource(struct uart_port *port,
				    struct s3c24xx_uart_clksrc *clk)
{
	unsigned long ucon = rd_regl(port, S3C2410_UCON);

	switch (ucon & S3C2412_UCON_CLKMASK) {
	case S3C2412_UCON_UCLK:
		clk->divisor = 1;
		clk->name = "uclk";
		break;

	case S3C2412_UCON_PCLK:
	case S3C2412_UCON_PCLK2:
		clk->divisor = 1;
		clk->name = "pclk";
		break;

	case S3C2412_UCON_USYSCLK:
		clk->divisor = 1;
		clk->name = "usysclk";
		break;
	}

	return 0;
}

static int s3c2412_serial_resetport(struct uart_port *port,
				    struct s3c2410_uartcfg *cfg)
{
	unsigned long ucon = rd_regl(port, S3C2410_UCON);

	dbg("%s: port=%p (%08lx), cfg=%p\n",
	    __func__, port, port->mapbase, cfg);

	/* ensure we don't change the clock settings... */

	ucon &= S3C2412_UCON_CLKMASK;

	wr_regl(port, S3C2410_UCON,  ucon | cfg->ucon);
	wr_regl(port, S3C2410_ULCON, cfg->ulcon);

	/* reset both fifos */

	wr_regl(port, S3C2410_UFCON, cfg->ufcon | S3C2410_UFCON_RESETBOTH);
	wr_regl(port, S3C2410_UFCON, cfg->ufcon);

	return 0;
}

static struct s3c24xx_uart_info s3c2412_uart_inf = {
	.name		= "Samsung S3C2412 UART",
	.type		= PORT_S3C2412,
	.fifosize	= 64,
	.rx_fifomask	= S3C2440_UFSTAT_RXMASK,
	.rx_fifoshift	= S3C2440_UFSTAT_RXSHIFT,
	.rx_fifofull	= S3C2440_UFSTAT_RXFULL,
	.tx_fifofull	= S3C2440_UFSTAT_TXFULL,
	.tx_fifomask	= S3C2440_UFSTAT_TXMASK,
	.tx_fifoshift	= S3C2440_UFSTAT_TXSHIFT,
	.get_clksrc	= s3c2412_serial_getsource,
	.set_clksrc	= s3c2412_serial_setsource,
	.reset_port	= s3c2412_serial_resetport,
};

/* device management */

static int s3c2412_serial_probe(struct platform_device *dev)
{
	dbg("s3c2440_serial_probe: dev=%p\n", dev);
	return s3c24xx_serial_probe(dev, &s3c2412_uart_inf);
}

static struct platform_driver s3c2412_serial_drv = {
	.probe		= s3c2412_serial_probe,
	.remove		= s3c24xx_serial_remove,
	.driver		= {
		.name	= "s3c2412-uart",
		.owner	= THIS_MODULE,
	},
};

s3c24xx_console_init(&s3c2412_serial_drv, &s3c2412_uart_inf);

static inline int s3c2412_serial_init(void)
{
	return s3c24xx_serial_init(&s3c2412_serial_drv, &s3c2412_uart_inf);
}

static inline void s3c2412_serial_exit(void)
{
	platform_driver_unregister(&s3c2412_serial_drv);
}

module_init(s3c2412_serial_init);
module_exit(s3c2412_serial_exit);

MODULE_DESCRIPTION("Samsung S3C2412,S3C2413 SoC Serial port driver");
MODULE_AUTHOR("Ben Dooks <ben@simtec.co.uk>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:s3c2412-uart");
