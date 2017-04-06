/*
 * NVM Express device driver
 * Copyright (c) 2011-2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

/*
 * Refer to the SCSI-NVMe Translation spec for details on how
 * each command is translated.
 */

#include <linux/bio.h>
#include <linux/bitops.h>
#include <linux/blkdev.h>
#include <linux/compat.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/idr.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kdev_t.h>
#include <linux/kthread.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/poison.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <asm/unaligned.h>
#include <scsi/sg.h>
#include <scsi/scsi.h>
#include <scsi/scsi_request.h>

#include "nvme.h"

static int sg_version_num = 30534;	/* 2 digits for each component */

/* VPD Page Codes */
#define VPD_SUPPORTED_PAGES				0x00
#define VPD_SERIAL_NUMBER				0x80
#define VPD_DEVICE_IDENTIFIERS				0x83
#define VPD_EXTENDED_INQUIRY				0x86
#define VPD_BLOCK_LIMITS				0xB0
#define VPD_BLOCK_DEV_CHARACTERISTICS			0xB1

/* format unit paramter list offsets */
#define FORMAT_UNIT_SHORT_PARM_LIST_LEN			4
#define FORMAT_UNIT_LONG_PARM_LIST_LEN			8
#define FORMAT_UNIT_PROT_INT_OFFSET			3
#define FORMAT_UNIT_PROT_FIELD_USAGE_OFFSET		0
#define FORMAT_UNIT_PROT_FIELD_USAGE_MASK		0x07

/* Misc. defines */
#define FIXED_SENSE_DATA				0x70
#define DESC_FORMAT_SENSE_DATA				0x72
#define FIXED_SENSE_DATA_ADD_LENGTH			10
#define LUN_ENTRY_SIZE					8
#define LUN_DATA_HEADER_SIZE				8
#define ALL_LUNS_RETURNED				0x02
#define ALL_WELL_KNOWN_LUNS_RETURNED			0x01
#define RESTRICTED_LUNS_RETURNED			0x00
#define DOWNLOAD_SAVE_ACTIVATE				0x05
#define DOWNLOAD_SAVE_DEFER_ACTIVATE			0x0E
#define ACTIVATE_DEFERRED_MICROCODE			0x0F
#define FORMAT_UNIT_IMMED_MASK				0x2
#define FORMAT_UNIT_IMMED_OFFSET			1
#define KELVIN_TEMP_FACTOR				273
#define FIXED_FMT_SENSE_DATA_SIZE			18
#define DESC_FMT_SENSE_DATA_SIZE			8

/* SCSI/NVMe defines and bit masks */
#define INQ_STANDARD_INQUIRY_PAGE			0x00
#define INQ_SUPPORTED_VPD_PAGES_PAGE			0x00
#define INQ_UNIT_SERIAL_NUMBER_PAGE			0x80
#define INQ_DEVICE_IDENTIFICATION_PAGE			0x83
#define INQ_EXTENDED_INQUIRY_DATA_PAGE			0x86
#define INQ_BDEV_LIMITS_PAGE				0xB0
#define INQ_BDEV_CHARACTERISTICS_PAGE			0xB1
#define INQ_SERIAL_NUMBER_LENGTH			0x14
#define INQ_NUM_SUPPORTED_VPD_PAGES			6
#define VERSION_SPC_4					0x06
#define ACA_UNSUPPORTED					0
#define STANDARD_INQUIRY_LENGTH				36
#define ADDITIONAL_STD_INQ_LENGTH			31
#define EXTENDED_INQUIRY_DATA_PAGE_LENGTH		0x3C
#define RESERVED_FIELD					0

/* Mode Sense/Select defines */
#define MODE_PAGE_INFO_EXCEP				0x1C
#define MODE_PAGE_CACHING				0x08
#define MODE_PAGE_CONTROL				0x0A
#define MODE_PAGE_POWER_CONDITION			0x1A
#define MODE_PAGE_RETURN_ALL				0x3F
#define MODE_PAGE_BLK_DES_LEN				0x08
#define MODE_PAGE_LLBAA_BLK_DES_LEN			0x10
#define MODE_PAGE_CACHING_LEN				0x14
#define MODE_PAGE_CONTROL_LEN				0x0C
#define MODE_PAGE_POW_CND_LEN				0x28
#define MODE_PAGE_INF_EXC_LEN				0x0C
#define MODE_PAGE_ALL_LEN				0x54
#define MODE_SENSE6_MPH_SIZE				4
#define MODE_SENSE_PAGE_CONTROL_MASK			0xC0
#define MODE_SENSE_PAGE_CODE_OFFSET			2
#define MODE_SENSE_PAGE_CODE_MASK			0x3F
#define MODE_SENSE_LLBAA_MASK				0x10
#define MODE_SENSE_LLBAA_SHIFT				4
#define MODE_SENSE_DBD_MASK				8
#define MODE_SENSE_DBD_SHIFT				3
#define MODE_SENSE10_MPH_SIZE				8
#define MODE_SELECT_CDB_PAGE_FORMAT_MASK		0x10
#define MODE_SELECT_CDB_SAVE_PAGES_MASK			0x1
#define MODE_SELECT_6_BD_OFFSET				3
#define MODE_SELECT_10_BD_OFFSET			6
#define MODE_SELECT_10_LLBAA_OFFSET			4
#define MODE_SELECT_10_LLBAA_MASK			1
#define MODE_SELECT_6_MPH_SIZE				4
#define MODE_SELECT_10_MPH_SIZE				8
#define CACHING_MODE_PAGE_WCE_MASK			0x04
#define MODE_SENSE_BLK_DESC_ENABLED			0
#define MODE_SENSE_BLK_DESC_COUNT			1
#define MODE_SELECT_PAGE_CODE_MASK			0x3F
#define SHORT_DESC_BLOCK				8
#define LONG_DESC_BLOCK					16
#define MODE_PAGE_POW_CND_LEN_FIELD			0x26
#define MODE_PAGE_INF_EXC_LEN_FIELD			0x0A
#define MODE_PAGE_CACHING_LEN_FIELD			0x12
#define MODE_PAGE_CONTROL_LEN_FIELD			0x0A
#define MODE_SENSE_PC_CURRENT_VALUES			0

/* Log Sense defines */
#define LOG_PAGE_SUPPORTED_LOG_PAGES_PAGE		0x00
#define LOG_PAGE_SUPPORTED_LOG_PAGES_LENGTH		0x07
#define LOG_PAGE_INFORMATIONAL_EXCEPTIONS_PAGE		0x2F
#define LOG_PAGE_TEMPERATURE_PAGE			0x0D
#define LOG_SENSE_CDB_SP_NOT_ENABLED			0
#define LOG_SENSE_CDB_PC_MASK				0xC0
#define LOG_SENSE_CDB_PC_SHIFT				6
#define LOG_SENSE_CDB_PC_CUMULATIVE_VALUES		1
#define LOG_SENSE_CDB_PAGE_CODE_MASK			0x3F
#define REMAINING_INFO_EXCP_PAGE_LENGTH			0x8
#define LOG_INFO_EXCP_PAGE_LENGTH			0xC
#define REMAINING_TEMP_PAGE_LENGTH			0xC
#define LOG_TEMP_PAGE_LENGTH				0x10
#define LOG_TEMP_UNKNOWN				0xFF
#define SUPPORTED_LOG_PAGES_PAGE_LENGTH			0x3

/* Read Capacity defines */
#define READ_CAP_10_RESP_SIZE				8
#define READ_CAP_16_RESP_SIZE				32

/* NVMe Namespace and Command Defines */
#define BYTES_TO_DWORDS					4
#define NVME_MAX_FIRMWARE_SLOT				7

/* Report LUNs defines */
#define REPORT_LUNS_FIRST_LUN_OFFSET			8

/* SCSI ADDITIONAL SENSE Codes */

#define SCSI_ASC_NO_SENSE				0x00
#define SCSI_ASC_PERIPHERAL_DEV_WRITE_FAULT		0x03
#define SCSI_ASC_LUN_NOT_READY				0x04
#define SCSI_ASC_WARNING				0x0B
#define SCSI_ASC_LOG_BLOCK_GUARD_CHECK_FAILED		0x10
#define SCSI_ASC_LOG_BLOCK_APPTAG_CHECK_FAILED		0x10
#define SCSI_ASC_LOG_BLOCK_REFTAG_CHECK_FAILED		0x10
#define SCSI_ASC_UNRECOVERED_READ_ERROR			0x11
#define SCSI_ASC_MISCOMPARE_DURING_VERIFY		0x1D
#define SCSI_ASC_ACCESS_DENIED_INVALID_LUN_ID		0x20
#define SCSI_ASC_ILLEGAL_COMMAND			0x20
#define SCSI_ASC_ILLEGAL_BLOCK				0x21
#define SCSI_ASC_INVALID_CDB				0x24
#define SCSI_ASC_INVALID_LUN				0x25
#define SCSI_ASC_INVALID_PARAMETER			0x26
#define SCSI_ASC_FORMAT_COMMAND_FAILED			0x31
#define SCSI_ASC_INTERNAL_TARGET_FAILURE		0x44

/* SCSI ADDITIONAL SENSE Code Qualifiers */

#define SCSI_ASCQ_CAUSE_NOT_REPORTABLE			0x00
#define SCSI_ASCQ_FORMAT_COMMAND_FAILED			0x01
#define SCSI_ASCQ_LOG_BLOCK_GUARD_CHECK_FAILED		0x01
#define SCSI_ASCQ_LOG_BLOCK_APPTAG_CHECK_FAILED		0x02
#define SCSI_ASCQ_LOG_BLOCK_REFTAG_CHECK_FAILED		0x03
#define SCSI_ASCQ_FORMAT_IN_PROGRESS			0x04
#define SCSI_ASCQ_POWER_LOSS_EXPECTED			0x08
#define SCSI_ASCQ_INVALID_LUN_ID			0x09

/* copied from drivers/usb/gadget/function/storage_common.h */
static inline u32 get_unaligned_be24(u8 *buf)
{
	return 0xffffff & (u32) get_unaligned_be32(buf - 1);
}

/* Struct to gather data that needs to be extracted from a SCSI CDB.
   Not conforming to any particular CDB variant, but compatible with all. */

struct nvme_trans_io_cdb {
	u8 fua;
	u8 prot_info;
	u64 lba;
	u32 xfer_len;
};


/* Internal Helper Functions */


/* Copy data to userspace memory */

static int nvme_trans_copy_to_user(struct sg_io_hdr *hdr, void *from,
								unsigned long n)
{
	int i;
	void *index = from;
	size_t remaining = n;
	size_t xfer_len;

	if (hdr->iovec_count > 0) {
		struct sg_iovec sgl;

		for (i = 0; i < hdr->iovec_count; i++) {
			if (copy_from_user(&sgl, hdr->dxferp +
						i * sizeof(struct sg_iovec),
						sizeof(struct sg_iovec)))
				return -EFAULT;
			xfer_len = min(remaining, sgl.iov_len);
			if (copy_to_user(sgl.iov_base, index, xfer_len))
				return -EFAULT;

			index += xfer_len;
			remaining -= xfer_len;
			if (remaining == 0)
				break;
		}
		return 0;
	}

	if (copy_to_user(hdr->dxferp, from, n))
		return -EFAULT;
	return 0;
}

/* Copy data from userspace memory */

static int nvme_trans_copy_from_user(struct sg_io_hdr *hdr, void *to,
								unsigned long n)
{
	int i;
	void *index = to;
	size_t remaining = n;
	size_t xfer_len;

	if (hdr->iovec_count > 0) {
		struct sg_iovec sgl;

		for (i = 0; i < hdr->iovec_count; i++) {
			if (copy_from_user(&sgl, hdr->dxferp +
						i * sizeof(struct sg_iovec),
						sizeof(struct sg_iovec)))
				return -EFAULT;
			xfer_len = min(remaining, sgl.iov_len);
			if (copy_from_user(index, sgl.iov_base, xfer_len))
				return -EFAULT;
			index += xfer_len;
			remaining -= xfer_len;
			if (remaining == 0)
				break;
		}
		return 0;
	}

	if (copy_from_user(to, hdr->dxferp, n))
		return -EFAULT;
	return 0;
}

/* Status/Sense Buffer Writeback */

