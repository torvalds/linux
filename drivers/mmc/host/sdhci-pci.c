/*  linux/drivers/mmc/host/sdhci-pci.c - SDHCI on PCI bus interface
 *
 *  Copyright (C) 2005-2008 Pierre Ossman, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * Thanks to the following companies for their support:
 *
 *     - JMicron (hardware and technical support)
 */

#include <linux/delay.h>
#include <linux/highmem.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>

#include <linux/mmc/host.h>

#include <asm/scatterlist.h>
#include <asm/io.h>

#include "sdhci.h"

/*
 * PCI registers
 */

#define PCI_SDHCI_IFPIO			0x00
#define PCI_SDHCI_IFDMA			0x01
#define PCI_SDHCI_IFVENDOR		0x02

#define PCI_SLOT_INFO			0x40	/* 8 bits */
#define  PCI_SLOT_INFO_SLOTS(x)		((x >> 4) & 7)
#define  PCI_SLOT_INFO_FIRST_BAR_MASK	0x07

#define MAX_SLOTS			8

struct sdhci_pci_chip;

struct sdhci_pci_fixes {
	unsigned int		quirks;

	int			(*probe)(struct sdhci_pci_chip*);
};

struct sdhci_pci_slot {
	struct sdhci_pci_chip	*chip;
	struct sdhci_host	*host;

	int			pci_bar;
};

struct sdhci_pci_chip {
	struct pci_dev		*pdev;

	unsigned int		quirks;
	const struct sdhci_pci_fixes *fixes;

	int			num_slots;	/* Slots on controller */
	struct sdhci_pci_slot	*slots[MAX_SLOTS]; /* Pointers to host slots */
};


/*****************************************************************************\
 *                                                                           *
 * Hardware specific quirk handling                                          *
 *                                                                           *
\*****************************************************************************/

static int ricoh_probe(struct sdhci_pci_chip *chip)
{
	if (chip->pdev->subsystem_vendor == PCI_VENDOR_ID_IBM)
		chip->quirks |= SDHCI_QUIRK_CLOCK_BEFORE_RESET;

	if (chip->pdev->subsystem_vendor == PCI_VENDOR_ID_SAMSUNG)
		chip->quirks |= SDHCI_QUIRK_NO_CARD_NO_RESET;

	return 0;
}

static const struct sdhci_pci_fixes sdhci_ricoh = {
	.probe		= ricoh_probe,
};

static const struct sdhci_pci_fixes sdhci_ene_712 = {
	.quirks		= SDHCI_QUIRK_SINGLE_POWER_WRITE |
			  SDHCI_QUIRK_BROKEN_DMA,
};

static const struct sdhci_pci_fixes sdhci_ene_714 = {
	.quirks		= SDHCI_QUIRK_SINGLE_POWER_WRITE |
			  SDHCI_QUIRK_RESET_CMD_DATA_ON_IOS |
			  SDHCI_QUIRK_BROKEN_DMA,
};

static const struct sdhci_pci_fixes sdhci_cafe = {
	.quirks		= SDHCI_QUIRK_NO_SIMULT_VDD_AND_POWER |
			  SDHCI_QUIRK_BROKEN_TIMEOUT_VAL,
};

static const struct sdhci_pci_fixes sdhci_jmicron = {
	.quirks		= SDHCI_QUIRK_32BIT_DMA_ADDR |
			  SDHCI_QUIRK_32BIT_DMA_SIZE |
			  SDHCI_QUIRK_RESET_AFTER_REQUEST,
};

static const struct pci_device_id pci_ids[] __devinitdata = {
	{
		.vendor		= PCI_VENDOR_ID_RICOH,
		.device		= PCI_DEVICE_ID_RICOH_R5C822,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (kernel_ulong_t)&sdhci_ricoh,
	},

	{
		.vendor		= PCI_VENDOR_ID_ENE,
		.device		= PCI_DEVICE_ID_ENE_CB712_SD,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (kernel_ulong_t)&sdhci_ene_712,
	},

	{
		.vendor		= PCI_VENDOR_ID_ENE,
		.device		= PCI_DEVICE_ID_ENE_CB712_SD_2,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (kernel_ulong_t)&sdhci_ene_712,
	},

	{
		.vendor		= PCI_VENDOR_ID_ENE,
		.device		= PCI_DEVICE_ID_ENE_CB714_SD,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (kernel_ulong_t)&sdhci_ene_714,
	},

	{
		.vendor		= PCI_VENDOR_ID_ENE,
		.device		= PCI_DEVICE_ID_ENE_CB714_SD_2,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (kernel_ulong_t)&sdhci_ene_714,
	},

	{
		.vendor         = PCI_VENDOR_ID_MARVELL,
		.device         = PCI_DEVICE_ID_MARVELL_CAFE_SD,
		.subvendor      = PCI_ANY_ID,
		.subdevice      = PCI_ANY_ID,
		.driver_data    = (kernel_ulong_t)&sdhci_cafe,
	},

	{
		.vendor		= PCI_VENDOR_ID_JMICRON,
		.device		= PCI_DEVICE_ID_JMICRON_JMB38X_SD,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (kernel_ulong_t)&sdhci_jmicron,
	},

	{	/* Generic SD host controller */
		PCI_DEVICE_CLASS((PCI_CLASS_SYSTEM_SDHCI << 8), 0xFFFF00)
	},

	{ /* end: all zeroes */ },
};

