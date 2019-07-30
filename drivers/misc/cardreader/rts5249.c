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
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/rtsx_pci.h>

#include "rtsx_pcr.h"

static u8 rts5249_get_ic_version(struct rtsx_pcr *pcr)
{
	u8 val;

	rtsx_pci_read_register(pcr, DUMMY_REG_RESET_0, &val);
	return val & 0x0F;
}

static void rts5249_fill_driving(struct rtsx_pcr *pcr, u8 voltage)
{
	u8 driving_3v3[4][3] = {
		{0x11, 0x11, 0x18},
		{0x55, 0x55, 0x5C},
		{0xFF, 0xFF, 0xFF},
		{0x96, 0x96, 0x96},
	};
	u8 driving_1v8[4][3] = {
		{0xC4, 0xC4, 0xC4},
		{0x3C, 0x3C, 0x3C},
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

static void rtsx_base_fetch_vendor_settings(struct rtsx_pcr *pcr)
{
	u32 reg;

	rtsx_pci_read_config_dword(pcr, PCR_SETTING_REG1, &reg);
	pcr_dbg(pcr, "Cfg 0x%x: 0x%x\n", PCR_SETTING_REG1, reg);

	if (!rtsx_vendor_setting_valid(reg)) {
		pcr_dbg(pcr, "skip fetch vendor setting\n");
		return;
	}

	pcr->aspm_en = rtsx_reg_to_aspm(reg);
	pcr->sd30_drive_sel_1v8 = rtsx_reg_to_sd30_drive_sel_1v8(reg);
	pcr->card_drive_sel &= 0x3F;
	pcr->card_drive_sel |= rtsx_reg_to_card_drive_sel(reg);

	rtsx_pci_read_config_dword(pcr, PCR_SETTING_REG2, &reg);
	pcr_dbg(pcr, "Cfg 0x%x: 0x%x\n", PCR_SETTING_REG2, reg);
	pcr->sd30_drive_sel_3v3 = rtsx_reg_to_sd30_drive_sel_3v3(reg);
	if (rtsx_reg_check_reverse_socket(reg))
		pcr->flags |= PCR_REVERSE_SOCKET;
}

static void rtsx_base_force_power_down(struct rtsx_pcr *pcr, u8 pm_state)
{
	/* Set relink_time to 0 */
	rtsx_pci_write_register(pcr, AUTOLOAD_CFG_BASE + 1, 0xFF, 0);
	rtsx_pci_write_register(pcr, AUTOLOAD_CFG_BASE + 2, 0xFF, 0);
	rtsx_pci_write_register(pcr, AUTOLOAD_CFG_BASE + 3, 0x01, 0);

	if (pm_state == HOST_ENTER_S3)
		rtsx_pci_write_register(pcr, pcr->reg_pm_ctrl3,
			D3_DELINK_MODE_EN, D3_DELINK_MODE_EN);

	rtsx_pci_write_register(pcr, FPDCTL, 0x03, 0x03);
}

static void rts5249_init_from_cfg(struct rtsx_pcr *pcr)
{
	struct rtsx_cr_option *option = &(pcr->option);
	u32 lval;

	if (CHK_PCI_PID(pcr, PID_524A))
		rtsx_pci_read_config_dword(pcr,
			PCR_ASPM_SETTING_REG1, &lval);
	else
		rtsx_pci_read_config_dword(pcr,
			PCR_ASPM_SETTING_REG2, &lval);

	if (lval & ASPM_L1_1_EN_MASK)
		rtsx_set_dev_flag(pcr, ASPM_L1_1_EN);

	if (lval & ASPM_L1_2_EN_MASK)
		rtsx_set_dev_flag(pcr, ASPM_L1_2_EN);

	if (lval & PM_L1_1_EN_MASK)
		rtsx_set_dev_flag(pcr, PM_L1_1_EN);

	if (lval & PM_L1_2_EN_MASK)
		rtsx_set_dev_flag(pcr, PM_L1_2_EN);

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
}

static int rts5249_init_from_hw(struct rtsx_pcr *pcr)
{
	struct rtsx_cr_option *option = &(pcr->option);

	if (rtsx_check_dev_flag(pcr, ASPM_L1_1_EN | ASPM_L1_2_EN
				| PM_L1_1_EN | PM_L1_2_EN))
		option->force_clkreq_0 = false;
	else
		option->force_clkreq_0 = true;

	return 0;
}

static int rts5249_extra_init_hw(struct rtsx_pcr *pcr)
{
	struct rtsx_cr_option *option = &(pcr->option);

	rts5249_init_from_cfg(pcr);
	rts5249_init_from_hw(pcr);

	rtsx_pci_init_cmd(pcr);

	/* Rest L1SUB Config */
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, L1SUB_CONFIG3, 0xFF, 0x00);
	/* Configure GPIO as output */
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, GPIO_CTL, 0x02, 0x02);
	/* Reset ASPM state to default value */
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, ASPM_FORCE_CTL, 0x3F, 0);
	/* Switch LDO3318 source from DV33 to card_3v3 */
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, LDO_PWR_SEL, 0x03, 0x00);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, LDO_PWR_SEL, 0x03, 0x01);
	/* LED shine disabled, set initial shine cycle period */
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, OLT_LED_CTL, 0x0F, 0x02);
	/* Configure driving */
	rts5249_fill_driving(pcr, OUTPUT_3V3);
	if (pcr->flags & PCR_REVERSE_SOCKET)
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, PETXCFG, 0xB0, 0xB0);
	else
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, PETXCFG, 0xB0, 0x80);

	/*
	 * If u_force_clkreq_0 is enabled, CLKREQ# PIN will be forced
	 * to drive low, and we forcibly request clock.
	 */
	if (option->force_clkreq_0)
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, PETXCFG,
			FORCE_CLKREQ_DELINK_MASK, FORCE_CLKREQ_LOW);
	else
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, PETXCFG,
			FORCE_CLKREQ_DELINK_MASK, FORCE_CLKREQ_HIGH);

	return rtsx_pci_send_cmd(pcr, CMD_TIMEOUT_DEF);
}

