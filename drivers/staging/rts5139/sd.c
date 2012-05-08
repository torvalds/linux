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

#include "debug.h"
#include "trace.h"
#include "rts51x.h"
#include "rts51x_transport.h"
#include "rts51x_scsi.h"
#include "rts51x_card.h"
#include "sd.h"

static inline void sd_set_reset_fail(struct rts51x_chip *chip, u8 err_code)
{
	struct sd_info *sd_card = &(chip->sd_card);

	sd_card->sd_reset_fail |= err_code;
}

static inline void sd_clear_reset_fail(struct rts51x_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);

	sd_card->sd_reset_fail = 0;
}

static inline int sd_check_reset_fail(struct rts51x_chip *chip, u8 err_code)
{
	struct sd_info *sd_card = &(chip->sd_card);

	return sd_card->sd_reset_fail & err_code;
}

static inline void sd_set_err_code(struct rts51x_chip *chip, u8 err_code)
{
	struct sd_info *sd_card = &(chip->sd_card);

	sd_card->err_code |= err_code;
}

static inline void sd_clr_err_code(struct rts51x_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);

	sd_card->err_code = 0;
}

static inline int sd_check_err_code(struct rts51x_chip *chip, u8 err_code)
{
	struct sd_info *sd_card = &(chip->sd_card);

	return sd_card->err_code & err_code;
}

static int sd_parse_err_code(struct rts51x_chip *chip)
{
	TRACE_RET(chip, STATUS_FAIL);
}

int sd_check_data0_status(struct rts51x_chip *chip)
{
	int retval;
	u8 stat;

	retval = rts51x_ep0_read_register(chip, SD_BUS_STAT, &stat);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);
	if (!(stat & SD_DAT0_STATUS)) {
		sd_set_err_code(chip, SD_BUSY);
		TRACE_RET(chip, STATUS_FAIL);
	}

	return STATUS_SUCCESS;
}

static int sd_send_cmd_get_rsp(struct rts51x_chip *chip, u8 cmd_idx,
			       u32 arg, u8 rsp_type, u8 *rsp, int rsp_len)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;
	int timeout = 50;
	u16 reg_addr;
	u8 buf[17], stat;
	int len = 2;
	int rty_cnt = 0;

	sd_clr_err_code(chip);

	RTS51X_DEBUGP("SD/MMC CMD %d, arg = 0x%08x\n", cmd_idx, arg);

	if (rsp_type == SD_RSP_TYPE_R1b)
		timeout = 3000;

RTY_SEND_CMD:

	rts51x_init_cmd(chip);

	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CMD0, 0xFF, 0x40 | cmd_idx);
	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CMD1, 0xFF, (u8) (arg >> 24));
	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CMD2, 0xFF, (u8) (arg >> 16));
	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CMD3, 0xFF, (u8) (arg >> 8));
	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CMD4, 0xFF, (u8) arg);

	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CFG2, 0xFF, rsp_type);
	rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_DATA_SOURCE, 0x01,
		       PINGPONG_BUFFER);
	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_TRANSFER, 0xFF,
		       SD_TM_CMD_RSP | SD_TRANSFER_START);
	rts51x_add_cmd(chip, CHECK_REG_CMD, SD_TRANSFER,
		       SD_TRANSFER_END | SD_STAT_IDLE,
		       SD_TRANSFER_END | SD_STAT_IDLE);

	rts51x_add_cmd(chip, READ_REG_CMD, SD_STAT1, 0, 0);

	if (CHECK_USB(chip, USB_20)) {
		if (rsp_type == SD_RSP_TYPE_R2) {
			/* Read data from ping-pong buffer */
			for (reg_addr = PPBUF_BASE2;
			     reg_addr < PPBUF_BASE2 + 16; reg_addr++) {
				rts51x_add_cmd(chip, READ_REG_CMD, reg_addr, 0,
					       0);
			}
			len = 18;
		} else if (rsp_type != SD_RSP_TYPE_R0) {
			/* Read data from SD_CMDx registers */
			for (reg_addr = SD_CMD0; reg_addr <= SD_CMD4;
			     reg_addr++) {
				rts51x_add_cmd(chip, READ_REG_CMD, reg_addr, 0,
					       0);
			}
			len = 7;
		} else {
			len = 2;
		}
	} else {
		len = 2;
	}

	retval = rts51x_send_cmd(chip, MODE_CR, 100);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	retval = rts51x_get_rsp(chip, len, timeout);

	if (CHECK_SD_TRANS_FAIL(chip, retval)) {
		u8 val;

		rts51x_ep0_read_register(chip, SD_STAT1, &val);
		RTS51X_DEBUGP("SD_STAT1: 0x%x\n", val);

		rts51x_ep0_read_register(chip, SD_STAT2, &val);
		RTS51X_DEBUGP("SD_STAT2: 0x%x\n", val);

		if (val & SD_RSP_80CLK_TIMEOUT)
			sd_set_err_code(chip, SD_RSP_TIMEOUT);

		rts51x_ep0_read_register(chip, SD_BUS_STAT, &val);
		RTS51X_DEBUGP("SD_BUS_STAT: 0x%x\n", val);

		if (retval == STATUS_TIMEDOUT) {
			if (rsp_type & SD_WAIT_BUSY_END) {
				retval = sd_check_data0_status(chip);
				if (retval != STATUS_SUCCESS)
					TRACE_RET(chip, retval);
			} else {
				sd_set_err_code(chip, SD_TO_ERR);
			}
		}
		rts51x_clear_sd_error(chip);

		TRACE_RET(chip, STATUS_FAIL);
	}

	if (rsp_type == SD_RSP_TYPE_R0)
		return STATUS_SUCCESS;

	if (CHECK_USB(chip, USB_20)) {
		rts51x_read_rsp_buf(chip, 2, buf, len - 2);
	} else {
		if (rsp_type == SD_RSP_TYPE_R2) {
			reg_addr = PPBUF_BASE2;
			len = 16;
		} else {
			reg_addr = SD_CMD0;
			len = 5;
		}
		retval = rts51x_seq_read_register(chip, reg_addr,
						     (unsigned short)len, buf);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, retval);
	}
	stat = chip->rsp_buf[1];

	/* Check (Start,Transmission) bit of Response */
	if ((buf[0] & 0xC0) != 0) {
		sd_set_err_code(chip, SD_STS_ERR);
		TRACE_RET(chip, STATUS_FAIL);
	}
	/* Check CRC7 */
	if (!(rsp_type & SD_NO_CHECK_CRC7)) {
		if (stat & SD_CRC7_ERR) {
			if (cmd_idx == WRITE_MULTIPLE_BLOCK) {
				sd_set_err_code(chip, SD_CRC_ERR);
				TRACE_RET(chip, STATUS_FAIL);
			}
			if (rty_cnt < SD_MAX_RETRY_COUNT) {
				wait_timeout(20);
				rty_cnt++;
				goto RTY_SEND_CMD;
			} else {
				sd_set_err_code(chip, SD_CRC_ERR);
				TRACE_RET(chip, STATUS_FAIL);
			}
		}
	}
	/* Check Status */
	if ((rsp_type == SD_RSP_TYPE_R1) || (rsp_type == SD_RSP_TYPE_R1b)) {
		if ((cmd_idx != SEND_RELATIVE_ADDR)
		    && (cmd_idx != SEND_IF_COND)) {
			if (cmd_idx != STOP_TRANSMISSION) {
				if (buf[1] & 0x80)
					TRACE_RET(chip, STATUS_FAIL);
			}
			if (buf[1] & 0x7F) {
				RTS51X_DEBUGP("buf[1]: 0x%02x\n", buf[1]);
				TRACE_RET(chip, STATUS_FAIL);
			}
			if (buf[2] & 0xFF) {
				RTS51X_DEBUGP("buf[2]: 0x%02x\n", buf[2]);
				TRACE_RET(chip, STATUS_FAIL);
			}
			if (buf[3] & 0x80) {
				RTS51X_DEBUGP("buf[3]: 0x%02x\n", buf[3]);
				TRACE_RET(chip, STATUS_FAIL);
			}
			if (buf[3] & 0x01) {
				/* Get "READY_FOR_DATA" bit */
				sd_card->sd_data_buf_ready = 1;
			} else {
				sd_card->sd_data_buf_ready = 0;
			}
		}
	}

	if (rsp && rsp_len)
		memcpy(rsp, buf, rsp_len);

	return STATUS_SUCCESS;
}

static inline void sd_print_debug_reg(struct rts51x_chip *chip)
{
#ifdef CONFIG_RTS5139_DEBUG
	u8 val = 0;

	rts51x_ep0_read_register(chip, SD_STAT1, &val);
	RTS51X_DEBUGP("SD_STAT1: 0x%x\n", val);
	rts51x_ep0_read_register(chip, SD_STAT2, &val);
	RTS51X_DEBUGP("SD_STAT2: 0x%x\n", val);
	rts51x_ep0_read_register(chip, SD_BUS_STAT, &val);
	RTS51X_DEBUGP("SD_BUS_STAT: 0x%x\n", val);
#endif
}

int sd_read_data(struct rts51x_chip *chip, u8 trans_mode, u8 *cmd, int cmd_len,
		 u16 byte_cnt, u16 blk_cnt, u8 bus_width, u8 *buf, int buf_len,
		 int timeout)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;
	int i;

	sd_clr_err_code(chip);

	if (!buf)
		buf_len = 0;

	if (buf_len > 512)
		/* This function can't read data more than one page */
		TRACE_RET(chip, STATUS_FAIL);

	rts51x_init_cmd(chip);

	if (cmd_len) {
		RTS51X_DEBUGP("SD/MMC CMD %d\n", cmd[0] - 0x40);
		for (i = 0; i < (cmd_len < 6 ? cmd_len : 6); i++) {
			rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CMD0 + i, 0xFF,
				       cmd[i]);
		}
	}
	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_BYTE_CNT_L, 0xFF, (u8) byte_cnt);
	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_BYTE_CNT_H, 0xFF,
		       (u8) (byte_cnt >> 8));
	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_BLOCK_CNT_L, 0xFF, (u8) blk_cnt);
	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_BLOCK_CNT_H, 0xFF,
		       (u8) (blk_cnt >> 8));

	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CFG1, 0x03, bus_width);

	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CFG2, 0xFF,
		       SD_CALCULATE_CRC7 | SD_CHECK_CRC16 | SD_NO_WAIT_BUSY_END
		       | SD_CHECK_CRC7 | SD_RSP_LEN_6);
	if (trans_mode != SD_TM_AUTO_TUNING) {
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_DATA_SOURCE, 0x01,
			       PINGPONG_BUFFER);
	}
	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_TRANSFER, 0xFF,
		       trans_mode | SD_TRANSFER_START);
	rts51x_add_cmd(chip, CHECK_REG_CMD, SD_TRANSFER, SD_TRANSFER_END,
		       SD_TRANSFER_END);

	retval = rts51x_send_cmd(chip, MODE_CR, 100);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	retval = rts51x_get_rsp(chip, 1, timeout);

	if (CHECK_SD_TRANS_FAIL(chip, retval)) {
		sd_print_debug_reg(chip);
		if (retval == STATUS_TIMEDOUT) {
			sd_send_cmd_get_rsp(chip, SEND_STATUS, sd_card->sd_addr,
					    SD_RSP_TYPE_R1, NULL, 0);
		}

		TRACE_RET(chip, STATUS_FAIL);
	}

	if (buf && buf_len) {
		retval = rts51x_read_ppbuf(chip, buf, buf_len);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, retval);
	}

	return STATUS_SUCCESS;
}

static int sd_write_data(struct rts51x_chip *chip, u8 trans_mode,
			 u8 *cmd, int cmd_len, u16 byte_cnt, u16 blk_cnt,
			 u8 bus_width, u8 *buf, int buf_len, int timeout)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;
	int i;

	sd_clr_err_code(chip);

	if (!buf)
		buf_len = 0;

	/* This function can't write data more than one page */
	if (buf_len > 512)
		TRACE_RET(chip, STATUS_FAIL);

	if (buf && buf_len) {
		retval = rts51x_write_ppbuf(chip, buf, buf_len);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, retval);
	}

	rts51x_init_cmd(chip);

	if (cmd_len) {
		RTS51X_DEBUGP("SD/MMC CMD %d\n", cmd[0] - 0x40);
		for (i = 0; i < (cmd_len < 6 ? cmd_len : 6); i++) {
			rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CMD0 + i, 0xFF,
				       cmd[i]);
		}
	}
	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_BYTE_CNT_L, 0xFF, (u8) byte_cnt);
	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_BYTE_CNT_H, 0xFF,
		       (u8) (byte_cnt >> 8));
	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_BLOCK_CNT_L, 0xFF, (u8) blk_cnt);
	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_BLOCK_CNT_H, 0xFF,
		       (u8) (blk_cnt >> 8));

	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CFG1, 0x03, bus_width);

	if (cmd_len) {
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CFG2, 0xFF,
			       SD_CALCULATE_CRC7 | SD_CHECK_CRC16 |
			       SD_WAIT_BUSY_END | SD_CHECK_CRC7 | SD_RSP_LEN_6);

	} else {
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CFG2, 0xFF,
			       SD_CALCULATE_CRC7 | SD_CHECK_CRC16 |
			       SD_NO_WAIT_BUSY_END | SD_CHECK_CRC7 |
			       SD_RSP_LEN_6);
	}

	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_TRANSFER, 0xFF,
		       trans_mode | SD_TRANSFER_START);
	rts51x_add_cmd(chip, CHECK_REG_CMD, SD_TRANSFER, SD_TRANSFER_END,
		       SD_TRANSFER_END);

	retval = rts51x_send_cmd(chip, MODE_CR, 100);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	retval = rts51x_get_rsp(chip, 1, timeout);

	if (CHECK_SD_TRANS_FAIL(chip, retval)) {
		sd_print_debug_reg(chip);

		if (retval == STATUS_TIMEDOUT)
			sd_send_cmd_get_rsp(chip, SEND_STATUS, sd_card->sd_addr,
					    SD_RSP_TYPE_R1, NULL, 0);

		TRACE_RET(chip, STATUS_FAIL);
	}

	return STATUS_SUCCESS;
}

