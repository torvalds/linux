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
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/mmc/host.h>
#include <linux/scatterlist.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/pm_runtime.h>
#include <linux/mmc/sdhci-pci-data.h>

#include "sdhci.h"

/*
 * PCI device IDs
 */
#define PCI_DEVICE_ID_INTEL_PCH_SDIO0	0x8809
#define PCI_DEVICE_ID_INTEL_PCH_SDIO1	0x880a

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
	unsigned int		quirks2;
	bool			allow_runtime_pm;

	int			(*probe) (struct sdhci_pci_chip *);

	int			(*probe_slot) (struct sdhci_pci_slot *);
	void			(*remove_slot) (struct sdhci_pci_slot *, int);

	int			(*suspend) (struct sdhci_pci_chip *);
	int			(*resume) (struct sdhci_pci_chip *);
};

struct sdhci_pci_slot {
	struct sdhci_pci_chip	*chip;
	struct sdhci_host	*host;
	struct sdhci_pci_data	*data;

	int			pci_bar;
	int			rst_n_gpio;
	int			cd_gpio;
	int			cd_irq;
};

struct sdhci_pci_chip {
	struct pci_dev		*pdev;

	unsigned int		quirks;
	unsigned int		quirks2;
	bool			allow_runtime_pm;
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
	if (chip->pdev->subsystem_vendor == PCI_VENDOR_ID_SAMSUNG ||
	    chip->pdev->subsystem_vendor == PCI_VENDOR_ID_SONY)
		chip->quirks |= SDHCI_QUIRK_NO_CARD_NO_RESET;
	return 0;
}

static int ricoh_mmc_probe_slot(struct sdhci_pci_slot *slot)
{
	slot->host->caps =
		((0x21 << SDHCI_TIMEOUT_CLK_SHIFT)
			& SDHCI_TIMEOUT_CLK_MASK) |

		((0x21 << SDHCI_CLOCK_BASE_SHIFT)
			& SDHCI_CLOCK_BASE_MASK) |

		SDHCI_TIMEOUT_CLK_UNIT |
		SDHCI_CAN_VDD_330 |
		SDHCI_CAN_DO_SDMA;
	return 0;
}

static int ricoh_mmc_resume(struct sdhci_pci_chip *chip)
{
	/* Apply a delay to allow controller to settle */
	/* Otherwise it becomes confused if card state changed
		during suspend */
	msleep(500);
	return 0;
}

static const struct sdhci_pci_fixes sdhci_ricoh = {
	.probe		= ricoh_probe,
	.quirks		= SDHCI_QUIRK_32BIT_DMA_ADDR |
			  SDHCI_QUIRK_FORCE_DMA |
			  SDHCI_QUIRK_CLOCK_BEFORE_RESET,
};

static const struct sdhci_pci_fixes sdhci_ricoh_mmc = {
	.probe_slot	= ricoh_mmc_probe_slot,
	.resume		= ricoh_mmc_resume,
	.quirks		= SDHCI_QUIRK_32BIT_DMA_ADDR |
			  SDHCI_QUIRK_CLOCK_BEFORE_RESET |
			  SDHCI_QUIRK_NO_CARD_NO_RESET |
			  SDHCI_QUIRK_MISSING_CAPS
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
			  SDHCI_QUIRK_NO_BUSY_IRQ |
			  SDHCI_QUIRK_BROKEN_CARD_DETECTION |
			  SDHCI_QUIRK_BROKEN_TIMEOUT_VAL,
};

static int mrst_hc_probe_slot(struct sdhci_pci_slot *slot)
{
	slot->host->mmc->caps |= MMC_CAP_8_BIT_DATA;
	return 0;
}

/*
 * ADMA operation is disabled for Moorestown platform due to
 * hardware bugs.
 */
static int mrst_hc_probe(struct sdhci_pci_chip *chip)
{
	/*
	 * slots number is fixed here for MRST as SDIO3/5 are never used and
	 * have hardware bugs.
	 */
	chip->num_slots = 1;
	return 0;
}

static int pch_hc_probe_slot(struct sdhci_pci_slot *slot)
{
	slot->host->mmc->caps |= MMC_CAP_8_BIT_DATA;
	return 0;
}

#ifdef CONFIG_PM_RUNTIME

static irqreturn_t sdhci_pci_sd_cd(int irq, void *dev_id)
{
	struct sdhci_pci_slot *slot = dev_id;
	struct sdhci_host *host = slot->host;

	mmc_detect_change(host->mmc, msecs_to_jiffies(200));
	return IRQ_HANDLED;
}

