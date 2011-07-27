/* Driver for Realtek PCI-Express card reader
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
 */

#include <linux/blkdev.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>

#include "rtsx.h"
#include "rtsx_transport.h"
#include "rtsx_scsi.h"
#include "rtsx_card.h"
#include "ms.h"

static inline void ms_set_err_code(struct rtsx_chip *chip, u8 err_code)
{
	struct ms_info *ms_card = &(chip->ms_card);

	ms_card->err_code = err_code;
}

static inline int ms_check_err_code(struct rtsx_chip *chip, u8 err_code)
{
	struct ms_info *ms_card = &(chip->ms_card);

	return (ms_card->err_code == err_code);
}

static int ms_parse_err_code(struct rtsx_chip *chip)
{
	TRACE_RET(chip, STATUS_FAIL);
}

static int ms_transfer_tpc(struct rtsx_chip *chip, u8 trans_mode, u8 tpc, u8 cnt, u8 cfg)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int retval;
	u8 *ptr;

	RTSX_DEBUGP("ms_transfer_tpc: tpc = 0x%x\n", tpc);

	rtsx_init_cmd(chip);

	rtsx_add_cmd(chip, WRITE_REG_CMD, MS_TPC, 0xFF, tpc);
	rtsx_add_cmd(chip, WRITE_REG_CMD, MS_BYTE_CNT, 0xFF, cnt);
	rtsx_add_cmd(chip, WRITE_REG_CMD, MS_TRANS_CFG, 0xFF, cfg);
	rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_DATA_SOURCE, 0x01, PINGPONG_BUFFER);

	rtsx_add_cmd(chip, WRITE_REG_CMD, MS_TRANSFER, 0xFF, MS_TRANSFER_START | trans_mode);
	rtsx_add_cmd(chip, CHECK_REG_CMD, MS_TRANSFER, MS_TRANSFER_END, MS_TRANSFER_END);

	rtsx_add_cmd(chip, READ_REG_CMD, MS_TRANS_CFG, 0, 0);

	retval = rtsx_send_cmd(chip, MS_CARD, 5000);
	if (retval < 0) {
		rtsx_clear_ms_error(chip);
		ms_set_err_code(chip, MS_TO_ERROR);
		TRACE_RET(chip, ms_parse_err_code(chip));
	}

	ptr = rtsx_get_cmd_data(chip) + 1;

	if (!(tpc & 0x08)) {		/* Read Packet */
		if (*ptr & MS_CRC16_ERR) {
			ms_set_err_code(chip, MS_CRC16_ERROR);
			TRACE_RET(chip, ms_parse_err_code(chip));
		}
	} else {			/* Write Packet */
		if (CHK_MSPRO(ms_card) && !(*ptr & 0x80)) {
			if (*ptr & (MS_INT_ERR | MS_INT_CMDNK)) {
				ms_set_err_code(chip, MS_CMD_NK);
				TRACE_RET(chip, ms_parse_err_code(chip));
			}
		}
	}

	if (*ptr & MS_RDY_TIMEOUT) {
		rtsx_clear_ms_error(chip);
		ms_set_err_code(chip, MS_TO_ERROR);
		TRACE_RET(chip, ms_parse_err_code(chip));
	}

	return STATUS_SUCCESS;
}

static int ms_transfer_data(struct rtsx_chip *chip, u8 trans_mode, u8 tpc, u16 sec_cnt,
		u8 cfg, int mode_2k, int use_sg, void *buf, int buf_len)
{
	int retval;
	u8 val, err_code = 0;
	enum dma_data_direction dir;

	if (!buf || !buf_len) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	if (trans_mode == MS_TM_AUTO_READ) {
		dir = DMA_FROM_DEVICE;
		err_code = MS_FLASH_READ_ERROR;
	} else if (trans_mode == MS_TM_AUTO_WRITE) {
		dir = DMA_TO_DEVICE;
		err_code = MS_FLASH_WRITE_ERROR;
	} else {
		TRACE_RET(chip, STATUS_FAIL);
	}

	rtsx_init_cmd(chip);

	rtsx_add_cmd(chip, WRITE_REG_CMD, MS_TPC, 0xFF, tpc);
	rtsx_add_cmd(chip, WRITE_REG_CMD,
		     MS_SECTOR_CNT_H, 0xFF, (u8)(sec_cnt >> 8));
	rtsx_add_cmd(chip, WRITE_REG_CMD, MS_SECTOR_CNT_L, 0xFF, (u8)sec_cnt);
	rtsx_add_cmd(chip, WRITE_REG_CMD, MS_TRANS_CFG, 0xFF, cfg);

	if (mode_2k) {
		rtsx_add_cmd(chip, WRITE_REG_CMD,
			     MS_CFG, MS_2K_SECTOR_MODE, MS_2K_SECTOR_MODE);
	} else {
		rtsx_add_cmd(chip, WRITE_REG_CMD, MS_CFG, MS_2K_SECTOR_MODE, 0);
	}

	trans_dma_enable(dir, chip, sec_cnt * 512, DMA_512);

	rtsx_add_cmd(chip, WRITE_REG_CMD,
		     MS_TRANSFER, 0xFF, MS_TRANSFER_START | trans_mode);
	rtsx_add_cmd(chip, CHECK_REG_CMD,
		     MS_TRANSFER, MS_TRANSFER_END, MS_TRANSFER_END);

	rtsx_send_cmd_no_wait(chip);

	retval = rtsx_transfer_data(chip, MS_CARD, buf, buf_len,
				    use_sg, dir, chip->mspro_timeout);
	if (retval < 0) {
		ms_set_err_code(chip, err_code);
		if (retval == -ETIMEDOUT) {
			retval = STATUS_TIMEDOUT;
		} else {
			retval = STATUS_FAIL;
		}
		TRACE_RET(chip, retval);
	}

	RTSX_READ_REG(chip, MS_TRANS_CFG, &val);
	if (val & (MS_INT_CMDNK | MS_INT_ERR | MS_CRC16_ERR | MS_RDY_TIMEOUT)) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	return STATUS_SUCCESS;
}

static int ms_write_bytes(struct rtsx_chip *chip,
			  u8 tpc, u8 cnt, u8 cfg, u8 *data, int data_len)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int retval, i;

	if (!data || (data_len < cnt)) {
		TRACE_RET(chip, STATUS_ERROR);
	}

	rtsx_init_cmd(chip);

	for (i = 0; i < cnt; i++) {
		rtsx_add_cmd(chip, WRITE_REG_CMD,
			     PPBUF_BASE2 + i, 0xFF, data[i]);
	}
	if (cnt % 2) {
		rtsx_add_cmd(chip, WRITE_REG_CMD, PPBUF_BASE2 + i, 0xFF, 0xFF);
	}

	rtsx_add_cmd(chip, WRITE_REG_CMD, MS_TPC, 0xFF, tpc);
	rtsx_add_cmd(chip, WRITE_REG_CMD, MS_BYTE_CNT, 0xFF, cnt);
	rtsx_add_cmd(chip, WRITE_REG_CMD, MS_TRANS_CFG, 0xFF, cfg);
	rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_DATA_SOURCE, 0x01, PINGPONG_BUFFER);

	rtsx_add_cmd(chip, WRITE_REG_CMD,
		     MS_TRANSFER, 0xFF, MS_TRANSFER_START | MS_TM_WRITE_BYTES);
	rtsx_add_cmd(chip, CHECK_REG_CMD,
		     MS_TRANSFER, MS_TRANSFER_END, MS_TRANSFER_END);

	retval = rtsx_send_cmd(chip, MS_CARD, 5000);
	if (retval < 0) {
		u8 val = 0;

		rtsx_read_register(chip, MS_TRANS_CFG, &val);
		RTSX_DEBUGP("MS_TRANS_CFG: 0x%02x\n", val);

		rtsx_clear_ms_error(chip);

		if (!(tpc & 0x08)) {
			if (val & MS_CRC16_ERR) {
				ms_set_err_code(chip, MS_CRC16_ERROR);
				TRACE_RET(chip, ms_parse_err_code(chip));
			}
		} else {
			if (CHK_MSPRO(ms_card) && !(val & 0x80)) {
				if (val & (MS_INT_ERR | MS_INT_CMDNK)) {
					ms_set_err_code(chip, MS_CMD_NK);
					TRACE_RET(chip, ms_parse_err_code(chip));
				}
			}
		}

		if (val & MS_RDY_TIMEOUT) {
			ms_set_err_code(chip, MS_TO_ERROR);
			TRACE_RET(chip, ms_parse_err_code(chip));
		}

		ms_set_err_code(chip, MS_TO_ERROR);
		TRACE_RET(chip, ms_parse_err_code(chip));
	}

	return STATUS_SUCCESS;
}

static int ms_read_bytes(struct rtsx_chip *chip, u8 tpc, u8 cnt, u8 cfg, u8 *data, int data_len)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int retval, i;
	u8 *ptr;

	if (!data) {
		TRACE_RET(chip, STATUS_ERROR);
	}

	rtsx_init_cmd(chip);

	rtsx_add_cmd(chip, WRITE_REG_CMD, MS_TPC, 0xFF, tpc);
	rtsx_add_cmd(chip, WRITE_REG_CMD, MS_BYTE_CNT, 0xFF, cnt);
	rtsx_add_cmd(chip, WRITE_REG_CMD, MS_TRANS_CFG, 0xFF, cfg);
	rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_DATA_SOURCE, 0x01, PINGPONG_BUFFER);

	rtsx_add_cmd(chip, WRITE_REG_CMD, MS_TRANSFER, 0xFF, MS_TRANSFER_START | MS_TM_READ_BYTES);
	rtsx_add_cmd(chip, CHECK_REG_CMD, MS_TRANSFER, MS_TRANSFER_END, MS_TRANSFER_END);

	for (i = 0; i < data_len - 1; i++) {
	       rtsx_add_cmd(chip, READ_REG_CMD, PPBUF_BASE2 + i, 0, 0);
	}
	if (data_len % 2) {
		rtsx_add_cmd(chip, READ_REG_CMD, PPBUF_BASE2 + data_len, 0, 0);
	} else {
		rtsx_add_cmd(chip, READ_REG_CMD, PPBUF_BASE2 + data_len - 1, 0, 0);
	}

	retval = rtsx_send_cmd(chip, MS_CARD, 5000);
	if (retval < 0) {
		u8 val = 0;

		rtsx_read_register(chip, MS_TRANS_CFG, &val);
		rtsx_clear_ms_error(chip);

		if (!(tpc & 0x08)) {
			if (val & MS_CRC16_ERR) {
				ms_set_err_code(chip, MS_CRC16_ERROR);
				TRACE_RET(chip, ms_parse_err_code(chip));
			}
		} else {
			if (CHK_MSPRO(ms_card) && !(val & 0x80)) {
				if (val & (MS_INT_ERR | MS_INT_CMDNK)) {
					ms_set_err_code(chip, MS_CMD_NK);
					TRACE_RET(chip, ms_parse_err_code(chip));
				}
			}
		}

		if (val & MS_RDY_TIMEOUT) {
			ms_set_err_code(chip, MS_TO_ERROR);
			TRACE_RET(chip, ms_parse_err_code(chip));
		}

		ms_set_err_code(chip, MS_TO_ERROR);
		TRACE_RET(chip, ms_parse_err_code(chip));
	}

	ptr = rtsx_get_cmd_data(chip) + 1;

	for (i = 0; i < data_len; i++) {
		data[i] = ptr[i];
	}

	if ((tpc == PRO_READ_SHORT_DATA) && (data_len == 8)) {
		RTSX_DEBUGP("Read format progress:\n");
		RTSX_DUMP(ptr, cnt);
	}

	return STATUS_SUCCESS;
}

static int ms_set_rw_reg_addr(struct rtsx_chip *chip,
		u8 read_start, u8 read_cnt, u8 write_start, u8 write_cnt)
{
	int retval, i;
	u8 data[4];

	data[0] = read_start;
	data[1] = read_cnt;
	data[2] = write_start;
	data[3] = write_cnt;

	for (i = 0; i < MS_MAX_RETRY_COUNT; i++) {
		retval = ms_write_bytes(chip, SET_RW_REG_ADRS, 4,
					NO_WAIT_INT, data, 4);
		if (retval == STATUS_SUCCESS)
			return STATUS_SUCCESS;
		rtsx_clear_ms_error(chip);
	}

	TRACE_RET(chip, STATUS_FAIL);
}

static int ms_send_cmd(struct rtsx_chip *chip, u8 cmd, u8 cfg)
{
	u8 data[2];

	data[0] = cmd;
	data[1] = 0;

	return ms_write_bytes(chip, PRO_SET_CMD, 1, cfg, data, 1);
}

static int ms_set_init_para(struct rtsx_chip *chip)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int retval;

	if (CHK_HG8BIT(ms_card)) {
		if (chip->asic_code) {
			ms_card->ms_clock = chip->asic_ms_hg_clk;
		} else {
			ms_card->ms_clock = chip->fpga_ms_hg_clk;
		}
	} else if (CHK_MSPRO(ms_card) || CHK_MS4BIT(ms_card)) {
		if (chip->asic_code) {
			ms_card->ms_clock = chip->asic_ms_4bit_clk;
		} else {
			ms_card->ms_clock = chip->fpga_ms_4bit_clk;
		}
	} else {
		if (chip->asic_code) {
			ms_card->ms_clock = chip->asic_ms_1bit_clk;
		} else {
			ms_card->ms_clock = chip->fpga_ms_1bit_clk;
		}
	}

	retval = switch_clock(chip, ms_card->ms_clock);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	retval = select_card(chip, MS_CARD);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	return STATUS_SUCCESS;
}

static int ms_switch_clock(struct rtsx_chip *chip)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int retval;

	retval = select_card(chip, MS_CARD);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	retval = switch_clock(chip, ms_card->ms_clock);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	return STATUS_SUCCESS;
}

static int ms_pull_ctl_disable(struct rtsx_chip *chip)
{
	if (CHECK_PID(chip, 0x5209)) {
		RTSX_WRITE_REG(chip, CARD_PULL_CTL4, 0xFF, 0x55);
		RTSX_WRITE_REG(chip, CARD_PULL_CTL5, 0xFF, 0x55);
		RTSX_WRITE_REG(chip, CARD_PULL_CTL6, 0xFF, 0x15);
	} else if (CHECK_PID(chip, 0x5208)) {
		RTSX_WRITE_REG(chip, CARD_PULL_CTL1, 0xFF,
			MS_D1_PD | MS_D2_PD | MS_CLK_PD | MS_D6_PD);
		RTSX_WRITE_REG(chip, CARD_PULL_CTL2, 0xFF,
			MS_D3_PD | MS_D0_PD | MS_BS_PD | XD_D4_PD);
		RTSX_WRITE_REG(chip, CARD_PULL_CTL3, 0xFF,
			MS_D7_PD | XD_CE_PD | XD_CLE_PD | XD_CD_PU);
		RTSX_WRITE_REG(chip, CARD_PULL_CTL4, 0xFF,
			XD_RDY_PD | SD_D3_PD | SD_D2_PD | XD_ALE_PD);
		RTSX_WRITE_REG(chip, CARD_PULL_CTL5, 0xFF,
			MS_INS_PU | SD_WP_PD | SD_CD_PU | SD_CMD_PD);
		RTSX_WRITE_REG(chip, CARD_PULL_CTL6, 0xFF,
			MS_D5_PD | MS_D4_PD);
	} else if (CHECK_PID(chip, 0x5288)) {
		if (CHECK_BARO_PKG(chip, QFN)) {
			RTSX_WRITE_REG(chip, CARD_PULL_CTL1, 0xFF, 0x55);
			RTSX_WRITE_REG(chip, CARD_PULL_CTL2, 0xFF, 0x55);
			RTSX_WRITE_REG(chip, CARD_PULL_CTL3, 0xFF, 0x4B);
			RTSX_WRITE_REG(chip, CARD_PULL_CTL4, 0xFF, 0x69);
		}
	}

	return STATUS_SUCCESS;
}

static int ms_pull_ctl_enable(struct rtsx_chip *chip)
{
	int retval;

	rtsx_init_cmd(chip);

	if (CHECK_PID(chip, 0x5209)) {
		rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL4, 0xFF, 0x55);
		rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL5, 0xFF, 0x55);
		rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL6, 0xFF, 0x15);
	} else if (CHECK_PID(chip, 0x5208)) {
		rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL1, 0xFF,
			MS_D1_PD | MS_D2_PD | MS_CLK_NP | MS_D6_PD);
		rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL2, 0xFF,
			MS_D3_PD | MS_D0_PD | MS_BS_NP | XD_D4_PD);
		rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL3, 0xFF,
			MS_D7_PD | XD_CE_PD | XD_CLE_PD | XD_CD_PU);
		rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL4, 0xFF,
			XD_RDY_PD | SD_D3_PD | SD_D2_PD | XD_ALE_PD);
		rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL5, 0xFF,
			MS_INS_PU | SD_WP_PD | SD_CD_PU | SD_CMD_PD);
		rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_PULL_CTL6, 0xFF,
			MS_D5_PD | MS_D4_PD);
	} else if (CHECK_PID(chip, 0x5288)) {
		if (CHECK_BARO_PKG(chip, QFN)) {
			rtsx_add_cmd(chip, WRITE_REG_CMD,
				     CARD_PULL_CTL1, 0xFF, 0x55);
			rtsx_add_cmd(chip, WRITE_REG_CMD,
				     CARD_PULL_CTL2, 0xFF, 0x45);
			rtsx_add_cmd(chip, WRITE_REG_CMD,
				     CARD_PULL_CTL3, 0xFF, 0x4B);
			rtsx_add_cmd(chip, WRITE_REG_CMD,
				     CARD_PULL_CTL4, 0xFF, 0x29);
		}
	}

	retval = rtsx_send_cmd(chip, MS_CARD, 100);
	if (retval < 0) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	return STATUS_SUCCESS;
}

