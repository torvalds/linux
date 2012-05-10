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
#include "ms.h"

#ifdef SUPPORT_MAGIC_GATE

static int mg_check_int_error(struct rts51x_chip *chip)
{
	u8 value;

	rts51x_read_register(chip, MS_TRANS_CFG, &value);
	if (value & (INT_ERR | INT_CMDNK))
		TRACE_RET(chip, STATUS_FAIL);

	return STATUS_SUCCESS;
}

static int mg_send_ex_cmd(struct rts51x_chip *chip, u8 cmd, u8 entry_num)
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
		retval =
		    ms_write_bytes(chip, PRO_EX_SET_CMD, 7, WAIT_INT, data, 8);
		if (retval == STATUS_SUCCESS)
			break;
	}
	if (i == MS_MAX_RETRY_COUNT)
		TRACE_RET(chip, STATUS_FAIL);
	retval = mg_check_int_error(chip);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, STATUS_FAIL);

	return STATUS_SUCCESS;
}

int mg_set_tpc_para_sub(struct rts51x_chip *chip, int type, u8 mg_entry_num)
{
	int retval;
	u8 buf[6];

	RTS51X_DEBUGP("--%s--\n", __func__);

	if (type == 0)
		retval = ms_set_rw_reg_addr(chip, 0, 0, Pro_TPCParm, 1);
	else
		retval = ms_set_rw_reg_addr(chip, 0, 0, Pro_DataCount1, 6);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	buf[0] = 0;
	buf[1] = 0;
	if (type == 1) {
		buf[2] = 0;
		buf[3] = 0;
		buf[4] = 0;
		buf[5] = mg_entry_num;
	}
	retval =
	    ms_write_bytes(chip, PRO_WRITE_REG, (type == 0) ? 1 : 6,
			   NO_WAIT_INT, buf, 6);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	return STATUS_SUCCESS;
}

/**
  * Get MagciGate ID and set Leaf ID to medium.

  * After receiving this SCSI command, adapter shall fulfill 2 tasks
  * below in order:
  * 1. send GET_ID TPC command to get MagicGate ID and hold it till
  * Response&challenge CMD.
  * 2. send SET_ID TPC command to medium with Leaf ID released by host
  * in this SCSI CMD.
  */
int mg_set_leaf_id(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	int retval;
	int i;
	unsigned int lun = SCSI_LUN(srb);
	u8 buf1[32], buf2[12];

	RTS51X_DEBUGP("--%s--\n", __func__);

	if (scsi_bufflen(srb) < 12) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_INVALID_CMD_FIELD);
		TRACE_RET(chip, STATUS_FAIL);
	}
	ms_cleanup_work(chip);

	retval = ms_switch_clock(chip);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	retval = mg_send_ex_cmd(chip, MG_SET_LID, 0);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_KEY_FAIL_NOT_ESTAB);
		TRACE_RET(chip, retval);
	}

	memset(buf1, 0, 32);
	rts51x_get_xfer_buf(buf2, min(12, (int)scsi_bufflen(srb)), srb);
	for (i = 0; i < 8; i++)
		buf1[8 + i] = buf2[4 + i];
	retval =
	    ms_write_bytes(chip, PRO_WRITE_SHORT_DATA, 32, WAIT_INT, buf1, 32);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_KEY_FAIL_NOT_ESTAB);
		TRACE_RET(chip, retval);
	}
	retval = mg_check_int_error(chip);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_KEY_FAIL_NOT_ESTAB);
		TRACE_RET(chip, retval);
	}

	return STATUS_SUCCESS;
}

/**
  * Send Local EKB to host.

  * After receiving this SCSI command, adapter shall read the divided
  * data(1536 bytes totally) from medium by using READ_LONG_DATA TPC
  * for 3 times, and report data to host with data-length is 1052 bytes.
  */
int mg_get_local_EKB(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	int retval = STATUS_FAIL;
	int bufflen;
	unsigned int lun = SCSI_LUN(srb);
	u8 *buf = NULL;

	RTS51X_DEBUGP("--%s--\n", __func__);

	ms_cleanup_work(chip);

	retval = ms_switch_clock(chip);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	buf = kmalloc(1540, GFP_KERNEL);
	if (!buf)
		TRACE_RET(chip, STATUS_NOMEM);

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
		rts51x_write_register(chip, CARD_STOP, MS_STOP | MS_CLR_ERR,
				      MS_STOP | MS_CLR_ERR);
		TRACE_GOTO(chip, GetEKBFinish);
	}
	retval = mg_check_int_error(chip);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_KEY_FAIL_NOT_AUTHEN);
		TRACE_GOTO(chip, GetEKBFinish);
	}

	bufflen = min(1052, (int)scsi_bufflen(srb));
	rts51x_set_xfer_buf(buf, bufflen, srb);

