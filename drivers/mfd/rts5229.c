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

static u8 rts5229_get_ic_version(struct rtsx_pcr *pcr)
{
	u8 val;

	rtsx_pci_read_register(pcr, DUMMY_REG_RESET_0, &val);
	return val & 0x0F;
}

static int rts5229_extra_init_hw(struct rtsx_pcr *pcr)
{
	rtsx_pci_init_cmd(pcr);

	/* Configure GPIO as output */
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, GPIO_CTL, 0x02, 0x02);
	/* Switch LDO3318 source from DV33 to card_3v3 */
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, LDO_PWR_SEL, 0x03, 0x00);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, LDO_PWR_SEL, 0x03, 0x01);
	/* LED shine disabled, set initial shine cycle period */
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, OLT_LED_CTL, 0x0F, 0x02);

	return rtsx_pci_send_cmd(pcr, 100);
}

static int rts5229_optimize_phy(struct rtsx_pcr *pcr)
{
	/* Optimize RX sensitivity */
	return rtsx_pci_write_phy_register(pcr, 0x00, 0xBA42);
}

static int rts5229_turn_on_led(struct rtsx_pcr *pcr)
{
	return rtsx_pci_write_register(pcr, GPIO_CTL, 0x02, 0x02);
}

static int rts5229_turn_off_led(struct rtsx_pcr *pcr)
{
	return rtsx_pci_write_register(pcr, GPIO_CTL, 0x02, 0x00);
}

static int rts5229_enable_auto_blink(struct rtsx_pcr *pcr)
{
	return rtsx_pci_write_register(pcr, OLT_LED_CTL, 0x08, 0x08);
}

static int rts5229_disable_auto_blink(struct rtsx_pcr *pcr)
{
	return rtsx_pci_write_register(pcr, OLT_LED_CTL, 0x08, 0x00);
}

static int rts5229_card_power_on(struct rtsx_pcr *pcr, int card)
{
	int err;

	rtsx_pci_init_cmd(pcr);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CARD_PWR_CTL,
			SD_POWER_MASK, SD_PARTIAL_POWER_ON);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, PWR_GATE_CTRL,
			LDO3318_PWR_MASK, 0x02);
	err = rtsx_pci_send_cmd(pcr, 100);
	if (err < 0)
		return err;

	/* To avoid too large in-rush current */
	udelay(150);

	rtsx_pci_init_cmd(pcr);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CARD_PWR_CTL,
			SD_POWER_MASK, SD_POWER_ON);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, PWR_GATE_CTRL,
			LDO3318_PWR_MASK, 0x06);
	err = rtsx_pci_send_cmd(pcr, 100);
	if (err < 0)
		return err;

	return 0;
}

static int rts5229_card_power_off(struct rtsx_pcr *pcr, int card)
{
	rtsx_pci_init_cmd(pcr);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CARD_PWR_CTL,
			SD_POWER_MASK | PMOS_STRG_MASK,
			SD_POWER_OFF | PMOS_STRG_400mA);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, PWR_GATE_CTRL,
			LDO3318_PWR_MASK, 0X00);
	return rtsx_pci_send_cmd(pcr, 100);
}

static int rts5229_switch_output_voltage(struct rtsx_pcr *pcr, u8 voltage)
{
	int err;

	if (voltage == OUTPUT_3V3) {
		err = rtsx_pci_write_phy_register(pcr, 0x08, 0x4FC0 | 0x24);
		if (err < 0)
			return err;
	} else if (voltage == OUTPUT_1V8) {
		err = rtsx_pci_write_phy_register(pcr, 0x08, 0x4C40 | 0x24);
		if (err < 0)
			return err;
	} else {
		return -EINVAL;
	}

	return 0;
}

static const struct pcr_ops rts5229_pcr_ops = {
	.extra_init_hw = rts5229_extra_init_hw,
	.optimize_phy = rts5229_optimize_phy,
	.turn_on_led = rts5229_turn_on_led,
	.turn_off_led = rts5229_turn_off_led,
	.enable_auto_blink = rts5229_enable_auto_blink,
	.disable_auto_blink = rts5229_disable_auto_blink,
	.card_power_on = rts5229_card_power_on,
	.card_power_off = rts5229_card_power_off,
	.switch_output_voltage = rts5229_switch_output_voltage,
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
static const u32 rts5229_sd_pull_ctl_enable_tbl1[] = {
	RTSX_REG_PAIR(CARD_PULL_CTL2, 0xAA),
	RTSX_REG_PAIR(CARD_PULL_CTL3, 0xE9),
	0,
};

/* For RTS5229 version C */
static const u32 rts5229_sd_pull_ctl_enable_tbl2[] = {
	RTSX_REG_PAIR(CARD_PULL_CTL2, 0xAA),
	RTSX_REG_PAIR(CARD_PULL_CTL3, 0xD9),
	0,
};

/* SD Pull Control Disable:
 *     SD_DAT[3:0] ==> pull down
 *     SD_CD       ==> pull up
 *     SD_WP       ==> pull down
 *     SD_CMD      ==> pull down
 *     SD_CLK      ==> pull down
 */
static const u32 rts5229_sd_pull_ctl_disable_tbl1[] = {
	RTSX_REG_PAIR(CARD_PULL_CTL2, 0x55),
	RTSX_REG_PAIR(CARD_PULL_CTL3, 0xD5),
	0,
};

/* For RTS5229 version C */
static const u32 rts5229_sd_pull_ctl_disable_tbl2[] = {
	RTSX_REG_PAIR(CARD_PULL_CTL2, 0x55),
	RTSX_REG_PAIR(CARD_PULL_CTL3, 0xE5),
	0,
};

/* MS Pull Control Enable:
 *     MS CD       ==> pull up
 *     others      ==> pull down
 */
static const u32 rts5229_ms_pull_ctl_enable_tbl[] = {
	RTSX_REG_PAIR(CARD_PULL_CTL5, 0x55),
	RTSX_REG_PAIR(CARD_PULL_CTL6, 0x15),
	0,
};

/* MS Pull Control Disable:
 *     MS CD       ==> pull up
 *     others      ==> pull down
 */
static const u32 rts5229_ms_pull_ctl_disable_tbl[] = {
	RTSX_REG_PAIR(CARD_PULL_CTL5, 0x55),
	RTSX_REG_PAIR(CARD_PULL_CTL6, 0x15),
	0,
};

void rts5229_init_params(struct rtsx_pcr *pcr)
{
	pcr->extra_caps = EXTRA_CAPS_SD_SDR50 | EXTRA_CAPS_SD_SDR104;
	pcr->num_slots = 2;
	pcr->ops = &rts5229_pcr_ops;

	pcr->ic_version = rts5229_get_ic_version(pcr);
	if (pcr->ic_version == IC_VER_C) {
		pcr->sd_pull_ctl_enable_tbl = rts5229_sd_pull_ctl_enable_tbl2;
		pcr->sd_pull_ctl_disable_tbl = rts5229_sd_pull_ctl_disable_tbl2;
	} else {
		pcr->sd_pull_ctl_enable_tbl = rts5229_sd_pull_ctl_enable_tbl1;
		pcr->sd_pull_ctl_disable_tbl = rts5229_sd_pull_ctl_disable_tbl1;
	}
	pcr->ms_pull_ctl_enable_tbl = rts5229_ms_pull_ctl_enable_tbl;
	pcr->ms_pull_ctl_disable_tbl = rts5229_ms_pull_ctl_disable_tbl;
}
