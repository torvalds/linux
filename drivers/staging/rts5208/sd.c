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

#include "rtsx.h"
#include "sd.h"

#define SD_MAX_RETRY_COUNT	3

static u16 REG_SD_CFG1;
static u16 REG_SD_CFG2;
static u16 REG_SD_CFG3;
static u16 REG_SD_STAT1;
static u16 REG_SD_STAT2;
static u16 REG_SD_BUS_STAT;
static u16 REG_SD_PAD_CTL;
static u16 REG_SD_SAMPLE_POINT_CTL;
static u16 REG_SD_PUSH_POINT_CTL;
static u16 REG_SD_CMD0;
static u16 REG_SD_CMD1;
static u16 REG_SD_CMD2;
static u16 REG_SD_CMD3;
static u16 REG_SD_CMD4;
static u16 REG_SD_CMD5;
static u16 REG_SD_BYTE_CNT_L;
static u16 REG_SD_BYTE_CNT_H;
static u16 REG_SD_BLOCK_CNT_L;
static u16 REG_SD_BLOCK_CNT_H;
static u16 REG_SD_TRANSFER;
static u16 REG_SD_VPCLK0_CTL;
static u16 REG_SD_VPCLK1_CTL;
static u16 REG_SD_DCMPS0_CTL;
static u16 REG_SD_DCMPS1_CTL;

static inline void sd_set_err_code(struct rtsx_chip *chip, u8 err_code)
{
	struct sd_info *sd_card = &(chip->sd_card);

	sd_card->err_code |= err_code;
}

static inline void sd_clr_err_code(struct rtsx_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);

	sd_card->err_code = 0;
}

static inline int sd_check_err_code(struct rtsx_chip *chip, u8 err_code)
{
	struct sd_info *sd_card = &(chip->sd_card);

	return sd_card->err_code & err_code;
}

static void sd_init_reg_addr(struct rtsx_chip *chip)
{
	REG_SD_CFG1 = 0xFD31;
	REG_SD_CFG2 = 0xFD33;
	REG_SD_CFG3 = 0xFD3E;
	REG_SD_STAT1 = 0xFD30;
	REG_SD_STAT2 = 0;
	REG_SD_BUS_STAT = 0;
	REG_SD_PAD_CTL = 0;
	REG_SD_SAMPLE_POINT_CTL = 0;
	REG_SD_PUSH_POINT_CTL = 0;
	REG_SD_CMD0 = 0xFD34;
	REG_SD_CMD1 = 0xFD35;
	REG_SD_CMD2 = 0xFD36;
	REG_SD_CMD3 = 0xFD37;
	REG_SD_CMD4 = 0xFD38;
	REG_SD_CMD5 = 0xFD5A;
	REG_SD_BYTE_CNT_L = 0xFD39;
	REG_SD_BYTE_CNT_H = 0xFD3A;
	REG_SD_BLOCK_CNT_L = 0xFD3B;
	REG_SD_BLOCK_CNT_H = 0xFD3C;
	REG_SD_TRANSFER = 0xFD32;
	REG_SD_VPCLK0_CTL = 0;
	REG_SD_VPCLK1_CTL = 0;
	REG_SD_DCMPS0_CTL = 0;
	REG_SD_DCMPS1_CTL = 0;
}

static int sd_check_data0_status(struct rtsx_chip *chip)
{
	int retval;
	u8 stat;

	retval = rtsx_read_register(chip, REG_SD_STAT1, &stat);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}

	if (!(stat & SD_DAT0_STATUS)) {
		sd_set_err_code(chip, SD_BUSY);
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}

static int sd_send_cmd_get_rsp(struct rtsx_chip *chip, u8 cmd_idx,
		u32 arg, u8 rsp_type, u8 *rsp, int rsp_len)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;
	int timeout = 100;
	u16 reg_addr;
	u8 *ptr;
	int stat_idx = 0;
	int rty_cnt = 0;

	sd_clr_err_code(chip);

	dev_dbg(rtsx_dev(chip), "SD/MMC CMD %d, arg = 0x%08x\n", cmd_idx, arg);

	if (rsp_type == SD_RSP_TYPE_R1b)
		timeout = 3000;

RTY_SEND_CMD:

	rtsx_init_cmd(chip);

	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CMD0, 0xFF, 0x40 | cmd_idx);
	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CMD1, 0xFF, (u8)(arg >> 24));
	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CMD2, 0xFF, (u8)(arg >> 16));
	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CMD3, 0xFF, (u8)(arg >> 8));
	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CMD4, 0xFF, (u8)arg);

	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CFG2, 0xFF, rsp_type);
	rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_DATA_SOURCE,
			0x01, PINGPONG_BUFFER);
	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_TRANSFER,
			0xFF, SD_TM_CMD_RSP | SD_TRANSFER_START);
	rtsx_add_cmd(chip, CHECK_REG_CMD, REG_SD_TRANSFER,
		SD_TRANSFER_END | SD_STAT_IDLE, SD_TRANSFER_END | SD_STAT_IDLE);

	if (rsp_type == SD_RSP_TYPE_R2) {
		for (reg_addr = PPBUF_BASE2; reg_addr < PPBUF_BASE2 + 16;
		     reg_addr++)
			rtsx_add_cmd(chip, READ_REG_CMD, reg_addr, 0, 0);

		stat_idx = 16;
	} else if (rsp_type != SD_RSP_TYPE_R0) {
		for (reg_addr = REG_SD_CMD0; reg_addr <= REG_SD_CMD4;
		     reg_addr++)
			rtsx_add_cmd(chip, READ_REG_CMD, reg_addr, 0, 0);

		stat_idx = 5;
	}

	rtsx_add_cmd(chip, READ_REG_CMD, REG_SD_STAT1, 0, 0);

	retval = rtsx_send_cmd(chip, SD_CARD, timeout);
	if (retval < 0) {
		u8 val;

		rtsx_read_register(chip, REG_SD_STAT1, &val);
		dev_dbg(rtsx_dev(chip), "SD_STAT1: 0x%x\n", val);

		rtsx_read_register(chip, REG_SD_CFG3, &val);
		dev_dbg(rtsx_dev(chip), "SD_CFG3: 0x%x\n", val);

		if (retval == -ETIMEDOUT) {
			if (rsp_type & SD_WAIT_BUSY_END) {
				retval = sd_check_data0_status(chip);
				if (retval != STATUS_SUCCESS) {
					rtsx_clear_sd_error(chip);
					rtsx_trace(chip);
					return retval;
				}
			} else {
				sd_set_err_code(chip, SD_TO_ERR);
			}
			retval = STATUS_TIMEDOUT;
		} else {
			retval = STATUS_FAIL;
		}
		rtsx_clear_sd_error(chip);

		rtsx_trace(chip);
		return retval;
	}

	if (rsp_type == SD_RSP_TYPE_R0)
		return STATUS_SUCCESS;

	ptr = rtsx_get_cmd_data(chip) + 1;

	if ((ptr[0] & 0xC0) != 0) {
		sd_set_err_code(chip, SD_STS_ERR);
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	if (!(rsp_type & SD_NO_CHECK_CRC7)) {
		if (ptr[stat_idx] & SD_CRC7_ERR) {
			if (cmd_idx == WRITE_MULTIPLE_BLOCK) {
				sd_set_err_code(chip, SD_CRC_ERR);
				rtsx_trace(chip);
				return STATUS_FAIL;
			}
			if (rty_cnt < SD_MAX_RETRY_COUNT) {
				wait_timeout(20);
				rty_cnt++;
				goto RTY_SEND_CMD;
			} else {
				sd_set_err_code(chip, SD_CRC_ERR);
				rtsx_trace(chip);
				return STATUS_FAIL;
			}
		}
	}

	if ((rsp_type == SD_RSP_TYPE_R1) || (rsp_type == SD_RSP_TYPE_R1b)) {
		if ((cmd_idx != SEND_RELATIVE_ADDR) &&
			(cmd_idx != SEND_IF_COND)) {
			if (cmd_idx != STOP_TRANSMISSION) {
				if (ptr[1] & 0x80) {
					rtsx_trace(chip);
					return STATUS_FAIL;
				}
			}
#ifdef SUPPORT_SD_LOCK
			if (ptr[1] & 0x7D) {
#else
			if (ptr[1] & 0x7F) {
#endif
				dev_dbg(rtsx_dev(chip), "ptr[1]: 0x%02x\n",
					ptr[1]);
				rtsx_trace(chip);
				return STATUS_FAIL;
			}
			if (ptr[2] & 0xFF) {
				dev_dbg(rtsx_dev(chip), "ptr[2]: 0x%02x\n",
					ptr[2]);
				rtsx_trace(chip);
				return STATUS_FAIL;
			}
			if (ptr[3] & 0x80) {
				dev_dbg(rtsx_dev(chip), "ptr[3]: 0x%02x\n",
					ptr[3]);
				rtsx_trace(chip);
				return STATUS_FAIL;
			}
			if (ptr[3] & 0x01)
				sd_card->sd_data_buf_ready = 1;
			else
				sd_card->sd_data_buf_ready = 0;
		}
	}

	if (rsp && rsp_len)
		memcpy(rsp, ptr, rsp_len);

	return STATUS_SUCCESS;
}

static int sd_read_data(struct rtsx_chip *chip,
			u8 trans_mode, u8 *cmd, int cmd_len, u16 byte_cnt,
			u16 blk_cnt, u8 bus_width, u8 *buf, int buf_len,
			int timeout)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;
	int i;

	sd_clr_err_code(chip);

	if (!buf)
		buf_len = 0;

	if (buf_len > 512) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	rtsx_init_cmd(chip);

	if (cmd_len) {
		dev_dbg(rtsx_dev(chip), "SD/MMC CMD %d\n", cmd[0] - 0x40);
		for (i = 0; i < (min(cmd_len, 6)); i++)
			rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CMD0 + i,
				     0xFF, cmd[i]);
	}
	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_BYTE_CNT_L, 0xFF,
		(u8)byte_cnt);
	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_BYTE_CNT_H, 0xFF,
		(u8)(byte_cnt >> 8));
	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_BLOCK_CNT_L, 0xFF,
		(u8)blk_cnt);
	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_BLOCK_CNT_H, 0xFF,
		(u8)(blk_cnt >> 8));

	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CFG1, 0x03, bus_width);

	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CFG2, 0xFF,
		SD_CALCULATE_CRC7 | SD_CHECK_CRC16 | SD_NO_WAIT_BUSY_END|
		SD_CHECK_CRC7 | SD_RSP_LEN_6);
	if (trans_mode != SD_TM_AUTO_TUNING)
		rtsx_add_cmd(chip, WRITE_REG_CMD,
			CARD_DATA_SOURCE, 0x01, PINGPONG_BUFFER);

	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_TRANSFER, 0xFF,
		trans_mode | SD_TRANSFER_START);
	rtsx_add_cmd(chip, CHECK_REG_CMD, REG_SD_TRANSFER, SD_TRANSFER_END,
		SD_TRANSFER_END);

	retval = rtsx_send_cmd(chip, SD_CARD, timeout);
	if (retval < 0) {
		if (retval == -ETIMEDOUT) {
			sd_send_cmd_get_rsp(chip, SEND_STATUS, sd_card->sd_addr,
					    SD_RSP_TYPE_R1, NULL, 0);
		}

		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	if (buf && buf_len) {
		retval = rtsx_read_ppbuf(chip, buf, buf_len);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}
	}

	return STATUS_SUCCESS;
}

static int sd_write_data(struct rtsx_chip *chip, u8 trans_mode,
		u8 *cmd, int cmd_len, u16 byte_cnt, u16 blk_cnt, u8 bus_width,
		u8 *buf, int buf_len, int timeout)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;
	int i;

	sd_clr_err_code(chip);

	if (!buf)
		buf_len = 0;

	if (buf_len > 512) {
		/* This function can't write data more than one page */
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	if (buf && buf_len) {
		retval = rtsx_write_ppbuf(chip, buf, buf_len);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}
	}

	rtsx_init_cmd(chip);

	if (cmd_len) {
		dev_dbg(rtsx_dev(chip), "SD/MMC CMD %d\n", cmd[0] - 0x40);
		for (i = 0; i < (min(cmd_len, 6)); i++) {
			rtsx_add_cmd(chip, WRITE_REG_CMD,
				     REG_SD_CMD0 + i, 0xFF, cmd[i]);
		}
	}
	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_BYTE_CNT_L, 0xFF,
		(u8)byte_cnt);
	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_BYTE_CNT_H, 0xFF,
		(u8)(byte_cnt >> 8));
	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_BLOCK_CNT_L, 0xFF,
		(u8)blk_cnt);
	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_BLOCK_CNT_H, 0xFF,
		(u8)(blk_cnt >> 8));

	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CFG1, 0x03, bus_width);

	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CFG2, 0xFF,
		SD_CALCULATE_CRC7 | SD_CHECK_CRC16 | SD_NO_WAIT_BUSY_END |
		SD_CHECK_CRC7 | SD_RSP_LEN_6);

	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_TRANSFER, 0xFF,
		trans_mode | SD_TRANSFER_START);
	rtsx_add_cmd(chip, CHECK_REG_CMD, REG_SD_TRANSFER, SD_TRANSFER_END,
		SD_TRANSFER_END);

	retval = rtsx_send_cmd(chip, SD_CARD, timeout);
	if (retval < 0) {
		if (retval == -ETIMEDOUT) {
			sd_send_cmd_get_rsp(chip, SEND_STATUS,
				sd_card->sd_addr, SD_RSP_TYPE_R1, NULL, 0);
		}

		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}

static int sd_check_csd(struct rtsx_chip *chip, char check_wp)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;
	int i;
	u8 csd_ver, trans_speed;
	u8 rsp[16];

	for (i = 0; i < 6; i++) {
		if (detect_card_cd(chip, SD_CARD) != STATUS_SUCCESS) {
			sd_set_err_code(chip, SD_NO_CARD);
			rtsx_trace(chip);
			return STATUS_FAIL;
		}

		retval = sd_send_cmd_get_rsp(chip, SEND_CSD, sd_card->sd_addr,
					SD_RSP_TYPE_R2, rsp, 16);
		if (retval == STATUS_SUCCESS)
			break;
	}

	if (i == 6) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	memcpy(sd_card->raw_csd, rsp + 1, 15);

	dev_dbg(rtsx_dev(chip), "CSD Response:\n");
	dev_dbg(rtsx_dev(chip), "%*ph\n", 16, sd_card->raw_csd);

	csd_ver = (rsp[1] & 0xc0) >> 6;
	dev_dbg(rtsx_dev(chip), "csd_ver = %d\n", csd_ver);

	trans_speed = rsp[4];
	if ((trans_speed & 0x07) == 0x02) {
		if ((trans_speed & 0xf8) >= 0x30) {
			if (chip->asic_code)
				sd_card->sd_clock = 47;
			else
				sd_card->sd_clock = CLK_50;

		} else if ((trans_speed & 0xf8) == 0x28) {
			if (chip->asic_code)
				sd_card->sd_clock = 39;
			else
				sd_card->sd_clock = CLK_40;

		} else if ((trans_speed & 0xf8) == 0x20) {
			if (chip->asic_code)
				sd_card->sd_clock = 29;
			else
				sd_card->sd_clock = CLK_30;

		} else if ((trans_speed & 0xf8) >= 0x10) {
			if (chip->asic_code)
				sd_card->sd_clock = 23;
			else
				sd_card->sd_clock = CLK_20;

		} else if ((trans_speed & 0x08) >= 0x08) {
			if (chip->asic_code)
				sd_card->sd_clock = 19;
			else
				sd_card->sd_clock = CLK_20;
		} else {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}
	} else {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	if (CHK_MMC_SECTOR_MODE(sd_card)) {
		sd_card->capacity = 0;
	} else {
		if ((!CHK_SD_HCXC(sd_card)) || (csd_ver == 0)) {
			u8 blk_size, c_size_mult;
			u16 c_size;

			blk_size = rsp[6] & 0x0F;
			c_size =  ((u16)(rsp[7] & 0x03) << 10)
					+ ((u16)rsp[8] << 2)
					+ ((u16)(rsp[9] & 0xC0) >> 6);
			c_size_mult = (u8)((rsp[10] & 0x03) << 1);
			c_size_mult += (rsp[11] & 0x80) >> 7;
			sd_card->capacity = (((u32)(c_size + 1)) *
					(1 << (c_size_mult + 2)))
				<< (blk_size - 9);
		} else {
			u32 total_sector = 0;

			total_sector = (((u32)rsp[8] & 0x3f) << 16) |
				((u32)rsp[9] << 8) | (u32)rsp[10];
			sd_card->capacity = (total_sector + 1) << 10;
		}
	}

	if (check_wp) {
		if (rsp[15] & 0x30)
			chip->card_wp |= SD_CARD;

		dev_dbg(rtsx_dev(chip), "CSD WP Status: 0x%x\n", rsp[15]);
	}

	return STATUS_SUCCESS;
}

static int sd_set_sample_push_timing(struct rtsx_chip *chip)
{
	int retval;
	struct sd_info *sd_card = &(chip->sd_card);
	u8 val = 0;

	if ((chip->sd_ctl & SD_PUSH_POINT_CTL_MASK) == SD_PUSH_POINT_DELAY)
		val |= 0x10;

	if ((chip->sd_ctl & SD_SAMPLE_POINT_CTL_MASK) == SD_SAMPLE_POINT_AUTO) {
		if (chip->asic_code) {
			if (CHK_SD_HS(sd_card) || CHK_MMC_52M(sd_card)) {
				if (val & 0x10)
					val |= 0x04;
				else
					val |= 0x08;
			}
		} else {
			if (val & 0x10)
				val |= 0x04;
			else
				val |= 0x08;
		}
	} else if ((chip->sd_ctl & SD_SAMPLE_POINT_CTL_MASK) ==
		SD_SAMPLE_POINT_DELAY) {
		if (val & 0x10)
			val |= 0x04;
		else
			val |= 0x08;
	}

	retval = rtsx_write_register(chip, REG_SD_CFG1, 0x1C, val);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}

	return STATUS_SUCCESS;
}