static int rts5249_optimize_phy(struct rtsx_pcr *pcr)
{
	int err;

	err = rtsx_pci_write_register(pcr, PM_CTRL3, D3_DELINK_MODE_EN, 0x00);
	if (err < 0)
		return err;

	err = rtsx_pci_write_phy_register(pcr, PHY_REV,
			PHY_REV_RESV | PHY_REV_RXIDLE_LATCHED |
			PHY_REV_P1_EN | PHY_REV_RXIDLE_EN |
			PHY_REV_CLKREQ_TX_EN | PHY_REV_RX_PWST |
			PHY_REV_CLKREQ_DT_1_0 | PHY_REV_STOP_CLKRD |
			PHY_REV_STOP_CLKWR);
	if (err < 0)
		return err;

	msleep(1);

	err = rtsx_pci_write_phy_register(pcr, PHY_BPCR,
			PHY_BPCR_IBRXSEL | PHY_BPCR_IBTXSEL |
			PHY_BPCR_IB_FILTER | PHY_BPCR_CMIRROR_EN);
	if (err < 0)
		return err;

	err = rtsx_pci_write_phy_register(pcr, PHY_PCR,
			PHY_PCR_FORCE_CODE | PHY_PCR_OOBS_CALI_50 |
			PHY_PCR_OOBS_VCM_08 | PHY_PCR_OOBS_SEN_90 |
			PHY_PCR_RSSI_EN | PHY_PCR_RX10K);
	if (err < 0)
		return err;

	err = rtsx_pci_write_phy_register(pcr, PHY_RCR2,
			PHY_RCR2_EMPHASE_EN | PHY_RCR2_NADJR |
			PHY_RCR2_CDR_SR_2 | PHY_RCR2_FREQSEL_12 |
			PHY_RCR2_CDR_SC_12P | PHY_RCR2_CALIB_LATE);
	if (err < 0)
		return err;

	err = rtsx_pci_write_phy_register(pcr, PHY_FLD4,
			PHY_FLD4_FLDEN_SEL | PHY_FLD4_REQ_REF |
			PHY_FLD4_RXAMP_OFF | PHY_FLD4_REQ_ADDA |
			PHY_FLD4_BER_COUNT | PHY_FLD4_BER_TIMER |
			PHY_FLD4_BER_CHK_EN);
	if (err < 0)
		return err;
	err = rtsx_pci_write_phy_register(pcr, PHY_RDR,
			PHY_RDR_RXDSEL_1_9 | PHY_SSC_AUTO_PWD);
	if (err < 0)
		return err;
	err = rtsx_pci_write_phy_register(pcr, PHY_RCR1,
			PHY_RCR1_ADP_TIME_4 | PHY_RCR1_VCO_COARSE);
	if (err < 0)
		return err;
	err = rtsx_pci_write_phy_register(pcr, PHY_FLD3,
			PHY_FLD3_TIMER_4 | PHY_FLD3_TIMER_6 |
			PHY_FLD3_RXDELINK);
	if (err < 0)
		return err;

	return rtsx_pci_write_phy_register(pcr, PHY_TUNE,
			PHY_TUNE_TUNEREF_1_0 | PHY_TUNE_VBGSEL_1252 |
			PHY_TUNE_SDBUS_33 | PHY_TUNE_TUNED18 |
			PHY_TUNE_TUNED12 | PHY_TUNE_TUNEA12);
}

