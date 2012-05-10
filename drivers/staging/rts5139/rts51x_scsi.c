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
#include <linux/vmalloc.h>
#include <linux/export.h>

#include <scsi/scsi.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_device.h>

#include "debug.h"
#include "rts51x.h"
#include "rts51x_chip.h"
#include "rts51x_scsi.h"
#include "rts51x_card.h"
#include "rts51x_transport.h"
#include "sd_cprm.h"
#include "ms_mg.h"
#include "trace.h"

void scsi_show_command(struct scsi_cmnd *srb)
{
	char *what = NULL;
	int i, unknown_cmd = 0;

	switch (srb->cmnd[0]) {
	case TEST_UNIT_READY:
		what = (char *)"TEST_UNIT_READY";
		break;
	case REZERO_UNIT:
		what = (char *)"REZERO_UNIT";
		break;
	case REQUEST_SENSE:
		what = (char *)"REQUEST_SENSE";
		break;
	case FORMAT_UNIT:
		what = (char *)"FORMAT_UNIT";
		break;
	case READ_BLOCK_LIMITS:
		what = (char *)"READ_BLOCK_LIMITS";
		break;
	case 0x07:
		what = (char *)"REASSIGN_BLOCKS";
		break;
	case READ_6:
		what = (char *)"READ_6";
		break;
	case WRITE_6:
		what = (char *)"WRITE_6";
		break;
	case SEEK_6:
		what = (char *)"SEEK_6";
		break;
	case READ_REVERSE:
		what = (char *)"READ_REVERSE";
		break;
	case WRITE_FILEMARKS:
		what = (char *)"WRITE_FILEMARKS";
		break;
	case SPACE:
		what = (char *)"SPACE";
		break;
	case INQUIRY:
		what = (char *)"INQUIRY";
		break;
	case RECOVER_BUFFERED_DATA:
		what = (char *)"RECOVER_BUFFERED_DATA";
		break;
	case MODE_SELECT:
		what = (char *)"MODE_SELECT";
		break;
	case RESERVE:
		what = (char *)"RESERVE";
		break;
	case RELEASE:
		what = (char *)"RELEASE";
		break;
	case COPY:
		what = (char *)"COPY";
		break;
	case ERASE:
		what = (char *)"ERASE";
		break;
	case MODE_SENSE:
		what = (char *)"MODE_SENSE";
		break;
	case START_STOP:
		what = (char *)"START_STOP";
		break;
	case RECEIVE_DIAGNOSTIC:
		what = (char *)"RECEIVE_DIAGNOSTIC";
		break;
	case SEND_DIAGNOSTIC:
		what = (char *)"SEND_DIAGNOSTIC";
		break;
	case ALLOW_MEDIUM_REMOVAL:
		what = (char *)"ALLOW_MEDIUM_REMOVAL";
		break;
	case SET_WINDOW:
		what = (char *)"SET_WINDOW";
		break;
	case READ_CAPACITY:
		what = (char *)"READ_CAPACITY";
		break;
	case READ_10:
		what = (char *)"READ_10";
		break;
	case WRITE_10:
		what = (char *)"WRITE_10";
		break;
	case SEEK_10:
		what = (char *)"SEEK_10";
		break;
	case WRITE_VERIFY:
		what = (char *)"WRITE_VERIFY";
		break;
	case VERIFY:
		what = (char *)"VERIFY";
		break;
	case SEARCH_HIGH:
		what = (char *)"SEARCH_HIGH";
		break;
	case SEARCH_EQUAL:
		what = (char *)"SEARCH_EQUAL";
		break;
	case SEARCH_LOW:
		what = (char *)"SEARCH_LOW";
		break;
	case SET_LIMITS:
		what = (char *)"SET_LIMITS";
		break;
	case READ_POSITION:
		what = (char *)"READ_POSITION";
		break;
	case SYNCHRONIZE_CACHE:
		what = (char *)"SYNCHRONIZE_CACHE";
		break;
	case LOCK_UNLOCK_CACHE:
		what = (char *)"LOCK_UNLOCK_CACHE";
		break;
	case READ_DEFECT_DATA:
		what = (char *)"READ_DEFECT_DATA";
		break;
	case MEDIUM_SCAN:
		what = (char *)"MEDIUM_SCAN";
		break;
	case COMPARE:
		what = (char *)"COMPARE";
		break;
	case COPY_VERIFY:
		what = (char *)"COPY_VERIFY";
		break;
	case WRITE_BUFFER:
		what = (char *)"WRITE_BUFFER";
		break;
	case READ_BUFFER:
		what = (char *)"READ_BUFFER";
		break;
	case UPDATE_BLOCK:
		what = (char *)"UPDATE_BLOCK";
		break;
	case READ_LONG:
		what = (char *)"READ_LONG";
		break;
	case WRITE_LONG:
		what = (char *)"WRITE_LONG";
		break;
	case CHANGE_DEFINITION:
		what = (char *)"CHANGE_DEFINITION";
		break;
	case WRITE_SAME:
		what = (char *)"WRITE_SAME";
		break;
	case GPCMD_READ_SUBCHANNEL:
		what = (char *)"READ SUBCHANNEL";
		break;
	case READ_TOC:
		what = (char *)"READ_TOC";
		break;
	case GPCMD_READ_HEADER:
		what = (char *)"READ HEADER";
		break;
	case GPCMD_PLAY_AUDIO_10:
		what = (char *)"PLAY AUDIO (10)";
		break;
	case GPCMD_PLAY_AUDIO_MSF:
		what = (char *)"PLAY AUDIO MSF";
		break;
	case GPCMD_GET_EVENT_STATUS_NOTIFICATION:
		what = (char *)"GET EVENT/STATUS NOTIFICATION";
		break;
	case GPCMD_PAUSE_RESUME:
		what = (char *)"PAUSE/RESUME";
		break;
	case LOG_SELECT:
		what = (char *)"LOG_SELECT";
		break;
	case LOG_SENSE:
		what = (char *)"LOG_SENSE";
		break;
	case GPCMD_STOP_PLAY_SCAN:
		what = (char *)"STOP PLAY/SCAN";
		break;
	case GPCMD_READ_DISC_INFO:
		what = (char *)"READ DISC INFORMATION";
		break;
	case GPCMD_READ_TRACK_RZONE_INFO:
		what = (char *)"READ TRACK INFORMATION";
		break;
	case GPCMD_RESERVE_RZONE_TRACK:
		what = (char *)"RESERVE TRACK";
		break;
	case GPCMD_SEND_OPC:
		what = (char *)"SEND OPC";
		break;
	case MODE_SELECT_10:
		what = (char *)"MODE_SELECT_10";
		break;
	case GPCMD_REPAIR_RZONE_TRACK:
		what = (char *)"REPAIR TRACK";
		break;
	case 0x59:
		what = (char *)"READ MASTER CUE";
		break;
	case MODE_SENSE_10:
		what = (char *)"MODE_SENSE_10";
		break;
	case GPCMD_CLOSE_TRACK:
		what = (char *)"CLOSE TRACK/SESSION";
		break;
	case 0x5C:
		what = (char *)"READ BUFFER CAPACITY";
		break;
	case 0x5D:
		what = (char *)"SEND CUE SHEET";
		break;
	case GPCMD_BLANK:
		what = (char *)"BLANK";
		break;
	case REPORT_LUNS:
		what = (char *)"REPORT LUNS";
		break;
	case MOVE_MEDIUM:
		what = (char *)"MOVE_MEDIUM or PLAY AUDIO (12)";
		break;
	case READ_12:
		what = (char *)"READ_12";
		break;
	case WRITE_12:
		what = (char *)"WRITE_12";
		break;
	case WRITE_VERIFY_12:
		what = (char *)"WRITE_VERIFY_12";
		break;
	case SEARCH_HIGH_12:
		what = (char *)"SEARCH_HIGH_12";
		break;
	case SEARCH_EQUAL_12:
		what = (char *)"SEARCH_EQUAL_12";
		break;
	case SEARCH_LOW_12:
		what = (char *)"SEARCH_LOW_12";
		break;
	case SEND_VOLUME_TAG:
		what = (char *)"SEND_VOLUME_TAG";
		break;
	case READ_ELEMENT_STATUS:
		what = (char *)"READ_ELEMENT_STATUS";
		break;
	case GPCMD_READ_CD_MSF:
		what = (char *)"READ CD MSF";
		break;
	case GPCMD_SCAN:
		what = (char *)"SCAN";
		break;
	case GPCMD_SET_SPEED:
		what = (char *)"SET CD SPEED";
		break;
	case GPCMD_MECHANISM_STATUS:
		what = (char *)"MECHANISM STATUS";
		break;
	case GPCMD_READ_CD:
		what = (char *)"READ CD";
		break;
	case 0xE1:
		what = (char *)"WRITE CONTINUE";
		break;
	case WRITE_LONG_2:
		what = (char *)"WRITE_LONG_2";
		break;
	case VENDOR_CMND:
		what = (char *)"Realtek's vendor command";
		break;
	default:
		what = (char *)"(unknown command)";
		unknown_cmd = 1;
		break;
	}

	if (srb->cmnd[0] != TEST_UNIT_READY)
		RTS51X_DEBUGP("Command %s (%d bytes)\n", what, srb->cmd_len);
	if (unknown_cmd) {
		RTS51X_DEBUGP("");
		for (i = 0; i < srb->cmd_len && i < 16; i++)
			RTS51X_DEBUGPN(" %02x", srb->cmnd[i]);
		RTS51X_DEBUGPN("\n");
	}
}

