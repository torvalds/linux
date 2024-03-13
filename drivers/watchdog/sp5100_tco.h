/* SPDX-License-Identifier: GPL-2.0 */
/*
 *	sp5100_tco:	TCO timer driver for sp5100 chipsets.
 *
 *	(c) Copyright 2009 Google Inc., All Rights Reserved.
 *
 *	TCO timer driver for sp5100 chipsets
 */

#include <linux/bitops.h>

/*
 * Some address definitions for the Watchdog
 */
#define SP5100_WDT_MEM_MAP_SIZE		0x08
#define SP5100_WDT_CONTROL(base)	((base) + 0x00) /* Watchdog Control */
#define SP5100_WDT_COUNT(base)		((base) + 0x04) /* Watchdog Count */

#define SP5100_WDT_START_STOP_BIT	BIT(0)
#define SP5100_WDT_FIRED		BIT(1)
#define SP5100_WDT_ACTION_RESET		BIT(2)
#define SP5100_WDT_DISABLED		BIT(3)
#define SP5100_WDT_TRIGGER_BIT		BIT(7)

#define SP5100_PM_IOPORTS_SIZE		0x02

/*
 * These two IO registers are hardcoded and there doesn't seem to be a way to
 * read them from a register.
 */

/*  For SP5100/SB7x0/SB8x0 chipset */
#define SP5100_IO_PM_INDEX_REG		0xCD6
#define SP5100_IO_PM_DATA_REG		0xCD7

/* For SP5100/SB7x0 chipset */
#define SP5100_SB_RESOURCE_MMIO_BASE	0x9C

#define SP5100_PM_WATCHDOG_CONTROL	0x69
#define SP5100_PM_WATCHDOG_BASE		0x6C

#define SP5100_PCI_WATCHDOG_MISC_REG	0x41
#define SP5100_PCI_WATCHDOG_DECODE_EN	BIT(3)

#define SP5100_PM_WATCHDOG_DISABLE	((u8)BIT(0))
#define SP5100_PM_WATCHDOG_SECOND_RES	GENMASK(2, 1)

#define SP5100_DEVNAME			"SP5100 TCO"

/*  For SB8x0(or later) chipset */
#define SB800_PM_ACPI_MMIO_EN		0x24
#define SB800_PM_WATCHDOG_CONTROL	0x48
#define SB800_PM_WATCHDOG_BASE		0x48
#define SB800_PM_WATCHDOG_CONFIG	0x4C

#define SB800_PCI_WATCHDOG_DECODE_EN	BIT(0)
#define SB800_PM_WATCHDOG_DISABLE	((u8)BIT(1))
#define SB800_PM_WATCHDOG_SECOND_RES	GENMASK(1, 0)
#define SB800_ACPI_MMIO_DECODE_EN	BIT(0)
#define SB800_ACPI_MMIO_SEL		BIT(1)
#define SB800_ACPI_MMIO_MASK		GENMASK(1, 0)

#define SB800_PM_WDT_MMIO_OFFSET	0xB00

#define SB800_DEVNAME			"SB800 TCO"

/* For recent chips with embedded FCH (rev 40+) */

#define EFCH_PM_DECODEEN		0x00

#define EFCH_PM_DECODEEN_WDT_TMREN	BIT(7)


#define EFCH_PM_DECODEEN3		0x03
#define EFCH_PM_DECODEEN_SECOND_RES	GENMASK(1, 0)
#define EFCH_PM_WATCHDOG_DISABLE	((u8)GENMASK(3, 2))

/* WDT MMIO if enabled with PM00_DECODEEN_WDT_TMREN */
#define EFCH_PM_WDT_ADDR		0xfeb00000

#define EFCH_PM_ISACONTROL		0x04

#define EFCH_PM_ISACONTROL_MMIOEN	BIT(1)

#define EFCH_PM_ACPI_MMIO_ADDR		0xfed80000
#define EFCH_PM_ACPI_MMIO_PM_OFFSET	0x00000300
#define EFCH_PM_ACPI_MMIO_WDT_OFFSET	0x00000b00

#define EFCH_PM_ACPI_MMIO_PM_ADDR	(EFCH_PM_ACPI_MMIO_ADDR +	\
					 EFCH_PM_ACPI_MMIO_PM_OFFSET)
#define EFCH_PM_ACPI_MMIO_PM_SIZE	8
#define AMD_ZEN_SMBUS_PCI_REV		0x51
