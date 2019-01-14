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
#include <linux/kernel.h>

#include "rtsx.h"
#include "sd.h"
#include "xd.h"
#include "ms.h"

void do_remaining_work(struct rtsx_chip *chip)
{
	struct sd_info *sd_card = &chip->sd_card;
#ifdef XD_DELAY_WRITE
	struct xd_info *xd_card = &chip->xd_card;
#endif
	struct ms_info *ms_card = &chip->ms_card;

	if (chip->card_ready & SD_CARD) {
		if (sd_card->seq_mode) {
			rtsx_set_stat(chip, RTSX_STAT_RUN);
			sd_card->cleanup_counter++;
		} else {
			sd_card->cleanup_counter = 0;
		}
	}

#ifdef XD_DELAY_WRITE
	if (chip->card_ready & XD_CARD) {
		if (xd_card->delay_write.delay_write_flag) {
			rtsx_set_stat(chip, RTSX_STAT_RUN);
			xd_card->cleanup_counter++;
		} else {
			xd_card->cleanup_counter = 0;
		}
	}
#endif

	if (chip->card_ready & MS_CARD) {
		if (CHK_MSPRO(ms_card)) {
			if (ms_card->seq_mode) {
				rtsx_set_stat(chip, RTSX_STAT_RUN);
				ms_card->cleanup_counter++;
			} else {
				ms_card->cleanup_counter = 0;
			}
		} else {
#ifdef MS_DELAY_WRITE
			if (ms_card->delay_write.delay_write_flag) {
				rtsx_set_stat(chip, RTSX_STAT_RUN);
				ms_card->cleanup_counter++;
			} else {
				ms_card->cleanup_counter = 0;
			}
#endif
		}
	}

	if (sd_card->cleanup_counter > POLLING_WAIT_CNT)
		sd_cleanup_work(chip);

	if (xd_card->cleanup_counter > POLLING_WAIT_CNT)
		xd_cleanup_work(chip);

	if (ms_card->cleanup_counter > POLLING_WAIT_CNT)
		ms_cleanup_work(chip);
}

void try_to_switch_sdio_ctrl(struct rtsx_chip *chip)
{
	u8 reg1 = 0, reg2 = 0;

	rtsx_read_register(chip, 0xFF34, &reg1);
	rtsx_read_register(chip, 0xFF38, &reg2);
	dev_dbg(rtsx_dev(chip), "reg 0xFF34: 0x%x, reg 0xFF38: 0x%x\n",
		reg1, reg2);
	if ((reg1 & 0xC0) && (reg2 & 0xC0)) {
		chip->sd_int = 1;
		rtsx_write_register(chip, SDIO_CTRL, 0xFF,
				    SDIO_BUS_CTRL | SDIO_CD_CTRL);
		rtsx_write_register(chip, PWR_GATE_CTRL,
				    LDO3318_PWR_MASK, LDO_ON);
	}
}

#ifdef SUPPORT_SDIO_ASPM
void dynamic_configure_sdio_aspm(struct rtsx_chip *chip)
{
	u8 buf[12], reg;
	int i;

	for (i = 0; i < 12; i++)
		rtsx_read_register(chip, 0xFF08 + i, &buf[i]);
	rtsx_read_register(chip, 0xFF25, &reg);
	if ((memcmp(buf, chip->sdio_raw_data, 12) != 0) || (reg & 0x03)) {
		chip->sdio_counter = 0;
		chip->sdio_idle = 0;
	} else {
		if (!chip->sdio_idle) {
			chip->sdio_counter++;
			if (chip->sdio_counter >= SDIO_IDLE_COUNT) {
				chip->sdio_counter = 0;
				chip->sdio_idle = 1;
			}
		}
	}
	memcpy(chip->sdio_raw_data, buf, 12);

	if (chip->sdio_idle) {
		if (!chip->sdio_aspm) {
			dev_dbg(rtsx_dev(chip), "SDIO enter ASPM!\n");
			rtsx_write_register(chip, ASPM_FORCE_CTL, 0xFC,
					    0x30 | (chip->aspm_level[1] << 2));
			chip->sdio_aspm = 1;
		}
	} else {
		if (chip->sdio_aspm) {
			dev_dbg(rtsx_dev(chip), "SDIO exit ASPM!\n");
			rtsx_write_register(chip, ASPM_FORCE_CTL, 0xFC, 0x30);
			chip->sdio_aspm = 0;
		}
	}
}
#endif

