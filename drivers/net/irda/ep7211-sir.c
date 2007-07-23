/*
 * IR port driver for the Cirrus Logic EP7211 processor.
 *
 * Copyright 2001, Blue Mug Inc.  All rights reserved.
 * Copyright 2007, Samuel Ortiz <samuel@sortiz.org>
 */
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/tty.h>
#include <linux/init.h>
#include <linux/spinlock.h>

#include <net/irda/irda.h>
#include <net/irda/irda_device.h>

#include <asm/io.h>
#include <asm/hardware.h>

#include "sir-dev.h"

#define MIN_DELAY 25      /* 15 us, but wait a little more to be sure */
#define MAX_DELAY 10000   /* 1 ms */

static int ep7211_open(struct sir_dev *dev);
static int ep7211_close(struct sir_dev *dev);
static int ep7211_change_speed(struct sir_dev *dev, unsigned speed);
static int ep7211_reset(struct sir_dev *dev);

static struct dongle_driver ep7211 = {
	.owner		= THIS_MODULE,
	.driver_name	= "EP7211 IR driver",
	.type		= IRDA_EP7211_DONGLE,
	.open		= ep7211_open,
	.close		= ep7211_close,
	.reset		= ep7211_reset,
	.set_speed	= ep7211_change_speed,
};

static int __init ep7211_sir_init(void)
{
	return irda_register_dongle(&ep7211);
}

static void __exit ep7211_sir_cleanup(void)
{
	irda_unregister_dongle(&ep7211);
}

static int ep7211_open(struct sir_dev *dev)
{
	unsigned int syscon;

	/* Turn on the SIR encoder. */
	syscon = clps_readl(SYSCON1);
	syscon |= SYSCON1_SIREN;
	clps_writel(syscon, SYSCON1);

	return 0;
}

static int ep7211_close(struct sir_dev *dev)
{
	unsigned int syscon;

	/* Turn off the SIR encoder. */
	syscon = clps_readl(SYSCON1);
	syscon &= ~SYSCON1_SIREN;
	clps_writel(syscon, SYSCON1);

	return 0;
}

static int ep7211_change_speed(struct sir_dev *dev, unsigned speed)
{
	return 0;
}

static int ep7211_reset(struct sir_dev *dev)
{
	return 0;
}

MODULE_AUTHOR("Samuel Ortiz <samuel@sortiz.org>");
MODULE_DESCRIPTION("EP7211 IR dongle driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("irda-dongle-13"); /* IRDA_EP7211_DONGLE */

module_init(ep7211_sir_init);
module_exit(ep7211_sir_cleanup);
