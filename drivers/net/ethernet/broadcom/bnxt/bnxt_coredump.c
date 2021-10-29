/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2021 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include "bnxt_hsi.h"
#include "bnxt.h"
#include "bnxt_hwrm.h"
#include "bnxt_coredump.h"

static int bnxt_hwrm_dbg_dma_data(struct bnxt *bp, void *msg,
				  struct bnxt_hwrm_dbg_dma_info *info)
{
	struct hwrm_dbg_cmn_input *cmn_req = msg;
	__le16 *seq_ptr = msg + info->seq_off;
	struct hwrm_dbg_cmn_output *cmn_resp;
	u16 seq = 0, len, segs_off;
	dma_addr_t dma_handle;
	void *dma_buf, *resp;
	int rc, off = 0;

	dma_buf = hwrm_req_dma_slice(bp, msg, info->dma_len, &dma_handle);
	if (!dma_buf) {
		hwrm_req_drop(bp, msg);
		return -ENOMEM;
	}

	hwrm_req_timeout(bp, msg, HWRM_COREDUMP_TIMEOUT);
	cmn_resp = hwrm_req_hold(bp, msg);
	resp = cmn_resp;

	segs_off = offsetof(struct hwrm_dbg_coredump_list_output,
			    total_segments);
	cmn_req->host_dest_addr = cpu_to_le64(dma_handle);
	cmn_req->host_buf_len = cpu_to_le32(info->dma_len);
	while (1) {
		*seq_ptr = cpu_to_le16(seq);
		rc = hwrm_req_send(bp, msg);
		if (rc)
			break;

		len = le16_to_cpu(*((__le16 *)(resp + info->data_len_off)));
		if (!seq &&
		    cmn_req->req_type == cpu_to_le16(HWRM_DBG_COREDUMP_LIST)) {
			info->segs = le16_to_cpu(*((__le16 *)(resp +
							      segs_off)));
			if (!info->segs) {
				rc = -EIO;
				break;
			}

			info->dest_buf_size = info->segs *
					sizeof(struct coredump_segment_record);
			info->dest_buf = kmalloc(info->dest_buf_size,
						 GFP_KERNEL);
			if (!info->dest_buf) {
				rc = -ENOMEM;
				break;
			}
		}

		if (info->dest_buf) {
			if ((info->seg_start + off + len) <=
			    BNXT_COREDUMP_BUF_LEN(info->buf_len)) {
				memcpy(info->dest_buf + off, dma_buf, len);
			} else {
				rc = -ENOBUFS;
				break;
			}
		}

		if (cmn_req->req_type ==
				cpu_to_le16(HWRM_DBG_COREDUMP_RETRIEVE))
			info->dest_buf_size += len;

		if (!(cmn_resp->flags & HWRM_DBG_CMN_FLAGS_MORE))
			break;

		seq++;
		off += len;
	}
	hwrm_req_drop(bp, msg);
	return rc;
}

static int bnxt_hwrm_dbg_coredump_list(struct bnxt *bp,
				       struct bnxt_coredump *coredump)
{
	struct bnxt_hwrm_dbg_dma_info info = {NULL};
	struct hwrm_dbg_coredump_list_input *req;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_DBG_COREDUMP_LIST);
	if (rc)
		return rc;

	info.dma_len = COREDUMP_LIST_BUF_LEN;
	info.seq_off = offsetof(struct hwrm_dbg_coredump_list_input, seq_no);
	info.data_len_off = offsetof(struct hwrm_dbg_coredump_list_output,
				     data_len);

	rc = bnxt_hwrm_dbg_dma_data(bp, req, &info);
	if (!rc) {
		coredump->data = info.dest_buf;
		coredump->data_size = info.dest_buf_size;
		coredump->total_segs = info.segs;
	}
	return rc;
}

static int bnxt_hwrm_dbg_coredump_initiate(struct bnxt *bp, u16 component_id,
					   u16 segment_id)
{
	struct hwrm_dbg_coredump_initiate_input *req;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_DBG_COREDUMP_INITIATE);
	if (rc)
		return rc;

	hwrm_req_timeout(bp, req, HWRM_COREDUMP_TIMEOUT);
	req->component_id = cpu_to_le16(component_id);
	req->segment_id = cpu_to_le16(segment_id);

	return hwrm_req_send(bp, req);
}