static int rtsx_base_turn_on_led(struct rtsx_pcr *pcr)
{
	return rtsx_pci_write_register(pcr, GPIO_CTL, 0x02, 0x02);
}

static int rtsx_base_turn_off_led(struct rtsx_pcr *pcr)
{
	return rtsx_pci_write_register(pcr, GPIO_CTL, 0x02, 0x00);
}

static int rtsx_base_enable_auto_blink(struct rtsx_pcr *pcr)
{
	return rtsx_pci_write_register(pcr, OLT_LED_CTL, 0x08, 0x08);
}

static int rtsx_base_disable_auto_blink(struct rtsx_pcr *pcr)
{
	return rtsx_pci_write_register(pcr, OLT_LED_CTL, 0x08, 0x00);
}

static int rtsx_base_card_power_on(struct rtsx_pcr *pcr, int card)
{
	int err;
	struct rtsx_cr_option *option = &pcr->option;

	if (option->ocp_en)
		rtsx_pci_enable_ocp(pcr);

	rtsx_pci_init_cmd(pcr);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CARD_PWR_CTL,
			SD_POWER_MASK, SD_VCC_PARTIAL_POWER_ON);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, PWR_GATE_CTRL,
			LDO3318_PWR_MASK, 0x02);
	err = rtsx_pci_send_cmd(pcr, 100);
	if (err < 0)
		return err;

	msleep(5);

	rtsx_pci_init_cmd(pcr);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CARD_PWR_CTL,
			SD_POWER_MASK, SD_VCC_POWER_ON);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, PWR_GATE_CTRL,
			LDO3318_PWR_MASK, 0x06);
	return rtsx_pci_send_cmd(pcr, 100);
}

static int rtsx_base_card_power_off(struct rtsx_pcr *pcr, int card)
{
	struct rtsx_cr_option *option = &pcr->option;

	if (option->ocp_en)
		rtsx_pci_disable_ocp(pcr);

	rtsx_pci_write_register(pcr, CARD_PWR_CTL, SD_POWER_MASK, SD_POWER_OFF);

	rtsx_pci_write_register(pcr, PWR_GATE_CTRL, LDO3318_PWR_MASK, 0x00);
	return 0;
}

static int rtsx_base_switch_output_voltage(struct rtsx_pcr *pcr, u8 voltage)
{
	int err;
	u16 append;

	switch (voltage) {
	case OUTPUT_3V3:
		err = rtsx_pci_update_phy(pcr, PHY_TUNE, PHY_TUNE_VOLTAGE_MASK,
			PHY_TUNE_VOLTAGE_3V3);
		if (err < 0)
			return err;
		break;
	case OUTPUT_1V8:
		append = PHY_TUNE_D18_1V8;
		if (CHK_PCI_PID(pcr, 0x5249)) {
			err = rtsx_pci_update_phy(pcr, PHY_BACR,
				PHY_BACR_BASIC_MASK, 0);
			if (err < 0)
				return err;
			append = PHY_TUNE_D18_1V7;
		}

		err = rtsx_pci_update_phy(pcr, PHY_TUNE, PHY_TUNE_VOLTAGE_MASK,
			append);
		if (err < 0)
			return err;
		break;
	default:
		pcr_dbg(pcr, "unknown output voltage %d\n", voltage);
		return -EINVAL;
	}

	/* set pad drive */
	rtsx_pci_init_cmd(pcr);
	rts5249_fill_driving(pcr, voltage);
	return rtsx_pci_send_cmd(pcr, 100);
}

