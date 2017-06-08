/*
 *  lpc_ich.c - LPC interface for Intel ICH
 *
 *  LPC bridge function of the Intel ICH contains many other
 *  functional units, such as Interrupt controllers, Timers,
 *  Power Management, System Management, GPIO, RTC, and LPC
 *  Configuration Registers.
 *
 *  This driver is derived from lpc_sch.

 *  Copyright (c) 2011 Extreme Engineering Solution, Inc.
 *  Author: Aaron Sierra <asierra@xes-inc.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License 2 as published
 *  by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  This driver supports the following I/O Controller hubs:
 *	(See the intel documentation on http://developer.intel.com.)
 *	document number 290655-003, 290677-014: 82801AA (ICH), 82801AB (ICHO)
 *	document number 290687-002, 298242-027: 82801BA (ICH2)
 *	document number 290733-003, 290739-013: 82801CA (ICH3-S)
 *	document number 290716-001, 290718-007: 82801CAM (ICH3-M)
 *	document number 290744-001, 290745-025: 82801DB (ICH4)
 *	document number 252337-001, 252663-008: 82801DBM (ICH4-M)
 *	document number 273599-001, 273645-002: 82801E (C-ICH)
 *	document number 252516-001, 252517-028: 82801EB (ICH5), 82801ER (ICH5R)
 *	document number 300641-004, 300884-013: 6300ESB
 *	document number 301473-002, 301474-026: 82801F (ICH6)
 *	document number 313082-001, 313075-006: 631xESB, 632xESB
 *	document number 307013-003, 307014-024: 82801G (ICH7)
 *	document number 322896-001, 322897-001: NM10
 *	document number 313056-003, 313057-017: 82801H (ICH8)
 *	document number 316972-004, 316973-012: 82801I (ICH9)
 *	document number 319973-002, 319974-002: 82801J (ICH10)
 *	document number 322169-001, 322170-003: 5 Series, 3400 Series (PCH)
 *	document number 320066-003, 320257-008: EP80597 (IICH)
 *	document number 324645-001, 324646-001: Cougar Point (CPT)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/acpi.h>
#include <linux/pci.h>
#include <linux/mfd/core.h>
#include <linux/mfd/lpc_ich.h>
#include <linux/platform_data/itco_wdt.h>

#define ACPIBASE		0x40
#define ACPIBASE_GPE_OFF	0x28
#define ACPIBASE_GPE_END	0x2f
#define ACPIBASE_SMI_OFF	0x30
#define ACPIBASE_SMI_END	0x33
#define ACPIBASE_PMC_OFF	0x08
#define ACPIBASE_PMC_END	0x0c
#define ACPIBASE_TCO_OFF	0x60
#define ACPIBASE_TCO_END	0x7f
#define ACPICTRL_PMCBASE	0x44

#define ACPIBASE_GCS_OFF	0x3410
#define ACPIBASE_GCS_END	0x3414

#define SPIBASE_BYT		0x54
#define SPIBASE_BYT_SZ		512
#define SPIBASE_BYT_EN		BIT(1)

#define SPIBASE_LPT		0x3800
#define SPIBASE_LPT_SZ		512
#define BCR			0xdc
#define BCR_WPD			BIT(0)

#define SPIBASE_APL_SZ		4096

#define GPIOBASE_ICH0		0x58
#define GPIOCTRL_ICH0		0x5C
#define GPIOBASE_ICH6		0x48
#define GPIOCTRL_ICH6		0x4C

#define RCBABASE		0xf0

#define wdt_io_res(i) wdt_res(0, i)
#define wdt_mem_res(i) wdt_res(ICH_RES_MEM_OFF, i)
#define wdt_res(b, i) (&wdt_ich_res[(b) + (i)])

struct lpc_ich_priv {
	int chipset;

	int abase;		/* ACPI base */
	int actrl_pbase;	/* ACPI control or PMC base */
	int gbase;		/* GPIO base */
	int gctrl;		/* GPIO control */

	int abase_save;		/* Cached ACPI base value */
	int actrl_pbase_save;		/* Cached ACPI control or PMC base value */
	int gctrl_save;		/* Cached GPIO control value */
};

static struct resource wdt_ich_res[] = {
	/* ACPI - TCO */
	{
		.flags = IORESOURCE_IO,
	},
	/* ACPI - SMI */
	{
		.flags = IORESOURCE_IO,
	},
	/* GCS or PMC */
	{
		.flags = IORESOURCE_MEM,
	},
};

static struct resource gpio_ich_res[] = {
	/* GPIO */
	{
		.flags = IORESOURCE_IO,
	},
	/* ACPI - GPE0 */
	{
		.flags = IORESOURCE_IO,
	},
};

static struct resource intel_spi_res[] = {
	{
		.flags = IORESOURCE_MEM,
	}
};

static struct mfd_cell lpc_ich_wdt_cell = {
	.name = "iTCO_wdt",
	.num_resources = ARRAY_SIZE(wdt_ich_res),
	.resources = wdt_ich_res,
	.ignore_resource_conflicts = true,
};

static struct mfd_cell lpc_ich_gpio_cell = {
	.name = "gpio_ich",
	.num_resources = ARRAY_SIZE(gpio_ich_res),
	.resources = gpio_ich_res,
	.ignore_resource_conflicts = true,
};


static struct mfd_cell lpc_ich_spi_cell = {
	.name = "intel-spi",
	.num_resources = ARRAY_SIZE(intel_spi_res),
	.resources = intel_spi_res,
	.ignore_resource_conflicts = true,
};

