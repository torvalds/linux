/*
 * Copyright (C) 2008-2010 Pavel Cheblakov <P.B.Cheblakov@inp.nsk.su>
 *
 * Derived from the ems_pci.c driver:
 *	Copyright (C) 2007 Wolfgang Grandegger <wg@grandegger.com>
 *	Copyright (C) 2008 Markus Plessing <plessing@ems-wuensche.com>
 *	Copyright (C) 2008 Sebastian Haas <haas@ems-wuensche.com>
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

#define DRV_NAME  "sja1000_plx_pci"

MODULE_AUTHOR("Pavel Cheblakov <P.B.Cheblakov@inp.nsk.su>");
MODULE_DESCRIPTION("Socket-CAN driver for PLX90xx PCI-bridge cards with "
		   "the SJA1000 chips");
MODULE_SUPPORTED_DEVICE("Adlink PCI-7841/cPCI-7841, "
			"Adlink PCI-7841/cPCI-7841 SE, "
			"Marathon CAN-bus-PCI, "
			"TEWS TECHNOLOGIES TPMC810, "
			"esd CAN-PCI/CPCI/PCI104/200, "
			"esd CAN-PCI/PMC/266, "
			"esd CAN-PCIe/2000, "
			"IXXAT PC-I 04/PCI")
MODULE_LICENSE("GPL v2");

#define PLX_PCI_MAX_CHAN 2

struct plx_pci_card {
	int channels;			/* detected channels count */
	struct net_device *net_dev[PLX_PCI_MAX_CHAN];
	void __iomem *conf_addr;

	/* Pointer to device-dependent reset function */
	void (*reset_func)(struct pci_dev *pdev);
};

#define PLX_PCI_CAN_CLOCK (16000000 / 2)

/* PLX9030/9050/9052 registers */
#define PLX_INTCSR	0x4c		/* Interrupt Control/Status */
#define PLX_CNTRL	0x50		/* User I/O, Direct Slave Response,
					 * Serial EEPROM, and Initialization
					 * Control register
					 */

#define PLX_LINT1_EN	0x1		/* Local interrupt 1 enable */
#define PLX_LINT2_EN	(1 << 3)	/* Local interrupt 2 enable */
#define PLX_PCI_INT_EN	(1 << 6)	/* PCI Interrupt Enable */
#define PLX_PCI_RESET	(1 << 30)	/* PCI Adapter Software Reset */

/* PLX9056 registers */
#define PLX9056_INTCSR	0x68		/* Interrupt Control/Status */
#define PLX9056_CNTRL	0x6c		/* Control / Software Reset */

#define PLX9056_LINTI	(1 << 11)
#define PLX9056_PCI_INT_EN (1 << 8)
#define PLX9056_PCI_RCR	(1 << 29)	/* Read Configuration Registers */

/*
 * The board configuration is probably following:
 * RX1 is connected to ground.
 * TX1 is not connected.
 * CLKO is not connected.
 * Setting the OCR register to 0xDA is a good idea.
 * This means normal output mode, push-pull and the correct polarity.
 */
#define PLX_PCI_OCR	(OCR_TX0_PUSHPULL | OCR_TX1_PUSHPULL)

/*
 * In the CDR register, you should set CBP to 1.
 * You will probably also want to set the clock divider value to 7
 * (meaning direct oscillator output) because the second SJA1000 chip
 * is driven by the first one CLKOUT output.
 */
#define PLX_PCI_CDR			(CDR_CBP | CDR_CLKOUT_MASK)

/* SJA1000 Control Register in the BasicCAN Mode */
#define REG_CR				0x00

/* States of some SJA1000 registers after hardware reset in the BasicCAN mode*/
#define REG_CR_BASICCAN_INITIAL		0x21
#define REG_CR_BASICCAN_INITIAL_MASK	0xa1
#define REG_SR_BASICCAN_INITIAL		0x0c
#define REG_IR_BASICCAN_INITIAL		0xe0

