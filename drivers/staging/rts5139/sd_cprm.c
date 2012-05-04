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
#include <linux/slab.h>

#include "debug.h"
#include "trace.h"
#include "rts51x.h"
#include "rts51x_transport.h"
#include "rts51x_scsi.h"
#include "rts51x_card.h"
#include "rts51x_chip.h"
#include "sd.h"

#ifdef SUPPORT_CPRM

static inline int get_rsp_type(u8 rsp_code, u8 *rsp_type, int *rsp_len)
{
	if (!rsp_type || !rsp_len)
		return STATUS_FAIL;

	switch (rsp_code) {
	case 0x03:
		*rsp_type = SD_RSP_TYPE_R0; /* no response */
		*rsp_len = 0;
		break;

	case 0x04:
		*rsp_type = SD_RSP_TYPE_R1; /* R1,R6(,R4,R5) */
		*rsp_len = 6;
		break;

	case 0x05:
		*rsp_type = SD_RSP_TYPE_R1b;	/* R1b */
		*rsp_len = 6;
		break;

	case 0x06:
		*rsp_type = SD_RSP_TYPE_R2;	/* R2 */
		*rsp_len = 17;
		break;

	case 0x07:
		*rsp_type = SD_RSP_TYPE_R3;	/* R3 */
		*rsp_len = 6;
		break;

	default:
		return STATUS_FAIL;
	}

	return STATUS_SUCCESS;
}

int soft_reset_sd_card(struct rts51x_chip *chip)
{
	return reset_sd(chip);
}

int ext_sd_send_cmd_get_rsp(struct rts51x_chip *chip, u8 cmd_idx,
			    u32 arg, u8 rsp_type, u8 *rsp, int rsp_len,
			    int special_check)
{
	int retval;
	int timeout = 50;
	u16 reg_addr;
	u8 buf[17], stat;
	int len = 2;
	int rty_cnt = 0;

	RTS51X_DEBUGP("EXT SD/MMC CMD %d\n", cmd_idx);

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
	rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_DATA_SOURCE,
		       0x01, PINGPONG_BUFFER);
	rts51x_add_cmd(chip, WRITE_REG_CMD, SD_TRANSFER,
		       0xFF, SD_TM_CMD_RSP | SD_TRANSFER_START);
	rts51x_add_cmd(chip, CHECK_REG_CMD, SD_TRANSFER, SD_TRANSFER_END,
		       SD_TRANSFER_END);

	rts51x_add_cmd(chip, READ_REG_CMD, SD_STAT1, 0, 0);

	if (CHECK_USB(chip, USB_20)) {
		if (rsp_type == SD_RSP_TYPE_R2) {
			for (reg_addr = PPBUF_BASE2;
			     reg_addr < PPBUF_BASE2 + 16; reg_addr++) {
				rts51x_add_cmd(chip, READ_REG_CMD, reg_addr, 0,
					       0);
			}
			len = 19;
		} else if (rsp_type != SD_RSP_TYPE_R0) {
			/* Read data from SD_CMDx registers */
			for (reg_addr = SD_CMD0; reg_addr <= SD_CMD4;
			     reg_addr++) {
				rts51x_add_cmd(chip, READ_REG_CMD, reg_addr, 0,
					       0);
			}
			len = 8;
		} else {
			len = 3;
		}
		rts51x_add_cmd(chip, READ_REG_CMD, SD_CMD5, 0, 0);
	} else {
		len = 2;
	}

	retval = rts51x_send_cmd(chip, MODE_CR, 100);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	retval = rts51x_get_rsp(chip, len, timeout);

	if (CHECK_SD_TRANS_FAIL(chip, retval)) {
		rts51x_clear_sd_error(chip);

		if (retval == STATUS_TIMEDOUT) {
			if (rsp_type & SD_WAIT_BUSY_END) {
				retval = sd_check_data0_status(chip);
				if (retval != STATUS_SUCCESS)
					TRACE_RET(chip, retval);
			}
		}
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
		retval =
		    rts51x_seq_read_register(chip, reg_addr,
						     (unsigned short)len, buf);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, retval);
		RTS51X_READ_REG(chip, SD_CMD5, buf + len);
	}
	stat = chip->rsp_buf[1];

	if ((buf[0] & 0xC0) != 0)
		TRACE_RET(chip, STATUS_FAIL);

	if (!(rsp_type & SD_NO_CHECK_CRC7)) {
		if (stat & SD_CRC7_ERR) {
			if (cmd_idx == WRITE_MULTIPLE_BLOCK)
				TRACE_RET(chip, STATUS_FAIL);
			if (rty_cnt < SD_MAX_RETRY_COUNT) {
				wait_timeout(20);
				rty_cnt++;
				goto RTY_SEND_CMD;
			} else {
				TRACE_RET(chip, STATUS_FAIL);
			}
		}
	}

	if ((cmd_idx == SELECT_CARD) || (cmd_idx == APP_CMD) ||
	    (cmd_idx == SEND_STATUS) || (cmd_idx == STOP_TRANSMISSION)) {
		if ((cmd_idx != STOP_TRANSMISSION) && (special_check == 0)) {
			if (buf[1] & 0x80)
				TRACE_RET(chip, STATUS_FAIL);
		}
		if (buf[1] & 0x7F) {
			TRACE_RET(chip, STATUS_FAIL);
		}
		if (buf[2] & 0xF8)
			TRACE_RET(chip, STATUS_FAIL);

		if (cmd_idx == SELECT_CARD) {
			if (rsp_type == SD_RSP_TYPE_R2) {
				if ((buf[3] & 0x1E) != 0x04)
					TRACE_RET(chip, STATUS_FAIL);
			} else if (rsp_type == SD_RSP_TYPE_R2) {
				if ((buf[3] & 0x1E) != 0x03)
					TRACE_RET(chip, STATUS_FAIL);
			}
		}
	}

	if (rsp && rsp_len)
		memcpy(rsp, buf, rsp_len);

	return STATUS_SUCCESS;
}

