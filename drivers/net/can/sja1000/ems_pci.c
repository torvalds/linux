/*
 * Copyright (C) 2007 Wolfgang Grandegger <wg@grandegger.com>
 * Copyright (C) 2008 Markus Plessing <plessing@ems-wuensche.com>
 * Copyright (C) 2008 Sebastian Haas <haas@ems-wuensche.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the version 2 of the GNU General Public License
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/can/dev.h>
#include <linux/io.h>

#include "sja1000.h"

#define DRV_NAME  "ems_pci"

MODULE_AUTHOR("Sebastian Haas <haas@ems-wuenche.com>");
MODULE_DESCRIPTION("Socket-CAN driver for EMS CPC-PCI/PCIe/104P CAN cards");
MODULE_SUPPORTED_DEVICE("EMS CPC-PCI/PCIe/104P CAN card");
MODULE_LICENSE("GPL v2");

#define EMS_PCI_V1_MAX_CHAN 2
#define EMS_PCI_V2_MAX_CHAN 4
#define EMS_PCI_MAX_CHAN    EMS_PCI_V2_MAX_CHAN

struct ems_pci_card {
	int version;
	int channels;

	struct pci_dev *pci_dev;
	struct net_device *net_dev[EMS_PCI_MAX_CHAN];

	void __iomem *conf_addr;
	void __iomem *base_addr;
};

#define EMS_PCI_CAN_CLOCK (16000000 / 2)

/*
 * Register definitions and descriptions are from LinCAN 0.3.3.
 *
 * PSB4610 PITA-2 bridge control registers
 */
#define PITA2_ICR           0x00	/* Interrupt Control Register */
#define PITA2_ICR_INT0      0x00000002	/* [RC] INT0 Active/Clear */
#define PITA2_ICR_INT0_EN   0x00020000	/* [RW] Enable INT0 */

#define PITA2_MISC          0x1c	/* Miscellaneous Register */
#define PITA2_MISC_CONFIG   0x04000000	/* Multiplexed parallel interface */

/*
 * Register definitions for the PLX 9030
 */
#define PLX_ICSR            0x4c   /* Interrupt Control/Status register */
#define PLX_ICSR_LINTI1_ENA 0x0001 /* LINTi1 Enable */
#define PLX_ICSR_PCIINT_ENA 0x0040 /* PCI Interrupt Enable */
#define PLX_ICSR_LINTI1_CLR 0x0400 /* Local Edge Triggerable Interrupt Clear */
#define PLX_ICSR_ENA_CLR    (PLX_ICSR_LINTI1_ENA | PLX_ICSR_PCIINT_ENA | \
			     PLX_ICSR_LINTI1_CLR)

/*
 * The board configuration is probably following:
 * RX1 is connected to ground.
 * TX1 is not connected.
 * CLKO is not connected.
 * Setting the OCR register to 0xDA is a good idea.
 * This means normal output mode, push-pull and the correct polarity.
 */
#define EMS_PCI_OCR         (OCR_TX0_PUSHPULL | OCR_TX1_PUSHPULL)

/*
 * In the CDR register, you should set CBP to 1.
 * You will probably also want to set the clock divider value to 7
 * (meaning direct oscillator output) because the second SJA1000 chip
 * is driven by the first one CLKOUT output.
 */
#define EMS_PCI_CDR             (CDR_CBP | CDR_CLKOUT_MASK)

#define EMS_PCI_V1_BASE_BAR     1
#define EMS_PCI_V1_CONF_SIZE    4096 /* size of PITA control area */
#define EMS_PCI_V2_BASE_BAR     2
#define EMS_PCI_V2_CONF_SIZE    128 /* size of PLX control area */
#define EMS_PCI_CAN_BASE_OFFSET 0x400 /* offset where the controllers starts */
#define EMS_PCI_CAN_CTRL_SIZE   0x200 /* memory size for each controller */

#define EMS_PCI_BASE_SIZE  4096 /* size of controller area */

static DEFINE_PCI_DEVICE_TABLE(ems_pci_tbl) = {
	/* CPC-PCI v1 */
	{PCI_VENDOR_ID_SIEMENS, 0x2104, PCI_ANY_ID, PCI_ANY_ID,},
	/* CPC-PCI v2 */
	{PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9030, PCI_VENDOR_ID_PLX, 0x4000},
	/* CPC-104P v2 */
	{PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9030, PCI_VENDOR_ID_PLX, 0x4002},
	{0,}
};
MODULE_DEVICE_TABLE(pci, ems_pci_tbl);