/* chipset related info */
enum lpc_chipsets {
	LPC_ICH = 0,	/* ICH */
	LPC_ICH0,	/* ICH0 */
	LPC_ICH2,	/* ICH2 */
	LPC_ICH2M,	/* ICH2-M */
	LPC_ICH3,	/* ICH3-S */
	LPC_ICH3M,	/* ICH3-M */
	LPC_ICH4,	/* ICH4 */
	LPC_ICH4M,	/* ICH4-M */
	LPC_CICH,	/* C-ICH */
	LPC_ICH5,	/* ICH5 & ICH5R */
	LPC_6300ESB,	/* 6300ESB */
	LPC_ICH6,	/* ICH6 & ICH6R */
	LPC_ICH6M,	/* ICH6-M */
	LPC_ICH6W,	/* ICH6W & ICH6RW */
	LPC_631XESB,	/* 631xESB/632xESB */
	LPC_ICH7,	/* ICH7 & ICH7R */
	LPC_ICH7DH,	/* ICH7DH */
	LPC_ICH7M,	/* ICH7-M & ICH7-U */
	LPC_ICH7MDH,	/* ICH7-M DH */
	LPC_NM10,	/* NM10 */
	LPC_ICH8,	/* ICH8 & ICH8R */
	LPC_ICH8DH,	/* ICH8DH */
	LPC_ICH8DO,	/* ICH8DO */
	LPC_ICH8M,	/* ICH8M */
	LPC_ICH8ME,	/* ICH8M-E */
	LPC_ICH9,	/* ICH9 */
	LPC_ICH9R,	/* ICH9R */
	LPC_ICH9DH,	/* ICH9DH */
	LPC_ICH9DO,	/* ICH9DO */
	LPC_ICH9M,	/* ICH9M */
	LPC_ICH9ME,	/* ICH9M-E */
	LPC_ICH10,	/* ICH10 */
	LPC_ICH10R,	/* ICH10R */
	LPC_ICH10D,	/* ICH10D */
	LPC_ICH10DO,	/* ICH10DO */
	LPC_PCH,	/* PCH Desktop Full Featured */
	LPC_PCHM,	/* PCH Mobile Full Featured */
	LPC_P55,	/* P55 */
	LPC_PM55,	/* PM55 */
	LPC_H55,	/* H55 */
	LPC_QM57,	/* QM57 */
	LPC_H57,	/* H57 */
	LPC_HM55,	/* HM55 */
	LPC_Q57,	/* Q57 */
	LPC_HM57,	/* HM57 */
	LPC_PCHMSFF,	/* PCH Mobile SFF Full Featured */
	LPC_QS57,	/* QS57 */
	LPC_3400,	/* 3400 */
	LPC_3420,	/* 3420 */
	LPC_3450,	/* 3450 */
	LPC_EP80579,	/* EP80579 */
	LPC_CPT,	/* Cougar Point */
	LPC_CPTD,	/* Cougar Point Desktop */
	LPC_CPTM,	/* Cougar Point Mobile */
	LPC_PBG,	/* Patsburg */
	LPC_DH89XXCC,	/* DH89xxCC */
	LPC_PPT,	/* Panther Point */
	LPC_LPT,	/* Lynx Point */
	LPC_LPT_LP,	/* Lynx Point-LP */
	LPC_WBG,	/* Wellsburg */
	LPC_AVN,	/* Avoton SoC */
	LPC_BAYTRAIL,   /* Bay Trail SoC */
	LPC_COLETO,	/* Coleto Creek */
	LPC_WPT_LP,	/* Wildcat Point-LP */
	LPC_BRASWELL,	/* Braswell SoC */
	LPC_LEWISBURG,	/* Lewisburg */
	LPC_9S,		/* 9 Series */
	LPC_APL,	/* Apollo Lake SoC */
	LPC_GLK,	/* Gemini Lake SoC */
	LPC_COUGARMOUNTAIN,/* Cougar Mountain SoC*/
};