static void rts5249_set_aspm(struct rtsx_pcr *pcr, bool enable)
{
	struct rtsx_cr_option *option = &pcr->option;
	u8 val = 0;

	if (pcr->aspm_enabled == enable)
		return;

	if (option->dev_aspm_mode == DEV_ASPM_DYNAMIC) {
		if (enable)
			val = pcr->aspm_en;
		rtsx_pci_update_cfg_byte(pcr,
			pcr->pcie_cap + PCI_EXP_LNKCTL,
			ASPM_MASK_NEG, val);
	} else if (option->dev_aspm_mode == DEV_ASPM_BACKDOOR) {
		u8 mask = FORCE_ASPM_VAL_MASK | FORCE_ASPM_CTL0;

		if (!enable)
			val = FORCE_ASPM_CTL0;
		rtsx_pci_write_register(pcr, ASPM_FORCE_CTL, mask, val);
	}

	pcr->aspm_enabled = enable;
}

static const struct pcr_ops rts5249_pcr_ops = {
	.fetch_vendor_settings = rtsx_base_fetch_vendor_settings,
	.extra_init_hw = rts5249_extra_init_hw,
	.optimize_phy = rts5249_optimize_phy,
	.turn_on_led = rtsx_base_turn_on_led,
	.turn_off_led = rtsx_base_turn_off_led,
	.enable_auto_blink = rtsx_base_enable_auto_blink,
	.disable_auto_blink = rtsx_base_disable_auto_blink,
	.card_power_on = rtsx_base_card_power_on,
	.card_power_off = rtsx_base_card_power_off,
	.switch_output_voltage = rtsx_base_switch_output_voltage,
	.force_power_down = rtsx_base_force_power_down,
	.set_aspm = rts5249_set_aspm,
};

/* SD Pull Control Enable:
 *     SD_DAT[3:0] ==> pull up
 *     SD_CD       ==> pull up
 *     SD_WP       ==> pull up
 *     SD_CMD      ==> pull up
 *     SD_CLK      ==> pull down
 */
static const u32 rts5249_sd_pull_ctl_enable_tbl[] = {
	RTSX_REG_PAIR(CARD_PULL_CTL1, 0x66),
	RTSX_REG_PAIR(CARD_PULL_CTL2, 0xAA),
	RTSX_REG_PAIR(CARD_PULL_CTL3, 0xE9),
	RTSX_REG_PAIR(CARD_PULL_CTL4, 0xAA),
	0,
};

/* SD Pull Control Disable:
 *     SD_DAT[3:0] ==> pull down
 *     SD_CD       ==> pull up
 *     SD_WP       ==> pull down
 *     SD_CMD      ==> pull down
 *     SD_CLK      ==> pull down
 */
static const u32 rts5249_sd_pull_ctl_disable_tbl[] = {
	RTSX_REG_PAIR(CARD_PULL_CTL1, 0x66),
	RTSX_REG_PAIR(CARD_PULL_CTL2, 0x55),
	RTSX_REG_PAIR(CARD_PULL_CTL3, 0xD5),
	RTSX_REG_PAIR(CARD_PULL_CTL4, 0x55),
	0,
};

/* MS Pull Control Enable:
 *     MS CD       ==> pull up
 *     others      ==> pull down
 */
static const u32 rts5249_ms_pull_ctl_enable_tbl[] = {
	RTSX_REG_PAIR(CARD_PULL_CTL4, 0x55),
	RTSX_REG_PAIR(CARD_PULL_CTL5, 0x55),
	RTSX_REG_PAIR(CARD_PULL_CTL6, 0x15),
	0,
};

/* MS Pull Control Disable:
 *     MS CD       ==> pull up
 *     others      ==> pull down
 */