void set_sense_type(struct rts51x_chip *chip, unsigned int lun, int sense_type)
{
	switch (sense_type) {
	case SENSE_TYPE_MEDIA_CHANGE:
		set_sense_data(chip, lun, CUR_ERR, 0x06, 0, 0x28, 0, 0, 0);
		break;

	case SENSE_TYPE_MEDIA_NOT_PRESENT:
		set_sense_data(chip, lun, CUR_ERR, 0x02, 0, 0x3A, 0, 0, 0);
		break;

	case SENSE_TYPE_MEDIA_LBA_OVER_RANGE:
		set_sense_data(chip, lun, CUR_ERR, 0x05, 0, 0x21, 0, 0, 0);
		break;

	case SENSE_TYPE_MEDIA_LUN_NOT_SUPPORT:
		set_sense_data(chip, lun, CUR_ERR, 0x05, 0, 0x25, 0, 0, 0);
		break;

	case SENSE_TYPE_MEDIA_WRITE_PROTECT:
		set_sense_data(chip, lun, CUR_ERR, 0x07, 0, 0x27, 0, 0, 0);
		break;

	case SENSE_TYPE_MEDIA_UNRECOVER_READ_ERR:
		set_sense_data(chip, lun, CUR_ERR, 0x03, 0, 0x11, 0, 0, 0);
		break;

	case SENSE_TYPE_MEDIA_WRITE_ERR:
		set_sense_data(chip, lun, CUR_ERR, 0x03, 0, 0x0C, 0x02, 0, 0);
		break;

	case SENSE_TYPE_MEDIA_INVALID_CMD_FIELD:
		set_sense_data(chip, lun, CUR_ERR, ILGAL_REQ, 0,
			       ASC_INVLD_CDB, ASCQ_INVLD_CDB, CDB_ILLEGAL, 1);
		break;

	case SENSE_TYPE_FORMAT_IN_PROGRESS:
		set_sense_data(chip, lun, CUR_ERR, 0x02, 0, 0x04, 0x04, 0, 0);
		break;

	case SENSE_TYPE_FORMAT_CMD_FAILED:
		set_sense_data(chip, lun, CUR_ERR, 0x03, 0, 0x31, 0x01, 0, 0);
		break;

#ifdef SUPPORT_MAGIC_GATE
	case SENSE_TYPE_MG_KEY_FAIL_NOT_ESTAB:
		set_sense_data(chip, lun, CUR_ERR, 0x05, 0, 0x6F, 0x02, 0, 0);
		break;

	case SENSE_TYPE_MG_KEY_FAIL_NOT_AUTHEN:
		set_sense_data(chip, lun, CUR_ERR, 0x05, 0, 0x6F, 0x00, 0, 0);
		break;

	case SENSE_TYPE_MG_INCOMPATIBLE_MEDIUM:
		set_sense_data(chip, lun, CUR_ERR, 0x02, 0, 0x30, 0x00, 0, 0);
		break;

	case SENSE_TYPE_MG_WRITE_ERR:
		set_sense_data(chip, lun, CUR_ERR, 0x03, 0, 0x0C, 0x00, 0, 0);
		break;
#endif

	case SENSE_TYPE_NO_SENSE:
	default:
		set_sense_data(chip, lun, CUR_ERR, 0, 0, 0, 0, 0, 0);
		break;
	}
}

void set_sense_data(struct rts51x_chip *chip, unsigned int lun, u8 err_code,
		    u8 sense_key, u32 info, u8 asc, u8 ascq, u8 sns_key_info0,
		    u16 sns_key_info1)
{
	struct sense_data_t *sense = &(chip->sense_buffer[lun]);

	sense->err_code = err_code;
	sense->sense_key = sense_key;
	sense->info[0] = (u8) (info >> 24);
	sense->info[1] = (u8) (info >> 16);
	sense->info[2] = (u8) (info >> 8);
	sense->info[3] = (u8) info;

	sense->ad_sense_len = sizeof(struct sense_data_t) - 8;
	sense->asc = asc;
	sense->ascq = ascq;
	if (sns_key_info0 != 0) {
		sense->sns_key_info[0] = SKSV | sns_key_info0;
		sense->sns_key_info[1] = (sns_key_info1 & 0xf0) >> 8;
		sense->sns_key_info[2] = sns_key_info1 & 0x0f;
	}
}

static int test_unit_ready(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	unsigned int lun = SCSI_LUN(srb);

	rts51x_init_cards(chip);

	if (!check_card_ready(chip, lun)) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_NOT_PRESENT);
		return TRANSPORT_FAILED;
	}

	if (!check_lun_mc(chip, lun)) {
		set_lun_mc(chip, lun);
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_CHANGE);
		return TRANSPORT_FAILED;
	}

	return TRANSPORT_GOOD;
}

unsigned char formatter_inquiry_str[20] = {
	'M', 'E', 'M', 'O', 'R', 'Y', 'S', 'T', 'I', 'C', 'K',
	'-', 'M', 'G',		/* Byte[47:49] */
	0x0B,			/* Byte[50]: MG, MS, MSPro, MSXC */
	0x00,			/* Byte[51]: Category Specific Commands */
	0x00,			/* Byte[52]: Access Control and feature */
	0x20, 0x20, 0x20,	/* Byte[53:55] */
};

static int inquiry(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	unsigned int lun = SCSI_LUN(srb);
	char *inquiry_default = (char *)"Generic-xD/SD/M.S.      1.00 ";
	char *inquiry_string;
	unsigned char sendbytes;
	unsigned char *buf;
	u8 card = get_lun_card(chip, lun);
	int pro_formatter_flag = 0;
	unsigned char inquiry_buf[] = {
		QULIFIRE | DRCT_ACCESS_DEV,
		RMB_DISC | 0x0D,
		0x00,
		0x01,
		0x1f,
		0x02,
		0,
		REL_ADR | WBUS_32 | WBUS_16 | SYNC | LINKED | CMD_QUE | SFT_RE,
	};

	inquiry_string = inquiry_default;

	buf = vmalloc(scsi_bufflen(srb));
	if (buf == NULL)
		TRACE_RET(chip, TRANSPORT_ERROR);

	if (MS_FORMATTER_ENABLED(chip) && (get_lun2card(chip, lun) & MS_CARD)) {
		if (!card || (card == MS_CARD))
			pro_formatter_flag = 1;
	}

	if (pro_formatter_flag) {
		if (scsi_bufflen(srb) < 56)
			sendbytes = (unsigned char)(scsi_bufflen(srb));
		else
			sendbytes = 56;
	} else {
		if (scsi_bufflen(srb) < 36)
			sendbytes = (unsigned char)(scsi_bufflen(srb));
		else
			sendbytes = 36;
	}

	if (sendbytes > 8) {
		memcpy(buf, inquiry_buf, 8);
		memcpy(buf + 8, inquiry_string, sendbytes - 8);
		if (pro_formatter_flag)
			buf[4] = 0x33;	/* Additional Length */
	} else {
		memcpy(buf, inquiry_buf, sendbytes);
	}

	if (pro_formatter_flag) {
		if (sendbytes > 36)
			memcpy(buf + 36, formatter_inquiry_str, sendbytes - 36);
	}

	scsi_set_resid(srb, 0);

	rts51x_set_xfer_buf(buf, scsi_bufflen(srb), srb);
	vfree(buf);

	return TRANSPORT_GOOD;
}