GetEKBFinish:
	kfree(buf);
	return retval;
}

/**
  * Send challenge(host) to medium.

  * After receiving this SCSI command, adapter shall sequentially issues
  * TPC commands to the medium for writing 8-bytes data as challenge
  * by host within a short data packet.
  */
int mg_chg(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int retval;
	int bufflen;
	int i;
	unsigned int lun = SCSI_LUN(srb);
	u8 buf[32], tmp;

	RTS51X_DEBUGP("--%s--\n", __func__);

	ms_cleanup_work(chip);

	retval = ms_switch_clock(chip);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	retval = mg_send_ex_cmd(chip, MG_GET_ID, 0);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_INCOMPATIBLE_MEDIUM);
		TRACE_RET(chip, retval);
	}

	retval =
	    ms_read_bytes(chip, PRO_READ_SHORT_DATA, 32, WAIT_INT, buf, 32);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_INCOMPATIBLE_MEDIUM);
		TRACE_RET(chip, retval);
	}
	retval = mg_check_int_error(chip);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_INCOMPATIBLE_MEDIUM);
		TRACE_RET(chip, retval);
	}

	memcpy(ms_card->magic_gate_id, buf, 16);

	for (i = 0; i < 2500; i++) {
		RTS51X_READ_REG(chip, MS_TRANS_CFG, &tmp);
		if (tmp &
		    (MS_INT_CED | MS_INT_CMDNK | MS_INT_BREQ | MS_INT_ERR))
			break;

		wait_timeout(1);
	}

	if (i == 2500) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_INCOMPATIBLE_MEDIUM);
		TRACE_RET(chip, STATUS_FAIL);
	}

	retval = mg_send_ex_cmd(chip, MG_SET_RD, 0);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_INCOMPATIBLE_MEDIUM);
		TRACE_RET(chip, retval);
	}

	bufflen = min(12, (int)scsi_bufflen(srb));
	rts51x_get_xfer_buf(buf, bufflen, srb);

	for (i = 0; i < 8; i++)
		buf[i] = buf[4 + i];
	for (i = 0; i < 24; i++)
		buf[8 + i] = 0;
	retval =
	    ms_write_bytes(chip, PRO_WRITE_SHORT_DATA, 32, WAIT_INT, buf, 32);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_INCOMPATIBLE_MEDIUM);
		TRACE_RET(chip, retval);
	}
	retval = mg_check_int_error(chip);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_INCOMPATIBLE_MEDIUM);
		TRACE_RET(chip, retval);
	}

	ms_card->mg_auth = 0;

	return STATUS_SUCCESS;
}

/**
  * Send Response and Challenge data  to host.

  * After receiving this SCSI command, adapter shall communicates with
  * the medium, get parameters(HRd, Rms, MagicGateID) by using READ_SHORT_DATA
  * TPC and send the data to host according to certain format required by
  * MG-R specification.
  * The paremeter MagicGateID is the one that adapter has obtained from
  * the medium by TPC commands in Set Leaf ID command phase previously.
  */
int mg_get_rsp_chg(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int retval, i;
	int bufflen;
	unsigned int lun = SCSI_LUN(srb);
	u8 buf1[32], buf2[36], tmp;

	RTS51X_DEBUGP("--%s--\n", __func__);

	ms_cleanup_work(chip);

	retval = ms_switch_clock(chip);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	retval = mg_send_ex_cmd(chip, MG_MAKE_RMS, 0);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_KEY_FAIL_NOT_AUTHEN);
		TRACE_RET(chip, retval);
	}

	retval =
	    ms_read_bytes(chip, PRO_READ_SHORT_DATA, 32, WAIT_INT, buf1, 32);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_KEY_FAIL_NOT_AUTHEN);
		TRACE_RET(chip, retval);
	}
	retval = mg_check_int_error(chip);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_KEY_FAIL_NOT_AUTHEN);
		TRACE_RET(chip, retval);
	}

	buf2[0] = 0x00;
	buf2[1] = 0x22;
	buf2[2] = 0x00;
	buf2[3] = 0x00;

	memcpy(buf2 + 4, ms_card->magic_gate_id, 16);
	memcpy(buf2 + 20, buf1, 16);

	bufflen = min(36, (int)scsi_bufflen(srb));
	rts51x_set_xfer_buf(buf2, bufflen, srb);

	for (i = 0; i < 2500; i++) {
		RTS51X_READ_REG(chip, MS_TRANS_CFG, &tmp);
		if (tmp & (MS_INT_CED | MS_INT_CMDNK |
				MS_INT_BREQ | MS_INT_ERR))
			break;

		wait_timeout(1);
	}

	if (i == 2500) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_KEY_FAIL_NOT_AUTHEN);
		TRACE_RET(chip, STATUS_FAIL);
	}

	return STATUS_SUCCESS;
}