static int nvme_trans_completion(struct sg_io_hdr *hdr, u8 status, u8 sense_key,
				 u8 asc, u8 ascq)
{
	u8 xfer_len;
	u8 resp[DESC_FMT_SENSE_DATA_SIZE];

	if (scsi_status_is_good(status)) {
		hdr->status = SAM_STAT_GOOD;
		hdr->masked_status = GOOD;
		hdr->host_status = DID_OK;
		hdr->driver_status = DRIVER_OK;
		hdr->sb_len_wr = 0;
	} else {
		hdr->status = status;
		hdr->masked_status = status >> 1;
		hdr->host_status = DID_OK;
		hdr->driver_status = DRIVER_OK;

		memset(resp, 0, DESC_FMT_SENSE_DATA_SIZE);
		resp[0] = DESC_FORMAT_SENSE_DATA;
		resp[1] = sense_key;
		resp[2] = asc;
		resp[3] = ascq;

		xfer_len = min_t(u8, hdr->mx_sb_len, DESC_FMT_SENSE_DATA_SIZE);
		hdr->sb_len_wr = xfer_len;
		if (copy_to_user(hdr->sbp, resp, xfer_len) > 0)
			return -EFAULT;
	}

	return 0;
}

/*
 * Take a status code from a lowlevel routine, and if it was a positive NVMe
 * error code update the sense data based on it.  In either case the passed
 * in value is returned again, unless an -EFAULT from copy_to_user overrides
 * it.
 */
static int nvme_trans_status_code(struct sg_io_hdr *hdr, int nvme_sc)
{
	u8 status, sense_key, asc, ascq;
	int res;

	/* For non-nvme (Linux) errors, simply return the error code */
	if (nvme_sc < 0)
		return nvme_sc;

	/* Mask DNR, More, and reserved fields */
	switch (nvme_sc & 0x7FF) {
	/* Generic Command Status */
	case NVME_SC_SUCCESS:
		status = SAM_STAT_GOOD;
		sense_key = NO_SENSE;
		asc = SCSI_ASC_NO_SENSE;
		ascq = SCSI_ASCQ_CAUSE_NOT_REPORTABLE;
		break;
	case NVME_SC_INVALID_OPCODE:
		status = SAM_STAT_CHECK_CONDITION;
		sense_key = ILLEGAL_REQUEST;
		asc = SCSI_ASC_ILLEGAL_COMMAND;
		ascq = SCSI_ASCQ_CAUSE_NOT_REPORTABLE;
		break;
	case NVME_SC_INVALID_FIELD:
		status = SAM_STAT_CHECK_CONDITION;
		sense_key = ILLEGAL_REQUEST;
		asc = SCSI_ASC_INVALID_CDB;
		ascq = SCSI_ASCQ_CAUSE_NOT_REPORTABLE;
		break;
	case NVME_SC_DATA_XFER_ERROR:
		status = SAM_STAT_CHECK_CONDITION;
		sense_key = MEDIUM_ERROR;
		asc = SCSI_ASC_NO_SENSE;
		ascq = SCSI_ASCQ_CAUSE_NOT_REPORTABLE;
		break;
	case NVME_SC_POWER_LOSS:
		status = SAM_STAT_TASK_ABORTED;
		sense_key = ABORTED_COMMAND;
		asc = SCSI_ASC_WARNING;
		ascq = SCSI_ASCQ_POWER_LOSS_EXPECTED;
		break;
	case NVME_SC_INTERNAL:
		status = SAM_STAT_CHECK_CONDITION;
		sense_key = HARDWARE_ERROR;
		asc = SCSI_ASC_INTERNAL_TARGET_FAILURE;
		ascq = SCSI_ASCQ_CAUSE_NOT_REPORTABLE;
		break;
	case NVME_SC_ABORT_REQ:
		status = SAM_STAT_TASK_ABORTED;
		sense_key = ABORTED_COMMAND;
		asc = SCSI_ASC_NO_SENSE;
		ascq = SCSI_ASCQ_CAUSE_NOT_REPORTABLE;
		break;
	case NVME_SC_ABORT_QUEUE:
		status = SAM_STAT_TASK_ABORTED;
		sense_key = ABORTED_COMMAND;
		asc = SCSI_ASC_NO_SENSE;
		ascq = SCSI_ASCQ_CAUSE_NOT_REPORTABLE;
		break;
	case NVME_SC_FUSED_FAIL:
		status = SAM_STAT_TASK_ABORTED;
		sense_key = ABORTED_COMMAND;
		asc = SCSI_ASC_NO_SENSE;
		ascq = SCSI_ASCQ_CAUSE_NOT_REPORTABLE;
		break;
	case NVME_SC_FUSED_MISSING:
		status = SAM_STAT_TASK_ABORTED;
		sense_key = ABORTED_COMMAND;
		asc = SCSI_ASC_NO_SENSE;
		ascq = SCSI_ASCQ_CAUSE_NOT_REPORTABLE;
		break;
	case NVME_SC_INVALID_NS:
		status = SAM_STAT_CHECK_CONDITION;
		sense_key = ILLEGAL_REQUEST;
		asc = SCSI_ASC_ACCESS_DENIED_INVALID_LUN_ID;
		ascq = SCSI_ASCQ_INVALID_LUN_ID;
		break;
	case NVME_SC_LBA_RANGE:
		status = SAM_STAT_CHECK_CONDITION;
		sense_key = ILLEGAL_REQUEST;
		asc = SCSI_ASC_ILLEGAL_BLOCK;
		ascq = SCSI_ASCQ_CAUSE_NOT_REPORTABLE;
		break;
	case NVME_SC_CAP_EXCEEDED:
		status = SAM_STAT_CHECK_CONDITION;
		sense_key = MEDIUM_ERROR;
		asc = SCSI_ASC_NO_SENSE;
		ascq = SCSI_ASCQ_CAUSE_NOT_REPORTABLE;
		break;
	case NVME_SC_NS_NOT_READY:
		status = SAM_STAT_CHECK_CONDITION;
		sense_key = NOT_READY;
		asc = SCSI_ASC_LUN_NOT_READY;
		ascq = SCSI_ASCQ_CAUSE_NOT_REPORTABLE;
		break;

	/* Command Specific Status */
	case NVME_SC_INVALID_FORMAT:
		status = SAM_STAT_CHECK_CONDITION;
		sense_key = ILLEGAL_REQUEST;
		asc = SCSI_ASC_FORMAT_COMMAND_FAILED;
		ascq = SCSI_ASCQ_FORMAT_COMMAND_FAILED;
		break;
	case NVME_SC_BAD_ATTRIBUTES:
		status = SAM_STAT_CHECK_CONDITION;
		sense_key = ILLEGAL_REQUEST;
		asc = SCSI_ASC_INVALID_CDB;
		ascq = SCSI_ASCQ_CAUSE_NOT_REPORTABLE;
		break;

	/* Media Errors */
	case NVME_SC_WRITE_FAULT:
		status = SAM_STAT_CHECK_CONDITION;
		sense_key = MEDIUM_ERROR;
		asc = SCSI_ASC_PERIPHERAL_DEV_WRITE_FAULT;
		ascq = SCSI_ASCQ_CAUSE_NOT_REPORTABLE;
		break;
	case NVME_SC_READ_ERROR:
		status = SAM_STAT_CHECK_CONDITION;
		sense_key = MEDIUM_ERROR;
		asc = SCSI_ASC_UNRECOVERED_READ_ERROR;
		ascq = SCSI_ASCQ_CAUSE_NOT_REPORTABLE;
		break;
	case NVME_SC_GUARD_CHECK:
		status = SAM_STAT_CHECK_CONDITION;
		sense_key = MEDIUM_ERROR;
		asc = SCSI_ASC_LOG_BLOCK_GUARD_CHECK_FAILED;
		ascq = SCSI_ASCQ_LOG_BLOCK_GUARD_CHECK_FAILED;
		break;
	case NVME_SC_APPTAG_CHECK:
		status = SAM_STAT_CHECK_CONDITION;
		sense_key = MEDIUM_ERROR;
		asc = SCSI_ASC_LOG_BLOCK_APPTAG_CHECK_FAILED;
		ascq = SCSI_ASCQ_LOG_BLOCK_APPTAG_CHECK_FAILED;
		break;
	case NVME_SC_REFTAG_CHECK:
		status = SAM_STAT_CHECK_CONDITION;
		sense_key = MEDIUM_ERROR;
		asc = SCSI_ASC_LOG_BLOCK_REFTAG_CHECK_FAILED;
		ascq = SCSI_ASCQ_LOG_BLOCK_REFTAG_CHECK_FAILED;
		break;
	case NVME_SC_COMPARE_FAILED:
		status = SAM_STAT_CHECK_CONDITION;
		sense_key = MISCOMPARE;
		asc = SCSI_ASC_MISCOMPARE_DURING_VERIFY;
		ascq = SCSI_ASCQ_CAUSE_NOT_REPORTABLE;
		break;
	case NVME_SC_ACCESS_DENIED:
		status = SAM_STAT_CHECK_CONDITION;
		sense_key = ILLEGAL_REQUEST;
		asc = SCSI_ASC_ACCESS_DENIED_INVALID_LUN_ID;
		ascq = SCSI_ASCQ_INVALID_LUN_ID;
		break;

	/* Unspecified/Default */
	case NVME_SC_CMDID_CONFLICT:
	case NVME_SC_CMD_SEQ_ERROR:
	case NVME_SC_CQ_INVALID:
	case NVME_SC_QID_INVALID:
	case NVME_SC_QUEUE_SIZE:
	case NVME_SC_ABORT_LIMIT:
	case NVME_SC_ABORT_MISSING:
	case NVME_SC_ASYNC_LIMIT:
	case NVME_SC_FIRMWARE_SLOT:
	case NVME_SC_FIRMWARE_IMAGE:
	case NVME_SC_INVALID_VECTOR:
	case NVME_SC_INVALID_LOG_PAGE:
	default:
		status = SAM_STAT_CHECK_CONDITION;
		sense_key = ILLEGAL_REQUEST;
		asc = SCSI_ASC_NO_SENSE;
		ascq = SCSI_ASCQ_CAUSE_NOT_REPORTABLE;
		break;
	}

	res = nvme_trans_completion(hdr, status, sense_key, asc, ascq);
	return res ? res : nvme_sc;
}

/* INQUIRY Helper Functions */

static int nvme_trans_standard_inquiry_page(struct nvme_ns *ns,
					struct sg_io_hdr *hdr, u8 *inq_response,
					int alloc_len)
{
	struct nvme_ctrl *ctrl = ns->ctrl;
	struct nvme_id_ns *id_ns;
	int res;
	int nvme_sc;
	int xfer_len;
	u8 resp_data_format = 0x02;
	u8 protect;
	u8 cmdque = 0x01 << 1;
	u8 fw_offset = sizeof(ctrl->firmware_rev);

	/* nvme ns identify - use DPS value for PROTECT field */
	nvme_sc = nvme_identify_ns(ctrl, ns->ns_id, &id_ns);
	res = nvme_trans_status_code(hdr, nvme_sc);
	if (res)
		return res;

	if (id_ns->dps)
		protect = 0x01;
	else
		protect = 0;
	kfree(id_ns);

	memset(inq_response, 0, STANDARD_INQUIRY_LENGTH);
	inq_response[2] = VERSION_SPC_4;
	inq_response[3] = resp_data_format;	/*normaca=0 | hisup=0 */
	inq_response[4] = ADDITIONAL_STD_INQ_LENGTH;
	inq_response[5] = protect;	/* sccs=0 | acc=0 | tpgs=0 | pc3=0 */
	inq_response[7] = cmdque;	/* wbus16=0 | sync=0 | vs=0 */
	strncpy(&inq_response[8], "NVMe    ", 8);
	strncpy(&inq_response[16], ctrl->model, 16);

	while (ctrl->firmware_rev[fw_offset - 1] == ' ' && fw_offset > 4)
		fw_offset--;
	fw_offset -= 4;
	strncpy(&inq_response[32], ctrl->firmware_rev + fw_offset, 4);

	xfer_len = min(alloc_len, STANDARD_INQUIRY_LENGTH);
	return nvme_trans_copy_to_user(hdr, inq_response, xfer_len);
}