static int start_stop_unit(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	unsigned int lun = SCSI_LUN(srb);

	scsi_set_resid(srb, scsi_bufflen(srb));

	if (srb->cmnd[1] == 1)
		return TRANSPORT_GOOD;

	switch (srb->cmnd[0x4]) {
	case STOP_MEDIUM:
		/* Media disabled */
		return TRANSPORT_GOOD;

	case UNLOAD_MEDIUM:
		/* Media shall be unload */
		if (check_card_ready(chip, lun))
			eject_card(chip, lun);
		return TRANSPORT_GOOD;

	case MAKE_MEDIUM_READY:
	case LOAD_MEDIUM:
		if (check_card_ready(chip, lun)) {
			return TRANSPORT_GOOD;
		} else {
			set_sense_type(chip, lun, SENSE_TYPE_MEDIA_NOT_PRESENT);
			TRACE_RET(chip, TRANSPORT_FAILED);
		}

		break;
	}

	TRACE_RET(chip, TRANSPORT_ERROR);
}

static int allow_medium_removal(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	int prevent;

	prevent = srb->cmnd[4] & 0x1;

	scsi_set_resid(srb, 0);

	if (prevent) {
		set_sense_type(chip, SCSI_LUN(srb),
			       SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	return TRANSPORT_GOOD;
}

static void ms_mode_sense(struct rts51x_chip *chip, u8 cmd,
			  int lun, u8 *buf, int buf_len)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int sys_info_offset;
	int data_size = buf_len;
	int support_format = 0;
	int i = 0;

	if (cmd == MODE_SENSE) {
		sys_info_offset = 8;
		if (data_size > 0x68)
			data_size = 0x68;
		buf[i++] = 0x67;	/* Mode Data Length */
	} else {
		sys_info_offset = 12;
		if (data_size > 0x6C)
			data_size = 0x6C;
		buf[i++] = 0x00;	/* Mode Data Length (MSB) */
		buf[i++] = 0x6A;	/* Mode Data Length (LSB) */
	}

	/* Medium Type Code */
	if (check_card_ready(chip, lun)) {
		if (CHK_MSXC(ms_card)) {
			support_format = 1;
			buf[i++] = 0x40;
		} else if (CHK_MSPRO(ms_card)) {
			support_format = 1;
			buf[i++] = 0x20;
		} else {
			buf[i++] = 0x10;
		}

		/* WP */
		if (check_card_wp(chip, lun))
			buf[i++] = 0x80;
		else
			buf[i++] = 0x00;
	} else {
		buf[i++] = 0x00;	/* MediaType */
		buf[i++] = 0x00;	/* WP */
	}

	buf[i++] = 0x00;	/* Reserved */

	if (cmd == MODE_SENSE_10) {
		buf[i++] = 0x00;	/* Reserved */
		buf[i++] = 0x00;	/* Block descriptor length(MSB) */
		buf[i++] = 0x00;	/* Block descriptor length(LSB) */

		/* The Following Data is the content of "Page 0x20" */
		if (data_size >= 9)
			buf[i++] = 0x20;	/* Page Code */
		if (data_size >= 10)
			buf[i++] = 0x62;	/* Page Length */
		if (data_size >= 11)
			buf[i++] = 0x00;	/* No Access Control */
		if (data_size >= 12) {
			if (support_format)
				buf[i++] = 0xC0;	/* SF, SGM */
			else
				buf[i++] = 0x00;
		}
	} else {
		/* The Following Data is the content of "Page 0x20" */
		if (data_size >= 5)
			buf[i++] = 0x20;	/* Page Code */
		if (data_size >= 6)
			buf[i++] = 0x62;	/* Page Length */
		if (data_size >= 7)
			buf[i++] = 0x00;	/* No Access Control */
		if (data_size >= 8) {
			if (support_format)
				buf[i++] = 0xC0;	/* SF, SGM */
			else
				buf[i++] = 0x00;
		}
	}

	if (data_size > sys_info_offset) {
		/* 96 Bytes Attribute Data */
		int len = data_size - sys_info_offset;
		len = (len < 96) ? len : 96;

		memcpy(buf + sys_info_offset, ms_card->raw_sys_info, len);
	}
}

static int mode_sense(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	unsigned int lun = SCSI_LUN(srb);
	unsigned int dataSize;
	int status;
	int pro_formatter_flag;
	unsigned char pageCode, *buf;
	u8 card = get_lun_card(chip, lun);

	if (!check_card_ready(chip, lun)) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_NOT_PRESENT);
		scsi_set_resid(srb, scsi_bufflen(srb));
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	pro_formatter_flag = 0;
	dataSize = 8;
	/* In Combo mode, device responses ModeSense command as a MS LUN
	 * when no card is inserted */
	if ((get_lun2card(chip, lun) & MS_CARD)) {
		if (!card || (card == MS_CARD)) {
			dataSize = 108;
			if (chip->option.mspro_formatter_enable)
				pro_formatter_flag = 1;
		}
	}

	buf = kmalloc(dataSize, GFP_KERNEL);
	if (buf == NULL)
		TRACE_RET(chip, TRANSPORT_ERROR);

	pageCode = srb->cmnd[2] & 0x3f;

	if ((pageCode == 0x3F) || (pageCode == 0x1C) ||
	    (pageCode == 0x00) || (pro_formatter_flag && (pageCode == 0x20))) {
		if (srb->cmnd[0] == MODE_SENSE) {
			if ((pageCode == 0x3F) || (pageCode == 0x20)) {
				ms_mode_sense(chip, srb->cmnd[0], lun, buf,
					      dataSize);
			} else {
				dataSize = 4;
				buf[0] = 0x03;
				buf[1] = 0x00;
				if (check_card_wp(chip, lun))
					buf[2] = 0x80;
				else
				buf[3] = 0x00;
			}
		} else {
			if ((pageCode == 0x3F) || (pageCode == 0x20)) {
				ms_mode_sense(chip, srb->cmnd[0], lun, buf,
					      dataSize);
			} else {
				dataSize = 8;
				buf[0] = 0x00;
				buf[1] = 0x06;
				buf[2] = 0x00;
				if (check_card_wp(chip, lun))
					buf[3] = 0x80;
				else
					buf[3] = 0x00;
				buf[4] = 0x00;
				buf[5] = 0x00;
				buf[6] = 0x00;
				buf[7] = 0x00;
			}
		}
		status = TRANSPORT_GOOD;
	} else {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		scsi_set_resid(srb, scsi_bufflen(srb));
		status = TRANSPORT_FAILED;
	}

	if (status == TRANSPORT_GOOD) {
		unsigned int len = min(scsi_bufflen(srb), dataSize);
		rts51x_set_xfer_buf(buf, len, srb);
		scsi_set_resid(srb, scsi_bufflen(srb) - len);
	}
	kfree(buf);

	return status;
}

static int request_sense(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	struct sense_data_t *sense;
	unsigned int lun = SCSI_LUN(srb);
	struct ms_info *ms_card = &(chip->ms_card);
	unsigned char *tmp, *buf;

	sense = &(chip->sense_buffer[lun]);

	if ((get_lun_card(chip, lun) == MS_CARD)
	    && PRO_UNDER_FORMATTING(ms_card)) {
		mspro_format_sense(chip, lun);
	}

	buf = vmalloc(scsi_bufflen(srb));
	if (buf == NULL)
		TRACE_RET(chip, TRANSPORT_ERROR);

	tmp = (unsigned char *)sense;
	memcpy(buf, tmp, scsi_bufflen(srb));

	rts51x_set_xfer_buf(buf, scsi_bufflen(srb), srb);
	vfree(buf);

	scsi_set_resid(srb, 0);
	/* Reset Sense Data */
	set_sense_type(chip, lun, SENSE_TYPE_NO_SENSE);
	return TRANSPORT_GOOD;
}