static int ms_prepare_reset(struct rtsx_chip *chip)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int retval;
	u8 oc_mask = 0;

	ms_card->ms_type = 0;
	ms_card->check_ms_flow = 0;
	ms_card->switch_8bit_fail = 0;
	ms_card->delay_write.delay_write_flag = 0;

	ms_card->pro_under_formatting = 0;

	retval = ms_power_off_card3v3(chip);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	if (!chip->ft2_fast_mode)
		wait_timeout(250);

	retval = enable_card_clock(chip, MS_CARD);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	if (chip->asic_code) {
		retval = ms_pull_ctl_enable(chip);
		if (retval != STATUS_SUCCESS) {
			TRACE_RET(chip, STATUS_FAIL);
		}
	} else {
		RTSX_WRITE_REG(chip, FPGA_PULL_CTL, FPGA_MS_PULL_CTL_BIT | 0x20, 0);
	}

	if (!chip->ft2_fast_mode) {
		retval = card_power_on(chip, MS_CARD);
		if (retval != STATUS_SUCCESS) {
			TRACE_RET(chip, STATUS_FAIL);
		}
		wait_timeout(150);

#ifdef SUPPORT_OCP
		if (CHECK_LUN_MODE(chip, SD_MS_2LUN)) {
			oc_mask = MS_OC_NOW | MS_OC_EVER;
		} else {
			oc_mask = SD_OC_NOW | SD_OC_EVER;
		}
		if (chip->ocp_stat & oc_mask) {
			RTSX_DEBUGP("Over current, OCPSTAT is 0x%x\n",
				     chip->ocp_stat);
			TRACE_RET(chip, STATUS_FAIL);
		}
#endif
	}

	RTSX_WRITE_REG(chip, CARD_OE, MS_OUTPUT_EN, MS_OUTPUT_EN);

	if (chip->asic_code) {
		RTSX_WRITE_REG(chip, MS_CFG, 0xFF,
			SAMPLE_TIME_RISING | PUSH_TIME_DEFAULT |
			NO_EXTEND_TOGGLE | MS_BUS_WIDTH_1);
	} else {
		RTSX_WRITE_REG(chip, MS_CFG, 0xFF,
			SAMPLE_TIME_FALLING | PUSH_TIME_DEFAULT |
			NO_EXTEND_TOGGLE | MS_BUS_WIDTH_1);
	}
	RTSX_WRITE_REG(chip, MS_TRANS_CFG, 0xFF, NO_WAIT_INT | NO_AUTO_READ_INT_REG);
	RTSX_WRITE_REG(chip, CARD_STOP, MS_STOP | MS_CLR_ERR, MS_STOP | MS_CLR_ERR);

	retval = ms_set_init_para(chip);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	return STATUS_SUCCESS;
}

static int ms_identify_media_type(struct rtsx_chip *chip, int switch_8bit_bus)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int retval, i;
	u8 val;

	retval = ms_set_rw_reg_addr(chip, Pro_StatusReg, 6, SystemParm, 1);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	for (i = 0; i < MS_MAX_RETRY_COUNT; i++) {
		retval = ms_transfer_tpc(chip, MS_TM_READ_BYTES, READ_REG, 6, NO_WAIT_INT);
		if (retval == STATUS_SUCCESS) {
			break;
		}
	}
	if (i == MS_MAX_RETRY_COUNT) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	RTSX_READ_REG(chip, PPBUF_BASE2 + 2, &val);
	RTSX_DEBUGP("Type register: 0x%x\n", val);
	if (val != 0x01) {
		if (val != 0x02) {
			ms_card->check_ms_flow = 1;
		}
		TRACE_RET(chip, STATUS_FAIL);
	}

	RTSX_READ_REG(chip, PPBUF_BASE2 + 4, &val);
	RTSX_DEBUGP("Category register: 0x%x\n", val);
	if (val != 0) {
		ms_card->check_ms_flow = 1;
		TRACE_RET(chip, STATUS_FAIL);
	}

	RTSX_READ_REG(chip, PPBUF_BASE2 + 5, &val);
	RTSX_DEBUGP("Class register: 0x%x\n", val);
	if (val == 0) {
		RTSX_READ_REG(chip, PPBUF_BASE2, &val);
		if (val & WRT_PRTCT) {
			chip->card_wp |= MS_CARD;
		} else {
			chip->card_wp &= ~MS_CARD;
		}
	} else if ((val == 0x01) || (val == 0x02) || (val == 0x03)) {
		chip->card_wp |= MS_CARD;
	} else {
		ms_card->check_ms_flow = 1;
		TRACE_RET(chip, STATUS_FAIL);
	}

	ms_card->ms_type |= TYPE_MSPRO;

	RTSX_READ_REG(chip, PPBUF_BASE2 + 3, &val);
	RTSX_DEBUGP("IF Mode register: 0x%x\n", val);
	if (val == 0) {
		ms_card->ms_type &= 0x0F;
	} else if (val == 7) {
		if (switch_8bit_bus) {
			ms_card->ms_type |= MS_HG;
		} else {
			ms_card->ms_type &= 0x0F;
		}
	} else {
		TRACE_RET(chip, STATUS_FAIL);
	}

	return STATUS_SUCCESS;
}

static int ms_confirm_cpu_startup(struct rtsx_chip *chip)
{
	int retval, i, k;
	u8 val;

	/* Confirm CPU StartUp */
	k = 0;
	do {
		if (detect_card_cd(chip, MS_CARD) != STATUS_SUCCESS) {
			ms_set_err_code(chip, MS_NO_CARD);
			TRACE_RET(chip, STATUS_FAIL);
		}

		for (i = 0; i < MS_MAX_RETRY_COUNT; i++) {
			retval = ms_read_bytes(chip, GET_INT, 1, NO_WAIT_INT, &val, 1);
			if (retval == STATUS_SUCCESS) {
				break;
			}
		}
		if (i == MS_MAX_RETRY_COUNT) {
			TRACE_RET(chip, STATUS_FAIL);
		}

		if (k > 100) {
			TRACE_RET(chip, STATUS_FAIL);
		}
		k++;
		wait_timeout(100);
	} while (!(val & INT_REG_CED));

	for (i = 0; i < MS_MAX_RETRY_COUNT; i++) {
		retval = ms_read_bytes(chip, GET_INT, 1, NO_WAIT_INT, &val, 1);
		if (retval == STATUS_SUCCESS)
			break;
	}
	if (i == MS_MAX_RETRY_COUNT) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	if (val & INT_REG_ERR) {
		if (val & INT_REG_CMDNK) {
			chip->card_wp |= (MS_CARD);
		} else {
			TRACE_RET(chip, STATUS_FAIL);
		}
	}
	/* --  end confirm CPU startup */

	return STATUS_SUCCESS;
}

static int ms_switch_parallel_bus(struct rtsx_chip *chip)
{
	int retval, i;
	u8 data[2];

	data[0] = PARALLEL_4BIT_IF;
	data[1] = 0;
	for (i = 0; i < MS_MAX_RETRY_COUNT; i++) {
		retval = ms_write_bytes(chip, WRITE_REG, 1, NO_WAIT_INT, data, 2);
		if (retval == STATUS_SUCCESS)
			break;
	}
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	return STATUS_SUCCESS;
}

static int ms_switch_8bit_bus(struct rtsx_chip *chip)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int retval, i;
	u8 data[2];

	data[0] = PARALLEL_8BIT_IF;
	data[1] = 0;
	for (i = 0; i < MS_MAX_RETRY_COUNT; i++) {
		retval = ms_write_bytes(chip, WRITE_REG, 1, NO_WAIT_INT, data, 2);
		if (retval == STATUS_SUCCESS) {
			break;
		}
	}
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	RTSX_WRITE_REG(chip, MS_CFG, 0x98, MS_BUS_WIDTH_8 | SAMPLE_TIME_FALLING);
	ms_card->ms_type |= MS_8BIT;
	retval = ms_set_init_para(chip);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	for (i = 0; i < MS_MAX_RETRY_COUNT; i++) {
		retval = ms_transfer_tpc(chip, MS_TM_READ_BYTES, GET_INT, 1, NO_WAIT_INT);
		if (retval != STATUS_SUCCESS) {
			TRACE_RET(chip, STATUS_FAIL);
		}
	}

	return STATUS_SUCCESS;
}

static int ms_pro_reset_flow(struct rtsx_chip *chip, int switch_8bit_bus)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int retval, i;

	for (i = 0; i < 3; i++) {
		retval = ms_prepare_reset(chip);
		if (retval != STATUS_SUCCESS) {
			TRACE_RET(chip, STATUS_FAIL);
		}

		retval = ms_identify_media_type(chip, switch_8bit_bus);
		if (retval != STATUS_SUCCESS) {
			TRACE_RET(chip, STATUS_FAIL);
		}

		retval = ms_confirm_cpu_startup(chip);
		if (retval != STATUS_SUCCESS) {
			TRACE_RET(chip, STATUS_FAIL);
		}

		retval = ms_switch_parallel_bus(chip);
		if (retval != STATUS_SUCCESS) {
			if (detect_card_cd(chip, MS_CARD) != STATUS_SUCCESS) {
				ms_set_err_code(chip, MS_NO_CARD);
				TRACE_RET(chip, STATUS_FAIL);
			}
			continue;
		} else {
			break;
		}
	}

	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	/* Switch MS-PRO into Parallel mode */
	RTSX_WRITE_REG(chip, MS_CFG, 0x18, MS_BUS_WIDTH_4);
	RTSX_WRITE_REG(chip, MS_CFG, PUSH_TIME_ODD, PUSH_TIME_ODD);

	retval = ms_set_init_para(chip);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	/* If MSPro HG Card, We shall try to switch to 8-bit bus */
	if (CHK_MSHG(ms_card) && chip->support_ms_8bit && switch_8bit_bus) {
		retval = ms_switch_8bit_bus(chip);
		if (retval != STATUS_SUCCESS) {
			ms_card->switch_8bit_fail = 1;
			TRACE_RET(chip, STATUS_FAIL);
		}
	}

	return STATUS_SUCCESS;
}

#ifdef XC_POWERCLASS
static int msxc_change_power(struct rtsx_chip *chip, u8 mode)
{
	int retval;
	u8 buf[6];

	ms_cleanup_work(chip);

	retval = ms_set_rw_reg_addr(chip, 0, 0, Pro_DataCount1, 6);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	buf[0] = 0;
	buf[1] = mode;
	buf[2] = 0;
	buf[3] = 0;
	buf[4] = 0;
	buf[5] = 0;

	retval = ms_write_bytes(chip, PRO_WRITE_REG , 6, NO_WAIT_INT, buf, 6);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	retval = ms_send_cmd(chip, XC_CHG_POWER, WAIT_INT);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	RTSX_READ_REG(chip, MS_TRANS_CFG, buf);
	if (buf[0] & (MS_INT_CMDNK | MS_INT_ERR)) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	return STATUS_SUCCESS;
}
#endif

static int ms_read_attribute_info(struct rtsx_chip *chip)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int retval, i;
	u8 val, *buf, class_code, device_type, sub_class, data[16];
	u16 total_blk = 0, blk_size = 0;
#ifdef SUPPORT_MSXC
	u32 xc_total_blk = 0, xc_blk_size = 0;
#endif
	u32 sys_info_addr = 0, sys_info_size;
#ifdef SUPPORT_PCGL_1P18
	u32 model_name_addr = 0, model_name_size;
	int found_sys_info = 0, found_model_name = 0;
#endif

	retval = ms_set_rw_reg_addr(chip, Pro_IntReg, 2, Pro_SystemParm, 7);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	if (CHK_MS8BIT(ms_card)) {
		data[0] = PARALLEL_8BIT_IF;
	} else {
		data[0] = PARALLEL_4BIT_IF;
	}
	data[1] = 0;

	data[2] = 0x40;
	data[3] = 0;
	data[4] = 0;
	data[5] = 0;
	data[6] = 0;
	data[7] = 0;

	for (i = 0; i < MS_MAX_RETRY_COUNT; i++) {
		retval = ms_write_bytes(chip, PRO_WRITE_REG, 7, NO_WAIT_INT, data, 8);
		if (retval == STATUS_SUCCESS) {
			break;
		}
	}
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	buf = kmalloc(64 * 512, GFP_KERNEL);
	if (buf == NULL) {
		TRACE_RET(chip, STATUS_ERROR);
	}

	for (i = 0; i < MS_MAX_RETRY_COUNT; i++) {
		retval = ms_send_cmd(chip, PRO_READ_ATRB, WAIT_INT);
		if (retval != STATUS_SUCCESS) {
			continue;
		}
		retval = rtsx_read_register(chip, MS_TRANS_CFG, &val);
		if (retval != STATUS_SUCCESS) {
			kfree(buf);
			TRACE_RET(chip, STATUS_FAIL);
		}
		if (!(val & MS_INT_BREQ)) {
			kfree(buf);
			TRACE_RET(chip, STATUS_FAIL);
		}
		retval = ms_transfer_data(chip, MS_TM_AUTO_READ, PRO_READ_LONG_DATA,
				0x40, WAIT_INT, 0, 0, buf, 64 * 512);
		if (retval == STATUS_SUCCESS) {
			break;
		} else {
			rtsx_clear_ms_error(chip);
		}
	}
	if (retval != STATUS_SUCCESS) {
		kfree(buf);
		TRACE_RET(chip, STATUS_FAIL);
	}

	i = 0;
	do {
		retval = rtsx_read_register(chip, MS_TRANS_CFG, &val);
		if (retval != STATUS_SUCCESS) {
			kfree(buf);
			TRACE_RET(chip, STATUS_FAIL);
		}

		if ((val & MS_INT_CED) || !(val & MS_INT_BREQ))
			break;

		retval = ms_transfer_tpc(chip, MS_TM_NORMAL_READ, PRO_READ_LONG_DATA, 0, WAIT_INT);
		if (retval != STATUS_SUCCESS) {
			kfree(buf);
			TRACE_RET(chip, STATUS_FAIL);
		}

		i++;
	} while (i < 1024);

	if (retval != STATUS_SUCCESS) {
		kfree(buf);
		TRACE_RET(chip, STATUS_FAIL);
	}

	if ((buf[0] != 0xa5) && (buf[1] != 0xc3)) {
		/* Signature code is wrong */
		kfree(buf);
		TRACE_RET(chip, STATUS_FAIL);
	}

	if ((buf[4] < 1) || (buf[4] > 12)) {
		kfree(buf);
		TRACE_RET(chip, STATUS_FAIL);
	}

	for (i = 0; i < buf[4]; i++) {
		int cur_addr_off = 16 + i * 12;

#ifdef SUPPORT_MSXC
		if ((buf[cur_addr_off + 8] == 0x10) || (buf[cur_addr_off + 8] == 0x13))
#else
		if (buf[cur_addr_off + 8] == 0x10)
#endif
		{
			sys_info_addr = ((u32)buf[cur_addr_off + 0] << 24) |
				((u32)buf[cur_addr_off + 1] << 16) |
				((u32)buf[cur_addr_off + 2] << 8) | buf[cur_addr_off + 3];
			sys_info_size = ((u32)buf[cur_addr_off + 4] << 24) |
				((u32)buf[cur_addr_off + 5] << 16) |
				((u32)buf[cur_addr_off + 6] << 8) | buf[cur_addr_off + 7];
			RTSX_DEBUGP("sys_info_addr = 0x%x, sys_info_size = 0x%x\n",
					sys_info_addr, sys_info_size);
			if (sys_info_size != 96)  {
				kfree(buf);
				TRACE_RET(chip, STATUS_FAIL);
			}
			if (sys_info_addr < 0x1A0) {
				kfree(buf);
				TRACE_RET(chip, STATUS_FAIL);
			}
			if ((sys_info_size + sys_info_addr) > 0x8000) {
				kfree(buf);
				TRACE_RET(chip, STATUS_FAIL);
			}

#ifdef SUPPORT_MSXC
			if (buf[cur_addr_off + 8] == 0x13) {
				ms_card->ms_type |= MS_XC;
			}
#endif
#ifdef SUPPORT_PCGL_1P18
			found_sys_info = 1;
#else
			break;
#endif
		}
#ifdef SUPPORT_PCGL_1P18
		if (buf[cur_addr_off + 8] == 0x15) {
			model_name_addr = ((u32)buf[cur_addr_off + 0] << 24) |
				((u32)buf[cur_addr_off + 1] << 16) |
				((u32)buf[cur_addr_off + 2] << 8) | buf[cur_addr_off + 3];
			model_name_size = ((u32)buf[cur_addr_off + 4] << 24) |
				((u32)buf[cur_addr_off + 5] << 16) |
				((u32)buf[cur_addr_off + 6] << 8) | buf[cur_addr_off + 7];
			RTSX_DEBUGP("model_name_addr = 0x%x, model_name_size = 0x%x\n",
					model_name_addr, model_name_size);
			if (model_name_size != 48)  {
				kfree(buf);
				TRACE_RET(chip, STATUS_FAIL);
			}
			if (model_name_addr < 0x1A0) {
				kfree(buf);
				TRACE_RET(chip, STATUS_FAIL);
			}
			if ((model_name_size + model_name_addr) > 0x8000) {
				kfree(buf);
				TRACE_RET(chip, STATUS_FAIL);
			}

			found_model_name = 1;
		}

		if (found_sys_info && found_model_name)
			break;
#endif
	}

	if (i == buf[4]) {
		kfree(buf);
		TRACE_RET(chip, STATUS_FAIL);
	}

	class_code =  buf[sys_info_addr + 0];
	device_type = buf[sys_info_addr + 56];
	sub_class = buf[sys_info_addr + 46];