int ext_sd_get_rsp(struct rts51x_chip *chip, int len, u8 *rsp, u8 rsp_type)
{
	int retval, rsp_len;
	u16 reg_addr;

	if (rsp_type == SD_RSP_TYPE_R0)
		return STATUS_SUCCESS;

	rts51x_init_cmd(chip);

	if (rsp_type == SD_RSP_TYPE_R2) {
		for (reg_addr = PPBUF_BASE2; reg_addr < PPBUF_BASE2 + 16;
		     reg_addr++) {
			rts51x_add_cmd(chip, READ_REG_CMD, reg_addr, 0xFF, 0);
		}
		rsp_len = 17;
	} else if (rsp_type != SD_RSP_TYPE_R0) {
		for (reg_addr = SD_CMD0; reg_addr <= SD_CMD4; reg_addr++)
			rts51x_add_cmd(chip, READ_REG_CMD, reg_addr, 0xFF, 0);
		rsp_len = 6;
	}
	rts51x_add_cmd(chip, READ_REG_CMD, SD_CMD5, 0xFF, 0);

	retval = rts51x_send_cmd(chip, MODE_CR, 100);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	retval = rts51x_get_rsp(chip, rsp_len, 100);

	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	if (rsp) {
		int min_len = (rsp_len < len) ? rsp_len : len;

		memcpy(rsp, rts51x_get_rsp_data(chip), min_len);

		RTS51X_DEBUGP("min_len = %d\n", min_len);
		RTS51X_DEBUGP("Response in cmd buf: 0x%x 0x%x 0x%x 0x%x\n",
			       rsp[0], rsp[1], rsp[2], rsp[3]);
	}

	return STATUS_SUCCESS;
}

int ext_sd_execute_no_data(struct rts51x_chip *chip, unsigned int lun,
			   u8 cmd_idx, u8 standby, u8 acmd, u8 rsp_code,
			   u32 arg)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval, rsp_len;
	u8 rsp_type;

	retval = sd_switch_clock(chip);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, TRANSPORT_FAILED);

	if (sd_card->pre_cmd_err) {
		sd_card->pre_cmd_err = 0;
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_CHANGE);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}
	retval = get_rsp_type(rsp_code, &rsp_type, &rsp_len);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}
	sd_card->last_rsp_type = rsp_type;

	retval = sd_switch_clock(chip);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, TRANSPORT_FAILED);
	/* Set H/W SD/MMC Bus Width */
	rts51x_write_register(chip, SD_CFG1, 0x03, SD_BUS_WIDTH_4);

	if (standby) {
		retval = sd_select_card(chip, 0);
		if (retval != STATUS_SUCCESS)
			TRACE_GOTO(chip, SD_Execute_Cmd_Failed);
	}

	if (acmd) {
		retval =
		    ext_sd_send_cmd_get_rsp(chip, APP_CMD, sd_card->sd_addr,
					    SD_RSP_TYPE_R1, NULL, 0, 0);
		if (retval != STATUS_SUCCESS)
			TRACE_GOTO(chip, SD_Execute_Cmd_Failed);
	}

	retval = ext_sd_send_cmd_get_rsp(chip, cmd_idx, arg, rsp_type,
					 sd_card->rsp, rsp_len, 0);
	if (retval != STATUS_SUCCESS)
		TRACE_GOTO(chip, SD_Execute_Cmd_Failed);

	if (standby) {
		retval = sd_select_card(chip, 1);
		if (retval != STATUS_SUCCESS)
			TRACE_GOTO(chip, SD_Execute_Cmd_Failed);
	}

	return TRANSPORT_GOOD;

