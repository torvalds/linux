/*
 * linux/arch/arm/mach-omap2/gpmc-smc91x.c
 *
 * Copyright (C) 2009 Nokia Corporation
 * Contact:	Tony Lindgren
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/smc91x.h>

#include "gpmc.h"
#include "gpmc-smc91x.h"

#include "soc.h"

static struct omap_smc91x_platform_data *gpmc_cfg;

static struct resource gpmc_smc91x_resources[] = {
	[0] = {
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.flags		= IORESOURCE_IRQ,
	},
};

static struct smc91x_platdata gpmc_smc91x_info = {
	.flags	= SMC91X_USE_16BIT | SMC91X_NOWAIT | SMC91X_IO_SHIFT_0,
	.leda	= RPC_LED_100_10,
	.ledb	= RPC_LED_TX_RX,
};

static struct platform_device gpmc_smc91x_device = {
	.name		= "smc91x",
	.id		= -1,
	.dev		= {
		.platform_data = &gpmc_smc91x_info,
	},
	.num_resources	= ARRAY_SIZE(gpmc_smc91x_resources),
	.resource	= gpmc_smc91x_resources,
};

/*
 * Set the gpmc timings for smc91c96. The timings are taken
 * from the data sheet available at:
 * http://www.smsc.com/main/catalog/lan91c96.html
 * REVISIT: Level shifters can add at least to the access latency.
 */
static int smc91c96_gpmc_retime(void)
{
	struct gpmc_timings t;
	struct gpmc_device_timings dev_t;
	const int t3 = 10;	/* Figure 12.2 read and 12.4 write */
	const int t4_r = 20;	/* Figure 12.2 read */
	const int t4_w = 5;	/* Figure 12.4 write */
	const int t5 = 25;	/* Figure 12.2 read */
	const int t6 = 15;	/* Figure 12.2 read */
	const int t7 = 5;	/* Figure 12.4 write */
	const int t8 = 5;	/* Figure 12.4 write */
	const int t20 = 185;	/* Figure 12.2 read and 12.4 write */
	u32 l;

	l = GPMC_CONFIG1_DEVICESIZE_16;
	if (gpmc_cfg->flags & GPMC_MUX_ADD_DATA)
		l |= GPMC_CONFIG1_MUXADDDATA;
	if (gpmc_cfg->flags & GPMC_READ_MON)
		l |= GPMC_CONFIG1_WAIT_READ_MON;
	if (gpmc_cfg->flags & GPMC_WRITE_MON)
		l |= GPMC_CONFIG1_WAIT_WRITE_MON;
	if (gpmc_cfg->wait_pin)
		l |= GPMC_CONFIG1_WAIT_PIN_SEL(gpmc_cfg->wait_pin);
	gpmc_cs_write_reg(gpmc_cfg->cs, GPMC_CS_CONFIG1, l);

	/*
	 * FIXME: Calculate the address and data bus muxed timings.
	 * Note that at least adv_rd_off needs to be changed according
	 * to omap3430 TRM Figure 11-11. Are the sdp boards using the
	 * FPGA in between smc91x and omap as the timings are different
	 * from above?
	 */
	if (gpmc_cfg->flags & GPMC_MUX_ADD_DATA)
		return 0;

	memset(&dev_t, 0, sizeof(dev_t));

	dev_t.t_oeasu = t3 * 1000;
	dev_t.t_oe = t5 * 1000;
	dev_t.t_cez_r = t4_r * 1000;
	dev_t.t_oez = t6 * 1000;
	dev_t.t_rd_cycle = (t20 - t3) * 1000;

	dev_t.t_weasu = t3 * 1000;
	dev_t.t_wpl = t7 * 1000;
	dev_t.t_wph = t8 * 1000;
	dev_t.t_cez_w = t4_w * 1000;
	dev_t.t_wr_cycle = (t20 - t3) * 1000;

	gpmc_calc_timings(&t, &dev_t);

	return gpmc_cs_set_timings(gpmc_cfg->cs, &t);
}

/*
 * Initialize smc91x device connected to the GPMC. Note that we
 * assume that pin multiplexing is done in the board-*.c file,
 * or in the bootloader.
 */
void __init gpmc_smc91x_init(struct omap_smc91x_platform_data *board_data)
{
	unsigned long cs_mem_base;
	int ret;

	gpmc_cfg = board_data;

	if (gpmc_cfg->flags & GPMC_TIMINGS_SMC91C96)
		gpmc_cfg->retime = smc91c96_gpmc_retime;

	if (gpmc_cs_request(gpmc_cfg->cs, SZ_16M, &cs_mem_base) < 0) {
		printk(KERN_ERR "Failed to request GPMC mem for smc91x\n");
		return;
	}

	gpmc_smc91x_resources[0].start = cs_mem_base + 0x300;
	gpmc_smc91x_resources[0].end = cs_mem_base + 0x30f;
	gpmc_smc91x_resources[1].flags |= (gpmc_cfg->flags & IRQF_TRIGGER_MASK);

	if (gpmc_cfg->retime) {
		ret = gpmc_cfg->retime();
		if (ret != 0)
			goto free1;
	}

	if (gpio_request_one(gpmc_cfg->gpio_irq, GPIOF_IN, "SMC91X irq") < 0)
		goto free1;

	gpmc_smc91x_resources[1].start = gpio_to_irq(gpmc_cfg->gpio_irq);

	if (gpmc_cfg->gpio_pwrdwn) {
		ret = gpio_request_one(gpmc_cfg->gpio_pwrdwn,
				       GPIOF_OUT_INIT_LOW, "SMC91X powerdown");
		if (ret)
			goto free2;
	}

	if (gpmc_cfg->gpio_reset) {
		ret = gpio_request_one(gpmc_cfg->gpio_reset,
				       GPIOF_OUT_INIT_LOW, "SMC91X reset");
		if (ret)
			goto free3;

		gpio_set_value(gpmc_cfg->gpio_reset, 1);
		msleep(100);
		gpio_set_value(gpmc_cfg->gpio_reset, 0);
	}

	if (platform_device_register(&gpmc_smc91x_device) < 0) {
		printk(KERN_ERR "Unable to register smc91x device\n");
		gpio_free(gpmc_cfg->gpio_reset);
		goto free3;
	}

	return;

free3:
	if (gpmc_cfg->gpio_pwrdwn)
		gpio_free(gpmc_cfg->gpio_pwrdwn);
free2:
	gpio_free(gpmc_cfg->gpio_irq);
free1:
	gpmc_cs_free(gpmc_cfg->cs);

	printk(KERN_ERR "Could not initialize smc91x\n");
}