static int sd_check_csd(struct rts51x_chip *chip, char check_wp)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;
	int i;
	u8 csd_ver, trans_speed;
	u8 rsp[16];

	for (i = 0; i < 6; i++) {
		if (monitor_card_cd(chip, SD_CARD) == CD_NOT_EXIST) {
			sd_set_reset_fail(chip, SD_RESET_FAIL);
			TRACE_RET(chip, STATUS_FAIL);
		}

		retval =
		    sd_send_cmd_get_rsp(chip, SEND_CSD, sd_card->sd_addr,
					SD_RSP_TYPE_R2, rsp, 16);
		if (retval == STATUS_SUCCESS)
			break;
	}

	if (i == 6)
		TRACE_RET(chip, STATUS_FAIL);
	memcpy(sd_card->raw_csd, rsp + 1, 15);
	/* Get CRC7 */
	RTS51X_READ_REG(chip, SD_CMD5, sd_card->raw_csd + 15);

	RTS51X_DEBUGP("CSD Response:\n");
	RTS51X_DUMP(rsp, 16);

	/* Get CSD Version */
	csd_ver = (rsp[1] & 0xc0) >> 6;
	RTS51X_DEBUGP("csd_ver = %d\n", csd_ver);

	trans_speed = rsp[4];
	if ((trans_speed & 0x07) == 0x02) {	/* 10Mbits/s */
		if ((trans_speed & 0xf8) >= 0x30) {	/* >25Mbits/s */
			if (chip->asic_code)
				sd_card->sd_clock = 46;
			else
				sd_card->sd_clock = CLK_50;
		} else if ((trans_speed & 0xf8) == 0x28) { /* 20Mbits/s */
			if (chip->asic_code)
				sd_card->sd_clock = 39;
			else
				sd_card->sd_clock = CLK_40;
		} else if ((trans_speed & 0xf8) == 0x20) { /* 15Mbits/s */
			if (chip->asic_code)
				sd_card->sd_clock = 29;
			else
				sd_card->sd_clock = CLK_30;
		} else if ((trans_speed & 0xf8) >= 0x10) { /* 12Mbits/s */
			if (chip->asic_code)
				sd_card->sd_clock = 23;
			else
				sd_card->sd_clock = CLK_20;
		} else if ((trans_speed & 0x08) >= 0x08) { /* 10Mbits/s */
			if (chip->asic_code)
				sd_card->sd_clock = 19;
			else
				sd_card->sd_clock = CLK_20;
		} /*else { */
			/*If this ,then slow card will use 30M clock */
			/* TRACE_RET(chip, STATUS_FAIL); */
		/* } */
	}
	/*else {
	   TRACE_RET(chip, STATUS_FAIL);
	   } */
	if (CHK_MMC_SECTOR_MODE(sd_card)) {
		sd_card->capacity = 0;
	} else {
		/* For High-Capacity Card, CSD_STRUCTURE always be "0x1" */
		if ((!CHK_SD_HCXC(sd_card)) || (csd_ver == 0)) {
			/* Calculate total sector according to C_SIZE,
			 * C_SIZE_MULT & READ_BL_LEN */
			u8 blk_size, c_size_mult;
			u16 c_size;
			/* Get READ_BL_LEN */
			blk_size = rsp[6] & 0x0F;
			/* Get C_SIZE */
			c_size = ((u16) (rsp[7] & 0x03) << 10)
			    + ((u16) rsp[8] << 2)
			    + ((u16) (rsp[9] & 0xC0) >> 6);
			/* Get C_SIZE_MUL */
			c_size_mult = (u8) ((rsp[10] & 0x03) << 1);
			c_size_mult += (rsp[11] & 0x80) >> 7;
			/* Calculate total Capacity  */
			sd_card->capacity =
			    (((u32) (c_size + 1)) *
			     (1 << (c_size_mult + 2))) << (blk_size - 9);
		} else {
			/* High Capacity Card and Use CSD2.0 Version */
			u32 total_sector = 0;
			total_sector = (((u32) rsp[8] & 0x3f) << 16) |
			    ((u32) rsp[9] << 8) | (u32) rsp[10];
			/* Total Capacity= (C_SIZE+1) *
			 * 512K Byte = (C_SIZE+1)K Sector,1K = 1024 Bytes */
			sd_card->capacity = (total_sector + 1) << 10;
		}
	}

	/* We need check Write-Protected Status by Field PERM WP or TEMP WP */
	if (check_wp) {
		if (rsp[15] & 0x30)
			chip->card_wp |= SD_CARD;
		RTS51X_DEBUGP("CSD WP Status: 0x%x\n", rsp[15]);
	}

	return STATUS_SUCCESS;
}

static int sd_set_sample_push_timing(struct rts51x_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;

	rts51x_init_cmd(chip);

	if (CHK_SD_SDR104(sd_card) || CHK_SD_SDR50(sd_card)) {
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CFG1,
			       0x0C | SD_ASYNC_FIFO_RST,
			       SD_30_MODE | SD_ASYNC_FIFO_RST);
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_CLK_SOURCE, 0xFF,
			       CRC_VAR_CLK0 | SD30_FIX_CLK | SAMPLE_VAR_CLK1);
	} else if (CHK_SD_DDR50(sd_card) || CHK_MMC_DDR52(sd_card)) {
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CFG1,
			       0x0C | SD_ASYNC_FIFO_RST,
			       SD_DDR_MODE | SD_ASYNC_FIFO_RST);
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_CLK_SOURCE, 0xFF,
			       CRC_VAR_CLK0 | SD30_FIX_CLK | SAMPLE_VAR_CLK1);
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_PUSH_POINT_CTL,
			       DDR_VAR_TX_CMD_DAT, DDR_VAR_TX_CMD_DAT);
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_SAMPLE_POINT_CTL,
			       DDR_VAR_RX_DAT | DDR_VAR_RX_CMD,
			       DDR_VAR_RX_DAT | DDR_VAR_RX_CMD);
	} else {
		u8 val = 0;

		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CFG1, 0x0C, SD_20_MODE);
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_CLK_SOURCE, 0xFF,
			       CRC_FIX_CLK | SD30_VAR_CLK0 | SAMPLE_VAR_CLK1);

		if ((chip->option.sd_ctl & SD_PUSH_POINT_CTL_MASK) ==
		    SD_PUSH_POINT_AUTO) {
			val = SD20_TX_NEG_EDGE;
		} else if ((chip->option.sd_ctl & SD_PUSH_POINT_CTL_MASK) ==
			   SD_PUSH_POINT_DELAY) {
			val = SD20_TX_14_AHEAD;
		} else {
			val = SD20_TX_NEG_EDGE;
		}
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_PUSH_POINT_CTL,
			       SD20_TX_SEL_MASK, val);

		if ((chip->option.sd_ctl & SD_SAMPLE_POINT_CTL_MASK) ==
		    SD_SAMPLE_POINT_AUTO) {
			if (chip->asic_code) {
				if (CHK_SD_HS(sd_card) || CHK_MMC_52M(sd_card))
					val = SD20_RX_14_DELAY;
				else
					val = SD20_RX_POS_EDGE;
			} else {
				val = SD20_RX_14_DELAY;
			}
		} else if ((chip->option.sd_ctl & SD_SAMPLE_POINT_CTL_MASK) ==
			   SD_SAMPLE_POINT_DELAY) {
			val = SD20_RX_14_DELAY;
		} else {
			val = SD20_RX_POS_EDGE;
		}
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_SAMPLE_POINT_CTL,
			       SD20_RX_SEL_MASK, val);
	}

	if (CHK_MMC_DDR52(sd_card) && CHK_MMC_8BIT(sd_card)) {
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_DMA1_CTL,
			       EXTEND_DMA1_ASYNC_SIGNAL, 0);
	}

	retval = rts51x_send_cmd(chip, MODE_C, 100);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	return STATUS_SUCCESS;
}

static void sd_choose_proper_clock(struct rts51x_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);

	if (CHK_SD_SDR104(sd_card)) {
		if (chip->asic_code)
			sd_card->sd_clock = chip->option.asic_sd_sdr104_clk;
		else
			sd_card->sd_clock = chip->option.fpga_sd_sdr104_clk;
	} else if (CHK_SD_DDR50(sd_card)) {
		if (chip->asic_code)
			sd_card->sd_clock = chip->option.asic_sd_ddr50_clk;
		else
			sd_card->sd_clock = chip->option.fpga_sd_ddr50_clk;
	} else if (CHK_SD_SDR50(sd_card)) {
		if (chip->asic_code)
			sd_card->sd_clock = chip->option.asic_sd_sdr50_clk;
		else
			sd_card->sd_clock = chip->option.fpga_sd_sdr50_clk;
	} else if (CHK_SD_HS(sd_card)) {
		if (chip->asic_code)
			sd_card->sd_clock = chip->option.asic_sd_hs_clk;
		else
			sd_card->sd_clock = chip->option.fpga_sd_hs_clk;
	} else if (CHK_MMC_52M(sd_card) || CHK_MMC_DDR52(sd_card)) {
		if (chip->asic_code)
			sd_card->sd_clock = chip->option.asic_mmc_52m_clk;
		else
			sd_card->sd_clock = chip->option.fpga_mmc_52m_clk;
	} else if (CHK_MMC_26M(sd_card)) {
		if (chip->asic_code) {
			sd_card->sd_clock = 46;
			RTS51X_DEBUGP("Set MMC clock to 22.5MHz\n");
		} else {
			sd_card->sd_clock = CLK_50;
		}
	}
}

static int sd_set_init_para(struct rts51x_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;

	retval = sd_set_sample_push_timing(chip);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	sd_choose_proper_clock(chip);

	retval = switch_clock(chip, sd_card->sd_clock);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	return STATUS_SUCCESS;
}

int sd_select_card(struct rts51x_chip *chip, int select)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;
	u8 cmd_idx, cmd_type;
	u32 addr;

	if (select) {
		cmd_idx = SELECT_CARD;
		cmd_type = SD_RSP_TYPE_R1;
		addr = sd_card->sd_addr;
	} else {
		cmd_idx = DESELECT_CARD;
		cmd_type = SD_RSP_TYPE_R0;
		addr = 0;
	}

	retval = sd_send_cmd_get_rsp(chip, cmd_idx, addr, cmd_type, NULL, 0);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	return STATUS_SUCCESS;
}

int sd_wait_currentstate_dataready(struct rts51x_chip *chip, u8 statechk,
				   u8 rdychk, u16 pollingcnt)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;
	u8 rsp[5];
	u16 i;

	for (i = 0; i < pollingcnt; i++) {

		retval =
		    sd_send_cmd_get_rsp(chip, SEND_STATUS, sd_card->sd_addr,
					SD_RSP_TYPE_R1, rsp, 5);
		if (retval == STATUS_SUCCESS) {
			if (((rsp[3] & 0x1E) == statechk)
			    && ((rsp[3] & 0x01) == rdychk)) {
				return STATUS_SUCCESS;
			}
		} else {
			rts51x_clear_sd_error(chip);
			TRACE_RET(chip, STATUS_FAIL);
		}
	}

	return STATUS_TIMEDOUT;
}

static int sd_voltage_switch(struct rts51x_chip *chip)
{
	int retval;
	u8 stat;

	RTS51X_WRITE_REG(chip, SD_BUS_STAT,
			 SD_CLK_TOGGLE_EN | SD_CLK_FORCE_STOP,
			 SD_CLK_TOGGLE_EN);

	retval =
	    sd_send_cmd_get_rsp(chip, VOLTAGE_SWITCH, 0, SD_RSP_TYPE_R1, NULL,
				0);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	RTS51X_READ_REG(chip, SD_BUS_STAT, &stat);
	if (stat & (SD_CMD_STATUS | SD_DAT3_STATUS | SD_DAT2_STATUS |
		    SD_DAT1_STATUS | SD_DAT0_STATUS))
		TRACE_RET(chip, STATUS_FAIL);

	rts51x_init_cmd(chip);
	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_BUS_STAT, 0xFF,
		       SD_CLK_FORCE_STOP);
	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_PAD_CTL, SD_IO_USING_1V8,
		       SD_IO_USING_1V8);
	if (chip->asic_code)
		rts51x_add_cmd(chip, WRITE_REG_CMD, LDO_POWER_CFG,
			       TUNE_SD18_MASK, TUNE_SD18_1V8);
	retval = rts51x_send_cmd(chip, MODE_C, 100);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	wait_timeout(chip->option.D3318_off_delay);

	RTS51X_WRITE_REG(chip, SD_BUS_STAT, 0xFF, SD_CLK_TOGGLE_EN);
	wait_timeout(10);

	RTS51X_READ_REG(chip, SD_BUS_STAT, &stat);
	if ((stat & (SD_CMD_STATUS | SD_DAT3_STATUS | SD_DAT2_STATUS |
		     SD_DAT1_STATUS | SD_DAT0_STATUS)) !=
	    (SD_CMD_STATUS | SD_DAT3_STATUS | SD_DAT2_STATUS |
	     SD_DAT1_STATUS | SD_DAT0_STATUS)) {
		rts51x_init_cmd(chip);
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_BUS_STAT, 0xFF,
			       SD_CLK_FORCE_STOP);
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_CLK_EN, 0xFF, 0);
		rts51x_send_cmd(chip, MODE_C, 100);
		TRACE_RET(chip, STATUS_FAIL);
	}
	RTS51X_WRITE_REG(chip, SD_BUS_STAT,
			 SD_CLK_TOGGLE_EN | SD_CLK_FORCE_STOP, 0);

	return STATUS_SUCCESS;
}