void do_reset_sd_card(struct rtsx_chip *chip)
{
	int retval;

	dev_dbg(rtsx_dev(chip), "%s: %d, card2lun = 0x%x\n", __func__,
		chip->sd_reset_counter, chip->card2lun[SD_CARD]);

	if (chip->card2lun[SD_CARD] >= MAX_ALLOWED_LUN_CNT) {
		clear_bit(SD_NR, &chip->need_reset);
		chip->sd_reset_counter = 0;
		chip->sd_show_cnt = 0;
		return;
	}

	chip->rw_fail_cnt[chip->card2lun[SD_CARD]] = 0;

	rtsx_set_stat(chip, RTSX_STAT_RUN);
	rtsx_write_register(chip, SDIO_CTRL, 0xFF, 0);

	retval = reset_sd_card(chip);
	if (chip->need_release & SD_CARD)
		return;
	if (retval == STATUS_SUCCESS) {
		clear_bit(SD_NR, &chip->need_reset);
		chip->sd_reset_counter = 0;
		chip->sd_show_cnt = 0;
		chip->card_ready |= SD_CARD;
		chip->card_fail &= ~SD_CARD;
		chip->rw_card[chip->card2lun[SD_CARD]] = sd_rw;
	} else {
		if (chip->sd_io || (chip->sd_reset_counter >= MAX_RESET_CNT)) {
			clear_bit(SD_NR, &chip->need_reset);
			chip->sd_reset_counter = 0;
			chip->sd_show_cnt = 0;
		} else {
			chip->sd_reset_counter++;
		}
		chip->card_ready &= ~SD_CARD;
		chip->card_fail |= SD_CARD;
		chip->capacity[chip->card2lun[SD_CARD]] = 0;
		chip->rw_card[chip->card2lun[SD_CARD]] = NULL;

		rtsx_write_register(chip, CARD_OE, SD_OUTPUT_EN, 0);
		if (!chip->ft2_fast_mode)
			card_power_off(chip, SD_CARD);
		if (chip->sd_io) {
			chip->sd_int = 0;
			try_to_switch_sdio_ctrl(chip);
		} else {
			disable_card_clock(chip, SD_CARD);
		}
	}
}

void do_reset_xd_card(struct rtsx_chip *chip)
{
	int retval;

	dev_dbg(rtsx_dev(chip), "%s: %d, card2lun = 0x%x\n", __func__,
		chip->xd_reset_counter, chip->card2lun[XD_CARD]);

	if (chip->card2lun[XD_CARD] >= MAX_ALLOWED_LUN_CNT) {
		clear_bit(XD_NR, &chip->need_reset);
		chip->xd_reset_counter = 0;
		chip->xd_show_cnt = 0;
		return;
	}

	chip->rw_fail_cnt[chip->card2lun[XD_CARD]] = 0;

	rtsx_set_stat(chip, RTSX_STAT_RUN);
	rtsx_write_register(chip, SDIO_CTRL, 0xFF, 0);

	retval = reset_xd_card(chip);
	if (chip->need_release & XD_CARD)
		return;
	if (retval == STATUS_SUCCESS) {
		clear_bit(XD_NR, &chip->need_reset);
		chip->xd_reset_counter = 0;
		chip->card_ready |= XD_CARD;
		chip->card_fail &= ~XD_CARD;
		chip->rw_card[chip->card2lun[XD_CARD]] = xd_rw;
	} else {
		if (chip->xd_reset_counter >= MAX_RESET_CNT) {
			clear_bit(XD_NR, &chip->need_reset);
			chip->xd_reset_counter = 0;
			chip->xd_show_cnt = 0;
		} else {
			chip->xd_reset_counter++;
		}
		chip->card_ready &= ~XD_CARD;
		chip->card_fail |= XD_CARD;
		chip->capacity[chip->card2lun[XD_CARD]] = 0;
		chip->rw_card[chip->card2lun[XD_CARD]] = NULL;

		rtsx_write_register(chip, CARD_OE, XD_OUTPUT_EN, 0);
		if (!chip->ft2_fast_mode)
			card_power_off(chip, XD_CARD);
		disable_card_clock(chip, XD_CARD);
	}
}

void do_reset_ms_card(struct rtsx_chip *chip)
{
	int retval;

	dev_dbg(rtsx_dev(chip), "%s: %d, card2lun = 0x%x\n", __func__,
		chip->ms_reset_counter, chip->card2lun[MS_CARD]);

	if (chip->card2lun[MS_CARD] >= MAX_ALLOWED_LUN_CNT) {
		clear_bit(MS_NR, &chip->need_reset);
		chip->ms_reset_counter = 0;
		chip->ms_show_cnt = 0;
		return;
	}

	chip->rw_fail_cnt[chip->card2lun[MS_CARD]] = 0;

	rtsx_set_stat(chip, RTSX_STAT_RUN);
	rtsx_write_register(chip, SDIO_CTRL, 0xFF, 0);

	retval = reset_ms_card(chip);
	if (chip->need_release & MS_CARD)
		return;
	if (retval == STATUS_SUCCESS) {
		clear_bit(MS_NR, &chip->need_reset);
		chip->ms_reset_counter = 0;
		chip->card_ready |= MS_CARD;
		chip->card_fail &= ~MS_CARD;
		chip->rw_card[chip->card2lun[MS_CARD]] = ms_rw;
	} else {
		if (chip->ms_reset_counter >= MAX_RESET_CNT) {
			clear_bit(MS_NR, &chip->need_reset);
			chip->ms_reset_counter = 0;
			chip->ms_show_cnt = 0;
		} else {
			chip->ms_reset_counter++;
		}
		chip->card_ready &= ~MS_CARD;
		chip->card_fail |= MS_CARD;
		chip->capacity[chip->card2lun[MS_CARD]] = 0;
		chip->rw_card[chip->card2lun[MS_CARD]] = NULL;

		rtsx_write_register(chip, CARD_OE, MS_OUTPUT_EN, 0);
		if (!chip->ft2_fast_mode)
			card_power_off(chip, MS_CARD);
		disable_card_clock(chip, MS_CARD);
	}
}