static const u32 rts5249_ms_pull_ctl_disable_tbl[] = {
	RTSX_REG_PAIR(CARD_PULL_CTL4, 0x55),
	RTSX_REG_PAIR(CARD_PULL_CTL5, 0x55),
	RTSX_REG_PAIR(CARD_PULL_CTL6, 0x15),
	0,
};

void rts5249_init_params(struct rtsx_pcr *pcr)
{
	struct rtsx_cr_option *option = &(pcr->option);

	pcr->extra_caps = EXTRA_CAPS_SD_SDR50 | EXTRA_CAPS_SD_SDR104;
	pcr->num_slots = 2;
	pcr->ops = &rts5249_pcr_ops;

	pcr->flags = 0;
	pcr->card_drive_sel = RTSX_CARD_DRIVE_DEFAULT;
	pcr->sd30_drive_sel_1v8 = CFG_DRIVER_TYPE_B;
	pcr->sd30_drive_sel_3v3 = CFG_DRIVER_TYPE_B;
	pcr->aspm_en = ASPM_L1_EN;
	pcr->tx_initial_phase = SET_CLOCK_PHASE(1, 29, 16);
	pcr->rx_initial_phase = SET_CLOCK_PHASE(24, 6, 5);

	pcr->ic_version = rts5249_get_ic_version(pcr);
	pcr->sd_pull_ctl_enable_tbl = rts5249_sd_pull_ctl_enable_tbl;
	pcr->sd_pull_ctl_disable_tbl = rts5249_sd_pull_ctl_disable_tbl;
	pcr->ms_pull_ctl_enable_tbl = rts5249_ms_pull_ctl_enable_tbl;
	pcr->ms_pull_ctl_disable_tbl = rts5249_ms_pull_ctl_disable_tbl;

	pcr->reg_pm_ctrl3 = PM_CTRL3;

	option->dev_flags = (LTR_L1SS_PWR_GATE_CHECK_CARD_EN
				| LTR_L1SS_PWR_GATE_EN);
	option->ltr_en = true;

	/* Init latency of active, idle, L1OFF to 60us, 300us, 3ms */
	option->ltr_active_latency = LTR_ACTIVE_LATENCY_DEF;
	option->ltr_idle_latency = LTR_IDLE_LATENCY_DEF;
	option->ltr_l1off_latency = LTR_L1OFF_LATENCY_DEF;
	option->dev_aspm_mode = DEV_ASPM_DYNAMIC;
	option->l1_snooze_delay = L1_SNOOZE_DELAY_DEF;
	option->ltr_l1off_sspwrgate = LTR_L1OFF_SSPWRGATE_5249_DEF;
	option->ltr_l1off_snooze_sspwrgate =
		LTR_L1OFF_SNOOZE_SSPWRGATE_5249_DEF;
}

static int rts524a_write_phy(struct rtsx_pcr *pcr, u8 addr, u16 val)
{
	addr = addr & 0x80 ? (addr & 0x7F) | 0x40 : addr;

	return __rtsx_pci_write_phy_register(pcr, addr, val);
}

static int rts524a_read_phy(struct rtsx_pcr *pcr, u8 addr, u16 *val)
{
	addr = addr & 0x80 ? (addr & 0x7F) | 0x40 : addr;

	return __rtsx_pci_read_phy_register(pcr, addr, val);
}