static int sd_change_phase(struct rts51x_chip *chip, u8 sample_point,
			   u8 tune_dir)
{
	u16 SD_VP_CTL, SD_DCMPS_CTL;
	u8 val;
	int retval;

	RTS51X_DEBUGP("sd_change_phase (sample_point = %d, tune_dir = %d)\n",
		       sample_point, tune_dir);

	if (tune_dir == TUNE_RX) {
		SD_VP_CTL = SD_VPCLK1_CTL;
		SD_DCMPS_CTL = SD_DCMPS1_CTL;
	} else {
		SD_VP_CTL = SD_VPCLK0_CTL;
		SD_DCMPS_CTL = SD_DCMPS0_CTL;
	}

	if (chip->asic_code) {
		RTS51X_WRITE_REG(chip, CLK_DIV, CLK_CHANGE, CLK_CHANGE);
		RTS51X_WRITE_REG(chip, SD_VP_CTL, 0x1F, sample_point);
		RTS51X_WRITE_REG(chip, SD_VPCLK0_CTL, PHASE_NOT_RESET, 0);
		RTS51X_WRITE_REG(chip, SD_VPCLK0_CTL, PHASE_NOT_RESET,
				 PHASE_NOT_RESET);
		RTS51X_WRITE_REG(chip, CLK_DIV, CLK_CHANGE, 0);
	} else {
#ifdef CONFIG_RTS5139_DEBUG
		RTS51X_READ_REG(chip, SD_VP_CTL, &val);
		RTS51X_DEBUGP("SD_VP_CTL: 0x%x\n", val);
		RTS51X_READ_REG(chip, SD_DCMPS_CTL, &val);
		RTS51X_DEBUGP("SD_DCMPS_CTL: 0x%x\n", val);
#endif

		RTS51X_WRITE_REG(chip, CLK_DIV, CLK_CHANGE, CLK_CHANGE);
		udelay(100);
		RTS51X_WRITE_REG(chip, SD_VP_CTL, 0xFF,
				 PHASE_NOT_RESET | sample_point);
		udelay(200);

		rts51x_init_cmd(chip);
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_DCMPS_CTL, DCMPS_CHANGE,
			       DCMPS_CHANGE);
		rts51x_add_cmd(chip, CHECK_REG_CMD, SD_DCMPS_CTL,
			       DCMPS_CHANGE_DONE, DCMPS_CHANGE_DONE);
		retval = rts51x_send_cmd(chip, MODE_CR, 100);
		if (retval != STATUS_SUCCESS)
			TRACE_GOTO(chip, Fail);

		retval = rts51x_get_rsp(chip, 1, 500);
		if (retval != STATUS_SUCCESS)
			TRACE_GOTO(chip, Fail);

		val = chip->rsp_buf[0];
		if (val & DCMPS_ERROR)
			TRACE_GOTO(chip, Fail);
		if ((val & DCMPS_CURRENT_PHASE) != sample_point)
			TRACE_GOTO(chip, Fail);
		RTS51X_WRITE_REG(chip, SD_DCMPS_CTL, DCMPS_CHANGE, 0);
		RTS51X_WRITE_REG(chip, CLK_DIV, CLK_CHANGE, 0);
		udelay(100);
	}

	RTS51X_WRITE_REG(chip, SD_CFG1, SD_ASYNC_FIFO_RST, 0);

	return STATUS_SUCCESS;

Fail:
#ifdef CONFIG_RTS5139_DEBUG
	rts51x_ep0_read_register(chip, SD_VP_CTL, &val);
	RTS51X_DEBUGP("SD_VP_CTL: 0x%x\n", val);
	rts51x_ep0_read_register(chip, SD_DCMPS_CTL, &val);
	RTS51X_DEBUGP("SD_DCMPS_CTL: 0x%x\n", val);
#endif

	RTS51X_WRITE_REG(chip, SD_DCMPS_CTL, DCMPS_CHANGE, 0);
	RTS51X_WRITE_REG(chip, SD_VP_CTL, PHASE_CHANGE, 0);
	wait_timeout(10);

	return STATUS_FAIL;
}

static int sd_check_spec(struct rts51x_chip *chip, u8 bus_width)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;
	u8 cmd[5], buf[8];

	retval =
	    sd_send_cmd_get_rsp(chip, APP_CMD, sd_card->sd_addr, SD_RSP_TYPE_R1,
				NULL, 0);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, STATUS_FAIL);

	cmd[0] = 0x40 | SEND_SCR;
	cmd[1] = 0;
	cmd[2] = 0;
	cmd[3] = 0;
	cmd[4] = 0;

	retval =
	    sd_read_data(chip, SD_TM_NORMAL_READ, cmd, 5, 8, 1, bus_width, buf,
			 8, 250);
	if (retval != STATUS_SUCCESS) {
		rts51x_clear_sd_error(chip);
		TRACE_RET(chip, retval);
	}

	memcpy(sd_card->raw_scr, buf, 8);

	if ((buf[0] & 0x0F) == 0)
		TRACE_RET(chip, STATUS_FAIL);

	return STATUS_SUCCESS;
}

static int sd_query_switch_result(struct rts51x_chip *chip, u8 func_group,
				  u8 func_to_switch, u8 *buf, int buf_len)
{
	u8 support_mask = 0, query_switch = 0, switch_busy = 0;
	int support_offset = 0, query_switch_offset = 0, check_busy_offset = 0;

	if (func_group == SD_FUNC_GROUP_1) {
		support_offset = FUNCTION_GROUP1_SUPPORT_OFFSET;
		query_switch_offset = FUNCTION_GROUP1_QUERY_SWITCH_OFFSET;
		check_busy_offset = FUNCTION_GROUP1_CHECK_BUSY_OFFSET;

		switch (func_to_switch) {
		case HS_SUPPORT:
			support_mask = HS_SUPPORT_MASK;
			query_switch = HS_QUERY_SWITCH_OK;
			switch_busy = HS_SWITCH_BUSY;
			break;

		case SDR50_SUPPORT:
			support_mask = SDR50_SUPPORT_MASK;
			query_switch = SDR50_QUERY_SWITCH_OK;
			switch_busy = SDR50_SWITCH_BUSY;
			break;

		case SDR104_SUPPORT:
			support_mask = SDR104_SUPPORT_MASK;
			query_switch = SDR104_QUERY_SWITCH_OK;
			switch_busy = SDR104_SWITCH_BUSY;
			break;

		case DDR50_SUPPORT:
			support_mask = DDR50_SUPPORT_MASK;
			query_switch = DDR50_QUERY_SWITCH_OK;
			switch_busy = DDR50_SWITCH_BUSY;
			break;

		default:
			TRACE_RET(chip, STATUS_FAIL);
		}
	} else if (func_group == SD_FUNC_GROUP_3) {
		support_offset = FUNCTION_GROUP3_SUPPORT_OFFSET;
		query_switch_offset = FUNCTION_GROUP3_QUERY_SWITCH_OFFSET;
		check_busy_offset = FUNCTION_GROUP3_CHECK_BUSY_OFFSET;

		switch (func_to_switch) {
		case DRIVING_TYPE_A:
			support_mask = DRIVING_TYPE_A_MASK;
			query_switch = TYPE_A_QUERY_SWITCH_OK;
			switch_busy = TYPE_A_SWITCH_BUSY;
			break;

		case DRIVING_TYPE_C:
			support_mask = DRIVING_TYPE_C_MASK;
			query_switch = TYPE_C_QUERY_SWITCH_OK;
			switch_busy = TYPE_C_SWITCH_BUSY;
			break;

		case DRIVING_TYPE_D:
			support_mask = DRIVING_TYPE_D_MASK;
			query_switch = TYPE_D_QUERY_SWITCH_OK;
			switch_busy = TYPE_D_SWITCH_BUSY;
			break;

		default:
			TRACE_RET(chip, STATUS_FAIL);
		}
	} else if (func_group == SD_FUNC_GROUP_4) {
		support_offset = FUNCTION_GROUP4_SUPPORT_OFFSET;
		query_switch_offset = FUNCTION_GROUP4_QUERY_SWITCH_OFFSET;
		check_busy_offset = FUNCTION_GROUP4_CHECK_BUSY_OFFSET;

		switch (func_to_switch) {
		case CURRENT_LIMIT_400:
			support_mask = CURRENT_LIMIT_400_MASK;
			query_switch = CURRENT_LIMIT_400_QUERY_SWITCH_OK;
			switch_busy = CURRENT_LIMIT_400_SWITCH_BUSY;
			break;

		case CURRENT_LIMIT_600:
			support_mask = CURRENT_LIMIT_600_MASK;
			query_switch = CURRENT_LIMIT_600_QUERY_SWITCH_OK;
			switch_busy = CURRENT_LIMIT_600_SWITCH_BUSY;
			break;

		case CURRENT_LIMIT_800:
			support_mask = CURRENT_LIMIT_800_MASK;
			query_switch = CURRENT_LIMIT_800_QUERY_SWITCH_OK;
			switch_busy = CURRENT_LIMIT_800_SWITCH_BUSY;
			break;

		default:
			TRACE_RET(chip, STATUS_FAIL);
		}
	} else {
		TRACE_RET(chip, STATUS_FAIL);
	}

	if (func_group == SD_FUNC_GROUP_4)
		buf[query_switch_offset] =
		    (buf[query_switch_offset] & 0xf0) >> 4;
	if (!(buf[support_offset] & support_mask) ||
	    ((buf[query_switch_offset] & 0x0F) != query_switch))
		TRACE_RET(chip, STATUS_FAIL);

	if ((buf[DATA_STRUCTURE_VER_OFFSET] == 0x01) &&
	    ((buf[check_busy_offset] & switch_busy) == switch_busy))
		TRACE_RET(chip, STATUS_FAIL);

	return STATUS_SUCCESS;
}

static int sd_check_switch_mode(struct rts51x_chip *chip, u8 mode,
				u8 func_group, u8 func_to_switch, u8 bus_width)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;
	u8 cmd[5], buf[64];

	RTS51X_DEBUGP("sd_check_switch_mode (mode = %d, func_group = %d,"
		"func_to_switch = %d)\n", mode, func_group, func_to_switch);

	cmd[0] = 0x40 | SWITCH;
	cmd[1] = mode;

	if (func_group == SD_FUNC_GROUP_1) {
		cmd[2] = 0xFF;
		cmd[3] = 0xFF;
		cmd[4] = 0xF0 + func_to_switch;
	} else if (func_group == SD_FUNC_GROUP_3) {
		cmd[2] = 0xFF;
		cmd[3] = 0xF0 + func_to_switch;
		cmd[4] = 0xFF;
	} else if (func_group == SD_FUNC_GROUP_4) {
		cmd[2] = 0xFF;
		cmd[3] = 0x0F + (func_to_switch << 4);
		cmd[4] = 0xFF;
	} else {
		cmd[1] = SD_CHECK_MODE;
		cmd[2] = 0xFF;
		cmd[3] = 0xFF;
		cmd[4] = 0xFF;
	}

	retval =
	    sd_read_data(chip, SD_TM_NORMAL_READ, cmd, 5, 64, 1, bus_width, buf,
			 64, 250);
	if (retval != STATUS_SUCCESS) {
		rts51x_clear_sd_error(chip);
		TRACE_RET(chip, retval);
	}

	if (func_group == NO_ARGUMENT) {
		sd_card->func_group1_mask = buf[0x0D];
		sd_card->func_group2_mask = buf[0x0B];
		sd_card->func_group3_mask = buf[0x09];
		sd_card->func_group4_mask = buf[0x07];

		RTS51X_DEBUGP("func_group1_mask = 0x%02x\n", buf[0x0D]);
		RTS51X_DEBUGP("func_group2_mask = 0x%02x\n", buf[0x0B]);
		RTS51X_DEBUGP("func_group3_mask = 0x%02x\n", buf[0x09]);
		RTS51X_DEBUGP("func_group4_mask = 0x%02x\n", buf[0x07]);
	} else {
		if ((buf[0] == 0) && (buf[1] == 0))
			TRACE_RET(chip, STATUS_FAIL);
		retval =
		    sd_query_switch_result(chip, func_group, func_to_switch,
					   buf, 64);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, retval);
	}

	return STATUS_SUCCESS;
}

static int sd_check_switch(struct rts51x_chip *chip,
			   u8 func_group, u8 func_to_switch, u8 bus_width)
{
	int retval;
	int i;
	int switch_good = 0;

	for (i = 0; i < 3; i++) {
		if (monitor_card_cd(chip, SD_CARD) == CD_NOT_EXIST) {
			sd_set_reset_fail(chip, SD_RESET_FAIL);
			TRACE_RET(chip, STATUS_FAIL);
		}

		retval = sd_check_switch_mode(chip, SD_CHECK_MODE, func_group,
					      func_to_switch, bus_width);
		if (retval == STATUS_SUCCESS) {
			u8 stat;

			retval = sd_check_switch_mode(chip, SD_SWITCH_MODE,
					func_group, func_to_switch, bus_width);
			if (retval == STATUS_SUCCESS) {
				switch_good = 1;
				break;
			}

			RTS51X_READ_REG(chip, SD_STAT1, &stat);

			if (stat & SD_CRC16_ERR) {
				RTS51X_DEBUGP("SD CRC16 error when switching"
							"mode\n");
				TRACE_RET(chip, STATUS_FAIL);
			}
		}

		wait_timeout(20);
	}

	if (!switch_good)
		TRACE_RET(chip, STATUS_FAIL);

	return STATUS_SUCCESS;
}

static int sd_switch_function(struct rts51x_chip *chip, u8 bus_width)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;
	int i;
	u8 func_to_switch = 0;

	/* Get supported functions */
	retval = sd_check_switch_mode(chip, SD_CHECK_MODE,
				      NO_ARGUMENT, NO_ARGUMENT, bus_width);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	sd_card->func_group1_mask &= ~(sd_card->sd_switch_fail);

	for (i = 0; i < 4; i++) {
		switch ((u8) (chip->option.sd_speed_prior >> (i * 8))) {
		case DDR50_SUPPORT:
			if ((sd_card->func_group1_mask & DDR50_SUPPORT_MASK)
			    && (CHECK_UHS50(chip)))
				func_to_switch = DDR50_SUPPORT;
			break;

		case SDR50_SUPPORT:
			if ((sd_card->func_group1_mask & SDR50_SUPPORT_MASK)
			    && (CHECK_UHS50(chip)))
				func_to_switch = SDR50_SUPPORT;
			break;

		case HS_SUPPORT:
			if (sd_card->func_group1_mask & HS_SUPPORT_MASK)
				func_to_switch = HS_SUPPORT;
			break;

		default:
			continue;
		}

		if (func_to_switch)
			break;
	}
	RTS51X_DEBUGP("SD_FUNC_GROUP_1: func_to_switch = 0x%02x",
		       func_to_switch);

	if (func_to_switch) {
		retval =
		    sd_check_switch(chip, SD_FUNC_GROUP_1, func_to_switch,
				    bus_width);
		if (retval != STATUS_SUCCESS) {
			if (func_to_switch == SDR104_SUPPORT)
				sd_card->sd_switch_fail = SDR104_SUPPORT_MASK;
			else if (func_to_switch == DDR50_SUPPORT)
				sd_card->sd_switch_fail = DDR50_SUPPORT_MASK;
			else if (func_to_switch == SDR50_SUPPORT)
				sd_card->sd_switch_fail = SDR50_SUPPORT_MASK;
			else if (func_to_switch == HS_SUPPORT)
				sd_card->sd_switch_fail = HS_SUPPORT_MASK;

			TRACE_RET(chip, retval);
		}

		if (func_to_switch == SDR104_SUPPORT)
			SET_SD_SDR104(sd_card);
		else if (func_to_switch == DDR50_SUPPORT)
			SET_SD_DDR50(sd_card);
		else if (func_to_switch == SDR50_SUPPORT)
			SET_SD_SDR50(sd_card);
		else
			SET_SD_HS(sd_card);
	}

	if (CHK_SD_DDR50(sd_card))
		RTS51X_WRITE_REG(chip, SD_CFG1, 0x0C, SD_DDR_MODE);

	func_to_switch = 0;
	if (sd_card->func_group4_mask & CURRENT_LIMIT_400_MASK)
		func_to_switch = CURRENT_LIMIT_400;

	if (func_to_switch) {
		RTS51X_DEBUGP("Try to switch current_limit_400\n");
		retval =
		    sd_check_switch(chip, SD_FUNC_GROUP_4, func_to_switch,
				    bus_width);
		RTS51X_DEBUGP("Switch current_limit_400 status: (%d)\n",
			       retval);
	}

	return STATUS_SUCCESS;
}