static void release_sdio(struct rtsx_chip *chip)
{
	if (chip->sd_io) {
		rtsx_write_register(chip, CARD_STOP, SD_STOP | SD_CLR_ERR,
				    SD_STOP | SD_CLR_ERR);

		if (chip->chip_insert_with_sdio) {
			chip->chip_insert_with_sdio = 0;

			if (CHECK_PID(chip, 0x5288))
				rtsx_write_register(chip, 0xFE5A, 0x08, 0x00);
			else
				rtsx_write_register(chip, 0xFE70, 0x80, 0x00);
		}

		rtsx_write_register(chip, SDIO_CTRL, SDIO_CD_CTRL, 0);
		chip->sd_io = 0;
	}
}

void rtsx_power_off_card(struct rtsx_chip *chip)
{
	if ((chip->card_ready & SD_CARD) || chip->sd_io) {
		sd_cleanup_work(chip);
		sd_power_off_card3v3(chip);
	}

	if (chip->card_ready & XD_CARD) {
		xd_cleanup_work(chip);
		xd_power_off_card3v3(chip);
	}

	if (chip->card_ready & MS_CARD) {
		ms_cleanup_work(chip);
		ms_power_off_card3v3(chip);
	}
}

void rtsx_release_cards(struct rtsx_chip *chip)
{
	chip->int_reg = rtsx_readl(chip, RTSX_BIPR);

	if ((chip->card_ready & SD_CARD) || chip->sd_io) {
		if (chip->int_reg & SD_EXIST)
			sd_cleanup_work(chip);
		release_sd_card(chip);
	}

	if (chip->card_ready & XD_CARD) {
		if (chip->int_reg & XD_EXIST)
			xd_cleanup_work(chip);
		release_xd_card(chip);
	}

	if (chip->card_ready & MS_CARD) {
		if (chip->int_reg & MS_EXIST)
			ms_cleanup_work(chip);
		release_ms_card(chip);
	}
}

void rtsx_reset_cards(struct rtsx_chip *chip)
{
	if (!chip->need_reset)
		return;

	rtsx_set_stat(chip, RTSX_STAT_RUN);

	rtsx_force_power_on(chip, SSC_PDCTL | OC_PDCTL);

	rtsx_disable_aspm(chip);

	if ((chip->need_reset & SD_CARD) && chip->chip_insert_with_sdio)
		clear_bit(SD_NR, &chip->need_reset);

	if (chip->need_reset & XD_CARD) {
		chip->card_exist |= XD_CARD;

		if (chip->xd_show_cnt >= MAX_SHOW_CNT)
			do_reset_xd_card(chip);
		else
			chip->xd_show_cnt++;
	}
	if (CHECK_PID(chip, 0x5288) && CHECK_BARO_PKG(chip, QFN)) {
		if (chip->card_exist & XD_CARD) {
			clear_bit(SD_NR, &chip->need_reset);
			clear_bit(MS_NR, &chip->need_reset);
		}
	}
	if (chip->need_reset & SD_CARD) {
		chip->card_exist |= SD_CARD;

		if (chip->sd_show_cnt >= MAX_SHOW_CNT) {
			rtsx_write_register(chip, RBCTL, RB_FLUSH, RB_FLUSH);
			do_reset_sd_card(chip);
		} else {
			chip->sd_show_cnt++;
		}
	}
	if (chip->need_reset & MS_CARD) {
		chip->card_exist |= MS_CARD;

		if (chip->ms_show_cnt >= MAX_SHOW_CNT)
			do_reset_ms_card(chip);
		else
			chip->ms_show_cnt++;
	}
}

void rtsx_reinit_cards(struct rtsx_chip *chip, int reset_chip)
{
	rtsx_set_stat(chip, RTSX_STAT_RUN);

	rtsx_force_power_on(chip, SSC_PDCTL | OC_PDCTL);

	if (reset_chip)
		rtsx_reset_chip(chip);

	chip->int_reg = rtsx_readl(chip, RTSX_BIPR);

	if ((chip->int_reg & SD_EXIST) && (chip->need_reinit & SD_CARD)) {
		release_sdio(chip);
		release_sd_card(chip);

		wait_timeout(100);

		chip->card_exist |= SD_CARD;
		do_reset_sd_card(chip);
	}

	if ((chip->int_reg & XD_EXIST) && (chip->need_reinit & XD_CARD)) {
		release_xd_card(chip);

		wait_timeout(100);

		chip->card_exist |= XD_CARD;
		do_reset_xd_card(chip);
	}

	if ((chip->int_reg & MS_EXIST) && (chip->need_reinit & MS_CARD)) {
		release_ms_card(chip);

		wait_timeout(100);

		chip->card_exist |= MS_CARD;
		do_reset_ms_card(chip);
	}

	chip->need_reinit = 0;
}