SD_Execute_Cmd_Failed:
	sd_card->pre_cmd_err = 1;
	set_sense_type(chip, lun, SENSE_TYPE_NO_SENSE);
	release_sd_card(chip);
	do_reset_sd_card(chip);
	if (!(chip->card_ready & SD_CARD))
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_NOT_PRESENT);

	TRACE_RET(chip, TRANSPORT_FAILED);
}

int ext_sd_execute_read_data(struct rts51x_chip *chip, unsigned int lun,
			     u8 cmd_idx, u8 cmd12, u8 standby,
			     u8 acmd, u8 rsp_code, u32 arg, u32 data_len,
			     void *data_buf, unsigned int buf_len, int use_sg)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval, rsp_len, i;
	int cmd13_checkbit = 0, read_err = 0;
	u8 rsp_type, bus_width;

	if (sd_card->pre_cmd_err) {
		sd_card->pre_cmd_err = 0;
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_CHANGE);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	retval = sd_switch_clock(chip);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, STATUS_FAIL);
	retval = get_rsp_type(rsp_code, &rsp_type, &rsp_len);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}
	sd_card->last_rsp_type = rsp_type;

	retval = sd_switch_clock(chip);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, TRANSPORT_FAILED);
	bus_width = SD_BUS_WIDTH_4;

	if (data_len < 512) {
		retval = ext_sd_send_cmd_get_rsp(chip, SET_BLOCKLEN, data_len,
						 SD_RSP_TYPE_R1, NULL, 0, 0);
		if (retval != STATUS_SUCCESS)
			TRACE_GOTO(chip, SD_Execute_Read_Cmd_Failed);
	}

	if (standby) {
		retval = sd_select_card(chip, 0);
		if (retval != STATUS_SUCCESS)
			TRACE_GOTO(chip, SD_Execute_Read_Cmd_Failed);
	}

	if (acmd) {
		retval =
		    ext_sd_send_cmd_get_rsp(chip, APP_CMD, sd_card->sd_addr,
					    SD_RSP_TYPE_R1, NULL, 0, 0);
		if (retval != STATUS_SUCCESS)
			TRACE_GOTO(chip, SD_Execute_Read_Cmd_Failed);
	}

	if (data_len <= 512) {
		int min_len;
		u8 *buf;
		u16 byte_cnt, blk_cnt;
		u8 cmd[5];
		unsigned int offset = 0;
		void *sg = NULL;

		byte_cnt = (u16) (data_len & 0x3FF);
		blk_cnt = 1;

		cmd[0] = 0x40 | cmd_idx;
		cmd[1] = (u8) (arg >> 24);
		cmd[2] = (u8) (arg >> 16);
		cmd[3] = (u8) (arg >> 8);
		cmd[4] = (u8) arg;

		buf = kmalloc(data_len, GFP_KERNEL);
		if (buf == NULL)
			TRACE_RET(chip, TRANSPORT_ERROR);

		retval = sd_read_data(chip, SD_TM_NORMAL_READ, cmd, 5, byte_cnt,
				      blk_cnt, bus_width, buf, data_len, 2000);
		if (retval != STATUS_SUCCESS) {
			read_err = 1;
			kfree(buf);
			rts51x_write_register(chip, CARD_STOP,
					      SD_STOP | SD_CLR_ERR,
					      SD_STOP | SD_CLR_ERR);
			TRACE_GOTO(chip, SD_Execute_Read_Cmd_Failed);
		}

		min_len = min(data_len, buf_len);
		if (use_sg)
			rts51x_access_sglist(buf, min_len, (void *)data_buf,
					     &sg, &offset, TO_XFER_BUF);
		else
			memcpy(data_buf, buf, min_len);

		kfree(buf);
	} else if (!(data_len & 0x1FF)) {
		rts51x_init_cmd(chip);
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_BYTE_CNT_H, 0xFF, 0x02);
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_BYTE_CNT_L, 0xFF, 0x00);
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_BLOCK_CNT_H,
			       0xFF, (u8) (data_len >> 17));
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_BLOCK_CNT_L,
			       0xFF, (u8) ((data_len & 0x0001FE00) >> 9));
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CMD0, 0xFF,
			       0x40 | cmd_idx);
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CMD1, 0xFF,
			       (u8) (arg >> 24));
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CMD2, 0xFF,
			       (u8) (arg >> 16));
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CMD3, 0xFF,
			       (u8) (arg >> 8));
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CMD4, 0xFF, (u8) arg);
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CFG1, 0x03, bus_width);
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_CFG2, 0xFF, rsp_type);
		trans_dma_enable(DMA_FROM_DEVICE, chip, data_len, DMA_512);
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_TRANSFER, 0xFF,
			       SD_TM_AUTO_READ_2 | SD_TRANSFER_START);
		rts51x_add_cmd(chip, CHECK_REG_CMD, SD_TRANSFER,
			       SD_TRANSFER_END, SD_TRANSFER_END);
		retval = rts51x_send_cmd(chip, MODE_CDIR, 100);
		if (retval != STATUS_SUCCESS) {
			read_err = 1;
			rts51x_ep0_write_register(chip, CARD_STOP,
						  SD_STOP | SD_CLR_ERR,
						  SD_STOP | SD_CLR_ERR);
			TRACE_GOTO(chip, SD_Execute_Read_Cmd_Failed);
		}

		retval =
		    rts51x_transfer_data_rcc(chip, RCV_BULK_PIPE(chip),
					     data_buf, buf_len, use_sg, NULL,
					     10000, STAGE_DI);
		if (retval != STATUS_SUCCESS) {
			read_err = 1;
			rts51x_ep0_write_register(chip, CARD_STOP,
						  SD_STOP | SD_CLR_ERR,
						  SD_STOP | SD_CLR_ERR);
			TRACE_GOTO(chip, SD_Execute_Read_Cmd_Failed);
		}
		retval = rts51x_get_rsp(chip, 1, 500);
		if (CHECK_SD_TRANS_FAIL(chip, retval)) {
			read_err = 1;
			rts51x_ep0_write_register(chip, CARD_STOP,
						  SD_STOP | SD_CLR_ERR,
						  SD_STOP | SD_CLR_ERR);
			TRACE_GOTO(chip, SD_Execute_Read_Cmd_Failed);
		}
	} else {
		TRACE_GOTO(chip, SD_Execute_Read_Cmd_Failed);
	}

	retval = ext_sd_get_rsp(chip, rsp_len, sd_card->rsp, rsp_type);
	if (retval != STATUS_SUCCESS)
		TRACE_GOTO(chip, SD_Execute_Read_Cmd_Failed);

	if (standby) {
		retval = sd_select_card(chip, 1);
		if (retval != STATUS_SUCCESS)
			TRACE_GOTO(chip, SD_Execute_Read_Cmd_Failed);
	}

	if (cmd12) {
		retval = ext_sd_send_cmd_get_rsp(chip, STOP_TRANSMISSION,
						 0, SD_RSP_TYPE_R1b, NULL, 0,
						 0);
		if (retval != STATUS_SUCCESS)
			TRACE_GOTO(chip, SD_Execute_Read_Cmd_Failed);
	}

	if (data_len < 512) {
		retval = ext_sd_send_cmd_get_rsp(chip, SET_BLOCKLEN, 0x200,
						 SD_RSP_TYPE_R1, NULL, 0, 0);
		if (retval != STATUS_SUCCESS)
			TRACE_GOTO(chip, SD_Execute_Read_Cmd_Failed);

		rts51x_write_register(chip, SD_BYTE_CNT_H, 0xFF, 0x02);
		rts51x_write_register(chip, SD_BYTE_CNT_L, 0xFF, 0x00);
	}

	if (standby || cmd12)
		cmd13_checkbit = 1;

	for (i = 0; i < 3; i++) {
		retval =
		    ext_sd_send_cmd_get_rsp(chip, SEND_STATUS, sd_card->sd_addr,
					    SD_RSP_TYPE_R1, NULL, 0,
					    cmd13_checkbit);
		if (retval == STATUS_SUCCESS)
			break;
	}
	if (retval != STATUS_SUCCESS)
		TRACE_GOTO(chip, SD_Execute_Read_Cmd_Failed);

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

	TRACE_RET(chip, TRANSPORT_FAILED);
}