static int sd_wait_data_idle(struct rts51x_chip *chip)
{
	int retval = STATUS_TIMEDOUT;
	int i;
	u8 val = 0;

	for (i = 0; i < 100; i++) {
		retval = rts51x_ep0_read_register(chip, SD_DATA_STATE, &val);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, STATUS_FAIL);
		if (val & SD_DATA_IDLE) {
			retval = STATUS_SUCCESS;
			break;
		}
		udelay(100);
	}
	RTS51X_DEBUGP("SD_DATA_STATE: 0x%02x\n", val);

	return retval;
}

static int sd_sdr_tuning_rx_cmd(struct rts51x_chip *chip, u8 sample_point)
{
	int retval;
	u8 cmd[5];

	retval = sd_change_phase(chip, sample_point, TUNE_RX);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	cmd[0] = 0x40 | SEND_TUNING_PATTERN;
	cmd[1] = 0;
	cmd[2] = 0;
	cmd[3] = 0;
	cmd[4] = 0;

	retval = sd_read_data(chip, SD_TM_AUTO_TUNING,
			      cmd, 5, 0x40, 1, SD_BUS_WIDTH_4, NULL, 0, 100);
	if (retval != STATUS_SUCCESS) {
		/* Wait till SD DATA IDLE */
		(void)sd_wait_data_idle(chip);

		rts51x_clear_sd_error(chip);
		TRACE_RET(chip, STATUS_FAIL);
	}

	return STATUS_SUCCESS;
}

static int sd_ddr_tuning_rx_cmd(struct rts51x_chip *chip, u8 sample_point)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;
	u8 cmd[5];

	retval = sd_change_phase(chip, sample_point, TUNE_RX);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	RTS51X_DEBUGP("sd ddr tuning rx\n");

	retval =
	    sd_send_cmd_get_rsp(chip, APP_CMD, sd_card->sd_addr, SD_RSP_TYPE_R1,
				NULL, 0);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	cmd[0] = 0x40 | SD_STATUS;
	cmd[1] = 0;
	cmd[2] = 0;
	cmd[3] = 0;
	cmd[4] = 0;

	retval = sd_read_data(chip, SD_TM_NORMAL_READ,
			      cmd, 5, 64, 1, SD_BUS_WIDTH_4, NULL, 0, 100);
	if (retval != STATUS_SUCCESS) {
		/* Wait till SD DATA IDLE */
		(void)sd_wait_data_idle(chip);

		rts51x_clear_sd_error(chip);
		TRACE_RET(chip, STATUS_FAIL);
	}

	return STATUS_SUCCESS;
}

static int mmc_ddr_tunning_rx_cmd(struct rts51x_chip *chip, u8 sample_point)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;
	u8 cmd[5], bus_width;

	if (CHK_MMC_8BIT(sd_card))
		bus_width = SD_BUS_WIDTH_8;
	else if (CHK_MMC_4BIT(sd_card))
		bus_width = SD_BUS_WIDTH_4;
	else
		bus_width = SD_BUS_WIDTH_1;

	retval = sd_change_phase(chip, sample_point, TUNE_RX);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	RTS51X_DEBUGP("mmc ddr tuning rx\n");

	cmd[0] = 0x40 | SEND_EXT_CSD;
	cmd[1] = 0;
	cmd[2] = 0;
	cmd[3] = 0;
	cmd[4] = 0;

	retval = sd_read_data(chip, SD_TM_NORMAL_READ,
			      cmd, 5, 0x200, 1, bus_width, NULL, 0, 100);
	if (retval != STATUS_SUCCESS) {
		/* Wait till SD DATA IDLE */
		(void)sd_wait_data_idle(chip);

		rts51x_clear_sd_error(chip);
		TRACE_RET(chip, STATUS_FAIL);
	}

	return STATUS_SUCCESS;
}

static int sd_sdr_tuning_tx_cmd(struct rts51x_chip *chip, u8 sample_point)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;

	retval = sd_change_phase(chip, sample_point, TUNE_TX);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	RTS51X_WRITE_REG(chip, SD_CFG3, SD_RSP_80CLK_TIMEOUT_EN,
			 SD_RSP_80CLK_TIMEOUT_EN);

	retval = sd_send_cmd_get_rsp(chip, SEND_STATUS, sd_card->sd_addr,
				     SD_RSP_TYPE_R1, NULL, 0);
	if (retval != STATUS_SUCCESS) {
		if (sd_check_err_code(chip, SD_RSP_TIMEOUT)) {
			/* Tunning TX fail */
			rts51x_ep0_write_register(chip, SD_CFG3,
						  SD_RSP_80CLK_TIMEOUT_EN, 0);
			TRACE_RET(chip, STATUS_FAIL);
		}
	}

	RTS51X_WRITE_REG(chip, SD_CFG3, SD_RSP_80CLK_TIMEOUT_EN, 0);

	return STATUS_SUCCESS;
}

static int sd_ddr_tuning_tx_cmd(struct rts51x_chip *chip, u8 sample_point)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;
	u8 cmd[5], bus_width;

	retval = sd_change_phase(chip, sample_point, TUNE_TX);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	if (CHK_SD(sd_card)) {
		bus_width = SD_BUS_WIDTH_4;
	} else {
		if (CHK_MMC_8BIT(sd_card))
			bus_width = SD_BUS_WIDTH_8;
		else if (CHK_MMC_4BIT(sd_card))
			bus_width = SD_BUS_WIDTH_4;
		else
			bus_width = SD_BUS_WIDTH_1;
	}
	retval = sd_wait_currentstate_dataready(chip, 0x08, 1, 20);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, STATUS_FAIL);

	RTS51X_WRITE_REG(chip, SD_CFG3, SD_RSP_80CLK_TIMEOUT_EN,
			 SD_RSP_80CLK_TIMEOUT_EN);

	cmd[0] = 0x40 | PROGRAM_CSD;
	cmd[1] = 0;
	cmd[2] = 0;
	cmd[3] = 0;
	cmd[4] = 0;

	retval = sd_write_data(chip, SD_TM_AUTO_WRITE_2,
			cmd, 5, 16, 1, bus_width, sd_card->raw_csd, 16, 100);
	if (retval != STATUS_SUCCESS) {
		rts51x_clear_sd_error(chip);
		/* Tunning TX fail */
		rts51x_ep0_write_register(chip, SD_CFG3,
					  SD_RSP_80CLK_TIMEOUT_EN, 0);
		TRACE_RET(chip, STATUS_FAIL);
	}

	RTS51X_WRITE_REG(chip, SD_CFG3, SD_RSP_80CLK_TIMEOUT_EN, 0);

	sd_send_cmd_get_rsp(chip, SEND_STATUS, sd_card->sd_addr, SD_RSP_TYPE_R1,
			    NULL, 0);

	return STATUS_SUCCESS;
}

static u8 sd_search_final_phase(struct rts51x_chip *chip, u32 phase_map,
				u8 tune_dir)
{
	struct sd_info *sd_card = &(chip->sd_card);
	struct timing_phase_path path[MAX_PHASE + 1];
	int i, j, cont_path_cnt;
	int new_block, max_len;
	u8 final_phase = 0xFF;
	int final_path_idx;

	if (phase_map == 0xffff) {
		if (CHK_SD_DDR50(sd_card)) {
			if (tune_dir == TUNE_TX)
				final_phase = chip->option.ddr50_tx_phase;
			else
				final_phase = chip->option.ddr50_rx_phase;
			RTS51X_DEBUGP("DDR50 tuning dir:%d all pass,"
					"so select default phase:0x%x.\n",
					tune_dir, final_phase);
		} else {
			if (tune_dir == TUNE_TX)
				final_phase = chip->option.sdr50_tx_phase;
			else
				final_phase = chip->option.sdr50_rx_phase;
			RTS51X_DEBUGP("SDR50 tuning dir:%d all pass,"
					"so select default phase:0x%x.\n",
					tune_dir, final_phase);
		}
		goto Search_Finish;
	}

	cont_path_cnt = 0;
	new_block = 1;
	j = 0;
	for (i = 0; i < MAX_PHASE + 1; i++) {
		if (phase_map & (1 << i)) {
			if (new_block) {
				new_block = 0;
				j = cont_path_cnt++;
				path[j].start = i;
				path[j].end = i;
			} else {
				path[j].end = i;
			}
		} else {
			new_block = 1;
			if (cont_path_cnt) {
				int idx = cont_path_cnt - 1;
				path[idx].len =
				    path[idx].end - path[idx].start + 1;
				path[idx].mid =
				    path[idx].start + path[idx].len / 2;
			}
		}
	}

	if (cont_path_cnt == 0) {
		RTS51X_DEBUGP("No continuous phase path\n");
		goto Search_Finish;
	} else {
		int idx = cont_path_cnt - 1;
		path[idx].len = path[idx].end - path[idx].start + 1;
		path[idx].mid = path[idx].start + path[idx].len / 2;
	}

	if ((path[0].start == 0) &&
			(path[cont_path_cnt - 1].end == MAX_PHASE)) {
		path[0].start = path[cont_path_cnt - 1].start - MAX_PHASE - 1;
		path[0].len += path[cont_path_cnt - 1].len;
		path[0].mid = path[0].start + path[0].len / 2;
		if (path[0].mid < 0)
			path[0].mid += MAX_PHASE + 1;
		cont_path_cnt--;
	}
	max_len = 0;
	final_phase = 0;
	final_path_idx = 0;
	for (i = 0; i < cont_path_cnt; i++) {
		if (path[i].len > max_len) {
			max_len = path[i].len;
			final_phase = (u8) path[i].mid;
			final_path_idx = i;
		}

		RTS51X_DEBUGP("path[%d].start = %d\n", i, path[i].start);
		RTS51X_DEBUGP("path[%d].end = %d\n", i, path[i].end);
		RTS51X_DEBUGP("path[%d].len = %d\n", i, path[i].len);
		RTS51X_DEBUGP("path[%d].mid = %d\n", i, path[i].mid);
		RTS51X_DEBUGP("\n");
	}

	if ((tune_dir == TUNE_TX) && (CHK_SD_SDR50(sd_card))
	    && chip->option.sdr50_phase_sel) {
		if (max_len > 6) {
			int temp_mid = (max_len - 6) / 2;
			int temp_final_phase =
			    path[final_path_idx].end - (max_len -
							(3 + temp_mid));

			if (temp_final_phase < 0)
				final_phase = temp_final_phase + MAX_PHASE + 1;
			else
				final_phase = (u8) temp_final_phase;
		}
	}

Search_Finish:
	RTS51X_DEBUGP("Final chosen phase: %d\n", final_phase);
	return final_phase;
}

static int sd_tuning_rx(struct rts51x_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;
	int i, j;
	u32 raw_phase_map[3], phase_map;
	u8 final_phase;
	int (*tuning_cmd) (struct rts51x_chip *chip, u8 sample_point);

	if (CHK_SD(sd_card)) {
		if (CHK_SD_DDR50(sd_card))
			tuning_cmd = sd_ddr_tuning_rx_cmd;
		else
			tuning_cmd = sd_sdr_tuning_rx_cmd;
	} else {
		if (CHK_MMC_DDR52(sd_card))
			tuning_cmd = mmc_ddr_tunning_rx_cmd;
		else
			TRACE_RET(chip, STATUS_FAIL);
	}

	for (i = 0; i < 3; i++) {
		raw_phase_map[i] = 0;
		for (j = MAX_PHASE; j >= 0; j--) {
			if (monitor_card_cd(chip, SD_CARD) == CD_NOT_EXIST) {
				sd_set_reset_fail(chip, SD_RESET_FAIL);
				TRACE_RET(chip, STATUS_FAIL);
			}

			retval = tuning_cmd(chip, (u8) j);
			if (retval == STATUS_SUCCESS)
				raw_phase_map[i] |= 1 << j;
			else
				RTS51X_DEBUGP("Tuning phase %d fail\n", j);
		}
	}

	phase_map = raw_phase_map[0] & raw_phase_map[1] & raw_phase_map[2];
	for (i = 0; i < 3; i++)
		RTS51X_DEBUGP("RX raw_phase_map[%d] = 0x%04x\n", i,
			       raw_phase_map[i]);
	RTS51X_DEBUGP("RX phase_map = 0x%04x\n", phase_map);

	final_phase = sd_search_final_phase(chip, phase_map, TUNE_RX);
	if (final_phase == 0xFF)
		TRACE_RET(chip, STATUS_FAIL);

	retval = tuning_cmd(chip, final_phase);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	return STATUS_SUCCESS;
}

static int sd_ddr_pre_tuning_tx(struct rts51x_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;
	u8 i;
	u8 pre_tune_tx_phase;
	u32 pre_tune_phase_map;

	RTS51X_WRITE_REG(chip, SD_CFG3, SD_RSP_80CLK_TIMEOUT_EN,
			 SD_RSP_80CLK_TIMEOUT_EN);

	pre_tune_tx_phase = 0xFF;
	pre_tune_phase_map = 0x0000;
	for (i = 0; i < MAX_PHASE + 1; i++) {
		if (monitor_card_cd(chip, SD_CARD) == CD_NOT_EXIST) {
			sd_set_reset_fail(chip, SD_RESET_FAIL);
			TRACE_RET(chip, STATUS_FAIL);
		}

		retval = sd_change_phase(chip, (u8) i, TUNE_TX);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, retval);

		retval =
		    sd_send_cmd_get_rsp(chip, SEND_STATUS, sd_card->sd_addr,
					SD_RSP_TYPE_R1, NULL, 0);
		if ((retval == STATUS_SUCCESS)
		    || !sd_check_err_code(chip, SD_RSP_TIMEOUT))
			pre_tune_phase_map |= (u32) 1 << i;
	}

	RTS51X_WRITE_REG(chip, SD_CFG3, SD_RSP_80CLK_TIMEOUT_EN, 0);

	pre_tune_tx_phase =
	    sd_search_final_phase(chip, pre_tune_phase_map, TUNE_TX);
	if (pre_tune_tx_phase == 0xFF)
		TRACE_RET(chip, STATUS_FAIL);

	sd_change_phase(chip, pre_tune_tx_phase, TUNE_TX);
	RTS51X_DEBUGP("DDR TX pre tune phase: %d\n", (int)pre_tune_tx_phase);

	return STATUS_SUCCESS;
}