/* States of some SJA1000 registers after hardware reset in the PeliCAN mode*/
#define REG_MOD_PELICAN_INITIAL		0x01
#define REG_SR_PELICAN_INITIAL		0x3c
#define REG_IR_PELICAN_INITIAL		0x00

#define ADLINK_PCI_VENDOR_ID		0x144A
#define ADLINK_PCI_DEVICE_ID		0x7841

#define ESD_PCI_SUB_SYS_ID_PCI200	0x0004
#define ESD_PCI_SUB_SYS_ID_PCI266	0x0009
#define ESD_PCI_SUB_SYS_ID_PMC266	0x000e
#define ESD_PCI_SUB_SYS_ID_CPCI200	0x010b
#define ESD_PCI_SUB_SYS_ID_PCIE2000	0x0200
#define ESD_PCI_SUB_SYS_ID_PCI104200	0x0501

#define IXXAT_PCI_VENDOR_ID		0x10b5
#define IXXAT_PCI_DEVICE_ID		0x9050
#define IXXAT_PCI_SUB_SYS_ID		0x2540

#define MARATHON_PCI_DEVICE_ID		0x2715

#define TEWS_PCI_VENDOR_ID		0x1498
#define TEWS_PCI_DEVICE_ID_TMPC810	0x032A

static void plx_pci_reset_common(struct pci_dev *pdev);
static void plx_pci_reset_marathon(struct pci_dev *pdev);
static void plx9056_pci_reset_common(struct pci_dev *pdev);

struct plx_pci_channel_map {
	u32 bar;
	u32 offset;
	u32 size;		/* 0x00 - auto, e.g. length of entire bar */
};

struct plx_pci_card_info {
	const char *name;
	int channel_count;
	u32 can_clock;
	u8 ocr;			/* output control register */
	u8 cdr;			/* clock divider register */

	/* Parameters for mapping local configuration space */
	struct plx_pci_channel_map conf_map;

	/* Parameters for mapping the SJA1000 chips */
	struct plx_pci_channel_map chan_map_tbl[PLX_PCI_MAX_CHAN];

	/* Pointer to device-dependent reset function */
	void (*reset_func)(struct pci_dev *pdev);
};

static struct plx_pci_card_info plx_pci_card_info_adlink __devinitdata = {
	"Adlink PCI-7841/cPCI-7841", 2,
	PLX_PCI_CAN_CLOCK, PLX_PCI_OCR, PLX_PCI_CDR,
	{1, 0x00, 0x00}, { {2, 0x00, 0x80}, {2, 0x80, 0x80} },
	&plx_pci_reset_common
	/* based on PLX9052 */
};

static struct plx_pci_card_info plx_pci_card_info_adlink_se __devinitdata = {
	"Adlink PCI-7841/cPCI-7841 SE", 2,
	PLX_PCI_CAN_CLOCK, PLX_PCI_OCR, PLX_PCI_CDR,
	{0, 0x00, 0x00}, { {2, 0x00, 0x80}, {2, 0x80, 0x80} },
	&plx_pci_reset_common
	/* based on PLX9052 */
};

static struct plx_pci_card_info plx_pci_card_info_esd200 __devinitdata = {
	"esd CAN-PCI/CPCI/PCI104/200", 2,
	PLX_PCI_CAN_CLOCK, PLX_PCI_OCR, PLX_PCI_CDR,
	{0, 0x00, 0x00}, { {2, 0x00, 0x80}, {2, 0x100, 0x80} },
	&plx_pci_reset_common
	/* based on PLX9030/9050 */
};

static struct plx_pci_card_info plx_pci_card_info_esd266 __devinitdata = {
	"esd CAN-PCI/PMC/266", 2,
	PLX_PCI_CAN_CLOCK, PLX_PCI_OCR, PLX_PCI_CDR,
	{0, 0x00, 0x00}, { {2, 0x00, 0x80}, {2, 0x100, 0x80} },
	&plx9056_pci_reset_common
	/* based on PLX9056 */
};