/**
  * Send response(host) to medium.

  * After receiving this SCSI command, adapter shall sequentially
  * issues TPC commands to the medium for writing 8-bytes data as
  * challenge by host within a short data packet.
  */
int mg_rsp(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int retval;
	int i;
	int bufflen;
	unsigned int lun = SCSI_LUN(srb);
	u8 buf[32];

	RTS51X_DEBUGP("--%s--\n", __func__);

	ms_cleanup_work(chip);

	retval = ms_switch_clock(chip);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	retval = mg_send_ex_cmd(chip, MG_MAKE_KSE, 0);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_KEY_FAIL_NOT_AUTHEN);
		TRACE_RET(chip, retval);
	}

	bufflen = min(12, (int)scsi_bufflen(srb));
	rts51x_get_xfer_buf(buf, bufflen, srb);

	for (i = 0; i < 8; i++)
		buf[i] = buf[4 + i];
	for (i = 0; i < 24; i++)
		buf[8 + i] = 0;
	retval =
	    ms_write_bytes(chip, PRO_WRITE_SHORT_DATA, 32, WAIT_INT, buf, 32);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_KEY_FAIL_NOT_AUTHEN);
		TRACE_RET(chip, retval);
	}
	retval = mg_check_int_error(chip);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MG_KEY_FAIL_NOT_AUTHEN);
		TRACE_RET(chip, retval);
	}

	ms_card->mg_auth = 1;

	return STATUS_SUCCESS;
}

/** * Send ICV data to host.

  * After receiving this SCSI command, adapter shall read the divided
  * data(1024 bytes totally) from medium by using READ_LONG_DATA TPC
  * for 2 times, and report data to host with data-length is 1028 bytes.
  *
  * Since the extra 4 bytes data is just only a prefix to original data
  * that read from medium, so that the 4-byte data pushed into Ring buffer
  * precedes data tramsinssion from medium to Ring buffer by DMA mechanisim
  * in order to get maximum performance and minimum code size simultaneously.
  */
int mg_get_ICV(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int retval;
	int bufflen;
	unsigned int lun = SCSI_LUN(srb);
	u8 *buf = NULL;

	RTS51X_DEBUGP("--%s--\n", __func__);

	ms_cleanup_work(chip);

	retval = ms_switch_clock(chip);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	buf = kmalloc(1028, GFP_KERNEL);
	if (!buf)
		TRACE_RET(chip, STATUS_NOMEM);

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
		rts51x_write_register(chip, CARD_STOP, MS_STOP | MS_CLR_ERR,
				      MS_STOP | MS_CLR_ERR);
		TRACE_GOTO(chip, GetICVFinish);
	}
	retval = mg_check_int_error(chip);
	if (retval != STATUS_SUCCESS) {
		set_sense_type(chip, lun, SENSE_TYPE_MEDIA_UNRECOVER_READ_ERR);
		TRACE_GOTO(chip, GetICVFinish);
	}

	bufflen = min(1028, (int)scsi_bufflen(srb));
	rts51x_set_xfer_buf(buf, bufflen, srb);

GetICVFinish:
	kfree(buf);
	return retval;
}

/**
  * Send ICV data to medium.

  * After receiving this SCSI command, adapter shall receive 1028 bytes
  * and write the later 1024 bytes to medium by WRITE_LONG_DATA TPC
  * consecutively.
  *
  * Since the first 4-bytes data is just only a prefix to original data
  * that sent by host, and it should be skipped by shifting DMA pointer
  * before writing 1024 bytes to medium.
  */