static int nvme_trans_supported_vpd_pages(struct nvme_ns *ns,
					struct sg_io_hdr *hdr, u8 *inq_response,
					int alloc_len)
{
	int xfer_len;

	memset(inq_response, 0, STANDARD_INQUIRY_LENGTH);
	inq_response[1] = INQ_SUPPORTED_VPD_PAGES_PAGE;   /* Page Code */
	inq_response[3] = INQ_NUM_SUPPORTED_VPD_PAGES;    /* Page Length */
	inq_response[4] = INQ_SUPPORTED_VPD_PAGES_PAGE;
	inq_response[5] = INQ_UNIT_SERIAL_NUMBER_PAGE;
	inq_response[6] = INQ_DEVICE_IDENTIFICATION_PAGE;
	inq_response[7] = INQ_EXTENDED_INQUIRY_DATA_PAGE;
	inq_response[8] = INQ_BDEV_CHARACTERISTICS_PAGE;
	inq_response[9] = INQ_BDEV_LIMITS_PAGE;

	xfer_len = min(alloc_len, STANDARD_INQUIRY_LENGTH);
	return nvme_trans_copy_to_user(hdr, inq_response, xfer_len);
}

static int nvme_trans_unit_serial_page(struct nvme_ns *ns,
					struct sg_io_hdr *hdr, u8 *inq_response,
					int alloc_len)
{
	int xfer_len;

	memset(inq_response, 0, STANDARD_INQUIRY_LENGTH);
	inq_response[1] = INQ_UNIT_SERIAL_NUMBER_PAGE; /* Page Code */
	inq_response[3] = INQ_SERIAL_NUMBER_LENGTH;    /* Page Length */
	strncpy(&inq_response[4], ns->ctrl->serial, INQ_SERIAL_NUMBER_LENGTH);

	xfer_len = min(alloc_len, STANDARD_INQUIRY_LENGTH);
	return nvme_trans_copy_to_user(hdr, inq_response, xfer_len);
}

static int nvme_fill_device_id_eui64(struct nvme_ns *ns, struct sg_io_hdr *hdr,
		u8 *inq_response, int alloc_len)
{
	struct nvme_id_ns *id_ns;
	int nvme_sc, res;
	size_t len;
	void *eui;

	nvme_sc = nvme_identify_ns(ns->ctrl, ns->ns_id, &id_ns);
	res = nvme_trans_status_code(hdr, nvme_sc);
	if (res)
		return res;

	eui = id_ns->eui64;
	len = sizeof(id_ns->eui64);

	if (ns->ctrl->vs >= NVME_VS(1, 2, 0)) {
		if (bitmap_empty(eui, len * 8)) {
			eui = id_ns->nguid;
			len = sizeof(id_ns->nguid);
		}
	}

	if (bitmap_empty(eui, len * 8)) {
		res = -EOPNOTSUPP;
		goto out_free_id;
	}

	memset(inq_response, 0, alloc_len);
	inq_response[1] = INQ_DEVICE_IDENTIFICATION_PAGE;
	inq_response[3] = 4 + len; /* Page Length */

	/* Designation Descriptor start */
	inq_response[4] = 0x01;	/* Proto ID=0h | Code set=1h */
	inq_response[5] = 0x02;	/* PIV=0b | Asso=00b | Designator Type=2h */
	inq_response[6] = 0x00;	/* Rsvd */
	inq_response[7] = len;	/* Designator Length */
	memcpy(&inq_response[8], eui, len);

	res = nvme_trans_copy_to_user(hdr, inq_response, alloc_len);
out_free_id:
	kfree(id_ns);
	return res;
}

static int nvme_fill_device_id_scsi_string(struct nvme_ns *ns,
		struct sg_io_hdr *hdr, u8 *inq_response, int alloc_len)
{
	struct nvme_ctrl *ctrl = ns->ctrl;
	struct nvme_id_ctrl *id_ctrl;
	int nvme_sc, res;

	if (alloc_len < 72) {
		return nvme_trans_completion(hdr,
				SAM_STAT_CHECK_CONDITION,
				ILLEGAL_REQUEST, SCSI_ASC_INVALID_CDB,
				SCSI_ASCQ_CAUSE_NOT_REPORTABLE);
	}

	nvme_sc = nvme_identify_ctrl(ctrl, &id_ctrl);
	res = nvme_trans_status_code(hdr, nvme_sc);
	if (res)
		return res;

	memset(inq_response, 0, alloc_len);
	inq_response[1] = INQ_DEVICE_IDENTIFICATION_PAGE;
	inq_response[3] = 0x48;	/* Page Length */

	/* Designation Descriptor start */
	inq_response[4] = 0x03;	/* Proto ID=0h | Code set=3h */
	inq_response[5] = 0x08;	/* PIV=0b | Asso=00b | Designator Type=8h */
	inq_response[6] = 0x00;	/* Rsvd */
	inq_response[7] = 0x44;	/* Designator Length */

	sprintf(&inq_response[8], "%04x", le16_to_cpu(id_ctrl->vid));
	memcpy(&inq_response[12], ctrl->model, sizeof(ctrl->model));
	sprintf(&inq_response[52], "%04x", cpu_to_be32(ns->ns_id));
	memcpy(&inq_response[56], ctrl->serial, sizeof(ctrl->serial));

	res = nvme_trans_copy_to_user(hdr, inq_response, alloc_len);
	kfree(id_ctrl);
	return res;
}

static int nvme_trans_device_id_page(struct nvme_ns *ns, struct sg_io_hdr *hdr,
					u8 *resp, int alloc_len)
{
	int res;

	if (ns->ctrl->vs >= NVME_VS(1, 1, 0)) {
		res = nvme_fill_device_id_eui64(ns, hdr, resp, alloc_len);
		if (res != -EOPNOTSUPP)
			return res;
	}

	return nvme_fill_device_id_scsi_string(ns, hdr, resp, alloc_len);
}

static int nvme_trans_ext_inq_page(struct nvme_ns *ns, struct sg_io_hdr *hdr,
					int alloc_len)
{
	u8 *inq_response;
	int res;
	int nvme_sc;
	struct nvme_ctrl *ctrl = ns->ctrl;
	struct nvme_id_ctrl *id_ctrl;
	struct nvme_id_ns *id_ns;
	int xfer_len;
	u8 microcode = 0x80;
	u8 spt;
	u8 spt_lut[8] = {0, 0, 2, 1, 4, 6, 5, 7};
	u8 grd_chk, app_chk, ref_chk, protect;
	u8 uask_sup = 0x20;
	u8 v_sup;
	u8 luiclr = 0x01;

	inq_response = kmalloc(EXTENDED_INQUIRY_DATA_PAGE_LENGTH, GFP_KERNEL);
	if (inq_response == NULL)
		return -ENOMEM;

	nvme_sc = nvme_identify_ns(ctrl, ns->ns_id, &id_ns);
	res = nvme_trans_status_code(hdr, nvme_sc);
	if (res)
		goto out_free_inq;

	spt = spt_lut[id_ns->dpc & 0x07] << 3;
	if (id_ns->dps)
		protect = 0x01;
	else
		protect = 0;
	kfree(id_ns);

	grd_chk = protect << 2;
	app_chk = protect << 1;
	ref_chk = protect;

	nvme_sc = nvme_identify_ctrl(ctrl, &id_ctrl);
	res = nvme_trans_status_code(hdr, nvme_sc);
	if (res)
		goto out_free_inq;

	v_sup = id_ctrl->vwc;
	kfree(id_ctrl);

	memset(inq_response, 0, EXTENDED_INQUIRY_DATA_PAGE_LENGTH);
	inq_response[1] = INQ_EXTENDED_INQUIRY_DATA_PAGE;    /* Page Code */
	inq_response[2] = 0x00;    /* Page Length MSB */
	inq_response[3] = 0x3C;    /* Page Length LSB */
	inq_response[4] = microcode | spt | grd_chk | app_chk | ref_chk;
	inq_response[5] = uask_sup;
	inq_response[6] = v_sup;
	inq_response[7] = luiclr;
	inq_response[8] = 0;
	inq_response[9] = 0;

	xfer_len = min(alloc_len, EXTENDED_INQUIRY_DATA_PAGE_LENGTH);
	res = nvme_trans_copy_to_user(hdr, inq_response, xfer_len);

 out_free_inq:
	kfree(inq_response);
	return res;
}

static int nvme_trans_bdev_limits_page(struct nvme_ns *ns, struct sg_io_hdr *hdr,
					u8 *inq_response, int alloc_len)
{
	__be32 max_sectors = cpu_to_be32(
		nvme_block_nr(ns, queue_max_hw_sectors(ns->queue)));
	__be32 max_discard = cpu_to_be32(ns->queue->limits.max_discard_sectors);
	__be32 discard_desc_count = cpu_to_be32(0x100);

	memset(inq_response, 0, STANDARD_INQUIRY_LENGTH);
	inq_response[1] = VPD_BLOCK_LIMITS;
	inq_response[3] = 0x3c; /* Page Length */
	memcpy(&inq_response[8], &max_sectors, sizeof(u32));
	memcpy(&inq_response[20], &max_discard, sizeof(u32));

	if (max_discard)
		memcpy(&inq_response[24], &discard_desc_count, sizeof(u32));

	return nvme_trans_copy_to_user(hdr, inq_response, 0x3c);
}

static int nvme_trans_bdev_char_page(struct nvme_ns *ns, struct sg_io_hdr *hdr,
					int alloc_len)
{
	u8 *inq_response;
	int res;
	int xfer_len;

	inq_response = kzalloc(EXTENDED_INQUIRY_DATA_PAGE_LENGTH, GFP_KERNEL);
	if (inq_response == NULL) {
		res = -ENOMEM;
		goto out_mem;
	}

	inq_response[1] = INQ_BDEV_CHARACTERISTICS_PAGE;    /* Page Code */
	inq_response[2] = 0x00;    /* Page Length MSB */
	inq_response[3] = 0x3C;    /* Page Length LSB */
	inq_response[4] = 0x00;    /* Medium Rotation Rate MSB */
	inq_response[5] = 0x01;    /* Medium Rotation Rate LSB */
	inq_response[6] = 0x00;    /* Form Factor */

	xfer_len = min(alloc_len, EXTENDED_INQUIRY_DATA_PAGE_LENGTH);
	res = nvme_trans_copy_to_user(hdr, inq_response, xfer_len);

	kfree(inq_response);
 out_mem:
	return res;
}

/* LOG SENSE Helper Functions */

static int nvme_trans_log_supp_pages(struct nvme_ns *ns, struct sg_io_hdr *hdr,
					int alloc_len)
{
	int res;
	int xfer_len;
	u8 *log_response;

	log_response = kzalloc(LOG_PAGE_SUPPORTED_LOG_PAGES_LENGTH, GFP_KERNEL);
	if (log_response == NULL) {
		res = -ENOMEM;
		goto out_mem;
	}

	log_response[0] = LOG_PAGE_SUPPORTED_LOG_PAGES_PAGE;
	/* Subpage=0x00, Page Length MSB=0 */
	log_response[3] = SUPPORTED_LOG_PAGES_PAGE_LENGTH;
	log_response[4] = LOG_PAGE_SUPPORTED_LOG_PAGES_PAGE;
	log_response[5] = LOG_PAGE_INFORMATIONAL_EXCEPTIONS_PAGE;
	log_response[6] = LOG_PAGE_TEMPERATURE_PAGE;

	xfer_len = min(alloc_len, LOG_PAGE_SUPPORTED_LOG_PAGES_LENGTH);
	res = nvme_trans_copy_to_user(hdr, log_response, xfer_len);

	kfree(log_response);
 out_mem:
	return res;
}

static int nvme_trans_log_info_exceptions(struct nvme_ns *ns,
					struct sg_io_hdr *hdr, int alloc_len)
{
	int res;
	int xfer_len;
	u8 *log_response;
	struct nvme_smart_log *smart_log;
	u8 temp_c;
	u16 temp_k;

	log_response = kzalloc(LOG_INFO_EXCP_PAGE_LENGTH, GFP_KERNEL);
	if (log_response == NULL)
		return -ENOMEM;

	res = nvme_get_log_page(ns->ctrl, &smart_log);
	if (res < 0)
		goto out_free_response;

	if (res != NVME_SC_SUCCESS) {
		temp_c = LOG_TEMP_UNKNOWN;
	} else {
		temp_k = (smart_log->temperature[1] << 8) +
				(smart_log->temperature[0]);
		temp_c = temp_k - KELVIN_TEMP_FACTOR;
	}
	kfree(smart_log);