static void sd_choose_proper_clock(struct rtsx_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);

	if (CHK_SD_SDR104(sd_card)) {
		if (chip->asic_code)
			sd_card->sd_clock = chip->asic_sd_sdr104_clk;
		else
			sd_card->sd_clock = chip->fpga_sd_sdr104_clk;

	} else if (CHK_SD_DDR50(sd_card)) {
		if (chip->asic_code)
			sd_card->sd_clock = chip->asic_sd_ddr50_clk;
		else
			sd_card->sd_clock = chip->fpga_sd_ddr50_clk;

	} else if (CHK_SD_SDR50(sd_card)) {
		if (chip->asic_code)
			sd_card->sd_clock = chip->asic_sd_sdr50_clk;
		else
			sd_card->sd_clock = chip->fpga_sd_sdr50_clk;

	} else if (CHK_SD_HS(sd_card)) {
		if (chip->asic_code)
			sd_card->sd_clock = chip->asic_sd_hs_clk;
		else
			sd_card->sd_clock = chip->fpga_sd_hs_clk;

	} else if (CHK_MMC_52M(sd_card) || CHK_MMC_DDR52(sd_card)) {
		if (chip->asic_code)
			sd_card->sd_clock = chip->asic_mmc_52m_clk;
		else
			sd_card->sd_clock = chip->fpga_mmc_52m_clk;

	} else if (CHK_MMC_26M(sd_card)) {
		if (chip->asic_code)
			sd_card->sd_clock = 48;
		else
			sd_card->sd_clock = CLK_50;
	}
}

static int sd_set_clock_divider(struct rtsx_chip *chip, u8 clk_div)
{
	int retval;
	u8 mask = 0, val = 0;

	mask = 0x60;
	if (clk_div == SD_CLK_DIVIDE_0)
		val = 0x00;
	else if (clk_div == SD_CLK_DIVIDE_128)
		val = 0x40;
	else if (clk_div == SD_CLK_DIVIDE_256)
		val = 0x20;

	retval = rtsx_write_register(chip, REG_SD_CFG1, mask, val);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}

	return STATUS_SUCCESS;
}

static int sd_set_init_para(struct rtsx_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;

	retval = sd_set_sample_push_timing(chip);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	sd_choose_proper_clock(chip);

	retval = switch_clock(chip, sd_card->sd_clock);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}

int sd_select_card(struct rtsx_chip *chip, int select)
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
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}

#ifdef SUPPORT_SD_LOCK
static int sd_update_lock_status(struct rtsx_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;
	u8 rsp[5];

	retval = sd_send_cmd_get_rsp(chip, SEND_STATUS, sd_card->sd_addr,
				SD_RSP_TYPE_R1, rsp, 5);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	if (rsp[1] & 0x02)
		sd_card->sd_lock_status |= SD_LOCKED;
	else
		sd_card->sd_lock_status &= ~SD_LOCKED;

	dev_dbg(rtsx_dev(chip), "sd_card->sd_lock_status = 0x%x\n",
		sd_card->sd_lock_status);

	if (rsp[1] & 0x01) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}
#endif

static int sd_wait_state_data_ready(struct rtsx_chip *chip, u8 state,
				u8 data_ready, int polling_cnt)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval, i;
	u8 rsp[5];

	for (i = 0; i < polling_cnt; i++) {
		retval = sd_send_cmd_get_rsp(chip, SEND_STATUS,
					sd_card->sd_addr, SD_RSP_TYPE_R1, rsp,
					5);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}

		if (((rsp[3] & 0x1E) == state) &&
			((rsp[3] & 0x01) == data_ready))
			return STATUS_SUCCESS;
	}

	rtsx_trace(chip);
	return STATUS_FAIL;
}

static int sd_change_bank_voltage(struct rtsx_chip *chip, u8 voltage)
{
	int retval;

	if (voltage == SD_IO_3V3) {
		if (chip->asic_code) {
			retval = rtsx_write_phy_register(chip, 0x08,
							0x4FC0 |
							chip->phy_voltage);
			if (retval != STATUS_SUCCESS) {
				rtsx_trace(chip);
				return STATUS_FAIL;
			}
		} else {
			retval = rtsx_write_register(chip, SD_PAD_CTL,
						     SD_IO_USING_1V8, 0);
			if (retval) {
				rtsx_trace(chip);
				return retval;
			}
		}
	} else if (voltage == SD_IO_1V8) {
		if (chip->asic_code) {
			retval = rtsx_write_phy_register(chip, 0x08,
							0x4C40 |
							chip->phy_voltage);
			if (retval != STATUS_SUCCESS) {
				rtsx_trace(chip);
				return STATUS_FAIL;
			}
		} else {
			retval = rtsx_write_register(chip, SD_PAD_CTL,
						     SD_IO_USING_1V8,
						     SD_IO_USING_1V8);
			if (retval) {
				rtsx_trace(chip);
				return retval;
			}
		}
	} else {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}

static int sd_voltage_switch(struct rtsx_chip *chip)
{
	int retval;
	u8 stat;

	retval = rtsx_write_register(chip, SD_BUS_STAT,
				     SD_CLK_TOGGLE_EN | SD_CLK_FORCE_STOP,
				     SD_CLK_TOGGLE_EN);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}

	retval = sd_send_cmd_get_rsp(chip, VOLTAGE_SWITCH, 0, SD_RSP_TYPE_R1,
				NULL, 0);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	udelay(chip->sd_voltage_switch_delay);

	retval = rtsx_read_register(chip, SD_BUS_STAT, &stat);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}
	if (stat & (SD_CMD_STATUS | SD_DAT3_STATUS | SD_DAT2_STATUS |
				SD_DAT1_STATUS | SD_DAT0_STATUS)) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	retval = rtsx_write_register(chip, SD_BUS_STAT, 0xFF,
				     SD_CLK_FORCE_STOP);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}
	retval = sd_change_bank_voltage(chip, SD_IO_1V8);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	wait_timeout(50);

	retval = rtsx_write_register(chip, SD_BUS_STAT, 0xFF,
				     SD_CLK_TOGGLE_EN);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}
	wait_timeout(10);

	retval = rtsx_read_register(chip, SD_BUS_STAT, &stat);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}
	if ((stat & (SD_CMD_STATUS | SD_DAT3_STATUS | SD_DAT2_STATUS |
				SD_DAT1_STATUS | SD_DAT0_STATUS)) !=
			(SD_CMD_STATUS | SD_DAT3_STATUS | SD_DAT2_STATUS |
				SD_DAT1_STATUS | SD_DAT0_STATUS)) {
		dev_dbg(rtsx_dev(chip), "SD_BUS_STAT: 0x%x\n", stat);
		rtsx_write_register(chip, SD_BUS_STAT,
				SD_CLK_TOGGLE_EN | SD_CLK_FORCE_STOP, 0);
		rtsx_write_register(chip, CARD_CLK_EN, 0xFF, 0);
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	retval = rtsx_write_register(chip, SD_BUS_STAT,
				     SD_CLK_TOGGLE_EN | SD_CLK_FORCE_STOP, 0);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}

	return STATUS_SUCCESS;
}

static int sd_reset_dcm(struct rtsx_chip *chip, u8 tune_dir)
{
	int retval;

	if (tune_dir == TUNE_RX) {
		retval = rtsx_write_register(chip, DCM_DRP_CTL, 0xFF,
					     DCM_RESET | DCM_RX);
		if (retval) {
			rtsx_trace(chip);
			return retval;
		}
		retval = rtsx_write_register(chip, DCM_DRP_CTL, 0xFF, DCM_RX);
		if (retval) {
			rtsx_trace(chip);
			return retval;
		}
	} else {
		retval = rtsx_write_register(chip, DCM_DRP_CTL, 0xFF,
					     DCM_RESET | DCM_TX);
		if (retval) {
			rtsx_trace(chip);
			return retval;
		}
		retval = rtsx_write_register(chip, DCM_DRP_CTL, 0xFF, DCM_TX);
		if (retval) {
			rtsx_trace(chip);
			return retval;
		}
	}

	return STATUS_SUCCESS;
}

static int sd_change_phase(struct rtsx_chip *chip, u8 sample_point, u8 tune_dir)
{
	struct sd_info *sd_card = &(chip->sd_card);
	u16 SD_VP_CTL, SD_DCMPS_CTL;
	u8 val;
	int retval;
	bool ddr_rx = false;

	dev_dbg(rtsx_dev(chip), "sd_change_phase (sample_point = %d, tune_dir = %d)\n",
		sample_point, tune_dir);

	if (tune_dir == TUNE_RX) {
		SD_VP_CTL = SD_VPRX_CTL;
		SD_DCMPS_CTL = SD_DCMPS_RX_CTL;
		if (CHK_SD_DDR50(sd_card))
			ddr_rx = true;
	} else {
		SD_VP_CTL = SD_VPTX_CTL;
		SD_DCMPS_CTL = SD_DCMPS_TX_CTL;
	}

	if (chip->asic_code) {
		retval = rtsx_write_register(chip, CLK_CTL, CHANGE_CLK,
					     CHANGE_CLK);
		if (retval) {
			rtsx_trace(chip);
			return retval;
		}
		retval = rtsx_write_register(chip, SD_VP_CTL, 0x1F,
					     sample_point);
		if (retval) {
			rtsx_trace(chip);
			return retval;
		}
		retval = rtsx_write_register(chip, SD_VPCLK0_CTL,
					     PHASE_NOT_RESET, 0);
		if (retval) {
			rtsx_trace(chip);
			return retval;
		}
		retval = rtsx_write_register(chip, SD_VPCLK0_CTL,
					     PHASE_NOT_RESET, PHASE_NOT_RESET);
		if (retval) {
			rtsx_trace(chip);
			return retval;
		}
		retval = rtsx_write_register(chip, CLK_CTL, CHANGE_CLK, 0);
		if (retval) {
			rtsx_trace(chip);
			return retval;
		}
	} else {
		rtsx_read_register(chip, SD_VP_CTL, &val);
		dev_dbg(rtsx_dev(chip), "SD_VP_CTL: 0x%x\n", val);
		rtsx_read_register(chip, SD_DCMPS_CTL, &val);
		dev_dbg(rtsx_dev(chip), "SD_DCMPS_CTL: 0x%x\n", val);

		if (ddr_rx) {
			retval = rtsx_write_register(chip, SD_VP_CTL,
						     PHASE_CHANGE,
						     PHASE_CHANGE);
			if (retval) {
				rtsx_trace(chip);
				return retval;
			}
			udelay(50);
			retval = rtsx_write_register(chip, SD_VP_CTL, 0xFF,
						     PHASE_CHANGE | PHASE_NOT_RESET | sample_point);
			if (retval) {
				rtsx_trace(chip);
				return retval;
			}
		} else {
			retval = rtsx_write_register(chip, CLK_CTL,
						     CHANGE_CLK, CHANGE_CLK);
			if (retval) {
				rtsx_trace(chip);
				return retval;
			}
			udelay(50);
			retval = rtsx_write_register(chip, SD_VP_CTL, 0xFF,
						     PHASE_NOT_RESET | sample_point);
			if (retval) {
				rtsx_trace(chip);
				return retval;
			}
		}
		udelay(100);

		rtsx_init_cmd(chip);
		rtsx_add_cmd(chip, WRITE_REG_CMD, SD_DCMPS_CTL, DCMPS_CHANGE,
			DCMPS_CHANGE);
		rtsx_add_cmd(chip, CHECK_REG_CMD, SD_DCMPS_CTL,
			DCMPS_CHANGE_DONE, DCMPS_CHANGE_DONE);
		retval = rtsx_send_cmd(chip, SD_CARD, 100);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			goto Fail;
		}

		val = *rtsx_get_cmd_data(chip);
		if (val & DCMPS_ERROR) {
			rtsx_trace(chip);
			goto Fail;
		}

		if ((val & DCMPS_CURRENT_PHASE) != sample_point) {
			rtsx_trace(chip);
			goto Fail;
		}

		retval = rtsx_write_register(chip, SD_DCMPS_CTL,
					     DCMPS_CHANGE, 0);
		if (retval) {
			rtsx_trace(chip);
			return retval;
		}
		if (ddr_rx) {
			retval = rtsx_write_register(chip, SD_VP_CTL,
						     PHASE_CHANGE, 0);
			if (retval) {
				rtsx_trace(chip);
				return retval;
			}
		} else {
			retval = rtsx_write_register(chip, CLK_CTL,
						     CHANGE_CLK, 0);
			if (retval) {
				rtsx_trace(chip);
				return retval;
			}
		}

		udelay(50);
	}

	retval = rtsx_write_register(chip, SD_CFG1, SD_ASYNC_FIFO_NOT_RST, 0);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}

	return STATUS_SUCCESS;

Fail:
	rtsx_read_register(chip, SD_VP_CTL, &val);
	dev_dbg(rtsx_dev(chip), "SD_VP_CTL: 0x%x\n", val);
	rtsx_read_register(chip, SD_DCMPS_CTL, &val);
	dev_dbg(rtsx_dev(chip), "SD_DCMPS_CTL: 0x%x\n", val);

	rtsx_write_register(chip, SD_DCMPS_CTL, DCMPS_CHANGE, 0);
	rtsx_write_register(chip, SD_VP_CTL, PHASE_CHANGE, 0);
	wait_timeout(10);
	sd_reset_dcm(chip, tune_dir);
	return STATUS_FAIL;
}

static int sd_check_spec(struct rtsx_chip *chip, u8 bus_width)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;
	u8 cmd[5], buf[8];

	retval = sd_send_cmd_get_rsp(chip, APP_CMD, sd_card->sd_addr,
				SD_RSP_TYPE_R1, NULL, 0);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	cmd[0] = 0x40 | SEND_SCR;
	cmd[1] = 0;
	cmd[2] = 0;
	cmd[3] = 0;
	cmd[4] = 0;

	retval = sd_read_data(chip, SD_TM_NORMAL_READ, cmd, 5, 8, 1, bus_width,
			buf, 8, 250);
	if (retval != STATUS_SUCCESS) {
		rtsx_clear_sd_error(chip);
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	memcpy(sd_card->raw_scr, buf, 8);

	if ((buf[0] & 0x0F) == 0) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}

static int sd_query_switch_result(struct rtsx_chip *chip, u8 func_group,
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
			rtsx_trace(chip);
			return STATUS_FAIL;
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
			rtsx_trace(chip);
			return STATUS_FAIL;
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
			rtsx_trace(chip);
			return STATUS_FAIL;
		}
	} else {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	if (func_group == SD_FUNC_GROUP_1) {
		if (!(buf[support_offset] & support_mask) ||
			((buf[query_switch_offset] & 0x0F) != query_switch)) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}
	}

	/* Check 'Busy Status' */
	if ((buf[DATA_STRUCTURE_VER_OFFSET] == 0x01) &&
		    ((buf[check_busy_offset] & switch_busy) == switch_busy)) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}

static int sd_check_switch_mode(struct rtsx_chip *chip, u8 mode,
		u8 func_group, u8 func_to_switch, u8 bus_width)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;
	u8 cmd[5], buf[64];

	dev_dbg(rtsx_dev(chip), "sd_check_switch_mode (mode = %d, func_group = %d, func_to_switch = %d)\n",
		mode, func_group, func_to_switch);

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

	retval = sd_read_data(chip, SD_TM_NORMAL_READ, cmd, 5, 64, 1, bus_width,
			buf, 64, 250);
	if (retval != STATUS_SUCCESS) {
		rtsx_clear_sd_error(chip);
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	dev_dbg(rtsx_dev(chip), "%*ph\n", 64, buf);

	if (func_group == NO_ARGUMENT) {
		sd_card->func_group1_mask = buf[0x0D];
		sd_card->func_group2_mask = buf[0x0B];
		sd_card->func_group3_mask = buf[0x09];
		sd_card->func_group4_mask = buf[0x07];

		dev_dbg(rtsx_dev(chip), "func_group1_mask = 0x%02x\n",
			buf[0x0D]);
		dev_dbg(rtsx_dev(chip), "func_group2_mask = 0x%02x\n",
			buf[0x0B]);
		dev_dbg(rtsx_dev(chip), "func_group3_mask = 0x%02x\n",
			buf[0x09]);
		dev_dbg(rtsx_dev(chip), "func_group4_mask = 0x%02x\n",
			buf[0x07]);
	} else {
		/* Maximum current consumption, check whether current is
		 * acceptable; bit[511:496] = 0x0000 means some error happened.
		 */
		u16 cc = ((u16)buf[0] << 8) | buf[1];

		dev_dbg(rtsx_dev(chip), "Maximum current consumption: %dmA\n",
			cc);
		if ((cc == 0) || (cc > 800)) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}

		retval = sd_query_switch_result(chip, func_group,
						func_to_switch, buf, 64);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}

		if ((cc > 400) || (func_to_switch > CURRENT_LIMIT_400)) {
			retval = rtsx_write_register(chip, OCPPARA2,
						     SD_OCP_THD_MASK,
						     chip->sd_800mA_ocp_thd);
			if (retval) {
				rtsx_trace(chip);
				return retval;
			}
			retval = rtsx_write_register(chip, CARD_PWR_CTL,
						     PMOS_STRG_MASK,
						     PMOS_STRG_800mA);
			if (retval) {
				rtsx_trace(chip);
				return retval;
			}
		}
	}

	return STATUS_SUCCESS;
}

static u8 downgrade_switch_mode(u8 func_group, u8 func_to_switch)
{
	if (func_group == SD_FUNC_GROUP_1) {
		if (func_to_switch > HS_SUPPORT)
			func_to_switch--;

	} else if (func_group == SD_FUNC_GROUP_4) {
		if (func_to_switch > CURRENT_LIMIT_200)
			func_to_switch--;
	}

	return func_to_switch;
}

static int sd_check_switch(struct rtsx_chip *chip,
		u8 func_group, u8 func_to_switch, u8 bus_width)
{
	int retval;
	int i;
	bool switch_good = false;

	for (i = 0; i < 3; i++) {
		if (detect_card_cd(chip, SD_CARD) != STATUS_SUCCESS) {
			sd_set_err_code(chip, SD_NO_CARD);
			rtsx_trace(chip);
			return STATUS_FAIL;
		}

		retval = sd_check_switch_mode(chip, SD_CHECK_MODE, func_group,
				func_to_switch, bus_width);
		if (retval == STATUS_SUCCESS) {
			u8 stat;

			retval = sd_check_switch_mode(chip, SD_SWITCH_MODE,
					func_group, func_to_switch, bus_width);
			if (retval == STATUS_SUCCESS) {
				switch_good = true;
				break;
			}

			retval = rtsx_read_register(chip, SD_STAT1, &stat);
			if (retval) {
				rtsx_trace(chip);
				return retval;
			}
			if (stat & SD_CRC16_ERR) {
				dev_dbg(rtsx_dev(chip), "SD CRC16 error when switching mode\n");
				rtsx_trace(chip);
				return STATUS_FAIL;
			}
		}

		func_to_switch = downgrade_switch_mode(func_group,
						func_to_switch);

		wait_timeout(20);
	}

	if (!switch_good) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}

