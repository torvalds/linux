// SPDX-License-Identifier: GPL-2.0-or-later
/* Driver for Realtek PCI-Express card reader
 *
 * Copyright(c) 2009-2013 Realtek Semiconductor Corp. All rights reserved.
 *
 * Author:
 *   Ricky Wu <ricky_wu@realtek.com>
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/rtsx_pci.h>

#include "rts5264.h"
#include "rtsx_pcr.h"

static u8 rts5264_get_ic_version(struct rtsx_pcr *pcr)
{
	u8 val;

	rtsx_pci_read_register(pcr, DUMMY_REG_RESET_0, &val);
	return val & 0x0F;
}

static void rts5264_fill_driving(struct rtsx_pcr *pcr, u8 voltage)
{
	u8 driving_3v3[4][3] = {
		{0x88, 0x88, 0x88},
		{0x77, 0x77, 0x77},
		{0x99, 0x99, 0x99},
		{0x66, 0x66, 0x66},
	};
	u8 driving_1v8[4][3] = {
		{0x99, 0x99, 0x99},
		{0x77, 0x77, 0x77},
		{0xBB, 0xBB, 0xBB},
		{0x65, 0x65, 0x65},
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

static void rts5264_force_power_down(struct rtsx_pcr *pcr, u8 pm_state, bool runtime)
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
		rtsx_pci_write_register(pcr, RTS5264_AUTOLOAD_CFG1,
				CD_RESUME_EN_MASK, 0);
		rtsx_pci_write_register(pcr, pcr->reg_pm_ctrl3, 0x01, 0x00);
		rtsx_pci_write_register(pcr, RTS5264_REG_PME_FORCE_CTL,
				FORCE_PM_CONTROL | FORCE_PM_VALUE, FORCE_PM_CONTROL);
	} else {
		rtsx_pci_write_register(pcr, RTS5264_REG_PME_FORCE_CTL,
				FORCE_PM_CONTROL | FORCE_PM_VALUE, 0);
		rtsx_pci_write_register(pcr, pcr->reg_pm_ctrl3, 0x01, 0x01);
		rtsx_pci_write_register(pcr, pcr->reg_pm_ctrl3,
					D3_DELINK_MODE_EN, 0);
		rtsx_pci_write_register(pcr, RTS5264_FW_CTL,
				RTS5264_INFORM_RTD3_COLD, RTS5264_INFORM_RTD3_COLD);
		rtsx_pci_write_register(pcr, RTS5264_AUTOLOAD_CFG4,
				RTS5264_FORCE_PRSNT_LOW, RTS5264_FORCE_PRSNT_LOW);
	}

	rtsx_pci_write_register(pcr, RTS5264_REG_FPDCTL,
		SSC_POWER_DOWN, SSC_POWER_DOWN);
}

static int rts5264_enable_auto_blink(struct rtsx_pcr *pcr)
{
	return rtsx_pci_write_register(pcr, OLT_LED_CTL,
		LED_SHINE_MASK, LED_SHINE_EN);
}

static int rts5264_disable_auto_blink(struct rtsx_pcr *pcr)
{
	return rtsx_pci_write_register(pcr, OLT_LED_CTL,
		LED_SHINE_MASK, LED_SHINE_DISABLE);
}

static int rts5264_turn_on_led(struct rtsx_pcr *pcr)
{
	return rtsx_pci_write_register(pcr, GPIO_CTL,
		0x02, 0x02);
}

static int rts5264_turn_off_led(struct rtsx_pcr *pcr)
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
static const u32 rts5264_sd_pull_ctl_enable_tbl[] = {
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
static const u32 rts5264_sd_pull_ctl_disable_tbl[] = {
	RTSX_REG_PAIR(CARD_PULL_CTL2, 0x55),
	RTSX_REG_PAIR(CARD_PULL_CTL3, 0xD5),
	0,
};

static int rts5264_sd_set_sample_push_timing_sd30(struct rtsx_pcr *pcr)
{
	rtsx_pci_write_register(pcr, SD_CFG1, SD_MODE_SELECT_MASK
		| SD_ASYNC_FIFO_NOT_RST, SD_30_MODE | SD_ASYNC_FIFO_NOT_RST);
	rtsx_pci_write_register(pcr, CLK_CTL, CLK_LOW_FREQ, CLK_LOW_FREQ);
	rtsx_pci_write_register(pcr, CARD_CLK_SOURCE, 0xFF,
			CRC_VAR_CLK0 | SD30_FIX_CLK | SAMPLE_VAR_CLK1);
	rtsx_pci_write_register(pcr, CLK_CTL, CLK_LOW_FREQ, 0);

	return 0;
}

static int rts5264_card_power_on(struct rtsx_pcr *pcr, int card)
{
	struct rtsx_cr_option *option = &pcr->option;

	if (option->ocp_en)
		rtsx_pci_enable_ocp(pcr);

	rtsx_pci_write_register(pcr, REG_CRC_DUMMY_0,
		CFG_SD_POW_AUTO_PD, CFG_SD_POW_AUTO_PD);

	rtsx_pci_write_register(pcr, RTS5264_LDO1_CFG1,
			RTS5264_LDO1_TUNE_MASK, RTS5264_LDO1_33);
	rtsx_pci_write_register(pcr, RTS5264_LDO1233318_POW_CTL,
			RTS5264_LDO1_POWERON, RTS5264_LDO1_POWERON);
	rtsx_pci_write_register(pcr, RTS5264_LDO1233318_POW_CTL,
			RTS5264_LDO3318_POWERON, RTS5264_LDO3318_POWERON);

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
		rts5264_sd_set_sample_push_timing_sd30(pcr);

	return 0;
}

static int rts5264_switch_output_voltage(struct rtsx_pcr *pcr, u8 voltage)
{
	rtsx_pci_write_register(pcr, RTS5264_CARD_PWR_CTL,
			RTS5264_PUPDC, RTS5264_PUPDC);

	switch (voltage) {
	case OUTPUT_3V3:
		rtsx_pci_write_register(pcr, RTS5264_LDO1233318_POW_CTL,
				RTS5264_TUNE_REF_LDO3318, RTS5264_TUNE_REF_LDO3318);
		rtsx_pci_write_register(pcr, RTS5264_DV3318_CFG,
				RTS5264_DV3318_TUNE_MASK, RTS5264_DV3318_33);
		rtsx_pci_write_register(pcr, SD_PAD_CTL,
				SD_IO_USING_1V8, 0);
		break;
	case OUTPUT_1V8:
		rtsx_pci_write_register(pcr, RTS5264_LDO1233318_POW_CTL,
				RTS5264_TUNE_REF_LDO3318, RTS5264_TUNE_REF_LDO3318_DFT);
		rtsx_pci_write_register(pcr, RTS5264_DV3318_CFG,
				RTS5264_DV3318_TUNE_MASK, RTS5264_DV3318_18);
		rtsx_pci_write_register(pcr, SD_PAD_CTL,
				SD_IO_USING_1V8, SD_IO_USING_1V8);
		break;
	default:
		return -EINVAL;
	}

	/* set pad drive */
	rts5264_fill_driving(pcr, voltage);

	return 0;
}