static struct plx_pci_card_info plx_pci_card_info_esd2000 __devinitdata = {
	"esd CAN-PCIe/2000", 2,
	PLX_PCI_CAN_CLOCK, PLX_PCI_OCR, PLX_PCI_CDR,
	{0, 0x00, 0x00}, { {2, 0x00, 0x80}, {2, 0x100, 0x80} },
	&plx9056_pci_reset_common
	/* based on PEX8311 */
};

static struct plx_pci_card_info plx_pci_card_info_ixxat __devinitdata = {
	"IXXAT PC-I 04/PCI", 2,
	PLX_PCI_CAN_CLOCK, PLX_PCI_OCR, PLX_PCI_CDR,
	{0, 0x00, 0x00}, { {2, 0x00, 0x80}, {2, 0x200, 0x80} },
	&plx_pci_reset_common
	/* based on PLX9050 */
};

static struct plx_pci_card_info plx_pci_card_info_marathon __devinitdata = {
	"Marathon CAN-bus-PCI", 2,
	PLX_PCI_CAN_CLOCK, PLX_PCI_OCR, PLX_PCI_CDR,
	{0, 0x00, 0x00}, { {2, 0x00, 0x00}, {4, 0x00, 0x00} },
	&plx_pci_reset_marathon
	/* based on PLX9052 */
};

static struct plx_pci_card_info plx_pci_card_info_tews __devinitdata = {
	"TEWS TECHNOLOGIES TPMC810", 2,
	PLX_PCI_CAN_CLOCK, PLX_PCI_OCR, PLX_PCI_CDR,
	{0, 0x00, 0x00}, { {2, 0x000, 0x80}, {2, 0x100, 0x80} },
	&plx_pci_reset_common
	/* based on PLX9030 */
};

static DEFINE_PCI_DEVICE_TABLE(plx_pci_tbl) = {
	{
		/* Adlink PCI-7841/cPCI-7841 */
		ADLINK_PCI_VENDOR_ID, ADLINK_PCI_DEVICE_ID,
		PCI_ANY_ID, PCI_ANY_ID,
		PCI_CLASS_NETWORK_OTHER << 8, ~0,
		(kernel_ulong_t)&plx_pci_card_info_adlink
	},
	{
		/* Adlink PCI-7841/cPCI-7841 SE */
		ADLINK_PCI_VENDOR_ID, ADLINK_PCI_DEVICE_ID,
		PCI_ANY_ID, PCI_ANY_ID,
		PCI_CLASS_COMMUNICATION_OTHER << 8, ~0,
		(kernel_ulong_t)&plx_pci_card_info_adlink_se
	},
	{
		/* esd CAN-PCI/200 */
		PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9050,
		PCI_VENDOR_ID_ESDGMBH, ESD_PCI_SUB_SYS_ID_PCI200,
		0, 0,
		(kernel_ulong_t)&plx_pci_card_info_esd200
	},
	{
		/* esd CAN-CPCI/200 */
		PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9030,
		PCI_VENDOR_ID_ESDGMBH, ESD_PCI_SUB_SYS_ID_CPCI200,
		0, 0,
		(kernel_ulong_t)&plx_pci_card_info_esd200
	},
	{
		/* esd CAN-PCI104/200 */
		PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9030,
		PCI_VENDOR_ID_ESDGMBH, ESD_PCI_SUB_SYS_ID_PCI104200,
		0, 0,
		(kernel_ulong_t)&plx_pci_card_info_esd200
	},
	{
		/* esd CAN-PCI/266 */
		PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9056,
		PCI_VENDOR_ID_ESDGMBH, ESD_PCI_SUB_SYS_ID_PCI266,
		0, 0,
		(kernel_ulong_t)&plx_pci_card_info_esd266
	},
	{
		/* esd CAN-PMC/266 */
		PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9056,
		PCI_VENDOR_ID_ESDGMBH, ESD_PCI_SUB_SYS_ID_PMC266,
		0, 0,
		(kernel_ulong_t)&plx_pci_card_info_esd266
	},
	{
		/* esd CAN-PCIE/2000 */
		PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9056,
		PCI_VENDOR_ID_ESDGMBH, ESD_PCI_SUB_SYS_ID_PCIE2000,
		0, 0,
		(kernel_ulong_t)&plx_pci_card_info_esd2000
	},
	{
		/* IXXAT PC-I 04/PCI card */
		IXXAT_PCI_VENDOR_ID, IXXAT_PCI_DEVICE_ID,
		PCI_ANY_ID, IXXAT_PCI_SUB_SYS_ID,
		0, 0,
		(kernel_ulong_t)&plx_pci_card_info_ixxat
	},
	{
		/* Marathon CAN-bus-PCI card */
		PCI_VENDOR_ID_PLX, MARATHON_PCI_DEVICE_ID,
		PCI_ANY_ID, PCI_ANY_ID,
		0, 0,
		(kernel_ulong_t)&plx_pci_card_info_marathon
	},
	{
		/* TEWS TECHNOLOGIES TPMC810 card */
		TEWS_PCI_VENDOR_ID, TEWS_PCI_DEVICE_ID_TMPC810,
		PCI_ANY_ID, PCI_ANY_ID,
		0, 0,
		(kernel_ulong_t)&plx_pci_card_info_tews
	},
	{ 0,}
};
MODULE_DEVICE_TABLE(pci, plx_pci_tbl);