int ext_sd_execute_write_data(struct rts51x_chip *chip, unsigned int lun,
			      u8 cmd_idx, u8 cmd12, u8 standby, u8 acmd,
			      u8 rsp_code, u32 arg, u32 data_len,
			      void *data_buf, unsigned int buf_len, int use_sg)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval, rsp_len;
	int cmd13_checkbit = 0, write_err = 0;
	u8 rsp_type;
	u32 i;

	if (sd_card->pre_cmd_err) {
		sd_card->pre_cmd_err = 0;
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_CHANGE);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	retval = sd_switch_clock(chip);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, STATUS_FAIL);

	retval = get_rsp_type(rsp_code, &rsp_type, &rsp_len);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}
	sd_card->last_rsp_type = rsp_type;

	retval = sd_switch_clock(chip);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, TRANSPORT_FAILED);
	rts51x_write_register(chip, SD_CFG1, 0x03, SD_BUS_WIDTH_4);

	if (data_len < 512) {
		retval = ext_sd_send_cmd_get_rsp(chip, SET_BLOCKLEN, data_len,
						 SD_RSP_TYPE_R1, NULL, 0, 0);
		if (retval != STATUS_SUCCESS)
			TRACE_GOTO(chip, SD_Execute_Write_Cmd_Failed);
	}

	if (standby) {
		retval = sd_select_card(chip, 0);
		if (retval != STATUS_SUCCESS)
			TRACE_GOTO(chip, SD_Execute_Write_Cmd_Failed);
	}

	if (acmd) {
		retval =
		    ext_sd_send_cmd_get_rsp(chip, APP_CMD, sd_card->sd_addr,
					    SD_RSP_TYPE_R1, NULL, 0, 0);
		if (retval != STATUS_SUCCESS)
			TRACE_GOTO(chip, SD_Execute_Write_Cmd_Failed);
	}

	retval = ext_sd_send_cmd_get_rsp(chip, cmd_idx, arg, rsp_type,
					 sd_card->rsp, rsp_len, 0);
	if (retval != STATUS_SUCCESS)
		TRACE_GOTO(chip, SD_Execute_Write_Cmd_Failed);

	if (data_len <= 512) {
		u8 *buf;
		unsigned int offset = 0;
		void *sg = NULL;

		buf = kmalloc(data_len, GFP_KERNEL);
		if (buf == NULL)
			TRACE_RET(chip, TRANSPORT_ERROR);

		if (use_sg)
			rts51x_access_sglist(buf, data_len, (void *)data_buf,
					     &sg, &offset, FROM_XFER_BUF);
		else
			memcpy(buf, data_buf, data_len);


		if (data_len > 256) {
			rts51x_init_cmd(chip);
			for (i = 0; i < 256; i++) {
				rts51x_add_cmd(chip, WRITE_REG_CMD,
					       (u16) (PPBUF_BASE2 + i), 0xFF,
					       buf[i]);
			}
			retval = rts51x_send_cmd(chip, MODE_C, 250);
			if (retval != STATUS_SUCCESS) {
				kfree(buf);
				TRACE_GOTO(chip, SD_Execute_Write_Cmd_Failed);
			}

			rts51x_init_cmd(chip);
			for (i = 256; i < data_len; i++) {
				rts51x_add_cmd(chip, WRITE_REG_CMD,
					       (u16) (PPBUF_BASE2 + i), 0xFF,
					       buf[i]);
			}
			retval = rts51x_send_cmd(chip, MODE_C, 250);
			if (retval != STATUS_SUCCESS) {
				kfree(buf);
				TRACE_GOTO(chip, SD_Execute_Write_Cmd_Failed);
			}
		} else {
			rts51x_init_cmd(chip);
			for (i = 0; i < data_len; i++) {
				rts51x_add_cmd(chip, WRITE_REG_CMD,
					       (u16) (PPBUF_BASE2 + i), 0xFF,
					       buf[i]);
			}
			retval = rts51x_send_cmd(chip, MODE_C, 250);
			if (retval != STATUS_SUCCESS) {
				kfree(buf);
				TRACE_GOTO(chip, SD_Execute_Write_Cmd_Failed);
			}
		}

		kfree(buf);

		rts51x_init_cmd(chip);

		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_BYTE_CNT_H, 0xFF,
			       (u8) ((data_len >> 8) & 0x03));
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_BYTE_CNT_L, 0xFF,
			       (u8) data_len);
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_BLOCK_CNT_H, 0xFF, 0x00);
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_BLOCK_CNT_L, 0xFF, 0x01);
		rts51x_add_cmd(chip, WRITE_REG_CMD, CARD_DATA_SOURCE, 0x01,
			       PINGPONG_BUFFER);

		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_TRANSFER, 0xFF,
			       SD_TM_AUTO_WRITE_3 | SD_TRANSFER_START);
		rts51x_add_cmd(chip, CHECK_REG_CMD, SD_TRANSFER,
			       SD_TRANSFER_END, SD_TRANSFER_END);

		retval = rts51x_send_cmd(chip, MODE_CR, 100);
		if (retval != STATUS_SUCCESS)
			TRACE_GOTO(chip, SD_Execute_Write_Cmd_Failed);

		retval = rts51x_get_rsp(chip, 1, 250);
		if (CHECK_SD_TRANS_FAIL(chip, retval))
			TRACE_GOTO(chip, SD_Execute_Write_Cmd_Failed);
	} else if (!(data_len & 0x1FF)) {
		rts51x_init_cmd(chip);

		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_BYTE_CNT_H, 0xFF, 0x02);
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_BYTE_CNT_L, 0xFF, 0x00);
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_BLOCK_CNT_H,
			       0xFF, (u8) (data_len >> 17));
		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_BLOCK_CNT_L,
			       0xFF, (u8) ((data_len & 0x0001FE00) >> 9));

		trans_dma_enable(DMA_TO_DEVICE, chip, data_len, DMA_512);

		rts51x_add_cmd(chip, WRITE_REG_CMD, SD_TRANSFER, 0xFF,
			       SD_TM_AUTO_WRITE_3 | SD_TRANSFER_START);
		rts51x_add_cmd(chip, CHECK_REG_CMD, SD_TRANSFER,
			       SD_TRANSFER_END, SD_TRANSFER_END);

		retval = rts51x_send_cmd(chip, MODE_CDOR, 100);
		if (retval != STATUS_SUCCESS)
			TRACE_GOTO(chip, SD_Execute_Write_Cmd_Failed);

		retval =
		    rts51x_transfer_data_rcc(chip, SND_BULK_PIPE(chip),
					     data_buf, buf_len, use_sg, NULL,
					     10000, STAGE_DO);
		if (retval != STATUS_SUCCESS)
			TRACE_GOTO(chip, SD_Execute_Write_Cmd_Failed);

		retval = rts51x_get_rsp(chip, 1, 10000);
		if (CHECK_SD_TRANS_FAIL(chip, retval))
			TRACE_GOTO(chip, SD_Execute_Write_Cmd_Failed);

	} else {
		TRACE_GOTO(chip, SD_Execute_Write_Cmd_Failed);
	}

	if (retval < 0) {
		write_err = 1;
		rts51x_write_register(chip, CARD_STOP, SD_STOP | SD_CLR_ERR,
				      SD_STOP | SD_CLR_ERR);
		TRACE_GOTO(chip, SD_Execute_Write_Cmd_Failed);
	}

	if (standby) {
		retval = sd_select_card(chip, 1);
		if (retval != STATUS_SUCCESS)
			TRACE_GOTO(chip, SD_Execute_Write_Cmd_Failed);
	}

	if (cmd12) {
		retval = ext_sd_send_cmd_get_rsp(chip, STOP_TRANSMISSION,
						 0, SD_RSP_TYPE_R1b, NULL, 0,
						 0);
		if (retval != STATUS_SUCCESS)
			TRACE_GOTO(chip, SD_Execute_Write_Cmd_Failed);
	}

	if (data_len < 512) {
		retval = ext_sd_send_cmd_get_rsp(chip, SET_BLOCKLEN, 0x200,
						 SD_RSP_TYPE_R1, NULL, 0, 0);
		if (retval != STATUS_SUCCESS)
			TRACE_GOTO(chip, SD_Execute_Write_Cmd_Failed);

		rts51x_write_register(chip, SD_BYTE_CNT_H, 0xFF, 0x02);
		rts51x_write_register(chip, SD_BYTE_CNT_L, 0xFF, 0x00);
	}

	if (cmd12 || standby) {
		/* There is CMD7 or CMD12 sent before CMD13 */
		cmd13_checkbit = 1;
	}

	for (i = 0; i < 3; i++) {
		retval =
		    ext_sd_send_cmd_get_rsp(chip, SEND_STATUS, sd_card->sd_addr,
					    SD_RSP_TYPE_R1, NULL, 0,
					    cmd13_checkbit);
		if (retval == STATUS_SUCCESS)
			break;
	}
	if (retval != STATUS_SUCCESS)
		TRACE_GOTO(chip, SD_Execute_Write_Cmd_Failed);

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

	TRACE_RET(chip, TRANSPORT_FAILED);
}