#ifdef SUPPORT_MSXC
	if (CHK_MSXC(ms_card)) {
		xc_total_blk = ((u32)buf[sys_info_addr + 6] << 24) |
				((u32)buf[sys_info_addr + 7] << 16) |
				((u32)buf[sys_info_addr + 8] << 8) |
				buf[sys_info_addr + 9];
		xc_blk_size = ((u32)buf[sys_info_addr + 32] << 24) |
				((u32)buf[sys_info_addr + 33] << 16) |
				((u32)buf[sys_info_addr + 34] << 8) |
				buf[sys_info_addr + 35];
		RTSX_DEBUGP("xc_total_blk = 0x%x, xc_blk_size = 0x%x\n", xc_total_blk, xc_blk_size);
	} else {
		total_blk = ((u16)buf[sys_info_addr + 6] << 8) | buf[sys_info_addr + 7];
		blk_size = ((u16)buf[sys_info_addr + 2] << 8) | buf[sys_info_addr + 3];
		RTSX_DEBUGP("total_blk = 0x%x, blk_size = 0x%x\n", total_blk, blk_size);
	}
#else
	total_blk = ((u16)buf[sys_info_addr + 6] << 8) | buf[sys_info_addr + 7];
	blk_size = ((u16)buf[sys_info_addr + 2] << 8) | buf[sys_info_addr + 3];
	RTSX_DEBUGP("total_blk = 0x%x, blk_size = 0x%x\n", total_blk, blk_size);
#endif

	RTSX_DEBUGP("class_code = 0x%x, device_type = 0x%x, sub_class = 0x%x\n",
			class_code, device_type, sub_class);

	memcpy(ms_card->raw_sys_info, buf + sys_info_addr, 96);
#ifdef SUPPORT_PCGL_1P18
	memcpy(ms_card->raw_model_name, buf + model_name_addr, 48);
#endif

	kfree(buf);

#ifdef SUPPORT_MSXC
	if (CHK_MSXC(ms_card)) {
		if (class_code != 0x03) {
			TRACE_RET(chip, STATUS_FAIL);
		}
	} else {
		if (class_code != 0x02) {
			TRACE_RET(chip, STATUS_FAIL);
		}
	}
#else
	if (class_code != 0x02) {
		TRACE_RET(chip, STATUS_FAIL);
	}
#endif

	if (device_type != 0x00) {
		if ((device_type == 0x01) || (device_type == 0x02) ||
				(device_type == 0x03)) {
			chip->card_wp |= MS_CARD;
		} else {
			TRACE_RET(chip, STATUS_FAIL);
		}
	}

	if (sub_class & 0xC0) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	RTSX_DEBUGP("class_code: 0x%x, device_type: 0x%x, sub_class: 0x%x\n",
		class_code, device_type, sub_class);

#ifdef SUPPORT_MSXC
	if (CHK_MSXC(ms_card)) {
		chip->capacity[chip->card2lun[MS_CARD]] =
			ms_card->capacity = xc_total_blk * xc_blk_size;
	} else {
		chip->capacity[chip->card2lun[MS_CARD]] =
			ms_card->capacity = total_blk * blk_size;
	}
#else
	chip->capacity[chip->card2lun[MS_CARD]] = ms_card->capacity = total_blk * blk_size;
#endif

	return STATUS_SUCCESS;
}

#ifdef SUPPORT_MAGIC_GATE
static int mg_set_tpc_para_sub(struct rtsx_chip *chip, int type, u8 mg_entry_num);
#endif

static int reset_ms_pro(struct rtsx_chip *chip)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int retval;
#ifdef XC_POWERCLASS
	u8 change_power_class;

	if (chip->ms_power_class_en & 0x02)
		change_power_class = 2;
	else if (chip->ms_power_class_en & 0x01)
		change_power_class = 1;
	else
		change_power_class = 0;
#endif

#ifdef XC_POWERCLASS
Retry:
#endif
	retval = ms_pro_reset_flow(chip, 1);
	if (retval != STATUS_SUCCESS) {
		if (ms_card->switch_8bit_fail) {
			retval = ms_pro_reset_flow(chip, 0);
			if (retval != STATUS_SUCCESS) {
				TRACE_RET(chip, STATUS_FAIL);
			}
		} else {
			TRACE_RET(chip, STATUS_FAIL);
		}
	}

	retval = ms_read_attribute_info(chip);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

#ifdef XC_POWERCLASS
	if (CHK_HG8BIT(ms_card)) {
		change_power_class = 0;
	}

	if (change_power_class && CHK_MSXC(ms_card)) {
		u8 power_class_en = chip->ms_power_class_en;

		RTSX_DEBUGP("power_class_en = 0x%x\n", power_class_en);
		RTSX_DEBUGP("change_power_class = %d\n", change_power_class);

		if (change_power_class) {
			power_class_en &= (1 << (change_power_class - 1));
		} else {
			power_class_en = 0;
		}

		if (power_class_en) {
			u8 power_class_mode = (ms_card->raw_sys_info[46] & 0x18) >> 3;
			RTSX_DEBUGP("power_class_mode = 0x%x", power_class_mode);
			if (change_power_class > power_class_mode)
				change_power_class = power_class_mode;
			if (change_power_class) {
				retval = msxc_change_power(chip, change_power_class);
				if (retval != STATUS_SUCCESS) {
					change_power_class--;
					goto Retry;
				}
			}
		}
	}
#endif

#ifdef SUPPORT_MAGIC_GATE
	retval = mg_set_tpc_para_sub(chip, 0, 0);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}
#endif

	if (CHK_HG8BIT(ms_card)) {
		chip->card_bus_width[chip->card2lun[MS_CARD]] = 8;
	} else {
		chip->card_bus_width[chip->card2lun[MS_CARD]] = 4;
	}

	return STATUS_SUCCESS;
}

static int ms_read_status_reg(struct rtsx_chip *chip)
{
	int retval;
	u8 val[2];

	retval = ms_set_rw_reg_addr(chip, StatusReg0, 2, 0, 0);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	retval = ms_read_bytes(chip, READ_REG, 2, NO_WAIT_INT, val, 2);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	if (val[1] & (STS_UCDT | STS_UCEX | STS_UCFG)) {
		ms_set_err_code(chip, MS_FLASH_READ_ERROR);
		TRACE_RET(chip, STATUS_FAIL);
	}

	return STATUS_SUCCESS;
}


static int ms_read_extra_data(struct rtsx_chip *chip,
		u16 block_addr, u8 page_num, u8 *buf, int buf_len)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int retval, i;
	u8 val, data[10];

	retval = ms_set_rw_reg_addr(chip, OverwriteFlag, MS_EXTRA_SIZE, SystemParm, 6);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	if (CHK_MS4BIT(ms_card)) {
		/* Parallel interface */
		data[0] = 0x88;
	} else {
		/* Serial interface */
		data[0] = 0x80;
	}
	data[1] = 0;
	data[2] = (u8)(block_addr >> 8);
	data[3] = (u8)block_addr;
	data[4] = 0x40;
	data[5] = page_num;

	for (i = 0; i < MS_MAX_RETRY_COUNT; i++) {
		retval = ms_write_bytes(chip, WRITE_REG, 6, NO_WAIT_INT, data, 6);
		if (retval == STATUS_SUCCESS)
			break;
	}
	if (i == MS_MAX_RETRY_COUNT) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	ms_set_err_code(chip, MS_NO_ERROR);

	for (i = 0; i < MS_MAX_RETRY_COUNT; i++) {
		retval = ms_send_cmd(chip, BLOCK_READ, WAIT_INT);
		if (retval == STATUS_SUCCESS)
			break;
	}
	if (i == MS_MAX_RETRY_COUNT) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	ms_set_err_code(chip, MS_NO_ERROR);
	retval = ms_read_bytes(chip, GET_INT, 1, NO_WAIT_INT, &val, 1);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}
	if (val & INT_REG_CMDNK) {
		ms_set_err_code(chip, MS_CMD_NK);
		TRACE_RET(chip, STATUS_FAIL);
	}
	if (val & INT_REG_CED) {
		if (val & INT_REG_ERR) {
			retval = ms_read_status_reg(chip);
			if (retval != STATUS_SUCCESS) {
				TRACE_RET(chip, STATUS_FAIL);
			}
			retval = ms_set_rw_reg_addr(chip, OverwriteFlag, MS_EXTRA_SIZE, SystemParm, 6);
			if (retval != STATUS_SUCCESS) {
				TRACE_RET(chip, STATUS_FAIL);
			}
		}
	}

	retval = ms_read_bytes(chip, READ_REG, MS_EXTRA_SIZE, NO_WAIT_INT, data, MS_EXTRA_SIZE);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	if (buf && buf_len) {
		if (buf_len > MS_EXTRA_SIZE)
			buf_len = MS_EXTRA_SIZE;
		memcpy(buf, data, buf_len);
	}

	return STATUS_SUCCESS;
}

static int ms_write_extra_data(struct rtsx_chip *chip,
		u16 block_addr, u8 page_num, u8 *buf, int buf_len)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int retval, i;
	u8 val, data[16];

	if (!buf || (buf_len < MS_EXTRA_SIZE)) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	retval = ms_set_rw_reg_addr(chip, OverwriteFlag, MS_EXTRA_SIZE, SystemParm, 6 + MS_EXTRA_SIZE);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	if (CHK_MS4BIT(ms_card)) {
		data[0] = 0x88;
	} else {
		data[0] = 0x80;
	}
	data[1] = 0;
	data[2] = (u8)(block_addr >> 8);
	data[3] = (u8)block_addr;
	data[4] = 0x40;
	data[5] = page_num;

	for (i = 6; i < MS_EXTRA_SIZE + 6; i++) {
		data[i] = buf[i - 6];
	}

	retval = ms_write_bytes(chip, WRITE_REG , (6+MS_EXTRA_SIZE), NO_WAIT_INT, data, 16);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	retval = ms_send_cmd(chip, BLOCK_WRITE, WAIT_INT);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	ms_set_err_code(chip, MS_NO_ERROR);
	retval = ms_read_bytes(chip, GET_INT, 1, NO_WAIT_INT, &val, 1);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}
	if (val & INT_REG_CMDNK) {
		ms_set_err_code(chip, MS_CMD_NK);
		TRACE_RET(chip, STATUS_FAIL);
	}
	if (val & INT_REG_CED) {
		if (val & INT_REG_ERR) {
			ms_set_err_code(chip, MS_FLASH_WRITE_ERROR);
			TRACE_RET(chip, STATUS_FAIL);
		}
	}

	return STATUS_SUCCESS;
}


static int ms_read_page(struct rtsx_chip *chip, u16 block_addr, u8 page_num)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int retval;
	u8 val, data[6];

	retval = ms_set_rw_reg_addr(chip, OverwriteFlag, MS_EXTRA_SIZE, SystemParm, 6);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	if (CHK_MS4BIT(ms_card)) {
		data[0] = 0x88;
	} else {
		data[0] = 0x80;
	}
	data[1] = 0;
	data[2] = (u8)(block_addr >> 8);
	data[3] = (u8)block_addr;
	data[4] = 0x20;
	data[5] = page_num;

	retval = ms_write_bytes(chip, WRITE_REG , 6, NO_WAIT_INT, data, 6);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	retval = ms_send_cmd(chip, BLOCK_READ, WAIT_INT);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	ms_set_err_code(chip, MS_NO_ERROR);
	retval = ms_read_bytes(chip, GET_INT, 1, NO_WAIT_INT, &val, 1);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}
	if (val & INT_REG_CMDNK) {
		ms_set_err_code(chip, MS_CMD_NK);
		TRACE_RET(chip, STATUS_FAIL);
	}

	if (val & INT_REG_CED) {
		if (val & INT_REG_ERR) {
			if (!(val & INT_REG_BREQ)) {
				ms_set_err_code(chip,  MS_FLASH_READ_ERROR);
				TRACE_RET(chip, STATUS_FAIL);
			}
			retval = ms_read_status_reg(chip);
			if (retval != STATUS_SUCCESS) {
				ms_set_err_code(chip,  MS_FLASH_WRITE_ERROR);
			}
		} else {
			if (!(val & INT_REG_BREQ)) {
				ms_set_err_code(chip, MS_BREQ_ERROR);
				TRACE_RET(chip, STATUS_FAIL);
			}
		}
	}

	retval = ms_transfer_tpc(chip, MS_TM_NORMAL_READ, READ_PAGE_DATA, 0, NO_WAIT_INT);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	if (ms_check_err_code(chip, MS_FLASH_WRITE_ERROR)) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	return STATUS_SUCCESS;
}


static int ms_set_bad_block(struct rtsx_chip *chip, u16 phy_blk)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int retval;
	u8 val, data[8], extra[MS_EXTRA_SIZE];

	retval = ms_read_extra_data(chip, phy_blk, 0, extra, MS_EXTRA_SIZE);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	retval = ms_set_rw_reg_addr(chip, OverwriteFlag, MS_EXTRA_SIZE, SystemParm, 7);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	ms_set_err_code(chip, MS_NO_ERROR);

	if (CHK_MS4BIT(ms_card)) {
		data[0] = 0x88;
	} else {
		data[0] = 0x80;
	}
	data[1] = 0;
	data[2] = (u8)(phy_blk >> 8);
	data[3] = (u8)phy_blk;
	data[4] = 0x80;
	data[5] = 0;
	data[6] = extra[0] & 0x7F;
	data[7] = 0xFF;

	retval = ms_write_bytes(chip, WRITE_REG , 7, NO_WAIT_INT, data, 7);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	retval = ms_send_cmd(chip, BLOCK_WRITE, WAIT_INT);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	ms_set_err_code(chip, MS_NO_ERROR);
	retval = ms_read_bytes(chip, GET_INT, 1, NO_WAIT_INT, &val, 1);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	if (val & INT_REG_CMDNK) {
		ms_set_err_code(chip, MS_CMD_NK);
		TRACE_RET(chip, STATUS_FAIL);
	}

	if (val & INT_REG_CED) {
		if (val & INT_REG_ERR) {
			ms_set_err_code(chip, MS_FLASH_WRITE_ERROR);
			TRACE_RET(chip, STATUS_FAIL);
		}
	}

	return STATUS_SUCCESS;
}


static int ms_erase_block(struct rtsx_chip *chip, u16 phy_blk)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int retval, i = 0;
	u8 val, data[6];

	retval = ms_set_rw_reg_addr(chip, OverwriteFlag, MS_EXTRA_SIZE, SystemParm, 6);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	ms_set_err_code(chip, MS_NO_ERROR);

	if (CHK_MS4BIT(ms_card)) {
		data[0] = 0x88;
	} else {
		data[0] = 0x80;
	}
	data[1] = 0;
	data[2] = (u8)(phy_blk >> 8);
	data[3] = (u8)phy_blk;
	data[4] = 0;
	data[5] = 0;

	retval = ms_write_bytes(chip, WRITE_REG, 6, NO_WAIT_INT, data, 6);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

ERASE_RTY:
	retval = ms_send_cmd(chip, BLOCK_ERASE, WAIT_INT);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	ms_set_err_code(chip, MS_NO_ERROR);
	retval = ms_read_bytes(chip, GET_INT, 1, NO_WAIT_INT, &val, 1);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	if (val & INT_REG_CMDNK) {
		if (i < 3) {
			i++;
			goto ERASE_RTY;
		}

		ms_set_err_code(chip, MS_CMD_NK);
		ms_set_bad_block(chip, phy_blk);
		TRACE_RET(chip, STATUS_FAIL);
	}

	if (val & INT_REG_CED) {
		if (val & INT_REG_ERR) {
			ms_set_err_code(chip, MS_FLASH_WRITE_ERROR);
			TRACE_RET(chip, STATUS_FAIL);
		}
	}

	return STATUS_SUCCESS;
}


static void ms_set_page_status(u16 log_blk, u8 type, u8 *extra, int extra_len)
{
	if (!extra || (extra_len < MS_EXTRA_SIZE)) {
		return;
	}

	memset(extra, 0xFF, MS_EXTRA_SIZE);

	if (type == setPS_NG) {
		/* set page status as 1:NG,and block status keep 1:OK */
		extra[0] = 0xB8;
	} else {
		/* set page status as 0:Data Error,and block status keep 1:OK */
		extra[0] = 0x98;
	}

	extra[2] = (u8)(log_blk >> 8);
	extra[3] = (u8)log_blk;
}

static int ms_init_page(struct rtsx_chip *chip, u16 phy_blk, u16 log_blk, u8 start_page, u8 end_page)
{
	int retval;
	u8 extra[MS_EXTRA_SIZE], i;

	memset(extra, 0xff, MS_EXTRA_SIZE);

	extra[0] = 0xf8;	/* Block, page OK, data erased */
	extra[1] = 0xff;
	extra[2] = (u8)(log_blk >> 8);
	extra[3] = (u8)log_blk;

	for (i = start_page; i < end_page; i++) {
		if (detect_card_cd(chip, MS_CARD) != STATUS_SUCCESS) {
			ms_set_err_code(chip, MS_NO_CARD);
			TRACE_RET(chip, STATUS_FAIL);
		}

		retval = ms_write_extra_data(chip, phy_blk, i, extra, MS_EXTRA_SIZE);
		if (retval != STATUS_SUCCESS) {
			TRACE_RET(chip, STATUS_FAIL);
		}
	}

	return STATUS_SUCCESS;
}