static u8 plx_pci_read_reg(const struct sja1000_priv *priv, int port)
{
	return ioread8(priv->reg_base + port);
}

static void plx_pci_write_reg(const struct sja1000_priv *priv, int port, u8 val)
{
	iowrite8(val, priv->reg_base + port);
}

/*
 * Check if a CAN controller is present at the specified location
 * by trying to switch 'em from the Basic mode into the PeliCAN mode.
 * Also check states of some registers in reset mode.
 */
static inline int plx_pci_check_sja1000(const struct sja1000_priv *priv)
{
	int flag = 0;

	/*
	 * Check registers after hardware reset (the Basic mode)
	 * See states on p. 10 of the Datasheet.
	 */
	if ((priv->read_reg(priv, REG_CR) & REG_CR_BASICCAN_INITIAL_MASK) ==
	    REG_CR_BASICCAN_INITIAL &&
	    (priv->read_reg(priv, REG_SR) == REG_SR_BASICCAN_INITIAL) &&
	    (priv->read_reg(priv, REG_IR) == REG_IR_BASICCAN_INITIAL))
		flag = 1;

	/* Bring the SJA1000 into the PeliCAN mode*/
	priv->write_reg(priv, REG_CDR, CDR_PELICAN);

	/*
	 * Check registers after reset in the PeliCAN mode.
	 * See states on p. 23 of the Datasheet.
	 */
	if (priv->read_reg(priv, REG_MOD) == REG_MOD_PELICAN_INITIAL &&
	    priv->read_reg(priv, REG_SR) == REG_SR_PELICAN_INITIAL &&
	    priv->read_reg(priv, REG_IR) == REG_IR_PELICAN_INITIAL)
		return flag;

	return 0;
}

/*
 * PLX9030/50/52 software reset
 * Also LRESET# asserts and brings to reset device on the Local Bus (if wired).
 * For most cards it's enough for reset the SJA1000 chips.
 */
static void plx_pci_reset_common(struct pci_dev *pdev)
{
	struct plx_pci_card *card = pci_get_drvdata(pdev);
	u32 cntrl;

	cntrl = ioread32(card->conf_addr + PLX_CNTRL);
	cntrl |= PLX_PCI_RESET;
	iowrite32(cntrl, card->conf_addr + PLX_CNTRL);
	udelay(100);
	cntrl ^= PLX_PCI_RESET;
	iowrite32(cntrl, card->conf_addr + PLX_CNTRL);
};

/*
 * PLX9056 software reset
 * Assert LRESET# and reset device(s) on the Local Bus (if wired).
 */
