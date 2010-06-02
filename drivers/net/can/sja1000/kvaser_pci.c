/*
 * Copyright (C) 2008 Per Dalen <per.dalen@cnw.se>
 *
 * Parts of this software are based on (derived) the following:
 *
 * - Kvaser linux driver, version 4.72 BETA
 *   Copyright (C) 2002-2007 KVASER AB
 *
 * - Lincan driver, version 0.3.3, OCERA project
 *   Copyright (C) 2004 Pavel Pisa
 *   Copyright (C) 2001 Arnaud Westenberg
 *
 * - Socketcan SJA1000 drivers
 *   Copyright (C) 2007 Wolfgang Grandegger <wg@grandegger.com>
 *   Copyright (c) 2002-2007 Volkswagen Group Electronic Research
 *   Copyright (c) 2003 Matthias Brukner, Trajet Gmbh, Rebenring 33,
 *   38106 Braunschweig, GERMANY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the version 2 of the GNU General Public License
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
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
#include <linux/pci.h>
#include <linux/can/dev.h>
#include <linux/io.h>

#include "sja1000.h"

#define DRV_NAME  "kvaser_pci"

MODULE_AUTHOR("Per Dalen <per.dalen@cnw.se>");
MODULE_DESCRIPTION("Socket-CAN driver for KVASER PCAN PCI cards");
MODULE_SUPPORTED_DEVICE("KVASER PCAN PCI CAN card");
MODULE_LICENSE("GPL v2");

#define MAX_NO_OF_CHANNELS        4 /* max no of channels on a single card */

struct kvaser_pci {
	int channel;
	struct pci_dev *pci_dev;
	struct net_device *slave_dev[MAX_NO_OF_CHANNELS-1];
	void __iomem *conf_addr;
	void __iomem *res_addr;
	int no_channels;
	u8 xilinx_ver;
};

#define KVASER_PCI_CAN_CLOCK      (16000000 / 2)

/*
 * The board configuration is probably following:
 * RX1 is connected to ground.
 * TX1 is not connected.
 * CLKO is not connected.
 * Setting the OCR register to 0xDA is a good idea.
 * This means  normal output mode , push-pull and the correct polarity.
 */
#define KVASER_PCI_OCR            (OCR_TX0_PUSHPULL | OCR_TX1_PUSHPULL)

/*
 * In the CDR register, you should set CBP to 1.
 * You will probably also want to set the clock divider value to 0
 * (meaning divide-by-2), the Pelican bit, and the clock-off bit
 * (you will have no need for CLKOUT anyway).
 */
#define KVASER_PCI_CDR            (CDR_CBP | CDR_CLKOUT_MASK)

/*
 * These register values are valid for revision 14 of the Xilinx logic.
 */
#define XILINX_VERINT             7   /* Lower nibble simulate interrupts,
					 high nibble version number. */

#define XILINX_PRESUMED_VERSION   14

/*
 * Important S5920 registers
 */
#define S5920_INTCSR              0x38
#define S5920_PTCR                0x60
#define INTCSR_ADDON_INTENABLE_M  0x2000


#define KVASER_PCI_PORT_BYTES     0x20

#define PCI_CONFIG_PORT_SIZE      0x80      /* size of the config io-memory */
#define PCI_PORT_SIZE             0x80      /* size of a channel io-memory */
#define PCI_PORT_XILINX_SIZE      0x08      /* size of a xilinx io-memory */

#define KVASER_PCI_VENDOR_ID1     0x10e8    /* the PCI device and vendor IDs */
#define KVASER_PCI_DEVICE_ID1     0x8406

#define KVASER_PCI_VENDOR_ID2     0x1a07    /* the PCI device and vendor IDs */
#define KVASER_PCI_DEVICE_ID2     0x0008

static DEFINE_PCI_DEVICE_TABLE(kvaser_pci_tbl) = {
	{KVASER_PCI_VENDOR_ID1, KVASER_PCI_DEVICE_ID1, PCI_ANY_ID, PCI_ANY_ID,},
	{KVASER_PCI_VENDOR_ID2, KVASER_PCI_DEVICE_ID2, PCI_ANY_ID, PCI_ANY_ID,},
	{ 0,}
};

MODULE_DEVICE_TABLE(pci, kvaser_pci_tbl);