static struct lpc_ich_info lpc_chipset_info[] = {
	[LPC_ICH] = {
		.name = "ICH",
		.iTCO_version = 1,
	},
	[LPC_ICH0] = {
		.name = "ICH0",
		.iTCO_version = 1,
	},
	[LPC_ICH2] = {
		.name = "ICH2",
		.iTCO_version = 1,
	},
	[LPC_ICH2M] = {
		.name = "ICH2-M",
		.iTCO_version = 1,
	},
	[LPC_ICH3] = {
		.name = "ICH3-S",
		.iTCO_version = 1,
	},
	[LPC_ICH3M] = {
		.name = "ICH3-M",
		.iTCO_version = 1,
	},
	[LPC_ICH4] = {
		.name = "ICH4",
		.iTCO_version = 1,
	},
	[LPC_ICH4M] = {
		.name = "ICH4-M",
		.iTCO_version = 1,
	},
	[LPC_CICH] = {
		.name = "C-ICH",
		.iTCO_version = 1,
	},
	[LPC_ICH5] = {
		.name = "ICH5 or ICH5R",
		.iTCO_version = 1,
	},
	[LPC_6300ESB] = {
		.name = "6300ESB",
		.iTCO_version = 1,
	},
	[LPC_ICH6] = {
		.name = "ICH6 or ICH6R",
		.iTCO_version = 2,
		.gpio_version = ICH_V6_GPIO,
	},
	[LPC_ICH6M] = {
		.name = "ICH6-M",
		.iTCO_version = 2,
		.gpio_version = ICH_V6_GPIO,
	},
	[LPC_ICH6W] = {
		.name = "ICH6W or ICH6RW",
		.iTCO_version = 2,
		.gpio_version = ICH_V6_GPIO,
	},
	[LPC_631XESB] = {
		.name = "631xESB/632xESB",
		.iTCO_version = 2,
		.gpio_version = ICH_V6_GPIO,
	},
	[LPC_ICH7] = {
		.name = "ICH7 or ICH7R",
		.iTCO_version = 2,
		.gpio_version = ICH_V7_GPIO,
	},
	[LPC_ICH7DH] = {
		.name = "ICH7DH",
		.iTCO_version = 2,
		.gpio_version = ICH_V7_GPIO,
	},
	[LPC_ICH7M] = {
		.name = "ICH7-M or ICH7-U",
		.iTCO_version = 2,
		.gpio_version = ICH_V7_GPIO,
	},
	[LPC_ICH7MDH] = {
		.name = "ICH7-M DH",
		.iTCO_version = 2,
		.gpio_version = ICH_V7_GPIO,
	},
	[LPC_NM10] = {
		.name = "NM10",
		.iTCO_version = 2,
		.gpio_version = ICH_V7_GPIO,
	},
	[LPC_ICH8] = {
		.name = "ICH8 or ICH8R",
		.iTCO_version = 2,
		.gpio_version = ICH_V7_GPIO,
	},
	[LPC_ICH8DH] = {
		.name = "ICH8DH",
		.iTCO_version = 2,
		.gpio_version = ICH_V7_GPIO,
	},
	[LPC_ICH8DO] = {
		.name = "ICH8DO",
		.iTCO_version = 2,
		.gpio_version = ICH_V7_GPIO,
	},
	[LPC_ICH8M] = {
		.name = "ICH8M",
		.iTCO_version = 2,
		.gpio_version = ICH_V7_GPIO,
	},
	[LPC_ICH8ME] = {
		.name = "ICH8M-E",
		.iTCO_version = 2,
		.gpio_version = ICH_V7_GPIO,
	},
	[LPC_ICH9] = {
		.name = "ICH9",
		.iTCO_version = 2,
		.gpio_version = ICH_V9_GPIO,
	},
	[LPC_ICH9R] = {
		.name = "ICH9R",
		.iTCO_version = 2,
		.gpio_version = ICH_V9_GPIO,
	},
	[LPC_ICH9DH] = {
		.name = "ICH9DH",
		.iTCO_version = 2,
		.gpio_version = ICH_V9_GPIO,
	},
	[LPC_ICH9DO] = {
		.name = "ICH9DO",
		.iTCO_version = 2,
		.gpio_version = ICH_V9_GPIO,
	},
	[LPC_ICH9M] = {
		.name = "ICH9M",
		.iTCO_version = 2,
		.gpio_version = ICH_V9_GPIO,
	},
	[LPC_ICH9ME] = {
		.name = "ICH9M-E",
		.iTCO_version = 2,
		.gpio_version = ICH_V9_GPIO,
	},
	[LPC_ICH10] = {
		.name = "ICH10",
		.iTCO_version = 2,
		.gpio_version = ICH_V10CONS_GPIO,
	},
	[LPC_ICH10R] = {
		.name = "ICH10R",
		.iTCO_version = 2,
		.gpio_version = ICH_V10CONS_GPIO,
	},
	[LPC_ICH10D] = {
		.name = "ICH10D",
		.iTCO_version = 2,
		.gpio_version = ICH_V10CORP_GPIO,
	},
	[LPC_ICH10DO] = {
		.name = "ICH10DO",
		.iTCO_version = 2,
		.gpio_version = ICH_V10CORP_GPIO,
	},
	[LPC_PCH] = {
		.name = "PCH Desktop Full Featured",
		.iTCO_version = 2,
		.gpio_version = ICH_V5_GPIO,
	},
	[LPC_PCHM] = {
		.name = "PCH Mobile Full Featured",
		.iTCO_version = 2,
		.gpio_version = ICH_V5_GPIO,
	},
	[LPC_P55] = {
		.name = "P55",
		.iTCO_version = 2,
		.gpio_version = ICH_V5_GPIO,
	},
	[LPC_PM55] = {
		.name = "PM55",
		.iTCO_version = 2,
		.gpio_version = ICH_V5_GPIO,
	},
	[LPC_H55] = {
		.name = "H55",
		.iTCO_version = 2,
		.gpio_version = ICH_V5_GPIO,
	},
	[LPC_QM57] = {
		.name = "QM57",
		.iTCO_version = 2,
		.gpio_version = ICH_V5_GPIO,
	},
	[LPC_H57] = {
		.name = "H57",
		.iTCO_version = 2,
		.gpio_version = ICH_V5_GPIO,
	},
	[LPC_HM55] = {
		.name = "HM55",
		.iTCO_version = 2,
		.gpio_version = ICH_V5_GPIO,
	},
	[LPC_Q57] = {
		.name = "Q57",
		.iTCO_version = 2,
		.gpio_version = ICH_V5_GPIO,
	},
	[LPC_HM57] = {
		.name = "HM57",
		.iTCO_version = 2,
		.gpio_version = ICH_V5_GPIO,
	},
	[LPC_PCHMSFF] = {
		.name = "PCH Mobile SFF Full Featured",
		.iTCO_version = 2,
		.gpio_version = ICH_V5_GPIO,
	},
	[LPC_QS57] = {
		.name = "QS57",
		.iTCO_version = 2,
		.gpio_version = ICH_V5_GPIO,
	},
	[LPC_3400] = {
		.name = "3400",
		.iTCO_version = 2,
		.gpio_version = ICH_V5_GPIO,
	},
	[LPC_3420] = {
		.name = "3420",
		.iTCO_version = 2,
		.gpio_version = ICH_V5_GPIO,
	},
	[LPC_3450] = {
		.name = "3450",
		.iTCO_version = 2,
		.gpio_version = ICH_V5_GPIO,
	},
	[LPC_EP80579] = {
		.name = "EP80579",
		.iTCO_version = 2,
	},
	[LPC_CPT] = {
		.name = "Cougar Point",
		.iTCO_version = 2,
		.gpio_version = ICH_V5_GPIO,
	},
	[LPC_CPTD] = {
		.name = "Cougar Point Desktop",
		.iTCO_version = 2,
		.gpio_version = ICH_V5_GPIO,
	},
	[LPC_CPTM] = {
		.name = "Cougar Point Mobile",
		.iTCO_version = 2,
		.gpio_version = ICH_V5_GPIO,
	},
	[LPC_PBG] = {
		.name = "Patsburg",
		.iTCO_version = 2,
	},
	[LPC_DH89XXCC] = {
		.name = "DH89xxCC",
		.iTCO_version = 2,
	},
	[LPC_PPT] = {
		.name = "Panther Point",
		.iTCO_version = 2,
		.gpio_version = ICH_V5_GPIO,
	},
	[LPC_LPT] = {
		.name = "Lynx Point",
		.iTCO_version = 2,
		.gpio_version = ICH_V5_GPIO,
		.spi_type = INTEL_SPI_LPT,
	},
	[LPC_LPT_LP] = {
		.name = "Lynx Point_LP",
		.iTCO_version = 2,
		.spi_type = INTEL_SPI_LPT,
	},
	[LPC_WBG] = {
		.name = "Wellsburg",
		.iTCO_version = 2,
	},
	[LPC_AVN] = {
		.name = "Avoton SoC",
		.iTCO_version = 3,
		.gpio_version = AVOTON_GPIO,
	},
	[LPC_BAYTRAIL] = {
		.name = "Bay Trail SoC",
		.iTCO_version = 3,
		.spi_type = INTEL_SPI_BYT,
	},
	[LPC_COLETO] = {
		.name = "Coleto Creek",
		.iTCO_version = 2,
	},
	[LPC_WPT_LP] = {
		.name = "Wildcat Point_LP",
		.iTCO_version = 2,
		.spi_type = INTEL_SPI_LPT,
	},
	[LPC_BRASWELL] = {
		.name = "Braswell SoC",
		.iTCO_version = 3,
		.spi_type = INTEL_SPI_BYT,
	},
	[LPC_LEWISBURG] = {
		.name = "Lewisburg",
		.iTCO_version = 2,
	},
	[LPC_9S] = {
		.name = "9 Series",
		.iTCO_version = 2,
		.gpio_version = ICH_V5_GPIO,
	},
	[LPC_APL] = {
		.name = "Apollo Lake SoC",
		.iTCO_version = 5,
		.spi_type = INTEL_SPI_BXT,
	},
	[LPC_GLK] = {
		.name = "Gemini Lake SoC",
		.spi_type = INTEL_SPI_BXT,
	},
	[LPC_COUGARMOUNTAIN] = {
		.name = "Cougar Mountain SoC",
		.iTCO_version = 3,
	},
};

