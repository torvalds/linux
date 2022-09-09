// SPDX-License-Identifier: GPL-2.0-or-later
/* Driver for Realtek PCI-Express card reader
 *
 * Copyright(c) 2018-2019 Realtek Semiconductor Corp. All rights reserved.
 *
 * Author:
 *   Rui FENG <rui_feng@realsil.com.cn>
 *   Wei WANG <wei_wang@realsil.com.cn>
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/rtsx_pci.h>

#include "rts5261.h"
#include "rtsx_pcr.h"

static u8 rts5261_get_ic_version(struct rtsx_pcr *pcr)
{
	u8 val;

	rtsx_pci_read_register(pcr, DUMMY_REG_RESET_0, &val);
	return val & IC_VERSION_MASK;
}

static void rts5261_fill_driving(struct rtsx_pcr *pcr, u8 voltage)
{
	u8 driving_3v3[4][3] = {
		{0x96, 0x96, 0x96},
		{0x96, 0x96, 0x96},
		{0x7F, 0x7F, 0x7F},
		{0x13, 0x13, 0x13},
	};
	u8 driving_1v8[4][3] = {
		{0xB3, 0xB3, 0xB3},
		{0x3A, 0x3A, 0x3A},
		{0xE6, 0xE6, 0xE6},
		{0x99, 0x99, 0x99},
	};
	u8 (*driving)[3], drive_sel;

	if (voltage == OUTPUT_3V3) {
		driving = driving_3v3;
		drive_sel = pcr->sd30_drive_sel_3v3;
	} else {
		driving = driving_1v8;
		drive_sel = pcr->sd30_drive_sel_1v8;
	}

	rtsx_pci_write_register(pcr, SD30_CLK_DRIVE_SEL,
			 0xFF, driving[drive_sel][0]);

	rtsx_pci_write_register(pcr, SD30_CMD_DRIVE_SEL,
			 0xFF, driving[drive_sel][1]);

	rtsx_pci_write_register(pcr, SD30_DAT_DRIVE_SEL,
			 0xFF, driving[drive_sel][2]);
}

static void rts5261_force_power_down(struct rtsx_pcr *pcr, u8 pm_state, bool runtime)
{
	/* Set relink_time to 0 */
	rtsx_pci_write_register(pcr, AUTOLOAD_CFG_BASE + 1, MASK_8_BIT_DEF, 0);
	rtsx_pci_write_register(pcr, AUTOLOAD_CFG_BASE + 2, MASK_8_BIT_DEF, 0);
	rtsx_pci_write_register(pcr, AUTOLOAD_CFG_BASE + 3,
				RELINK_TIME_MASK, 0);

	if (pm_state == HOST_ENTER_S3)
		rtsx_pci_write_register(pcr, pcr->reg_pm_ctrl3,
					D3_DELINK_MODE_EN, D3_DELINK_MODE_EN);

	if (!runtime) {
		rtsx_pci_write_register(pcr, RTS5261_AUTOLOAD_CFG1,
				CD_RESUME_EN_MASK, 0);
		rtsx_pci_write_register(pcr, pcr->reg_pm_ctrl3, 0x01, 0x00);
		rtsx_pci_write_register(pcr, RTS5261_REG_PME_FORCE_CTL,
				FORCE_PM_CONTROL | FORCE_PM_VALUE, FORCE_PM_CONTROL);

	} else {
		rtsx_pci_write_register(pcr, RTS5261_REG_PME_FORCE_CTL,
				FORCE_PM_CONTROL | FORCE_PM_VALUE, 0);

		rtsx_pci_write_register(pcr, RTS5261_FW_CTL,
				RTS5261_INFORM_RTD3_COLD, RTS5261_INFORM_RTD3_COLD);
		rtsx_pci_write_register(pcr, RTS5261_AUTOLOAD_CFG4,
				RTS5261_FORCE_PRSNT_LOW, RTS5261_FORCE_PRSNT_LOW);

	}

	rtsx_pci_write_register(pcr, RTS5261_REG_FPDCTL,
		SSC_POWER_DOWN, SSC_POWER_DOWN);
}

static int rts5261_enable_auto_blink(struct rtsx_pcr *pcr)
{
	return rtsx_pci_write_register(pcr, OLT_LED_CTL,
		LED_SHINE_MASK, LED_SHINE_EN);
}

static int rts5261_disable_auto_blink(struct rtsx_pcr *pcr)
{
	return rtsx_pci_write_register(pcr, OLT_LED_CTL,
		LED_SHINE_MASK, LED_SHINE_DISABLE);
}

