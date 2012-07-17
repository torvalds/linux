/* Driver for Realtek RTS51xx USB card reader
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
 *   wwang (wei_wang@realsil.com.cn)
 *   No. 450, Shenhu Road, Suzhou Industry Park, Suzhou, China
 * Maintainer:
 *   Edwin Rong (edwin_rong@realsil.com.cn)
 *   No. 450, Shenhu Road, Suzhou Industry Park, Suzhou, China
 */

#include <linux/blkdev.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/workqueue.h>

#include <scsi/scsi.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_device.h>

#include "debug.h"
#include "rts51x.h"
#include "rts51x_chip.h"
#include "rts51x_card.h"
#include "rts51x_transport.h"
#include "xd.h"
#include "sd.h"
#include "ms.h"

void do_remaining_work(struct rts51x_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	struct xd_info *xd_card = &(chip->xd_card);
	struct ms_info *ms_card = &(chip->ms_card);

	if (chip->card_ready & SD_CARD) {
		if (sd_card->seq_mode) {
			RTS51X_SET_STAT(chip, STAT_RUN);
			sd_card->counter++;
		} else {
			sd_card->counter = 0;
		}
	}

	if (chip->card_ready & XD_CARD) {
		if (xd_card->delay_write.delay_write_flag) {
			RTS51X_SET_STAT(chip, STAT_RUN);
			xd_card->counter++;
		} else {
			xd_card->counter = 0;
		}
	}

	if (chip->card_ready & MS_CARD) {
		if (CHK_MSPRO(ms_card)) {
			if (ms_card->seq_mode) {
				RTS51X_SET_STAT(chip, STAT_RUN);
				ms_card->counter++;
			} else {
				ms_card->counter = 0;
			}
		} else {
			if (ms_card->delay_write.delay_write_flag) {
				RTS51X_SET_STAT(chip, STAT_RUN);
				ms_card->counter++;
			} else {
				ms_card->counter = 0;
			}
		}
	}

	if (sd_card->counter > POLLING_WAIT_CNT)
		sd_cleanup_work(chip);

	if (xd_card->counter > POLLING_WAIT_CNT)
		xd_cleanup_work(chip);

	if (ms_card->counter > POLLING_WAIT_CNT)
		ms_cleanup_work(chip);
}

static void do_reset_xd_card(struct rts51x_chip *chip)
{
	int retval;

	if (chip->card2lun[XD_CARD] >= MAX_ALLOWED_LUN_CNT)
		return;

	retval = reset_xd_card(chip);
	if (retval == STATUS_SUCCESS) {
		chip->card_ready |= XD_CARD;
		chip->card_fail &= ~XD_CARD;
		chip->rw_card[chip->card2lun[XD_CARD]] = xd_rw;
	} else {
		chip->card_ready &= ~XD_CARD;
		chip->card_fail |= XD_CARD;
		chip->capacity[chip->card2lun[XD_CARD]] = 0;
		chip->rw_card[chip->card2lun[XD_CARD]] = NULL;

		rts51x_init_cmd(chip);
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_OE, XD_OUTPUT_EN, 0);
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_PWR_CTL, POWER_MASK,
			       POWER_OFF);
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_CLK_EN, XD_CLK_EN, 0);
		rts51x_send_cmd(chip, MODE_C, 100);
	}
}

void do_reset_sd_card(struct rts51x_chip *chip)
{
	int retval;

	if (chip->card2lun[SD_CARD] >= MAX_ALLOWED_LUN_CNT)
		return;

	retval = reset_sd_card(chip);
	if (retval == STATUS_SUCCESS) {
		chip->card_ready |= SD_CARD;
		chip->card_fail &= ~SD_CARD;
		chip->rw_card[chip->card2lun[SD_CARD]] = sd_rw;
	} else {
		chip->card_ready &= ~SD_CARD;
		chip->card_fail |= SD_CARD;
		chip->capacity[chip->card2lun[SD_CARD]] = 0;
		chip->rw_card[chip->card2lun[SD_CARD]] = NULL;

		rts51x_init_cmd(chip);
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_OE, SD_OUTPUT_EN, 0);
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_PWR_CTL, POWER_MASK,
			       POWER_OFF);
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_CLK_EN, SD_CLK_EN, 0);
		rts51x_send_cmd(chip, MODE_C, 100);
	}
}

