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
#include <linux/interrupt.h>
#include <linux/acpi.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/delay.h>

#include <linux/mmc/host.h>
#include <linux/mmc/pm.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/mmc/sdhci.h>

#include "sdhci.h"

enum {
	SDHCI_ACPI_SD_CD		= BIT(0),
	SDHCI_ACPI_RUNTIME_PM		= BIT(1),
	SDHCI_ACPI_SD_CD_OVERRIDE_LEVEL	= BIT(2),
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
	int (*probe_slot)(struct platform_device *, const char *, const char *);
	int (*remove_slot)(struct platform_device *);
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

static void sdhci_acpi_int_hw_reset(struct sdhci_host *host)
{
	u8 reg;

	reg = sdhci_readb(host, SDHCI_POWER_CONTROL);
	reg |= 0x10;
	sdhci_writeb(host, reg, SDHCI_POWER_CONTROL);
	/* For eMMC, minimum is 1us but give it 9us for good measure */
	udelay(9);
	reg &= ~0x10;
	sdhci_writeb(host, reg, SDHCI_POWER_CONTROL);
	/* For eMMC, minimum is 200us but give it 300us for good measure */
	usleep_range(300, 1000);
}

static const struct sdhci_ops sdhci_acpi_ops_dflt = {
	.set_clock = sdhci_set_clock,
	.enable_dma = sdhci_acpi_enable_dma,
	.set_bus_width = sdhci_set_bus_width,
	.reset = sdhci_reset,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
};

static const struct sdhci_ops sdhci_acpi_ops_int = {
	.set_clock = sdhci_set_clock,
	.enable_dma = sdhci_acpi_enable_dma,
	.set_bus_width = sdhci_set_bus_width,
	.reset = sdhci_reset,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
	.hw_reset   = sdhci_acpi_int_hw_reset,
};

static const struct sdhci_acpi_chip sdhci_acpi_chip_int = {
	.ops = &sdhci_acpi_ops_int,
};

static int sdhci_acpi_emmc_probe_slot(struct platform_device *pdev,
				      const char *hid, const char *uid)
{
	struct sdhci_acpi_host *c = platform_get_drvdata(pdev);
	struct sdhci_host *host;

	if (!c || !c->host)
		return 0;

	host = c->host;

	/* Platform specific code during emmc proble slot goes here */

	if (hid && uid && !strcmp(hid, "80860F14") && !strcmp(uid, "1") &&
	    sdhci_readl(host, SDHCI_CAPABILITIES) == 0x446cc8b2 &&
	    sdhci_readl(host, SDHCI_CAPABILITIES_1) == 0x00000807)
		host->timeout_clk = 1000; /* 1000 kHz i.e. 1 MHz */

	return 0;
}

static int sdhci_acpi_sdio_probe_slot(struct platform_device *pdev,
				      const char *hid, const char *uid)
{
	struct sdhci_acpi_host *c = platform_get_drvdata(pdev);
	struct sdhci_host *host;

	if (!c || !c->host)
		return 0;

	host = c->host;

	/* Platform specific code during emmc proble slot goes here */

	return 0;
}

static int sdhci_acpi_sd_probe_slot(struct platform_device *pdev,
				    const char *hid, const char *uid)
{
	struct sdhci_acpi_host *c = platform_get_drvdata(pdev);
	struct sdhci_host *host;

	if (!c || !c->host || !c->slot)
		return 0;

	host = c->host;

	/* Platform specific code during emmc proble slot goes here */

	return 0;
}

static const struct sdhci_acpi_slot sdhci_acpi_slot_int_emmc = {
	.chip    = &sdhci_acpi_chip_int,
	.caps    = MMC_CAP_8_BIT_DATA | MMC_CAP_NONREMOVABLE |
		   MMC_CAP_HW_RESET | MMC_CAP_1_8V_DDR,
	.caps2   = MMC_CAP2_HC_ERASE_SZ,
	.flags   = SDHCI_ACPI_RUNTIME_PM,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN | SDHCI_QUIRK2_STOP_WITH_TC,
	.probe_slot	= sdhci_acpi_emmc_probe_slot,
};

static const struct sdhci_acpi_slot sdhci_acpi_slot_int_sdio = {
	.quirks  = SDHCI_QUIRK_BROKEN_CARD_DETECTION,
	.quirks2 = SDHCI_QUIRK2_HOST_OFF_CARD_ON,
	.caps    = MMC_CAP_NONREMOVABLE | MMC_CAP_POWER_OFF_CARD,
	.flags   = SDHCI_ACPI_RUNTIME_PM,
	.pm_caps = MMC_PM_KEEP_POWER,
	.probe_slot	= sdhci_acpi_sdio_probe_slot,
};

static const struct sdhci_acpi_slot sdhci_acpi_slot_int_sd = {
	.flags   = SDHCI_ACPI_SD_CD | SDHCI_ACPI_SD_CD_OVERRIDE_LEVEL |
		   SDHCI_ACPI_RUNTIME_PM,
	.quirks2 = SDHCI_QUIRK2_CARD_ON_NEEDS_BUS_ON |
		   SDHCI_QUIRK2_STOP_WITH_TC,
	.probe_slot	= sdhci_acpi_sd_probe_slot,
};

struct sdhci_acpi_uid_slot {
	const char *hid;
	const char *uid;
	const struct sdhci_acpi_slot *slot;
};

static const struct sdhci_acpi_uid_slot sdhci_acpi_uids[] = {
	{ "80860F14" , "1" , &sdhci_acpi_slot_int_emmc },
	{ "80860F14" , "3" , &sdhci_acpi_slot_int_sd   },
	{ "80860F16" , NULL, &sdhci_acpi_slot_int_sd   },
	{ "INT33BB"  , "2" , &sdhci_acpi_slot_int_sdio },
	{ "INT33BB"  , "3" , &sdhci_acpi_slot_int_sd },
	{ "INT33C6"  , NULL, &sdhci_acpi_slot_int_sdio },
	{ "INT3436"  , NULL, &sdhci_acpi_slot_int_sdio },
	{ "PNP0D40"  },
	{ },
};

static const struct acpi_device_id sdhci_acpi_ids[] = {
	{ "80860F14" },
	{ "80860F16" },
	{ "INT33BB"  },
	{ "INT33C6"  },
	{ "INT3436"  },
	{ "PNP0D40"  },
	{ },
};
MODULE_DEVICE_TABLE(acpi, sdhci_acpi_ids);

static const struct sdhci_acpi_slot *sdhci_acpi_get_slot(const char *hid,
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
	const char *uid;
	int err;

	if (acpi_bus_get_device(handle, &device))
		return -ENODEV;

	if (acpi_bus_get_status(device) || !device->status.present)
		return -ENODEV;

	hid = acpi_device_hid(device);
	uid = device->pnp.unique_id;

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

	c = sdhci_priv(host);
	c->host = host;
	c->slot = sdhci_acpi_get_slot(hid, uid);
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

		err = dma_coerce_mask_and_coherent(dev, dma_mask);
		if (err)
			goto err_free;
	}

	if (c->slot) {
		if (c->slot->probe_slot) {
			err = c->slot->probe_slot(pdev, hid, uid);
			if (err)
				goto err_free;
		}
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

	if (sdhci_acpi_flag(c, SDHCI_ACPI_SD_CD)) {
		bool v = sdhci_acpi_flag(c, SDHCI_ACPI_SD_CD_OVERRIDE_LEVEL);

		if (mmc_gpiod_request_cd(host->mmc, NULL, 0, v, 0, NULL)) {
			dev_warn(dev, "failed to setup card detect gpio\n");
			c->use_runtime_pm = false;
		}
	}

	err = sdhci_add_host(host);
	if (err)
		goto err_free;

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

	if (c->slot && c->slot->remove_slot)
		c->slot->remove_slot(pdev);

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

#endif

static const struct dev_pm_ops sdhci_acpi_pm_ops = {
	.suspend		= sdhci_acpi_suspend,
	.resume			= sdhci_acpi_resume,
	SET_RUNTIME_PM_OPS(sdhci_acpi_runtime_suspend,
			sdhci_acpi_runtime_resume, sdhci_acpi_runtime_idle)
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
