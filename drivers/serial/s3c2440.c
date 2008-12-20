/* linux/drivers/serial/s3c2440.c
 *
 * Driver for Samsung S3C2440 and S3C2442 SoC onboard UARTs.
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

#include <plat/regs-serial.h>
#include <mach/regs-gpio.h>

#include "samsung.h"


static int s3c2440_serial_setsource(struct uart_port *port,
				     struct s3c24xx_uart_clksrc *clk)
{
	unsigned long ucon = rd_regl(port, S3C2410_UCON);

	/* todo - proper fclk<>nonfclk switch. */

	ucon &= ~S3C2440_UCON_CLKMASK;

	if (strcmp(clk->name, "uclk") == 0)
		ucon |= S3C2440_UCON_UCLK;
	else if (strcmp(clk->name, "pclk") == 0)
		ucon |= S3C2440_UCON_PCLK;
	else if (strcmp(clk->name, "fclk") == 0)
		ucon |= S3C2440_UCON_FCLK;
	else {
		printk(KERN_ERR "unknown clock source %s\n", clk->name);
		return -EINVAL;
	}

	wr_regl(port, S3C2410_UCON, ucon);
	return 0;
}


static int s3c2440_serial_getsource(struct uart_port *port,
				    struct s3c24xx_uart_clksrc *clk)
{
	unsigned long ucon = rd_regl(port, S3C2410_UCON);
	unsigned long ucon0, ucon1, ucon2;

	switch (ucon & S3C2440_UCON_CLKMASK) {
	case S3C2440_UCON_UCLK:
		clk->divisor = 1;
		clk->name = "uclk";
		break;

	case S3C2440_UCON_PCLK:
	case S3C2440_UCON_PCLK2:
		clk->divisor = 1;
		clk->name = "pclk";
		break;

	case S3C2440_UCON_FCLK:
		/* the fun of calculating the uart divisors on
		 * the s3c2440 */

		ucon0 = __raw_readl(S3C24XX_VA_UART0 + S3C2410_UCON);
		ucon1 = __raw_readl(S3C24XX_VA_UART1 + S3C2410_UCON);
		ucon2 = __raw_readl(S3C24XX_VA_UART2 + S3C2410_UCON);

		printk("ucons: %08lx, %08lx, %08lx\n", ucon0, ucon1, ucon2);

		ucon0 &= S3C2440_UCON0_DIVMASK;
		ucon1 &= S3C2440_UCON1_DIVMASK;
		ucon2 &= S3C2440_UCON2_DIVMASK;

		if (ucon0 != 0) {
			clk->divisor = ucon0 >> S3C2440_UCON_DIVSHIFT;
			clk->divisor += 6;
		} else if (ucon1 != 0) {
			clk->divisor = ucon1 >> S3C2440_UCON_DIVSHIFT;
			clk->divisor += 21;
		} else if (ucon2 != 0) {
			clk->divisor = ucon2 >> S3C2440_UCON_DIVSHIFT;
			clk->divisor += 36;
		} else {
			/* manual calims 44, seems to be 9 */
			clk->divisor = 9;
		}

		clk->name = "fclk";
		break;
	}

	return 0;
}

static int s3c2440_serial_resetport(struct uart_port *port,
				    struct s3c2410_uartcfg *cfg)
{
	unsigned long ucon = rd_regl(port, S3C2410_UCON);

	dbg("s3c2440_serial_resetport: port=%p (%08lx), cfg=%p\n",
	    port, port->mapbase, cfg);

	/* ensure we don't change the clock settings... */

	ucon &= (S3C2440_UCON0_DIVMASK | (3<<10));

	wr_regl(port, S3C2410_UCON,  ucon | cfg->ucon);
	wr_regl(port, S3C2410_ULCON, cfg->ulcon);

	/* reset both fifos */

	wr_regl(port, S3C2410_UFCON, cfg->ufcon | S3C2410_UFCON_RESETBOTH);
	wr_regl(port, S3C2410_UFCON, cfg->ufcon);

	return 0;
}

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
	.get_clksrc	= s3c2440_serial_getsource,
	.set_clksrc	= s3c2440_serial_setsource,
	.reset_port	= s3c2440_serial_resetport,
};

/* device management */

static int s3c2440_serial_probe(struct platform_device *dev)
{
	dbg("s3c2440_serial_probe: dev=%p\n", dev);
	return s3c24xx_serial_probe(dev, &s3c2440_uart_inf);
}

static struct platform_driver s3c2440_serial_drv = {
	.probe		= s3c2440_serial_probe,
	.remove		= s3c24xx_serial_remove,
	.driver		= {
		.name	= "s3c2440-uart",
		.owner	= THIS_MODULE,
	},
};

s3c24xx_console_init(&s3c2440_serial_drv, &s3c2440_uart_inf);

static int __init s3c2440_serial_init(void)
{
	return s3c24xx_serial_init(&s3c2440_serial_drv, &s3c2440_uart_inf);
}

static void __exit s3c2440_serial_exit(void)
{
	platform_driver_unregister(&s3c2440_serial_drv);
}

module_init(s3c2440_serial_init);
module_exit(s3c2440_serial_exit);

MODULE_DESCRIPTION("Samsung S3C2440,S3C2442 SoC Serial port driver");
MODULE_AUTHOR("Ben Dooks <ben@simtec.co.uk>");
MODULE_LICENSE("GPLi v2");
MODULE_ALIAS("platform:s3c2440-uart");
