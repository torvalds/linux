/*
 * Secure Digital Host Controller Interface ACPI driver.
 *
 * Copyright (c) 2012, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <linux/init.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/compiler.h>
#include <linux/stddef.h>
#include <linux/bitops.h>
#include <linux/types.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/acpi.h>
#include <linux/acpi_gpio.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>

#include <linux/mmc/host.h>
#include <linux/mmc/pm.h>
#include <linux/mmc/sdhci.h>

#include "sdhci.h"

enum {
	SDHCI_ACPI_SD_CD	= BIT(0),
	SDHCI_ACPI_RUNTIME_PM	= BIT(1),
};

struct sdhci_acpi_chip {
	const struct	sdhci_ops *ops;
	unsigned int	quirks;
	unsigned int	quirks2;
	unsigned long	caps;
	unsigned int	caps2;
	mmc_pm_flag_t	pm_caps;
};

struct sdhci_acpi_slot {
	const struct	sdhci_acpi_chip *chip;
	unsigned int	quirks;
	unsigned int	quirks2;
	unsigned long	caps;
	unsigned int	caps2;
	mmc_pm_flag_t	pm_caps;
	unsigned int	flags;
};

struct sdhci_acpi_host {
	struct sdhci_host		*host;
	const struct sdhci_acpi_slot	*slot;
	struct platform_device		*pdev;
	bool				use_runtime_pm;
};

static inline bool sdhci_acpi_flag(struct sdhci_acpi_host *c, unsigned int flag)
{
	return c->slot && (c->slot->flags & flag);
}

static int sdhci_acpi_enable_dma(struct sdhci_host *host)
{
	return 0;
}

static const struct sdhci_ops sdhci_acpi_ops_dflt = {
	.enable_dma = sdhci_acpi_enable_dma,
};

static const struct sdhci_acpi_slot sdhci_acpi_slot_int_emmc = {
	.caps    = MMC_CAP_8_BIT_DATA | MMC_CAP_NONREMOVABLE,
	.caps2   = MMC_CAP2_HC_ERASE_SZ,
	.flags   = SDHCI_ACPI_RUNTIME_PM,
};

static const struct sdhci_acpi_slot sdhci_acpi_slot_int_sdio = {
	.quirks2 = SDHCI_QUIRK2_HOST_OFF_CARD_ON,
	.caps    = MMC_CAP_NONREMOVABLE | MMC_CAP_POWER_OFF_CARD,
	.flags   = SDHCI_ACPI_RUNTIME_PM,
	.pm_caps = MMC_PM_KEEP_POWER,
};

static const struct sdhci_acpi_slot sdhci_acpi_slot_int_sd = {
	.flags   = SDHCI_ACPI_SD_CD | SDHCI_ACPI_RUNTIME_PM,
	.quirks2 = SDHCI_QUIRK2_CARD_ON_NEEDS_BUS_ON,
};

struct sdhci_acpi_uid_slot {
	const char *hid;
	const char *uid;
	const struct sdhci_acpi_slot *slot;
};

static const struct sdhci_acpi_uid_slot sdhci_acpi_uids[] = {
	{ "80860F14" , "1" , &sdhci_acpi_slot_int_emmc },
	{ "80860F14" , "3" , &sdhci_acpi_slot_int_sd   },
	{ "INT33BB"  , "2" , &sdhci_acpi_slot_int_sdio },
	{ "INT33C6"  , NULL, &sdhci_acpi_slot_int_sdio },
	{ "PNP0D40"  },
	{ },
};

static const struct acpi_device_id sdhci_acpi_ids[] = {
	{ "80860F14" },
	{ "INT33BB"  },
	{ "INT33C6"  },
	{ "PNP0D40"  },
	{ },
};
MODULE_DEVICE_TABLE(acpi, sdhci_acpi_ids);

static const struct sdhci_acpi_slot *sdhci_acpi_get_slot_by_ids(const char *hid,
								const char *uid)
{
	const struct sdhci_acpi_uid_slot *u;

	for (u = sdhci_acpi_uids; u->hid; u++) {
		if (strcmp(u->hid, hid))
			continue;
		if (!u->uid)
			return u->slot;
		if (uid && !strcmp(u->uid, uid))
			return u->slot;
	}
	return NULL;
}

static const struct sdhci_acpi_slot *sdhci_acpi_get_slot(acpi_handle handle,
							 const char *hid)
{
	const struct sdhci_acpi_slot *slot;
	struct acpi_device_info *info;
	const char *uid = NULL;
	acpi_status status;

	status = acpi_get_object_info(handle, &info);
	if (!ACPI_FAILURE(status) && (info->valid & ACPI_VALID_UID))
		uid = info->unique_id.string;

	slot = sdhci_acpi_get_slot_by_ids(hid, uid);

	kfree(info);
	return slot;
}

#ifdef CONFIG_PM_RUNTIME

static irqreturn_t sdhci_acpi_sd_cd(int irq, void *dev_id)
{
	mmc_detect_change(dev_id, msecs_to_jiffies(200));
	return IRQ_HANDLED;
}

static int sdhci_acpi_add_own_cd(struct device *dev, int gpio,
				 struct mmc_host *mmc)
{
	unsigned long flags;
	int err, irq;

	if (gpio < 0) {
		err = gpio;
		goto out;
	}

	err = devm_gpio_request_one(dev, gpio, GPIOF_DIR_IN, "sd_cd");
	if (err)
		goto out;

	irq = gpio_to_irq(gpio);
	if (irq < 0) {
		err = irq;
		goto out_free;
	}

	flags = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING;
	err = devm_request_irq(dev, irq, sdhci_acpi_sd_cd, flags, "sd_cd", mmc);
	if (err)
		goto out_free;

	return 0;

out_free:
	devm_gpio_free(dev, gpio);
out:
	dev_warn(dev, "failed to setup card detect wake up\n");
	return err;
}

#else

static int sdhci_acpi_add_own_cd(struct device *dev, int gpio,
				 struct mmc_host *mmc)
{
	return 0;
}

#endif

static int sdhci_acpi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	acpi_handle handle = ACPI_HANDLE(dev);
	struct acpi_device *device;
	struct sdhci_acpi_host *c;
	struct sdhci_host *host;
	struct resource *iomem;
	resource_size_t len;
	const char *hid;
	int err, gpio;

	if (acpi_bus_get_device(handle, &device))
		return -ENODEV;

	if (acpi_bus_get_status(device) || !device->status.present)
		return -ENODEV;

	hid = acpi_device_hid(device);

	iomem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!iomem)
		return -ENOMEM;

	len = resource_size(iomem);
	if (len < 0x100)
		dev_err(dev, "Invalid iomem size!\n");

	if (!devm_request_mem_region(dev, iomem->start, len, dev_name(dev)))
		return -ENOMEM;

	host = sdhci_alloc_host(dev, sizeof(struct sdhci_acpi_host));
	if (IS_ERR(host))
		return PTR_ERR(host);

	gpio = acpi_get_gpio_by_index(dev, 0, NULL);

	c = sdhci_priv(host);
	c->host = host;
	c->slot = sdhci_acpi_get_slot(handle, hid);
	c->pdev = pdev;
	c->use_runtime_pm = sdhci_acpi_flag(c, SDHCI_ACPI_RUNTIME_PM);

	platform_set_drvdata(pdev, c);

	host->hw_name	= "ACPI";
	host->ops	= &sdhci_acpi_ops_dflt;
	host->irq	= platform_get_irq(pdev, 0);

	host->ioaddr = devm_ioremap_nocache(dev, iomem->start,
					    resource_size(iomem));
	if (host->ioaddr == NULL) {
		err = -ENOMEM;
		goto err_free;
	}

	if (!dev->dma_mask) {
		u64 dma_mask;

		if (sdhci_readl(host, SDHCI_CAPABILITIES) & SDHCI_CAN_64BIT) {
			/* 64-bit DMA is not supported at present */
			dma_mask = DMA_BIT_MASK(32);
		} else {
			dma_mask = DMA_BIT_MASK(32);
		}

		dev->dma_mask = &dev->coherent_dma_mask;
		dev->coherent_dma_mask = dma_mask;
	}

	if (c->slot) {
		if (c->slot->chip) {
			host->ops            = c->slot->chip->ops;
			host->quirks        |= c->slot->chip->quirks;
			host->quirks2       |= c->slot->chip->quirks2;
			host->mmc->caps     |= c->slot->chip->caps;
			host->mmc->caps2    |= c->slot->chip->caps2;
			host->mmc->pm_caps  |= c->slot->chip->pm_caps;
		}
		host->quirks        |= c->slot->quirks;
		host->quirks2       |= c->slot->quirks2;
		host->mmc->caps     |= c->slot->caps;
		host->mmc->caps2    |= c->slot->caps2;
		host->mmc->pm_caps  |= c->slot->pm_caps;
	}

	host->mmc->caps2 |= MMC_CAP2_NO_PRESCAN_POWERUP;

	err = sdhci_add_host(host);
	if (err)
		goto err_free;

	if (sdhci_acpi_flag(c, SDHCI_ACPI_SD_CD)) {
		if (sdhci_acpi_add_own_cd(dev, gpio, host->mmc))
			c->use_runtime_pm = false;
	}

	if (c->use_runtime_pm) {
		pm_runtime_set_active(dev);
		pm_suspend_ignore_children(dev, 1);
		pm_runtime_set_autosuspend_delay(dev, 50);
		pm_runtime_use_autosuspend(dev);
		pm_runtime_enable(dev);
	}

	return 0;