/*
 * Helper to read internal registers from card logic (not CAN)
 */
static u8 ems_pci_v1_readb(struct ems_pci_card *card, unsigned int port)
{
	return readb(card->base_addr + (port * 4));
}

static u8 ems_pci_v1_read_reg(const struct sja1000_priv *priv, int port)
{
	return readb(priv->reg_base + (port * 4));
}

static void ems_pci_v1_write_reg(const struct sja1000_priv *priv,
				 int port, u8 val)
{
	writeb(val, priv->reg_base + (port * 4));
}

static void ems_pci_v1_post_irq(const struct sja1000_priv *priv)
{
	struct ems_pci_card *card = (struct ems_pci_card *)priv->priv;

	/* reset int flag of pita */
	writel(PITA2_ICR_INT0_EN | PITA2_ICR_INT0,
	       card->conf_addr + PITA2_ICR);
}

static u8 ems_pci_v2_read_reg(const struct sja1000_priv *priv, int port)
{
	return readb(priv->reg_base + port);
}

static void ems_pci_v2_write_reg(const struct sja1000_priv *priv,
				 int port, u8 val)
{
	writeb(val, priv->reg_base + port);
}

static void ems_pci_v2_post_irq(const struct sja1000_priv *priv)
{
	struct ems_pci_card *card = (struct ems_pci_card *)priv->priv;

	writel(PLX_ICSR_ENA_CLR, card->conf_addr + PLX_ICSR);
}

/*
 * Check if a CAN controller is present at the specified location
 * by trying to set 'em into the PeliCAN mode
 */
static inline int ems_pci_check_chan(const struct sja1000_priv *priv)
{
	unsigned char res;

	/* Make sure SJA1000 is in reset mode */
	priv->write_reg(priv, SJA1000_MOD, 1);

	priv->write_reg(priv, SJA1000_CDR, CDR_PELICAN);

	/* read reset-values */
	res = priv->read_reg(priv, SJA1000_CDR);

	if (res == CDR_PELICAN)
		return 1;

	return 0;
}

static void ems_pci_del_card(struct pci_dev *pdev)
{
	struct ems_pci_card *card = pci_get_drvdata(pdev);
	struct net_device *dev;
	int i = 0;

	for (i = 0; i < card->channels; i++) {
		dev = card->net_dev[i];

		if (!dev)
			continue;

		dev_info(&pdev->dev, "Removing %s.\n", dev->name);
		unregister_sja1000dev(dev);
		free_sja1000dev(dev);
	}

	if (card->base_addr != NULL)
		pci_iounmap(card->pci_dev, card->base_addr);

	if (card->conf_addr != NULL)
		pci_iounmap(card->pci_dev, card->conf_addr);

	kfree(card);

	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
}

static void ems_pci_card_reset(struct ems_pci_card *card)
{
	/* Request board reset */
	writeb(0, card->base_addr);
}

/*
 * Probe PCI device for EMS CAN signature and register each available
 * CAN channel to SJA1000 Socket-CAN subsystem.
 */
