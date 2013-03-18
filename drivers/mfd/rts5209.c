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
#include <linux/delay.h>
#include <linux/mfd/rtsx_pci.h>

#include "rtsx_pcr.h"

static u8 rts5209_get_ic_version(struct rtsx_pcr *pcr)
{
	u8 val;

	val = rtsx_pci_readb(pcr, 0x1C);
	return val & 0x0F;
}

static void rts5209_init_vendor_cfg(struct rtsx_pcr *pcr)
{
	u32 val;

	rtsx_pci_read_config_dword(pcr, 0x724, &val);
	dev_dbg(&(pcr->pci->dev), "Cfg 0x724: 0x%x\n", val);

	if (!(val & 0x80)) {
		if (val & 0x08)
			pcr->ms_pmos = false;
		else
			pcr->ms_pmos = true;
	}
}

static int rts5209_extra_init_hw(struct rtsx_pcr *pcr)
{
	rtsx_pci_init_cmd(pcr);

	/* Turn off LED */
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CARD_GPIO, 0xFF, 0x03);
	/* Configure GPIO as output */
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CARD_GPIO_DIR, 0xFF, 0x03);

	return rtsx_pci_send_cmd(pcr, 100);
}

static int rts5209_optimize_phy(struct rtsx_pcr *pcr)
{
	return rtsx_pci_write_phy_register(pcr, 0x00, 0xB966);
}

static int rts5209_turn_on_led(struct rtsx_pcr *pcr)
{
	return rtsx_pci_write_register(pcr, CARD_GPIO, 0x01, 0x00);
}

static int rts5209_turn_off_led(struct rtsx_pcr *pcr)
{
	return rtsx_pci_write_register(pcr, CARD_GPIO, 0x01, 0x01);
}

static int rts5209_enable_auto_blink(struct rtsx_pcr *pcr)
{
	return rtsx_pci_write_register(pcr, CARD_AUTO_BLINK, 0xFF, 0x0D);
}

static int rts5209_disable_auto_blink(struct rtsx_pcr *pcr)
{
	return rtsx_pci_write_register(pcr, CARD_AUTO_BLINK, 0x08, 0x00);
}

static int rts5209_card_power_on(struct rtsx_pcr *pcr, int card)
{
	int err;
	u8 pwr_mask, partial_pwr_on, pwr_on;

	pwr_mask = SD_POWER_MASK;
	partial_pwr_on = SD_PARTIAL_POWER_ON;
	pwr_on = SD_POWER_ON;

	if (pcr->ms_pmos && (card == RTSX_MS_CARD)) {
		pwr_mask = MS_POWER_MASK;
		partial_pwr_on = MS_PARTIAL_POWER_ON;
		pwr_on = MS_POWER_ON;
	}

	rtsx_pci_init_cmd(pcr);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CARD_PWR_CTL,
			pwr_mask, partial_pwr_on);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, PWR_GATE_CTRL,
			LDO3318_PWR_MASK, 0x04);
	err = rtsx_pci_send_cmd(pcr, 100);
	if (err < 0)
		return err;

	/* To avoid too large in-rush current */
	udelay(150);

	rtsx_pci_init_cmd(pcr);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CARD_PWR_CTL, pwr_mask, pwr_on);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, PWR_GATE_CTRL,
			LDO3318_PWR_MASK, 0x00);
	err = rtsx_pci_send_cmd(pcr, 100);
	if (err < 0)
		return err;

	return 0;
}

static int rts5209_card_power_off(struct rtsx_pcr *pcr, int card)
{
	u8 pwr_mask, pwr_off;

	pwr_mask = SD_POWER_MASK;
	pwr_off = SD_POWER_OFF;

	if (pcr->ms_pmos && (card == RTSX_MS_CARD)) {
		pwr_mask = MS_POWER_MASK;
		pwr_off = MS_POWER_OFF;
	}

	rtsx_pci_init_cmd(pcr);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CARD_PWR_CTL,
			pwr_mask | PMOS_STRG_MASK, pwr_off | PMOS_STRG_400mA);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, PWR_GATE_CTRL,
			LDO3318_PWR_MASK, 0X06);
	return rtsx_pci_send_cmd(pcr, 100);
}