static void plx9056_pci_reset_common(struct pci_dev *pdev)
{
	struct plx_pci_card *card = pci_get_drvdata(pdev);
	u32 cntrl;

	/* issue a local bus reset */
	cntrl = ioread32(card->conf_addr + PLX9056_CNTRL);
	cntrl |= PLX_PCI_RESET;
	iowrite32(cntrl, card->conf_addr + PLX9056_CNTRL);
	udelay(100);
	cntrl ^= PLX_PCI_RESET;
	iowrite32(cntrl, card->conf_addr + PLX9056_CNTRL);

	/* reload local configuration from EEPROM */
	cntrl |= PLX9056_PCI_RCR;
	iowrite32(cntrl, card->conf_addr + PLX9056_CNTRL);

	/*
	 * There is no safe way to poll for the end
	 * of reconfiguration process. Waiting for 10ms
	 * is safe.
	 */
	mdelay(10);

	cntrl ^= PLX9056_PCI_RCR;
	iowrite32(cntrl, card->conf_addr + PLX9056_CNTRL);
};

/* Special reset function for Marathon card */
static void plx_pci_reset_marathon(struct pci_dev *pdev)
{
	void __iomem *reset_addr;
	int i;
	static const int reset_bar[2] = {3, 5};

	plx_pci_reset_common(pdev);

	for (i = 0; i < 2; i++) {
		reset_addr = pci_iomap(pdev, reset_bar[i], 0);
		if (!reset_addr) {
			dev_err(&pdev->dev, "Failed to remap reset "
				"space %d (BAR%d)\n", i, reset_bar[i]);
		} else {
			/* reset the SJA1000 chip */
			iowrite8(0x1, reset_addr);
			udelay(100);
			pci_iounmap(pdev, reset_addr);
		}
	}
}

static void plx_pci_del_card(struct pci_dev *pdev)
{
	struct plx_pci_card *card = pci_get_drvdata(pdev);
	struct net_device *dev;
	struct sja1000_priv *priv;
	int i = 0;

	for (i = 0; i < PLX_PCI_MAX_CHAN; i++) {
		dev = card->net_dev[i];
		if (!dev)
			continue;

		dev_info(&pdev->dev, "Removing %s\n", dev->name);
		unregister_sja1000dev(dev);
		priv = netdev_priv(dev);
		if (priv->reg_base)
			pci_iounmap(pdev, priv->reg_base);
		free_sja1000dev(dev);
	}

	card->reset_func(pdev);

	/*
	 * Disable interrupts from PCI-card and disable local
	 * interrupts
	 */
	if (pdev->device != PCI_DEVICE_ID_PLX_9056)
		iowrite32(0x0, card->conf_addr + PLX_INTCSR);
	else
		iowrite32(0x0, card->conf_addr + PLX9056_INTCSR);

	if (card->conf_addr)
		pci_iounmap(pdev, card->conf_addr);

	kfree(card);

	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
}

/*
 * Probe PLX90xx based device for the SJA1000 chips and register each
 * available CAN channel to SJA1000 Socket-CAN subsystem.
 */
