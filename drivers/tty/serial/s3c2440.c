/*
 * Driver for Samsung S3C2440 and S3C2442 SoC onboard UARTs.
 *
 * Ben Dooks, Copyright (c) 2003-2008 Simtec Electronics
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
#include <mach/regs-gpio.h>

#include "samsung.h"

static struct s3c24xx_uart_info s3c2440_uart_inf = {
	.name		= "Samsung S3C2440 UART",
	.type		= PORT_S3C2440,
	.fifosize	= 64,
	.rx_fifomask	= S3C2440_UFSTAT_RXMASK,
	.rx_fifoshift	= S3C2440_UFSTAT_RXSHIFT,
	.rx_fifofull	= S3C2440_UFSTAT_RXFULL,
	.tx_fifofull	= S3C2440_UFSTAT_TXFULL,
	.tx_fifomask	= S3C2440_UFSTAT_TXMASK,
	.tx_fifoshift	= S3C2440_UFSTAT_TXSHIFT,
	.def_clk_sel	= S3C2410_UCON_CLKSEL2,
	.num_clks	= 4,
	.clksel_mask	= S3C2440_UCON_CLKMASK,
	.clksel_shift	= S3C2440_UCON_CLKSHIFT,
};

/* device management */

static int s3c2440_serial_probe(struct platform_device *dev)
{
	dbg("s3c2440_serial_probe: dev=%p\n", dev);
	return s3c24xx_serial_probe(dev, &s3c2440_uart_inf);
}

static struct platform_driver s3c2440_serial_driver = {
	.probe		= s3c2440_serial_probe,
	.remove		= __devexit_p(s3c24xx_serial_remove),
	.driver		= {
		.name	= "s3c2440-uart",
		.owner	= THIS_MODULE,
	},
};

static int __init s3c2440_serial_init(void)
{
	return s3c24xx_serial_init(&s3c2440_serial_driver, &s3c2440_uart_inf);
}

static void __exit s3c2440_serial_exit(void)
{
	platform_driver_unregister(&s3c2440_serial_driver);
}

module_init(s3c2440_serial_init);
module_exit(s3c2440_serial_exit);

MODULE_DESCRIPTION("Samsung S3C2440,S3C2442 SoC Serial port driver");
MODULE_AUTHOR("Ben Dooks <ben@simtec.co.uk>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:s3c2440-uart");