static int ems_pci_add_card(struct pci_dev *pdev,
			    const struct pci_device_id *ent)
{
	struct sja1000_priv *priv;
	struct net_device *dev;
	struct ems_pci_card *card;
	int max_chan, conf_size, base_bar;
	int err, i;

	/* Enabling PCI device */
	if (pci_enable_device(pdev) < 0) {
		dev_err(&pdev->dev, "Enabling PCI device failed\n");
		return -ENODEV;
	}

	/* Allocating card structures to hold addresses, ... */
	card = kzalloc(sizeof(struct ems_pci_card), GFP_KERNEL);
	if (card == NULL) {
		pci_disable_device(pdev);
		return -ENOMEM;
	}

	pci_set_drvdata(pdev, card);

	card->pci_dev = pdev;

	card->channels = 0;

	if (pdev->vendor == PCI_VENDOR_ID_PLX) {
		card->version = 2; /* CPC-PCI v2 */
		max_chan = EMS_PCI_V2_MAX_CHAN;
		base_bar = EMS_PCI_V2_BASE_BAR;
		conf_size = EMS_PCI_V2_CONF_SIZE;
	} else {
		card->version = 1; /* CPC-PCI v1 */
		max_chan = EMS_PCI_V1_MAX_CHAN;
		base_bar = EMS_PCI_V1_BASE_BAR;
		conf_size = EMS_PCI_V1_CONF_SIZE;
	}

	/* Remap configuration space and controller memory area */
	card->conf_addr = pci_iomap(pdev, 0, conf_size);
	if (card->conf_addr == NULL) {
		err = -ENOMEM;
		goto failure_cleanup;
	}

	card->base_addr = pci_iomap(pdev, base_bar, EMS_PCI_BASE_SIZE);
	if (card->base_addr == NULL) {
		err = -ENOMEM;
		goto failure_cleanup;
	}

	if (card->version == 1) {
		/* Configure PITA-2 parallel interface (enable MUX) */
		writel(PITA2_MISC_CONFIG, card->conf_addr + PITA2_MISC);

		/* Check for unique EMS CAN signature */
		if (ems_pci_v1_readb(card, 0) != 0x55 ||
		    ems_pci_v1_readb(card, 1) != 0xAA ||
		    ems_pci_v1_readb(card, 2) != 0x01 ||
		    ems_pci_v1_readb(card, 3) != 0xCB ||
		    ems_pci_v1_readb(card, 4) != 0x11) {
			dev_err(&pdev->dev,
				"Not EMS Dr. Thomas Wuensche interface\n");
			err = -ENODEV;
			goto failure_cleanup;
		}
	}

	ems_pci_card_reset(card);

	/* Detect available channels */
	for (i = 0; i < max_chan; i++) {
		dev = alloc_sja1000dev(0);
		if (dev == NULL) {
			err = -ENOMEM;
			goto failure_cleanup;
		}

		card->net_dev[i] = dev;
		priv = netdev_priv(dev);
		priv->priv = card;
		priv->irq_flags = IRQF_SHARED;

		dev->irq = pdev->irq;
		priv->reg_base = card->base_addr + EMS_PCI_CAN_BASE_OFFSET
					+ (i * EMS_PCI_CAN_CTRL_SIZE);
		if (card->version == 1) {
			priv->read_reg  = ems_pci_v1_read_reg;
			priv->write_reg = ems_pci_v1_write_reg;
			priv->post_irq  = ems_pci_v1_post_irq;
		} else {
			priv->read_reg  = ems_pci_v2_read_reg;
			priv->write_reg = ems_pci_v2_write_reg;
			priv->post_irq  = ems_pci_v2_post_irq;
		}

		/* Check if channel is present */
		if (ems_pci_check_chan(priv)) {
			priv->can.clock.freq = EMS_PCI_CAN_CLOCK;
			priv->ocr = EMS_PCI_OCR;
			priv->cdr = EMS_PCI_CDR;

			SET_NETDEV_DEV(dev, &pdev->dev);

			if (card->version == 1)
				/* reset int flag of pita */
				writel(PITA2_ICR_INT0_EN | PITA2_ICR_INT0,
				       card->conf_addr + PITA2_ICR);
			else
				/* enable IRQ in PLX 9030 */
				writel(PLX_ICSR_ENA_CLR,
				       card->conf_addr + PLX_ICSR);

			/* Register SJA1000 device */
			err = register_sja1000dev(dev);
			if (err) {
				dev_err(&pdev->dev, "Registering device failed "
							"(err=%d)\n", err);
				free_sja1000dev(dev);
				goto failure_cleanup;
			}

			card->channels++;

			dev_info(&pdev->dev, "Channel #%d at 0x%p, irq %d\n",
					i + 1, priv->reg_base, dev->irq);
		} else {
			free_sja1000dev(dev);
		}
	}

	return 0;

failure_cleanup:
	dev_err(&pdev->dev, "Error: %d. Cleaning Up.\n", err);

	ems_pci_del_card(pdev);

	return err;
}

static struct pci_driver ems_pci_driver = {
	.name = DRV_NAME,
	.id_table = ems_pci_tbl,
	.probe = ems_pci_add_card,
	.remove = ems_pci_del_card,
};

module_pci_driver(ems_pci_driver);