err_free:
	sdhci_free_host(c->host);
	return err;
}

static int sdhci_acpi_remove(struct platform_device *pdev)
{
	struct sdhci_acpi_host *c = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	int dead;

	if (c->use_runtime_pm) {
		pm_runtime_get_sync(dev);
		pm_runtime_disable(dev);
		pm_runtime_put_noidle(dev);
	}

	dead = (sdhci_readl(c->host, SDHCI_INT_STATUS) == ~0);
	sdhci_remove_host(c->host, dead);
	sdhci_free_host(c->host);

	return 0;
}

#ifdef CONFIG_PM_SLEEP

static int sdhci_acpi_suspend(struct device *dev)
{
	struct sdhci_acpi_host *c = dev_get_drvdata(dev);

	return sdhci_suspend_host(c->host);
}

static int sdhci_acpi_resume(struct device *dev)
{
	struct sdhci_acpi_host *c = dev_get_drvdata(dev);

	return sdhci_resume_host(c->host);
}

#else

#define sdhci_acpi_suspend	NULL
#define sdhci_acpi_resume	NULL

#endif

#ifdef CONFIG_PM_RUNTIME

static int sdhci_acpi_runtime_suspend(struct device *dev)
{
	struct sdhci_acpi_host *c = dev_get_drvdata(dev);

	return sdhci_runtime_suspend_host(c->host);
}