	log_response[0] = LOG_PAGE_INFORMATIONAL_EXCEPTIONS_PAGE;
	/* Subpage=0x00, Page Length MSB=0 */
	log_response[3] = REMAINING_INFO_EXCP_PAGE_LENGTH;
	/* Informational Exceptions Log Parameter 1 Start */
	/* Parameter Code=0x0000 bytes 4,5 */
	log_response[6] = 0x23; /* DU=0, TSD=1, ETC=0, TMC=0, FMT_AND_LNK=11b */
	log_response[7] = 0x04; /* PARAMETER LENGTH */
	/* Add sense Code and qualifier = 0x00 each */
	/* Use Temperature from NVMe Get Log Page, convert to C from K */
	log_response[10] = temp_c;

	xfer_len = min(alloc_len, LOG_INFO_EXCP_PAGE_LENGTH);
	res = nvme_trans_copy_to_user(hdr, log_response, xfer_len);

 out_free_response:
	kfree(log_response);
	return res;
}

static int nvme_trans_log_temperature(struct nvme_ns *ns, struct sg_io_hdr *hdr,
					int alloc_len)
{
	int res;
	int xfer_len;
	u8 *log_response;
	struct nvme_smart_log *smart_log;
	u32 feature_resp;
	u8 temp_c_cur, temp_c_thresh;
	u16 temp_k;

	log_response = kzalloc(LOG_TEMP_PAGE_LENGTH, GFP_KERNEL);
	if (log_response == NULL)
		return -ENOMEM;

	res = nvme_get_log_page(ns->ctrl, &smart_log);
	if (res < 0)
		goto out_free_response;

	if (res != NVME_SC_SUCCESS) {
		temp_c_cur = LOG_TEMP_UNKNOWN;
	} else {
		temp_k = (smart_log->temperature[1] << 8) +
				(smart_log->temperature[0]);
		temp_c_cur = temp_k - KELVIN_TEMP_FACTOR;
	}
	kfree(smart_log);

	/* Get Features for Temp Threshold */
	res = nvme_get_features(ns->ctrl, NVME_FEAT_TEMP_THRESH, 0, NULL, 0,
								&feature_resp);
	if (res != NVME_SC_SUCCESS)
		temp_c_thresh = LOG_TEMP_UNKNOWN;
	else
		temp_c_thresh = (feature_resp & 0xFFFF) - KELVIN_TEMP_FACTOR;

	log_response[0] = LOG_PAGE_TEMPERATURE_PAGE;
	/* Subpage=0x00, Page Length MSB=0 */
	log_response[3] = REMAINING_TEMP_PAGE_LENGTH;
	/* Temperature Log Parameter 1 (Temperature) Start */
	/* Parameter Code = 0x0000 */
	log_response[6] = 0x01;		/* Format and Linking = 01b */
	log_response[7] = 0x02;		/* Parameter Length */
	/* Use Temperature from NVMe Get Log Page, convert to C from K */
	log_response[9] = temp_c_cur;
	/* Temperature Log Parameter 2 (Reference Temperature) Start */
	log_response[11] = 0x01;	/* Parameter Code = 0x0001 */
	log_response[12] = 0x01;	/* Format and Linking = 01b */
	log_response[13] = 0x02;	/* Parameter Length */
	/* Use Temperature Thresh from NVMe Get Log Page, convert to C from K */
	log_response[15] = temp_c_thresh;

	xfer_len = min(alloc_len, LOG_TEMP_PAGE_LENGTH);
	res = nvme_trans_copy_to_user(hdr, log_response, xfer_len);

 out_free_response:
	kfree(log_response);
	return res;
}

/* MODE SENSE Helper Functions */

static int nvme_trans_fill_mode_parm_hdr(u8 *resp, int len, u8 cdb10, u8 llbaa,
					u16 mode_data_length, u16 blk_desc_len)
{
	/* Quick check to make sure I don't stomp on my own memory... */
	if ((cdb10 && len < 8) || (!cdb10 && len < 4))
		return -EINVAL;

	if (cdb10) {
		resp[0] = (mode_data_length & 0xFF00) >> 8;
		resp[1] = (mode_data_length & 0x00FF);
		resp[3] = 0x10 /* DPOFUA */;
		resp[4] = llbaa;
		resp[5] = RESERVED_FIELD;
		resp[6] = (blk_desc_len & 0xFF00) >> 8;
		resp[7] = (blk_desc_len & 0x00FF);
	} else {
		resp[0] = (mode_data_length & 0x00FF);
		resp[2] = 0x10 /* DPOFUA */;
		resp[3] = (blk_desc_len & 0x00FF);
	}

	return 0;
}

static int nvme_trans_fill_blk_desc(struct nvme_ns *ns, struct sg_io_hdr *hdr,
				    u8 *resp, int len, u8 llbaa)
{
	int res;
	int nvme_sc;
	struct nvme_id_ns *id_ns;
	u8 flbas;
	u32 lba_length;

	if (llbaa == 0 && len < MODE_PAGE_BLK_DES_LEN)
		return -EINVAL;
	else if (llbaa > 0 && len < MODE_PAGE_LLBAA_BLK_DES_LEN)
		return -EINVAL;

	nvme_sc = nvme_identify_ns(ns->ctrl, ns->ns_id, &id_ns);
	res = nvme_trans_status_code(hdr, nvme_sc);
	if (res)
		return res;

	flbas = (id_ns->flbas) & 0x0F;
	lba_length = (1 << (id_ns->lbaf[flbas].ds));

	if (llbaa == 0) {
		__be32 tmp_cap = cpu_to_be32(le64_to_cpu(id_ns->ncap));
		/* Byte 4 is reserved */
		__be32 tmp_len = cpu_to_be32(lba_length & 0x00FFFFFF);

		memcpy(resp, &tmp_cap, sizeof(u32));
		memcpy(&resp[4], &tmp_len, sizeof(u32));
	} else {
		__be64 tmp_cap = cpu_to_be64(le64_to_cpu(id_ns->ncap));
		__be32 tmp_len = cpu_to_be32(lba_length);

		memcpy(resp, &tmp_cap, sizeof(u64));
		/* Bytes 8, 9, 10, 11 are reserved */
		memcpy(&resp[12], &tmp_len, sizeof(u32));
	}

	kfree(id_ns);
	return res;
}

static int nvme_trans_fill_control_page(struct nvme_ns *ns,
					struct sg_io_hdr *hdr, u8 *resp,
					int len)
{
	if (len < MODE_PAGE_CONTROL_LEN)
		return -EINVAL;

	resp[0] = MODE_PAGE_CONTROL;
	resp[1] = MODE_PAGE_CONTROL_LEN_FIELD;
	resp[2] = 0x0E;		/* TST=000b, TMF_ONLY=0, DPICZ=1,
				 * D_SENSE=1, GLTSD=1, RLEC=0 */
	resp[3] = 0x12;		/* Q_ALGO_MODIFIER=1h, NUAR=0, QERR=01b */
	/* Byte 4:  VS=0, RAC=0, UA_INT=0, SWP=0 */
	resp[5] = 0x40;		/* ATO=0, TAS=1, ATMPE=0, RWWP=0, AUTOLOAD=0 */
	/* resp[6] and [7] are obsolete, thus zero */
	resp[8] = 0xFF;		/* Busy timeout period = 0xffff */
	resp[9] = 0xFF;
	/* Bytes 10,11: Extended selftest completion time = 0x0000 */

	return 0;
}

static int nvme_trans_fill_caching_page(struct nvme_ns *ns,
					struct sg_io_hdr *hdr,
					u8 *resp, int len)
{
	int res = 0;
	int nvme_sc;
	u32 feature_resp;
	u8 vwc;

	if (len < MODE_PAGE_CACHING_LEN)
		return -EINVAL;

	nvme_sc = nvme_get_features(ns->ctrl, NVME_FEAT_VOLATILE_WC, 0, NULL, 0,
								&feature_resp);
	res = nvme_trans_status_code(hdr, nvme_sc);
	if (res)
		return res;

	vwc = feature_resp & 0x00000001;

	resp[0] = MODE_PAGE_CACHING;
	resp[1] = MODE_PAGE_CACHING_LEN_FIELD;
	resp[2] = vwc << 2;
	return 0;
}

static int nvme_trans_fill_pow_cnd_page(struct nvme_ns *ns,
					struct sg_io_hdr *hdr, u8 *resp,
					int len)
{
	if (len < MODE_PAGE_POW_CND_LEN)
		return -EINVAL;

	resp[0] = MODE_PAGE_POWER_CONDITION;
	resp[1] = MODE_PAGE_POW_CND_LEN_FIELD;
	/* All other bytes are zero */

	return 0;
}

static int nvme_trans_fill_inf_exc_page(struct nvme_ns *ns,
					struct sg_io_hdr *hdr, u8 *resp,
					int len)
{
	if (len < MODE_PAGE_INF_EXC_LEN)
		return -EINVAL;

	resp[0] = MODE_PAGE_INFO_EXCEP;
	resp[1] = MODE_PAGE_INF_EXC_LEN_FIELD;
	resp[2] = 0x88;
	/* All other bytes are zero */

	return 0;
}

static int nvme_trans_fill_all_pages(struct nvme_ns *ns, struct sg_io_hdr *hdr,
				     u8 *resp, int len)
{
	int res;
	u16 mode_pages_offset_1 = 0;
	u16 mode_pages_offset_2, mode_pages_offset_3, mode_pages_offset_4;

	mode_pages_offset_2 = mode_pages_offset_1 + MODE_PAGE_CACHING_LEN;
	mode_pages_offset_3 = mode_pages_offset_2 + MODE_PAGE_CONTROL_LEN;
	mode_pages_offset_4 = mode_pages_offset_3 + MODE_PAGE_POW_CND_LEN;

	res = nvme_trans_fill_caching_page(ns, hdr, &resp[mode_pages_offset_1],
					MODE_PAGE_CACHING_LEN);
	if (res)
		return res;
	res = nvme_trans_fill_control_page(ns, hdr, &resp[mode_pages_offset_2],
					MODE_PAGE_CONTROL_LEN);
	if (res)
		return res;
	res = nvme_trans_fill_pow_cnd_page(ns, hdr, &resp[mode_pages_offset_3],
					MODE_PAGE_POW_CND_LEN);
	if (res)
		return res;
	return nvme_trans_fill_inf_exc_page(ns, hdr, &resp[mode_pages_offset_4],
					MODE_PAGE_INF_EXC_LEN);
}

static inline int nvme_trans_get_blk_desc_len(u8 dbd, u8 llbaa)
{
	if (dbd == MODE_SENSE_BLK_DESC_ENABLED) {
		/* SPC-4: len = 8 x Num_of_descriptors if llbaa = 0, 16x if 1 */
		return 8 * (llbaa + 1) * MODE_SENSE_BLK_DESC_COUNT;
	} else {
		return 0;
	}
}

static int nvme_trans_mode_page_create(struct nvme_ns *ns,
					struct sg_io_hdr *hdr, u8 *cmd,
					u16 alloc_len, u8 cdb10,
					int (*mode_page_fill_func)
					(struct nvme_ns *,
					struct sg_io_hdr *hdr, u8 *, int),
					u16 mode_pages_tot_len)
{
	int res;
	int xfer_len;
	u8 *response;
	u8 dbd, llbaa;
	u16 resp_size;
	int mph_size;
	u16 mode_pages_offset_1;
	u16 blk_desc_len, blk_desc_offset, mode_data_length;

	dbd = (cmd[1] & MODE_SENSE_DBD_MASK) >> MODE_SENSE_DBD_SHIFT;
	llbaa = (cmd[1] & MODE_SENSE_LLBAA_MASK) >> MODE_SENSE_LLBAA_SHIFT;
	mph_size = cdb10 ? MODE_SENSE10_MPH_SIZE : MODE_SENSE6_MPH_SIZE;

	blk_desc_len = nvme_trans_get_blk_desc_len(dbd, llbaa);

	resp_size = mph_size + blk_desc_len + mode_pages_tot_len;
	/* Refer spc4r34 Table 440 for calculation of Mode data Length field */
	mode_data_length = 3 + (3 * cdb10) + blk_desc_len + mode_pages_tot_len;

	blk_desc_offset = mph_size;
	mode_pages_offset_1 = blk_desc_offset + blk_desc_len;

	response = kzalloc(resp_size, GFP_KERNEL);
	if (response == NULL) {
		res = -ENOMEM;
		goto out_mem;
	}

	res = nvme_trans_fill_mode_parm_hdr(&response[0], mph_size, cdb10,
					llbaa, mode_data_length, blk_desc_len);
	if (res)
		goto out_free;
	if (blk_desc_len > 0) {
		res = nvme_trans_fill_blk_desc(ns, hdr,
					       &response[blk_desc_offset],
					       blk_desc_len, llbaa);
		if (res)
			goto out_free;
	}
	res = mode_page_fill_func(ns, hdr, &response[mode_pages_offset_1],
					mode_pages_tot_len);
	if (res)
		goto out_free;

	xfer_len = min(alloc_len, resp_size);
	res = nvme_trans_copy_to_user(hdr, response, xfer_len);

 out_free:
	kfree(response);
 out_mem:
	return res;
}