static void do_reset_ms_card(struct rts51x_chip *chip)
{
	int retval;

	if (chip->card2lun[MS_CARD] >= MAX_ALLOWED_LUN_CNT)
		return;

	retval = reset_ms_card(chip);
	if (retval == STATUS_SUCCESS) {
		chip->card_ready |= MS_CARD;
		chip->card_fail &= ~MS_CARD;
		chip->rw_card[chip->card2lun[MS_CARD]] = ms_rw;
	} else {
		chip->card_ready &= ~MS_CARD;
		chip->card_fail |= MS_CARD;
		chip->capacity[chip->card2lun[MS_CARD]] = 0;
		chip->rw_card[chip->card2lun[MS_CARD]] = NULL;

		rts51x_init_cmd(chip);
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_OE, MS_OUTPUT_EN, 0);
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_PWR_CTL, POWER_MASK,
			       POWER_OFF);
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_CLK_EN, MS_CLK_EN, 0);
		rts51x_send_cmd(chip, MODE_C, 100);
	}
}

static void card_cd_debounce(struct rts51x_chip *chip, u8 *need_reset,
		      u8 *need_release)
{
	int retval;
	u8 release_map = 0, reset_map = 0;
	u8 value;

	retval = rts51x_get_card_status(chip, &(chip->card_status));
#ifdef SUPPORT_OCP
	chip->ocp_stat = (chip->card_status >> 4) & 0x03;
#endif

	if (retval != STATUS_SUCCESS)
		goto Exit_Debounce;

	if (chip->card_exist) {
		retval = rts51x_read_register(chip, CARD_INT_PEND, &value);
		if (retval != STATUS_SUCCESS) {
			rts51x_ep0_write_register(chip, MC_FIFO_CTL, FIFO_FLUSH,
						  FIFO_FLUSH);
			rts51x_ep0_write_register(chip, SFSM_ED, 0xf8, 0xf8);
			value = 0;
		}

		if (chip->card_exist & XD_CARD) {
			if (!(chip->card_status & XD_CD))
				release_map |= XD_CARD;
		} else if (chip->card_exist & SD_CARD) {
			/* if (!(chip->card_status & SD_CD)) { */
			if (!(chip->card_status & SD_CD) || (value & SD_INT))
				release_map |= SD_CARD;
		} else if (chip->card_exist & MS_CARD) {
			/* if (!(chip->card_status & MS_CD)) { */
			if (!(chip->card_status & MS_CD) || (value & MS_INT))
				release_map |= MS_CARD;
		}
	} else {
		if (chip->card_status & XD_CD)
			reset_map |= XD_CARD;
		else if (chip->card_status & SD_CD)
			reset_map |= SD_CARD;
		else if (chip->card_status & MS_CD)
			reset_map |= MS_CARD;
	}

	if (CHECK_PKG(chip, QFN24) && reset_map) {
		if (chip->card_exist & XD_CARD) {
			reset_map = 0;
			goto Exit_Debounce;
		}
	}

	if (reset_map) {
		int xd_cnt = 0, sd_cnt = 0, ms_cnt = 0;
		int i;

		for (i = 0; i < (chip->option.debounce_num); i++) {
			retval =
			    rts51x_get_card_status(chip, &(chip->card_status));
			if (retval != STATUS_SUCCESS) {
				reset_map = release_map = 0;
				goto Exit_Debounce;
			}
			if (chip->card_status & XD_CD)
				xd_cnt++;
			else
				xd_cnt = 0;
			if (chip->card_status & SD_CD)
				sd_cnt++;
			else
				sd_cnt = 0;
			if (chip->card_status & MS_CD)
				ms_cnt++;
			else
				ms_cnt = 0;
			wait_timeout(30);
		}

		reset_map = 0;
		if (!(chip->card_exist & XD_CARD)
		    && (xd_cnt > (chip->option.debounce_num - 1))) {
			reset_map |= XD_CARD;
		}
		if (!(chip->card_exist & SD_CARD)
		    && (sd_cnt > (chip->option.debounce_num - 1))) {
			reset_map |= SD_CARD;
		}
		if (!(chip->card_exist & MS_CARD)
		    && (ms_cnt > (chip->option.debounce_num - 1))) {
			reset_map |= MS_CARD;
		}
	}
	rts51x_write_register(chip, CARD_INT_PEND, XD_INT | MS_INT | SD_INT,
			      XD_INT | MS_INT | SD_INT);

Exit_Debounce:
	if (need_reset)
		*need_reset = reset_map;
	if (need_release)
		*need_release = release_map;
}