static int sd_switch_function(struct rtsx_chip *chip, u8 bus_width)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;
	int i;
	u8 func_to_switch = 0;

	/* Get supported functions */
	retval = sd_check_switch_mode(chip, SD_CHECK_MODE,
			NO_ARGUMENT, NO_ARGUMENT, bus_width);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	sd_card->func_group1_mask &= ~(sd_card->sd_switch_fail);

	/* Function Group 1: Access Mode */
	for (i = 0; i < 4; i++) {
		switch ((u8)(chip->sd_speed_prior >> (i*8))) {
		case SDR104_SUPPORT:
			if ((sd_card->func_group1_mask & SDR104_SUPPORT_MASK)
					&& chip->sdr104_en) {
				func_to_switch = SDR104_SUPPORT;
			}
			break;

		case DDR50_SUPPORT:
			if ((sd_card->func_group1_mask & DDR50_SUPPORT_MASK)
					&& chip->ddr50_en) {
				func_to_switch = DDR50_SUPPORT;
			}
			break;

		case SDR50_SUPPORT:
			if ((sd_card->func_group1_mask & SDR50_SUPPORT_MASK)
					&& chip->sdr50_en) {
				func_to_switch = SDR50_SUPPORT;
			}
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
	dev_dbg(rtsx_dev(chip), "SD_FUNC_GROUP_1: func_to_switch = 0x%02x",
		func_to_switch);

#ifdef SUPPORT_SD_LOCK
	if ((sd_card->sd_lock_status & SD_SDR_RST) &&
	    (func_to_switch == DDR50_SUPPORT) &&
	    (sd_card->func_group1_mask & SDR50_SUPPORT_MASK)) {
		func_to_switch = SDR50_SUPPORT;
		dev_dbg(rtsx_dev(chip), "Using SDR50 instead of DDR50 for SD Lock\n");
	}
#endif

	if (func_to_switch) {
		retval = sd_check_switch(chip, SD_FUNC_GROUP_1, func_to_switch,
					bus_width);
		if (retval != STATUS_SUCCESS) {
			if (func_to_switch == SDR104_SUPPORT) {
				sd_card->sd_switch_fail = SDR104_SUPPORT_MASK;
			} else if (func_to_switch == DDR50_SUPPORT) {
				sd_card->sd_switch_fail = SDR104_SUPPORT_MASK |
					DDR50_SUPPORT_MASK;
			} else if (func_to_switch == SDR50_SUPPORT) {
				sd_card->sd_switch_fail = SDR104_SUPPORT_MASK |
					DDR50_SUPPORT_MASK | SDR50_SUPPORT_MASK;
			}
			rtsx_trace(chip);
			return STATUS_FAIL;
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

	if (CHK_SD_DDR50(sd_card)) {
		retval = rtsx_write_register(chip, SD_PUSH_POINT_CTL, 0x06,
					     0x04);
		if (retval) {
			rtsx_trace(chip);
			return retval;
		}
		retval = sd_set_sample_push_timing(chip);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}
	}

	if (!func_to_switch || (func_to_switch == HS_SUPPORT)) {
		/* Do not try to switch current limit if the card doesn't
		 * support UHS mode or we don't want it to support UHS mode
		 */
		return STATUS_SUCCESS;
	}

	/* Function Group 4: Current Limit */
	func_to_switch = 0xFF;

	for (i = 0; i < 4; i++) {
		switch ((u8)(chip->sd_current_prior >> (i*8))) {
		case CURRENT_LIMIT_800:
			if (sd_card->func_group4_mask & CURRENT_LIMIT_800_MASK)
				func_to_switch = CURRENT_LIMIT_800;

			break;

		case CURRENT_LIMIT_600:
			if (sd_card->func_group4_mask & CURRENT_LIMIT_600_MASK)
				func_to_switch = CURRENT_LIMIT_600;

			break;

		case CURRENT_LIMIT_400:
			if (sd_card->func_group4_mask & CURRENT_LIMIT_400_MASK)
				func_to_switch = CURRENT_LIMIT_400;

			break;

		case CURRENT_LIMIT_200:
			if (sd_card->func_group4_mask & CURRENT_LIMIT_200_MASK)
				func_to_switch = CURRENT_LIMIT_200;

			break;

		default:
			continue;
		}

		if (func_to_switch != 0xFF)
			break;
	}

	dev_dbg(rtsx_dev(chip), "SD_FUNC_GROUP_4: func_to_switch = 0x%02x",
		func_to_switch);

	if (func_to_switch <= CURRENT_LIMIT_800) {
		retval = sd_check_switch(chip, SD_FUNC_GROUP_4, func_to_switch,
					bus_width);
		if (retval != STATUS_SUCCESS) {
			if (sd_check_err_code(chip, SD_NO_CARD)) {
				rtsx_trace(chip);
				return STATUS_FAIL;
			}
		}
		dev_dbg(rtsx_dev(chip), "Switch current limit finished! (%d)\n",
			retval);
	}

	if (CHK_SD_DDR50(sd_card)) {
		retval = rtsx_write_register(chip, SD_PUSH_POINT_CTL, 0x06, 0);
		if (retval) {
			rtsx_trace(chip);
			return retval;
		}
	}

	return STATUS_SUCCESS;
}

static int sd_wait_data_idle(struct rtsx_chip *chip)
{
	int retval = STATUS_TIMEDOUT;
	int i;
	u8 val = 0;

	for (i = 0; i < 100; i++) {
		retval = rtsx_read_register(chip, SD_DATA_STATE, &val);
		if (retval) {
			rtsx_trace(chip);
			return retval;
		}
		if (val & SD_DATA_IDLE) {
			retval = STATUS_SUCCESS;
			break;
		}
		udelay(100);
	}
	dev_dbg(rtsx_dev(chip), "SD_DATA_STATE: 0x%02x\n", val);

	return retval;
}

static int sd_sdr_tuning_rx_cmd(struct rtsx_chip *chip, u8 sample_point)
{
	int retval;
	u8 cmd[5];

	retval = sd_change_phase(chip, sample_point, TUNE_RX);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	cmd[0] = 0x40 | SEND_TUNING_PATTERN;
	cmd[1] = 0;
	cmd[2] = 0;
	cmd[3] = 0;
	cmd[4] = 0;

	retval = sd_read_data(chip, SD_TM_AUTO_TUNING,
			cmd, 5, 0x40, 1, SD_BUS_WIDTH_4, NULL, 0, 100);
	if (retval != STATUS_SUCCESS) {
		(void)sd_wait_data_idle(chip);

		rtsx_clear_sd_error(chip);
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}

static int sd_ddr_tuning_rx_cmd(struct rtsx_chip *chip, u8 sample_point)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;
	u8 cmd[5];

	retval = sd_change_phase(chip, sample_point, TUNE_RX);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	dev_dbg(rtsx_dev(chip), "sd ddr tuning rx\n");

	retval = sd_send_cmd_get_rsp(chip, APP_CMD, sd_card->sd_addr,
				SD_RSP_TYPE_R1, NULL, 0);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	cmd[0] = 0x40 | SD_STATUS;
	cmd[1] = 0;
	cmd[2] = 0;
	cmd[3] = 0;
	cmd[4] = 0;

	retval = sd_read_data(chip, SD_TM_NORMAL_READ,
			cmd, 5, 64, 1, SD_BUS_WIDTH_4, NULL, 0, 100);
	if (retval != STATUS_SUCCESS) {
		(void)sd_wait_data_idle(chip);

		rtsx_clear_sd_error(chip);
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}

static int mmc_ddr_tunning_rx_cmd(struct rtsx_chip *chip, u8 sample_point)
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
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	dev_dbg(rtsx_dev(chip), "mmc ddr tuning rx\n");

	cmd[0] = 0x40 | SEND_EXT_CSD;
	cmd[1] = 0;
	cmd[2] = 0;
	cmd[3] = 0;
	cmd[4] = 0;

	retval = sd_read_data(chip, SD_TM_NORMAL_READ,
			cmd, 5, 0x200, 1, bus_width, NULL, 0, 100);
	if (retval != STATUS_SUCCESS) {
		(void)sd_wait_data_idle(chip);

		rtsx_clear_sd_error(chip);
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}

static int sd_sdr_tuning_tx_cmd(struct rtsx_chip *chip, u8 sample_point)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;

	retval = sd_change_phase(chip, sample_point, TUNE_TX);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	retval = rtsx_write_register(chip, SD_CFG3, SD_RSP_80CLK_TIMEOUT_EN,
				     SD_RSP_80CLK_TIMEOUT_EN);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}

	retval = sd_send_cmd_get_rsp(chip, SEND_STATUS, sd_card->sd_addr,
		SD_RSP_TYPE_R1, NULL, 0);
	if (retval != STATUS_SUCCESS) {
		if (sd_check_err_code(chip, SD_RSP_TIMEOUT)) {
			rtsx_write_register(chip, SD_CFG3,
					SD_RSP_80CLK_TIMEOUT_EN, 0);
			rtsx_trace(chip);
			return STATUS_FAIL;
		}
	}

	retval = rtsx_write_register(chip, SD_CFG3, SD_RSP_80CLK_TIMEOUT_EN,
				     0);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}

	return STATUS_SUCCESS;
}

static int sd_ddr_tuning_tx_cmd(struct rtsx_chip *chip, u8 sample_point)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;
	u8 cmd[5], bus_width;

	retval = sd_change_phase(chip, sample_point, TUNE_TX);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

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

	retval = sd_wait_state_data_ready(chip, 0x08, 1, 1000);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	retval = rtsx_write_register(chip, SD_CFG3, SD_RSP_80CLK_TIMEOUT_EN,
				     SD_RSP_80CLK_TIMEOUT_EN);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}

	cmd[0] = 0x40 | PROGRAM_CSD;
	cmd[1] = 0;
	cmd[2] = 0;
	cmd[3] = 0;
	cmd[4] = 0;

	retval = sd_write_data(chip, SD_TM_AUTO_WRITE_2,
			cmd, 5, 16, 1, bus_width, sd_card->raw_csd, 16, 100);
	if (retval != STATUS_SUCCESS) {
		rtsx_clear_sd_error(chip);
		rtsx_write_register(chip, SD_CFG3, SD_RSP_80CLK_TIMEOUT_EN, 0);
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	retval = rtsx_write_register(chip, SD_CFG3, SD_RSP_80CLK_TIMEOUT_EN,
				     0);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}

	sd_send_cmd_get_rsp(chip, SEND_STATUS, sd_card->sd_addr, SD_RSP_TYPE_R1,
			NULL, 0);

	return STATUS_SUCCESS;
}

static u8 sd_search_final_phase(struct rtsx_chip *chip, u32 phase_map,
				u8 tune_dir)
{
	struct sd_info *sd_card = &(chip->sd_card);
	struct timing_phase_path path[MAX_PHASE + 1];
	int i, j, cont_path_cnt;
	bool new_block;
	int max_len, final_path_idx;
	u8 final_phase = 0xFF;

	if (phase_map == 0xFFFFFFFF) {
		if (tune_dir == TUNE_RX)
			final_phase = (u8)chip->sd_default_rx_phase;
		else
			final_phase = (u8)chip->sd_default_tx_phase;

		goto Search_Finish;
	}

	cont_path_cnt = 0;
	new_block = true;
	j = 0;
	for (i = 0; i < MAX_PHASE + 1; i++) {
		if (phase_map & (1 << i)) {
			if (new_block) {
				new_block = false;
				j = cont_path_cnt++;
				path[j].start = i;
				path[j].end = i;
			} else {
				path[j].end = i;
			}
		} else {
			new_block = true;
			if (cont_path_cnt) {
				int idx = cont_path_cnt - 1;

				path[idx].len = path[idx].end -
					path[idx].start + 1;
				path[idx].mid = path[idx].start +
					path[idx].len / 2;
			}
		}
	}

	if (cont_path_cnt == 0) {
		dev_dbg(rtsx_dev(chip), "No continuous phase path\n");
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
			final_phase = (u8)path[i].mid;
			final_path_idx = i;
		}

		dev_dbg(rtsx_dev(chip), "path[%d].start = %d\n",
			i, path[i].start);
		dev_dbg(rtsx_dev(chip), "path[%d].end = %d\n", i, path[i].end);
		dev_dbg(rtsx_dev(chip), "path[%d].len = %d\n", i, path[i].len);
		dev_dbg(rtsx_dev(chip), "path[%d].mid = %d\n", i, path[i].mid);
		dev_dbg(rtsx_dev(chip), "\n");
	}

	if (tune_dir == TUNE_TX) {
		if (CHK_SD_SDR104(sd_card)) {
			if (max_len > 15) {
				int temp_mid = (max_len - 16) / 2;
				int temp_final_phase =
					path[final_path_idx].end -
					(max_len - (6 + temp_mid));

				if (temp_final_phase < 0)
					final_phase = (u8)(temp_final_phase +
							MAX_PHASE + 1);
				else
					final_phase = (u8)temp_final_phase;
			}
		} else if (CHK_SD_SDR50(sd_card)) {
			if (max_len > 12) {
				int temp_mid = (max_len - 13) / 2;
				int temp_final_phase =
					path[final_path_idx].end -
					(max_len - (3 + temp_mid));

				if (temp_final_phase < 0)
					final_phase = (u8)(temp_final_phase +
							MAX_PHASE + 1);
				else
					final_phase = (u8)temp_final_phase;
			}
		}
	}

Search_Finish:
	dev_dbg(rtsx_dev(chip), "Final chosen phase: %d\n", final_phase);
	return final_phase;
}

static int sd_tuning_rx(struct rtsx_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;
	int i, j;
	u32 raw_phase_map[3], phase_map;
	u8 final_phase;
	int (*tuning_cmd)(struct rtsx_chip *chip, u8 sample_point);

	if (CHK_SD(sd_card)) {
		if (CHK_SD_DDR50(sd_card))
			tuning_cmd = sd_ddr_tuning_rx_cmd;
		else
			tuning_cmd = sd_sdr_tuning_rx_cmd;

	} else {
		if (CHK_MMC_DDR52(sd_card)) {
			tuning_cmd = mmc_ddr_tunning_rx_cmd;
		} else {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}
	}

	for (i = 0; i < 3; i++) {
		raw_phase_map[i] = 0;
		for (j = MAX_PHASE; j >= 0; j--) {
			if (detect_card_cd(chip, SD_CARD) != STATUS_SUCCESS) {
				sd_set_err_code(chip, SD_NO_CARD);
				rtsx_trace(chip);
				return STATUS_FAIL;
			}

			retval = tuning_cmd(chip, (u8)j);
			if (retval == STATUS_SUCCESS)
				raw_phase_map[i] |= 1 << j;
		}
	}

	phase_map = raw_phase_map[0] & raw_phase_map[1] & raw_phase_map[2];
	for (i = 0; i < 3; i++)
		dev_dbg(rtsx_dev(chip), "RX raw_phase_map[%d] = 0x%08x\n",
			i, raw_phase_map[i]);

	dev_dbg(rtsx_dev(chip), "RX phase_map = 0x%08x\n", phase_map);

	final_phase = sd_search_final_phase(chip, phase_map, TUNE_RX);
	if (final_phase == 0xFF) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	retval = sd_change_phase(chip, final_phase, TUNE_RX);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}

static int sd_ddr_pre_tuning_tx(struct rtsx_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;
	int i;
	u32 phase_map;
	u8 final_phase;

	retval = rtsx_write_register(chip, SD_CFG3, SD_RSP_80CLK_TIMEOUT_EN,
				     SD_RSP_80CLK_TIMEOUT_EN);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}

	phase_map = 0;
	for (i = MAX_PHASE; i >= 0; i--) {
		if (detect_card_cd(chip, SD_CARD) != STATUS_SUCCESS) {
			sd_set_err_code(chip, SD_NO_CARD);
			rtsx_write_register(chip, SD_CFG3,
						SD_RSP_80CLK_TIMEOUT_EN, 0);
			rtsx_trace(chip);
			return STATUS_FAIL;
		}

		retval = sd_change_phase(chip, (u8)i, TUNE_TX);
		if (retval != STATUS_SUCCESS)
			continue;

		retval = sd_send_cmd_get_rsp(chip, SEND_STATUS,
					sd_card->sd_addr, SD_RSP_TYPE_R1, NULL,
					0);
		if ((retval == STATUS_SUCCESS) ||
			!sd_check_err_code(chip, SD_RSP_TIMEOUT))
			phase_map |= 1 << i;
	}

	retval = rtsx_write_register(chip, SD_CFG3, SD_RSP_80CLK_TIMEOUT_EN,
				     0);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}

	dev_dbg(rtsx_dev(chip), "DDR TX pre tune phase_map = 0x%08x\n",
		phase_map);

	final_phase = sd_search_final_phase(chip, phase_map, TUNE_TX);
	if (final_phase == 0xFF) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	retval = sd_change_phase(chip, final_phase, TUNE_TX);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	dev_dbg(rtsx_dev(chip), "DDR TX pre tune phase: %d\n",
		(int)final_phase);

	return STATUS_SUCCESS;
}

static int sd_tuning_tx(struct rtsx_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;
	int i, j;
	u32 raw_phase_map[3], phase_map;
	u8 final_phase;
	int (*tuning_cmd)(struct rtsx_chip *chip, u8 sample_point);

	if (CHK_SD(sd_card)) {
		if (CHK_SD_DDR50(sd_card))
			tuning_cmd = sd_ddr_tuning_tx_cmd;
		else
			tuning_cmd = sd_sdr_tuning_tx_cmd;

	} else {
		if (CHK_MMC_DDR52(sd_card)) {
			tuning_cmd = sd_ddr_tuning_tx_cmd;
		} else {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}
	}

	for (i = 0; i < 3; i++) {
		raw_phase_map[i] = 0;
		for (j = MAX_PHASE; j >= 0; j--) {
			if (detect_card_cd(chip, SD_CARD) != STATUS_SUCCESS) {
				sd_set_err_code(chip, SD_NO_CARD);
				rtsx_write_register(chip, SD_CFG3,
						    SD_RSP_80CLK_TIMEOUT_EN, 0);
				rtsx_trace(chip);
				return STATUS_FAIL;
			}

			retval = tuning_cmd(chip, (u8)j);
			if (retval == STATUS_SUCCESS)
				raw_phase_map[i] |= 1 << j;
		}
	}

	phase_map = raw_phase_map[0] & raw_phase_map[1] & raw_phase_map[2];
	for (i = 0; i < 3; i++)
		dev_dbg(rtsx_dev(chip), "TX raw_phase_map[%d] = 0x%08x\n",
			i, raw_phase_map[i]);

	dev_dbg(rtsx_dev(chip), "TX phase_map = 0x%08x\n", phase_map);

	final_phase = sd_search_final_phase(chip, phase_map, TUNE_TX);
	if (final_phase == 0xFF) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	retval = sd_change_phase(chip, final_phase, TUNE_TX);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}