static int ms_copy_page(struct rtsx_chip *chip, u16 old_blk, u16 new_blk,
		u16 log_blk, u8 start_page, u8 end_page)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int retval, rty_cnt, uncorrect_flag = 0;
	u8 extra[MS_EXTRA_SIZE], val, i, j, data[16];

	RTSX_DEBUGP("Copy page from 0x%x to 0x%x, logical block is 0x%x\n",
		old_blk, new_blk, log_blk);
	RTSX_DEBUGP("start_page = %d, end_page = %d\n", start_page, end_page);

	retval = ms_read_extra_data(chip, new_blk, 0, extra, MS_EXTRA_SIZE);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	retval = ms_read_status_reg(chip);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	RTSX_READ_REG(chip, PPBUF_BASE2, &val);

	if (val & BUF_FULL) {
		retval = ms_send_cmd(chip, CLEAR_BUF, WAIT_INT);
		if (retval != STATUS_SUCCESS) {
			TRACE_RET(chip, STATUS_FAIL);
		}

		retval = ms_read_bytes(chip, GET_INT, 1, NO_WAIT_INT, &val, 1);
		if (retval != STATUS_SUCCESS) {
			TRACE_RET(chip, STATUS_FAIL);
		}

		if (!(val & INT_REG_CED)) {
			ms_set_err_code(chip, MS_FLASH_WRITE_ERROR);
			TRACE_RET(chip, STATUS_FAIL);
		}
	}

	for (i = start_page; i < end_page; i++) {
		if (detect_card_cd(chip, MS_CARD) != STATUS_SUCCESS) {
			ms_set_err_code(chip, MS_NO_CARD);
			TRACE_RET(chip, STATUS_FAIL);
		}

		ms_read_extra_data(chip, old_blk, i, extra, MS_EXTRA_SIZE);

		retval = ms_set_rw_reg_addr(chip, OverwriteFlag, MS_EXTRA_SIZE, SystemParm, 6);
		if (retval != STATUS_SUCCESS) {
			TRACE_RET(chip, STATUS_FAIL);
		}

		ms_set_err_code(chip, MS_NO_ERROR);

		if (CHK_MS4BIT(ms_card)) {
			data[0] = 0x88;
		} else {
			data[0] = 0x80;
		}
		data[1] = 0;
		data[2] = (u8)(old_blk >> 8);
		data[3] = (u8)old_blk;
		data[4] = 0x20;
		data[5] = i;

		retval = ms_write_bytes(chip, WRITE_REG , 6, NO_WAIT_INT, data, 6);
		if (retval != STATUS_SUCCESS) {
			TRACE_RET(chip, STATUS_FAIL);
		}

		retval = ms_send_cmd(chip, BLOCK_READ, WAIT_INT);
		if (retval != STATUS_SUCCESS) {
			TRACE_RET(chip, STATUS_FAIL);
		}

		ms_set_err_code(chip, MS_NO_ERROR);
		retval = ms_read_bytes(chip, GET_INT, 1, NO_WAIT_INT, &val, 1);
		if (retval != STATUS_SUCCESS) {
			TRACE_RET(chip, STATUS_FAIL);
		}

		if (val & INT_REG_CMDNK) {
			ms_set_err_code(chip, MS_CMD_NK);
			TRACE_RET(chip, STATUS_FAIL);
		}

		if (val & INT_REG_CED) {
			if (val & INT_REG_ERR) {
				retval = ms_read_status_reg(chip);
				if (retval != STATUS_SUCCESS) {
					uncorrect_flag = 1;
					RTSX_DEBUGP("Uncorrectable error\n");
				} else {
					uncorrect_flag = 0;
				}

				retval = ms_transfer_tpc(chip, MS_TM_NORMAL_READ, READ_PAGE_DATA, 0, NO_WAIT_INT);
				if (retval != STATUS_SUCCESS) {
					TRACE_RET(chip, STATUS_FAIL);
				}

				if (uncorrect_flag) {
					ms_set_page_status(log_blk, setPS_NG, extra, MS_EXTRA_SIZE);
					if (i == 0) {
						extra[0] &= 0xEF;
					}
					ms_write_extra_data(chip, old_blk, i, extra, MS_EXTRA_SIZE);
					RTSX_DEBUGP("page %d : extra[0] = 0x%x\n", i, extra[0]);
					MS_SET_BAD_BLOCK_FLG(ms_card);

					ms_set_page_status(log_blk, setPS_Error, extra, MS_EXTRA_SIZE);
					ms_write_extra_data(chip, new_blk, i, extra, MS_EXTRA_SIZE);
					continue;
				}

				for (rty_cnt = 0; rty_cnt < MS_MAX_RETRY_COUNT; rty_cnt++) {
					retval = ms_transfer_tpc(chip, MS_TM_NORMAL_WRITE,
							WRITE_PAGE_DATA, 0, NO_WAIT_INT);
					if (retval == STATUS_SUCCESS) {
						break;
					}
				}
				if (rty_cnt == MS_MAX_RETRY_COUNT) {
					TRACE_RET(chip, STATUS_FAIL);
				}
			}

			if (!(val & INT_REG_BREQ)) {
				ms_set_err_code(chip, MS_BREQ_ERROR);
				TRACE_RET(chip, STATUS_FAIL);
			}
		}

		retval = ms_set_rw_reg_addr(chip, OverwriteFlag,
				MS_EXTRA_SIZE, SystemParm, (6+MS_EXTRA_SIZE));

		ms_set_err_code(chip, MS_NO_ERROR);

		if (CHK_MS4BIT(ms_card)) {
			data[0] = 0x88;
		} else {
			data[0] = 0x80;
		}
		data[1] = 0;
		data[2] = (u8)(new_blk >> 8);
		data[3] = (u8)new_blk;
		data[4] = 0x20;
		data[5] = i;

		if ((extra[0] & 0x60) != 0x60) {
			data[6] = extra[0];
		} else {
			data[6] = 0xF8;
		}
		data[6 + 1] = 0xFF;
		data[6 + 2] = (u8)(log_blk >> 8);
		data[6 + 3] = (u8)log_blk;

		for (j = 4; j <= MS_EXTRA_SIZE; j++) {
			data[6 + j] = 0xFF;
		}

		retval = ms_write_bytes(chip, WRITE_REG, (6 + MS_EXTRA_SIZE), NO_WAIT_INT, data, 16);
		if (retval != STATUS_SUCCESS) {
			TRACE_RET(chip, STATUS_FAIL);
		}

		retval = ms_send_cmd(chip, BLOCK_WRITE, WAIT_INT);
		if (retval != STATUS_SUCCESS) {
			TRACE_RET(chip, STATUS_FAIL);
		}

		ms_set_err_code(chip, MS_NO_ERROR);
		retval = ms_read_bytes(chip, GET_INT, 1, NO_WAIT_INT, &val, 1);
		if (retval != STATUS_SUCCESS) {
			TRACE_RET(chip, STATUS_FAIL);
		}

		if (val & INT_REG_CMDNK) {
			ms_set_err_code(chip, MS_CMD_NK);
			TRACE_RET(chip, STATUS_FAIL);
		}

		if (val & INT_REG_CED) {
			if (val & INT_REG_ERR) {
				ms_set_err_code(chip, MS_FLASH_WRITE_ERROR);
				TRACE_RET(chip, STATUS_FAIL);
			}
		}

		if (i == 0) {
			retval = ms_set_rw_reg_addr(chip, OverwriteFlag, MS_EXTRA_SIZE, SystemParm, 7);
			if (retval != STATUS_SUCCESS) {
				TRACE_RET(chip, STATUS_FAIL);
			}

			ms_set_err_code(chip, MS_NO_ERROR);

			if (CHK_MS4BIT(ms_card)) {
				data[0] = 0x88;
			} else {
				data[0] = 0x80;
			}
			data[1] = 0;
			data[2] = (u8)(old_blk >> 8);
			data[3] = (u8)old_blk;
			data[4] = 0x80;
			data[5] = 0;
			data[6] = 0xEF;
			data[7] = 0xFF;

			retval = ms_write_bytes(chip, WRITE_REG, 7, NO_WAIT_INT, data, 8);
			if (retval != STATUS_SUCCESS) {
				TRACE_RET(chip, STATUS_FAIL);
			}

			retval = ms_send_cmd(chip, BLOCK_WRITE, WAIT_INT);
			if (retval != STATUS_SUCCESS) {
				TRACE_RET(chip, STATUS_FAIL);
			}

			ms_set_err_code(chip, MS_NO_ERROR);
			retval = ms_read_bytes(chip, GET_INT, 1, NO_WAIT_INT, &val, 1);
			if (retval != STATUS_SUCCESS) {
				TRACE_RET(chip, STATUS_FAIL);
			}

			if (val & INT_REG_CMDNK) {
				ms_set_err_code(chip, MS_CMD_NK);
				TRACE_RET(chip, STATUS_FAIL);
			}

			if (val & INT_REG_CED) {
				if (val & INT_REG_ERR) {
					ms_set_err_code(chip, MS_FLASH_WRITE_ERROR);
					TRACE_RET(chip, STATUS_FAIL);
				}
			}
		}
	}

	return STATUS_SUCCESS;
}


static int reset_ms(struct rtsx_chip *chip)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int retval;
	u16 i, reg_addr, block_size;
	u8 val, extra[MS_EXTRA_SIZE], j, *ptr;
#ifndef SUPPORT_MAGIC_GATE
	u16 eblock_cnt;
#endif

	retval = ms_prepare_reset(chip);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	ms_card->ms_type |= TYPE_MS;

	retval = ms_send_cmd(chip, MS_RESET, NO_WAIT_INT);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	retval = ms_read_status_reg(chip);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	RTSX_READ_REG(chip, PPBUF_BASE2, &val);
	if (val & WRT_PRTCT) {
		chip->card_wp |= MS_CARD;
	} else {
		chip->card_wp &= ~MS_CARD;
	}

	i = 0;

RE_SEARCH:
	/* Search Boot Block */
	while (i < (MAX_DEFECTIVE_BLOCK + 2)) {
		if (detect_card_cd(chip, MS_CARD) != STATUS_SUCCESS) {
			ms_set_err_code(chip, MS_NO_CARD);
			TRACE_RET(chip, STATUS_FAIL);
		}

		retval = ms_read_extra_data(chip, i, 0, extra, MS_EXTRA_SIZE);
		if (retval != STATUS_SUCCESS) {
			i++;
			continue;
		}

		if (extra[0] & BLOCK_OK) {
			if (!(extra[1] & NOT_BOOT_BLOCK)) {
				ms_card->boot_block = i;
				break;
			}
		}
		i++;
	}

	if (i == (MAX_DEFECTIVE_BLOCK + 2)) {
		RTSX_DEBUGP("No boot block found!");
		TRACE_RET(chip, STATUS_FAIL);
	}

	for (j = 0; j < 3; j++) {
		retval = ms_read_page(chip, ms_card->boot_block, j);
		if (retval != STATUS_SUCCESS) {
			if (ms_check_err_code(chip, MS_FLASH_WRITE_ERROR)) {
				i = ms_card->boot_block + 1;
				ms_set_err_code(chip, MS_NO_ERROR);
				goto RE_SEARCH;
			}
		}
	}

	retval = ms_read_page(chip, ms_card->boot_block, 0);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	/* Read MS system information as sys_info */
	rtsx_init_cmd(chip);

	for (i = 0; i < 96; i++) {
		rtsx_add_cmd(chip, READ_REG_CMD, PPBUF_BASE2 + 0x1A0 + i, 0, 0);
	}

	retval = rtsx_send_cmd(chip, MS_CARD, 100);
	if (retval < 0) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	ptr = rtsx_get_cmd_data(chip);
	memcpy(ms_card->raw_sys_info, ptr, 96);

	/* Read useful block contents */
	rtsx_init_cmd(chip);

	rtsx_add_cmd(chip, READ_REG_CMD, HEADER_ID0, 0, 0);
	rtsx_add_cmd(chip, READ_REG_CMD, HEADER_ID1, 0, 0);

	for (reg_addr = DISABLED_BLOCK0; reg_addr <= DISABLED_BLOCK3; reg_addr++) {
		rtsx_add_cmd(chip, READ_REG_CMD, reg_addr, 0, 0);
	}

	for (reg_addr = BLOCK_SIZE_0; reg_addr <= PAGE_SIZE_1; reg_addr++) {
		rtsx_add_cmd(chip, READ_REG_CMD, reg_addr, 0, 0);
	}

	rtsx_add_cmd(chip, READ_REG_CMD, MS_Device_Type, 0, 0);
	rtsx_add_cmd(chip, READ_REG_CMD, MS_4bit_Support, 0, 0);

	retval = rtsx_send_cmd(chip, MS_CARD, 100);
	if (retval < 0) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	ptr = rtsx_get_cmd_data(chip);

	RTSX_DEBUGP("Boot block data:\n");
	RTSX_DUMP(ptr, 16);

	/* Block ID error
	 * HEADER_ID0, HEADER_ID1
	 */
	if (ptr[0] != 0x00 || ptr[1] != 0x01) {
		i = ms_card->boot_block + 1;
		goto RE_SEARCH;
	}

	/* Page size error
	 * PAGE_SIZE_0, PAGE_SIZE_1
	 */
	if (ptr[12] != 0x02 || ptr[13] != 0x00) {
		i = ms_card->boot_block + 1;
		goto RE_SEARCH;
	}

	if ((ptr[14] == 1) || (ptr[14] == 3)) {
		chip->card_wp |= MS_CARD;
	}

	/* BLOCK_SIZE_0, BLOCK_SIZE_1 */
	block_size = ((u16)ptr[6] << 8) | ptr[7];
	if (block_size == 0x0010) {
		/* Block size 16KB */
		ms_card->block_shift = 5;
		ms_card->page_off = 0x1F;
	} else if (block_size == 0x0008) {
		/* Block size 8KB */
		ms_card->block_shift = 4;
		ms_card->page_off = 0x0F;
	}

	/* BLOCK_COUNT_0, BLOCK_COUNT_1 */
	ms_card->total_block = ((u16)ptr[8] << 8) | ptr[9];

#ifdef SUPPORT_MAGIC_GATE
	j = ptr[10];

	if (ms_card->block_shift == 4)  { /* 4MB or 8MB */
		if (j < 2)  { /* Effective block for 4MB: 0x1F0 */
			ms_card->capacity = 0x1EE0;
		} else { /* Effective block for 8MB: 0x3E0 */
			ms_card->capacity = 0x3DE0;
		}
	} else  { /* 16MB, 32MB, 64MB or 128MB */
		if (j < 5)  { /* Effective block for 16MB: 0x3E0 */
			ms_card->capacity = 0x7BC0;
		} else if (j < 0xA) { /* Effective block for 32MB: 0x7C0 */
			ms_card->capacity = 0xF7C0;
		} else if (j < 0x11) { /* Effective block for 64MB: 0xF80 */
			ms_card->capacity = 0x1EF80;
		} else { /* Effective block for 128MB: 0x1F00 */
			ms_card->capacity = 0x3DF00;
		}
	}
#else
	/* EBLOCK_COUNT_0, EBLOCK_COUNT_1 */
	eblock_cnt = ((u16)ptr[10] << 8) | ptr[11];

	ms_card->capacity = ((u32)eblock_cnt - 2) << ms_card->block_shift;
#endif

	chip->capacity[chip->card2lun[MS_CARD]] = ms_card->capacity;

	/* Switch I/F Mode */
	if (ptr[15]) {
		retval = ms_set_rw_reg_addr(chip, 0, 0, SystemParm, 1);
		if (retval != STATUS_SUCCESS) {
			TRACE_RET(chip, STATUS_FAIL);
		}

		RTSX_WRITE_REG(chip, PPBUF_BASE2, 0xFF, 0x88);
		RTSX_WRITE_REG(chip, PPBUF_BASE2 + 1, 0xFF, 0);

		retval = ms_transfer_tpc(chip, MS_TM_WRITE_BYTES, WRITE_REG , 1, NO_WAIT_INT);
		if (retval != STATUS_SUCCESS) {
			TRACE_RET(chip, STATUS_FAIL);
		}

		RTSX_WRITE_REG(chip, MS_CFG, 0x58 | MS_NO_CHECK_INT,
				MS_BUS_WIDTH_4 | PUSH_TIME_ODD | MS_NO_CHECK_INT);

		ms_card->ms_type |= MS_4BIT;
	}

	if (CHK_MS4BIT(ms_card)) {
		chip->card_bus_width[chip->card2lun[MS_CARD]] = 4;
	} else {
		chip->card_bus_width[chip->card2lun[MS_CARD]] = 1;
	}

	return STATUS_SUCCESS;
}

static int ms_init_l2p_tbl(struct rtsx_chip *chip)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int size, i, seg_no, retval;
	u16 defect_block, reg_addr;
	u8 val1, val2;

	ms_card->segment_cnt = ms_card->total_block >> 9;
	RTSX_DEBUGP("ms_card->segment_cnt = %d\n", ms_card->segment_cnt);

	size = ms_card->segment_cnt * sizeof(struct zone_entry);
	ms_card->segment = (struct zone_entry *)vmalloc(size);
	if (ms_card->segment == NULL) {
		TRACE_RET(chip, STATUS_FAIL);
	}
	memset(ms_card->segment, 0, size);

	retval = ms_read_page(chip, ms_card->boot_block, 1);
	if (retval != STATUS_SUCCESS) {
		TRACE_GOTO(chip, INIT_FAIL);
	}

	reg_addr = PPBUF_BASE2;
	for (i = 0; i < (((ms_card->total_block >> 9) * 10) + 1); i++) {
		retval = rtsx_read_register(chip, reg_addr++, &val1);
		if (retval != STATUS_SUCCESS) {
			TRACE_GOTO(chip, INIT_FAIL);
		}
		retval = rtsx_read_register(chip, reg_addr++, &val2);
		if (retval != STATUS_SUCCESS) {
			TRACE_GOTO(chip, INIT_FAIL);
		}

		defect_block = ((u16)val1 << 8) | val2;
		if (defect_block == 0xFFFF) {
			break;
		}
		seg_no = defect_block / 512;
		ms_card->segment[seg_no].defect_list[ms_card->segment[seg_no].disable_count++] = defect_block;
	}

	for (i = 0; i < ms_card->segment_cnt; i++) {
		ms_card->segment[i].build_flag = 0;
		ms_card->segment[i].l2p_table = NULL;
		ms_card->segment[i].free_table = NULL;
		ms_card->segment[i].get_index = 0;
		ms_card->segment[i].set_index = 0;
		ms_card->segment[i].unused_blk_cnt = 0;

		RTSX_DEBUGP("defective block count of segment %d is %d\n",
					i, ms_card->segment[i].disable_count);
	}

	return STATUS_SUCCESS;