static int bnxt_hwrm_dbg_coredump_retrieve(struct bnxt *bp, u16 component_id,
					   u16 segment_id, u32 *seg_len,
					   void *buf, u32 buf_len, u32 offset)
{
	struct hwrm_dbg_coredump_retrieve_input *req;
	struct bnxt_hwrm_dbg_dma_info info = {NULL};
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_DBG_COREDUMP_RETRIEVE);
	if (rc)
		return rc;

	req->component_id = cpu_to_le16(component_id);
	req->segment_id = cpu_to_le16(segment_id);

	info.dma_len = COREDUMP_RETRIEVE_BUF_LEN;
	info.seq_off = offsetof(struct hwrm_dbg_coredump_retrieve_input,
				seq_no);
	info.data_len_off = offsetof(struct hwrm_dbg_coredump_retrieve_output,
				     data_len);
	if (buf) {
		info.dest_buf = buf + offset;
		info.buf_len = buf_len;
		info.seg_start = offset;
	}

	rc = bnxt_hwrm_dbg_dma_data(bp, req, &info);
	if (!rc)
		*seg_len = info.dest_buf_size;

	return rc;
}

static void
bnxt_fill_coredump_seg_hdr(struct bnxt *bp,
			   struct bnxt_coredump_segment_hdr *seg_hdr,
			   struct coredump_segment_record *seg_rec, u32 seg_len,
			   int status, u32 duration, u32 instance)
{
	memset(seg_hdr, 0, sizeof(*seg_hdr));
	memcpy(seg_hdr->signature, "sEgM", 4);
	if (seg_rec) {
		seg_hdr->component_id = (__force __le32)seg_rec->component_id;
		seg_hdr->segment_id = (__force __le32)seg_rec->segment_id;
		seg_hdr->low_version = seg_rec->version_low;
		seg_hdr->high_version = seg_rec->version_hi;
	} else {
		/* For hwrm_ver_get response Component id = 2
		 * and Segment id = 0
		 */
		seg_hdr->component_id = cpu_to_le32(2);
		seg_hdr->segment_id = 0;
	}
	seg_hdr->function_id = cpu_to_le16(bp->pdev->devfn);
	seg_hdr->length = cpu_to_le32(seg_len);
	seg_hdr->status = cpu_to_le32(status);
	seg_hdr->duration = cpu_to_le32(duration);
	seg_hdr->data_offset = cpu_to_le32(sizeof(*seg_hdr));
	seg_hdr->instance = cpu_to_le32(instance);
}

static void
bnxt_fill_coredump_record(struct bnxt *bp, struct bnxt_coredump_record *record,
			  time64_t start, s16 start_utc, u16 total_segs,
			  int status)
{
	time64_t end = ktime_get_real_seconds();
	u32 os_ver_major = 0, os_ver_minor = 0;
	struct tm tm;

	time64_to_tm(start, 0, &tm);
	memset(record, 0, sizeof(*record));
	memcpy(record->signature, "cOrE", 4);
	record->flags = 0;
	record->low_version = 0;
	record->high_version = 1;
	record->asic_state = 0;
	strscpy(record->system_name, utsname()->nodename,
		sizeof(record->system_name));
	record->year = cpu_to_le16(tm.tm_year + 1900);
	record->month = cpu_to_le16(tm.tm_mon + 1);
	record->day = cpu_to_le16(tm.tm_mday);
	record->hour = cpu_to_le16(tm.tm_hour);
	record->minute = cpu_to_le16(tm.tm_min);
	record->second = cpu_to_le16(tm.tm_sec);
	record->utc_bias = cpu_to_le16(start_utc);
	strcpy(record->commandline, "ethtool -w");
	record->total_segments = cpu_to_le32(total_segs);

	if (sscanf(utsname()->release, "%u.%u", &os_ver_major, &os_ver_minor) != 2)
		netdev_warn(bp->dev, "Unknown OS release in coredump\n");
	record->os_ver_major = cpu_to_le32(os_ver_major);
	record->os_ver_minor = cpu_to_le32(os_ver_minor);

	strscpy(record->os_name, utsname()->sysname, sizeof(record->os_name));
	time64_to_tm(end, 0, &tm);
	record->end_year = cpu_to_le16(tm.tm_year + 1900);
	record->end_month = cpu_to_le16(tm.tm_mon + 1);
	record->end_day = cpu_to_le16(tm.tm_mday);
	record->end_hour = cpu_to_le16(tm.tm_hour);
	record->end_minute = cpu_to_le16(tm.tm_min);
	record->end_second = cpu_to_le16(tm.tm_sec);
	record->end_utc_bias = cpu_to_le16(sys_tz.tz_minuteswest * 60);
	record->asic_id1 = cpu_to_le32(bp->chip_num << 16 |
				       bp->ver_resp.chip_rev << 8 |
				       bp->ver_resp.chip_metal);
	record->asic_id2 = 0;
	record->coredump_status = cpu_to_le32(status);
	record->ioctl_low_version = 0;
	record->ioctl_high_version = 0;
}