void rts51x_init_cards(struct rts51x_chip *chip)
{
	u8 need_reset = 0, need_release = 0;

	card_cd_debounce(chip, &need_reset, &need_release);

	if (need_release) {
		RTS51X_DEBUGP("need_release = 0x%x\n", need_release);

		rts51x_prepare_run(chip);
		RTS51X_SET_STAT(chip, STAT_RUN);

#ifdef SUPPORT_OCP
		if (chip->ocp_stat & (MS_OCP_NOW | MS_OCP_EVER)) {
			rts51x_write_register(chip, OCPCTL, MS_OCP_CLEAR,
					      MS_OCP_CLEAR);
			chip->ocp_stat = 0;
			RTS51X_DEBUGP("Clear OCP status.\n");
		}
#endif

		if (need_release & XD_CARD) {
			chip->card_exist &= ~XD_CARD;
			chip->card_ejected = 0;
			if (chip->card_ready & XD_CARD) {
				release_xd_card(chip);
				chip->rw_card[chip->card2lun[XD_CARD]] = NULL;
				clear_bit(chip->card2lun[XD_CARD],
					  &(chip->lun_mc));
			}
		}

		if (need_release & SD_CARD) {
			chip->card_exist &= ~SD_CARD;
			chip->card_ejected = 0;
			if (chip->card_ready & SD_CARD) {
				release_sd_card(chip);
				chip->rw_card[chip->card2lun[SD_CARD]] = NULL;
				clear_bit(chip->card2lun[SD_CARD],
					  &(chip->lun_mc));
			}
		}

		if (need_release & MS_CARD) {
			chip->card_exist &= ~MS_CARD;
			chip->card_ejected = 0;
			if (chip->card_ready & MS_CARD) {
				release_ms_card(chip);
				chip->rw_card[chip->card2lun[MS_CARD]] = NULL;
				clear_bit(chip->card2lun[MS_CARD],
					  &(chip->lun_mc));
			}
		}
	}

	if (need_reset && !chip->card_ready) {
		RTS51X_DEBUGP("need_reset = 0x%x\n", need_reset);

		rts51x_prepare_run(chip);
		RTS51X_SET_STAT(chip, STAT_RUN);

		if (need_reset & XD_CARD) {
			chip->card_exist |= XD_CARD;
			do_reset_xd_card(chip);
		} else if (need_reset & SD_CARD) {
			chip->card_exist |= SD_CARD;
			do_reset_sd_card(chip);
		} else if (need_reset & MS_CARD) {
			chip->card_exist |= MS_CARD;
			do_reset_ms_card(chip);
		}
	}
}

void rts51x_release_cards(struct rts51x_chip *chip)
{
	if (chip->card_ready & SD_CARD) {
		sd_cleanup_work(chip);
		release_sd_card(chip);
		chip->card_ready &= ~SD_CARD;
	}

	if (chip->card_ready & XD_CARD) {
		xd_cleanup_work(chip);
		release_xd_card(chip);
		chip->card_ready &= ~XD_CARD;
	}

	if (chip->card_ready & MS_CARD) {
		ms_cleanup_work(chip);
		release_ms_card(chip);
		chip->card_ready &= ~MS_CARD;
	}
}

static inline u8 double_depth(u8 depth)
{
	return ((depth > 1) ? (depth - 1) : depth);
}