/*
 * This data only exists for exporting the supported PCI ids
 * via MODULE_DEVICE_TABLE.  We do not actually register a
 * pci_driver, because the I/O Controller Hub has also other
 * functions that probably will be registered by other drivers.
 */
static const struct pci_device_id lpc_ich_ids[] = {
	{ PCI_VDEVICE(INTEL, 0x0f1c), LPC_BAYTRAIL},
	{ PCI_VDEVICE(INTEL, 0x1c41), LPC_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c42), LPC_CPTD},
	{ PCI_VDEVICE(INTEL, 0x1c43), LPC_CPTM},
	{ PCI_VDEVICE(INTEL, 0x1c44), LPC_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c45), LPC_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c46), LPC_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c47), LPC_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c48), LPC_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c49), LPC_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c4a), LPC_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c4b), LPC_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c4c), LPC_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c4d), LPC_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c4e), LPC_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c4f), LPC_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c50), LPC_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c51), LPC_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c52), LPC_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c53), LPC_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c54), LPC_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c55), LPC_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c56), LPC_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c57), LPC_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c58), LPC_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c59), LPC_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c5a), LPC_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c5b), LPC_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c5c), LPC_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c5d), LPC_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c5e), LPC_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c5f), LPC_CPT},
	{ PCI_VDEVICE(INTEL, 0x1d40), LPC_PBG},
	{ PCI_VDEVICE(INTEL, 0x1d41), LPC_PBG},
	{ PCI_VDEVICE(INTEL, 0x1e40), LPC_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e41), LPC_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e42), LPC_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e43), LPC_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e44), LPC_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e45), LPC_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e46), LPC_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e47), LPC_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e48), LPC_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e49), LPC_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e4a), LPC_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e4b), LPC_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e4c), LPC_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e4d), LPC_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e4e), LPC_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e4f), LPC_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e50), LPC_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e51), LPC_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e52), LPC_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e53), LPC_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e54), LPC_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e55), LPC_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e56), LPC_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e57), LPC_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e58), LPC_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e59), LPC_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e5a), LPC_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e5b), LPC_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e5c), LPC_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e5d), LPC_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e5e), LPC_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e5f), LPC_PPT},
	{ PCI_VDEVICE(INTEL, 0x1f38), LPC_AVN},
	{ PCI_VDEVICE(INTEL, 0x1f39), LPC_AVN},
	{ PCI_VDEVICE(INTEL, 0x1f3a), LPC_AVN},
	{ PCI_VDEVICE(INTEL, 0x1f3b), LPC_AVN},
	{ PCI_VDEVICE(INTEL, 0x229c), LPC_BRASWELL},
	{ PCI_VDEVICE(INTEL, 0x2310), LPC_DH89XXCC},
	{ PCI_VDEVICE(INTEL, 0x2390), LPC_COLETO},
	{ PCI_VDEVICE(INTEL, 0x2410), LPC_ICH},
	{ PCI_VDEVICE(INTEL, 0x2420), LPC_ICH0},
	{ PCI_VDEVICE(INTEL, 0x2440), LPC_ICH2},
	{ PCI_VDEVICE(INTEL, 0x244c), LPC_ICH2M},
	{ PCI_VDEVICE(INTEL, 0x2450), LPC_CICH},
	{ PCI_VDEVICE(INTEL, 0x2480), LPC_ICH3},
	{ PCI_VDEVICE(INTEL, 0x248c), LPC_ICH3M},
	{ PCI_VDEVICE(INTEL, 0x24c0), LPC_ICH4},
	{ PCI_VDEVICE(INTEL, 0x24cc), LPC_ICH4M},
	{ PCI_VDEVICE(INTEL, 0x24d0), LPC_ICH5},
	{ PCI_VDEVICE(INTEL, 0x25a1), LPC_6300ESB},
	{ PCI_VDEVICE(INTEL, 0x2640), LPC_ICH6},
	{ PCI_VDEVICE(INTEL, 0x2641), LPC_ICH6M},
	{ PCI_VDEVICE(INTEL, 0x2642), LPC_ICH6W},
	{ PCI_VDEVICE(INTEL, 0x2670), LPC_631XESB},
	{ PCI_VDEVICE(INTEL, 0x2671), LPC_631XESB},
	{ PCI_VDEVICE(INTEL, 0x2672), LPC_631XESB},
	{ PCI_VDEVICE(INTEL, 0x2673), LPC_631XESB},
	{ PCI_VDEVICE(INTEL, 0x2674), LPC_631XESB},
	{ PCI_VDEVICE(INTEL, 0x2675), LPC_631XESB},
	{ PCI_VDEVICE(INTEL, 0x2676), LPC_631XESB},
	{ PCI_VDEVICE(INTEL, 0x2677), LPC_631XESB},
	{ PCI_VDEVICE(INTEL, 0x2678), LPC_631XESB},
	{ PCI_VDEVICE(INTEL, 0x2679), LPC_631XESB},
	{ PCI_VDEVICE(INTEL, 0x267a), LPC_631XESB},
	{ PCI_VDEVICE(INTEL, 0x267b), LPC_631XESB},
	{ PCI_VDEVICE(INTEL, 0x267c), LPC_631XESB},
	{ PCI_VDEVICE(INTEL, 0x267d), LPC_631XESB},
	{ PCI_VDEVICE(INTEL, 0x267e), LPC_631XESB},
	{ PCI_VDEVICE(INTEL, 0x267f), LPC_631XESB},
	{ PCI_VDEVICE(INTEL, 0x27b0), LPC_ICH7DH},
	{ PCI_VDEVICE(INTEL, 0x27b8), LPC_ICH7},
	{ PCI_VDEVICE(INTEL, 0x27b9), LPC_ICH7M},
	{ PCI_VDEVICE(INTEL, 0x27bc), LPC_NM10},
	{ PCI_VDEVICE(INTEL, 0x27bd), LPC_ICH7MDH},
	{ PCI_VDEVICE(INTEL, 0x2810), LPC_ICH8},
	{ PCI_VDEVICE(INTEL, 0x2811), LPC_ICH8ME},
	{ PCI_VDEVICE(INTEL, 0x2812), LPC_ICH8DH},
	{ PCI_VDEVICE(INTEL, 0x2814), LPC_ICH8DO},
	{ PCI_VDEVICE(INTEL, 0x2815), LPC_ICH8M},
	{ PCI_VDEVICE(INTEL, 0x2912), LPC_ICH9DH},
	{ PCI_VDEVICE(INTEL, 0x2914), LPC_ICH9DO},
	{ PCI_VDEVICE(INTEL, 0x2916), LPC_ICH9R},
	{ PCI_VDEVICE(INTEL, 0x2917), LPC_ICH9ME},
	{ PCI_VDEVICE(INTEL, 0x2918), LPC_ICH9},
	{ PCI_VDEVICE(INTEL, 0x2919), LPC_ICH9M},
	{ PCI_VDEVICE(INTEL, 0x3197), LPC_GLK},
	{ PCI_VDEVICE(INTEL, 0x2b9c), LPC_COUGARMOUNTAIN},
	{ PCI_VDEVICE(INTEL, 0x3a14), LPC_ICH10DO},
	{ PCI_VDEVICE(INTEL, 0x3a16), LPC_ICH10R},
	{ PCI_VDEVICE(INTEL, 0x3a18), LPC_ICH10},
	{ PCI_VDEVICE(INTEL, 0x3a1a), LPC_ICH10D},
	{ PCI_VDEVICE(INTEL, 0x3b00), LPC_PCH},
	{ PCI_VDEVICE(INTEL, 0x3b01), LPC_PCHM},
	{ PCI_VDEVICE(INTEL, 0x3b02), LPC_P55},
	{ PCI_VDEVICE(INTEL, 0x3b03), LPC_PM55},
	{ PCI_VDEVICE(INTEL, 0x3b06), LPC_H55},
	{ PCI_VDEVICE(INTEL, 0x3b07), LPC_QM57},
	{ PCI_VDEVICE(INTEL, 0x3b08), LPC_H57},
	{ PCI_VDEVICE(INTEL, 0x3b09), LPC_HM55},
	{ PCI_VDEVICE(INTEL, 0x3b0a), LPC_Q57},
	{ PCI_VDEVICE(INTEL, 0x3b0b), LPC_HM57},
	{ PCI_VDEVICE(INTEL, 0x3b0d), LPC_PCHMSFF},
	{ PCI_VDEVICE(INTEL, 0x3b0f), LPC_QS57},
	{ PCI_VDEVICE(INTEL, 0x3b12), LPC_3400},
	{ PCI_VDEVICE(INTEL, 0x3b14), LPC_3420},
	{ PCI_VDEVICE(INTEL, 0x3b16), LPC_3450},
	{ PCI_VDEVICE(INTEL, 0x5031), LPC_EP80579},
	{ PCI_VDEVICE(INTEL, 0x5ae8), LPC_APL},
	{ PCI_VDEVICE(INTEL, 0x8c40), LPC_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c41), LPC_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c42), LPC_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c43), LPC_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c44), LPC_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c45), LPC_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c46), LPC_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c47), LPC_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c48), LPC_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c49), LPC_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c4a), LPC_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c4b), LPC_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c4c), LPC_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c4d), LPC_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c4e), LPC_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c4f), LPC_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c50), LPC_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c51), LPC_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c52), LPC_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c53), LPC_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c54), LPC_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c55), LPC_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c56), LPC_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c57), LPC_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c58), LPC_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c59), LPC_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c5a), LPC_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c5b), LPC_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c5c), LPC_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c5d), LPC_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c5e), LPC_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c5f), LPC_LPT},
	{ PCI_VDEVICE(INTEL, 0x8cc1), LPC_9S},
	{ PCI_VDEVICE(INTEL, 0x8cc2), LPC_9S},
	{ PCI_VDEVICE(INTEL, 0x8cc3), LPC_9S},
	{ PCI_VDEVICE(INTEL, 0x8cc4), LPC_9S},
	{ PCI_VDEVICE(INTEL, 0x8cc6), LPC_9S},
	{ PCI_VDEVICE(INTEL, 0x8d40), LPC_WBG},
	{ PCI_VDEVICE(INTEL, 0x8d41), LPC_WBG},
	{ PCI_VDEVICE(INTEL, 0x8d42), LPC_WBG},
	{ PCI_VDEVICE(INTEL, 0x8d43), LPC_WBG},
	{ PCI_VDEVICE(INTEL, 0x8d44), LPC_WBG},
	{ PCI_VDEVICE(INTEL, 0x8d45), LPC_WBG},
	{ PCI_VDEVICE(INTEL, 0x8d46), LPC_WBG},
	{ PCI_VDEVICE(INTEL, 0x8d47), LPC_WBG},
	{ PCI_VDEVICE(INTEL, 0x8d48), LPC_WBG},
	{ PCI_VDEVICE(INTEL, 0x8d49), LPC_WBG},
	{ PCI_VDEVICE(INTEL, 0x8d4a), LPC_WBG},
	{ PCI_VDEVICE(INTEL, 0x8d4b), LPC_WBG},
	{ PCI_VDEVICE(INTEL, 0x8d4c), LPC_WBG},
	{ PCI_VDEVICE(INTEL, 0x8d4d), LPC_WBG},
	{ PCI_VDEVICE(INTEL, 0x8d4e), LPC_WBG},
	{ PCI_VDEVICE(INTEL, 0x8d4f), LPC_WBG},
	{ PCI_VDEVICE(INTEL, 0x8d50), LPC_WBG},
	{ PCI_VDEVICE(INTEL, 0x8d51), LPC_WBG},
	{ PCI_VDEVICE(INTEL, 0x8d52), LPC_WBG},
	{ PCI_VDEVICE(INTEL, 0x8d53), LPC_WBG},
	{ PCI_VDEVICE(INTEL, 0x8d54), LPC_WBG},
	{ PCI_VDEVICE(INTEL, 0x8d55), LPC_WBG},
	{ PCI_VDEVICE(INTEL, 0x8d56), LPC_WBG},
	{ PCI_VDEVICE(INTEL, 0x8d57), LPC_WBG},
	{ PCI_VDEVICE(INTEL, 0x8d58), LPC_WBG},
	{ PCI_VDEVICE(INTEL, 0x8d59), LPC_WBG},
	{ PCI_VDEVICE(INTEL, 0x8d5a), LPC_WBG},
	{ PCI_VDEVICE(INTEL, 0x8d5b), LPC_WBG},
	{ PCI_VDEVICE(INTEL, 0x8d5c), LPC_WBG},
	{ PCI_VDEVICE(INTEL, 0x8d5d), LPC_WBG},
	{ PCI_VDEVICE(INTEL, 0x8d5e), LPC_WBG},
	{ PCI_VDEVICE(INTEL, 0x8d5f), LPC_WBG},
	{ PCI_VDEVICE(INTEL, 0x9c40), LPC_LPT_LP},
	{ PCI_VDEVICE(INTEL, 0x9c41), LPC_LPT_LP},
	{ PCI_VDEVICE(INTEL, 0x9c42), LPC_LPT_LP},
	{ PCI_VDEVICE(INTEL, 0x9c43), LPC_LPT_LP},
	{ PCI_VDEVICE(INTEL, 0x9c44), LPC_LPT_LP},
	{ PCI_VDEVICE(INTEL, 0x9c45), LPC_LPT_LP},
	{ PCI_VDEVICE(INTEL, 0x9c46), LPC_LPT_LP},
	{ PCI_VDEVICE(INTEL, 0x9c47), LPC_LPT_LP},
	{ PCI_VDEVICE(INTEL, 0x9cc1), LPC_WPT_LP},
	{ PCI_VDEVICE(INTEL, 0x9cc2), LPC_WPT_LP},
	{ PCI_VDEVICE(INTEL, 0x9cc3), LPC_WPT_LP},
	{ PCI_VDEVICE(INTEL, 0x9cc5), LPC_WPT_LP},
	{ PCI_VDEVICE(INTEL, 0x9cc6), LPC_WPT_LP},
	{ PCI_VDEVICE(INTEL, 0x9cc7), LPC_WPT_LP},
	{ PCI_VDEVICE(INTEL, 0x9cc9), LPC_WPT_LP},
	{ PCI_VDEVICE(INTEL, 0xa1c1), LPC_LEWISBURG},
	{ PCI_VDEVICE(INTEL, 0xa1c2), LPC_LEWISBURG},
	{ PCI_VDEVICE(INTEL, 0xa1c3), LPC_LEWISBURG},
	{ PCI_VDEVICE(INTEL, 0xa1c4), LPC_LEWISBURG},
	{ PCI_VDEVICE(INTEL, 0xa1c5), LPC_LEWISBURG},
	{ PCI_VDEVICE(INTEL, 0xa1c6), LPC_LEWISBURG},
	{ PCI_VDEVICE(INTEL, 0xa1c7), LPC_LEWISBURG},
	{ PCI_VDEVICE(INTEL, 0xa242), LPC_LEWISBURG},
	{ PCI_VDEVICE(INTEL, 0xa243), LPC_LEWISBURG},
	{ 0, },			/* End of list */
};
MODULE_DEVICE_TABLE(pci, lpc_ich_ids);

