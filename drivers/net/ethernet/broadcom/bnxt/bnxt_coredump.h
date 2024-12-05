/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2018 Broadcom Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#ifndef BNXT_COREDUMP_H
#define BNXT_COREDUMP_H

#include <linux/utsname.h>
#include <linux/time.h>
#include <linux/rtc.h>

struct bnxt_coredump_segment_hdr {
	__u8 signature[4];
	__le32 component_id;
	__le32 segment_id;
	__le32 flags;
	__u8 low_version;
	__u8 high_version;
	__le16 function_id;
	__le32 offset;
	__le32 length;
	__le32 status;
	__le32 duration;
	__le32 data_offset;
	__le32 instance;
	__le32 rsvd[5];
};

struct bnxt_coredump_record {
	__u8 signature[4];
	__le32 flags;
	__u8 low_version;
	__u8 high_version;
	__u8 asic_state;
	__u8 rsvd0[5];
	char system_name[32];
	__le16 year;
	__le16 month;
	__le16 day;
	__le16 hour;
	__le16 minute;
	__le16 second;
	__le16 utc_bias;
	__le16 rsvd1;
	char commandline[256];
	__le32 total_segments;
	__le32 os_ver_major;
	__le32 os_ver_minor;
	__le32 rsvd2;
	char os_name[32];
	__le16 end_year;
	__le16 end_month;
	__le16 end_day;
	__le16 end_hour;
	__le16 end_minute;
	__le16 end_second;
	__le16 end_utc_bias;
	__le32 asic_id1;
	__le32 asic_id2;
	__le32 coredump_status;
	__u8 ioctl_low_version;
	__u8 ioctl_high_version;
	__le16 rsvd3[313];
};

struct bnxt_driver_segment_record {
	__le32 max_entries;
	__le32 entry_size;
	__le32 offset;
	__u8 wrapped:1;
	__u8 unused[3];
};

#define BNXT_VER_GET_COMP_ID	2
#define BNXT_DRV_COMP_ID	0xd

#define BNXT_CTX_MEM_SEG_ID_START  0x200

#define BNXT_CTX_MEM_SEG_QP	(BNXT_CTX_MEM_SEG_ID_START + BNXT_CTX_QP)
#define BNXT_CTX_MEM_SEG_SRQ	(BNXT_CTX_MEM_SEG_ID_START + BNXT_CTX_SRQ)
#define BNXT_CTX_MEM_SEG_CQ	(BNXT_CTX_MEM_SEG_ID_START + BNXT_CTX_CQ)
#define BNXT_CTX_MEM_SEG_VNIC	(BNXT_CTX_MEM_SEG_ID_START + BNXT_CTX_VNIC)
#define BNXT_CTX_MEM_SEG_STAT	(BNXT_CTX_MEM_SEG_ID_START + BNXT_CTX_STAT)
#define BNXT_CTX_MEM_SEG_STQM	(BNXT_CTX_MEM_SEG_ID_START + BNXT_CTX_STQM)
#define BNXT_CTX_MEM_SEG_FTQM	(BNXT_CTX_MEM_SEG_ID_START + BNXT_CTX_FTQM)
#define BNXT_CTX_MEM_SEG_MRAV	(BNXT_CTX_MEM_SEG_ID_START + BNXT_CTX_MRAV)
#define BNXT_CTX_MEM_SEG_TIM	(BNXT_CTX_MEM_SEG_ID_START + BNXT_CTX_TIM)

#define BNXT_CTX_MEM_SEG_SRT	0x1
#define BNXT_CTX_MEM_SEG_SRT2	0x2
#define BNXT_CTX_MEM_SEG_CRT	0x3
#define BNXT_CTX_MEM_SEG_CRT2	0x4
#define BNXT_CTX_MEM_SEG_RIGP0	0x5
#define BNXT_CTX_MEM_SEG_L2HWRM	0x6
#define BNXT_CTX_MEM_SEG_REHWRM	0x7
#define BNXT_CTX_MEM_SEG_CA0	0x8
#define BNXT_CTX_MEM_SEG_CA1	0x9
#define BNXT_CTX_MEM_SEG_CA2	0xa
#define BNXT_CTX_MEM_SEG_RIGP1	0xb

#define BNXT_CRASH_DUMP_LEN	(8 << 20)

#define COREDUMP_LIST_BUF_LEN		2048
#define COREDUMP_RETRIEVE_BUF_LEN	4096

#define BNXT_SEG_HDR_LEN	sizeof(struct bnxt_coredump_segment_hdr)
#define BNXT_SEG_RCD_LEN	sizeof(struct bnxt_driver_segment_record)

struct bnxt_coredump {
	void		*data;
	int		data_size;
	u16		total_segs;
};

#define BNXT_COREDUMP_BUF_LEN(len) ((len) - sizeof(struct bnxt_coredump_record))

struct bnxt_hwrm_dbg_dma_info {
	void *dest_buf;
	int dest_buf_size;
	u16 dma_len;
	u16 seq_off;
	u16 data_len_off;
	u16 segs;
	u32 seg_start;
	u32 buf_len;
};

struct hwrm_dbg_cmn_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le64 host_dest_addr;
	__le32 host_buf_len;
};

struct hwrm_dbg_cmn_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	u8 flags;
	#define HWRM_DBG_CMN_FLAGS_MORE	1
};

#define BNXT_DBG_FL_CR_DUMP_SIZE_SOC	\
	DBG_QCFG_REQ_FLAGS_CRASHDUMP_SIZE_FOR_DEST_DEST_SOC_DDR
#define BNXT_DBG_FL_CR_DUMP_SIZE_HOST	\
	DBG_QCFG_REQ_FLAGS_CRASHDUMP_SIZE_FOR_DEST_DEST_HOST_DDR
#define BNXT_DBG_CR_DUMP_MDM_CFG_DDR	\
	DBG_CRASHDUMP_MEDIUM_CFG_REQ_TYPE_DDR

void bnxt_fill_coredump_seg_hdr(struct bnxt *bp,
				struct bnxt_coredump_segment_hdr *seg_hdr,
				struct coredump_segment_record *seg_rec,
				u32 seg_len, int status, u32 duration,
				u32 instance, u32 comp_id, u32 seg_id);
int bnxt_get_coredump(struct bnxt *bp, u16 dump_type, void *buf, u32 *dump_len);
int bnxt_hwrm_get_dump_len(struct bnxt *bp, u16 dump_type, u32 *dump_len);
u32 bnxt_get_coredump_length(struct bnxt *bp, u16 dump_type);

#endif