static int __bnxt_get_coredump(struct bnxt *bp, void *buf, u32 *dump_len)
{
	u32 ver_get_resp_len = sizeof(struct hwrm_ver_get_output);
	u32 offset = 0, seg_hdr_len, seg_record_len, buf_len = 0;
	struct coredump_segment_record *seg_record = NULL;
	struct bnxt_coredump_segment_hdr seg_hdr;
	struct bnxt_coredump coredump = {NULL};
	time64_t start_time;
	u16 start_utc;
	int rc = 0, i;

	if (buf)
		buf_len = *dump_len;

	start_time = ktime_get_real_seconds();
	start_utc = sys_tz.tz_minuteswest * 60;
	seg_hdr_len = sizeof(seg_hdr);

	/* First segment should be hwrm_ver_get response */
	*dump_len = seg_hdr_len + ver_get_resp_len;
	if (buf) {
		bnxt_fill_coredump_seg_hdr(bp, &seg_hdr, NULL, ver_get_resp_len,
					   0, 0, 0);
		memcpy(buf + offset, &seg_hdr, seg_hdr_len);
		offset += seg_hdr_len;
		memcpy(buf + offset, &bp->ver_resp, ver_get_resp_len);
		offset += ver_get_resp_len;
	}

	rc = bnxt_hwrm_dbg_coredump_list(bp, &coredump);
	if (rc) {
		netdev_err(bp->dev, "Failed to get coredump segment list\n");
		goto err;
	}

	*dump_len += seg_hdr_len * coredump.total_segs;

	seg_record = (struct coredump_segment_record *)coredump.data;
	seg_record_len = sizeof(*seg_record);

	for (i = 0; i < coredump.total_segs; i++) {
		u16 comp_id = le16_to_cpu(seg_record->component_id);
		u16 seg_id = le16_to_cpu(seg_record->segment_id);
		u32 duration = 0, seg_len = 0;
		unsigned long start, end;

		if (buf && ((offset + seg_hdr_len) >
			    BNXT_COREDUMP_BUF_LEN(buf_len))) {
			rc = -ENOBUFS;
			goto err;
		}

		start = jiffies;

		rc = bnxt_hwrm_dbg_coredump_initiate(bp, comp_id, seg_id);
		if (rc) {
			netdev_err(bp->dev,
				   "Failed to initiate coredump for seg = %d\n",
				   seg_record->segment_id);
			goto next_seg;
		}

		/* Write segment data into the buffer */
		rc = bnxt_hwrm_dbg_coredump_retrieve(bp, comp_id, seg_id,
						     &seg_len, buf, buf_len,
						     offset + seg_hdr_len);
		if (rc && rc == -ENOBUFS)
			goto err;
		else if (rc)
			netdev_err(bp->dev,
				   "Failed to retrieve coredump for seg = %d\n",
				   seg_record->segment_id);

next_seg:
		end = jiffies;
		duration = jiffies_to_msecs(end - start);
		bnxt_fill_coredump_seg_hdr(bp, &seg_hdr, seg_record, seg_len,
					   rc, duration, 0);

		if (buf) {
			/* Write segment header into the buffer */
			memcpy(buf + offset, &seg_hdr, seg_hdr_len);
			offset += seg_hdr_len + seg_len;
		}

		*dump_len += seg_len;
		seg_record =
			(struct coredump_segment_record *)((u8 *)seg_record +
							   seg_record_len);
	}

err:
	if (buf)
		bnxt_fill_coredump_record(bp, buf + offset, start_time,
					  start_utc, coredump.total_segs + 1,
					  rc);
	kfree(coredump.data);
	*dump_len += sizeof(struct bnxt_coredump_record);
	if (rc == -ENOBUFS)
		netdev_err(bp->dev, "Firmware returned large coredump buffer\n");
	return rc;
}

int bnxt_get_coredump(struct bnxt *bp, u16 dump_type, void *buf, u32 *dump_len)
{
	if (dump_type == BNXT_DUMP_CRASH) {
#ifdef CONFIG_TEE_BNXT_FW
		return tee_bnxt_copy_coredump(buf, 0, *dump_len);
#else
		return -EOPNOTSUPP;
#endif
	} else {
		return __bnxt_get_coredump(bp, buf, dump_len);
	}
}

u32 bnxt_get_coredump_length(struct bnxt *bp, u16 dump_type)
{
	u32 len = 0;

	if (dump_type == BNXT_DUMP_CRASH)
		len = BNXT_CRASH_DUMP_LEN;
	else
		__bnxt_get_coredump(bp, NULL, &len);
	return len;
}
