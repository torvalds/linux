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
 *   Wei WANG (wei_wang@realsil.com.cn)
 *   Micky Ching (micky_ching@realsil.com.cn)
 */

#include <linux/blkdev.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/vmalloc.h>

#include "rtsx.h"
#include "rtsx_transport.h"
#include "rtsx_scsi.h"
#include "rtsx_card.h"
#include "rtsx_chip.h"
#include "rtsx_sys.h"
#include "general.h"

#include "sd.h"
#include "xd.h"
#include "ms.h"

static void rtsx_calibration(struct rtsx_chip *chip)
{
	rtsx_write_phy_register(chip, 0x1B, 0x135E);
	wait_timeout(10);
	rtsx_write_phy_register(chip, 0x00, 0x0280);
	rtsx_write_phy_register(chip, 0x01, 0x7112);
	rtsx_write_phy_register(chip, 0x01, 0x7110);
	rtsx_write_phy_register(chip, 0x01, 0x7112);
	rtsx_write_phy_register(chip, 0x01, 0x7113);
	rtsx_write_phy_register(chip, 0x00, 0x0288);
}

void rtsx_disable_card_int(struct rtsx_chip *chip)
{
	u32 reg = rtsx_readl(chip, RTSX_BIER);

	reg &= ~(XD_INT_EN | SD_INT_EN | MS_INT_EN);
	rtsx_writel(chip, RTSX_BIER, reg);
}

void rtsx_enable_card_int(struct rtsx_chip *chip)
{
	u32 reg = rtsx_readl(chip, RTSX_BIER);
	int i;

	for (i = 0; i <= chip->max_lun; i++) {
		if (chip->lun2card[i] & XD_CARD)
			reg |= XD_INT_EN;
		if (chip->lun2card[i] & SD_CARD)
			reg |= SD_INT_EN;
		if (chip->lun2card[i] & MS_CARD)
			reg |= MS_INT_EN;
	}
	if (chip->hw_bypass_sd)
		reg &= ~((u32)SD_INT_EN);

	rtsx_writel(chip, RTSX_BIER, reg);
}

void rtsx_enable_bus_int(struct rtsx_chip *chip)
{
	u32 reg = 0;
#ifndef DISABLE_CARD_INT
	int i;
#endif

	reg = TRANS_OK_INT_EN | TRANS_FAIL_INT_EN;

#ifndef DISABLE_CARD_INT
	for (i = 0; i <= chip->max_lun; i++) {
		RTSX_DEBUGP("lun2card[%d] = 0x%02x\n", i, chip->lun2card[i]);

		if (chip->lun2card[i] & XD_CARD)
			reg |= XD_INT_EN;
		if (chip->lun2card[i] & SD_CARD)
			reg |= SD_INT_EN;
		if (chip->lun2card[i] & MS_CARD)
			reg |= MS_INT_EN;
	}
	if (chip->hw_bypass_sd)
		reg &= ~((u32)SD_INT_EN);
#endif

	if (chip->ic_version >= IC_VER_C)
		reg |= DELINK_INT_EN;
#ifdef SUPPORT_OCP
		reg |= OC_INT_EN;
#endif
	if (!chip->adma_mode)
		reg |= DATA_DONE_INT_EN;

	/* Enable Bus Interrupt */
	rtsx_writel(chip, RTSX_BIER, reg);

	RTSX_DEBUGP("RTSX_BIER: 0x%08x\n", reg);
}

void rtsx_disable_bus_int(struct rtsx_chip *chip)
{
	rtsx_writel(chip, RTSX_BIER, 0);
}

static int rtsx_pre_handle_sdio_old(struct rtsx_chip *chip)
{
	if (chip->ignore_sd && CHK_SDIO_EXIST(chip)) {
		if (chip->asic_code) {
			RTSX_WRITE_REG(chip, CARD_PULL_CTL5, 0xFF,
				MS_INS_PU | SD_WP_PU | SD_CD_PU | SD_CMD_PU);
		} else {
			RTSX_WRITE_REG(chip, FPGA_PULL_CTL, 0xFF,
				FPGA_SD_PULL_CTL_EN);
		}
		RTSX_WRITE_REG(chip, CARD_SHARE_MODE, 0xFF, CARD_SHARE_48_SD);

		/* Enable SDIO internal clock */
		RTSX_WRITE_REG(chip, 0xFF2C, 0x01, 0x01);

		RTSX_WRITE_REG(chip, SDIO_CTRL, 0xFF,
			SDIO_BUS_CTRL | SDIO_CD_CTRL);

		chip->sd_int = 1;
		chip->sd_io = 1;
	} else {
		chip->need_reset |= SD_CARD;
	}

	return STATUS_SUCCESS;
}

#ifdef HW_AUTO_SWITCH_SD_BUS
static int rtsx_pre_handle_sdio_new(struct rtsx_chip *chip)
{
	u8 tmp;
	int sw_bypass_sd = 0;
	int retval;

	if (chip->driver_first_load) {
		if (CHECK_PID(chip, 0x5288)) {
			RTSX_READ_REG(chip, 0xFE5A, &tmp);
			if (tmp & 0x08)
				sw_bypass_sd = 1;
		} else if (CHECK_PID(chip, 0x5208)) {
			RTSX_READ_REG(chip, 0xFE70, &tmp);
			if (tmp & 0x80)
				sw_bypass_sd = 1;
		}
	} else {
		if (chip->sdio_in_charge)
			sw_bypass_sd = 1;
	}
	RTSX_DEBUGP("chip->sdio_in_charge = %d\n", chip->sdio_in_charge);
	RTSX_DEBUGP("chip->driver_first_load = %d\n", chip->driver_first_load);
	RTSX_DEBUGP("sw_bypass_sd = %d\n", sw_bypass_sd);

	if (sw_bypass_sd) {
		u8 cd_toggle_mask = 0;

		RTSX_READ_REG(chip, TLPTISTAT, &tmp);
		cd_toggle_mask = 0x08;

		if (tmp & cd_toggle_mask) {
			/* Disable sdio_bus_auto_switch */
			if (CHECK_PID(chip, 0x5288))
				RTSX_WRITE_REG(chip, 0xFE5A, 0x08, 0x00);
			else if (CHECK_PID(chip, 0x5208))
				RTSX_WRITE_REG(chip, 0xFE70, 0x80, 0x00);

			RTSX_WRITE_REG(chip, TLPTISTAT, 0xFF, tmp);

			chip->need_reset |= SD_CARD;
		} else {
			RTSX_DEBUGP("Chip inserted with SDIO!\n");

			if (chip->asic_code) {
				retval = sd_pull_ctl_enable(chip);
				if (retval != STATUS_SUCCESS)
					TRACE_RET(chip, STATUS_FAIL);
			} else {
				RTSX_WRITE_REG(chip, FPGA_PULL_CTL,
					FPGA_SD_PULL_CTL_BIT | 0x20, 0);
			}
			retval = card_share_mode(chip, SD_CARD);
			if (retval != STATUS_SUCCESS)
				TRACE_RET(chip, STATUS_FAIL);

			/* Enable sdio_bus_auto_switch */
			if (CHECK_PID(chip, 0x5288))
				RTSX_WRITE_REG(chip, 0xFE5A, 0x08, 0x08);
			else if (CHECK_PID(chip, 0x5208))
				RTSX_WRITE_REG(chip, 0xFE70, 0x80, 0x80);

			chip->chip_insert_with_sdio = 1;
			chip->sd_io = 1;
		}
	} else {
		RTSX_WRITE_REG(chip, TLPTISTAT, 0x08, 0x08);

		chip->need_reset |= SD_CARD;
	}

	return STATUS_SUCCESS;
}
#endif