/* Read Capacity Helper Functions */

static void nvme_trans_fill_read_cap(u8 *response, struct nvme_id_ns *id_ns,
								u8 cdb16)
{
	u8 flbas;
	u32 lba_length;
	u64 rlba;
	u8 prot_en;
	u8 p_type_lut[4] = {0, 0, 1, 2};
	__be64 tmp_rlba;
	__be32 tmp_rlba_32;
	__be32 tmp_len;

	flbas = (id_ns->flbas) & 0x0F;
	lba_length = (1 << (id_ns->lbaf[flbas].ds));
	rlba = le64_to_cpup(&id_ns->nsze) - 1;
	(id_ns->dps) ? (prot_en = 0x01) : (prot_en = 0);

	if (!cdb16) {
		if (rlba > 0xFFFFFFFF)
			rlba = 0xFFFFFFFF;
		tmp_rlba_32 = cpu_to_be32(rlba);
		tmp_len = cpu_to_be32(lba_length);
		memcpy(response, &tmp_rlba_32, sizeof(u32));
		memcpy(&response[4], &tmp_len, sizeof(u32));
	} else {
		tmp_rlba = cpu_to_be64(rlba);
		tmp_len = cpu_to_be32(lba_length);
		memcpy(response, &tmp_rlba, sizeof(u64));
		memcpy(&response[8], &tmp_len, sizeof(u32));
		response[12] = (p_type_lut[id_ns->dps & 0x3] << 1) | prot_en;
		/* P_I_Exponent = 0x0 | LBPPBE = 0x0 */
		/* LBPME = 0 | LBPRZ = 0 | LALBA = 0x00 */
		/* Bytes 16-31 - Reserved */
	}
}

/* Start Stop Unit Helper Functions */

static int nvme_trans_send_activate_fw_cmd(struct nvme_ns *ns, struct sg_io_hdr *hdr,
					u8 buffer_id)
{
	struct nvme_command c;
	int nvme_sc;

	memset(&c, 0, sizeof(c));
	c.common.opcode = nvme_admin_activate_fw;
	c.common.cdw10[0] = cpu_to_le32(buffer_id | NVME_FWACT_REPL_ACTV);

	nvme_sc = nvme_submit_sync_cmd(ns->queue, &c, NULL, 0);
	return nvme_trans_status_code(hdr, nvme_sc);
}

static int nvme_trans_send_download_fw_cmd(struct nvme_ns *ns, struct sg_io_hdr *hdr,
					u8 opcode, u32 tot_len, u32 offset,
					u8 buffer_id)
{
	int nvme_sc;
	struct nvme_command c;

	if (hdr->iovec_count > 0) {
		/* Assuming SGL is not allowed for this command */
		return nvme_trans_completion(hdr,
					SAM_STAT_CHECK_CONDITION,
					ILLEGAL_REQUEST,
					SCSI_ASC_INVALID_CDB,
					SCSI_ASCQ_CAUSE_NOT_REPORTABLE);
	}

	memset(&c, 0, sizeof(c));
	c.common.opcode = nvme_admin_download_fw;
	c.dlfw.numd = cpu_to_le32((tot_len/BYTES_TO_DWORDS) - 1);
	c.dlfw.offset = cpu_to_le32(offset/BYTES_TO_DWORDS);

	nvme_sc = nvme_submit_user_cmd(ns->ctrl->admin_q, &c,
			hdr->dxferp, tot_len, NULL, 0);
	return nvme_trans_status_code(hdr, nvme_sc);
}

/* Mode Select Helper Functions */

static inline void nvme_trans_modesel_get_bd_len(u8 *parm_list, u8 cdb10,
						u16 *bd_len, u8 *llbaa)
{
	if (cdb10) {
		/* 10 Byte CDB */
		*bd_len = (parm_list[MODE_SELECT_10_BD_OFFSET] << 8) +
			parm_list[MODE_SELECT_10_BD_OFFSET + 1];
		*llbaa = parm_list[MODE_SELECT_10_LLBAA_OFFSET] &
				MODE_SELECT_10_LLBAA_MASK;
	} else {
		/* 6 Byte CDB */
		*bd_len = parm_list[MODE_SELECT_6_BD_OFFSET];
	}
}

static void nvme_trans_modesel_save_bd(struct nvme_ns *ns, u8 *parm_list,
					u16 idx, u16 bd_len, u8 llbaa)
{
	/* Store block descriptor info if a FORMAT UNIT comes later */
	/* TODO Saving 1st BD info; what to do if multiple BD received? */
	if (llbaa == 0) {
		/* Standard Block Descriptor - spc4r34 7.5.5.1 */
		ns->mode_select_num_blocks =
				(parm_list[idx + 1] << 16) +
				(parm_list[idx + 2] << 8) +
				(parm_list[idx + 3]);

		ns->mode_select_block_len =
				(parm_list[idx + 5] << 16) +
				(parm_list[idx + 6] << 8) +
				(parm_list[idx + 7]);
	} else {
		/* Long LBA Block Descriptor - sbc3r27 6.4.2.3 */
		ns->mode_select_num_blocks =
				(((u64)parm_list[idx + 0]) << 56) +
				(((u64)parm_list[idx + 1]) << 48) +
				(((u64)parm_list[idx + 2]) << 40) +
				(((u64)parm_list[idx + 3]) << 32) +
				(((u64)parm_list[idx + 4]) << 24) +
				(((u64)parm_list[idx + 5]) << 16) +
				(((u64)parm_list[idx + 6]) << 8) +
				((u64)parm_list[idx + 7]);

		ns->mode_select_block_len =
				(parm_list[idx + 12] << 24) +
				(parm_list[idx + 13] << 16) +
				(parm_list[idx + 14] << 8) +
				(parm_list[idx + 15]);
	}
}

static int nvme_trans_modesel_get_mp(struct nvme_ns *ns, struct sg_io_hdr *hdr,
					u8 *mode_page, u8 page_code)
{
	int res = 0;
	int nvme_sc;
	unsigned dword11;

	switch (page_code) {
	case MODE_PAGE_CACHING:
		dword11 = ((mode_page[2] & CACHING_MODE_PAGE_WCE_MASK) ? 1 : 0);
		nvme_sc = nvme_set_features(ns->ctrl, NVME_FEAT_VOLATILE_WC,
					    dword11, NULL, 0, NULL);
		res = nvme_trans_status_code(hdr, nvme_sc);
		break;
	case MODE_PAGE_CONTROL:
		break;
	case MODE_PAGE_POWER_CONDITION:
		/* Verify the OS is not trying to set timers */
		if ((mode_page[2] & 0x01) != 0 || (mode_page[3] & 0x0F) != 0) {
			res = nvme_trans_completion(hdr,
						SAM_STAT_CHECK_CONDITION,
						ILLEGAL_REQUEST,
						SCSI_ASC_INVALID_PARAMETER,
						SCSI_ASCQ_CAUSE_NOT_REPORTABLE);
			break;
		}
		break;
	default:
		res = nvme_trans_completion(hdr, SAM_STAT_CHECK_CONDITION,
					ILLEGAL_REQUEST, SCSI_ASC_INVALID_CDB,
					SCSI_ASCQ_CAUSE_NOT_REPORTABLE);
		break;
	}

	return res;
}

static int nvme_trans_modesel_data(struct nvme_ns *ns, struct sg_io_hdr *hdr,
					u8 *cmd, u16 parm_list_len, u8 pf,
					u8 sp, u8 cdb10)
{
	int res;
	u8 *parm_list;
	u16 bd_len;
	u8 llbaa = 0;
	u16 index, saved_index;
	u8 page_code;
	u16 mp_size;

	/* Get parm list from data-in/out buffer */
	parm_list = kmalloc(parm_list_len, GFP_KERNEL);
	if (parm_list == NULL) {
		res = -ENOMEM;
		goto out;
	}

	res = nvme_trans_copy_from_user(hdr, parm_list, parm_list_len);
	if (res)
		goto out_mem;

	nvme_trans_modesel_get_bd_len(parm_list, cdb10, &bd_len, &llbaa);
	index = (cdb10) ? (MODE_SELECT_10_MPH_SIZE) : (MODE_SELECT_6_MPH_SIZE);

	if (bd_len != 0) {
		/* Block Descriptors present, parse */
		nvme_trans_modesel_save_bd(ns, parm_list, index, bd_len, llbaa);
		index += bd_len;
	}
	saved_index = index;

	/* Multiple mode pages may be present; iterate through all */
	/* In 1st Iteration, don't do NVME Command, only check for CDB errors */
	do {
		page_code = parm_list[index] & MODE_SELECT_PAGE_CODE_MASK;
		mp_size = parm_list[index + 1] + 2;
		if ((page_code != MODE_PAGE_CACHING) &&
		    (page_code != MODE_PAGE_CONTROL) &&
		    (page_code != MODE_PAGE_POWER_CONDITION)) {
			res = nvme_trans_completion(hdr,
						SAM_STAT_CHECK_CONDITION,
						ILLEGAL_REQUEST,
						SCSI_ASC_INVALID_CDB,
						SCSI_ASCQ_CAUSE_NOT_REPORTABLE);
			goto out_mem;
		}
		index += mp_size;
	} while (index < parm_list_len);

	/* In 2nd Iteration, do the NVME Commands */
	index = saved_index;
	do {
		page_code = parm_list[index] & MODE_SELECT_PAGE_CODE_MASK;
		mp_size = parm_list[index + 1] + 2;
		res = nvme_trans_modesel_get_mp(ns, hdr, &parm_list[index],
								page_code);
		if (res)
			break;
		index += mp_size;
	} while (index < parm_list_len);

 out_mem:
	kfree(parm_list);
 out:
	return res;
}

/* Format Unit Helper Functions */

static int nvme_trans_fmt_set_blk_size_count(struct nvme_ns *ns,
					     struct sg_io_hdr *hdr)
{
	int res = 0;
	int nvme_sc;
	u8 flbas;

	/*
	 * SCSI Expects a MODE SELECT would have been issued prior to
	 * a FORMAT UNIT, and the block size and number would be used
	 * from the block descriptor in it. If a MODE SELECT had not
	 * been issued, FORMAT shall use the current values for both.
	 */

	if (ns->mode_select_num_blocks == 0 || ns->mode_select_block_len == 0) {
		struct nvme_id_ns *id_ns;

		nvme_sc = nvme_identify_ns(ns->ctrl, ns->ns_id, &id_ns);
		res = nvme_trans_status_code(hdr, nvme_sc);
		if (res)
			return res;

		if (ns->mode_select_num_blocks == 0)
			ns->mode_select_num_blocks = le64_to_cpu(id_ns->ncap);
		if (ns->mode_select_block_len == 0) {
			flbas = (id_ns->flbas) & 0x0F;
			ns->mode_select_block_len =
						(1 << (id_ns->lbaf[flbas].ds));
		}

		kfree(id_ns);
	}

	return 0;
}