int sd_pass_thru_mode(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	unsigned int lun = SCSI_LUN(srb);
	int len;
	u8 buf[18] = {
		0x00,
		0x00,
		0x00,
		0x0E,
		0x00,		/* Version Number */
		0x00,		/* WP | Media Type */
		0x00,		/* RCA (Low byte) */
		0x00,		/* RCA (High byte) */
		0x53,		/* 'S' */
		0x44,		/* 'D' */
		0x20,		/* ' ' */
		0x43,		/* 'C' */
		0x61,		/* 'a' */
		0x72,		/* 'r' */
		0x64,		/* 'd' */
		0x00,		/* Max LUN Number */
		0x00,
		0x00,
	};

	sd_card->pre_cmd_err = 0;

	if (!(CHK_BIT(chip->lun_mc, lun))) {
		SET_BIT(chip->lun_mc, lun);
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_CHANGE);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	if ((0x53 != srb->cmnd[2]) || (0x44 != srb->cmnd[3])
	    || (0x20 != srb->cmnd[4]) || (0x43 != srb->cmnd[5])
	    || (0x61 != srb->cmnd[6]) || (0x72 != srb->cmnd[7])
	    || (0x64 != srb->cmnd[8])) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		TRACE_RET(chip, TRANSPORT_FAILED);
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
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	/* 0x01:SD Memory Card; 0x02:Other Media; 0x03:Illegal Media; */
	buf[5] = (1 == CHK_SD(sd_card)) ? 0x01 : 0x02;
	if (chip->card_wp & SD_CARD)
		buf[5] |= 0x80;

	buf[6] = (u8) (sd_card->sd_addr >> 16);
	buf[7] = (u8) (sd_card->sd_addr >> 24);

	buf[15] = chip->max_lun;

	len = min(18, (int)scsi_bufflen(srb));
	rts51x_set_xfer_buf(buf, len, srb);

	return TRANSPORT_GOOD;
}