static void sdhci_pci_add_own_cd(struct sdhci_pci_slot *slot)
{
	int err, irq, gpio = slot->cd_gpio;

	slot->cd_gpio = -EINVAL;
	slot->cd_irq = -EINVAL;

	if (!gpio_is_valid(gpio))
		return;

	err = gpio_request(gpio, "sd_cd");
	if (err < 0)
		goto out;

	err = gpio_direction_input(gpio);
	if (err < 0)
		goto out_free;

	irq = gpio_to_irq(gpio);
	if (irq < 0)
		goto out_free;

	err = request_irq(irq, sdhci_pci_sd_cd, IRQF_TRIGGER_RISING |
			  IRQF_TRIGGER_FALLING, "sd_cd", slot);
	if (err)
		goto out_free;

	slot->cd_gpio = gpio;
	slot->cd_irq = irq;

	return;

out_free:
	gpio_free(gpio);
out:
	dev_warn(&slot->chip->pdev->dev, "failed to setup card detect wake up\n");
}

static void sdhci_pci_remove_own_cd(struct sdhci_pci_slot *slot)
{
	if (slot->cd_irq >= 0)
		free_irq(slot->cd_irq, slot);
	if (gpio_is_valid(slot->cd_gpio))
		gpio_free(slot->cd_gpio);
}

#else

static inline void sdhci_pci_add_own_cd(struct sdhci_pci_slot *slot)
{
}

static inline void sdhci_pci_remove_own_cd(struct sdhci_pci_slot *slot)
{
}

#endif

static int mfd_emmc_probe_slot(struct sdhci_pci_slot *slot)
{
	slot->host->mmc->caps |= MMC_CAP_8_BIT_DATA | MMC_CAP_NONREMOVABLE;
	slot->host->mmc->caps2 |= MMC_CAP2_BOOTPART_NOACC |
				  MMC_CAP2_HC_ERASE_SZ;
	return 0;
}

static int mfd_sdio_probe_slot(struct sdhci_pci_slot *slot)
{
	slot->host->mmc->caps |= MMC_CAP_POWER_OFF_CARD | MMC_CAP_NONREMOVABLE;
	return 0;
}

static const struct sdhci_pci_fixes sdhci_intel_mrst_hc0 = {
	.quirks		= SDHCI_QUIRK_BROKEN_ADMA | SDHCI_QUIRK_NO_HISPD_BIT,
	.probe_slot	= mrst_hc_probe_slot,
};

static const struct sdhci_pci_fixes sdhci_intel_mrst_hc1_hc2 = {
	.quirks		= SDHCI_QUIRK_BROKEN_ADMA | SDHCI_QUIRK_NO_HISPD_BIT,
	.probe		= mrst_hc_probe,
};

static const struct sdhci_pci_fixes sdhci_intel_mfd_sd = {
	.quirks		= SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC,
	.allow_runtime_pm = true,
};

static const struct sdhci_pci_fixes sdhci_intel_mfd_sdio = {
	.quirks		= SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC,
	.quirks2	= SDHCI_QUIRK2_HOST_OFF_CARD_ON,
	.allow_runtime_pm = true,
	.probe_slot	= mfd_sdio_probe_slot,
};

static const struct sdhci_pci_fixes sdhci_intel_mfd_emmc = {
	.quirks		= SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC,
	.allow_runtime_pm = true,
	.probe_slot	= mfd_emmc_probe_slot,
};

static const struct sdhci_pci_fixes sdhci_intel_pch_sdio = {
	.quirks		= SDHCI_QUIRK_BROKEN_ADMA,
	.probe_slot	= pch_hc_probe_slot,
};

/* O2Micro extra registers */
#define O2_SD_LOCK_WP		0xD3
#define O2_SD_MULTI_VCC3V	0xEE
#define O2_SD_CLKREQ		0xEC
#define O2_SD_CAPS		0xE0
#define O2_SD_ADMA1		0xE2
#define O2_SD_ADMA2		0xE7
#define O2_SD_INF_MOD		0xF1