static void lpc_ich_restore_config_space(struct pci_dev *dev)
{
	struct lpc_ich_priv *priv = pci_get_drvdata(dev);

	if (priv->abase_save >= 0) {
		pci_write_config_byte(dev, priv->abase, priv->abase_save);
		priv->abase_save = -1;
	}

	if (priv->actrl_pbase_save >= 0) {
		pci_write_config_byte(dev, priv->actrl_pbase,
			priv->actrl_pbase_save);
		priv->actrl_pbase_save = -1;
	}

	if (priv->gctrl_save >= 0) {
		pci_write_config_byte(dev, priv->gctrl, priv->gctrl_save);
		priv->gctrl_save = -1;
	}
}

static void lpc_ich_enable_acpi_space(struct pci_dev *dev)
{
	struct lpc_ich_priv *priv = pci_get_drvdata(dev);
	u8 reg_save;

	switch (lpc_chipset_info[priv->chipset].iTCO_version) {
	case 3:
		/*
		 * Some chipsets (eg Avoton) enable the ACPI space in the
		 * ACPI BASE register.
		 */
		pci_read_config_byte(dev, priv->abase, &reg_save);
		pci_write_config_byte(dev, priv->abase, reg_save | 0x2);
		priv->abase_save = reg_save;
		break;
	default:
		/*
		 * Most chipsets enable the ACPI space in the ACPI control
		 * register.
		 */
		pci_read_config_byte(dev, priv->actrl_pbase, &reg_save);
		pci_write_config_byte(dev, priv->actrl_pbase, reg_save | 0x80);
		priv->actrl_pbase_save = reg_save;
		break;
	}
}