INIT_FAIL:
	if (ms_card->segment) {
		vfree(ms_card->segment);
		ms_card->segment = NULL;
	}

	return STATUS_FAIL;
}

static u16 ms_get_l2p_tbl(struct rtsx_chip *chip, int seg_no, u16 log_off)
{
	struct ms_info *ms_card = &(chip->ms_card);
	struct zone_entry *segment;

	if (ms_card->segment == NULL)
		return 0xFFFF;

	segment = &(ms_card->segment[seg_no]);

	if (segment->l2p_table)
		return segment->l2p_table[log_off];

	return 0xFFFF;
}

static void ms_set_l2p_tbl(struct rtsx_chip *chip, int seg_no, u16 log_off, u16 phy_blk)
{
	struct ms_info *ms_card = &(chip->ms_card);
	struct zone_entry *segment;

	if (ms_card->segment == NULL)
		return;

	segment = &(ms_card->segment[seg_no]);
	if (segment->l2p_table) {
		segment->l2p_table[log_off] = phy_blk;
	}
}

static void ms_set_unused_block(struct rtsx_chip *chip, u16 phy_blk)
{
	struct ms_info *ms_card = &(chip->ms_card);
	struct zone_entry *segment;
	int seg_no;

	seg_no = (int)phy_blk >> 9;
	segment = &(ms_card->segment[seg_no]);

	segment->free_table[segment->set_index++] = phy_blk;
	if (segment->set_index >= MS_FREE_TABLE_CNT) {
		segment->set_index = 0;
	}
	segment->unused_blk_cnt++;
}

static u16 ms_get_unused_block(struct rtsx_chip *chip, int seg_no)
{
	struct ms_info *ms_card = &(chip->ms_card);
	struct zone_entry *segment;
	u16 phy_blk;

	segment = &(ms_card->segment[seg_no]);

	if (segment->unused_blk_cnt <= 0)
		return 0xFFFF;

	phy_blk = segment->free_table[segment->get_index];
	segment->free_table[segment->get_index++] = 0xFFFF;
	if (segment->get_index >= MS_FREE_TABLE_CNT) {
		segment->get_index = 0;
	}
	segment->unused_blk_cnt--;

	return phy_blk;
}

static const unsigned short ms_start_idx[] = {0, 494, 990, 1486, 1982, 2478, 2974, 3470,
	3966, 4462, 4958, 5454, 5950, 6446, 6942, 7438, 7934};

static int ms_arbitrate_l2p(struct rtsx_chip *chip, u16 phy_blk, u16 log_off, u8 us1, u8 us2)
{
	struct ms_info *ms_card = &(chip->ms_card);
	struct zone_entry *segment;
	int seg_no;
	u16 tmp_blk;

	seg_no = (int)phy_blk >> 9;
	segment = &(ms_card->segment[seg_no]);
	tmp_blk = segment->l2p_table[log_off];

	if (us1 != us2) {
		if (us1 == 0) {
			if (!(chip->card_wp & MS_CARD)) {
				ms_erase_block(chip, tmp_blk);
			}
			ms_set_unused_block(chip, tmp_blk);
			segment->l2p_table[log_off] = phy_blk;
		} else {
			if (!(chip->card_wp & MS_CARD)) {
				ms_erase_block(chip, phy_blk);
			}
			ms_set_unused_block(chip, phy_blk);
		}
	} else {
		if (phy_blk < tmp_blk) {
			if (!(chip->card_wp & MS_CARD)) {
				ms_erase_block(chip, phy_blk);
			}
			ms_set_unused_block(chip, phy_blk);
		} else {
			if (!(chip->card_wp & MS_CARD)) {
				ms_erase_block(chip, tmp_blk);
			}
			ms_set_unused_block(chip, tmp_blk);
			segment->l2p_table[log_off] = phy_blk;
		}
	}

	return STATUS_SUCCESS;
}

static int ms_build_l2p_tbl(struct rtsx_chip *chip, int seg_no)
{
	struct ms_info *ms_card = &(chip->ms_card);
	struct zone_entry *segment;
	int retval, table_size, disable_cnt, defect_flag, i;
	u16 start, end, phy_blk, log_blk, tmp_blk;
	u8 extra[MS_EXTRA_SIZE], us1, us2;

	RTSX_DEBUGP("ms_build_l2p_tbl: %d\n", seg_no);

	if (ms_card->segment == NULL) {
		retval = ms_init_l2p_tbl(chip);
		if (retval != STATUS_SUCCESS) {
			TRACE_RET(chip, retval);
		}
	}

	if (ms_card->segment[seg_no].build_flag) {
		RTSX_DEBUGP("l2p table of segment %d has been built\n", seg_no);
		return STATUS_SUCCESS;
	}

	if (seg_no == 0) {
		table_size = 494;
	} else {
		table_size = 496;
	}

	segment = &(ms_card->segment[seg_no]);

	if (segment->l2p_table == NULL) {
		segment->l2p_table = (u16 *)vmalloc(table_size * 2);
		if (segment->l2p_table == NULL) {
			TRACE_GOTO(chip, BUILD_FAIL);
		}
	}
	memset((u8 *)(segment->l2p_table), 0xff, table_size * 2);

	if (segment->free_table == NULL) {
		segment->free_table = (u16 *)vmalloc(MS_FREE_TABLE_CNT * 2);
		if (segment->free_table == NULL) {
			TRACE_GOTO(chip, BUILD_FAIL);
		}
	}
	memset((u8 *)(segment->free_table), 0xff, MS_FREE_TABLE_CNT * 2);

	start = (u16)seg_no << 9;
	end = (u16)(seg_no + 1) << 9;

	disable_cnt = segment->disable_count;

	segment->get_index = segment->set_index = 0;
	segment->unused_blk_cnt = 0;

	for (phy_blk = start; phy_blk < end; phy_blk++) {
		if (disable_cnt) {
			defect_flag = 0;
			for (i = 0; i < segment->disable_count; i++) {
				if (phy_blk == segment->defect_list[i]) {
					defect_flag = 1;
					break;
				}
			}
			if (defect_flag) {
				disable_cnt--;
				continue;
			}
		}

		retval = ms_read_extra_data(chip, phy_blk, 0, extra, MS_EXTRA_SIZE);
		if (retval != STATUS_SUCCESS) {
			RTSX_DEBUGP("read extra data fail\n");
			ms_set_bad_block(chip, phy_blk);
			continue;
		}

		if (seg_no == ms_card->segment_cnt - 1) {
			if (!(extra[1] & NOT_TRANSLATION_TABLE)) {
				if (!(chip->card_wp & MS_CARD)) {
					retval = ms_erase_block(chip, phy_blk);
					if (retval != STATUS_SUCCESS)
						continue;
					extra[2] = 0xff;
					extra[3] = 0xff;
				}
			}
		}

		if (!(extra[0] & BLOCK_OK))
			continue;
		if (!(extra[1] & NOT_BOOT_BLOCK))
			continue;
		if ((extra[0] & PAGE_OK) != PAGE_OK)
			continue;

		log_blk = ((u16)extra[2] << 8) | extra[3];

		if (log_blk == 0xFFFF) {
			if (!(chip->card_wp & MS_CARD)) {
				retval = ms_erase_block(chip, phy_blk);
				if (retval != STATUS_SUCCESS)
					continue;
			}
			ms_set_unused_block(chip, phy_blk);
			continue;
		}

		if ((log_blk < ms_start_idx[seg_no]) ||
				(log_blk >= ms_start_idx[seg_no+1])) {
			if (!(chip->card_wp & MS_CARD)) {
				retval = ms_erase_block(chip, phy_blk);
				if (retval != STATUS_SUCCESS)
					continue;
			}
			ms_set_unused_block(chip, phy_blk);
			continue;
		}

		if (segment->l2p_table[log_blk - ms_start_idx[seg_no]] == 0xFFFF) {
			segment->l2p_table[log_blk - ms_start_idx[seg_no]] = phy_blk;
			continue;
		}

		us1 = extra[0] & 0x10;
		tmp_blk = segment->l2p_table[log_blk - ms_start_idx[seg_no]];
		retval = ms_read_extra_data(chip, tmp_blk, 0, extra, MS_EXTRA_SIZE);
		if (retval != STATUS_SUCCESS)
			continue;
		us2 = extra[0] & 0x10;

		(void)ms_arbitrate_l2p(chip, phy_blk, log_blk-ms_start_idx[seg_no], us1, us2);
		continue;
	}

	segment->build_flag = 1;

	RTSX_DEBUGP("unused block count: %d\n", segment->unused_blk_cnt);

	/* Logical Address Confirmation Process */
	if (seg_no == ms_card->segment_cnt - 1) {
		if (segment->unused_blk_cnt < 2) {
			chip->card_wp |= MS_CARD;
		}
	} else {
		if (segment->unused_blk_cnt < 1) {
			chip->card_wp |= MS_CARD;
		}
	}

	if (chip->card_wp & MS_CARD)
		return STATUS_SUCCESS;

	for (log_blk = ms_start_idx[seg_no]; log_blk < ms_start_idx[seg_no + 1]; log_blk++) {
		if (segment->l2p_table[log_blk-ms_start_idx[seg_no]] == 0xFFFF) {
			phy_blk = ms_get_unused_block(chip, seg_no);
			if (phy_blk == 0xFFFF) {
				chip->card_wp |= MS_CARD;
				return STATUS_SUCCESS;
			}
			retval = ms_init_page(chip, phy_blk, log_blk, 0, 1);
			if (retval != STATUS_SUCCESS) {
				TRACE_GOTO(chip, BUILD_FAIL);
			}
			segment->l2p_table[log_blk-ms_start_idx[seg_no]] = phy_blk;
			if (seg_no == ms_card->segment_cnt - 1) {
				if (segment->unused_blk_cnt < 2) {
					chip->card_wp |= MS_CARD;
					return STATUS_SUCCESS;
				}
			} else {
				if (segment->unused_blk_cnt < 1) {
					chip->card_wp |= MS_CARD;
					return STATUS_SUCCESS;
				}
			}
		}
	}

	/* Make boot block be the first normal block */
	if (seg_no == 0) {
		for (log_blk = 0; log_blk < 494; log_blk++) {
			tmp_blk = segment->l2p_table[log_blk];
			if (tmp_blk < ms_card->boot_block) {
				RTSX_DEBUGP("Boot block is not the first normal block.\n");

				if (chip->card_wp & MS_CARD)
					break;

				phy_blk = ms_get_unused_block(chip, 0);
				retval = ms_copy_page(chip, tmp_blk, phy_blk,
						log_blk, 0, ms_card->page_off + 1);
				if (retval != STATUS_SUCCESS) {
					TRACE_RET(chip, STATUS_FAIL);
				}

				segment->l2p_table[log_blk] = phy_blk;

				retval = ms_set_bad_block(chip, tmp_blk);
				if (retval != STATUS_SUCCESS) {
					TRACE_RET(chip, STATUS_FAIL);
				}
			}
		}
	}

	return STATUS_SUCCESS;

BUILD_FAIL:
	segment->build_flag = 0;
	if (segment->l2p_table) {
		vfree(segment->l2p_table);
		segment->l2p_table = NULL;
	}
	if (segment->free_table) {
		vfree(segment->free_table);
		segment->free_table = NULL;
	}

	return STATUS_FAIL;
}


int reset_ms_card(struct rtsx_chip *chip)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int retval;

	memset(ms_card, 0, sizeof(struct ms_info));

	retval = enable_card_clock(chip, MS_CARD);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	retval = select_card(chip, MS_CARD);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	ms_card->ms_type = 0;

	retval = reset_ms_pro(chip);
	if (retval != STATUS_SUCCESS) {
		if (ms_card->check_ms_flow) {
			retval = reset_ms(chip);
			if (retval != STATUS_SUCCESS) {
				TRACE_RET(chip, STATUS_FAIL);
			}
		} else {
			TRACE_RET(chip, STATUS_FAIL);
		}
	}

	retval = ms_set_init_para(chip);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	if (!CHK_MSPRO(ms_card)) {
		/* Build table for the last segment,
		 * to check if L2P talbe block exist,erasing it
		 */
		retval = ms_build_l2p_tbl(chip, ms_card->total_block / 512 - 1);
		if (retval != STATUS_SUCCESS) {
			TRACE_RET(chip, STATUS_FAIL);
		}
	}

	RTSX_DEBUGP("ms_card->ms_type = 0x%x\n", ms_card->ms_type);

	return STATUS_SUCCESS;
}

static int mspro_set_rw_cmd(struct rtsx_chip *chip, u32 start_sec, u16 sec_cnt, u8 cmd)
{
	int retval, i;
	u8 data[8];

	data[0] = cmd;
	data[1] = (u8)(sec_cnt >> 8);
	data[2] = (u8)sec_cnt;
	data[3] = (u8)(start_sec >> 24);
	data[4] = (u8)(start_sec >> 16);
	data[5] = (u8)(start_sec >> 8);
	data[6] = (u8)start_sec;
	data[7] = 0;

	for (i = 0; i < MS_MAX_RETRY_COUNT; i++) {
		retval = ms_write_bytes(chip, PRO_EX_SET_CMD, 7, WAIT_INT, data, 8);
		if (retval == STATUS_SUCCESS)
			break;
	}
	if (i == MS_MAX_RETRY_COUNT) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	return STATUS_SUCCESS;
}


void mspro_stop_seq_mode(struct rtsx_chip *chip)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int retval;

	RTSX_DEBUGP("--%s--\n", __func__);

	if (ms_card->seq_mode) {
		retval = ms_switch_clock(chip);
		if (retval != STATUS_SUCCESS)
			return;

		ms_card->seq_mode = 0;
		ms_card->total_sec_cnt = 0;
		ms_send_cmd(chip, PRO_STOP, WAIT_INT);

		rtsx_write_register(chip, RBCTL, RB_FLUSH, RB_FLUSH);
	}
}

static inline int ms_auto_tune_clock(struct rtsx_chip *chip)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int retval;

	RTSX_DEBUGP("--%s--\n", __func__);

	if (chip->asic_code) {
		if (ms_card->ms_clock > 30) {
			ms_card->ms_clock -= 20;
		}
	} else {
		if (ms_card->ms_clock == CLK_80) {
			ms_card->ms_clock = CLK_60;
		} else if (ms_card->ms_clock == CLK_60) {
			ms_card->ms_clock = CLK_40;
		}
	}

	retval = ms_switch_clock(chip);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	return STATUS_SUCCESS;
}

static int mspro_rw_multi_sector(struct scsi_cmnd *srb, struct rtsx_chip *chip, u32 start_sector, u16 sector_cnt)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int retval, mode_2k = 0;
	u16 count;
	u8 val, trans_mode, rw_tpc, rw_cmd;

	ms_set_err_code(chip, MS_NO_ERROR);

	ms_card->cleanup_counter = 0;

	if (CHK_MSHG(ms_card)) {
		if ((start_sector % 4) || (sector_cnt % 4)) {
			if (srb->sc_data_direction == DMA_FROM_DEVICE) {
				rw_tpc = PRO_READ_LONG_DATA;
				rw_cmd = PRO_READ_DATA;
			} else {
				rw_tpc = PRO_WRITE_LONG_DATA;
				rw_cmd = PRO_WRITE_DATA;
			}
		} else {
			if (srb->sc_data_direction == DMA_FROM_DEVICE) {
				rw_tpc = PRO_READ_QUAD_DATA;
				rw_cmd = PRO_READ_2K_DATA;
			} else {
				rw_tpc = PRO_WRITE_QUAD_DATA;
				rw_cmd = PRO_WRITE_2K_DATA;
			}
			mode_2k = 1;
		}
	} else {
		if (srb->sc_data_direction == DMA_FROM_DEVICE) {
			rw_tpc = PRO_READ_LONG_DATA;
			rw_cmd = PRO_READ_DATA;
		} else {
			rw_tpc = PRO_WRITE_LONG_DATA;
			rw_cmd = PRO_WRITE_DATA;
		}
	}

	retval = ms_switch_clock(chip);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	if (srb->sc_data_direction == DMA_FROM_DEVICE) {
		trans_mode = MS_TM_AUTO_READ;
	} else {
		trans_mode = MS_TM_AUTO_WRITE;
	}

	RTSX_READ_REG(chip, MS_TRANS_CFG, &val);

	if (ms_card->seq_mode) {
		if ((ms_card->pre_dir != srb->sc_data_direction)
				|| ((ms_card->pre_sec_addr + ms_card->pre_sec_cnt) != start_sector)
				|| (mode_2k && (ms_card->seq_mode & MODE_512_SEQ))
				|| (!mode_2k && (ms_card->seq_mode & MODE_2K_SEQ))
				|| !(val & MS_INT_BREQ)
				|| ((ms_card->total_sec_cnt + sector_cnt) > 0xFE00)) {
			ms_card->seq_mode = 0;
			ms_card->total_sec_cnt = 0;
			if (val & MS_INT_BREQ) {
				retval = ms_send_cmd(chip, PRO_STOP, WAIT_INT);
				if (retval != STATUS_SUCCESS) {
					TRACE_RET(chip, STATUS_FAIL);
				}

				rtsx_write_register(chip, RBCTL, RB_FLUSH, RB_FLUSH);
			}
		}
	}

	if (!ms_card->seq_mode) {
		ms_card->total_sec_cnt = 0;
		if (sector_cnt >= SEQ_START_CRITERIA) {
			if ((ms_card->capacity - start_sector) > 0xFE00) {
				count = 0xFE00;
			} else {
				count = (u16)(ms_card->capacity - start_sector);
			}
			if (count > sector_cnt) {
				if (mode_2k) {
					ms_card->seq_mode |= MODE_2K_SEQ;
				} else {
					ms_card->seq_mode |= MODE_512_SEQ;
				}
			}
		} else {
			count = sector_cnt;
		}
		retval = mspro_set_rw_cmd(chip, start_sector, count, rw_cmd);
		if (retval != STATUS_SUCCESS) {
			ms_card->seq_mode = 0;
			TRACE_RET(chip, STATUS_FAIL);
		}
	}

	retval = ms_transfer_data(chip, trans_mode, rw_tpc, sector_cnt, WAIT_INT, mode_2k,
			scsi_sg_count(srb), scsi_sglist(srb), scsi_bufflen(srb));
	if (retval != STATUS_SUCCESS) {
		ms_card->seq_mode = 0;
		rtsx_read_register(chip, MS_TRANS_CFG, &val);
		rtsx_clear_ms_error(chip);

		if (detect_card_cd(chip, MS_CARD) != STATUS_SUCCESS) {
			chip->rw_need_retry = 0;
			RTSX_DEBUGP("No card exist, exit mspro_rw_multi_sector\n");
			TRACE_RET(chip, STATUS_FAIL);
		}

		if (val & MS_INT_BREQ) {
			ms_send_cmd(chip, PRO_STOP, WAIT_INT);
		}
		if (val & (MS_CRC16_ERR | MS_RDY_TIMEOUT)) {
			RTSX_DEBUGP("MSPro CRC error, tune clock!\n");
			chip->rw_need_retry = 1;
			ms_auto_tune_clock(chip);
		}

		TRACE_RET(chip, retval);
	}

	if (ms_card->seq_mode) {
		ms_card->pre_sec_addr = start_sector;
		ms_card->pre_sec_cnt = sector_cnt;
		ms_card->pre_dir = srb->sc_data_direction;
		ms_card->total_sec_cnt += sector_cnt;
	}

	return STATUS_SUCCESS;
}

