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
#include <linux/vmalloc.h>

#include "rtsx.h"
#include "rtsx_transport.h"
#include "rtsx_sys.h"
#include "rtsx_card.h"
#include "rtsx_chip.h"
#include "rtsx_scsi.h"
#include "sd.h"
#include "ms.h"
#include "spi.h"

void scsi_show_command(struct scsi_cmnd *srb)
{
	char *what = NULL;
	int i, unknown_cmd = 0;

	switch (srb->cmnd[0]) {
	case TEST_UNIT_READY:
		what = "TEST_UNIT_READY";
		break;
	case REZERO_UNIT:
		what = "REZERO_UNIT";
		break;
	case REQUEST_SENSE:
		what = "REQUEST_SENSE";
		break;
	case FORMAT_UNIT:
		what = "FORMAT_UNIT";
		break;
	case READ_BLOCK_LIMITS:
		what = "READ_BLOCK_LIMITS";
		break;
	case REASSIGN_BLOCKS:
		what = "REASSIGN_BLOCKS";
		break;
	case READ_6:
		what = "READ_6";
		break;
	case WRITE_6:
		what = "WRITE_6";
		break;
	case SEEK_6:
		what = "SEEK_6";
		break;
	case READ_REVERSE:
		what = "READ_REVERSE";
		break;
	case WRITE_FILEMARKS:
		what = "WRITE_FILEMARKS";
		break;
	case SPACE:
		what = "SPACE";
		break;
	case INQUIRY:
		what = "INQUIRY";
		break;
	case RECOVER_BUFFERED_DATA:
		what = "RECOVER_BUFFERED_DATA";
		break;
	case MODE_SELECT:
		what = "MODE_SELECT";
		break;
	case RESERVE:
		what = "RESERVE";
		break;
	case RELEASE:
		what = "RELEASE";
		break;
	case COPY:
		what = "COPY";
		break;
	case ERASE:
		what = "ERASE";
		break;
	case MODE_SENSE:
		what = "MODE_SENSE";
		break;
	case START_STOP:
		what = "START_STOP";
		break;
	case RECEIVE_DIAGNOSTIC:
		what = "RECEIVE_DIAGNOSTIC";
		break;
	case SEND_DIAGNOSTIC:
		what = "SEND_DIAGNOSTIC";
		break;
	case ALLOW_MEDIUM_REMOVAL:
		what = "ALLOW_MEDIUM_REMOVAL";
		break;
	case SET_WINDOW:
		what = "SET_WINDOW";
		break;
	case READ_CAPACITY:
		what = "READ_CAPACITY";
		break;
	case READ_10:
		what = "READ_10";
		break;
	case WRITE_10:
		what = "WRITE_10";
		break;
	case SEEK_10:
		what = "SEEK_10";
		break;
	case WRITE_VERIFY:
		what = "WRITE_VERIFY";
		break;
	case VERIFY:
		what = "VERIFY";
		break;
	case SEARCH_HIGH:
		what = "SEARCH_HIGH";
		break;
	case SEARCH_EQUAL:
		what = "SEARCH_EQUAL";
		break;
	case SEARCH_LOW:
		what = "SEARCH_LOW";
		break;
	case SET_LIMITS:
		what = "SET_LIMITS";
		break;
	case READ_POSITION:
		what = "READ_POSITION";
		break;
	case SYNCHRONIZE_CACHE:
		what = "SYNCHRONIZE_CACHE";
		break;
	case LOCK_UNLOCK_CACHE:
		what = "LOCK_UNLOCK_CACHE";
		break;
	case READ_DEFECT_DATA:
		what = "READ_DEFECT_DATA";
		break;
	case MEDIUM_SCAN:
		what = "MEDIUM_SCAN";
		break;
	case COMPARE:
		what = "COMPARE";
		break;
	case COPY_VERIFY:
		what = "COPY_VERIFY";
		break;
	case WRITE_BUFFER:
		what = "WRITE_BUFFER";
		break;
	case READ_BUFFER:
		what = "READ_BUFFER";
		break;
	case UPDATE_BLOCK:
		what = "UPDATE_BLOCK";
		break;
	case READ_LONG:
		what = "READ_LONG";
		break;
	case WRITE_LONG:
		what = "WRITE_LONG";
		break;
	case CHANGE_DEFINITION:
		what = "CHANGE_DEFINITION";
		break;
	case WRITE_SAME:
		what = "WRITE_SAME";
		break;
	case GPCMD_READ_SUBCHANNEL:
		what = "READ SUBCHANNEL";
		break;
	case READ_TOC:
		what = "READ_TOC";
		break;
	case GPCMD_READ_HEADER:
		what = "READ HEADER";
		break;
	case GPCMD_PLAY_AUDIO_10:
		what = "PLAY AUDIO (10)";
		break;
	case GPCMD_PLAY_AUDIO_MSF:
		what = "PLAY AUDIO MSF";
		break;
	case GPCMD_GET_EVENT_STATUS_NOTIFICATION:
		what = "GET EVENT/STATUS NOTIFICATION";
		break;
	case GPCMD_PAUSE_RESUME:
		what = "PAUSE/RESUME";
		break;
	case LOG_SELECT:
		what = "LOG_SELECT";
		break;
	case LOG_SENSE:
		what = "LOG_SENSE";
		break;
	case GPCMD_STOP_PLAY_SCAN:
		what = "STOP PLAY/SCAN";
		break;
	case GPCMD_READ_DISC_INFO:
		what = "READ DISC INFORMATION";
		break;
	case GPCMD_READ_TRACK_RZONE_INFO:
		what = "READ TRACK INFORMATION";
		break;
	case GPCMD_RESERVE_RZONE_TRACK:
		what = "RESERVE TRACK";
		break;
	case GPCMD_SEND_OPC:
		what = "SEND OPC";
		break;
	case MODE_SELECT_10:
		what = "MODE_SELECT_10";
		break;
	case GPCMD_REPAIR_RZONE_TRACK:
		what = "REPAIR TRACK";
		break;
	case 0x59:
		what = "READ MASTER CUE";
		break;
	case MODE_SENSE_10:
		what = "MODE_SENSE_10";
		break;
	case GPCMD_CLOSE_TRACK:
		what = "CLOSE TRACK/SESSION";
		break;
	case 0x5C:
		what = "READ BUFFER CAPACITY";
		break;
	case 0x5D:
		what = "SEND CUE SHEET";
		break;
	case GPCMD_BLANK:
		what = "BLANK";
		break;
	case REPORT_LUNS:
		what = "REPORT LUNS";
		break;
	case MOVE_MEDIUM:
		what = "MOVE_MEDIUM or PLAY AUDIO (12)";
		break;
	case READ_12:
		what = "READ_12";
		break;
	case WRITE_12:
		what = "WRITE_12";
		break;
	case WRITE_VERIFY_12:
		what = "WRITE_VERIFY_12";
		break;
	case SEARCH_HIGH_12:
		what = "SEARCH_HIGH_12";
		break;
	case SEARCH_EQUAL_12:
		what = "SEARCH_EQUAL_12";
		break;
	case SEARCH_LOW_12:
		what = "SEARCH_LOW_12";
		break;
	case SEND_VOLUME_TAG:
		what = "SEND_VOLUME_TAG";
		break;
	case READ_ELEMENT_STATUS:
		what = "READ_ELEMENT_STATUS";
		break;
	case GPCMD_READ_CD_MSF:
		what = "READ CD MSF";
		break;
	case GPCMD_SCAN:
		what = "SCAN";
		break;
	case GPCMD_SET_SPEED:
		what = "SET CD SPEED";
		break;
	case GPCMD_MECHANISM_STATUS:
		what = "MECHANISM STATUS";
		break;
	case GPCMD_READ_CD:
		what = "READ CD";
		break;
	case 0xE1:
		what = "WRITE CONTINUE";
		break;
	case WRITE_LONG_2:
		what = "WRITE_LONG_2";
		break;
	case VENDOR_CMND:
		what = "Realtek's vendor command";
		break;
	default:
		what = "(unknown command)"; unknown_cmd = 1;
		break;
	}

	if (srb->cmnd[0] != TEST_UNIT_READY)
		RTSX_DEBUGP("Command %s (%d bytes)\n", what, srb->cmd_len);

	if (unknown_cmd) {
		RTSX_DEBUGP("");
		for (i = 0; i < srb->cmd_len && i < 16; i++)
			RTSX_DEBUGPN(" %02x", srb->cmnd[i]);
		RTSX_DEBUGPN("\n");
	}
}

void set_sense_type(struct rtsx_chip *chip, unsigned int lun, int sense_type)
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

#ifdef SUPPORT_SD_LOCK
	case SENSE_TYPE_MEDIA_READ_FORBIDDEN:
		set_sense_data(chip, lun, CUR_ERR, 0x07, 0, 0x11, 0x13, 0, 0);
		break;
#endif

	case SENSE_TYPE_NO_SENSE:
	default:
		set_sense_data(chip, lun, CUR_ERR, 0, 0, 0, 0, 0, 0);
		break;
	}
}

void set_sense_data(struct rtsx_chip *chip, unsigned int lun, u8 err_code,
		u8 sense_key, u32 info, u8 asc, u8 ascq, u8 sns_key_info0,
		u16 sns_key_info1)
{
	struct sense_data_t *sense = &(chip->sense_buffer[lun]);

	sense->err_code = err_code;
	sense->sense_key = sense_key;
	sense->info[0] = (u8)(info >> 24);
	sense->info[1] = (u8)(info >> 16);
	sense->info[2] = (u8)(info >> 8);
	sense->info[3] = (u8)info;

	sense->ad_sense_len = sizeof(struct sense_data_t) - 8;
	sense->asc = asc;
	sense->ascq = ascq;
	if (sns_key_info0 != 0) {
		sense->sns_key_info[0] = SKSV | sns_key_info0;
		sense->sns_key_info[1] = (sns_key_info1 & 0xf0) >> 8;
		sense->sns_key_info[2] = sns_key_info1 & 0x0f;
	}
}

static int test_unit_ready(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	unsigned int lun = SCSI_LUN(srb);

	if (!check_card_ready(chip, lun)) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_NOT_PRESENT);
		return TRANSPORT_FAILED;
	}

	if (!(CHK_BIT(chip->lun_mc, lun))) {
		SET_BIT(chip->lun_mc, lun);
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_CHANGE);
		return TRANSPORT_FAILED;
	}