static int sd_sdr_tuning(struct rtsx_chip *chip)
{
	int retval;

	retval = sd_tuning_tx(chip);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	retval = sd_tuning_rx(chip);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}

static int sd_ddr_tuning(struct rtsx_chip *chip)
{
	int retval;

	if (!(chip->sd_ctl & SD_DDR_TX_PHASE_SET_BY_USER)) {
		retval = sd_ddr_pre_tuning_tx(chip);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}
	} else {
		retval = sd_change_phase(chip, (u8)chip->sd_ddr_tx_phase,
					TUNE_TX);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}
	}

	retval = sd_tuning_rx(chip);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	if (!(chip->sd_ctl & SD_DDR_TX_PHASE_SET_BY_USER)) {
		retval = sd_tuning_tx(chip);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}
	}

	return STATUS_SUCCESS;
}

static int mmc_ddr_tuning(struct rtsx_chip *chip)
{
	int retval;

	if (!(chip->sd_ctl & MMC_DDR_TX_PHASE_SET_BY_USER)) {
		retval = sd_ddr_pre_tuning_tx(chip);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}
	} else {
		retval = sd_change_phase(chip, (u8)chip->mmc_ddr_tx_phase,
					TUNE_TX);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}
	}

	retval = sd_tuning_rx(chip);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	if (!(chip->sd_ctl & MMC_DDR_TX_PHASE_SET_BY_USER)) {
		retval = sd_tuning_tx(chip);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}
	}

	return STATUS_SUCCESS;
}

int sd_switch_clock(struct rtsx_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;
	int re_tuning = 0;

	retval = select_card(chip, SD_CARD);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	retval = switch_clock(chip, sd_card->sd_clock);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

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

		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}
	}

	return STATUS_SUCCESS;
}

static int sd_prepare_reset(struct rtsx_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;

	if (chip->asic_code)
		sd_card->sd_clock = 29;
	else
		sd_card->sd_clock = CLK_30;

	sd_card->sd_type = 0;
	sd_card->seq_mode = 0;
	sd_card->sd_data_buf_ready = 0;
	sd_card->capacity = 0;

#ifdef SUPPORT_SD_LOCK
	sd_card->sd_lock_status = 0;
	sd_card->sd_erase_status = 0;
#endif

	chip->capacity[chip->card2lun[SD_CARD]] = 0;
	chip->sd_io = 0;

	retval = sd_set_init_para(chip);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return retval;
	}

	retval = rtsx_write_register(chip, REG_SD_CFG1, 0xFF, 0x40);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}

	retval = rtsx_write_register(chip, CARD_STOP, SD_STOP | SD_CLR_ERR,
				     SD_STOP | SD_CLR_ERR);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}

	retval = select_card(chip, SD_CARD);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}

static int sd_pull_ctl_disable(struct rtsx_chip *chip)
{
	int retval;

	if (CHECK_PID(chip, 0x5208)) {
		retval = rtsx_write_register(chip, CARD_PULL_CTL1, 0xFF,
					     XD_D3_PD | SD_D7_PD | SD_CLK_PD | SD_D5_PD);
		if (retval) {
			rtsx_trace(chip);
			return retval;
		}
		retval = rtsx_write_register(chip, CARD_PULL_CTL2, 0xFF,
					     SD_D6_PD | SD_D0_PD | SD_D1_PD | XD_D5_PD);
		if (retval) {
			rtsx_trace(chip);
			return retval;
		}
		retval = rtsx_write_register(chip, CARD_PULL_CTL3, 0xFF,
					     SD_D4_PD | XD_CE_PD | XD_CLE_PD | XD_CD_PU);
		if (retval) {
			rtsx_trace(chip);
			return retval;
		}
		retval = rtsx_write_register(chip, CARD_PULL_CTL4, 0xFF,
					     XD_RDY_PD | SD_D3_PD | SD_D2_PD | XD_ALE_PD);
		if (retval) {
			rtsx_trace(chip);
			return retval;
		}
		retval = rtsx_write_register(chip, CARD_PULL_CTL5, 0xFF,
					     MS_INS_PU | SD_WP_PD | SD_CD_PU | SD_CMD_PD);
		if (retval) {
			rtsx_trace(chip);
			return retval;
		}
		retval = rtsx_write_register(chip, CARD_PULL_CTL6, 0xFF,
					     MS_D5_PD | MS_D4_PD);
		if (retval) {
			rtsx_trace(chip);
			return retval;
		}
	} else if (CHECK_PID(chip, 0x5288)) {
		if (CHECK_BARO_PKG(chip, QFN)) {
			retval = rtsx_write_register(chip, CARD_PULL_CTL1,
						     0xFF, 0x55);
			if (retval) {
				rtsx_trace(chip);
				return retval;
			}
			retval = rtsx_write_register(chip, CARD_PULL_CTL2,
						     0xFF, 0x55);
			if (retval) {
				rtsx_trace(chip);
				return retval;
			}
			retval = rtsx_write_register(chip, CARD_PULL_CTL3,
						     0xFF, 0x4B);
			if (retval) {
				rtsx_trace(chip);
				return retval;
			}
			retval = rtsx_write_register(chip, CARD_PULL_CTL4,
						     0xFF, 0x69);
			if (retval) {
				rtsx_trace(chip);
				return retval;
			}
		}
	}

	return STATUS_SUCCESS;
}

int sd_pull_ctl_enable(struct rtsx_chip *chip)
{
	int retval;

	rtsx_init_cmd(chip);

	if (CHECK_PID(chip, 0x5208)) {
		rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL1, 0xFF,
			XD_D3_PD | SD_DAT7_PU | SD_CLK_NP | SD_D5_PU);
		rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL2, 0xFF,
			SD_D6_PU | SD_D0_PU | SD_D1_PU | XD_D5_PD);
		rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL3, 0xFF,
			SD_D4_PU | XD_CE_PD | XD_CLE_PD | XD_CD_PU);
		rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL4, 0xFF,
			XD_RDY_PD | SD_D3_PU | SD_D2_PU | XD_ALE_PD);
		rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL5, 0xFF,
			MS_INS_PU | SD_WP_PU | SD_CD_PU | SD_CMD_PU);
		rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL6, 0xFF,
			MS_D5_PD | MS_D4_PD);
	} else if (CHECK_PID(chip, 0x5288)) {
		if (CHECK_BARO_PKG(chip, QFN)) {
			rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL1, 0xFF,
				0xA8);
			rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL2, 0xFF,
				0x5A);
			rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL3, 0xFF,
				0x95);
			rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL4, 0xFF,
				0xAA);
		}
	}

	retval = rtsx_send_cmd(chip, SD_CARD, 100);
	if (retval < 0) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}

static int sd_init_power(struct rtsx_chip *chip)
{
	int retval;

	retval = sd_power_off_card3v3(chip);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	if (!chip->ft2_fast_mode)
		wait_timeout(250);

	retval = enable_card_clock(chip, SD_CARD);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	if (chip->asic_code) {
		retval = sd_pull_ctl_enable(chip);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}
	} else {
		retval = rtsx_write_register(chip, FPGA_PULL_CTL,
					     FPGA_SD_PULL_CTL_BIT | 0x20, 0);
		if (retval) {
			rtsx_trace(chip);
			return retval;
		}
	}

	if (!chip->ft2_fast_mode) {
		retval = card_power_on(chip, SD_CARD);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}

		wait_timeout(260);

#ifdef SUPPORT_OCP
		if (chip->ocp_stat & (SD_OC_NOW | SD_OC_EVER)) {
			dev_dbg(rtsx_dev(chip), "Over current, OCPSTAT is 0x%x\n",
				chip->ocp_stat);
			rtsx_trace(chip);
			return STATUS_FAIL;
		}
#endif
	}

	retval = rtsx_write_register(chip, CARD_OE, SD_OUTPUT_EN,
				     SD_OUTPUT_EN);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}

	return STATUS_SUCCESS;
}

static int sd_dummy_clock(struct rtsx_chip *chip)
{
	int retval;

	retval = rtsx_write_register(chip, REG_SD_CFG3, 0x01, 0x01);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}
	wait_timeout(5);
	retval = rtsx_write_register(chip, REG_SD_CFG3, 0x01, 0);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}

	return STATUS_SUCCESS;
}

static int sd_read_lba0(struct rtsx_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;
	u8 cmd[5], bus_width;

	cmd[0] = 0x40 | READ_SINGLE_BLOCK;
	cmd[1] = 0;
	cmd[2] = 0;
	cmd[3] = 0;
	cmd[4] = 0;

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

	retval = sd_read_data(chip, SD_TM_NORMAL_READ, cmd,
		5, 512, 1, bus_width, NULL, 0, 100);
	if (retval != STATUS_SUCCESS) {
		rtsx_clear_sd_error(chip);
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}

static int sd_check_wp_state(struct rtsx_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;
	u32 val;
	u16 sd_card_type;
	u8 cmd[5], buf[64];

	retval = sd_send_cmd_get_rsp(chip, APP_CMD,
			sd_card->sd_addr, SD_RSP_TYPE_R1, NULL, 0);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	cmd[0] = 0x40 | SD_STATUS;
	cmd[1] = 0;
	cmd[2] = 0;
	cmd[3] = 0;
	cmd[4] = 0;

	retval = sd_read_data(chip, SD_TM_NORMAL_READ, cmd, 5, 64, 1,
			SD_BUS_WIDTH_4, buf, 64, 250);
	if (retval != STATUS_SUCCESS) {
		rtsx_clear_sd_error(chip);

		sd_send_cmd_get_rsp(chip, SEND_STATUS, sd_card->sd_addr,
				SD_RSP_TYPE_R1, NULL, 0);
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	dev_dbg(rtsx_dev(chip), "ACMD13:\n");
	dev_dbg(rtsx_dev(chip), "%*ph\n", 64, buf);

	sd_card_type = ((u16)buf[2] << 8) | buf[3];
	dev_dbg(rtsx_dev(chip), "sd_card_type = 0x%04x\n", sd_card_type);
	if ((sd_card_type == 0x0001) || (sd_card_type == 0x0002)) {
		/* ROM card or OTP */
		chip->card_wp |= SD_CARD;
	}

	/* Check SD Machanical Write-Protect Switch */
	val = rtsx_readl(chip, RTSX_BIPR);
	if (val & SD_WRITE_PROTECT)
		chip->card_wp |= SD_CARD;

	return STATUS_SUCCESS;
}

static int reset_sd(struct rtsx_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	bool hi_cap_flow = false;
	int retval, i = 0, j = 0, k = 0;
	bool sd_dont_switch = false;
	bool support_1v8 = false;
	bool try_sdio = true;
	u8 rsp[16];
	u8 switch_bus_width;
	u32 voltage = 0;
	bool sd20_mode = false;

	SET_SD(sd_card);

Switch_Fail:

	i = 0;
	j = 0;
	k = 0;
	hi_cap_flow = false;

#ifdef SUPPORT_SD_LOCK
	if (sd_card->sd_lock_status & SD_UNLOCK_POW_ON)
		goto SD_UNLOCK_ENTRY;
#endif

	retval = sd_prepare_reset(chip);
	if (retval != STATUS_SUCCESS)
		goto Status_Fail;

	retval = sd_dummy_clock(chip);
	if (retval != STATUS_SUCCESS)
		goto Status_Fail;

	if (CHK_SDIO_EXIST(chip) && !CHK_SDIO_IGNORED(chip) && try_sdio) {
		int rty_cnt = 0;

		for (; rty_cnt < chip->sdio_retry_cnt; rty_cnt++) {
			if (detect_card_cd(chip, SD_CARD) != STATUS_SUCCESS) {
				sd_set_err_code(chip, SD_NO_CARD);
				goto Status_Fail;
			}

			retval = sd_send_cmd_get_rsp(chip, IO_SEND_OP_COND, 0,
						SD_RSP_TYPE_R4, rsp, 5);
			if (retval == STATUS_SUCCESS) {
				int func_num = (rsp[1] >> 4) & 0x07;

				if (func_num) {
					dev_dbg(rtsx_dev(chip), "SD_IO card (Function number: %d)!\n",
						func_num);
					chip->sd_io = 1;
					goto Status_Fail;
				}

				break;
			}

			sd_init_power(chip);

			sd_dummy_clock(chip);
		}

		dev_dbg(rtsx_dev(chip), "Normal card!\n");
	}

	/* Start Initialization Process of SD Card */
RTY_SD_RST:
	retval = sd_send_cmd_get_rsp(chip, GO_IDLE_STATE, 0, SD_RSP_TYPE_R0,
				NULL, 0);
	if (retval != STATUS_SUCCESS)
		goto Status_Fail;

	wait_timeout(20);

	retval = sd_send_cmd_get_rsp(chip, SEND_IF_COND, 0x000001AA,
				SD_RSP_TYPE_R7, rsp, 5);
	if (retval == STATUS_SUCCESS) {
		if ((rsp[4] == 0xAA) && ((rsp[3] & 0x0f) == 0x01)) {
			hi_cap_flow = true;
			voltage = SUPPORT_VOLTAGE | 0x40000000;
		}
	}

	if (!hi_cap_flow) {
		voltage = SUPPORT_VOLTAGE;

		retval = sd_send_cmd_get_rsp(chip, GO_IDLE_STATE, 0,
					SD_RSP_TYPE_R0, NULL, 0);
		if (retval != STATUS_SUCCESS)
			goto Status_Fail;

		wait_timeout(20);
	}

	do {
		retval = sd_send_cmd_get_rsp(chip, APP_CMD, 0, SD_RSP_TYPE_R1,
					NULL, 0);
		if (retval != STATUS_SUCCESS) {
			if (detect_card_cd(chip, SD_CARD) != STATUS_SUCCESS) {
				sd_set_err_code(chip, SD_NO_CARD);
				goto Status_Fail;
			}

			j++;
			if (j < 3)
				goto RTY_SD_RST;
			else
				goto Status_Fail;
		}

		retval = sd_send_cmd_get_rsp(chip, SD_APP_OP_COND, voltage,
					SD_RSP_TYPE_R3, rsp, 5);
		if (retval != STATUS_SUCCESS) {
			k++;
			if (k < 3)
				goto RTY_SD_RST;
			else
				goto Status_Fail;
		}

		i++;
		wait_timeout(20);
	} while (!(rsp[1] & 0x80) && (i < 255));

	if (i == 255)
		goto Status_Fail;

	if (hi_cap_flow) {
		if (rsp[1] & 0x40)
			SET_SD_HCXC(sd_card);
		else
			CLR_SD_HCXC(sd_card);

		support_1v8 = false;
	} else {
		CLR_SD_HCXC(sd_card);
		support_1v8 = false;
	}
	dev_dbg(rtsx_dev(chip), "support_1v8 = %d\n", support_1v8);

	if (support_1v8) {
		retval = sd_voltage_switch(chip);
		if (retval != STATUS_SUCCESS)
			goto Status_Fail;
	}

	retval = sd_send_cmd_get_rsp(chip, ALL_SEND_CID, 0, SD_RSP_TYPE_R2,
				NULL, 0);
	if (retval != STATUS_SUCCESS)
		goto Status_Fail;

	for (i = 0; i < 3; i++) {
		retval = sd_send_cmd_get_rsp(chip, SEND_RELATIVE_ADDR, 0,
					SD_RSP_TYPE_R6, rsp, 5);
		if (retval != STATUS_SUCCESS)
			goto Status_Fail;

		sd_card->sd_addr = (u32)rsp[1] << 24;
		sd_card->sd_addr += (u32)rsp[2] << 16;

		if (sd_card->sd_addr)
			break;
	}

	retval = sd_check_csd(chip, 1);
	if (retval != STATUS_SUCCESS)
		goto Status_Fail;

	retval = sd_select_card(chip, 1);
	if (retval != STATUS_SUCCESS)
		goto Status_Fail;

#ifdef SUPPORT_SD_LOCK
SD_UNLOCK_ENTRY:
	retval = sd_update_lock_status(chip);
	if (retval != STATUS_SUCCESS)
		goto Status_Fail;

	if (sd_card->sd_lock_status & SD_LOCKED) {
		sd_card->sd_lock_status |= (SD_LOCK_1BIT_MODE | SD_PWD_EXIST);
		return STATUS_SUCCESS;
	} else if (!(sd_card->sd_lock_status & SD_UNLOCK_POW_ON)) {
		sd_card->sd_lock_status &= ~SD_PWD_EXIST;
	}
#endif

	retval = sd_send_cmd_get_rsp(chip, APP_CMD, sd_card->sd_addr,
				SD_RSP_TYPE_R1, NULL, 0);
	if (retval != STATUS_SUCCESS)
		goto Status_Fail;

	retval = sd_send_cmd_get_rsp(chip, SET_CLR_CARD_DETECT, 0,
				SD_RSP_TYPE_R1, NULL, 0);
	if (retval != STATUS_SUCCESS)
		goto Status_Fail;

	if (support_1v8) {
		retval = sd_send_cmd_get_rsp(chip, APP_CMD, sd_card->sd_addr,
					SD_RSP_TYPE_R1, NULL, 0);
		if (retval != STATUS_SUCCESS)
			goto Status_Fail;

		retval = sd_send_cmd_get_rsp(chip, SET_BUS_WIDTH, 2,
					SD_RSP_TYPE_R1, NULL, 0);
		if (retval != STATUS_SUCCESS)
			goto Status_Fail;

		switch_bus_width = SD_BUS_WIDTH_4;
	} else {
		switch_bus_width = SD_BUS_WIDTH_1;
	}

	retval = sd_send_cmd_get_rsp(chip, SET_BLOCKLEN, 0x200, SD_RSP_TYPE_R1,
				NULL, 0);
	if (retval != STATUS_SUCCESS)
		goto Status_Fail;

	retval = sd_set_clock_divider(chip, SD_CLK_DIVIDE_0);
	if (retval != STATUS_SUCCESS)
		goto Status_Fail;

	if (!(sd_card->raw_csd[4] & 0x40))
		sd_dont_switch = true;

	if (!sd_dont_switch) {
		if (sd20_mode) {
			/* Set sd_switch_fail here, because we needn't
			 * switch to UHS mode
			 */
			sd_card->sd_switch_fail = SDR104_SUPPORT_MASK |
				DDR50_SUPPORT_MASK | SDR50_SUPPORT_MASK;
		}

		/* Check the card whether follow SD1.1 spec or higher */
		retval = sd_check_spec(chip, switch_bus_width);
		if (retval == STATUS_SUCCESS) {
			retval = sd_switch_function(chip, switch_bus_width);
			if (retval != STATUS_SUCCESS) {
				sd_init_power(chip);
				sd_dont_switch = true;
				try_sdio = false;

				goto Switch_Fail;
			}
		} else {
			if (support_1v8) {
				sd_init_power(chip);
				sd_dont_switch = true;
				try_sdio = false;

				goto Switch_Fail;
			}
		}
	}

	if (!support_1v8) {
		retval = sd_send_cmd_get_rsp(chip, APP_CMD, sd_card->sd_addr,
					SD_RSP_TYPE_R1, NULL, 0);
		if (retval != STATUS_SUCCESS)
			goto Status_Fail;

		retval = sd_send_cmd_get_rsp(chip, SET_BUS_WIDTH, 2,
					SD_RSP_TYPE_R1, NULL, 0);
		if (retval != STATUS_SUCCESS)
			goto Status_Fail;
	}

#ifdef SUPPORT_SD_LOCK
	sd_card->sd_lock_status &= ~SD_LOCK_1BIT_MODE;
#endif

	if (!sd20_mode && CHK_SD30_SPEED(sd_card)) {
		int read_lba0 = 1;

		retval = rtsx_write_register(chip, SD30_DRIVE_SEL, 0x07,
					     chip->sd30_drive_sel_1v8);
		if (retval) {
			rtsx_trace(chip);
			return retval;
		}

		retval = sd_set_init_para(chip);
		if (retval != STATUS_SUCCESS)
			goto Status_Fail;

		if (CHK_SD_DDR50(sd_card))
			retval = sd_ddr_tuning(chip);
		else
			retval = sd_sdr_tuning(chip);

		if (retval != STATUS_SUCCESS) {
			if (sd20_mode) {
				goto Status_Fail;
			} else {
				retval = sd_init_power(chip);
				if (retval != STATUS_SUCCESS)
					goto Status_Fail;

				try_sdio = false;
				sd20_mode = true;
				goto Switch_Fail;
			}
		}

		sd_send_cmd_get_rsp(chip, SEND_STATUS, sd_card->sd_addr,
				SD_RSP_TYPE_R1, NULL, 0);

		if (CHK_SD_DDR50(sd_card)) {
			retval = sd_wait_state_data_ready(chip, 0x08, 1, 1000);
			if (retval != STATUS_SUCCESS)
				read_lba0 = 0;
		}

		if (read_lba0) {
			retval = sd_read_lba0(chip);
			if (retval != STATUS_SUCCESS) {
				if (sd20_mode) {
					goto Status_Fail;
				} else {
					retval = sd_init_power(chip);
					if (retval != STATUS_SUCCESS)
						goto Status_Fail;

					try_sdio = false;
					sd20_mode = true;
					goto Switch_Fail;
				}
			}
		}
	}

	retval = sd_check_wp_state(chip);
	if (retval != STATUS_SUCCESS)
		goto Status_Fail;

	chip->card_bus_width[chip->card2lun[SD_CARD]] = 4;

#ifdef SUPPORT_SD_LOCK
	if (sd_card->sd_lock_status & SD_UNLOCK_POW_ON) {
		retval = rtsx_write_register(chip, REG_SD_BLOCK_CNT_H, 0xFF,
					     0x02);
		if (retval) {
			rtsx_trace(chip);
			return retval;
		}
		retval = rtsx_write_register(chip, REG_SD_BLOCK_CNT_L, 0xFF,
					     0x00);
		if (retval) {
			rtsx_trace(chip);
			return retval;
		}
	}
#endif

	return STATUS_SUCCESS;

Status_Fail:
	rtsx_trace(chip);
	return STATUS_FAIL;
}

static int mmc_test_switch_bus(struct rtsx_chip *chip, u8 width)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;
	u8 buf[8] = {0}, bus_width, *ptr;
	u16 byte_cnt;
	int len;

	retval = sd_send_cmd_get_rsp(chip, BUSTEST_W, 0, SD_RSP_TYPE_R1, NULL,
				0);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return SWITCH_FAIL;
	}

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

	retval = rtsx_write_register(chip, REG_SD_CFG3, 0x02, 0x02);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return SWITCH_ERR;
	}

	retval = sd_write_data(chip, SD_TM_AUTO_WRITE_3,
			NULL, 0, byte_cnt, 1, bus_width, buf, len, 100);
	if (retval != STATUS_SUCCESS) {
		rtsx_clear_sd_error(chip);
		rtsx_write_register(chip, REG_SD_CFG3, 0x02, 0);
		rtsx_trace(chip);
		return SWITCH_ERR;
	}

	retval = rtsx_write_register(chip, REG_SD_CFG3, 0x02, 0);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return SWITCH_ERR;
	}

	dev_dbg(rtsx_dev(chip), "SD/MMC CMD %d\n", BUSTEST_R);

	rtsx_init_cmd(chip);

	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CMD0, 0xFF, 0x40 | BUSTEST_R);

	if (width == MMC_8BIT_BUS)
		rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_BYTE_CNT_L,
			0xFF, 0x08);
	else
		rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_BYTE_CNT_L,
			0xFF, 0x04);

	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_BLOCK_CNT_L, 0xFF, 1);
	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_BLOCK_CNT_H, 0xFF, 0);

	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CFG2, 0xFF,
		SD_CALCULATE_CRC7 | SD_NO_CHECK_CRC16 | SD_NO_WAIT_BUSY_END|
		SD_CHECK_CRC7 | SD_RSP_LEN_6);
	rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_DATA_SOURCE, 0x01,
		PINGPONG_BUFFER);
	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_TRANSFER, 0xFF,
		SD_TM_NORMAL_READ | SD_TRANSFER_START);
	rtsx_add_cmd(chip, CHECK_REG_CMD, REG_SD_TRANSFER, SD_TRANSFER_END,
		SD_TRANSFER_END);

	rtsx_add_cmd(chip, READ_REG_CMD, PPBUF_BASE2, 0, 0);
	if (width == MMC_8BIT_BUS)
		rtsx_add_cmd(chip, READ_REG_CMD, PPBUF_BASE2 + 1, 0, 0);

	retval = rtsx_send_cmd(chip, SD_CARD, 100);
	if (retval < 0) {
		rtsx_clear_sd_error(chip);
		rtsx_trace(chip);
		return SWITCH_ERR;
	}

	ptr = rtsx_get_cmd_data(chip) + 1;

	if (width == MMC_8BIT_BUS) {
		dev_dbg(rtsx_dev(chip), "BUSTEST_R [8bits]: 0x%02x 0x%02x\n",
			ptr[0], ptr[1]);
		if ((ptr[0] == 0xAA) && (ptr[1] == 0x55)) {
			u8 rsp[5];
			u32 arg;

			if (CHK_MMC_DDR52(sd_card))
				arg = 0x03B70600;
			else
				arg = 0x03B70200;

			retval = sd_send_cmd_get_rsp(chip, SWITCH, arg,
						SD_RSP_TYPE_R1b, rsp, 5);
			if ((retval == STATUS_SUCCESS) &&
				!(rsp[4] & MMC_SWITCH_ERR))
				return SWITCH_SUCCESS;
		}
	} else {
		dev_dbg(rtsx_dev(chip), "BUSTEST_R [4bits]: 0x%02x\n", ptr[0]);
		if (ptr[0] == 0xA5) {
			u8 rsp[5];
			u32 arg;

			if (CHK_MMC_DDR52(sd_card))
				arg = 0x03B70500;
			else
				arg = 0x03B70100;

			retval = sd_send_cmd_get_rsp(chip, SWITCH, arg,
						SD_RSP_TYPE_R1b, rsp, 5);
			if ((retval == STATUS_SUCCESS) &&
				!(rsp[4] & MMC_SWITCH_ERR))
				return SWITCH_SUCCESS;
		}
	}

	rtsx_trace(chip);
	return SWITCH_FAIL;
}