static int mspro_read_format_progress(struct rtsx_chip *chip, const int short_data_len)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int retval, i;
	u32 total_progress, cur_progress;
	u8 cnt, tmp;
	u8 data[8];

	RTSX_DEBUGP("mspro_read_format_progress, short_data_len = %d\n", short_data_len);

	retval = ms_switch_clock(chip);
	if (retval != STATUS_SUCCESS) {
		ms_card->format_status = FORMAT_FAIL;
		TRACE_RET(chip, STATUS_FAIL);
	}

	retval = rtsx_read_register(chip, MS_TRANS_CFG, &tmp);
	if (retval != STATUS_SUCCESS) {
		ms_card->format_status = FORMAT_FAIL;
		TRACE_RET(chip, STATUS_FAIL);
	}

	if (!(tmp & MS_INT_BREQ)) {
		if ((tmp &  (MS_INT_CED | MS_INT_BREQ | MS_INT_CMDNK | MS_INT_ERR)) == MS_INT_CED) {
			ms_card->format_status = FORMAT_SUCCESS;
			return STATUS_SUCCESS;
		}
		ms_card->format_status = FORMAT_FAIL;
		TRACE_RET(chip, STATUS_FAIL);
	}

	if (short_data_len >= 256) {
		cnt = 0;
	} else {
		cnt = (u8)short_data_len;
	}

	retval = rtsx_write_register(chip, MS_CFG, MS_NO_CHECK_INT, MS_NO_CHECK_INT);
	if (retval != STATUS_SUCCESS) {
		ms_card->format_status = FORMAT_FAIL;
		TRACE_RET(chip, STATUS_FAIL);
	}

	retval = ms_read_bytes(chip, PRO_READ_SHORT_DATA, cnt, WAIT_INT, data, 8);
	if (retval != STATUS_SUCCESS) {
		ms_card->format_status = FORMAT_FAIL;
		TRACE_RET(chip, STATUS_FAIL);
	}

	total_progress = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
	cur_progress = (data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7];

	RTSX_DEBUGP("total_progress = %d, cur_progress = %d\n",
				total_progress, cur_progress);

	if (total_progress == 0) {
		ms_card->progress = 0;
	} else {
		u64 ulltmp = (u64)cur_progress * (u64)65535;
		do_div(ulltmp, total_progress);
		ms_card->progress = (u16)ulltmp;
	}
	RTSX_DEBUGP("progress = %d\n", ms_card->progress);

	for (i = 0; i < 5000; i++) {
		retval = rtsx_read_register(chip, MS_TRANS_CFG, &tmp);
		if (retval != STATUS_SUCCESS) {
			ms_card->format_status = FORMAT_FAIL;
			TRACE_RET(chip, STATUS_FAIL);
		}
		if (tmp & (MS_INT_CED | MS_INT_CMDNK | MS_INT_BREQ | MS_INT_ERR)) {
			break;
		}

		wait_timeout(1);
	}

	retval = rtsx_write_register(chip, MS_CFG, MS_NO_CHECK_INT, 0);
	if (retval != STATUS_SUCCESS) {
		ms_card->format_status = FORMAT_FAIL;
		TRACE_RET(chip, STATUS_FAIL);
	}

	if (i == 5000) {
		ms_card->format_status = FORMAT_FAIL;
		TRACE_RET(chip, STATUS_FAIL);
	}

	if (tmp & (MS_INT_CMDNK | MS_INT_ERR)) {
		ms_card->format_status = FORMAT_FAIL;
		TRACE_RET(chip, STATUS_FAIL);
	}

	if (tmp & MS_INT_CED) {
		ms_card->format_status = FORMAT_SUCCESS;
		ms_card->pro_under_formatting = 0;
	} else if (tmp & MS_INT_BREQ) {
		ms_card->format_status = FORMAT_IN_PROGRESS;
	} else {
		ms_card->format_status = FORMAT_FAIL;
		ms_card->pro_under_formatting = 0;
		TRACE_RET(chip, STATUS_FAIL);
	}

	return STATUS_SUCCESS;
}

void mspro_polling_format_status(struct rtsx_chip *chip)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int i;

	if (ms_card->pro_under_formatting && (rtsx_get_stat(chip) != RTSX_STAT_SS)) {
		rtsx_set_stat(chip, RTSX_STAT_RUN);

		for (i = 0; i < 65535; i++) {
			mspro_read_format_progress(chip, MS_SHORT_DATA_LEN);
			if (ms_card->format_status != FORMAT_IN_PROGRESS)
				break;
		}
	}

	return;
}

int mspro_format(struct scsi_cmnd *srb, struct rtsx_chip *chip, int short_data_len, int quick_format)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int retval, i;
	u8 buf[8], tmp;
	u16 para;

	RTSX_DEBUGP("--%s--\n", __func__);

	retval = ms_switch_clock(chip);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	retval = ms_set_rw_reg_addr(chip, 0x00, 0x00, Pro_TPCParm, 0x01);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	memset(buf, 0, 2);
	switch (short_data_len) {
	case 32:
		buf[0] = 0;
		break;
	case 64:
		buf[0] = 1;
		break;
	case 128:
		buf[0] = 2;
		break;
	case 256:
	default:
		buf[0] = 3;
		break;
	}

	for (i = 0; i < MS_MAX_RETRY_COUNT; i++) {
		retval = ms_write_bytes(chip, PRO_WRITE_REG, 1, NO_WAIT_INT, buf, 2);
		if (retval == STATUS_SUCCESS)
			break;
	}
	if (i == MS_MAX_RETRY_COUNT) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	if (quick_format) {
		para = 0x0000;
	} else {
		para = 0x0001;
	}
	retval = mspro_set_rw_cmd(chip, 0, para, PRO_FORMAT);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	RTSX_READ_REG(chip, MS_TRANS_CFG, &tmp);

	if (tmp & (MS_INT_CMDNK | MS_INT_ERR)) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	if ((tmp & (MS_INT_BREQ | MS_INT_CED)) == MS_INT_BREQ) {
		ms_card->pro_under_formatting = 1;
		ms_card->progress = 0;
		ms_card->format_status = FORMAT_IN_PROGRESS;
		return STATUS_SUCCESS;
	}

	if (tmp & MS_INT_CED) {
		ms_card->pro_under_formatting = 0;
		ms_card->progress = 0;
		ms_card->format_status = FORMAT_SUCCESS;
		set_sense_type(chip, SCSI_LUN(srb), SENSE_TYPE_NO_SENSE);
		return STATUS_SUCCESS;
	}

	TRACE_RET(chip, STATUS_FAIL);
}


static int ms_read_multiple_pages(struct rtsx_chip *chip, u16 phy_blk, u16 log_blk,
		u8 start_page, u8 end_page, u8 *buf, unsigned int *index, unsigned int *offset)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int retval, i;
	u8 extra[MS_EXTRA_SIZE], page_addr, val, trans_cfg, data[6];
	u8 *ptr;

	retval = ms_read_extra_data(chip, phy_blk, start_page, extra, MS_EXTRA_SIZE);
	if (retval == STATUS_SUCCESS) {
		if ((extra[1] & 0x30) != 0x30) {
			ms_set_err_code(chip, MS_FLASH_READ_ERROR);
			TRACE_RET(chip, STATUS_FAIL);
		}
	}

	retval = ms_set_rw_reg_addr(chip, OverwriteFlag, MS_EXTRA_SIZE, SystemParm, 6);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	if (CHK_MS4BIT(ms_card)) {
		data[0] = 0x88;
	} else {
		data[0] = 0x80;
	}
	data[1] = 0;
	data[2] = (u8)(phy_blk >> 8);
	data[3] = (u8)phy_blk;
	data[4] = 0;
	data[5] = start_page;

	for (i = 0; i < MS_MAX_RETRY_COUNT; i++) {
		retval = ms_write_bytes(chip, WRITE_REG, 6, NO_WAIT_INT, data, 6);
		if (retval == STATUS_SUCCESS)
			break;
	}
	if (i == MS_MAX_RETRY_COUNT) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	ms_set_err_code(chip, MS_NO_ERROR);

	retval = ms_send_cmd(chip, BLOCK_READ, WAIT_INT);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	ptr = buf;

	for (page_addr = start_page; page_addr < end_page; page_addr++) {
		ms_set_err_code(chip, MS_NO_ERROR);

		if (detect_card_cd(chip, MS_CARD) != STATUS_SUCCESS) {
			ms_set_err_code(chip, MS_NO_CARD);
			TRACE_RET(chip, STATUS_FAIL);
		}

		retval = ms_read_bytes(chip, GET_INT, 1, NO_WAIT_INT, &val, 1);
		if (retval != STATUS_SUCCESS) {
			TRACE_RET(chip, STATUS_FAIL);
		}
		if (val & INT_REG_CMDNK) {
			ms_set_err_code(chip, MS_CMD_NK);
			TRACE_RET(chip, STATUS_FAIL);
		}
		if (val & INT_REG_ERR) {
			if (val & INT_REG_BREQ) {
				retval = ms_read_status_reg(chip);
				if (retval != STATUS_SUCCESS) {
					if (!(chip->card_wp & MS_CARD)) {
						reset_ms(chip);
						ms_set_page_status(log_blk, setPS_NG, extra, MS_EXTRA_SIZE);
						ms_write_extra_data(chip, phy_blk,
								page_addr, extra, MS_EXTRA_SIZE);
					}
					ms_set_err_code(chip, MS_FLASH_READ_ERROR);
					TRACE_RET(chip, STATUS_FAIL);
				}
			} else {
				ms_set_err_code(chip, MS_FLASH_READ_ERROR);
				TRACE_RET(chip, STATUS_FAIL);
			}
		} else {
			if (!(val & INT_REG_BREQ)) {
				ms_set_err_code(chip, MS_BREQ_ERROR);
				TRACE_RET(chip, STATUS_FAIL);
			}
		}

		if (page_addr == (end_page - 1)) {
			if (!(val & INT_REG_CED)) {
				retval = ms_send_cmd(chip, BLOCK_END, WAIT_INT);
				if (retval != STATUS_SUCCESS) {
					TRACE_RET(chip, STATUS_FAIL);
				}
			}

			retval = ms_read_bytes(chip, GET_INT, 1, NO_WAIT_INT, &val, 1);
			if (retval != STATUS_SUCCESS) {
				TRACE_RET(chip, STATUS_FAIL);
			}
			if (!(val & INT_REG_CED)) {
				ms_set_err_code(chip, MS_FLASH_READ_ERROR);
				TRACE_RET(chip, STATUS_FAIL);
			}

			trans_cfg = NO_WAIT_INT;
		} else {
			trans_cfg = WAIT_INT;
		}

		rtsx_init_cmd(chip);

		rtsx_add_cmd(chip, WRITE_REG_CMD, MS_TPC, 0xFF, READ_PAGE_DATA);
		rtsx_add_cmd(chip, WRITE_REG_CMD, MS_TRANS_CFG, 0xFF, trans_cfg);
		rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_DATA_SOURCE, 0x01, RING_BUFFER);

		trans_dma_enable(DMA_FROM_DEVICE, chip, 512, DMA_512);

		rtsx_add_cmd(chip, WRITE_REG_CMD, MS_TRANSFER, 0xFF,
				MS_TRANSFER_START |  MS_TM_NORMAL_READ);
		rtsx_add_cmd(chip, CHECK_REG_CMD, MS_TRANSFER, MS_TRANSFER_END, MS_TRANSFER_END);

		rtsx_send_cmd_no_wait(chip);

		retval = rtsx_transfer_data_partial(chip, MS_CARD, ptr, 512, scsi_sg_count(chip->srb),
				index, offset, DMA_FROM_DEVICE, chip->ms_timeout);
		if (retval < 0) {
			if (retval == -ETIMEDOUT) {
				ms_set_err_code(chip, MS_TO_ERROR);
				rtsx_clear_ms_error(chip);
				TRACE_RET(chip, STATUS_TIMEDOUT);
			}

			retval = rtsx_read_register(chip, MS_TRANS_CFG, &val);
			if (retval != STATUS_SUCCESS) {
				ms_set_err_code(chip, MS_TO_ERROR);
				rtsx_clear_ms_error(chip);
				TRACE_RET(chip, STATUS_TIMEDOUT);
			}
			if (val & (MS_CRC16_ERR | MS_RDY_TIMEOUT)) {
				ms_set_err_code(chip, MS_CRC16_ERROR);
				rtsx_clear_ms_error(chip);
				TRACE_RET(chip, STATUS_FAIL);
			}
		}

		if (scsi_sg_count(chip->srb) == 0)
			ptr += 512;
	}

	return STATUS_SUCCESS;
}