static int nvme_trans_fmt_get_parm_header(struct sg_io_hdr *hdr, u8 len,
					u8 format_prot_info, u8 *nvme_pf_code)
{
	int res;
	u8 *parm_list;
	u8 pf_usage, pf_code;

	parm_list = kmalloc(len, GFP_KERNEL);
	if (parm_list == NULL) {
		res = -ENOMEM;
		goto out;
	}
	res = nvme_trans_copy_from_user(hdr, parm_list, len);
	if (res)
		goto out_mem;

	if ((parm_list[FORMAT_UNIT_IMMED_OFFSET] &
				FORMAT_UNIT_IMMED_MASK) != 0) {
		res = nvme_trans_completion(hdr, SAM_STAT_CHECK_CONDITION,
					ILLEGAL_REQUEST, SCSI_ASC_INVALID_CDB,
					SCSI_ASCQ_CAUSE_NOT_REPORTABLE);
		goto out_mem;
	}

	if (len == FORMAT_UNIT_LONG_PARM_LIST_LEN &&
	    (parm_list[FORMAT_UNIT_PROT_INT_OFFSET] & 0x0F) != 0) {
		res = nvme_trans_completion(hdr, SAM_STAT_CHECK_CONDITION,
					ILLEGAL_REQUEST, SCSI_ASC_INVALID_CDB,
					SCSI_ASCQ_CAUSE_NOT_REPORTABLE);
		goto out_mem;
	}
	pf_usage = parm_list[FORMAT_UNIT_PROT_FIELD_USAGE_OFFSET] &
			FORMAT_UNIT_PROT_FIELD_USAGE_MASK;
	pf_code = (pf_usage << 2) | format_prot_info;
	switch (pf_code) {
	case 0:
		*nvme_pf_code = 0;
		break;
	case 2:
		*nvme_pf_code = 1;
		break;
	case 3:
		*nvme_pf_code = 2;
		break;
	case 7:
		*nvme_pf_code = 3;
		break;
	default:
		res = nvme_trans_completion(hdr, SAM_STAT_CHECK_CONDITION,
					ILLEGAL_REQUEST, SCSI_ASC_INVALID_CDB,
					SCSI_ASCQ_CAUSE_NOT_REPORTABLE);
		break;
	}

 out_mem:
	kfree(parm_list);
 out:
	return res;
}

static int nvme_trans_fmt_send_cmd(struct nvme_ns *ns, struct sg_io_hdr *hdr,
				   u8 prot_info)
{
	int res;
	int nvme_sc;
	struct nvme_id_ns *id_ns;
	u8 i;
	u8 nlbaf;
	u8 selected_lbaf = 0xFF;
	u32 cdw10 = 0;
	struct nvme_command c;

	/* Loop thru LBAF's in id_ns to match reqd lbaf, put in cdw10 */
	nvme_sc = nvme_identify_ns(ns->ctrl, ns->ns_id, &id_ns);
	res = nvme_trans_status_code(hdr, nvme_sc);
	if (res)
		return res;

	nlbaf = id_ns->nlbaf;

	for (i = 0; i < nlbaf; i++) {
		if (ns->mode_select_block_len == (1 << (id_ns->lbaf[i].ds))) {
			selected_lbaf = i;
			break;
		}
	}
	if (selected_lbaf > 0x0F) {
		res = nvme_trans_completion(hdr, SAM_STAT_CHECK_CONDITION,
				ILLEGAL_REQUEST, SCSI_ASC_INVALID_PARAMETER,
				SCSI_ASCQ_CAUSE_NOT_REPORTABLE);
	}
	if (ns->mode_select_num_blocks != le64_to_cpu(id_ns->ncap)) {
		res = nvme_trans_completion(hdr, SAM_STAT_CHECK_CONDITION,
				ILLEGAL_REQUEST, SCSI_ASC_INVALID_PARAMETER,
				SCSI_ASCQ_CAUSE_NOT_REPORTABLE);
	}

	cdw10 |= prot_info << 5;
	cdw10 |= selected_lbaf & 0x0F;
	memset(&c, 0, sizeof(c));
	c.format.opcode = nvme_admin_format_nvm;
	c.format.nsid = cpu_to_le32(ns->ns_id);
	c.format.cdw10 = cpu_to_le32(cdw10);

	nvme_sc = nvme_submit_sync_cmd(ns->ctrl->admin_q, &c, NULL, 0);
	res = nvme_trans_status_code(hdr, nvme_sc);

	kfree(id_ns);
	return res;
}

static inline u32 nvme_trans_io_get_num_cmds(struct sg_io_hdr *hdr,
					struct nvme_trans_io_cdb *cdb_info,
					u32 max_blocks)
{
	/* If using iovecs, send one nvme command per vector */
	if (hdr->iovec_count > 0)
		return hdr->iovec_count;
	else if (cdb_info->xfer_len > max_blocks)
		return ((cdb_info->xfer_len - 1) / max_blocks) + 1;
	else
		return 1;
}

static u16 nvme_trans_io_get_control(struct nvme_ns *ns,
					struct nvme_trans_io_cdb *cdb_info)
{
	u16 control = 0;

	/* When Protection information support is added, implement here */

	if (cdb_info->fua > 0)
		control |= NVME_RW_FUA;

	return control;
}

static int nvme_trans_do_nvme_io(struct nvme_ns *ns, struct sg_io_hdr *hdr,
				struct nvme_trans_io_cdb *cdb_info, u8 is_write)
{
	int nvme_sc = NVME_SC_SUCCESS;
	u32 num_cmds;
	u64 unit_len;
	u64 unit_num_blocks;	/* Number of blocks to xfer in each nvme cmd */
	u32 retcode;
	u32 i = 0;
	u64 nvme_offset = 0;
	void __user *next_mapping_addr;
	struct nvme_command c;
	u8 opcode = (is_write ? nvme_cmd_write : nvme_cmd_read);
	u16 control;
	u32 max_blocks = queue_max_hw_sectors(ns->queue);

	num_cmds = nvme_trans_io_get_num_cmds(hdr, cdb_info, max_blocks);

	/*
	 * This loop handles two cases.
	 * First, when an SGL is used in the form of an iovec list:
	 *   - Use iov_base as the next mapping address for the nvme command_id
	 *   - Use iov_len as the data transfer length for the command.
	 * Second, when we have a single buffer
	 *   - If larger than max_blocks, split into chunks, offset
	 *        each nvme command accordingly.
	 */
	for (i = 0; i < num_cmds; i++) {
		memset(&c, 0, sizeof(c));
		if (hdr->iovec_count > 0) {
			struct sg_iovec sgl;

			retcode = copy_from_user(&sgl, hdr->dxferp +
					i * sizeof(struct sg_iovec),
					sizeof(struct sg_iovec));
			if (retcode)
				return -EFAULT;
			unit_len = sgl.iov_len;
			unit_num_blocks = unit_len >> ns->lba_shift;
			next_mapping_addr = sgl.iov_base;
		} else {
			unit_num_blocks = min((u64)max_blocks,
					(cdb_info->xfer_len - nvme_offset));
			unit_len = unit_num_blocks << ns->lba_shift;
			next_mapping_addr = hdr->dxferp +
					((1 << ns->lba_shift) * nvme_offset);
		}

		c.rw.opcode = opcode;
		c.rw.nsid = cpu_to_le32(ns->ns_id);
		c.rw.slba = cpu_to_le64(cdb_info->lba + nvme_offset);
		c.rw.length = cpu_to_le16(unit_num_blocks - 1);
		control = nvme_trans_io_get_control(ns, cdb_info);
		c.rw.control = cpu_to_le16(control);

		if (get_capacity(ns->disk) - unit_num_blocks <
				cdb_info->lba + nvme_offset) {
			nvme_sc = NVME_SC_LBA_RANGE;
			break;
		}
		nvme_sc = nvme_submit_user_cmd(ns->queue, &c,
				next_mapping_addr, unit_len, NULL, 0);
		if (nvme_sc)
			break;

		nvme_offset += unit_num_blocks;
	}

	return nvme_trans_status_code(hdr, nvme_sc);
}


/* SCSI Command Translation Functions */

static int nvme_trans_io(struct nvme_ns *ns, struct sg_io_hdr *hdr, u8 is_write,
							u8 *cmd)
{
	int res = 0;
	struct nvme_trans_io_cdb cdb_info = { 0, };
	u8 opcode = cmd[0];
	u64 xfer_bytes;
	u64 sum_iov_len = 0;
	struct sg_iovec sgl;
	int i;
	size_t not_copied;

	/*
	 * The FUA and WPROTECT fields are not supported in 6-byte CDBs,
	 * but always in the same place for all others.
	 */
	switch (opcode) {
	case WRITE_6:
	case READ_6:
		break;
	default:
		cdb_info.fua = cmd[1] & 0x8;
		cdb_info.prot_info = (cmd[1] & 0xe0) >> 5;
		if (cdb_info.prot_info && !ns->pi_type) {
			return nvme_trans_completion(hdr,
					SAM_STAT_CHECK_CONDITION,
					ILLEGAL_REQUEST,
					SCSI_ASC_INVALID_CDB,
					SCSI_ASCQ_CAUSE_NOT_REPORTABLE);
		}
	}

	switch (opcode) {
	case WRITE_6:
	case READ_6:
		cdb_info.lba = get_unaligned_be24(&cmd[1]);
		cdb_info.xfer_len = cmd[4];
		if (cdb_info.xfer_len == 0)
			cdb_info.xfer_len = 256;
		break;
	case WRITE_10:
	case READ_10:
		cdb_info.lba = get_unaligned_be32(&cmd[2]);
		cdb_info.xfer_len = get_unaligned_be16(&cmd[7]);
		break;
	case WRITE_12:
	case READ_12:
		cdb_info.lba = get_unaligned_be32(&cmd[2]);
		cdb_info.xfer_len = get_unaligned_be32(&cmd[6]);
		break;
	case WRITE_16:
	case READ_16:
		cdb_info.lba = get_unaligned_be64(&cmd[2]);
		cdb_info.xfer_len = get_unaligned_be32(&cmd[10]);
		break;
	default:
		/* Will never really reach here */
		res = -EIO;
		goto out;
	}

	/* Calculate total length of transfer (in bytes) */
	if (hdr->iovec_count > 0) {
		for (i = 0; i < hdr->iovec_count; i++) {
			not_copied = copy_from_user(&sgl, hdr->dxferp +
						i * sizeof(struct sg_iovec),
						sizeof(struct sg_iovec));
			if (not_copied)
				return -EFAULT;
			sum_iov_len += sgl.iov_len;
			/* IO vector sizes should be multiples of block size */
			if (sgl.iov_len % (1 << ns->lba_shift) != 0) {
				res = nvme_trans_completion(hdr,
						SAM_STAT_CHECK_CONDITION,
						ILLEGAL_REQUEST,
						SCSI_ASC_INVALID_PARAMETER,
						SCSI_ASCQ_CAUSE_NOT_REPORTABLE);
				goto out;
			}
		}
	} else {
		sum_iov_len = hdr->dxfer_len;
	}

	/* As Per sg ioctl howto, if the lengths differ, use the lower one */
	xfer_bytes = min(((u64)hdr->dxfer_len), sum_iov_len);

	/* If block count and actual data buffer size dont match, error out */
	if (xfer_bytes != (cdb_info.xfer_len << ns->lba_shift)) {
		res = -EINVAL;
		goto out;
	}

	/* Check for 0 length transfer - it is not illegal */
	if (cdb_info.xfer_len == 0)
		goto out;

	/* Send NVMe IO Command(s) */
	res = nvme_trans_do_nvme_io(ns, hdr, &cdb_info, is_write);
	if (res)
		goto out;

 out:
	return res;
}

