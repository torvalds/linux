/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Freescale eSDHC controller driver generics for OF and pltfm.
 *
 * Copyright (c) 2007 Freescale Semiconductor, Inc.
 * Copyright (c) 2009 MontaVista Software, Inc.
 * Copyright (c) 2010 Pengutronix e.K.
 *   Author: Wolfram Sang <w.sang@pengutronix.de>
 */

#ifndef _DRIVERS_MMC_SDHCI_ESDHC_H
#define _DRIVERS_MMC_SDHCI_ESDHC_H

/*
 * Ops and quirks for the Freescale eSDHC controller.
 */

#define ESDHC_DEFAULT_QUIRKS	(SDHCI_QUIRK_FORCE_BLK_SZ_2048 | \
				SDHCI_QUIRK_32BIT_DMA_ADDR | \
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
#define ESDHC_CLOCK_GATE_OFF		0x00000080
#define ESDHC_CLOCK_STABLE		0x00000008

/* Protocol Control Register */
#define ESDHC_PROCTL			0x28
#define ESDHC_VOLT_SEL			0x00000400
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

/* System Control 2 Register */
#define ESDHC_SYSTEM_CONTROL_2		0x3c
#define ESDHC_SMPCLKSEL			0x00800000
#define ESDHC_EXTN			0x00400000

/* Host Controller Capabilities Register 2 */
#define ESDHC_CAPABILITIES_1		0x114

/* Tuning Block Control Register */
#define ESDHC_TBCTL			0x120
#define ESDHC_HS400_WNDW_ADJUST		0x00000040
#define ESDHC_HS400_MODE		0x00000010
#define ESDHC_TB_EN			0x00000004
#define ESDHC_TB_MODE_MASK		0x00000003
#define ESDHC_TB_MODE_SW		0x00000003
#define ESDHC_TB_MODE_3			0x00000002

#define ESDHC_TBSTAT			0x124

#define ESDHC_TBPTR			0x128
#define ESDHC_WNDW_STRT_PTR_SHIFT	8
#define ESDHC_WNDW_STRT_PTR_MASK	(0x7f << 8)
#define ESDHC_WNDW_END_PTR_MASK		0x7f

/* SD Clock Control Register */
#define ESDHC_SDCLKCTL			0x144
#define ESDHC_LPBK_CLK_SEL		0x80000000
#define ESDHC_CMD_CLK_CTL		0x00008000

/* SD Timing Control Register */
#define ESDHC_SDTIMNGCTL		0x148
#define ESDHC_FLW_CTL_BG		0x00008000

/* DLL Config 0 Register */
#define ESDHC_DLLCFG0			0x160
#define ESDHC_DLL_ENABLE		0x80000000
#define ESDHC_DLL_FREQ_SEL		0x08000000

/* DLL Config 1 Register */
#define ESDHC_DLLCFG1			0x164
#define ESDHC_DLL_PD_PULSE_STRETCH_SEL	0x80000000

/* DLL Status 0 Register */
#define ESDHC_DLLSTAT0			0x170
#define ESDHC_DLL_STS_SLV_LOCK		0x08000000

/* Control Register for DMA transfer */
#define ESDHC_DMA_SYSCTL		0x40c
#define ESDHC_PERIPHERAL_CLK_SEL	0x00080000
#define ESDHC_FLUSH_ASYNC_FIFO		0x00040000
#define ESDHC_DMA_SNOOP			0x00000040

#endif /* _DRIVERS_MMC_SDHCI_ESDHC_H */