static u8 kvaser_pci_read_reg(const struct sja1000_priv *priv, int port)
{
	return ioread8(priv->reg_base + port);
}

static void kvaser_pci_write_reg(const struct sja1000_priv *priv,
				 int port, u8 val)
{
	iowrite8(val, priv->reg_base + port);
}

static void kvaser_pci_disable_irq(struct net_device *dev)
{
	struct sja1000_priv *priv = netdev_priv(dev);
	struct kvaser_pci *board = priv->priv;
	u32 intcsr;

	/* Disable interrupts from card */
	intcsr = ioread32(board->conf_addr + S5920_INTCSR);
	intcsr &= ~INTCSR_ADDON_INTENABLE_M;
	iowrite32(intcsr, board->conf_addr + S5920_INTCSR);
}

static void kvaser_pci_enable_irq(struct net_device *dev)
{
	struct sja1000_priv *priv = netdev_priv(dev);
	struct kvaser_pci *board = priv->priv;
	u32 tmp_en_io;

	/* Enable interrupts from card */
	tmp_en_io = ioread32(board->conf_addr + S5920_INTCSR);
	tmp_en_io |= INTCSR_ADDON_INTENABLE_M;
	iowrite32(tmp_en_io, board->conf_addr + S5920_INTCSR);
}

static int number_of_sja1000_chip(void __iomem *base_addr)
{
	u8 status;
	int i;

	for (i = 0; i < MAX_NO_OF_CHANNELS; i++) {
		/* reset chip */
		iowrite8(MOD_RM, base_addr +
			 (i * KVASER_PCI_PORT_BYTES) + REG_MOD);
		status = ioread8(base_addr +
				 (i * KVASER_PCI_PORT_BYTES) + REG_MOD);
		/* check reset bit */
		if (!(status & MOD_RM))
			break;
	}

	return i;
}

static void kvaser_pci_del_chan(struct net_device *dev)
{
	struct sja1000_priv *priv;
	struct kvaser_pci *board;
	int i;

	if (!dev)
		return;
	priv = netdev_priv(dev);
	board = priv->priv;
	if (!board)
		return;

	dev_info(&board->pci_dev->dev, "Removing device %s\n",
		 dev->name);

	/* Disable PCI interrupts */
	kvaser_pci_disable_irq(dev);

	for (i = 0; i < board->no_channels - 1; i++) {
		if (board->slave_dev[i]) {
			dev_info(&board->pci_dev->dev, "Removing device %s\n",
				 board->slave_dev[i]->name);
			unregister_sja1000dev(board->slave_dev[i]);
			free_sja1000dev(board->slave_dev[i]);
		}
	}
	unregister_sja1000dev(dev);

	pci_iounmap(board->pci_dev, priv->reg_base);
	pci_iounmap(board->pci_dev, board->conf_addr);
	pci_iounmap(board->pci_dev, board->res_addr);

	free_sja1000dev(dev);
}

static int kvaser_pci_add_chan(struct pci_dev *pdev, int channel,
			       struct net_device **master_dev,
			       void __iomem *conf_addr,
			       void __iomem *res_addr,
			       void __iomem *base_addr)
{
	struct net_device *dev;
	struct sja1000_priv *priv;
	struct kvaser_pci *board;
	int err, init_step;

	dev = alloc_sja1000dev(sizeof(struct kvaser_pci));
	if (dev == NULL)
		return -ENOMEM;

	priv = netdev_priv(dev);
	board = priv->priv;

	board->pci_dev = pdev;
	board->channel = channel;

	/* S5920 */
	board->conf_addr = conf_addr;

	/* XILINX board wide address */
	board->res_addr = res_addr;

	if (channel == 0) {
		board->xilinx_ver =
			ioread8(board->res_addr + XILINX_VERINT) >> 4;
		init_step = 2;

		/* Assert PTADR# - we're in passive mode so the other bits are
		   not important */
		iowrite32(0x80808080UL, board->conf_addr + S5920_PTCR);

		/* Enable interrupts from card */
		kvaser_pci_enable_irq(dev);
	} else {
		struct sja1000_priv *master_priv = netdev_priv(*master_dev);
		struct kvaser_pci *master_board = master_priv->priv;
		master_board->slave_dev[channel - 1] = dev;
		master_board->no_channels = channel + 1;
		board->xilinx_ver = master_board->xilinx_ver;
	}

	priv->reg_base = base_addr + channel * KVASER_PCI_PORT_BYTES;

	priv->read_reg = kvaser_pci_read_reg;
	priv->write_reg = kvaser_pci_write_reg;

	priv->can.clock.freq = KVASER_PCI_CAN_CLOCK;

	priv->ocr = KVASER_PCI_OCR;
	priv->cdr = KVASER_PCI_CDR;

	priv->irq_flags = IRQF_SHARED;
	dev->irq = pdev->irq;

	init_step = 4;

	dev_info(&pdev->dev, "reg_base=%p conf_addr=%p irq=%d\n",
		 priv->reg_base, board->conf_addr, dev->irq);

	SET_NETDEV_DEV(dev, &pdev->dev);

	/* Register SJA1000 device */
	err = register_sja1000dev(dev);
	if (err) {
		dev_err(&pdev->dev, "Registering device failed (err=%d)\n",
			err);
		goto failure;
	}

	if (channel == 0)
		*master_dev = dev;

	return 0;

failure:
	kvaser_pci_del_chan(dev);
	return err;
}