static int rts5261_turn_on_led(struct rtsx_pcr *pcr)
{
	return rtsx_pci_write_register(pcr, GPIO_CTL,
		0x02, 0x02);
}

static int rts5261_turn_off_led(struct rtsx_pcr *pcr)
{
	return rtsx_pci_write_register(pcr, GPIO_CTL,
		0x02, 0x00);
}

/* SD Pull Control Enable:
 *     SD_DAT[3:0] ==> pull up
 *     SD_CD       ==> pull up
 *     SD_WP       ==> pull up
 *     SD_CMD      ==> pull up
 *     SD_CLK      ==> pull down
 */
static const u32 rts5261_sd_pull_ctl_enable_tbl[] = {
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
static const u32 rts5261_sd_pull_ctl_disable_tbl[] = {
	RTSX_REG_PAIR(CARD_PULL_CTL2, 0x55),
	RTSX_REG_PAIR(CARD_PULL_CTL3, 0xD5),
	0,
};

static int rts5261_sd_set_sample_push_timing_sd30(struct rtsx_pcr *pcr)
{
	rtsx_pci_write_register(pcr, SD_CFG1, SD_MODE_SELECT_MASK
		| SD_ASYNC_FIFO_NOT_RST, SD_30_MODE | SD_ASYNC_FIFO_NOT_RST);
	rtsx_pci_write_register(pcr, CLK_CTL, CLK_LOW_FREQ, CLK_LOW_FREQ);
	rtsx_pci_write_register(pcr, CARD_CLK_SOURCE, 0xFF,
			CRC_VAR_CLK0 | SD30_FIX_CLK | SAMPLE_VAR_CLK1);
	rtsx_pci_write_register(pcr, CLK_CTL, CLK_LOW_FREQ, 0);

	return 0;
}

static int rts5261_card_power_on(struct rtsx_pcr *pcr, int card)
{
	struct rtsx_cr_option *option = &pcr->option;

	if (option->ocp_en)
		rtsx_pci_enable_ocp(pcr);

	rtsx_pci_write_register(pcr, REG_CRC_DUMMY_0,
		CFG_SD_POW_AUTO_PD, CFG_SD_POW_AUTO_PD);

	rtsx_pci_write_register(pcr, RTS5261_LDO1_CFG1,
			RTS5261_LDO1_TUNE_MASK, RTS5261_LDO1_33);
	rtsx_pci_write_register(pcr, RTS5261_LDO1233318_POW_CTL,
			RTS5261_LDO1_POWERON, RTS5261_LDO1_POWERON);

	rtsx_pci_write_register(pcr, RTS5261_LDO1233318_POW_CTL,
			RTS5261_LDO3318_POWERON, RTS5261_LDO3318_POWERON);

	msleep(20);

	rtsx_pci_write_register(pcr, CARD_OE, SD_OUTPUT_EN, SD_OUTPUT_EN);

	/* Initialize SD_CFG1 register */
	rtsx_pci_write_register(pcr, SD_CFG1, 0xFF,
			SD_CLK_DIVIDE_128 | SD_20_MODE | SD_BUS_WIDTH_1BIT);

	rtsx_pci_write_register(pcr, SD_SAMPLE_POINT_CTL,
			0xFF, SD20_RX_POS_EDGE);
	rtsx_pci_write_register(pcr, SD_PUSH_POINT_CTL, 0xFF, 0);
	rtsx_pci_write_register(pcr, CARD_STOP, SD_STOP | SD_CLR_ERR,
			SD_STOP | SD_CLR_ERR);

	/* Reset SD_CFG3 register */
	rtsx_pci_write_register(pcr, SD_CFG3, SD30_CLK_END_EN, 0);
	rtsx_pci_write_register(pcr, REG_SD_STOP_SDCLK_CFG,
			SD30_CLK_STOP_CFG_EN | SD30_CLK_STOP_CFG1 |
			SD30_CLK_STOP_CFG0, 0);

	if (pcr->extra_caps & EXTRA_CAPS_SD_SDR50 ||
	    pcr->extra_caps & EXTRA_CAPS_SD_SDR104)
		rts5261_sd_set_sample_push_timing_sd30(pcr);

	return 0;
}

static int rts5261_switch_output_voltage(struct rtsx_pcr *pcr, u8 voltage)
{
	int err;
	u16 val = 0;

	rtsx_pci_write_register(pcr, RTS5261_CARD_PWR_CTL,
			RTS5261_PUPDC, RTS5261_PUPDC);

	switch (voltage) {
	case OUTPUT_3V3:
		rtsx_pci_read_phy_register(pcr, PHY_TUNE, &val);
		val |= PHY_TUNE_SDBUS_33;
		err = rtsx_pci_write_phy_register(pcr, PHY_TUNE, val);
		if (err < 0)
			return err;

		rtsx_pci_write_register(pcr, RTS5261_DV3318_CFG,
				RTS5261_DV3318_TUNE_MASK, RTS5261_DV3318_33);
		rtsx_pci_write_register(pcr, SD_PAD_CTL,
				SD_IO_USING_1V8, 0);
		break;
	case OUTPUT_1V8:
		rtsx_pci_read_phy_register(pcr, PHY_TUNE, &val);
		val &= ~PHY_TUNE_SDBUS_33;
		err = rtsx_pci_write_phy_register(pcr, PHY_TUNE, val);
		if (err < 0)
			return err;

		rtsx_pci_write_register(pcr, RTS5261_DV3318_CFG,
				RTS5261_DV3318_TUNE_MASK, RTS5261_DV3318_18);
		rtsx_pci_write_register(pcr, SD_PAD_CTL,
				SD_IO_USING_1V8, SD_IO_USING_1V8);
		break;
	default:
		return -EINVAL;
	}

	/* set pad drive */
	rts5261_fill_driving(pcr, voltage);

	return 0;
}

static void rts5261_stop_cmd(struct rtsx_pcr *pcr)
{
	rtsx_pci_writel(pcr, RTSX_HCBCTLR, STOP_CMD);
	rtsx_pci_writel(pcr, RTSX_HDBCTLR, STOP_DMA);
	rtsx_pci_write_register(pcr, RTS5260_DMA_RST_CTL_0,
				RTS5260_DMA_RST | RTS5260_ADMA3_RST,
				RTS5260_DMA_RST | RTS5260_ADMA3_RST);
	rtsx_pci_write_register(pcr, RBCTL, RB_FLUSH, RB_FLUSH);
}

static void rts5261_card_before_power_off(struct rtsx_pcr *pcr)
{
	rts5261_stop_cmd(pcr);
	rts5261_switch_output_voltage(pcr, OUTPUT_3V3);

}

static void rts5261_enable_ocp(struct rtsx_pcr *pcr)
{
	u8 val = 0;

	val = SD_OCP_INT_EN | SD_DETECT_EN;
	rtsx_pci_write_register(pcr, RTS5261_LDO1_CFG0,
			RTS5261_LDO1_OCP_EN | RTS5261_LDO1_OCP_LMT_EN,
			RTS5261_LDO1_OCP_EN | RTS5261_LDO1_OCP_LMT_EN);
	rtsx_pci_write_register(pcr, REG_OCPCTL, 0xFF, val);

}

static void rts5261_disable_ocp(struct rtsx_pcr *pcr)
{
	u8 mask = 0;

	mask = SD_OCP_INT_EN | SD_DETECT_EN;
	rtsx_pci_write_register(pcr, REG_OCPCTL, mask, 0);
	rtsx_pci_write_register(pcr, RTS5261_LDO1_CFG0,
			RTS5261_LDO1_OCP_EN | RTS5261_LDO1_OCP_LMT_EN, 0);

}

static int rts5261_card_power_off(struct rtsx_pcr *pcr, int card)
{
	int err = 0;

	rts5261_card_before_power_off(pcr);
	err = rtsx_pci_write_register(pcr, RTS5261_LDO1233318_POW_CTL,
				RTS5261_LDO_POWERON_MASK, 0);

	rtsx_pci_write_register(pcr, REG_CRC_DUMMY_0,
		CFG_SD_POW_AUTO_PD, 0);
	if (pcr->option.ocp_en)
		rtsx_pci_disable_ocp(pcr);

	return err;
}

static void rts5261_init_ocp(struct rtsx_pcr *pcr)
{
	struct rtsx_cr_option *option = &pcr->option;

	if (option->ocp_en) {
		u8 mask, val;

		rtsx_pci_write_register(pcr, RTS5261_LDO1_CFG0,
			RTS5261_LDO1_OCP_EN | RTS5261_LDO1_OCP_LMT_EN,
			RTS5261_LDO1_OCP_EN | RTS5261_LDO1_OCP_LMT_EN);

		rtsx_pci_write_register(pcr, RTS5261_LDO1_CFG0,
			RTS5261_LDO1_OCP_THD_MASK, option->sd_800mA_ocp_thd);

		rtsx_pci_write_register(pcr, RTS5261_LDO1_CFG0,
			RTS5261_LDO1_OCP_LMT_THD_MASK,
			RTS5261_LDO1_LMT_THD_2000);

		mask = SD_OCP_GLITCH_MASK;
		val = pcr->hw_param.ocp_glitch;
		rtsx_pci_write_register(pcr, REG_OCPGLITCH, mask, val);

		rts5261_enable_ocp(pcr);
	} else {
		rtsx_pci_write_register(pcr, RTS5261_LDO1_CFG0,
			RTS5261_LDO1_OCP_EN | RTS5261_LDO1_OCP_LMT_EN, 0);
	}
}

static void rts5261_clear_ocpstat(struct rtsx_pcr *pcr)
{
	u8 mask = 0;
	u8 val = 0;

	mask = SD_OCP_INT_CLR | SD_OC_CLR;
	val = SD_OCP_INT_CLR | SD_OC_CLR;

	rtsx_pci_write_register(pcr, REG_OCPCTL, mask, val);

	udelay(1000);
	rtsx_pci_write_register(pcr, REG_OCPCTL, mask, 0);

}

static void rts5261_process_ocp(struct rtsx_pcr *pcr)
{
	if (!pcr->option.ocp_en)
		return;

	rtsx_pci_get_ocpstat(pcr, &pcr->ocp_stat);

	if (pcr->ocp_stat & (SD_OC_NOW | SD_OC_EVER)) {
		rts5261_clear_ocpstat(pcr);
		rts5261_card_power_off(pcr, RTSX_SD_CARD);
		rtsx_pci_write_register(pcr, CARD_OE, SD_OUTPUT_EN, 0);
		pcr->ocp_stat = 0;
	}

}

static void rts5261_init_from_hw(struct rtsx_pcr *pcr)
{
	struct pci_dev *pdev = pcr->pci;
	u32 lval1, lval2, i;
	u16 setting_reg1, setting_reg2;
	u8 valid, efuse_valid, tmp;

	rtsx_pci_write_register(pcr, RTS5261_REG_PME_FORCE_CTL,
		REG_EFUSE_POR | REG_EFUSE_POWER_MASK,
		REG_EFUSE_POR | REG_EFUSE_POWERON);
	udelay(1);
	rtsx_pci_write_register(pcr, RTS5261_EFUSE_ADDR,
		RTS5261_EFUSE_ADDR_MASK, 0x00);
	rtsx_pci_write_register(pcr, RTS5261_EFUSE_CTL,
		RTS5261_EFUSE_ENABLE | RTS5261_EFUSE_MODE_MASK,
		RTS5261_EFUSE_ENABLE);

	/* Wait transfer end */
	for (i = 0; i < MAX_RW_REG_CNT; i++) {
		rtsx_pci_read_register(pcr, RTS5261_EFUSE_CTL, &tmp);
		if ((tmp & 0x80) == 0)
			break;
	}
	rtsx_pci_read_register(pcr, RTS5261_EFUSE_READ_DATA, &tmp);
	efuse_valid = ((tmp & 0x0C) >> 2);
	pcr_dbg(pcr, "Load efuse valid: 0x%x\n", efuse_valid);

	pci_read_config_dword(pdev, PCR_SETTING_REG2, &lval2);
	pcr_dbg(pcr, "Cfg 0x%x: 0x%x\n", PCR_SETTING_REG2, lval2);
	/* 0x816 */
	valid = (u8)((lval2 >> 16) & 0x03);

	rtsx_pci_write_register(pcr, RTS5261_REG_PME_FORCE_CTL,
		REG_EFUSE_POR, 0);
	pcr_dbg(pcr, "Disable efuse por!\n");

	if (efuse_valid == 2 || efuse_valid == 3) {
		if (valid == 3) {
			/* Bypass efuse */
			setting_reg1 = PCR_SETTING_REG1;
			setting_reg2 = PCR_SETTING_REG2;
		} else {
			/* Use efuse data */
			setting_reg1 = PCR_SETTING_REG4;
			setting_reg2 = PCR_SETTING_REG5;
		}
	} else if (efuse_valid == 0) {
		// default
		setting_reg1 = PCR_SETTING_REG1;
		setting_reg2 = PCR_SETTING_REG2;
	} else {
		return;
	}

	pci_read_config_dword(pdev, setting_reg2, &lval2);
	pcr_dbg(pcr, "Cfg 0x%x: 0x%x\n", setting_reg2, lval2);

	if (!rts5261_vendor_setting_valid(lval2)) {
		/* Not support MMC default */
		pcr->extra_caps |= EXTRA_CAPS_NO_MMC;
		pcr_dbg(pcr, "skip fetch vendor setting\n");
		return;
	}

	if (!rts5261_reg_check_mmc_support(lval2))
		pcr->extra_caps |= EXTRA_CAPS_NO_MMC;

	pcr->rtd3_en = rts5261_reg_to_rtd3(lval2);

	if (rts5261_reg_check_reverse_socket(lval2))
		pcr->flags |= PCR_REVERSE_SOCKET;

	pci_read_config_dword(pdev, setting_reg1, &lval1);
	pcr_dbg(pcr, "Cfg 0x%x: 0x%x\n", setting_reg1, lval1);

	pcr->aspm_en = rts5261_reg_to_aspm(lval1);
	pcr->sd30_drive_sel_1v8 = rts5261_reg_to_sd30_drive_sel_1v8(lval1);
	pcr->sd30_drive_sel_3v3 = rts5261_reg_to_sd30_drive_sel_3v3(lval1);

	if (setting_reg1 == PCR_SETTING_REG1) {
		/* store setting */
		rtsx_pci_write_register(pcr, 0xFF0C, 0xFF, (u8)(lval1 & 0xFF));
		rtsx_pci_write_register(pcr, 0xFF0D, 0xFF, (u8)((lval1 >> 8) & 0xFF));
		rtsx_pci_write_register(pcr, 0xFF0E, 0xFF, (u8)((lval1 >> 16) & 0xFF));
		rtsx_pci_write_register(pcr, 0xFF0F, 0xFF, (u8)((lval1 >> 24) & 0xFF));
		rtsx_pci_write_register(pcr, 0xFF10, 0xFF, (u8)(lval2 & 0xFF));
		rtsx_pci_write_register(pcr, 0xFF11, 0xFF, (u8)((lval2 >> 8) & 0xFF));
		rtsx_pci_write_register(pcr, 0xFF12, 0xFF, (u8)((lval2 >> 16) & 0xFF));

		pci_write_config_dword(pdev, PCR_SETTING_REG4, lval1);
		lval2 = lval2 & 0x00FFFFFF;
		pci_write_config_dword(pdev, PCR_SETTING_REG5, lval2);
	}
}

static void rts5261_init_from_cfg(struct rtsx_pcr *pcr)
{
	struct pci_dev *pdev = pcr->pci;
	int l1ss;
	u32 lval;
	struct rtsx_cr_option *option = &pcr->option;

	l1ss = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_L1SS);
	if (!l1ss)
		return;

	pci_read_config_dword(pdev, l1ss + PCI_L1SS_CTL1, &lval);

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

	rtsx_pci_write_register(pcr, ASPM_FORCE_CTL, 0xFF, 0);
	if (option->ltr_en) {
		u16 val;

		pcie_capability_read_word(pdev, PCI_EXP_DEVCTL2, &val);
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

static int rts5261_extra_init_hw(struct rtsx_pcr *pcr)
{
	struct rtsx_cr_option *option = &pcr->option;
	u32 val;

	rtsx_pci_write_register(pcr, RTS5261_AUTOLOAD_CFG1,
			CD_RESUME_EN_MASK, CD_RESUME_EN_MASK);

	rts5261_init_from_cfg(pcr);
	rts5261_init_from_hw(pcr);

	/* power off efuse */
	rtsx_pci_write_register(pcr, RTS5261_REG_PME_FORCE_CTL,
			REG_EFUSE_POWER_MASK, REG_EFUSE_POWEROFF);
	rtsx_pci_write_register(pcr, L1SUB_CONFIG1,
			AUX_CLK_ACTIVE_SEL_MASK, MAC_CKSW_DONE);
	rtsx_pci_write_register(pcr, L1SUB_CONFIG3, 0xFF, 0);

	if (is_version_higher_than(pcr, PID_5261, IC_VER_B)) {
		val = rtsx_pci_readl(pcr, RTSX_DUM_REG);
		rtsx_pci_writel(pcr, RTSX_DUM_REG, val | 0x1);
	}
	rtsx_pci_write_register(pcr, RTS5261_AUTOLOAD_CFG4,
			RTS5261_AUX_CLK_16M_EN, 0);

	/* Release PRSNT# */
	rtsx_pci_write_register(pcr, RTS5261_AUTOLOAD_CFG4,
			RTS5261_FORCE_PRSNT_LOW, 0);
	rtsx_pci_write_register(pcr, FUNC_FORCE_CTL,
			FUNC_FORCE_UPME_XMT_DBG, FUNC_FORCE_UPME_XMT_DBG);

	rtsx_pci_write_register(pcr, PCLK_CTL,
			PCLK_MODE_SEL, PCLK_MODE_SEL);

	rtsx_pci_write_register(pcr, PM_EVENT_DEBUG, PME_DEBUG_0, PME_DEBUG_0);
	rtsx_pci_write_register(pcr, PM_CLK_FORCE_CTL, CLK_PM_EN, CLK_PM_EN);

	/* LED shine disabled, set initial shine cycle period */
	rtsx_pci_write_register(pcr, OLT_LED_CTL, 0x0F, 0x02);

	/* Configure driving */
	rts5261_fill_driving(pcr, OUTPUT_3V3);

	if (pcr->flags & PCR_REVERSE_SOCKET)
		rtsx_pci_write_register(pcr, PETXCFG, 0x30, 0x30);
	else
		rtsx_pci_write_register(pcr, PETXCFG, 0x30, 0x00);

	/*
	 * If u_force_clkreq_0 is enabled, CLKREQ# PIN will be forced
	 * to drive low, and we forcibly request clock.
	 */
	if (option->force_clkreq_0)
		rtsx_pci_write_register(pcr, PETXCFG,
				 FORCE_CLKREQ_DELINK_MASK, FORCE_CLKREQ_LOW);
	else
		rtsx_pci_write_register(pcr, PETXCFG,
				 FORCE_CLKREQ_DELINK_MASK, FORCE_CLKREQ_HIGH);

	rtsx_pci_write_register(pcr, PWD_SUSPEND_EN, 0xFF, 0xFB);

	if (pcr->rtd3_en) {
		rtsx_pci_write_register(pcr, pcr->reg_pm_ctrl3, 0x01, 0x01);
		rtsx_pci_write_register(pcr, RTS5261_REG_PME_FORCE_CTL,
				FORCE_PM_CONTROL | FORCE_PM_VALUE,
				FORCE_PM_CONTROL | FORCE_PM_VALUE);
	} else {
		rtsx_pci_write_register(pcr, pcr->reg_pm_ctrl3, 0x01, 0x00);
		rtsx_pci_write_register(pcr, RTS5261_REG_PME_FORCE_CTL,
				FORCE_PM_CONTROL | FORCE_PM_VALUE, FORCE_PM_CONTROL);
	}
	rtsx_pci_write_register(pcr, pcr->reg_pm_ctrl3, D3_DELINK_MODE_EN, 0x00);

	/* Clear Enter RTD3_cold Information*/
	rtsx_pci_write_register(pcr, RTS5261_FW_CTL,
		RTS5261_INFORM_RTD3_COLD, 0);

	return 0;
}

static void rts5261_enable_aspm(struct rtsx_pcr *pcr, bool enable)
{
	u8 val = FORCE_ASPM_CTL0 | FORCE_ASPM_CTL1;
	u8 mask = FORCE_ASPM_VAL_MASK | FORCE_ASPM_CTL0 | FORCE_ASPM_CTL1;

	if (pcr->aspm_enabled == enable)
		return;

	val |= (pcr->aspm_en & 0x02);
	rtsx_pci_write_register(pcr, ASPM_FORCE_CTL, mask, val);
	pcie_capability_clear_and_set_word(pcr->pci, PCI_EXP_LNKCTL,
					   PCI_EXP_LNKCTL_ASPMC, pcr->aspm_en);
	pcr->aspm_enabled = enable;
}

static void rts5261_disable_aspm(struct rtsx_pcr *pcr, bool enable)
{
	u8 val = FORCE_ASPM_CTL0 | FORCE_ASPM_CTL1;
	u8 mask = FORCE_ASPM_VAL_MASK | FORCE_ASPM_CTL0 | FORCE_ASPM_CTL1;

	if (pcr->aspm_enabled == enable)
		return;

	pcie_capability_clear_and_set_word(pcr->pci, PCI_EXP_LNKCTL,
					   PCI_EXP_LNKCTL_ASPMC, 0);
	rtsx_pci_write_register(pcr, ASPM_FORCE_CTL, mask, val);
	rtsx_pci_write_register(pcr, SD_CFG1, SD_ASYNC_FIFO_NOT_RST, 0);
	udelay(10);
	pcr->aspm_enabled = enable;
}

static void rts5261_set_aspm(struct rtsx_pcr *pcr, bool enable)
{
	if (enable)
		rts5261_enable_aspm(pcr, true);
	else
		rts5261_disable_aspm(pcr, false);
}

static void rts5261_set_l1off_cfg_sub_d0(struct rtsx_pcr *pcr, int active)
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

static const struct pcr_ops rts5261_pcr_ops = {
	.turn_on_led = rts5261_turn_on_led,
	.turn_off_led = rts5261_turn_off_led,
	.extra_init_hw = rts5261_extra_init_hw,
	.enable_auto_blink = rts5261_enable_auto_blink,
	.disable_auto_blink = rts5261_disable_auto_blink,
	.card_power_on = rts5261_card_power_on,
	.card_power_off = rts5261_card_power_off,
	.switch_output_voltage = rts5261_switch_output_voltage,
	.force_power_down = rts5261_force_power_down,
	.stop_cmd = rts5261_stop_cmd,
	.set_aspm = rts5261_set_aspm,
	.set_l1off_cfg_sub_d0 = rts5261_set_l1off_cfg_sub_d0,
	.enable_ocp = rts5261_enable_ocp,
	.disable_ocp = rts5261_disable_ocp,
	.init_ocp = rts5261_init_ocp,
	.process_ocp = rts5261_process_ocp,
	.clear_ocpstat = rts5261_clear_ocpstat,
};

static inline u8 double_ssc_depth(u8 depth)
{
	return ((depth > 1) ? (depth - 1) : depth);
}

int rts5261_pci_switch_clock(struct rtsx_pcr *pcr, unsigned int card_clock,
		u8 ssc_depth, bool initial_mode, bool double_clk, bool vpclk)
{
	int err, clk;
	u16 n;
	u8 clk_divider, mcu_cnt, div;
	static const u8 depth[] = {
		[RTSX_SSC_DEPTH_4M] = RTS5261_SSC_DEPTH_4M,
		[RTSX_SSC_DEPTH_2M] = RTS5261_SSC_DEPTH_2M,
		[RTSX_SSC_DEPTH_1M] = RTS5261_SSC_DEPTH_1M,
		[RTSX_SSC_DEPTH_500K] = RTS5261_SSC_DEPTH_512K,
	};

	if (initial_mode) {
		/* We use 250k(around) here, in initial stage */
		if (is_version_higher_than(pcr, PID_5261, IC_VER_C)) {
			clk_divider = SD_CLK_DIVIDE_256;
			card_clock = 60000000;
		} else {
			clk_divider = SD_CLK_DIVIDE_128;
			card_clock = 30000000;
		}
	} else {
		clk_divider = SD_CLK_DIVIDE_0;
	}
	err = rtsx_pci_write_register(pcr, SD_CFG1,
			SD_CLK_DIVIDE_MASK, clk_divider);
	if (err < 0)
		return err;

	card_clock /= 1000000;
	pcr_dbg(pcr, "Switch card clock to %dMHz\n", card_clock);

	clk = card_clock;
	if (!initial_mode && double_clk)
		clk = card_clock * 2;
	pcr_dbg(pcr, "Internal SSC clock: %dMHz (cur_clock = %d)\n",
		clk, pcr->cur_clock);

	if (clk == pcr->cur_clock)
		return 0;

	if (pcr->ops->conv_clk_and_div_n)
		n = pcr->ops->conv_clk_and_div_n(clk, CLK_TO_DIV_N);
	else
		n = clk - 4;
	if ((clk <= 4) || (n > 396))
		return -EINVAL;

	mcu_cnt = 125/clk + 3;
	if (mcu_cnt > 15)
		mcu_cnt = 15;

	div = CLK_DIV_1;
	while ((n < MIN_DIV_N_PCR - 4) && (div < CLK_DIV_8)) {
		if (pcr->ops->conv_clk_and_div_n) {
			int dbl_clk = pcr->ops->conv_clk_and_div_n(n,
					DIV_N_TO_CLK) * 2;
			n = pcr->ops->conv_clk_and_div_n(dbl_clk,
					CLK_TO_DIV_N);
		} else {
			n = (n + 4) * 2 - 4;
		}
		div++;
	}

	n = (n / 2) - 1;
	pcr_dbg(pcr, "n = %d, div = %d\n", n, div);

	ssc_depth = depth[ssc_depth];
	if (double_clk)
		ssc_depth = double_ssc_depth(ssc_depth);

	if (ssc_depth) {
		if (div == CLK_DIV_2) {
			if (ssc_depth > 1)
				ssc_depth -= 1;
			else
				ssc_depth = RTS5261_SSC_DEPTH_8M;
		} else if (div == CLK_DIV_4) {
			if (ssc_depth > 2)
				ssc_depth -= 2;
			else
				ssc_depth = RTS5261_SSC_DEPTH_8M;
		} else if (div == CLK_DIV_8) {
			if (ssc_depth > 3)
				ssc_depth -= 3;
			else
				ssc_depth = RTS5261_SSC_DEPTH_8M;
		}
	} else {
		ssc_depth = 0;
	}
	pcr_dbg(pcr, "ssc_depth = %d\n", ssc_depth);

	rtsx_pci_init_cmd(pcr);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CLK_CTL,
				CLK_LOW_FREQ, CLK_LOW_FREQ);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CLK_DIV,
			0xFF, (div << 4) | mcu_cnt);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SSC_CTL1, SSC_RSTB, 0);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SSC_CTL2,
			SSC_DEPTH_MASK, ssc_depth);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SSC_DIV_N_0, 0xFF, n);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SSC_CTL1, SSC_RSTB, SSC_RSTB);
	if (vpclk) {
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_VPCLK0_CTL,
				PHASE_NOT_RESET, 0);
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_VPCLK1_CTL,
				PHASE_NOT_RESET, 0);
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_VPCLK0_CTL,
				PHASE_NOT_RESET, PHASE_NOT_RESET);
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SD_VPCLK1_CTL,
				PHASE_NOT_RESET, PHASE_NOT_RESET);
	}

	err = rtsx_pci_send_cmd(pcr, 2000);
	if (err < 0)
		return err;

	/* Wait SSC clock stable */
	udelay(SSC_CLOCK_STABLE_WAIT);
	err = rtsx_pci_write_register(pcr, CLK_CTL, CLK_LOW_FREQ, 0);
	if (err < 0)
		return err;

	pcr->cur_clock = clk;
	return 0;

}