static void rts5264_stop_cmd(struct rtsx_pcr *pcr)
{
	rtsx_pci_writel(pcr, RTSX_HCBCTLR, STOP_CMD);
	rtsx_pci_writel(pcr, RTSX_HDBCTLR, STOP_DMA);
	rtsx_pci_write_register(pcr, DMACTL, DMA_RST, DMA_RST);
	rtsx_pci_write_register(pcr, RBCTL, RB_FLUSH, RB_FLUSH);
}

static void rts5264_card_before_power_off(struct rtsx_pcr *pcr)
{
	rts5264_stop_cmd(pcr);
	rts5264_switch_output_voltage(pcr, OUTPUT_3V3);
}

static int rts5264_card_power_off(struct rtsx_pcr *pcr, int card)
{
	int err = 0;

	rts5264_card_before_power_off(pcr);
	err = rtsx_pci_write_register(pcr, RTS5264_LDO1233318_POW_CTL,
				RTS5264_LDO_POWERON_MASK, 0);

	rtsx_pci_write_register(pcr, REG_CRC_DUMMY_0,
		CFG_SD_POW_AUTO_PD, 0);
	if (pcr->option.ocp_en)
		rtsx_pci_disable_ocp(pcr);

	return err;
}

static void rts5264_enable_ocp(struct rtsx_pcr *pcr)
{
	u8 mask = 0;
	u8 val = 0;

	rtsx_pci_write_register(pcr, RTS5264_LDO1_CFG0,
			RTS5264_LDO1_OCP_EN | RTS5264_LDO1_OCP_LMT_EN,
			RTS5264_LDO1_OCP_EN | RTS5264_LDO1_OCP_LMT_EN);
	rtsx_pci_write_register(pcr, RTS5264_LDO2_CFG0,
			RTS5264_LDO2_OCP_EN | RTS5264_LDO2_OCP_LMT_EN,
			RTS5264_LDO2_OCP_EN | RTS5264_LDO2_OCP_LMT_EN);
	rtsx_pci_write_register(pcr, RTS5264_LDO3_CFG0,
			RTS5264_LDO3_OCP_EN | RTS5264_LDO3_OCP_LMT_EN,
			RTS5264_LDO3_OCP_EN | RTS5264_LDO3_OCP_LMT_EN);
	rtsx_pci_write_register(pcr, RTS5264_OVP_DET,
			RTS5264_POW_VDET, RTS5264_POW_VDET);

	mask = SD_OCP_INT_EN | SD_DETECT_EN;
	mask |= SDVIO_OCP_INT_EN | SDVIO_DETECT_EN;
	val = mask;
	rtsx_pci_write_register(pcr, REG_OCPCTL, mask, val);

	mask = SD_VDD3_OCP_INT_EN | SD_VDD3_DETECT_EN;
	val = mask;
	rtsx_pci_write_register(pcr, RTS5264_OCP_VDD3_CTL, mask, val);

	mask = RTS5264_OVP_INT_EN | RTS5264_OVP_DETECT_EN;
	val = mask;
	rtsx_pci_write_register(pcr, RTS5264_OVP_CTL, mask, val);
}