static int nvme_trans_inquiry(struct nvme_ns *ns, struct sg_io_hdr *hdr,
							u8 *cmd)
{
	int res = 0;
	u8 evpd;
	u8 page_code;
	int alloc_len;
	u8 *inq_response;

	evpd = cmd[1] & 0x01;
	page_code = cmd[2];
	alloc_len = get_unaligned_be16(&cmd[3]);

	inq_response = kmalloc(max(alloc_len, STANDARD_INQUIRY_LENGTH),
				GFP_KERNEL);
	if (inq_response == NULL) {
		res = -ENOMEM;
		goto out_mem;
	}

	if (evpd == 0) {
		if (page_code == INQ_STANDARD_INQUIRY_PAGE) {
			res = nvme_trans_standard_inquiry_page(ns, hdr,
						inq_response, alloc_len);
		} else {
			res = nvme_trans_completion(hdr,
						SAM_STAT_CHECK_CONDITION,
						ILLEGAL_REQUEST,
						SCSI_ASC_INVALID_CDB,
						SCSI_ASCQ_CAUSE_NOT_REPORTABLE);
		}
	} else {
		switch (page_code) {
		case VPD_SUPPORTED_PAGES:
			res = nvme_trans_supported_vpd_pages(ns, hdr,
						inq_response, alloc_len);
			break;
		case VPD_SERIAL_NUMBER:
			res = nvme_trans_unit_serial_page(ns, hdr, inq_response,
								alloc_len);
			break;
		case VPD_DEVICE_IDENTIFIERS:
			res = nvme_trans_device_id_page(ns, hdr, inq_response,
								alloc_len);
			break;
		case VPD_EXTENDED_INQUIRY:
			res = nvme_trans_ext_inq_page(ns, hdr, alloc_len);
			break;
		case VPD_BLOCK_LIMITS:
			res = nvme_trans_bdev_limits_page(ns, hdr, inq_response,
								alloc_len);
			break;
		case VPD_BLOCK_DEV_CHARACTERISTICS:
			res = nvme_trans_bdev_char_page(ns, hdr, alloc_len);
			break;
		default:
			res = nvme_trans_completion(hdr,
						SAM_STAT_CHECK_CONDITION,
						ILLEGAL_REQUEST,
						SCSI_ASC_INVALID_CDB,
						SCSI_ASCQ_CAUSE_NOT_REPORTABLE);
			break;
		}
	}
	kfree(inq_response);
 out_mem:
	return res;
}

static int nvme_trans_log_sense(struct nvme_ns *ns, struct sg_io_hdr *hdr,
							u8 *cmd)
{
	int res;
	u16 alloc_len;
	u8 pc;
	u8 page_code;

	if (cmd[1] != LOG_SENSE_CDB_SP_NOT_ENABLED) {
		res = nvme_trans_completion(hdr, SAM_STAT_CHECK_CONDITION,
					ILLEGAL_REQUEST, SCSI_ASC_INVALID_CDB,
					SCSI_ASCQ_CAUSE_NOT_REPORTABLE);
		goto out;
	}

	page_code = cmd[2] & LOG_SENSE_CDB_PAGE_CODE_MASK;
	pc = (cmd[2] & LOG_SENSE_CDB_PC_MASK) >> LOG_SENSE_CDB_PC_SHIFT;
	if (pc != LOG_SENSE_CDB_PC_CUMULATIVE_VALUES) {
		res = nvme_trans_completion(hdr, SAM_STAT_CHECK_CONDITION,
					ILLEGAL_REQUEST, SCSI_ASC_INVALID_CDB,
					SCSI_ASCQ_CAUSE_NOT_REPORTABLE);
		goto out;
	}
	alloc_len = get_unaligned_be16(&cmd[7]);
	switch (page_code) {
	case LOG_PAGE_SUPPORTED_LOG_PAGES_PAGE:
		res = nvme_trans_log_supp_pages(ns, hdr, alloc_len);
		break;
	case LOG_PAGE_INFORMATIONAL_EXCEPTIONS_PAGE:
		res = nvme_trans_log_info_exceptions(ns, hdr, alloc_len);
		break;
	case LOG_PAGE_TEMPERATURE_PAGE:
		res = nvme_trans_log_temperature(ns, hdr, alloc_len);
		break;
	default:
		res = nvme_trans_completion(hdr, SAM_STAT_CHECK_CONDITION,
					ILLEGAL_REQUEST, SCSI_ASC_INVALID_CDB,
					SCSI_ASCQ_CAUSE_NOT_REPORTABLE);
		break;
	}

 out:
	return res;
}

static int nvme_trans_mode_select(struct nvme_ns *ns, struct sg_io_hdr *hdr,
							u8 *cmd)
{
	u8 cdb10 = 0;
	u16 parm_list_len;
	u8 page_format;
	u8 save_pages;

	page_format = cmd[1] & MODE_SELECT_CDB_PAGE_FORMAT_MASK;
	save_pages = cmd[1] & MODE_SELECT_CDB_SAVE_PAGES_MASK;

	if (cmd[0] == MODE_SELECT) {
		parm_list_len = cmd[4];
	} else {
		parm_list_len = cmd[7];
		cdb10 = 1;
	}

	if (parm_list_len != 0) {
		/*
		 * According to SPC-4 r24, a paramter list length field of 0
		 * shall not be considered an error
		 */
		return nvme_trans_modesel_data(ns, hdr, cmd, parm_list_len,
						page_format, save_pages, cdb10);
	}

	return 0;
}

static int nvme_trans_mode_sense(struct nvme_ns *ns, struct sg_io_hdr *hdr,
							u8 *cmd)
{
	int res = 0;
	u16 alloc_len;
	u8 cdb10 = 0;

	if (cmd[0] == MODE_SENSE) {
		alloc_len = cmd[4];
	} else {
		alloc_len = get_unaligned_be16(&cmd[7]);
		cdb10 = 1;
	}

	if ((cmd[2] & MODE_SENSE_PAGE_CONTROL_MASK) !=
			MODE_SENSE_PC_CURRENT_VALUES) {
		res = nvme_trans_completion(hdr, SAM_STAT_CHECK_CONDITION,
					ILLEGAL_REQUEST, SCSI_ASC_INVALID_CDB,
					SCSI_ASCQ_CAUSE_NOT_REPORTABLE);
		goto out;
	}

	switch (cmd[2] & MODE_SENSE_PAGE_CODE_MASK) {
	case MODE_PAGE_CACHING:
		res = nvme_trans_mode_page_create(ns, hdr, cmd, alloc_len,
						cdb10,
						&nvme_trans_fill_caching_page,
						MODE_PAGE_CACHING_LEN);
		break;
	case MODE_PAGE_CONTROL:
		res = nvme_trans_mode_page_create(ns, hdr, cmd, alloc_len,
						cdb10,
						&nvme_trans_fill_control_page,
						MODE_PAGE_CONTROL_LEN);
		break;
	case MODE_PAGE_POWER_CONDITION:
		res = nvme_trans_mode_page_create(ns, hdr, cmd, alloc_len,
						cdb10,
						&nvme_trans_fill_pow_cnd_page,
						MODE_PAGE_POW_CND_LEN);
		break;
	case MODE_PAGE_INFO_EXCEP:
		res = nvme_trans_mode_page_create(ns, hdr, cmd, alloc_len,
						cdb10,
						&nvme_trans_fill_inf_exc_page,
						MODE_PAGE_INF_EXC_LEN);
		break;
	case MODE_PAGE_RETURN_ALL:
		res = nvme_trans_mode_page_create(ns, hdr, cmd, alloc_len,
						cdb10,
						&nvme_trans_fill_all_pages,
						MODE_PAGE_ALL_LEN);
		break;
	default:
		res = nvme_trans_completion(hdr, SAM_STAT_CHECK_CONDITION,
					ILLEGAL_REQUEST, SCSI_ASC_INVALID_CDB,
					SCSI_ASCQ_CAUSE_NOT_REPORTABLE);
		break;
	}

 out:
	return res;
}

static int nvme_trans_read_capacity(struct nvme_ns *ns, struct sg_io_hdr *hdr,
							u8 *cmd, u8 cdb16)
{
	int res;
	int nvme_sc;
	u32 alloc_len;
	u32 resp_size;
	u32 xfer_len;
	struct nvme_id_ns *id_ns;
	u8 *response;

	if (cdb16) {
		alloc_len = get_unaligned_be32(&cmd[10]);
		resp_size = READ_CAP_16_RESP_SIZE;
	} else {
		alloc_len = READ_CAP_10_RESP_SIZE;
		resp_size = READ_CAP_10_RESP_SIZE;
	}

	nvme_sc = nvme_identify_ns(ns->ctrl, ns->ns_id, &id_ns);
	res = nvme_trans_status_code(hdr, nvme_sc);
	if (res)
		return res;	

	response = kzalloc(resp_size, GFP_KERNEL);
	if (response == NULL) {
		res = -ENOMEM;
		goto out_free_id;
	}
	nvme_trans_fill_read_cap(response, id_ns, cdb16);

	xfer_len = min(alloc_len, resp_size);
	res = nvme_trans_copy_to_user(hdr, response, xfer_len);

	kfree(response);
 out_free_id:
	kfree(id_ns);
	return res;
}

static int nvme_trans_report_luns(struct nvme_ns *ns, struct sg_io_hdr *hdr,
							u8 *cmd)
{
	int res;
	int nvme_sc;
	u32 alloc_len, xfer_len, resp_size;
	u8 *response;
	struct nvme_id_ctrl *id_ctrl;
	u32 ll_length, lun_id;
	u8 lun_id_offset = REPORT_LUNS_FIRST_LUN_OFFSET;
	__be32 tmp_len;

	switch (cmd[2]) {
	default:
		return nvme_trans_completion(hdr, SAM_STAT_CHECK_CONDITION,
					ILLEGAL_REQUEST, SCSI_ASC_INVALID_CDB,
					SCSI_ASCQ_CAUSE_NOT_REPORTABLE);
	case ALL_LUNS_RETURNED:
	case ALL_WELL_KNOWN_LUNS_RETURNED:
	case RESTRICTED_LUNS_RETURNED:
		nvme_sc = nvme_identify_ctrl(ns->ctrl, &id_ctrl);
		res = nvme_trans_status_code(hdr, nvme_sc);
		if (res)
			return res;

		ll_length = le32_to_cpu(id_ctrl->nn) * LUN_ENTRY_SIZE;
		resp_size = ll_length + LUN_DATA_HEADER_SIZE;

		alloc_len = get_unaligned_be32(&cmd[6]);
		if (alloc_len < resp_size) {
			res = nvme_trans_completion(hdr,
					SAM_STAT_CHECK_CONDITION,
					ILLEGAL_REQUEST, SCSI_ASC_INVALID_CDB,
					SCSI_ASCQ_CAUSE_NOT_REPORTABLE);
			goto out_free_id;
		}

		response = kzalloc(resp_size, GFP_KERNEL);
		if (response == NULL) {
			res = -ENOMEM;
			goto out_free_id;
		}

		/* The first LUN ID will always be 0 per the SAM spec */
		for (lun_id = 0; lun_id < le32_to_cpu(id_ctrl->nn); lun_id++) {
			/*
			 * Set the LUN Id and then increment to the next LUN
			 * location in the parameter data.
			 */
			__be64 tmp_id = cpu_to_be64(lun_id);
			memcpy(&response[lun_id_offset], &tmp_id, sizeof(u64));
			lun_id_offset += LUN_ENTRY_SIZE;
		}
		tmp_len = cpu_to_be32(ll_length);
		memcpy(response, &tmp_len, sizeof(u32));
	}

	xfer_len = min(alloc_len, resp_size);
	res = nvme_trans_copy_to_user(hdr, response, xfer_len);

	kfree(response);
 out_free_id:
	kfree(id_ctrl);
	return res;
}

static int nvme_trans_request_sense(struct nvme_ns *ns, struct sg_io_hdr *hdr,
							u8 *cmd)
{
	int res;
	u8 alloc_len, xfer_len, resp_size;
	u8 desc_format;
	u8 *response;

	desc_format = cmd[1] & 0x01;
	alloc_len = cmd[4];

	resp_size = ((desc_format) ? (DESC_FMT_SENSE_DATA_SIZE) :
					(FIXED_FMT_SENSE_DATA_SIZE));
	response = kzalloc(resp_size, GFP_KERNEL);
	if (response == NULL) {
		res = -ENOMEM;
		goto out;
	}

	if (desc_format) {
		/* Descriptor Format Sense Data */
		response[0] = DESC_FORMAT_SENSE_DATA;
		response[1] = NO_SENSE;
		/* TODO How is LOW POWER CONDITION ON handled? (byte 2) */
		response[2] = SCSI_ASC_NO_SENSE;
		response[3] = SCSI_ASCQ_CAUSE_NOT_REPORTABLE;
		/* SDAT_OVFL = 0 | Additional Sense Length = 0 */
	} else {
		/* Fixed Format Sense Data */
		response[0] = FIXED_SENSE_DATA;
		/* Byte 1 = Obsolete */
		response[2] = NO_SENSE; /* FM, EOM, ILI, SDAT_OVFL = 0 */
		/* Bytes 3-6 - Information - set to zero */
		response[7] = FIXED_SENSE_DATA_ADD_LENGTH;
		/* Bytes 8-11 - Cmd Specific Information - set to zero */
		response[12] = SCSI_ASC_NO_SENSE;
		response[13] = SCSI_ASCQ_CAUSE_NOT_REPORTABLE;
		/* Byte 14 = Field Replaceable Unit Code = 0 */
		/* Bytes 15-17 - SKSV=0; Sense Key Specific = 0 */
	}

	xfer_len = min(alloc_len, resp_size);
	res = nvme_trans_copy_to_user(hdr, response, xfer_len);

	kfree(response);
 out:
	return res;
}