MODULE_DEVICE_TABLE(pci, pci_ids);

/*****************************************************************************\
 *                                                                           *
 * SDHCI core callbacks                                                      *
 *                                                                           *
\*****************************************************************************/

static int sdhci_pci_enable_dma(struct sdhci_host *host)
{
	struct sdhci_pci_slot *slot;
	struct pci_dev *pdev;
	int ret;

	slot = sdhci_priv(host);
	pdev = slot->chip->pdev;

	if (((pdev->class & 0xFFFF00) == (PCI_CLASS_SYSTEM_SDHCI << 8)) &&
		((pdev->class & 0x0000FF) != PCI_SDHCI_IFDMA) &&
		(host->flags & SDHCI_USE_DMA)) {
		dev_warn(&pdev->dev, "Will use DMA mode even though HW "
			"doesn't fully claim to support it.\n");
	}

	ret = pci_set_dma_mask(pdev, DMA_32BIT_MASK);
	if (ret)
		return ret;

	pci_set_master(pdev);

	return 0;
}

static struct sdhci_ops sdhci_pci_ops = {
	.enable_dma	= sdhci_pci_enable_dma,
};

/*****************************************************************************\
 *                                                                           *
 * Suspend/resume                                                            *
 *                                                                           *
\*****************************************************************************/

#ifdef CONFIG_PM

static int sdhci_pci_suspend (struct pci_dev *pdev, pm_message_t state)
{
	struct sdhci_pci_chip *chip;
	struct sdhci_pci_slot *slot;
	int i, ret;

	chip = pci_get_drvdata(pdev);
	if (!chip)
		return 0;

	for (i = 0;i < chip->num_slots;i++) {
		slot = chip->slots[i];
		if (!slot)
			continue;

		ret = sdhci_suspend_host(slot->host, state);

		if (ret) {
			for (i--;i >= 0;i--)
				sdhci_resume_host(chip->slots[i]->host);
			return ret;
		}
	}

	pci_save_state(pdev);
	pci_enable_wake(pdev, pci_choose_state(pdev, state), 0);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, pci_choose_state(pdev, state));

	return 0;
}

static int sdhci_pci_resume (struct pci_dev *pdev)
{
	struct sdhci_pci_chip *chip;
	struct sdhci_pci_slot *slot;
	int i, ret;

	chip = pci_get_drvdata(pdev);
	if (!chip)
		return 0;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	ret = pci_enable_device(pdev);
	if (ret)
		return ret;

	for (i = 0;i < chip->num_slots;i++) {
		slot = chip->slots[i];
		if (!slot)
			continue;

		ret = sdhci_resume_host(slot->host);
		if (ret)
			return ret;
	}

	return 0;
}

#else /* CONFIG_PM */

#define sdhci_pci_suspend NULL
#define sdhci_pci_resume NULL

#endif /* CONFIG_PM */

/*****************************************************************************\
 *                                                                           *
 * Device probing/removal                                                    *
 *                                                                           *
\*****************************************************************************/

static struct sdhci_pci_slot * __devinit sdhci_pci_probe_slot(
	struct pci_dev *pdev, struct sdhci_pci_chip *chip, int bar)
{
	struct sdhci_pci_slot *slot;
	struct sdhci_host *host;

	resource_size_t addr;

	int ret;

	if (!(pci_resource_flags(pdev, bar) & IORESOURCE_MEM)) {
		dev_err(&pdev->dev, "BAR %d is not iomem. Aborting.\n", bar);
		return ERR_PTR(-ENODEV);
	}

	if (pci_resource_len(pdev, bar) != 0x100) {
		dev_err(&pdev->dev, "Invalid iomem size. You may "
			"experience problems.\n");
	}

	if ((pdev->class & 0x0000FF) == PCI_SDHCI_IFVENDOR) {
		dev_err(&pdev->dev, "Vendor specific interface. Aborting.\n");
		return ERR_PTR(-ENODEV);
	}

	if ((pdev->class & 0x0000FF) > PCI_SDHCI_IFVENDOR) {
		dev_err(&pdev->dev, "Unknown interface. Aborting.\n");
		return ERR_PTR(-ENODEV);
	}

	host = sdhci_alloc_host(&pdev->dev, sizeof(struct sdhci_pci_slot));
	if (IS_ERR(host)) {
		ret = PTR_ERR(host);
		goto unmap;
	}

	slot = sdhci_priv(host);

	slot->chip = chip;
	slot->host = host;
	slot->pci_bar = bar;

	host->hw_name = "PCI";
	host->ops = &sdhci_pci_ops;
	host->quirks = chip->quirks;

