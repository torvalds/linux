// SPDX-License-Identifier: GPL-2.0-or-later
/* Driver for Realtek PCI-Express card reader
 *
 * Copyright(c) 2009-2013 Realtek Semiconductor Corp. All rights reserved.
 *
 * Author:
 *   Wei WANG <wei_wang@realsil.com.cn>
 *   Roger Tseng <rogerable@realtek.com>
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/rtsx_pci.h>

#include "rtsx_pcr.h"

static u8 rts5227_get_ic_version(struct rtsx_pcr *pcr)
{
	u8 val;

	rtsx_pci_read_register(pcr, DUMMY_REG_RESET_0, &val);
	return val & 0x0F;
}

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
	struct pci_dev *pdev = pcr->pci;
	u32 reg;

	pci_read_config_dword(pdev, PCR_SETTING_REG1, &reg);
	pcr_dbg(pcr, "Cfg 0x%x: 0x%x\n", PCR_SETTING_REG1, reg);

	if (!rtsx_vendor_setting_valid(reg))
		return;

	pcr->aspm_en = rtsx_reg_to_aspm(reg);
	pcr->sd30_drive_sel_1v8 = rtsx_reg_to_sd30_drive_sel_1v8(reg);
	pcr->card_drive_sel &= 0x3F;
	pcr->card_drive_sel |= rtsx_reg_to_card_drive_sel(reg);

	pci_read_config_dword(pdev, PCR_SETTING_REG2, &reg);
	pcr_dbg(pcr, "Cfg 0x%x: 0x%x\n", PCR_SETTING_REG2, reg);
	if (rtsx_check_mmc_support(reg))
		pcr->extra_caps |= EXTRA_CAPS_NO_MMC;
	pcr->sd30_drive_sel_3v3 = rtsx_reg_to_sd30_drive_sel_3v3(reg);
	if (rtsx_reg_check_reverse_socket(reg))
		pcr->flags |= PCR_REVERSE_SOCKET;
}

static void rts5227_init_from_cfg(struct rtsx_pcr *pcr)
{
	struct pci_dev *pdev = pcr->pci;
	int l1ss;
	u32 lval;
	struct rtsx_cr_option *option = &pcr->option;

	l1ss = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_L1SS);
	if (!l1ss)
		return;

	pci_read_config_dword(pdev, l1ss + PCI_L1SS_CTL1, &lval);

	if (CHK_PCI_PID(pcr, 0x522A)) {
		if (0 == (lval & 0x0F))
			rtsx_pci_enable_oobs_polling(pcr);
		else
			rtsx_pci_disable_oobs_polling(pcr);
	}

	if (lval & PCI_L1SS_CTL1_ASPM_L1_1)
		rtsx_set_dev_flag(pcr, ASPM_L1_1_EN);
	else
		rtsx_clear_dev_flag(pcr, ASPM_L1_1_EN);

	if (lval & PCI_L1SS_CTL1_ASPM_L1_2)
		rtsx_set_dev_flag(pcr, ASPM_L1_2_EN);
	else
		rtsx_clear_dev_flag(pcr, ASPM_L1_2_EN);

	if (lval & PCI_L1SS_CTL1_PCIPM_L1_1)
		rtsx_set_dev_flag(pcr, PM_L1_1_EN);
	else
		rtsx_clear_dev_flag(pcr, PM_L1_1_EN);

	if (lval & PCI_L1SS_CTL1_PCIPM_L1_2)
		rtsx_set_dev_flag(pcr, PM_L1_2_EN);
	else
		rtsx_clear_dev_flag(pcr, PM_L1_2_EN);

	if (option->ltr_en) {
		u16 val;

		pcie_capability_read_word(pcr->pci, PCI_EXP_DEVCTL2, &val);
		if (val & PCI_EXP_DEVCTL2_LTR_EN) {
			option->ltr_enabled = true;
			option->ltr_active = true;
			rtsx_set_ltr_latency(pcr, option->ltr_active_latency);
		} else {
			option->ltr_enabled = false;
		}
	}

	if (rtsx_check_dev_flag(pcr, ASPM_L1_1_EN | ASPM_L1_2_EN
				| PM_L1_1_EN | PM_L1_2_EN))
		option->force_clkreq_0 = false;
	else
		option->force_clkreq_0 = true;

}

static int rts5227_extra_init_hw(struct rtsx_pcr *pcr)
{
	u16 cap;
	struct rtsx_cr_option *option = &pcr->option;

	rts5227_init_from_cfg(pcr);
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
	if (cap & PCI_EXP_DEVCTL2_LTR_EN)
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, LTR_CTL, 0xFF, 0xA3);
	/* Configure OBFF */
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, OBFF_CFG, 0x03, 0x03);
	/* Configure driving */
	rts5227_fill_driving(pcr, OUTPUT_3V3);
	/* Configure force_clock_req */
	if (pcr->flags & PCR_REVERSE_SOCKET)
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, PETXCFG, 0x30, 0x30);
	else
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, PETXCFG, 0x30, 0x00);

	if (option->force_clkreq_0)
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, PETXCFG,
				FORCE_CLKREQ_DELINK_MASK, FORCE_CLKREQ_LOW);
	else
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, PETXCFG,
				FORCE_CLKREQ_DELINK_MASK, FORCE_CLKREQ_HIGH);

	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, pcr->reg_pm_ctrl3, 0x10, 0x00);

	return rtsx_pci_send_cmd(pcr, 100);
}