int sd_execute_no_data(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	unsigned int lun = SCSI_LUN(srb);
	int retval;
	u8 cmd_idx, rsp_code;
	u8 standby = 0, acmd = 0;
	u32 arg;

	if (!sd_card->sd_pass_thru_en) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	cmd_idx = srb->cmnd[2] & 0x3F;
	if (srb->cmnd[1] & 0x02)
		standby = 1;
	if (srb->cmnd[1] & 0x01)
		acmd = 1;

	arg = ((u32) srb->cmnd[3] << 24) | ((u32) srb->cmnd[4] << 16) |
	    ((u32) srb->cmnd[5] << 8) | srb->cmnd[6];

	rsp_code = srb->cmnd[10];

	retval =
	    ext_sd_execute_no_data(chip, lun, cmd_idx, standby, acmd, rsp_code,
				   arg);
	scsi_set_resid(srb, 0);
	return retval;
}

int sd_execute_read_data(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;
	unsigned int lun = SCSI_LUN(srb);
	u8 cmd_idx, rsp_code, send_cmd12 = 0, standby = 0, acmd = 0;
	u32 arg, data_len;

	if (!sd_card->sd_pass_thru_en) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	cmd_idx = srb->cmnd[2] & 0x3F;
	if (srb->cmnd[1] & 0x04)
		send_cmd12 = 1;
	if (srb->cmnd[1] & 0x02)
		standby = 1;
	if (srb->cmnd[1] & 0x01)
		acmd = 1;

	arg = ((u32) srb->cmnd[3] << 24) | ((u32) srb->cmnd[4] << 16) |
	    ((u32) srb->cmnd[5] << 8) | srb->cmnd[6];

	data_len =
	    ((u32) srb->cmnd[7] << 16) | ((u32) srb->cmnd[8] << 8) |
	    srb->cmnd[9];
	rsp_code = srb->cmnd[10];

	retval =
	    ext_sd_execute_read_data(chip, lun, cmd_idx, send_cmd12, standby,
				     acmd, rsp_code, arg, data_len,
				     scsi_sglist(srb), scsi_bufflen(srb),
				     scsi_sg_count(srb));
	scsi_set_resid(srb, 0);
	return retval;
}