static int sd_tuning_tx(struct rts51x_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;
	int i, j;
	u32 raw_phase_map[3], phase_map;
	u8 final_phase;
	int (*tuning_cmd) (struct rts51x_chip *chip, u8 sample_point);

	if (CHK_SD(sd_card)) {
		if (CHK_SD_DDR50(sd_card))
			tuning_cmd = sd_ddr_tuning_tx_cmd;
		else
			tuning_cmd = sd_sdr_tuning_tx_cmd;
	} else {
		if (CHK_MMC_DDR52(sd_card))
			tuning_cmd = sd_ddr_tuning_tx_cmd;
		else
			TRACE_RET(chip, STATUS_FAIL);
	}

	for (i = 0; i < 3; i++) {
		raw_phase_map[i] = 0;
		for (j = MAX_PHASE; j >= 0; j--) {
			if (monitor_card_cd(chip, SD_CARD) == CD_NOT_EXIST) {
				sd_set_reset_fail(chip, SD_RESET_FAIL);
				TRACE_RET(chip, STATUS_FAIL);
			}

			retval = tuning_cmd(chip, (u8) j);
			if (retval == STATUS_SUCCESS)
				raw_phase_map[i] |= 1 << j;
			else
				RTS51X_DEBUGP("Tuning phase %d fail\n", j);
		}
	}

	phase_map = raw_phase_map[0] & raw_phase_map[1] & raw_phase_map[2];
	for (i = 0; i < 3; i++)
		RTS51X_DEBUGP("TX raw_phase_map[%d] = 0x%04x\n", i,
			       raw_phase_map[i]);
	RTS51X_DEBUGP("TX phase_map = 0x%04x\n", phase_map);

	final_phase = sd_search_final_phase(chip, phase_map, TUNE_TX);
	if (final_phase == 0xFF)
		TRACE_RET(chip, STATUS_FAIL);

	retval = tuning_cmd(chip, final_phase);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	return STATUS_SUCCESS;
}

static int sd_sdr_tuning(struct rts51x_chip *chip)
{
	int retval;

	retval = sd_tuning_tx(chip);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	retval = sd_tuning_rx(chip);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	return STATUS_SUCCESS;
}

static int sd_ddr_tuning(struct rts51x_chip *chip)
{
	int retval;

	if (!(chip->option.sd_ctl & SD_DDR_TX_PHASE_SET_BY_USER)) {
		retval = sd_ddr_pre_tuning_tx(chip);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, retval);
	} else {
		retval =
		    sd_change_phase(chip, (u8) chip->option.sd_ddr_tx_phase,
				    TUNE_TX);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, retval);
	}

	retval = sd_tuning_rx(chip);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	if (!(chip->option.sd_ctl & SD_DDR_TX_PHASE_SET_BY_USER)) {
		retval = sd_tuning_tx(chip);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, retval);
	}

	return STATUS_SUCCESS;
}

static int mmc_ddr_tuning(struct rts51x_chip *chip)
{
	int retval;

	if (!(chip->option.sd_ctl & MMC_DDR_TX_PHASE_SET_BY_USER)) {
		retval = sd_ddr_pre_tuning_tx(chip);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, retval);
	} else {
		retval =
		    sd_change_phase(chip, (u8) chip->option.mmc_ddr_tx_phase,
				    TUNE_TX);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, retval);
	}

	retval = sd_tuning_rx(chip);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	if (!(chip->option.sd_ctl & MMC_DDR_TX_PHASE_SET_BY_USER)) {
		retval = sd_tuning_tx(chip);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, retval);
	}

	return STATUS_SUCCESS;
}

int sd_switch_clock(struct rts51x_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;
	int re_tuning = 0;

	retval = rts51x_select_card(chip, SD_CARD);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	if (CHK_SD30_SPEED(sd_card) || CHK_MMC_DDR52(sd_card)) {
		if (sd_card->sd_clock != chip->cur_clk)
			re_tuning = 1;
	}

	retval = switch_clock(chip, sd_card->sd_clock);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	if (re_tuning) {
		if (CHK_SD(sd_card)) {
			if (CHK_SD_DDR50(sd_card))
				retval = sd_ddr_tuning(chip);
			else
				retval = sd_sdr_tuning(chip);
		} else {
			if (CHK_MMC_DDR52(sd_card))
				retval = mmc_ddr_tuning(chip);
		}

		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, retval);
	}

	return STATUS_SUCCESS;
}

static int sd_prepare_reset(struct rts51x_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;

	if (chip->asic_code)
		sd_card->sd_clock = 29;
	else
		sd_card->sd_clock = CLK_30;

	/* Set SD Clocks */
	retval = sd_set_init_para(chip);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	rts51x_init_cmd(chip);

	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CFG1, 0xFF,
		       SD_CLK_DIVIDE_128 | SD_20_MODE | SD_BUS_WIDTH_1);
	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_SAMPLE_POINT_CTL, 0xFF,
		       SD20_RX_POS_EDGE);
	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_PUSH_POINT_CTL, 0xFF, 0);

	retval = rts51x_send_cmd(chip, MODE_C, 100);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	retval = rts51x_select_card(chip, SD_CARD);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	return STATUS_SUCCESS;
}

static void sd_pull_ctl_disable(struct rts51x_chip *chip)
{
	if (CHECK_PKG(chip, LQFP48)) {
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL1, 0xFF, 0x55);
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL2, 0xFF, 0x55);
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL3, 0xFF, 0x95);
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL4, 0xFF, 0x55);
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL5, 0xFF, 0x55);
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL6, 0xFF, 0xA5);
	} else {
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL1, 0xFF, 0x65);
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL2, 0xFF, 0x55);
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL3, 0xFF, 0x95);
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL4, 0xFF, 0x55);
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL5, 0xFF, 0x56);
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL6, 0xFF, 0x59);
	}
}

static void sd_pull_ctl_enable(struct rts51x_chip *chip)
{
	if (CHECK_PKG(chip, LQFP48)) {
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL1, 0xFF, 0xAA);
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL2, 0xFF, 0xAA);
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL3, 0xFF, 0xA9);
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL4, 0xFF, 0x55);
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL5, 0xFF, 0x55);
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL6, 0xFF, 0xA5);
	} else {
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL1, 0xFF, 0xA5);
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL2, 0xFF, 0x9A);
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL3, 0xFF, 0xA5);
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL4, 0xFF, 0x9A);
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL5, 0xFF, 0x65);
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL6, 0xFF, 0x5A);
	}
}

static int sd_init_power(struct rts51x_chip *chip)
{
	int retval;

	rts51x_init_cmd(chip);

	rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_PWR_CTL, LDO3318_PWR_MASK,
		       LDO_ON);
	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_PAD_CTL, SD_IO_USING_1V8,
		       SD_IO_USING_3V3);
	if (chip->asic_code)
		rts51x_add_cmd(chip, WRITE_REG_CMD, LDO_POWER_CFG,
			       TUNE_SD18_MASK, TUNE_SD18_3V3);
	if (chip->asic_code)
		sd_pull_ctl_disable(chip);
	else
		rts51x_add_cmd(chip, WRITE_REG_CMD, FPGA_PULL_CTL,
			       FPGA_SD_PULL_CTL_BIT | 0x20,
			       FPGA_SD_PULL_CTL_BIT);
	rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_OE, SD_OUTPUT_EN, 0);
	if (!chip->option.FT2_fast_mode)
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_PWR_CTL, POWER_MASK,
			       POWER_OFF);

	retval = rts51x_send_cmd(chip, MODE_C, 100);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);
	if (!chip->option.FT2_fast_mode) {
#ifdef SD_XD_IO_FOLLOW_PWR
		if (CHECK_PKG(chip, LQFP48)
		    || chip->option.rts5129_D3318_off_enable)
			rts51x_write_register(chip, CARD_PWR_CTL,
					LDO_OFF, LDO_OFF);
#endif
		wait_timeout(250);

#ifdef SD_XD_IO_FOLLOW_PWR
		if (CHECK_PKG(chip, LQFP48)
		    || chip->option.rts5129_D3318_off_enable) {
			rts51x_init_cmd(chip);
			if (chip->asic_code)
				sd_pull_ctl_enable(chip);
			else
				rts51x_add_cmd(chip, WRITE_REG_CMD,
					       FPGA_PULL_CTL,
					       FPGA_SD_PULL_CTL_BIT | 0x20, 0);
			retval = rts51x_send_cmd(chip, MODE_C, 100);
			if (retval != STATUS_SUCCESS)
				TRACE_RET(chip, retval);
		} else {
			if (chip->asic_code)
				rts51x_write_register(chip, CARD_PULL_CTL6,
						      0x03, 0x00);
		}
#endif

		/* Power on card */
		retval = card_power_on(chip, SD_CARD);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, retval);

		wait_timeout(260);

#ifdef SUPPORT_OCP
		rts51x_get_card_status(chip, &(chip->card_status));
		chip->ocp_stat = (chip->card_status >> 4) & 0x03;

		if (chip->ocp_stat & (MS_OCP_NOW | MS_OCP_EVER)) {
			RTS51X_DEBUGP("Over current, OCPSTAT is 0x%x\n",
				       chip->ocp_stat);
			TRACE_RET(chip, STATUS_FAIL);
		}
#endif
	}

	rts51x_init_cmd(chip);
	if (chip->asic_code) {
		sd_pull_ctl_enable(chip);
	} else {
		rts51x_add_cmd(chip, WRITE_REG_CMD, FPGA_PULL_CTL,
			       FPGA_SD_PULL_CTL_BIT | 0x20, 0);
	}
	retval = rts51x_send_cmd(chip, MODE_C, 100);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);
#ifdef SD_XD_IO_FOLLOW_PWR
	rts51x_write_register(chip, CARD_INT_PEND, XD_INT | MS_INT | SD_INT,
			      XD_INT | MS_INT | SD_INT);
#endif

	RTS51X_WRITE_REG(chip, CARD_OE, SD_OUTPUT_EN, SD_OUTPUT_EN);

	return STATUS_SUCCESS;
}

static int sd_dummy_clock(struct rts51x_chip *chip)
{
	RTS51X_WRITE_REG(chip, SD_BUS_STAT, SD_CLK_TOGGLE_EN, SD_CLK_TOGGLE_EN);
	wait_timeout(5);
	RTS51X_WRITE_REG(chip, SD_BUS_STAT, SD_CLK_TOGGLE_EN, 0x00);

	return STATUS_SUCCESS;
}