static int o2_probe(struct sdhci_pci_chip *chip)
{
	int ret;
	u8 scratch;

	switch (chip->pdev->device) {
	case PCI_DEVICE_ID_O2_8220:
	case PCI_DEVICE_ID_O2_8221:
	case PCI_DEVICE_ID_O2_8320:
	case PCI_DEVICE_ID_O2_8321:
		/* This extra setup is required due to broken ADMA. */
		ret = pci_read_config_byte(chip->pdev, O2_SD_LOCK_WP, &scratch);
		if (ret)
			return ret;
		scratch &= 0x7f;
		pci_write_config_byte(chip->pdev, O2_SD_LOCK_WP, scratch);

		/* Set Multi 3 to VCC3V# */
		pci_write_config_byte(chip->pdev, O2_SD_MULTI_VCC3V, 0x08);

		/* Disable CLK_REQ# support after media DET */
		ret = pci_read_config_byte(chip->pdev, O2_SD_CLKREQ, &scratch);
		if (ret)
			return ret;
		scratch |= 0x20;
		pci_write_config_byte(chip->pdev, O2_SD_CLKREQ, scratch);

		/* Choose capabilities, enable SDMA.  We have to write 0x01
		 * to the capabilities register first to unlock it.
		 */
		ret = pci_read_config_byte(chip->pdev, O2_SD_CAPS, &scratch);
		if (ret)
			return ret;
		scratch |= 0x01;
		pci_write_config_byte(chip->pdev, O2_SD_CAPS, scratch);
		pci_write_config_byte(chip->pdev, O2_SD_CAPS, 0x73);

		/* Disable ADMA1/2 */
		pci_write_config_byte(chip->pdev, O2_SD_ADMA1, 0x39);
		pci_write_config_byte(chip->pdev, O2_SD_ADMA2, 0x08);

		/* Disable the infinite transfer mode */
		ret = pci_read_config_byte(chip->pdev, O2_SD_INF_MOD, &scratch);
		if (ret)
			return ret;
		scratch |= 0x08;
		pci_write_config_byte(chip->pdev, O2_SD_INF_MOD, scratch);

		/* Lock WP */
		ret = pci_read_config_byte(chip->pdev, O2_SD_LOCK_WP, &scratch);
		if (ret)
			return ret;
		scratch |= 0x80;
		pci_write_config_byte(chip->pdev, O2_SD_LOCK_WP, scratch);
	}

	return 0;
}

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
	u16 mmcdev = 0;

	if (chip->pdev->revision == 0) {
		chip->quirks |= SDHCI_QUIRK_32BIT_DMA_ADDR |
			  SDHCI_QUIRK_32BIT_DMA_SIZE |
			  SDHCI_QUIRK_32BIT_ADMA_SIZE |
			  SDHCI_QUIRK_RESET_AFTER_REQUEST |
			  SDHCI_QUIRK_BROKEN_SMALL_PIO;
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
	if (chip->pdev->device == PCI_DEVICE_ID_JMICRON_JMB38X_SD)
		mmcdev = PCI_DEVICE_ID_JMICRON_JMB38X_MMC;
	else if (chip->pdev->device == PCI_DEVICE_ID_JMICRON_JMB388_SD)
		mmcdev = PCI_DEVICE_ID_JMICRON_JMB388_ESD;

	if (mmcdev) {
		struct pci_dev *sd_dev;

		sd_dev = NULL;
		while ((sd_dev = pci_get_device(PCI_VENDOR_ID_JMICRON,
						mmcdev, sd_dev)) != NULL) {
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

	/* quirk for unsable RO-detection on JM388 chips */
	if (chip->pdev->device == PCI_DEVICE_ID_JMICRON_JMB388_SD ||
	    chip->pdev->device == PCI_DEVICE_ID_JMICRON_JMB388_ESD)
		chip->quirks |= SDHCI_QUIRK_UNSTABLE_RO_DETECT;

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

	/* JM388 MMC doesn't support 1.8V while SD supports it */
	if (slot->chip->pdev->device == PCI_DEVICE_ID_JMICRON_JMB388_ESD) {
		slot->host->ocr_avail_sd = MMC_VDD_32_33 | MMC_VDD_33_34 |
			MMC_VDD_29_30 | MMC_VDD_30_31 |
			MMC_VDD_165_195; /* allow 1.8V */
		slot->host->ocr_avail_mmc = MMC_VDD_32_33 | MMC_VDD_33_34 |
			MMC_VDD_29_30 | MMC_VDD_30_31; /* no 1.8V for MMC */
	}

	/*
	 * The secondary interface requires a bit set to get the
	 * interrupts.
	 */
	if (slot->chip->pdev->device == PCI_DEVICE_ID_JMICRON_JMB38X_MMC ||
	    slot->chip->pdev->device == PCI_DEVICE_ID_JMICRON_JMB388_ESD)
		jmicron_enable_mmc(slot->host, 1);

	slot->host->mmc->caps |= MMC_CAP_BUS_WIDTH_TEST;

	return 0;
}

static void jmicron_remove_slot(struct sdhci_pci_slot *slot, int dead)
{
	if (dead)
		return;

	if (slot->chip->pdev->device == PCI_DEVICE_ID_JMICRON_JMB38X_MMC ||
	    slot->chip->pdev->device == PCI_DEVICE_ID_JMICRON_JMB388_ESD)
		jmicron_enable_mmc(slot->host, 0);
}

static int jmicron_suspend(struct sdhci_pci_chip *chip)
{
	int i;

	if (chip->pdev->device == PCI_DEVICE_ID_JMICRON_JMB38X_MMC ||
	    chip->pdev->device == PCI_DEVICE_ID_JMICRON_JMB388_ESD) {
		for (i = 0; i < chip->num_slots; i++)
			jmicron_enable_mmc(chip->slots[i]->host, 0);
	}

	return 0;
}

static int jmicron_resume(struct sdhci_pci_chip *chip)
{
	int ret, i;

	if (chip->pdev->device == PCI_DEVICE_ID_JMICRON_JMB38X_MMC ||
	    chip->pdev->device == PCI_DEVICE_ID_JMICRON_JMB388_ESD) {
		for (i = 0; i < chip->num_slots; i++)
			jmicron_enable_mmc(chip->slots[i]->host, 1);
	}

	ret = jmicron_pmos(chip, 1);
	if (ret) {
		dev_err(&chip->pdev->dev, "Failure enabling card power\n");
		return ret;
	}

	return 0;
}

static const struct sdhci_pci_fixes sdhci_o2 = {
	.probe		= o2_probe,
};

static const struct sdhci_pci_fixes sdhci_jmicron = {
	.probe		= jmicron_probe,

	.probe_slot	= jmicron_probe_slot,
	.remove_slot	= jmicron_remove_slot,

	.suspend	= jmicron_suspend,
	.resume		= jmicron_resume,
};

/* SysKonnect CardBus2SDIO extra registers */
#define SYSKT_CTRL		0x200
#define SYSKT_RDFIFO_STAT	0x204
#define SYSKT_WRFIFO_STAT	0x208
#define SYSKT_POWER_DATA	0x20c
#define   SYSKT_POWER_330	0xef
#define   SYSKT_POWER_300	0xf8
#define   SYSKT_POWER_184	0xcc
#define SYSKT_POWER_CMD		0x20d
#define   SYSKT_POWER_START	(1 << 7)
#define SYSKT_POWER_STATUS	0x20e
#define   SYSKT_POWER_STATUS_OK	(1 << 0)
#define SYSKT_BOARD_REV		0x210
#define SYSKT_CHIP_REV		0x211
#define SYSKT_CONF_DATA		0x212
#define   SYSKT_CONF_DATA_1V8	(1 << 2)
#define   SYSKT_CONF_DATA_2V5	(1 << 1)
#define   SYSKT_CONF_DATA_3V3	(1 << 0)

static int syskt_probe(struct sdhci_pci_chip *chip)
{
	if ((chip->pdev->class & 0x0000FF) == PCI_SDHCI_IFVENDOR) {
		chip->pdev->class &= ~0x0000FF;
		chip->pdev->class |= PCI_SDHCI_IFDMA;
	}
	return 0;
}

static int syskt_probe_slot(struct sdhci_pci_slot *slot)
{
	int tm, ps;

	u8 board_rev = readb(slot->host->ioaddr + SYSKT_BOARD_REV);
	u8  chip_rev = readb(slot->host->ioaddr + SYSKT_CHIP_REV);
	dev_info(&slot->chip->pdev->dev, "SysKonnect CardBus2SDIO, "
					 "board rev %d.%d, chip rev %d.%d\n",
					 board_rev >> 4, board_rev & 0xf,
					 chip_rev >> 4,  chip_rev & 0xf);
	if (chip_rev >= 0x20)
		slot->host->quirks |= SDHCI_QUIRK_FORCE_DMA;

	writeb(SYSKT_POWER_330, slot->host->ioaddr + SYSKT_POWER_DATA);
	writeb(SYSKT_POWER_START, slot->host->ioaddr + SYSKT_POWER_CMD);
	udelay(50);
	tm = 10;  /* Wait max 1 ms */
	do {
		ps = readw(slot->host->ioaddr + SYSKT_POWER_STATUS);
		if (ps & SYSKT_POWER_STATUS_OK)
			break;
		udelay(100);
	} while (--tm);
	if (!tm) {
		dev_err(&slot->chip->pdev->dev,
			"power regulator never stabilized");
		writeb(0, slot->host->ioaddr + SYSKT_POWER_CMD);
		return -ENODEV;
	}

	return 0;
}

static const struct sdhci_pci_fixes sdhci_syskt = {
	.quirks		= SDHCI_QUIRK_NO_SIMULT_VDD_AND_POWER,
	.probe		= syskt_probe,
	.probe_slot	= syskt_probe_slot,
};

static int via_probe(struct sdhci_pci_chip *chip)
{
	if (chip->pdev->revision == 0x10)
		chip->quirks |= SDHCI_QUIRK_DELAY_AFTER_POWER;

	return 0;
}

static const struct sdhci_pci_fixes sdhci_via = {
	.probe		= via_probe,
};

static const struct pci_device_id pci_ids[] __devinitconst = {
	{
		.vendor		= PCI_VENDOR_ID_RICOH,
		.device		= PCI_DEVICE_ID_RICOH_R5C822,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (kernel_ulong_t)&sdhci_ricoh,
	},

	{
		.vendor         = PCI_VENDOR_ID_RICOH,
		.device         = 0x843,
		.subvendor      = PCI_ANY_ID,
		.subdevice      = PCI_ANY_ID,
		.driver_data    = (kernel_ulong_t)&sdhci_ricoh_mmc,
	},

	{
		.vendor         = PCI_VENDOR_ID_RICOH,
		.device         = 0xe822,
		.subvendor      = PCI_ANY_ID,
		.subdevice      = PCI_ANY_ID,
		.driver_data    = (kernel_ulong_t)&sdhci_ricoh_mmc,
	},

	{
		.vendor         = PCI_VENDOR_ID_RICOH,
		.device         = 0xe823,
		.subvendor      = PCI_ANY_ID,
		.subdevice      = PCI_ANY_ID,
		.driver_data    = (kernel_ulong_t)&sdhci_ricoh_mmc,
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

	{
		.vendor		= PCI_VENDOR_ID_JMICRON,
		.device		= PCI_DEVICE_ID_JMICRON_JMB388_SD,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (kernel_ulong_t)&sdhci_jmicron,
	},

	{
		.vendor		= PCI_VENDOR_ID_JMICRON,
		.device		= PCI_DEVICE_ID_JMICRON_JMB388_ESD,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (kernel_ulong_t)&sdhci_jmicron,
	},

	{
		.vendor		= PCI_VENDOR_ID_SYSKONNECT,
		.device		= 0x8000,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (kernel_ulong_t)&sdhci_syskt,
	},

	{
		.vendor		= PCI_VENDOR_ID_VIA,
		.device		= 0x95d0,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (kernel_ulong_t)&sdhci_via,
	},

	{
		.vendor		= PCI_VENDOR_ID_INTEL,
		.device		= PCI_DEVICE_ID_INTEL_MRST_SD0,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (kernel_ulong_t)&sdhci_intel_mrst_hc0,
	},

	{
		.vendor		= PCI_VENDOR_ID_INTEL,
		.device		= PCI_DEVICE_ID_INTEL_MRST_SD1,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (kernel_ulong_t)&sdhci_intel_mrst_hc1_hc2,
	},

	{
		.vendor		= PCI_VENDOR_ID_INTEL,
		.device		= PCI_DEVICE_ID_INTEL_MRST_SD2,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (kernel_ulong_t)&sdhci_intel_mrst_hc1_hc2,
	},

	{
		.vendor		= PCI_VENDOR_ID_INTEL,
		.device		= PCI_DEVICE_ID_INTEL_MFD_SD,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (kernel_ulong_t)&sdhci_intel_mfd_sd,
	},

	{
		.vendor		= PCI_VENDOR_ID_INTEL,
		.device		= PCI_DEVICE_ID_INTEL_MFD_SDIO1,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (kernel_ulong_t)&sdhci_intel_mfd_sdio,
	},

	{
		.vendor		= PCI_VENDOR_ID_INTEL,
		.device		= PCI_DEVICE_ID_INTEL_MFD_SDIO2,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (kernel_ulong_t)&sdhci_intel_mfd_sdio,
	},

	{
		.vendor		= PCI_VENDOR_ID_INTEL,
		.device		= PCI_DEVICE_ID_INTEL_MFD_EMMC0,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (kernel_ulong_t)&sdhci_intel_mfd_emmc,
	},

	{
		.vendor		= PCI_VENDOR_ID_INTEL,
		.device		= PCI_DEVICE_ID_INTEL_MFD_EMMC1,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (kernel_ulong_t)&sdhci_intel_mfd_emmc,
	},

	{
		.vendor		= PCI_VENDOR_ID_INTEL,
		.device		= PCI_DEVICE_ID_INTEL_PCH_SDIO0,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (kernel_ulong_t)&sdhci_intel_pch_sdio,
	},

	{
		.vendor		= PCI_VENDOR_ID_INTEL,
		.device		= PCI_DEVICE_ID_INTEL_PCH_SDIO1,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (kernel_ulong_t)&sdhci_intel_pch_sdio,
	},

	{
		.vendor		= PCI_VENDOR_ID_O2,
		.device		= PCI_DEVICE_ID_O2_8120,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (kernel_ulong_t)&sdhci_o2,
	},

	{
		.vendor		= PCI_VENDOR_ID_O2,
		.device		= PCI_DEVICE_ID_O2_8220,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (kernel_ulong_t)&sdhci_o2,
	},

	{
		.vendor		= PCI_VENDOR_ID_O2,
		.device		= PCI_DEVICE_ID_O2_8221,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (kernel_ulong_t)&sdhci_o2,
	},

	{
		.vendor		= PCI_VENDOR_ID_O2,
		.device		= PCI_DEVICE_ID_O2_8320,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (kernel_ulong_t)&sdhci_o2,
	},

	{
		.vendor		= PCI_VENDOR_ID_O2,
		.device		= PCI_DEVICE_ID_O2_8321,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (kernel_ulong_t)&sdhci_o2,
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
		(host->flags & SDHCI_USE_SDMA)) {
		dev_warn(&pdev->dev, "Will use DMA mode even though HW "
			"doesn't fully claim to support it.\n");
	}

	ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	pci_set_master(pdev);

	return 0;
}

static int sdhci_pci_8bit_width(struct sdhci_host *host, int width)
{
	u8 ctrl;

	ctrl = sdhci_readb(host, SDHCI_HOST_CONTROL);

	switch (width) {
	case MMC_BUS_WIDTH_8:
		ctrl |= SDHCI_CTRL_8BITBUS;
		ctrl &= ~SDHCI_CTRL_4BITBUS;
		break;
	case MMC_BUS_WIDTH_4:
		ctrl |= SDHCI_CTRL_4BITBUS;
		ctrl &= ~SDHCI_CTRL_8BITBUS;
		break;
	default:
		ctrl &= ~(SDHCI_CTRL_8BITBUS | SDHCI_CTRL_4BITBUS);
		break;
	}

	sdhci_writeb(host, ctrl, SDHCI_HOST_CONTROL);

	return 0;
}

static void sdhci_pci_hw_reset(struct sdhci_host *host)
{
	struct sdhci_pci_slot *slot = sdhci_priv(host);
	int rst_n_gpio = slot->rst_n_gpio;

	if (!gpio_is_valid(rst_n_gpio))
		return;
	gpio_set_value_cansleep(rst_n_gpio, 0);
	/* For eMMC, minimum is 1us but give it 10us for good measure */
	udelay(10);
	gpio_set_value_cansleep(rst_n_gpio, 1);
	/* For eMMC, minimum is 200us but give it 300us for good measure */
	usleep_range(300, 1000);
}

static struct sdhci_ops sdhci_pci_ops = {
	.enable_dma	= sdhci_pci_enable_dma,
	.platform_8bit_width	= sdhci_pci_8bit_width,
	.hw_reset		= sdhci_pci_hw_reset,
};

/*****************************************************************************\
 *                                                                           *
 * Suspend/resume                                                            *
 *                                                                           *
\*****************************************************************************/

#ifdef CONFIG_PM

static int sdhci_pci_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct sdhci_pci_chip *chip;
	struct sdhci_pci_slot *slot;
	mmc_pm_flag_t slot_pm_flags;
	mmc_pm_flag_t pm_flags = 0;
	int i, ret;

	chip = pci_get_drvdata(pdev);
	if (!chip)
		return 0;

	for (i = 0; i < chip->num_slots; i++) {
		slot = chip->slots[i];
		if (!slot)
			continue;

		ret = sdhci_suspend_host(slot->host);

		if (ret)
			goto err_pci_suspend;

		slot_pm_flags = slot->host->mmc->pm_flags;
		if (slot_pm_flags & MMC_PM_WAKE_SDIO_IRQ)
			sdhci_enable_irq_wakeups(slot->host);

		pm_flags |= slot_pm_flags;
	}

	if (chip->fixes && chip->fixes->suspend) {
		ret = chip->fixes->suspend(chip);
		if (ret)
			goto err_pci_suspend;
	}

	pci_save_state(pdev);
	if (pm_flags & MMC_PM_KEEP_POWER) {
		if (pm_flags & MMC_PM_WAKE_SDIO_IRQ) {
			pci_pme_active(pdev, true);
			pci_enable_wake(pdev, PCI_D3hot, 1);
		}
		pci_set_power_state(pdev, PCI_D3hot);
	} else {
		pci_enable_wake(pdev, PCI_D3hot, 0);
		pci_disable_device(pdev);
		pci_set_power_state(pdev, PCI_D3hot);
	}

	return 0;

err_pci_suspend:
	while (--i >= 0)
		sdhci_resume_host(chip->slots[i]->host);
	return ret;
}

static int sdhci_pci_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
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

	for (i = 0; i < chip->num_slots; i++) {
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

#ifdef CONFIG_PM_RUNTIME

static int sdhci_pci_runtime_suspend(struct device *dev)
{
	struct pci_dev *pdev = container_of(dev, struct pci_dev, dev);
	struct sdhci_pci_chip *chip;
	struct sdhci_pci_slot *slot;
	int i, ret;

	chip = pci_get_drvdata(pdev);
	if (!chip)
		return 0;

	for (i = 0; i < chip->num_slots; i++) {
		slot = chip->slots[i];
		if (!slot)
			continue;

		ret = sdhci_runtime_suspend_host(slot->host);

		if (ret)
			goto err_pci_runtime_suspend;
	}

	if (chip->fixes && chip->fixes->suspend) {
		ret = chip->fixes->suspend(chip);
		if (ret)
			goto err_pci_runtime_suspend;
	}

	return 0;

err_pci_runtime_suspend:
	while (--i >= 0)
		sdhci_runtime_resume_host(chip->slots[i]->host);
	return ret;
}

static int sdhci_pci_runtime_resume(struct device *dev)
{
	struct pci_dev *pdev = container_of(dev, struct pci_dev, dev);
	struct sdhci_pci_chip *chip;
	struct sdhci_pci_slot *slot;
	int i, ret;

	chip = pci_get_drvdata(pdev);
	if (!chip)
		return 0;

	if (chip->fixes && chip->fixes->resume) {
		ret = chip->fixes->resume(chip);
		if (ret)
			return ret;
	}

	for (i = 0; i < chip->num_slots; i++) {
		slot = chip->slots[i];
		if (!slot)
			continue;

		ret = sdhci_runtime_resume_host(slot->host);
		if (ret)
			return ret;
	}

	return 0;
}

static int sdhci_pci_runtime_idle(struct device *dev)
{
	return 0;
}

#else

#define sdhci_pci_runtime_suspend	NULL
#define sdhci_pci_runtime_resume	NULL
#define sdhci_pci_runtime_idle		NULL

#endif

static const struct dev_pm_ops sdhci_pci_pm_ops = {
	.suspend = sdhci_pci_suspend,
	.resume = sdhci_pci_resume,
	.runtime_suspend = sdhci_pci_runtime_suspend,
	.runtime_resume = sdhci_pci_runtime_resume,
	.runtime_idle = sdhci_pci_runtime_idle,
};

/*****************************************************************************\
 *                                                                           *
 * Device probing/removal                                                    *
 *                                                                           *
\*****************************************************************************/

static struct sdhci_pci_slot * __devinit sdhci_pci_probe_slot(
	struct pci_dev *pdev, struct sdhci_pci_chip *chip, int first_bar,
	int slotno)
{
	struct sdhci_pci_slot *slot;
	struct sdhci_host *host;
	int ret, bar = first_bar + slotno;

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
		dev_err(&pdev->dev, "cannot allocate host\n");
		return ERR_CAST(host);
	}

	slot = sdhci_priv(host);

	slot->chip = chip;
	slot->host = host;
	slot->pci_bar = bar;
	slot->rst_n_gpio = -EINVAL;
	slot->cd_gpio = -EINVAL;

	/* Retrieve platform data if there is any */
	if (*sdhci_pci_get_data)
		slot->data = sdhci_pci_get_data(pdev, slotno);

	if (slot->data) {
		if (slot->data->setup) {
			ret = slot->data->setup(slot->data);
			if (ret) {
				dev_err(&pdev->dev, "platform setup failed\n");
				goto free;
			}
		}
		slot->rst_n_gpio = slot->data->rst_n_gpio;
		slot->cd_gpio = slot->data->cd_gpio;
	}

	host->hw_name = "PCI";
	host->ops = &sdhci_pci_ops;
	host->quirks = chip->quirks;
	host->quirks2 = chip->quirks2;

	host->irq = pdev->irq;

	ret = pci_request_region(pdev, bar, mmc_hostname(host->mmc));
	if (ret) {
		dev_err(&pdev->dev, "cannot request region\n");
		goto cleanup;
	}

	host->ioaddr = pci_ioremap_bar(pdev, bar);
	if (!host->ioaddr) {
		dev_err(&pdev->dev, "failed to remap registers\n");
		ret = -ENOMEM;
		goto release;
	}

	if (chip->fixes && chip->fixes->probe_slot) {
		ret = chip->fixes->probe_slot(slot);
		if (ret)
			goto unmap;
	}

	if (gpio_is_valid(slot->rst_n_gpio)) {
		if (!gpio_request(slot->rst_n_gpio, "eMMC_reset")) {
			gpio_direction_output(slot->rst_n_gpio, 1);
			slot->host->mmc->caps |= MMC_CAP_HW_RESET;
		} else {
			dev_warn(&pdev->dev, "failed to request rst_n_gpio\n");
			slot->rst_n_gpio = -EINVAL;
		}
	}

	host->mmc->pm_caps = MMC_PM_KEEP_POWER | MMC_PM_WAKE_SDIO_IRQ;

	ret = sdhci_add_host(host);
	if (ret)
		goto remove;

	sdhci_pci_add_own_cd(slot);

	return slot;

remove:
	if (gpio_is_valid(slot->rst_n_gpio))
		gpio_free(slot->rst_n_gpio);

	if (chip->fixes && chip->fixes->remove_slot)
		chip->fixes->remove_slot(slot, 0);

unmap:
	iounmap(host->ioaddr);

release:
	pci_release_region(pdev, bar);

cleanup:
	if (slot->data && slot->data->cleanup)
		slot->data->cleanup(slot->data);

free:
	sdhci_free_host(host);

	return ERR_PTR(ret);
}

static void sdhci_pci_remove_slot(struct sdhci_pci_slot *slot)
{
	int dead;
	u32 scratch;

	sdhci_pci_remove_own_cd(slot);

	dead = 0;
	scratch = readl(slot->host->ioaddr + SDHCI_INT_STATUS);
	if (scratch == (u32)-1)
		dead = 1;

	sdhci_remove_host(slot->host, dead);

	if (gpio_is_valid(slot->rst_n_gpio))
		gpio_free(slot->rst_n_gpio);

	if (slot->chip->fixes && slot->chip->fixes->remove_slot)
		slot->chip->fixes->remove_slot(slot, dead);

	if (slot->data && slot->data->cleanup)
		slot->data->cleanup(slot->data);

	pci_release_region(slot->chip->pdev, slot->pci_bar);

	sdhci_free_host(slot->host);
}

static void __devinit sdhci_pci_runtime_pm_allow(struct device *dev)
{
	pm_runtime_put_noidle(dev);
	pm_runtime_allow(dev);
	pm_runtime_set_autosuspend_delay(dev, 50);
	pm_runtime_use_autosuspend(dev);
	pm_suspend_ignore_children(dev, 1);
}

static void __devexit sdhci_pci_runtime_pm_forbid(struct device *dev)
{
	pm_runtime_forbid(dev);
	pm_runtime_get_noresume(dev);
}

static int __devinit sdhci_pci_probe(struct pci_dev *pdev,
				     const struct pci_device_id *ent)
{
	struct sdhci_pci_chip *chip;
	struct sdhci_pci_slot *slot;

	u8 slots, first_bar;
	int ret, i;

	BUG_ON(pdev == NULL);
	BUG_ON(ent == NULL);

	dev_info(&pdev->dev, "SDHCI controller found [%04x:%04x] (rev %x)\n",
		 (int)pdev->vendor, (int)pdev->device, (int)pdev->revision);

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
	chip->fixes = (const struct sdhci_pci_fixes *)ent->driver_data;
	if (chip->fixes) {
		chip->quirks = chip->fixes->quirks;
		chip->quirks2 = chip->fixes->quirks2;
		chip->allow_runtime_pm = chip->fixes->allow_runtime_pm;
	}
	chip->num_slots = slots;

	pci_set_drvdata(pdev, chip);

	if (chip->fixes && chip->fixes->probe) {
		ret = chip->fixes->probe(chip);
		if (ret)
			goto free;
	}

	slots = chip->num_slots;	/* Quirk may have changed this */

	for (i = 0; i < slots; i++) {
		slot = sdhci_pci_probe_slot(pdev, chip, first_bar, i);
		if (IS_ERR(slot)) {
			for (i--; i >= 0; i--)
				sdhci_pci_remove_slot(chip->slots[i]);
			ret = PTR_ERR(slot);
			goto free;
		}

		chip->slots[i] = slot;
	}

	if (chip->allow_runtime_pm)
		sdhci_pci_runtime_pm_allow(&pdev->dev);

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
		if (chip->allow_runtime_pm)
			sdhci_pci_runtime_pm_forbid(&pdev->dev);

		for (i = 0; i < chip->num_slots; i++)
			sdhci_pci_remove_slot(chip->slots[i]);

		pci_set_drvdata(pdev, NULL);
		kfree(chip);
	}

	pci_disable_device(pdev);
}

static struct pci_driver sdhci_driver = {
	.name =		"sdhci-pci",
	.id_table =	pci_ids,
	.probe =	sdhci_pci_probe,
	.remove =	__devexit_p(sdhci_pci_remove),
	.driver =	{
		.pm =   &sdhci_pci_pm_ops
	},
};

module_pci_driver(sdhci_driver);

MODULE_AUTHOR("Pierre Ossman <pierre@ossman.eu>");
MODULE_DESCRIPTION("Secure Digital Host Controller Interface PCI driver");
MODULE_LICENSE("GPL");