static int sdhci_acpi_runtime_resume(struct device *dev)
{
	struct sdhci_acpi_host *c = dev_get_drvdata(dev);

	return sdhci_runtime_resume_host(c->host);
}

static int sdhci_acpi_runtime_idle(struct device *dev)
{
	return 0;
}

#else

#define sdhci_acpi_runtime_suspend	NULL
#define sdhci_acpi_runtime_resume	NULL
#define sdhci_acpi_runtime_idle		NULL

#endif

static const struct dev_pm_ops sdhci_acpi_pm_ops = {
	.suspend		= sdhci_acpi_suspend,
	.resume			= sdhci_acpi_resume,
	.runtime_suspend	= sdhci_acpi_runtime_suspend,
	.runtime_resume		= sdhci_acpi_runtime_resume,
	.runtime_idle		= sdhci_acpi_runtime_idle,
};

static struct platform_driver sdhci_acpi_driver = {
	.driver = {
		.name			= "sdhci-acpi",
		.owner			= THIS_MODULE,
		.acpi_match_table	= sdhci_acpi_ids,
		.pm			= &sdhci_acpi_pm_ops,
	},
	.probe	= sdhci_acpi_probe,
	.remove	= sdhci_acpi_remove,
};

module_platform_driver(sdhci_acpi_driver);

MODULE_DESCRIPTION("Secure Digital Host Controller Interface ACPI driver");
MODULE_AUTHOR("Adrian Hunter");
MODULE_LICENSE("GPL v2");