static int rts5227_optimize_phy(struct rtsx_pcr *pcr)
{
	int err;

	err = rtsx_pci_write_register(pcr, PM_CTRL3, D3_DELINK_MODE_EN, 0x00);
	if (err < 0)
		return err;

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

	if (pcr->option.ocp_en)
		rtsx_pci_enable_ocp(pcr);

	rtsx_pci_init_cmd(pcr);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CARD_PWR_CTL,
			SD_POWER_MASK, SD_PARTIAL_POWER_ON);

	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, PWR_GATE_CTRL,
			LDO3318_PWR_MASK, 0x02);

	err = rtsx_pci_send_cmd(pcr, 100);
	if (err < 0)
		return err;

	/* To avoid too large in-rush current */
	msleep(20);
	rtsx_pci_init_cmd(pcr);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CARD_PWR_CTL,
			SD_POWER_MASK, SD_POWER_ON);

	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, PWR_GATE_CTRL,
			LDO3318_PWR_MASK, 0x06);

	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CARD_OE,
			SD_OUTPUT_EN, SD_OUTPUT_EN);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CARD_OE,
			MS_OUTPUT_EN, MS_OUTPUT_EN);
	return rtsx_pci_send_cmd(pcr, 100);
}

static int rts5227_card_power_off(struct rtsx_pcr *pcr, int card)
{
	if (pcr->option.ocp_en)
		rtsx_pci_disable_ocp(pcr);

	rtsx_pci_write_register(pcr, CARD_PWR_CTL, SD_POWER_MASK |
			PMOS_STRG_MASK, SD_POWER_OFF | PMOS_STRG_400mA);
	rtsx_pci_write_register(pcr, PWR_GATE_CTRL, LDO3318_PWR_MASK, 0X00);

	return 0;
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
	pcr->aspm_mode = ASPM_MODE_CFG;
	pcr->tx_initial_phase = SET_CLOCK_PHASE(27, 27, 15);
	pcr->rx_initial_phase = SET_CLOCK_PHASE(30, 7, 7);

	pcr->ic_version = rts5227_get_ic_version(pcr);
	pcr->sd_pull_ctl_enable_tbl = rts5227_sd_pull_ctl_enable_tbl;
	pcr->sd_pull_ctl_disable_tbl = rts5227_sd_pull_ctl_disable_tbl;
	pcr->ms_pull_ctl_enable_tbl = rts5227_ms_pull_ctl_enable_tbl;
	pcr->ms_pull_ctl_disable_tbl = rts5227_ms_pull_ctl_disable_tbl;

	pcr->reg_pm_ctrl3 = PM_CTRL3;
}

static int rts522a_optimize_phy(struct rtsx_pcr *pcr)
{
	int err;

	err = rtsx_pci_write_register(pcr, RTS522A_PM_CTRL3, D3_DELINK_MODE_EN,
		0x00);
	if (err < 0)
		return err;

	if (is_version(pcr, 0x522A, IC_VER_A)) {
		err = rtsx_pci_write_phy_register(pcr, PHY_RCR2,
			PHY_RCR2_INIT_27S);
		if (err)
			return err;

		rtsx_pci_write_phy_register(pcr, PHY_RCR1, PHY_RCR1_INIT_27S);
		rtsx_pci_write_phy_register(pcr, PHY_FLD0, PHY_FLD0_INIT_27S);
		rtsx_pci_write_phy_register(pcr, PHY_FLD3, PHY_FLD3_INIT_27S);
		rtsx_pci_write_phy_register(pcr, PHY_FLD4, PHY_FLD4_INIT_27S);
	}

	return 0;
}