static void rts5264_disable_ocp(struct rtsx_pcr *pcr)
{
	u8 mask = 0;

	mask = SD_OCP_INT_EN | SD_DETECT_EN;
	mask |= SDVIO_OCP_INT_EN | SDVIO_DETECT_EN;
	rtsx_pci_write_register(pcr, REG_OCPCTL, mask, 0);

	mask = SD_VDD3_OCP_INT_EN | SD_VDD3_DETECT_EN;
	rtsx_pci_write_register(pcr, RTS5264_OCP_VDD3_CTL, mask, 0);

	mask = RTS5264_OVP_INT_EN | RTS5264_OVP_DETECT_EN;
	rtsx_pci_write_register(pcr, RTS5264_OVP_CTL, mask, 0);

	rtsx_pci_write_register(pcr, RTS5264_LDO1_CFG0,
			RTS5264_LDO1_OCP_EN | RTS5264_LDO1_OCP_LMT_EN, 0);
	rtsx_pci_write_register(pcr, RTS5264_LDO2_CFG0,
			RTS5264_LDO2_OCP_EN | RTS5264_LDO2_OCP_LMT_EN, 0);
	rtsx_pci_write_register(pcr, RTS5264_LDO3_CFG0,
			RTS5264_LDO3_OCP_EN | RTS5264_LDO3_OCP_LMT_EN, 0);
	rtsx_pci_write_register(pcr, RTS5264_OVP_DET, RTS5264_POW_VDET, 0);
}