static int rts524a_optimize_phy(struct rtsx_pcr *pcr)
{
	int err;

	err = rtsx_pci_write_register(pcr, RTS524A_PM_CTRL3,
		D3_DELINK_MODE_EN, 0x00);
	if (err < 0)
		return err;

	rtsx_pci_write_phy_register(pcr, PHY_PCR,
		PHY_PCR_FORCE_CODE | PHY_PCR_OOBS_CALI_50 |
		PHY_PCR_OOBS_VCM_08 | PHY_PCR_OOBS_SEN_90 | PHY_PCR_RSSI_EN);
	rtsx_pci_write_phy_register(pcr, PHY_SSCCR3,
		PHY_SSCCR3_STEP_IN | PHY_SSCCR3_CHECK_DELAY);

	if (is_version(pcr, 0x524A, IC_VER_A)) {
		rtsx_pci_write_phy_register(pcr, PHY_SSCCR3,
			PHY_SSCCR3_STEP_IN | PHY_SSCCR3_CHECK_DELAY);
		rtsx_pci_write_phy_register(pcr, PHY_SSCCR2,
			PHY_SSCCR2_PLL_NCODE | PHY_SSCCR2_TIME0 |
			PHY_SSCCR2_TIME2_WIDTH);
		rtsx_pci_write_phy_register(pcr, PHY_ANA1A,
			PHY_ANA1A_TXR_LOOPBACK | PHY_ANA1A_RXT_BIST |
			PHY_ANA1A_TXR_BIST | PHY_ANA1A_REV);
		rtsx_pci_write_phy_register(pcr, PHY_ANA1D,
			PHY_ANA1D_DEBUG_ADDR);
		rtsx_pci_write_phy_register(pcr, PHY_DIG1E,
			PHY_DIG1E_REV | PHY_DIG1E_D0_X_D1 |
			PHY_DIG1E_RX_ON_HOST | PHY_DIG1E_RCLK_REF_HOST |
			PHY_DIG1E_RCLK_TX_EN_KEEP |
			PHY_DIG1E_RCLK_TX_TERM_KEEP |
			PHY_DIG1E_RCLK_RX_EIDLE_ON | PHY_DIG1E_TX_TERM_KEEP |
			PHY_DIG1E_RX_TERM_KEEP | PHY_DIG1E_TX_EN_KEEP |
			PHY_DIG1E_RX_EN_KEEP);
	}

	rtsx_pci_write_phy_register(pcr, PHY_ANA08,
		PHY_ANA08_RX_EQ_DCGAIN | PHY_ANA08_SEL_RX_EN |
		PHY_ANA08_RX_EQ_VAL | PHY_ANA08_SCP | PHY_ANA08_SEL_IPI);

	return 0;
}

static int rts524a_extra_init_hw(struct rtsx_pcr *pcr)
{
	rts5249_extra_init_hw(pcr);

	rtsx_pci_write_register(pcr, FUNC_FORCE_CTL,
		FORCE_ASPM_L1_EN, FORCE_ASPM_L1_EN);
	rtsx_pci_write_register(pcr, PM_EVENT_DEBUG, PME_DEBUG_0, PME_DEBUG_0);
	rtsx_pci_write_register(pcr, LDO_VCC_CFG1, LDO_VCC_LMT_EN,
		LDO_VCC_LMT_EN);
	rtsx_pci_write_register(pcr, PCLK_CTL, PCLK_MODE_SEL, PCLK_MODE_SEL);
	if (is_version(pcr, 0x524A, IC_VER_A)) {
		rtsx_pci_write_register(pcr, LDO_DV18_CFG,
			LDO_DV18_SR_MASK, LDO_DV18_SR_DF);
		rtsx_pci_write_register(pcr, LDO_VCC_CFG1,
			LDO_VCC_REF_TUNE_MASK, LDO_VCC_REF_1V2);
		rtsx_pci_write_register(pcr, LDO_VIO_CFG,
			LDO_VIO_REF_TUNE_MASK, LDO_VIO_REF_1V2);
		rtsx_pci_write_register(pcr, LDO_VIO_CFG,
			LDO_VIO_SR_MASK, LDO_VIO_SR_DF);
		rtsx_pci_write_register(pcr, LDO_DV12S_CFG,
			LDO_REF12_TUNE_MASK, LDO_REF12_TUNE_DF);
		rtsx_pci_write_register(pcr, SD40_LDO_CTL1,
			SD40_VIO_TUNE_MASK, SD40_VIO_TUNE_1V7);
	}

	return 0;
}