int sd_execute_write_data(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int retval;
	unsigned int lun = SCSI_LUN(srb);
	u8 cmd_idx, rsp_code, send_cmd12 = 0, standby = 0, acmd = 0;
	u32 data_len, arg;

	if (!sd_card->sd_pass_thru_en) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	cmd_idx = srb->cmnd[2] & 0x3F;
	if (srb->cmnd[1] & 0x04)
		send_cmd12 = 1;
	if (srb->cmnd[1] & 0x02)
		standby = 1;
	if (srb->cmnd[1] & 0x01)
		acmd = 1;

	data_len =
	    ((u32) srb->cmnd[7] << 16) | ((u32) srb->cmnd[8] << 8) |
	    srb->cmnd[9];
	arg =
	    ((u32) srb->cmnd[3] << 24) | ((u32) srb->cmnd[4] << 16) |
	    ((u32) srb->cmnd[5] << 8) | srb->cmnd[6];
	rsp_code = srb->cmnd[10];

	retval =
	    ext_sd_execute_write_data(chip, lun, cmd_idx, send_cmd12, standby,
				      acmd, rsp_code, arg, data_len,
				      scsi_sglist(srb), scsi_bufflen(srb),
				      scsi_sg_count(srb));
	scsi_set_resid(srb, 0);
	return retval;
}