int reset_sd(struct rts51x_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval, i = 0, j = 0, k = 0, hi_cap_flow = 0;
	int sd_dont_switch = 0;
	int support_1v8 = 0;
	u8 rsp[16];
	u8 switch_bus_width;
	u32 voltage = 0;
	u8 cmd[5], buf[64];
	u16 sd_card_type;

	SET_SD(sd_card);
	CLR_RETRY_SD20_MODE(sd_card);
Switch_Fail:
	i = 0;
	j = 0;
	k = 0;
	hi_cap_flow = 0;
	support_1v8 = 0;

	retval = sd_prepare_reset(chip);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	sd_dummy_clock(chip);

	/* Start Initialization Process of SD Card */
RTY_SD_RST:
	retval =
	    sd_send_cmd_get_rsp(chip, GO_IDLE_STATE, 0, SD_RSP_TYPE_R0, NULL,
				0);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	wait_timeout(20);

	retval =
	    sd_send_cmd_get_rsp(chip, SEND_IF_COND, 0x000001AA, SD_RSP_TYPE_R7,
				rsp, 5);
	if (retval == STATUS_SUCCESS) {
		if ((rsp[4] == 0xAA) && ((rsp[3] & 0x0f) == 0x01)) {
			hi_cap_flow = 1;
			if (CHK_RETRY_SD20_MODE(sd_card)) {
				voltage =
				    SUPPORT_VOLTAGE |
				    SUPPORT_HIGH_AND_EXTENDED_CAPACITY;
			} else {
				voltage =
				    SUPPORT_VOLTAGE |
				    SUPPORT_HIGH_AND_EXTENDED_CAPACITY |
				    SUPPORT_MAX_POWER_PERMANCE | SUPPORT_1V8;
			}
		}
	}

	if (!hi_cap_flow) {
		voltage = SUPPORT_VOLTAGE;

		retval =
		    sd_send_cmd_get_rsp(chip, GO_IDLE_STATE, 0, SD_RSP_TYPE_R0,
					NULL, 0);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, retval);
		wait_timeout(20);
	}

	/* ACMD41 */
	do {
		{
			u8 temp = 0;
			rts51x_read_register(chip, CARD_INT_PEND, &temp);
			RTS51X_DEBUGP("CARD_INT_PEND:%x\n", temp);
			if (temp & SD_INT) {
				chip->reset_need_retry = 1;
				rts51x_write_register(chip, CARD_INT_PEND,
						      XD_INT | SD_INT | MS_INT,
						      XD_INT | SD_INT | MS_INT);
				sd_set_reset_fail(chip, SD_RESET_FAIL);
				TRACE_RET(chip, STATUS_FAIL);
			}
		}

RTY_CMD55:
		retval =
		    sd_send_cmd_get_rsp(chip, APP_CMD, 0, SD_RSP_TYPE_R1, NULL,
					0);
		if (retval != STATUS_SUCCESS) {
			if (monitor_card_cd(chip, SD_CARD) == CD_NOT_EXIST) {
				sd_set_reset_fail(chip, SD_RESET_FAIL);
				TRACE_RET(chip, STATUS_FAIL);
			}

			j++;
			if (chip->option.speed_mmc) {
				if (j < 2)
					goto RTY_CMD55;
				else
					TRACE_RET(chip, STATUS_FAIL);
			} else {
				if (j < 3)
					goto RTY_SD_RST;
				else
					TRACE_RET(chip, STATUS_FAIL);
			}
		}

		retval =
		    sd_send_cmd_get_rsp(chip, SD_APP_OP_COND, voltage,
					SD_RSP_TYPE_R3, rsp, 5);
		if (retval != STATUS_SUCCESS) {
			k++;
			if (k < 3)
				goto RTY_SD_RST;
			else
				TRACE_RET(chip, STATUS_FAIL);
		}

		i++;
		wait_timeout(20);
	} while (!(rsp[1] & 0x80) && (i < 255)); /* Not complete power on */

	if (i == 255) {
		/* Time out */
		TRACE_RET(chip, STATUS_FAIL);
	}

	if (hi_cap_flow) {
		if (rsp[1] & 0x40)
			SET_SD_HCXC(sd_card);
		else
			CLR_SD_HCXC(sd_card);
		if (!CHK_RETRY_SD20_MODE(sd_card)) {
			if ((CHK_SD_HCXC(sd_card)) && (CHECK_UHS50(chip))) {
				support_1v8 = (rsp[1] & 0x01) ? 1 : 0;
				RTS51X_DEBUGP("support_1v8 = %d\n",
					       support_1v8);
			}
		}
	} else {
		CLR_SD_HCXC(sd_card);
		support_1v8 = 0;
	}

	/* CMD11: Switch Voltage */
	if (support_1v8 && CHECK_UHS50(chip)
	    && !(((u8) chip->option.sd_speed_prior & SDR104_SUPPORT) ==
		 HS_SUPPORT)) {
		retval = sd_voltage_switch(chip);
		if (retval != STATUS_SUCCESS) {
			SET_RETRY_SD20_MODE(sd_card);
			sd_init_power(chip);
			RTS51X_DEBUGP("1.8v switch fail\n");
			goto Switch_Fail;
		}
	}

	/* CMD 2 */
	retval =
	    sd_send_cmd_get_rsp(chip, ALL_SEND_CID, 0, SD_RSP_TYPE_R2, NULL, 0);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	/* CMD 3 */
	retval =
	    sd_send_cmd_get_rsp(chip, SEND_RELATIVE_ADDR, 0, SD_RSP_TYPE_R6,
				rsp, 5);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	sd_card->sd_addr = (u32) rsp[1] << 24;
	sd_card->sd_addr += (u32) rsp[2] << 16;

	/* Get CSD register for Calculating Timing,Capacity,
	 * Check CSD to determaine as if this is the SD ROM card */
	retval = sd_check_csd(chip, 1);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);
	/* Select SD card */
	retval = sd_select_card(chip, 1);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	/* ACMD42 */
	retval =
	    sd_send_cmd_get_rsp(chip, APP_CMD, sd_card->sd_addr, SD_RSP_TYPE_R1,
				NULL, 0);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	retval =
	    sd_send_cmd_get_rsp(chip, SET_CLR_CARD_DETECT, 0, SD_RSP_TYPE_R1,
				NULL, 0);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	if (support_1v8) {
		/* ACMD6 */
		retval =
		    sd_send_cmd_get_rsp(chip, APP_CMD, sd_card->sd_addr,
					SD_RSP_TYPE_R1, NULL, 0);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, retval);
		/* Enable 4 bit data bus */
		retval =
		    sd_send_cmd_get_rsp(chip, SET_BUS_WIDTH, 2, SD_RSP_TYPE_R1,
					NULL, 0);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, retval);
		switch_bus_width = SD_BUS_WIDTH_4;
	} else {
		switch_bus_width = SD_BUS_WIDTH_1;
	}

	/* Set block length 512 bytes for all block commands */
	retval = sd_send_cmd_get_rsp(chip, SET_BLOCKLEN,
			0x200, SD_RSP_TYPE_R1, NULL, 0);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	RTS51X_WRITE_REG(chip, SD_CFG1, SD_CLK_DIVIDE_MASK, SD_CLK_DIVIDE_0);

	if (!(sd_card->raw_csd[4] & 0x40)) {
		sd_dont_switch = 1;
		RTS51X_DEBUGP("Not support class ten\n");
	}

	if (!sd_dont_switch) {
		/* Check the card whether flow SD1.1 spec or higher */
		retval = sd_check_spec(chip, switch_bus_width);
		if (retval == STATUS_SUCCESS) {
			retval = sd_switch_function(chip, switch_bus_width);
			if (retval != STATUS_SUCCESS) {
				if ((sd_card->sd_switch_fail ==
				     SDR104_SUPPORT_MASK)
				    || (sd_card->sd_switch_fail ==
					DDR50_SUPPORT_MASK)
				    || (sd_card->sd_switch_fail ==
					    SDR50_SUPPORT_MASK)) {
					sd_init_power(chip);
					SET_RETRY_SD20_MODE(sd_card);
				} else if (sd_card->sd_switch_fail ==
						HS_SUPPORT_MASK) {
					sd_dont_switch = 1;
				}
				goto Switch_Fail;
			}
		} else {
			if (support_1v8) {
				SET_RETRY_SD20_MODE(sd_card);
				sd_init_power(chip);
				sd_dont_switch = 1;

				goto Switch_Fail;
			}
		}
	}

	if (!support_1v8) {
		/* ACMD6 */
		retval =
		    sd_send_cmd_get_rsp(chip, APP_CMD, sd_card->sd_addr,
					SD_RSP_TYPE_R1, NULL, 0);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, retval);
		/* Enable 4 bit data bus */
		retval =
		    sd_send_cmd_get_rsp(chip, SET_BUS_WIDTH, 2, SD_RSP_TYPE_R1,
					NULL, 0);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, retval);
	}

	if (CHK_SD30_SPEED(sd_card)) {
		rts51x_write_register(chip, SD30_DRIVE_SEL, SD30_DRIVE_MASK,
				      0x03);

		retval = sd_set_init_para(chip);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, retval);

		if (CHK_SD_DDR50(sd_card))
			retval = sd_ddr_tuning(chip);
		else
			retval = sd_sdr_tuning(chip);

		if (retval != STATUS_SUCCESS) {
			SET_RETRY_SD20_MODE(sd_card);
			RTS51X_DEBUGP("tuning phase fail,goto SD20 mode\n");
			sd_init_power(chip);
			CLR_SD30_SPEED(sd_card);
			goto Switch_Fail;
		}
		if (STATUS_SUCCESS ==
		    sd_wait_currentstate_dataready(chip, 0x08, 1, 20)) {
			cmd[0] = 0x40 | READ_SINGLE_BLOCK;
			cmd[1] = 0x00;
			cmd[2] = 0x00;
			cmd[3] = 0x00;
			cmd[4] = 0x00;
			retval =
			    sd_read_data(chip, SD_TM_NORMAL_READ, cmd, 5, 512,
					 1, SD_BUS_WIDTH_4, NULL, 0, 600);
			if (retval != STATUS_SUCCESS) {
				SET_RETRY_SD20_MODE(sd_card);
				RTS51X_DEBUGP("read lba0 fail,"
							"goto SD20 mode\n");
				sd_init_power(chip);
				CLR_SD30_SPEED(sd_card);
				goto Switch_Fail;
			}
		}
	}
	sd_send_cmd_get_rsp(chip, SEND_STATUS, sd_card->sd_addr, SD_RSP_TYPE_R1,
			    NULL, 0);

	retval = sd_send_cmd_get_rsp(chip, APP_CMD, sd_card->sd_addr,
			SD_RSP_TYPE_R1, NULL, 0);
	if (retval == STATUS_SUCCESS) {
		int ret;
		cmd[0] = 0x40 | SEND_STATUS;
		cmd[1] = 0x00;
		cmd[2] = 0x00;
		cmd[3] = 0x00;
		cmd[4] = 0x00;
		ret =
		    sd_read_data(chip, SD_TM_NORMAL_READ, cmd, 5, 64, 1,
				 SD_BUS_WIDTH_4, buf, 64, 600);
		if (ret == STATUS_SUCCESS) {
			sd_card_type = ((u16) buf[2] << 8) | (u16) buf[3];
			RTS51X_DEBUGP("sd_card_type:0x%4x\n", sd_card_type);
			if ((sd_card_type == 0x0001)
			    || (sd_card_type == 0x0002))
				chip->card_wp |= SD_CARD;
		} else {
			rts51x_clear_sd_error(chip);
			sd_send_cmd_get_rsp(chip, SEND_STATUS, sd_card->sd_addr,
					    SD_RSP_TYPE_R1, NULL, 0);
		}
	} else {
		rts51x_clear_sd_error(chip);
		sd_send_cmd_get_rsp(chip, SEND_STATUS, sd_card->sd_addr,
				    SD_RSP_TYPE_R1, NULL, 0);
	}

	/* Check SD Machanical Write-Protect Switch */
	retval = rts51x_get_card_status(chip, &(chip->card_status));
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);
	if (chip->card_status & SD_WP)
		chip->card_wp |= SD_CARD;

	chip->card_bus_width[chip->card2lun[SD_CARD]] = 4;

	return STATUS_SUCCESS;
}

static int mmc_test_switch_bus(struct rts51x_chip *chip, u8 width)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;
	u8 buf[8] = { 0 }, bus_width;
	u16 byte_cnt;
	int len;

	retval =
	    sd_send_cmd_get_rsp(chip, BUSTEST_W, 0, SD_RSP_TYPE_R1, NULL, 0);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	if (width == MMC_8BIT_BUS) {
		buf[0] = 0x55;
		buf[1] = 0xAA;
		len = 8;
		byte_cnt = 8;
		bus_width = SD_BUS_WIDTH_8;
	} else {
		buf[0] = 0x5A;
		len = 4;
		byte_cnt = 4;
		bus_width = SD_BUS_WIDTH_4;
	}

	retval = sd_write_data(chip, SD_TM_AUTO_WRITE_3,
			       NULL, 0, byte_cnt, 1, bus_width, buf, len, 100);
	if (retval != STATUS_SUCCESS) {
		u8 val1 = 0, val2 = 0;
		rts51x_ep0_read_register(chip, SD_STAT1, &val1);
		rts51x_ep0_read_register(chip, SD_STAT2, &val2);
		rts51x_clear_sd_error(chip);
		if ((val1 & 0xE0) || val2)
			TRACE_RET(chip, STATUS_FAIL);
	}
	RTS51X_DEBUGP("SD/MMC CMD %d\n", BUSTEST_R);

	rts51x_init_cmd(chip);

	/* CMD14 */
	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CMD0, 0xFF, 0x40 | BUSTEST_R);

	if (width == MMC_8BIT_BUS)
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_BYTE_CNT_L, 0xFF, 0x08);
	else
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_BYTE_CNT_L, 0xFF, 0x04);

	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_BLOCK_CNT_L, 0xFF, 1);
	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_BLOCK_CNT_H, 0xFF, 0);

	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CFG2, 0xFF,
		       SD_CALCULATE_CRC7 | SD_NO_CHECK_CRC16 |
		       SD_NO_WAIT_BUSY_END | SD_CHECK_CRC7 | SD_RSP_LEN_6);
	rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_DATA_SOURCE, 0x01,
		       PINGPONG_BUFFER);
	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_TRANSFER, 0xFF,
		       SD_TM_NORMAL_READ | SD_TRANSFER_START);
	rts51x_add_cmd(chip, CHECK_REG_CMD, SD_TRANSFER, SD_TRANSFER_END,
		       SD_TRANSFER_END);

	rts51x_add_cmd(chip, READ_REG_CMD, PPBUF_BASE2, 0, 0);
	if (width == MMC_8BIT_BUS) {
		len = 3;
		rts51x_add_cmd(chip, READ_REG_CMD, PPBUF_BASE2 + 1, 0, 0);
	} else {
		len = 2;
	}

	retval = rts51x_send_cmd(chip, MODE_CR, 100);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	retval = rts51x_get_rsp(chip, len, 100);
	if (CHECK_SD_TRANS_FAIL(chip, retval)) {
		rts51x_clear_sd_error(chip);
		TRACE_RET(chip, STATUS_FAIL);
	}

	rts51x_read_rsp_buf(chip, 1, buf, 2);

	if (width == MMC_8BIT_BUS) {
		RTS51X_DEBUGP("BUSTEST_R [8bits]: 0x%02x 0x%02x\n",
					buf[0], buf[1]);
		if ((buf[0] == 0xAA) && (buf[1] == 0x55)) {
			u8 rsp[5];
			u32 arg;

			if (CHK_MMC_DDR52(sd_card))
				arg = 0x03B70600;
			else
				arg = 0x03B70200;
			/* Switch MMC to  8-bit mode */
			retval =
			    sd_send_cmd_get_rsp(chip, SWITCH, arg,
						SD_RSP_TYPE_R1b, rsp, 5);
			if ((retval == STATUS_SUCCESS)
			    && !(rsp[4] & MMC_SWITCH_ERR))
				return STATUS_SUCCESS;
		}
	} else {
		RTS51X_DEBUGP("BUSTEST_R [4bits]: 0x%02x\n", buf[0]);
		if (buf[0] == 0xA5) {
			u8 rsp[5];
			u32 arg;

			if (CHK_MMC_DDR52(sd_card))
				arg = 0x03B70500;
			else
				arg = 0x03B70100;
			/* Switch MMC to  4-bit mode */
			retval =
			    sd_send_cmd_get_rsp(chip, SWITCH, arg,
						SD_RSP_TYPE_R1b, rsp, 5);
			if ((retval == STATUS_SUCCESS)
			    && !(rsp[4] & MMC_SWITCH_ERR))
				return STATUS_SUCCESS;
		}
	}

	TRACE_RET(chip, STATUS_FAIL);
}