int switch_ssc_clock(struct rts51x_chip *chip, int clk)
{
	struct sd_info *sd_card = &(chip->sd_card);
	struct ms_info *ms_card = &(chip->ms_card);
	int retval;
	u8 N = (u8) (clk - 2), min_N, max_N;
	u8 mcu_cnt, div, max_div, ssc_depth;
	int sd_vpclk_phase_reset = 0;

	if (chip->cur_clk == clk)
		return STATUS_SUCCESS;

	min_N = 60;
	max_N = 120;
	max_div = CLK_DIV_4;

	RTS51X_DEBUGP("Switch SSC clock to %dMHz\n", clk);

	if ((clk <= 2) || (N > max_N))
		TRACE_RET(chip, STATUS_FAIL);

	mcu_cnt = (u8) (60 / clk + 3);
	if (mcu_cnt > 15)
		mcu_cnt = 15;
	/* To make sure that the SSC clock div_n is
	 * equal or greater than min_N */
	div = CLK_DIV_1;
	while ((N < min_N) && (div < max_div)) {
		N = (N + 2) * 2 - 2;
		div++;
	}
	RTS51X_DEBUGP("N = %d, div = %d\n", N, div);

	if (chip->option.ssc_en) {
		if (chip->cur_card == SD_CARD) {
			if (CHK_SD_SDR104(sd_card)) {
				ssc_depth = chip->option.ssc_depth_sd_sdr104;
			} else if (CHK_SD_SDR50(sd_card)) {
				ssc_depth = chip->option.ssc_depth_sd_sdr50;
			} else if (CHK_SD_DDR50(sd_card)) {
				ssc_depth =
				    double_depth(chip->option.
						 ssc_depth_sd_ddr50);
			} else if (CHK_SD_HS(sd_card)) {
				ssc_depth =
				    double_depth(chip->option.ssc_depth_sd_hs);
			} else if (CHK_MMC_52M(sd_card)
				   || CHK_MMC_DDR52(sd_card)) {
				ssc_depth =
				    double_depth(chip->option.
						 ssc_depth_mmc_52m);
			} else {
				ssc_depth =
				    double_depth(chip->option.
						 ssc_depth_low_speed);
			}
		} else if (chip->cur_card == MS_CARD) {
			if (CHK_MSPRO(ms_card)) {
				if (CHK_HG8BIT(ms_card)) {
					ssc_depth =
					    double_depth(chip->option.
							 ssc_depth_ms_hg);
				} else {
					ssc_depth =
					    double_depth(chip->option.
							 ssc_depth_ms_4bit);
				}
			} else {
				if (CHK_MS4BIT(ms_card)) {
					ssc_depth =
					    double_depth(chip->option.
							 ssc_depth_ms_4bit);
				} else {
					ssc_depth =
					    double_depth(chip->option.
							 ssc_depth_low_speed);
				}
			}
		} else {
			ssc_depth =
			    double_depth(chip->option.ssc_depth_low_speed);
		}

		if (ssc_depth) {
			if (div == CLK_DIV_2) {
				/* If clock divided by 2, ssc depth must
				 * be multiplied by 2 */
				if (ssc_depth > 1)
					ssc_depth -= 1;
				else
					ssc_depth = SSC_DEPTH_2M;
			} else if (div == CLK_DIV_4) {
				/* If clock divided by 4, ssc depth must
				 * be multiplied by 4 */
				if (ssc_depth > 2)
					ssc_depth -= 2;
				else
					ssc_depth = SSC_DEPTH_2M;
			}
		}
	} else {
		/* Disable SSC */
		ssc_depth = 0;
	}

	RTS51X_DEBUGP("ssc_depth = %d\n", ssc_depth);

	rts51x_init_cmd(chip);
	rts51x_add_cmd(chip, WRITE_REG_CMD, CLK_DIV, CLK_CHANGE, CLK_CHANGE);
	rts51x_add_cmd(chip, WRITE_REG_CMD, CLK_DIV, 0x3F,
		       (div << 4) | mcu_cnt);
	rts51x_add_cmd(chip, WRITE_REG_CMD, SSC_CTL1, SSC_RSTB, 0);
	rts51x_add_cmd(chip, WRITE_REG_CMD, SSC_CTL2, SSC_DEPTH_MASK,
		       ssc_depth);
	rts51x_add_cmd(chip, WRITE_REG_CMD, SSC_DIV_N_0, 0xFF, N);
	if (sd_vpclk_phase_reset) {
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_VPCLK0_CTL,
			       PHASE_NOT_RESET, 0);
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_VPCLK0_CTL,
			       PHASE_NOT_RESET, PHASE_NOT_RESET);
	}

	retval = rts51x_send_cmd(chip, MODE_C, 2000);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);
	if (chip->option.ssc_en && ssc_depth)
		rts51x_write_register(chip, SSC_CTL1, 0xff, 0xD0);
	else
		rts51x_write_register(chip, SSC_CTL1, 0xff, 0x50);
	udelay(100);
	RTS51X_WRITE_REG(chip, CLK_DIV, CLK_CHANGE, 0);

	chip->cur_clk = clk;

	return STATUS_SUCCESS;
}