static int read_write(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	unsigned int lun = SCSI_LUN(srb);
	int retval;
	u32 start_sec;
	u16 sec_cnt;

	if (!check_card_ready(chip, lun) || (chip->capacity[lun] == 0)) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_NOT_PRESENT);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	if (!check_lun_mc(chip, lun)) {
		set_lun_mc(chip, lun);
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_CHANGE);
		return TRANSPORT_FAILED;
	}

	rts51x_prepare_run(chip);
	RTS51X_SET_STAT(chip, STAT_RUN);

	if ((srb->cmnd[0] == READ_10) || (srb->cmnd[0] == WRITE_10)) {
		start_sec =
		    ((u32) srb->cmnd[2] << 24) |
		    ((u32) srb->cmnd[3] << 16) |
		    ((u32) srb->cmnd[4] << 8) |
		    ((u32) srb->cmnd[5]);
		sec_cnt = ((u16) (srb->cmnd[7]) << 8) | srb->cmnd[8];
	} else if ((srb->cmnd[0] == READ_6) || (srb->cmnd[0] == WRITE_6)) {
		start_sec = ((u32) (srb->cmnd[1] & 0x1F) << 16) |
		    ((u32) srb->cmnd[2] << 8) | ((u32) srb->cmnd[3]);
		sec_cnt = srb->cmnd[4];
	} else if ((srb->cmnd[0] == VENDOR_CMND) &&
			(srb->cmnd[1] == SCSI_APP_CMD) &&
			((srb->cmnd[2] == PP_READ10) ||
			 (srb->cmnd[2] == PP_WRITE10))) {
		start_sec = ((u32) srb->cmnd[4] << 24) |
			((u32) srb->cmnd[5] << 16) |
			((u32) srb->cmnd[6] << 8) |
			((u32) srb->cmnd[7]);
		sec_cnt = ((u16) (srb->cmnd[9]) << 8) | srb->cmnd[10];
	} else {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	if ((start_sec > chip->capacity[lun]) ||
	    ((start_sec + sec_cnt) > chip->capacity[lun])) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_LBA_OVER_RANGE);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	if (sec_cnt == 0) {
		scsi_set_resid(srb, 0);
		return TRANSPORT_GOOD;
	}

	if ((srb->sc_data_direction == DMA_TO_DEVICE)
	    && check_card_wp(chip, lun)) {
		RTS51X_DEBUGP("Write protected card!\n");
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_WRITE_PROTECT);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	retval = card_rw(srb, chip, start_sec, sec_cnt);
	if (retval != STATUS_SUCCESS) {
		if (srb->sc_data_direction == DMA_FROM_DEVICE) {
			set_sense_type(chip, lun,
				       SENSE_TYPE_MEDIA_UNRECOVER_READ_ERR);
		} else {
			set_sense_type(chip, lun, SENSE_TYPE_MEDIA_WRITE_ERR);
		}
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	scsi_set_resid(srb, 0);

	return TRANSPORT_GOOD;
}

static int read_format_capacity(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	unsigned char *buf;
	unsigned int lun = SCSI_LUN(srb);
	unsigned int buf_len;
	u8 card = get_lun_card(chip, lun);
	int desc_cnt;
	int i = 0;

	if (!check_card_ready(chip, lun)) {
		if (!chip->option.mspro_formatter_enable) {
			set_sense_type(chip, lun, SENSE_TYPE_MEDIA_NOT_PRESENT);
			TRACE_RET(chip, TRANSPORT_FAILED);
		}
	}

	buf_len = (scsi_bufflen(srb) > 12) ? 0x14 : 12;

	buf = kmalloc(buf_len, GFP_KERNEL);
	if (buf == NULL)
		TRACE_RET(chip, TRANSPORT_ERROR);

	buf[i++] = 0;
	buf[i++] = 0;
	buf[i++] = 0;

	/* Capacity List Length */
	if ((buf_len > 12) && chip->option.mspro_formatter_enable &&
	    (chip->lun2card[lun] & MS_CARD) && (!card || (card == MS_CARD))) {
		buf[i++] = 0x10;
		desc_cnt = 2;
	} else {
		buf[i++] = 0x08;
		desc_cnt = 1;
	}

	while (desc_cnt) {
		if (check_card_ready(chip, lun)) {
			buf[i++] = (unsigned char)((chip->capacity[lun]) >> 24);
			buf[i++] = (unsigned char)((chip->capacity[lun]) >> 16);
			buf[i++] = (unsigned char)((chip->capacity[lun]) >> 8);
			buf[i++] = (unsigned char)(chip->capacity[lun]);

			if (desc_cnt == 2)
				/* Byte[8]: Descriptor Type: Formatted medium */
				buf[i++] = 2;
			else
				buf[i++] = 0;	/* Byte[16] */
		} else {
			buf[i++] = 0xFF;
			buf[i++] = 0xFF;
			buf[i++] = 0xFF;
			buf[i++] = 0xFF;

			if (desc_cnt == 2)
				/* Byte[8]: Descriptor Type: No medium */
				buf[i++] = 3;
			else
				buf[i++] = 0;	/*Byte[16] */
		}

		buf[i++] = 0x00;
		buf[i++] = 0x02;
		buf[i++] = 0x00;

		desc_cnt--;
	}

	buf_len = min(scsi_bufflen(srb), buf_len);
	rts51x_set_xfer_buf(buf, buf_len, srb);
	kfree(buf);

	scsi_set_resid(srb, scsi_bufflen(srb) - buf_len);

	return TRANSPORT_GOOD;
}

static int read_capacity(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	unsigned char *buf;
	unsigned int lun = SCSI_LUN(srb);

	if (!check_card_ready(chip, lun)) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_NOT_PRESENT);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	if (!check_lun_mc(chip, lun)) {
		set_lun_mc(chip, lun);
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_CHANGE);
		return TRANSPORT_FAILED;
	}

	buf = kmalloc(8, GFP_KERNEL);
	if (buf == NULL)
		TRACE_RET(chip, TRANSPORT_ERROR);

	buf[0] = (unsigned char)((chip->capacity[lun] - 1) >> 24);
	buf[1] = (unsigned char)((chip->capacity[lun] - 1) >> 16);
	buf[2] = (unsigned char)((chip->capacity[lun] - 1) >> 8);
	buf[3] = (unsigned char)(chip->capacity[lun] - 1);

	buf[4] = 0x00;
	buf[5] = 0x00;
	buf[6] = 0x02;
	buf[7] = 0x00;

	rts51x_set_xfer_buf(buf, scsi_bufflen(srb), srb);
	kfree(buf);

	scsi_set_resid(srb, 0);

	return TRANSPORT_GOOD;
}

static int get_dev_status(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	unsigned int lun = SCSI_LUN(srb);
	unsigned int buf_len;
	u8 status[32] = { 0 };

	rts51x_pp_status(chip, lun, status, 32);

	buf_len = min(scsi_bufflen(srb), (unsigned int)sizeof(status));
	rts51x_set_xfer_buf(status, buf_len, srb);
	scsi_set_resid(srb, scsi_bufflen(srb) - buf_len);

	return TRANSPORT_GOOD;
}

static int read_status(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	u8 rts51x_status[16];
	unsigned int buf_len;
	unsigned int lun = SCSI_LUN(srb);

	rts51x_read_status(chip, lun, rts51x_status, 16);

	buf_len = min(scsi_bufflen(srb), (unsigned int)sizeof(rts51x_status));
	rts51x_set_xfer_buf(rts51x_status, buf_len, srb);
	scsi_set_resid(srb, scsi_bufflen(srb) - buf_len);

	return TRANSPORT_GOOD;
}