static int mmc_switch_timing_bus(struct rts51x_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;
	u8 card_type, card_type_mask = 0;
	u8 buf[6];

	CLR_MMC_HS(sd_card);

	RTS51X_DEBUGP("SD/MMC CMD %d\n", SEND_EXT_CSD);

	rts51x_init_cmd(chip);

	/* SEND_EXT_CSD command */
	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CMD0, 0xFF,
			0x40 | SEND_EXT_CSD);
	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CMD1, 0xFF, 0);
	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CMD2, 0xFF, 0);
	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CMD3, 0xFF, 0);
	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CMD4, 0xFF, 0);

	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_BYTE_CNT_L, 0xFF, 0);
	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_BYTE_CNT_H, 0xFF, 2);
	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_BLOCK_CNT_L, 0xFF, 1);
	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_BLOCK_CNT_H, 0xFF, 0);

	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CFG2, 0xFF,
		       SD_CALCULATE_CRC7 | SD_CHECK_CRC16 | SD_NO_WAIT_BUSY_END
		       | SD_CHECK_CRC7 | SD_RSP_LEN_6);
	rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_DATA_SOURCE, 0x01,
		       PINGPONG_BUFFER);
	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_TRANSFER, 0xFF,
		       SD_TM_NORMAL_READ | SD_TRANSFER_START);
	rts51x_add_cmd(chip, CHECK_REG_CMD, SD_TRANSFER, SD_TRANSFER_END,
		       SD_TRANSFER_END);

	rts51x_add_cmd(chip, READ_REG_CMD, PPBUF_BASE2 + 196, 0xFF, 0);
	rts51x_add_cmd(chip, READ_REG_CMD, PPBUF_BASE2 + 212, 0xFF, 0);
	rts51x_add_cmd(chip, READ_REG_CMD, PPBUF_BASE2 + 213, 0xFF, 0);
	rts51x_add_cmd(chip, READ_REG_CMD, PPBUF_BASE2 + 214, 0xFF, 0);
	rts51x_add_cmd(chip, READ_REG_CMD, PPBUF_BASE2 + 215, 0xFF, 0);

	retval = rts51x_send_cmd(chip, MODE_CR, 100);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	retval = rts51x_get_rsp(chip, 6, 1000);

	if (CHECK_SD_TRANS_FAIL(chip, retval)) {
		if (retval == STATUS_TIMEDOUT) {
			rts51x_clear_sd_error(chip);
			sd_send_cmd_get_rsp(chip, SEND_STATUS, sd_card->sd_addr,
					    SD_RSP_TYPE_R1, NULL, 0);
		}
		TRACE_RET(chip, STATUS_FAIL);
	}

	rts51x_read_rsp_buf(chip, 0, buf, 6);

	if (buf[0] & SD_TRANSFER_ERR) {
		sd_send_cmd_get_rsp(chip, SEND_STATUS, sd_card->sd_addr,
				    SD_RSP_TYPE_R1, NULL, 0);
		TRACE_RET(chip, STATUS_FAIL);
	}
	if (CHK_MMC_SECTOR_MODE(sd_card))
		sd_card->capacity =
		    ((u32) buf[5] << 24) | ((u32) buf[4] << 16) |
		    ((u32) buf[3] << 8) | ((u32) buf[2]);
	if (CHECK_UHS50(chip))
		card_type_mask = 0x07;
	else
		card_type_mask = 0x03;

	card_type = buf[1] & card_type_mask;
	if (card_type) {
		/* CARD TYPE FIELD = DDR52MHz, 52MHz or 26MHz */
		u8 rsp[5];

		if (card_type & 0x04)
			SET_MMC_DDR52(sd_card);
		else if (card_type & 0x02)
			SET_MMC_52M(sd_card);
		else
			SET_MMC_26M(sd_card);

		retval =
		    sd_send_cmd_get_rsp(chip, SWITCH, 0x03B90100,
					SD_RSP_TYPE_R1b, rsp, 5);
		if ((retval != STATUS_SUCCESS) || (rsp[4] & MMC_SWITCH_ERR))
			CLR_MMC_HS(sd_card);
	}
	sd_choose_proper_clock(chip);
	retval = switch_clock(chip, sd_card->sd_clock);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	/* Test Bus Procedure */
	if (mmc_test_switch_bus(chip, MMC_8BIT_BUS) == STATUS_SUCCESS) {
		SET_MMC_8BIT(sd_card);
		chip->card_bus_width[chip->card2lun[SD_CARD]] = 8;
	} else if (mmc_test_switch_bus(chip, MMC_4BIT_BUS) == STATUS_SUCCESS) {
		SET_MMC_4BIT(sd_card);
		chip->card_bus_width[chip->card2lun[SD_CARD]] = 4;
	} else {
		CLR_MMC_8BIT(sd_card);
		CLR_MMC_4BIT(sd_card);
	}

	return STATUS_SUCCESS;
}

static int reset_mmc(struct rts51x_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval, i = 0, j = 0, k = 0;
	u8 rsp[16];
	u8 spec_ver = 0;
	u8 change_to_ddr52 = 1;
	u8 cmd[5];

MMC_DDR_FAIL:

	retval = sd_prepare_reset(chip);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	SET_MMC(sd_card);

RTY_MMC_RST:
	retval =
	    sd_send_cmd_get_rsp(chip, GO_IDLE_STATE, 0, SD_RSP_TYPE_R0, NULL,
				0);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	do {
		{
			u8 temp = 0;
			rts51x_read_register(chip, CARD_INT_PEND, &temp);
			if (temp & SD_INT) {
				chip->reset_need_retry = 1;
				rts51x_write_register(chip, CARD_INT_PEND,
						      XD_INT | SD_INT | MS_INT,
						      XD_INT | SD_INT | MS_INT);
				sd_set_reset_fail(chip, MMC_RESET_FAIL);
				TRACE_RET(chip, STATUS_FAIL);
			}
		}

		/* CMD  1 */
		retval = sd_send_cmd_get_rsp(chip, SEND_OP_COND,
					     (SUPPORT_VOLTAGE | 0x40000000),
					     SD_RSP_TYPE_R3, rsp, 5);
		if (retval != STATUS_SUCCESS) {
			if (monitor_card_cd(chip, SD_CARD) == CD_NOT_EXIST) {
				sd_set_reset_fail(chip, MMC_RESET_FAIL);
				TRACE_RET(chip, STATUS_FAIL);
			}

			if (sd_check_err_code(chip, SD_BUSY)
			    || sd_check_err_code(chip, SD_TO_ERR)) {
				k++;
				if (k < 20) {
					sd_clr_err_code(chip);
					goto RTY_MMC_RST;
				} else {
					TRACE_RET(chip, STATUS_FAIL);
				}
			} else {
				j++;
				if (j < 100) {
					sd_clr_err_code(chip);
					goto RTY_MMC_RST;
				} else {
					TRACE_RET(chip, STATUS_FAIL);
				}
			}
		}

		wait_timeout(20);
		i++;
	} while (!(rsp[1] & 0x80) && (i < 100)); /* Not complete power on */

	if (i == 100) {
		/* Time out */
		TRACE_RET(chip, STATUS_FAIL);
	}

	if ((rsp[1] & 0x60) == 0x40)
		SET_MMC_SECTOR_MODE(sd_card);
	else
		CLR_MMC_SECTOR_MODE(sd_card);

	/* CMD 2 */
	retval =
	    sd_send_cmd_get_rsp(chip, ALL_SEND_CID, 0, SD_RSP_TYPE_R2, NULL, 0);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	/* CMD 3 */
	sd_card->sd_addr = 0x00100000;
	retval =
	    sd_send_cmd_get_rsp(chip, SET_RELATIVE_ADDR, sd_card->sd_addr,
				SD_RSP_TYPE_R6, rsp, 5);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	/* Get CSD register for Calculating Timing,Capacity
	 * Check CSD to determaine as if this is the SD ROM card */
	retval = sd_check_csd(chip, 1);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);
	/* Get MMC Spec_Ver in the CSD register */
	spec_ver = (sd_card->raw_csd[0] & 0x3C) >> 2;

	/* Select MMC card */
	retval = sd_select_card(chip, 1);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	/* Set block length 512 bytes for all block commands */
	retval =
	    sd_send_cmd_get_rsp(chip, SET_BLOCKLEN, 0x200, SD_RSP_TYPE_R1, NULL,
				0);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	RTS51X_WRITE_REG(chip, SD_CFG1, SD_CLK_DIVIDE_MASK, SD_CLK_DIVIDE_0);

	if (chip->ic_version < 2)
		rts51x_write_register(chip, SD30_DRIVE_SEL, SD30_DRIVE_MASK,
				      0x02);
	rts51x_write_register(chip, CARD_DRIVE_SEL, SD20_DRIVE_MASK, DRIVE_8mA);

	chip->card_bus_width[chip->card2lun[SD_CARD]] = 1;
	if (spec_ver == 4) {
		/* MMC 4.x Cards */
		(void)mmc_switch_timing_bus(chip);
	}

	if (CHK_MMC_SECTOR_MODE(sd_card) && (sd_card->capacity == 0))
		TRACE_RET(chip, STATUS_FAIL);

	if (CHK_MMC_DDR52(sd_card) && change_to_ddr52) {
		/* Card is extracted while identifying */
		if (monitor_card_cd(chip, SD_CARD) == CD_NOT_EXIST)
			TRACE_RET(chip, STATUS_FAIL);

		retval = sd_set_init_para(chip);
		if (retval != STATUS_SUCCESS) {
			CLR_MMC_DDR52(sd_card);
			sd_init_power(chip);
			change_to_ddr52 = 0;
			goto MMC_DDR_FAIL;
		}

		retval = mmc_ddr_tuning(chip);
		if (retval != STATUS_SUCCESS) {
			CLR_MMC_DDR52(sd_card);
			sd_init_power(chip);
			change_to_ddr52 = 0;
			goto MMC_DDR_FAIL;
		}

		if (STATUS_SUCCESS ==
		    sd_wait_currentstate_dataready(chip, 0x08, 1, 20)) {
			cmd[0] = 0x40 | READ_SINGLE_BLOCK;
			cmd[1] = 0x00;
			cmd[2] = 0x00;
			cmd[3] = 0x00;
			cmd[4] = 0x00;
			if (CHK_MMC_8BIT(sd_card)) {
				retval =
				    sd_read_data(chip, SD_TM_NORMAL_READ, cmd,
						 5, 512, 1, SD_BUS_WIDTH_8,
						 NULL, 0, 600);
			} else if (CHK_MMC_4BIT(sd_card)) {
				retval =
				    sd_read_data(chip, SD_TM_NORMAL_READ, cmd,
						 5, 512, 1, SD_BUS_WIDTH_4,
						 NULL, 0, 600);
			} else {
				retval =
				    sd_read_data(chip, SD_TM_NORMAL_READ, cmd,
						 5, 512, 1, SD_BUS_WIDTH_1,
						 NULL, 0, 600);
			}

			if (retval != STATUS_SUCCESS) {
				CLR_MMC_DDR52(sd_card);
				change_to_ddr52 = 0;
				RTS51X_DEBUGP("read lba0 fail,"
							"goto SD20 mode\n");
				sd_init_power(chip);
				goto MMC_DDR_FAIL;
			}
		}
	}

	retval = rts51x_get_card_status(chip, &(chip->card_status));
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);
	if (chip->card_status & SD_WP)
		chip->card_wp |= SD_CARD;

	return STATUS_SUCCESS;
}

int reset_sd_card(struct rts51x_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;
	int i;

	memset(sd_card, 0, sizeof(struct sd_info));

	/* Init variables */
	sd_card->sd_type = 0;
	sd_card->seq_mode = 0;
	sd_card->sd_data_buf_ready = 0;
	sd_card->capacity = 0;
	sd_card->sd_switch_fail = 0;

	sd_clear_reset_fail(chip);
	enable_card_clock(chip, SD_CARD);

	sd_init_power(chip);

	chip->reset_need_retry = 0;
	for (i = 0; i < 3; i++) {
		if (!chip->option.reset_mmc_first) { /* reset sd first */
			retval = reset_sd(chip);
			if (retval != STATUS_SUCCESS) {
				/* Switch SD bus to 3V3 signal */
				RTS51X_WRITE_REG(chip, SD_PAD_CTL,
						 SD_IO_USING_1V8, 0);
				if (sd_check_reset_fail(chip, SD_RESET_FAIL))
					sd_clear_reset_fail(chip);
				else
					retval = reset_mmc(chip);
			}
		} else { /* reset MMC first */
			retval = reset_mmc(chip);
			if (retval != STATUS_SUCCESS) {
				if (sd_check_reset_fail(chip, MMC_RESET_FAIL)) {
					sd_clear_reset_fail(chip);
				} else {
					retval = reset_sd(chip);
					if (retval != STATUS_SUCCESS) {
						/* Switch SD bus to
						 * 3V3 signal */
						RTS51X_WRITE_REG(chip,
							SD_PAD_CTL,
							SD_IO_USING_1V8, 0);
					}
				}
			}
		}

		if ((retval == STATUS_SUCCESS) || (!chip->reset_need_retry)) {
			/* if reset success or don't need retry,then break */
			break;
		}
		if (monitor_card_cd(chip, SD_CARD) == CD_NOT_EXIST) {
			/* card is extracted */
			break;
		}
		RTS51X_DEBUGP("retry reset sd card,%d\n", i);
		chip->reset_need_retry = 0;
	}

	sd_clear_reset_fail(chip);
	chip->reset_need_retry = 0;

	if (retval == STATUS_SUCCESS) {
		rts51x_init_cmd(chip);
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CFG1, SD_CLK_DIVIDE_MASK,
			       SD_CLK_DIVIDE_0);
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_BYTE_CNT_L, 0xFF, 0);
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_BYTE_CNT_H, 0xFF, 2);
		retval = rts51x_send_cmd(chip, MODE_C, 100);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, retval);
	} else {
		chip->capacity[chip->card2lun[SD_CARD]] = sd_card->capacity = 0;
		if (chip->option.reset_or_rw_fail_set_pad_drive) {
			rts51x_write_register(chip, CARD_DRIVE_SEL,
					      SD20_DRIVE_MASK, DRIVE_8mA);
		}
		TRACE_RET(chip, STATUS_FAIL);
	}

	chip->capacity[chip->card2lun[SD_CARD]] = sd_card->capacity;

	if (chip->option.sd_send_status_en) {
		sd_card->sd_send_status_en = 1;
	} else {
		if (sd_card->capacity > 0x20000) { /* 64MB */
			sd_card->sd_send_status_en = 0;
		} else {
			sd_card->sd_send_status_en = 1;
		}
	}
	RTS51X_DEBUGP("sd_card->sd_send_status = %d\n",
		       sd_card->sd_send_status_en);

	retval = sd_set_init_para(chip);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	RTS51X_DEBUGP("sd_card->sd_type = 0x%x\n", sd_card->sd_type);

	return STATUS_SUCCESS;
}

#define WAIT_DATA_READY_RTY_CNT		255

static int wait_data_buf_ready(struct rts51x_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int i, retval;

	for (i = 0; i < WAIT_DATA_READY_RTY_CNT; i++) {
		if (monitor_card_cd(chip, SD_CARD) == CD_NOT_EXIST)
			TRACE_RET(chip, STATUS_FAIL);

		sd_card->sd_data_buf_ready = 0;

		retval = sd_send_cmd_get_rsp(chip, SEND_STATUS,
					     sd_card->sd_addr, SD_RSP_TYPE_R1,
					     NULL, 0);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, retval);

		if (sd_card->sd_data_buf_ready)
			return sd_send_cmd_get_rsp(chip, SEND_STATUS,
						   sd_card->sd_addr,
						   SD_RSP_TYPE_R1, NULL, 0);
	}

	sd_set_err_code(chip, SD_TO_ERR);

	TRACE_RET(chip, STATUS_FAIL);
}

