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

#include <linux/pci.h>

#include "sdhci.h"
#include "sdhci-pci.h"
#include "sdhci-pci-o2micro.h"

static void o2_pci_set_baseclk(struct sdhci_pci_chip *chip, u32 value)
{
	u32 scratch_32;
	pci_read_config_dword(chip->pdev,
			      O2_SD_PLL_SETTING, &scratch_32);

	scratch_32 &= 0x0000FFFF;
	scratch_32 |= value;

	pci_write_config_dword(chip->pdev,
			       O2_SD_PLL_SETTING, scratch_32);
}

static void o2_pci_led_enable(struct sdhci_pci_chip *chip)
{
	int ret;
	u32 scratch_32;

	/* Set led of SD host function enable */
	ret = pci_read_config_dword(chip->pdev,
				    O2_SD_FUNC_REG0, &scratch_32);
	if (ret)
		return;

	scratch_32 &= ~O2_SD_FREG0_LEDOFF;
	pci_write_config_dword(chip->pdev,
			       O2_SD_FUNC_REG0, scratch_32);

	ret = pci_read_config_dword(chip->pdev,
				    O2_SD_TEST_REG, &scratch_32);
	if (ret)
		return;

	scratch_32 |= O2_SD_LED_ENABLE;
	pci_write_config_dword(chip->pdev,
			       O2_SD_TEST_REG, scratch_32);

}

void sdhci_pci_o2_fujin2_pci_init(struct sdhci_pci_chip *chip)
{
	u32 scratch_32;
	int ret;
	/* Improve write performance for SD3.0 */
	ret = pci_read_config_dword(chip->pdev, O2_SD_DEV_CTRL, &scratch_32);
	if (ret)
		return;
	scratch_32 &= ~((1 << 12) | (1 << 13) | (1 << 14));
	pci_write_config_dword(chip->pdev, O2_SD_DEV_CTRL, scratch_32);

	/* Enable Link abnormal reset generating Reset */
	ret = pci_read_config_dword(chip->pdev, O2_SD_MISC_REG5, &scratch_32);
	if (ret)
		return;
	scratch_32 &= ~((1 << 19) | (1 << 11));
	scratch_32 |= (1 << 10);
	pci_write_config_dword(chip->pdev, O2_SD_MISC_REG5, scratch_32);

	/* set card power over current protection */
	ret = pci_read_config_dword(chip->pdev, O2_SD_TEST_REG, &scratch_32);
	if (ret)
		return;
	scratch_32 |= (1 << 4);
	pci_write_config_dword(chip->pdev, O2_SD_TEST_REG, scratch_32);

	/* adjust the output delay for SD mode */
	pci_write_config_dword(chip->pdev, O2_SD_DELAY_CTRL, 0x00002492);

	/* Set the output voltage setting of Aux 1.2v LDO */
	ret = pci_read_config_dword(chip->pdev, O2_SD_LD0_CTRL, &scratch_32);
	if (ret)
		return;
	scratch_32 &= ~(3 << 12);
	pci_write_config_dword(chip->pdev, O2_SD_LD0_CTRL, scratch_32);

	/* Set Max power supply capability of SD host */
	ret = pci_read_config_dword(chip->pdev, O2_SD_CAP_REG0, &scratch_32);
	if (ret)
		return;
	scratch_32 &= ~(0x01FE);
	scratch_32 |= 0x00CC;
	pci_write_config_dword(chip->pdev, O2_SD_CAP_REG0, scratch_32);
	/* Set DLL Tuning Window */
	ret = pci_read_config_dword(chip->pdev,
				    O2_SD_TUNING_CTRL, &scratch_32);
	if (ret)
		return;
	scratch_32 &= ~(0x000000FF);
	scratch_32 |= 0x00000066;
	pci_write_config_dword(chip->pdev, O2_SD_TUNING_CTRL, scratch_32);

	/* Set UHS2 T_EIDLE */
	ret = pci_read_config_dword(chip->pdev,
				    O2_SD_UHS2_L1_CTRL, &scratch_32);
	if (ret)
		return;
	scratch_32 &= ~(0x000000FC);
	scratch_32 |= 0x00000084;
	pci_write_config_dword(chip->pdev, O2_SD_UHS2_L1_CTRL, scratch_32);

	/* Set UHS2 Termination */
	ret = pci_read_config_dword(chip->pdev, O2_SD_FUNC_REG3, &scratch_32);
	if (ret)
		return;
	scratch_32 &= ~((1 << 21) | (1 << 30));

	/* Set RTD3 function disabled */
	scratch_32 |= ((1 << 29) | (1 << 28));
	pci_write_config_dword(chip->pdev, O2_SD_FUNC_REG3, scratch_32);

	/* Set L1 Entrance Timer */
	ret = pci_read_config_dword(chip->pdev, O2_SD_CAPS, &scratch_32);
	if (ret)
		return;
	scratch_32 &= ~(0xf0000000);
	scratch_32 |= 0x30000000;
	pci_write_config_dword(chip->pdev, O2_SD_CAPS, scratch_32);

	ret = pci_read_config_dword(chip->pdev,
				    O2_SD_MISC_CTRL4, &scratch_32);
	if (ret)
		return;
	scratch_32 &= ~(0x000f0000);
	scratch_32 |= 0x00080000;
	pci_write_config_dword(chip->pdev, O2_SD_MISC_CTRL4, scratch_32);
}
EXPORT_SYMBOL_GPL(sdhci_pci_o2_fujin2_pci_init);