static int read_mem(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	unsigned int lun = SCSI_LUN(srb);
	unsigned short addr, len, i;
	int retval;
	u8 *buf;

	rts51x_prepare_run(chip);
	RTS51X_SET_STAT(chip, STAT_RUN);

	addr = ((u16) srb->cmnd[2] << 8) | srb->cmnd[3];
	len = ((u16) srb->cmnd[4] << 8) | srb->cmnd[5];

	if (addr < 0xe000) {
		RTS51X_DEBUGP("filter!addr=0x%x\n", addr);
		return TRANSPORT_GOOD;
	}

	buf = vmalloc(len);
	if (!buf)
		TRACE_RET(chip, TRANSPORT_ERROR);

	for (i = 0; i < len; i++) {
		retval = rts51x_ep0_read_register(chip, addr + i, buf + i);
		if (retval != STATUS_SUCCESS) {
			vfree(buf);
			set_sense_type(chip, lun,
				       SENSE_TYPE_MEDIA_UNRECOVER_READ_ERR);
			TRACE_RET(chip, TRANSPORT_FAILED);
		}
	}

	len = (unsigned short)min(scsi_bufflen(srb), (unsigned int)len);
	rts51x_set_xfer_buf(buf, len, srb);
	scsi_set_resid(srb, scsi_bufflen(srb) - len);

	vfree(buf);

	return TRANSPORT_GOOD;
}

static int write_mem(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	unsigned int lun = SCSI_LUN(srb);
	unsigned short addr, len, i;
	int retval;
	u8 *buf;

	rts51x_prepare_run(chip);
	RTS51X_SET_STAT(chip, STAT_RUN);

	addr = ((u16) srb->cmnd[2] << 8) | srb->cmnd[3];
	len = ((u16) srb->cmnd[4] << 8) | srb->cmnd[5];

	if (addr < 0xe000) {
		RTS51X_DEBUGP("filter!addr=0x%x\n", addr);
		return TRANSPORT_GOOD;
	}

	len = (unsigned short)min(scsi_bufflen(srb), (unsigned int)len);
	buf = vmalloc(len);
	if (!buf)
		TRACE_RET(chip, TRANSPORT_ERROR);

	rts51x_get_xfer_buf(buf, len, srb);

	for (i = 0; i < len; i++) {
		retval =
		    rts51x_ep0_write_register(chip, addr + i, 0xFF, buf[i]);
		if (retval != STATUS_SUCCESS) {
			vfree(buf);
			set_sense_type(chip, lun, SENSE_TYPE_MEDIA_WRITE_ERR);
			TRACE_RET(chip, TRANSPORT_FAILED);
		}
	}

	vfree(buf);
	scsi_set_resid(srb, scsi_bufflen(srb) - len);

	return TRANSPORT_GOOD;
}

static int get_sd_csd(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	unsigned int lun = SCSI_LUN(srb);

	if (!check_card_ready(chip, lun)) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_NOT_PRESENT);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	if (get_lun_card(chip, lun) != SD_CARD) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_UNRECOVER_READ_ERR);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	scsi_set_resid(srb, 0);
	rts51x_set_xfer_buf(sd_card->raw_csd, scsi_bufflen(srb), srb);

	return TRANSPORT_GOOD;
}

static int read_phy_register(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	int retval;
	u8 addr, len, i;
	u8 *buf;

	rts51x_prepare_run(chip);
	RTS51X_SET_STAT(chip, STAT_RUN);

	addr = srb->cmnd[5];
	len = srb->cmnd[7];

	if (len) {
		buf = vmalloc(len);
		if (!buf)
			TRACE_RET(chip, TRANSPORT_ERROR);

		for (i = 0; i < len; i++) {
			retval =
			    rts51x_read_phy_register(chip, addr + i, buf + i);
			if (retval != STATUS_SUCCESS) {
				vfree(buf);
				set_sense_type(chip, SCSI_LUN(srb),
					SENSE_TYPE_MEDIA_UNRECOVER_READ_ERR);
				TRACE_RET(chip, TRANSPORT_FAILED);
			}
		}

		len = min(scsi_bufflen(srb), (unsigned int)len);
		rts51x_set_xfer_buf(buf, len, srb);
		scsi_set_resid(srb, scsi_bufflen(srb) - len);

		vfree(buf);
	}

	return TRANSPORT_GOOD;
}

static int write_phy_register(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	int retval;
	u8 addr, len, i;
	u8 *buf;

	rts51x_prepare_run(chip);
	RTS51X_SET_STAT(chip, STAT_RUN);

	addr = srb->cmnd[5];
	len = srb->cmnd[7];

	if (len) {
		len = min(scsi_bufflen(srb), (unsigned int)len);

		buf = vmalloc(len);
		if (buf == NULL)
			TRACE_RET(chip, TRANSPORT_ERROR);

		rts51x_get_xfer_buf(buf, len, srb);
		scsi_set_resid(srb, scsi_bufflen(srb) - len);

		for (i = 0; i < len; i++) {
			retval =
			    rts51x_write_phy_register(chip, addr + i, buf[i]);
			if (retval != STATUS_SUCCESS) {
				vfree(buf);
				set_sense_type(chip, SCSI_LUN(srb),
					       SENSE_TYPE_MEDIA_WRITE_ERR);
				TRACE_RET(chip, TRANSPORT_FAILED);
			}
		}

		vfree(buf);
	}

	return TRANSPORT_GOOD;
}

static int get_card_bus_width(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	unsigned int lun = SCSI_LUN(srb);
	u8 card, bus_width;

	if (!check_card_ready(chip, lun)) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_NOT_PRESENT);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	card = get_lun_card(chip, lun);
	if ((card == SD_CARD) || (card == MS_CARD)) {
		bus_width = chip->card_bus_width[lun];
	} else {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_UNRECOVER_READ_ERR);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	scsi_set_resid(srb, 0);
	rts51x_set_xfer_buf(&bus_width, scsi_bufflen(srb), srb);

	return TRANSPORT_GOOD;
}