static int mmc_switch_timing_bus(struct rtsx_chip *chip, bool switch_ddr)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;
	u8 *ptr, card_type, card_type_mask = 0;

	CLR_MMC_HS(sd_card);

	dev_dbg(rtsx_dev(chip), "SD/MMC CMD %d\n", SEND_EXT_CSD);

	rtsx_init_cmd(chip);

	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CMD0, 0xFF,
		0x40 | SEND_EXT_CSD);
	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CMD1, 0xFF, 0);
	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CMD2, 0xFF, 0);
	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CMD3, 0xFF, 0);
	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CMD4, 0xFF, 0);

	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_BYTE_CNT_L, 0xFF, 0);
	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_BYTE_CNT_H, 0xFF, 2);
	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_BLOCK_CNT_L, 0xFF, 1);
	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_BLOCK_CNT_H, 0xFF, 0);

	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CFG2, 0xFF,
		SD_CALCULATE_CRC7 | SD_CHECK_CRC16 | SD_NO_WAIT_BUSY_END|
		SD_CHECK_CRC7 | SD_RSP_LEN_6);
	rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_DATA_SOURCE, 0x01,
		PINGPONG_BUFFER);
	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_TRANSFER, 0xFF,
		SD_TM_NORMAL_READ | SD_TRANSFER_START);
	rtsx_add_cmd(chip, CHECK_REG_CMD, REG_SD_TRANSFER, SD_TRANSFER_END,
		SD_TRANSFER_END);

	rtsx_add_cmd(chip, READ_REG_CMD, PPBUF_BASE2 + 196, 0xFF, 0);
	rtsx_add_cmd(chip, READ_REG_CMD, PPBUF_BASE2 + 212, 0xFF, 0);
	rtsx_add_cmd(chip, READ_REG_CMD, PPBUF_BASE2 + 213, 0xFF, 0);
	rtsx_add_cmd(chip, READ_REG_CMD, PPBUF_BASE2 + 214, 0xFF, 0);
	rtsx_add_cmd(chip, READ_REG_CMD, PPBUF_BASE2 + 215, 0xFF, 0);

	retval = rtsx_send_cmd(chip, SD_CARD, 1000);
	if (retval < 0) {
		if (retval == -ETIMEDOUT) {
			rtsx_clear_sd_error(chip);
			sd_send_cmd_get_rsp(chip, SEND_STATUS, sd_card->sd_addr,
					SD_RSP_TYPE_R1, NULL, 0);
		}
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	ptr = rtsx_get_cmd_data(chip);
	if (ptr[0] & SD_TRANSFER_ERR) {
		sd_send_cmd_get_rsp(chip, SEND_STATUS, sd_card->sd_addr,
				SD_RSP_TYPE_R1, NULL, 0);
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	if (CHK_MMC_SECTOR_MODE(sd_card)) {
		sd_card->capacity = ((u32)ptr[5] << 24) | ((u32)ptr[4] << 16) |
			((u32)ptr[3] << 8) | ((u32)ptr[2]);
	}

	card_type_mask = 0x03;
	card_type = ptr[1] & card_type_mask;
	if (card_type) {
		u8 rsp[5];

		if (card_type & 0x04) {
			if (switch_ddr)
				SET_MMC_DDR52(sd_card);
			else
				SET_MMC_52M(sd_card);
		} else if (card_type & 0x02) {
			SET_MMC_52M(sd_card);
		} else {
			SET_MMC_26M(sd_card);
		}

		retval = sd_send_cmd_get_rsp(chip, SWITCH,
				0x03B90100, SD_RSP_TYPE_R1b, rsp, 5);
		if ((retval != STATUS_SUCCESS) || (rsp[4] & MMC_SWITCH_ERR))
			CLR_MMC_HS(sd_card);
	}

	sd_choose_proper_clock(chip);
	retval = switch_clock(chip, sd_card->sd_clock);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	/* Test Bus Procedure */
	retval = mmc_test_switch_bus(chip, MMC_8BIT_BUS);
	if (retval == SWITCH_SUCCESS) {
		SET_MMC_8BIT(sd_card);
		chip->card_bus_width[chip->card2lun[SD_CARD]] = 8;
#ifdef SUPPORT_SD_LOCK
		sd_card->sd_lock_status &= ~SD_LOCK_1BIT_MODE;
#endif
	} else if (retval == SWITCH_FAIL) {
		retval = mmc_test_switch_bus(chip, MMC_4BIT_BUS);
		if (retval == SWITCH_SUCCESS) {
			SET_MMC_4BIT(sd_card);
			chip->card_bus_width[chip->card2lun[SD_CARD]] = 4;
#ifdef SUPPORT_SD_LOCK
			sd_card->sd_lock_status &= ~SD_LOCK_1BIT_MODE;
#endif
		} else if (retval == SWITCH_FAIL) {
			CLR_MMC_8BIT(sd_card);
			CLR_MMC_4BIT(sd_card);
		} else {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}
	} else {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}

static int reset_mmc(struct rtsx_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval, i = 0, j = 0, k = 0;
	bool switch_ddr = true;
	u8 rsp[16];
	u8 spec_ver = 0;
	u32 temp;

#ifdef SUPPORT_SD_LOCK
	if (sd_card->sd_lock_status & SD_UNLOCK_POW_ON)
		goto MMC_UNLOCK_ENTRY;
#endif

Switch_Fail:
	retval = sd_prepare_reset(chip);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return retval;
	}

	SET_MMC(sd_card);

RTY_MMC_RST:
	retval = sd_send_cmd_get_rsp(chip, GO_IDLE_STATE, 0, SD_RSP_TYPE_R0,
				NULL, 0);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	do {
		if (detect_card_cd(chip, SD_CARD) != STATUS_SUCCESS) {
			sd_set_err_code(chip, SD_NO_CARD);
			rtsx_trace(chip);
			return STATUS_FAIL;
		}

		retval = sd_send_cmd_get_rsp(chip, SEND_OP_COND,
					(SUPPORT_VOLTAGE | 0x40000000),
					SD_RSP_TYPE_R3, rsp, 5);
		if (retval != STATUS_SUCCESS) {
			if (sd_check_err_code(chip, SD_BUSY) ||
				sd_check_err_code(chip, SD_TO_ERR)) {
				k++;
				if (k < 20) {
					sd_clr_err_code(chip);
					goto RTY_MMC_RST;
				} else {
					rtsx_trace(chip);
					return STATUS_FAIL;
				}
			} else {
				j++;
				if (j < 100) {
					sd_clr_err_code(chip);
					goto RTY_MMC_RST;
				} else {
					rtsx_trace(chip);
					return STATUS_FAIL;
				}
			}
		}

		wait_timeout(20);
		i++;
	} while (!(rsp[1] & 0x80) && (i < 255));

	if (i == 255) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	if ((rsp[1] & 0x60) == 0x40)
		SET_MMC_SECTOR_MODE(sd_card);
	else
		CLR_MMC_SECTOR_MODE(sd_card);

	retval = sd_send_cmd_get_rsp(chip, ALL_SEND_CID, 0, SD_RSP_TYPE_R2,
				NULL, 0);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	sd_card->sd_addr = 0x00100000;
	retval = sd_send_cmd_get_rsp(chip, SET_RELATIVE_ADDR, sd_card->sd_addr,
				SD_RSP_TYPE_R6, rsp, 5);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	retval = sd_check_csd(chip, 1);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	spec_ver = (sd_card->raw_csd[0] & 0x3C) >> 2;

	retval = sd_select_card(chip, 1);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	retval = sd_send_cmd_get_rsp(chip, SET_BLOCKLEN, 0x200, SD_RSP_TYPE_R1,
				NULL, 0);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

#ifdef SUPPORT_SD_LOCK
MMC_UNLOCK_ENTRY:
	retval = sd_update_lock_status(chip);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}
#endif

	retval = sd_set_clock_divider(chip, SD_CLK_DIVIDE_0);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	chip->card_bus_width[chip->card2lun[SD_CARD]] = 1;

	if (!sd_card->mmc_dont_switch_bus) {
		if (spec_ver == 4) {
			/* MMC 4.x Cards */
			retval = mmc_switch_timing_bus(chip, switch_ddr);
			if (retval != STATUS_SUCCESS) {
				retval = sd_init_power(chip);
				if (retval != STATUS_SUCCESS) {
					rtsx_trace(chip);
					return STATUS_FAIL;
				}
				sd_card->mmc_dont_switch_bus = 1;
				rtsx_trace(chip);
				goto Switch_Fail;
			}
		}

		if (CHK_MMC_SECTOR_MODE(sd_card) && (sd_card->capacity == 0)) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}

		if (switch_ddr && CHK_MMC_DDR52(sd_card)) {
			retval = sd_set_init_para(chip);
			if (retval != STATUS_SUCCESS) {
				rtsx_trace(chip);
				return STATUS_FAIL;
			}

			retval = mmc_ddr_tuning(chip);
			if (retval != STATUS_SUCCESS) {
				retval = sd_init_power(chip);
				if (retval != STATUS_SUCCESS) {
					rtsx_trace(chip);
					return STATUS_FAIL;
				}

				switch_ddr = false;
				rtsx_trace(chip);
				goto Switch_Fail;
			}

			retval = sd_wait_state_data_ready(chip, 0x08, 1, 1000);
			if (retval == STATUS_SUCCESS) {
				retval = sd_read_lba0(chip);
				if (retval != STATUS_SUCCESS) {
					retval = sd_init_power(chip);
					if (retval != STATUS_SUCCESS) {
						rtsx_trace(chip);
						return STATUS_FAIL;
					}

					switch_ddr = false;
					rtsx_trace(chip);
					goto Switch_Fail;
				}
			}
		}
	}

#ifdef SUPPORT_SD_LOCK
	if (sd_card->sd_lock_status & SD_UNLOCK_POW_ON) {
		retval = rtsx_write_register(chip, REG_SD_BLOCK_CNT_H, 0xFF,
					     0x02);
		if (retval) {
			rtsx_trace(chip);
			return retval;
		}
		retval = rtsx_write_register(chip, REG_SD_BLOCK_CNT_L, 0xFF,
					     0x00);
		if (retval) {
			rtsx_trace(chip);
			return retval;
		}
	}
#endif

	temp = rtsx_readl(chip, RTSX_BIPR);
	if (temp & SD_WRITE_PROTECT)
		chip->card_wp |= SD_CARD;

	return STATUS_SUCCESS;
}