int switch_normal_clock(struct rts51x_chip *chip, int clk)
{
	int retval;
	u8 sel, div, mcu_cnt;
	int sd_vpclk_phase_reset = 0;

	if (chip->cur_clk == clk)
		return STATUS_SUCCESS;

	if (chip->cur_card == SD_CARD) {
		struct sd_info *sd_card = &(chip->sd_card);
		if (CHK_SD30_SPEED(sd_card) || CHK_MMC_DDR52(sd_card))
			sd_vpclk_phase_reset = 1;
	}

	switch (clk) {
	case CLK_20:
		RTS51X_DEBUGP("Switch clock to 20MHz\n");
		sel = SSC_80;
		div = CLK_DIV_4;
		mcu_cnt = 5;
		break;

	case CLK_30:
		RTS51X_DEBUGP("Switch clock to 30MHz\n");
		sel = SSC_60;
		div = CLK_DIV_2;
		mcu_cnt = 4;
		break;

	case CLK_40:
		RTS51X_DEBUGP("Switch clock to 40MHz\n");
		sel = SSC_80;
		div = CLK_DIV_2;
		mcu_cnt = 3;
		break;

	case CLK_50:
		RTS51X_DEBUGP("Switch clock to 50MHz\n");
		sel = SSC_100;
		div = CLK_DIV_2;
		mcu_cnt = 3;
		break;

	case CLK_60:
		RTS51X_DEBUGP("Switch clock to 60MHz\n");
		sel = SSC_60;
		div = CLK_DIV_1;
		mcu_cnt = 3;
		break;

	case CLK_80:
		RTS51X_DEBUGP("Switch clock to 80MHz\n");
		sel = SSC_80;
		div = CLK_DIV_1;
		mcu_cnt = 2;
		break;

	case CLK_100:
		RTS51X_DEBUGP("Switch clock to 100MHz\n");
		sel = SSC_100;
		div = CLK_DIV_1;
		mcu_cnt = 2;
		break;

	/* case CLK_120:
		RTS51X_DEBUGP("Switch clock to 120MHz\n");
		sel = SSC_120;
		div = CLK_DIV_1;
		mcu_cnt = 2;
		break;

	case CLK_150:
		RTS51X_DEBUGP("Switch clock to 150MHz\n");
		sel = SSC_150;
		div = CLK_DIV_1;
		mcu_cnt = 2;
		break; */

	default:
		RTS51X_DEBUGP("Try to switch to an illegal clock (%d)\n",
			       clk);
		TRACE_RET(chip, STATUS_FAIL);
	}

	if (!sd_vpclk_phase_reset) {
		rts51x_init_cmd(chip);

		rts51x_add_cmd(chip, WRITE_REG_CMD, CLK_DIV, CLK_CHANGE,
			       CLK_CHANGE);
		rts51x_add_cmd(chip, WRITE_REG_CMD, CLK_DIV, 0x3F,
			       (div << 4) | mcu_cnt);
		rts51x_add_cmd(chip, WRITE_REG_CMD, SSC_CLK_FPGA_SEL, 0xFF,
			       sel);
		rts51x_add_cmd(chip, WRITE_REG_CMD, CLK_DIV, CLK_CHANGE, 0);

		retval = rts51x_send_cmd(chip, MODE_C, 100);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, retval);
	} else {
		rts51x_init_cmd(chip);

		rts51x_add_cmd(chip, WRITE_REG_CMD, CLK_DIV, CLK_CHANGE,
			       CLK_CHANGE);
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_VPCLK0_CTL,
			       PHASE_NOT_RESET, 0);
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_VPCLK1_CTL,
			       PHASE_NOT_RESET, 0);
		rts51x_add_cmd(chip, WRITE_REG_CMD, CLK_DIV, 0x3F,
			       (div << 4) | mcu_cnt);
		rts51x_add_cmd(chip, WRITE_REG_CMD, SSC_CLK_FPGA_SEL, 0xFF,
			       sel);

		retval = rts51x_send_cmd(chip, MODE_C, 100);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, retval);

		udelay(200);

		rts51x_init_cmd(chip);

		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_VPCLK0_CTL,
			       PHASE_NOT_RESET, PHASE_NOT_RESET);
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_VPCLK1_CTL,
			       PHASE_NOT_RESET, PHASE_NOT_RESET);

		retval = rts51x_send_cmd(chip, MODE_C, 100);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, retval);

		udelay(200);

		RTS51X_WRITE_REG(chip, CLK_DIV, CLK_CHANGE, 0);
	}

	chip->cur_clk = clk;

	return STATUS_SUCCESS;
}