#ifdef _MSG_TRACE
static int trace_msg_cmd(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	unsigned char *buf = NULL;
	u8 clear;
	unsigned int buf_len;

	buf_len =
	    4 +
	    ((2 + MSG_FUNC_LEN + MSG_FILE_LEN + TIME_VAL_LEN) * TRACE_ITEM_CNT);

	if ((scsi_bufflen(srb) < buf_len) || (scsi_sglist(srb) == NULL)) {
		set_sense_type(chip, SCSI_LUN(srb),
			       SENSE_TYPE_MEDIA_UNRECOVER_READ_ERR);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	clear = srb->cmnd[2];

	buf = vmalloc(scsi_bufflen(srb));
	if (buf == NULL)
		TRACE_RET(chip, TRANSPORT_ERROR);

	rts51x_trace_msg(chip, buf, clear);

	rts51x_set_xfer_buf(buf, scsi_bufflen(srb), srb);
	vfree(buf);

	scsi_set_resid(srb, 0);
	return TRANSPORT_GOOD;
}
#endif

static int rw_mem_cmd_buf(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	int retval = STATUS_SUCCESS;
	unsigned int lun = SCSI_LUN(srb);
	u8 cmd_type, mask, value, idx, mode, len;
	u16 addr;
	u32 timeout;

	rts51x_prepare_run(chip);
	RTS51X_SET_STAT(chip, STAT_RUN);

	switch (srb->cmnd[3]) {
	case INIT_BATCHCMD:
		rts51x_init_cmd(chip);
		break;

	case ADD_BATCHCMD:
		cmd_type = srb->cmnd[4];
		if (cmd_type > 2) {
			set_sense_type(chip, lun,
				       SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
			TRACE_RET(chip, TRANSPORT_FAILED);
		}
		addr = (srb->cmnd[5] << 8) | srb->cmnd[6];
		mask = srb->cmnd[7];
		value = srb->cmnd[8];
		rts51x_add_cmd(chip, cmd_type, addr, mask, value);
		break;

	case SEND_BATCHCMD:
		mode = srb->cmnd[4];
		len = srb->cmnd[5];
		timeout =
		    ((u32) srb->cmnd[6] << 24) | ((u32) srb->
						  cmnd[7] << 16) | ((u32) srb->
								    cmnd[8] <<
								    8) | ((u32)
									  srb->
									  cmnd
									  [9]);
		retval = rts51x_send_cmd(chip, mode, 1000);
		if (retval != STATUS_SUCCESS) {
			set_sense_type(chip, lun, SENSE_TYPE_MEDIA_WRITE_ERR);
			TRACE_RET(chip, TRANSPORT_FAILED);
		}
		if (mode & STAGE_R) {
			retval = rts51x_get_rsp(chip, len, timeout);
			if (retval != STATUS_SUCCESS) {
				set_sense_type(chip, lun,
					SENSE_TYPE_MEDIA_UNRECOVER_READ_ERR);
				TRACE_RET(chip, TRANSPORT_FAILED);
			}
		}
		break;

	case GET_BATCHRSP:
		idx = srb->cmnd[4];
		value = chip->rsp_buf[idx];
		if (scsi_bufflen(srb) < 1) {
			set_sense_type(chip, lun,
				       SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
			TRACE_RET(chip, TRANSPORT_FAILED);
		}
		rts51x_set_xfer_buf(&value, 1, srb);
		scsi_set_resid(srb, 0);
		break;

	default:
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_WRITE_ERR);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	return TRANSPORT_GOOD;
}

static int suit_cmd(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	int result;

	switch (srb->cmnd[3]) {
	case INIT_BATCHCMD:
	case ADD_BATCHCMD:
	case SEND_BATCHCMD:
	case GET_BATCHRSP:
		result = rw_mem_cmd_buf(srb, chip);
		break;
	default:
		result = TRANSPORT_ERROR;
	}

	return result;
}

static int app_cmd(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	int result;

	switch (srb->cmnd[2]) {
	case PP_READ10:
	case PP_WRITE10:
		result = read_write(srb, chip);
		break;

	case SUIT_CMD:
		result = suit_cmd(srb, chip);
		break;

	case READ_PHY:
		result = read_phy_register(srb, chip);
		break;

	case WRITE_PHY:
		result = write_phy_register(srb, chip);
		break;

	case GET_DEV_STATUS:
		result = get_dev_status(srb, chip);
		break;

	default:
		set_sense_type(chip, SCSI_LUN(srb),
			       SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	return result;
}

static int vendor_cmnd(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	int result = TRANSPORT_GOOD;

	switch (srb->cmnd[1]) {
	case READ_STATUS:
		result = read_status(srb, chip);
		break;

	case READ_MEM:
		result = read_mem(srb, chip);
		break;

	case WRITE_MEM:
		result = write_mem(srb, chip);
		break;

	case GET_BUS_WIDTH:
		result = get_card_bus_width(srb, chip);
		break;

	case GET_SD_CSD:
		result = get_sd_csd(srb, chip);
		break;

#ifdef _MSG_TRACE
	case TRACE_MSG:
		result = trace_msg_cmd(srb, chip);
		break;
#endif

	case SCSI_APP_CMD:
		result = app_cmd(srb, chip);
		break;

	default:
		set_sense_type(chip, SCSI_LUN(srb),
			       SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	return result;
}

static int ms_format_cmnd(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	struct ms_info *ms_card = &(chip->ms_card);
	unsigned int lun = SCSI_LUN(srb);
	int retval, quick_format;

	if (get_lun_card(chip, lun) != MS_CARD) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_LUN_NOT_SUPPORT);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	if ((srb->cmnd[3] != 0x4D) || (srb->cmnd[4] != 0x47)
	    || (srb->cmnd[5] != 0x66) || (srb->cmnd[6] != 0x6D)
	    || (srb->cmnd[7] != 0x74)) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	if (srb->cmnd[8] & 0x01)
		quick_format = 0;
	else
		quick_format = 1;

	if (!(chip->card_ready & MS_CARD)) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_NOT_PRESENT);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	if (chip->card_wp & MS_CARD) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_WRITE_PROTECT);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	if (!CHK_MSPRO(ms_card)) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_LUN_NOT_SUPPORT);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	rts51x_prepare_run(chip);
	RTS51X_SET_STAT(chip, STAT_RUN);

	retval = mspro_format(srb, chip, MS_SHORT_DATA_LEN, quick_format);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_FORMAT_CMD_FAILED);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	scsi_set_resid(srb, 0);
	return TRANSPORT_GOOD;
}

#ifdef SUPPORT_PCGL_1P18
static int get_ms_information(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	struct ms_info *ms_card = &(chip->ms_card);
	unsigned int lun = SCSI_LUN(srb);
	u8 dev_info_id, data_len;
	u8 *buf;
	unsigned int buf_len;
	int i;

	if (!check_card_ready(chip, lun)) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_NOT_PRESENT);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}
	if ((get_lun_card(chip, lun) != MS_CARD)) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_LUN_NOT_SUPPORT);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	if ((srb->cmnd[2] != 0xB0) || (srb->cmnd[4] != 0x4D) ||
	    (srb->cmnd[5] != 0x53) || (srb->cmnd[6] != 0x49) ||
	    (srb->cmnd[7] != 0x44)) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	dev_info_id = srb->cmnd[3];
	if ((CHK_MSXC(ms_card) && (dev_info_id == 0x10)) ||
	    (!CHK_MSXC(ms_card) && (dev_info_id == 0x13)) ||
	    !CHK_MSPRO(ms_card)) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	if (dev_info_id == 0x15)
		buf_len = data_len = 0x3A;
	else
		buf_len = data_len = 0x6A;

	buf = kmalloc(buf_len, GFP_KERNEL);
	if (!buf)
		TRACE_RET(chip, TRANSPORT_ERROR);

	i = 0;
	/* GET Memory Stick Media Information Response Header */
	buf[i++] = 0x00;	/* Data length MSB */
	buf[i++] = data_len;	/* Data length LSB */
	/* Device Information Type Code */
	if (CHK_MSXC(ms_card))
		buf[i++] = 0x03;
	else
		buf[i++] = 0x02;
	/* SGM bit */
	buf[i++] = 0x01;
	/* Reserved */
	buf[i++] = 0x00;
	buf[i++] = 0x00;
	buf[i++] = 0x00;
	/* Number of Device Information */
	buf[i++] = 0x01;

	/*  Device Information Body
	 *  Device Information ID Number */
	buf[i++] = dev_info_id;
	/* Device Information Length */
	if (dev_info_id == 0x15)
		data_len = 0x31;
	else
		data_len = 0x61;
	buf[i++] = 0x00;	/* Data length MSB */
	buf[i++] = data_len;	/* Data length LSB */
	/* Valid Bit */
	buf[i++] = 0x80;
	if ((dev_info_id == 0x10) || (dev_info_id == 0x13)) {
		/* System Information */
		memcpy(buf + i, ms_card->raw_sys_info, 96);
	} else {
		/* Model Name */
		memcpy(buf + i, ms_card->raw_model_name, 48);
	}

	rts51x_set_xfer_buf(buf, buf_len, srb);

	if (dev_info_id == 0x15)
		scsi_set_resid(srb, scsi_bufflen(srb) - 0x3C);
	else
		scsi_set_resid(srb, scsi_bufflen(srb) - 0x6C);

	kfree(buf);
	return STATUS_SUCCESS;
}
#endif

static int ms_sp_cmnd(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	int retval = TRANSPORT_ERROR;

	if (srb->cmnd[2] == MS_FORMAT)
		retval = ms_format_cmnd(srb, chip);
#ifdef SUPPORT_PCGL_1P18
	else if (srb->cmnd[2] == GET_MS_INFORMATION)
		retval = get_ms_information(srb, chip);
#endif

	return retval;
}

#ifdef SUPPORT_CPRM
static int sd_extention_cmnd(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	unsigned int lun = SCSI_LUN(srb);
	int result;

	rts51x_prepare_run(chip);
	RTS51X_SET_STAT(chip, STAT_RUN);

	sd_cleanup_work(chip);

	if (!check_card_ready(chip, lun)) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_NOT_PRESENT);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}
	if ((get_lun_card(chip, lun) != SD_CARD)) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_LUN_NOT_SUPPORT);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	switch (srb->cmnd[0]) {
	case SD_PASS_THRU_MODE:
		result = sd_pass_thru_mode(srb, chip);
		break;

	case SD_EXECUTE_NO_DATA:
		result = sd_execute_no_data(srb, chip);
		break;

	case SD_EXECUTE_READ:
		result = sd_execute_read_data(srb, chip);
		break;

	case SD_EXECUTE_WRITE:
		result = sd_execute_write_data(srb, chip);
		break;

	case SD_GET_RSP:
		result = sd_get_cmd_rsp(srb, chip);
		break;

	case SD_HW_RST:
		result = sd_hw_rst(srb, chip);
		break;

	default:
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	return result;
}
#endif