int rtsx_reset_chip(struct rtsx_chip *chip)
{
	int retval;

	rtsx_writel(chip, RTSX_HCBAR, chip->host_cmds_addr);

	rtsx_disable_aspm(chip);

	RTSX_WRITE_REG(chip, HOST_SLEEP_STATE, 0x03, 0x00);

	/* Disable card clock */
	RTSX_WRITE_REG(chip, CARD_CLK_EN, 0x1E, 0);

#ifdef SUPPORT_OCP
	/* SSC power on, OCD power on */
	if (CHECK_LUN_MODE(chip, SD_MS_2LUN))
		RTSX_WRITE_REG(chip, FPDCTL, OC_POWER_DOWN, 0);
	else
		RTSX_WRITE_REG(chip, FPDCTL, OC_POWER_DOWN, MS_OC_POWER_DOWN);

	RTSX_WRITE_REG(chip, OCPPARA1, OCP_TIME_MASK, OCP_TIME_800);
	RTSX_WRITE_REG(chip, OCPPARA2, OCP_THD_MASK, OCP_THD_244_946);
	RTSX_WRITE_REG(chip, OCPCTL, 0xFF, CARD_OC_INT_EN | CARD_DETECT_EN);
#else
	/* OC power down */
	RTSX_WRITE_REG(chip, FPDCTL, OC_POWER_DOWN, OC_POWER_DOWN);
#endif

	if (!CHECK_PID(chip, 0x5288))
		RTSX_WRITE_REG(chip, CARD_GPIO_DIR, 0xFF, 0x03);

	/* Turn off LED */
	RTSX_WRITE_REG(chip, CARD_GPIO, 0xFF, 0x03);

	/* Reset delink mode */
	RTSX_WRITE_REG(chip, CHANGE_LINK_STATE, 0x0A, 0);

	/* Card driving select */
	RTSX_WRITE_REG(chip, CARD_DRIVE_SEL, 0xFF, chip->card_drive_sel);

#ifdef LED_AUTO_BLINK
	RTSX_WRITE_REG(chip, CARD_AUTO_BLINK, 0xFF,
			LED_BLINK_SPEED | BLINK_EN | LED_GPIO0);
#endif

	if (chip->asic_code) {
		/* Enable SSC Clock */
		RTSX_WRITE_REG(chip, SSC_CTL1, 0xFF, SSC_8X_EN | SSC_SEL_4M);
		RTSX_WRITE_REG(chip, SSC_CTL2, 0xFF, 0x12);
	}

	/* Disable cd_pwr_save (u_force_rst_core_en=0, u_cd_rst_core_en=0)
	      0xFE5B
	      bit[1]    u_cd_rst_core_en	rst_value = 0
	      bit[2]    u_force_rst_core_en	rst_value = 0
	      bit[5]    u_mac_phy_rst_n_dbg	rst_value = 1
	      bit[4]	u_non_sticky_rst_n_dbg	rst_value = 0
	*/
	RTSX_WRITE_REG(chip, CHANGE_LINK_STATE, 0x16, 0x10);

	/* Enable ASPM */
	if (chip->aspm_l0s_l1_en) {
		if (chip->dynamic_aspm) {
			if (CHK_SDIO_EXIST(chip)) {
				if (CHECK_PID(chip, 0x5288)) {
					retval = rtsx_write_cfg_dw(chip, 2, 0xC0, 0xFF, chip->aspm_l0s_l1_en);
					if (retval != STATUS_SUCCESS)
						TRACE_RET(chip, STATUS_FAIL);
				}
			}
		} else {
			if (CHECK_PID(chip, 0x5208))
				RTSX_WRITE_REG(chip, ASPM_FORCE_CTL,
					0xFF, 0x3F);

			retval = rtsx_write_config_byte(chip, LCTLR,
							chip->aspm_l0s_l1_en);
			if (retval != STATUS_SUCCESS)
				TRACE_RET(chip, STATUS_FAIL);

			chip->aspm_level[0] = chip->aspm_l0s_l1_en;
			if (CHK_SDIO_EXIST(chip)) {
				chip->aspm_level[1] = chip->aspm_l0s_l1_en;
				if (CHECK_PID(chip, 0x5288))
					retval = rtsx_write_cfg_dw(chip, 2, 0xC0, 0xFF, chip->aspm_l0s_l1_en);
				else
					retval = rtsx_write_cfg_dw(chip, 1, 0xC0, 0xFF, chip->aspm_l0s_l1_en);

				if (retval != STATUS_SUCCESS)
					TRACE_RET(chip, STATUS_FAIL);

			}

			chip->aspm_enabled = 1;
		}
	} else {
		if (chip->asic_code && CHECK_PID(chip, 0x5208)) {
			retval = rtsx_write_phy_register(chip, 0x07, 0x0129);
			if (retval != STATUS_SUCCESS)
				TRACE_RET(chip, STATUS_FAIL);
		}
		retval = rtsx_write_config_byte(chip, LCTLR,
						chip->aspm_l0s_l1_en);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, STATUS_FAIL);
	}

	retval = rtsx_write_config_byte(chip, 0x81, 1);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, STATUS_FAIL);

	if (CHK_SDIO_EXIST(chip)) {
		if (CHECK_PID(chip, 0x5288))
			retval = rtsx_write_cfg_dw(chip, 2, 0xC0,
						0xFF00, 0x0100);
		else
			retval = rtsx_write_cfg_dw(chip, 1, 0xC0,
						0xFF00, 0x0100);

		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, STATUS_FAIL);

	}

	if (CHECK_PID(chip, 0x5288)) {
		if (!CHK_SDIO_EXIST(chip)) {
			retval = rtsx_write_cfg_dw(chip, 2, 0xC0, 0xFFFF,
						0x0103);
			if (retval != STATUS_SUCCESS)
				TRACE_RET(chip, STATUS_FAIL);

			retval = rtsx_write_cfg_dw(chip, 2, 0x84, 0xFF, 0x03);
			if (retval != STATUS_SUCCESS)
				TRACE_RET(chip, STATUS_FAIL);

		}
	}

	RTSX_WRITE_REG(chip, IRQSTAT0, LINK_RDY_INT, LINK_RDY_INT);

	RTSX_WRITE_REG(chip, PERST_GLITCH_WIDTH, 0xFF, 0x80);

	/* Enable PCIE interrupt */
	if (chip->asic_code) {
		if (CHECK_PID(chip, 0x5208)) {
			if (chip->phy_debug_mode) {
				RTSX_WRITE_REG(chip, CDRESUMECTL, 0x77, 0);
				rtsx_disable_bus_int(chip);
			} else {
				rtsx_enable_bus_int(chip);
			}

			if (chip->ic_version >= IC_VER_D) {
				u16 reg;
				retval = rtsx_read_phy_register(chip, 0x00,
								&reg);
				if (retval != STATUS_SUCCESS)
					TRACE_RET(chip, STATUS_FAIL);

				reg &= 0xFE7F;
				reg |= 0x80;
				retval = rtsx_write_phy_register(chip, 0x00,
								reg);
				if (retval != STATUS_SUCCESS)
					TRACE_RET(chip, STATUS_FAIL);

				retval = rtsx_read_phy_register(chip, 0x1C,
								&reg);
				if (retval != STATUS_SUCCESS)
					TRACE_RET(chip, STATUS_FAIL);

				reg &= 0xFFF7;
				retval = rtsx_write_phy_register(chip, 0x1C,
								reg);
				if (retval != STATUS_SUCCESS)
					TRACE_RET(chip, STATUS_FAIL);

			}

			if (chip->driver_first_load &&
				(chip->ic_version < IC_VER_C))
				rtsx_calibration(chip);

		} else {
			rtsx_enable_bus_int(chip);
		}
	} else {
		rtsx_enable_bus_int(chip);
	}

	chip->need_reset = 0;

	chip->int_reg = rtsx_readl(chip, RTSX_BIPR);

	if (chip->hw_bypass_sd)
		goto NextCard;
	RTSX_DEBUGP("In rtsx_reset_chip, chip->int_reg = 0x%x\n",
		chip->int_reg);
	if (chip->int_reg & SD_EXIST) {
#ifdef HW_AUTO_SWITCH_SD_BUS
		if (CHECK_PID(chip, 0x5208) && (chip->ic_version < IC_VER_C))
			retval = rtsx_pre_handle_sdio_old(chip);
		else
			retval = rtsx_pre_handle_sdio_new(chip);

		RTSX_DEBUGP("chip->need_reset = 0x%x (rtsx_reset_chip)\n",
			(unsigned int)(chip->need_reset));
#else  /* HW_AUTO_SWITCH_SD_BUS */
		retval = rtsx_pre_handle_sdio_old(chip);
#endif  /* HW_AUTO_SWITCH_SD_BUS */
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, STATUS_FAIL);

	} else {
		chip->sd_io = 0;
		RTSX_WRITE_REG(chip, SDIO_CTRL, SDIO_BUS_CTRL | SDIO_CD_CTRL,
			0);
	}

