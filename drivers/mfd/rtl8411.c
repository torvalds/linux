/* Driver for Realtek PCI-Express card reader
 *
 * Copyright(c) 2009 Realtek Semiconductor Corp. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author:
 *   Wei WANG <wei_wang@realsil.com.cn>
 *   No. 450, Shenhu Road, Suzhou Industry Park, Suzhou, China
 */

#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/mfd/rtsx_pci.h>

#include "rtsx_pcr.h"

static u8 rtl8411_get_ic_version(struct rtsx_pcr *pcr)
{
	u8 val;

	rtsx_pci_read_register(pcr, SYS_VER, &val);
	return val & 0x0F;
}

static int rtl8411_extra_init_hw(struct rtsx_pcr *pcr)
{
	return rtsx_pci_write_register(pcr, CD_PAD_CTL,
			CD_DISABLE_MASK | CD_AUTO_DISABLE, CD_ENABLE);
}

static int rtl8411_turn_on_led(struct rtsx_pcr *pcr)
{
	return rtsx_pci_write_register(pcr, CARD_GPIO, 0x01, 0x00);
}

static int rtl8411_turn_off_led(struct rtsx_pcr *pcr)
{
	return rtsx_pci_write_register(pcr, CARD_GPIO, 0x01, 0x01);
}

static int rtl8411_enable_auto_blink(struct rtsx_pcr *pcr)
{
	return rtsx_pci_write_register(pcr, CARD_AUTO_BLINK, 0xFF, 0x0D);
}

static int rtl8411_disable_auto_blink(struct rtsx_pcr *pcr)
{
	return rtsx_pci_write_register(pcr, CARD_AUTO_BLINK, 0x08, 0x00);
}

static int rtl8411_card_power_on(struct rtsx_pcr *pcr, int card)
{
	int err;

	rtsx_pci_init_cmd(pcr);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CARD_PWR_CTL,
			BPP_POWER_MASK, BPP_POWER_5_PERCENT_ON);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, LDO_CTL,
			BPP_LDO_POWB, BPP_LDO_SUSPEND);
	err = rtsx_pci_send_cmd(pcr, 100);
	if (err < 0)
		return err;

	/* To avoid too large in-rush current */
	udelay(150);

	err = rtsx_pci_write_register(pcr, CARD_PWR_CTL,
			BPP_POWER_MASK, BPP_POWER_10_PERCENT_ON);
	if (err < 0)
		return err;

	udelay(150);

	err = rtsx_pci_write_register(pcr, CARD_PWR_CTL,
			BPP_POWER_MASK, BPP_POWER_15_PERCENT_ON);
	if (err < 0)
		return err;

	udelay(150);

	err = rtsx_pci_write_register(pcr, CARD_PWR_CTL,
			BPP_POWER_MASK, BPP_POWER_ON);
	if (err < 0)
		return err;

	return rtsx_pci_write_register(pcr, LDO_CTL, BPP_LDO_POWB, BPP_LDO_ON);
}

static int rtl8411_card_power_off(struct rtsx_pcr *pcr, int card)
{
	int err;

	err = rtsx_pci_write_register(pcr, CARD_PWR_CTL,
			BPP_POWER_MASK, BPP_POWER_OFF);
	if (err < 0)
		return err;

	return rtsx_pci_write_register(pcr, LDO_CTL,
			BPP_LDO_POWB, BPP_LDO_SUSPEND);
}

static int rtl8411_switch_output_voltage(struct rtsx_pcr *pcr, u8 voltage)
{
	u8 mask, val;
	int err;

	mask = (BPP_REG_TUNED18 << BPP_TUNED18_SHIFT_8411) | BPP_PAD_MASK;
	if (voltage == OUTPUT_3V3) {
		err = rtsx_pci_write_register(pcr,
				SD30_DRIVE_SEL, 0x07, DRIVER_TYPE_D);
		if (err < 0)
			return err;
		val = (BPP_ASIC_3V3 << BPP_TUNED18_SHIFT_8411) | BPP_PAD_3V3;
	} else if (voltage == OUTPUT_1V8) {
		err = rtsx_pci_write_register(pcr,
				SD30_DRIVE_SEL, 0x07, DRIVER_TYPE_B);
		if (err < 0)
			return err;
		val = (BPP_ASIC_1V8 << BPP_TUNED18_SHIFT_8411) | BPP_PAD_1V8;
	} else {
		return -EINVAL;
	}

	return rtsx_pci_write_register(pcr, LDO_CTL, mask, val);
}

static unsigned int rtl8411_cd_deglitch(struct rtsx_pcr *pcr)
{
	unsigned int card_exist;

	card_exist = rtsx_pci_readl(pcr, RTSX_BIPR);
	card_exist &= CARD_EXIST;
	if (!card_exist) {
		/* Enable card CD */
		rtsx_pci_write_register(pcr, CD_PAD_CTL,
				CD_DISABLE_MASK, CD_ENABLE);
		/* Enable card interrupt */
		rtsx_pci_write_register(pcr, EFUSE_CONTENT, 0xe0, 0x00);
		return 0;
	}

	if (hweight32(card_exist) > 1) {
		rtsx_pci_write_register(pcr, CARD_PWR_CTL,
				BPP_POWER_MASK, BPP_POWER_5_PERCENT_ON);
		msleep(100);

		card_exist = rtsx_pci_readl(pcr, RTSX_BIPR);
		if (card_exist & MS_EXIST)
			card_exist = MS_EXIST;
		else if (card_exist & SD_EXIST)
			card_exist = SD_EXIST;
		else
			card_exist = 0;

		rtsx_pci_write_register(pcr, CARD_PWR_CTL,
				BPP_POWER_MASK, BPP_POWER_OFF);

		dev_dbg(&(pcr->pci->dev),
				"After CD deglitch, card_exist = 0x%x\n",
				card_exist);
	}

	if (card_exist & MS_EXIST) {
		/* Disable SD interrupt */
		rtsx_pci_write_register(pcr, EFUSE_CONTENT, 0xe0, 0x40);
		rtsx_pci_write_register(pcr, CD_PAD_CTL,
				CD_DISABLE_MASK, MS_CD_EN_ONLY);
	} else if (card_exist & SD_EXIST) {
		/* Disable MS interrupt */
		rtsx_pci_write_register(pcr, EFUSE_CONTENT, 0xe0, 0x80);
		rtsx_pci_write_register(pcr, CD_PAD_CTL,
				CD_DISABLE_MASK, SD_CD_EN_ONLY);
	}

	return card_exist;
}

