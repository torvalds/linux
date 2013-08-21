/* Driver for Realtek PCI-Express card reader
 *
 * Copyright(c) 2009-2013 Realtek Semiconductor Corp. All rights reserved.
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
 *   Roger Tseng <rogerable@realtek.com>
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/mfd/rtsx_pci.h>

#include "rtsx_pcr.h"

static void rts5227_fill_driving(struct rtsx_pcr *pcr, u8 voltage)
{
	u8 driving_3v3[4][3] = {
		{0x13, 0x13, 0x13},
		{0x96, 0x96, 0x96},
		{0x7F, 0x7F, 0x7F},
		{0x96, 0x96, 0x96},
	};
	u8 driving_1v8[4][3] = {
		{0x99, 0x99, 0x99},
		{0xAA, 0xAA, 0xAA},
		{0xFE, 0xFE, 0xFE},
		{0xB3, 0xB3, 0xB3},
	};
	u8 (*driving)[3], drive_sel;

	if (voltage == OUTPUT_3V3) {
		driving = driving_3v3;
		drive_sel = pcr->sd30_drive_sel_3v3;
	} else {
		driving = driving_1v8;
		drive_sel = pcr->sd30_drive_sel_1v8;
	}

	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD30_CLK_DRIVE_SEL,
			0xFF, driving[drive_sel][0]);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD30_CMD_DRIVE_SEL,
			0xFF, driving[drive_sel][1]);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD30_DAT_DRIVE_SEL,
			0xFF, driving[drive_sel][2]);
}

static void rts5227_fetch_vendor_settings(struct rtsx_pcr *pcr)
{
	u32 reg;

	rtsx_pci_read_config_dword(pcr, PCR_SETTING_REG1, &reg);
	dev_dbg(&(pcr->pci->dev), "Cfg 0x%x: 0x%x\n", PCR_SETTING_REG1, reg);

	if (!rtsx_vendor_setting_valid(reg))
		return;

	pcr->aspm_en = rtsx_reg_to_aspm(reg);
	pcr->sd30_drive_sel_1v8 = rtsx_reg_to_sd30_drive_sel_1v8(reg);
	pcr->card_drive_sel &= 0x3F;
	pcr->card_drive_sel |= rtsx_reg_to_card_drive_sel(reg);

	rtsx_pci_read_config_dword(pcr, PCR_SETTING_REG2, &reg);
	dev_dbg(&(pcr->pci->dev), "Cfg 0x%x: 0x%x\n", PCR_SETTING_REG2, reg);
	pcr->sd30_drive_sel_3v3 = rtsx_reg_to_sd30_drive_sel_3v3(reg);
	if (rtsx_reg_check_reverse_socket(reg))
		pcr->flags |= PCR_REVERSE_SOCKET;
}

static void rts5227_force_power_down(struct rtsx_pcr *pcr, u8 pm_state)
{
	/* Set relink_time to 0 */
	rtsx_pci_write_register(pcr, AUTOLOAD_CFG_BASE + 1, 0xFF, 0);
	rtsx_pci_write_register(pcr, AUTOLOAD_CFG_BASE + 2, 0xFF, 0);
	rtsx_pci_write_register(pcr, AUTOLOAD_CFG_BASE + 3, 0x01, 0);

	if (pm_state == HOST_ENTER_S3)
		rtsx_pci_write_register(pcr, PM_CTRL3, 0x10, 0x10);

	rtsx_pci_write_register(pcr, FPDCTL, 0x03, 0x03);
}

static int rts5227_extra_init_hw(struct rtsx_pcr *pcr)
{
	u16 cap;

	rtsx_pci_init_cmd(pcr);

	/* Configure GPIO as output */
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, GPIO_CTL, 0x02, 0x02);
	/* Reset ASPM state to default value */
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, ASPM_FORCE_CTL, 0x3F, 0);
	/* Switch LDO3318 source from DV33 to card_3v3 */
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, LDO_PWR_SEL, 0x03, 0x00);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, LDO_PWR_SEL, 0x03, 0x01);
	/* LED shine disabled, set initial shine cycle period */
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, OLT_LED_CTL, 0x0F, 0x02);
	/* Configure LTR */
	pcie_capability_read_word(pcr->pci, PCI_EXP_DEVCTL2, &cap);
	if (cap & PCI_EXP_LTR_EN)
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, LTR_CTL, 0xFF, 0xA3);
	/* Configure OBFF */
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, OBFF_CFG, 0x03, 0x03);
	/* Configure driving */
	rts5227_fill_driving(pcr, OUTPUT_3V3);
	/* Configure force_clock_req */
	if (pcr->flags & PCR_REVERSE_SOCKET)
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD,
				AUTOLOAD_CFG_BASE + 3, 0xB8, 0xB8);
	else
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD,
				AUTOLOAD_CFG_BASE + 3, 0xB8, 0x88);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, PM_CTRL3, 0x10, 0x00);

	return rtsx_pci_send_cmd(pcr, 100);
}

static int rts5227_optimize_phy(struct rtsx_pcr *pcr)
{
	/* Optimize RX sensitivity */
	return rtsx_pci_write_phy_register(pcr, 0x00, 0xBA42);
}

static int rts5227_turn_on_led(struct rtsx_pcr *pcr)
{
	return rtsx_pci_write_register(pcr, GPIO_CTL, 0x02, 0x02);
}