NextCard:
	if (chip->int_reg & XD_EXIST)
		chip->need_reset |= XD_CARD;
	if (chip->int_reg & MS_EXIST)
		chip->need_reset |= MS_CARD;
	if (chip->int_reg & CARD_EXIST)
		RTSX_WRITE_REG(chip, SSC_CTL1, SSC_RSTB, SSC_RSTB);

	RTSX_DEBUGP("In rtsx_init_chip, chip->need_reset = 0x%x\n",
		(unsigned int)(chip->need_reset));

	RTSX_WRITE_REG(chip, RCCTL, 0x01, 0x00);

	if (CHECK_PID(chip, 0x5208) || CHECK_PID(chip, 0x5288)) {
		/* Turn off main power when entering S3/S4 state */
		RTSX_WRITE_REG(chip, MAIN_PWR_OFF_CTL, 0x03, 0x03);
	}

	if (chip->remote_wakeup_en && !chip->auto_delink_en) {
		RTSX_WRITE_REG(chip, WAKE_SEL_CTL, 0x07, 0x07);
		if (chip->aux_pwr_exist)
			RTSX_WRITE_REG(chip, PME_FORCE_CTL, 0xFF, 0x33);
	} else {
		RTSX_WRITE_REG(chip, WAKE_SEL_CTL, 0x07, 0x04);
		RTSX_WRITE_REG(chip, PME_FORCE_CTL, 0xFF, 0x30);
	}

	if (CHECK_PID(chip, 0x5208) && (chip->ic_version >= IC_VER_D))
		RTSX_WRITE_REG(chip, PETXCFG, 0x1C, 0x14);

	if (chip->asic_code && CHECK_PID(chip, 0x5208)) {
		retval = rtsx_clr_phy_reg_bit(chip, 0x1C, 2);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, STATUS_FAIL);
	}

	if (chip->ft2_fast_mode) {
		RTSX_WRITE_REG(chip, CARD_PWR_CTL, 0xFF,
			MS_PARTIAL_POWER_ON | SD_PARTIAL_POWER_ON);
		udelay(chip->pmos_pwr_on_interval);
		RTSX_WRITE_REG(chip, CARD_PWR_CTL, 0xFF,
			MS_POWER_ON | SD_POWER_ON);

		wait_timeout(200);
	}

	/* Reset card */
	rtsx_reset_detected_cards(chip, 0);

	chip->driver_first_load = 0;

	return STATUS_SUCCESS;
}

static inline int check_sd_speed_prior(u32 sd_speed_prior)
{
	int i, fake_para = 0;

	for (i = 0; i < 4; i++) {
		u8 tmp = (u8)(sd_speed_prior >> (i*8));
		if ((tmp < 0x01) || (tmp > 0x04)) {
			fake_para = 1;
			break;
		}
	}

	return !fake_para;
}

static inline int check_sd_current_prior(u32 sd_current_prior)
{
	int i, fake_para = 0;

	for (i = 0; i < 4; i++) {
		u8 tmp = (u8)(sd_current_prior >> (i*8));
		if (tmp > 0x03) {
			fake_para = 1;
			break;
		}
	}

	return !fake_para;
}

static int rts5208_init(struct rtsx_chip *chip)
{
	int retval;
	u16 reg = 0;
	u8 val = 0;

	RTSX_WRITE_REG(chip, CLK_SEL, 0x03, 0x03);
	RTSX_READ_REG(chip, CLK_SEL, &val);
	if (val == 0)
		chip->asic_code = 1;
	else
		chip->asic_code = 0;

	if (chip->asic_code) {
		retval = rtsx_read_phy_register(chip, 0x1C, &reg);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, STATUS_FAIL);

		RTSX_DEBUGP("Value of phy register 0x1C is 0x%x\n", reg);
		chip->ic_version = (reg >> 4) & 0x07;
		if (reg & PHY_DEBUG_MODE)
			chip->phy_debug_mode = 1;
		else
			chip->phy_debug_mode = 0;

	} else {
		RTSX_READ_REG(chip, 0xFE80, &val);
		chip->ic_version = val;
		chip->phy_debug_mode = 0;
	}

	RTSX_READ_REG(chip, PDINFO, &val);
	RTSX_DEBUGP("PDINFO: 0x%x\n", val);
	if (val & AUX_PWR_DETECTED)
		chip->aux_pwr_exist = 1;
	else
		chip->aux_pwr_exist = 0;

	RTSX_READ_REG(chip, 0xFE50, &val);
	if (val & 0x01)
		chip->hw_bypass_sd = 1;
	else
		chip->hw_bypass_sd = 0;

	rtsx_read_config_byte(chip, 0x0E, &val);
	if (val & 0x80)
		SET_SDIO_EXIST(chip);
	else
		CLR_SDIO_EXIST(chip);

	if (chip->use_hw_setting) {
		RTSX_READ_REG(chip, CHANGE_LINK_STATE, &val);
		if (val & 0x80)
			chip->auto_delink_en = 1;
		else
			chip->auto_delink_en = 0;
	}

	return STATUS_SUCCESS;
}

static int rts5288_init(struct rtsx_chip *chip)
{
	int retval;
	u8 val = 0, max_func;
	u32 lval = 0;

	RTSX_WRITE_REG(chip, CLK_SEL, 0x03, 0x03);
	RTSX_READ_REG(chip, CLK_SEL, &val);
	if (val == 0)
		chip->asic_code = 1;
	else
		chip->asic_code = 0;

	chip->ic_version = 0;
	chip->phy_debug_mode = 0;

	RTSX_READ_REG(chip, PDINFO, &val);
	RTSX_DEBUGP("PDINFO: 0x%x\n", val);
	if (val & AUX_PWR_DETECTED)
		chip->aux_pwr_exist = 1;
	else
		chip->aux_pwr_exist = 0;

	RTSX_READ_REG(chip, CARD_SHARE_MODE, &val);
	RTSX_DEBUGP("CARD_SHARE_MODE: 0x%x\n", val);
	if (val & 0x04)
		chip->baro_pkg = QFN;
	else
		chip->baro_pkg = LQFP;

	RTSX_READ_REG(chip, 0xFE5A, &val);
	if (val & 0x10)
		chip->hw_bypass_sd = 1;
	else
		chip->hw_bypass_sd = 0;

	retval = rtsx_read_cfg_dw(chip, 0, 0x718, &lval);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, STATUS_FAIL);

	max_func = (u8)((lval >> 29) & 0x07);
	RTSX_DEBUGP("Max function number: %d\n", max_func);
	if (max_func == 0x02)
		SET_SDIO_EXIST(chip);
	else
		CLR_SDIO_EXIST(chip);

	if (chip->use_hw_setting) {
		RTSX_READ_REG(chip, CHANGE_LINK_STATE, &val);
		if (val & 0x80)
			chip->auto_delink_en = 1;
		else
			chip->auto_delink_en = 0;

		if (CHECK_BARO_PKG(chip, LQFP))
			chip->lun_mode = SD_MS_1LUN;
		else
			chip->lun_mode = DEFAULT_SINGLE;

	}

	return STATUS_SUCCESS;
}