int reset_sd_card(struct rtsx_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;

	sd_init_reg_addr(chip);

	memset(sd_card, 0, sizeof(struct sd_info));
	chip->capacity[chip->card2lun[SD_CARD]] = 0;

	retval = enable_card_clock(chip, SD_CARD);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	if (chip->ignore_sd && CHK_SDIO_EXIST(chip) &&
		!CHK_SDIO_IGNORED(chip)) {
		if (chip->asic_code) {
			retval = sd_pull_ctl_enable(chip);
			if (retval != STATUS_SUCCESS) {
				rtsx_trace(chip);
				return STATUS_FAIL;
			}
		} else {
			retval = rtsx_write_register(chip, FPGA_PULL_CTL,
						FPGA_SD_PULL_CTL_BIT | 0x20, 0);
			if (retval != STATUS_SUCCESS) {
				rtsx_trace(chip);
				return STATUS_FAIL;
			}
		}
		retval = card_share_mode(chip, SD_CARD);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}

		chip->sd_io = 1;
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	retval = sd_init_power(chip);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	if (chip->sd_ctl & RESET_MMC_FIRST) {
		retval = reset_mmc(chip);
		if (retval != STATUS_SUCCESS) {
			if (sd_check_err_code(chip, SD_NO_CARD)) {
				rtsx_trace(chip);
				return STATUS_FAIL;
			}

			retval = reset_sd(chip);
			if (retval != STATUS_SUCCESS) {
				rtsx_trace(chip);
				return STATUS_FAIL;
			}
		}
	} else {
		retval = reset_sd(chip);
		if (retval != STATUS_SUCCESS) {
			if (sd_check_err_code(chip, SD_NO_CARD)) {
				rtsx_trace(chip);
				return STATUS_FAIL;
			}

			if (chip->sd_io) {
				rtsx_trace(chip);
				return STATUS_FAIL;
			}
			retval = reset_mmc(chip);
			if (retval != STATUS_SUCCESS) {
				rtsx_trace(chip);
				return STATUS_FAIL;
			}
		}
	}

	retval = sd_set_clock_divider(chip, SD_CLK_DIVIDE_0);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	retval = rtsx_write_register(chip, REG_SD_BYTE_CNT_L, 0xFF, 0);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}
	retval = rtsx_write_register(chip, REG_SD_BYTE_CNT_H, 0xFF, 2);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}

	chip->capacity[chip->card2lun[SD_CARD]] = sd_card->capacity;

	retval = sd_set_init_para(chip);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	dev_dbg(rtsx_dev(chip), "sd_card->sd_type = 0x%x\n", sd_card->sd_type);

	return STATUS_SUCCESS;
}

static int reset_mmc_only(struct rtsx_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;

	sd_card->sd_type = 0;
	sd_card->seq_mode = 0;
	sd_card->sd_data_buf_ready = 0;
	sd_card->capacity = 0;
	sd_card->sd_switch_fail = 0;

#ifdef SUPPORT_SD_LOCK
	sd_card->sd_lock_status = 0;
	sd_card->sd_erase_status = 0;
#endif

	chip->capacity[chip->card2lun[SD_CARD]] = sd_card->capacity = 0;

	retval = enable_card_clock(chip, SD_CARD);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	retval = sd_init_power(chip);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	retval = reset_mmc(chip);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	retval = sd_set_clock_divider(chip, SD_CLK_DIVIDE_0);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	retval = rtsx_write_register(chip, REG_SD_BYTE_CNT_L, 0xFF, 0);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}
	retval = rtsx_write_register(chip, REG_SD_BYTE_CNT_H, 0xFF, 2);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}

	chip->capacity[chip->card2lun[SD_CARD]] = sd_card->capacity;

	retval = sd_set_init_para(chip);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	dev_dbg(rtsx_dev(chip), "In reset_mmc_only, sd_card->sd_type = 0x%x\n",
		sd_card->sd_type);

	return STATUS_SUCCESS;
}

#define WAIT_DATA_READY_RTY_CNT		255

static int wait_data_buf_ready(struct rtsx_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int i, retval;

	for (i = 0; i < WAIT_DATA_READY_RTY_CNT; i++) {
		if (detect_card_cd(chip, SD_CARD) != STATUS_SUCCESS) {
			sd_set_err_code(chip, SD_NO_CARD);
			rtsx_trace(chip);
			return STATUS_FAIL;
		}

		sd_card->sd_data_buf_ready = 0;

		retval = sd_send_cmd_get_rsp(chip, SEND_STATUS,
				sd_card->sd_addr, SD_RSP_TYPE_R1, NULL, 0);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}

		if (sd_card->sd_data_buf_ready) {
			return sd_send_cmd_get_rsp(chip, SEND_STATUS,
				sd_card->sd_addr, SD_RSP_TYPE_R1, NULL, 0);
		}
	}

	sd_set_err_code(chip, SD_TO_ERR);

	rtsx_trace(chip);
	return STATUS_FAIL;
}

void sd_stop_seq_mode(struct rtsx_chip *chip)
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

		retval = sd_wait_state_data_ready(chip, 0x08, 1, 1000);
		if (retval != STATUS_SUCCESS)
			sd_set_err_code(chip, SD_STS_ERR);

		sd_card->seq_mode = 0;

		rtsx_write_register(chip, RBCTL, RB_FLUSH, RB_FLUSH);
	}
}

static inline int sd_auto_tune_clock(struct rtsx_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;

	if (chip->asic_code) {
		if (sd_card->sd_clock > 30)
			sd_card->sd_clock -= 20;
	} else {
		switch (sd_card->sd_clock) {
		case CLK_200:
			sd_card->sd_clock = CLK_150;
			break;

		case CLK_150:
			sd_card->sd_clock = CLK_120;
			break;

		case CLK_120:
			sd_card->sd_clock = CLK_100;
			break;

		case CLK_100:
			sd_card->sd_clock = CLK_80;
			break;

		case CLK_80:
			sd_card->sd_clock = CLK_60;
			break;

		case CLK_60:
			sd_card->sd_clock = CLK_50;
			break;

		default:
			break;
		}
	}

	retval = sd_switch_clock(chip);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}

int sd_rw(struct scsi_cmnd *srb, struct rtsx_chip *chip, u32 start_sector,
	u16 sector_cnt)
{
	struct sd_info *sd_card = &(chip->sd_card);
	u32 data_addr;
	u8 cfg2;
	int retval;

	if (srb->sc_data_direction == DMA_FROM_DEVICE) {
		dev_dbg(rtsx_dev(chip), "sd_rw: Read %d %s from 0x%x\n",
			sector_cnt, (sector_cnt > 1) ? "sectors" : "sector",
			start_sector);
	} else {
		dev_dbg(rtsx_dev(chip), "sd_rw: Write %d %s to 0x%x\n",
			sector_cnt, (sector_cnt > 1) ? "sectors" : "sector",
			start_sector);
	}

	sd_card->cleanup_counter = 0;

	if (!(chip->card_ready & SD_CARD)) {
		sd_card->seq_mode = 0;

		retval = reset_sd_card(chip);
		if (retval == STATUS_SUCCESS) {
			chip->card_ready |= SD_CARD;
			chip->card_fail &= ~SD_CARD;
		} else {
			chip->card_ready &= ~SD_CARD;
			chip->card_fail |= SD_CARD;
			chip->capacity[chip->card2lun[SD_CARD]] = 0;
			chip->rw_need_retry = 1;
			rtsx_trace(chip);
			return STATUS_FAIL;
		}
	}

	if (!CHK_SD_HCXC(sd_card) && !CHK_MMC_SECTOR_MODE(sd_card))
		data_addr = start_sector << 9;
	else
		data_addr = start_sector;

	sd_clr_err_code(chip);

	retval = sd_switch_clock(chip);
	if (retval != STATUS_SUCCESS) {
		sd_set_err_code(chip, SD_IO_ERR);
		rtsx_trace(chip);
		goto RW_FAIL;
	}

	if (sd_card->seq_mode &&
		((sd_card->pre_dir != srb->sc_data_direction) ||
			((sd_card->pre_sec_addr + sd_card->pre_sec_cnt) !=
				start_sector))) {
		if ((sd_card->pre_sec_cnt < 0x80)
				&& (sd_card->pre_dir == DMA_FROM_DEVICE)
				&& !CHK_SD30_SPEED(sd_card)
				&& !CHK_SD_HS(sd_card)
				&& !CHK_MMC_HS(sd_card)) {
			sd_send_cmd_get_rsp(chip, SEND_STATUS, sd_card->sd_addr,
					SD_RSP_TYPE_R1, NULL, 0);
		}

		retval = sd_send_cmd_get_rsp(chip, STOP_TRANSMISSION,
				0, SD_RSP_TYPE_R1b, NULL, 0);
		if (retval != STATUS_SUCCESS) {
			chip->rw_need_retry = 1;
			sd_set_err_code(chip, SD_STS_ERR);
			rtsx_trace(chip);
			goto RW_FAIL;
		}

		sd_card->seq_mode = 0;

		retval = rtsx_write_register(chip, RBCTL, RB_FLUSH, RB_FLUSH);
		if (retval != STATUS_SUCCESS) {
			sd_set_err_code(chip, SD_IO_ERR);
			rtsx_trace(chip);
			goto RW_FAIL;
		}

		if ((sd_card->pre_sec_cnt < 0x80)
				&& !CHK_SD30_SPEED(sd_card)
				&& !CHK_SD_HS(sd_card)
				&& !CHK_MMC_HS(sd_card)) {
			sd_send_cmd_get_rsp(chip, SEND_STATUS, sd_card->sd_addr,
					SD_RSP_TYPE_R1, NULL, 0);
		}
	}

	rtsx_init_cmd(chip);

	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_BYTE_CNT_L, 0xFF, 0x00);
	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_BYTE_CNT_H, 0xFF, 0x02);
	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_BLOCK_CNT_L, 0xFF,
		(u8)sector_cnt);
	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_BLOCK_CNT_H, 0xFF,
		(u8)(sector_cnt >> 8));

	rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_DATA_SOURCE, 0x01, RING_BUFFER);

	if (CHK_MMC_8BIT(sd_card))
		rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CFG1,
			0x03, SD_BUS_WIDTH_8);
	else if (CHK_MMC_4BIT(sd_card) || CHK_SD(sd_card))
		rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CFG1,
			0x03, SD_BUS_WIDTH_4);
	else
		rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CFG1,
			0x03, SD_BUS_WIDTH_1);

	if (sd_card->seq_mode) {
		cfg2 = SD_NO_CALCULATE_CRC7 | SD_CHECK_CRC16|
			SD_NO_WAIT_BUSY_END | SD_NO_CHECK_CRC7 |
			SD_RSP_LEN_0;
		rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CFG2, 0xFF, cfg2);

		trans_dma_enable(srb->sc_data_direction, chip, sector_cnt * 512,
				DMA_512);

		if (srb->sc_data_direction == DMA_FROM_DEVICE) {
			rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_TRANSFER, 0xFF,
				     SD_TM_AUTO_READ_3 | SD_TRANSFER_START);
		} else {
			rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_TRANSFER, 0xFF,
				     SD_TM_AUTO_WRITE_3 | SD_TRANSFER_START);
		}

		rtsx_add_cmd(chip, CHECK_REG_CMD, REG_SD_TRANSFER,
			SD_TRANSFER_END, SD_TRANSFER_END);

		rtsx_send_cmd_no_wait(chip);
	} else {
		if (srb->sc_data_direction == DMA_FROM_DEVICE) {
			dev_dbg(rtsx_dev(chip), "SD/MMC CMD %d\n",
				READ_MULTIPLE_BLOCK);
			rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CMD0, 0xFF,
				     0x40 | READ_MULTIPLE_BLOCK);
			rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CMD1, 0xFF,
				(u8)(data_addr >> 24));
			rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CMD2, 0xFF,
				(u8)(data_addr >> 16));
			rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CMD3, 0xFF,
				(u8)(data_addr >> 8));
			rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CMD4, 0xFF,
				(u8)data_addr);

			cfg2 = SD_CALCULATE_CRC7 | SD_CHECK_CRC16 |
				SD_NO_WAIT_BUSY_END | SD_CHECK_CRC7 |
				SD_RSP_LEN_6;
			rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CFG2, 0xFF,
				cfg2);

			trans_dma_enable(srb->sc_data_direction, chip,
					sector_cnt * 512, DMA_512);

			rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_TRANSFER, 0xFF,
				     SD_TM_AUTO_READ_2 | SD_TRANSFER_START);
			rtsx_add_cmd(chip, CHECK_REG_CMD, REG_SD_TRANSFER,
				     SD_TRANSFER_END, SD_TRANSFER_END);

			rtsx_send_cmd_no_wait(chip);
		} else {
			retval = rtsx_send_cmd(chip, SD_CARD, 50);
			if (retval < 0) {
				rtsx_clear_sd_error(chip);

				chip->rw_need_retry = 1;
				sd_set_err_code(chip, SD_TO_ERR);
				rtsx_trace(chip);
				goto RW_FAIL;
			}

			retval = wait_data_buf_ready(chip);
			if (retval != STATUS_SUCCESS) {
				chip->rw_need_retry = 1;
				sd_set_err_code(chip, SD_TO_ERR);
				rtsx_trace(chip);
				goto RW_FAIL;
			}

			retval = sd_send_cmd_get_rsp(chip, WRITE_MULTIPLE_BLOCK,
					data_addr, SD_RSP_TYPE_R1, NULL, 0);
			if (retval != STATUS_SUCCESS) {
				chip->rw_need_retry = 1;
				rtsx_trace(chip);
				goto RW_FAIL;
			}

			rtsx_init_cmd(chip);

			cfg2 = SD_NO_CALCULATE_CRC7 | SD_CHECK_CRC16 |
				SD_NO_WAIT_BUSY_END |
				SD_NO_CHECK_CRC7 | SD_RSP_LEN_0;
			rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CFG2, 0xFF,
				cfg2);

			trans_dma_enable(srb->sc_data_direction, chip,
					sector_cnt * 512, DMA_512);

			rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_TRANSFER, 0xFF,
				     SD_TM_AUTO_WRITE_3 | SD_TRANSFER_START);
			rtsx_add_cmd(chip, CHECK_REG_CMD, REG_SD_TRANSFER,
				     SD_TRANSFER_END, SD_TRANSFER_END);

			rtsx_send_cmd_no_wait(chip);
		}

		sd_card->seq_mode = 1;
	}

	retval = rtsx_transfer_data(chip, SD_CARD, scsi_sglist(srb),
				scsi_bufflen(srb), scsi_sg_count(srb),
				srb->sc_data_direction, chip->sd_timeout);
	if (retval < 0) {
		u8 stat = 0;
		int err;

		sd_card->seq_mode = 0;

		if (retval == -ETIMEDOUT)
			err = STATUS_TIMEDOUT;
		else
			err = STATUS_FAIL;

		rtsx_read_register(chip, REG_SD_STAT1, &stat);
		rtsx_clear_sd_error(chip);
		if (detect_card_cd(chip, SD_CARD) != STATUS_SUCCESS) {
			chip->rw_need_retry = 0;
			dev_dbg(rtsx_dev(chip), "No card exist, exit sd_rw\n");
			rtsx_trace(chip);
			return STATUS_FAIL;
		}

		chip->rw_need_retry = 1;

		retval = sd_send_cmd_get_rsp(chip, STOP_TRANSMISSION, 0,
					SD_RSP_TYPE_R1b, NULL, 0);
		if (retval != STATUS_SUCCESS) {
			sd_set_err_code(chip, SD_STS_ERR);
			rtsx_trace(chip);
			goto RW_FAIL;
		}

		if (stat & (SD_CRC7_ERR | SD_CRC16_ERR | SD_CRC_WRITE_ERR)) {
			dev_dbg(rtsx_dev(chip), "SD CRC error, tune clock!\n");
			sd_set_err_code(chip, SD_CRC_ERR);
			rtsx_trace(chip);
			goto RW_FAIL;
		}

		if (err == STATUS_TIMEDOUT) {
			sd_set_err_code(chip, SD_TO_ERR);
			rtsx_trace(chip);
			goto RW_FAIL;
		}

		rtsx_trace(chip);
		return err;
	}

	sd_card->pre_sec_addr = start_sector;
	sd_card->pre_sec_cnt = sector_cnt;
	sd_card->pre_dir = srb->sc_data_direction;

	return STATUS_SUCCESS;

RW_FAIL:
	sd_card->seq_mode = 0;

	if (detect_card_cd(chip, SD_CARD) != STATUS_SUCCESS) {
		chip->rw_need_retry = 0;
		dev_dbg(rtsx_dev(chip), "No card exist, exit sd_rw\n");
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	if (sd_check_err_code(chip, SD_CRC_ERR)) {
		if (CHK_MMC_4BIT(sd_card) || CHK_MMC_8BIT(sd_card)) {
			sd_card->mmc_dont_switch_bus = 1;
			reset_mmc_only(chip);
			sd_card->mmc_dont_switch_bus = 0;
		} else {
			sd_card->need_retune = 1;
			sd_auto_tune_clock(chip);
		}
	} else if (sd_check_err_code(chip, SD_TO_ERR | SD_STS_ERR)) {
		retval = reset_sd_card(chip);
		if (retval != STATUS_SUCCESS) {
			chip->card_ready &= ~SD_CARD;
			chip->card_fail |= SD_CARD;
			chip->capacity[chip->card2lun[SD_CARD]] = 0;
		}
	}

	rtsx_trace(chip);
	return STATUS_FAIL;
}

#ifdef SUPPORT_CPRM
int soft_reset_sd_card(struct rtsx_chip *chip)
{
	return reset_sd(chip);
}