static int ms_write_multiple_pages(struct rtsx_chip *chip, u16 old_blk, u16 new_blk,
		u16 log_blk, u8 start_page, u8 end_page, u8 *buf,
		unsigned int *index, unsigned int *offset)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int retval, i;
	u8 page_addr, val, data[16];
	u8 *ptr;

	if (!start_page) {
		retval = ms_set_rw_reg_addr(chip, OverwriteFlag, MS_EXTRA_SIZE, SystemParm, 7);
		if (retval != STATUS_SUCCESS) {
			TRACE_RET(chip, STATUS_FAIL);
		}

		if (CHK_MS4BIT(ms_card)) {
			data[0] = 0x88;
		} else {
			data[0] = 0x80;
		}
		data[1] = 0;
		data[2] = (u8)(old_blk >> 8);
		data[3] = (u8)old_blk;
		data[4] = 0x80;
		data[5] = 0;
		data[6] = 0xEF;
		data[7] = 0xFF;

		retval = ms_write_bytes(chip, WRITE_REG, 7, NO_WAIT_INT, data, 8);
		if (retval != STATUS_SUCCESS) {
			TRACE_RET(chip, STATUS_FAIL);
		}

		retval = ms_send_cmd(chip, BLOCK_WRITE, WAIT_INT);
		if (retval != STATUS_SUCCESS) {
			TRACE_RET(chip, STATUS_FAIL);
		}

		ms_set_err_code(chip, MS_NO_ERROR);
		retval = ms_transfer_tpc(chip, MS_TM_READ_BYTES, GET_INT, 1, NO_WAIT_INT);
		if (retval != STATUS_SUCCESS) {
			TRACE_RET(chip, STATUS_FAIL);
		}
	}

	retval = ms_set_rw_reg_addr(chip, OverwriteFlag, MS_EXTRA_SIZE, SystemParm, (6 + MS_EXTRA_SIZE));
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	ms_set_err_code(chip, MS_NO_ERROR);

	if (CHK_MS4BIT(ms_card)) {
		data[0] = 0x88;
	} else {
		data[0] = 0x80;
	}
	data[1] = 0;
	data[2] = (u8)(new_blk >> 8);
	data[3] = (u8)new_blk;
	if ((end_page - start_page) == 1) {
		data[4] = 0x20;
	} else {
		data[4] = 0;
	}
	data[5] = start_page;
	data[6] = 0xF8;
	data[7] = 0xFF;
	data[8] = (u8)(log_blk >> 8);
	data[9] = (u8)log_blk;

	for (i = 0x0A; i < 0x10; i++) {
		data[i] = 0xFF;
	}

	for (i = 0; i < MS_MAX_RETRY_COUNT; i++) {
		retval = ms_write_bytes(chip, WRITE_REG, 6 + MS_EXTRA_SIZE, NO_WAIT_INT, data, 16);
		if (retval == STATUS_SUCCESS)
			break;
	}
	if (i == MS_MAX_RETRY_COUNT) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	for (i = 0; i < MS_MAX_RETRY_COUNT; i++) {
		retval = ms_send_cmd(chip, BLOCK_WRITE, WAIT_INT);
		if (retval == STATUS_SUCCESS)
			break;
	}
	if (i == MS_MAX_RETRY_COUNT) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	retval = ms_read_bytes(chip, GET_INT, 1, NO_WAIT_INT, &val, 1);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	ptr = buf;
	for (page_addr = start_page; page_addr < end_page; page_addr++) {
		ms_set_err_code(chip, MS_NO_ERROR);

		if (detect_card_cd(chip, MS_CARD) != STATUS_SUCCESS) {
			ms_set_err_code(chip, MS_NO_CARD);
			TRACE_RET(chip, STATUS_FAIL);
		}

		if (val & INT_REG_CMDNK) {
			ms_set_err_code(chip, MS_CMD_NK);
			TRACE_RET(chip, STATUS_FAIL);
		}
		if (val & INT_REG_ERR) {
			ms_set_err_code(chip, MS_FLASH_WRITE_ERROR);
			TRACE_RET(chip, STATUS_FAIL);
		}
		if (!(val & INT_REG_BREQ)) {
			ms_set_err_code(chip, MS_BREQ_ERROR);
			TRACE_RET(chip, STATUS_FAIL);
		}

		udelay(30);

		rtsx_init_cmd(chip);

		rtsx_add_cmd(chip, WRITE_REG_CMD, MS_TPC, 0xFF, WRITE_PAGE_DATA);
		rtsx_add_cmd(chip, WRITE_REG_CMD, MS_TRANS_CFG, 0xFF, WAIT_INT);
		rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_DATA_SOURCE, 0x01, RING_BUFFER);

		trans_dma_enable(DMA_TO_DEVICE, chip, 512, DMA_512);

		rtsx_add_cmd(chip, WRITE_REG_CMD, MS_TRANSFER, 0xFF,
				MS_TRANSFER_START |  MS_TM_NORMAL_WRITE);
		rtsx_add_cmd(chip, CHECK_REG_CMD, MS_TRANSFER, MS_TRANSFER_END, MS_TRANSFER_END);

		rtsx_send_cmd_no_wait(chip);

		retval = rtsx_transfer_data_partial(chip, MS_CARD, ptr, 512, scsi_sg_count(chip->srb),
				index, offset, DMA_TO_DEVICE, chip->ms_timeout);
		if (retval < 0) {
			ms_set_err_code(chip, MS_TO_ERROR);
			rtsx_clear_ms_error(chip);

			if (retval == -ETIMEDOUT) {
				TRACE_RET(chip, STATUS_TIMEDOUT);
			} else {
				TRACE_RET(chip, STATUS_FAIL);
			}
		}

		retval = ms_read_bytes(chip, GET_INT, 1, NO_WAIT_INT, &val, 1);
		if (retval != STATUS_SUCCESS) {
			TRACE_RET(chip, STATUS_FAIL);
		}

		if ((end_page - start_page) == 1) {
			if (!(val & INT_REG_CED)) {
				ms_set_err_code(chip, MS_FLASH_WRITE_ERROR);
				TRACE_RET(chip, STATUS_FAIL);
			}
		} else {
			if (page_addr == (end_page - 1)) {
				if (!(val & INT_REG_CED)) {
					retval = ms_send_cmd(chip, BLOCK_END, WAIT_INT);
					if (retval != STATUS_SUCCESS) {
						TRACE_RET(chip, STATUS_FAIL);
					}
				}

				retval = ms_read_bytes(chip, GET_INT, 1, NO_WAIT_INT, &val, 1);
				if (retval != STATUS_SUCCESS) {
					TRACE_RET(chip, STATUS_FAIL);
				}
			}

			if ((page_addr == (end_page - 1)) || (page_addr == ms_card->page_off)) {
				if (!(val & INT_REG_CED)) {
					ms_set_err_code(chip, MS_FLASH_WRITE_ERROR);
					TRACE_RET(chip, STATUS_FAIL);
				}
			}
		}

		if (scsi_sg_count(chip->srb) == 0)
			ptr += 512;
	}

	return STATUS_SUCCESS;
}


static int ms_finish_write(struct rtsx_chip *chip, u16 old_blk, u16 new_blk,
		u16 log_blk, u8 page_off)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int retval, seg_no;

	retval = ms_copy_page(chip, old_blk, new_blk, log_blk,
			page_off, ms_card->page_off + 1);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	seg_no = old_blk >> 9;

	if (MS_TST_BAD_BLOCK_FLG(ms_card)) {
		MS_CLR_BAD_BLOCK_FLG(ms_card);
		ms_set_bad_block(chip, old_blk);
	} else {
		retval = ms_erase_block(chip, old_blk);
		if (retval == STATUS_SUCCESS) {
			ms_set_unused_block(chip, old_blk);
		}
	}

	ms_set_l2p_tbl(chip, seg_no, log_blk - ms_start_idx[seg_no], new_blk);

	return STATUS_SUCCESS;
}

static int ms_prepare_write(struct rtsx_chip *chip, u16 old_blk, u16 new_blk,
		u16 log_blk, u8 start_page)
{
	int retval;

	if (start_page) {
		retval = ms_copy_page(chip, old_blk, new_blk, log_blk, 0, start_page);
		if (retval != STATUS_SUCCESS) {
			TRACE_RET(chip, STATUS_FAIL);
		}
	}

	return STATUS_SUCCESS;
}

#ifdef MS_DELAY_WRITE
int ms_delay_write(struct rtsx_chip *chip)
{
	struct ms_info *ms_card = &(chip->ms_card);
	struct ms_delay_write_tag *delay_write = &(ms_card->delay_write);
	int retval;

	if (delay_write->delay_write_flag) {
		retval = ms_set_init_para(chip);
		if (retval != STATUS_SUCCESS) {
			TRACE_RET(chip, STATUS_FAIL);
		}

		delay_write->delay_write_flag = 0;
		retval = ms_finish_write(chip,
				delay_write->old_phyblock, delay_write->new_phyblock,
				delay_write->logblock, delay_write->pageoff);
		if (retval != STATUS_SUCCESS) {
			TRACE_RET(chip, STATUS_FAIL);
		}
	}

	return STATUS_SUCCESS;
}
#endif

static inline void ms_rw_fail(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	if (srb->sc_data_direction == DMA_FROM_DEVICE) {
		set_sense_type(chip, SCSI_LUN(srb), SENSE_TYPE_MEDIA_UNRECOVER_READ_ERR);
	} else {
		set_sense_type(chip, SCSI_LUN(srb), SENSE_TYPE_MEDIA_WRITE_ERR);
	}
}

static int ms_rw_multi_sector(struct scsi_cmnd *srb, struct rtsx_chip *chip, u32 start_sector, u16 sector_cnt)
{
	struct ms_info *ms_card = &(chip->ms_card);
	unsigned int lun = SCSI_LUN(srb);
	int retval, seg_no;
	unsigned int index = 0, offset = 0;
	u16 old_blk = 0, new_blk = 0, log_blk, total_sec_cnt = sector_cnt;
	u8 start_page, end_page = 0, page_cnt;
	u8 *ptr;
#ifdef MS_DELAY_WRITE
	struct ms_delay_write_tag *delay_write = &(ms_card->delay_write);
#endif

	ms_set_err_code(chip, MS_NO_ERROR);

	ms_card->cleanup_counter = 0;

	ptr = (u8 *)scsi_sglist(srb);

	retval = ms_switch_clock(chip);
	if (retval != STATUS_SUCCESS) {
		ms_rw_fail(srb, chip);
		TRACE_RET(chip, STATUS_FAIL);
	}

	log_blk = (u16)(start_sector >> ms_card->block_shift);
	start_page = (u8)(start_sector & ms_card->page_off);

	for (seg_no = 0; seg_no < ARRAY_SIZE(ms_start_idx) - 1; seg_no++) {
		if (log_blk < ms_start_idx[seg_no+1])
			break;
	}

	if (ms_card->segment[seg_no].build_flag == 0) {
		retval = ms_build_l2p_tbl(chip, seg_no);
		if (retval != STATUS_SUCCESS) {
			chip->card_fail |= MS_CARD;
			set_sense_type(chip, lun, SENSE_TYPE_MEDIA_NOT_PRESENT);
			TRACE_RET(chip, STATUS_FAIL);
		}
	}

	if (srb->sc_data_direction == DMA_TO_DEVICE) {
#ifdef MS_DELAY_WRITE
		if (delay_write->delay_write_flag &&
				(delay_write->logblock == log_blk) &&
				(start_page > delay_write->pageoff)) {
			delay_write->delay_write_flag = 0;
			retval = ms_copy_page(chip,
				delay_write->old_phyblock,
				delay_write->new_phyblock, log_blk,
				delay_write->pageoff, start_page);
			if (retval != STATUS_SUCCESS) {
				set_sense_type(chip, lun, SENSE_TYPE_MEDIA_WRITE_ERR);
				TRACE_RET(chip, STATUS_FAIL);
			}
			old_blk = delay_write->old_phyblock;
			new_blk = delay_write->new_phyblock;
		} else if (delay_write->delay_write_flag &&
				(delay_write->logblock == log_blk) &&
				(start_page == delay_write->pageoff)) {
			delay_write->delay_write_flag = 0;
			old_blk = delay_write->old_phyblock;
			new_blk = delay_write->new_phyblock;
		} else {
			retval = ms_delay_write(chip);
			if (retval != STATUS_SUCCESS) {
				set_sense_type(chip, lun, SENSE_TYPE_MEDIA_WRITE_ERR);
				TRACE_RET(chip, STATUS_FAIL);
			}
#endif
			old_blk = ms_get_l2p_tbl(chip, seg_no, log_blk - ms_start_idx[seg_no]);
			new_blk  = ms_get_unused_block(chip, seg_no);
			if ((old_blk == 0xFFFF) || (new_blk == 0xFFFF)) {
				set_sense_type(chip, lun, SENSE_TYPE_MEDIA_WRITE_ERR);
				TRACE_RET(chip, STATUS_FAIL);
			}

			retval = ms_prepare_write(chip, old_blk, new_blk, log_blk, start_page);
			if (retval != STATUS_SUCCESS) {
				if (detect_card_cd(chip, MS_CARD) != STATUS_SUCCESS) {
					set_sense_type(chip, lun, SENSE_TYPE_MEDIA_NOT_PRESENT);
					TRACE_RET(chip, STATUS_FAIL);
				}
				set_sense_type(chip, lun, SENSE_TYPE_MEDIA_WRITE_ERR);
				TRACE_RET(chip, STATUS_FAIL);
			}
#ifdef MS_DELAY_WRITE
		}
#endif
	} else {
#ifdef MS_DELAY_WRITE
		retval = ms_delay_write(chip);
		if (retval != STATUS_SUCCESS) {
			if (detect_card_cd(chip, MS_CARD) != STATUS_SUCCESS) {
				set_sense_type(chip, lun, SENSE_TYPE_MEDIA_NOT_PRESENT);
				TRACE_RET(chip, STATUS_FAIL);
			}
			set_sense_type(chip, lun, SENSE_TYPE_MEDIA_UNRECOVER_READ_ERR);
			TRACE_RET(chip, STATUS_FAIL);
		}
#endif
		old_blk = ms_get_l2p_tbl(chip, seg_no, log_blk - ms_start_idx[seg_no]);
		if (old_blk == 0xFFFF) {
			set_sense_type(chip, lun, SENSE_TYPE_MEDIA_UNRECOVER_READ_ERR);
			TRACE_RET(chip, STATUS_FAIL);
		}
	}

	RTSX_DEBUGP("seg_no = %d, old_blk = 0x%x, new_blk = 0x%x\n", seg_no, old_blk, new_blk);

	while (total_sec_cnt) {
		if ((start_page + total_sec_cnt) > (ms_card->page_off + 1)) {
			end_page = ms_card->page_off + 1;
		} else {
			end_page = start_page + (u8)total_sec_cnt;
		}
		page_cnt = end_page - start_page;

		RTSX_DEBUGP("start_page = %d, end_page = %d, page_cnt = %d\n",
				start_page, end_page, page_cnt);

		if (srb->sc_data_direction == DMA_FROM_DEVICE) {
			retval = ms_read_multiple_pages(chip,
				old_blk, log_blk, start_page, end_page,
				ptr, &index, &offset);
		} else {
			retval = ms_write_multiple_pages(chip, old_blk,
				new_blk, log_blk, start_page, end_page,
				ptr, &index, &offset);
		}

		if (retval != STATUS_SUCCESS) {
			toggle_gpio(chip, 1);
			if (detect_card_cd(chip, MS_CARD) != STATUS_SUCCESS) {
				set_sense_type(chip, lun, SENSE_TYPE_MEDIA_NOT_PRESENT);
				TRACE_RET(chip, STATUS_FAIL);
			}
			ms_rw_fail(srb, chip);
			TRACE_RET(chip, STATUS_FAIL);
		}

		if (srb->sc_data_direction == DMA_TO_DEVICE) {
			if (end_page == (ms_card->page_off + 1)) {
				retval = ms_erase_block(chip, old_blk);
				if (retval == STATUS_SUCCESS) {
					ms_set_unused_block(chip, old_blk);
				}
				ms_set_l2p_tbl(chip, seg_no, log_blk - ms_start_idx[seg_no], new_blk);
			}
		}

		total_sec_cnt -= page_cnt;
		if (scsi_sg_count(srb) == 0)
			ptr += page_cnt * 512;

		if (total_sec_cnt == 0)
			break;

		log_blk++;

		for (seg_no = 0; seg_no < sizeof(ms_start_idx)/2; seg_no++) {
			if (log_blk < ms_start_idx[seg_no+1])
				break;
		}

		if (ms_card->segment[seg_no].build_flag == 0) {
			retval = ms_build_l2p_tbl(chip, seg_no);
			if (retval != STATUS_SUCCESS) {
				chip->card_fail |= MS_CARD;
				set_sense_type(chip, lun, SENSE_TYPE_MEDIA_NOT_PRESENT);
				TRACE_RET(chip, STATUS_FAIL);
			}
		}

		old_blk = ms_get_l2p_tbl(chip, seg_no, log_blk - ms_start_idx[seg_no]);
		if (old_blk == 0xFFFF) {
			ms_rw_fail(srb, chip);
			TRACE_RET(chip, STATUS_FAIL);
		}

		if (srb->sc_data_direction == DMA_TO_DEVICE) {
			new_blk = ms_get_unused_block(chip, seg_no);
			if (new_blk == 0xFFFF) {
				ms_rw_fail(srb, chip);
				TRACE_RET(chip, STATUS_FAIL);
			}
		}

		RTSX_DEBUGP("seg_no = %d, old_blk = 0x%x, new_blk = 0x%x\n", seg_no, old_blk, new_blk);

		start_page = 0;
	}

	if (srb->sc_data_direction == DMA_TO_DEVICE) {
		if (end_page < (ms_card->page_off + 1)) {
#ifdef MS_DELAY_WRITE
			delay_write->delay_write_flag = 1;
			delay_write->old_phyblock = old_blk;
			delay_write->new_phyblock = new_blk;
			delay_write->logblock = log_blk;
			delay_write->pageoff = end_page;
#else
			retval = ms_finish_write(chip, old_blk, new_blk, log_blk, end_page);
			if (retval != STATUS_SUCCESS) {
				if (detect_card_cd(chip, MS_CARD) != STATUS_SUCCESS) {
					set_sense_type(chip, lun, SENSE_TYPE_MEDIA_NOT_PRESENT);
					TRACE_RET(chip, STATUS_FAIL);
				}

				ms_rw_fail(srb, chip);
				TRACE_RET(chip, STATUS_FAIL);
			}
#endif
		}
	}

	scsi_set_resid(srb, 0);

	return STATUS_SUCCESS;
}

int ms_rw(struct scsi_cmnd *srb, struct rtsx_chip *chip, u32 start_sector, u16 sector_cnt)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int retval;

	if (CHK_MSPRO(ms_card)) {
		retval = mspro_rw_multi_sector(srb, chip, start_sector, sector_cnt);
	} else {
		retval = ms_rw_multi_sector(srb, chip, start_sector, sector_cnt);
	}

	return retval;
}


void ms_free_l2p_tbl(struct rtsx_chip *chip)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int i = 0;

	if (ms_card->segment != NULL) {
		for (i = 0; i < ms_card->segment_cnt; i++) {
			if (ms_card->segment[i].l2p_table != NULL) {
				vfree(ms_card->segment[i].l2p_table);
				ms_card->segment[i].l2p_table = NULL;
			}
			if (ms_card->segment[i].free_table != NULL) {
				vfree(ms_card->segment[i].free_table);
				ms_card->segment[i].free_table = NULL;
			}
		}
		vfree(ms_card->segment);
		ms_card->segment = NULL;
	}
}

#ifdef SUPPORT_MAGIC_GATE

#ifdef READ_BYTES_WAIT_INT
static int ms_poll_int(struct rtsx_chip *chip)
{
	int retval;
	u8 val;

	rtsx_init_cmd(chip);

	rtsx_add_cmd(chip, CHECK_REG_CMD, MS_TRANS_CFG, MS_INT_CED, MS_INT_CED);

	retval = rtsx_send_cmd(chip, MS_CARD, 5000);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	val = *rtsx_get_cmd_data(chip);
	if (val & MS_INT_ERR) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	return STATUS_SUCCESS;
}
#endif

#ifdef MS_SAMPLE_INT_ERR
static int check_ms_err(struct rtsx_chip *chip)
{
	int retval;
	u8 val;

	retval = rtsx_read_register(chip, MS_TRANSFER, &val);
	if (retval != STATUS_SUCCESS)
		return 1;
	if (val & MS_TRANSFER_ERR)
		return 1;

	retval = rtsx_read_register(chip, MS_TRANS_CFG, &val);
	if (retval != STATUS_SUCCESS)
		return 1;

	if (val & (MS_INT_ERR | MS_INT_CMDNK))
		return 1;

	return 0;
}
#else
static int check_ms_err(struct rtsx_chip *chip)
{
	int retval;
	u8 val;

	retval = rtsx_read_register(chip, MS_TRANSFER, &val);
	if (retval != STATUS_SUCCESS)
		return 1;
	if (val & MS_TRANSFER_ERR)
		return 1;

	return 0;
}
#endif