int rtsx_init_chip(struct rtsx_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	struct xd_info *xd_card = &(chip->xd_card);
	struct ms_info *ms_card = &(chip->ms_card);
	int retval;
	unsigned int i;

	RTSX_DEBUGP("Vendor ID: 0x%04x, Product ID: 0x%04x\n",
		     chip->vendor_id, chip->product_id);

	chip->ic_version = 0;

#ifdef _MSG_TRACE
	chip->msg_idx = 0;
#endif

	memset(xd_card, 0, sizeof(struct xd_info));
	memset(sd_card, 0, sizeof(struct sd_info));
	memset(ms_card, 0, sizeof(struct ms_info));

	chip->xd_reset_counter = 0;
	chip->sd_reset_counter = 0;
	chip->ms_reset_counter = 0;

	chip->xd_show_cnt = MAX_SHOW_CNT;
	chip->sd_show_cnt = MAX_SHOW_CNT;
	chip->ms_show_cnt = MAX_SHOW_CNT;

	chip->sd_io = 0;
	chip->auto_delink_cnt = 0;
	chip->auto_delink_allowed = 1;
	rtsx_set_stat(chip, RTSX_STAT_INIT);

	chip->aspm_enabled = 0;
	chip->chip_insert_with_sdio = 0;
	chip->sdio_aspm = 0;
	chip->sdio_idle = 0;
	chip->sdio_counter = 0;
	chip->cur_card = 0;
	chip->phy_debug_mode = 0;
	chip->sdio_func_exist = 0;
	memset(chip->sdio_raw_data, 0, 12);

	for (i = 0; i < MAX_ALLOWED_LUN_CNT; i++) {
		set_sense_type(chip, i, SENSE_TYPE_NO_SENSE);
		chip->rw_fail_cnt[i] = 0;
	}

	if (!check_sd_speed_prior(chip->sd_speed_prior))
		chip->sd_speed_prior = 0x01040203;

	RTSX_DEBUGP("sd_speed_prior = 0x%08x\n", chip->sd_speed_prior);

	if (!check_sd_current_prior(chip->sd_current_prior))
		chip->sd_current_prior = 0x00010203;

	RTSX_DEBUGP("sd_current_prior = 0x%08x\n", chip->sd_current_prior);

	if ((chip->sd_ddr_tx_phase > 31) || (chip->sd_ddr_tx_phase < 0))
		chip->sd_ddr_tx_phase = 0;

	if ((chip->mmc_ddr_tx_phase > 31) || (chip->mmc_ddr_tx_phase < 0))
		chip->mmc_ddr_tx_phase = 0;

	RTSX_WRITE_REG(chip, FPDCTL, SSC_POWER_DOWN, 0);
	wait_timeout(200);
	RTSX_WRITE_REG(chip, CLK_DIV, 0x07, 0x07);
	RTSX_DEBUGP("chip->use_hw_setting = %d\n", chip->use_hw_setting);

	if (CHECK_PID(chip, 0x5208)) {
		retval = rts5208_init(chip);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, STATUS_FAIL);

	} else if (CHECK_PID(chip, 0x5288)) {
		retval = rts5288_init(chip);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, STATUS_FAIL);

	}

	if (chip->ss_en == 2)
		chip->ss_en = 0;

	RTSX_DEBUGP("chip->asic_code = %d\n", chip->asic_code);
	RTSX_DEBUGP("chip->ic_version = 0x%x\n", chip->ic_version);
	RTSX_DEBUGP("chip->phy_debug_mode = %d\n", chip->phy_debug_mode);
	RTSX_DEBUGP("chip->aux_pwr_exist = %d\n", chip->aux_pwr_exist);
	RTSX_DEBUGP("chip->sdio_func_exist = %d\n", chip->sdio_func_exist);
	RTSX_DEBUGP("chip->hw_bypass_sd = %d\n", chip->hw_bypass_sd);
	RTSX_DEBUGP("chip->aspm_l0s_l1_en = %d\n", chip->aspm_l0s_l1_en);
	RTSX_DEBUGP("chip->lun_mode = %d\n", chip->lun_mode);
	RTSX_DEBUGP("chip->auto_delink_en = %d\n", chip->auto_delink_en);
	RTSX_DEBUGP("chip->ss_en = %d\n", chip->ss_en);
	RTSX_DEBUGP("chip->baro_pkg = %d\n", chip->baro_pkg);

	if (CHECK_LUN_MODE(chip, SD_MS_2LUN)) {
		chip->card2lun[SD_CARD] = 0;
		chip->card2lun[MS_CARD] = 1;
		chip->card2lun[XD_CARD] = 0xFF;
		chip->lun2card[0] = SD_CARD;
		chip->lun2card[1] = MS_CARD;
		chip->max_lun = 1;
		SET_SDIO_IGNORED(chip);
	} else if (CHECK_LUN_MODE(chip, SD_MS_1LUN)) {
		chip->card2lun[SD_CARD] = 0;
		chip->card2lun[MS_CARD] = 0;
		chip->card2lun[XD_CARD] = 0xFF;
		chip->lun2card[0] = SD_CARD | MS_CARD;
		chip->max_lun = 0;
	} else {
		chip->card2lun[XD_CARD] = 0;
		chip->card2lun[SD_CARD] = 0;
		chip->card2lun[MS_CARD] = 0;
		chip->lun2card[0] = XD_CARD | SD_CARD | MS_CARD;
		chip->max_lun = 0;
	}

	retval = rtsx_reset_chip(chip);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, STATUS_FAIL);

	return STATUS_SUCCESS;
}

void rtsx_release_chip(struct rtsx_chip *chip)
{
	xd_free_l2p_tbl(chip);
	ms_free_l2p_tbl(chip);
	chip->card_exist = 0;
	chip->card_ready = 0;
}

#if !defined(LED_AUTO_BLINK) && defined(REGULAR_BLINK)
static inline void rtsx_blink_led(struct rtsx_chip *chip)
{
	if (chip->card_exist && chip->blink_led) {
		if (chip->led_toggle_counter < LED_TOGGLE_INTERVAL) {
			chip->led_toggle_counter++;
		} else {
			chip->led_toggle_counter = 0;
			toggle_gpio(chip, LED_GPIO);
		}
	}
}
#endif

static void rtsx_monitor_aspm_config(struct rtsx_chip *chip)
{
	int maybe_support_aspm, reg_changed;
	u32 tmp = 0;
	u8 reg0 = 0, reg1 = 0;

	maybe_support_aspm = 0;
	reg_changed = 0;
	rtsx_read_config_byte(chip, LCTLR, &reg0);
	if (chip->aspm_level[0] != reg0) {
		reg_changed = 1;
		chip->aspm_level[0] = reg0;
	}
	if (CHK_SDIO_EXIST(chip) && !CHK_SDIO_IGNORED(chip)) {
		rtsx_read_cfg_dw(chip, 1, 0xC0, &tmp);
		reg1 = (u8)tmp;
		if (chip->aspm_level[1] != reg1) {
			reg_changed = 1;
			chip->aspm_level[1] = reg1;
		}

		if ((reg0 & 0x03) && (reg1 & 0x03))
			maybe_support_aspm = 1;

	} else {
		if (reg0 & 0x03)
			maybe_support_aspm = 1;

	}

	if (reg_changed) {
		if (maybe_support_aspm)
			chip->aspm_l0s_l1_en = 0x03;

		RTSX_DEBUGP("aspm_level[0] = 0x%02x, aspm_level[1] = 0x%02x\n",
			      chip->aspm_level[0], chip->aspm_level[1]);

		if (chip->aspm_l0s_l1_en) {
			chip->aspm_enabled = 1;
		} else {
			chip->aspm_enabled = 0;
			chip->sdio_aspm = 0;
		}
		rtsx_write_register(chip, ASPM_FORCE_CTL, 0xFF,
				0x30 | chip->aspm_level[0] |
				(chip->aspm_level[1] << 2));
	}
}