static int rts5209_switch_output_voltage(struct rtsx_pcr *pcr, u8 voltage)
{
	int err;

	if (voltage == OUTPUT_3V3) {
		err = rtsx_pci_write_register(pcr,
				SD30_DRIVE_SEL, 0x07, DRIVER_TYPE_D);
		if (err < 0)
			return err;
		err = rtsx_pci_write_phy_register(pcr, 0x08, 0x4FC0 | 0x24);
		if (err < 0)
			return err;
	} else if (voltage == OUTPUT_1V8) {
		err = rtsx_pci_write_register(pcr,
				SD30_DRIVE_SEL, 0x07, DRIVER_TYPE_B);
		if (err < 0)
			return err;
		err = rtsx_pci_write_phy_register(pcr, 0x08, 0x4C40 | 0x24);
		if (err < 0)
			return err;
	} else {
		return -EINVAL;
	}

	return 0;
}

static const struct pcr_ops rts5209_pcr_ops = {
	.extra_init_hw = rts5209_extra_init_hw,
	.optimize_phy = rts5209_optimize_phy,
	.turn_on_led = rts5209_turn_on_led,
	.turn_off_led = rts5209_turn_off_led,
	.enable_auto_blink = rts5209_enable_auto_blink,
	.disable_auto_blink = rts5209_disable_auto_blink,
	.card_power_on = rts5209_card_power_on,
	.card_power_off = rts5209_card_power_off,
	.switch_output_voltage = rts5209_switch_output_voltage,
	.cd_deglitch = NULL,
	.conv_clk_and_div_n = NULL,
};

/* SD Pull Control Enable:
 *     SD_DAT[3:0] ==> pull up
 *     SD_CD       ==> pull up
 *     SD_WP       ==> pull up
 *     SD_CMD      ==> pull up
 *     SD_CLK      ==> pull down
 */
static const u32 rts5209_sd_pull_ctl_enable_tbl[] = {
	RTSX_REG_PAIR(CARD_PULL_CTL1, 0xAA),
	RTSX_REG_PAIR(CARD_PULL_CTL2, 0xAA),
	RTSX_REG_PAIR(CARD_PULL_CTL3, 0xE9),
	0,
};

/* SD Pull Control Disable:
 *     SD_DAT[3:0] ==> pull down
 *     SD_CD       ==> pull up
 *     SD_WP       ==> pull down
 *     SD_CMD      ==> pull down
 *     SD_CLK      ==> pull down
 */
static const u32 rts5209_sd_pull_ctl_disable_tbl[] = {
	RTSX_REG_PAIR(CARD_PULL_CTL1, 0x55),
	RTSX_REG_PAIR(CARD_PULL_CTL2, 0x55),
	RTSX_REG_PAIR(CARD_PULL_CTL3, 0xD5),
	0,
};

/* MS Pull Control Enable:
 *     MS CD       ==> pull up
 *     others      ==> pull down
 */
static const u32 rts5209_ms_pull_ctl_enable_tbl[] = {
	RTSX_REG_PAIR(CARD_PULL_CTL4, 0x55),
	RTSX_REG_PAIR(CARD_PULL_CTL5, 0x55),
	RTSX_REG_PAIR(CARD_PULL_CTL6, 0x15),
	0,
};

/* MS Pull Control Disable:
 *     MS CD       ==> pull up
 *     others      ==> pull down
 */
static const u32 rts5209_ms_pull_ctl_disable_tbl[] = {
	RTSX_REG_PAIR(CARD_PULL_CTL4, 0x55),
	RTSX_REG_PAIR(CARD_PULL_CTL5, 0x55),
	RTSX_REG_PAIR(CARD_PULL_CTL6, 0x15),
	0,
};

void rts5209_init_params(struct rtsx_pcr *pcr)
{
	pcr->extra_caps = EXTRA_CAPS_SD_SDR50 |
		EXTRA_CAPS_SD_SDR104 | EXTRA_CAPS_MMC_8BIT;
	pcr->num_slots = 2;
	pcr->ops = &rts5209_pcr_ops;

	rts5209_init_vendor_cfg(pcr);

	pcr->ic_version = rts5209_get_ic_version(pcr);
	pcr->sd_pull_ctl_enable_tbl = rts5209_sd_pull_ctl_enable_tbl;
	pcr->sd_pull_ctl_disable_tbl = rts5209_sd_pull_ctl_disable_tbl;
	pcr->ms_pull_ctl_enable_tbl = rts5209_ms_pull_ctl_enable_tbl;
	pcr->ms_pull_ctl_disable_tbl = rts5209_ms_pull_ctl_disable_tbl;
}