#ifdef SUPPORT_SD_LOCK
	if (get_lun_card(chip, SCSI_LUN(srb)) == SD_CARD) {
		struct sd_info *sd_card = &(chip->sd_card);
		if (sd_card->sd_lock_notify) {
			sd_card->sd_lock_notify = 0;
			set_sense_type(chip, lun, SENSE_TYPE_MEDIA_CHANGE);
			return TRANSPORT_FAILED;
		} else if (sd_card->sd_lock_status & SD_LOCKED) {
			set_sense_type(chip, lun,
				SENSE_TYPE_MEDIA_READ_FORBIDDEN);
			return TRANSPORT_FAILED;
		}
	}
#endif

	return TRANSPORT_GOOD;
}

static unsigned char formatter_inquiry_str[20] = {
	'M', 'E', 'M', 'O', 'R', 'Y', 'S', 'T', 'I', 'C', 'K',
#ifdef SUPPORT_MAGIC_GATE
	'-', 'M', 'G', /* Byte[47:49] */
#else
	0x20, 0x20, 0x20,  /* Byte[47:49] */
#endif

#ifdef SUPPORT_MAGIC_GATE
	0x0B,  /* Byte[50]: MG, MS, MSPro, MSXC */
#else
	0x09,  /* Byte[50]: MS, MSPro, MSXC */
#endif
	0x00,  /* Byte[51]: Category Specific Commands */
	0x00,  /* Byte[52]: Access Control and feature */
	0x20, 0x20, 0x20, /* Byte[53:55] */
};

static int inquiry(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	unsigned int lun = SCSI_LUN(srb);
	char *inquiry_default = (char *)"Generic-xD/SD/M.S.      1.00 ";
	char *inquiry_sdms =    (char *)"Generic-SD/MemoryStick  1.00 ";
	char *inquiry_sd =      (char *)"Generic-SD/MMC          1.00 ";
	char *inquiry_ms =      (char *)"Generic-MemoryStick     1.00 ";
	char *inquiry_string;
	unsigned char sendbytes;
	unsigned char *buf;
	u8 card = get_lun_card(chip, lun);
	int pro_formatter_flag = 0;
	unsigned char inquiry_buf[] = {
		QULIFIRE|DRCT_ACCESS_DEV,
		RMB_DISC|0x0D,
		0x00,
		0x01,
		0x1f,
		0x02,
		0,
		REL_ADR|WBUS_32|WBUS_16|SYNC|LINKED|CMD_QUE|SFT_RE,
	};

	if (CHECK_LUN_MODE(chip, SD_MS_2LUN)) {
		if (chip->lun2card[lun] == SD_CARD)
			inquiry_string = inquiry_sd;
		else
			inquiry_string = inquiry_ms;

	} else if (CHECK_LUN_MODE(chip, SD_MS_1LUN)) {
		inquiry_string = inquiry_sdms;
	} else {
		inquiry_string = inquiry_default;
	}

	buf = vmalloc(scsi_bufflen(srb));
	if (buf == NULL)
		TRACE_RET(chip, TRANSPORT_ERROR);

#ifdef SUPPORT_MAGIC_GATE
	if ((chip->mspro_formatter_enable) &&
			(chip->lun2card[lun] & MS_CARD))
#else
	if (chip->mspro_formatter_enable)
#endif
	{
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
		memcpy(buf + 8, inquiry_string,	sendbytes - 8);
		if (pro_formatter_flag) {
			/* Additional Length */
			buf[4] = 0x33;
		}
	} else {
		memcpy(buf, inquiry_buf, sendbytes);
	}

	if (pro_formatter_flag) {
		if (sendbytes > 36)
			memcpy(buf + 36, formatter_inquiry_str, sendbytes - 36);
	}

	scsi_set_resid(srb, 0);

	rtsx_stor_set_xfer_buf(buf, scsi_bufflen(srb), srb);
	vfree(buf);

	return TRANSPORT_GOOD;
}


static int start_stop_unit(struct scsi_cmnd *srb, struct rtsx_chip *chip)
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


static int allow_medium_removal(struct scsi_cmnd *srb, struct rtsx_chip *chip)
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


static int request_sense(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	struct sense_data_t *sense;
	unsigned int lun = SCSI_LUN(srb);
	struct ms_info *ms_card = &(chip->ms_card);
	unsigned char *tmp, *buf;

	sense = &(chip->sense_buffer[lun]);

	if ((get_lun_card(chip, lun) == MS_CARD) &&
		ms_card->pro_under_formatting) {
		if (ms_card->format_status == FORMAT_SUCCESS) {
			set_sense_type(chip, lun, SENSE_TYPE_NO_SENSE);
			ms_card->pro_under_formatting = 0;
			ms_card->progress = 0;
		} else if (ms_card->format_status == FORMAT_IN_PROGRESS) {
			/* Logical Unit Not Ready Format in Progress */
			set_sense_data(chip, lun, CUR_ERR, 0x02, 0, 0x04, 0x04,
					0, (u16)(ms_card->progress));
		} else {
			/* Format Command Failed */
			set_sense_type(chip, lun, SENSE_TYPE_FORMAT_CMD_FAILED);
			ms_card->pro_under_formatting = 0;
			ms_card->progress = 0;
		}

		rtsx_set_stat(chip, RTSX_STAT_RUN);
	}

	buf = vmalloc(scsi_bufflen(srb));
	if (buf == NULL)
		TRACE_RET(chip, TRANSPORT_ERROR);

	tmp = (unsigned char *)sense;
	memcpy(buf, tmp, scsi_bufflen(srb));

	rtsx_stor_set_xfer_buf(buf, scsi_bufflen(srb), srb);
	vfree(buf);

	scsi_set_resid(srb, 0);
	/* Reset Sense Data */
	set_sense_type(chip, lun, SENSE_TYPE_NO_SENSE);
	return TRANSPORT_GOOD;
}