void rtsx_polling_func(struct rtsx_chip *chip)
{
#ifdef SUPPORT_SD_LOCK
	struct sd_info *sd_card = &(chip->sd_card);
#endif
	int ss_allowed;

	if (rtsx_chk_stat(chip, RTSX_STAT_SUSPEND))
		return;

	if (rtsx_chk_stat(chip, RTSX_STAT_DELINK))
		goto Delink_Stage;

	if (chip->polling_config) {
		u8 val;
		rtsx_read_config_byte(chip, 0, &val);
	}

	if (rtsx_chk_stat(chip, RTSX_STAT_SS))
		return;

#ifdef SUPPORT_OCP
	if (chip->ocp_int) {
		rtsx_read_register(chip, OCPSTAT, &(chip->ocp_stat));

		if (chip->card_exist & SD_CARD)
			sd_power_off_card3v3(chip);
		else if (chip->card_exist & MS_CARD)
			ms_power_off_card3v3(chip);
		else if (chip->card_exist & XD_CARD)
			xd_power_off_card3v3(chip);

		chip->ocp_int = 0;
	}
#endif

#ifdef SUPPORT_SD_LOCK
	if (sd_card->sd_erase_status) {
		if (chip->card_exist & SD_CARD) {
			u8 val;
			rtsx_read_register(chip, 0xFD30, &val);
			if (val & 0x02) {
				sd_card->sd_erase_status = SD_NOT_ERASE;
				sd_card->sd_lock_notify = 1;
				chip->need_reinit |= SD_CARD;
			}
		} else {
			sd_card->sd_erase_status = SD_NOT_ERASE;
		}
	}
#endif

	rtsx_init_cards(chip);

	if (chip->ss_en) {
		ss_allowed = 1;

		if (CHECK_PID(chip, 0x5288)) {
			ss_allowed = 0;
		} else {
			if (CHK_SDIO_EXIST(chip) && !CHK_SDIO_IGNORED(chip)) {
				u32 val;
				rtsx_read_cfg_dw(chip, 1, 0x04, &val);
				if (val & 0x07)
					ss_allowed = 0;

			}
		}
	} else {
		ss_allowed = 0;
	}

	if (ss_allowed && !chip->sd_io) {
		if (rtsx_get_stat(chip) != RTSX_STAT_IDLE) {
			chip->ss_counter = 0;
		} else {
			if (chip->ss_counter <
				(chip->ss_idle_period / POLLING_INTERVAL)) {
				chip->ss_counter++;
			} else {
				rtsx_exclusive_enter_ss(chip);
				return;
			}
		}
	}

	if (CHECK_PID(chip, 0x5208)) {
		rtsx_monitor_aspm_config(chip);

#ifdef SUPPORT_SDIO_ASPM
		if (CHK_SDIO_EXIST(chip) && !CHK_SDIO_IGNORED(chip) &&
				chip->aspm_l0s_l1_en && chip->dynamic_aspm) {
			if (chip->sd_io) {
				dynamic_configure_sdio_aspm(chip);
			} else {
				if (!chip->sdio_aspm) {
					RTSX_DEBUGP("SDIO enter ASPM!\n");
					rtsx_write_register(chip,
						ASPM_FORCE_CTL, 0xFC,
						0x30 | (chip->aspm_level[1] << 2));
					chip->sdio_aspm = 1;
				}
			}
		}
#endif
	}

	if (chip->idle_counter < IDLE_MAX_COUNT) {
		chip->idle_counter++;
	} else {
		if (rtsx_get_stat(chip) != RTSX_STAT_IDLE) {
			RTSX_DEBUGP("Idle state!\n");
			rtsx_set_stat(chip, RTSX_STAT_IDLE);

#if !defined(LED_AUTO_BLINK) && defined(REGULAR_BLINK)
			chip->led_toggle_counter = 0;
#endif
			rtsx_force_power_on(chip, SSC_PDCTL);

			turn_off_led(chip, LED_GPIO);

			if (chip->auto_power_down && !chip->card_ready && !chip->sd_io)
				rtsx_force_power_down(chip, SSC_PDCTL | OC_PDCTL);

		}
	}

	switch (rtsx_get_stat(chip)) {
	case RTSX_STAT_RUN:
#if !defined(LED_AUTO_BLINK) && defined(REGULAR_BLINK)
		rtsx_blink_led(chip);
#endif
		do_remaining_work(chip);
		break;

	case RTSX_STAT_IDLE:
		if (chip->sd_io && !chip->sd_int)
			try_to_switch_sdio_ctrl(chip);

		rtsx_enable_aspm(chip);
		break;

	default:
		break;
	}


#ifdef SUPPORT_OCP
	if (CHECK_LUN_MODE(chip, SD_MS_2LUN)) {
#ifdef CONFIG_RTS5208_DEBUG
		if (chip->ocp_stat &
			(SD_OC_NOW | SD_OC_EVER | MS_OC_NOW | MS_OC_EVER))
			RTSX_DEBUGP("Over current, OCPSTAT is 0x%x\n",
				chip->ocp_stat);
#endif

		if (chip->ocp_stat & (SD_OC_NOW | SD_OC_EVER)) {
			if (chip->card_exist & SD_CARD) {
				rtsx_write_register(chip, CARD_OE, SD_OUTPUT_EN,
						0);
				card_power_off(chip, SD_CARD);
				chip->card_fail |= SD_CARD;
			}
		}
		if (chip->ocp_stat & (MS_OC_NOW | MS_OC_EVER)) {
			if (chip->card_exist & MS_CARD) {
				rtsx_write_register(chip, CARD_OE, MS_OUTPUT_EN,
						0);
				card_power_off(chip, MS_CARD);
				chip->card_fail |= MS_CARD;
			}
		}
	} else {
		if (chip->ocp_stat & (SD_OC_NOW | SD_OC_EVER)) {
			RTSX_DEBUGP("Over current, OCPSTAT is 0x%x\n",
				chip->ocp_stat);
			if (chip->card_exist & SD_CARD) {
				rtsx_write_register(chip, CARD_OE, SD_OUTPUT_EN,
						0);
				chip->card_fail |= SD_CARD;
			} else if (chip->card_exist & MS_CARD) {
				rtsx_write_register(chip, CARD_OE, MS_OUTPUT_EN,
						0);
				chip->card_fail |= MS_CARD;
			} else if (chip->card_exist & XD_CARD) {
				rtsx_write_register(chip, CARD_OE, XD_OUTPUT_EN,
						0);
				chip->card_fail |= XD_CARD;
			}
			card_power_off(chip, SD_CARD);
		}
	}
#endif

Delink_Stage:
	if (chip->auto_delink_en && chip->auto_delink_allowed &&
		!chip->card_ready && !chip->card_ejected && !chip->sd_io) {
		int enter_L1 = chip->auto_delink_in_L1 && (
			chip->aspm_l0s_l1_en || chip->ss_en);
		int delink_stage1_cnt = chip->delink_stage1_step;
		int delink_stage2_cnt = delink_stage1_cnt +
			chip->delink_stage2_step;
		int delink_stage3_cnt = delink_stage2_cnt +
			chip->delink_stage3_step;

		if (chip->auto_delink_cnt <= delink_stage3_cnt) {
			if (chip->auto_delink_cnt == delink_stage1_cnt) {
				rtsx_set_stat(chip, RTSX_STAT_DELINK);

				if (chip->asic_code && CHECK_PID(chip, 0x5208))
					rtsx_set_phy_reg_bit(chip, 0x1C, 2);

				if (chip->card_exist) {
					RTSX_DEBUGP("False card inserted, do force delink\n");

					if (enter_L1)
						rtsx_write_register(chip, HOST_SLEEP_STATE, 0x03, 1);

					rtsx_write_register(chip,
							CHANGE_LINK_STATE, 0x0A,
							0x0A);

					if (enter_L1)
						rtsx_enter_L1(chip);

					chip->auto_delink_cnt = delink_stage3_cnt + 1;
				} else {
					RTSX_DEBUGP("No card inserted, do delink\n");

					if (enter_L1)
						rtsx_write_register(chip, HOST_SLEEP_STATE, 0x03, 1);

					rtsx_write_register(chip, CHANGE_LINK_STATE, 0x02, 0x02);

					if (enter_L1)
						rtsx_enter_L1(chip);

				}
			}

			if (chip->auto_delink_cnt == delink_stage2_cnt) {
				RTSX_DEBUGP("Try to do force delink\n");

				if (enter_L1)
					rtsx_exit_L1(chip);

				if (chip->asic_code && CHECK_PID(chip, 0x5208))
					rtsx_set_phy_reg_bit(chip, 0x1C, 2);

				rtsx_write_register(chip, CHANGE_LINK_STATE,
						0x0A, 0x0A);
			}

			chip->auto_delink_cnt++;
		}
	} else {
		chip->auto_delink_cnt = 0;
	}
}

void rtsx_undo_delink(struct rtsx_chip *chip)
{
	chip->auto_delink_allowed = 0;
	rtsx_write_register(chip, CHANGE_LINK_STATE, 0x0A, 0x00);
}

/**
 * rtsx_stop_cmd - stop command transfer and DMA transfer
 * @chip: Realtek's card reader chip
 * @card: flash card type
 *
 * Stop command transfer and DMA transfer.
 * This function is called in error handler.
 */
void rtsx_stop_cmd(struct rtsx_chip *chip, int card)
{
	int i;

	for (i = 0; i <= 8; i++) {
		int addr = RTSX_HCBAR + i * 4;
		u32 reg;
		reg = rtsx_readl(chip, addr);
		RTSX_DEBUGP("BAR (0x%02x): 0x%08x\n", addr, reg);
	}
	rtsx_writel(chip, RTSX_HCBCTLR, STOP_CMD);
	rtsx_writel(chip, RTSX_HDBCTLR, STOP_DMA);

	for (i = 0; i < 16; i++) {
		u16 addr = 0xFE20 + (u16)i;
		u8 val;
		rtsx_read_register(chip, addr, &val);
		RTSX_DEBUGP("0x%04X: 0x%02x\n", addr, val);
	}

	rtsx_write_register(chip, DMACTL, 0x80, 0x80);
	rtsx_write_register(chip, RBCTL, 0x80, 0x80);
}

#define MAX_RW_REG_CNT		1024

int rtsx_write_register(struct rtsx_chip *chip, u16 addr, u8 mask, u8 data)
{
	int i;
	u32 val = 3 << 30;

	val |= (u32)(addr & 0x3FFF) << 16;
	val |= (u32)mask << 8;
	val |= (u32)data;

	rtsx_writel(chip, RTSX_HAIMR, val);

	for (i = 0; i < MAX_RW_REG_CNT; i++) {
		val = rtsx_readl(chip, RTSX_HAIMR);
		if ((val & (1 << 31)) == 0) {
			if (data != (u8)val)
				TRACE_RET(chip, STATUS_FAIL);

			return STATUS_SUCCESS;
		}
	}

	TRACE_RET(chip, STATUS_TIMEDOUT);
}