int sd_get_cmd_rsp(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	unsigned int lun = SCSI_LUN(srb);
	int count;
	u16 data_len;

	if (!sd_card->sd_pass_thru_en) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	if (sd_card->pre_cmd_err) {
		sd_card->pre_cmd_err = 0;
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_CHANGE);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	data_len = ((u16) srb->cmnd[7] << 8) | srb->cmnd[8];

	if (sd_card->last_rsp_type == SD_RSP_TYPE_R0) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		TRACE_RET(chip, TRANSPORT_FAILED);
	} else if (sd_card->last_rsp_type == SD_RSP_TYPE_R2) {
		count = (data_len < 17) ? data_len : 17;
	} else {
		count = (data_len < 6) ? data_len : 6;
	}
	rts51x_set_xfer_buf(sd_card->rsp, count, srb);

	RTS51X_DEBUGP("Response length: %d\n", data_len);
	RTS51X_DEBUGP("Response: 0x%x 0x%x 0x%x 0x%x\n",
		       sd_card->rsp[0], sd_card->rsp[1], sd_card->rsp[2],
		       sd_card->rsp[3]);

	scsi_set_resid(srb, 0);
	return TRANSPORT_GOOD;
}

int sd_hw_rst(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	unsigned int lun = SCSI_LUN(srb);
	int retval;

	if (!sd_card->sd_pass_thru_en) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	if (sd_card->pre_cmd_err) {
		sd_card->pre_cmd_err = 0;
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_CHANGE);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	if ((0x53 != srb->cmnd[2]) || (0x44 != srb->cmnd[3])
	    || (0x20 != srb->cmnd[4]) || (0x43 != srb->cmnd[5])
	    || (0x61 != srb->cmnd[6]) || (0x72 != srb->cmnd[7])
	    || (0x64 != srb->cmnd[8])) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	switch (srb->cmnd[1] & 0x0F) {
	case 0:
		/* SD Card Power Off -> ON and Initialization */
		retval = reset_sd_card(chip);
		if (retval != STATUS_SUCCESS) {
			set_sense_type(chip, lun, SENSE_TYPE_MEDIA_NOT_PRESENT);
			sd_card->pre_cmd_err = 1;
			TRACE_RET(chip, TRANSPORT_FAILED);
		}
		break;

	case 1:
		/* reset CMD(CMD0) and Initialization
		 * (without SD Card Power Off -> ON) */
		retval = soft_reset_sd_card(chip);
		if (retval != STATUS_SUCCESS) {
			set_sense_type(chip, lun, SENSE_TYPE_MEDIA_NOT_PRESENT);
			sd_card->pre_cmd_err = 1;
			TRACE_RET(chip, TRANSPORT_FAILED);
		}
		break;

	default:
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	scsi_set_resid(srb, 0);
	return TRANSPORT_GOOD;
}
#endif