static int __devinit kvaser_pci_init_one(struct pci_dev *pdev,
					 const struct pci_device_id *ent)
{
	int err;
	struct net_device *master_dev = NULL;
	struct sja1000_priv *priv;
	struct kvaser_pci *board;
	int no_channels;
	void __iomem *base_addr = NULL;
	void __iomem *conf_addr = NULL;
	void __iomem *res_addr = NULL;
	int i;

	dev_info(&pdev->dev, "initializing device %04x:%04x\n",
		 pdev->vendor, pdev->device);

	err = pci_enable_device(pdev);
	if (err)
		goto failure;

	err = pci_request_regions(pdev, DRV_NAME);
	if (err)
		goto failure_release_pci;

	/* S5920 */
	conf_addr = pci_iomap(pdev, 0, PCI_CONFIG_PORT_SIZE);
	if (conf_addr == NULL) {
		err = -ENODEV;
		goto failure_release_regions;
	}

	/* XILINX board wide address */
	res_addr = pci_iomap(pdev, 2, PCI_PORT_XILINX_SIZE);
	if (res_addr == NULL) {
		err = -ENOMEM;
		goto failure_iounmap;
	}

	base_addr = pci_iomap(pdev, 1, PCI_PORT_SIZE);
	if (base_addr == NULL) {
		err = -ENOMEM;
		goto failure_iounmap;
	}

	no_channels = number_of_sja1000_chip(base_addr);
	if (no_channels == 0) {
		err = -ENOMEM;
		goto failure_iounmap;
	}

	for (i = 0; i < no_channels; i++) {
		err = kvaser_pci_add_chan(pdev, i, &master_dev,
					  conf_addr, res_addr,
					  base_addr);
		if (err)
			goto failure_cleanup;
	}

	priv = netdev_priv(master_dev);
	board = priv->priv;

	dev_info(&pdev->dev, "xilinx version=%d number of channels=%d\n",
		 board->xilinx_ver, board->no_channels);

	pci_set_drvdata(pdev, master_dev);
	return 0;

failure_cleanup:
	kvaser_pci_del_chan(master_dev);

failure_iounmap:
	if (conf_addr != NULL)
		pci_iounmap(pdev, conf_addr);
	if (res_addr != NULL)
		pci_iounmap(pdev, res_addr);
	if (base_addr != NULL)
		pci_iounmap(pdev, base_addr);

failure_release_regions:
	pci_release_regions(pdev);

failure_release_pci:
	pci_disable_device(pdev);

failure:
	return err;

}

static void __devexit kvaser_pci_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);

	kvaser_pci_del_chan(dev);

	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
}

static struct pci_driver kvaser_pci_driver = {
	.name = DRV_NAME,
	.id_table = kvaser_pci_tbl,
	.probe = kvaser_pci_init_one,
	.remove = __devexit_p(kvaser_pci_remove_one),
};

static int __init kvaser_pci_init(void)
{
	return pci_register_driver(&kvaser_pci_driver);
}

static void __exit kvaser_pci_exit(void)
{
	pci_unregister_driver(&kvaser_pci_driver);
}

module_init(kvaser_pci_init);
module_exit(kvaser_pci_exit);
