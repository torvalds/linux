/*
 * Freescale eSDHC controller driver generics for OF and pltfm.
 *
 * Copyright (c) 2007 Freescale Semiconductor, Inc.
 * Copyright (c) 2009 MontaVista Software, Inc.
 * Copyright (c) 2010 Pengutronix e.K.
 *   Author: Wolfram Sang <w.sang@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 */

#ifndef _DRIVERS_MMC_SDHCI_ESDHC_H
#define _DRIVERS_MMC_SDHCI_ESDHC_H

/*
 * Ops and quirks for the Freescale eSDHC controller.
 */

#define ESDHC_DEFAULT_QUIRKS	(SDHCI_QUIRK_FORCE_BLK_SZ_2048 | \
				SDHCI_QUIRK_NO_BUSY_IRQ | \
				SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK | \
				SDHCI_QUIRK_PIO_NEEDS_DELAY | \
				SDHCI_QUIRK_NO_HISPD_BIT)

/* pltfm-specific */
#define ESDHC_HOST_CONTROL_LE	0x20

/*
 * eSDHC register definition
 */

/* Present State Register */
#define ESDHC_PRSSTAT			0x24
#define ESDHC_CLOCK_STABLE		0x00000008

/* Protocol Control Register */
#define ESDHC_PROCTL			0x28
#define ESDHC_CTRL_4BITBUS		(0x1 << 1)
#define ESDHC_CTRL_8BITBUS		(0x2 << 1)
#define ESDHC_CTRL_BUSWIDTH_MASK	(0x3 << 1)
#define ESDHC_HOST_CONTROL_RES		0x01

/* System Control Register */
#define ESDHC_SYSTEM_CONTROL		0x2c
#define ESDHC_CLOCK_MASK		0x0000fff0
#define ESDHC_PREDIV_SHIFT		8
#define ESDHC_DIVIDER_SHIFT		4
#define ESDHC_CLOCK_SDCLKEN		0x00000008
#define ESDHC_CLOCK_PEREN		0x00000004
#define ESDHC_CLOCK_HCKEN		0x00000002
#define ESDHC_CLOCK_IPGEN		0x00000001

/* Control Register for DMA transfer */
#define ESDHC_DMA_SYSCTL		0x40c
#define ESDHC_DMA_SNOOP			0x00000040

#endif /* _DRIVERS_MMC_SDHCI_ESDHC_H */