static void ms_mode_sense(struct rtsx_chip *chip, u8 cmd,
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

		buf[i++] = 0x67;  /* Mode Data Length */
	} else {
		sys_info_offset = 12;
		if (data_size > 0x6C)
			data_size = 0x6C;

		buf[i++] = 0x00;  /* Mode Data Length (MSB) */
		buf[i++] = 0x6A;  /* Mode Data Length (LSB) */
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

	buf[i++] = 0x00;		/* Reserved */

	if (cmd == MODE_SENSE_10) {
		buf[i++] = 0x00;  /* Reserved */
		buf[i++] = 0x00;  /* Block descriptor length(MSB) */
		buf[i++] = 0x00;  /* Block descriptor length(LSB) */

		/* The Following Data is the content of "Page 0x20" */
		if (data_size >= 9)
			buf[i++] = 0x20;		/* Page Code */
		if (data_size >= 10)
			buf[i++] = 0x62;		/* Page Length */
		if (data_size >= 11)
			buf[i++] = 0x00;		/* No Access Control */
		if (data_size >= 12) {
			if (support_format)
				buf[i++] = 0xC0;	/* SF, SGM */
			else
				buf[i++] = 0x00;
		}
	} else {
		/* The Following Data is the content of "Page 0x20" */
		if (data_size >= 5)
			buf[i++] = 0x20;		/* Page Code */
		if (data_size >= 6)
			buf[i++] = 0x62;		/* Page Length */
		if (data_size >= 7)
			buf[i++] = 0x00;		/* No Access Control */
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

static int mode_sense(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	unsigned int lun = SCSI_LUN(srb);
	unsigned int dataSize;
	int status;
	int pro_formatter_flag;
	unsigned char pageCode, *buf;
	u8 card = get_lun_card(chip, lun);

#ifndef SUPPORT_MAGIC_GATE
	if (!check_card_ready(chip, lun)) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_NOT_PRESENT);
		scsi_set_resid(srb, scsi_bufflen(srb));
		TRACE_RET(chip, TRANSPORT_FAILED);
	}
#endif

	pro_formatter_flag = 0;
	dataSize = 8;
#ifdef SUPPORT_MAGIC_GATE
	if ((chip->lun2card[lun] & MS_CARD)) {
		if (!card || (card == MS_CARD)) {
			dataSize = 108;
			if (chip->mspro_formatter_enable)
				pro_formatter_flag = 1;
		}
	}
#else
	if (card == MS_CARD) {
		if (chip->mspro_formatter_enable) {
			pro_formatter_flag = 1;
			dataSize = 108;
		}
	}
#endif

	buf = kmalloc(dataSize, GFP_KERNEL);
	if (buf == NULL)
		TRACE_RET(chip, TRANSPORT_ERROR);

	pageCode = srb->cmnd[2] & 0x3f;

	if ((pageCode == 0x3F) || (pageCode == 0x1C) ||
		(pageCode == 0x00) ||
		(pro_formatter_flag && (pageCode == 0x20))) {
		if (srb->cmnd[0] == MODE_SENSE) {
			if ((pageCode == 0x3F) || (pageCode == 0x20)) {
				ms_mode_sense(chip, srb->cmnd[0],
					      lun, buf, dataSize);
			} else {
				dataSize = 4;
				buf[0] = 0x03;
				buf[1] = 0x00;
				if (check_card_wp(chip, lun))
					buf[2] = 0x80;
				else
					buf[2] = 0x00;

				buf[3] = 0x00;
			}
		} else {
			if ((pageCode == 0x3F) || (pageCode == 0x20)) {
				ms_mode_sense(chip, srb->cmnd[0],
					      lun, buf, dataSize);
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
		unsigned int len = min_t(unsigned int, scsi_bufflen(srb),
					dataSize);
		rtsx_stor_set_xfer_buf(buf, len, srb);
		scsi_set_resid(srb, scsi_bufflen(srb) - len);
	}
	kfree(buf);

	return status;
}

static int read_write(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
#ifdef SUPPORT_SD_LOCK
	struct sd_info *sd_card = &(chip->sd_card);
#endif
	unsigned int lun = SCSI_LUN(srb);
	int retval;
	u32 start_sec;
	u16 sec_cnt;

	rtsx_disable_aspm(chip);

	if (chip->ss_en && (rtsx_get_stat(chip) == RTSX_STAT_SS)) {
		rtsx_exit_ss(chip);
		wait_timeout(100);
	}
	rtsx_set_stat(chip, RTSX_STAT_RUN);

	if (!check_card_ready(chip, lun) || (get_card_size(chip, lun) == 0)) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_NOT_PRESENT);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	if (!(CHK_BIT(chip->lun_mc, lun))) {
		SET_BIT(chip->lun_mc, lun);
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_CHANGE);
		return TRANSPORT_FAILED;
	}

#ifdef SUPPORT_SD_LOCK
	if (sd_card->sd_erase_status) {
		/* Accessing to any card is forbidden
		 * until the erase procedure of SD is completed
		 */
		RTSX_DEBUGP("SD card being erased!\n");
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_READ_FORBIDDEN);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	if (get_lun_card(chip, lun) == SD_CARD) {
		if (sd_card->sd_lock_status & SD_LOCKED) {
			RTSX_DEBUGP("SD card locked!\n");
			set_sense_type(chip, lun,
				SENSE_TYPE_MEDIA_READ_FORBIDDEN);
			TRACE_RET(chip, TRANSPORT_FAILED);
		}
	}
#endif

	if ((srb->cmnd[0] == READ_10) || (srb->cmnd[0] == WRITE_10)) {
		start_sec = ((u32)srb->cmnd[2] << 24) |
			((u32)srb->cmnd[3] << 16) |
			((u32)srb->cmnd[4] << 8) | ((u32)srb->cmnd[5]);
		sec_cnt = ((u16)(srb->cmnd[7]) << 8) | srb->cmnd[8];
	} else if ((srb->cmnd[0] == READ_6) || (srb->cmnd[0] == WRITE_6)) {
		start_sec = ((u32)(srb->cmnd[1] & 0x1F) << 16) |
			((u32)srb->cmnd[2] << 8) | ((u32)srb->cmnd[3]);
		sec_cnt = srb->cmnd[4];
	} else if ((srb->cmnd[0] == VENDOR_CMND) &&
		(srb->cmnd[1] == SCSI_APP_CMD) &&
		((srb->cmnd[2] == PP_READ10) || (srb->cmnd[2] == PP_WRITE10))) {
		start_sec = ((u32)srb->cmnd[4] << 24) |
			((u32)srb->cmnd[5] << 16) |
			((u32)srb->cmnd[6] << 8) | ((u32)srb->cmnd[7]);
		sec_cnt = ((u16)(srb->cmnd[9]) << 8) | srb->cmnd[10];
	} else {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	/* In some test, we will receive a start_sec like 0xFFFFFFFF.
	 * In this situation, start_sec + sec_cnt will overflow, so we
	 * need to judge start_sec at first
	 */
	if ((start_sec > get_card_size(chip, lun)) ||
			((start_sec + sec_cnt) > get_card_size(chip, lun))) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_LBA_OVER_RANGE);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	if (sec_cnt == 0) {
		scsi_set_resid(srb, 0);
		return TRANSPORT_GOOD;
	}

	if (chip->rw_fail_cnt[lun] == 3) {
		RTSX_DEBUGP("read/write fail three times in succession\n");
		if (srb->sc_data_direction == DMA_FROM_DEVICE)
			set_sense_type(chip, lun,
				SENSE_TYPE_MEDIA_UNRECOVER_READ_ERR);
		else
			set_sense_type(chip, lun, SENSE_TYPE_MEDIA_WRITE_ERR);

		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	if (srb->sc_data_direction == DMA_TO_DEVICE) {
		if (check_card_wp(chip, lun)) {
			RTSX_DEBUGP("Write protected card!\n");
			set_sense_type(chip, lun,
				SENSE_TYPE_MEDIA_WRITE_PROTECT);
			TRACE_RET(chip, TRANSPORT_FAILED);
		}
	}

	retval = card_rw(srb, chip, start_sec, sec_cnt);
	if (retval != STATUS_SUCCESS) {
		if (chip->need_release & chip->lun2card[lun]) {
			chip->rw_fail_cnt[lun] = 0;
			set_sense_type(chip, lun, SENSE_TYPE_MEDIA_NOT_PRESENT);
		} else {
			chip->rw_fail_cnt[lun]++;
			if (srb->sc_data_direction == DMA_FROM_DEVICE)
				set_sense_type(chip, lun,
					SENSE_TYPE_MEDIA_UNRECOVER_READ_ERR);
			else
				set_sense_type(chip, lun,
					SENSE_TYPE_MEDIA_WRITE_ERR);
		}
		retval = TRANSPORT_FAILED;
		TRACE_GOTO(chip, Exit);
	} else {
		chip->rw_fail_cnt[lun] = 0;
		retval = TRANSPORT_GOOD;
	}

	scsi_set_resid(srb, 0);

Exit:
	return retval;
}

static int read_format_capacity(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	unsigned char *buf;
	unsigned int lun = SCSI_LUN(srb);
	unsigned int buf_len;
	u8 card = get_lun_card(chip, lun);
	u32 card_size;
	int desc_cnt;
	int i = 0;

	if (!check_card_ready(chip, lun)) {
		if (!chip->mspro_formatter_enable) {
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
	if ((buf_len > 12) && chip->mspro_formatter_enable &&
			(chip->lun2card[lun] & MS_CARD) &&
			(!card || (card == MS_CARD))) {
		buf[i++] = 0x10;
		desc_cnt = 2;
	} else {
		buf[i++] = 0x08;
		desc_cnt = 1;
	}

	while (desc_cnt) {
		if (check_card_ready(chip, lun)) {
			card_size = get_card_size(chip, lun);
			buf[i++] = (unsigned char)(card_size >> 24);
			buf[i++] = (unsigned char)(card_size >> 16);
			buf[i++] = (unsigned char)(card_size >> 8);
			buf[i++] = (unsigned char)card_size;

			if (desc_cnt == 2)
				buf[i++] = 2;
			else
				buf[i++] = 0;
		} else {
			buf[i++] = 0xFF;
			buf[i++] = 0xFF;
			buf[i++] = 0xFF;
			buf[i++] = 0xFF;

			if (desc_cnt == 2)
				buf[i++] = 3;
			else
				buf[i++] = 0;
		}

		buf[i++] = 0x00;
		buf[i++] = 0x02;
		buf[i++] = 0x00;

		desc_cnt--;
	}

	buf_len = min_t(unsigned int, scsi_bufflen(srb), buf_len);
	rtsx_stor_set_xfer_buf(buf, buf_len, srb);
	kfree(buf);

	scsi_set_resid(srb, scsi_bufflen(srb) - buf_len);

	return TRANSPORT_GOOD;
}

static int read_capacity(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	unsigned char *buf;
	unsigned int lun = SCSI_LUN(srb);
	u32 card_size;

	if (!check_card_ready(chip, lun)) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_NOT_PRESENT);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	if (!(CHK_BIT(chip->lun_mc, lun))) {
		SET_BIT(chip->lun_mc, lun);
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_CHANGE);
		return TRANSPORT_FAILED;
	}

	buf = kmalloc(8, GFP_KERNEL);
	if (buf == NULL)
		TRACE_RET(chip, TRANSPORT_ERROR);

	card_size = get_card_size(chip, lun);
	buf[0] = (unsigned char)((card_size - 1) >> 24);
	buf[1] = (unsigned char)((card_size - 1) >> 16);
	buf[2] = (unsigned char)((card_size - 1) >> 8);
	buf[3] = (unsigned char)(card_size - 1);

	buf[4] = 0x00;
	buf[5] = 0x00;
	buf[6] = 0x02;
	buf[7] = 0x00;

	rtsx_stor_set_xfer_buf(buf, scsi_bufflen(srb), srb);
	kfree(buf);

	scsi_set_resid(srb, 0);

	return TRANSPORT_GOOD;
}

static int read_eeprom(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	unsigned short len, i;
	int retval;
	u8 *buf;

	rtsx_disable_aspm(chip);

	if (chip->ss_en && (rtsx_get_stat(chip) == RTSX_STAT_SS)) {
		rtsx_exit_ss(chip);
		wait_timeout(100);
	}
	rtsx_set_stat(chip, RTSX_STAT_RUN);

	len = ((u16)srb->cmnd[4] << 8) | srb->cmnd[5];

	buf = vmalloc(len);
	if (!buf)
		TRACE_RET(chip, TRANSPORT_ERROR);

	retval = rtsx_force_power_on(chip, SSC_PDCTL);
	if (retval != STATUS_SUCCESS) {
		vfree(buf);
		set_sense_type(chip, SCSI_LUN(srb),
			SENSE_TYPE_MEDIA_UNRECOVER_READ_ERR);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	for (i = 0; i < len; i++) {
		retval = spi_read_eeprom(chip, i, buf + i);
		if (retval != STATUS_SUCCESS) {
			vfree(buf);
			set_sense_type(chip, SCSI_LUN(srb),
				SENSE_TYPE_MEDIA_UNRECOVER_READ_ERR);
			TRACE_RET(chip, TRANSPORT_FAILED);
		}
	}

	len = (unsigned short)min_t(unsigned int, scsi_bufflen(srb), len);
	rtsx_stor_set_xfer_buf(buf, len, srb);
	scsi_set_resid(srb, scsi_bufflen(srb) - len);

	vfree(buf);

	return TRANSPORT_GOOD;
}

static int write_eeprom(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	unsigned short len, i;
	int retval;
	u8 *buf;

	rtsx_disable_aspm(chip);

	if (chip->ss_en && (rtsx_get_stat(chip) == RTSX_STAT_SS)) {
		rtsx_exit_ss(chip);
		wait_timeout(100);
	}
	rtsx_set_stat(chip, RTSX_STAT_RUN);

	len = ((u16)srb->cmnd[4] << 8) | srb->cmnd[5];

	retval = rtsx_force_power_on(chip, SSC_PDCTL);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, SCSI_LUN(srb), SENSE_TYPE_MEDIA_WRITE_ERR);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	if (len == 511) {
		retval = spi_erase_eeprom_chip(chip);
		if (retval != STATUS_SUCCESS) {
			set_sense_type(chip, SCSI_LUN(srb),
				SENSE_TYPE_MEDIA_WRITE_ERR);
			TRACE_RET(chip, TRANSPORT_FAILED);
		}
	} else {
		len = (unsigned short)min_t(unsigned int, scsi_bufflen(srb),
					len);
		buf = vmalloc(len);
		if (buf == NULL)
			TRACE_RET(chip, TRANSPORT_ERROR);

		rtsx_stor_get_xfer_buf(buf, len, srb);
		scsi_set_resid(srb, scsi_bufflen(srb) - len);

		for (i = 0; i < len; i++) {
			retval = spi_write_eeprom(chip, i, buf[i]);
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

static int read_mem(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	unsigned short addr, len, i;
	int retval;
	u8 *buf;

	rtsx_disable_aspm(chip);

	if (chip->ss_en && (rtsx_get_stat(chip) == RTSX_STAT_SS)) {
		rtsx_exit_ss(chip);
		wait_timeout(100);
	}
	rtsx_set_stat(chip, RTSX_STAT_RUN);

	addr = ((u16)srb->cmnd[2] << 8) | srb->cmnd[3];
	len = ((u16)srb->cmnd[4] << 8) | srb->cmnd[5];

	if (addr < 0xFC00) {
		set_sense_type(chip, SCSI_LUN(srb),
			SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	buf = vmalloc(len);
	if (!buf)
		TRACE_RET(chip, TRANSPORT_ERROR);

	retval = rtsx_force_power_on(chip, SSC_PDCTL);
	if (retval != STATUS_SUCCESS) {
		vfree(buf);
		set_sense_type(chip, SCSI_LUN(srb), SENSE_TYPE_MEDIA_WRITE_ERR);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	for (i = 0; i < len; i++) {
		retval = rtsx_read_register(chip, addr + i, buf + i);
		if (retval != STATUS_SUCCESS) {
			vfree(buf);
			set_sense_type(chip, SCSI_LUN(srb),
				SENSE_TYPE_MEDIA_UNRECOVER_READ_ERR);
			TRACE_RET(chip, TRANSPORT_FAILED);
		}
	}

	len = (unsigned short)min_t(unsigned int, scsi_bufflen(srb), len);
	rtsx_stor_set_xfer_buf(buf, len, srb);
	scsi_set_resid(srb, scsi_bufflen(srb) - len);

	vfree(buf);

	return TRANSPORT_GOOD;
}

static int write_mem(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	unsigned short addr, len, i;
	int retval;
	u8 *buf;

	rtsx_disable_aspm(chip);

	if (chip->ss_en && (rtsx_get_stat(chip) == RTSX_STAT_SS)) {
		rtsx_exit_ss(chip);
		wait_timeout(100);
	}
	rtsx_set_stat(chip, RTSX_STAT_RUN);

	addr = ((u16)srb->cmnd[2] << 8) | srb->cmnd[3];
	len = ((u16)srb->cmnd[4] << 8) | srb->cmnd[5];

	if (addr < 0xFC00) {
		set_sense_type(chip, SCSI_LUN(srb),
			SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	len = (unsigned short)min_t(unsigned int, scsi_bufflen(srb), len);
	buf = vmalloc(len);
	if (buf == NULL)
		TRACE_RET(chip, TRANSPORT_ERROR);

	rtsx_stor_get_xfer_buf(buf, len, srb);
	scsi_set_resid(srb, scsi_bufflen(srb) - len);

	retval = rtsx_force_power_on(chip, SSC_PDCTL);
	if (retval != STATUS_SUCCESS) {
		vfree(buf);
		set_sense_type(chip, SCSI_LUN(srb), SENSE_TYPE_MEDIA_WRITE_ERR);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	for (i = 0; i < len; i++) {
		retval = rtsx_write_register(chip, addr + i, 0xFF, buf[i]);
		if (retval != STATUS_SUCCESS) {
			vfree(buf);
			set_sense_type(chip, SCSI_LUN(srb),
				SENSE_TYPE_MEDIA_WRITE_ERR);
			TRACE_RET(chip, TRANSPORT_FAILED);
		}
	}

	vfree(buf);

	return TRANSPORT_GOOD;
}

static int get_sd_csd(struct scsi_cmnd *srb, struct rtsx_chip *chip)
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
	rtsx_stor_set_xfer_buf(sd_card->raw_csd, scsi_bufflen(srb), srb);

	return TRANSPORT_GOOD;
}

static int toggle_gpio_cmd(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	u8 gpio = srb->cmnd[2];

	rtsx_disable_aspm(chip);

	if (chip->ss_en && (rtsx_get_stat(chip) == RTSX_STAT_SS)) {
		rtsx_exit_ss(chip);
		wait_timeout(100);
	}
	rtsx_set_stat(chip, RTSX_STAT_RUN);

	if (gpio > 3)
		gpio = 1;
	toggle_gpio(chip, gpio);

	return TRANSPORT_GOOD;
}

#ifdef _MSG_TRACE
static int trace_msg_cmd(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	unsigned char *ptr, *buf = NULL;
	int i, msg_cnt;
	u8 clear;
	unsigned int buf_len;

	buf_len = 4 + ((2 + MSG_FUNC_LEN + MSG_FILE_LEN + TIME_VAL_LEN) *
		TRACE_ITEM_CNT);

	if ((scsi_bufflen(srb) < buf_len) || (scsi_sglist(srb) == NULL)) {
		set_sense_type(chip, SCSI_LUN(srb),
			SENSE_TYPE_MEDIA_UNRECOVER_READ_ERR);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	clear = srb->cmnd[2];

	buf = vmalloc(scsi_bufflen(srb));
	if (buf == NULL)
		TRACE_RET(chip, TRANSPORT_ERROR);
	ptr = buf;

	if (chip->trace_msg[chip->msg_idx].valid)
		msg_cnt = TRACE_ITEM_CNT;
	else
		msg_cnt = chip->msg_idx;

	*(ptr++) = (u8)(msg_cnt >> 24);
	*(ptr++) = (u8)(msg_cnt >> 16);
	*(ptr++) = (u8)(msg_cnt >> 8);
	*(ptr++) = (u8)msg_cnt;
	RTSX_DEBUGP("Trace message count is %d\n", msg_cnt);

	for (i = 1; i <= msg_cnt; i++) {
		int j, idx;

		idx = chip->msg_idx - i;
		if (idx < 0)
			idx += TRACE_ITEM_CNT;

		*(ptr++) = (u8)(chip->trace_msg[idx].line >> 8);
		*(ptr++) = (u8)(chip->trace_msg[idx].line);
		for (j = 0; j < MSG_FUNC_LEN; j++)
			*(ptr++) = chip->trace_msg[idx].func[j];

		for (j = 0; j < MSG_FILE_LEN; j++)
			*(ptr++) = chip->trace_msg[idx].file[j];

		for (j = 0; j < TIME_VAL_LEN; j++)
			*(ptr++) = chip->trace_msg[idx].timeval_buf[j];
	}

	rtsx_stor_set_xfer_buf(buf, scsi_bufflen(srb), srb);
	vfree(buf);

	if (clear) {
		chip->msg_idx = 0;
		for (i = 0; i < TRACE_ITEM_CNT; i++)
			chip->trace_msg[i].valid = 0;
	}

	scsi_set_resid(srb, 0);
	return TRANSPORT_GOOD;
}
#endif

static int read_host_reg(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	u8 addr, buf[4];
	u32 val;
	unsigned int len;

	rtsx_disable_aspm(chip);

	if (chip->ss_en && (rtsx_get_stat(chip) == RTSX_STAT_SS)) {
		rtsx_exit_ss(chip);
		wait_timeout(100);
	}
	rtsx_set_stat(chip, RTSX_STAT_RUN);

	addr = srb->cmnd[4];

	val = rtsx_readl(chip, addr);
	RTSX_DEBUGP("Host register (0x%x): 0x%x\n", addr, val);

	buf[0] = (u8)(val >> 24);
	buf[1] = (u8)(val >> 16);
	buf[2] = (u8)(val >> 8);
	buf[3] = (u8)val;

	len = min_t(unsigned int, scsi_bufflen(srb), 4);
	rtsx_stor_set_xfer_buf(buf, len, srb);
	scsi_set_resid(srb, scsi_bufflen(srb) - len);

	return TRANSPORT_GOOD;
}

static int write_host_reg(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	u8 addr, buf[4];
	u32 val;
	unsigned int len;

	rtsx_disable_aspm(chip);

	if (chip->ss_en && (rtsx_get_stat(chip) == RTSX_STAT_SS)) {
		rtsx_exit_ss(chip);
		wait_timeout(100);
	}
	rtsx_set_stat(chip, RTSX_STAT_RUN);

	addr = srb->cmnd[4];

	len = min_t(unsigned int, scsi_bufflen(srb), 4);
	rtsx_stor_get_xfer_buf(buf, len, srb);
	scsi_set_resid(srb, scsi_bufflen(srb) - len);

	val = ((u32)buf[0] << 24) | ((u32)buf[1] << 16) | ((u32)buf[2]
							<< 8) | buf[3];

	rtsx_writel(chip, addr, val);

	return TRANSPORT_GOOD;
}

static int set_variable(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	unsigned lun = SCSI_LUN(srb);

	if (srb->cmnd[3] == 1) {
		/* Variable Clock */
		struct xd_info *xd_card = &(chip->xd_card);
		struct sd_info *sd_card = &(chip->sd_card);
		struct ms_info *ms_card = &(chip->ms_card);

		switch (srb->cmnd[4]) {
		case XD_CARD:
			xd_card->xd_clock = srb->cmnd[5];
			break;

		case SD_CARD:
			sd_card->sd_clock = srb->cmnd[5];
			break;

		case MS_CARD:
			ms_card->ms_clock = srb->cmnd[5];
			break;

		default:
			set_sense_type(chip, lun,
				SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
			TRACE_RET(chip, TRANSPORT_FAILED);
		}
	} else if (srb->cmnd[3] == 2) {
		if (srb->cmnd[4]) {
			chip->blink_led = 1;
		} else {
			int retval;

			chip->blink_led = 0;

			rtsx_disable_aspm(chip);

			if (chip->ss_en &&
				(rtsx_get_stat(chip) == RTSX_STAT_SS)) {
				rtsx_exit_ss(chip);
				wait_timeout(100);
			}
			rtsx_set_stat(chip, RTSX_STAT_RUN);

			retval = rtsx_force_power_on(chip, SSC_PDCTL);
			if (retval != STATUS_SUCCESS) {
				set_sense_type(chip, SCSI_LUN(srb),
					SENSE_TYPE_MEDIA_WRITE_ERR);
				TRACE_RET(chip, TRANSPORT_FAILED);
			}

			turn_off_led(chip, LED_GPIO);
		}
	} else {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	return TRANSPORT_GOOD;
}

static int get_variable(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	unsigned int lun = SCSI_LUN(srb);

	if (srb->cmnd[3] == 1) {
		struct xd_info *xd_card = &(chip->xd_card);
		struct sd_info *sd_card = &(chip->sd_card);
		struct ms_info *ms_card = &(chip->ms_card);
		u8 tmp;

		switch (srb->cmnd[4]) {
		case XD_CARD:
			tmp = (u8)(xd_card->xd_clock);
			break;

		case SD_CARD:
			tmp = (u8)(sd_card->sd_clock);
			break;

		case MS_CARD:
			tmp = (u8)(ms_card->ms_clock);
			break;

		default:
			set_sense_type(chip, lun,
				SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
			TRACE_RET(chip, TRANSPORT_FAILED);
		}

		rtsx_stor_set_xfer_buf(&tmp, 1, srb);
	} else if (srb->cmnd[3] == 2) {
		u8 tmp = chip->blink_led;
		rtsx_stor_set_xfer_buf(&tmp, 1, srb);
	} else {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	return TRANSPORT_GOOD;
}

static int dma_access_ring_buffer(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	int retval;
	unsigned int lun = SCSI_LUN(srb);
	u16 len;

	rtsx_disable_aspm(chip);

	if (chip->ss_en && (rtsx_get_stat(chip) == RTSX_STAT_SS)) {
		rtsx_exit_ss(chip);
		wait_timeout(100);
	}
	rtsx_set_stat(chip, RTSX_STAT_RUN);

	len = ((u16)(srb->cmnd[4]) << 8) | srb->cmnd[5];
	len = min_t(u16, len, scsi_bufflen(srb));

	if (srb->sc_data_direction == DMA_FROM_DEVICE)
		RTSX_DEBUGP("Read from device\n");
	else
		RTSX_DEBUGP("Write to device\n");

	retval = rtsx_transfer_data(chip, 0, scsi_sglist(srb), len,
			scsi_sg_count(srb), srb->sc_data_direction, 1000);
	if (retval < 0) {
		if (srb->sc_data_direction == DMA_FROM_DEVICE)
			set_sense_type(chip, lun,
				SENSE_TYPE_MEDIA_UNRECOVER_READ_ERR);
		else
			set_sense_type(chip, lun,
				SENSE_TYPE_MEDIA_WRITE_ERR);

		TRACE_RET(chip, TRANSPORT_FAILED);
	}
	scsi_set_resid(srb, 0);

	return TRANSPORT_GOOD;
}

static int get_dev_status(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	struct sd_info *sd_card = &(chip->sd_card);
	struct ms_info *ms_card = &(chip->ms_card);
	int buf_len;
	unsigned int lun = SCSI_LUN(srb);
	u8 card = get_lun_card(chip, lun);
	u8 status[32];
#ifdef SUPPORT_OCP
	u8 oc_now_mask = 0, oc_ever_mask = 0;
#endif

	memset(status, 0, 32);

	status[0] = (u8)(chip->product_id);
	status[1] = chip->ic_version;

	if (chip->auto_delink_en)
		status[2] = 0x10;
	else
		status[2] = 0x00;

	status[3] = 20;
	status[4] = 10;
	status[5] = 05;
	status[6] = 21;

	if (chip->card_wp)
		status[7] = 0x20;
	else
		status[7] = 0x00;

#ifdef SUPPORT_OCP
	status[8] = 0;
	if (CHECK_LUN_MODE(chip,
		SD_MS_2LUN) && (chip->lun2card[lun] == MS_CARD)) {
		oc_now_mask = MS_OC_NOW;
		oc_ever_mask = MS_OC_EVER;
	} else {
		oc_now_mask = SD_OC_NOW;
		oc_ever_mask = SD_OC_EVER;
	}

	if (chip->ocp_stat & oc_now_mask)
		status[8] |= 0x02;

	if (chip->ocp_stat & oc_ever_mask)
		status[8] |= 0x01;
#endif

	if (card == SD_CARD) {
		if (CHK_SD(sd_card)) {
			if (CHK_SD_HCXC(sd_card)) {
				if (sd_card->capacity > 0x4000000)
					status[0x0E] = 0x02;
				else
					status[0x0E] = 0x01;
			} else {
				status[0x0E] = 0x00;
			}

			if (CHK_SD_SDR104(sd_card))
				status[0x0F] = 0x03;
			else if (CHK_SD_DDR50(sd_card))
				status[0x0F] = 0x04;
			else if (CHK_SD_SDR50(sd_card))
				status[0x0F] = 0x02;
			else if (CHK_SD_HS(sd_card))
				status[0x0F] = 0x01;
			else
				status[0x0F] = 0x00;
		} else {
			if (CHK_MMC_SECTOR_MODE(sd_card))
				status[0x0E] = 0x01;
			else
				status[0x0E] = 0x00;

			if (CHK_MMC_DDR52(sd_card))
				status[0x0F] = 0x03;
			else if (CHK_MMC_52M(sd_card))
				status[0x0F] = 0x02;
			else if (CHK_MMC_26M(sd_card))
				status[0x0F] = 0x01;
			else
				status[0x0F] = 0x00;
		}
	} else if (card == MS_CARD) {
		if (CHK_MSPRO(ms_card)) {
			if (CHK_MSXC(ms_card))
				status[0x0E] = 0x01;
			else
				status[0x0E] = 0x00;

			if (CHK_HG8BIT(ms_card))
				status[0x0F] = 0x01;
			else
				status[0x0F] = 0x00;
		}
	}

#ifdef SUPPORT_SD_LOCK
	if (card == SD_CARD) {
		status[0x17] = 0x80;
		if (sd_card->sd_erase_status)
			status[0x17] |= 0x01;
		if (sd_card->sd_lock_status & SD_LOCKED) {
			status[0x17] |= 0x02;
			status[0x07] |= 0x40;
		}
		if (sd_card->sd_lock_status & SD_PWD_EXIST)
			status[0x17] |= 0x04;
	} else {
		status[0x17] = 0x00;
	}

	RTSX_DEBUGP("status[0x17] = 0x%x\n", status[0x17]);
#endif

	status[0x18] = 0x8A;
	status[0x1A] = 0x28;
#ifdef SUPPORT_SD_LOCK
	status[0x1F] = 0x01;
#endif

	buf_len = min_t(unsigned int, scsi_bufflen(srb), sizeof(status));
	rtsx_stor_set_xfer_buf(status, buf_len, srb);
	scsi_set_resid(srb, scsi_bufflen(srb) - buf_len);

	return TRANSPORT_GOOD;
}

static int set_chip_mode(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	int phy_debug_mode;
	int retval;
	u16 reg;

	if (!CHECK_PID(chip, 0x5208)) {
		set_sense_type(chip, SCSI_LUN(srb),
			SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	phy_debug_mode = (int)(srb->cmnd[3]);

	if (phy_debug_mode) {
		chip->phy_debug_mode = 1;
		retval = rtsx_write_register(chip, CDRESUMECTL, 0x77, 0);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, TRANSPORT_FAILED);

		rtsx_disable_bus_int(chip);

		retval = rtsx_read_phy_register(chip, 0x1C, &reg);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, TRANSPORT_FAILED);

		reg |= 0x0001;
		retval = rtsx_write_phy_register(chip, 0x1C, reg);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, TRANSPORT_FAILED);
	} else {
		chip->phy_debug_mode = 0;
		retval = rtsx_write_register(chip, CDRESUMECTL, 0x77, 0x77);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, TRANSPORT_FAILED);

		rtsx_enable_bus_int(chip);

		retval = rtsx_read_phy_register(chip, 0x1C, &reg);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, TRANSPORT_FAILED);

		reg &= 0xFFFE;
		retval = rtsx_write_phy_register(chip, 0x1C, reg);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, TRANSPORT_FAILED);
	}

	return TRANSPORT_GOOD;
}

static int rw_mem_cmd_buf(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	int retval =  STATUS_SUCCESS;
	unsigned int lun = SCSI_LUN(srb);
	u8 cmd_type, mask, value, idx;
	u16 addr;

	rtsx_disable_aspm(chip);

	if (chip->ss_en && (rtsx_get_stat(chip) == RTSX_STAT_SS)) {
		rtsx_exit_ss(chip);
		wait_timeout(100);
	}
	rtsx_set_stat(chip, RTSX_STAT_RUN);

	switch (srb->cmnd[3]) {
	case INIT_BATCHCMD:
		rtsx_init_cmd(chip);
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
		rtsx_add_cmd(chip, cmd_type, addr, mask, value);
		break;

	case SEND_BATCHCMD:
		retval = rtsx_send_cmd(chip, 0, 1000);
		break;

	case GET_BATCHRSP:
		idx = srb->cmnd[4];
		value = *(rtsx_get_cmd_data(chip) + idx);
		if (scsi_bufflen(srb) < 1) {
			set_sense_type(chip, lun,
				SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
			TRACE_RET(chip, TRANSPORT_FAILED);
		}
		rtsx_stor_set_xfer_buf(&value, 1, srb);
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

static int suit_cmd(struct scsi_cmnd *srb, struct rtsx_chip *chip)
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

static int read_phy_register(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	unsigned short addr, len, i;
	int retval;
	u8 *buf;
	u16 val;

	rtsx_disable_aspm(chip);

	if (chip->ss_en && (rtsx_get_stat(chip) == RTSX_STAT_SS)) {
		rtsx_exit_ss(chip);
		wait_timeout(100);
	}
	rtsx_set_stat(chip, RTSX_STAT_RUN);

	addr = ((u16)srb->cmnd[4] << 8) | srb->cmnd[5];
	len = ((u16)srb->cmnd[6] << 8) | srb->cmnd[7];

	if (len % 2)
		len -= len % 2;

	if (len) {
		buf = vmalloc(len);
		if (!buf)
			TRACE_RET(chip, TRANSPORT_ERROR);

		retval = rtsx_force_power_on(chip, SSC_PDCTL);
		if (retval != STATUS_SUCCESS) {
			vfree(buf);
			set_sense_type(chip, SCSI_LUN(srb),
				SENSE_TYPE_MEDIA_UNRECOVER_READ_ERR);
			TRACE_RET(chip, TRANSPORT_FAILED);
		}

		for (i = 0; i < len / 2; i++) {
			retval = rtsx_read_phy_register(chip, addr + i, &val);
			if (retval != STATUS_SUCCESS) {
				vfree(buf);
				set_sense_type(chip, SCSI_LUN(srb),
					SENSE_TYPE_MEDIA_UNRECOVER_READ_ERR);
				TRACE_RET(chip, TRANSPORT_FAILED);
			}

			buf[2*i] = (u8)(val >> 8);
			buf[2*i+1] = (u8)val;
		}

		len = (unsigned short)min_t(unsigned int, scsi_bufflen(srb),
					len);
		rtsx_stor_set_xfer_buf(buf, len, srb);
		scsi_set_resid(srb, scsi_bufflen(srb) - len);

		vfree(buf);
	}

	return TRANSPORT_GOOD;
}

static int write_phy_register(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	unsigned short addr, len, i;
	int retval;
	u8 *buf;
	u16 val;

	rtsx_disable_aspm(chip);

	if (chip->ss_en && (rtsx_get_stat(chip) == RTSX_STAT_SS)) {
		rtsx_exit_ss(chip);
		wait_timeout(100);
	}
	rtsx_set_stat(chip, RTSX_STAT_RUN);

	addr = ((u16)srb->cmnd[4] << 8) | srb->cmnd[5];
	len = ((u16)srb->cmnd[6] << 8) | srb->cmnd[7];

	if (len % 2)
		len -= len % 2;

	if (len) {
		len = (unsigned short)min_t(unsigned int, scsi_bufflen(srb),
					len);

		buf = vmalloc(len);
		if (buf == NULL)
			TRACE_RET(chip, TRANSPORT_ERROR);

		rtsx_stor_get_xfer_buf(buf, len, srb);
		scsi_set_resid(srb, scsi_bufflen(srb) - len);

		retval = rtsx_force_power_on(chip, SSC_PDCTL);
		if (retval != STATUS_SUCCESS) {
			vfree(buf);
			set_sense_type(chip, SCSI_LUN(srb),
				SENSE_TYPE_MEDIA_WRITE_ERR);
			TRACE_RET(chip, TRANSPORT_FAILED);
		}

		for (i = 0; i < len / 2; i++) {
			val = ((u16)buf[2*i] << 8) | buf[2*i+1];
			retval = rtsx_write_phy_register(chip, addr + i, val);
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

static int erase_eeprom2(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	unsigned short addr;
	int retval;
	u8 mode;

	rtsx_disable_aspm(chip);

	if (chip->ss_en && (rtsx_get_stat(chip) == RTSX_STAT_SS)) {
		rtsx_exit_ss(chip);
		wait_timeout(100);
	}
	rtsx_set_stat(chip, RTSX_STAT_RUN);

	retval = rtsx_force_power_on(chip, SSC_PDCTL);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, SCSI_LUN(srb), SENSE_TYPE_MEDIA_WRITE_ERR);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	mode = srb->cmnd[3];
	addr = ((u16)srb->cmnd[4] << 8) | srb->cmnd[5];

	if (mode == 0) {
		retval = spi_erase_eeprom_chip(chip);
		if (retval != STATUS_SUCCESS) {
			set_sense_type(chip, SCSI_LUN(srb),
				SENSE_TYPE_MEDIA_WRITE_ERR);
			TRACE_RET(chip, TRANSPORT_FAILED);
		}
	} else if (mode == 1) {
		retval = spi_erase_eeprom_byte(chip, addr);
		if (retval != STATUS_SUCCESS) {
			set_sense_type(chip, SCSI_LUN(srb),
				SENSE_TYPE_MEDIA_WRITE_ERR);
			TRACE_RET(chip, TRANSPORT_FAILED);
		}
	} else {
		set_sense_type(chip, SCSI_LUN(srb),
			SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	return TRANSPORT_GOOD;
}

static int read_eeprom2(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	unsigned short addr, len, i;
	int retval;
	u8 *buf;

	rtsx_disable_aspm(chip);

	if (chip->ss_en && (rtsx_get_stat(chip) == RTSX_STAT_SS)) {
		rtsx_exit_ss(chip);
		wait_timeout(100);
	}
	rtsx_set_stat(chip, RTSX_STAT_RUN);

	addr = ((u16)srb->cmnd[4] << 8) | srb->cmnd[5];
	len = ((u16)srb->cmnd[6] << 8) | srb->cmnd[7];

	buf = vmalloc(len);
	if (!buf)
		TRACE_RET(chip, TRANSPORT_ERROR);

	retval = rtsx_force_power_on(chip, SSC_PDCTL);
	if (retval != STATUS_SUCCESS) {
		vfree(buf);
		set_sense_type(chip, SCSI_LUN(srb),
			SENSE_TYPE_MEDIA_UNRECOVER_READ_ERR);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	for (i = 0; i < len; i++) {
		retval = spi_read_eeprom(chip, addr + i, buf + i);
		if (retval != STATUS_SUCCESS) {
			vfree(buf);
			set_sense_type(chip, SCSI_LUN(srb),
				SENSE_TYPE_MEDIA_UNRECOVER_READ_ERR);
			TRACE_RET(chip, TRANSPORT_FAILED);
		}
	}

	len = (unsigned short)min_t(unsigned int, scsi_bufflen(srb), len);
	rtsx_stor_set_xfer_buf(buf, len, srb);
	scsi_set_resid(srb, scsi_bufflen(srb) - len);

	vfree(buf);

	return TRANSPORT_GOOD;
}

static int write_eeprom2(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	unsigned short addr, len, i;
	int retval;
	u8 *buf;

	rtsx_disable_aspm(chip);

	if (chip->ss_en && (rtsx_get_stat(chip) == RTSX_STAT_SS)) {
		rtsx_exit_ss(chip);
		wait_timeout(100);
	}
	rtsx_set_stat(chip, RTSX_STAT_RUN);

	addr = ((u16)srb->cmnd[4] << 8) | srb->cmnd[5];
	len = ((u16)srb->cmnd[6] << 8) | srb->cmnd[7];

	len = (unsigned short)min_t(unsigned int, scsi_bufflen(srb), len);
	buf = vmalloc(len);
	if (buf == NULL)
		TRACE_RET(chip, TRANSPORT_ERROR);

	rtsx_stor_get_xfer_buf(buf, len, srb);
	scsi_set_resid(srb, scsi_bufflen(srb) - len);

	retval = rtsx_force_power_on(chip, SSC_PDCTL);
	if (retval != STATUS_SUCCESS) {
		vfree(buf);
		set_sense_type(chip, SCSI_LUN(srb), SENSE_TYPE_MEDIA_WRITE_ERR);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	for (i = 0; i < len; i++) {
		retval = spi_write_eeprom(chip, addr + i, buf[i]);
		if (retval != STATUS_SUCCESS) {
			vfree(buf);
			set_sense_type(chip, SCSI_LUN(srb),
				SENSE_TYPE_MEDIA_WRITE_ERR);
			TRACE_RET(chip, TRANSPORT_FAILED);
		}
	}

	vfree(buf);

	return TRANSPORT_GOOD;
}

static int read_efuse(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	int retval;
	u8 addr, len, i;
	u8 *buf;

	rtsx_disable_aspm(chip);

	if (chip->ss_en && (rtsx_get_stat(chip) == RTSX_STAT_SS)) {
		rtsx_exit_ss(chip);
		wait_timeout(100);
	}
	rtsx_set_stat(chip, RTSX_STAT_RUN);

	addr = srb->cmnd[4];
	len = srb->cmnd[5];

	buf = vmalloc(len);
	if (!buf)
		TRACE_RET(chip, TRANSPORT_ERROR);

	retval = rtsx_force_power_on(chip, SSC_PDCTL);
	if (retval != STATUS_SUCCESS) {
		vfree(buf);
		set_sense_type(chip, SCSI_LUN(srb),
			SENSE_TYPE_MEDIA_UNRECOVER_READ_ERR);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	for (i = 0; i < len; i++) {
		retval = rtsx_read_efuse(chip, addr + i, buf + i);
		if (retval != STATUS_SUCCESS) {
			vfree(buf);
			set_sense_type(chip, SCSI_LUN(srb),
				SENSE_TYPE_MEDIA_UNRECOVER_READ_ERR);
			TRACE_RET(chip, TRANSPORT_FAILED);
		}
	}

	len = (u8)min_t(unsigned int, scsi_bufflen(srb), len);
	rtsx_stor_set_xfer_buf(buf, len, srb);
	scsi_set_resid(srb, scsi_bufflen(srb) - len);

	vfree(buf);

	return TRANSPORT_GOOD;
}

static int write_efuse(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	int retval, result = TRANSPORT_GOOD;
	u16 val;
	u8 addr, len, i;
	u8 *buf;

	rtsx_disable_aspm(chip);

	if (chip->ss_en && (rtsx_get_stat(chip) == RTSX_STAT_SS)) {
		rtsx_exit_ss(chip);
		wait_timeout(100);
	}
	rtsx_set_stat(chip, RTSX_STAT_RUN);

	addr = srb->cmnd[4];
	len = srb->cmnd[5];

	len = (u8)min_t(unsigned int, scsi_bufflen(srb), len);
	buf = vmalloc(len);
	if (buf == NULL)
		TRACE_RET(chip, TRANSPORT_ERROR);

	rtsx_stor_get_xfer_buf(buf, len, srb);
	scsi_set_resid(srb, scsi_bufflen(srb) - len);

	retval = rtsx_force_power_on(chip, SSC_PDCTL);
	if (retval != STATUS_SUCCESS) {
		vfree(buf);
		TRACE_RET(chip, TRANSPORT_ERROR);
	}

	if (chip->asic_code) {
		retval = rtsx_read_phy_register(chip, 0x08, &val);
		if (retval != STATUS_SUCCESS) {
			vfree(buf);
			TRACE_RET(chip, TRANSPORT_ERROR);
		}

		retval = rtsx_write_register(chip, PWR_GATE_CTRL,
					LDO3318_PWR_MASK, LDO_OFF);
		if (retval != STATUS_SUCCESS) {
			vfree(buf);
			TRACE_RET(chip, TRANSPORT_ERROR);
		}

		wait_timeout(600);

		retval = rtsx_write_phy_register(chip, 0x08,
						0x4C00 | chip->phy_voltage);
		if (retval != STATUS_SUCCESS) {
			vfree(buf);
			TRACE_RET(chip, TRANSPORT_ERROR);
		}

		retval = rtsx_write_register(chip, PWR_GATE_CTRL,
					LDO3318_PWR_MASK, LDO_ON);
		if (retval != STATUS_SUCCESS) {
			vfree(buf);
			TRACE_RET(chip, TRANSPORT_ERROR);
		}

		wait_timeout(600);
	}

	retval = card_power_on(chip, SPI_CARD);
	if (retval != STATUS_SUCCESS) {
		vfree(buf);
		TRACE_RET(chip, TRANSPORT_ERROR);
	}

	wait_timeout(50);

	for (i = 0; i < len; i++) {
		retval = rtsx_write_efuse(chip, addr + i, buf[i]);
		if (retval != STATUS_SUCCESS) {
			set_sense_type(chip, SCSI_LUN(srb),
				SENSE_TYPE_MEDIA_WRITE_ERR);
			result = TRANSPORT_FAILED;
			TRACE_GOTO(chip, Exit);
		}
	}

Exit:
	vfree(buf);

	retval = card_power_off(chip, SPI_CARD);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, TRANSPORT_ERROR);

	if (chip->asic_code) {
		retval = rtsx_write_register(chip, PWR_GATE_CTRL,
					LDO3318_PWR_MASK, LDO_OFF);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, TRANSPORT_ERROR);

		wait_timeout(600);

		retval = rtsx_write_phy_register(chip, 0x08, val);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, TRANSPORT_ERROR);

		retval = rtsx_write_register(chip, PWR_GATE_CTRL,
					LDO3318_PWR_MASK, LDO_ON);
		if (retval != STATUS_SUCCESS)
			TRACE_RET(chip, TRANSPORT_ERROR);
	}

	return result;
}

static int read_cfg_byte(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	int retval;
	u8 func, func_max;
	u16 addr, len;
	u8 *buf;

	rtsx_disable_aspm(chip);

	if (chip->ss_en && (rtsx_get_stat(chip) == RTSX_STAT_SS)) {
		rtsx_exit_ss(chip);
		wait_timeout(100);
	}
	rtsx_set_stat(chip, RTSX_STAT_RUN);

	func = srb->cmnd[3];
	addr = ((u16)(srb->cmnd[4]) << 8) | srb->cmnd[5];
	len = ((u16)(srb->cmnd[6]) << 8) | srb->cmnd[7];

	RTSX_DEBUGP("%s: func = %d, addr = 0x%x, len = %d\n", __func__, func,
		addr, len);

	if (CHK_SDIO_EXIST(chip) && !CHK_SDIO_IGNORED(chip))
		func_max = 1;
	else
		func_max = 0;

	if (func > func_max) {
		set_sense_type(chip, SCSI_LUN(srb),
			SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	buf = vmalloc(len);
	if (!buf)
		TRACE_RET(chip, TRANSPORT_ERROR);

	retval = rtsx_read_cfg_seq(chip, func, addr, buf, len);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, SCSI_LUN(srb),
			SENSE_TYPE_MEDIA_UNRECOVER_READ_ERR);
		vfree(buf);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	len = (u16)min_t(unsigned int, scsi_bufflen(srb), len);
	rtsx_stor_set_xfer_buf(buf, len, srb);
	scsi_set_resid(srb, scsi_bufflen(srb) - len);

	vfree(buf);

	return TRANSPORT_GOOD;
}

static int write_cfg_byte(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	int retval;
	u8 func, func_max;
	u16 addr, len;
	u8 *buf;

	rtsx_disable_aspm(chip);

	if (chip->ss_en && (rtsx_get_stat(chip) == RTSX_STAT_SS)) {
		rtsx_exit_ss(chip);
		wait_timeout(100);
	}
	rtsx_set_stat(chip, RTSX_STAT_RUN);

	func = srb->cmnd[3];
	addr = ((u16)(srb->cmnd[4]) << 8) | srb->cmnd[5];
	len = ((u16)(srb->cmnd[6]) << 8) | srb->cmnd[7];

	RTSX_DEBUGP("%s: func = %d, addr = 0x%x\n", __func__, func, addr);

	if (CHK_SDIO_EXIST(chip) && !CHK_SDIO_IGNORED(chip))
		func_max = 1;
	else
		func_max = 0;

	if (func > func_max) {
		set_sense_type(chip, SCSI_LUN(srb),
			SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	len = (unsigned short)min_t(unsigned int, scsi_bufflen(srb), len);
	buf = vmalloc(len);
	if (!buf)
		TRACE_RET(chip, TRANSPORT_ERROR);

	rtsx_stor_get_xfer_buf(buf, len, srb);
	scsi_set_resid(srb, scsi_bufflen(srb) - len);

	retval = rtsx_write_cfg_seq(chip, func, addr, buf, len);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, SCSI_LUN(srb), SENSE_TYPE_MEDIA_WRITE_ERR);
		vfree(buf);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	vfree(buf);

	return TRANSPORT_GOOD;
}

static int app_cmd(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	int result;

	switch (srb->cmnd[2]) {
	case PP_READ10:
	case PP_WRITE10:
		result = read_write(srb, chip);
		break;

	case READ_HOST_REG:
		result = read_host_reg(srb, chip);
		break;

	case WRITE_HOST_REG:
		result = write_host_reg(srb, chip);
		break;

	case GET_VAR:
		result = get_variable(srb, chip);
		break;

	case SET_VAR:
		result = set_variable(srb, chip);
		break;

	case DMA_READ:
	case DMA_WRITE:
		result = dma_access_ring_buffer(srb, chip);
		break;

	case READ_PHY:
		result = read_phy_register(srb, chip);
		break;

	case WRITE_PHY:
		result = write_phy_register(srb, chip);
		break;

	case ERASE_EEPROM2:
		result = erase_eeprom2(srb, chip);
		break;

	case READ_EEPROM2:
		result = read_eeprom2(srb, chip);
		break;

	case WRITE_EEPROM2:
		result = write_eeprom2(srb, chip);
		break;

	case READ_EFUSE:
		result = read_efuse(srb, chip);
		break;

	case WRITE_EFUSE:
		result = write_efuse(srb, chip);
		break;

	case READ_CFG:
		result = read_cfg_byte(srb, chip);
		break;

	case WRITE_CFG:
		result = write_cfg_byte(srb, chip);
		break;

	case SET_CHIP_MODE:
		result = set_chip_mode(srb, chip);
		break;

	case SUIT_CMD:
		result = suit_cmd(srb, chip);
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


static int read_status(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	u8 rtsx_status[16];
	int buf_len;
	unsigned int lun = SCSI_LUN(srb);

	rtsx_status[0] = (u8)(chip->vendor_id >> 8);
	rtsx_status[1] = (u8)(chip->vendor_id);

	rtsx_status[2] = (u8)(chip->product_id >> 8);
	rtsx_status[3] = (u8)(chip->product_id);

	rtsx_status[4] = (u8)lun;

	if (CHECK_LUN_MODE(chip, SD_MS_2LUN)) {
		if (chip->lun2card[lun] == SD_CARD)
			rtsx_status[5] = 2;
		else
			rtsx_status[5] = 3;
	} else {
		if (chip->card_exist) {
			if (chip->card_exist & XD_CARD)
				rtsx_status[5] = 4;
			else if (chip->card_exist & SD_CARD)
				rtsx_status[5] = 2;
			else if (chip->card_exist & MS_CARD)
				rtsx_status[5] = 3;
			else
				rtsx_status[5] = 7;
		} else {
			rtsx_status[5] = 7;
		}
	}

	if (CHECK_LUN_MODE(chip, SD_MS_2LUN))
		rtsx_status[6] = 2;
	else
		rtsx_status[6] = 1;

	rtsx_status[7] = (u8)(chip->product_id);
	rtsx_status[8] = chip->ic_version;

	if (check_card_exist(chip, lun))
		rtsx_status[9] = 1;
	else
		rtsx_status[9] = 0;

	if (CHECK_LUN_MODE(chip, SD_MS_2LUN))
		rtsx_status[10] = 0;
	else
		rtsx_status[10] = 1;

	if (CHECK_LUN_MODE(chip, SD_MS_2LUN)) {
		if (chip->lun2card[lun] == SD_CARD)
			rtsx_status[11] = SD_CARD;
		else
			rtsx_status[11] = MS_CARD;
	} else {
		rtsx_status[11] = XD_CARD | SD_CARD | MS_CARD;
	}

	if (check_card_ready(chip, lun))
		rtsx_status[12] = 1;
	else
		rtsx_status[12] = 0;

	if (get_lun_card(chip, lun) == XD_CARD) {
		rtsx_status[13] = 0x40;
	} else if (get_lun_card(chip, lun) == SD_CARD) {
		struct sd_info *sd_card = &(chip->sd_card);

		rtsx_status[13] = 0x20;
		if (CHK_SD(sd_card)) {
			if (CHK_SD_HCXC(sd_card))
				rtsx_status[13] |= 0x04;
			if (CHK_SD_HS(sd_card))
				rtsx_status[13] |= 0x02;
		} else {
			rtsx_status[13] |= 0x08;
			if (CHK_MMC_52M(sd_card))
				rtsx_status[13] |= 0x02;
			if (CHK_MMC_SECTOR_MODE(sd_card))
				rtsx_status[13] |= 0x04;
		}
	} else if (get_lun_card(chip, lun) == MS_CARD) {
		struct ms_info *ms_card = &(chip->ms_card);

		if (CHK_MSPRO(ms_card)) {
			rtsx_status[13] = 0x38;
			if (CHK_HG8BIT(ms_card))
				rtsx_status[13] |= 0x04;
#ifdef SUPPORT_MSXC
			if (CHK_MSXC(ms_card))
				rtsx_status[13] |= 0x01;
#endif
		} else {
			rtsx_status[13] = 0x30;
		}
	} else {
		if (CHECK_LUN_MODE(chip, DEFAULT_SINGLE)) {
#ifdef SUPPORT_SDIO
			if (chip->sd_io && chip->sd_int)
				rtsx_status[13] = 0x60;
			else
				rtsx_status[13] = 0x70;
#else
			rtsx_status[13] = 0x70;
#endif
		} else {
			if (chip->lun2card[lun] == SD_CARD)
				rtsx_status[13] = 0x20;
			else
				rtsx_status[13] = 0x30;
		}
	}

	rtsx_status[14] = 0x78;
	if (CHK_SDIO_EXIST(chip) && !CHK_SDIO_IGNORED(chip))
		rtsx_status[15] = 0x83;
	else
		rtsx_status[15] = 0x82;

	buf_len = min_t(unsigned int, scsi_bufflen(srb), sizeof(rtsx_status));
	rtsx_stor_set_xfer_buf(rtsx_status, buf_len, srb);
	scsi_set_resid(srb, scsi_bufflen(srb) - buf_len);

	return TRANSPORT_GOOD;
}

static int get_card_bus_width(struct scsi_cmnd *srb, struct rtsx_chip *chip)
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
	rtsx_stor_set_xfer_buf(&bus_width, scsi_bufflen(srb), srb);

	return TRANSPORT_GOOD;
}

static int spi_vendor_cmd(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	int result;
	unsigned int lun = SCSI_LUN(srb);
	u8 gpio_dir;

	if (CHECK_PID(chip, 0x5208) || CHECK_PID(chip, 0x5288)) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	rtsx_disable_aspm(chip);

	if (chip->ss_en && (rtsx_get_stat(chip) == RTSX_STAT_SS)) {
		rtsx_exit_ss(chip);
		wait_timeout(100);
	}
	rtsx_set_stat(chip, RTSX_STAT_RUN);

	rtsx_force_power_on(chip, SSC_PDCTL);

	rtsx_read_register(chip, CARD_GPIO_DIR, &gpio_dir);
	rtsx_write_register(chip, CARD_GPIO_DIR, 0x07, gpio_dir & 0x06);

	switch (srb->cmnd[2]) {
	case SCSI_SPI_GETSTATUS:
		result = spi_get_status(srb, chip);
		break;

	case SCSI_SPI_SETPARAMETER:
		result = spi_set_parameter(srb, chip);
		break;

	case SCSI_SPI_READFALSHID:
		result = spi_read_flash_id(srb, chip);
		break;

	case SCSI_SPI_READFLASH:
		result = spi_read_flash(srb, chip);
		break;

	case SCSI_SPI_WRITEFLASH:
		result = spi_write_flash(srb, chip);
		break;

	case SCSI_SPI_WRITEFLASHSTATUS:
		result = spi_write_flash_status(srb, chip);
		break;

	case SCSI_SPI_ERASEFLASH:
		result = spi_erase_flash(srb, chip);
		break;

	default:
		rtsx_write_register(chip, CARD_GPIO_DIR, 0x07, gpio_dir);

		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	rtsx_write_register(chip, CARD_GPIO_DIR, 0x07, gpio_dir);

	if (result != STATUS_SUCCESS)
		TRACE_RET(chip, TRANSPORT_FAILED);

	return TRANSPORT_GOOD;
}

static int vendor_cmnd(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	int result;

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

	case READ_EEPROM:
		result = read_eeprom(srb, chip);
		break;

	case WRITE_EEPROM:
		result = write_eeprom(srb, chip);
		break;

	case TOGGLE_GPIO:
		result = toggle_gpio_cmd(srb, chip);
		break;

	case GET_SD_CSD:
		result = get_sd_csd(srb, chip);
		break;

	case GET_BUS_WIDTH:
		result = get_card_bus_width(srb, chip);
		break;

#ifdef _MSG_TRACE
	case TRACE_MSG:
		result = trace_msg_cmd(srb, chip);
		break;
#endif

	case SCSI_APP_CMD:
		result = app_cmd(srb, chip);
		break;

	case SPI_VENDOR_COMMAND:
		result = spi_vendor_cmd(srb, chip);
		break;

	default:
		set_sense_type(chip, SCSI_LUN(srb),
			SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	return result;
}

#if !defined(LED_AUTO_BLINK) && !defined(REGULAR_BLINK)
void led_shine(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	unsigned int lun = SCSI_LUN(srb);
	u16 sec_cnt;

	if ((srb->cmnd[0] == READ_10) || (srb->cmnd[0] == WRITE_10))
		sec_cnt = ((u16)(srb->cmnd[7]) << 8) | srb->cmnd[8];
	else if ((srb->cmnd[0] == READ_6) || (srb->cmnd[0] == WRITE_6))
		sec_cnt = srb->cmnd[4];
	else
		return;

	if (chip->rw_cap[lun] >= GPIO_TOGGLE_THRESHOLD) {
		toggle_gpio(chip, LED_GPIO);
		chip->rw_cap[lun] = 0;
	} else {
		chip->rw_cap[lun] += sec_cnt;
	}
}
#endif

static int ms_format_cmnd(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	struct ms_info *ms_card = &(chip->ms_card);
	unsigned int lun = SCSI_LUN(srb);
	int retval, quick_format;

	if (get_lun_card(chip, lun) != MS_CARD) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_LUN_NOT_SUPPORT);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	if ((srb->cmnd[3] != 0x4D) || (srb->cmnd[4] != 0x47) ||
		(srb->cmnd[5] != 0x66) || (srb->cmnd[6] != 0x6D) ||
		(srb->cmnd[7] != 0x74)) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	rtsx_disable_aspm(chip);

	if (chip->ss_en && (rtsx_get_stat(chip) == RTSX_STAT_SS)) {
		rtsx_exit_ss(chip);
		wait_timeout(100);

		if (!check_card_ready(chip, lun) ||
				(get_card_size(chip, lun) == 0)) {
			set_sense_type(chip, lun, SENSE_TYPE_MEDIA_NOT_PRESENT);
			TRACE_RET(chip, TRANSPORT_FAILED);
		}
	}
	rtsx_set_stat(chip, RTSX_STAT_RUN);

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

	retval = mspro_format(srb, chip, MS_SHORT_DATA_LEN, quick_format);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_FORMAT_CMD_FAILED);
		TRACE_RET(chip, TRANSPORT_FAILED);
	}

	scsi_set_resid(srb, 0);
	return TRANSPORT_GOOD;
}

#ifdef SUPPORT_PCGL_1P18
static int get_ms_information(struct scsi_cmnd *srb, struct rtsx_chip *chip)
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
	/*  GET Memory Stick Media Information Response Header */
	buf[i++] = 0x00;		/* Data length MSB */
	buf[i++] = data_len;		/* Data length LSB */
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

	/*  Device Information Body */

	/* Device Information ID Number */
	buf[i++] = dev_info_id;
	/* Device Information Length */
	if (dev_info_id == 0x15)
		data_len = 0x31;
	else
		data_len = 0x61;

	buf[i++] = 0x00;		/* Data length MSB */
	buf[i++] = data_len;		/* Data length LSB */
	/* Valid Bit */
	buf[i++] = 0x80;
	if ((dev_info_id == 0x10) || (dev_info_id == 0x13)) {
		/* System Information */
		memcpy(buf+i, ms_card->raw_sys_info, 96);
	} else {
		/* Model Name */
		memcpy(buf+i, ms_card->raw_model_name, 48);
	}

	rtsx_stor_set_xfer_buf(buf, buf_len, srb);

	if (dev_info_id == 0x15)
		scsi_set_resid(srb, scsi_bufflen(srb)-0x3C);
	else
		scsi_set_resid(srb, scsi_bufflen(srb)-0x6C);

	kfree(buf);
	return STATUS_SUCCESS;
}
#endif

static int ms_sp_cmnd(struct scsi_cmnd *srb, struct rtsx_chip *chip)
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
static int sd_extention_cmnd(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	unsigned int lun = SCSI_LUN(srb);
	int result;

	rtsx_disable_aspm(chip);

	if (chip->ss_en && (rtsx_get_stat(chip) == RTSX_STAT_SS)) {
		rtsx_exit_ss(chip);
		wait_timeout(100);
	}
	rtsx_set_stat(chip, RTSX_STAT_RUN);

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
static int mg_report_key(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	struct ms_info *ms_card = &(chip->ms_card);
	unsigned int lun = SCSI_LUN(srb);
	int retval;
	u8 key_format;

	RTSX_DEBUGP("--%s--\n", __func__);

	rtsx_disable_aspm(chip);

	if (chip->ss_en && (rtsx_get_stat(chip) == RTSX_STAT_SS)) {
		rtsx_exit_ss(chip);
		wait_timeout(100);
	}
	rtsx_set_stat(chip, RTSX_STAT_RUN);

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
	RTSX_DEBUGP("key_format = 0x%x\n", key_format);

	switch (key_format) {
	case KF_GET_LOC_EKB:
		if ((scsi_bufflen(srb) == 0x41C) &&
			(srb->cmnd[8] == 0x04) &&
			(srb->cmnd[9] == 0x1C)) {
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
			(srb->cmnd[8] == 0x00) &&
			(srb->cmnd[9] == 0x24)) {
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
			(srb->cmnd[4] == 0x00) &&
			(srb->cmnd[5] < 32)) {
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

static int mg_send_key(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
	struct ms_info *ms_card = &(chip->ms_card);
	unsigned int lun = SCSI_LUN(srb);
	int retval;
	u8 key_format;

	RTSX_DEBUGP("--%s--\n", __func__);

	rtsx_disable_aspm(chip);

	if (chip->ss_en && (rtsx_get_stat(chip) == RTSX_STAT_SS)) {
		rtsx_exit_ss(chip);
		wait_timeout(100);
	}
	rtsx_set_stat(chip, RTSX_STAT_RUN);

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
	RTSX_DEBUGP("key_format = 0x%x\n", key_format);

	switch (key_format) {
	case KF_SET_LEAF_ID:
		if ((scsi_bufflen(srb) == 0x0C) &&
			(srb->cmnd[8] == 0x00) &&
			(srb->cmnd[9] == 0x0C)) {
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
			(srb->cmnd[8] == 0x00) &&
			(srb->cmnd[9] == 0x0C)) {
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
			(srb->cmnd[8] == 0x00) &&
			(srb->cmnd[9] == 0x0C)) {
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
			(srb->cmnd[4] == 0x00) &&
			(srb->cmnd[5] < 32)) {
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

int rtsx_scsi_handler(struct scsi_cmnd *srb, struct rtsx_chip *chip)
{
#ifdef SUPPORT_SD_LOCK
	struct sd_info *sd_card = &(chip->sd_card);
#endif
	struct ms_info *ms_card = &(chip->ms_card);
	unsigned int lun = SCSI_LUN(srb);
	int result;

#ifdef SUPPORT_SD_LOCK
	if (sd_card->sd_erase_status) {
		/* Block all SCSI command except for
		 * REQUEST_SENSE and rs_ppstatus
		 */
		if (!((srb->cmnd[0] == VENDOR_CMND) &&
				(srb->cmnd[1] == SCSI_APP_CMD) &&
				(srb->cmnd[2] == GET_DEV_STATUS)) &&
				(srb->cmnd[0] != REQUEST_SENSE)) {
			/* Logical Unit Not Ready Format in Progress */
			set_sense_data(chip, lun, CUR_ERR,
				       0x02, 0, 0x04, 0x04, 0, 0);
			TRACE_RET(chip, TRANSPORT_FAILED);
		}
	}
#endif

	if ((get_lun_card(chip, lun) == MS_CARD) &&
			(ms_card->format_status == FORMAT_IN_PROGRESS)) {
		if ((srb->cmnd[0] != REQUEST_SENSE) &&
			(srb->cmnd[0] != INQUIRY)) {
			/* Logical Unit Not Ready Format in Progress */
			set_sense_data(chip, lun, CUR_ERR, 0x02, 0, 0x04, 0x04,
					0, (u16)(ms_card->progress));
			TRACE_RET(chip, TRANSPORT_FAILED);
		}
	}

	switch (srb->cmnd[0]) {
	case READ_10:
	case WRITE_10:
	case READ_6:
	case WRITE_6:
		result = read_write(srb, chip);
#if !defined(LED_AUTO_BLINK) && !defined(REGULAR_BLINK)
		led_shine(srb, chip);
#endif
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