int mg_set_ICV(struct scsi_cmnd *srb, struct rts51x_chip *chip)
{
	struct ms_info *ms_card = &(chip->ms_card);
	int retval;
	int bufflen;
#ifdef MG_SET_ICV_SLOW
	int i;
#endif
	unsigned int lun = SCSI_LUN(srb);
	u8 *buf = NULL;

	RTS51X_DEBUGP("--%s--\n", __func__);

	ms_cleanup_work(chip);

	retval = ms_switch_clock(chip);
	if (retval != STATUS_SUCCESS)
		TRACE_RET(chip, retval);

	buf = kmalloc(1028, GFP_KERNEL);
	if (!buf)
		TRACE_RET(chip, STATUS_NOMEM);

	bufflen = min(1028, (int)scsi_bufflen(srb));
	rts51x_get_xfer_buf(buf, bufflen, srb);

	retval = mg_send_ex_cmd(chip, MG_SET_IBD, ms_card->mg_entry_num);
	if (retval != STATUS_SUCCESS) {
		if (ms_card->mg_auth == 0) {
			if ((buf[5] & 0xC0) != 0)
				set_sense_type(chip, lun,
					SENSE_TYPE_MG_KEY_FAIL_NOT_ESTAB);
			else
				set_sense_type(chip, lun,
					SENSE_TYPE_MG_WRITE_ERR);
		} else {
			set_sense_type(chip, lun, SENSE_TYPE_MG_WRITE_ERR);
		}
		TRACE_GOTO(chip, SetICVFinish);
	}

#ifdef MG_SET_ICV_SLOW
	for (i = 0; i < 2; i++) {
		udelay(50);

		rts51x_init_cmd(chip);

		rts51x_add_cmd(chip, WRITE_REG_CMD, MS_TPC, 0xFF,
			       PRO_WRITE_LONG_DATA);
		rts51x_add_cmd(chip, WRITE_REG_CMD, MS_TRANS_CFG, 0xFF,
			       WAIT_INT);

		trans_dma_enable(DMA_TO_DEVICE, chip, 512, DMA_512);

		rts51x_add_cmd(chip, WRITE_REG_CMD, MS_TRANSFER, 0xFF,
			       MS_TRANSFER_START | MS_TM_NORMAL_WRITE);
		rts51x_add_cmd(chip, CHECK_REG_CMD, MS_TRANSFER,
			       MS_TRANSFER_END, MS_TRANSFER_END);

		retval = rts51x_send_cmd(chip, MODE_CDOR, 100);
		if (retval != STATUS_SUCCESS) {
			set_sense_type(chip, lun, SENSE_TYPE_MG_WRITE_ERR);
			TRACE_GOTO(chip, SetICVFinish);
		}

		retval = rts51x_transfer_data_rcc(chip, SND_BULK_PIPE(chip),
						  buf + 4 + i * 512, 512, 0,
						  NULL, 3000, STAGE_DO);
		if (retval != STATUS_SUCCESS) {
			rts51x_clear_ms_error(chip);
			if (ms_card->mg_auth == 0) {
				if ((buf[5] & 0xC0) != 0)
					set_sense_type(chip, lun,
						SENSE_TYPE_MG_KEY_FAIL_NOT_ESTAB);
				else
					set_sense_type(chip, lun,
						SENSE_TYPE_MG_WRITE_ERR);
			} else {
				set_sense_type(chip, lun,
					       SENSE_TYPE_MG_WRITE_ERR);
			}
			retval = STATUS_FAIL;
			TRACE_GOTO(chip, SetICVFinish);
		}

		retval = rts51x_get_rsp(chip, 1, 3000);
		if (CHECK_MS_TRANS_FAIL(chip, retval)
		    || mg_check_int_error(chip)) {
			rts51x_clear_ms_error(chip);
			if (ms_card->mg_auth == 0) {
				if ((buf[5] & 0xC0) != 0)
					set_sense_type(chip, lun,
						SENSE_TYPE_MG_KEY_FAIL_NOT_ESTAB);
				else
					set_sense_type(chip, lun,
						SENSE_TYPE_MG_WRITE_ERR);
			} else {
				set_sense_type(chip, lun,
					       SENSE_TYPE_MG_WRITE_ERR);
			}
			retval = STATUS_FAIL;
			TRACE_GOTO(chip, SetICVFinish);
		}
	}
#else
	retval = ms_transfer_data(chip, MS_TM_AUTO_WRITE, PRO_WRITE_LONG_DATA,
				  2, WAIT_INT, 0, 0, buf + 4, 1024);
	if (retval != STATUS_SUCCESS) {
		rts51x_clear_ms_error(chip);
		if (ms_card->mg_auth == 0) {
			if ((buf[5] & 0xC0) != 0)
				set_sense_type(chip, lun,
					SENSE_TYPE_MG_KEY_FAIL_NOT_ESTAB);
			else
				set_sense_type(chip, lun,
					SENSE_TYPE_MG_WRITE_ERR);
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