static int rtl8411_conv_clk_and_div_n(int input, int dir)
{
	int output;

	if (dir == CLK_TO_DIV_N)
		output = input * 4 / 5 - 2;
	else
		output = (input + 2) * 5 / 4;

	return output;
}

static const struct pcr_ops rtl8411_pcr_ops = {
	.extra_init_hw = rtl8411_extra_init_hw,
	.optimize_phy = NULL,
	.turn_on_led = rtl8411_turn_on_led,
	.turn_off_led = rtl8411_turn_off_led,
	.enable_auto_blink = rtl8411_enable_auto_blink,
	.disable_auto_blink = rtl8411_disable_auto_blink,
	.card_power_on = rtl8411_card_power_on,
	.card_power_off = rtl8411_card_power_off,
	.switch_output_voltage = rtl8411_switch_output_voltage,
	.cd_deglitch = rtl8411_cd_deglitch,
	.conv_clk_and_div_n = rtl8411_conv_clk_and_div_n,
};

/* SD Pull Control Enable:
 *     SD_DAT[3:0] ==> pull up
 *     SD_CD       ==> pull up
 *     SD_WP       ==> pull up
 *     SD_CMD      ==> pull up
 *     SD_CLK      ==> pull down
 */
static const u32 rtl8411_sd_pull_ctl_enable_tbl[] = {
	RTSX_REG_PAIR(CARD_PULL_CTL1, 0xAA),
	RTSX_REG_PAIR(CARD_PULL_CTL2, 0xAA),
	RTSX_REG_PAIR(CARD_PULL_CTL3, 0xA9),
	RTSX_REG_PAIR(CARD_PULL_CTL4, 0x09),
	RTSX_REG_PAIR(CARD_PULL_CTL5, 0x09),
	RTSX_REG_PAIR(CARD_PULL_CTL6, 0x04),
	0,
};

/* SD Pull Control Disable:
 *     SD_DAT[3:0] ==> pull down
 *     SD_CD       ==> pull up
 *     SD_WP       ==> pull down
 *     SD_CMD      ==> pull down
 *     SD_CLK      ==> pull down
 */
static const u32 rtl8411_sd_pull_ctl_disable_tbl[] = {
	RTSX_REG_PAIR(CARD_PULL_CTL1, 0x65),
	RTSX_REG_PAIR(CARD_PULL_CTL2, 0x55),
	RTSX_REG_PAIR(CARD_PULL_CTL3, 0x95),
	RTSX_REG_PAIR(CARD_PULL_CTL4, 0x09),
	RTSX_REG_PAIR(CARD_PULL_CTL5, 0x05),
	RTSX_REG_PAIR(CARD_PULL_CTL6, 0x04),
	0,
};

/* MS Pull Control Enable:
 *     MS CD       ==> pull up
 *     others      ==> pull down
 */
static const u32 rtl8411_ms_pull_ctl_enable_tbl[] = {
	RTSX_REG_PAIR(CARD_PULL_CTL1, 0x65),
	RTSX_REG_PAIR(CARD_PULL_CTL2, 0x55),
	RTSX_REG_PAIR(CARD_PULL_CTL3, 0x95),
	RTSX_REG_PAIR(CARD_PULL_CTL4, 0x05),
	RTSX_REG_PAIR(CARD_PULL_CTL5, 0x05),
	RTSX_REG_PAIR(CARD_PULL_CTL6, 0x04),
	0,
};

/* MS Pull Control Disable:
 *     MS CD       ==> pull up
 *     others      ==> pull down
 */
static const u32 rtl8411_ms_pull_ctl_disable_tbl[] = {
	RTSX_REG_PAIR(CARD_PULL_CTL1, 0x65),
	RTSX_REG_PAIR(CARD_PULL_CTL2, 0x55),
	RTSX_REG_PAIR(CARD_PULL_CTL3, 0x95),
	RTSX_REG_PAIR(CARD_PULL_CTL4, 0x09),
	RTSX_REG_PAIR(CARD_PULL_CTL5, 0x05),
	RTSX_REG_PAIR(CARD_PULL_CTL6, 0x04),
	0,
};

void rtl8411_init_params(struct rtsx_pcr *pcr)
{
	pcr->extra_caps = EXTRA_CAPS_SD_SDR50 | EXTRA_CAPS_SD_SDR104;
	pcr->num_slots = 2;
	pcr->ops = &rtl8411_pcr_ops;

	pcr->ic_version = rtl8411_get_ic_version(pcr);
	pcr->sd_pull_ctl_enable_tbl = rtl8411_sd_pull_ctl_enable_tbl;
	pcr->sd_pull_ctl_disable_tbl = rtl8411_sd_pull_ctl_disable_tbl;
	pcr->ms_pull_ctl_enable_tbl = rtl8411_ms_pull_ctl_enable_tbl;
	pcr->ms_pull_ctl_disable_tbl = rtl8411_ms_pull_ctl_disable_tbl;
}