static void rts5264_init_ocp(struct rtsx_pcr *pcr)
{
	struct rtsx_cr_option *option = &pcr->option;

	if (option->ocp_en) {
		u8 mask, val;

		rtsx_pci_write_register(pcr, RTS5264_LDO1_CFG0,
			RTS5264_LDO1_OCP_THD_MASK, option->sd_800mA_ocp_thd);
		rtsx_pci_write_register(pcr, RTS5264_LDO1_CFG0,
			RTS5264_LDO1_OCP_LMT_THD_MASK,
			RTS5264_LDO1_LMT_THD_2000);

		rtsx_pci_write_register(pcr, RTS5264_LDO2_CFG0,
			RTS5264_LDO2_OCP_THD_MASK, RTS5264_LDO2_OCP_THD_950);
		rtsx_pci_write_register(pcr, RTS5264_LDO2_CFG0,
			RTS5264_LDO2_OCP_LMT_THD_MASK,
			RTS5264_LDO2_LMT_THD_2000);

		rtsx_pci_write_register(pcr, RTS5264_LDO3_CFG0,
			RTS5264_LDO3_OCP_THD_MASK, RTS5264_LDO3_OCP_THD_710);
		rtsx_pci_write_register(pcr, RTS5264_LDO3_CFG0,
			RTS5264_LDO3_OCP_LMT_THD_MASK,
			RTS5264_LDO3_LMT_THD_1500);

		rtsx_pci_write_register(pcr, RTS5264_OVP_DET,
			RTS5264_TUNE_VROV_MASK, RTS5264_TUNE_VROV_1V6);

		mask = SD_OCP_GLITCH_MASK | SDVIO_OCP_GLITCH_MASK;
		val = pcr->hw_param.ocp_glitch;
		rtsx_pci_write_register(pcr, REG_OCPGLITCH, mask, val);

	} else {
		rtsx_pci_write_register(pcr, RTS5264_LDO1_CFG0,
			RTS5264_LDO1_OCP_EN | RTS5264_LDO1_OCP_LMT_EN, 0);
		rtsx_pci_write_register(pcr, RTS5264_LDO2_CFG0,
			RTS5264_LDO2_OCP_EN | RTS5264_LDO2_OCP_LMT_EN, 0);
		rtsx_pci_write_register(pcr, RTS5264_LDO3_CFG0,
			RTS5264_LDO3_OCP_EN | RTS5264_LDO3_OCP_LMT_EN, 0);
		rtsx_pci_write_register(pcr, RTS5264_OVP_DET,
			RTS5264_POW_VDET, 0);
	}
}

static int rts5264_get_ocpstat2(struct rtsx_pcr *pcr, u8 *val)
{
	return rtsx_pci_read_register(pcr, RTS5264_OCP_VDD3_STS, val);
}

static int rts5264_get_ovpstat(struct rtsx_pcr *pcr, u8 *val)
{
	return rtsx_pci_read_register(pcr, RTS5264_OVP_STS, val);
}

static void rts5264_clear_ocpstat(struct rtsx_pcr *pcr)
{
	u8 mask = 0;
	u8 val = 0;

	mask = SD_OCP_INT_CLR | SD_OC_CLR;
	mask |= SDVIO_OCP_INT_CLR | SDVIO_OC_CLR;
	val = mask;
	rtsx_pci_write_register(pcr, REG_OCPCTL, mask, val);
	rtsx_pci_write_register(pcr, RTS5264_OCP_VDD3_CTL,
		SD_VDD3_OCP_INT_CLR | SD_VDD3_OC_CLR,
		SD_VDD3_OCP_INT_CLR | SD_VDD3_OC_CLR);
	rtsx_pci_write_register(pcr, RTS5264_OVP_CTL,
		RTS5264_OVP_INT_CLR | RTS5264_OVP_CLR,
		RTS5264_OVP_INT_CLR | RTS5264_OVP_CLR);

	udelay(1000);

	rtsx_pci_write_register(pcr, REG_OCPCTL, mask, 0);
	rtsx_pci_write_register(pcr, RTS5264_OCP_VDD3_CTL,
		SD_VDD3_OCP_INT_CLR | SD_VDD3_OC_CLR, 0);
	rtsx_pci_write_register(pcr, RTS5264_OVP_CTL,
		RTS5264_OVP_INT_CLR | RTS5264_OVP_CLR, 0);
}