int rtsx_read_register(struct rtsx_chip *chip, u16 addr, u8 *data)
{
	u32 val = 2 << 30;
	int i;

	if (data)
		*data = 0;

	val |= (u32)(addr & 0x3FFF) << 16;

	rtsx_writel(chip, RTSX_HAIMR, val);

	for (i = 0; i < MAX_RW_REG_CNT; i++) {
		val = rtsx_readl(chip, RTSX_HAIMR);
		if ((val & (1 << 31)) == 0)
			break;
	}

	if (i >= MAX_RW_REG_CNT)
		TRACE_RET(chip, STATUS_TIMEDOUT);

	if (data)
		*data = (u8)(val & 0xFF);

	return STATUS_SUCCESS;
}

int rtsx_write_cfg_dw(struct rtsx_chip *chip, u8 func_no, u16 addr, u32 mask,
		u32 val)
{
	u8 mode = 0, tmp;
	int i;

	for (i = 0; i < 4; i++) {
		if (mask & 0xFF) {
			RTSX_WRITE_REG(chip, CFGDATA0 + i,
				       0xFF, (u8)(val & mask & 0xFF));
			mode |= (1 << i);
		}
		mask >>= 8;
		val >>= 8;
	}

	if (mode) {
		RTSX_WRITE_REG(chip, CFGADDR0, 0xFF, (u8)addr);
		RTSX_WRITE_REG(chip, CFGADDR1, 0xFF, (u8)(addr >> 8));

		RTSX_WRITE_REG(chip, CFGRWCTL, 0xFF,
			       0x80 | mode | ((func_no & 0x03) << 4));

		for (i = 0; i < MAX_RW_REG_CNT; i++) {
			RTSX_READ_REG(chip, CFGRWCTL, &tmp);
			if ((tmp & 0x80) == 0)
				break;
		}
	}

	return STATUS_SUCCESS;
}

int rtsx_read_cfg_dw(struct rtsx_chip *chip, u8 func_no, u16 addr, u32 *val)
{
	int i;
	u8 tmp;
	u32 data = 0;

	RTSX_WRITE_REG(chip, CFGADDR0, 0xFF, (u8)addr);
	RTSX_WRITE_REG(chip, CFGADDR1, 0xFF, (u8)(addr >> 8));
	RTSX_WRITE_REG(chip, CFGRWCTL, 0xFF, 0x80 | ((func_no & 0x03) << 4));

	for (i = 0; i < MAX_RW_REG_CNT; i++) {
		RTSX_READ_REG(chip, CFGRWCTL, &tmp);
		if ((tmp & 0x80) == 0)
			break;
	}

	for (i = 0; i < 4; i++) {
		RTSX_READ_REG(chip, CFGDATA0 + i, &tmp);
		data |= (u32)tmp << (i * 8);
	}

	if (val)
		*val = data;

	return STATUS_SUCCESS;
}

int rtsx_write_cfg_seq(struct rtsx_chip *chip, u8 func, u16 addr, u8 *buf,
		int len)
{
	u32 *data, *mask;
	u16 offset = addr % 4;
	u16 aligned_addr = addr - offset;
	int dw_len, i, j;
	int retval;

	RTSX_DEBUGP("%s\n", __func__);

	if (!buf)
		TRACE_RET(chip, STATUS_NOMEM);

	if ((len + offset) % 4)
		dw_len = (len + offset) / 4 + 1;
	else
		dw_len = (len + offset) / 4;

	RTSX_DEBUGP("dw_len = %d\n", dw_len);

	data = vzalloc(dw_len * 4);
	if (!data)
		TRACE_RET(chip, STATUS_NOMEM);

	mask = vzalloc(dw_len * 4);
	if (!mask) {
		vfree(data);
		TRACE_RET(chip, STATUS_NOMEM);
	}

	j = 0;
	for (i = 0; i < len; i++) {
		mask[j] |= 0xFF << (offset * 8);
		data[j] |= buf[i] << (offset * 8);
		if (++offset == 4) {
			j++;
			offset = 0;
		}
	}

	RTSX_DUMP(mask, dw_len * 4);
	RTSX_DUMP(data, dw_len * 4);

	for (i = 0; i < dw_len; i++) {
		retval = rtsx_write_cfg_dw(chip, func, aligned_addr + i * 4,
					mask[i], data[i]);
		if (retval != STATUS_SUCCESS) {
			vfree(data);
			vfree(mask);
			TRACE_RET(chip, STATUS_FAIL);
		}
	}

	vfree(data);
	vfree(mask);

	return STATUS_SUCCESS;
}

int rtsx_read_cfg_seq(struct rtsx_chip *chip, u8 func, u16 addr, u8 *buf,
		int len)
{
	u32 *data;
	u16 offset = addr % 4;
	u16 aligned_addr = addr - offset;
	int dw_len, i, j;
	int retval;

	RTSX_DEBUGP("%s\n", __func__);

	if ((len + offset) % 4)
		dw_len = (len + offset) / 4 + 1;
	else
		dw_len = (len + offset) / 4;

	RTSX_DEBUGP("dw_len = %d\n", dw_len);

	data = vmalloc(dw_len * 4);
	if (!data)
		TRACE_RET(chip, STATUS_NOMEM);

	for (i = 0; i < dw_len; i++) {
		retval = rtsx_read_cfg_dw(chip, func, aligned_addr + i * 4,
					data + i);
		if (retval != STATUS_SUCCESS) {
			vfree(data);
			TRACE_RET(chip, STATUS_FAIL);
		}
	}

	if (buf) {
		j = 0;

		for (i = 0; i < len; i++) {
			buf[i] = (u8)(data[j] >> (offset * 8));
			if (++offset == 4) {
				j++;
				offset = 0;
			}
		}
	}

	vfree(data);

	return STATUS_SUCCESS;
}

int rtsx_write_phy_register(struct rtsx_chip *chip, u8 addr, u16 val)
{
	int i, finished = 0;
	u8 tmp;

	RTSX_WRITE_REG(chip, PHYDATA0, 0xFF, (u8)val);
	RTSX_WRITE_REG(chip, PHYDATA1, 0xFF, (u8)(val >> 8));
	RTSX_WRITE_REG(chip, PHYADDR, 0xFF, addr);
	RTSX_WRITE_REG(chip, PHYRWCTL, 0xFF, 0x81);

	for (i = 0; i < 100000; i++) {
		RTSX_READ_REG(chip, PHYRWCTL, &tmp);
		if (!(tmp & 0x80)) {
			finished = 1;
			break;
		}
	}

	if (!finished)
		TRACE_RET(chip, STATUS_FAIL);

	return STATUS_SUCCESS;
}

int rtsx_read_phy_register(struct rtsx_chip *chip, u8 addr, u16 *val)
{
	int i, finished = 0;
	u16 data = 0;
	u8 tmp;

	RTSX_WRITE_REG(chip, PHYADDR, 0xFF, addr);
	RTSX_WRITE_REG(chip, PHYRWCTL, 0xFF, 0x80);

	for (i = 0; i < 100000; i++) {
		RTSX_READ_REG(chip, PHYRWCTL, &tmp);
		if (!(tmp & 0x80)) {
			finished = 1;
			break;
		}
	}

	if (!finished)
		TRACE_RET(chip, STATUS_FAIL);

	RTSX_READ_REG(chip, PHYDATA0, &tmp);
	data = tmp;
	RTSX_READ_REG(chip, PHYDATA1, &tmp);
	data |= (u16)tmp << 8;

	if (val)
		*val = data;

	return STATUS_SUCCESS;
}

int rtsx_read_efuse(struct rtsx_chip *chip, u8 addr, u8 *val)
{
	int i;
	u8 data = 0;

	RTSX_WRITE_REG(chip, EFUSE_CTRL, 0xFF, 0x80|addr);

	for (i = 0; i < 100; i++) {
		RTSX_READ_REG(chip, EFUSE_CTRL, &data);
		if (!(data & 0x80))
			break;
		udelay(1);
	}

	if (data & 0x80)
		TRACE_RET(chip, STATUS_TIMEDOUT);

	RTSX_READ_REG(chip, EFUSE_DATA, &data);
	if (val)
		*val = data;

	return STATUS_SUCCESS;
}