static int nvme_trans_security_protocol(struct nvme_ns *ns,
					struct sg_io_hdr *hdr,
					u8 *cmd)
{
	return nvme_trans_completion(hdr, SAM_STAT_CHECK_CONDITION,
				ILLEGAL_REQUEST, SCSI_ASC_ILLEGAL_COMMAND,
				SCSI_ASCQ_CAUSE_NOT_REPORTABLE);
}

static int nvme_trans_synchronize_cache(struct nvme_ns *ns,
					struct sg_io_hdr *hdr)
{
	int nvme_sc;
	struct nvme_command c;

	memset(&c, 0, sizeof(c));
	c.common.opcode = nvme_cmd_flush;
	c.common.nsid = cpu_to_le32(ns->ns_id);

	nvme_sc = nvme_submit_sync_cmd(ns->queue, &c, NULL, 0);
	return nvme_trans_status_code(hdr, nvme_sc);
}

static int nvme_trans_format_unit(struct nvme_ns *ns, struct sg_io_hdr *hdr,
							u8 *cmd)
{
	int res;
	u8 parm_hdr_len = 0;
	u8 nvme_pf_code = 0;
	u8 format_prot_info, long_list, format_data;

	format_prot_info = (cmd[1] & 0xc0) >> 6;
	long_list = cmd[1] & 0x20;
	format_data = cmd[1] & 0x10;

	if (format_data != 0) {
		if (format_prot_info != 0) {
			if (long_list == 0)
				parm_hdr_len = FORMAT_UNIT_SHORT_PARM_LIST_LEN;
			else
				parm_hdr_len = FORMAT_UNIT_LONG_PARM_LIST_LEN;
		}
	} else if (format_data == 0 && format_prot_info != 0) {
		res = nvme_trans_completion(hdr, SAM_STAT_CHECK_CONDITION,
					ILLEGAL_REQUEST, SCSI_ASC_INVALID_CDB,
					SCSI_ASCQ_CAUSE_NOT_REPORTABLE);
		goto out;
	}

	/* Get parm header from data-in/out buffer */
	/*
	 * According to the translation spec, the only fields in the parameter
	 * list we are concerned with are in the header. So allocate only that.
	 */
	if (parm_hdr_len > 0) {
		res = nvme_trans_fmt_get_parm_header(hdr, parm_hdr_len,
					format_prot_info, &nvme_pf_code);
		if (res)
			goto out;
	}

	/* Attempt to activate any previously downloaded firmware image */
	res = nvme_trans_send_activate_fw_cmd(ns, hdr, 0);

	/* Determine Block size and count and send format command */
	res = nvme_trans_fmt_set_blk_size_count(ns, hdr);
	if (res)
		goto out;

	res = nvme_trans_fmt_send_cmd(ns, hdr, nvme_pf_code);

 out:
	return res;
}

static int nvme_trans_test_unit_ready(struct nvme_ns *ns,
					struct sg_io_hdr *hdr,
					u8 *cmd)
{
	if (nvme_ctrl_ready(ns->ctrl))
		return nvme_trans_completion(hdr, SAM_STAT_CHECK_CONDITION,
					    NOT_READY, SCSI_ASC_LUN_NOT_READY,
					    SCSI_ASCQ_CAUSE_NOT_REPORTABLE);
	else
		return nvme_trans_completion(hdr, SAM_STAT_GOOD, NO_SENSE, 0, 0);
}

static int nvme_trans_write_buffer(struct nvme_ns *ns, struct sg_io_hdr *hdr,
							u8 *cmd)
{
	int res = 0;
	u32 buffer_offset, parm_list_length;
	u8 buffer_id, mode;

	parm_list_length = get_unaligned_be24(&cmd[6]);
	if (parm_list_length % BYTES_TO_DWORDS != 0) {
		/* NVMe expects Firmware file to be a whole number of DWORDS */
		res = nvme_trans_completion(hdr, SAM_STAT_CHECK_CONDITION,
					ILLEGAL_REQUEST, SCSI_ASC_INVALID_CDB,
					SCSI_ASCQ_CAUSE_NOT_REPORTABLE);
		goto out;
	}
	buffer_id = cmd[2];
	if (buffer_id > NVME_MAX_FIRMWARE_SLOT) {
		res = nvme_trans_completion(hdr, SAM_STAT_CHECK_CONDITION,
					ILLEGAL_REQUEST, SCSI_ASC_INVALID_CDB,
					SCSI_ASCQ_CAUSE_NOT_REPORTABLE);
		goto out;
	}
	mode = cmd[1] & 0x1f;
	buffer_offset = get_unaligned_be24(&cmd[3]);

	switch (mode) {
	case DOWNLOAD_SAVE_ACTIVATE:
		res = nvme_trans_send_download_fw_cmd(ns, hdr, nvme_admin_download_fw,
						parm_list_length, buffer_offset,
						buffer_id);
		if (res)
			goto out;
		res = nvme_trans_send_activate_fw_cmd(ns, hdr, buffer_id);
		break;
	case DOWNLOAD_SAVE_DEFER_ACTIVATE:
		res = nvme_trans_send_download_fw_cmd(ns, hdr, nvme_admin_download_fw,
						parm_list_length, buffer_offset,
						buffer_id);
		break;
	case ACTIVATE_DEFERRED_MICROCODE:
		res = nvme_trans_send_activate_fw_cmd(ns, hdr, buffer_id);
		break;
	default:
		res = nvme_trans_completion(hdr, SAM_STAT_CHECK_CONDITION,
					ILLEGAL_REQUEST, SCSI_ASC_INVALID_CDB,
					SCSI_ASCQ_CAUSE_NOT_REPORTABLE);
		break;
	}

 out:
	return res;
}

struct scsi_unmap_blk_desc {
	__be64	slba;
	__be32	nlb;
	u32	resv;
};

struct scsi_unmap_parm_list {
	__be16	unmap_data_len;
	__be16	unmap_blk_desc_data_len;
	u32	resv;
	struct scsi_unmap_blk_desc desc[0];
};

static int nvme_trans_unmap(struct nvme_ns *ns, struct sg_io_hdr *hdr,
							u8 *cmd)
{
	struct scsi_unmap_parm_list *plist;
	struct nvme_dsm_range *range;
	struct nvme_command c;
	int i, nvme_sc, res;
	u16 ndesc, list_len;

	list_len = get_unaligned_be16(&cmd[7]);
	if (!list_len)
		return -EINVAL;

	plist = kmalloc(list_len, GFP_KERNEL);
	if (!plist)
		return -ENOMEM;

	res = nvme_trans_copy_from_user(hdr, plist, list_len);
	if (res)
		goto out;

	ndesc = be16_to_cpu(plist->unmap_blk_desc_data_len) >> 4;
	if (!ndesc || ndesc > 256) {
		res = -EINVAL;
		goto out;
	}

	range = kcalloc(ndesc, sizeof(*range), GFP_KERNEL);
	if (!range) {
		res = -ENOMEM;
		goto out;
	}

	for (i = 0; i < ndesc; i++) {
		range[i].nlb = cpu_to_le32(be32_to_cpu(plist->desc[i].nlb));
		range[i].slba = cpu_to_le64(be64_to_cpu(plist->desc[i].slba));
		range[i].cattr = 0;
	}

	memset(&c, 0, sizeof(c));
	c.dsm.opcode = nvme_cmd_dsm;
	c.dsm.nsid = cpu_to_le32(ns->ns_id);
	c.dsm.nr = cpu_to_le32(ndesc - 1);
	c.dsm.attributes = cpu_to_le32(NVME_DSMGMT_AD);

	nvme_sc = nvme_submit_sync_cmd(ns->queue, &c, range,
			ndesc * sizeof(*range));
	res = nvme_trans_status_code(hdr, nvme_sc);

	kfree(range);
 out:
	kfree(plist);
	return res;
}

static int nvme_scsi_translate(struct nvme_ns *ns, struct sg_io_hdr *hdr)
{
	u8 cmd[16];
	int retcode;
	unsigned int opcode;

	if (hdr->cmdp == NULL)
		return -EMSGSIZE;
	if (hdr->cmd_len > sizeof(cmd))
		return -EINVAL;
	if (copy_from_user(cmd, hdr->cmdp, hdr->cmd_len))
		return -EFAULT;

	/*
	 * Prime the hdr with good status for scsi commands that don't require
	 * an nvme command for translation.
	 */
	retcode = nvme_trans_status_code(hdr, NVME_SC_SUCCESS);
	if (retcode)
		return retcode;

	opcode = cmd[0];

	switch (opcode) {
	case READ_6:
	case READ_10:
	case READ_12:
	case READ_16:
		retcode = nvme_trans_io(ns, hdr, 0, cmd);
		break;
	case WRITE_6:
	case WRITE_10:
	case WRITE_12:
	case WRITE_16:
		retcode = nvme_trans_io(ns, hdr, 1, cmd);
		break;
	case INQUIRY:
		retcode = nvme_trans_inquiry(ns, hdr, cmd);
		break;
	case LOG_SENSE:
		retcode = nvme_trans_log_sense(ns, hdr, cmd);
		break;
	case MODE_SELECT:
	case MODE_SELECT_10:
		retcode = nvme_trans_mode_select(ns, hdr, cmd);
		break;
	case MODE_SENSE:
	case MODE_SENSE_10:
		retcode = nvme_trans_mode_sense(ns, hdr, cmd);
		break;
	case READ_CAPACITY:
		retcode = nvme_trans_read_capacity(ns, hdr, cmd, 0);
		break;
	case SERVICE_ACTION_IN_16:
		switch (cmd[1]) {
		case SAI_READ_CAPACITY_16:
			retcode = nvme_trans_read_capacity(ns, hdr, cmd, 1);
			break;
		default:
			goto out;
		}
		break;
	case REPORT_LUNS:
		retcode = nvme_trans_report_luns(ns, hdr, cmd);
		break;
	case REQUEST_SENSE:
		retcode = nvme_trans_request_sense(ns, hdr, cmd);
		break;
	case SECURITY_PROTOCOL_IN:
	case SECURITY_PROTOCOL_OUT:
		retcode = nvme_trans_security_protocol(ns, hdr, cmd);
		break;
	case SYNCHRONIZE_CACHE:
		retcode = nvme_trans_synchronize_cache(ns, hdr);
		break;
	case FORMAT_UNIT:
		retcode = nvme_trans_format_unit(ns, hdr, cmd);
		break;
	case TEST_UNIT_READY:
		retcode = nvme_trans_test_unit_ready(ns, hdr, cmd);
		break;
	case WRITE_BUFFER:
		retcode = nvme_trans_write_buffer(ns, hdr, cmd);
		break;
	case UNMAP:
		retcode = nvme_trans_unmap(ns, hdr, cmd);
		break;
	default:
 out:
		retcode = nvme_trans_completion(hdr, SAM_STAT_CHECK_CONDITION,
				ILLEGAL_REQUEST, SCSI_ASC_ILLEGAL_COMMAND,
				SCSI_ASCQ_CAUSE_NOT_REPORTABLE);
		break;
	}
	return retcode;
}

int nvme_sg_io(struct nvme_ns *ns, struct sg_io_hdr __user *u_hdr)
{
	struct sg_io_hdr hdr;
	int retcode;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	if (copy_from_user(&hdr, u_hdr, sizeof(hdr)))
		return -EFAULT;
	if (hdr.interface_id != 'S')
		return -EINVAL;

	/*
	 * A positive return code means a NVMe status, which has been
	 * translated to sense data.
	 */
	retcode = nvme_scsi_translate(ns, &hdr);
	if (retcode < 0)
		return retcode;
	if (copy_to_user(u_hdr, &hdr, sizeof(sg_io_hdr_t)) > 0)
		return -EFAULT;
	return 0;
}

int nvme_sg_get_version_num(int __user *ip)
{
	return put_user(sg_version_num, ip);
}