static void rts5250_set_l1off_cfg_sub_d0(struct rtsx_pcr *pcr, int active)
{
	struct rtsx_cr_option *option = &(pcr->option);

	u32 interrupt = rtsx_pci_readl(pcr, RTSX_BIPR);
	int card_exist = (interrupt & SD_EXIST) | (interrupt & MS_EXIST);
	int aspm_L1_1, aspm_L1_2;
	u8 val = 0;

	aspm_L1_1 = rtsx_check_dev_flag(pcr, ASPM_L1_1_EN);
	aspm_L1_2 = rtsx_check_dev_flag(pcr, ASPM_L1_2_EN);

	if (active) {
		/* Run, latency: 60us */
		if (aspm_L1_1)
			val = option->ltr_l1off_snooze_sspwrgate;
	} else {
		/* L1off, latency: 300us */
		if (aspm_L1_2)
			val = option->ltr_l1off_sspwrgate;
	}

	if (aspm_L1_1 || aspm_L1_2) {
		if (rtsx_check_dev_flag(pcr,
					LTR_L1SS_PWR_GATE_CHECK_CARD_EN)) {
			if (card_exist)
				val &= ~L1OFF_MBIAS2_EN_5250;
			else
				val |= L1OFF_MBIAS2_EN_5250;
		}
	}
	rtsx_set_l1off_sub(pcr, val);
}

static const struct pcr_ops rts524a_pcr_ops = {
	.write_phy = rts524a_write_phy,
	.read_phy = rts524a_read_phy,
	.fetch_vendor_settings = rtsx_base_fetch_vendor_settings,
	.extra_init_hw = rts524a_extra_init_hw,
	.optimize_phy = rts524a_optimize_phy,
	.turn_on_led = rtsx_base_turn_on_led,
	.turn_off_led = rtsx_base_turn_off_led,
	.enable_auto_blink = rtsx_base_enable_auto_blink,
	.disable_auto_blink = rtsx_base_disable_auto_blink,
	.card_power_on = rtsx_base_card_power_on,
	.card_power_off = rtsx_base_card_power_off,
	.switch_output_voltage = rtsx_base_switch_output_voltage,
	.force_power_down = rtsx_base_force_power_down,
	.set_l1off_cfg_sub_d0 = rts5250_set_l1off_cfg_sub_d0,
	.set_aspm = rts5249_set_aspm,
};

void rts524a_init_params(struct rtsx_pcr *pcr)
{
	rts5249_init_params(pcr);
	pcr->option.ltr_l1off_sspwrgate = LTR_L1OFF_SSPWRGATE_5250_DEF;
	pcr->option.ltr_l1off_snooze_sspwrgate =
		LTR_L1OFF_SNOOZE_SSPWRGATE_5250_DEF;

	pcr->reg_pm_ctrl3 = RTS524A_PM_CTRL3;
	pcr->ops = &rts524a_pcr_ops;

	pcr->option.ocp_en = 1;
	if (pcr->option.ocp_en)
		pcr->hw_param.interrupt_en |= SD_OC_INT_EN;
	pcr->hw_param.ocp_glitch = SD_OCP_GLITCH_10M;
	pcr->option.sd_800mA_ocp_thd = RTS524A_OCP_THD_800;

}

static int rts525a_card_power_on(struct rtsx_pcr *pcr, int card)
{
	rtsx_pci_write_register(pcr, LDO_VCC_CFG1,
		LDO_VCC_TUNE_MASK, LDO_VCC_3V3);
	return rtsx_base_card_power_on(pcr, card);
}

static int rts525a_switch_output_voltage(struct rtsx_pcr *pcr, u8 voltage)
{
	switch (voltage) {
	case OUTPUT_3V3:
		rtsx_pci_write_register(pcr, LDO_CONFIG2,
			LDO_D3318_MASK, LDO_D3318_33V);
		rtsx_pci_write_register(pcr, SD_PAD_CTL, SD_IO_USING_1V8, 0);
		break;
	case OUTPUT_1V8:
		rtsx_pci_write_register(pcr, LDO_CONFIG2,
			LDO_D3318_MASK, LDO_D3318_18V);
		rtsx_pci_write_register(pcr, SD_PAD_CTL, SD_IO_USING_1V8,
			SD_IO_USING_1V8);
		break;
	default:
		return -EINVAL;
	}

	rtsx_pci_init_cmd(pcr);
	rts5249_fill_driving(pcr, voltage);
	return rtsx_pci_send_cmd(pcr, 100);
}