int rtsx_write_efuse(struct rtsx_chip *chip, u8 addr, u8 val)
{
	int i, j;
	u8 data = 0, tmp = 0xFF;

	for (i = 0; i < 8; i++) {
		if (val & (u8)(1 << i))
			continue;

		tmp &= (~(u8)(1 << i));
		RTSX_DEBUGP("Write 0x%x to 0x%x\n", tmp, addr);

		RTSX_WRITE_REG(chip, EFUSE_DATA, 0xFF, tmp);
		RTSX_WRITE_REG(chip, EFUSE_CTRL, 0xFF, 0xA0|addr);

		for (j = 0; j < 100; j++) {
			RTSX_READ_REG(chip, EFUSE_CTRL, &data);
			if (!(data & 0x80))
				break;
			wait_timeout(3);
		}

		if (data & 0x80)
			TRACE_RET(chip, STATUS_TIMEDOUT);

		wait_timeout(5);
	}

	return STATUS_SUCCESS;
}

int rtsx_clr_phy_reg_bit(struct rtsx_chip *chip, u8 reg, u8 bit)
{
	int retval;
	u16 value;

	retval = rtsx_read_phy_register(chip, reg, &value);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, STATUS_FAIL);

	if (value & (1 << bit)) {
		value &= ~(1 << bit);
		retval = rtsx_write_phy_register(chip, reg, value);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, STATUS_FAIL);
	}

	return STATUS_SUCCESS;
}

int rtsx_set_phy_reg_bit(struct rtsx_chip *chip, u8 reg, u8 bit)
{
	int retval;
	u16 value;

	retval = rtsx_read_phy_register(chip, reg, &value);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, STATUS_FAIL);

	if (0 == (value & (1 << bit))) {
		value |= (1 << bit);
		retval = rtsx_write_phy_register(chip, reg, value);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, STATUS_FAIL);
	}

	return STATUS_SUCCESS;
}