static void rts5264_process_ocp(struct rtsx_pcr *pcr)
{
	if (!pcr->option.ocp_en)
		return;

	rtsx_pci_get_ocpstat(pcr, &pcr->ocp_stat);
	rts5264_get_ocpstat2(pcr, &pcr->ocp_stat2);
	rts5264_get_ovpstat(pcr, &pcr->ovp_stat);

	if ((pcr->ocp_stat & (SD_OC_NOW | SD_OC_EVER | SDVIO_OC_NOW | SDVIO_OC_EVER)) ||
			(pcr->ocp_stat2 & (SD_VDD3_OC_NOW | SD_VDD3_OC_EVER)) ||
			(pcr->ovp_stat & (RTS5264_OVP_NOW | RTS5264_OVP_EVER))) {
		rts5264_clear_ocpstat(pcr);
		rts5264_card_power_off(pcr, RTSX_SD_CARD);
		rtsx_pci_write_register(pcr, CARD_OE, SD_OUTPUT_EN, 0);
		pcr->ocp_stat = 0;
		pcr->ocp_stat2 = 0;
		pcr->ovp_stat = 0;
	}
}

static void rts5264_init_from_hw(struct rtsx_pcr *pcr)
{
	struct pci_dev *pdev = pcr->pci;
	u32 lval1, lval2, i;
	u16 setting_reg1, setting_reg2;
	u8 valid, efuse_valid, tmp;

	rtsx_pci_write_register(pcr, RTS5264_REG_PME_FORCE_CTL,
		REG_EFUSE_POR | REG_EFUSE_POWER_MASK,
		REG_EFUSE_POR | REG_EFUSE_POWERON);
	udelay(1);
	rtsx_pci_write_register(pcr, RTS5264_EFUSE_ADDR,
		RTS5264_EFUSE_ADDR_MASK, 0x00);
	rtsx_pci_write_register(pcr, RTS5264_EFUSE_CTL,
		RTS5264_EFUSE_ENABLE | RTS5264_EFUSE_MODE_MASK,
		RTS5264_EFUSE_ENABLE);

	/* Wait transfer end */
	for (i = 0; i < MAX_RW_REG_CNT; i++) {
		rtsx_pci_read_register(pcr, RTS5264_EFUSE_CTL, &tmp);
		if ((tmp & 0x80) == 0)
			break;
	}
	rtsx_pci_read_register(pcr, RTS5264_EFUSE_READ_DATA, &tmp);
	efuse_valid = ((tmp & 0x0C) >> 2);
	pcr_dbg(pcr, "Load efuse valid: 0x%x\n", efuse_valid);

	pci_read_config_dword(pdev, PCR_SETTING_REG2, &lval2);
	pcr_dbg(pcr, "Cfg 0x%x: 0x%x\n", PCR_SETTING_REG2, lval2);
	/* 0x816 */
	valid = (u8)((lval2 >> 16) & 0x03);

	rtsx_pci_write_register(pcr, RTS5264_REG_PME_FORCE_CTL,
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

	if (!rts5264_vendor_setting_valid(lval2)) {
		pcr_dbg(pcr, "skip fetch vendor setting\n");
		return;
	}

	pcr->rtd3_en = rts5264_reg_to_rtd3(lval2);

	if (rts5264_reg_check_reverse_socket(lval2))
		pcr->flags |= PCR_REVERSE_SOCKET;

	pci_read_config_dword(pdev, setting_reg1, &lval1);
	pcr_dbg(pcr, "Cfg 0x%x: 0x%x\n", setting_reg1, lval1);

	pcr->aspm_en = rts5264_reg_to_aspm(lval1);
	pcr->sd30_drive_sel_1v8 = rts5264_reg_to_sd30_drive_sel_1v8(lval1);
	pcr->sd30_drive_sel_3v3 = rts5264_reg_to_sd30_drive_sel_3v3(lval1);

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

static void rts5264_init_from_cfg(struct rtsx_pcr *pcr)
{
	struct rtsx_cr_option *option = &pcr->option;

	if (rtsx_check_dev_flag(pcr, ASPM_L1_1_EN | ASPM_L1_2_EN
				| PM_L1_1_EN | PM_L1_2_EN))
		rtsx_pci_disable_oobs_polling(pcr);
	else
		rtsx_pci_enable_oobs_polling(pcr);

	rtsx_pci_write_register(pcr, ASPM_FORCE_CTL, 0xFF, 0);

	if (option->ltr_en) {
		if (option->ltr_enabled)
			rtsx_set_ltr_latency(pcr, option->ltr_active_latency);
	}
}

static int rts5264_extra_init_hw(struct rtsx_pcr *pcr)
{
	struct rtsx_cr_option *option = &pcr->option;

	rtsx_pci_write_register(pcr, RTS5264_AUTOLOAD_CFG1,
			CD_RESUME_EN_MASK, CD_RESUME_EN_MASK);
	rtsx_pci_write_register(pcr, REG_VREF, PWD_SUSPND_EN, PWD_SUSPND_EN);

	rts5264_init_from_cfg(pcr);
	rts5264_init_from_hw(pcr);

	/* power off efuse */
	rtsx_pci_write_register(pcr, RTS5264_REG_PME_FORCE_CTL,
			REG_EFUSE_POWER_MASK, REG_EFUSE_POWEROFF);
	rtsx_pci_write_register(pcr, RTS5264_AUTOLOAD_CFG2,
			RTS5264_CHIP_RST_N_SEL, 0);
	rtsx_pci_write_register(pcr, RTS5264_REG_LDO12_CFG,
			RTS5264_LDO12_SR_MASK, RTS5264_LDO12_SR_0_0_MS);
	rtsx_pci_write_register(pcr, CDGW, 0xFF, 0x01);
	rtsx_pci_write_register(pcr, RTS5264_CKMUX_MBIAS_PWR,
			RTS5264_POW_CKMUX, RTS5264_POW_CKMUX);
	rtsx_pci_write_register(pcr, RTS5264_CMD_OE_START_EARLY,
			RTS5264_CMD_OE_EARLY_EN | RTS5264_CMD_OE_EARLY_CYCLE_MASK,
			RTS5264_CMD_OE_EARLY_EN);
	rtsx_pci_write_register(pcr, RTS5264_DAT_OE_START_EARLY,
			RTS5264_DAT_OE_EARLY_EN | RTS5264_DAT_OE_EARLY_CYCLE_MASK,
			RTS5264_DAT_OE_EARLY_EN);
	rtsx_pci_write_register(pcr, SSC_DIV_N_0, 0xFF, 0x5D);

	rtsx_pci_write_register(pcr, RTS5264_PWR_CUT,
			RTS5264_CFG_MEM_PD, RTS5264_CFG_MEM_PD);
	rtsx_pci_write_register(pcr, L1SUB_CONFIG1,
			AUX_CLK_ACTIVE_SEL_MASK, MAC_CKSW_DONE);
	rtsx_pci_write_register(pcr, L1SUB_CONFIG3, 0xFF, 0);
	rtsx_pci_write_register(pcr, RTS5264_AUTOLOAD_CFG4,
			RTS5264_AUX_CLK_16M_EN, 0);

	/* Release PRSNT# */
	rtsx_pci_write_register(pcr, RTS5264_AUTOLOAD_CFG4,
			RTS5264_FORCE_PRSNT_LOW, 0);
	rtsx_pci_write_register(pcr, PCLK_CTL,
			PCLK_MODE_SEL, PCLK_MODE_SEL);

	/* LED shine disabled, set initial shine cycle period */
	rtsx_pci_write_register(pcr, OLT_LED_CTL, 0x0F, 0x02);

	/* Configure driving */
	rts5264_fill_driving(pcr, OUTPUT_3V3);

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

	rtsx_pci_write_register(pcr, PWD_SUSPEND_EN, 0xFF, 0xFF);
	rtsx_pci_write_register(pcr, RBCTL, U_AUTO_DMA_EN_MASK, 0);
	rtsx_pci_write_register(pcr, RTS5264_AUTOLOAD_CFG4,
			RTS5264_F_HIGH_RC_MASK, RTS5264_F_HIGH_RC_400K);

	if (pcr->rtd3_en) {
		rtsx_pci_write_register(pcr, pcr->reg_pm_ctrl3, 0x01, 0x00);
		rtsx_pci_write_register(pcr, RTS5264_REG_PME_FORCE_CTL,
				FORCE_PM_CONTROL | FORCE_PM_VALUE, 0);
	} else {
		rtsx_pci_write_register(pcr, pcr->reg_pm_ctrl3, 0x01, 0x00);
		rtsx_pci_write_register(pcr, RTS5264_REG_PME_FORCE_CTL,
				FORCE_PM_CONTROL | FORCE_PM_VALUE, FORCE_PM_CONTROL);
	}
	rtsx_pci_write_register(pcr, pcr->reg_pm_ctrl3, D3_DELINK_MODE_EN, 0x00);

	/* Clear Enter RTD3_cold Information*/
	rtsx_pci_write_register(pcr, RTS5264_FW_CTL,
		RTS5264_INFORM_RTD3_COLD, 0);

	return 0;
}

static void rts5264_enable_aspm(struct rtsx_pcr *pcr, bool enable)
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

static void rts5264_disable_aspm(struct rtsx_pcr *pcr, bool enable)
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

static void rts5264_set_aspm(struct rtsx_pcr *pcr, bool enable)
{
	if (enable)
		rts5264_enable_aspm(pcr, true);
	else
		rts5264_disable_aspm(pcr, false);
}

static void rts5264_set_l1off_cfg_sub_d0(struct rtsx_pcr *pcr, int active)
{
	struct rtsx_cr_option *option = &(pcr->option);

	u32 interrupt = rtsx_pci_readl(pcr, RTSX_BIPR);
	int card_exist = (interrupt & SD_EXIST);
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

static const struct pcr_ops rts5264_pcr_ops = {
	.turn_on_led = rts5264_turn_on_led,
	.turn_off_led = rts5264_turn_off_led,
	.extra_init_hw = rts5264_extra_init_hw,
	.enable_auto_blink = rts5264_enable_auto_blink,
	.disable_auto_blink = rts5264_disable_auto_blink,
	.card_power_on = rts5264_card_power_on,
	.card_power_off = rts5264_card_power_off,
	.switch_output_voltage = rts5264_switch_output_voltage,
	.force_power_down = rts5264_force_power_down,
	.stop_cmd = rts5264_stop_cmd,
	.set_aspm = rts5264_set_aspm,
	.set_l1off_cfg_sub_d0 = rts5264_set_l1off_cfg_sub_d0,
	.enable_ocp = rts5264_enable_ocp,
	.disable_ocp = rts5264_disable_ocp,
	.init_ocp = rts5264_init_ocp,
	.process_ocp = rts5264_process_ocp,
	.clear_ocpstat = rts5264_clear_ocpstat,
};

static inline u8 double_ssc_depth(u8 depth)
{
	return ((depth > 1) ? (depth - 1) : depth);
}

int rts5264_pci_switch_clock(struct rtsx_pcr *pcr, unsigned int card_clock,
		u8 ssc_depth, bool initial_mode, bool double_clk, bool vpclk)
{
	int err, clk;
	u16 n;
	u8 clk_divider, mcu_cnt, div;
	static const u8 depth[] = {
		[RTSX_SSC_DEPTH_4M] = RTS5264_SSC_DEPTH_4M,
		[RTSX_SSC_DEPTH_2M] = RTS5264_SSC_DEPTH_2M,
		[RTSX_SSC_DEPTH_1M] = RTS5264_SSC_DEPTH_1M,
		[RTSX_SSC_DEPTH_500K] = RTS5264_SSC_DEPTH_512K,
	};

	if (initial_mode) {
		/* We use 250k(around) here, in initial stage */
		clk_divider = SD_CLK_DIVIDE_128;
		card_clock = 30000000;
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
				ssc_depth = RTS5264_SSC_DEPTH_8M;
		} else if (div == CLK_DIV_4) {
			if (ssc_depth > 2)
				ssc_depth -= 2;
			else
				ssc_depth = RTS5264_SSC_DEPTH_8M;
		} else if (div == CLK_DIV_8) {
			if (ssc_depth > 3)
				ssc_depth -= 3;
			else
				ssc_depth = RTS5264_SSC_DEPTH_8M;
		}
	} else {
		ssc_depth = 0;
	}
	pcr_dbg(pcr, "ssc_depth = %d\n", ssc_depth);

	rtsx_pci_init_cmd(pcr);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CLK_CTL,
				CHANGE_CLK, CHANGE_CLK);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, CLK_DIV,
			0xFF, (div << 4) | mcu_cnt);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SSC_CTL1, SSC_RSTB, 0);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SSC_CTL2,
			SSC_DEPTH_MASK, ssc_depth);
	rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SSC_DIV_N_0, 0xFF, n);

	if (is_version(pcr, 0x5264, IC_VER_A)) {
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SSC_CTL1, SSC_RSTB, 0);
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, RTS5264_CARD_CLK_SRC2,
			RTS5264_REG_BIG_KVCO_A, 0);
	} else {
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, SSC_CTL1, SSC_RSTB, SSC_RSTB);
		rtsx_pci_add_cmd(pcr, WRITE_REG_CMD, RTS5264_SYS_DUMMY_1,
			RTS5264_REG_BIG_KVCO, 0);
	}

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
	err = rtsx_pci_write_register(pcr, CLK_CTL, CHANGE_CLK, 0);
	if (err < 0)
		return err;

	pcr->cur_clock = clk;
	return 0;
}