int card_rw(struct scsi_cmnd *srb, struct rts51x_chip *chip, u32 sec_addr,
	    u16 sec_cnt)
{
	int retval;
	unsigned int lun = SCSI_LUN(srb);
	int i;

	if (chip->rw_card[lun] == NULL)
		return STATUS_FAIL;

	RTS51X_DEBUGP("%s card, sector addr: 0x%x, sector cnt: %d\n",
		       (srb->sc_data_direction ==
			DMA_TO_DEVICE) ? "Write" : "Read", sec_addr, sec_cnt);

	chip->rw_need_retry = 0;
	for (i = 0; i < 3; i++) {
		retval = chip->rw_card[lun] (srb, chip, sec_addr, sec_cnt);
		if (retval != STATUS_SUCCESS) {
			CATCH_TRIGGER(chip);
			if (chip->option.reset_or_rw_fail_set_pad_drive) {
				rts51x_write_register(chip, CARD_DRIVE_SEL,
						      SD20_DRIVE_MASK,
						      DRIVE_8mA);
			}
		}

		if (!chip->rw_need_retry)
			break;

		RTS51X_DEBUGP("Retry RW, (i = %d\n)", i);
	}

	return retval;
}

u8 get_lun_card(struct rts51x_chip *chip, unsigned int lun)
{
	if ((chip->card_ready & chip->lun2card[lun]) == XD_CARD)
		return (u8) XD_CARD;
	else if ((chip->card_ready & chip->lun2card[lun]) == SD_CARD)
		return (u8) SD_CARD;
	else if ((chip->card_ready & chip->lun2card[lun]) == MS_CARD)
		return (u8) MS_CARD;

	return 0;
}

static int card_share_mode(struct rts51x_chip *chip, int card)
{
	u8 value;

	if (card == SD_CARD)
		value = CARD_SHARE_SD;
	else if (card == MS_CARD)
		value = CARD_SHARE_MS;
	else if (card == XD_CARD)
		value = CARD_SHARE_XD;
	else
		TRACE_RET(chip, STATUS_FAIL);

	RTS51X_WRITE_REG(chip, CARD_SHARE_MODE, CARD_SHARE_MASK, value);

	return STATUS_SUCCESS;
}

int rts51x_select_card(struct rts51x_chip *chip, int card)
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
		else
			TRACE_RET(chip, STATUS_FAIL);
		RTS51X_WRITE_REG(chip, CARD_SELECT, 0x07, mod);
		chip->cur_card = card;

		retval = card_share_mode(chip, card);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, retval);
	}

	return STATUS_SUCCESS;
}

void eject_card(struct rts51x_chip *chip, unsigned int lun)
{
	RTS51X_DEBUGP("eject card\n");
	RTS51X_SET_STAT(chip, STAT_RUN);
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
	rts51x_write_register(chip, CARD_INT_PEND, XD_INT | MS_INT | SD_INT,
			      XD_INT | MS_INT | SD_INT);
}

void trans_dma_enable(enum dma_data_direction dir, struct rts51x_chip *chip,
		      u32 byte_cnt, u8 pack_size)
{
	if (pack_size > DMA_1024)
		pack_size = DMA_512;

	rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_DATA_SOURCE, 0x01,
		       RING_BUFFER);

	rts51x_add_cmd(chip, WRITE_REG_CMD, MC_DMA_TC3, 0xFF,
		       (u8) (byte_cnt >> 24));
	rts51x_add_cmd(chip, WRITE_REG_CMD, MC_DMA_TC2, 0xFF,
		       (u8) (byte_cnt >> 16));
	rts51x_add_cmd(chip, WRITE_REG_CMD, MC_DMA_TC1, 0xFF,
		       (u8) (byte_cnt >> 8));
	rts51x_add_cmd(chip, WRITE_REG_CMD, MC_DMA_TC0, 0xFF, (u8) byte_cnt);

	if (dir == DMA_FROM_DEVICE) {
		rts51x_add_cmd(chip, WRITE_REG_CMD, MC_DMA_CTL,
			       0x03 | DMA_PACK_SIZE_MASK,
			       DMA_DIR_FROM_CARD | DMA_EN | pack_size);
	} else {
		rts51x_add_cmd(chip, WRITE_REG_CMD, MC_DMA_CTL,
			       0x03 | DMA_PACK_SIZE_MASK,
			       DMA_DIR_TO_CARD | DMA_EN | pack_size);
	}
}