#ifdef DISABLE_CARD_INT
void card_cd_debounce(struct rtsx_chip *chip, unsigned long *need_reset,
		      unsigned long *need_release)
{
	u8 release_map = 0, reset_map = 0;

	chip->int_reg = rtsx_readl(chip, RTSX_BIPR);

	if (chip->card_exist) {
		if (chip->card_exist & XD_CARD) {
			if (!(chip->int_reg & XD_EXIST))
				release_map |= XD_CARD;
		} else if (chip->card_exist & SD_CARD) {
			if (!(chip->int_reg & SD_EXIST))
				release_map |= SD_CARD;
		} else if (chip->card_exist & MS_CARD) {
			if (!(chip->int_reg & MS_EXIST))
				release_map |= MS_CARD;
		}
	} else {
		if (chip->int_reg & XD_EXIST)
			reset_map |= XD_CARD;
		else if (chip->int_reg & SD_EXIST)
			reset_map |= SD_CARD;
		else if (chip->int_reg & MS_EXIST)
			reset_map |= MS_CARD;
	}

	if (reset_map) {
		int xd_cnt = 0, sd_cnt = 0, ms_cnt = 0;
		int i;

		for (i = 0; i < (DEBOUNCE_CNT); i++) {
			chip->int_reg = rtsx_readl(chip, RTSX_BIPR);

			if (chip->int_reg & XD_EXIST)
				xd_cnt++;
			else
				xd_cnt = 0;

			if (chip->int_reg & SD_EXIST)
				sd_cnt++;
			else
				sd_cnt = 0;

			if (chip->int_reg & MS_EXIST)
				ms_cnt++;
			else
				ms_cnt = 0;

			wait_timeout(30);
		}

		reset_map = 0;
		if (!(chip->card_exist & XD_CARD) &&
		    (xd_cnt > (DEBOUNCE_CNT - 1)))
			reset_map |= XD_CARD;
		if (!(chip->card_exist & SD_CARD) &&
		    (sd_cnt > (DEBOUNCE_CNT - 1)))
			reset_map |= SD_CARD;
		if (!(chip->card_exist & MS_CARD) &&
		    (ms_cnt > (DEBOUNCE_CNT - 1)))
			reset_map |= MS_CARD;
	}

	if (CHECK_PID(chip, 0x5288) && CHECK_BARO_PKG(chip, QFN))
		rtsx_write_register(chip, HOST_SLEEP_STATE, 0xC0, 0x00);

	if (need_reset)
		*need_reset = reset_map;
	if (need_release)
		*need_release = release_map;
}
#endif