int rtsx_check_link_ready(struct rtsx_chip *chip)
{
	u8 val;

	RTSX_READ_REG(chip, IRQSTAT0, &val);

	RTSX_DEBUGP("IRQSTAT0: 0x%x\n", val);
	if (val & LINK_RDY_INT) {
		RTSX_DEBUGP("Delinked!\n");
		rtsx_write_register(chip, IRQSTAT0, LINK_RDY_INT, LINK_RDY_INT);
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}

static void rtsx_handle_pm_dstate(struct rtsx_chip *chip, u8 dstate)
{
	u32 ultmp;

	RTSX_DEBUGP("%04x set pm_dstate to %d\n", chip->product_id, dstate);

	if (CHK_SDIO_EXIST(chip)) {
		u8 func_no;

		if (CHECK_PID(chip, 0x5288))
			func_no = 2;
		else
			func_no = 1;

		rtsx_read_cfg_dw(chip, func_no, 0x84, &ultmp);
		RTSX_DEBUGP("pm_dstate of function %d: 0x%x\n", (int)func_no,
			ultmp);
		rtsx_write_cfg_dw(chip, func_no, 0x84, 0xFF, dstate);
	}

	rtsx_write_config_byte(chip, 0x44, dstate);
	rtsx_write_config_byte(chip, 0x45, 0);
}

void rtsx_enter_L1(struct rtsx_chip *chip)
{
	rtsx_handle_pm_dstate(chip, 2);
}

void rtsx_exit_L1(struct rtsx_chip *chip)
{
	rtsx_write_config_byte(chip, 0x44, 0);
	rtsx_write_config_byte(chip, 0x45, 0);
}

void rtsx_enter_ss(struct rtsx_chip *chip)
{
	RTSX_DEBUGP("Enter Selective Suspend State!\n");

	rtsx_write_register(chip, IRQSTAT0, LINK_RDY_INT, LINK_RDY_INT);

	if (chip->power_down_in_ss) {
		rtsx_power_off_card(chip);
		rtsx_force_power_down(chip, SSC_PDCTL | OC_PDCTL);
	}

	if (CHK_SDIO_EXIST(chip)) {
		if (CHECK_PID(chip, 0x5288))
			rtsx_write_cfg_dw(chip, 2, 0xC0, 0xFF00, 0x0100);
		else
			rtsx_write_cfg_dw(chip, 1, 0xC0, 0xFF00, 0x0100);
	}

	if (chip->auto_delink_en) {
		rtsx_write_register(chip, HOST_SLEEP_STATE, 0x01, 0x01);
	} else {
		if (!chip->phy_debug_mode) {
			u32 tmp;
			tmp = rtsx_readl(chip, RTSX_BIER);
			tmp |= CARD_INT;
			rtsx_writel(chip, RTSX_BIER, tmp);
		}

		rtsx_write_register(chip, CHANGE_LINK_STATE, 0x02, 0);
	}

	rtsx_enter_L1(chip);

	RTSX_CLR_DELINK(chip);
	rtsx_set_stat(chip, RTSX_STAT_SS);
}

void rtsx_exit_ss(struct rtsx_chip *chip)
{
	RTSX_DEBUGP("Exit Selective Suspend State!\n");

	rtsx_exit_L1(chip);

	if (chip->power_down_in_ss) {
		rtsx_force_power_on(chip, SSC_PDCTL | OC_PDCTL);
		udelay(1000);
	}

	if (RTSX_TST_DELINK(chip)) {
		chip->need_reinit = SD_CARD | MS_CARD | XD_CARD;
		rtsx_reinit_cards(chip, 1);
		RTSX_CLR_DELINK(chip);
	} else if (chip->power_down_in_ss) {
		chip->need_reinit = SD_CARD | MS_CARD | XD_CARD;
		rtsx_reinit_cards(chip, 0);
	}
}

int rtsx_pre_handle_interrupt(struct rtsx_chip *chip)
{
	u32 status, int_enable;
	int exit_ss = 0;
#ifdef SUPPORT_OCP
	u32 ocp_int = 0;

	ocp_int = OC_INT;
#endif

	if (chip->ss_en) {
		chip->ss_counter = 0;
		if (rtsx_get_stat(chip) == RTSX_STAT_SS) {
			exit_ss = 1;
			rtsx_exit_L1(chip);
			rtsx_set_stat(chip, RTSX_STAT_RUN);
		}
	}

	int_enable = rtsx_readl(chip, RTSX_BIER);
	chip->int_reg = rtsx_readl(chip, RTSX_BIPR);

	if (((chip->int_reg & int_enable) == 0) ||
		(chip->int_reg == 0xFFFFFFFF))
		return STATUS_FAIL;

	status = chip->int_reg &= (int_enable | 0x7FFFFF);

	if (status & CARD_INT) {
		chip->auto_delink_cnt = 0;

		if (status & SD_INT) {
			if (status & SD_EXIST) {
				set_bit(SD_NR, &(chip->need_reset));
			} else {
				set_bit(SD_NR, &(chip->need_release));
				chip->sd_reset_counter = 0;
				chip->sd_show_cnt = 0;
				clear_bit(SD_NR, &(chip->need_reset));
			}
		} else {
			/* If multi-luns, it's possible that
			   when plugging/unplugging one card
			   there is another card which still
			   exists in the slot. In this case,
			   all existed cards should be reset.
			*/
			if (exit_ss && (status & SD_EXIST))
				set_bit(SD_NR, &(chip->need_reinit));
		}
		if (!CHECK_PID(chip, 0x5288) || CHECK_BARO_PKG(chip, QFN)) {
			if (status & XD_INT) {
				if (status & XD_EXIST) {
					set_bit(XD_NR, &(chip->need_reset));
				} else {
					set_bit(XD_NR, &(chip->need_release));
					chip->xd_reset_counter = 0;
					chip->xd_show_cnt = 0;
					clear_bit(XD_NR, &(chip->need_reset));
				}
			} else {
				if (exit_ss && (status & XD_EXIST))
					set_bit(XD_NR, &(chip->need_reinit));
			}
		}
		if (status & MS_INT) {
			if (status & MS_EXIST) {
				set_bit(MS_NR, &(chip->need_reset));
			} else {
				set_bit(MS_NR, &(chip->need_release));
				chip->ms_reset_counter = 0;
				chip->ms_show_cnt = 0;
				clear_bit(MS_NR, &(chip->need_reset));
			}
		} else {
			if (exit_ss && (status & MS_EXIST))
				set_bit(MS_NR, &(chip->need_reinit));
		}
	}

#ifdef SUPPORT_OCP
	chip->ocp_int = ocp_int & status;
#endif

	if (chip->sd_io) {
		if (chip->int_reg & DATA_DONE_INT)
			chip->int_reg &= ~(u32)DATA_DONE_INT;
	}

	return STATUS_SUCCESS;
}

void rtsx_do_before_power_down(struct rtsx_chip *chip, int pm_stat)
{
	int retval;

	RTSX_DEBUGP("rtsx_do_before_power_down, pm_stat = %d\n", pm_stat);

	rtsx_set_stat(chip, RTSX_STAT_SUSPEND);

	retval = rtsx_force_power_on(chip, SSC_PDCTL);
	if (retval != STATUS_SUCCESS)
		return;

	rtsx_release_cards(chip);
	rtsx_disable_bus_int(chip);
	turn_off_led(chip, LED_GPIO);

#ifdef HW_AUTO_SWITCH_SD_BUS
	if (chip->sd_io) {
		chip->sdio_in_charge = 1;
		if (CHECK_PID(chip, 0x5208)) {
			rtsx_write_register(chip, TLPTISTAT, 0x08, 0x08);
			/* Enable sdio_bus_auto_switch */
			rtsx_write_register(chip, 0xFE70, 0x80, 0x80);
		} else if (CHECK_PID(chip, 0x5288)) {
			rtsx_write_register(chip, TLPTISTAT, 0x08, 0x08);
			/* Enable sdio_bus_auto_switch */
			rtsx_write_register(chip, 0xFE5A, 0x08, 0x08);
		}
	}
#endif

	if (CHECK_PID(chip, 0x5208) && (chip->ic_version >= IC_VER_D)) {
		/* u_force_clkreq_0 */
		rtsx_write_register(chip, PETXCFG, 0x08, 0x08);
	}

	if (pm_stat == PM_S1) {
		RTSX_DEBUGP("Host enter S1\n");
		rtsx_write_register(chip, HOST_SLEEP_STATE, 0x03,
				HOST_ENTER_S1);
	} else if (pm_stat == PM_S3) {
		if (chip->s3_pwr_off_delay > 0)
			wait_timeout(chip->s3_pwr_off_delay);

		RTSX_DEBUGP("Host enter S3\n");
		rtsx_write_register(chip, HOST_SLEEP_STATE, 0x03,
				HOST_ENTER_S3);
	}

	if (chip->do_delink_before_power_down && chip->auto_delink_en)
		rtsx_write_register(chip, CHANGE_LINK_STATE, 0x02, 2);

	rtsx_force_power_down(chip, SSC_PDCTL | OC_PDCTL);

	chip->cur_clk = 0;
	chip->cur_card = 0;
	chip->card_exist = 0;
}

void rtsx_enable_aspm(struct rtsx_chip *chip)
{
	if (chip->aspm_l0s_l1_en && chip->dynamic_aspm) {
		if (!chip->aspm_enabled) {
			RTSX_DEBUGP("Try to enable ASPM\n");
			chip->aspm_enabled = 1;

			if (chip->asic_code && CHECK_PID(chip, 0x5208))
				rtsx_write_phy_register(chip, 0x07, 0);
			if (CHECK_PID(chip, 0x5208)) {
				rtsx_write_register(chip, ASPM_FORCE_CTL, 0xF3,
					0x30 | chip->aspm_level[0]);
			} else {
				rtsx_write_config_byte(chip, LCTLR,
						chip->aspm_l0s_l1_en);
			}

			if (CHK_SDIO_EXIST(chip)) {
				u16 val = chip->aspm_l0s_l1_en | 0x0100;
				if (CHECK_PID(chip, 0x5288))
					rtsx_write_cfg_dw(chip, 2, 0xC0,
							0xFFFF, val);
				else
					rtsx_write_cfg_dw(chip, 1, 0xC0,
							0xFFFF, val);
			}
		}
	}

	return;
}

void rtsx_disable_aspm(struct rtsx_chip *chip)
{
	if (CHECK_PID(chip, 0x5208))
		rtsx_monitor_aspm_config(chip);

	if (chip->aspm_l0s_l1_en && chip->dynamic_aspm) {
		if (chip->aspm_enabled) {
			RTSX_DEBUGP("Try to disable ASPM\n");
			chip->aspm_enabled = 0;

			if (chip->asic_code && CHECK_PID(chip, 0x5208))
				rtsx_write_phy_register(chip, 0x07, 0x0129);
			if (CHECK_PID(chip, 0x5208))
				rtsx_write_register(chip, ASPM_FORCE_CTL,
						0xF3, 0x30);
			else
				rtsx_write_config_byte(chip, LCTLR, 0x00);

			wait_timeout(1);
		}
	}

	return;
}

int rtsx_read_ppbuf(struct rtsx_chip *chip, u8 *buf, int buf_len)
{
	int retval;
	int i, j;
	u16 reg_addr;
	u8 *ptr;

	if (!buf)
		TRACE_RET(chip, STATUS_ERROR);

	ptr = buf;
	reg_addr = PPBUF_BASE2;
	for (i = 0; i < buf_len/256; i++) {
		rtsx_init_cmd(chip);

		for (j = 0; j < 256; j++)
			rtsx_add_cmd(chip, READ_REG_CMD, reg_addr++, 0, 0);

		retval = rtsx_send_cmd(chip, 0, 250);
		if (retval < 0)
			TRACE_RET(chip, STATUS_FAIL);

		memcpy(ptr, rtsx_get_cmd_data(chip), 256);
		ptr += 256;
	}

	if (buf_len%256) {
		rtsx_init_cmd(chip);

		for (j = 0; j < buf_len%256; j++)
			rtsx_add_cmd(chip, READ_REG_CMD, reg_addr++, 0, 0);

		retval = rtsx_send_cmd(chip, 0, 250);
		if (retval < 0)
			TRACE_RET(chip, STATUS_FAIL);
	}

	memcpy(ptr, rtsx_get_cmd_data(chip), buf_len%256);

	return STATUS_SUCCESS;
}

int rtsx_write_ppbuf(struct rtsx_chip *chip, u8 *buf, int buf_len)
{
	int retval;
	int i, j;
	u16 reg_addr;
	u8 *ptr;

	if (!buf)
		TRACE_RET(chip, STATUS_ERROR);

	ptr = buf;
	reg_addr = PPBUF_BASE2;
	for (i = 0; i < buf_len/256; i++) {
		rtsx_init_cmd(chip);

		for (j = 0; j < 256; j++) {
			rtsx_add_cmd(chip, WRITE_REG_CMD, reg_addr++, 0xFF,
				*ptr);
			ptr++;
		}

		retval = rtsx_send_cmd(chip, 0, 250);
		if (retval < 0)
			TRACE_RET(chip, STATUS_FAIL);
	}

	if (buf_len%256) {
		rtsx_init_cmd(chip);

		for (j = 0; j < buf_len%256; j++) {
			rtsx_add_cmd(chip, WRITE_REG_CMD, reg_addr++, 0xFF,
				*ptr);
			ptr++;
		}

		retval = rtsx_send_cmd(chip, 0, 250);
		if (retval < 0)
			TRACE_RET(chip, STATUS_FAIL);
	}

	return STATUS_SUCCESS;
}

int rtsx_check_chip_exist(struct rtsx_chip *chip)
{
	if (rtsx_readl(chip, 0) == 0xFFFFFFFF)
		TRACE_RET(chip, STATUS_FAIL);

	return STATUS_SUCCESS;
}

int rtsx_force_power_on(struct rtsx_chip *chip, u8 ctl)
{
	int retval;
	u8 mask = 0;

	if (ctl & SSC_PDCTL)
		mask |= SSC_POWER_DOWN;

#ifdef SUPPORT_OCP
	if (ctl & OC_PDCTL) {
		mask |= SD_OC_POWER_DOWN;
		if (CHECK_LUN_MODE(chip, SD_MS_2LUN))
			mask |= MS_OC_POWER_DOWN;
	}
#endif

	if (mask) {
		retval = rtsx_write_register(chip, FPDCTL, mask, 0);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, STATUS_FAIL);

		if (CHECK_PID(chip, 0x5288))
			wait_timeout(200);
	}

	return STATUS_SUCCESS;
}

int rtsx_force_power_down(struct rtsx_chip *chip, u8 ctl)
{
	int retval;
	u8 mask = 0, val = 0;

	if (ctl & SSC_PDCTL)
		mask |= SSC_POWER_DOWN;

#ifdef SUPPORT_OCP
	if (ctl & OC_PDCTL) {
		mask |= SD_OC_POWER_DOWN;
		if (CHECK_LUN_MODE(chip, SD_MS_2LUN))
			mask |= MS_OC_POWER_DOWN;
	}
#endif

	if (mask) {
		val = mask;
		retval = rtsx_write_register(chip, FPDCTL, mask, val);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, STATUS_FAIL);
	}

	return STATUS_SUCCESS;
}