int enable_card_clock(struct rts51x_chip *chip, u8 card)
{
	u8 clk_en = 0;

	if (card & XD_CARD)
		clk_en |= XD_CLK_EN;
	if (card & SD_CARD)
		clk_en |= SD_CLK_EN;
	if (card & MS_CARD)
		clk_en |= MS_CLK_EN;

	RTS51X_WRITE_REG(chip, CARD_CLK_EN, clk_en, clk_en);

	return STATUS_SUCCESS;
}

int card_power_on(struct rts51x_chip *chip, u8 card)
{
	u8 mask, val1, val2;

	mask = POWER_MASK;
	val1 = PARTIAL_POWER_ON;
	val2 = POWER_ON;

#ifdef SD_XD_IO_FOLLOW_PWR
	if ((card == SD_CARD) || (card == XD_CARD)) {
		RTS51X_WRITE_REG(chip, CARD_PWR_CTL, mask | LDO3318_PWR_MASK,
				 val1 | LDO_SUSPEND);
	} else {
#endif
		RTS51X_WRITE_REG(chip, CARD_PWR_CTL, mask, val1);
#ifdef SD_XD_IO_FOLLOW_PWR
	}
#endif
	udelay(chip->option.pwr_delay);
	RTS51X_WRITE_REG(chip, CARD_PWR_CTL, mask, val2);
#ifdef SD_XD_IO_FOLLOW_PWR
	if (card == SD_CARD) {
		rts51x_write_register(chip, CARD_PWR_CTL, LDO3318_PWR_MASK,
				      LDO_ON);
	}
#endif

	return STATUS_SUCCESS;
}

int monitor_card_cd(struct rts51x_chip *chip, u8 card)
{
	int retval;
	u8 card_cd[32] = { 0 };

	card_cd[SD_CARD] = SD_CD;
	card_cd[XD_CARD] = XD_CD;
	card_cd[MS_CARD] = MS_CD;

	retval = rts51x_get_card_status(chip, &(chip->card_status));
	if (retval != STATUS_SUCCESS)
		return CD_NOT_EXIST;

	if (chip->card_status & card_cd[card])
		return CD_EXIST;

	return CD_NOT_EXIST;
}

int toggle_gpio(struct rts51x_chip *chip, u8 gpio)
{
	int retval;
	u8 temp_reg;
	u8 gpio_output[4] = {
		0x01,
	};
	u8 gpio_oe[4] = {
		0x02,
	};
	if (chip->rts5179) {
		retval = rts51x_ep0_read_register(chip, CARD_GPIO, &temp_reg);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, STATUS_FAIL);
		temp_reg ^= gpio_oe[gpio];
		temp_reg &= 0xfe; /* bit 0 always set 0 */
		retval =
		    rts51x_ep0_write_register(chip, CARD_GPIO, 0x03, temp_reg);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, STATUS_FAIL);
	} else {
		retval = rts51x_ep0_read_register(chip, CARD_GPIO, &temp_reg);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, STATUS_FAIL);
		temp_reg ^= gpio_output[gpio];
		retval =
		    rts51x_ep0_write_register(chip, CARD_GPIO, 0xFF,
					      temp_reg | gpio_oe[gpio]);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, STATUS_FAIL);
	}

	return STATUS_SUCCESS;
}

int turn_on_led(struct rts51x_chip *chip, u8 gpio)
{
	int retval;
	u8 gpio_oe[4] = {
		0x02,
	};
	u8 gpio_mask[4] = {
		0x03,
	};

	retval =
	    rts51x_ep0_write_register(chip, CARD_GPIO, gpio_mask[gpio],
				      gpio_oe[gpio]);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, STATUS_FAIL);

	return STATUS_SUCCESS;
}

int turn_off_led(struct rts51x_chip *chip, u8 gpio)
{
	int retval;
	u8 gpio_output[4] = {
		0x01,
	};
	u8 gpio_oe[4] = {
		0x02,
	};
	u8 gpio_mask[4] = {
		0x03,
	};

	retval =
	    rts51x_ep0_write_register(chip, CARD_GPIO, gpio_mask[gpio],
				      gpio_oe[gpio] | gpio_output[gpio]);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, STATUS_FAIL);

	return STATUS_SUCCESS;
}