int sdhci_pci_o2_probe_slot(struct sdhci_pci_slot *slot)
{
	struct sdhci_pci_chip *chip;
	struct sdhci_host *host;
	u32 reg;

	chip = slot->chip;
	host = slot->host;
	switch (chip->pdev->device) {
	case PCI_DEVICE_ID_O2_SDS0:
	case PCI_DEVICE_ID_O2_SEABIRD0:
	case PCI_DEVICE_ID_O2_SEABIRD1:
	case PCI_DEVICE_ID_O2_SDS1:
	case PCI_DEVICE_ID_O2_FUJIN2:
		reg = sdhci_readl(host, O2_SD_VENDOR_SETTING);
		if (reg & 0x1)
			host->quirks |= SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12;

		if (chip->pdev->device != PCI_DEVICE_ID_O2_FUJIN2)
			break;
		/* set dll watch dog timer */
		reg = sdhci_readl(host, O2_SD_VENDOR_SETTING2);
		reg |= (1 << 12);
		sdhci_writel(host, reg, O2_SD_VENDOR_SETTING2);

		break;
	default:
		break;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(sdhci_pci_o2_probe_slot);

int sdhci_pci_o2_probe(struct sdhci_pci_chip *chip)
{
	int ret;
	u8 scratch;
	u32 scratch_32;

	switch (chip->pdev->device) {
	case PCI_DEVICE_ID_O2_8220:
	case PCI_DEVICE_ID_O2_8221:
	case PCI_DEVICE_ID_O2_8320:
	case PCI_DEVICE_ID_O2_8321:
		/* This extra setup is required due to broken ADMA. */
		ret = pci_read_config_byte(chip->pdev,
				O2_SD_LOCK_WP, &scratch);
		if (ret)
			return ret;
		scratch &= 0x7f;
		pci_write_config_byte(chip->pdev, O2_SD_LOCK_WP, scratch);

		/* Set Multi 3 to VCC3V# */
		pci_write_config_byte(chip->pdev, O2_SD_MULTI_VCC3V, 0x08);

		/* Disable CLK_REQ# support after media DET */
		ret = pci_read_config_byte(chip->pdev,
				O2_SD_CLKREQ, &scratch);
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
		ret = pci_read_config_byte(chip->pdev,
				O2_SD_INF_MOD, &scratch);
		if (ret)
			return ret;
		scratch |= 0x08;
		pci_write_config_byte(chip->pdev, O2_SD_INF_MOD, scratch);

		/* Lock WP */
		ret = pci_read_config_byte(chip->pdev,
				O2_SD_LOCK_WP, &scratch);
		if (ret)
			return ret;
		scratch |= 0x80;
		pci_write_config_byte(chip->pdev, O2_SD_LOCK_WP, scratch);
		break;
	case PCI_DEVICE_ID_O2_SDS0:
	case PCI_DEVICE_ID_O2_SDS1:
	case PCI_DEVICE_ID_O2_FUJIN2:
		/* UnLock WP */
		ret = pci_read_config_byte(chip->pdev,
				O2_SD_LOCK_WP, &scratch);
		if (ret)
			return ret;

		scratch &= 0x7f;
		pci_write_config_byte(chip->pdev, O2_SD_LOCK_WP, scratch);

		/* DevId=8520 subId= 0x11 or 0x12  Type Chip support */
		if (chip->pdev->device == PCI_DEVICE_ID_O2_FUJIN2) {
			ret = pci_read_config_dword(chip->pdev,
						    O2_SD_FUNC_REG0,
						    &scratch_32);
			scratch_32 = ((scratch_32 & 0xFF000000) >> 24);

			/* Check Whether subId is 0x11 or 0x12 */
			if ((scratch_32 == 0x11) || (scratch_32 == 0x12)) {
				scratch_32 = 0x2c280000;

				/* Set Base Clock to 208MZ */
				o2_pci_set_baseclk(chip, scratch_32);
				ret = pci_read_config_dword(chip->pdev,
							    O2_SD_FUNC_REG4,
							    &scratch_32);

				/* Enable Base Clk setting change */
				scratch_32 |= O2_SD_FREG4_ENABLE_CLK_SET;
				pci_write_config_dword(chip->pdev,
						       O2_SD_FUNC_REG4,
						       scratch_32);

				/* Set Tuning Window to 4 */
				pci_write_config_byte(chip->pdev,
						      O2_SD_TUNING_CTRL, 0x44);

				break;
			}
		}

		/* Enable 8520 led function */
		o2_pci_led_enable(chip);

		/* Set timeout CLK */
		ret = pci_read_config_dword(chip->pdev,
					    O2_SD_CLK_SETTING, &scratch_32);
		if (ret)
			return ret;

		scratch_32 &= ~(0xFF00);
		scratch_32 |= 0x07E0C800;
		pci_write_config_dword(chip->pdev,
				       O2_SD_CLK_SETTING, scratch_32);

		ret = pci_read_config_dword(chip->pdev,
					    O2_SD_CLKREQ, &scratch_32);
		if (ret)
			return ret;
		scratch_32 |= 0x3;
		pci_write_config_dword(chip->pdev, O2_SD_CLKREQ, scratch_32);

		ret = pci_read_config_dword(chip->pdev,
					    O2_SD_PLL_SETTING, &scratch_32);
		if (ret)
			return ret;

		scratch_32 &= ~(0x1F3F070E);
		scratch_32 |= 0x18270106;
		pci_write_config_dword(chip->pdev,
				       O2_SD_PLL_SETTING, scratch_32);

		/* Disable UHS1 funciton */
		ret = pci_read_config_dword(chip->pdev,
					    O2_SD_CAP_REG2, &scratch_32);
		if (ret)
			return ret;
		scratch_32 &= ~(0xE0);
		pci_write_config_dword(chip->pdev,
				       O2_SD_CAP_REG2, scratch_32);

		if (chip->pdev->device == PCI_DEVICE_ID_O2_FUJIN2)
			sdhci_pci_o2_fujin2_pci_init(chip);

		/* Lock WP */
		ret = pci_read_config_byte(chip->pdev,
					   O2_SD_LOCK_WP, &scratch);
		if (ret)
			return ret;
		scratch |= 0x80;
		pci_write_config_byte(chip->pdev, O2_SD_LOCK_WP, scratch);
		break;
	case PCI_DEVICE_ID_O2_SEABIRD0:
	case PCI_DEVICE_ID_O2_SEABIRD1:
		/* UnLock WP */
		ret = pci_read_config_byte(chip->pdev,
				O2_SD_LOCK_WP, &scratch);
		if (ret)
			return ret;

		scratch &= 0x7f;
		pci_write_config_byte(chip->pdev, O2_SD_LOCK_WP, scratch);

		ret = pci_read_config_dword(chip->pdev,
					    O2_SD_PLL_SETTING, &scratch_32);

		if ((scratch_32 & 0xff000000) == 0x01000000) {
			scratch_32 &= 0x0000FFFF;
			scratch_32 |= 0x1F340000;

			pci_write_config_dword(chip->pdev,
					       O2_SD_PLL_SETTING, scratch_32);
		} else {
			scratch_32 &= 0x0000FFFF;
			scratch_32 |= 0x2c280000;

			pci_write_config_dword(chip->pdev,
					       O2_SD_PLL_SETTING, scratch_32);

			ret = pci_read_config_dword(chip->pdev,
						    O2_SD_FUNC_REG4,
						    &scratch_32);
			scratch_32 |= (1 << 22);
			pci_write_config_dword(chip->pdev,
					       O2_SD_FUNC_REG4, scratch_32);
		}

		/* Set Tuning Windows to 5 */
		pci_write_config_byte(chip->pdev,
				O2_SD_TUNING_CTRL, 0x55);
		/* Lock WP */
		ret = pci_read_config_byte(chip->pdev,
					   O2_SD_LOCK_WP, &scratch);
		if (ret)
			return ret;
		scratch |= 0x80;
		pci_write_config_byte(chip->pdev, O2_SD_LOCK_WP, scratch);
		break;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(sdhci_pci_o2_probe);

int sdhci_pci_o2_resume(struct sdhci_pci_chip *chip)
{
	sdhci_pci_o2_probe(chip);
	return 0;
}
EXPORT_SYMBOL_GPL(sdhci_pci_o2_resume);