void sd_stop_seq_mode(struct rts51x_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;

	if (sd_card->seq_mode) {
		retval = sd_switch_clock(chip);
		if (retval != STATUS_SUCCESS)
			return;

		retval = sd_send_cmd_get_rsp(chip, STOP_TRANSMISSION, 0,
					     SD_RSP_TYPE_R1b, NULL, 0);
		if (retval != STATUS_SUCCESS)
			sd_set_err_code(chip, SD_STS_ERR);
		sd_card->seq_mode = 0;

		rts51x_ep0_write_register(chip, MC_FIFO_CTL, FIFO_FLUSH,
					  FIFO_FLUSH);
	}
}

static inline int sd_auto_tune_clock(struct rts51x_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;

	if (chip->asic_code) {
		if (sd_card->sd_clock > 30)
			sd_card->sd_clock -= 20;
	} else {
		if (sd_card->sd_clock == CLK_100)
			sd_card->sd_clock = CLK_80;
		else if (sd_card->sd_clock == CLK_80)
			sd_card->sd_clock = CLK_60;
		else if (sd_card->sd_clock == CLK_60)
			sd_card->sd_clock = CLK_50;
	}

	retval = sd_switch_clock(chip);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	return STATUS_SUCCESS;
}

int sd_rw(struct scsi_cmnd *srb, struct rts51x_chip *chip, u32 start_sector,
	  u16 sector_cnt)
{
	struct sd_info *sd_card = &(chip->sd_card);
	u32 data_addr;
	int retval;
	u8 flag;
	unsigned int pipe;
	u8 stageflag;

	sd_card->counter = 0;

	if (!CHK_SD_HCXC(sd_card) && !CHK_MMC_SECTOR_MODE(sd_card))
		data_addr = start_sector << 9;
	else
		data_addr = start_sector;

	RTS51X_DEBUGP("sd_rw, data_addr = 0x%x\n", data_addr);

	sd_clr_err_code(chip);

	retval = sd_switch_clock(chip);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	if (sd_card->seq_mode && ((sd_card->pre_dir != srb->sc_data_direction)
				  ||
				  ((sd_card->pre_sec_addr +
				    sd_card->pre_sec_cnt) != start_sector))) {
		if ((sd_card->pre_dir == DMA_FROM_DEVICE)
		    && !CHK_SD30_SPEED(sd_card)
		    && !CHK_SD_HS(sd_card)
		    && !CHK_MMC_HS(sd_card)
		    && sd_card->sd_send_status_en) {
			sd_send_cmd_get_rsp(chip, SEND_STATUS,
					    sd_card->sd_addr, SD_RSP_TYPE_R1,
					    NULL, 0);
		}

		retval =
		    sd_send_cmd_get_rsp(chip, STOP_TRANSMISSION, 0,
					SD_RSP_TYPE_R1b, NULL, 0);
		if (retval != STATUS_SUCCESS) {
			sd_set_err_code(chip, SD_STS_ERR);
			TRACE_RET(chip, sd_parse_err_code(chip));
		}

		sd_card->seq_mode = 0;

		RTS51X_WRITE_REG(chip, MC_FIFO_CTL, FIFO_FLUSH, FIFO_FLUSH);

		if (!CHK_SD30_SPEED(sd_card)
		    && !CHK_SD_HS(sd_card)
		    && !CHK_MMC_HS(sd_card)
		    && sd_card->sd_send_status_en) {
			/* random rw, so pre_sec_cnt < 0x80 */
			sd_send_cmd_get_rsp(chip, SEND_STATUS,
					    sd_card->sd_addr, SD_RSP_TYPE_R1,
					    NULL, 0);
		}
	}

	rts51x_init_cmd(chip);

	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_BYTE_CNT_L, 0xFF, 0x00);
	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_BYTE_CNT_H, 0xFF, 0x02);
	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_BLOCK_CNT_L, 0xFF,
		       (u8) sector_cnt);
	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_BLOCK_CNT_H, 0xFF,
		       (u8) (sector_cnt >> 8));

	rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_DATA_SOURCE, 0x01,
		       RING_BUFFER);

	if (CHK_MMC_8BIT(sd_card))
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CFG1, 0x03,
			       SD_BUS_WIDTH_8);
	else if (CHK_MMC_4BIT(sd_card) || CHK_SD(sd_card))
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CFG1, 0x03,
			       SD_BUS_WIDTH_4);
	else
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CFG1, 0x03,
			       SD_BUS_WIDTH_1);

	if (sd_card->seq_mode) {
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CFG2, 0xFF,
			       SD_NO_CALCULATE_CRC7 | SD_CHECK_CRC16 |
			       SD_NO_WAIT_BUSY_END | SD_NO_CHECK_CRC7 |
			       SD_RSP_LEN_0);

		trans_dma_enable(srb->sc_data_direction, chip, sector_cnt * 512,
				 DMA_512);

		if (srb->sc_data_direction == DMA_FROM_DEVICE) {
			flag = MODE_CDIR;
			rts51x_add_cmd(chip, WRITE_REG_CMD, SD_TRANSFER, 0xFF,
				       SD_TM_AUTO_READ_3 | SD_TRANSFER_START);
		} else {
			flag = MODE_CDOR;
			rts51x_add_cmd(chip, WRITE_REG_CMD, SD_TRANSFER, 0xFF,
				       SD_TM_AUTO_WRITE_3 | SD_TRANSFER_START);
		}

		rts51x_add_cmd(chip, CHECK_REG_CMD, SD_TRANSFER,
			       SD_TRANSFER_END, SD_TRANSFER_END);

		retval = rts51x_send_cmd(chip, flag, 100);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, retval);
	} else {
		if (srb->sc_data_direction == DMA_FROM_DEVICE) {
			RTS51X_DEBUGP("SD/MMC CMD %d\n", READ_MULTIPLE_BLOCK);
			rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CMD0, 0xFF,
				       0x40 | READ_MULTIPLE_BLOCK);
			rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CMD1, 0xFF,
				       (u8) (data_addr >> 24));
			rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CMD2, 0xFF,
				       (u8) (data_addr >> 16));
			rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CMD3, 0xFF,
				       (u8) (data_addr >> 8));
			rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CMD4, 0xFF,
				       (u8) data_addr);

			rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CFG2, 0xFF,
				       SD_CALCULATE_CRC7 | SD_CHECK_CRC16 |
				       SD_NO_WAIT_BUSY_END | SD_CHECK_CRC7 |
				       SD_RSP_LEN_6);

			trans_dma_enable(srb->sc_data_direction, chip,
					 sector_cnt * 512, DMA_512);

			rts51x_add_cmd(chip, WRITE_REG_CMD, SD_TRANSFER, 0xFF,
				       SD_TM_AUTO_READ_2 | SD_TRANSFER_START);
			rts51x_add_cmd(chip, CHECK_REG_CMD, SD_TRANSFER,
				       SD_TRANSFER_END, SD_TRANSFER_END);

			retval = rts51x_send_cmd(chip, MODE_CDIR, 100);
			if (retval != STATUS_SUCCESS)
				TRACE_RET(chip, retval);
		} else {
			retval = rts51x_send_cmd(chip, MODE_C, 50);
			if (retval != STATUS_SUCCESS) {
				rts51x_clear_sd_error(chip);

				sd_set_err_code(chip, SD_TO_ERR);
				TRACE_RET(chip, sd_parse_err_code(chip));
			}

			retval = wait_data_buf_ready(chip);
			if (retval != STATUS_SUCCESS) {
				sd_set_err_code(chip, SD_TO_ERR);
				TRACE_RET(chip, sd_parse_err_code(chip));
			}

			retval = sd_send_cmd_get_rsp(chip, WRITE_MULTIPLE_BLOCK,
						     data_addr, SD_RSP_TYPE_R1,
						     NULL, 0);
			if (retval != STATUS_SUCCESS) {
				sd_set_err_code(chip, SD_CRC_ERR);
				TRACE_RET(chip, sd_parse_err_code(chip));
			}

			rts51x_init_cmd(chip);

			rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CFG2, 0xFF,
				       SD_NO_CALCULATE_CRC7 | SD_CHECK_CRC16 |
				       SD_NO_WAIT_BUSY_END | SD_NO_CHECK_CRC7 |
				       SD_RSP_LEN_0);

			trans_dma_enable(srb->sc_data_direction, chip,
					 sector_cnt * 512, DMA_512);

			rts51x_add_cmd(chip, WRITE_REG_CMD, SD_TRANSFER, 0xFF,
				       SD_TM_AUTO_WRITE_3 | SD_TRANSFER_START);
			rts51x_add_cmd(chip, CHECK_REG_CMD, SD_TRANSFER,
				       SD_TRANSFER_END, SD_TRANSFER_END);

			retval = rts51x_send_cmd(chip, MODE_CDOR, 100);
			if (retval != STATUS_SUCCESS)
				TRACE_RET(chip, retval);
		}

		sd_card->seq_mode = 1;
	}

	if (srb->sc_data_direction == DMA_FROM_DEVICE) {
		pipe = RCV_BULK_PIPE(chip);
		stageflag = STAGE_DI;
	} else {
		pipe = SND_BULK_PIPE(chip);
		stageflag = STAGE_DO;
	}

	retval =
	    rts51x_transfer_data_rcc(chip, pipe, scsi_sglist(srb),
				     scsi_bufflen(srb), scsi_sg_count(srb),
				     NULL, 10000, stageflag);
	if (retval != STATUS_SUCCESS) {
		u8 stat = 0;
		int err = retval;

		sd_print_debug_reg(chip);

		rts51x_ep0_read_register(chip, SD_STAT1, &stat);
		RTS51X_DEBUGP("SD_STAT1: 0x%x\n", stat);

		rts51x_clear_sd_error(chip);

		retval =
		    sd_send_cmd_get_rsp(chip, STOP_TRANSMISSION, 0,
					SD_RSP_TYPE_R1b, NULL, 0);
		if (retval != STATUS_SUCCESS) {
			sd_set_err_code(chip, SD_STS_ERR);
			TRACE_RET(chip, retval);
		}

		if (stat & (SD_CRC7_ERR | SD_CRC16_ERR | SD_CRC_WRITE_ERR)) {
			RTS51X_DEBUGP("SD CRC error, tune clock!\n");
			sd_auto_tune_clock(chip);
		}

		sd_card->seq_mode = 0;

		TRACE_RET(chip, err);
	}
	retval = rts51x_get_rsp(chip, 1, 2000);
	if (CHECK_SD_TRANS_FAIL(chip, retval)) {
		rts51x_clear_sd_error(chip);
		TRACE_RET(chip, STATUS_FAIL);
	}

	sd_card->pre_sec_addr = start_sector;
	sd_card->pre_sec_cnt = sector_cnt;
	sd_card->pre_dir = srb->sc_data_direction;

	return STATUS_SUCCESS;
}

void sd_cleanup_work(struct rts51x_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);

	if (sd_card->seq_mode) {
		RTS51X_DEBUGP("SD: stop transmission\n");
		sd_stop_seq_mode(chip);
		sd_card->counter = 0;
	}
}

inline void sd_fill_power_off_card3v3(struct rts51x_chip *chip)
{
	rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_CLK_EN, SD_CLK_EN, 0);

	rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_OE, SD_OUTPUT_EN, 0);
	if (!chip->option.FT2_fast_mode) {
#ifdef SD_XD_IO_FOLLOW_PWR
		if (CHECK_PKG(chip, LQFP48)
		    || chip->option.rts5129_D3318_off_enable)
			rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_PWR_CTL,
				       POWER_MASK | LDO_OFF,
				       POWER_OFF | LDO_OFF);
		else
			rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_PWR_CTL,
				       POWER_MASK, POWER_OFF);
#else
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_PWR_CTL, POWER_MASK,
			       POWER_OFF);
#endif
	}
}

int sd_power_off_card3v3(struct rts51x_chip *chip)
{
	int retval;

	rts51x_init_cmd(chip);

	sd_fill_power_off_card3v3(chip);

	retval = rts51x_send_cmd(chip, MODE_C, 100);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);
#ifdef SD_XD_IO_FOLLOW_PWR
	if (!chip->option.FT2_fast_mode)
		wait_timeout(chip->option.D3318_off_delay);
#endif

	return STATUS_SUCCESS;
}

int release_sd_card(struct rts51x_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;

	RTS51X_DEBUGP("elease_sd_card\n");

	chip->card_ready &= ~SD_CARD;
	chip->card_fail &= ~SD_CARD;
	chip->card_wp &= ~SD_CARD;

	memset(sd_card->raw_csd, 0, 16);
	memset(sd_card->raw_scr, 0, 8);

	rts51x_write_register(chip, SFSM_ED, HW_CMD_STOP, HW_CMD_STOP);
	rts51x_write_register(chip, SD_PAD_CTL, SD_IO_USING_1V8, 0);
	if (CHECK_PKG(chip, LQFP48) || chip->option.rts5129_D3318_off_enable)
		sd_power_off_card3v3(chip);

	rts51x_init_cmd(chip);
	if (!(CHECK_PKG(chip, LQFP48) || chip->option.rts5129_D3318_off_enable))
		sd_fill_power_off_card3v3(chip);

	if (chip->asic_code)
		sd_pull_ctl_disable(chip);
	else
		rts51x_add_cmd(chip, WRITE_REG_CMD, FPGA_PULL_CTL,
			       FPGA_SD_PULL_CTL_BIT | 0x20,
			       FPGA_SD_PULL_CTL_BIT);

	/* Switch LDO3318 to 3.3V */
	rts51x_add_cmd(chip, WRITE_REG_CMD, LDO_POWER_CFG, TUNE_SD18_MASK,
		       TUNE_SD18_3V3);

	if (CHK_MMC_DDR52(sd_card) && CHK_MMC_8BIT(sd_card))
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_DMA1_CTL,
			       EXTEND_DMA1_ASYNC_SIGNAL,
			       EXTEND_DMA1_ASYNC_SIGNAL);
	if (CHK_SD30_SPEED(sd_card) || CHK_MMC(sd_card))
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD30_DRIVE_SEL,
			       SD30_DRIVE_MASK, chip->option.sd30_pad_drive);
	/* Suspend LDO3318 */
	rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_PWR_CTL, LDO3318_PWR_MASK,
		       LDO_SUSPEND);

	retval = rts51x_send_cmd(chip, MODE_C, 100);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);
	wait_timeout(20);

	return STATUS_SUCCESS;
}
