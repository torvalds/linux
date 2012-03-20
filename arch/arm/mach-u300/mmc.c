/*
 *
 * arch/arm/mach-u300/mmc.c
 *
 *
 * Copyright (C) 2009 ST-Ericsson SA
 * License terms: GNU General Public License (GPL) version 2
 *
 * Author: Linus Walleij <linus.walleij@stericsson.com>
 * Author: Johan Lundin
 * Author: Jonas Aaberg <jonas.aberg@stericsson.com>
 */
#include <linux/device.h>
#include <linux/amba/bus.h>
#include <linux/mmc/host.h>
#include <linux/dmaengine.h>
#include <linux/amba/mmci.h>
#include <linux/slab.h>
#include <mach/coh901318.h>
#include <mach/dma_channels.h>

#include "u300-gpio.h"
#include "mmc.h"

static struct mmci_platform_data mmc0_plat_data = {
	/*
	 * Do not set ocr_mask or voltage translation function,
	 * we have a regulator we can control instead.
	 */
	/* Nominally 2.85V on our platform */
	.f_max = 24000000,
	.gpio_wp = -1,
	.gpio_cd = U300_GPIO_PIN_MMC_CD,
	.cd_invert = true,
	.capabilities = MMC_CAP_MMC_HIGHSPEED |
	MMC_CAP_SD_HIGHSPEED | MMC_CAP_4_BIT_DATA | MMC_CAP_8_BIT_DATA,
#ifdef CONFIG_COH901318
	.dma_filter = coh901318_filter_id,
	.dma_rx_param = (void *) U300_DMA_MMCSD_RX_TX,
	/* Don't specify a TX channel, this RX channel is bidirectional */
#endif
};

int __devinit mmc_init(struct amba_device *adev)
{
	struct device *mmcsd_device = &adev->dev;
	int ret = 0;

	mmcsd_device->platform_data = &mmc0_plat_data;

	return ret;
}
