// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * tscan1.c: driver for Technologic Systems TS-CAN1 PC104 boards
 *
 * Copyright 2010 Andre B. Oliveira
 */

/* References:
 * - Getting started with TS-CAN1, Technologic Systems, Feb 2022
 *	https://docs.embeddedts.com/TS-CAN1
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/isa.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include "sja1000.h"

MODULE_DESCRIPTION("Driver for Technologic Systems TS-CAN1 PC104 boards");
MODULE_AUTHOR("Andre B. Oliveira <anbadeol@gmail.com>");
MODULE_LICENSE("GPL");

/* Maximum number of boards (one in each JP1:JP2 setting of IO address) */
#define TSCAN1_MAXDEV 4

/* PLD registers address offsets */
#define TSCAN1_ID1	0
#define TSCAN1_ID2	1
#define TSCAN1_VERSION	2
#define TSCAN1_LED	3
#define TSCAN1_PAGE	4
#define TSCAN1_MODE	5
#define TSCAN1_JUMPERS	6

/* PLD board identifier registers magic values */
#define TSCAN1_ID1_VALUE 0xf6
#define TSCAN1_ID2_VALUE 0xb9

/* PLD mode register SJA1000 IO enable bit */
#define TSCAN1_MODE_ENABLE 0x40

/* PLD jumpers register bits */
#define TSCAN1_JP4 0x10
#define TSCAN1_JP5 0x20

/* PLD IO base addresses start */
#define TSCAN1_PLD_ADDRESS 0x150

/* PLD register space size */
#define TSCAN1_PLD_SIZE 8

/* SJA1000 register space size */
#define TSCAN1_SJA1000_SIZE 32

/* SJA1000 crystal frequency (16MHz) */
#define TSCAN1_SJA1000_XTAL 16000000

/* SJA1000 IO base addresses */
static const unsigned short tscan1_sja1000_addresses[] = {
	0x100, 0x120, 0x180, 0x1a0, 0x200, 0x240, 0x280, 0x320
};

/* Read SJA1000 register */
static u8 tscan1_read(const struct sja1000_priv *priv, int reg)
{
	return inb((unsigned long)priv->reg_base + reg);
}

/* Write SJA1000 register */
static void tscan1_write(const struct sja1000_priv *priv, int reg, u8 val)
{
	outb(val, (unsigned long)priv->reg_base + reg);
}

/* Probe for a TS-CAN1 board with JP2:JP1 jumper setting ID */
static int tscan1_probe(struct device *dev, unsigned id)
{
	struct net_device *netdev;
	struct sja1000_priv *priv;
	unsigned long pld_base, sja1000_base;
	int irq, i;

	pld_base = TSCAN1_PLD_ADDRESS + id * TSCAN1_PLD_SIZE;
	if (!request_region(pld_base, TSCAN1_PLD_SIZE, dev_name(dev)))
		return -EBUSY;

	if (inb(pld_base + TSCAN1_ID1) != TSCAN1_ID1_VALUE ||
	    inb(pld_base + TSCAN1_ID2) != TSCAN1_ID2_VALUE) {
		release_region(pld_base, TSCAN1_PLD_SIZE);
		return -ENODEV;
	}

	switch (inb(pld_base + TSCAN1_JUMPERS) & (TSCAN1_JP4 | TSCAN1_JP5)) {
	case TSCAN1_JP4:
		irq = 6;
		break;
	case TSCAN1_JP5:
		irq = 7;
		break;
	case TSCAN1_JP4 | TSCAN1_JP5:
		irq = 5;
		break;
	default:
		dev_err(dev, "invalid JP4:JP5 setting (no IRQ)\n");
		release_region(pld_base, TSCAN1_PLD_SIZE);
		return -EINVAL;
	}

	netdev = alloc_sja1000dev(0);
	if (!netdev) {
		release_region(pld_base, TSCAN1_PLD_SIZE);
		return -ENOMEM;
	}

	dev_set_drvdata(dev, netdev);
	SET_NETDEV_DEV(netdev, dev);

	netdev->base_addr = pld_base;
	netdev->irq = irq;

	priv = netdev_priv(netdev);
	priv->read_reg = tscan1_read;
	priv->write_reg = tscan1_write;
	priv->can.clock.freq = TSCAN1_SJA1000_XTAL / 2;
	priv->cdr = CDR_CBP | CDR_CLK_OFF;
	priv->ocr = OCR_TX0_PUSHPULL;

	/* Select the first SJA1000 IO address that is free and that works */
	for (i = 0; i < ARRAY_SIZE(tscan1_sja1000_addresses); i++) {
		sja1000_base = tscan1_sja1000_addresses[i];
		if (!request_region(sja1000_base, TSCAN1_SJA1000_SIZE,
								dev_name(dev)))
			continue;

		/* Set SJA1000 IO base address and enable it */
		outb(TSCAN1_MODE_ENABLE | i, pld_base + TSCAN1_MODE);

		priv->reg_base = (void __iomem *)sja1000_base;
		if (!register_sja1000dev(netdev)) {
			/* SJA1000 probe succeeded; turn LED off and return */
			outb(0, pld_base + TSCAN1_LED);
			netdev_info(netdev, "TS-CAN1 at 0x%lx 0x%lx irq %d\n",
						pld_base, sja1000_base, irq);
			return 0;
		}

		/* SJA1000 probe failed; release and try next address */
		outb(0, pld_base + TSCAN1_MODE);
		release_region(sja1000_base, TSCAN1_SJA1000_SIZE);
	}

	dev_err(dev, "failed to assign SJA1000 IO address\n");
	dev_set_drvdata(dev, NULL);
	free_sja1000dev(netdev);
	release_region(pld_base, TSCAN1_PLD_SIZE);
	return -ENXIO;
}

static void tscan1_remove(struct device *dev, unsigned id /*unused*/)
{
	struct net_device *netdev;
	struct sja1000_priv *priv;
	unsigned long pld_base, sja1000_base;

	netdev = dev_get_drvdata(dev);
	unregister_sja1000dev(netdev);
	dev_set_drvdata(dev, NULL);

	priv = netdev_priv(netdev);
	pld_base = netdev->base_addr;
	sja1000_base = (unsigned long)priv->reg_base;

	outb(0, pld_base + TSCAN1_MODE);	/* disable SJA1000 IO space */

	release_region(sja1000_base, TSCAN1_SJA1000_SIZE);
	release_region(pld_base, TSCAN1_PLD_SIZE);

	free_sja1000dev(netdev);
}

static struct isa_driver tscan1_isa_driver = {
	.probe = tscan1_probe,
	.remove = tscan1_remove,
	.driver = {
		.name = "tscan1",
	},
};

module_isa_driver(tscan1_isa_driver, TSCAN1_MAXDEV);