void rtsx_init_cards(struct rtsx_chip *chip)
{
	if (RTSX_TST_DELINK(chip) && (rtsx_get_stat(chip) != RTSX_STAT_SS)) {
		dev_dbg(rtsx_dev(chip), "Reset chip in polling thread!\n");
		rtsx_reset_chip(chip);
		RTSX_CLR_DELINK(chip);
	}

#ifdef DISABLE_CARD_INT
	card_cd_debounce(chip, &chip->need_reset, &chip->need_release);
#endif

	if (chip->need_release) {
		if (CHECK_PID(chip, 0x5288) && CHECK_BARO_PKG(chip, QFN)) {
			if (chip->int_reg & XD_EXIST) {
				clear_bit(SD_NR, &chip->need_release);
				clear_bit(MS_NR, &chip->need_release);
			}
		}

		if (!(chip->card_exist & SD_CARD) && !chip->sd_io)
			clear_bit(SD_NR, &chip->need_release);
		if (!(chip->card_exist & XD_CARD))
			clear_bit(XD_NR, &chip->need_release);
		if (!(chip->card_exist & MS_CARD))
			clear_bit(MS_NR, &chip->need_release);

		dev_dbg(rtsx_dev(chip), "chip->need_release = 0x%x\n",
			(unsigned int)(chip->need_release));

#ifdef SUPPORT_OCP
		if (chip->need_release) {
			if (chip->ocp_stat & (CARD_OC_NOW | CARD_OC_EVER))
				rtsx_write_register(chip, OCPCLR,
						    CARD_OC_INT_CLR |
						    CARD_OC_CLR,
						    CARD_OC_INT_CLR |
						    CARD_OC_CLR);
			chip->ocp_stat = 0;
		}
#endif
		if (chip->need_release) {
			rtsx_set_stat(chip, RTSX_STAT_RUN);
			rtsx_force_power_on(chip, SSC_PDCTL | OC_PDCTL);
		}

		if (chip->need_release & SD_CARD) {
			clear_bit(SD_NR, &chip->need_release);
			chip->card_exist &= ~SD_CARD;
			chip->card_ejected &= ~SD_CARD;
			chip->card_fail &= ~SD_CARD;
			CLR_BIT(chip->lun_mc, chip->card2lun[SD_CARD]);
			chip->rw_fail_cnt[chip->card2lun[SD_CARD]] = 0;
			rtsx_write_register(chip, RBCTL, RB_FLUSH, RB_FLUSH);

			release_sdio(chip);
			release_sd_card(chip);
		}

		if (chip->need_release & XD_CARD) {
			clear_bit(XD_NR, &chip->need_release);
			chip->card_exist &= ~XD_CARD;
			chip->card_ejected &= ~XD_CARD;
			chip->card_fail &= ~XD_CARD;
			CLR_BIT(chip->lun_mc, chip->card2lun[XD_CARD]);
			chip->rw_fail_cnt[chip->card2lun[XD_CARD]] = 0;

			release_xd_card(chip);

			if (CHECK_PID(chip, 0x5288) &&
			    CHECK_BARO_PKG(chip, QFN))
				rtsx_write_register(chip, HOST_SLEEP_STATE,
						    0xC0, 0xC0);
		}

		if (chip->need_release & MS_CARD) {
			clear_bit(MS_NR, &chip->need_release);
			chip->card_exist &= ~MS_CARD;
			chip->card_ejected &= ~MS_CARD;
			chip->card_fail &= ~MS_CARD;
			CLR_BIT(chip->lun_mc, chip->card2lun[MS_CARD]);
			chip->rw_fail_cnt[chip->card2lun[MS_CARD]] = 0;

			release_ms_card(chip);
		}

		dev_dbg(rtsx_dev(chip), "chip->card_exist = 0x%x\n",
			chip->card_exist);

		if (!chip->card_exist)
			turn_off_led(chip, LED_GPIO);
	}

	if (chip->need_reset) {
		dev_dbg(rtsx_dev(chip), "chip->need_reset = 0x%x\n",
			(unsigned int)(chip->need_reset));

		rtsx_reset_cards(chip);
	}

	if (chip->need_reinit) {
		dev_dbg(rtsx_dev(chip), "chip->need_reinit = 0x%x\n",
			(unsigned int)(chip->need_reinit));

		rtsx_reinit_cards(chip, 0);
	}
}

int switch_ssc_clock(struct rtsx_chip *chip, int clk)
{
	int retval;
	u8 n = (u8)(clk - 2), min_n, max_n;
	u8 mcu_cnt, div, max_div, ssc_depth, ssc_depth_mask;
	int sd_vpclk_phase_reset = 0;

	if (chip->cur_clk == clk)
		return STATUS_SUCCESS;

	min_n = 60;
	max_n = 120;
	max_div = CLK_DIV_4;

	dev_dbg(rtsx_dev(chip), "Switch SSC clock to %dMHz (cur_clk = %d)\n",
		clk, chip->cur_clk);

	if ((clk <= 2) || (n > max_n))
		return STATUS_FAIL;

	mcu_cnt = (u8)(125 / clk + 3);
	if (mcu_cnt > 7)
		mcu_cnt = 7;

	div = CLK_DIV_1;
	while ((n < min_n) && (div < max_div)) {
		n = (n + 2) * 2 - 2;
		div++;
	}
	dev_dbg(rtsx_dev(chip), "n = %d, div = %d\n", n, div);

	if (chip->ssc_en) {
		ssc_depth = 0x01;
		n -= 2;
	} else {
		ssc_depth = 0;
	}

	ssc_depth_mask = 0x03;

	dev_dbg(rtsx_dev(chip), "ssc_depth = %d\n", ssc_depth);

	rtsx_init_cmd(chip);
	rtsx_add_cmd(chip, WRITE_REG_CMD, CLK_CTL, CLK_LOW_FREQ, CLK_LOW_FREQ);
	rtsx_add_cmd(chip, WRITE_REG_CMD, CLK_DIV, 0xFF, (div << 4) | mcu_cnt);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SSC_CTL1, SSC_RSTB, 0);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SSC_CTL2, ssc_depth_mask, ssc_depth);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SSC_DIV_N_0, 0xFF, n);
	rtsx_add_cmd(chip, WRITE_REG_CMD, SSC_CTL1, SSC_RSTB, SSC_RSTB);
	if (sd_vpclk_phase_reset) {
		rtsx_add_cmd(chip, WRITE_REG_CMD, SD_VPCLK0_CTL,
			     PHASE_NOT_RESET, 0);
		rtsx_add_cmd(chip, WRITE_REG_CMD, SD_VPCLK0_CTL,
			     PHASE_NOT_RESET, PHASE_NOT_RESET);
	}

	retval = rtsx_send_cmd(chip, 0, WAIT_TIME);
	if (retval < 0)
		return STATUS_ERROR;

	udelay(10);
	retval = rtsx_write_register(chip, CLK_CTL, CLK_LOW_FREQ, 0);
	if (retval)
		return retval;

	chip->cur_clk = clk;

	return STATUS_SUCCESS;
}