int ext_sd_send_cmd_get_rsp(struct rtsx_chip *chip, u8 cmd_idx,
		u32 arg, u8 rsp_type, u8 *rsp, int rsp_len, bool special_check)
{
	int retval;
	int timeout = 100;
	u16 reg_addr;
	u8 *ptr;
	int stat_idx = 0;
	int rty_cnt = 0;

	dev_dbg(rtsx_dev(chip), "EXT SD/MMC CMD %d\n", cmd_idx);

	if (rsp_type == SD_RSP_TYPE_R1b)
		timeout = 3000;

RTY_SEND_CMD:

	rtsx_init_cmd(chip);

	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CMD0, 0xFF, 0x40 | cmd_idx);
	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CMD1, 0xFF, (u8)(arg >> 24));
	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CMD2, 0xFF, (u8)(arg >> 16));
	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CMD3, 0xFF, (u8)(arg >> 8));
	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CMD4, 0xFF, (u8)arg);

	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CFG2, 0xFF, rsp_type);
	rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_DATA_SOURCE,
			0x01, PINGPONG_BUFFER);
	rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_TRANSFER,
			0xFF, SD_TM_CMD_RSP | SD_TRANSFER_START);
	rtsx_add_cmd(chip, CHECK_REG_CMD, REG_SD_TRANSFER, SD_TRANSFER_END,
		SD_TRANSFER_END);

	if (rsp_type == SD_RSP_TYPE_R2) {
		for (reg_addr = PPBUF_BASE2; reg_addr < PPBUF_BASE2 + 16;
		     reg_addr++)
			rtsx_add_cmd(chip, READ_REG_CMD, reg_addr, 0, 0);

		stat_idx = 17;
	} else if (rsp_type != SD_RSP_TYPE_R0) {
		for (reg_addr = REG_SD_CMD0; reg_addr <= REG_SD_CMD4;
		     reg_addr++)
			rtsx_add_cmd(chip, READ_REG_CMD, reg_addr, 0, 0);

		stat_idx = 6;
	}
	rtsx_add_cmd(chip, READ_REG_CMD, REG_SD_CMD5, 0, 0);

	rtsx_add_cmd(chip, READ_REG_CMD, REG_SD_STAT1, 0, 0);

	retval = rtsx_send_cmd(chip, SD_CARD, timeout);
	if (retval < 0) {
		if (retval == -ETIMEDOUT) {
			rtsx_clear_sd_error(chip);

			if (rsp_type & SD_WAIT_BUSY_END) {
				retval = sd_check_data0_status(chip);
				if (retval != STATUS_SUCCESS) {
					rtsx_trace(chip);
					return retval;
				}
			} else {
				sd_set_err_code(chip, SD_TO_ERR);
			}
		}
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	if (rsp_type == SD_RSP_TYPE_R0)
		return STATUS_SUCCESS;

	ptr = rtsx_get_cmd_data(chip) + 1;

	if ((ptr[0] & 0xC0) != 0) {
		sd_set_err_code(chip, SD_STS_ERR);
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	if (!(rsp_type & SD_NO_CHECK_CRC7)) {
		if (ptr[stat_idx] & SD_CRC7_ERR) {
			if (cmd_idx == WRITE_MULTIPLE_BLOCK) {
				sd_set_err_code(chip, SD_CRC_ERR);
				rtsx_trace(chip);
				return STATUS_FAIL;
			}
			if (rty_cnt < SD_MAX_RETRY_COUNT) {
				wait_timeout(20);
				rty_cnt++;
				goto RTY_SEND_CMD;
			} else {
				sd_set_err_code(chip, SD_CRC_ERR);
				rtsx_trace(chip);
				return STATUS_FAIL;
			}
		}
	}

	if ((cmd_idx == SELECT_CARD) || (cmd_idx == APP_CMD) ||
		(cmd_idx == SEND_STATUS) || (cmd_idx == STOP_TRANSMISSION)) {
		if ((cmd_idx != STOP_TRANSMISSION) && !special_check) {
			if (ptr[1] & 0x80) {
				rtsx_trace(chip);
				return STATUS_FAIL;
			}
		}
#ifdef SUPPORT_SD_LOCK
		if (ptr[1] & 0x7D) {
#else
		if (ptr[1] & 0x7F) {
#endif
			rtsx_trace(chip);
			return STATUS_FAIL;
		}
		if (ptr[2] & 0xF8) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}

		if (cmd_idx == SELECT_CARD) {
			if (rsp_type == SD_RSP_TYPE_R2) {
				if ((ptr[3] & 0x1E) != 0x04) {
					rtsx_trace(chip);
					return STATUS_FAIL;
				}

			} else if (rsp_type == SD_RSP_TYPE_R0) {
				if ((ptr[3] & 0x1E) != 0x03) {
					rtsx_trace(chip);
					return STATUS_FAIL;
				}
			}
		}
	}

	if (rsp && rsp_len)
		memcpy(rsp, ptr, rsp_len);

	return STATUS_SUCCESS;
}

int ext_sd_get_rsp(struct rtsx_chip *chip, int len, u8 *rsp, u8 rsp_type)
{
	int retval, rsp_len;
	u16 reg_addr;

	if (rsp_type == SD_RSP_TYPE_R0)
		return STATUS_SUCCESS;

	rtsx_init_cmd(chip);

	if (rsp_type == SD_RSP_TYPE_R2) {
		for (reg_addr = PPBUF_BASE2; reg_addr < PPBUF_BASE2 + 16;
		     reg_addr++)
			rtsx_add_cmd(chip, READ_REG_CMD, reg_addr, 0xFF, 0);

		rsp_len = 17;
	} else if (rsp_type != SD_RSP_TYPE_R0) {
		for (reg_addr = REG_SD_CMD0; reg_addr <= REG_SD_CMD4;
		     reg_addr++)
			rtsx_add_cmd(chip, READ_REG_CMD, reg_addr, 0xFF, 0);

		rsp_len = 6;
	}
	rtsx_add_cmd(chip, READ_REG_CMD, REG_SD_CMD5, 0xFF, 0);

	retval = rtsx_send_cmd(chip, SD_CARD, 100);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	if (rsp) {
		int min_len = (rsp_len < len) ? rsp_len : len;

		memcpy(rsp, rtsx_get_cmd_data(chip), min_len);

		dev_dbg(rtsx_dev(chip), "min_len = %d\n", min_len);
		dev_dbg(rtsx_dev(chip), "Response in cmd buf: 0x%x 0x%x 0x%x 0x%x\n",
			rsp[0], rsp[1], rsp[2], rsp[3]);
	}

	return STATUS_SUCCESS;
}

int sd_pass_thru_mode(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	unsigned int lun = SCSI_LUN(srb);
	int len;
	u8 buf[18] = {
		0x00,
		0x00,
		0x00,
		0x0E,
		0x00,
		0x00,
		0x00,
		0x00,
		0x53,
		0x44,
		0x20,
		0x43,
		0x61,
		0x72,
		0x64,
		0x00,
		0x00,
		0x00,
	};

	sd_card->pre_cmd_err = 0;

	if (!(CHK_BIT(chip->lun_mc, lun))) {
		SET_BIT(chip->lun_mc, lun);
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_CHANGE);
		rtsx_trace(chip);
		return TRANSPORT_FAILED;
	}

	if ((srb->cmnd[2] != 0x53) || (srb->cmnd[3] != 0x44) ||
		(srb->cmnd[4] != 0x20) || (srb->cmnd[5] != 0x43) ||
		(srb->cmnd[6] != 0x61) || (srb->cmnd[7] != 0x72) ||
		(srb->cmnd[8] != 0x64)) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		rtsx_trace(chip);
		return TRANSPORT_FAILED;
	}

	switch (srb->cmnd[1] & 0x0F) {
	case 0:
		sd_card->sd_pass_thru_en = 0;
		break;

	case 1:
		sd_card->sd_pass_thru_en = 1;
		break;

	default:
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		rtsx_trace(chip);
		return TRANSPORT_FAILED;
	}

	buf[5] = (CHK_SD(sd_card) == 1) ? 0x01 : 0x02;
	if (chip->card_wp & SD_CARD)
		buf[5] |= 0x80;

	buf[6] = (u8)(sd_card->sd_addr >> 16);
	buf[7] = (u8)(sd_card->sd_addr >> 24);

	buf[15] = chip->max_lun;

	len = min_t(int, 18, scsi_bufflen(srb));
	rtsx_stor_set_xfer_buf(buf, len, srb);

	return TRANSPORT_GOOD;
}

static inline int get_rsp_type(struct scsi_cmnd *srb, u8 *rsp_type,
			int *rsp_len)
{
	if (!rsp_type || !rsp_len)
		return STATUS_FAIL;

	switch (srb->cmnd[10]) {
	case 0x03:
		*rsp_type = SD_RSP_TYPE_R0;
		*rsp_len = 0;
		break;

	case 0x04:
		*rsp_type = SD_RSP_TYPE_R1;
		*rsp_len = 6;
		break;

	case 0x05:
		*rsp_type = SD_RSP_TYPE_R1b;
		*rsp_len = 6;
		break;

	case 0x06:
		*rsp_type = SD_RSP_TYPE_R2;
		*rsp_len = 17;
		break;

	case 0x07:
		*rsp_type = SD_RSP_TYPE_R3;
		*rsp_len = 6;
		break;

	default:
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}

int sd_execute_no_data(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	unsigned int lun = SCSI_LUN(srb);
	int retval, rsp_len;
	u8 cmd_idx, rsp_type;
	bool standby = false, acmd = false;
	u32 arg;

	if (!sd_card->sd_pass_thru_en) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		rtsx_trace(chip);
		return TRANSPORT_FAILED;
	}

	retval = sd_switch_clock(chip);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return TRANSPORT_FAILED;
	}

	if (sd_card->pre_cmd_err) {
		sd_card->pre_cmd_err = 0;
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_CHANGE);
		rtsx_trace(chip);
		return TRANSPORT_FAILED;
	}

	cmd_idx = srb->cmnd[2] & 0x3F;
	if (srb->cmnd[1] & 0x02)
		standby = true;

	if (srb->cmnd[1] & 0x01)
		acmd = true;

	arg = ((u32)srb->cmnd[3] << 24) | ((u32)srb->cmnd[4] << 16) |
		((u32)srb->cmnd[5] << 8) | srb->cmnd[6];

	retval = get_rsp_type(srb, &rsp_type, &rsp_len);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		rtsx_trace(chip);
		return TRANSPORT_FAILED;
	}
	sd_card->last_rsp_type = rsp_type;

	retval = sd_switch_clock(chip);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return TRANSPORT_FAILED;
	}

#ifdef SUPPORT_SD_LOCK
	if ((sd_card->sd_lock_status & SD_LOCK_1BIT_MODE) == 0) {
		if (CHK_MMC_8BIT(sd_card)) {
			retval = rtsx_write_register(chip, REG_SD_CFG1, 0x03,
						SD_BUS_WIDTH_8);
			if (retval != STATUS_SUCCESS) {
				rtsx_trace(chip);
				return TRANSPORT_FAILED;
			}

		} else if (CHK_SD(sd_card) || CHK_MMC_4BIT(sd_card)) {
			retval = rtsx_write_register(chip, REG_SD_CFG1, 0x03,
						SD_BUS_WIDTH_4);
			if (retval != STATUS_SUCCESS) {
				rtsx_trace(chip);
				return TRANSPORT_FAILED;
			}
		}
	}
#else
	retval = rtsx_write_register(chip, REG_SD_CFG1, 0x03, SD_BUS_WIDTH_4);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return TRANSPORT_FAILED;
	}
#endif

	if (standby) {
		retval = sd_select_card(chip, 0);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			goto SD_Execute_Cmd_Failed;
		}
	}

	if (acmd) {
		retval = ext_sd_send_cmd_get_rsp(chip, APP_CMD,
						sd_card->sd_addr,
						SD_RSP_TYPE_R1, NULL, 0, false);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			goto SD_Execute_Cmd_Failed;
		}
	}

	retval = ext_sd_send_cmd_get_rsp(chip, cmd_idx, arg, rsp_type,
			sd_card->rsp, rsp_len, false);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		goto SD_Execute_Cmd_Failed;
	}

	if (standby) {
		retval = sd_select_card(chip, 1);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			goto SD_Execute_Cmd_Failed;
		}
	}

#ifdef SUPPORT_SD_LOCK
	retval = sd_update_lock_status(chip);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		goto SD_Execute_Cmd_Failed;
	}
#endif

	scsi_set_resid(srb, 0);
	return TRANSPORT_GOOD;

SD_Execute_Cmd_Failed:
	sd_card->pre_cmd_err = 1;
	set_sense_type(chip, lun, SENSE_TYPE_NO_SENSE);
	release_sd_card(chip);
	do_reset_sd_card(chip);
	if (!(chip->card_ready & SD_CARD))
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_NOT_PRESENT);

	rtsx_trace(chip);
	return TRANSPORT_FAILED;
}

int sd_execute_read_data(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	unsigned int lun = SCSI_LUN(srb);
	int retval, rsp_len, i;
	bool read_err = false, cmd13_checkbit = false;
	u8 cmd_idx, rsp_type, bus_width;
	bool standby = false, send_cmd12 = false, acmd = false;
	u32 data_len;

	if (!sd_card->sd_pass_thru_en) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		rtsx_trace(chip);
		return TRANSPORT_FAILED;
	}

	if (sd_card->pre_cmd_err) {
		sd_card->pre_cmd_err = 0;
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_CHANGE);
		rtsx_trace(chip);
		return TRANSPORT_FAILED;
	}

	retval = sd_switch_clock(chip);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return TRANSPORT_FAILED;
	}

	cmd_idx = srb->cmnd[2] & 0x3F;
	if (srb->cmnd[1] & 0x04)
		send_cmd12 = true;

	if (srb->cmnd[1] & 0x02)
		standby = true;

	if (srb->cmnd[1] & 0x01)
		acmd = true;

	data_len = ((u32)srb->cmnd[7] << 16) | ((u32)srb->cmnd[8]
						<< 8) | srb->cmnd[9];

	retval = get_rsp_type(srb, &rsp_type, &rsp_len);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		rtsx_trace(chip);
		return TRANSPORT_FAILED;
	}
	sd_card->last_rsp_type = rsp_type;

	retval = sd_switch_clock(chip);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return TRANSPORT_FAILED;
	}

#ifdef SUPPORT_SD_LOCK
	if ((sd_card->sd_lock_status & SD_LOCK_1BIT_MODE) == 0) {
		if (CHK_MMC_8BIT(sd_card))
			bus_width = SD_BUS_WIDTH_8;
		else if (CHK_SD(sd_card) || CHK_MMC_4BIT(sd_card))
			bus_width = SD_BUS_WIDTH_4;
		else
			bus_width = SD_BUS_WIDTH_1;
	} else {
		bus_width = SD_BUS_WIDTH_4;
	}
	dev_dbg(rtsx_dev(chip), "bus_width = %d\n", bus_width);
#else
	bus_width = SD_BUS_WIDTH_4;
#endif

	if (data_len < 512) {
		retval = ext_sd_send_cmd_get_rsp(chip, SET_BLOCKLEN, data_len,
				SD_RSP_TYPE_R1, NULL, 0, false);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			goto SD_Execute_Read_Cmd_Failed;
		}
	}

	if (standby) {
		retval = sd_select_card(chip, 0);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			goto SD_Execute_Read_Cmd_Failed;
		}
	}

	if (acmd) {
		retval = ext_sd_send_cmd_get_rsp(chip, APP_CMD,
						sd_card->sd_addr,
						SD_RSP_TYPE_R1, NULL, 0, false);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			goto SD_Execute_Read_Cmd_Failed;
		}
	}

	if (data_len <= 512) {
		int min_len;
		u8 *buf;
		u16 byte_cnt, blk_cnt;
		u8 cmd[5];

		byte_cnt = ((u16)(srb->cmnd[8] & 0x03) << 8) | srb->cmnd[9];
		blk_cnt = 1;

		cmd[0] = 0x40 | cmd_idx;
		cmd[1] = srb->cmnd[3];
		cmd[2] = srb->cmnd[4];
		cmd[3] = srb->cmnd[5];
		cmd[4] = srb->cmnd[6];

		buf = kmalloc(data_len, GFP_KERNEL);
		if (!buf) {
			rtsx_trace(chip);
			return TRANSPORT_ERROR;
		}

		retval = sd_read_data(chip, SD_TM_NORMAL_READ, cmd, 5, byte_cnt,
				       blk_cnt, bus_width, buf, data_len, 2000);
		if (retval != STATUS_SUCCESS) {
			read_err = true;
			kfree(buf);
			rtsx_clear_sd_error(chip);
			rtsx_trace(chip);
			goto SD_Execute_Read_Cmd_Failed;
		}

		min_len = min(data_len, scsi_bufflen(srb));
		rtsx_stor_set_xfer_buf(buf, min_len, srb);

		kfree(buf);
	} else if (!(data_len & 0x1FF)) {
		rtsx_init_cmd(chip);

		trans_dma_enable(DMA_FROM_DEVICE, chip, data_len, DMA_512);

		rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_BYTE_CNT_H, 0xFF,
			0x02);
		rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_BYTE_CNT_L, 0xFF,
			0x00);
		rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_BLOCK_CNT_H,
				0xFF, (srb->cmnd[7] & 0xFE) >> 1);
		rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_BLOCK_CNT_L,
				0xFF, (u8)((data_len & 0x0001FE00) >> 9));

		rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CMD0, 0xFF,
			0x40 | cmd_idx);
		rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CMD1, 0xFF,
			srb->cmnd[3]);
		rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CMD2, 0xFF,
			srb->cmnd[4]);
		rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CMD3, 0xFF,
			srb->cmnd[5]);
		rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CMD4, 0xFF,
			srb->cmnd[6]);

		rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CFG1, 0x03, bus_width);
		rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_CFG2, 0xFF, rsp_type);

		rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_TRANSFER,
			     0xFF, SD_TM_AUTO_READ_2 | SD_TRANSFER_START);
		rtsx_add_cmd(chip, CHECK_REG_CMD, REG_SD_TRANSFER,
			SD_TRANSFER_END, SD_TRANSFER_END);

		rtsx_send_cmd_no_wait(chip);

		retval = rtsx_transfer_data(chip, SD_CARD, scsi_sglist(srb),
					scsi_bufflen(srb), scsi_sg_count(srb),
					DMA_FROM_DEVICE, 10000);
		if (retval < 0) {
			read_err = true;
			rtsx_clear_sd_error(chip);
			rtsx_trace(chip);
			goto SD_Execute_Read_Cmd_Failed;
		}

	} else {
		rtsx_trace(chip);
		goto SD_Execute_Read_Cmd_Failed;
	}

	retval = ext_sd_get_rsp(chip, rsp_len, sd_card->rsp, rsp_type);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		goto SD_Execute_Read_Cmd_Failed;
	}

	if (standby) {
		retval = sd_select_card(chip, 1);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			goto SD_Execute_Read_Cmd_Failed;
		}
	}

	if (send_cmd12) {
		retval = ext_sd_send_cmd_get_rsp(chip, STOP_TRANSMISSION,
				0, SD_RSP_TYPE_R1b, NULL, 0, false);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			goto SD_Execute_Read_Cmd_Failed;
		}
	}

	if (data_len < 512) {
		retval = ext_sd_send_cmd_get_rsp(chip, SET_BLOCKLEN, 0x200,
				SD_RSP_TYPE_R1, NULL, 0, false);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			goto SD_Execute_Read_Cmd_Failed;
		}

		retval = rtsx_write_register(chip, SD_BYTE_CNT_H, 0xFF, 0x02);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			goto SD_Execute_Read_Cmd_Failed;
		}

		retval = rtsx_write_register(chip, SD_BYTE_CNT_L, 0xFF, 0x00);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			goto SD_Execute_Read_Cmd_Failed;
		}
	}

	if ((srb->cmnd[1] & 0x02) || (srb->cmnd[1] & 0x04))
		cmd13_checkbit = true;

	for (i = 0; i < 3; i++) {
		retval = ext_sd_send_cmd_get_rsp(chip, SEND_STATUS,
						sd_card->sd_addr,
						SD_RSP_TYPE_R1, NULL, 0,
						cmd13_checkbit);
		if (retval == STATUS_SUCCESS)
			break;
	}
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		goto SD_Execute_Read_Cmd_Failed;
	}

	scsi_set_resid(srb, 0);
	return TRANSPORT_GOOD;

