/*
 *  linux/arch/arm/mach-socfpga5xs1/sdmmc.c
 *
 *  Copyright (C) 2011 Altera Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/mmc/dw_mmc.h>
#include <linux/mmc/host.h>
#include <linux/dma-mapping.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <asm/errno.h>/* Clock Manager */

#define PWREN		(0x4)

#define mci_writel(base, value, reg)			 \
	__raw_writel((value), base + reg)

#define mci_readl(base, reg)				\
	__raw_readl(base + reg)

static void __iomem *sdmmc_base = 0;

static void __iomem* sdmmc_get_base_addr(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "snps,dw-mmc");
	if (np) {
		return of_iomap(np, 0);
	}

	return NULL;
}

void sdmmc_setpower(unsigned int slot_id, unsigned int volt)
{
	unsigned int power;

	if (sdmmc_base == NULL) {
		sdmmc_base = sdmmc_get_base_addr();
		if (!sdmmc_base)
			return;
	}

	power = mci_readl(sdmmc_base, PWREN);

	if (volt == 0) {
		/* turn off */
		power &= ~(1 << slot_id);
	}
	else {
		/* turn on */
		power |= (1 << slot_id);
	}

	mci_writel(sdmmc_base, power, PWREN);

	return;
}

int sdmmc_get_bus_width(unsigned int slot_id)
{
	struct device_node *np;
	unsigned int bus_width;

	np = of_find_compatible_node(NULL, NULL, "snps,dw-mmc");
	if (np) {
		if (!(of_property_read_u32(np, "bus-width", &bus_width)))
			return bus_width;
	}

	/* Default 1-bit */
	return 1;
}

int sdmmc_get_ocr(unsigned int slot_id)
{
	struct device_node *np;
	unsigned int voltages = MMC_VDD_32_33 | MMC_VDD_33_34;
	unsigned int vol_switch;

	np = of_find_compatible_node(NULL, NULL, "snps,dw-mmc");
	if (np) {
		if (!of_property_read_u32(np, "voltage-switch", &vol_switch) &&
			vol_switch)
			voltages |= MMC_VDD_165_195;
	}

	return voltages;
}

int sdmmc_init (unsigned int slot_id, irq_handler_t handler, void *p)
{
	return 0;
}

void sdmmc_exit(unsigned int slot_id)
{
	if (sdmmc_base) {
		iounmap(sdmmc_base);
		sdmmc_base = NULL;
	}
	return;
}

struct dw_mci_board sdmmc_platform_data = {
	.quirks = 0,				/* Workaround / Quirk flags */
	.caps = (MMC_CAP_MMC_HIGHSPEED |
		 MMC_CAP_SD_HIGHSPEED),		/* Capabilities */

	/* delay in ms before detecting cards after interrupt */
	.detect_delay_ms = 25,

	.init = sdmmc_init,
	.get_ro = NULL,
	.get_cd = NULL,
	.get_ocr = sdmmc_get_ocr,
	.get_bus_wd = sdmmc_get_bus_width,	/* Get bus width */
	/*
	 * Enable power to selected slot and set voltage to desired level.
	 * Voltage levels are specified using MMC_VDD_xxx defines defined
	 * in linux/mmc/host.h file.
	 */
	.setpower = sdmmc_setpower,
	.exit = sdmmc_exit,
	.select_slot = NULL,		/* Nothing to select, we only have one
					   slot */

	.dma_ops = NULL,		/* support IDMA only */
	.data = NULL,
	.blk_settings = NULL, 		/* Use default setting for IDMAC */

};