static int rts522a_extra_init_hw(struct rtsx_pcr *pcr)
{
	rts5227_extra_init_hw(pcr);

	/* Power down OCP for power consumption */
	if (!pcr->card_exist)
		rtsx_pci_write_register(pcr, FPDCTL, OC_POWER_DOWN,
				OC_POWER_DOWN);

	rtsx_pci_write_register(pcr, FUNC_FORCE_CTL, FUNC_FORCE_UPME_XMT_DBG,
		FUNC_FORCE_UPME_XMT_DBG);
	rtsx_pci_write_register(pcr, PCLK_CTL, 0x04, 0x04);
	rtsx_pci_write_register(pcr, PM_EVENT_DEBUG, PME_DEBUG_0, PME_DEBUG_0);
	rtsx_pci_write_register(pcr, PM_CLK_FORCE_CTL, 0xFF, 0x11);

	return 0;
}

static int rts522a_switch_output_voltage(struct rtsx_pcr *pcr, u8 voltage)
{
	int err;

	if (voltage == OUTPUT_3V3) {
		err = rtsx_pci_write_phy_register(pcr, 0x08, 0x57E4);
		if (err < 0)
			return err;
	} else if (voltage == OUTPUT_1V8) {
		err = rtsx_pci_write_phy_register(pcr, 0x11, 0x3C02);
		if (err < 0)
			return err;
		err = rtsx_pci_write_phy_register(pcr, 0x08, 0x54A4);
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

static void rts522a_set_l1off_cfg_sub_d0(struct rtsx_pcr *pcr, int active)
{
	struct rtsx_cr_option *option = &pcr->option;
	int aspm_L1_1, aspm_L1_2;
	u8 val = 0;

	aspm_L1_1 = rtsx_check_dev_flag(pcr, ASPM_L1_1_EN);
	aspm_L1_2 = rtsx_check_dev_flag(pcr, ASPM_L1_2_EN);

	if (active) {
		/* run, latency: 60us */
		if (aspm_L1_1)
			val = option->ltr_l1off_snooze_sspwrgate;
	} else {
		/* l1off, latency: 300us */
		if (aspm_L1_2)
			val = option->ltr_l1off_sspwrgate;
	}

	rtsx_set_l1off_sub(pcr, val);
}

/* rts522a operations mainly derived from rts5227, except phy/hw init setting.
 */
static const struct pcr_ops rts522a_pcr_ops = {
	.fetch_vendor_settings = rts5227_fetch_vendor_settings,
	.extra_init_hw = rts522a_extra_init_hw,
	.optimize_phy = rts522a_optimize_phy,
	.turn_on_led = rts5227_turn_on_led,
	.turn_off_led = rts5227_turn_off_led,
	.enable_auto_blink = rts5227_enable_auto_blink,
	.disable_auto_blink = rts5227_disable_auto_blink,
	.card_power_on = rts5227_card_power_on,
	.card_power_off = rts5227_card_power_off,
	.switch_output_voltage = rts522a_switch_output_voltage,
	.cd_deglitch = NULL,
	.conv_clk_and_div_n = NULL,
	.set_l1off_cfg_sub_d0 = rts522a_set_l1off_cfg_sub_d0,
};

void rts522a_init_params(struct rtsx_pcr *pcr)
{
	struct rtsx_cr_option *option = &pcr->option;

	rts5227_init_params(pcr);
	pcr->ops = &rts522a_pcr_ops;
	pcr->aspm_mode = ASPM_MODE_REG;
	pcr->tx_initial_phase = SET_CLOCK_PHASE(20, 20, 11);
	pcr->reg_pm_ctrl3 = RTS522A_PM_CTRL3;

	option->dev_flags = LTR_L1SS_PWR_GATE_EN;
	option->ltr_en = true;

	/* init latency of active, idle, L1OFF to 60us, 300us, 3ms */
	option->ltr_active_latency = LTR_ACTIVE_LATENCY_DEF;
	option->ltr_idle_latency = LTR_IDLE_LATENCY_DEF;
	option->ltr_l1off_latency = LTR_L1OFF_LATENCY_DEF;
	option->l1_snooze_delay = L1_SNOOZE_DELAY_DEF;
	option->ltr_l1off_sspwrgate = 0x7F;
	option->ltr_l1off_snooze_sspwrgate = 0x78;

	pcr->option.ocp_en = 1;
	if (pcr->option.ocp_en)
		pcr->hw_param.interrupt_en |= SD_OC_INT_EN;
	pcr->hw_param.ocp_glitch = SD_OCP_GLITCH_10M;
	pcr->option.sd_800mA_ocp_thd = RTS522A_OCP_THD_800;

}