int switch_normal_clock(struct rtsx_chip *chip, int clk)
{
	int retval;
	u8 sel, div, mcu_cnt;
	int sd_vpclk_phase_reset = 0;

	if (chip->cur_clk == clk)
		return STATUS_SUCCESS;

	switch (clk) {
	case CLK_20:
		dev_dbg(rtsx_dev(chip), "Switch clock to 20MHz\n");
		sel = SSC_80;
		div = CLK_DIV_4;
		mcu_cnt = 7;
		break;

	case CLK_30:
		dev_dbg(rtsx_dev(chip), "Switch clock to 30MHz\n");
		sel = SSC_120;
		div = CLK_DIV_4;
		mcu_cnt = 7;
		break;

	case CLK_40:
		dev_dbg(rtsx_dev(chip), "Switch clock to 40MHz\n");
		sel = SSC_80;
		div = CLK_DIV_2;
		mcu_cnt = 7;
		break;

	case CLK_50:
		dev_dbg(rtsx_dev(chip), "Switch clock to 50MHz\n");
		sel = SSC_100;
		div = CLK_DIV_2;
		mcu_cnt = 6;
		break;

	case CLK_60:
		dev_dbg(rtsx_dev(chip), "Switch clock to 60MHz\n");
		sel = SSC_120;
		div = CLK_DIV_2;
		mcu_cnt = 6;
		break;

	case CLK_80:
		dev_dbg(rtsx_dev(chip), "Switch clock to 80MHz\n");
		sel = SSC_80;
		div = CLK_DIV_1;
		mcu_cnt = 5;
		break;

	case CLK_100:
		dev_dbg(rtsx_dev(chip), "Switch clock to 100MHz\n");
		sel = SSC_100;
		div = CLK_DIV_1;
		mcu_cnt = 5;
		break;

	case CLK_120:
		dev_dbg(rtsx_dev(chip), "Switch clock to 120MHz\n");
		sel = SSC_120;
		div = CLK_DIV_1;
		mcu_cnt = 5;
		break;

	case CLK_150:
		dev_dbg(rtsx_dev(chip), "Switch clock to 150MHz\n");
		sel = SSC_150;
		div = CLK_DIV_1;
		mcu_cnt = 4;
		break;

	case CLK_200:
		dev_dbg(rtsx_dev(chip), "Switch clock to 200MHz\n");
		sel = SSC_200;
		div = CLK_DIV_1;
		mcu_cnt = 4;
		break;

	default:
		dev_dbg(rtsx_dev(chip), "Try to switch to an illegal clock (%d)\n",
			clk);
		return STATUS_FAIL;
	}

	retval = rtsx_write_register(chip, CLK_CTL, 0xFF, CLK_LOW_FREQ);
	if (retval)
		return retval;
	if (sd_vpclk_phase_reset) {
		retval = rtsx_write_register(chip, SD_VPCLK0_CTL,
					     PHASE_NOT_RESET, 0);
		if (retval)
			return retval;
		retval = rtsx_write_register(chip, SD_VPCLK1_CTL,
					     PHASE_NOT_RESET, 0);
		if (retval)
			return retval;
	}
	retval = rtsx_write_register(chip, CLK_DIV, 0xFF,
				     (div << 4) | mcu_cnt);
	if (retval)
		return retval;
	retval = rtsx_write_register(chip, CLK_SEL, 0xFF, sel);
	if (retval)
		return retval;

	if (sd_vpclk_phase_reset) {
		udelay(200);
		retval = rtsx_write_register(chip, SD_VPCLK0_CTL,
					     PHASE_NOT_RESET, PHASE_NOT_RESET);
		if (retval)
			return retval;
		retval = rtsx_write_register(chip, SD_VPCLK1_CTL,
					     PHASE_NOT_RESET, PHASE_NOT_RESET);
		if (retval)
			return retval;
		udelay(200);
	}
	retval = rtsx_write_register(chip, CLK_CTL, 0xFF, 0);
	if (retval)
		return retval;

	chip->cur_clk = clk;

	return STATUS_SUCCESS;
}

void trans_dma_enable(enum dma_data_direction dir, struct rtsx_chip *chip,
		      u32 byte_cnt, u8 pack_size)
{
	if (pack_size > DMA_1024)
		pack_size = DMA_512;

	rtsx_add_cmd(chip, WRITE_REG_CMD, IRQSTAT0, DMA_DONE_INT, DMA_DONE_INT);

	rtsx_add_cmd(chip, WRITE_REG_CMD, DMATC3, 0xFF, (u8)(byte_cnt >> 24));
	rtsx_add_cmd(chip, WRITE_REG_CMD, DMATC2, 0xFF, (u8)(byte_cnt >> 16));
	rtsx_add_cmd(chip, WRITE_REG_CMD, DMATC1, 0xFF, (u8)(byte_cnt >> 8));
	rtsx_add_cmd(chip, WRITE_REG_CMD, DMATC0, 0xFF, (u8)byte_cnt);

	if (dir == DMA_FROM_DEVICE) {
		rtsx_add_cmd(chip, WRITE_REG_CMD, DMACTL,
			     0x03 | DMA_PACK_SIZE_MASK,
			     DMA_DIR_FROM_CARD | DMA_EN | pack_size);
	} else {
		rtsx_add_cmd(chip, WRITE_REG_CMD, DMACTL,
			     0x03 | DMA_PACK_SIZE_MASK,
			     DMA_DIR_TO_CARD | DMA_EN | pack_size);
	}

	rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_DATA_SOURCE, 0x01, RING_BUFFER);
}