SD_Execute_Read_Cmd_Failed:
	sd_card->pre_cmd_err = 1;
	set_sense_type(chip, lun, SENSE_TYPE_NO_SENSE);
	if (read_err)
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_UNRECOVER_READ_ERR);

	release_sd_card(chip);
	do_reset_sd_card(chip);
	if (!(chip->card_ready & SD_CARD))
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_NOT_PRESENT);

	rtsx_trace(chip);
	return TRANSPORT_FAILED;
}

int sd_execute_write_data(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	unsigned int lun = SCSI_LUN(srb);
	int retval, rsp_len, i;
	bool write_err = false, cmd13_checkbit = false;
	u8 cmd_idx, rsp_type;
	bool standby = false, send_cmd12 = false, acmd = false;
	u32 data_len, arg;
#ifdef SUPPORT_SD_LOCK
	int lock_cmd_fail = 0;
	u8 sd_lock_state = 0;
	u8 lock_cmd_type = 0;
#endif

	if (!sd_card->sd_pass_thru_en) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		rtsx_trace(chip);
		return TRANSPORT_FAILED;
	}

	if (sd_card->pre_cmd_err) {
		sd_card->pre_cmd_err = 0;
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_CHANGE);
		rtsx_trace(chip);
		return TRANSPORT_FAILED;
	}

	retval = sd_switch_clock(chip);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return TRANSPORT_FAILED;
	}

	cmd_idx = srb->cmnd[2] & 0x3F;
	if (srb->cmnd[1] & 0x04)
		send_cmd12 = true;

	if (srb->cmnd[1] & 0x02)
		standby = true;

	if (srb->cmnd[1] & 0x01)
		acmd = true;

	data_len = ((u32)srb->cmnd[7] << 16) | ((u32)srb->cmnd[8]
						<< 8) | srb->cmnd[9];
	arg = ((u32)srb->cmnd[3] << 24) | ((u32)srb->cmnd[4] << 16) |
		((u32)srb->cmnd[5] << 8) | srb->cmnd[6];

#ifdef SUPPORT_SD_LOCK
	if (cmd_idx == LOCK_UNLOCK) {
		sd_lock_state = sd_card->sd_lock_status;
		sd_lock_state &= SD_LOCKED;
	}
#endif

	retval = get_rsp_type(srb, &rsp_type, &rsp_len);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		rtsx_trace(chip);
		return TRANSPORT_FAILED;
	}
	sd_card->last_rsp_type = rsp_type;

	retval = sd_switch_clock(chip);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return TRANSPORT_FAILED;
	}

#ifdef SUPPORT_SD_LOCK
	if ((sd_card->sd_lock_status & SD_LOCK_1BIT_MODE) == 0) {
		if (CHK_MMC_8BIT(sd_card)) {
			retval = rtsx_write_register(chip, REG_SD_CFG1, 0x03,
						SD_BUS_WIDTH_8);
			if (retval != STATUS_SUCCESS) {
				rtsx_trace(chip);
				return TRANSPORT_FAILED;
			}

		} else if (CHK_SD(sd_card) || CHK_MMC_4BIT(sd_card)) {
			retval = rtsx_write_register(chip, REG_SD_CFG1, 0x03,
						SD_BUS_WIDTH_4);
			if (retval != STATUS_SUCCESS) {
				rtsx_trace(chip);
				return TRANSPORT_FAILED;
			}
		}
	}
#else
	retval = rtsx_write_register(chip, REG_SD_CFG1, 0x03, SD_BUS_WIDTH_4);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return TRANSPORT_FAILED;
	}
#endif

	if (data_len < 512) {
		retval = ext_sd_send_cmd_get_rsp(chip, SET_BLOCKLEN, data_len,
				SD_RSP_TYPE_R1, NULL, 0, false);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			goto SD_Execute_Write_Cmd_Failed;
		}
	}

	if (standby) {
		retval = sd_select_card(chip, 0);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			goto SD_Execute_Write_Cmd_Failed;
		}
	}

	if (acmd) {
		retval = ext_sd_send_cmd_get_rsp(chip, APP_CMD,
						sd_card->sd_addr,
						SD_RSP_TYPE_R1, NULL, 0, false);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			goto SD_Execute_Write_Cmd_Failed;
		}
	}

	retval = ext_sd_send_cmd_get_rsp(chip, cmd_idx, arg, rsp_type,
			sd_card->rsp, rsp_len, false);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		goto SD_Execute_Write_Cmd_Failed;
	}

	if (data_len <= 512) {
		u16 i;
		u8 *buf;

		buf = kmalloc(data_len, GFP_KERNEL);
		if (!buf) {
			rtsx_trace(chip);
			return TRANSPORT_ERROR;
		}

		rtsx_stor_get_xfer_buf(buf, data_len, srb);

#ifdef SUPPORT_SD_LOCK
		if (cmd_idx == LOCK_UNLOCK)
			lock_cmd_type = buf[0] & 0x0F;
#endif

		if (data_len > 256) {
			rtsx_init_cmd(chip);
			for (i = 0; i < 256; i++) {
				rtsx_add_cmd(chip, WRITE_REG_CMD,
						PPBUF_BASE2 + i, 0xFF, buf[i]);
			}
			retval = rtsx_send_cmd(chip, 0, 250);
			if (retval != STATUS_SUCCESS) {
				kfree(buf);
				rtsx_trace(chip);
				goto SD_Execute_Write_Cmd_Failed;
			}

			rtsx_init_cmd(chip);
			for (i = 256; i < data_len; i++) {
				rtsx_add_cmd(chip, WRITE_REG_CMD,
						PPBUF_BASE2 + i, 0xFF, buf[i]);
			}
			retval = rtsx_send_cmd(chip, 0, 250);
			if (retval != STATUS_SUCCESS) {
				kfree(buf);
				rtsx_trace(chip);
				goto SD_Execute_Write_Cmd_Failed;
			}
		} else {
			rtsx_init_cmd(chip);
			for (i = 0; i < data_len; i++) {
				rtsx_add_cmd(chip, WRITE_REG_CMD,
						PPBUF_BASE2 + i, 0xFF, buf[i]);
			}
			retval = rtsx_send_cmd(chip, 0, 250);
			if (retval != STATUS_SUCCESS) {
				kfree(buf);
				rtsx_trace(chip);
				goto SD_Execute_Write_Cmd_Failed;
			}
		}

		kfree(buf);

		rtsx_init_cmd(chip);

		rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_BYTE_CNT_H, 0xFF,
			srb->cmnd[8] & 0x03);
		rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_BYTE_CNT_L, 0xFF,
			srb->cmnd[9]);
		rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_BLOCK_CNT_H, 0xFF,
			0x00);
		rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_BLOCK_CNT_L, 0xFF,
			0x01);
		rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_DATA_SOURCE, 0x01,
			PINGPONG_BUFFER);

		rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_TRANSFER, 0xFF,
			     SD_TM_AUTO_WRITE_3 | SD_TRANSFER_START);
		rtsx_add_cmd(chip, CHECK_REG_CMD, REG_SD_TRANSFER,
			SD_TRANSFER_END, SD_TRANSFER_END);

		retval = rtsx_send_cmd(chip, SD_CARD, 250);
	} else if (!(data_len & 0x1FF)) {
		rtsx_init_cmd(chip);

		trans_dma_enable(DMA_TO_DEVICE, chip, data_len, DMA_512);

		rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_BYTE_CNT_H, 0xFF,
			0x02);
		rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_BYTE_CNT_L, 0xFF,
			0x00);
		rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_BLOCK_CNT_H,
				0xFF, (srb->cmnd[7] & 0xFE) >> 1);
		rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_BLOCK_CNT_L,
				0xFF, (u8)((data_len & 0x0001FE00) >> 9));

		rtsx_add_cmd(chip, WRITE_REG_CMD, REG_SD_TRANSFER, 0xFF,
			SD_TM_AUTO_WRITE_3 | SD_TRANSFER_START);
		rtsx_add_cmd(chip, CHECK_REG_CMD, REG_SD_TRANSFER,
			SD_TRANSFER_END, SD_TRANSFER_END);

		rtsx_send_cmd_no_wait(chip);

		retval = rtsx_transfer_data(chip, SD_CARD, scsi_sglist(srb),
					scsi_bufflen(srb), scsi_sg_count(srb),
					DMA_TO_DEVICE, 10000);

	} else {
		rtsx_trace(chip);
		goto SD_Execute_Write_Cmd_Failed;
	}

	if (retval < 0) {
		write_err = true;
		rtsx_clear_sd_error(chip);
		rtsx_trace(chip);
		goto SD_Execute_Write_Cmd_Failed;
	}

#ifdef SUPPORT_SD_LOCK
	if (cmd_idx == LOCK_UNLOCK) {
		if (lock_cmd_type == SD_ERASE) {
			sd_card->sd_erase_status = SD_UNDER_ERASING;
			scsi_set_resid(srb, 0);
			return TRANSPORT_GOOD;
		}

		rtsx_init_cmd(chip);
		rtsx_add_cmd(chip, CHECK_REG_CMD, 0xFD30, 0x02, 0x02);

		rtsx_send_cmd(chip, SD_CARD, 250);

		retval = sd_update_lock_status(chip);
		if (retval != STATUS_SUCCESS) {
			dev_dbg(rtsx_dev(chip), "Lock command fail!\n");
			lock_cmd_fail = 1;
		}
	}
#endif /* SUPPORT_SD_LOCK */

	if (standby) {
		retval = sd_select_card(chip, 1);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			goto SD_Execute_Write_Cmd_Failed;
		}
	}

	if (send_cmd12) {
		retval = ext_sd_send_cmd_get_rsp(chip, STOP_TRANSMISSION,
				0, SD_RSP_TYPE_R1b, NULL, 0, false);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			goto SD_Execute_Write_Cmd_Failed;
		}
	}

	if (data_len < 512) {
		retval = ext_sd_send_cmd_get_rsp(chip, SET_BLOCKLEN, 0x200,
				SD_RSP_TYPE_R1, NULL, 0, false);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			goto SD_Execute_Write_Cmd_Failed;
		}

		retval = rtsx_write_register(chip, SD_BYTE_CNT_H, 0xFF, 0x02);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			goto SD_Execute_Write_Cmd_Failed;
		}

		rtsx_write_register(chip, SD_BYTE_CNT_L, 0xFF, 0x00);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			goto SD_Execute_Write_Cmd_Failed;
		}
	}

	if ((srb->cmnd[1] & 0x02) || (srb->cmnd[1] & 0x04))
		cmd13_checkbit = true;

	for (i = 0; i < 3; i++) {
		retval = ext_sd_send_cmd_get_rsp(chip, SEND_STATUS,
						sd_card->sd_addr,
						SD_RSP_TYPE_R1, NULL, 0,
						cmd13_checkbit);
		if (retval == STATUS_SUCCESS)
			break;
	}
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		goto SD_Execute_Write_Cmd_Failed;
	}

#ifdef SUPPORT_SD_LOCK
	if (cmd_idx == LOCK_UNLOCK) {
		if (!lock_cmd_fail) {
			dev_dbg(rtsx_dev(chip), "lock_cmd_type = 0x%x\n",
				lock_cmd_type);
			if (lock_cmd_type & SD_CLR_PWD)
				sd_card->sd_lock_status &= ~SD_PWD_EXIST;

			if (lock_cmd_type & SD_SET_PWD)
				sd_card->sd_lock_status |= SD_PWD_EXIST;
		}

		dev_dbg(rtsx_dev(chip), "sd_lock_state = 0x%x, sd_card->sd_lock_status = 0x%x\n",
			sd_lock_state, sd_card->sd_lock_status);
		if (sd_lock_state ^ (sd_card->sd_lock_status & SD_LOCKED)) {
			sd_card->sd_lock_notify = 1;
			if (sd_lock_state) {
				if (sd_card->sd_lock_status & SD_LOCK_1BIT_MODE) {
					sd_card->sd_lock_status |= (
						SD_UNLOCK_POW_ON | SD_SDR_RST);
					if (CHK_SD(sd_card)) {
						retval = reset_sd(chip);
						if (retval != STATUS_SUCCESS) {
							sd_card->sd_lock_status &= ~(SD_UNLOCK_POW_ON | SD_SDR_RST);
							rtsx_trace(chip);
							goto SD_Execute_Write_Cmd_Failed;
						}
					}

					sd_card->sd_lock_status &= ~(SD_UNLOCK_POW_ON | SD_SDR_RST);
				}
			}
		}
	}

	if (lock_cmd_fail) {
		scsi_set_resid(srb, 0);
		set_sense_type(chip, lun, SENSE_TYPE_NO_SENSE);
		rtsx_trace(chip);
		return TRANSPORT_FAILED;
	}
#endif  /* SUPPORT_SD_LOCK */

	scsi_set_resid(srb, 0);
	return TRANSPORT_GOOD;

SD_Execute_Write_Cmd_Failed:
	sd_card->pre_cmd_err = 1;
	set_sense_type(chip, lun, SENSE_TYPE_NO_SENSE);
	if (write_err)
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_WRITE_ERR);

	release_sd_card(chip);
	do_reset_sd_card(chip);
	if (!(chip->card_ready & SD_CARD))
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_NOT_PRESENT);

	rtsx_trace(chip);
	return TRANSPORT_FAILED;
}

int sd_get_cmd_rsp(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	unsigned int lun = SCSI_LUN(srb);
	int count;
	u16 data_len;

	if (!sd_card->sd_pass_thru_en) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		rtsx_trace(chip);
		return TRANSPORT_FAILED;
	}

	if (sd_card->pre_cmd_err) {
		sd_card->pre_cmd_err = 0;
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_CHANGE);
		rtsx_trace(chip);
		return TRANSPORT_FAILED;
	}

	data_len = ((u16)srb->cmnd[7] << 8) | srb->cmnd[8];

	if (sd_card->last_rsp_type == SD_RSP_TYPE_R0) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		rtsx_trace(chip);
		return TRANSPORT_FAILED;
	} else if (sd_card->last_rsp_type == SD_RSP_TYPE_R2) {
		count = (data_len < 17) ? data_len : 17;
	} else {
		count = (data_len < 6) ? data_len : 6;
	}
	rtsx_stor_set_xfer_buf(sd_card->rsp, count, srb);

	dev_dbg(rtsx_dev(chip), "Response length: %d\n", data_len);
	dev_dbg(rtsx_dev(chip), "Response: 0x%x 0x%x 0x%x 0x%x\n",
		sd_card->rsp[0], sd_card->rsp[1],
		sd_card->rsp[2], sd_card->rsp[3]);

	scsi_set_resid(srb, 0);
	return TRANSPORT_GOOD;
}

int sd_hw_rst(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	unsigned int lun = SCSI_LUN(srb);
	int retval;

	if (!sd_card->sd_pass_thru_en) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		rtsx_trace(chip);
		return TRANSPORT_FAILED;
	}

	if (sd_card->pre_cmd_err) {
		sd_card->pre_cmd_err = 0;
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_CHANGE);
		rtsx_trace(chip);
		return TRANSPORT_FAILED;
	}

	if ((srb->cmnd[2] != 0x53) || (srb->cmnd[3] != 0x44) ||
		(srb->cmnd[4] != 0x20) || (srb->cmnd[5] != 0x43) ||
		(srb->cmnd[6] != 0x61) || (srb->cmnd[7] != 0x72) ||
		(srb->cmnd[8] != 0x64)) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		rtsx_trace(chip);
		return TRANSPORT_FAILED;
	}

	switch (srb->cmnd[1] & 0x0F) {
	case 0:
#ifdef SUPPORT_SD_LOCK
		if (srb->cmnd[9] == 0x64)
			sd_card->sd_lock_status |= SD_SDR_RST;
#endif
		retval = reset_sd_card(chip);
		if (retval != STATUS_SUCCESS) {
#ifdef SUPPORT_SD_LOCK
			sd_card->sd_lock_status &= ~SD_SDR_RST;
#endif
			set_sense_type(chip, lun, SENSE_TYPE_MEDIA_NOT_PRESENT);
			sd_card->pre_cmd_err = 1;
			rtsx_trace(chip);
			return TRANSPORT_FAILED;
		}
#ifdef SUPPORT_SD_LOCK
		sd_card->sd_lock_status &= ~SD_SDR_RST;
#endif
		break;

	case 1:
		retval = soft_reset_sd_card(chip);
		if (retval != STATUS_SUCCESS) {
			set_sense_type(chip, lun, SENSE_TYPE_MEDIA_NOT_PRESENT);
			sd_card->pre_cmd_err = 1;
			rtsx_trace(chip);
			return TRANSPORT_FAILED;
		}
		break;

	default:
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		rtsx_trace(chip);
		return TRANSPORT_FAILED;
	}

	scsi_set_resid(srb, 0);
	return TRANSPORT_GOOD;
}
#endif

void sd_cleanup_work(struct rtsx_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);

	if (sd_card->seq_mode) {
		dev_dbg(rtsx_dev(chip), "SD: stop transmission\n");
		sd_stop_seq_mode(chip);
		sd_card->cleanup_counter = 0;
	}
}

int sd_power_off_card3v3(struct rtsx_chip *chip)
{
	int retval;

	retval = disable_card_clock(chip, SD_CARD);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	retval = rtsx_write_register(chip, CARD_OE, SD_OUTPUT_EN, 0);
	if (retval) {
		rtsx_trace(chip);
		return retval;
	}

	if (!chip->ft2_fast_mode) {
		retval = card_power_off(chip, SD_CARD);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}

		wait_timeout(50);
	}

	if (chip->asic_code) {
		retval = sd_pull_ctl_disable(chip);
		if (retval != STATUS_SUCCESS) {
			rtsx_trace(chip);
			return STATUS_FAIL;
		}
	} else {
		retval = rtsx_write_register(chip, FPGA_PULL_CTL,
					     FPGA_SD_PULL_CTL_BIT | 0x20,
					     FPGA_SD_PULL_CTL_BIT);
		if (retval) {
			rtsx_trace(chip);
			return retval;
		}
	}

	return STATUS_SUCCESS;
}

int release_sd_card(struct rtsx_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;

	chip->card_ready &= ~SD_CARD;
	chip->card_fail &= ~SD_CARD;
	chip->card_wp &= ~SD_CARD;

	chip->sd_io = 0;
	chip->sd_int = 0;

#ifdef SUPPORT_SD_LOCK
	sd_card->sd_lock_status = 0;
	sd_card->sd_erase_status = 0;
#endif

	memset(sd_card->raw_csd, 0, 16);
	memset(sd_card->raw_scr, 0, 8);

	retval = sd_power_off_card3v3(chip);
	if (retval != STATUS_SUCCESS) {
		rtsx_trace(chip);
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}