static int rts5227_turn_off_led(struct rtsx_pcr *pcr)
{
	return rtsx_pci_write_register(pcr, GPIO_CTL, 0x02, 0x00);
}

static int rts5227_enable_auto_blink(struct rtsx_pcr *pcr)
{
	return rtsx_pci_write_register(pcr, OLT_LED_CTL, 0x08, 0x08);
}

static int rts5227_disable_auto_blink(struct rtsx_pcr *pcr)
{
	return rtsx_pci_write_register(pcr, OLT_LED_CTL, 0x08, 0x00);
}

static int rts5227_card_power_on(struct rtsx_pcr *pcr, int card)
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

static int rts5227_card_power_off(struct rtsx_pcr *pcr, int card)
{
	rtsx_pci_init_cmd(pcr);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CARD_PWR_CTL,
			SD_POWER_MASK | PMOS_STRG_MASK,
			SD_POWER_OFF | PMOS_STRG_400mA);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, PWR_GATE_CTRL,
			LDO3318_PWR_MASK, 0X00);
	return rtsx_pci_send_cmd(pcr, 100);
}

static int rts5227_switch_output_voltage(struct rtsx_pcr *pcr, u8 voltage)
{
	int err;

	if (voltage == OUTPUT_3V3) {
		err = rtsx_pci_write_phy_register(pcr, 0x08, 0x4FC0 | 0x24);
		if (err < 0)
			return err;
	} else if (voltage == OUTPUT_1V8) {
		err = rtsx_pci_write_phy_register(pcr, 0x11, 0x3C02);
		if (err < 0)
			return err;
		err = rtsx_pci_write_phy_register(pcr, 0x08, 0x4C80 | 0x24);
		if (err < 0)
			return err;
	} else {
		return -EINVAL;
	}

	/* set pad drive */
	rtsx_pci_init_cmd(pcr);
	rts5227_fill_driving(pcr, voltage);
	return rtsx_pci_send_cmd(pcr, 100);
}

static const struct pcr_ops rts5227_pcr_ops = {
	.fetch_vendor_settings = rts5227_fetch_vendor_settings,
	.extra_init_hw = rts5227_extra_init_hw,
	.optimize_phy = rts5227_optimize_phy,
	.turn_on_led = rts5227_turn_on_led,
	.turn_off_led = rts5227_turn_off_led,
	.enable_auto_blink = rts5227_enable_auto_blink,
	.disable_auto_blink = rts5227_disable_auto_blink,
	.card_power_on = rts5227_card_power_on,
	.card_power_off = rts5227_card_power_off,
	.switch_output_voltage = rts5227_switch_output_voltage,
	.cd_deglitch = NULL,
	.conv_clk_and_div_n = NULL,
	.force_power_down = rts5227_force_power_down,
};

/* SD Pull Control Enable:
 *     SD_DAT[3:0] ==> pull up
 *     SD_CD       ==> pull up
 *     SD_WP       ==> pull up
 *     SD_CMD      ==> pull up
 *     SD_CLK      ==> pull down
 */
static const u32 rts5227_sd_pull_ctl_enable_tbl[] = {
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
static const u32 rts5227_sd_pull_ctl_disable_tbl[] = {
	RTSX_REG_PAIR(CARD_PULL_CTL2, 0x55),
	RTSX_REG_PAIR(CARD_PULL_CTL3, 0xD5),
	0,
};

/* MS Pull Control Enable:
 *     MS CD       ==> pull up
 *     others      ==> pull down
 */
static const u32 rts5227_ms_pull_ctl_enable_tbl[] = {
	RTSX_REG_PAIR(CARD_PULL_CTL5, 0x55),
	RTSX_REG_PAIR(CARD_PULL_CTL6, 0x15),
	0,
};

/* MS Pull Control Disable:
 *     MS CD       ==> pull up
 *     others      ==> pull down
 */
static const u32 rts5227_ms_pull_ctl_disable_tbl[] = {
	RTSX_REG_PAIR(CARD_PULL_CTL5, 0x55),
	RTSX_REG_PAIR(CARD_PULL_CTL6, 0x15),
	0,
};

void rts5227_init_params(struct rtsx_pcr *pcr)
{
	pcr->extra_caps = EXTRA_CAPS_SD_SDR50 | EXTRA_CAPS_SD_SDR104;
	pcr->num_slots = 2;
	pcr->ops = &rts5227_pcr_ops;

	pcr->flags = 0;
	pcr->card_drive_sel = RTSX_CARD_DRIVE_DEFAULT;
	pcr->sd30_drive_sel_1v8 = CFG_DRIVER_TYPE_B;
	pcr->sd30_drive_sel_3v3 = CFG_DRIVER_TYPE_B;
	pcr->aspm_en = ASPM_L1_EN;
	pcr->tx_initial_phase = SET_CLOCK_PHASE(27, 27, 15);
	pcr->rx_initial_phase = SET_CLOCK_PHASE(30, 7, 7);

	pcr->sd_pull_ctl_enable_tbl = rts5227_sd_pull_ctl_enable_tbl;
	pcr->sd_pull_ctl_disable_tbl = rts5227_sd_pull_ctl_disable_tbl;
	pcr->ms_pull_ctl_enable_tbl = rts5227_ms_pull_ctl_enable_tbl;
	pcr->ms_pull_ctl_disable_tbl = rts5227_ms_pull_ctl_disable_tbl;
}
