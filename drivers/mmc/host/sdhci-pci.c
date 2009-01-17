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
struct sdhci_pci_slot;

struct sdhci_pci_fixes {
	unsigned int		quirks;

	int			(*probe)(struct sdhci_pci_chip*);

	int			(*probe_slot)(struct sdhci_pci_slot*);
	void			(*remove_slot)(struct sdhci_pci_slot*, int);

	int			(*suspend)(struct sdhci_pci_chip*,
					pm_message_t);
	int			(*resume)(struct sdhci_pci_chip*);
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
	.quirks		= SDHCI_QUIRK_32BIT_DMA_ADDR,
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

static int jmicron_pmos(struct sdhci_pci_chip *chip, int on)
{
	u8 scratch;
	int ret;

	ret = pci_read_config_byte(chip->pdev, 0xAE, &scratch);
	if (ret)
		return ret;

	/*
	 * Turn PMOS on [bit 0], set over current detection to 2.4 V
	 * [bit 1:2] and enable over current debouncing [bit 6].
	 */
	if (on)
		scratch |= 0x47;
	else
		scratch &= ~0x47;

	ret = pci_write_config_byte(chip->pdev, 0xAE, scratch);
	if (ret)
		return ret;

	return 0;
}

static int jmicron_probe(struct sdhci_pci_chip *chip)
{
	int ret;

	if (chip->pdev->revision == 0) {
		chip->quirks |= SDHCI_QUIRK_32BIT_DMA_ADDR |
			  SDHCI_QUIRK_32BIT_DMA_SIZE |
			  SDHCI_QUIRK_32BIT_ADMA_SIZE |
			  SDHCI_QUIRK_RESET_AFTER_REQUEST |
			  SDHCI_QUIRK_BROKEN_SMALL_PIO |
			  SDHCI_QUIRK_FORCE_HIGHSPEED;
	}

	/*
	 * JMicron chips can have two interfaces to the same hardware
	 * in order to work around limitations in Microsoft's driver.
	 * We need to make sure we only bind to one of them.
	 *
	 * This code assumes two things:
	 *
	 * 1. The PCI code adds subfunctions in order.
	 *
	 * 2. The MMC interface has a lower subfunction number
	 *    than the SD interface.
	 */
	if (chip->pdev->device == PCI_DEVICE_ID_JMICRON_JMB38X_SD) {
		struct pci_dev *sd_dev;

		sd_dev = NULL;
		while ((sd_dev = pci_get_device(PCI_VENDOR_ID_JMICRON,
			PCI_DEVICE_ID_JMICRON_JMB38X_MMC, sd_dev)) != NULL) {
			if ((PCI_SLOT(chip->pdev->devfn) ==
				PCI_SLOT(sd_dev->devfn)) &&
				(chip->pdev->bus == sd_dev->bus))
				break;
		}

		if (sd_dev) {
			pci_dev_put(sd_dev);
			dev_info(&chip->pdev->dev, "Refusing to bind to "
				"secondary interface.\n");
			return -ENODEV;
		}
	}

	/*
	 * JMicron chips need a bit of a nudge to enable the power
	 * output pins.
	 */
	ret = jmicron_pmos(chip, 1);
	if (ret) {
		dev_err(&chip->pdev->dev, "Failure enabling card power\n");
		return ret;
	}

	return 0;
}

static void jmicron_enable_mmc(struct sdhci_host *host, int on)
{
	u8 scratch;

	scratch = readb(host->ioaddr + 0xC0);

	if (on)
		scratch |= 0x01;
	else
		scratch &= ~0x01;

	writeb(scratch, host->ioaddr + 0xC0);
}

static int jmicron_probe_slot(struct sdhci_pci_slot *slot)
{
	if (slot->chip->pdev->revision == 0) {
		u16 version;

		version = readl(slot->host->ioaddr + SDHCI_HOST_VERSION);
		version = (version & SDHCI_VENDOR_VER_MASK) >>
			SDHCI_VENDOR_VER_SHIFT;

		/*
		 * Older versions of the chip have lots of nasty glitches
		 * in the ADMA engine. It's best just to avoid it
		 * completely.
		 */
		if (version < 0xAC)
			slot->host->quirks |= SDHCI_QUIRK_BROKEN_ADMA;
	}

	/*
	 * The secondary interface requires a bit set to get the
	 * interrupts.
	 */
	if (slot->chip->pdev->device == PCI_DEVICE_ID_JMICRON_JMB38X_MMC)
		jmicron_enable_mmc(slot->host, 1);

	return 0;
}

static void jmicron_remove_slot(struct sdhci_pci_slot *slot, int dead)
{
	if (dead)
		return;

	if (slot->chip->pdev->device == PCI_DEVICE_ID_JMICRON_JMB38X_MMC)
		jmicron_enable_mmc(slot->host, 0);
}

static int jmicron_suspend(struct sdhci_pci_chip *chip, pm_message_t state)
{
	int i;

	if (chip->pdev->device == PCI_DEVICE_ID_JMICRON_JMB38X_MMC) {
		for (i = 0;i < chip->num_slots;i++)
			jmicron_enable_mmc(chip->slots[i]->host, 0);
	}

	return 0;
}

static int jmicron_resume(struct sdhci_pci_chip *chip)
{
	int ret, i;

	if (chip->pdev->device == PCI_DEVICE_ID_JMICRON_JMB38X_MMC) {
		for (i = 0;i < chip->num_slots;i++)
			jmicron_enable_mmc(chip->slots[i]->host, 1);
	}

	ret = jmicron_pmos(chip, 1);
	if (ret) {
		dev_err(&chip->pdev->dev, "Failure enabling card power\n");
		return ret;
	}

	return 0;
}

static const struct sdhci_pci_fixes sdhci_jmicron = {
	.probe		= jmicron_probe,

	.probe_slot	= jmicron_probe_slot,
	.remove_slot	= jmicron_remove_slot,

	.suspend	= jmicron_suspend,
	.resume		= jmicron_resume,
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
		.device         = PCI_DEVICE_ID_MARVELL_88ALP01_SD,
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

	{
		.vendor		= PCI_VENDOR_ID_JMICRON,
		.device		= PCI_DEVICE_ID_JMICRON_JMB38X_MMC,
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

	if (chip->fixes && chip->fixes->suspend) {
		ret = chip->fixes->suspend(chip, state);
		if (ret) {
			for (i = chip->num_slots - 1;i >= 0;i--)
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

	if (chip->fixes && chip->fixes->resume) {
		ret = chip->fixes->resume(chip);
		if (ret)
			return ret;
	}

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
	host->ioaddr = pci_ioremap_bar(pdev, bar);
	if (!host->ioaddr) {
		dev_err(&pdev->dev, "failed to remap registers\n");
		goto release;
	}

	if (chip->fixes && chip->fixes->probe_slot) {
		ret = chip->fixes->probe_slot(slot);
		if (ret)
			goto unmap;
	}

	ret = sdhci_add_host(host);
	if (ret)
		goto remove;

	return slot;

remove:
	if (chip->fixes && chip->fixes->remove_slot)
		chip->fixes->remove_slot(slot, 0);

unmap:
	iounmap(host->ioaddr);

release:
	pci_release_region(pdev, bar);
	sdhci_free_host(host);

	return ERR_PTR(ret);
}

static void sdhci_pci_remove_slot(struct sdhci_pci_slot *slot)
{
	int dead;
	u32 scratch;

	dead = 0;
	scratch = readl(slot->host->ioaddr + SDHCI_INT_STATUS);
	if (scratch == (u32)-1)
		dead = 1;

	sdhci_remove_host(slot->host, dead);

	if (slot->chip->fixes && slot->chip->fixes->remove_slot)
		slot->chip->fixes->remove_slot(slot, dead);

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