int enable_card_clock(struct rtsx_chip *chip, u8 card)
{
	int retval;
	u8 clk_en = 0;

	if (card & XD_CARD)
		clk_en |= XD_CLK_EN;
	if (card & SD_CARD)
		clk_en |= SD_CLK_EN;
	if (card & MS_CARD)
		clk_en |= MS_CLK_EN;

	retval = rtsx_write_register(chip, CARD_CLK_EN, clk_en, clk_en);
	if (retval)
		return retval;

	return STATUS_SUCCESS;
}

int disable_card_clock(struct rtsx_chip *chip, u8 card)
{
	int retval;
	u8 clk_en = 0;

	if (card & XD_CARD)
		clk_en |= XD_CLK_EN;
	if (card & SD_CARD)
		clk_en |= SD_CLK_EN;
	if (card & MS_CARD)
		clk_en |= MS_CLK_EN;

	retval = rtsx_write_register(chip, CARD_CLK_EN, clk_en, 0);
	if (retval)
		return retval;

	return STATUS_SUCCESS;
}

int card_power_on(struct rtsx_chip *chip, u8 card)
{
	int retval;
	u8 mask, val1, val2;

	if (CHECK_LUN_MODE(chip, SD_MS_2LUN) && (card == MS_CARD)) {
		mask = MS_POWER_MASK;
		val1 = MS_PARTIAL_POWER_ON;
		val2 = MS_POWER_ON;
	} else {
		mask = SD_POWER_MASK;
		val1 = SD_PARTIAL_POWER_ON;
		val2 = SD_POWER_ON;
	}

	rtsx_init_cmd(chip);
	rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PWR_CTL, mask, val1);

	retval = rtsx_send_cmd(chip, 0, 100);
	if (retval != STATUS_SUCCESS)
		return STATUS_FAIL;

	udelay(chip->pmos_pwr_on_interval);

	rtsx_init_cmd(chip);
	rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PWR_CTL, mask, val2);

	retval = rtsx_send_cmd(chip, 0, 100);
	if (retval != STATUS_SUCCESS)
		return STATUS_FAIL;

	return STATUS_SUCCESS;
}

int card_power_off(struct rtsx_chip *chip, u8 card)
{
	int retval;
	u8 mask, val;

	if (CHECK_LUN_MODE(chip, SD_MS_2LUN) && (card == MS_CARD)) {
		mask = MS_POWER_MASK;
		val = MS_POWER_OFF;
	} else {
		mask = SD_POWER_MASK;
		val = SD_POWER_OFF;
	}

	retval = rtsx_write_register(chip, CARD_PWR_CTL, mask, val);
	if (retval)
		return retval;

	return STATUS_SUCCESS;
}

int card_rw(struct scsi_cmnd *srb, struct rtsx_chip *chip,
	    u32 sec_addr, u16 sec_cnt)
{
	int retval;
	unsigned int lun = SCSI_LUN(srb);
	int i;

	if (!chip->rw_card[lun])
		return STATUS_FAIL;

	for (i = 0; i < 3; i++) {
		chip->rw_need_retry = 0;

		retval = chip->rw_card[lun](srb, chip, sec_addr, sec_cnt);
		if (retval != STATUS_SUCCESS) {
			if (rtsx_check_chip_exist(chip) != STATUS_SUCCESS) {
				rtsx_release_chip(chip);
				return STATUS_FAIL;
			}
			if (detect_card_cd(chip, chip->cur_card) !=
							STATUS_SUCCESS) {
				return STATUS_FAIL;
			}

			if (!chip->rw_need_retry) {
				dev_dbg(rtsx_dev(chip), "RW fail, but no need to retry\n");
				break;
			}
		} else {
			chip->rw_need_retry = 0;
			break;
		}

		dev_dbg(rtsx_dev(chip), "Retry RW, (i = %d)\n", i);
	}

	return retval;
}