	host->irq = pdev->irq;

	ret = pci_request_region(pdev, bar, mmc_hostname(host->mmc));
	if (ret) {
		dev_err(&pdev->dev, "cannot request region\n");
		return ERR_PTR(ret);
	}

	addr = pci_resource_start(pdev, bar);
	host->ioaddr = ioremap_nocache(addr, pci_resource_len(pdev, bar));
	if (!host->ioaddr) {
		dev_err(&pdev->dev, "failed to remap registers\n");
		goto release;
	}

	ret = sdhci_add_host(host);
	if (ret)
		goto unmap;

	return slot;

unmap:
	iounmap(host->ioaddr);

release:
	pci_release_region(pdev, bar);
	sdhci_free_host(host);

	return ERR_PTR(ret);
}

static void sdhci_pci_remove_slot(struct sdhci_pci_slot *slot)
{
	sdhci_remove_host(slot->host);
	pci_release_region(slot->chip->pdev, slot->pci_bar);
	sdhci_free_host(slot->host);
}

static int __devinit sdhci_pci_probe(struct pci_dev *pdev,
				     const struct pci_device_id *ent)
{
	struct sdhci_pci_chip *chip;
	struct sdhci_pci_slot *slot;

	u8 slots, rev, first_bar;
	int ret, i;

	BUG_ON(pdev == NULL);
	BUG_ON(ent == NULL);

	pci_read_config_byte(pdev, PCI_CLASS_REVISION, &rev);

	dev_info(&pdev->dev, "SDHCI controller found [%04x:%04x] (rev %x)\n",
		 (int)pdev->vendor, (int)pdev->device, (int)rev);

	ret = pci_read_config_byte(pdev, PCI_SLOT_INFO, &slots);
	if (ret)
		return ret;

	slots = PCI_SLOT_INFO_SLOTS(slots) + 1;
	dev_dbg(&pdev->dev, "found %d slot(s)\n", slots);
	if (slots == 0)
		return -ENODEV;

	BUG_ON(slots > MAX_SLOTS);

	ret = pci_read_config_byte(pdev, PCI_SLOT_INFO, &first_bar);
	if (ret)
		return ret;

	first_bar &= PCI_SLOT_INFO_FIRST_BAR_MASK;

	if (first_bar > 5) {
		dev_err(&pdev->dev, "Invalid first BAR. Aborting.\n");
		return -ENODEV;
	}

	ret = pci_enable_device(pdev);
	if (ret)
		return ret;

	chip = kzalloc(sizeof(struct sdhci_pci_chip), GFP_KERNEL);
	if (!chip) {
		ret = -ENOMEM;
		goto err;
	}

	chip->pdev = pdev;
	chip->fixes = (const struct sdhci_pci_fixes*)ent->driver_data;
	if (chip->fixes)
		chip->quirks = chip->fixes->quirks;
	chip->num_slots = slots;

	pci_set_drvdata(pdev, chip);

	if (chip->fixes && chip->fixes->probe) {
		ret = chip->fixes->probe(chip);
		if (ret)
			goto free;
	}

	for (i = 0;i < slots;i++) {
		slot = sdhci_pci_probe_slot(pdev, chip, first_bar + i);
		if (IS_ERR(slot)) {
			for (i--;i >= 0;i--)
				sdhci_pci_remove_slot(chip->slots[i]);
			ret = PTR_ERR(slot);
			goto free;
		}

		chip->slots[i] = slot;
	}

	return 0;

free:
	pci_set_drvdata(pdev, NULL);
	kfree(chip);

err:
	pci_disable_device(pdev);
	return ret;
}

static void __devexit sdhci_pci_remove(struct pci_dev *pdev)
{
	int i;
	struct sdhci_pci_chip *chip;

	chip = pci_get_drvdata(pdev);

	if (chip) {
		for (i = 0;i < chip->num_slots; i++)
			sdhci_pci_remove_slot(chip->slots[i]);

		pci_set_drvdata(pdev, NULL);
		kfree(chip);
	}

	pci_disable_device(pdev);
}

static struct pci_driver sdhci_driver = {
	.name = 	"sdhci-pci",
	.id_table =	pci_ids,
	.probe = 	sdhci_pci_probe,
	.remove =	__devexit_p(sdhci_pci_remove),
	.suspend =	sdhci_pci_suspend,
	.resume	=	sdhci_pci_resume,
};

/*****************************************************************************\
 *                                                                           *
 * Driver init/exit                                                          *
 *                                                                           *
\*****************************************************************************/

static int __init sdhci_drv_init(void)
{
	return pci_register_driver(&sdhci_driver);
}

static void __exit sdhci_drv_exit(void)
{
	pci_unregister_driver(&sdhci_driver);
}

module_init(sdhci_drv_init);
module_exit(sdhci_drv_exit);

MODULE_AUTHOR("Pierre Ossman <drzeus@drzeus.cx>");
MODULE_DESCRIPTION("Secure Digital Host Controller Interface PCI driver");
MODULE_LICENSE("GPL");