void rts5264_init_params(struct rtsx_pcr *pcr)
{
	struct rtsx_cr_option *option = &pcr->option;
	struct rtsx_hw_param *hw_param = &pcr->hw_param;
	u8 val;

	pcr->extra_caps = EXTRA_CAPS_SD_SDR50 | EXTRA_CAPS_SD_SDR104;
	pcr->extra_caps |= EXTRA_CAPS_NO_MMC;
	rtsx_pci_read_register(pcr, RTS5264_FW_STATUS, &val);
	if (!(val & RTS5264_EXPRESS_LINK_FAIL_MASK))
		pcr->extra_caps |= EXTRA_CAPS_SD_EXPRESS;
	pcr->num_slots = 1;
	pcr->ops = &rts5264_pcr_ops;

	pcr->flags = 0;
	pcr->card_drive_sel = RTSX_CARD_DRIVE_DEFAULT;
	pcr->sd30_drive_sel_1v8 = 0x00;
	pcr->sd30_drive_sel_3v3 = 0x00;
	pcr->aspm_en = ASPM_L1_EN;
	pcr->aspm_mode = ASPM_MODE_REG;
	pcr->tx_initial_phase = SET_CLOCK_PHASE(24, 24, 11);
	pcr->rx_initial_phase = SET_CLOCK_PHASE(24, 6, 5);

	pcr->ic_version = rts5264_get_ic_version(pcr);
	pcr->sd_pull_ctl_enable_tbl = rts5264_sd_pull_ctl_enable_tbl;
	pcr->sd_pull_ctl_disable_tbl = rts5264_sd_pull_ctl_disable_tbl;

	pcr->reg_pm_ctrl3 = RTS5264_AUTOLOAD_CFG3;

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
	hw_param->interrupt_en |= (SD_OC_INT_EN | SD_OVP_INT_EN);
	hw_param->ocp_glitch =  SD_OCP_GLITCH_800U | SDVIO_OCP_GLITCH_800U;
	option->sd_800mA_ocp_thd =  RTS5264_LDO1_OCP_THD_1150;
}