static int rts525a_optimize_phy(struct rtsx_pcr *pcr)
{
	int err;

	err = rtsx_pci_write_register(pcr, RTS524A_PM_CTRL3,
		D3_DELINK_MODE_EN, 0x00);
	if (err < 0)
		return err;

	rtsx_pci_write_phy_register(pcr, _PHY_FLD0,
		_PHY_FLD0_CLK_REQ_20C | _PHY_FLD0_RX_IDLE_EN |
		_PHY_FLD0_BIT_ERR_RSTN | _PHY_FLD0_BER_COUNT |
		_PHY_FLD0_BER_TIMER | _PHY_FLD0_CHECK_EN);

	rtsx_pci_write_phy_register(pcr, _PHY_ANA03,
		_PHY_ANA03_TIMER_MAX | _PHY_ANA03_OOBS_DEB_EN |
		_PHY_CMU_DEBUG_EN);

	if (is_version(pcr, 0x525A, IC_VER_A))
		rtsx_pci_write_phy_register(pcr, _PHY_REV0,
			_PHY_REV0_FILTER_OUT | _PHY_REV0_CDR_BYPASS_PFD |
			_PHY_REV0_CDR_RX_IDLE_BYPASS);

	return 0;
}

static int rts525a_extra_init_hw(struct rtsx_pcr *pcr)
{
	rts5249_extra_init_hw(pcr);

	rtsx_pci_write_register(pcr, PCLK_CTL, PCLK_MODE_SEL, PCLK_MODE_SEL);
	if (is_version(pcr, 0x525A, IC_VER_A)) {
		rtsx_pci_write_register(pcr, L1SUB_CONFIG2,
			L1SUB_AUTO_CFG, L1SUB_AUTO_CFG);
		rtsx_pci_write_register(pcr, RREF_CFG,
			RREF_VBGSEL_MASK, RREF_VBGSEL_1V25);
		rtsx_pci_write_register(pcr, LDO_VIO_CFG,
			LDO_VIO_TUNE_MASK, LDO_VIO_1V7);
		rtsx_pci_write_register(pcr, LDO_DV12S_CFG,
			LDO_D12_TUNE_MASK, LDO_D12_TUNE_DF);
		rtsx_pci_write_register(pcr, LDO_AV12S_CFG,
			LDO_AV12S_TUNE_MASK, LDO_AV12S_TUNE_DF);
		rtsx_pci_write_register(pcr, LDO_VCC_CFG0,
			LDO_VCC_LMTVTH_MASK, LDO_VCC_LMTVTH_2A);
		rtsx_pci_write_register(pcr, OOBS_CONFIG,
			OOBS_AUTOK_DIS | OOBS_VAL_MASK, 0x89);
	}

	return 0;
}

static const struct pcr_ops rts525a_pcr_ops = {
	.fetch_vendor_settings = rtsx_base_fetch_vendor_settings,
	.extra_init_hw = rts525a_extra_init_hw,
	.optimize_phy = rts525a_optimize_phy,
	.turn_on_led = rtsx_base_turn_on_led,
	.turn_off_led = rtsx_base_turn_off_led,
	.enable_auto_blink = rtsx_base_enable_auto_blink,
	.disable_auto_blink = rtsx_base_disable_auto_blink,
	.card_power_on = rts525a_card_power_on,
	.card_power_off = rtsx_base_card_power_off,
	.switch_output_voltage = rts525a_switch_output_voltage,
	.force_power_down = rtsx_base_force_power_down,
	.set_l1off_cfg_sub_d0 = rts5250_set_l1off_cfg_sub_d0,
	.set_aspm = rts5249_set_aspm,
};

void rts525a_init_params(struct rtsx_pcr *pcr)
{
	rts5249_init_params(pcr);
	pcr->option.ltr_l1off_sspwrgate = LTR_L1OFF_SSPWRGATE_5250_DEF;
	pcr->option.ltr_l1off_snooze_sspwrgate =
		LTR_L1OFF_SNOOZE_SSPWRGATE_5250_DEF;

	pcr->reg_pm_ctrl3 = RTS524A_PM_CTRL3;
	pcr->ops = &rts525a_pcr_ops;

	pcr->option.ocp_en = 1;
	if (pcr->option.ocp_en)
		pcr->hw_param.interrupt_en |= SD_OC_INT_EN;
	pcr->hw_param.ocp_glitch = SD_OCP_GLITCH_10M;
	pcr->option.sd_800mA_ocp_thd = RTS525A_OCP_THD_800;
}