#ifdef SUPPORT_MAGIC_GATE
static int mg_report_key(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	struct ms_info *ms_card = &(chip->ms_card);
	unsigned int lun = SCSI_LUN(srb);
	int retval;
	u8 key_format;

	rts51x_prepare_run(chip);
	RTS51X_SET_STAT(chip, STAT_RUN);

	ms_cleanup_work(chip);

	if (!check_card_ready(chip, lun)) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_NOT_PRESENT);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}
	if ((get_lun_card(chip, lun) != MS_CARD)) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_LUN_NOT_SUPPORT);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	if (srb->cmnd[7] != KC_MG_R_PRO) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	if (!CHK_MSPRO(ms_card)) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_INCOMPATIBLE_MEDIUM);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	key_format = srb->cmnd[10] & 0x3F;

	switch (key_format) {
	case KF_GET_LOC_EKB:
		if ((scsi_bufflen(srb) == 0x41C) &&
		    (srb->cmnd[8] == 0x04) && (srb->cmnd[9] == 0x1C)) {
			retval = mg_get_local_EKB(srb, chip);
			if (retval != STATUS_SUCCESS)
				TRACE_RET(chip, TRANSPORT_FAILED);
		} else {
			set_sense_type(chip, lun,
				       SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
			TRACE_RET(chip, TRANSPORT_FAILED);
		}
		break;

	case KF_RSP_CHG:
		if ((scsi_bufflen(srb) == 0x24) &&
		    (srb->cmnd[8] == 0x00) && (srb->cmnd[9] == 0x24)) {
			retval = mg_get_rsp_chg(srb, chip);
			if (retval != STATUS_SUCCESS)
				TRACE_RET(chip, TRANSPORT_FAILED);
		} else {
			set_sense_type(chip, lun,
				       SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
			TRACE_RET(chip, TRANSPORT_FAILED);
		}
		break;

	case KF_GET_ICV:
		ms_card->mg_entry_num = srb->cmnd[5];
		if ((scsi_bufflen(srb) == 0x404) &&
		    (srb->cmnd[8] == 0x04) &&
		    (srb->cmnd[9] == 0x04) &&
		    (srb->cmnd[2] == 0x00) &&
		    (srb->cmnd[3] == 0x00) &&
		    (srb->cmnd[4] == 0x00) && (srb->cmnd[5] < 32)) {
			retval = mg_get_ICV(srb, chip);
			if (retval != STATUS_SUCCESS)
				TRACE_RET(chip, TRANSPORT_FAILED);
		} else {
			set_sense_type(chip, lun,
				       SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
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

static int mg_send_key(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	struct ms_info *ms_card = &(chip->ms_card);
	unsigned int lun = SCSI_LUN(srb);
	int retval;
	u8 key_format;

	rts51x_prepare_run(chip);
	RTS51X_SET_STAT(chip, STAT_RUN);

	ms_cleanup_work(chip);

	if (!check_card_ready(chip, lun)) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_NOT_PRESENT);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}
	if (check_card_wp(chip, lun)) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_WRITE_PROTECT);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}
	if ((get_lun_card(chip, lun) != MS_CARD)) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_LUN_NOT_SUPPORT);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	if (srb->cmnd[7] != KC_MG_R_PRO) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	if (!CHK_MSPRO(ms_card)) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_INCOMPATIBLE_MEDIUM);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	key_format = srb->cmnd[10] & 0x3F;

	switch (key_format) {
	case KF_SET_LEAF_ID:
		if ((scsi_bufflen(srb) == 0x0C) &&
		    (srb->cmnd[8] == 0x00) && (srb->cmnd[9] == 0x0C)) {
			retval = mg_set_leaf_id(srb, chip);
			if (retval != STATUS_SUCCESS)
				TRACE_RET(chip, TRANSPORT_FAILED);
		} else {
			set_sense_type(chip, lun,
				       SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
			TRACE_RET(chip, TRANSPORT_FAILED);
		}
		break;

	case KF_CHG_HOST:
		if ((scsi_bufflen(srb) == 0x0C) &&
		    (srb->cmnd[8] == 0x00) && (srb->cmnd[9] == 0x0C)) {
			retval = mg_chg(srb, chip);
			if (retval != STATUS_SUCCESS)
				TRACE_RET(chip, TRANSPORT_FAILED);
		} else {
			set_sense_type(chip, lun,
				       SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
			TRACE_RET(chip, TRANSPORT_FAILED);
		}
		break;

	case KF_RSP_HOST:
		if ((scsi_bufflen(srb) == 0x0C) &&
		    (srb->cmnd[8] == 0x00) && (srb->cmnd[9] == 0x0C)) {
			retval = mg_rsp(srb, chip);
			if (retval != STATUS_SUCCESS)
				TRACE_RET(chip, TRANSPORT_FAILED);
		} else {
			set_sense_type(chip, lun,
				       SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
			TRACE_RET(chip, TRANSPORT_FAILED);
		}
		break;

	case KF_SET_ICV:
		ms_card->mg_entry_num = srb->cmnd[5];
		if ((scsi_bufflen(srb) == 0x404) &&
		    (srb->cmnd[8] == 0x04) &&
		    (srb->cmnd[9] == 0x04) &&
		    (srb->cmnd[2] == 0x00) &&
		    (srb->cmnd[3] == 0x00) &&
		    (srb->cmnd[4] == 0x00) && (srb->cmnd[5] < 32)) {
			retval = mg_set_ICV(srb, chip);
			if (retval != STATUS_SUCCESS)
				TRACE_RET(chip, TRANSPORT_FAILED);
		} else {
			set_sense_type(chip, lun,
				       SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
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

int rts51x_scsi_handler(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	struct ms_info *ms_card = &(chip->ms_card);
	unsigned int lun = SCSI_LUN(srb);
	int result = TRANSPORT_GOOD;

	if ((get_lun_card(chip, lun) == MS_CARD) &&
	    (ms_card->format_status == FORMAT_IN_PROGRESS)) {
		if ((srb->cmnd[0] != REQUEST_SENSE)
		    && (srb->cmnd[0] != INQUIRY)) {
			/* Logical Unit Not Ready Format in Progress */
			set_sense_data(chip, lun, CUR_ERR, 0x02, 0, 0x04, 0x04,
				       0, (u16) (ms_card->progress));
			TRACE_RET(chip, TRANSPORT_FAILED);
		}
	}

	switch (srb->cmnd[0]) {
	case READ_10:
	case WRITE_10:
	case READ_6:
	case WRITE_6:
		result = read_write(srb, chip);
		break;

	case TEST_UNIT_READY:
		result = test_unit_ready(srb, chip);
		break;

	case INQUIRY:
		result = inquiry(srb, chip);
		break;

	case READ_CAPACITY:
		result = read_capacity(srb, chip);
		break;

	case START_STOP:
		result = start_stop_unit(srb, chip);
		break;

	case ALLOW_MEDIUM_REMOVAL:
		result = allow_medium_removal(srb, chip);
		break;

	case REQUEST_SENSE:
		result = request_sense(srb, chip);
		break;

	case MODE_SENSE:
	case MODE_SENSE_10:
		result = mode_sense(srb, chip);
		break;

	case 0x23:
		result = read_format_capacity(srb, chip);
		break;

	case VENDOR_CMND:
		result = vendor_cmnd(srb, chip);
		break;

	case MS_SP_CMND:
		result = ms_sp_cmnd(srb, chip);
		break;

#ifdef SUPPORT_CPRM
	case SD_PASS_THRU_MODE:
	case SD_EXECUTE_NO_DATA:
	case SD_EXECUTE_READ:
	case SD_EXECUTE_WRITE:
	case SD_GET_RSP:
	case SD_HW_RST:
		result = sd_extention_cmnd(srb, chip);
		break;
#endif

#ifdef SUPPORT_MAGIC_GATE
	case CMD_MSPRO_MG_RKEY:
		result = mg_report_key(srb, chip);
		break;

	case CMD_MSPRO_MG_SKEY:
		result = mg_send_key(srb, chip);
		break;
#endif

	case FORMAT_UNIT:
	case MODE_SELECT:
	case VERIFY:
		result = TRANSPORT_GOOD;
		break;

	default:
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		result = TRANSPORT_FAILED;
	}

	return result;
}

/***********************************************************************
 * Host functions
 ***********************************************************************/

const char *host_info(struct Scsi_Host *host)
{
	return "SCSI emulation for RTS51xx USB driver-based card reader";
}

int slave_alloc(struct scsi_device *sdev)
{
	/*
	 * Set the INQUIRY transfer length to 36.  We don't use any of
	 * the extra data and many devices choke if asked for more or
	 * less than 36 bytes.
	 */
	sdev->inquiry_len = 36;
	return 0;
}

int slave_configure(struct scsi_device *sdev)
{
	/* Scatter-gather buffers (all but the last) must have a length
	 * divisible by the bulk maxpacket size.  Otherwise a data packet
	 * would end up being short, causing a premature end to the data
	 * transfer.  Since high-speed bulk pipes have a maxpacket size
	 * of 512, we'll use that as the scsi device queue's DMA alignment
	 * mask.  Guaranteeing proper alignment of the first buffer will
	 * have the desired effect because, except at the beginning and
	 * the end, scatter-gather buffers follow page boundaries. */
	blk_queue_dma_alignment(sdev->request_queue, (512 - 1));

	/* Set the SCSI level to at least 2.  We'll leave it at 3 if that's
	 * what is originally reported.  We need this to avoid confusing
	 * the SCSI layer with devices that report 0 or 1, but need 10-byte
	 * commands (ala ATAPI devices behind certain bridges, or devices
	 * which simply have broken INQUIRY data).
	 *
	 * NOTE: This means /dev/sg programs (ala cdrecord) will get the
	 * actual information.  This seems to be the preference for
	 * programs like that.
	 *
	 * NOTE: This also means that /proc/scsi/scsi and sysfs may report
	 * the actual value or the modified one, depending on where the
	 * data comes from.
	 */
	if (sdev->scsi_level < SCSI_2)
		sdev->scsi_level = sdev->sdev_target->scsi_level = SCSI_2;

	return 0;
}

/***********************************************************************
 * /proc/scsi/ functions
 ***********************************************************************/

/* we use this macro to help us write into the buffer */
#undef SPRINTF
#define SPRINTF(args...) \
	do { if (pos < buffer+length) pos += sprintf(pos, ## args); } while (0)

int proc_info(struct Scsi_Host *host, char *buffer,
	      char **start, off_t offset, int length, int inout)
{
	char *pos = buffer;

	/* if someone is sending us data, just throw it away */
	if (inout)
		return length;

	/* print the controller name */
	SPRINTF("   Host scsi%d: %s\n", host->host_no, RTS51X_NAME);

	/* print product, vendor, and driver version strings */
	SPRINTF("       Vendor: Realtek Corp.\n");
	SPRINTF("      Product: RTS51xx USB Card Reader\n");
	SPRINTF("      Version: %s\n", DRIVER_VERSION);
	SPRINTF("        Build: %s\n", __TIME__);

	/*
	 * Calculate start of next buffer, and return value.
	 */
	*start = buffer + offset;

	if ((pos - buffer) < offset)
		return 0;
	else if ((pos - buffer - offset) < length)
		return pos - buffer - offset;
	else
		return length;
}

/* queue a command */
/* This is always called with scsi_lock(host) held */
int queuecommand_lck(struct scsi_cmnd *srb, void (*done) (struct scsi_cmnd *))
{
	struct rts51x_chip *chip = host_to_rts51x(srb->device->host);

	/* check for state-transition errors */
	if (chip->srb != NULL) {
		RTS51X_DEBUGP("Error in %s: chip->srb = %p\n",
			       __func__, chip->srb);
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	/* fail the command if we are disconnecting */
	if (test_bit(FLIDX_DISCONNECTING, &chip->usb->dflags)) {
		RTS51X_DEBUGP("Fail command during disconnect\n");
		srb->result = DID_NO_CONNECT << 16;
		done(srb);
		return 0;
	}

	/* enqueue the command and wake up the control thread */
	srb->scsi_done = done;
	chip->srb = srb;
	complete(&chip->usb->cmnd_ready);

	return 0;
}

DEF_SCSI_QCMD(queuecommand)
/***********************************************************************
 * Error handling functions
 ***********************************************************************/
/* Command timeout and abort */
int command_abort(struct scsi_cmnd *srb)
{
	struct rts51x_chip *chip = host_to_rts51x(srb->device->host);

	RTS51X_DEBUGP("%s called\n", __func__);

	/* us->srb together with the TIMED_OUT, RESETTING, and ABORTING
	 * bits are protected by the host lock. */
	scsi_lock(rts51x_to_host(chip));

	/* Is this command still active? */
	if (chip->srb != srb) {
		scsi_unlock(rts51x_to_host(chip));
		RTS51X_DEBUGP("-- nothing to abort\n");
		return FAILED;
	}

	/* Set the TIMED_OUT bit.  Also set the ABORTING bit, but only if
	 * a device reset isn't already in progress (to avoid interfering
	 * with the reset).  Note that we must retain the host lock while
	 * calling usb_stor_stop_transport(); otherwise it might interfere
	 * with an auto-reset that begins as soon as we release the lock. */
	set_bit(FLIDX_TIMED_OUT, &chip->usb->dflags);
	if (!test_bit(FLIDX_RESETTING, &chip->usb->dflags)) {
		set_bit(FLIDX_ABORTING, &chip->usb->dflags);
		/* rts51x_stop_transport(us); */
	}
	scsi_unlock(rts51x_to_host(chip));

	/* Wait for the aborted command to finish */
	wait_for_completion(&chip->usb->notify);
	return SUCCESS;
}

/* This invokes the transport reset mechanism to reset the state of the
 * device */
int device_reset(struct scsi_cmnd *srb)
{
	int result = 0;

	RTS51X_DEBUGP("%s called\n", __func__);

	return result < 0 ? FAILED : SUCCESS;
}

/* Simulate a SCSI bus reset by resetting the device's USB port. */
int bus_reset(struct scsi_cmnd *srb)
{
	int result = 0;

	RTS51X_DEBUGP("%s called\n", __func__);

	return result < 0 ? FAILED : SUCCESS;
}

static const char *rts5139_info(struct Scsi_Host *host)
{
	return "SCSI emulation for RTS5139 USB card reader";
}

struct scsi_host_template rts51x_host_template = {
	/* basic userland interface stuff */
	.name = RTS51X_NAME,
	.proc_name = RTS51X_NAME,
	.proc_info = proc_info,
	.info = rts5139_info,

	/* command interface -- queued only */
	.queuecommand = queuecommand,

	/* error and abort handlers */
	.eh_abort_handler = command_abort,
	.eh_device_reset_handler = device_reset,
	.eh_bus_reset_handler = bus_reset,

	/* queue commands only, only one command per LUN */
	.can_queue = 1,
	.cmd_per_lun = 1,

	/* unknown initiator id */
	.this_id = -1,

	.slave_alloc = slave_alloc,
	.slave_configure = slave_configure,

	/* lots of sg segments can be handled */
	.sg_tablesize = SG_ALL,

	/* limit the total size of a transfer to 120 KB */
	.max_sectors = 240,

	/* merge commands... this seems to help performance, but
	 * periodically someone should test to see which setting is more
	 * optimal.
	 */
	.use_clustering = 1,

	/* emulated HBA */
	.emulated = 1,

	/* we do our own delay after a device or bus reset */
	.skip_settle_delay = 1,

	/* sysfs device attributes */
	/* .sdev_attrs = sysfs_device_attr_list, */

	/* module management */
	.module = THIS_MODULE
};