static int __devinit plx_pci_add_card(struct pci_dev *pdev,
				      const struct pci_device_id *ent)
{
	struct sja1000_priv *priv;
	struct net_device *dev;
	struct plx_pci_card *card;
	struct plx_pci_card_info *ci;
	int err, i;
	u32 val;
	void __iomem *addr;

	ci = (struct plx_pci_card_info *)ent->driver_data;

	if (pci_enable_device(pdev) < 0) {
		dev_err(&pdev->dev, "Failed to enable PCI device\n");
		return -ENODEV;
	}

	dev_info(&pdev->dev, "Detected \"%s\" card at slot #%i\n",
		 ci->name, PCI_SLOT(pdev->devfn));

	/* Allocate card structures to hold addresses, ... */
	card = kzalloc(sizeof(*card), GFP_KERNEL);
	if (!card) {
		dev_err(&pdev->dev, "Unable to allocate memory\n");
		pci_disable_device(pdev);
		return -ENOMEM;
	}

	pci_set_drvdata(pdev, card);

	card->channels = 0;

	/* Remap PLX90xx configuration space */
	addr = pci_iomap(pdev, ci->conf_map.bar, ci->conf_map.size);
	if (!addr) {
		err = -ENOMEM;
		dev_err(&pdev->dev, "Failed to remap configuration space "
			"(BAR%d)\n", ci->conf_map.bar);
		goto failure_cleanup;
	}
	card->conf_addr = addr + ci->conf_map.offset;

	ci->reset_func(pdev);
	card->reset_func = ci->reset_func;

	/* Detect available channels */
	for (i = 0; i < ci->channel_count; i++) {
		struct plx_pci_channel_map *cm = &ci->chan_map_tbl[i];

		dev = alloc_sja1000dev(0);
		if (!dev) {
			err = -ENOMEM;
			goto failure_cleanup;
		}

		card->net_dev[i] = dev;
		priv = netdev_priv(dev);
		priv->priv = card;
		priv->irq_flags = IRQF_SHARED;

		dev->irq = pdev->irq;

		/*
		 * Remap IO space of the SJA1000 chips
		 * This is device-dependent mapping
		 */
		addr = pci_iomap(pdev, cm->bar, cm->size);
		if (!addr) {
			err = -ENOMEM;
			dev_err(&pdev->dev, "Failed to remap BAR%d\n", cm->bar);
			goto failure_cleanup;
		}

		priv->reg_base = addr + cm->offset;
		priv->read_reg = plx_pci_read_reg;
		priv->write_reg = plx_pci_write_reg;

		/* Check if channel is present */
		if (plx_pci_check_sja1000(priv)) {
			priv->can.clock.freq = ci->can_clock;
			priv->ocr = ci->ocr;
			priv->cdr = ci->cdr;

			SET_NETDEV_DEV(dev, &pdev->dev);

			/* Register SJA1000 device */
			err = register_sja1000dev(dev);
			if (err) {
				dev_err(&pdev->dev, "Registering device failed "
					"(err=%d)\n", err);
				goto failure_cleanup;
			}

			card->channels++;

			dev_info(&pdev->dev, "Channel #%d at 0x%p, irq %d "
				 "registered as %s\n", i + 1, priv->reg_base,
				 dev->irq, dev->name);
		} else {
			dev_err(&pdev->dev, "Channel #%d not detected\n",
				i + 1);
			free_sja1000dev(dev);
			card->net_dev[i] = NULL;
		}
	}

	if (!card->channels) {
		err = -ENODEV;
		goto failure_cleanup;
	}

	/*
	 * Enable interrupts from PCI-card (PLX90xx) and enable Local_1,
	 * Local_2 interrupts from the SJA1000 chips
	 */
	if (pdev->device != PCI_DEVICE_ID_PLX_9056) {
		val = ioread32(card->conf_addr + PLX_INTCSR);
		if (pdev->subsystem_vendor == PCI_VENDOR_ID_ESDGMBH)
			val |= PLX_LINT1_EN | PLX_PCI_INT_EN;
		else
			val |= PLX_LINT1_EN | PLX_LINT2_EN | PLX_PCI_INT_EN;
		iowrite32(val, card->conf_addr + PLX_INTCSR);
	} else {
		iowrite32(PLX9056_LINTI | PLX9056_PCI_INT_EN,
			  card->conf_addr + PLX9056_INTCSR);
	}
	return 0;

failure_cleanup:
	dev_err(&pdev->dev, "Error: %d. Cleaning Up.\n", err);

	plx_pci_del_card(pdev);

	return err;
}

static struct pci_driver plx_pci_driver = {
	.name = DRV_NAME,
	.id_table = plx_pci_tbl,
	.probe = plx_pci_add_card,
	.remove = plx_pci_del_card,
};

static int __init plx_pci_init(void)
{
	return pci_register_driver(&plx_pci_driver);
}

static void __exit plx_pci_exit(void)
{
	pci_unregister_driver(&plx_pci_driver);
}

module_init(plx_pci_init);
module_exit(plx_pci_exit);
