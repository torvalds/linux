// SPDX-License-Identifier: GPL-2.0
/* Fintek F81601 PCIE to 2 CAN controller driver
 *
 * Copyright (C) 2019 Peter Hong <peter_hong@fintek.com.tw>
 * Copyright (C) 2019 Linux Foundation
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

#define F81601_PCI_MAX_CHAN		2

#define F81601_DECODE_REG		0x209
#define F81601_IO_MODE			BIT(7)
#define F81601_MEM_MODE			BIT(6)
#define F81601_CFG_MODE			BIT(5)
#define F81601_CAN2_INTERNAL_CLK	BIT(3)
#define F81601_CAN1_INTERNAL_CLK	BIT(2)
#define F81601_CAN2_EN			BIT(1)
#define F81601_CAN1_EN			BIT(0)

#define F81601_TRAP_REG			0x20a
#define F81601_CAN2_HAS_EN		BIT(4)

struct f81601_pci_card {
	void __iomem *addr;
	spinlock_t lock;	/* use this spin lock only for write access */
	struct pci_dev *dev;
	struct net_device *net_dev[F81601_PCI_MAX_CHAN];
};

static const struct pci_device_id f81601_pci_tbl[] = {
	{ PCI_DEVICE(0x1c29, 0x1703) },
	{ /* sentinel */ },
};

MODULE_DEVICE_TABLE(pci, f81601_pci_tbl);

static bool internal_clk = true;
module_param(internal_clk, bool, 0444);
MODULE_PARM_DESC(internal_clk, "Use internal clock, default true (24MHz)");

static unsigned int external_clk;
module_param(external_clk, uint, 0444);
MODULE_PARM_DESC(external_clk, "External clock when internal_clk disabled");

static u8 f81601_pci_read_reg(const struct sja1000_priv *priv, int port)
{
	return readb(priv->reg_base + port);
}

static void f81601_pci_write_reg(const struct sja1000_priv *priv, int port,
				 u8 val)
{
	struct f81601_pci_card *card = priv->priv;
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);
	writeb(val, priv->reg_base + port);
	readb(priv->reg_base);
	spin_unlock_irqrestore(&card->lock, flags);
}

static void f81601_pci_remove(struct pci_dev *pdev)
{
	struct f81601_pci_card *card = pci_get_drvdata(pdev);
	struct net_device *dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(card->net_dev); i++) {
		dev = card->net_dev[i];
		if (!dev)
			continue;

		dev_info(&pdev->dev, "%s: Removing %s\n", __func__, dev->name);

		unregister_sja1000dev(dev);
		free_sja1000dev(dev);
	}
}

/* Probe F81601 based device for the SJA1000 chips and register each
 * available CAN channel to SJA1000 Socket-CAN subsystem.
 */
static int f81601_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *ent)
{
	struct sja1000_priv *priv;
	struct net_device *dev;
	struct f81601_pci_card *card;
	int err, i, count;
	u8 tmp;

	if (pcim_enable_device(pdev) < 0) {
		dev_err(&pdev->dev, "Failed to enable PCI device\n");
		return -ENODEV;
	}

	dev_info(&pdev->dev, "Detected card at slot #%i\n",
		 PCI_SLOT(pdev->devfn));

	card = devm_kzalloc(&pdev->dev, sizeof(*card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	card->dev = pdev;
	spin_lock_init(&card->lock);

	pci_set_drvdata(pdev, card);

	tmp = F81601_IO_MODE | F81601_MEM_MODE | F81601_CFG_MODE |
		F81601_CAN2_EN | F81601_CAN1_EN;

	if (internal_clk) {
		tmp |= F81601_CAN2_INTERNAL_CLK | F81601_CAN1_INTERNAL_CLK;

		dev_info(&pdev->dev,
			 "F81601 running with internal clock: 24Mhz\n");
	} else {
		dev_info(&pdev->dev,
			 "F81601 running with external clock: %dMhz\n",
			 external_clk / 1000000);
	}

	pci_write_config_byte(pdev, F81601_DECODE_REG, tmp);

	card->addr = pcim_iomap(pdev, 0, pci_resource_len(pdev, 0));

	if (!card->addr) {
		err = -ENOMEM;
		dev_err(&pdev->dev, "%s: Failed to remap BAR\n", __func__);
		goto failure_cleanup;
	}

	/* read CAN2_HW_EN strap pin to detect how many CANBUS do we have */
	count = ARRAY_SIZE(card->net_dev);
	pci_read_config_byte(pdev, F81601_TRAP_REG, &tmp);
	if (!(tmp & F81601_CAN2_HAS_EN))
		count = 1;

	for (i = 0; i < count; i++) {
		dev = alloc_sja1000dev(0);
		if (!dev) {
			err = -ENOMEM;
			goto failure_cleanup;
		}

		priv = netdev_priv(dev);
		priv->priv = card;
		priv->irq_flags = IRQF_SHARED;
		priv->reg_base = card->addr + 0x80 * i;
		priv->read_reg = f81601_pci_read_reg;
		priv->write_reg = f81601_pci_write_reg;

		if (internal_clk)
			priv->can.clock.freq = 24000000 / 2;
		else
			priv->can.clock.freq = external_clk / 2;

		priv->ocr = OCR_TX0_PUSHPULL | OCR_TX1_PUSHPULL;
		priv->cdr = CDR_CBP;

		SET_NETDEV_DEV(dev, &pdev->dev);
		dev->dev_id = i;
		dev->irq = pdev->irq;

		/* Register SJA1000 device */
		err = register_sja1000dev(dev);
		if (err) {
			dev_err(&pdev->dev,
				"%s: Registering device failed: %x\n", __func__,
				err);
			free_sja1000dev(dev);
			goto failure_cleanup;
		}

		card->net_dev[i] = dev;
		dev_info(&pdev->dev, "Channel #%d, %s at 0x%p, irq %d\n", i,
			 dev->name, priv->reg_base, dev->irq);
	}

	return 0;

 failure_cleanup:
	dev_err(&pdev->dev, "%s: failed: %d. Cleaning Up.\n", __func__, err);
	f81601_pci_remove(pdev);

	return err;
}

static struct pci_driver f81601_pci_driver = {
	.name =	"f81601",
	.id_table = f81601_pci_tbl,
	.probe = f81601_pci_probe,
	.remove = f81601_pci_remove,
};

MODULE_DESCRIPTION("Fintek F81601 PCIE to 2 CANBUS adaptor driver");
MODULE_AUTHOR("Peter Hong <peter_hong@fintek.com.tw>");
MODULE_LICENSE("GPL v2");

module_pci_driver(f81601_pci_driver);
