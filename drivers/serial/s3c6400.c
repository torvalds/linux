/* linux/drivers/serial/s3c6400.c
 *
 * Driver for Samsung S3C6400 and S3C6410 SoC onboard UARTs.
 *
 * Copyright 2008 Openmoko,  Inc.
 * Copyright 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
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

#include <plat/regs-serial.h>

#include "samsung.h"

static int s3c6400_serial_setsource(struct uart_port *port,
				    struct s3c24xx_uart_clksrc *clk)
{
	unsigned long ucon = rd_regl(port, S3C2410_UCON);

	if (strcmp(clk->name, "uclk0") == 0) {
		ucon &= ~S3C6400_UCON_CLKMASK;
		ucon |= S3C6400_UCON_UCLK0;
	} else if (strcmp(clk->name, "uclk1") == 0)
		ucon |= S3C6400_UCON_UCLK1;
	else if (strcmp(clk->name, "pclk") == 0) {
		/* See notes about transitioning from UCLK to PCLK */
		ucon &= ~S3C6400_UCON_UCLK0;
	} else {
		printk(KERN_ERR "unknown clock source %s\n", clk->name);
		return -EINVAL;
	}

	wr_regl(port, S3C2410_UCON, ucon);
	return 0;
}


static int s3c6400_serial_getsource(struct uart_port *port,
				    struct s3c24xx_uart_clksrc *clk)
{
	u32 ucon = rd_regl(port, S3C2410_UCON);

	clk->divisor = 1;

	switch (ucon & S3C6400_UCON_CLKMASK) {
	case S3C6400_UCON_UCLK0:
		clk->name = "uclk0";
		break;

	case S3C6400_UCON_UCLK1:
		clk->name = "uclk1";
		break;

	case S3C6400_UCON_PCLK:
	case S3C6400_UCON_PCLK2:
		clk->name = "pclk";
		break;
	}

	return 0;
}

static int s3c6400_serial_resetport(struct uart_port *port,
				    struct s3c2410_uartcfg *cfg)
{
	unsigned long ucon = rd_regl(port, S3C2410_UCON);

	dbg("s3c6400_serial_resetport: port=%p (%08lx), cfg=%p\n",
	    port, port->mapbase, cfg);

	/* ensure we don't change the clock settings... */

	ucon &= S3C6400_UCON_CLKMASK;

	wr_regl(port, S3C2410_UCON,  ucon | cfg->ucon);
	wr_regl(port, S3C2410_ULCON, cfg->ulcon);

	/* reset both fifos */

	wr_regl(port, S3C2410_UFCON, cfg->ufcon | S3C2410_UFCON_RESETBOTH);
	wr_regl(port, S3C2410_UFCON, cfg->ufcon);

	return 0;
}

static struct s3c24xx_uart_info s3c6400_uart_inf = {
	.name		= "Samsung S3C6400 UART",
	.type		= PORT_S3C6400,
	.fifosize	= 64,
	.has_divslot	= 1,
	.rx_fifomask	= S3C2440_UFSTAT_RXMASK,
	.rx_fifoshift	= S3C2440_UFSTAT_RXSHIFT,
	.rx_fifofull	= S3C2440_UFSTAT_RXFULL,
	.tx_fifofull	= S3C2440_UFSTAT_TXFULL,
	.tx_fifomask	= S3C2440_UFSTAT_TXMASK,
	.tx_fifoshift	= S3C2440_UFSTAT_TXSHIFT,
	.get_clksrc	= s3c6400_serial_getsource,
	.set_clksrc	= s3c6400_serial_setsource,
	.reset_port	= s3c6400_serial_resetport,
};

/* device management */

static int s3c6400_serial_probe(struct platform_device *dev)
{
	dbg("s3c6400_serial_probe: dev=%p\n", dev);
	return s3c24xx_serial_probe(dev, &s3c6400_uart_inf);
}

static struct platform_driver s3c6400_serial_drv = {
	.probe		= s3c6400_serial_probe,
	.remove		= __devexit_p(s3c24xx_serial_remove),
	.driver		= {
		.name	= "s3c6400-uart",
		.owner	= THIS_MODULE,
	},
};

s3c24xx_console_init(&s3c6400_serial_drv, &s3c6400_uart_inf);

static int __init s3c6400_serial_init(void)
{
	return s3c24xx_serial_init(&s3c6400_serial_drv, &s3c6400_uart_inf);
}

static void __exit s3c6400_serial_exit(void)
{
	platform_driver_unregister(&s3c6400_serial_drv);
}

module_init(s3c6400_serial_init);
module_exit(s3c6400_serial_exit);

MODULE_DESCRIPTION("Samsung S3C6400,S3C6410 SoC Serial port driver");
MODULE_AUTHOR("Ben Dooks <ben@simtec.co.uk>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:s3c6400-uart");
