/*
 * Copyright (C) 2013 BayHub Technology Ltd.
 *
 * Authors: Peter Guo <peter.guo@bayhubtech.com>
 *          Adam Lee <adam.lee@canonical.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __SDHCI_PCI_O2MICRO_H
#define __SDHCI_PCI_O2MICRO_H

#include "sdhci-pci.h"

/*
 * O2Micro device IDs
 */

#define PCI_DEVICE_ID_O2_SDS0		0x8420
#define PCI_DEVICE_ID_O2_SDS1		0x8421
#define PCI_DEVICE_ID_O2_FUJIN2		0x8520
#define PCI_DEVICE_ID_O2_SEABIRD0	0x8620
#define PCI_DEVICE_ID_O2_SEABIRD1	0x8621

/*
 * O2Micro device registers
 */

#define O2_SD_MISC_REG5		0x64
#define O2_SD_LD0_CTRL		0x68
#define O2_SD_DEV_CTRL		0x88
#define O2_SD_LOCK_WP		0xD3
#define O2_SD_TEST_REG		0xD4
#define O2_SD_FUNC_REG0		0xDC
#define O2_SD_MULTI_VCC3V	0xEE
#define O2_SD_CLKREQ		0xEC
#define O2_SD_CAPS		0xE0
#define O2_SD_ADMA1		0xE2
#define O2_SD_ADMA2		0xE7
#define O2_SD_INF_MOD		0xF1
#define O2_SD_MISC_CTRL4	0xFC
#define O2_SD_TUNING_CTRL	0x300
#define O2_SD_PLL_SETTING	0x304
#define O2_SD_CLK_SETTING	0x328
#define O2_SD_CAP_REG2		0x330
#define O2_SD_CAP_REG0		0x334
#define O2_SD_UHS1_CAP_SETTING	0x33C
#define O2_SD_DELAY_CTRL	0x350
#define O2_SD_UHS2_L1_CTRL	0x35C
#define O2_SD_FUNC_REG3		0x3E0
#define O2_SD_FUNC_REG4		0x3E4

#define O2_SD_VENDOR_SETTING	0x110
#define O2_SD_VENDOR_SETTING2	0x1C8

extern void sdhci_pci_o2_fujin2_pci_init(struct sdhci_pci_chip *chip);

extern int sdhci_pci_o2_probe_slot(struct sdhci_pci_slot *slot);

extern int sdhci_pci_o2_probe(struct sdhci_pci_chip *chip);

extern int sdhci_pci_o2_resume(struct sdhci_pci_chip *chip);

#endif /* __SDHCI_PCI_O2MICRO_H */