static int mg_send_ex_cmd(struct rtsx_chip *chip, u8 cmd, u8 entry_num)
{
	int retval, i;
	u8 data[8];

	data[0] = cmd;
	data[1] = 0;
	data[2] = 0;
	data[3] = 0;
	data[4] = 0;
	data[5] = 0;
	data[6] = entry_num;
	data[7] = 0;

	for (i = 0; i < MS_MAX_RETRY_COUNT; i++) {
		retval = ms_write_bytes(chip, PRO_EX_SET_CMD, 7, WAIT_INT, data, 8);
		if (retval == STATUS_SUCCESS)
			break;
	}
	if (i == MS_MAX_RETRY_COUNT) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	if (check_ms_err(chip)) {
		rtsx_clear_ms_error(chip);
		TRACE_RET(chip, STATUS_FAIL);
	}

	return STATUS_SUCCESS;
}

static int mg_set_tpc_para_sub(struct rtsx_chip *chip, int type, u8 mg_entry_num)
{
	int retval;
	u8 buf[6];

	RTSX_DEBUGP("--%s--\n", __func__);

	if (type == 0) {
		retval = ms_set_rw_reg_addr(chip, 0, 0, Pro_TPCParm, 1);
	} else {
		retval = ms_set_rw_reg_addr(chip, 0, 0, Pro_DataCount1, 6);
	}
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	buf[0] = 0;
	buf[1] = 0;
	if (type == 1) {
		buf[2] = 0;
		buf[3] = 0;
		buf[4] = 0;
		buf[5] = mg_entry_num;
	}
	retval = ms_write_bytes(chip, PRO_WRITE_REG, (type == 0) ? 1 : 6, NO_WAIT_INT, buf, 6);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	return STATUS_SUCCESS;
}

int mg_set_leaf_id(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	int retval;
	int i;
	unsigned int lun = SCSI_LUN(srb);
	u8 buf1[32], buf2[12];

	RTSX_DEBUGP("--%s--\n", __func__);

	if (scsi_bufflen(srb) < 12) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		TRACE_RET(chip, STATUS_FAIL);
	}

	ms_cleanup_work(chip);

	retval = ms_switch_clock(chip);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	retval = mg_send_ex_cmd(chip, MG_SET_LID, 0);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_KEY_FAIL_NOT_ESTAB);
		TRACE_RET(chip, STATUS_FAIL);
	}

	memset(buf1, 0, 32);
	rtsx_stor_get_xfer_buf(buf2, min(12, (int)scsi_bufflen(srb)), srb);
	for (i = 0; i < 8; i++) {
		buf1[8+i] = buf2[4+i];
	}
	retval = ms_write_bytes(chip, PRO_WRITE_SHORT_DATA, 32, WAIT_INT, buf1, 32);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_KEY_FAIL_NOT_ESTAB);
		TRACE_RET(chip, STATUS_FAIL);
	}
	if (check_ms_err(chip)) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_KEY_FAIL_NOT_ESTAB);
		rtsx_clear_ms_error(chip);
		TRACE_RET(chip, STATUS_FAIL);
	}

	return STATUS_SUCCESS;
}

int mg_get_local_EKB(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	int retval = STATUS_FAIL;
	int bufflen;
	unsigned int lun = SCSI_LUN(srb);
	u8 *buf = NULL;

	RTSX_DEBUGP("--%s--\n", __func__);

	ms_cleanup_work(chip);

	retval = ms_switch_clock(chip);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	buf = kmalloc(1540, GFP_KERNEL);
	if (!buf) {
		TRACE_RET(chip, STATUS_ERROR);
	}

	buf[0] = 0x04;
	buf[1] = 0x1A;
	buf[2] = 0x00;
	buf[3] = 0x00;

	retval = mg_send_ex_cmd(chip, MG_GET_LEKB, 0);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_KEY_FAIL_NOT_AUTHEN);
		TRACE_GOTO(chip, GetEKBFinish);
	}

	retval = ms_transfer_data(chip, MS_TM_AUTO_READ, PRO_READ_LONG_DATA,
				3, WAIT_INT, 0, 0, buf + 4, 1536);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_KEY_FAIL_NOT_AUTHEN);
		rtsx_clear_ms_error(chip);
		TRACE_GOTO(chip, GetEKBFinish);
	}
	if (check_ms_err(chip)) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_KEY_FAIL_NOT_AUTHEN);
		rtsx_clear_ms_error(chip);
		TRACE_RET(chip, STATUS_FAIL);
	}

	bufflen = min(1052, (int)scsi_bufflen(srb));
	rtsx_stor_set_xfer_buf(buf, bufflen, srb);

GetEKBFinish:
	kfree(buf);
	return retval;
}

int mg_chg(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int retval;
	int bufflen;
	int i;
	unsigned int lun = SCSI_LUN(srb);
	u8 buf[32];

	RTSX_DEBUGP("--%s--\n", __func__);

	ms_cleanup_work(chip);

	retval = ms_switch_clock(chip);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	retval = mg_send_ex_cmd(chip, MG_GET_ID, 0);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_INCOMPATIBLE_MEDIUM);
		TRACE_RET(chip, STATUS_FAIL);
	}

	retval = ms_read_bytes(chip, PRO_READ_SHORT_DATA, 32, WAIT_INT, buf, 32);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_INCOMPATIBLE_MEDIUM);
		TRACE_RET(chip, STATUS_FAIL);
	}
	if (check_ms_err(chip)) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_INCOMPATIBLE_MEDIUM);
		rtsx_clear_ms_error(chip);
		TRACE_RET(chip, STATUS_FAIL);
	}

	memcpy(ms_card->magic_gate_id, buf, 16);

#ifdef READ_BYTES_WAIT_INT
	retval = ms_poll_int(chip);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_INCOMPATIBLE_MEDIUM);
		TRACE_RET(chip, STATUS_FAIL);
	}
#endif

	retval = mg_send_ex_cmd(chip, MG_SET_RD, 0);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_INCOMPATIBLE_MEDIUM);
		TRACE_RET(chip, STATUS_FAIL);
	}

	bufflen = min(12, (int)scsi_bufflen(srb));
	rtsx_stor_get_xfer_buf(buf, bufflen, srb);

	for (i = 0; i < 8; i++) {
		buf[i] = buf[4+i];
	}
	for (i = 0; i < 24; i++) {
		buf[8+i] = 0;
	}
	retval = ms_write_bytes(chip, PRO_WRITE_SHORT_DATA,
				32, WAIT_INT, buf, 32);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_INCOMPATIBLE_MEDIUM);
		TRACE_RET(chip, STATUS_FAIL);
	}
	if (check_ms_err(chip)) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_INCOMPATIBLE_MEDIUM);
		rtsx_clear_ms_error(chip);
		TRACE_RET(chip, STATUS_FAIL);
	}

	ms_card->mg_auth = 0;

	return STATUS_SUCCESS;
}

int mg_get_rsp_chg(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int retval;
	int bufflen;
	unsigned int lun = SCSI_LUN(srb);
	u8 buf1[32], buf2[36];

	RTSX_DEBUGP("--%s--\n", __func__);

	ms_cleanup_work(chip);

	retval = ms_switch_clock(chip);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	retval = mg_send_ex_cmd(chip, MG_MAKE_RMS, 0);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_KEY_FAIL_NOT_AUTHEN);
		TRACE_RET(chip, STATUS_FAIL);
	}

	retval = ms_read_bytes(chip, PRO_READ_SHORT_DATA, 32, WAIT_INT, buf1, 32);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_KEY_FAIL_NOT_AUTHEN);
		TRACE_RET(chip, STATUS_FAIL);
	}
	if (check_ms_err(chip)) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_KEY_FAIL_NOT_AUTHEN);
		rtsx_clear_ms_error(chip);
		TRACE_RET(chip, STATUS_FAIL);
	}

	buf2[0] = 0x00;
	buf2[1] = 0x22;
	buf2[2] = 0x00;
	buf2[3] = 0x00;

	memcpy(buf2 + 4, ms_card->magic_gate_id, 16);
	memcpy(buf2 + 20, buf1, 16);

	bufflen = min(36, (int)scsi_bufflen(srb));
	rtsx_stor_set_xfer_buf(buf2, bufflen, srb);

#ifdef READ_BYTES_WAIT_INT
	retval = ms_poll_int(chip);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_KEY_FAIL_NOT_AUTHEN);
		TRACE_RET(chip, STATUS_FAIL);
	}
#endif

	return STATUS_SUCCESS;
}

int mg_rsp(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int retval;
	int i;
	int bufflen;
	unsigned int lun = SCSI_LUN(srb);
	u8 buf[32];

	RTSX_DEBUGP("--%s--\n", __func__);

	ms_cleanup_work(chip);

	retval = ms_switch_clock(chip);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	retval = mg_send_ex_cmd(chip, MG_MAKE_KSE, 0);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_KEY_FAIL_NOT_AUTHEN);
		TRACE_RET(chip, STATUS_FAIL);
	}

	bufflen = min(12, (int)scsi_bufflen(srb));
	rtsx_stor_get_xfer_buf(buf, bufflen, srb);

	for (i = 0; i < 8; i++) {
		buf[i] = buf[4+i];
	}
	for (i = 0; i < 24; i++) {
		buf[8+i] = 0;
	}
	retval = ms_write_bytes(chip, PRO_WRITE_SHORT_DATA, 32, WAIT_INT, buf, 32);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_KEY_FAIL_NOT_AUTHEN);
		TRACE_RET(chip, STATUS_FAIL);
	}
	if (check_ms_err(chip)) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_KEY_FAIL_NOT_AUTHEN);
		rtsx_clear_ms_error(chip);
		TRACE_RET(chip, STATUS_FAIL);
	}

	ms_card->mg_auth = 1;

	return STATUS_SUCCESS;
}

int mg_get_ICV(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int retval;
	int bufflen;
	unsigned int lun = SCSI_LUN(srb);
	u8 *buf = NULL;

	RTSX_DEBUGP("--%s--\n", __func__);

	ms_cleanup_work(chip);

	retval = ms_switch_clock(chip);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	buf = kmalloc(1028, GFP_KERNEL);
	if (!buf) {
		TRACE_RET(chip, STATUS_ERROR);
	}

	buf[0] = 0x04;
	buf[1] = 0x02;
	buf[2] = 0x00;
	buf[3] = 0x00;

	retval = mg_send_ex_cmd(chip, MG_GET_IBD, ms_card->mg_entry_num);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_UNRECOVER_READ_ERR);
		TRACE_GOTO(chip, GetICVFinish);
	}

	retval = ms_transfer_data(chip, MS_TM_AUTO_READ, PRO_READ_LONG_DATA,
				2, WAIT_INT, 0, 0, buf + 4, 1024);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_UNRECOVER_READ_ERR);
		rtsx_clear_ms_error(chip);
		TRACE_GOTO(chip, GetICVFinish);
	}
	if (check_ms_err(chip)) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_UNRECOVER_READ_ERR);
		rtsx_clear_ms_error(chip);
		TRACE_RET(chip, STATUS_FAIL);
	}

	bufflen = min(1028, (int)scsi_bufflen(srb));
	rtsx_stor_set_xfer_buf(buf, bufflen, srb);

GetICVFinish:
	kfree(buf);
	return retval;
}

int mg_set_ICV(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int retval;
	int bufflen;
#ifdef MG_SET_ICV_SLOW
	int i;
#endif
	unsigned int lun = SCSI_LUN(srb);
	u8 *buf = NULL;

	RTSX_DEBUGP("--%s--\n", __func__);

	ms_cleanup_work(chip);

	retval = ms_switch_clock(chip);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	buf = kmalloc(1028, GFP_KERNEL);
	if (!buf) {
		TRACE_RET(chip, STATUS_ERROR);
	}

	bufflen = min(1028, (int)scsi_bufflen(srb));
	rtsx_stor_get_xfer_buf(buf, bufflen, srb);

	retval = mg_send_ex_cmd(chip, MG_SET_IBD, ms_card->mg_entry_num);
	if (retval != STATUS_SUCCESS) {
		if (ms_card->mg_auth == 0) {
			if ((buf[5] & 0xC0) != 0) {
				set_sense_type(chip, lun, SENSE_TYPE_MG_KEY_FAIL_NOT_ESTAB);
			} else {
				set_sense_type(chip, lun, SENSE_TYPE_MG_WRITE_ERR);
			}
		} else {
			set_sense_type(chip, lun, SENSE_TYPE_MG_WRITE_ERR);
		}
		TRACE_GOTO(chip, SetICVFinish);
	}

#ifdef MG_SET_ICV_SLOW
	for (i = 0; i < 2; i++) {
		udelay(50);

		rtsx_init_cmd(chip);

		rtsx_add_cmd(chip, WRITE_REG_CMD, MS_TPC, 0xFF, PRO_WRITE_LONG_DATA);
		rtsx_add_cmd(chip, WRITE_REG_CMD, MS_TRANS_CFG, 0xFF, WAIT_INT);
		rtsx_add_cmd(chip, WRITE_REG_CMD, CARD_DATA_SOURCE, 0x01, RING_BUFFER);

		trans_dma_enable(DMA_TO_DEVICE, chip, 512, DMA_512);

		rtsx_add_cmd(chip, WRITE_REG_CMD, MS_TRANSFER, 0xFF,
				MS_TRANSFER_START |  MS_TM_NORMAL_WRITE);
		rtsx_add_cmd(chip, CHECK_REG_CMD, MS_TRANSFER, MS_TRANSFER_END, MS_TRANSFER_END);

		rtsx_send_cmd_no_wait(chip);

		retval = rtsx_transfer_data(chip, MS_CARD, buf + 4 + i*512, 512, 0, DMA_TO_DEVICE, 3000);
		if ((retval < 0) || check_ms_err(chip)) {
			rtsx_clear_ms_error(chip);
			if (ms_card->mg_auth == 0) {
				if ((buf[5] & 0xC0) != 0) {
					set_sense_type(chip, lun, SENSE_TYPE_MG_KEY_FAIL_NOT_ESTAB);
				} else {
					set_sense_type(chip, lun, SENSE_TYPE_MG_WRITE_ERR);
				}
			} else {
				set_sense_type(chip, lun, SENSE_TYPE_MG_WRITE_ERR);
			}
			retval = STATUS_FAIL;
			TRACE_GOTO(chip, SetICVFinish);
		}
	}
#else
	retval = ms_transfer_data(chip, MS_TM_AUTO_WRITE, PRO_WRITE_LONG_DATA,
				2, WAIT_INT, 0, 0, buf + 4, 1024);
	if ((retval != STATUS_SUCCESS) || check_ms_err(chip) {
		rtsx_clear_ms_error(chip);
		if (ms_card->mg_auth == 0) {
			if ((buf[5] & 0xC0) != 0) {
				set_sense_type(chip, lun, SENSE_TYPE_MG_KEY_FAIL_NOT_ESTAB);
			} else {
				set_sense_type(chip, lun, SENSE_TYPE_MG_WRITE_ERR);
			}
		} else {
			set_sense_type(chip, lun, SENSE_TYPE_MG_WRITE_ERR);
		}
		TRACE_GOTO(chip, SetICVFinish);
	}
#endif

SetICVFinish:
	kfree(buf);
	return retval;
}

#endif /* SUPPORT_MAGIC_GATE */

void ms_cleanup_work(struct rtsx_chip *chip)
{
	struct ms_info *ms_card = &(chip->ms_card);

	if (CHK_MSPRO(ms_card)) {
		if (ms_card->seq_mode) {
			RTSX_DEBUGP("MS Pro: stop transmission\n");
			mspro_stop_seq_mode(chip);
			ms_card->cleanup_counter = 0;
		}
		if (CHK_MSHG(ms_card)) {
			rtsx_write_register(chip, MS_CFG,
				MS_2K_SECTOR_MODE, 0x00);
		}
	}
#ifdef MS_DELAY_WRITE
	else if ((!CHK_MSPRO(ms_card)) && ms_card->delay_write.delay_write_flag) {
		RTSX_DEBUGP("MS: delay write\n");
		ms_delay_write(chip);
		ms_card->cleanup_counter = 0;
	}
#endif
}

int ms_power_off_card3v3(struct rtsx_chip *chip)
{
	int retval;

	retval = disable_card_clock(chip, MS_CARD);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}
	if (chip->asic_code) {
		retval = ms_pull_ctl_disable(chip);
		if (retval != STATUS_SUCCESS) {
			TRACE_RET(chip, STATUS_FAIL);
		}
	} else {
		RTSX_WRITE_REG(chip, FPGA_PULL_CTL,
			FPGA_MS_PULL_CTL_BIT | 0x20, FPGA_MS_PULL_CTL_BIT);
	}
	RTSX_WRITE_REG(chip, CARD_OE, MS_OUTPUT_EN, 0);
	if (!chip->ft2_fast_mode) {
		retval = card_power_off(chip, MS_CARD);
		if (retval != STATUS_SUCCESS) {
			TRACE_RET(chip, STATUS_FAIL);
		}
	}

	return STATUS_SUCCESS;
}

int release_ms_card(struct rtsx_chip *chip)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int retval;

	RTSX_DEBUGP("release_ms_card\n");

#ifdef MS_DELAY_WRITE
	ms_card->delay_write.delay_write_flag = 0;
#endif
	ms_card->pro_under_formatting = 0;

	chip->card_ready &= ~MS_CARD;
	chip->card_fail &= ~MS_CARD;
	chip->card_wp &= ~MS_CARD;

	ms_free_l2p_tbl(chip);

	memset(ms_card->raw_sys_info, 0, 96);
#ifdef SUPPORT_PCGL_1P18
	memset(ms_card->raw_model_name, 0, 48);
#endif

	retval = ms_power_off_card3v3(chip);
	if (retval != STATUS_SUCCESS) {
		TRACE_RET(chip, STATUS_FAIL);
	}

	return STATUS_SUCCESS;
}