static void lpc_ich_enable_gpio_space(struct pci_dev *dev)
{
	struct lpc_ich_priv *priv = pci_get_drvdata(dev);
	u8 reg_save;

	pci_read_config_byte(dev, priv->gctrl, &reg_save);
	pci_write_config_byte(dev, priv->gctrl, reg_save | 0x10);
	priv->gctrl_save = reg_save;
}

static void lpc_ich_enable_pmc_space(struct pci_dev *dev)
{
	struct lpc_ich_priv *priv = pci_get_drvdata(dev);
	u8 reg_save;

	pci_read_config_byte(dev, priv->actrl_pbase, &reg_save);
	pci_write_config_byte(dev, priv->actrl_pbase, reg_save | 0x2);

	priv->actrl_pbase_save = reg_save;
}

static int lpc_ich_finalize_wdt_cell(struct pci_dev *dev)
{
	struct itco_wdt_platform_data *pdata;
	struct lpc_ich_priv *priv = pci_get_drvdata(dev);
	struct lpc_ich_info *info;
	struct mfd_cell *cell = &lpc_ich_wdt_cell;

	pdata = devm_kzalloc(&dev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	info = &lpc_chipset_info[priv->chipset];

	pdata->version = info->iTCO_version;
	strlcpy(pdata->name, info->name, sizeof(pdata->name));

	cell->platform_data = pdata;
	cell->pdata_size = sizeof(*pdata);
	return 0;
}

static void lpc_ich_finalize_gpio_cell(struct pci_dev *dev)
{
	struct lpc_ich_priv *priv = pci_get_drvdata(dev);
	struct mfd_cell *cell = &lpc_ich_gpio_cell;

	cell->platform_data = &lpc_chipset_info[priv->chipset];
	cell->pdata_size = sizeof(struct lpc_ich_info);
}

/*
 * We don't check for resource conflict globally. There are 2 or 3 independent
 * GPIO groups and it's enough to have access to one of these to instantiate
 * the device.
 */
static int lpc_ich_check_conflict_gpio(struct resource *res)
{
	int ret;
	u8 use_gpio = 0;

	if (resource_size(res) >= 0x50 &&
	    !acpi_check_region(res->start + 0x40, 0x10, "LPC ICH GPIO3"))
		use_gpio |= 1 << 2;

	if (!acpi_check_region(res->start + 0x30, 0x10, "LPC ICH GPIO2"))
		use_gpio |= 1 << 1;

	ret = acpi_check_region(res->start + 0x00, 0x30, "LPC ICH GPIO1");
	if (!ret)
		use_gpio |= 1 << 0;

	return use_gpio ? use_gpio : ret;
}

static int lpc_ich_init_gpio(struct pci_dev *dev)
{
	struct lpc_ich_priv *priv = pci_get_drvdata(dev);
	u32 base_addr_cfg;
	u32 base_addr;
	int ret;
	bool acpi_conflict = false;
	struct resource *res;

	/* Setup power management base register */
	pci_read_config_dword(dev, priv->abase, &base_addr_cfg);
	base_addr = base_addr_cfg & 0x0000ff80;
	if (!base_addr) {
		dev_notice(&dev->dev, "I/O space for ACPI uninitialized\n");
		lpc_ich_gpio_cell.num_resources--;
		goto gpe0_done;
	}

	res = &gpio_ich_res[ICH_RES_GPE0];
	res->start = base_addr + ACPIBASE_GPE_OFF;
	res->end = base_addr + ACPIBASE_GPE_END;
	ret = acpi_check_resource_conflict(res);
	if (ret) {
		/*
		 * This isn't fatal for the GPIO, but we have to make sure that
		 * the platform_device subsystem doesn't see this resource
		 * or it will register an invalid region.
		 */
		lpc_ich_gpio_cell.num_resources--;
		acpi_conflict = true;
	} else {
		lpc_ich_enable_acpi_space(dev);
	}

gpe0_done:
	/* Setup GPIO base register */
	pci_read_config_dword(dev, priv->gbase, &base_addr_cfg);
	base_addr = base_addr_cfg & 0x0000ff80;
	if (!base_addr) {
		dev_notice(&dev->dev, "I/O space for GPIO uninitialized\n");
		ret = -ENODEV;
		goto gpio_done;
	}

	/* Older devices provide fewer GPIO and have a smaller resource size. */
	res = &gpio_ich_res[ICH_RES_GPIO];
	res->start = base_addr;
	switch (lpc_chipset_info[priv->chipset].gpio_version) {
	case ICH_V5_GPIO:
	case ICH_V10CORP_GPIO:
		res->end = res->start + 128 - 1;
		break;
	default:
		res->end = res->start + 64 - 1;
		break;
	}

	ret = lpc_ich_check_conflict_gpio(res);
	if (ret < 0) {
		/* this isn't necessarily fatal for the GPIO */
		acpi_conflict = true;
		goto gpio_done;
	}
	lpc_chipset_info[priv->chipset].use_gpio = ret;
	lpc_ich_enable_gpio_space(dev);

	lpc_ich_finalize_gpio_cell(dev);
	ret = mfd_add_devices(&dev->dev, PLATFORM_DEVID_AUTO,
			      &lpc_ich_gpio_cell, 1, NULL, 0, NULL);

gpio_done:
	if (acpi_conflict)
		pr_warn("Resource conflict(s) found affecting %s\n",
				lpc_ich_gpio_cell.name);
	return ret;
}

static int lpc_ich_init_wdt(struct pci_dev *dev)
{
	struct lpc_ich_priv *priv = pci_get_drvdata(dev);
	u32 base_addr_cfg;
	u32 base_addr;
	int ret;
	struct resource *res;

	/* If we have ACPI based watchdog use that instead */
	if (acpi_has_watchdog())
		return -ENODEV;

	/* Setup power management base register */
	pci_read_config_dword(dev, priv->abase, &base_addr_cfg);
	base_addr = base_addr_cfg & 0x0000ff80;
	if (!base_addr) {
		dev_notice(&dev->dev, "I/O space for ACPI uninitialized\n");
		ret = -ENODEV;
		goto wdt_done;
	}

	res = wdt_io_res(ICH_RES_IO_TCO);
	res->start = base_addr + ACPIBASE_TCO_OFF;
	res->end = base_addr + ACPIBASE_TCO_END;

	res = wdt_io_res(ICH_RES_IO_SMI);
	res->start = base_addr + ACPIBASE_SMI_OFF;
	res->end = base_addr + ACPIBASE_SMI_END;

	lpc_ich_enable_acpi_space(dev);

	/*
	 * iTCO v2:
	 * Get the Memory-Mapped GCS register. To get access to it
	 * we have to read RCBA from PCI Config space 0xf0 and use
	 * it as base. GCS = RCBA + ICH6_GCS(0x3410).
	 *
	 * iTCO v3:
	 * Get the Power Management Configuration register.  To get access
	 * to it we have to read the PMC BASE from config space and address
	 * the register at offset 0x8.
	 */
	if (lpc_chipset_info[priv->chipset].iTCO_version == 1) {
		/* Don't register iomem for TCO ver 1 */
		lpc_ich_wdt_cell.num_resources--;
	} else if (lpc_chipset_info[priv->chipset].iTCO_version == 2) {
		pci_read_config_dword(dev, RCBABASE, &base_addr_cfg);
		base_addr = base_addr_cfg & 0xffffc000;
		if (!(base_addr_cfg & 1)) {
			dev_notice(&dev->dev, "RCBA is disabled by "
					"hardware/BIOS, device disabled\n");
			ret = -ENODEV;
			goto wdt_done;
		}
		res = wdt_mem_res(ICH_RES_MEM_GCS_PMC);
		res->start = base_addr + ACPIBASE_GCS_OFF;
		res->end = base_addr + ACPIBASE_GCS_END;
	} else if (lpc_chipset_info[priv->chipset].iTCO_version == 3) {
		lpc_ich_enable_pmc_space(dev);
		pci_read_config_dword(dev, ACPICTRL_PMCBASE, &base_addr_cfg);
		base_addr = base_addr_cfg & 0xfffffe00;

		res = wdt_mem_res(ICH_RES_MEM_GCS_PMC);
		res->start = base_addr + ACPIBASE_PMC_OFF;
		res->end = base_addr + ACPIBASE_PMC_END;
	}

	ret = lpc_ich_finalize_wdt_cell(dev);
	if (ret)
		goto wdt_done;

	ret = mfd_add_devices(&dev->dev, PLATFORM_DEVID_AUTO,
			      &lpc_ich_wdt_cell, 1, NULL, 0, NULL);

wdt_done:
	return ret;
}

static int lpc_ich_init_spi(struct pci_dev *dev)
{
	struct lpc_ich_priv *priv = pci_get_drvdata(dev);
	struct resource *res = &intel_spi_res[0];
	struct intel_spi_boardinfo *info;
	u32 spi_base, rcba, bcr;

	info = devm_kzalloc(&dev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->type = lpc_chipset_info[priv->chipset].spi_type;

	switch (info->type) {
	case INTEL_SPI_BYT:
		pci_read_config_dword(dev, SPIBASE_BYT, &spi_base);
		if (spi_base & SPIBASE_BYT_EN) {
			res->start = spi_base & ~(SPIBASE_BYT_SZ - 1);
			res->end = res->start + SPIBASE_BYT_SZ - 1;
		}
		break;

	case INTEL_SPI_LPT:
		pci_read_config_dword(dev, RCBABASE, &rcba);
		if (rcba & 1) {
			spi_base = round_down(rcba, SPIBASE_LPT_SZ);
			res->start = spi_base + SPIBASE_LPT;
			res->end = res->start + SPIBASE_LPT_SZ - 1;

			/*
			 * Try to make the flash chip writeable now by
			 * setting BCR_WPD. It it fails we tell the driver
			 * that it can only read the chip.
			 */
			pci_read_config_dword(dev, BCR, &bcr);
			if (!(bcr & BCR_WPD)) {
				bcr |= BCR_WPD;
				pci_write_config_dword(dev, BCR, bcr);
				pci_read_config_dword(dev, BCR, &bcr);
			}
			info->writeable = !!(bcr & BCR_WPD);
		}
		break;

	case INTEL_SPI_BXT: {
		unsigned int p2sb = PCI_DEVFN(13, 0);
		unsigned int spi = PCI_DEVFN(13, 2);
		struct pci_bus *bus = dev->bus;

		/*
		 * The P2SB is hidden by BIOS and we need to unhide it in
		 * order to read BAR of the SPI flash device. Once that is
		 * done we hide it again.
		 */
		pci_bus_write_config_byte(bus, p2sb, 0xe1, 0x0);
		pci_bus_read_config_dword(bus, spi, PCI_BASE_ADDRESS_0,
					  &spi_base);
		if (spi_base != ~0) {
			res->start = spi_base & 0xfffffff0;
			res->end = res->start + SPIBASE_APL_SZ - 1;

			pci_bus_read_config_dword(bus, spi, BCR, &bcr);
			if (!(bcr & BCR_WPD)) {
				bcr |= BCR_WPD;
				pci_bus_write_config_dword(bus, spi, BCR, bcr);
				pci_bus_read_config_dword(bus, spi, BCR, &bcr);
			}
			info->writeable = !!(bcr & BCR_WPD);
		}

		pci_bus_write_config_byte(bus, p2sb, 0xe1, 0x1);
		break;
	}

	default:
		return -EINVAL;
	}

	if (!res->start)
		return -ENODEV;

	lpc_ich_spi_cell.platform_data = info;
	lpc_ich_spi_cell.pdata_size = sizeof(*info);

	return mfd_add_devices(&dev->dev, PLATFORM_DEVID_NONE,
			       &lpc_ich_spi_cell, 1, NULL, 0, NULL);
}

static int lpc_ich_probe(struct pci_dev *dev,
				const struct pci_device_id *id)
{
	struct lpc_ich_priv *priv;
	int ret;
	bool cell_added = false;

	priv = devm_kzalloc(&dev->dev,
			    sizeof(struct lpc_ich_priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->chipset = id->driver_data;

	priv->actrl_pbase_save = -1;
	priv->abase_save = -1;

	priv->abase = ACPIBASE;
	priv->actrl_pbase = ACPICTRL_PMCBASE;

	priv->gctrl_save = -1;
	if (priv->chipset <= LPC_ICH5) {
		priv->gbase = GPIOBASE_ICH0;
		priv->gctrl = GPIOCTRL_ICH0;
	} else {
		priv->gbase = GPIOBASE_ICH6;
		priv->gctrl = GPIOCTRL_ICH6;
	}

	pci_set_drvdata(dev, priv);

	if (lpc_chipset_info[priv->chipset].iTCO_version) {
		ret = lpc_ich_init_wdt(dev);
		if (!ret)
			cell_added = true;
	}

	if (lpc_chipset_info[priv->chipset].gpio_version) {
		ret = lpc_ich_init_gpio(dev);
		if (!ret)
			cell_added = true;
	}

	if (lpc_chipset_info[priv->chipset].spi_type) {
		ret = lpc_ich_init_spi(dev);
		if (!ret)
			cell_added = true;
	}

	/*
	 * We only care if at least one or none of the cells registered
	 * successfully.
	 */
	if (!cell_added) {
		dev_warn(&dev->dev, "No MFD cells added\n");
		lpc_ich_restore_config_space(dev);
		return -ENODEV;
	}

	return 0;
}

static void lpc_ich_remove(struct pci_dev *dev)
{
	mfd_remove_devices(&dev->dev);
	lpc_ich_restore_config_space(dev);
}

static struct pci_driver lpc_ich_driver = {
	.name		= "lpc_ich",
	.id_table	= lpc_ich_ids,
	.probe		= lpc_ich_probe,
	.remove		= lpc_ich_remove,
};

module_pci_driver(lpc_ich_driver);

MODULE_AUTHOR("Aaron Sierra <asierra@xes-inc.com>");
MODULE_DESCRIPTION("LPC interface for Intel ICH");
MODULE_LICENSE("GPL");