int card_share_mode(struct rtsx_chip *chip, int card)
{
	int retval;
	u8 mask, value;

	if (CHECK_PID(chip, 0x5208)) {
		mask = CARD_SHARE_MASK;
		if (card == SD_CARD)
			value = CARD_SHARE_48_SD;
		else if (card == MS_CARD)
			value = CARD_SHARE_48_MS;
		else if (card == XD_CARD)
			value = CARD_SHARE_48_XD;
		else
			return STATUS_FAIL;

	} else if (CHECK_PID(chip, 0x5288)) {
		mask = 0x03;
		if (card == SD_CARD)
			value = CARD_SHARE_BAROSSA_SD;
		else if (card == MS_CARD)
			value = CARD_SHARE_BAROSSA_MS;
		else if (card == XD_CARD)
			value = CARD_SHARE_BAROSSA_XD;
		else
			return STATUS_FAIL;

	} else {
		return STATUS_FAIL;
	}

	retval = rtsx_write_register(chip, CARD_SHARE_MODE, mask, value);
	if (retval)
		return retval;

	return STATUS_SUCCESS;
}

int select_card(struct rtsx_chip *chip, int card)
{
	int retval;

	if (chip->cur_card != card) {
		u8 mod;

		if (card == SD_CARD)
			mod = SD_MOD_SEL;
		else if (card == MS_CARD)
			mod = MS_MOD_SEL;
		else if (card == XD_CARD)
			mod = XD_MOD_SEL;
		else if (card == SPI_CARD)
			mod = SPI_MOD_SEL;
		else
			return STATUS_FAIL;

		retval = rtsx_write_register(chip, CARD_SELECT, 0x07, mod);
		if (retval)
			return retval;
		chip->cur_card = card;

		retval =  card_share_mode(chip, card);
		if (retval != STATUS_SUCCESS)
			return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}

void toggle_gpio(struct rtsx_chip *chip, u8 gpio)
{
	u8 temp_reg;

	rtsx_read_register(chip, CARD_GPIO, &temp_reg);
	temp_reg ^= (0x01 << gpio);
	rtsx_write_register(chip, CARD_GPIO, 0xFF, temp_reg);
}

void turn_on_led(struct rtsx_chip *chip, u8 gpio)
{
	if (CHECK_PID(chip, 0x5288))
		rtsx_write_register(chip, CARD_GPIO, (u8)(1 << gpio),
				    (u8)(1 << gpio));
	else
		rtsx_write_register(chip, CARD_GPIO, (u8)(1 << gpio), 0);
}

void turn_off_led(struct rtsx_chip *chip, u8 gpio)
{
	if (CHECK_PID(chip, 0x5288))
		rtsx_write_register(chip, CARD_GPIO, (u8)(1 << gpio), 0);
	else
		rtsx_write_register(chip, CARD_GPIO, (u8)(1 << gpio),
				    (u8)(1 << gpio));
}

int detect_card_cd(struct rtsx_chip *chip, int card)
{
	u32 card_cd, status;

	if (card == SD_CARD) {
		card_cd = SD_EXIST;
	} else if (card == MS_CARD) {
		card_cd = MS_EXIST;
	} else if (card == XD_CARD) {
		card_cd = XD_EXIST;
	} else {
		dev_dbg(rtsx_dev(chip), "Wrong card type: 0x%x\n", card);
		return STATUS_FAIL;
	}

	status = rtsx_readl(chip, RTSX_BIPR);
	if (!(status & card_cd))
		return STATUS_FAIL;

	return STATUS_SUCCESS;
}

int check_card_exist(struct rtsx_chip *chip, unsigned int lun)
{
	if (chip->card_exist & chip->lun2card[lun])
		return 1;

	return 0;
}

int check_card_ready(struct rtsx_chip *chip, unsigned int lun)
{
	if (chip->card_ready & chip->lun2card[lun])
		return 1;

	return 0;
}

int check_card_wp(struct rtsx_chip *chip, unsigned int lun)
{
	if (chip->card_wp & chip->lun2card[lun])
		return 1;

	return 0;
}

u8 get_lun_card(struct rtsx_chip *chip, unsigned int lun)
{
	if ((chip->card_ready & chip->lun2card[lun]) == XD_CARD)
		return (u8)XD_CARD;
	else if ((chip->card_ready & chip->lun2card[lun]) == SD_CARD)
		return (u8)SD_CARD;
	else if ((chip->card_ready & chip->lun2card[lun]) == MS_CARD)
		return (u8)MS_CARD;

	return 0;
}

void eject_card(struct rtsx_chip *chip, unsigned int lun)
{
	do_remaining_work(chip);

	if ((chip->card_ready & chip->lun2card[lun]) == SD_CARD) {
		release_sd_card(chip);
		chip->card_ejected |= SD_CARD;
		chip->card_ready &= ~SD_CARD;
		chip->capacity[lun] = 0;
	} else if ((chip->card_ready & chip->lun2card[lun]) == XD_CARD) {
		release_xd_card(chip);
		chip->card_ejected |= XD_CARD;
		chip->card_ready &= ~XD_CARD;
		chip->capacity[lun] = 0;
	} else if ((chip->card_ready & chip->lun2card[lun]) == MS_CARD) {
		release_ms_card(chip);
		chip->card_ejected |= MS_CARD;
		chip->card_ready &= ~MS_CARD;
		chip->capacity[lun] = 0;
	}
}