void rts5261_init_params(struct rtsx_pcr *pcr)
{
	struct rtsx_cr_option *option = &pcr->option;
	struct rtsx_hw_param *hw_param = &pcr->hw_param;
	u8 val;

	pcr->extra_caps = EXTRA_CAPS_SD_SDR50 | EXTRA_CAPS_SD_SDR104;
	rtsx_pci_read_register(pcr, RTS5261_FW_STATUS, &val);
	if (!(val & RTS5261_EXPRESS_LINK_FAIL_MASK))
		pcr->extra_caps |= EXTRA_CAPS_SD_EXPRESS;
	pcr->num_slots = 1;
	pcr->ops = &rts5261_pcr_ops;

	pcr->flags = 0;
	pcr->card_drive_sel = RTSX_CARD_DRIVE_DEFAULT;
	pcr->sd30_drive_sel_1v8 = 0x00;
	pcr->sd30_drive_sel_3v3 = 0x00;
	pcr->aspm_en = ASPM_L1_EN;
	pcr->aspm_mode = ASPM_MODE_REG;
	pcr->tx_initial_phase = SET_CLOCK_PHASE(27, 27, 11);
	pcr->rx_initial_phase = SET_CLOCK_PHASE(24, 6, 5);

	pcr->ic_version = rts5261_get_ic_version(pcr);
	pcr->sd_pull_ctl_enable_tbl = rts5261_sd_pull_ctl_enable_tbl;
	pcr->sd_pull_ctl_disable_tbl = rts5261_sd_pull_ctl_disable_tbl;

	pcr->reg_pm_ctrl3 = RTS5261_AUTOLOAD_CFG3;

	option->dev_flags = (LTR_L1SS_PWR_GATE_CHECK_CARD_EN
				| LTR_L1SS_PWR_GATE_EN);
	option->ltr_en = true;

	/* init latency of active, idle, L1OFF to 60us, 300us, 3ms */
	option->ltr_active_latency = LTR_ACTIVE_LATENCY_DEF;
	option->ltr_idle_latency = LTR_IDLE_LATENCY_DEF;
	option->ltr_l1off_latency = LTR_L1OFF_LATENCY_DEF;
	option->l1_snooze_delay = L1_SNOOZE_DELAY_DEF;
	option->ltr_l1off_sspwrgate = 0x7F;
	option->ltr_l1off_snooze_sspwrgate = 0x78;

	option->ocp_en = 1;
	hw_param->interrupt_en |= SD_OC_INT_EN;
	hw_param->ocp_glitch =  SD_OCP_GLITCH_800U;
	option->sd_800mA_ocp_thd =  RTS5261_LDO1_OCP_THD_1040;
}
